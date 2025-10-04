

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
#define MAX_CLIENTS FD_SETSIZE
#define MAX_MSG     1024
#define NICK_MAX    32

typedef struct {
    int sender_idx;   // index in tables (parent's view)
    int len;          // bytes in payload (no NUL)
} msg_hdr_t;

static volatile sig_atomic_t g_shutdown = 0;
static void on_sigint(int signo) { (void)signo; g_shutdown = 1; }

static void trim(char *s){
    size_t n = strlen(s);
    while (n && (s[n-1]=='\n' || s[n-1]=='\r')) s[--n] = '\0';
}
static ssize_t read_full(int fd, void *buf, size_t n){
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, (char*)buf + off, n - off);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) return (ssize_t)off;
        off += (size_t)r;
    }
    return (ssize_t)off;
}
static ssize_t write_full(int fd, const void *buf, size_t n){
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, (const char*)buf + off, n - off);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)w;
    }
    return (ssize_t)off;
}

// Child process: read from its client socket; forward lines to parent via pipe.
static void child_loop(int client_fd, int pipe_write_fd, int my_index) {
    char buf[MAX_MSG];

    // Welcome message & small hint
    const char *hello =
        "Welcome! Commands: /nick <name>, /who, /quit (or 'exit').\n";
    send(client_fd, hello, strlen(hello), 0);

    for (;;) {
        ssize_t n = recv(client_fd, buf, sizeof(buf)-1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        trim(buf);
        if (!buf[0]) continue;

        // package: index + length + payload
        msg_hdr_t hdr = { .sender_idx = my_index, .len = (int)strlen(buf) };
        if (write_full(pipe_write_fd, &hdr, sizeof(hdr)) < 0) break;
        if (write_full(pipe_write_fd, buf, (size_t)hdr.len) < 0) break;

        if (!strcmp(buf, "exit") || !strcmp(buf, "/quit")) break;
    }
    close(client_fd);
    close(pipe_write_fd);
    _exit(0);
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);         // reap children
    signal(SIGINT,  on_sigint);       // graceful shutdown on Ctrl+C

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }
    int opt = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(PORT);
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(listen_fd, 32) < 0) { perror("listen"); exit(1); }

    printf("Chat server (Ex8) on %d … (Ctrl+C to shut down)\n", PORT);

    int client_fds[MAX_CLIENTS];     // sockets parent keeps for broadcast
    int pipe_rfds[MAX_CLIENTS];      // read ends from children
    pid_t child_pids[MAX_CLIENTS];
    char nick[MAX_CLIENTS][NICK_MAX];
    int active = 0;

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        client_fds[i] = -1; pipe_rfds[i] = -1; child_pids[i] = -1;
        snprintf(nick[i], NICK_MAX, "user%d", i);
    }

    while (!g_shutdown) {
        fd_set rfds; FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        int maxfd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (pipe_rfds[i] != -1) {
                FD_SET(pipe_rfds[i], &rfds);
                if (pipe_rfds[i] > maxfd) maxfd = pipe_rfds[i];
            }
        }

        int ready = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;  // signal woke us; check g_shutdown
            perror("select"); continue;
        }

        // New connection?
        if (FD_ISSET(listen_fd, &rfds)) {
            struct sockaddr_in ca; socklen_t alen = sizeof(ca);
            int cs = accept(listen_fd, (struct sockaddr*)&ca, &alen);
            if (cs < 0) { perror("accept"); continue; }

            int slot = -1;
            for (int i = 0; i < MAX_CLIENTS; ++i) if (client_fds[i] == -1) { slot = i; break; }
            if (slot == -1) {
                const char *full = "Server full. Try later.\n";
                send(cs, full, strlen(full), 0);
                close(cs);
            } else {
                int pfd[2];
                if (pipe(pfd) < 0) { perror("pipe"); close(cs); continue; }
                pid_t pid = fork();
                if (pid < 0) { perror("fork"); close(cs); close(pfd[0]); close(pfd[1]); continue; }

                if (pid == 0) {
                    // child
                    close(listen_fd);
                    close(pfd[0]);
                    child_loop(cs, pfd[1], slot);
                } else {
                    // parent
                    client_fds[slot] = cs;
                    pipe_rfds[slot]  = pfd[0];
                    child_pids[slot] = pid;
                    close(pfd[1]);
                    snprintf(nick[slot], NICK_MAX, "user%d", slot);
                    active++;

                    char join[128];
                    int n = snprintf(join, sizeof(join), "%s joined. Active: %d\n", nick[slot], active);
                    for (int k = 0; k < MAX_CLIENTS; ++k)
                        if (client_fds[k] != -1) send(client_fds[k], join, (size_t)n, 0);
                }
            }
        }

        // Messages from children?
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            int rfd = pipe_rfds[i];
            if (rfd == -1 || !FD_ISSET(rfd, &rfds)) continue;

            msg_hdr_t hdr;
            ssize_t h = read_full(rfd, &hdr, sizeof(hdr));
            if (h == 0) {
                // child exited -> client gone
                if (client_fds[i] != -1) {
                    close(client_fds[i]); client_fds[i] = -1;
                    active--;
                    char leave[128];
                    int n = snprintf(leave, sizeof(leave), "%s left. Active: %d\n", nick[i], active);
                    for (int k = 0; k < MAX_CLIENTS; ++k)
                        if (client_fds[k] != -1) send(client_fds[k], leave, (size_t)n, 0);
                }
                close(rfd); pipe_rfds[i] = -1;
                continue;
            } else if (h < 0 || h != (ssize_t)sizeof(hdr)) {
                continue;
            }

            if (hdr.len <= 0 || hdr.len > MAX_MSG-1) { continue; }

            char msg[MAX_MSG];
            if (read_full(rfd, msg, (size_t)hdr.len) != hdr.len) continue;
            msg[hdr.len] = '\0';

            // Handle commands (/nick, /who, /quit) in parent
            if (!strncmp(msg, "/nick ", 6)) {
                const char *newn = msg + 6;
                char tmp[NICK_MAX]; strncpy(tmp, newn, NICK_MAX-1); tmp[NICK_MAX-1] = '\0';
                trim(tmp);
                if (tmp[0] == '\0') {
                    const char *err = "Usage: /nick <name>\n";
                    send(client_fds[i], err, strlen(err), 0);
                } else {
                    char old[NICK_MAX]; strncpy(old, nick[i], NICK_MAX);
                    strncpy(nick[i], tmp, NICK_MAX-1); nick[i][NICK_MAX-1] = '\0';

                    char note[160];
                    int n = snprintf(note, sizeof(note), "%s is now known as %s\n", old, nick[i]);
                    for (int k = 0; k < MAX_CLIENTS; ++k)
                        if (client_fds[k] != -1) send(client_fds[k], note, (size_t)n, 0);
                }
                continue;
            } else if (!strcmp(msg, "/who")) {
                // List users to requester only
                char line[160];
                int n = snprintf(line, sizeof(line), "Users (%d):\n", active);
                send(client_fds[i], line, (size_t)n, 0);
                for (int k = 0; k < MAX_CLIENTS; ++k) {
                    if (client_fds[k] != -1) {
                        n = snprintf(line, sizeof(line), " - %s\n", nick[k]);
                        send(client_fds[i], line, (size_t)n, 0);
                    }
                }
                continue;
            } else if (!strcmp(msg, "/quit") || !strcmp(msg, "exit")) {
                // Child will also exit; we’ll catch EOF on pipe next loop
                // Send a small ack so client returns cleanly
                const char *bye = "Goodbye.\n";
                send(client_fds[i], bye, strlen(bye), 0);
                continue;
            }

            // Normal chat: broadcast to everyone except sender
            char out[MAX_MSG + 64];
            int n = snprintf(out, sizeof(out), "%s: %s\n", nick[i], msg);
            for (int k = 0; k < MAX_CLIENTS; ++k) {
                if (client_fds[k] != -1 && k != i) {
                    (void)send(client_fds[k], out, (size_t)n, 0);
                }
            }
        }
    }

    // Graceful shutdown
    const char *shutdown_msg = "\n*** Server shutting down ***\n";
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_fds[i] != -1) {
            send(client_fds[i], shutdown_msg, strlen(shutdown_msg), 0);
            close(client_fds[i]); client_fds[i] = -1;
        }
        if (pipe_rfds[i] != -1) { close(pipe_rfds[i]); pipe_rfds[i] = -1; }
    }
    close(listen_fd);
    printf("Server stopped.\n");
    return 0;
}
