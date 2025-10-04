// server.c — Exercise 3
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8080

static void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
}

void handle_client(int client_sock) {
    char buffer[1024];
    for (;;) {
        ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;                // client closed or error
        buffer[n] = '\0';
        trim_newline(buffer);

        if (strcmp(buffer, "exit") == 0)  // client wants to quit
            break;

        char reply[1200];
        snprintf(reply, sizeof(reply), "Echo: %s", buffer);
        send(client_sock, reply, strlen(reply), 0);
    }
    close(client_sock);
}

int main(void) {
    // Avoid zombie processes when children exit
    signal(SIGCHLD, SIG_IGN);

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(server_sock, 5) < 0) {
        perror("listen"); exit(1);
    }

    printf("Server listening on port %d...\n", PORT);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_size);
        if (client_sock < 0) { perror("accept"); continue; }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(client_sock);
            continue;
        }
        if (pid == 0) {
            // child
            close(server_sock); // child doesn't accept new clients
            handle_client(client_sock);
            _exit(0);
        } else {
            // parent
            close(client_sock); // parent hands socket to child and keeps listening
        }
    }
    return 0;
}
