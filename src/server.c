#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>

#define MAX_CONNECTIONS 1024
#define MAX_WORKERS 4
#define BUFFER_SIZE 8192
#define MAX_PATH_LENGTH 2048
#define LOG_FILE "./logs/server.log"

typedef struct {
    int fd;
    char buffer[BUFFER_SIZE];
    size_t buffer_len;
    int file_fd;
    off_t file_size;
    off_t bytes_sent;
    int keep_alive;
} client_t;

typedef struct {
    client_t *clients;
    struct pollfd *fds;
    int num_clients;
    int max_clients;
} worker_data_t;

// Создание необходимых директорий
void create_directories() {
    mkdir("logs", 0755);
    mkdir("static", 0755);
}

// Функции для логирования
void log_message(const char *message) {
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
    char log_msg[256];
    snprintf(log_msg, sizeof(log_msg), "%s %s - %d", method, path, status);
    log_message(log_msg);
}

// Функции для работы с HTTP
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

void send_response(int client_fd, int status_code, const char* content_type, off_t content_length, int is_head) {
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
    
    send_response(client_fd, status_code, "text/html", body_len, is_head);
    
    if (!is_head) {
        send(client_fd, body, body_len, 0);
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
    // Защита от path traversal атак
    return strstr(path, "..") == NULL;
}

void handle_request(client_t *client, const char* request) {
    printf("DEBUG: Handling request: %s\n", request);
    
    char method[16], path[MAX_PATH_LENGTH], protocol[16];
    int is_head = 0;
    
    // Парсинг HTTP запроса
    if (sscanf(request, "%15s %2047s %15s", method, path, protocol) != 3) {
        printf("DEBUG: Failed to parse request\n");
        send_error(client->fd, 400, 0);
        return;
    }
    
    printf("DEBUG: Parsed: method=%s, path=%s, protocol=%s\n", method, path, protocol);
    
    // Проверка поддерживаемых методов
    if (strcmp(method, "GET") == 0) {
        is_head = 0;
    } else if (strcmp(method, "HEAD") == 0) {
        is_head = 1;
    } else {
        send_error(client->fd, 405, 0);
        log_request(method, path, 405);
        return;
    }
    
    // Безопасность пути
    if (!is_safe_path(path)) {
        send_error(client->fd, 403, is_head);
        log_request(method, path, 403);
        return;
    }
    
    // Корневой путь - отдаем index.html
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }
    
    // Формирование полного пути к файлу - ИСПРАВЛЕНО: ищем в static/
    char full_path[MAX_PATH_LENGTH];
    size_t path_len = strlen(path);
    
    // Проверка что путь не слишком длинный
    if (path_len + 10 >= sizeof(full_path)) { // +10 для "./static"
        send_error(client->fd, 414, is_head);
        log_request(method, path, 414);
        return;
    }
    
    // ИСПРАВЛЕНИЕ: ищем файлы в папке static/
    if (snprintf(full_path, sizeof(full_path), "./static%s", path) >= (int)sizeof(full_path)) {
        send_error(client->fd, 414, is_head);
        log_request(method, path, 414);
        return;
    }
    
    printf("Looking for file: %s\n", full_path); // Debug output
    
    // Проверка существования файла и прав доступа
    struct stat file_stat;
    if (stat(full_path, &file_stat) == -1) {
        printf("File not found: %s (errno: %d)\n", full_path, errno); // Debug
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
    
    // Проверка размера файла (до 128 МБ)
    if (file_stat.st_size > 128 * 1024 * 1024) {
        send_error(client->fd, 403, is_head);
        log_request(method, path, 403);
        return;
    }
    
    // Открытие файла
    int file_fd = open(full_path, O_RDONLY);
    if (file_fd == -1) {
        send_error(client->fd, 404, is_head);
        log_request(method, path, 404);
        return;
    }
    
    // Подготовка клиента для отправки файла
    client->file_fd = file_fd;
    client->file_size = file_stat.st_size;
    client->bytes_sent = 0;
    
    // Отправка заголовков
    send_response(client->fd, 200, get_content_type(full_path), file_stat.st_size, is_head);
    
    if (is_head) {
        close(file_fd);
        client->file_fd = -1;
    }
    
    log_request(method, path, 200);
    printf("Serving file: %s (%ld bytes)\n", full_path, file_stat.st_size); // Debug
}

// Остальные функции без изменений...
void process_client_data(client_t *client) {
    printf("DEBUG: Reading data from client fd: %d\n", client->fd);
    
    ssize_t bytes_read = recv(client->fd, client->buffer + client->buffer_len, 
                             BUFFER_SIZE - client->buffer_len - 1, 0);
    
    printf("DEBUG: recv returned: %zd\n", bytes_read);
    
    if (bytes_read <= 0) {
        if (bytes_read == 0) {
            printf("DEBUG: Client disconnected\n");
        } else {
            printf("DEBUG: recv error: %s\n", strerror(errno));
        }
        client->keep_alive = 0;
        return;
    }
    
    client->buffer_len += bytes_read;
    client->buffer[client->buffer_len] = '\0';
    
    printf("DEBUG: Received %zd bytes: %.*s\n", bytes_read, (int)bytes_read, client->buffer);
    
    // Поиск конца заголовков
    char *header_end = strstr(client->buffer, "\r\n\r\n");
    if (header_end) {
        printf("DEBUG: Found end of headers\n");
        *header_end = '\0';
        handle_request(client, client->buffer);
        
        // Очистка буфера для следующего запроса
        size_t body_start = (header_end - client->buffer) + 4;
        if (body_start < client->buffer_len) {
            memmove(client->buffer, client->buffer + body_start, 
                   client->buffer_len - body_start);
            client->buffer_len -= body_start;
        } else {
            client->buffer_len = 0;
        }
    } else {
        printf("DEBUG: Incomplete headers, waiting for more data\n");
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
    printf("Worker %d started\n", worker_id);
    
    while (1) {
        int ready = poll(worker_data.fds, worker_data.num_clients, -1);
        
        if (ready == -1) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        
        if (worker_data.fds[0].revents & POLLIN) {
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
                    
                    printf("Worker %d: New connection (fd: %d)\n", worker_id, client_fd);
                } else {
                    close(client_fd);
                }
            }
        }
        
        for (int i = 1; i < worker_data.num_clients; i++) {
            if (worker_data.fds[i].revents & (POLLIN | POLLOUT)) {
                // Находим соответствующий клиентский слот
                int client_index = -1;
                for (int j = 0; j < worker_data.max_clients; j++) {
                    if (worker_data.clients[j].fd == worker_data.fds[i].fd) {
                        client_index = j;
                        break;
                    }
                }
                
                if (client_index == -1) {
                    printf("ERROR: No client found for fd: %d\n", worker_data.fds[i].fd);
                    continue;
                }
                
                client_t *client = &worker_data.clients[client_index];
                
                printf("DEBUG: Processing client fd: %d (index: %d)\n", client->fd, client_index);
                
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

void create_static_content() {
    // Создание директорий
    create_directories();
    
    // index.html (без изменений)
    FILE *html_file = fopen("static/index.html", "w");
    if (html_file) {
        const char *html_content = 
            "<!DOCTYPE html>\n"
            "<html lang=\"ru\">\n"
            "<head>\n"
            "    <meta charset=\"UTF-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <title>Статический файловый сервер</title>\n"
            "    <link rel=\"stylesheet\" href=\"style.css\">\n"
            "</head>\n"
            "<body>\n"
            "    <div class=\"container\">\n"
            "        <header>\n"
            "            <h1>Добро пожаловать на файловый сервер</h1>\n"
            "            <p>Architecture: prefork + poll()</p>\n"
            "        </header>\n"
            "        \n"
            "        <main>\n"
            "            <section class=\"features\">\n"
            "                <h2>Возможности сервера</h2>\n"
            "                <ul>\n"
            "                    <li>Поддержка HTTP методов GET и HEAD</li>\n"
            "                    <li>Отдача файлов до 128 МБ</li>\n"
            "                    <li>Мультиплексирование с помощью poll()</li>\n"
            "                    <li>Prefork архитектура</li>\n"
            "                    <li>Защита от path traversal атак</li>\n"
            "                    <li>Логирование всех запросов</li>\n"
            "                </ul>\n"
            "            </section>\n"
            "            \n"
            "            <section class=\"demo\">\n"
            "                <h2>Демонстрация работы</h2>\n"
            "                <div class=\"demo-grid\">\n"
            "                    <div class=\"demo-item\">\n"
            "                        <h3>Статус сервера</h3>\n"
            "                        <div class=\"status online\">Online</div>\n"
            "                    </div>\n"
            "                    <div class=\"demo-item\">\n"
            "                        <h3>Тестовые файлы</h3>\n"
            "                        <ul>\n"
            "                            <li><a href=\"/test.txt\">test.txt</a></li>\n"
            "                            <li><a href=\"/image.jpg\">image.jpg</a></li>\n"
            "                        </ul>\n"
            "                    </div>\n"
            "                </div>\n"
            "            </section>\n"
            "        </main>\n"
            "        \n"
            "        <footer>\n"
            "            <p>Статический файловый сервер &copy; 2024</p>\n"
            "        </footer>\n"
            "    </div>\n"
            "</body>\n"
            "</html>";
        fwrite(html_content, 1, strlen(html_content), html_file);
        fclose(html_file);
        printf("Created static/index.html\n");
    }
    
    // style.css (без изменений)
    FILE *css_file = fopen("static/style.css", "w");
    if (css_file) {
        const char *css_content = 
            "* {\n"
            "    margin: 0;\n"
            "    padding: 0;\n"
            "    box-sizing: border-box;\n"
            "}\n"
            "\n"
            "body {\n"
            "    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;\n"
            "    line-height: 1.6;\n"
            "    color: #333;\n"
            "    background: linear-gradient(135deg, #667eea 0%%, #764ba2 100%%);\n"
            "    min-height: 100vh;\n"
            "}\n"
            "\n"
            ".container {\n"
            "    max-width: 1200px;\n"
            "    margin: 0 auto;\n"
            "    padding: 20px;\n"
            "}\n"
            "\n"
            "header {\n"
            "    text-align: center;\n"
            "    background: rgba(255, 255, 255, 0.95);\n"
            "    padding: 40px;\n"
            "    border-radius: 15px;\n"
            "    margin-bottom: 30px;\n"
            "    box-shadow: 0 10px 30px rgba(0, 0, 0, 0.1);\n"
            "}\n"
            "\n"
            "header h1 {\n"
            "    color: #2c3e50;\n"
            "    font-size: 2.5em;\n"
            "    margin-bottom: 10px;\n"
            "}\n"
            "\n"
            "header p {\n"
            "    color: #7f8c8d;\n"
            "    font-size: 1.2em;\n"
            "}\n"
            "\n"
            "main {\n"
            "    display: grid;\n"
            "    gap: 30px;\n"
            "}\n"
            "\n"
            "section {\n"
            "    background: rgba(255, 255, 255, 0.95);\n"
            "    padding: 30px;\n"
            "    border-radius: 15px;\n"
            "    box-shadow: 0 10px 30px rgba(0, 0, 0, 0.1);\n"
            "}\n"
            "\n"
            "h2 {\n"
            "    color: #2c3e50;\n"
            "    margin-bottom: 20px;\n"
            "    border-bottom: 2px solid #3498db;\n"
            "    padding-bottom: 10px;\n"
            "}\n"
            "\n"
            "h3 {\n"
            "    color: #34495e;\n"
            "    margin-bottom: 15px;\n"
            "}\n"
            "\n"
            "ul {\n"
            "    list-style-position: inside;\n"
            "}\n"
            "\n"
            "li {\n"
            "    margin-bottom: 8px;\n"
            "    padding-left: 10px;\n"
            "}\n"
            "\n"
            ".features li {\n"
            "    background: url('data:image/svg+xml,<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"#3498db\"><path d=\"M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z\"/></svg>') no-repeat left center;\n"
            "    padding-left: 30px;\n"
            "}\n"
            "\n"
            ".demo-grid {\n"
            "    display: grid;\n"
            "    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));\n"
            "    gap: 20px;\n"
            "}\n"
            "\n"
            ".demo-item {\n"
            "    background: #f8f9fa;\n"
            "    padding: 20px;\n"
            "    border-radius: 10px;\n"
            "    border-left: 4px solid #3498db;\n"
            "}\n"
            "\n"
            ".status {\n"
            "    display: inline-block;\n"
            "    padding: 8px 16px;\n"
            "    border-radius: 20px;\n"
            "    font-weight: bold;\n"
            "    text-transform: uppercase;\n"
            "    font-size: 0.9em;\n"
            "}\n"
            "\n"
            ".online {\n"
            "    background: #d4edda;\n"
            "    color: #155724;\n"
            "}\n"
            "\n"
            "a {\n"
            "    color: #3498db;\n"
            "    text-decoration: none;\n"
            "    transition: color 0.3s;\n"
            "}\n"
            "\n"
            "a:hover {\n"
            "    color: #2980b9;\n"
            "    text-decoration: underline;\n"
            "}\n"
            "\n"
            "footer {\n"
            "    text-align: center;\n"
            "    margin-top: 40px;\n"
            "    color: rgba(255, 255, 255, 0.8);\n"
            "    font-size: 0.9em;\n"
            "}\n"
            "\n"
            "@media (max-width: 768px) {\n"
            "    .container {\n"
            "        padding: 10px;\n"
            "    }\n"
            "    \n"
            "    header h1 {\n"
            "        font-size: 2em;\n"
            "    }\n"
            "    \n"
            "    section {\n"
            "        padding: 20px;\n"
            "    }\n"
            "}";
        fwrite(css_content, 1, strlen(css_content), css_file);
        fclose(css_file);
        printf("Created static/style.css\n");
    }
    
    // test.txt
    FILE *test_file = fopen("static/test.txt", "w");
    if (test_file) {
        const char *test_content = 
            "Это тестовый текстовый файл.\n"
            "Сервер успешно работает и отдает статический контент!\n";
        fwrite(test_content, 1, strlen(test_content), test_file);
        fclose(test_file);
        printf("Created static/test.txt\n");
    }
}

int main(int argc, char *argv[]) {
    int port = 8080;
    
    if (argc > 1) {
        port = atoi(argv[1]);
    }
    
    // Создание статического контента
    create_static_content();
    
    int server_fd = create_server_socket(port);
    
    printf("Server starting on port %d...\n", port);
    printf("Architecture: prefork + poll()\n");
    printf("Workers: %d\n", MAX_WORKERS);
    printf("Max connections: %d\n", MAX_CONNECTIONS);
    printf("Static files in: ./static/\n");
    
    log_message("Server starting");
    
    // Игнорирование SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    
    // Prefork: создание worker процессов
    for (int i = 0; i < MAX_WORKERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // Worker process
            worker_process(server_fd, i);
            exit(0);
        } else if (pid < 0) {
            perror("fork");
            exit(1);
        }
    }
    
    printf("Server ready! Access at: http://localhost:%d/\n", port);
    
    // Ожидание завершения worker процессов
    while (wait(NULL) > 0);
    
    close(server_fd);
    return 0;
}