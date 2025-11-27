#include "mc_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static uint64_t mc_htonll(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)htonl((uint32_t)(value & 0xFFFFFFFFULL)) << 32) |
           htonl((uint32_t)(value >> 32));
#else
    return value;
#endif
}

static uint64_t mc_ntohll(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)ntohl((uint32_t)(value & 0xFFFFFFFFULL)) << 32) |
           ntohl((uint32_t)(value >> 32));
#else
    return value;
#endif
}

static int mc_is_valid_command(mc_command_t command) {
    return command >= MC_CMD_ERROR && command <= MC_CMD_AUTH;
}

int mc_build_header(mc_packet_header_t *out,
                    mc_command_t command,
                    const char *filename,
                    uint64_t payload_len) {
    if (!out) {
        errno = EINVAL;
        return -1;
    }

    if (!mc_is_valid_command(command)) {
        errno = EINVAL;
        return -1;
    }

    size_t filename_len = filename ? strlen(filename) : 0U;
    if (filename_len > MC_MAX_FILENAME_LEN) {
        errno = ENAMETOOLONG;
        return -1;
    }

    out->magic = MC_PROTOCOL_MAGIC;
    out->version = MC_PROTOCOL_VERSION;
    out->command = (uint8_t)command;
    out->filename_len = (uint32_t)filename_len;
    out->payload_len = payload_len;
    return 0;
}

int mc_validate_header(const mc_packet_header_t *header) {
    if (!header) {
        return -1;
    }

    if (header->magic != MC_PROTOCOL_MAGIC) {
        return -2;
    }

    if (header->version != MC_PROTOCOL_VERSION) {
        return -3;
    }

    if (!mc_is_valid_command((mc_command_t)header->command)) {
        return -4;
    }

    if (header->filename_len > MC_MAX_FILENAME_LEN) {
        return -5;
    }

    return 0;
}

void mc_header_host_to_network(mc_packet_header_t *header) {
    if (!header) {
        return;
    }

    header->magic = htonl(header->magic);
    header->filename_len = htonl(header->filename_len);
    header->payload_len = mc_htonll(header->payload_len);
}

void mc_header_network_to_host(mc_packet_header_t *header) {
    if (!header) {
        return;
    }

    header->magic = ntohl(header->magic);
    header->filename_len = ntohl(header->filename_len);
    header->payload_len = mc_ntohll(header->payload_len);
}

ssize_t mc_send_all(int fd, const void *buf, size_t len) {
    const uint8_t *cursor = (const uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t written = write(fd, cursor, remaining); /* write() 시스템 콜로 전송 */
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        cursor += written;
        remaining -= (size_t)written;
    }

    return (ssize_t)len;
}

ssize_t mc_recv_all(int fd, void *buf, size_t len) {
    uint8_t *cursor = (uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t count = read(fd, cursor, remaining); /* read() 시스템 콜로 수신 */
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (count == 0) {
            return (ssize_t)(len - remaining);
        }
        cursor += count;
        remaining -= (size_t)count;
    }

    return (ssize_t)len;
}

int mc_send_header(int fd, const mc_packet_header_t *header) {
    if (!header) {
        errno = EINVAL;
        return -1;
    }

    mc_packet_header_t tmp = *header;
    mc_header_host_to_network(&tmp);
    return mc_send_all(fd, &tmp, sizeof(tmp)) == (ssize_t)sizeof(tmp) ? 0 : -1;
}

int mc_recv_header(int fd, mc_packet_header_t *out) {
    if (!out) {
        errno = EINVAL;
        return -1;
    }

    ssize_t received = mc_recv_all(fd, out, sizeof(*out));
    if (received != (ssize_t)sizeof(*out)) {
        return -1;
    }

    mc_header_network_to_host(out);
    return mc_validate_header(out);
}
