#define _POSIX_SOURCE
#include "workers.h"
#include "conf.h"
#include "logs.h"
#include <signal.h>

pid_t worker_pids[MAX_WORKERS];
int worker_count = 0;
volatile sig_atomic_t stop_server = 0;

// void sigint_handler(int sig_numb) {
//     (void) sig_numb;
//     stop_server = 1;
//     exit(EXIT_FAILURE);
// }

void sigint_handler(int sig_numb) {
    (void) sig_numb;
    stop_server = 1;
    printf("\nЗавершение сервера...\n");
    for (int i = 0; i < worker_count; i++) {
        if (worker_pids[i] > 0) {
            printf("Остановка worker PID=%d\n", worker_pids[i]);
            kill(worker_pids[i], SIGTERM);
        }
    }
    sleep(1);  
    for (int i = 0; i < worker_count; i++) {
        if (worker_pids[i] > 0) {
            if (waitpid(worker_pids[i], NULL, WNOHANG) == 0) {
                printf("Принудительная остановка PID=%d\n", worker_pids[i]);
                kill(worker_pids[i], SIGKILL);
            }
        }
    }
    exit(EXIT_SUCCESS);
}

void sigchld_handler(int sig_numb) {
    (void) sig_numb;
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {}
    return;
}

int main(int argc, char *argv[]) {
    int port = PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    printf("Server starting on port %d...\n", port);
    printf("Architecture: prefork + poll()\n");
    printf("Workers: %d\n", MAX_WORKERS);
    printf("Max connections: %d\n", MAX_CONNECTIONS);
    printf("Static files in: ./static/\n");
    log_message("Server starting");

    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        perror("signal"); 
        exit(EXIT_FAILURE);
    }
    if (signal(SIGTERM, sigint_handler) == SIG_ERR) {
        perror("signal"); 
        exit(EXIT_FAILURE);
    }
    if (signal(SIGCHLD, sigchld_handler) == SIG_ERR) {
        perror("signal"); 
        exit(EXIT_FAILURE);
    }
    
    int server_fd = create_server_socket(port);
    signal(SIGPIPE, SIG_IGN);
    
    for (int i = 0; i < MAX_WORKERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            worker_process(server_fd, i);
            exit(0);
        } else if (pid < 0) {
            perror("fork");
            exit(1);
        }
        worker_pids[i] = pid;
        worker_count += 1;
    }
    printf("Server ready! Access at: http://localhost:%d/\n", port);

    while (wait(NULL) > 0);    
    close(server_fd);
    return 0;
}