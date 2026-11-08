/**
 * @file include/cdsl/util/sha256.h
 * @brief Zero-dependency SHA-256 implementation for deterministic version hashing.
 */

#ifndef CDSL_UTIL_SHA256_H
#define CDSL_UTIL_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t state[8];
	uint64_t count;
	uint8_t buffer[64];
} cdsl_sha256_ctx_t;

/**
 * @brief Initialize SHA-256 context.
 */
void cdsl_sha256_init(cdsl_sha256_ctx_t* ctx);

/**
 * @brief Update SHA-256 context with input data.
 */
void cdsl_sha256_update(cdsl_sha256_ctx_t* ctx, const void* data, size_t len);

/**
 * @brief Finalize SHA-256 context and retrieve 32-byte digest.
 */
void cdsl_sha256_final(cdsl_sha256_ctx_t* ctx, uint8_t digest[32]);

/**
 * @brief Compute hex-encoded SHA-256 hash of a buffer (64 characters + null terminator).
 * @param data Input data buffer
 * @param len Length in bytes
 * @param hex_out Output buffer of at least 65 bytes
 */
void cdsl_sha256_hash_buffer(const void* data, size_t len, char hex_out[65]);

/**
 * @brief Compute hex-encoded SHA-256 hash of a null-terminated string.
 * @param str Input string
 * @param hex_out Output buffer of at least 65 bytes
 */
void cdsl_sha256_hash_string(const char* str, char hex_out[65]);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_UTIL_SHA256_H */
