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
