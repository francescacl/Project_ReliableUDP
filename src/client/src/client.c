#include "client.h"



void error(const char *msg) {
  perror(msg);
  exit(1);
}



void check_args(int argc, char *argv[]) {
  if (argc != 4) { // check args num
    printf("Wrong number of arguments\n");
    fprintf(stderr, "usage: client <server IP address> <packet loss probability> <timeout [ms]>\n");
    exit(1);
  }

  server_ip = argv[1];

  loss_prob = atof(argv[2]);
  if (loss_prob < 0 || loss_prob > 1) {
    fprintf(stderr, "Loss probability must be within 0 and 1\n");
    exit(1);
  }
  int temp = atoi(argv[3]);
  if (temp < 0) {
    fprintf(stderr, "Timeout cannot be less than 0\n");
    exit(1);
  } else {
    // convert timeout to seconds and microseconds
    timeout_s = temp / 1000;
    timeout_us = (temp % 1000) * 1000;
  }

  char loss_perc[10];
  snprintf(loss_perc, sizeof(loss_perc), "%.0f%%", loss_prob * 100);
  printf("Chosen arguments:\n  - Server ip: %s\n  - Loss probability: %s\n  - Timeout: %d [ms]\n\n\n", server_ip, loss_perc, temp);

  return;
}



size_t file_size(char *filename) {
  // Return file size [bytes]
  FILE *file = fopen(filename, "rb");
  size_t fsize = -1;

  if (file != NULL) {
    if (fseek(file, 0, SEEK_END) == 0) { // Shift the file pointer to the end of the file
      fsize = ftell(file); // Current position corresponds to the size of the file
    }
  fclose(file);
  }

  return fsize;
}



uint32_t calculate_checksum(udp_packet_t *packet) {
    uint32_t checksum = 0;
    uint32_t sum = 0;
    
     // Sum header fields
    sum += (packet->seq_num >> 16) & 0xFFFF;
    sum += packet->seq_num & 0xFFFF;
    sum += (packet->ack_num >> 16) & 0xFFFF;
    sum += packet->ack_num & 0xFFFF;

    // Sum data
    for (int i = 0; i < packet->data_size; i += 2) {
        uint16_t word = packet->data[i];
        if (i + 1 < packet->data_size) {
            word |= (packet->data[i + 1] << 8);
        }
        sum += word;
    }

    // Add returned carry to the sum
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // One's complement of the sum
    checksum = ~sum;

    return checksum;
}



char* file_path(char *fpath, char *fname) {
  // Return file_path
  size_t len1 = strlen(fpath);
  size_t len2 = strlen(fname);

  char *path = (char *)malloc((len1 + len2 + 1) * sizeof(char));
  strcpy(path, fpath);
  strcat(path, fname);

  return path;
}



void set_timeout(int sock, int timeout_s, int timeout_us) {
  struct timeval tv;
  tv.tv_sec = timeout_s;
  tv.tv_usec = timeout_us;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}



uint32_t num_packets(uint32_t size) {
  return (uint32_t)ceil((double)size / (double)MAXLINE);
}



void send_ack(int sockfd, struct sockaddr_in* address, uint32_t ack_num) {
  udp_packet_t ack_packet;
  ack_packet.seq_num = 0;
  ack_packet.ack_num = ack_num;
  ack_packet.data_size = 0;
  ack_packet.checksum = calculate_checksum(&ack_packet);
  if (sendto(sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr*) address, (socklen_t) sizeof(*address)) < 0) {
    error("Error in send_ack");
  }
}



void ls(char *list_command){

  sprintf(list_command, "ls %s", DEST_PATH);
     
  FILE *pipe = popen(list_command, "r");
  if (pipe == NULL) {
    error("Error in opening the pipe");
  }
  // Read the output of the command and save it in a string
  size_t bytes_read = fread(list_command, 1, MAXLINE, pipe);
  list_command[bytes_read] = '\0';

  // Close the pipe
  if (pclose(pipe) == -1) {
    error("Error in closing the pipe");
  }

}



void create_conn(char *ip_address, uint16_t port) {

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { // create the socket
    error("Error in socket");
  }
  
  memset(&servaddr, 0, sizeof(servaddr)); 
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = port;  // assign the port number
  // assign the server address by taking it from the command line. The address is a string to be converted to an integer according to network byte order.
  if (inet_pton(AF_INET, ip_address, &servaddr.sin_addr) <= 0) {
    fprintf(stderr, "Error in inet_pton for %s", ip_address);
    exit(1);
  }

  return;
}



int wait_recv(char *buff, long size, int sockfd, struct sockaddr_in *address, socklen_t *addr_length) { 
  int totalReceived = 0;
  if (sockfd > 0) {
    int received = 0;
    while(size > 0) {
      uint8_t buffer[MAX_SIZE_STRUCT] = {0};
      errno = 0;
      if ((received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)address, addr_length)) < 0) {
        if (errno == EINTR || errno == EAGAIN) {
          send_ack(sockfd, &servaddr, seq_num_recv-1);
          continue;
        }
        fprintf(stderr, "Error udp wait_recv: %d\n", errno);
        return -1;
      }

      udp_packet_t *temp_packet = (udp_packet_t*) buffer;
      udp_packet_t *packet = malloc(offsetof(struct temp, data) + temp_packet->data_size);

      if (packet == NULL) {
        error("malloc");
      }

      // Copy data to the new allocated buffer
      memcpy(packet, buffer, offsetof(struct temp, data) + temp_packet->data_size);

      double random_number = (double)rand() / RAND_MAX;
      if (random_number < loss_prob) {
        continue;
      }

      if (packet->checksum == calculate_checksum(packet) && packet->seq_num == seq_num_recv) {
        send_ack(sockfd, &servaddr, seq_num_recv);
        seq_num_recv += 1;
      } else {
        send_ack(sockfd, &servaddr, seq_num_recv-1);
        continue;
      }

      memcpy(buff + totalReceived, packet->data, packet->data_size);
      totalReceived += packet->data_size;
      size -= packet->data_size;

      free(packet);
    }

    return totalReceived;
  }
  return -1;
}



int recv_rel(int sock, char *buff, size_t dim, bool size_rcv, struct sockaddr_in *address, socklen_t *addr_length) {
  int k;
  if(!size_rcv) {
    char buffer[MAX_SIZE_STRUCT] = {0};
    set_timeout(sock, timeout_s, timeout_us);
    while(1) {
      errno = 0;
      if (recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)address, addr_length) < 0) {
        if (errno == EINTR || errno == EAGAIN /* timeout */) {
          fflush(stdout);
          continue;
        }
        error("Error in recvfrom");
        return -2;
      }

      udp_packet_t *temp_packet = (udp_packet_t*) buffer;
      udp_packet_t *packet = malloc(offsetof(struct temp, data) + temp_packet->data_size);

      if (packet == NULL) {
        error("Malloc");
      }

      // Copy data to the new allocated buffer
      memcpy(packet, buffer, offsetof(struct temp, data) + temp_packet->data_size);

      double random_number = (double)rand() / RAND_MAX;
      if (random_number < loss_prob) {
        continue;
      }

      k = packet->data_size;
      memcpy(buff, packet->data, k);

      if (address == NULL) {
        servaddr.sin_port = htons(strtoul(packet->data, NULL, 10));
      }

      if (packet->checksum == calculate_checksum(packet) && packet->seq_num == seq_num_recv) {
        send_ack(sock, &servaddr, seq_num_recv);
        seq_num_recv += 1;
        break;
      } else {
        send_ack(sock, &servaddr, seq_num_recv-1);
      }
    }
  } else {
    k = wait_recv(buff, dim, sock, address, addr_length);
  }
  set_timeout(sock, 1000000, 0);
  return k;
}



int bytes_read_funct(char **data, FILE* file, udp_packet_t** packet) {
  char buff[MAXLINE] = {0};
  uint32_t bytes_read;
  bool is_file = file != NULL;

  if (is_file) {
    bytes_read = fread(buff, 1, MAXLINE, file);
  } else {
    bytes_read = strlen(*data);
    if (bytes_read > MAXLINE) {
      bytes_read = MAXLINE;
    }
  }

  // prepare packet to be sent
  *packet = malloc(offsetof(struct temp, data) + bytes_read);
  if (*packet == NULL) {
    error("Malloc");
  }

  (*packet)->seq_num = seq_num_send;
  (*packet)->ack_num = seq_num_recv-1;
  (*packet)->data_size = bytes_read;
  for(uint32_t i = 0; i < bytes_read; i++) {
    if (is_file) {
      (*packet)->data[i] = buff[i];
    } else {
      (*packet)->data[i] = (*data)[i];
    }
  }
  (*packet)->checksum = calculate_checksum(*packet);
  if (!is_file) {
    *data += bytes_read;
  }

  return bytes_read;
}



void send_rel_single(int fd, struct sockaddr_in send_addr, char *data, bool last) {
  udp_packet_t* packet;
  uint32_t counter = 0;
  int bytes_read = bytes_read_funct(&data, NULL, &packet);
  if (bytes_read < 0) {
    error("Error in reading data");
  }

  set_timeout(fd, timeout_s, timeout_us);

  while(1) {
    if (sendto(fd, packet, offsetof(struct temp, data) + packet->data_size, 0, (struct sockaddr *)&send_addr, sizeof(send_addr)) < 0)
      error("Error in sendto");

    // wait for ack
    udp_packet_t ack_packet;
    while (1) {
      errno = 0;
      if (recvfrom(fd, &ack_packet, sizeof(ack_packet), 0, NULL, NULL) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          counter++;
          if (last && counter == 10) return;
          break;
        }
        error("Error in recvfrom");
      }

      double random_number = (double)rand() / RAND_MAX;
      if (random_number < loss_prob) {
        continue;
      }
      break;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      continue;
    }
    if (ack_packet.ack_num == packet->seq_num) {
      break;
    }
  }
  seq_num_send += 1;

  set_timeout(fd, 1000000, 0);

  free(packet);
}



void *send_rel_sender_thread(void *arg) {
    // parse arg
    thread_data_t* thread_data;
    thread_data = (thread_data_t *) arg;
    int sockfd = thread_data->sockfd;
    struct sockaddr_in server_addr = thread_data->addr;
    FILE* file = thread_data->file;
    uint32_t *base = thread_data->base;
    pthread_mutex_t *lock = thread_data->lock;
    pthread_cond_t *cond = thread_data->cond;
    atomic_bool *end_thread = thread_data->end_thread;
    struct timeval *timer_start = thread_data->timer_start;
    const uint32_t num_packets = thread_data->num_packets;

    uint32_t next_seq_num = *base;
    uint32_t next_seq_num_start = next_seq_num;
    uint32_t ack_rtt = next_seq_num_start;
    atomic_int *new_acks = thread_data->new_acks;
    atomic_bool *duplicate_acks = thread_data->duplicate_acks;
    long tid = pthread_self();

    udp_packet_t** packets = (udp_packet_t**) malloc(num_packets * sizeof(udp_packet_t*));
    memset(packets, 0, num_packets * sizeof(udp_packet_t*));
    for (uint32_t i = 0; i < num_packets; i++) {
        bytes_read_funct(NULL, file, &packets[i]);
    }

    uint32_t cwnd = 1; // Congestion window starts from 1
    uint32_t ssthresh = WINDOW_SIZE; // Set a high initial threshold
    
    while (!atomic_load(end_thread)) {
        pthread_mutex_lock(lock);
        
        // Send packets within the current cwnd
        while (next_seq_num < (*base) + cwnd && next_seq_num < num_packets + next_seq_num_start) {
          if (*base == num_packets + next_seq_num_start + 1) {
                break;
            }

            uint16_t data_size = packets[next_seq_num - next_seq_num_start]->data_size;
            udp_packet_t* packet = (udp_packet_t*) malloc(offsetof(struct temp, data) + data_size);
            if (packet == NULL) {
                printf("[%lu] Errore di allocazione della memoria per packet\n", tid);
                break;
            }
            size_t offset = next_seq_num - next_seq_num_start;
            memcpy(packet, packets[offset], offsetof(udp_packet_t, data) + data_size);

            packet->seq_num = next_seq_num;
            packet->checksum = calculate_checksum(packet);
            

           
            sendto(sockfd, packet, offsetof(struct temp, data) + packet->data_size, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));

            if (next_seq_num == *base) {
                gettimeofday(timer_start, NULL);
            }
            next_seq_num++;

            free(packet);
        }

        if (*base == num_packets + next_seq_num_start + 1) {
            pthread_mutex_unlock(lock);
            break; // break if all packets have been sent
        }

        pthread_cond_wait(cond, lock);

        // Handle timeout
        struct timeval timer_now;
        gettimeofday(&timer_now, NULL);
        double diff_seconds = difftime(timer_now.tv_sec, timer_start->tv_sec);
        double diff_microseconds = (timer_now.tv_usec - timer_start->tv_usec) / 1e6;
        double diff = diff_seconds + diff_microseconds;
        if (diff > ((double) timeout_s + (double) timeout_us / 1000000)) {
            memset(thread_data->acked + *base, 0, (cwnd) * sizeof(bool));
            next_seq_num = *base;
            ssthresh = (cwnd / 2 > WINDOW_SIZE/2) ? (cwnd / 2) : WINDOW_SIZE/2;
            cwnd = 1; // Reset cwnd on timeout
            atomic_store(duplicate_acks, 0);
            atomic_store(new_acks, 0);
        }

        // Process received ACKs and update base and cwnd
        for (uint32_t i = (*base) - next_seq_num_start -1; i < num_packets; i++) {
            if (thread_data->acked[i] && !atomic_load(duplicate_acks)) { 
                // Slow start or congestion avoidance
                if (cwnd < ssthresh) {
                    cwnd += atomic_load(new_acks); // Exponential growth
                    atomic_store(new_acks, 0);
                    } else if ((thread_data->acked[i] && i == ack_rtt)) {
                    ack_rtt = next_seq_num;
                    cwnd++; // Linear growth
                    atomic_store(new_acks, 0);
                }
                atomic_store(duplicate_acks, false); // Reset duplicate ACKs count
            } else if (atomic_load(duplicate_acks)){ // Fast recovery
                atomic_store(duplicate_acks, false);
                atomic_store(new_acks, 0);
                ssthresh = (cwnd / 2 > WINDOW_SIZE/2) ? (cwnd / 2) : WINDOW_SIZE/2;
                cwnd = ssthresh + 3;
                next_seq_num = *base;
                break;
            }
        }
        pthread_mutex_unlock(lock);
    }

    // Deallocate memory
    for (uint32_t i = 0; i < num_packets; i++) {
        free(packets[i]);
    }
    free(packets);

    pthread_exit(NULL);
}



void *send_rel_receiver_thread(void *arg) {
  // parse arg
  thread_data_t* thread_data;
  thread_data = (thread_data_t *) arg;
  int sockfd = thread_data->sockfd;
  uint32_t num_packets = thread_data->num_packets;
  uint32_t *base = thread_data->base;
  pthread_mutex_t *lock = thread_data->lock;
  pthread_cond_t *cond = thread_data->cond;
  atomic_bool *end_thread = thread_data->end_thread;
  struct timeval *timer_start = thread_data->timer_start;
  atomic_int *new_acks = thread_data->new_acks;
  atomic_bool *duplicate_acks = thread_data->duplicate_acks;

  uint32_t seq_num_start = *base;
  int dupl_a = 0;
  
  set_timeout(sockfd, timeout_s, timeout_us);
  while (!atomic_load(end_thread)) {
    udp_packet_t ack_packet;
    if (recvfrom(sockfd, &ack_packet, sizeof(ack_packet), 0, NULL, NULL) < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        pthread_cond_signal(cond);
        continue;
      }
      error("Error in recvfrom");
    }

    double random_number = (double)rand() / RAND_MAX;
    if (random_number < loss_prob) {
      continue;
    }

    uint32_t ack_num = ack_packet.ack_num;
    
    if (ack_num == num_packets + seq_num_start - 1) {
      atomic_store(end_thread, true);
      break;
    }
    pthread_mutex_lock(lock);
    if (ack_num >= (*base)) {
      dupl_a = 0;
      int count = 0;
      for (uint32_t i = *base; i <= ack_num && i < num_packets + seq_num_start; i++) {
        int new_a = atomic_load(new_acks) + 1;
        atomic_store(new_acks, new_a);
        if (i == ack_num) {
          thread_data->acked[ack_num - seq_num_start] = true;
        }
        count++;
      }

      gettimeofday(timer_start, NULL);
      (*base) += count;
      pthread_cond_signal(cond);
    } else {
      if (thread_data->acked[ack_num - seq_num_start]) {
        dupl_a += 1;
        if (dupl_a >= 3) {
          atomic_store(duplicate_acks, true);
          pthread_mutex_unlock(lock);
          pthread_cond_signal(cond);
          dupl_a = 0;
          continue;
        }
      } else {
        thread_data->acked[ack_num - seq_num_start] = true;
      }
    }
    pthread_mutex_unlock(lock);
  }

  set_timeout(sockfd, 1000000, 0);

  pthread_cond_signal(cond);

  pthread_exit(NULL);
}




void send_rel(int fd, struct sockaddr_in send_addr, FILE* file, size_t size_file) {

  uint32_t size = htonl(size_file);
  char size_str[11];
  size_str[0] = '\0';
  snprintf(size_str, sizeof(size_str), "%u", size);
  send_rel_single(fd, send_addr, size_str, false);

  pthread_mutex_t lock;
  pthread_cond_t cond;
  pthread_mutex_init(&lock, NULL);
  pthread_cond_init(&cond, NULL);
  struct timeval timer_start;

  atomic_bool end_thread;
  end_thread = ATOMIC_VAR_INIT(false);
  atomic_store(&end_thread, false);

  atomic_int new_acks;
  new_acks = ATOMIC_VAR_INIT(0);
  atomic_store(&new_acks, 0);

  atomic_bool duplicate_acks;
  duplicate_acks = ATOMIC_VAR_INIT(false);
  atomic_store(&duplicate_acks, false);

  uint32_t base = seq_num_send;
  uint32_t num_pack = num_packets(size_file);
  
  thread_data_t thread_data;
  thread_data.sockfd = fd;
  thread_data.addr = send_addr;
  thread_data.num_packets = num_pack;
  thread_data.file = file;
  thread_data.base = &base;
  thread_data.lock = &lock;
  thread_data.cond = &cond;
  thread_data.end_thread = &end_thread;
  thread_data.new_acks = &new_acks;
  thread_data.acked = calloc(num_pack, sizeof(bool));
  thread_data.timer_start = &timer_start;
  thread_data.duplicate_acks = &duplicate_acks;
  pthread_t sender, receiver;
  pthread_create(&sender, NULL, send_rel_sender_thread, &thread_data);
  pthread_create(&receiver, NULL, send_rel_receiver_thread, &thread_data);

  pthread_join(sender, NULL);
  pthread_join(receiver, NULL);

  free(thread_data.acked);

  pthread_mutex_destroy(&lock);
  pthread_cond_destroy(&cond);

  seq_num_send += thread_data.num_packets;
}



int wait_for_input(int count, char* buff_in) {
  int buff_in_int;

  while(1) { // input choice from user
    __fpurge(stdin);
    
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    FD_SET(sockfd, &readfds);

    int maxfd = (STDIN_FILENO > sockfd) ? STDIN_FILENO : sockfd;

    // Wait for one of the file descriptors to be ready for reading
    int activity = select(maxfd + 1, &readfds, NULL, NULL, NULL);

    if (activity < 0 && errno != EINTR) {
      error("Error in select function");
    }

    // Check if there is input from stdin
    if (FD_ISSET(STDIN_FILENO, &readfds)) {
      buff_in[0] = getchar();
      buff_in[1] = '\0';
      buff_in_int = atoi(buff_in)-1;
      if (buff_in_int < 0 || buff_in_int >= count) {
        printf("The number is not valid. Retry:\n");
        fflush(stdout);
        continue;
      }
      break;
    }

    // Check if there is input from the socket
    if (FD_ISSET(sockfd, &readfds)) {
      char buffer[MAX_SIZE_STRUCT] = {0};
      if (recvfrom(sockfd, buffer, sizeof(buffer), 0, NULL, NULL) < 0) {
        if (errno == EINTR || errno == EAGAIN) {
          continue;
        }
        error("Error in recvfrom");
      }

      udp_packet_t *temp_packet = (udp_packet_t*) buffer;
      udp_packet_t *packet = malloc(offsetof(struct temp, data) + temp_packet->data_size);

      if (packet == NULL) {
        error("Malloc");
      }

      // Copy data to the new allocated buffer
      memcpy(packet, buffer, offsetof(struct temp, data) + temp_packet->data_size);

      if (packet->checksum == calculate_checksum(packet) && packet->seq_num < seq_num_recv) {
        send_ack(sockfd, &servaddr, seq_num_recv-1);
      }
    }
  }
  return buff_in_int;
}



void list_option(socklen_t servaddr_len, int option, char **name) {
  
  char list_str[MAXLINE] = {0};
  recv_rel(sockfd, list_str, MAXLINE, false, &servaddr, &servaddr_len);
  if (!strcmp(list_str, "No files available")) {
      printf("%s\n", list_str);
      if (option == 1) {
        *name = malloc(sizeof(char));
        (*name)[0] = '\0';
      }
      return;
    }

  // Save the length of individual strings
  size_t l = strlen(list_str);
  size_t cumulative_index[MAXVECT] = {0};
  int count = 1;
  for (size_t i = 0; i < l-1; i++) {
    if (list_str[i] == '\n') {
      cumulative_index[count] = i+1;
      count++;
    }
  }
  // now I have cumulative_index which has the start indexes of each string inside list_str
  if (option == 0) {
    printf("Files available in the server: \n%s\n", list_str);
    fflush(stdout);
  } else {
    printf("Choose one of the following files:\n");
    size_t c_index = 0;
    size_t c = 0;
    printf("\t%lu. ", c+1);
    while (list_str[c_index] != '\0') {
      if (list_str[c_index] == '\n') {
        if (list_str[c_index+1] != '\0') {
          c++;
          printf("\n\t%lu. ", c+1);
        }
      } else {
        printf("%c", list_str[c_index]);
      }
      c_index++;
    }
    printf("\nType the corresponding number:\n");
    fflush(stdout);

    char buff_in;
    int buff_in_int = wait_for_input(count, &buff_in);
    
    size_t curr_index = cumulative_index[buff_in_int];
    size_t index = curr_index;
    size_t i = 0;
    while (list_str[curr_index] != '\n') {
      curr_index ++;
      i++;
    }
    *name = (char*)malloc((i+1)*sizeof(char));
    if (*name == NULL) {
      error("Error in memory allocation\n");
    }
    strncpy(*name, list_str + index, i);
    (*name)[i] = '\0';
  }
}



void get_option(socklen_t servaddr_len) {

  char* name;
  list_option(servaddr_len, 1, &name); // 1: get option

  if (name[0] == '\0') {
    return;
  }

  // send file name
  send_rel_single(sockfd, servaddr, name, false);
  // receive file dimension
  char size_rcv_str[11] = {0};
  
  int j = recv_rel(sockfd, size_rcv_str, sizeof(size_rcv_str), false, &servaddr, &servaddr_len);
  size_rcv_str[j] = '\0';
  uint32_t size_rcv = (uint32_t)strtol(size_rcv_str, NULL, 10);
  uint32_t size_next = ntohl(size_rcv);
  
  char *recvline = (char *)malloc(size_next + 1);
  // receive file
  n = recv_rel(sockfd, recvline, size_next, true, &servaddr, &servaddr_len);
  recvline[n] = '\0'; 
  printf("Recvline size: %d\n", size_next);
  char *path = file_path(DEST_PATH, name);

  FILE *file = fopen(path, "wb"); // Opening the file in binary write mode
  if (file == NULL) {
    error("Error in destination file opening");
  }

  // Writing the received data in the file
  if (fwrite(recvline, 1, n, file) != (size_t) n) {
    error("Error in file writing");
  }
  if (fclose(file) != 0) {
    error("Error in file closing");
  }
  printf("File saved successfully in: %s\n\n", path);
  fflush(stdout);

  free(recvline);
  free(name);
  free(path);
}



void put_option() {

  // file selection to save it to the server

  char list_command[MAXLINE];
  list_command[0] = '\0';
  ls(list_command);
  if (strlen(list_command) == 0) {
    printf("No files available");
    fflush(stdout);
    char *file_to_send = malloc(sizeof(char));
    file_to_send[0] = '\0';
    send_rel_single(sockfd, servaddr, file_to_send, false);
    return;
  }

  size_t l = strlen(list_command);
    size_t cumulative_index[MAXVECT] = {0};
    int count = 1;
    for (size_t i = 0; i < l-1; i++) {
      if (list_command[i] == '\n') {
        cumulative_index[count] = i+1;
        count++;
      }
    }

  printf("Choose one of the following files:\n");
  size_t c_index = 0;
  size_t c = 0;
  printf("\t%lu. ", c+1);
  while (list_command[c_index] != '\0') {
    if (list_command[c_index] == '\n') {
      if (list_command[c_index+1] != '\0') {
        c++;
        printf("\n\t%lu. ", c+1);
      }
    } else {
      printf("%c", list_command[c_index]);
    }
    c_index ++;
  }

  printf("\nType the corresponding number:\n");
  fflush(stdout);
  
  char buff_in;
  int buff_in_int = wait_for_input(count, &buff_in);

  size_t curr_index = cumulative_index[buff_in_int];
  size_t index = curr_index;
  size_t i = 0;
  while (list_command[curr_index] != '\n') {
    curr_index ++;
    i++;
  }
  char *file_to_send = '\0';
  file_to_send = malloc((i+1)*sizeof(char));
  if (file_to_send == NULL) {
    error("Error in memory allocation\n");
  }
  strncpy(file_to_send, list_command + index, i);
  file_to_send[i] = '\0';
  send_rel_single(sockfd, servaddr, file_to_send, false);
  
  // sending file
  printf("Sending file\n");
  fflush(stdout);
  char *FILENAME = file_path(DEST_PATH, file_to_send);
  file_to_save = fopen(FILENAME, "rb");
  if (file_to_save == NULL) {
    error("Error in file opening");
  }
  size_file_to_save = file_size(FILENAME);
  send_rel(sockfd, servaddr, file_to_save, size_file_to_save);

  fclose(file_to_save);
  printf("Sending complete\n");
  fflush(stdout);

  free(file_to_send);
}



int main(int argc, char *argv[]) {

  check_args(argc, argv);

  srand(time(NULL)); // random seed

  ////////// New connection //////////

  create_conn(server_ip, htons(SERV_PORT));
  char *new = "new";
  send_rel_single(sockfd, servaddr, new, false);

  // new port
  char new_port_str[6];
  recv_rel(sockfd, new_port_str, sizeof(new_port_str), false, NULL, NULL);
  
  ///////// Welcome message //////////
  
  char welcome[MAXLINE];
  socklen_t servaddr_len = sizeof(servaddr);
  n = recv_rel(sockfd, welcome, MAXLINE, false, &servaddr, &servaddr_len);
  if (n < 9) {
    error("Invalid data received");
  }
  welcome[n] = '\0';

  ////////// Choice action ///////////

  char choice_action[n-9+1];
  strcpy(choice_action, welcome + 9);
  welcome[9] = '\0';
  printf("\n%s", welcome);
  fflush(stdout);

  while(1) {
    printf("%s\n", choice_action);
    fflush(stdout);

    char buff_in;
    int buff_in_int = wait_for_input(4, &buff_in);

    send_rel_single(sockfd, servaddr, &buff_in, buff_in_int == 3 ? true : false);

    ////////// Manage choice ///////////

    if(buff_in_int == 0) {
      list_option(servaddr_len, 0, NULL); // 0: list option
    } else if (buff_in_int == 1) {
      get_option(servaddr_len);
    } else if (buff_in_int == 2) {
      put_option(servaddr_len);
    } else if(buff_in_int == 3) {
      break;
    } else error("Error in switch case / buff_in\n");

    printf("\n\n\n");
  }

  close(sockfd); 

  exit(0);

}