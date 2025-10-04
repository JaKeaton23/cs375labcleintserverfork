// server.cpp — Exercise 2 (C++ fork-based server)
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>

#define PORT 8080

void handle_client(int client_sock) {
    char buffer[1024];
    ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        std::cout << "Received: " << buffer << std::endl;
        const char *msg = "Hello from C++ server";
        send(client_sock, msg, std::strlen(msg), 0);
    }
    close(client_sock);
}

int main() {
    // avoid zombies
    signal(SIGCHLD, SIG_IGN);

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(server_sock, 5) < 0) {
        perror("listen"); return 1;
    }

    std::cout << "C++ server listening on " << PORT << "...\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_size = sizeof(client_addr);
        int client_sock = accept(server_sock, (sockaddr*)&client_addr, &addr_size);
        if (client_sock < 0) { perror("accept"); continue; }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(client_sock);
            continue;
        }
        if (pid == 0) {
            // child: handle this client
            close(server_sock);
            handle_client(client_sock);
            _exit(0);
        } else {
            // parent: keep listening
            close(client_sock);
        }
    }
    return 0;
}
