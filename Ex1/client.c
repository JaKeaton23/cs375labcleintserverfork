// client.c — Exercise 3
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8080

static void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
}

int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect"); return 1;
    }

    printf("Connected to 127.0.0.1:%d\n", PORT);
    char sendbuf[1024], recvbuf[1200];

    for (;;) {
        printf("> ");
        fflush(stdout);

        if (!fgets(sendbuf, sizeof(sendbuf), stdin)) break; // EOF/ctrl-D
        trim_newline(sendbuf);
        if (sendbuf[0] == '\0') continue; // ignore empty line

        send(sock, sendbuf, strlen(sendbuf), 0);

        if (strcmp(sendbuf, "exit") == 0) break;

        ssize_t n = recv(sock, recvbuf, sizeof(recvbuf) - 1, 0);
        if (n <= 0) { printf("\nServer closed connection.\n"); break; }
        recvbuf[n] = '\0';
        printf("Server response: %s\n", recvbuf);
    }

    close(sock);
    return 0;
}
