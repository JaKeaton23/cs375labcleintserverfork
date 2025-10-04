// server.cpp — Exercise 6


#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <csignal>
#include <ctime>
#include <cerrno>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 8080

// --- Logging helpers ---------------------------------------------------------

static std::string now_string() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

// Append one line to server_errors.log (open/close each time to avoid sharing issues)
static void log_error(const std::string& where, const std::string& what) {
    std::ofstream log("server_errors.log", std::ios::app);
    if (log) {
        log << "[" << now_string() << "] " << where << ": " << what << "\n";
    }
}

static void log_errno(const std::string& where, const std::string& context) {
    int e = errno;
    std::string msg = context + " | errno=" + std::to_string(e) + " (" + std::strerror(e) + ")";
    log_error(where, msg);
}

// --- Client handling ---------------------------------------------------------

static void handle_client(int client_sock) {
    // Child process: interact with the client; robust to short reads/writes.
    char buf[1024];

    for (;;) {
        ssize_t n = recv(client_sock, buf, sizeof(buf) - 1, 0);
        if (n < 0) {
            if (errno == EINTR) continue;                // interrupted — retry
            log_errno("handle_client/recv", "recv() failed");
            break;
        }
        if (n == 0) {
            // Client closed connection
            break;
        }
        buf[n] = '\0';

        // Build reply (simple echo)
        std::string reply = std::string("Echo: ") + buf;

        // Try sending all bytes (loop in case of partial sends)
        const char* p = reply.c_str();
        size_t to_send = reply.size();
        while (to_send > 0) {
            ssize_t s = send(client_sock, p, to_send, 0);
            if (s < 0) {
                if (errno == EINTR) continue;
                if (errno == EPIPE) {
                    // Client vanished; log and stop.
                    log_errno("handle_client/send", "EPIPE: client closed");
                    to_send = 0; // will break outer loop below
                    break;
                }
                log_errno("handle_client/send", "send() failed");
                to_send = 0;
                break;
            }
            p += s;
            to_send -= static_cast<size_t>(s);
        }
    }

    close(client_sock);
}

int main() {
    // 1) Hardening signals:
    //    - Ignore SIGPIPE so accidental writes to closed sockets don't kill us
    //    - Ignore/reap children to prevent zombies
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    // 2) Create socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        log_errno("main/socket", "socket() failed");
        std::perror("socket");
        return 1;
    }

    // 3) Reuse address for quick restarts
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_errno("main/setsockopt", "SO_REUSEADDR failed");
        // Non-fatal; continue.
    }

    // 4) Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    if (bind(server_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        log_errno("main/bind", "bind() failed (is another server running on this port?)");
        std::perror("bind");
        close(server_sock);
        return 1;
    }

    // 5) Listen
    if (listen(server_sock, 16) < 0) {
        log_errno("main/listen", "listen() failed");
        std::perror("listen");
        close(server_sock);
        return 1;
    }

    std::cout << "C++ server (robust) listening on " << PORT << " …\n";

    // 6) Accept loop: keep going on errors; never crash parent
    for (;;) {
        sockaddr_in client_addr{};
        socklen_t alen = sizeof(client_addr);

        int client_sock = accept(server_sock, (sockaddr*)&client_addr, &alen);
        if (client_sock < 0) {
            if (errno == EINTR) continue;       // interrupted by signal; retry
            log_errno("main/accept", "accept() failed");
            // Continue accepting new connections even after errors
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            log_errno("main/fork", "fork() failed");
            std::perror("fork");
            close(client_sock);
            continue;
        }

        if (pid == 0) {
            // Child process
            close(server_sock);                 // child does not accept()
            try {
                handle_client(client_sock);
            } catch (const std::exception& ex) {
                log_error("child/exception", std::string("std::exception: ") + ex.what());
            } catch (...) {
                log_error("child/exception", "Unknown exception");
            }
            _exit(0);
        } else {
            // Parent process: keep listening; child owns client_sock
            close(client_sock);
        }
    }

    // (Unreached in typical server; but if you add graceful shutdown, close server_sock)
    // close(server_sock);
    // return 0;
}
