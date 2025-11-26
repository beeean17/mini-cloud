#include "mc_client.h"

#include <stdio.h>
#include <stdlib.h>

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <ip> <port>\n", prog);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    char *end = NULL;
    long port_long = strtol(argv[2], &end, 10);
    if (!end || *end != '\0' || port_long <= 0 || port_long > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    mc_client_config_t config = {
        .host = argv[1],
        .port = (uint16_t)port_long,
    };

    if (mc_client_run(&config) != 0) {
        perror("mc_client_run");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
