#ifndef SERVER_H
#define SERVER_H

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


#define LONG_TIMEOUT  60 // TODO
#define MAXLINE		    2048 //32768
#define SERV_PORT	    5193
#define WINDOW_SIZE   64
#define FILENAME_PATH "files/server/"
#define MSS           2048

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


typedef struct {
    udp_packet_t **packets;
    bool *acked;
    uint32_t size;
    uint32_t capacity;
} dynamic_array_t;


// Funzioni
int bytes_read_funct(char **data, FILE* file, udp_packet_t** packet);
uint32_t calculate_checksum(udp_packet_t *packet);
void check_args(int argc, char *argv[]);
void create_conn();
void error(const char *msg);
char* file_path(char *fpath, char *fname);
size_t file_size(char *filename);
uint32_t num_packets(uint32_t size);
int recv_rel(int sock, char *buffer, size_t dim, bool size_rcv, struct sockaddr_in *address, socklen_t *addr_length, struct sockaddr_in *client_addr);
void send_ack(int sockfd, struct sockaddr_in *address, uint32_t ack_num);
void send_rel(int fd, struct sockaddr_in send_addr, FILE* file, size_t size_file);
void *send_rel_receiver_thread(void *arg);
void *send_rel_sender_thread(void *arg);
void send_rel_single(int fd, struct sockaddr_in send_addr, char *data);
void set_timeout(int sock, int timeout_s, int timeout_us);
int wait_recv(char *buff, long size, int , struct sockaddr_in *address, socklen_t *addr_length, struct sockaddr_in *client_addr);

// Variabili
int timeout_s;
int timeout_us;
double loss_prob;
int sockfd;
socklen_t len;
struct sockaddr_in addr;

// Variabili threads
__thread uint32_t seq_num_send;
__thread uint32_t seq_num_recv;
__thread pthread_t tid;

#endif // SERVER_H