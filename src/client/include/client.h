#ifndef CLIENT_H
#define CLIENT_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define MAXLINE		    2048
#define MAXVECT       512
#define WINDOW_SIZE   64
#define SERV_PORT	    5193
#define DEST_PATH     "files/client/"

#pragma pack(1)
typedef struct {
  uint32_t seq_num;   // Sequence number of the packet
  uint32_t ack_num;   // Acknowledgment number
  uint16_t data_size; // Size of the data payload
  uint32_t checksum;  // Checksum of the packet
  char data[1]; // Data payload
} udp_packet_t;

#pragma pack(1)
struct temp {
  uint32_t seq_num;   // Sequence number of the packet
  uint32_t ack_num;   // Acknowledgment number
  uint16_t data_size; // Size of the data payload
  uint32_t checksum;  // Checksum of the packet
  char data[1]; // Data payload
};

#define MAX_SIZE_STRUCT sizeof(struct temp) + MAXLINE

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
  struct timeval *timer_start;
  atomic_bool *duplicate_acks;
  atomic_int *new_acks;
} thread_data_t;

// Functions
int bytes_read_funct(char **data, FILE* file, udp_packet_t** packet);
uint32_t calculate_checksum(udp_packet_t *packet);
void check_args(int argc, char *argv[]);
void create_conn(char *ip_address, uint16_t port);
void error(const char *msg);
char* file_path(char *fpath, char *fname);
size_t file_size(char *filename);
uint32_t num_packets(uint32_t size);
void put_option();
int recv_rel(int sock, char *buff, size_t dim, bool size_rcv, struct sockaddr_in *address, socklen_t *addr_length); 
void send_ack(int sockfd, struct sockaddr_in* address, uint32_t ack_num);
void send_rel(int fd, struct sockaddr_in send_addr, FILE* file, size_t size_file);
void *send_rel_receiver_thread(void *arg);
void *send_rel_sender_thread(void *arg);
void send_rel_single(int fd, struct sockaddr_in send_addr, char *data, bool last);
void set_timeout(int sock, int timeout_s, int timeout_us);
int wait_recv(char *buff, long size, int sockfd, struct sockaddr_in *address, socklen_t *addr_length);
int wait_for_input(int count, char* buff_in);

// Variables
char* server_ip;
int timeout_s;
int timeout_us;
double loss_prob;

int sockfd, n;
struct sockaddr_in servaddr;
FILE *file;
char buff_in[2];
int buff_in_int;
FILE *file_to_save;
size_t size_file_to_save;
char buff[MAXLINE];
uint32_t seq_num_send = 0;
uint32_t seq_num_recv = 0;

#endif // CLIENT_H