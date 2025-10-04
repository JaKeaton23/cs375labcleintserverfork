// client.c — interactive chat client (Exercise 7 uses same client)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define PORT 8080

static void trim(char *s){ size_t n=strlen(s); while(n && (s[n-1]=='\n'||s[n-1]=='\r')) s[--n]='\0'; }

int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET; sa.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

    if (connect(sock, (struct sockaddr*)&sa, sizeof(sa)) < 0) { perror("connect"); return 1; }

    printf("Connected. Type messages; 'exit' to quit.\n");

    // Use select() to read from both stdin and socket so we can display broadcasts
    for (;;) {
        fd_set rfds; FD_ZERO(&rfds);
        FD_SET(0, &rfds);           // stdin
        FD_SET(sock, &rfds);        // server
        int maxfd = sock > 0 ? sock : 0;

        if (select(maxfd+1, &rfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // Incoming broadcast from server?
        if (FD_ISSET(sock, &rfds)) {
            char buf[1200];
            ssize_t n = recv(sock, buf, sizeof(buf)-1, 0);
            if (n <= 0) { puts("Disconnected."); break; }
            buf[n] = '\0';
            fputs(buf, stdout);
        }

        // User typed something?
        if (FD_ISSET(0, &rfds)) {
            char line[1024];
            if (!fgets(line, sizeof(line), stdin)) { // EOF
                break;
            }
            trim(line);
            if (line[0] == '\0') continue;
            if (!strcmp(line, "exit")) {
                send(sock, line, strlen(line), 0);
                break;
            }
            send(sock, line, strlen(line), 0);
        }
    }

    close(sock);
    return 0;
}
