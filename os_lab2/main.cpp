// server_pselect_sighup.c
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t wasSigHup = 0;

static void sigHupHandler(int signo) {
    wasSigHup = 1;
}

static void sysdie(const char *msg) {
    perror(msg);
    exit(1);
}

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void register_sighup_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigHupHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGHUP, &sa, NULL) == -1) sysdie("sigaction(SIGHUP)");
}

static void block_sighup(sigset_t *blockedMask, sigset_t *origMask) {
    sigemptyset(blockedMask);
    sigaddset(blockedMask, SIGHUP);
    if (sigprocmask(SIG_BLOCK, blockedMask, origMask) == -1)
        sysdie("sigprocmask(SIG_BLOCK)");
}

static int make_listen_socket(uint16_t port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == -1) sysdie("socket");

    int one = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1)
        sysdie("setsockopt(SO_REUSEADDR)");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) sysdie("bind");
    if (listen(s, 64) == -1) sysdie("listen");

    return s;
}

int main(int argc, char **argv) {
    uint16_t port = 5555;
    if (argc >= 2) {
        long p = strtol(argv[1], NULL, 10);
        if (p <= 0 || p > 65535) die("Bad port: %s", argv[1]);
        port = (uint16_t)p;
    }

    int listen_fd = make_listen_socket(port);
    printf("Listening on port %u\n", (unsigned)port);

    // 1) ставим обработчик
    register_sighup_handler();

    // 2) блокируем SIGHUP и сохраняем старую маску
    sigset_t blockedMask, origMask;
    block_sighup(&blockedMask, &origMask);

    int client_fd = -1;

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        int maxfd = listen_fd;

        if (client_fd != -1) {
            FD_SET(client_fd, &rfds);
            if (client_fd > maxfd) maxfd = client_fd;
        }

        // Во время ожидания действует origMask (SIGHUP разблокирован),
        // после возврата pselect восстановит текущую маску (SIGHUP снова заблокирован).
        int r = pselect(maxfd + 1, &rfds, NULL, NULL, NULL, &origMask);
        if (r == -1 && errno != EINTR) sysdie("pselect");

        // Сигнал обрабатываем безопасно: здесь он уже заблокирован.
        if (wasSigHup) {
            wasSigHup = 0;
            printf("[signal] SIGHUP received\n");
        }

        // Новое подключение
        if (FD_ISSET(listen_fd, &rfds)) {
            struct sockaddr_in peer;
            socklen_t peerlen = sizeof(peer);
            int c = accept(listen_fd, (struct sockaddr *)&peer, &peerlen);
            if (c == -1) sysdie("accept");

            char ip[INET_ADDRSTRLEN];
            const char *pip = inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
            unsigned pport = (unsigned)ntohs(peer.sin_port);

            if (client_fd == -1) {
                client_fd = c;
                printf("[conn] accepted and kept: fd=%d from %s:%u\n",
                       client_fd, pip ? pip : "?", pport);
            } else {
                printf("[conn] accepted and closed (only one allowed): fd=%d from %s:%u\n",
                       c, pip ? pip : "?", pport);
                close(c);
            }
        }        
        // Данные от текущего клиента (ровно один recv на готовность)
        if (client_fd != -1 && FD_ISSET(client_fd, &rfds)) {
            char buf[4096];
            ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
            if (n > 0) {
                printf("[data] received %zd bytes\n", n);
            } else if (n == 0) {
                printf("[conn] client closed: fd=%d\n", client_fd);
                close(client_fd);
                client_fd = -1;
            } else {
                sysdie("recv");
            }
        }
    }
}