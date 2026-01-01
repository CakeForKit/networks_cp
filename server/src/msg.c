#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include "conf.h"
#include "logs.h"

const char* get_status_message(int status_code) {
    switch(status_code) {
        case 200: return "OK";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 414: return "URI Too Long";
        case 400: return "Bad Request";
        default: return "Unknown";
    }
}

void send_response(int client_fd, int status_code, const char* content_type, off_t content_length) {
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Server: Static File Server\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, get_status_message(status_code),
        content_type, (long)content_length);
    
    send(client_fd, header, header_len, 0);
}

void send_error(int client_fd, int status_code, int is_head) {
    char body[512];
    const char* status_msg = get_status_message(status_code);
    
    int body_len = snprintf(body, sizeof(body),
        "<html><head><title>%d %s</title></head>"
        "<body><h1>%d %s</h1></body></html>",
        status_code, status_msg, status_code, status_msg);
    
    send_response(client_fd, status_code, "text/html", body_len);
    
    if (!is_head) {
        send(client_fd, body, body_len, 0);
    }
    {
        char log_msg[128];
        snprintf(log_msg, sizeof(log_msg), "Error response sent: %d %s", status_code, status_msg);
        log_message(log_msg);
    }
}

const char* get_content_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
        return "text/html";
    else if (strcmp(ext, ".css") == 0)
        return "text/css";
    else if (strcmp(ext, ".js") == 0)
        return "application/javascript";
    else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    else if (strcmp(ext, ".png") == 0)
        return "image/png";
    else if (strcmp(ext, ".gif") == 0)
        return "image/gif";
    else if (strcmp(ext, ".txt") == 0)
        return "text/plain";
    else
        return "application/octet-stream";
}

int is_safe_path(const char* path) {
    return strstr(path, "..") == NULL;
}

