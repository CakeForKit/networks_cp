#pragma once
#include <fcntl.h>

void send_response(int client_fd, int status_code, const char* content_type, off_t content_length);
void send_error(int client_fd, int status_code, int is_head);
const char* get_content_type(const char* path);
int is_safe_path(const char* path);
