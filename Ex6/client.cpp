// client.cpp — simple test client for Exercise 6
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 8080

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    if (connect(sock, (sockaddr*)&server, sizeof(server)) < 0) {
        perror("connect");
        return 1;
    }

    const char *msg = "Hello robust server!";
    send(sock, msg, std::strlen(msg), 0);

    char buf[1024];
    ssize_t n = recv(sock, buf, sizeof(buf)-1, 0);
    if (n > 0) {
        buf[n] = '\0';
        std::cout << "Server response: " << buf << std::endl;
    }

    close(sock);
    return 0;
}
