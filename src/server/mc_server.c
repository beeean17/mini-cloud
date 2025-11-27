#define _POSIX_C_SOURCE 200809L

#include "mc_server.h"
#include "mc_protocol.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MC_STORAGE_PATH_MAX PATH_MAX
#define MC_MAX_AUTH_TOKEN_LEN 256

static volatile sig_atomic_t g_should_terminate = 0;

static int is_safe_filename(const char *name) {
    if (!name || !*name) {
        return 0;
    }
    if (strstr(name, "..")) {
        return 0;
    }
    if (strchr(name, '/')) {
        return 0;
    }
    return 1;
}

static int build_storage_path(const mc_server_config_t *config,
                              const char *filename,
                              char *out,
                              size_t out_len) {
    if (!config || !filename || !out) {
        errno = EINVAL;
        return -1;
    }

    int written = snprintf(out, out_len, "%s/%s", config->storage_dir, filename);
    if (written < 0 || (size_t)written >= out_len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int send_message(int fd, mc_command_t cmd, const char *filename, const char *payload) {
    const char *msg = payload ? payload : "";
    size_t len = strlen(msg);
    mc_packet_header_t header;
    if (mc_build_header(&header, cmd, filename, (uint64_t)len) != 0) {
        return -1;
    }
    if (mc_send_header(fd, &header) != 0) {
        return -1;
    }
    if (filename && filename[0]) {
        if (mc_send_all(fd, filename, strlen(filename)) != (ssize_t)strlen(filename)) {
            return -1;
        }
    }
    if (len > 0) {
        if (mc_send_all(fd, msg, len) != (ssize_t)len) {
            return -1;
        }
    }
    return 0;
}

static int send_errorf(int fd, const char *fmt, ...) {
    char buffer[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    return send_message(fd, MC_CMD_ERROR, NULL, buffer);
}

static int receive_payload_to_fd(int src_fd, uint64_t total_bytes, int dest_fd) {
    uint8_t buffer[4096];
    uint64_t remaining = total_bytes;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        ssize_t read_bytes = mc_recv_all(src_fd, buffer, chunk);
        if (read_bytes != (ssize_t)chunk) {
            return -1;
        }
        ssize_t written = write(dest_fd, buffer, read_bytes); /* write() 시스템 콜로 파일 저장 */
        if (written != read_bytes) {
            return -1;
        }
        remaining -= (uint64_t)read_bytes;
    }
    return 0;
}

static int send_file_contents(int fd, int file_fd, uint64_t total_bytes) {
    uint8_t buffer[4096];
    uint64_t remaining = total_bytes;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        ssize_t read_bytes = read(file_fd, buffer, chunk); /* read() 시스템 콜로 파일 읽기 */
        if (read_bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (read_bytes == 0) {
            break;
        }
        if (mc_send_all(fd, buffer, (size_t)read_bytes) != read_bytes) {
            return -1;
        }
        remaining -= (uint64_t)read_bytes;
    }
    return remaining == 0 ? 0 : -1;
}

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

static int handle_upload_request(int client_fd,
                                 const mc_server_config_t *config,
                                 const mc_packet_info_t *info) {
    if (!info->filename[0]) {
        drain_payload(client_fd, info->header.payload_len);
        return send_errorf(client_fd, "UPLOAD requires filename");
    }
    if (!is_safe_filename(info->filename)) {
        drain_payload(client_fd, info->header.payload_len);
        return send_errorf(client_fd, "Invalid filename");
    }

    if (config->max_upload_bytes > 0 && info->header.payload_len > config->max_upload_bytes) {
        drain_payload(client_fd, info->header.payload_len);
        return send_errorf(client_fd,
                           "Upload exceeds limit (%" PRIu64 " bytes)",
                           (uint64_t)config->max_upload_bytes);
    }

    char final_path[MC_STORAGE_PATH_MAX];
    if (build_storage_path(config, info->filename, final_path, sizeof(final_path)) != 0) {
        drain_payload(client_fd, info->header.payload_len);
        return send_errorf(client_fd, "Path too long");
    }

    char tmp_path[MC_STORAGE_PATH_MAX];
    snprintf(tmp_path,
             sizeof(tmp_path),
             "%s/.%s.%ld.tmp",
             config->storage_dir,
             info->filename,
             (long)getpid());

    int file_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644); /* open() 시스템 콜로 임시 파일 생성 */
    if (file_fd == -1) {
        drain_payload(client_fd, info->header.payload_len);
        return send_errorf(client_fd, "Failed to open temp file: %s", strerror(errno));
    }

    int rc = receive_payload_to_fd(client_fd, info->header.payload_len, file_fd);
    close(file_fd); /* close() 시스템 콜로 임시 파일 닫기 */
    if (rc != 0) {
        unlink(tmp_path); /* unlink() 시스템 콜로 임시 파일 제거 */
        return send_errorf(client_fd, "Failed to receive file data");
    }

    if (rename(tmp_path, final_path) == -1) { /* rename() 시스템 콜로 원자적 교체 */
        unlink(tmp_path);
        return send_errorf(client_fd, "Failed to store file: %s", strerror(errno));
    }

    return send_message(client_fd, MC_CMD_UPLOAD, info->filename, "UPLOAD OK");
}

static int handle_download_request(int client_fd,
                                   const mc_server_config_t *config,
                                   const mc_packet_info_t *info) {
    if (!info->filename[0]) {
        drain_payload(client_fd, info->header.payload_len);
        return send_errorf(client_fd, "DOWNLOAD requires filename");
    }
    if (!is_safe_filename(info->filename)) {
        drain_payload(client_fd, info->header.payload_len);
        return send_errorf(client_fd, "Invalid filename");
    }
    if (info->header.payload_len > 0) {
        drain_payload(client_fd, info->header.payload_len);
    }

    char path[MC_STORAGE_PATH_MAX];
    if (build_storage_path(config, info->filename, path, sizeof(path)) != 0) {
        return send_errorf(client_fd, "Path too long");
    }

    int file_fd = open(path, O_RDONLY); /* open() 시스템 콜로 다운로드 파일 오픈 */
    if (file_fd == -1) {
        return send_errorf(client_fd, "File not found");
    }

    struct stat st;
    if (fstat(file_fd, &st) == -1) { /* fstat() 시스템 콜로 파일 크기 확인 */
        close(file_fd);
        return send_errorf(client_fd, "Failed to stat file");
    }
    if (!S_ISREG(st.st_mode)) {
        close(file_fd);
        return send_errorf(client_fd, "Not a regular file");
    }

    mc_packet_header_t header;
    if (mc_build_header(&header, MC_CMD_DOWNLOAD, info->filename, (uint64_t)st.st_size) != 0) {
        close(file_fd);
        return -1;
    }
    if (mc_send_header(client_fd, &header) != 0) {
        close(file_fd);
        return -1;
    }
    if (mc_send_all(client_fd, info->filename, strlen(info->filename)) != (ssize_t)strlen(info->filename)) {
        close(file_fd);
        return -1;
    }

    int rc = send_file_contents(client_fd, file_fd, (uint64_t)st.st_size);
    close(file_fd);
    return rc;
}

static int handle_list_request(int client_fd, const mc_server_config_t *config, const mc_packet_info_t *info) {
    if (info->header.payload_len > 0) {
        drain_payload(client_fd, info->header.payload_len);
    }

    DIR *dir = opendir(config->storage_dir); /* opendir() 시스템 콜로 저장소 열기 */
    if (!dir) {
        return send_errorf(client_fd, "Failed to open storage dir");
    }

    size_t cap = 1024;
    size_t used = 0;
    char *list_buf = malloc(cap);
    if (!list_buf) {
        closedir(dir);
        return send_errorf(client_fd, "Out of memory");
    }
    list_buf[0] = '\0';

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) { /* readdir() 시스템 콜로 항목 열람 */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (!is_safe_filename(entry->d_name)) {
            continue;
        }
        size_t len = strlen(entry->d_name) + 1;
        if (used + len + 1 >= cap) {
            cap *= 2;
            char *tmp = realloc(list_buf, cap);
            if (!tmp) {
                free(list_buf);
                closedir(dir);
                return send_errorf(client_fd, "Out of memory");
            }
            list_buf = tmp;
        }
        used += (size_t)snprintf(list_buf + used, cap - used, "%s\n", entry->d_name);
    }
    closedir(dir);

    if (used == 0) {
        strcpy(list_buf, "(empty)\n");
        used = strlen(list_buf);
    }

    mc_packet_header_t header;
    if (mc_build_header(&header, MC_CMD_LIST, NULL, (uint64_t)used) != 0) {
        free(list_buf);
        return -1;
    }
    if (mc_send_header(client_fd, &header) != 0) {
        free(list_buf);
        return -1;
    }
    int rc = 0;
    if (mc_send_all(client_fd, list_buf, used) != (ssize_t)used) {
        rc = -1;
    }
    free(list_buf);
    return rc;
}

static int handle_auth_request(int client_fd,
                               const mc_server_config_t *config,
                               const mc_packet_info_t *info,
                               bool *authenticated) {
    if (!authenticated) {
        if (info->header.payload_len > 0) {
            drain_payload(client_fd, info->header.payload_len);
        }
        return send_errorf(client_fd, "Authentication state unavailable");
    }

    if (*authenticated) {
        if (info->header.payload_len > 0) {
            drain_payload(client_fd, info->header.payload_len);
        }
        return send_message(client_fd, MC_CMD_AUTH, NULL, "Already authenticated");
    }

    if (!config->auth_token || !config->auth_token[0]) {
        if (info->header.payload_len > 0) {
            drain_payload(client_fd, info->header.payload_len);
        }
        *authenticated = true;
        return send_message(client_fd, MC_CMD_AUTH, NULL, "AUTH not required");
    }

    if (info->header.payload_len == 0 || info->header.payload_len > MC_MAX_AUTH_TOKEN_LEN) {
        drain_payload(client_fd, info->header.payload_len);
        return send_errorf(client_fd, "Invalid auth token length");
    }

    char token[MC_MAX_AUTH_TOKEN_LEN + 1];
    if (mc_recv_all(client_fd, token, (size_t)info->header.payload_len) !=
        (ssize_t)info->header.payload_len) {
        return -1;
    }
    token[info->header.payload_len] = '\0';

    if (strcmp(token, config->auth_token) != 0) {
        send_errorf(client_fd, "Invalid auth token");
        return -1;
    }

    *authenticated = true;
    return send_message(client_fd, MC_CMD_AUTH, NULL, "AUTH OK");
}

static void handle_client(int client_fd, const struct sockaddr_in *addr, const mc_server_config_t *config) {
    bool require_auth = config->auth_token && config->auth_token[0];
    bool authenticated = !require_auth;
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

        log_client_command(addr, &info);

        if (!authenticated && info.header.command != MC_CMD_AUTH) {
            if (info.header.payload_len > 0) {
                drain_payload(client_fd, info.header.payload_len);
            }
            if (send_errorf(client_fd, "Authentication required") != 0) {
                break;
            }
            continue;
        }

        int handler_rc = 0;
        switch (info.header.command) {
            case MC_CMD_UPLOAD:
                handler_rc = handle_upload_request(client_fd, config, &info);
                break;
            case MC_CMD_DOWNLOAD:
                handler_rc = handle_download_request(client_fd, config, &info);
                break;
            case MC_CMD_LIST:
                handler_rc = handle_list_request(client_fd, config, &info);
                break;
            case MC_CMD_AUTH:
                handler_rc = handle_auth_request(client_fd, config, &info, &authenticated);
                break;
            case MC_CMD_QUIT:
                if (info.header.payload_len > 0) {
                    drain_payload(client_fd, info.header.payload_len);
                }
                send_message(client_fd, MC_CMD_QUIT, NULL, "Goodbye");
                return;
            case MC_CMD_ERROR:
            default:
                if (info.header.payload_len > 0) {
                    drain_payload(client_fd, info.header.payload_len);
                }
                handler_rc = send_errorf(client_fd, "Unsupported command");
                break;
        }

        if (handler_rc != 0) {
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

    const char *auth_mode = (config->auth_token && config->auth_token[0]) ? "required" : "disabled";
    char limit_buf[64];
    if (config->max_upload_bytes > 0) {
        snprintf(limit_buf,
                 sizeof(limit_buf),
                 "%llu bytes",
                 (unsigned long long)config->max_upload_bytes);
    } else {
        snprintf(limit_buf, sizeof(limit_buf), "unlimited");
    }

    printf("Mini Cloud server listening on port %u (storage=%s, auth=%s, max_upload=%s)\n",
           config->port,
           config->storage_dir,
           auth_mode,
           limit_buf);
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
        if (pid == 0) {
            close(listen_fd); /* close() 시스템 콜로 부모 리스너 fd 정리 */
            handle_client(client_fd, &client_addr, config);
            close(client_fd); /* close() 시스템 콜로 클라이언트 소켓 정리 */
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
