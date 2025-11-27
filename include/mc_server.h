#ifndef MC_SERVER_H
#define MC_SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t port;
    int backlog;
    const char *storage_dir;
    const char *auth_token;    /* optional shared secret, NULL to disable */
    uint64_t max_upload_bytes; /* 0 means unlimited */
} mc_server_config_t;

int mc_server_run(const mc_server_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* MC_SERVER_H */
