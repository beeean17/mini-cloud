#define _POSIX_C_SOURCE 200809L

#include "mc_client.h"
#include "mc_protocol.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MC_CLIENT_READ_CHUNK 4096

typedef enum {
    CLI_ACTION_NONE = 0,
    CLI_ACTION_UPLOAD,
    CLI_ACTION_DOWNLOAD,
    CLI_ACTION_LIST,
    CLI_ACTION_QUIT
} cli_action_t;

typedef struct {
    cli_action_t action;
    char arg[MC_MAX_FILENAME_LEN + 1];
} cli_request_t;

static void lowercase(char *s) {
    while (*s) {
        *s = (char)tolower((unsigned char)*s);
        ++s;
    }
}

static char *trim(char *line) {
    if (!line) {
        return line;
    }
    while (*line && isspace((unsigned char)*line)) {
        ++line;
    }
    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)*(end - 1))) {
        --end;
    }
    *end = '\0';
    return line;
}

static const char *basename_safe(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return path;
    }
    return slash + 1;
}

static int connect_to_server(const mc_client_config_t *config) {
    int fd = socket(AF_INET, SOCK_STREAM, 0); /* socket() 시스템 콜로 클라이언트 소켓 생성 */
    if (fd == -1) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config->port);
    if (inet_pton(AF_INET, config->host, &addr.sin_addr) != 1) {
        close(fd);
        errno = EINVAL;
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) { /* connect() 시스템 콜로 서버 접속 */
        close(fd);
        return -1;
    }

    return fd;
}

static int send_header_and_filename(int fd, mc_command_t command, const char *filename, uint64_t payload_len) {
    mc_packet_header_t header;
    if (mc_build_header(&header, command, filename, payload_len) != 0) {
        return -1;
    }

    if (mc_send_header(fd, &header) != 0) {
        return -1;
    }

    if (filename && filename[0]) {
        size_t len = strlen(filename);
        if (mc_send_all(fd, filename, len) != (ssize_t)len) {
            return -1;
        }
    }
    return 0;
}

static int send_list(int fd) {
    return send_header_and_filename(fd, MC_CMD_LIST, NULL, 0);
}

static int send_quit(int fd) {
    return send_header_and_filename(fd, MC_CMD_QUIT, NULL, 0);
}

static int send_download(int fd, const char *remote_name) {
    return send_header_and_filename(fd, MC_CMD_DOWNLOAD, remote_name, 0);
}

static int transmit_file_payload(int socket_fd, int file_fd, uint64_t size) {
    uint8_t buffer[MC_CLIENT_READ_CHUNK];
    uint64_t remaining = size;

    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        ssize_t rd = read(file_fd, buffer, chunk); /* read() 시스템 콜로 로컬 파일 읽기 */
        if (rd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rd == 0) {
            break;
        }
        if (mc_send_all(socket_fd, buffer, (size_t)rd) != rd) {
            return -1;
        }
        remaining -= (uint64_t)rd;
    }

    return remaining == 0 ? 0 : -1;
}

static int send_upload(int fd, const char *local_path) {
    struct stat st;
    if (stat(local_path, &st) == -1) { /* stat() 시스템 콜로 파일 정보 확인 */
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        errno = EINVAL;
        return -1;
    }

    const char *base = basename_safe(local_path);
    if (strlen(base) > MC_MAX_FILENAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    int file_fd = open(local_path, O_RDONLY); /* open() 시스템 콜로 업로드 파일 오픈 */
    if (file_fd == -1) {
        return -1;
    }

    uint64_t payload_len = (uint64_t)st.st_size;
    int rc = send_header_and_filename(fd, MC_CMD_UPLOAD, base, payload_len);
    if (rc == 0) {
        rc = transmit_file_payload(fd, file_fd, payload_len);
    }

    close(file_fd); /* close() 시스템 콜로 로컬 파일 닫기 */
    return rc;
}

static int recv_filename_into(int fd, mc_packet_info_t *info) {
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

static int recv_packet(int fd, mc_packet_info_t *info) {
    mc_packet_header_t header;
    int rc = mc_recv_header(fd, &header);
    if (rc != 0) {
        return -1;
    }
    info->header = header;
    return recv_filename_into(fd, info);
}

static int recv_payload_to_buffer(int fd, uint64_t len, char **out_buf) {
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        return -1;
    }
    if (len > 0) {
        if (mc_recv_all(fd, buf, (size_t)len) != (ssize_t)len) {
            free(buf);
            return -1;
        }
    }
    buf[len] = '\0';
    *out_buf = buf;
    return 0;
}

static int recv_payload_to_file(int fd, uint64_t len, const char *path) {
    int out_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644); /* open() 시스템 콜로 다운로드 파일 생성 */
    if (out_fd == -1) {
        return -1;
    }
    uint8_t buffer[MC_CLIENT_READ_CHUNK];
    uint64_t remaining = len;
    int rc = 0;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        ssize_t read_bytes = mc_recv_all(fd, buffer, chunk);
        if (read_bytes != (ssize_t)chunk) {
            rc = -1;
            break;
        }
        ssize_t written = write(out_fd, buffer, read_bytes); /* write() 시스템 콜로 다운로드 데이터 기록 */
        if (written != read_bytes) {
            rc = -1;
            break;
        }
        remaining -= (uint64_t)read_bytes;
    }
    close(out_fd); /* close() 시스템 콜로 다운로드 파일 닫기 */
    if (rc != 0) {
        unlink(path); /* unlink() 시스템 콜로 손상된 파일 제거 */
    }
    return rc;
}

static void sanitize_download_name(const char *input, char *out, size_t out_len) {
    const char *candidate = (input && *input) ? input : "download.bin";
    const char *base = basename_safe(candidate);
    snprintf(out, out_len, "%s", base);
}

static int handle_download_payload(int fd, const mc_packet_info_t *info, const char *requested_name) {
    char local_name[MC_MAX_FILENAME_LEN + 1];
    if (info->filename[0]) {
        sanitize_download_name(info->filename, local_name, sizeof(local_name));
    } else {
        sanitize_download_name(requested_name, local_name, sizeof(local_name));
    }

    printf("[CLIENT] 서버에서 %s (%" PRIu64 " bytes) 다운로드\n",
           local_name,
           (uint64_t)info->header.payload_len);

    if (recv_payload_to_file(fd, info->header.payload_len, local_name) != 0) {
        fprintf(stderr, "다운로드 저장 실패: %s\n", strerror(errno));
        return -1;
    }

    printf("[CLIENT] 다운로드 완료 -> %s\n", local_name);
    return 0;
}

static bool parse_command(const char *line_in, cli_request_t *out) {
    if (!line_in || !out) {
        return false;
    }

    char *line = strdup(line_in);
    if (!line) {
        return false;
    }
    char *trimmed = trim(line);
    if (*trimmed == '\0') {
        free(line);
        return false;
    }

    char *save = NULL;
    char *cmd = strtok_r(trimmed, " \t", &save);
    lowercase(cmd);

    char *arg = strtok_r(NULL, "", &save);
    if (arg) {
        arg = trim(arg);
    }

    cli_request_t req = {0};
    if (strcmp(cmd, "upload") == 0) {
        if (!arg || *arg == '\0') {
            fprintf(stderr, "UPLOAD 명령에는 로컬 파일 경로가 필요합니다.\n");
            free(line);
            return false;
        }
        if (strlen(arg) > MC_MAX_FILENAME_LEN) {
            fprintf(stderr, "파일명이 너무 깁니다 (최대 %d).\n", MC_MAX_FILENAME_LEN);
            free(line);
            return false;
        }
        req.action = CLI_ACTION_UPLOAD;
        strncpy(req.arg, arg, sizeof(req.arg) - 1);
    } else if (strcmp(cmd, "download") == 0) {
        if (!arg || *arg == '\0') {
            fprintf(stderr, "DOWNLOAD 명령에는 서버 파일명이 필요합니다.\n");
            free(line);
            return false;
        }
        if (strlen(arg) > MC_MAX_FILENAME_LEN) {
            fprintf(stderr, "파일명이 너무 깁니다 (최대 %d).\n", MC_MAX_FILENAME_LEN);
            free(line);
            return false;
        }
        req.action = CLI_ACTION_DOWNLOAD;
        strncpy(req.arg, arg, sizeof(req.arg) - 1);
    } else if (strcmp(cmd, "list") == 0) {
        req.action = CLI_ACTION_LIST;
    } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
        req.action = CLI_ACTION_QUIT;
    } else {
        fprintf(stderr, "알 수 없는 명령: %s\n", cmd);
        free(line);
        return false;
    }

    *out = req;
    free(line);
    return true;
}

static bool is_interactive(void) {
    return isatty(STDIN_FILENO);
}

static void print_prompt(void) {
    if (is_interactive()) {
        fputs("mini-cloud> ", stdout);
        fflush(stdout);
    }
}

static void print_help(void) {
    puts("지원 명령: UPLOAD <path>, DOWNLOAD <filename>, LIST, QUIT");
}

static int handle_server_response(int fd, const cli_request_t *req, bool *should_exit) {
    if (should_exit) {
        *should_exit = false;
    }

    mc_packet_info_t info;
    if (recv_packet(fd, &info) != 0) {
        return -1;
    }

    uint64_t payload_len = info.header.payload_len;
    char *buffer = NULL;

    switch (info.header.command) {
        case MC_CMD_ERROR:
            if (recv_payload_to_buffer(fd, payload_len, &buffer) != 0) {
                return -1;
            }
            fprintf(stderr, "[SERVER ERROR] %s\n", buffer);
            free(buffer);
            return 0;
        case MC_CMD_UPLOAD:
            if (recv_payload_to_buffer(fd, payload_len, &buffer) != 0) {
                return -1;
            }
            printf("[CLIENT] 서버 응답: %s\n", buffer);
            free(buffer);
            return 0;
        case MC_CMD_LIST:
            if (recv_payload_to_buffer(fd, payload_len, &buffer) != 0) {
                return -1;
            }
            printf("[CLIENT] 서버 파일 목록:\n%s", buffer);
            free(buffer);
            return 0;
        case MC_CMD_DOWNLOAD:
            return handle_download_payload(fd, &info, req ? req->arg : NULL);
        case MC_CMD_QUIT:
            if (payload_len > 0) {
                if (recv_payload_to_buffer(fd, payload_len, &buffer) != 0) {
                    return -1;
                }
                printf("[CLIENT] 서버 종료 메시지: %s\n", buffer);
                free(buffer);
            }
            if (should_exit) {
                *should_exit = true;
            }
            return 0;
        default:
            if (payload_len > 0) {
                if (recv_payload_to_buffer(fd, payload_len, &buffer) != 0) {
                    return -1;
                }
                fprintf(stderr, "[CLIENT] 알 수 없는 응답: %s\n", buffer);
                free(buffer);
            }
            return 0;
    }
}

static int command_loop(int fd) {
    signal(SIGPIPE, SIG_IGN);

    print_help();
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;

    while (1) {
        print_prompt();
        len = getline(&line, &cap, stdin); /* getline()으로 사용자 입력 */
        if (len == -1) {
            if (feof(stdin)) {
                (void)send_quit(fd);
            }
            break;
        }

        cli_request_t req;
        if (!parse_command(line, &req)) {
            continue;
        }

        int rc = 0;
        switch (req.action) {
            case CLI_ACTION_UPLOAD:
                printf("[CLIENT] 업로드 시작: %s\n", req.arg);
                rc = send_upload(fd, req.arg);
                break;
            case CLI_ACTION_DOWNLOAD:
                printf("[CLIENT] 다운로드 요청: %s\n", req.arg);
                rc = send_download(fd, req.arg);
                break;
            case CLI_ACTION_LIST:
                printf("[CLIENT] LIST 요청 전송\n");
                rc = send_list(fd);
                break;
            case CLI_ACTION_QUIT:
                printf("[CLIENT] 종료 요청 전송\n");
                rc = send_quit(fd);
                break;
            default:
                rc = -1;
        }

        if (rc != 0) {
            perror("client-command");
            break;
        }

        bool exit_after = false;
        if (handle_server_response(fd, &req, &exit_after) != 0) {
            perror("client-response");
            break;
        }
        if (exit_after) {
            break;
        }
    }

    free(line);
    return 0;
}

int mc_client_run(const mc_client_config_t *config) {
    if (!config) {
        errno = EINVAL;
        return -1;
    }

    int fd = connect_to_server(config);
    if (fd == -1) {
        return -1;
    }

    int rc = command_loop(fd);
    close(fd); /* close() 시스템 콜로 서버 소켓 종료 */
    return rc;
}
