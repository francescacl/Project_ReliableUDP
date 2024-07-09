#ifndef SERVER_H
#define SERVER_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <pthread.h>

#include <time.h>
#include <sys/time.h>
#include <stdbool.h>
#include <math.h>
#include <stdatomic.h>

#define TIMEOUT 1 // Timeout in seconds
#define SERV_PORT	   5193
#define WINDOW_SIZE  10
#define MAXLINE		   2048
#define FILENAME_PATH "files/server/"

typedef struct {
  uint32_t seq_num;   // Sequence number of the packet
  uint32_t ack_num;   // Acknowledgment number
  uint8_t  data[MAXLINE]; // Data payload
  uint16_t data_size; // Size of the data payload
  uint32_t checksum;  // Checksum of the packet
} udp_packet_t;

typedef struct {
  int sockfd;
  struct sockaddr_in addr;
  uint32_t num_packets;
  FILE* file;
  uint32_t seq_num_start;
  uint32_t *base;
  pthread_mutex_t *lock;
  pthread_cond_t *cond;
  atomic_bool *end_thread;
  bool *acked;
} thread_data_t;

// Funzioni
void error(const char *msg);
size_t fileSize(char *filename);
char* filePath(char *fpath, char *fname);
int recv_rel(int sock, char *buffer, size_t dim, bool size_rcv, struct sockaddr_in *address, socklen_t *addr_length, struct sockaddr_in *client_addr);
void send_rel(int fd, struct sockaddr_in send_addr, FILE* file, size_t size_file);
void send_rel_single(int fd, struct sockaddr_in send_addr, char *data);
void create_conn();
int bytes_read_funct(char **data, FILE* file, udp_packet_t* packet);
int wait_recv(char *buff, long size, int , struct sockaddr_in *address, socklen_t *addr_length, struct sockaddr_in *client_addr);
uint32_t calculate_checksum(udp_packet_t *packet);
void set_timeout(int sock, int timeout_s, int timeout_us);
void send_ack(int sockfd, struct sockaddr_in *address, uint32_t ack_num);
uint32_t num_packets(uint32_t size);

// Variabili
int sockfd;
socklen_t len;
struct sockaddr_in addr;

// Variabili threads
__thread uint32_t seq_num_send;
__thread uint32_t seq_num_recv;
__thread pthread_t tid;

#endif // SERVER_H