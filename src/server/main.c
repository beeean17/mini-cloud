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

    const char *storage_dir = (argc >= 3) ? argv[2] : "storage";
    if (ensure_storage_dir(storage_dir) != 0) {
        perror("ensure_storage_dir");
        return EXIT_FAILURE;
    }

    mc_server_config_t config = {
        .port = (uint16_t)port_long,
        .backlog = 16,
        .storage_dir = storage_dir,
    };

    if (mc_server_run(&config) != 0) {
        perror("mc_server_run");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
