#include "mc_protocol.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void print_header(const mc_packet_header_t *header) {
    printf("magic=0x%08X, version=%u, cmd=%u, filename_len=%u, payload_len=%" PRIu64 "\n",
           header->magic,
           header->version,
           header->command,
           header->filename_len,
           (uint64_t)header->payload_len);
}

int main(void) {
    mc_packet_header_t header;
    if (mc_build_header(&header, MC_CMD_UPLOAD, "demo.bin", 4096) != 0) {
        perror("mc_build_header");
        return 1;
    }

    int fds[2];
    if (pipe(fds) == -1) { /* pipe() 시스템 콜로 테스트 채널 생성 */
        perror("pipe");
        return 1;
    }

    if (mc_send_header(fds[1], &header) != 0) {
        perror("mc_send_header");
        return 1;
    }

    mc_packet_header_t received;
    if (mc_recv_header(fds[0], &received) != 0) {
        perror("mc_recv_header");
        return 1;
    }

    print_header(&received);
    close(fds[0]); /* close() 시스템 콜로 파이프 종료 */
    close(fds[1]); /* close() 시스템 콜로 파이프 종료 */

    return 0;
}
