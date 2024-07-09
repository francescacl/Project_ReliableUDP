#ifndef CLIENT_H
#define CLIENT_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <sys/time.h>

#define TIMEOUT 1 // Timeout in seconds
//#define MAX_DATA_SIZE 1024
#define WINDOW_SIZE 5 // Example window size

#define SERV_PORT	5193
#define BACKLOG		10
#define MAXLINE		2048
#define DEST_PATH   "files/client/"
#define MAXVECT 512

typedef struct {
  uint32_t seq_num;   // Sequence number of the packet
  uint32_t ack_num;   // Acknowledgment number
  uint8_t  data[MAXLINE]; // Data payload
  uint16_t data_size; // Size of the data payload
  uint32_t checksum;  // Checksum of the packet
} udp_packet_t;

// Funzioni
void error(const char *msg);
size_t fileSize(char *filename);
char* filePath(char *fpath, char *fname);
int wait_recv(char *buff, long size, int sockfd, struct sockaddr_in *address, socklen_t *addr_length);
void check_args(int argc);
void create_conn(char *ip_address, uint16_t port);
void send_rel(int fd, struct sockaddr_in send_addr, FILE* file, size_t size_file);
void send_rel_single(int fd, struct sockaddr_in send_addr, char *data);
int recv_rel(int sock, char *buffer, size_t dim, bool size_rcv, struct sockaddr_in *address, socklen_t *addr_length); 
int bytes_read_funct(char **data, char* buff, FILE* file, udp_packet_t* packet);
uint32_t calculate_checksum(udp_packet_t *packet);
void set_timeout(int sock, int timeout_s, int timeout_us);
void send_ack(int sockfd, struct sockaddr_in* address, uint32_t ack_num);
void put_option();

// Variabili
int sockfd, n;
struct sockaddr_in servaddr;
FILE *file;
char buff_in[2];
int buff_in_int;
char list_str[MAXLINE];
FILE *file_to_save;
size_t size_file_to_save;
char buff[MAXLINE];
uint32_t seq_num_send = 0;
uint32_t seq_num_recv = 0;

#endif // CLIENT_H