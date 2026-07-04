#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

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

typedef struct {
    int client_fd;
} client_info;

void* handle_client(void* arg) {
    client_info *info = (client_info*)arg;
    int client_fd = info->client_fd;
    free(info);

    int keep_alive = 1;
    char buffer[4096] = {0};

    while (keep_alive) {
        // clear the buffer for the next request
        memset(buffer, 0, 4096);
        
        int bytes_read = recv(client_fd, buffer, 4096, 0);
        if (bytes_read <= 0) {
            break; // client disconnected
        }

        // check if the client wants to close the connection
        if (strstr(buffer, "Connection: close") != NULL) {
            keep_alive = 0;
        }

        char method[16] = {0};
        char path[256] = {0};
        extract_path_and_method(buffer, method, path);

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
            if (!keep_alive) break;
            continue;
        }

        FILE *file = fopen(filename, "rb");
        
        if (file == NULL) {
            char response[] = "HTTP/1.1 404 Not Found\r\n"
                              "Content-Type: text/html\r\n"
                              "Content-Length: 20\r\n"
                              "\r\n"
                              "<h1>404 Not Found</h1>";
            send(client_fd, response, strlen(response), 0);
        } else {
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            rewind(file);

            // get the correct mime type
            const char *mime = get_mime_type(filename);
            // build header
            char header[512];
            snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %ld\r\n"
                     "Connection: %s\r\n"
                     "\r\n", mime, file_size, keep_alive ? "keep-alive" : "close");

            send(client_fd, header, strlen(header), 0);

            if (strcmp(method, "GET") == 0) {
                char *file_content = (char *)malloc(file_size);
                fread(file_content, 1, file_size, file);
                send(client_fd, file_content, file_size, 0);
                free(file_content);
            }

            fclose(file);
        }

        // if client requested close, break the loop
        if (!keep_alive) {
            break;
        }
    }

    close(client_fd);
    printf("client disconnected\n");
    return NULL;
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        printf("socket creation failed\n");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        printf("bind failed\n");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        printf("listen failed\n");
        return 1;
    }

    printf("Server running on http://localhost:8080\n");

    while (1) {
        int addrlen = sizeof(address);
        int client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        
        if (client_fd < 0) {
            printf("accept failed\n");
            continue;
        }

        client_info *info = (client_info*)malloc(sizeof(client_info));
        info->client_fd = client_fd;

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, info);
        pthread_detach(thread);
    }

    close(server_fd);
    return 0;
}