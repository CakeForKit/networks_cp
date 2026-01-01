#include <stdio.h>
#include <stdlib.h>
#include "conf.h"
#include <time.h>

int if_log = 0;

void log_message(const char *message) {
    if (!if_log) 
        return;
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file) {
        time_t now = time(NULL);
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(log_file, "[%s] %s\n", time_buf, message);
        fclose(log_file);
    }
}

void log_request(const char *method, const char *path, int status) {
    if (!if_log) 
        return;
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "%s %s - %d", method, path, status);
    log_message(log_msg);
}

