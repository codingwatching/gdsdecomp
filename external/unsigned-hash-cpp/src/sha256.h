// Minimal SHA-256 implementation. Public domain.
//
// Based on the FIPS 180-4 specification. Implements only the streaming API we
// need: init / update / finalize. No HMAC, no algorithm dispatch.

#ifndef UNSIGNED_HASH_SHA256_H
#define UNSIGNED_HASH_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNSIGNED_HASH_SHA256_BLOCK_SIZE 32

typedef struct {
	uint8_t data[64];
	uint32_t datalen;
	uint64_t bitlen;
	uint32_t state[8];
} unsigned_hash_sha256_ctx;

void unsigned_hash_sha256_init(unsigned_hash_sha256_ctx *ctx);
void unsigned_hash_sha256_update(unsigned_hash_sha256_ctx *ctx, const uint8_t *data, size_t len);
void unsigned_hash_sha256_final(unsigned_hash_sha256_ctx *ctx, uint8_t hash[UNSIGNED_HASH_SHA256_BLOCK_SIZE]);

#ifdef __cplusplus
}
#endif

#endif // UNSIGNED_HASH_SHA256_H
