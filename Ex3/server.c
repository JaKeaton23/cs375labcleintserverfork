#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8080

static void trim(char *s){ size_t n=strlen(s); while(n && (s[n-1]=='\n'||s[n-1]=='\r')) s[--n]='\0'; }

static void handle_client(int cs) {
    char buf[1024], out[1200];
    for (;;) {
        ssize_t n = recv(cs, buf, sizeof(buf)-1, 0);
        if (n <= 0) break;         // disconnect/error
        buf[n] = '\0'; trim(buf);
        if (!strcmp(buf,"exit")) break;
        snprintf(out, sizeof(out), "Echo: %s", buf);
        send(cs, out, strlen(out), 0);
    }
    close(cs);
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);                // avoid zombies

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); exit(1); }

    int opt=1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(s, 5) < 0) { perror("listen"); exit(1); }

    printf("Server listening on %d …\n", PORT);

    for (;;) {
        struct sockaddr_in ca; socklen_t alen = sizeof(ca);
        int cs = accept(s, (struct sockaddr*)&ca, &alen);
        if (cs < 0) { perror("accept"); continue; }

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); close(cs); continue; }
        if (pid == 0) {              // child
            close(s);
            handle_client(cs);
            _exit(0);
        } else {                     // parent
            close(cs);
        }
    }
}
