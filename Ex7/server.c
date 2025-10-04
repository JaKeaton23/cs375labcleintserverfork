// server.c — Exercise 7 (C): Forked broadcast chat
// Parent accepts clients and keeps ALL client sockets open.
// For each client, we create a pipe and fork a child.
//   - Child reads from its client socket; when it gets a line,
//     it writes the message to the parent through the pipe.
//   - Parent select()s on all child-pipe read-ends; when data arrives,
//     it broadcasts to every other client socket.
//
// Build: gcc -Wall -Wextra -O2 server.c -o server
// Run:   ./server  (then run multiple ./client)

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
#define MAX_CLIENTS  FD_SETSIZE       // keep it simple; plenty for this lab
#define MAX_MSG      1024

// Header sent from child -> parent before each message payload
typedef struct {
    int sender_fd;   // child's client socket fd (as seen in parent too)
    int len;         // payload length in bytes (no NUL)
} msg_hdr_t;

static void trim(char *s){
    size_t n = strlen(s);
    while (n && (s[n-1]=='\n' || s[n-1]=='\r')) s[--n] = '\0';
}

static ssize_t read_full(int fd, void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, (char*)buf + off, n - off);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return (ssize_t)off; // EOF
        off += (size_t)r;
    }
    return (ssize_t)off;
}

static ssize_t write_full(int fd, const void *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, (const char*)buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return (ssize_t)off;
}

// Child: read from client socket -> send to parent via pipe
static void child_loop(int client_fd, int pipe_write_fd) {
    char buf[MAX_MSG];

    // Greet
    const char *g = "Welcome! Type messages; 'exit' to quit.\n";
    (void)send(client_fd, g, strlen(g), 0);

    for (;;) {
        ssize_t n = recv(client_fd, buf, sizeof(buf)-1, 0);
        if (n <= 0) break; // client closed or error
        buf[n] = '\0';
        trim(buf);
        if (buf[0] == '\0') continue;

        if (strcmp(buf, "exit") == 0) {
            break;
        }

        // build header + payload for parent
        msg_hdr_t hdr;
        hdr.sender_fd = client_fd;
        hdr.len = (int)strlen(buf);

        if (write_full(pipe_write_fd, &hdr, sizeof(hdr)) < 0) break;
        if (write_full(pipe_write_fd, buf, (size_t)hdr.len) < 0) break;
    }

    close(client_fd);
    close(pipe_write_fd);
    _exit(0);
}

int main(void) {
    // Reap children automatically; avoid zombies
    signal(SIGCHLD, SIG_IGN);

    // Listening socket
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(s, 16) < 0) { perror("listen"); exit(1); }

    printf("Broadcast server listening on %d …\n", PORT);

    // Parent's book-keeping
    int client_fds[MAX_CLIENTS];   // sockets open in the parent (for broadcasting)
    int pipe_fds[MAX_CLIENTS];     // read-ends of pipes from children
    pid_t child_pids[MAX_CLIENTS];
    int count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_fds[i] = -1;
        pipe_fds[i] = -1;
        child_pids[i] = -1;
    }

    for (;;) {
        // Build fdset for select()
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        int maxfd = s;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (pipe_fds[i] != -1) {
                FD_SET(pipe_fds[i], &rfds);
                if (pipe_fds[i] > maxfd) maxfd = pipe_fds[i];
            }
        }

        int ready = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            continue;
        }

        // New connection?
        if (FD_ISSET(s, &rfds)) {
            struct sockaddr_in ca; socklen_t alen = sizeof(ca);
            int cs = accept(s, (struct sockaddr*)&ca, &alen);
            if (cs < 0) { perror("accept"); continue; }

            // find a slot
            int slot = -1;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_fds[i] == -1) { slot = i; break; }
            }
            if (slot == -1) {
                const char *full = "Server full. Try later.\n";
                send(cs, full, strlen(full), 0);
                close(cs);
            } else {
                int pfd[2];
                if (pipe(pfd) < 0) {
                    perror("pipe");
                    close(cs);
                    continue;
                }

                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork");
                    close(cs);
                    close(pfd[0]); close(pfd[1]);
                    continue;
                }
                if (pid == 0) {
                    // child
                    close(s);
                    close(pfd[0]); // close read end
                    // child keeps client socket open
                    child_loop(cs, pfd[1]); // never returns
                } else {
                    // parent
                    count++;
                    client_fds[slot] = cs;   // keep client's socket for broadcasting
                    pipe_fds[slot] = pfd[0]; // read-end from this child
                    child_pids[slot] = pid;
                    close(pfd[1]);           // parent closes write end

                    char hello[128];
                    snprintf(hello, sizeof(hello), "You are client #%d. %d user(s) connected.\n",
                             slot, count);
                    send(cs, hello, strlen(hello), 0);

                    // Announce to others
                    char joinmsg[128];
                    snprintf(joinmsg, sizeof(joinmsg), "Client #%d joined. Active: %d\n", slot, count);
                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (client_fds[i] != -1 && i != slot) {
                            send(client_fds[i], joinmsg, strlen(joinmsg), 0);
                        }
                    }
                }
            }
        }

        // Messages from children?
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int rfd = pipe_fds[i];
            if (rfd == -1) continue;
            if (!FD_ISSET(rfd, &rfds)) continue;

            msg_hdr_t hdr;
            ssize_t h = read_full(rfd, &hdr, sizeof(hdr));
            if (h == 0) {
                // Child closed pipe -> client disconnected
                if (client_fds[i] != -1) {
                    close(client_fds[i]);
                    client_fds[i] = -1;
                    count--;
                    char leave[128];
                    snprintf(leave, sizeof(leave), "Client #%d left. Active: %d\n", i, count);
                    for (int k = 0; k < MAX_CLIENTS; k++) {
                        if (client_fds[k] != -1) send(client_fds[k], leave, strlen(leave), 0);
                    }
                }
                close(rfd);
                pipe_fds[i] = -1;
                continue;
            } else if (h < 0) {
                perror("read header");
                continue;
            } else if (h != (ssize_t)sizeof(hdr)) {
                // partial header (shouldn't happen with read_full)
                continue;
            }

            if (hdr.len <= 0 || hdr.len > MAX_MSG-1) {
                // bad length; drain/ignore
                continue;
            }

            char msg[MAX_MSG];
            ssize_t m = read_full(rfd, msg, (size_t)hdr.len);
            if (m != hdr.len) continue;
            msg[hdr.len] = '\0';

            // Broadcast to everyone except the sender
            char out[MAX_MSG + 64];
            int n = snprintf(out, sizeof(out), "Client #%d: %s\n", i, msg);
            for (int k = 0; k < MAX_CLIENTS; k++) {
                if (client_fds[k] != -1 && client_fds[k] != hdr.sender_fd) {
                    (void)send(client_fds[k], out, (size_t)n, 0);
                }
            }
        }
    }
    return 0;
}
