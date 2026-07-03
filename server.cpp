#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

// simple function to get the file path from the GET request
// example: "GET /index.html HTTP/1.1" -> returns "/index.html"
void extract_path(char *request, char *path) {
    // find the first space (after GET)
    char *first_space = strchr(request, ' ');
    if (!first_space) {
        path[0] = '\0';
        return;
    }
    
    // find the second space (before HTTP/1.1)
    char *second_space = strchr(first_space + 1, ' ');
    if (!second_space) {
        path[0] = '\0';
        return;
    }
    
    // copy everything between the two spaces into path
    int length = second_space - (first_space + 1);
    strncpy(path, first_space + 1, length);
    path[length] = '\0';
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

        char buffer[4096] = {0};
        recv(client_fd, buffer, 4096, 0);

        // extract the file path from the request
        char path[256] = {0};
        extract_path(buffer, path);

        // default to index.html if path is just "/"
        if (strcmp(path, "/") == 0) {
            strcpy(path, "/index.html");
        }

        // remove the leading "/" to get the actual filename
        char filename[256] = {0};
        strcpy(filename, path + 1);

        // try to open the file
        FILE *file = fopen(filename, "rb");
        
        if (file == NULL) {
            // file not found
            char response[] = "HTTP/1.1 404 Not Found\r\n"
                              "Content-Type: text/html\r\n"
                              "Content-Length: 20\r\n"
                              "\r\n"
                              "<h1>404 Not Found</h1>";
            send(client_fd, response, strlen(response), 0);
        } else {
            // move to the end of file to get its size
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            rewind(file);

            // read the whole file into memory
            char *file_content = (char *)malloc(file_size);
            fread(file_content, 1, file_size, file);

            // HTTP response header
            char header[256];
            snprintf(header, sizeof(header), 
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/html\r\n"
                     "Content-Length: %ld\r\n"
                     "\r\n", file_size);

            
            send(client_fd, header, strlen(header), 0);
            send(client_fd, file_content, file_size, 0);

            free(file_content);
            fclose(file);
        }

        close(client_fd);
        printf("client connected, requested: %s\n", path);
    }

    close(server_fd);
    return 0;
}