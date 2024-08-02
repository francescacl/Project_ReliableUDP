#include "server.h"



void error(const char *msg) {
  perror(msg);
  exit(1);
}



void check_args(int argc, char *argv[]) {
  if (argc != 3) { // check args num
    printf("Wrong number of arguments\n");
    fprintf(stderr, "usage: server <packet loss probability> <timeout [ms]>\n");
    exit(1);
  }

  loss_prob = atof(argv[1]);
  if (loss_prob < 0 || loss_prob > 1) {
    fprintf(stderr, "Loss probability must be within 0 and 1\n");
    exit(1);
  }
  int temp = atoi(argv[2]);
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
  printf("Chosen arguments:\n  - Loss probability: %s\n  - Timeout: %d [ms]\n\n\n", loss_perc, temp);

  return;
}



size_t file_size(char *filename) {
  //Return file size [bytes]
  FILE *file = fopen(filename, "rb");
  size_t size = -1;

  if (file != NULL) {
    if (fseek(file, 0, SEEK_END) == 0) { // Shift the file pointer to the end of the file
      size = ftell(file); // Current position corresponds to the size of the file
    }
  fclose(file);
  }

  return size;
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
  // Return file path
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



void send_ack(int sockfd, struct sockaddr_in *address, uint32_t ack_num) {
  udp_packet_t ack_packet;
  ack_packet.seq_num = 0;
  ack_packet.ack_num = ack_num;
  ack_packet.data_size = 0;
  ack_packet.checksum = calculate_checksum(&ack_packet);
  if (sendto(sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)address, (socklen_t) sizeof(*address)) < 0) {
    error("Error in send_ack");
  }
}



void ls(char *list_command){

  sprintf(list_command, "ls %s", FILENAME_PATH);
     
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



void create_conn() {

  len = sizeof(struct sockaddr_in);

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { // create the socket
    error("Error in socket");
  }
  printf("[%lu] Socket created\n", tid);
  fflush(stdout);

  memset((void *)&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // the socket will be bound to all the interfaces
  addr.sin_port = htons(SERV_PORT); // server port

  // assign the address to the socket
  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    error("Error in bind");
  }
  printf("[%lu] Socket binded\n", tid);
  fflush(stdout);
}



int wait_recv(char *buff, long size, int sockfd, struct sockaddr_in *address, socklen_t *addr_length, struct sockaddr_in *client_addr) { 
  int totalReceived = 0;
  if (sockfd > 0) {
    int received = 0;
    while(size > 0) {
      uint8_t buffer[MAX_SIZE_STRUCT];
      errno = 0;
      if ((received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)address, addr_length)) < 0) {
        if (errno == EINTR || errno == EAGAIN) {
          send_ack(sockfd, client_addr, seq_num_recv-1);
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
        send_ack(sockfd, client_addr, seq_num_recv);
        seq_num_recv += 1;
      } else {
        send_ack(sockfd, client_addr, seq_num_recv-1);
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



int recv_rel(int sock, char *buff, size_t dim, bool size_rcv, struct sockaddr_in *address, socklen_t *addr_length, struct sockaddr_in *client_addr) {
  int k;
  if(!size_rcv) {
    char buffer[MAX_SIZE_STRUCT] = {0};
    set_timeout(sock, timeout_s, timeout_us);
    while(1) {
      errno = 0;
      int j = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)address, addr_length);
      if (j < 0) {
        if (errno == EINTR || errno == EAGAIN) {
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

      if (packet->checksum == calculate_checksum(packet) && packet->seq_num == seq_num_recv) {
        send_ack(sock, client_addr, seq_num_recv);
        seq_num_recv += 1;
        break;
      } else {
        send_ack(sock, client_addr, seq_num_recv-1);
      }
      
      free(packet);
    }
  } else {
    k = wait_recv(buff, dim, sock, address, addr_length, client_addr);
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
  for (uint32_t i = 0; i < bytes_read; i++) {
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




void send_rel_single(int fd, struct sockaddr_in send_addr, char *data) {
  udp_packet_t* packet;
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

    printf("[%lu] End sender thread\n", tid);
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
  long tid = pthread_self();

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

  printf("[%lu] End receiver thread\n", tid);
  pthread_exit(NULL);
}



void send_rel(int fd, struct sockaddr_in send_addr, FILE* file, size_t size_file) {
  
  uint32_t size = htonl(size_file);
  char size_str[11];
  size_str[0] = '\0';
  snprintf(size_str, sizeof(size_str), "%u", size);
  send_rel_single(fd, send_addr, size_str);

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



void* handle_user(void* arg) {

  // parse arg
  thread_data_t* thread_data;
  thread_data = (thread_data_t *) arg;
  struct sockaddr_in client_addr = thread_data->addr;
  
  // thread variables
  size_t size_file;
  FILE *file;
  seq_num_send = 0;
  seq_num_recv = 1;

  // thread id
  tid = pthread_self();
  printf("[%lu] Thread tid\n", tid);

  // create new socket, bind it to a new port number and send it to the client
	int new_socket;
	struct sockaddr_in new_addr;

	if ((new_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		error("Error socket");
	}

  // find a new port number
	int i = 0;
  uint32_t new_port;
	while (1) {
		new_port = SERV_PORT + i;

    // new socket binding with new port number
		memset((void *)&new_addr, 0, sizeof(new_addr));
		new_addr.sin_family = AF_INET;
		new_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		new_addr.sin_port = htons(new_port);
		if (bind(new_socket, (struct sockaddr *)&new_addr, sizeof(new_addr)) < 0) {
			if (errno == EADDRINUSE) {
				i++;
			}
			else if (errno != EINTR) {
				error("Error bind");
			}
		}
		else {
			break;
		}
	}
  
  // print new port number
  printf("[%lu] New port number: %u\n\n", tid, new_port);
  fflush(stdout);

  // send new port number to the client
  char new_port_str[11];
  new_port_str[0] = '\0';
  snprintf(new_port_str, sizeof(new_port_str), "%u", new_port);
  printf("[%lu] Sending new port number\n", tid);
  send_rel_single(new_socket, client_addr, new_port_str);
  printf("[%lu] New port number sent\n\n", tid);

  // connection established, send welcome message
  char welcome_msg[] = "Welcome! Choose one of the following options:\n\t1. LIST all available files\n\t2. GET a file\n\t3. PUT a file in the server\n\t4. Quit.\nType the corresponding number:";
  printf("[%lu] Sending welcome message\n", tid);
  send_rel_single(new_socket, client_addr, welcome_msg);
  printf("[%lu] Welcome message sent\n", tid);
  
  while (1) {
    char option[2];
    option[0] = '\0';
    printf("\n[%lu] Waiting for option...\n", tid);
    recv_rel(new_socket, option, sizeof(option), false, NULL, NULL, &client_addr);
    printf("[%lu] Option received\n", tid);
    option[1] = '\0';
    int option_int = atoi(option);

    if (option_int == 1) { // LIST
      char list_command[MAXLINE] = {0};
      list_command[0] = '\0';
      ls(list_command);
      if (strlen(list_command) == 0) {
        char no_files[] = "No files available";
        send_rel_single(new_socket, client_addr, no_files);
      } else {
        send_rel_single(new_socket, client_addr, list_command);
      }
    }

    else if (option_int == 2) { // GET
      // list
      char list_command[MAXLINE] = {0};
      list_command[0] = '\0';
      ls(list_command);
      if (strlen(list_command) == 0) {
        char no_files[] = "No files available";
        send_rel_single(new_socket, client_addr, no_files);
      } else {
        send_rel_single(new_socket, client_addr, list_command);
        // file name
        char name[MAXLINE];
        int j = recv_rel(new_socket, name, MAXLINE, false, NULL, NULL, &client_addr);
        name[j] = '\0';
        // send file dimension
        printf("[%lu] Sending file\n", tid);
        fflush(stdout);
        char *FILENAME = file_path(FILENAME_PATH, name);
        file = fopen(FILENAME, "rb");
        if (file == NULL) {
          error("Error in file opening");
        }
        size_file = file_size(FILENAME);
        send_rel(new_socket, client_addr, file, size_file);
        fclose(file);
        printf("[%lu] Sending complete\n", tid);
        fflush(stdout);

        free(FILENAME);
      }
    }
    
    else if (option_int == 3) { // PUT
      // receive file name
      char name_to_save[MAXLINE];
      int k = recv_rel(new_socket, name_to_save, MAXLINE, false, NULL, NULL, &client_addr);
      if (name_to_save[0] == '\0') {
        continue;
      }
      name_to_save[k] = '\0';
      // receive file dimension
      char size_rcv_str[11] = {0};
      int j = recv_rel(new_socket, size_rcv_str, sizeof(size_rcv_str), false, NULL, NULL, &client_addr);
      size_rcv_str[j] = '\0';
      uint32_t size_rcv = (uint32_t)strtol(size_rcv_str, NULL, 10);
      uint32_t size_next = ntohl(size_rcv);
      
      char *recvline = (char *)malloc(size_next + 1);
      // receive file
      int n = recv_rel(new_socket, recvline, size_next, true, NULL, NULL, &client_addr);
      recvline[n] = '\0';
      char *path_to_save = file_path(FILENAME_PATH, name_to_save);
      FILE *file_to_save = fopen(path_to_save, "wb"); // Opening the file in binary write mode
      if (file_to_save == NULL) {
        error("Error in destination file opening");
      }
      // Writing the received data in the file
      if (fwrite(recvline, 1, n, file_to_save) != (size_t) n) {
        error("Error in file writing");
      }
      fclose(file_to_save); // Closing the file
      free(recvline);
      printf("[%lu] File saved successfully in: %s\n\n", tid, path_to_save);
      fflush(stdout);

    }
    
    else if (option_int == 4) break;

  }
  // End thread
  printf("[%lu] Thread terminated\n\n", tid);
  fflush(stdout);

  pthread_exit(NULL);

}



int main(int argc, char **argv) {

  check_args(argc, argv);

  srand(time(NULL)); // random seed

  tid = pthread_self();
  printf("[%lu] Main tid\n", tid);

  create_conn();
  
  while (1) {
    seq_num_send = 0;
    seq_num_recv = 0;
    
    printf("[%lu] Waiting for request...\n", tid);
    fflush(stdout);

    char buff[MAXLINE] = {0};
    buff[0] = '\0';
    int rec = recv_rel(sockfd, buff, MAXLINE, false, &addr, &len, &addr); 
    buff[rec] = '\0';
    if (strcmp(buff, "new") == 0) {
      printf("\n[%lu] Request received\n", tid);
      fflush(stdout);

      // Start a new thread to handle the connection
      pthread_t thread_id;
      thread_data_t thread_data;
      thread_data.addr = addr;
      if (pthread_create(&thread_id, NULL, handle_user, &thread_data) != 0) {
        error("Error creating thread");
      }
    } else {
      printf("[%lu] Invalid request\n", tid);
      fflush(stdout);
    }
  }
  
  exit(0);
}