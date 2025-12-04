#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include "conf.h"
#include "logs.h"
#include "msg.h"
#include "http.h"

typedef struct {
    client_t *clients;
    struct pollfd *fds;
    int num_clients;
    int max_clients;
} worker_data_t;

void worker_process(int server_fd, int worker_id);
int create_server_socket(int port);