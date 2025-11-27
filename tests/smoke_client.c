#include "mc_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <ip> <port>\n", prog);
}

static char *load_token_from_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    size_t size = (size_t)len;
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    size_t read = fread(buffer, 1, size, fp);
    fclose(fp);
    buffer[read] = '\0';
    while (read > 0 && (buffer[read - 1] == '\n' || buffer[read - 1] == '\r')) {
        buffer[--read] = '\0';
    }
    return buffer;
}

static int maybe_send_auth(int fd) {
    const char *token = NULL;
    const char *token_env = getenv("MC_CLIENT_TOKEN");
    if (token_env && *token_env) {
        token = token_env;
    }

    char *token_from_file = NULL;
    if (!token) {
        const char *token_file_env = getenv("MC_CLIENT_TOKEN_FILE");
        if (token_file_env && *token_file_env) {
            token_from_file = load_token_from_file(token_file_env);
            if (!token_from_file || token_from_file[0] == '\0') {
                fprintf(stderr, "Failed to read non-empty token from %s\n", token_file_env);
                free(token_from_file);
                return -1;
            }
            token = token_from_file;
        }
    }

    if (!token) {
        return 0;
    }

    size_t len = strlen(token);
    mc_packet_header_t header;
    if (mc_build_header(&header, MC_CMD_AUTH, NULL, len) != 0) {
        free(token_from_file);
        return -1;
    }
    if (mc_send_header(fd, &header) != 0) {
        free(token_from_file);
        return -1;
    }
    if (len > 0) {
        if (mc_send_all(fd, token, len) != (ssize_t)len) {
            free(token_from_file);
            return -1;
        }
    }

    mc_packet_header_t resp;
    if (mc_recv_header(fd, &resp) != 0) {
        free(token_from_file);
        return -1;
    }

    if (resp.filename_len > 0) {
        char tmp[MC_MAX_FILENAME_LEN + 1];
        if (resp.filename_len > MC_MAX_FILENAME_LEN ||
            mc_recv_all(fd, tmp, resp.filename_len) != (ssize_t)resp.filename_len) {
            free(token_from_file);
            return -1;
        }
        tmp[resp.filename_len] = '\0';
    }

    char *payload = NULL;
    if (resp.payload_len > 0) {
        payload = malloc((size_t)resp.payload_len + 1);
        if (!payload) {
            free(token_from_file);
            return -1;
        }
        if (mc_recv_all(fd, payload, (size_t)resp.payload_len) != (ssize_t)resp.payload_len) {
            free(payload);
            free(token_from_file);
            return -1;
        }
        payload[resp.payload_len] = '\0';
    }

    int rc = 0;
    if (resp.command != MC_CMD_AUTH) {
        fprintf(stderr, "AUTH handshake failed: server replied with command %u\n", resp.command);
        rc = -1;
    }

    if (payload) {
        printf("[smoke] AUTH response: %s\n", payload);
        free(payload);
    }

    free(token_from_file);
    return rc;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *ip = argv[1];
    char *end = NULL;
    long port_long = strtol(argv[2], &end, 10);
    if (!end || *end != '\0' || port_long <= 0 || port_long > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0); /* socket() 시스템 콜로 클라이언트 소켓 생성 */
    if (fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port_long);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        perror("inet_pton");
        close(fd);
        return EXIT_FAILURE;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) { /* connect() 시스템 콜로 서버 접속 */
        perror("connect");
        close(fd);
        return EXIT_FAILURE;
    }

    if (maybe_send_auth(fd) != 0) {
        fprintf(stderr, "AUTH handshake failed in smoke client\n");
        close(fd);
        return EXIT_FAILURE;
    }

    mc_packet_header_t header;
    if (mc_build_header(&header, MC_CMD_QUIT, NULL, 0) != 0) {
        perror("mc_build_header");
        close(fd);
        return EXIT_FAILURE;
    }

    if (mc_send_header(fd, &header) != 0) {
        perror("mc_send_header");
        close(fd);
        return EXIT_FAILURE;
    }

    close(fd); /* close() 시스템 콜로 소켓 종료 */
    return EXIT_SUCCESS;
}
