#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include "conf.h"
#include "logs.h"
#include "msg.h"

typedef struct {
    int fd;
    char buffer[BUFFER_SIZE];
    size_t buffer_len;
    int file_fd;
    off_t file_size;
    off_t bytes_sent;
    int keep_alive;
} client_t;

// void handle_request(client_t *client, const char* request);
void process_client_data(client_t *client);
void send_file_data(client_t *client);
void cleanup_client(client_t *client);
