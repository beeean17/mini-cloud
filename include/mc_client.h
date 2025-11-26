#ifndef MC_CLIENT_H
#define MC_CLIENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *host;
    uint16_t port;
} mc_client_config_t;

int mc_client_run(const mc_client_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* MC_CLIENT_H */
