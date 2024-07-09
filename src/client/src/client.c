#include "client.h"



void error(const char *msg) {
  perror(msg);
  exit(1);
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
  checksum ^= packet->seq_num;
  checksum ^= packet->ack_num;
  for (int i = 0; i < packet->data_size; i++) {
    checksum ^= packet->data[i];
  }
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



void check_args(int argc) {
  if (argc != 2) { /* controlla numero degli argomenti */
    printf("argc: %d\n",argc);
    fprintf(stderr, "usage: daytime_clientUDP <server IP address>\n");
    exit(1);
  }
  return;
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



int bytes_read_funct(char **data, char* buff, FILE* file, udp_packet_t* packet) {
  int bytes_read;
  bool is_file = file != NULL;

  // prepare packet to be sent
  packet->seq_num = seq_num_send;
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
  char buff[MAXLINE];
  udp_packet_t packet;
  int bytes_read = bytes_read_funct(&data, buff, NULL, &packet);
  if (bytes_read < 0) {
    error("Error in reading data");
  }

  printf("[send_rel_single] Sending packet %d\n", packet.seq_num);
  while(1) {
    if (sendto(fd, &packet, sizeof(packet), 0, (struct sockaddr *)&send_addr, sizeof(send_addr)) < 0)
      error("Error in sendto");

    // wait for ack
    // set_timeout(fd, 1, 0);
    udp_packet_t ack_packet;
    if (recvfrom(fd, &ack_packet, sizeof(ack_packet), 0, NULL, NULL) < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      error("Error in recvfrom");
    }
    printf("[send_rel_single] Received ack %d\n", ack_packet.ack_num);
    if (ack_packet.ack_num == packet.seq_num) {
      break;
    }
  }
  seq_num_send += 1;
}



void send_rel(int fd, struct sockaddr_in send_addr, FILE* file, size_t size_file) {
  
  //if(is_file) {
  printf("size_file: %ld\n", size_file);
  fflush(stdout);
  uint32_t size = htonl(size_file);
  char size_str[11];
  size_str[0] = '\0';
  snprintf(size_str, sizeof(size_str), "%u", size);
  // udp_packet_t packet_dim;
  // packet_dim.seq_num = seq_num_send;
  // packet_dim.data_size = sizeof(size_str);
  // memset(packet_dim.data, 0, MAXLINE);
  // memcpy(packet_dim.data, size_str, sizeof(size_str));
  // packet_dim.checksum = calculate_checksum(&packet_dim);
  // stampa size_str
  printf("size_str: %s\n", size_str);
  fflush(stdout);
  send_rel_single(fd, send_addr, size_str);
  //}
  
  int bytes_read;
  udp_packet_t packet;

  while ((bytes_read = bytes_read_funct(NULL, buff, file, &packet)) > 0) {
    printf("Sending packet %d\n", packet.seq_num);
    fflush(stdout);
    while(1) {
      if (sendto(fd, &packet, sizeof(packet), 0, (struct sockaddr *)&send_addr, sizeof(send_addr)) < 0)
        error("Error in sendto");

      // wait for ack
      //set_timeout(fd, 1, 0);
      udp_packet_t ack_packet;
      if (recvfrom(fd, &ack_packet, sizeof(ack_packet),  0, NULL, NULL) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          continue;
        }
        error("Error in recvfrom");
      }
      if (ack_packet.ack_num == seq_num_send) {
        printf("Ack received with ack_num %d\n", ack_packet.ack_num);
        seq_num_send += 1;
        break;
      }
    }
  }
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
          continue;
        }
        fprintf(stderr, "Error udp wait_recv: %d\n", errno);
        return -1;
      }
      
      printf("[wait_recv] packet.seq_num: %d\n", packet.seq_num);
      printf("[wait_recv] seq_num_recv: %d\n", seq_num_recv);
      printf("[wait_recv] check %d\n", packet.checksum == calculate_checksum(&packet));
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
      
      k = packet.data_size;
      memcpy(buffer, packet.data, k);

      if (address == NULL) {
        servaddr.sin_port = htons(strtoul(buffer, NULL, 10));
        printf("New port: %d\n", htonl(servaddr.sin_port));
      }
      printf("[recv_rel] packet.seq_num: %d\n", packet.seq_num);
      printf("[recv_rel] seq_num_recv: %d\n", seq_num_recv);
      if (packet.checksum == calculate_checksum(&packet) && packet.seq_num == seq_num_recv) {
        send_ack(sock, &servaddr, seq_num_recv);
        seq_num_recv += 1;
        printf("[recv_rel] seq_num_recv++: %d\n", seq_num_recv);
        break;
      } else {
        send_ack(sock, &servaddr, seq_num_recv-1);
      }
    }
  } else {
    k = wait_recv(buffer, dim, sock, address, addr_length);
  }
  return k;
}



void list_option(socklen_t servaddr_len, int option, char **name) {
  
  // list_str[0] = '\0';
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
    while(1) { // scelta opzione da terminale
      __fpurge(stdin);
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
  while(1) { // scelta opzione da terminale
    __fpurge(stdin);
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

  check_args(argc);

  ////////// New connection //////////

  create_conn(argv[1], htons(SERV_PORT));
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

    while(1) { // scelta opzione da terminale
      __fpurge(stdin);
      buff_in[0] = getchar();
      buff_in[1] = '\0';
      buff_in_int = atoi(buff_in);
      if (buff_in_int != 1 && buff_in_int != 2 && buff_in_int != 3 && buff_in_int != 4) {
        printf("Il numero inserito non è valido. Riprovare:\n");
        fflush(stdout);
        continue;
      }
      break;
    }
    // manda la scelta del client al server
    send_rel_single(sockfd, servaddr, buff_in);

    ////////// Manage choice ///////////

    if(buff_in_int == 1) {
      list_option(servaddr_len, 0, NULL); // 0: list option
    } else if (buff_in_int == 2) {
      get_option(servaddr_len);
    } else if (buff_in_int == 3) {
      put_option(servaddr_len);
    } else if(buff_in_int == 4) {
      break;
    } else error("Error in switch case / buff_in\n");

    // ask user to press enter
    printf("Press enter to continue\n");
    __fpurge(stdin);
    getchar();
  }

  close(sockfd); // Chiusura della socket

  exit(0);

}