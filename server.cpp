#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        printf("socket creation failed\n");
        return 1;
    }

    // reuse port so we don't get "address already in use" error on restart
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

    // HTML response for now
    char response[] = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: 19\r\n"
                      "\r\n"
                      "<h1>Hello World</h1>";

    while (1) {
        int addrlen = sizeof(address);
        int client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        
        if (client_fd < 0) {
            printf("accept failed\n");
            continue;
        }

        // ignore it for now
        char buffer[1024] = {0};
        recv(client_fd, buffer, 1024, 0);

        // hardcoded response
        send(client_fd, response, strlen(response), 0);

        close(client_fd);
        
        printf("client connected and disconnected\n");
    }

    close(server_fd);
    return 0;
}