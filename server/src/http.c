#include "http.h"


void handle_request(client_t *client, const char* request) { 
    char method[16], path[MAX_PATH_LENGTH], protocol[16];
    int is_head = 0;
    if (sscanf(request, "%15s %2047s %15s", method, path, protocol) != 3) {
        char log_msg[16 + MAX_PATH_LENGTH + 16 + 32 + 30];
        snprintf(log_msg, sizeof(log_msg), "Failed to parse HTTP request:\n %s\n", request);
        log_message(log_msg);

        send_error(client->fd, 400, 0);
        return;
    }
    {
        char log_msg[3000];
        snprintf(log_msg, sizeof(log_msg), "Request received: %s %s %s", method, path, protocol);
        log_message(log_msg);
    }
    if (strcmp(method, "GET") == 0) {
        is_head = 0;
    } else if (strcmp(method, "HEAD") == 0) {
        is_head = 1;
    } else {
        send_error(client->fd, 405, 0);
        log_request(method, path, 405);
        return;
    }
    if (!is_safe_path(path)) {
        send_error(client->fd, 403, is_head);
        log_request(method, path, 403);
        return;
    }
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }
    char full_path[MAX_PATH_LENGTH];
    size_t path_len = strlen(path);
    if (path_len + 10 >= sizeof(full_path)) { // +10 для "./static"
        send_error(client->fd, 414, is_head);
        log_request(method, path, 414);
        return;
    }
    if (snprintf(full_path, sizeof(full_path), "./static%s", path) >= (int)sizeof(full_path)) {
        send_error(client->fd, 414, is_head);
        log_request(method, path, 414);
        return;
    }
    // {
    //     char msg[30 + MAX_PATH_LENGTH];
    //     sprintf(msg, "Full_path: %s", full_path);
    //     log_message(msg);
    // }
    
    struct stat file_stat;
    if (stat(full_path, &file_stat) == -1) {
        send_error(client->fd, 404, is_head);
        log_request(method, path, 404);
        return;
    }
    if (S_ISDIR(file_stat.st_mode)) {
        send_error(client->fd, 403, is_head);
        log_request(method, path, 403);
        return;
    }
    if (access(full_path, R_OK) == -1) {
        send_error(client->fd, 403, is_head);
        log_request(method, path, 403);
        return;
    }
    if (file_stat.st_size > MAX_FILE_SIZE_BYTES) {
        send_error(client->fd, 403, is_head);
        log_request(method, path, 403);
        return;
    }
    int file_fd = open(full_path, O_RDONLY);
    if (file_fd == -1) {
        send_error(client->fd, 404, is_head);
        log_request(method, path, 404);
        return;
    }
    
    client->file_fd = file_fd;
    client->file_size = file_stat.st_size;
    client->bytes_sent = 0;
    send_response(client->fd, 200, get_content_type(full_path), file_stat.st_size);
    
    if (is_head) {
        close(file_fd);
        client->file_fd = -1;
    }
    
    log_request(method, path, 200);
    // {
    //     char msg[100 + MAX_PATH_LENGTH];
    //     sprintf(msg, "Serving file: %s (%ld bytes)\n", full_path, file_stat.st_size);
    //     log_message(msg);
    // }
}

void process_client_data(client_t *client) {
    ssize_t bytes_read = recv(client->fd, client->buffer + client->buffer_len, 
                             BUFFER_SIZE - client->buffer_len - 1, 0);
    if (bytes_read <= 0) {
        client->keep_alive = 0;
        return;
    }    
    client->buffer_len += bytes_read;
    client->buffer[client->buffer_len] = '\0';

    char *header_end = strstr(client->buffer, "\r\n\r\n");
    if (header_end) {
        *header_end = '\0';
        handle_request(client, client->buffer);
        
        size_t body_start = (header_end - client->buffer) + 4;
        if (body_start < client->buffer_len) {
            memmove(client->buffer, client->buffer + body_start, 
                   client->buffer_len - body_start);
            client->buffer_len -= body_start;
        } else {
            client->buffer_len = 0;
        }
    }
}

void send_file_data(client_t *client) {
    if (client->file_fd == -1 || client->bytes_sent >= client->file_size) {
        return;
    }
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes_to_send = client->file_size - client->bytes_sent;
    if (bytes_to_send > BUFFER_SIZE) {
        bytes_to_send = BUFFER_SIZE;
    }
    
    ssize_t bytes_read = read(client->file_fd, buffer, bytes_to_send);
    if (bytes_read > 0) {
        ssize_t bytes_sent = send(client->fd, buffer, bytes_read, 0);
        if (bytes_sent > 0) {
            client->bytes_sent += bytes_sent;
        } else {
            client->keep_alive = 0;
        }
    }
    
    if (client->bytes_sent >= client->file_size) {
        close(client->file_fd);
        client->file_fd = -1;
        client->keep_alive = 0;
    }
}

void cleanup_client(client_t *client) {
    if (client->file_fd != -1) {
        close(client->file_fd);
        client->file_fd = -1;
    }
    close(client->fd);
    client->fd = -1;
    client->buffer_len = 0;
}
