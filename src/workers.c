#include "workers.h"

void worker_process(int server_fd, int worker_id) {
    worker_data_t worker_data;
    worker_data.max_clients = MAX_CONNECTIONS / MAX_WORKERS;
    worker_data.clients = calloc(worker_data.max_clients, sizeof(client_t));
    worker_data.fds = calloc(worker_data.max_clients + 1, sizeof(struct pollfd));
    worker_data.num_clients = 0;
    
    for (int i = 0; i < worker_data.max_clients; i++) {
        worker_data.clients[i].fd = -1;
        worker_data.clients[i].file_fd = -1;
    }
    
    worker_data.fds[0].fd = server_fd;
    worker_data.fds[0].events = POLLIN;
    worker_data.num_clients = 1;
    
    char log_msg[64];
    snprintf(log_msg, sizeof(log_msg), "Worker %d started", worker_id);
    log_message(log_msg);
    
    while (1) {
        int ready = poll(worker_data.fds, worker_data.num_clients, -1);
        if (ready == -1) {
            if (errno == EINTR) {
                log_message("Poll interrupted, continuing");
                continue;
            }
            perror("poll");
            char log_msg[128];
            snprintf(log_msg, sizeof(log_msg), "Poll error: %s", strerror(errno));
            log_message(log_msg);
            break;
        }
        
        if (worker_data.fds[0].revents   & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_fd != -1) {
                int client_index = -1;
                for (int i = 1; i < worker_data.max_clients; i++) {
                    if (worker_data.clients[i].fd == -1) {
                        client_index = i;
                        break;
                    }
                }
                
                if (client_index != -1 && worker_data.num_clients < worker_data.max_clients + 1) {
                    worker_data.clients[client_index].fd = client_fd;
                    worker_data.clients[client_index].buffer_len = 0;
                    worker_data.clients[client_index].file_fd = -1;
                    worker_data.clients[client_index].bytes_sent = 0;
                    worker_data.clients[client_index].keep_alive = 1;
                    
                    worker_data.fds[worker_data.num_clients].fd = client_fd;
                    worker_data.fds[worker_data.num_clients].events = POLLIN;
                    worker_data.num_clients++;
                    
                    char log_msg[128];
                    snprintf(log_msg, sizeof(log_msg), 
                            "Worker %d: New connection accepted (fd: %d, total: %d)", 
                            worker_id, client_fd, worker_data.num_clients - 1);
                    log_message(log_msg);
                } else {
                    close(client_fd);
                    log_message("Connection rejected: client limit reached");
                }
            }
        }
        
        for (int i = 1; i < worker_data.num_clients; i++) {
            if (worker_data.fds[i].revents & (POLLIN | POLLOUT)) {
                int client_index = -1;
                for (int j = 0; j < worker_data.max_clients; j++) {
                    if (worker_data.clients[j].fd == worker_data.fds[i].fd) {
                        client_index = j;
                        break;
                    }
                }
                
                if (client_index == -1) {
                    char error_log[128];
                    snprintf(error_log, sizeof(error_log), 
                            "ERROR: No client found for fd: %d", worker_data.fds[i].fd);
                    log_message(error_log);
                    continue;
                }
                
                client_t *client = &worker_data.clients[client_index];
                char log_msg[128];
                snprintf(log_msg, sizeof(log_msg), 
                        "Processing client fd: %d (client_index: %d, pollfd.revents: %d)", 
                        client->fd, client_index, worker_data.fds[i].revents);
                log_message(log_msg);

                if (worker_data.fds[i].revents & POLLIN) {
                    process_client_data(client);
                }
                
                if (worker_data.fds[i].revents & POLLOUT) {
                    send_file_data(client);
                }
                
                worker_data.fds[i].events = POLLIN;
                if (client->file_fd != -1 && client->bytes_sent < client->file_size) {
                    worker_data.fds[i].events |= POLLOUT;
                }
                
                if (!client->keep_alive) {
                    cleanup_client(client);
                    worker_data.fds[i] = worker_data.fds[worker_data.num_clients - 1];
                    worker_data.num_clients--;
                    i--;
                }
            }
        }
    }
    
    free(worker_data.clients);
    free(worker_data.fds);
    exit(0);
}

int create_server_socket(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(1);
    }
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(1);
    }
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }
    if (listen(server_fd, 128) < 0) {
        perror("listen");
        exit(1);
    }
    
    return server_fd;
}
