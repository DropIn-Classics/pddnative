/* sha256.h - SHA-256 */
#ifndef PD_SHA256_H
#define PD_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    int n;
} Sha256;

void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, const uint8_t *p, size_t n);
void sha256_final(Sha256 *s, uint8_t out[32]);
void sha256(const void *data, size_t n, uint8_t out[32]);

#endif
