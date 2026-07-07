#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <time.h>
#include <sys/epoll.h>
#include <errno.h>

#define MAX_EVENTS 1000
#define PORT 8080
#define TIMEOUT_SEC 5

// function to get the correct content type based on file extension
const char* get_mime_type(char *path) {
    char *ext = strrchr(path, '.');
    if (!ext) return "text/plain";
    
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
        return "text/html";
    if (strcmp(ext, ".css") == 0)
        return "text/css";
    if (strcmp(ext, ".js") == 0)
        return "application/javascript";
    if (strcmp(ext, ".png") == 0)
        return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(ext, ".txt") == 0)
        return "text/plain";
    
    return "application/octet-stream";
}

void extract_path_and_method(char *request, char *method, char *path) {
    // copy the method (GET, HEAD, etc.)
    char *first_space = strchr(request, ' ');
    if (!first_space) {
        method[0] = '\0';
        path[0] = '\0';
        return;
    }
    int method_len = first_space - request;
    strncpy(method, request, method_len);
    method[method_len] = '\0';

    // find the path between first and second space
    char *second_space = strchr(first_space + 1, ' ');
    if (!second_space) {
        path[0] = '\0';
        return;
    }
    int path_len = second_space - (first_space + 1);
    strncpy(path, first_space + 1, path_len);
    path[path_len] = '\0';
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void handle_request(int client_fd, char *buffer) {
    char method[16] = {0};
    char path[256] = {0};
    extract_path_and_method(buffer, method, path);

    // log the request with timestamp
    time_t now = time(NULL);
    char *time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0';
    printf("[%s] %s %s\n", time_str, method, path);

    // strip query string (anything after ?)
    char *qmark = strchr(path, '?');
    if (qmark) {
        *qmark = '\0';
    }

    // default to index.html
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }

    // remove the leading "/" to get filename
    char filename[256] = {0};
    strcpy(filename, path + 1);

    // remove the leading "/" to get filename
    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        char response[] = "HTTP/1.1 405 Method Not Allowed\r\n"
                          "Content-Length: 0\r\n"
                          "\r\n";
        send(client_fd, response, strlen(response), 0);
        return;
    }

    // open file using open() instead of fopen()
    int file_fd = open(filename, O_RDONLY);
    if (file_fd == -1) {
        char response[] = "HTTP/1.1 404 Not Found\r\n"
                          "Content-Type: text/html\r\n"
                          "Content-Length: 20\r\n"
                          "\r\n"
                          "<h1>404 Not Found</h1>";
        send(client_fd, response, strlen(response), 0);
        return;
    }

    struct stat file_stat;
    fstat(file_fd, &file_stat);
    long file_size = file_stat.st_size;

    const char *mime = get_mime_type(filename);

    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n", mime, file_size);

    send(client_fd, header, strlen(header), 0);

    if (strcmp(method, "GET") == 0) {
        off_t offset = 0;
        sendfile(client_fd, file_fd, &offset, file_size);
    }

    close(file_fd);
    close(client_fd);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        fprintf(stderr, "socket creation failed\n");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // increase socket buffers for better throughput
    int buffer_size = 262144; // 256KB
    setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
    setsockopt(server_fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        fprintf(stderr, "bind failed\n");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        fprintf(stderr, "listen failed\n");
        return 1;
    }

    set_nonblocking(server_fd);

    printf("Server running on http://localhost:%d\n", PORT);
    printf("Using epoll, TCP_NODELAY, tuned buffers, connection timeout\n");

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        fprintf(stderr, "epoll_create failed\n");
        return 1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        fprintf(stderr, "epoll_ctl failed\n");
        return 1;
    }

    struct epoll_event events[MAX_EVENTS];
    int client_timeout[MAX_EVENTS] = {0}; // track when client connected

    while (1) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, TIMEOUT_SEC * 1000);
        if (num_events == -1) {
            if (errno == EINTR) continue;
            fprintf(stderr, "epoll_wait failed\n");
            break;
        }

        // check for timed-out clients
        time_t now = time(NULL);
        for (int i = 0; i < MAX_EVENTS; i++) {
            if (client_timeout[i] > 0 && (now - client_timeout[i]) > TIMEOUT_SEC) {
                printf("Client timed out: %d\n", i);
                close(i);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, i, NULL);
                client_timeout[i] = 0;
            }
        }

        for (int i = 0; i < num_events; i++) {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                int addrlen = sizeof(address);
                int client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
                if (client_fd == -1) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        fprintf(stderr, "accept failed\n");
                    }
                    continue;
                }

                // TCP_NODELAY: disable Nagle's algorithm for low latency
                int nodelay = 1;
                setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

                // increase socket buffers for client
                setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
                setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));

                set_nonblocking(client_fd);

                ev.events = EPOLLIN;
                ev.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    fprintf(stderr, "epoll_ctl failed for client\n");
                    close(client_fd);
                    continue;
                }

                client_timeout[client_fd] = time(NULL);
                printf("New client connected: %d\n", client_fd);
            } 
            else {
                char buffer[4096] = {0};
                int bytes_read = recv(fd, buffer, 4096, 0);
                
                if (bytes_read <= 0) {
                    printf("Client disconnected: %d\n", fd);
                    close(fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    client_timeout[fd] = 0;
                    continue;
                }

                client_timeout[fd] = time(NULL); // reset timeout
                handle_request(fd, buffer);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                client_timeout[fd] = 0;
                printf("Client served and disconnected: %d\n", fd);
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    return 0;
}