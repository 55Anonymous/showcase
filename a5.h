#ifndef _A5_H_
#define _A5_H_

#define  _POSIX_C_SOURCE 200809L
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<assert.h>

#include<sys/socket.h>
#include<arpa/inet.h>

#include<pthread.h>
#include<semaphore.h>

#define exit(N) {fflush(stdout); fflush(stderr); _exit(N); }

int a5_pthread_create(pthread_t *restrict thread, const pthread_attr_t *restrict attr, void *(*start_routine)(void *), void *restrict arg);

int recv_http_request(int sockfd, char * buffer, int max_size, int opts);

int send_fully(int sockfd, const char * data, int size, int opts);

int create_server_socket(int port, int nthreads);

void accept_client(int server_socket);

#endif
