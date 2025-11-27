#ifndef MC_PROTOCOL_H
#define MC_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Mini Cloud wire protocol constants
 */
#define MC_PROTOCOL_VERSION 1
#define MC_PROTOCOL_MAGIC   0x4D434C44U /* 'MCLD' */
#define MC_MAX_FILENAME_LEN 255

/**
 * Commands supported by the Mini Cloud protocol.
 */
typedef enum {
    MC_CMD_ERROR = 0,
    MC_CMD_UPLOAD = 1,
    MC_CMD_DOWNLOAD = 2,
    MC_CMD_LIST = 3,
    MC_CMD_QUIT = 4,
    MC_CMD_AUTH = 5
} mc_command_t;

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;        /* constant to detect corruption */
    uint8_t  version;      /* protocol version */
    uint8_t  command;      /* mc_command_t */
    uint32_t filename_len; /* bytes, not including null terminator */
    uint64_t payload_len;  /* bytes of payload that follow */
} mc_packet_header_t;
#pragma pack(pop)

/**
 * Lightweight descriptor after parsing the header and optional filename.
 */
typedef struct {
    mc_packet_header_t header;
    char filename[MC_MAX_FILENAME_LEN + 1];
} mc_packet_info_t;

int mc_build_header(mc_packet_header_t *out,
                    mc_command_t command,
                    const char *filename,
                    uint64_t payload_len);

int mc_validate_header(const mc_packet_header_t *header);

void mc_header_host_to_network(mc_packet_header_t *header);
void mc_header_network_to_host(mc_packet_header_t *header);

ssize_t mc_send_all(int fd, const void *buf, size_t len);
ssize_t mc_recv_all(int fd, void *buf, size_t len);
int mc_send_header(int fd, const mc_packet_header_t *header);
int mc_recv_header(int fd, mc_packet_header_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MC_PROTOCOL_H */
