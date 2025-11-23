#define _POSIX_C_SOURCE 200809L

#include "mc_server.h"
#include "mc_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t g_should_terminate = 0;

static void sigchld_handler(int signo) {
    (void)signo;
    int saved_errno = errno;
    while (1) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG); /* waitpid() 시스템 콜로 좀비 프로세스 회수 */
        if (pid <= 0) {
            break;
        }
    }
    errno = saved_errno;
}

static void sigterm_handler(int signo) {
    (void)signo;
    g_should_terminate = 1;
}

static int install_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) { /* sigaction() 시스템 콜로 SIGCHLD 핸들러 등록 */
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) { /* sigaction() 시스템 콜로 SIGINT 핸들러 등록 */
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) { /* sigaction() 시스템 콜로 SIGTERM 핸들러 등록 */
        return -1;
    }

    struct sigaction ignore_sa;
    memset(&ignore_sa, 0, sizeof(ignore_sa));
    ignore_sa.sa_handler = SIG_IGN;
    sigemptyset(&ignore_sa.sa_mask);
    ignore_sa.sa_flags = 0;
    if (sigaction(SIGPIPE, &ignore_sa, NULL) == -1) { /* sigaction() 시스템 콜로 SIGPIPE 무시 */
        return -1;
    }

    return 0;
}

static int setup_listener(uint16_t port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0); /* socket() 시스템 콜로 리스닝 소켓 생성 */
    if (fd == -1) {
        return -1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) { /* setsockopt() 시스템 콜로 주소 재사용 설정 */
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) { /* bind() 시스템 콜로 주소 할당 */
        close(fd);
        return -1;
    }

    if (listen(fd, backlog) == -1) { /* listen() 시스템 콜로 수신 대기 시작 */
        close(fd);
        return -1;
    }

    return fd;
}

static int recv_filename(int fd, mc_packet_info_t *info) {
    if (info->header.filename_len == 0) {
        info->filename[0] = '\0';
        return 0;
    }

    if (info->header.filename_len > MC_MAX_FILENAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    ssize_t received = mc_recv_all(fd, info->filename, info->header.filename_len);
    if (received != (ssize_t)info->header.filename_len) {
        return -1;
    }
    info->filename[info->header.filename_len] = '\0';
    return 0;
}

static int drain_payload(int fd, uint64_t remaining) {
    uint8_t buffer[4096];
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        ssize_t read_bytes = mc_recv_all(fd, buffer, chunk);
        if (read_bytes != (ssize_t)chunk) {
            return -1;
        }
        remaining -= (uint64_t)chunk;
    }
    return 0;
}

static void log_client_command(const struct sockaddr_in *addr, const mc_packet_info_t *info) {
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    fprintf(stdout,
            "[worker %ld] %s:%d cmd=%u filename=%s payload=%" PRIu64 " bytes\n",
            (long)getpid(),
            ip,
            ntohs(addr->sin_port),
            info->header.command,
            info->filename[0] ? info->filename : "(none)",
            (uint64_t)info->header.payload_len);
    fflush(stdout);
}

static void handle_client(int client_fd, const struct sockaddr_in *addr, const mc_server_config_t *config) {
    (void)config;
    mc_packet_info_t info;
    while (1) {
        mc_packet_header_t header;
        int rc = mc_recv_header(client_fd, &header);
        if (rc != 0) {
            if (rc == -1 && errno == 0) {
                /* client closed connection */
            }
            break;
        }
        info.header = header;
        if (recv_filename(client_fd, &info) != 0) {
            break;
        }
        if (drain_payload(client_fd, info.header.payload_len) != 0) {
            break;
        }
        log_client_command(addr, &info);

        if (info.header.command == MC_CMD_QUIT) {
            break;
        }
    }
}

int mc_server_run(const mc_server_config_t *config) {
    if (!config) {
        errno = EINVAL;
        return -1;
    }

    if (install_signal_handlers() != 0) {
        return -1;
    }

    int listen_fd = setup_listener(config->port, config->backlog);
    if (listen_fd == -1) {
        return -1;
    }

    printf("Mini Cloud server listening on port %u (storage=%s)\n",
           config->port,
           config->storage_dir);
    fflush(stdout);

    while (!g_should_terminate) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len); /* accept() 시스템 콜로 클라이언트 연결 수락 */
        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        pid_t pid = fork(); /* fork() 시스템 콜로 자식 프로세스 생성 */
        if (pid == -1) {
            perror("fork");
            close(client_fd);
            continue;
        }

        if (pid == 0) {
            close(listen_fd); /* close() 시스템 콜로 부모 리스너 fd 정리 */
            handle_client(client_fd, &client_addr, config);
            close(client_fd); /* close() 시스템 콜로 클라이언트 소켓 정리 */
            _exit(EXIT_SUCCESS); /* _exit() 시스템 콜로 자식 종료 */
        }

        close(client_fd); /* close() 시스템 콜로 부모에서 클라이언트 fd 해제 */
    }

    close(listen_fd); /* close() 시스템 콜로 리스너 종료 */
    return 0;
}
