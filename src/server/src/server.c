#include "server.h"



void error(const char *msg) {
  perror(msg);
  exit(1);
}



void check_args(int argc, char *argv[]) {
  if (argc != 3) { /* controlla numero degli argomenti */
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

  return;
}



size_t file_size(char *filename) {
/* Ritorna la grandezza del file in byte */
  FILE *file = fopen(filename, "rb");
  size_t size = -1;

  if (file != NULL) {
    if (fseek(file, 0, SEEK_END) == 0) { // Sposta l'indicatore di posizione alla fine del file
      size = ftell(file); // La posizione corrente corrisponde alla dimensione del file
    }
  fclose(file);
  }

  return size;
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



void send_ack(int sockfd, struct sockaddr_in *address, uint32_t ack_num) {
  udp_packet_t ack_packet;
  ack_packet.seq_num = 0;
  ack_packet.ack_num = ack_num;
  ack_packet.data_size = 0;
  ack_packet.checksum = calculate_checksum(&ack_packet);
  printf("Sending ack %d\n", ack_num);
  fflush(stdout);
  if (sendto(sockfd, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)address, (socklen_t) sizeof(*address)) < 0) {
    error("Error in send_ack");
  }
}



int wait_recv(char *buff, long size, int sockfd, struct sockaddr_in *address, socklen_t *addr_length, struct sockaddr_in *client_addr) { 
  udp_packet_t packet;
  int totalReceived = 0;
  if (sockfd > 0) {
    int received = 0;
    while(size > 0) {
      errno = 0;
      if ((received = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)address, addr_length)) < 0) {
        if (errno == EINTR || errno == EAGAIN) {
          send_ack(sockfd, client_addr, seq_num_recv-1);
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
      
      if (packet.checksum == calculate_checksum(&packet) && packet.seq_num == seq_num_recv) {
        send_ack(sockfd, client_addr, seq_num_recv);
        seq_num_recv += 1;
      } else {
        send_ack(sockfd, client_addr, seq_num_recv-1);
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



int recv_rel(int sock, char *buffer, size_t dim, bool size_rcv, struct sockaddr_in *address, socklen_t *addr_length, struct sockaddr_in *client_addr) {
  int k;
  if(!size_rcv) {
    udp_packet_t packet;
    while(1) {
      errno = 0;
      if (recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr *)address, addr_length) < 0) {
        if (errno == EINTR || errno == EAGAIN /* timeout */) {
          printf("errno: %d\n", errno);
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

      printf("packet.seq_num = %d\n", packet.seq_num);
      printf("seq_num_recv = %d\n", seq_num_recv);
      fflush(stdout); 
      
      if (packet.checksum == calculate_checksum(&packet) && packet.seq_num == seq_num_recv) {
        send_ack(sock, client_addr, seq_num_recv);
        seq_num_recv += 1;
        break;
      } else {
        send_ack(sock, client_addr, seq_num_recv-1);
      }
    }

    k = packet.data_size;
    printf("packet.data: %s\n", packet.data);
    fflush(stdout);
    memcpy(buffer, packet.data, k);
  } else {
    k = wait_recv(buffer, dim, sock, address, addr_length, client_addr);
  }
  return k;
}



int bytes_read_funct(char **data, FILE* file, udp_packet_t* packet) {
  int bytes_read;
  bool is_file = file != NULL;
  char buff[MAXLINE] = {0};

  // prepare packet to be sent
  packet->seq_num = seq_num_send;
  packet->ack_num = seq_num_recv-1;
  memset(packet->data, 0, MAXLINE);
  
  if (is_file) {
    bytes_read = fread(buff, 1, MAXLINE, file);
    //packet->data = (char*)malloc(bytes_read * sizeof(char));
    memcpy(packet->data, buff, bytes_read);
  } else {
    bytes_read = strlen(*data);
    if (bytes_read > MAXLINE) {
      bytes_read = MAXLINE;
    }
    //packet->data = (char*)malloc(bytes_read * sizeof(char));
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
          printf("[send_rel_single] Timeout...\n");
          break;
        }
        error("Error in recvfrom");
      }
      printf("[send_rel_single] ack_packet.ack_num %d\n", ack_packet.ack_num);

      double random_number = (double)rand() / RAND_MAX;
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
  printf("seq_num_send = %d\n", seq_num_send);

  set_timeout(fd, 1000000, 0);
    
  //free(packet.data);
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



void create_conn() {

  len = sizeof(struct sockaddr_in);

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea la socket */
    error("Error in socket");
  }
  printf("Socket created\n");
  fflush(stdout);

  memset((void *)&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY); /* il server accetta pacchetti su una qualunque delle sue interfacce di rete */
  addr.sin_port = htons(SERV_PORT); /* numero di porta del server */

  /* assegna l'indirizzo al socket */
  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    error("Error in bind");
  }
  printf("Socket binded\n");
  fflush(stdout);
}



void ls(char *list_command){

  sprintf(list_command, "ls %s", FILENAME_PATH);
     
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
  if (pclose(pipe) == -1) {
    error("Error in closing the pipe");
  }

}



void* handle_user(void* arg) {

  // parsa arg
  thread_data_t* thread_data;
  thread_data = (thread_data_t *) arg;
  struct sockaddr_in client_addr = thread_data->addr;
  
  // thread variables
  size_t size_file;
  FILE *file;
  seq_num_send = 0;
  seq_num_recv = 1;

  // ottiene il thread id
  tid = pthread_self();
  printf("[%lu] Thread tid\n", tid);

  // crea una nuova socket, ne fa la bind a un nuovo numero di porta e lo manda al client
	int new_socket;
	struct sockaddr_in new_addr;

	if ((new_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		error("Error socket");
	}

  // trova il nuovo numero di porta
	int i = 0;
  uint32_t new_port;
	while (1) {
		new_port = SERV_PORT + i;
		
		// bind della nuova socket col nuovo numero di porta
		memset((void *)&new_addr, 0, sizeof(new_addr));
		new_addr.sin_family = AF_INET;
		new_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		new_addr.sin_port = htons(new_port);
		if (bind(new_socket, (struct sockaddr *)&new_addr, sizeof(new_addr)) < 0) {
			if (errno == EADDRINUSE) {
				i++; // riprova incrementando i
			}
			else if (errno != EINTR) {
				error("Error bind");
			}
		}
		else {
			break;
		}
	}
  // set_timeout(new_socket, LONG_TIMEOUT, 0); TODO

  // stampa il nuovo numero di porta
  printf("[%lu] New port number: %u\n\n", tid, new_port);
  fflush(stdout);

  // invio della nuova porta al client
  char new_port_str[11];
  new_port_str[0] = '\0';
  snprintf(new_port_str, sizeof(new_port_str), "%u", new_port);
  printf("[%lu] Sending new port number\n", tid);
  printf("fd: %d\n", sockfd);
  printf("new_socket: %d\n", new_socket);
  send_rel_single(new_socket, client_addr, new_port_str);
  printf("[%lu] New port number sent\n\n", tid);

  // richiesta di connessione ricevuta, invio stringa con comandi
  char welcome_msg[] = "Welcome! Choose one of the following options:\n\t1. LIST all avaiable files\n\t2. GET a file\n\t3. PUT a file in the server\n\t4. Quit.\nType the corresponding number:";
  printf("[%lu] Sending welcome message\n", tid);
  send_rel_single(new_socket, client_addr, welcome_msg);
  printf("[%lu] Welcome message sent\n\n", tid);
  
  while (1) {
    char option[2];
    option[0] = '\0';
    printf("[%lu] Waiting for option\n", tid);
    recv_rel(new_socket, option, sizeof(option), false, NULL, NULL, &client_addr);
    printf("[%lu] Option received: %s\n", tid, option);
    int option_int = atoi(option);

    if (option_int == 1) { // LIST
      char list_command[MAXLINE];
      list_command[0] = '\0';
      ls(list_command);
      send_rel_single(new_socket, client_addr, list_command);
    }

    else if (option_int == 2) { // GET
      // list
      char list_command[MAXLINE];
      list_command[0] = '\0';
      ls(list_command);
      send_rel_single(new_socket, client_addr, list_command);
      // nome del file
      char name[MAXLINE];
      int j = recv_rel(new_socket, name, MAXLINE, false, NULL, NULL, &client_addr);
      name[j] = '\0';
      // invio della dimensione del file e del file
      printf("[%lu] Sending file\n", tid);
      fflush(stdout);
      char *FILENAME = file_path(FILENAME_PATH, name);
      file = fopen(FILENAME, "rb");
      if (file == NULL) {
        error("Error in file opening");
      }
      size_file = file_size(FILENAME);
      printf("size_file: %ld\n", size_file);
      send_rel(new_socket, client_addr, file, size_file);
      fclose(file);
      printf("[%lu] Sending complete\n", tid);
      fflush(stdout);
    }
    
    else if (option_int == 3) { // PUT
      // ricezione del nome del file da salvare
      char name_to_save[MAXLINE];
      int k = recv_rel(new_socket, name_to_save, MAXLINE, false, NULL, NULL, &client_addr);
      name_to_save[k] = '\0';
      // ricezione della grandezza del file da salvare
      char size_rcv_str[11] = {0};
      int j = recv_rel(new_socket, size_rcv_str, sizeof(size_rcv_str), false, NULL, NULL, &client_addr);
      size_rcv_str[j] = '\0';
      uint32_t size_rcv = (uint32_t)strtol(size_rcv_str, NULL, 10);
      uint32_t size_next = ntohl(size_rcv);

      char recvline[size_next + 1];
      // ricezione del file
      int n = recv_rel(new_socket, recvline, size_next, true, NULL, NULL, &client_addr);
      recvline[n] = '\0';        // aggiunge il carattere di terminazione
      char *path_to_save = file_path(FILENAME_PATH, name_to_save);
      FILE *file_to_save = fopen(path_to_save, "wb"); // Apertura del file in modalità binaria
      if (file_to_save == NULL) {
        error("Error in destination file opening");
      }
      // Scrittura dei dati nel file
      if (fwrite(recvline, 1, n, file_to_save) != (size_t) n) {
        error("Error in file writing");
      }
      fclose(file_to_save); // Chiusura del file
      printf("File saved successfully in: %s\n\n", path_to_save);
      fflush(stdout);

    }
    
    else if (option_int == 4) break;

  }
  // Termina correttamente il thread
  printf("Thread terminated\n\n");
  fflush(stdout);

  pthread_exit(NULL);

}



int main(int argc, char **argv) {

  check_args(argc, argv);

  srand(time(NULL)); // inizializzazione seed numeri randomici

  tid = pthread_self();
  printf("[%lu] Main tid\n", tid);

  create_conn();
  
  while (1) {
    seq_num_send = 0;
    seq_num_recv = 0;
    
    printf("Waiting for request...\n");
    fflush(stdout);

    char buff[MAXLINE] = {0};
    buff[0] = '\0';
    int rec = recv_rel(sockfd, buff, MAXLINE, false, &addr, &len, &addr); 
    buff[rec] = '\0';
    printf("Request received: %s\n", buff);
    if (strcmp(buff, "new") == 0) {
      printf("Request received\n");
      fflush(stdout);

      // Avvio di un nuovo thread per gestire la connessione
      pthread_t thread_id;
      thread_data_t thread_data;
      thread_data.addr = addr;
      if (pthread_create(&thread_id, NULL, handle_user, &thread_data) != 0) {
        error("Error creating thread");
      }
      // pthread_detach(thread_id);
    } else {
      printf("Invalid request\n");
      fflush(stdout);
    }
  }
  
  exit(0);
}