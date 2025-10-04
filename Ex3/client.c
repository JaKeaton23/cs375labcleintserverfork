#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8080

static void trim(char *s){ size_t n=strlen(s); while(n && (s[n-1]=='\n'||s[n-1]=='\r')) s[--n]='\0'; }

int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET; sa.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

    if (connect(sock, (struct sockaddr*)&sa, sizeof(sa)) < 0) { perror("connect"); return 1; }
    printf("Connected to 127.0.0.1:%d\n", PORT);

    char sendbuf[1024], recvbuf[1200];
    for (;;) {
        printf("> "); fflush(stdout);
        if (!fgets(sendbuf, sizeof(sendbuf), stdin)) break;
        trim(sendbuf);
        if (!sendbuf[0]) continue;

        send(sock, sendbuf, strlen(sendbuf), 0);
        if (!strcmp(sendbuf,"exit")) break;

        ssize_t n = recv(sock, recvbuf, sizeof(recvbuf)-1, 0);
        if (n <= 0) { puts("\nServer closed."); break; }
        recvbuf[n] = '\0';
        printf("Server response: %s\n", recvbuf);
    }
    close(sock);
    return 0;
}
