#include "mc_server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <port> [storage_dir]\n", prog);
}

static int ensure_storage_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) { /* stat() 시스템 콜로 디렉터리 존재 여부 확인 */
        if (errno != ENOENT) {
            return -1;
        }
        if (mkdir(path, 0755) == -1) { /* mkdir() 시스템 콜로 저장 디렉터리 생성 */
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
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

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char *end = NULL;
    long port_long = strtol(argv[1], &end, 10);
    if (!end || *end != '\0' || port_long <= 0 || port_long > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    const char *storage_env = getenv("MC_STORAGE_DIR");
    const char *storage_dir = NULL;
    if (argc >= 3) {
        storage_dir = argv[2];
    } else if (storage_env && *storage_env) {
        storage_dir = storage_env;
    } else {
        storage_dir = "storage";
    }
    if (ensure_storage_dir(storage_dir) != 0) {
        perror("ensure_storage_dir");
        return EXIT_FAILURE;
    }

    char *token_from_file = NULL;
    const char *token_file_env = getenv("MC_SERVER_TOKEN_FILE");
    if (token_file_env && *token_file_env) {
        token_from_file = load_token_from_file(token_file_env);
        if (!token_from_file || token_from_file[0] == '\0') {
            fprintf(stderr, "Failed to read non-empty token from %s\n", token_file_env);
            free(token_from_file);
            return EXIT_FAILURE;
        }
    }

    const char *token_env = getenv("MC_SERVER_TOKEN");
    const char *auth_token = NULL;
    if (token_from_file) {
        auth_token = token_from_file;
    } else if (token_env && *token_env) {
        auth_token = token_env;
    }

    int backlog = 16;
    const char *backlog_env = getenv("MC_SERVER_BACKLOG");
    if (backlog_env && *backlog_env) {
        errno = 0;
        char *endptr = NULL;
        long parsed = strtol(backlog_env, &endptr, 10);
        if (errno != 0 || !endptr || *endptr != '\0' || parsed <= 0 || parsed > 1024) {
            fprintf(stderr, "Invalid MC_SERVER_BACKLOG: %s\n", backlog_env);
            free(token_from_file);
            return EXIT_FAILURE;
        }
        backlog = (int)parsed;
    }

    uint64_t max_upload_bytes = 0;
    const char *max_env = getenv("MC_MAX_UPLOAD_BYTES");
    if (max_env && *max_env) {
        errno = 0;
        char *endptr = NULL;
        unsigned long long parsed = strtoull(max_env, &endptr, 10);
        if (errno != 0 || !endptr || *endptr != '\0') {
            fprintf(stderr, "Invalid MC_MAX_UPLOAD_BYTES: %s\n", max_env);
            free(token_from_file);
            return EXIT_FAILURE;
        }
        max_upload_bytes = (uint64_t)parsed;
    }

    mc_server_config_t config = {
        .port = (uint16_t)port_long,
        .backlog = backlog,
        .storage_dir = storage_dir,
        .auth_token = auth_token,
        .max_upload_bytes = max_upload_bytes,
    };

    if (mc_server_run(&config) != 0) {
        perror("mc_server_run");
        free(token_from_file);
        return EXIT_FAILURE;
    }

    free(token_from_file);
    return EXIT_SUCCESS;
}
