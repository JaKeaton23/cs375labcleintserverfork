// server.c — Exercise 5: fork per client + 10s idle timeout using select()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>

#define PORT 8080
#define IDLE_TIMEOUT_SEC 10

static void trim(char *s){
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
}

static void handle_client(int cs) {
    char buf[1024], out[1200];

    for (;;) {
        // Reinitialize fd_set and timeout every loop (select() mutates them)
        fd_set rfds; FD_ZERO(&rfds); FD_SET(cs, &rfds);
        struct timeval tv = { .tv_sec = IDLE_TIMEOUT_SEC, .tv_usec = 0 };

        int ready = select(cs + 1, &rfds, NULL, NULL, &tv);
        if (ready == 0) {
            // Timeout
            const char *msg = "Timeout: no message for 10 seconds. Goodbye.\n";
            (void)send(cs, msg, strlen(msg), 0);
            break;
        } else if (ready < 0) {
            if (errno == EINTR) continue; // interrupted by signal; try again
            perror("select");
            break;
        }

        ssize_t n = recv(cs, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break; // client closed or error
        buf[n] = '\0'; trim(buf);

        if (strcmp(buf, "exit") == 0) break;

        snprintf(out, sizeof(out), "Echo: %s", buf);
        if (send(cs, out, strlen(out), 0) < 0) break;
    }
    close(cs);
}

int main(void) {
    // Reap children automatically (avoid zombies)
    signal(SIGCHLD, SIG_IGN);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); exit(1); }

    int opt = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        // continue; it's safe but restarts may be slower without it
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(s, 5) < 0) { perror("listen"); exit(1); }

    printf("Server (timeout=%ds) listening on %d…\n", IDLE_TIMEOUT_SEC, PORT);

    for (;;) {
        struct sockaddr_in ca; socklen_t alen = sizeof(ca);
        int cs = accept(s, (struct sockaddr*)&ca, &alen);
        if (cs < 0) { perror("accept"); continue; }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(cs);
            continue;
        }
        if (pid == 0) {
            // child: handle this client
            close(s);
            handle_client(cs);
            _exit(0);
        } else {
            // parent: keep accepting others
            close(cs);
        }
    }
}
