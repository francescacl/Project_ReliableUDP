#include "client.h"



void error(const char *msg) {
  perror(msg);
  exit(1);
}



void check_args(int argc, char *argv[]) {
  if (argc != 4) { /* controlla numero degli argomenti */
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
/* Ritorna la grandezza del file in byte */
  FILE *file = fopen(filename, "rb");
  size_t fsize = -1;

  if (file != NULL) {
    if (fseek(file, 0, SEEK_END) == 0) { // Sposta l'indicatore di posizione alla fine del file
      fsize = ftell(file); // La posizione corrente corrisponde alla dimensione del file
    }
  fclose(file);
  }

  return fsize;
}



uint32_t calculate_checksum(udp_packet_t *packet) {
    uint32_t checksum = 0;
    uint32_t sum = 0;
    
    // Somma seq_num e ack_num come sequenze di 16 bit
    sum += (packet->seq_num >> 16) & 0xFFFF;
    sum += packet->seq_num & 0xFFFF;
    sum += (packet->ack_num >> 16) & 0xFFFF;
    sum += packet->ack_num & 0xFFFF;

    // Somma i dati come sequenze di 16 bit
    for (int i = 0; i < packet->data_size; i += 2) {
        uint16_t word = packet->data[i];
        if (i + 1 < packet->data_size) {
            word |= (packet->data[i + 1] << 8);
        }
        sum += word;
    }

    // Aggiungi eventuali riporti alla fine della somma
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // Inverti i bit del risultato finale
    checksum = ~sum;

    return checksum;
}



char* file_path(char *fpath, char *fname) {
/* Ritorna il path del file concatenato al nome del file */
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
  printf("[send_ack] Ack sent with ack_num %d\n", ack_num);
}



void ls(char *list_command){

  sprintf(list_command, "ls %s", DEST_PATH);
     
  FILE *pipe = popen(list_command, "r");
  if (pipe == NULL) {
    error("Error in opening the pipe");
  }
  // Leggi l'output del comando e salvalo in una stringa
  size_t bytes_read = fread(list_command, 1, MAXLINE, pipe);
  if (bytes_read == 0) {
    error("Error in reading the output of the command");
  }
  list_command[bytes_read] = '\0';

  // Chiudi la pipe
  pclose(pipe);
  printf("%s folder contents:\n%s", DEST_PATH, list_command);

}



void create_conn(char *ip_address, uint16_t port) {

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea la socket */
    error("Error in socket");
  }
  
  memset(&servaddr, 0, sizeof(servaddr));      /* azzera servaddr */
  servaddr.sin_family = AF_INET;       /* assegna il tipo di indirizzo */
  servaddr.sin_port = port;  /* assegna la porta del server */
  /* assegna l'indirizzo del server prendendolo dalla riga di comando. L'indirizzo è una stringa da convertire in intero secondo network byte order. */
  if (inet_pton(AF_INET, ip_address, &servaddr.sin_addr) <= 0) { /* inet_pton (p=presentation) vale anche per indirizzi IPv6 */
    fprintf(stderr, "Error in inet_pton for %s", ip_address);
    exit(1);
  }

  return;
}



int wait_recv(char *buff, long size, int sockfd, struct sockaddr_in *address, socklen_t *addr_length) { 
  udp_packet_t packet;
  int totalReceived = 0;
  if (sockfd > 0) {
    int received = 0;
    while(size > 0) {
      errno = 0;
      if ((received = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)address, addr_length)) < 0) {
        if (errno == EINTR || errno == EAGAIN) {
          send_ack(sockfd, &servaddr, seq_num_recv-1);
          continue;
        }
        fprintf(stderr, "Error udp wait_recv: %d\n", errno);
        return -1;
      }

      double random_number = (double)rand() / RAND_MAX;
      if (random_number < loss_prob) {
        printf("[wait_recv] Packet %d lost\n", packet.seq_num);
        continue;
      }

      printf("[wait_recv] Received packet with seq_num: %d\n", packet.seq_num);
      printf("[wait_recv] Current seq_num_recv: %d\n", seq_num_recv);
      if (packet.checksum == calculate_checksum(&packet) && packet.seq_num == seq_num_recv) {
        send_ack(sockfd, &servaddr, seq_num_recv);
        seq_num_recv += 1;
      } else {
        send_ack(sockfd, &servaddr, seq_num_recv-1);
        continue;
      }

      memcpy(buff + totalReceived, packet.data, packet.data_size);
      totalReceived += packet.data_size;
      size -= packet.data_size;
    }

    return totalReceived;
  }
  return -1;
}



int recv_rel(int sock, char *buffer, size_t dim, bool size_rcv, struct sockaddr_in *address, socklen_t *addr_length) {
  int k;
  if(!size_rcv) {
    udp_packet_t packet;
    set_timeout(sock, timeout_s, timeout_us);
    while(1) {
      errno = 0;
      if (recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr *)address, addr_length) < 0) {
        if (errno == EINTR || errno == EAGAIN /* timeout */) {
          printf("[recv_rel] errno: %d\n", errno);
          fflush(stdout);
          continue;
        }
        error("Error in recvfrom");
        return -2;
      }

      double random_number = (double)rand() / RAND_MAX;
      if (random_number < loss_prob) {
        printf("[recv_rel] Packet %d lost\n", packet.seq_num);
        continue;
      }

      k = packet.data_size;
      memcpy(buffer, packet.data, k);

      if (address == NULL) {
        servaddr.sin_port = htons(strtoul(buffer, NULL, 10));
        printf("New port: %d\n", htonl(servaddr.sin_port));
      }
      printf("[recv_rel] Received packet with seq_num: %d\n", packet.seq_num);
      printf("[recv_rel] Current seq_num_recv: %d\n", seq_num_recv);
      if (packet.checksum == calculate_checksum(&packet) && packet.seq_num == seq_num_recv) {
        printf("[recv_rel] Correct package, seq_num_recv++: %d\n", seq_num_recv+1);
        send_ack(sock, &servaddr, seq_num_recv);
        seq_num_recv += 1;
        break;
      } else {
        send_ack(sock, &servaddr, seq_num_recv-1);
      }
    }
  } else {
    k = wait_recv(buffer, dim, sock, address, addr_length);
  }
  set_timeout(sock, 1000000, 0);
  return k;
}



int bytes_read_funct(char **data, FILE* file, udp_packet_t* packet) {
  char buff[MAXLINE];
  int bytes_read;
  bool is_file = file != NULL;

  // prepare packet to be sent
  packet->seq_num = seq_num_send;
  packet->ack_num = seq_num_recv-1;
  memset(packet->data, 0, MAXLINE);
  
  if (is_file) {
    bytes_read = fread(buff, 1, MAXLINE, file);
    memcpy(packet->data, buff, bytes_read);
  } else {
    bytes_read = strlen(*data);
    if (bytes_read > MAXLINE) {
      bytes_read = MAXLINE;
    }
    memcpy(packet->data, *data, bytes_read);
  }

  packet->data_size = bytes_read;
  packet->checksum = calculate_checksum(packet);
  
  if (!is_file) {
    *data += bytes_read;
  }

  return bytes_read;
}



void send_rel_single(int fd, struct sockaddr_in send_addr, char *data) {
  udp_packet_t packet;
  int bytes_read = bytes_read_funct(&data, NULL, &packet);
  if (bytes_read < 0) {
    error("Error in reading data");
  }

  set_timeout(fd, timeout_s, timeout_us);

  while(1) {
    printf("[send_rel_single] Sending packet %d\n", packet.seq_num);
    if (sendto(fd, &packet, sizeof(packet), 0, (struct sockaddr *)&send_addr, sizeof(send_addr)) < 0)
      error("Error in sendto");

    // wait for ack
    udp_packet_t ack_packet;
    while (1) {
      memset(&ack_packet, 0, sizeof(ack_packet));
      errno = 0;
      printf("[send_rel_single] Waiting for ack %d\n", packet.seq_num);
      if (recvfrom(fd, &ack_packet, sizeof(ack_packet), 0, NULL, NULL) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          printf("[send_rel_single] errno: %d\n", errno);
          printf("[send_rel_single] Timeout...\n");
          break;
        }
        error("Error in recvfrom");
      }
      printf("[send_rel_single] ack_packet.ack_num %d\n", ack_packet.ack_num);

      double random_number = (double)rand() / RAND_MAX;
      printf("[send_rel_single] random_number: %f\n", random_number);
      if (random_number < loss_prob) {
        printf("[send_rel_single] Ack packet %d lost\n", ack_packet.ack_num);
        continue;
      }
      break;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      continue;
    }
    printf("[send_rel_single] ack_packet.ack_num %d\n", ack_packet.ack_num);
    printf("[send_rel_single] packet.seq_num = %d\n", packet.seq_num);
    if (ack_packet.ack_num == packet.seq_num) {
      printf("[send_rel_single] Ack %d received\n", ack_packet.ack_num);
      break;
    }
  }
  seq_num_send += 1;
  set_timeout(fd, 1000000, 0);
}



void *send_rel_sender_thread(void *arg) {
  // parsa arg
  thread_data_t* thread_data;
  thread_data = (thread_data_t *) arg;
  int sockfd = thread_data->sockfd;
  struct sockaddr_in server_addr = thread_data->addr;
  FILE* file = thread_data->file;
  uint32_t *base = thread_data->base;
  pthread_mutex_t *lock = thread_data->lock;
  pthread_cond_t *cond = thread_data->cond;
  atomic_bool *end_thread = thread_data->end_thread;
  time_t *timer_start = thread_data->timer_start;
  const uint32_t num_packets = thread_data->num_packets;

  uint32_t next_seq_num = *base;
  uint32_t next_seq_num_start = next_seq_num;

  udp_packet_t packets[num_packets];
  memset(packets, 0, sizeof(packets));
  for (uint32_t i = 0; i < num_packets; i++) {
    bytes_read_funct(NULL, file, &packets[i]);
  }

  while (!atomic_load(end_thread)) {
    pthread_mutex_lock(lock);

    while (next_seq_num < (*base) + WINDOW_SIZE) {
      if (next_seq_num == num_packets + next_seq_num_start) {
        break;
      }

      udp_packet_t packet = packets[next_seq_num - next_seq_num_start];
      packet.seq_num = next_seq_num;
      packet.checksum = calculate_checksum(&packet);
      
      thread_data->acked[next_seq_num % WINDOW_SIZE] = 0;

      sendto(sockfd, &packet, sizeof(packet), 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
      printf("Sent packet with seq_num %d\n", packet.seq_num);

      if (next_seq_num == *base) {
        printf("Setting timeout\n");
        *timer_start = time(NULL);
      }
      next_seq_num++;
    }

    pthread_cond_wait(cond, lock);
    if (difftime(time(NULL), *timer_start) > ((double) timeout_s + (double) timeout_us / 1000000)) {
      printf("\t\tTimeout\n");
      memset(thread_data->acked, 0, WINDOW_SIZE * sizeof(bool));
      next_seq_num = *base;
    }
    pthread_mutex_unlock(lock);
  }

  printf("End sender thread\n");
  pthread_exit(NULL);
}



void *send_rel_receiver_thread(void *arg) {
  // parsa arg
  thread_data_t* thread_data;
  thread_data = (thread_data_t *) arg;
  int sockfd = thread_data->sockfd;
  uint32_t num_packets = thread_data->num_packets;
  uint32_t *base = thread_data->base;
  pthread_mutex_t *lock = thread_data->lock;
  pthread_cond_t *cond = thread_data->cond;
  atomic_bool *end_thread = thread_data->end_thread;
  time_t *timer_start = thread_data->timer_start;

  uint32_t seq_num_start = *base;

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
      printf("[send_rel_receiver_thread] Ack packet %d lost\n", ack_packet.ack_num);
      continue;
    }

    uint32_t ack_num = ack_packet.ack_num;
    printf("[send_rel_receiver_thread] Received ack for packet with seq_num %d\n", ack_num);

    if (ack_num == num_packets + seq_num_start - 1) {
      atomic_store(end_thread, true);
      break;
    }
    pthread_mutex_lock(lock);

    if (ack_num >= (*base)) {
      for (uint32_t i = *base; i < ack_num; i++) {
        thread_data->acked[i % WINDOW_SIZE] = 1;
      }

      *timer_start = time(NULL);

      while (thread_data->acked[(*base) % WINDOW_SIZE]) {
        (*base)++;
      }
      pthread_cond_signal(cond);
    }

    pthread_mutex_unlock(lock);
  }

  set_timeout(sockfd, 1000000, 0);

  pthread_cond_signal(cond);

  printf("End receiver thread\n");
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
  time_t timer_start;

  atomic_bool end_thread;
  end_thread = ATOMIC_VAR_INIT(false);
  atomic_store(&end_thread, false);

  uint32_t base = seq_num_send;
  bool acked[WINDOW_SIZE];
  memset(acked, 0, sizeof(acked));

  thread_data_t thread_data;
  thread_data.sockfd = fd;
  thread_data.addr = send_addr;
  thread_data.num_packets = num_packets(size_file);
  thread_data.file = file;
  thread_data.base = &base;
  thread_data.lock = &lock;
  thread_data.cond = &cond;
  thread_data.end_thread = &end_thread;
  thread_data.acked = malloc(WINDOW_SIZE * sizeof(bool));
  thread_data.timer_start = &timer_start;

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

  while(1) { // scelta opzione da terminale
    __fpurge(stdin);
    
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    FD_SET(sockfd, &readfds);

    int maxfd = (STDIN_FILENO > sockfd) ? STDIN_FILENO : sockfd;

    // Attendere che uno dei file descriptor sia pronto per la lettura
    int activity = select(maxfd + 1, &readfds, NULL, NULL, NULL);

    if (activity < 0 && errno != EINTR) {
      error("Error in select function");
    }

    // Controllare se c'è input da stdin
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

    // Controllare se c'è input dalla socket
    if (FD_ISSET(sockfd, &readfds)) {
      udp_packet_t packet;
      if (recvfrom(sockfd, &packet, sizeof(packet), 0, NULL, NULL) < 0) {
        if (errno == EINTR || errno == EAGAIN /* timeout */) {
          printf("[wait_for_input] errno: %d\n", errno);
          fflush(stdout);
          continue;
        }
        error("Error in recvfrom");
      }
      if (packet.checksum == calculate_checksum(&packet) && packet.seq_num < seq_num_recv) {
        send_ack(sockfd, &servaddr, seq_num_recv-1);
      }
    }
    /////////////////////////////////////////////////////
  }
  return buff_in_int;
}



void list_option(socklen_t servaddr_len, int option, char **name) {
  
  recv_rel(sockfd, list_str, MAXLINE, false, &servaddr, &servaddr_len);

  // Salvo la lunghezza delle singole stringhe
  size_t l = strlen(list_str);
  size_t cumulative_index[MAXVECT] = {0};
  int count = 1;
  for (size_t i = 0; i < l-1; i++) {
    if (list_str[i] == '\n') {
      cumulative_index[count] = i+1;
      count++;
    }
  }
  // ora ho cumulative_index che ha gli indici di inizio di ogni stringa dentro list_str
  if (option == 0) {
    printf("Files available in the server: \n%s\n", list_str);
    fflush(stdout);
  } else {
    printf("Choose one of the following files:\n");
    size_t c_index = 0; //cumulative_index[buff_in_int];
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

  // Manda il nome del file desiderato al server
  send_rel_single(sockfd, servaddr, name);
  // Ricezione della grandezza del file dal server
  char size_rcv_str[11] = {0};
  
  int j = recv_rel(sockfd, size_rcv_str, sizeof(size_rcv_str), false, &servaddr, &servaddr_len);
  size_rcv_str[j] = '\0';
  uint32_t size_rcv = (uint32_t)strtol(size_rcv_str, NULL, 10);
  uint32_t size_next = ntohl(size_rcv);
 
  char recvline[size_next + 1];
  n = recv_rel(sockfd, recvline, size_next, true, &servaddr, &servaddr_len);
  recvline[n] = '\0';        // aggiunge il carattere di terminazione
  char *path = file_path(DEST_PATH, name);

  FILE *file = fopen(path, "wb"); // Apertura del file in modalità binaria
  if (file == NULL) {
    error("Error in destination file opening");
  }

  // Scrittura dei dati nel file
  if (fwrite(recvline, 1, n, file) != (size_t) n) {
    error("Error in file writing");
  }
  fclose(file); // Chiusura del file
  printf("File saved successfully in: %s\n\n", path);
  fflush(stdout);
}



void put_option() {

  // selezione del file da mandare invio del nome al server

  char list_command[MAXLINE];
  list_command[0] = '\0';
  ls(list_command);

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
  size_t c_index = 0; //cumulative_index[buff_in_int];
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
  send_rel_single(sockfd, servaddr, file_to_send);

  // invio dell'immagine al server
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
}



int main(int argc, char *argv[]) {

  check_args(argc, argv);

  srand(time(NULL)); // inizializzazione seed numeri randomici

  ////////// New connection //////////

  create_conn(server_ip, htons(SERV_PORT));
  printf("Connection with main established\n");
  fflush(stdout);
  //char *new = '\0'
  char *new = "new\0";
  printf("Sending request\n");
  send_rel_single(sockfd, servaddr, new);
  printf("Request sent\n\n");
  fflush(stdout);

  // ricezione della nuova porta
  char new_port_str[6];
  printf("Receiving new port\n");
  recv_rel(sockfd, new_port_str, sizeof(new_port_str), false, NULL, NULL);
  printf("Connection with new port established\n\n");
  fflush(stdout);
  
  ///////// Welcome message //////////
  
  char welcome[MAXLINE];
  socklen_t servaddr_len = sizeof(servaddr);
  printf("Receiving welcome message\n");
  n = recv_rel(sockfd, welcome, MAXLINE, false, &servaddr, &servaddr_len);
  if (n < 9) {
    error("Invalid data received");
  }
  welcome[n] = '\0';

  ////////// Choice action ///////////

  char choice_action[n-9+1]; // +1 per il terminatore null
  strcpy(choice_action, welcome + 9);
  welcome[9] = '\0';
  printf("\n%s", welcome);
  fflush(stdout);

  while(1) {
    printf("%s\n", choice_action);
    fflush(stdout);

    char buff_in;
    int buff_in_int = wait_for_input(4, &buff_in);

    // manda la scelta del client al server
    send_rel_single(sockfd, servaddr, &buff_in);

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

    // ask user to press enter
    // printf("Press enter to continue\n");
    // __fpurge(stdin);
    // getchar();
    printf("\n\n\n");
  }

  close(sockfd); // Chiusura della socket

  exit(0);

}