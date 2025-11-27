#include "mc_client.h"

#include <stdio.h>
#include <stdlib.h>

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <ip> <port> [token]\n", prog);
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
    if (argc < 3 || argc > 4) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char *end = NULL;
    long port_long = strtol(argv[2], &end, 10);
    if (!end || *end != '\0' || port_long <= 0 || port_long > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    const char *token_arg = NULL;
    if (argc == 4 && argv[3] && argv[3][0] != '\0') {
        token_arg = argv[3];
    }

    char *token_from_file = NULL;
    if (!token_arg) {
        const char *token_env = getenv("MC_CLIENT_TOKEN");
        if (token_env && *token_env) {
            token_arg = token_env;
        } else {
            const char *token_file_env = getenv("MC_CLIENT_TOKEN_FILE");
            if (token_file_env && *token_file_env) {
                token_from_file = load_token_from_file(token_file_env);
                if (!token_from_file || token_from_file[0] == '\0') {
                    fprintf(stderr, "Failed to read non-empty token from %s\n", token_file_env);
                    free(token_from_file);
                    return EXIT_FAILURE;
                }
                token_arg = token_from_file;
            }
        }
    }

    mc_client_config_t config = {
        .host = argv[1],
        .port = (uint16_t)port_long,
        .auth_token = token_arg,
    };

    if (mc_client_run(&config) != 0) {
        perror("mc_client_run");
        free(token_from_file);
        return EXIT_FAILURE;
    }

    free(token_from_file);
    return EXIT_SUCCESS;
}
