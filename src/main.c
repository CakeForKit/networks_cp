#include "workers.h"
#include "conf.h"
#include "logs.h"

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
    }
    printf("Server ready! Access at: http://localhost:%d/\n", port);

    while (wait(NULL) > 0);    
    close(server_fd);
    return 0;
}