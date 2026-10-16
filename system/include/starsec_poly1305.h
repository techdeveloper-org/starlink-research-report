/**
 * @file starsec_poly1305.h
 * @brief RFC 8439 Poly1305 One-Time Authenticator Interface.
 *
 * Provides wire-speed, constant-time 128-bit cryptographic message authentication
 * for StarSec optical inter-satellite links (ISL) using 26-bit limb arithmetic
 * modulo 2^130 - 5.
 *
 * Standard: RFC 8439 Section 2.5, StarSec CSF v1.0
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#ifndef STARSEC_POLY1305_H
#define STARSEC_POLY1305_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Key length in bytes for Poly1305 (16-byte r + 16-byte s). */
#define STARSEC_POLY1305_KEY_BYTES   32U

/** @brief Authentication tag length in bytes (128-bit MAC). */
#define STARSEC_POLY1305_TAG_BYTES   16U

/** @brief Internal block size in bytes for polynomial evaluation. */
#define STARSEC_POLY1305_BLOCK_BYTES 16U

/**
 * @brief Poly1305 evaluation context holding state across incremental blocks.
 */
typedef struct {
    uint32_t r[5];       /**< Clamped 26-bit evaluation key limbs */
    uint32_t h[5];       /**< 26-bit accumulator limbs modulo 2^130-5 */
    uint32_t pad[4];     /**< 32-bit secret mask s */
    size_t   leftover;   /**< Unprocessed byte count in buffer */
    uint8_t  buffer[16]; /**< Partial block accumulator */
    uint8_t  finalized;  /**< State completion flag */
} starsec_poly1305_ctx_t;

/**
 * @brief Initializes the Poly1305 state with a 32-byte one-time key.
 * @param ctx Pointer to the caller-allocated context structure.
 * @param key Pointer to 32-byte key buffer (first 16 bytes: r, last 16 bytes: s).
 */
void starsec_poly1305_init(starsec_poly1305_ctx_t *ctx, const uint8_t key[32]);

/**
 * @brief Feeds arbitrary-length message chunks into the polynomial accumulator.
 * @param ctx Pointer to the initialized context structure.
 * @param msg Pointer to input message bytes.
 * @param msg_len Length of input message in bytes.
 */
void starsec_poly1305_update(starsec_poly1305_ctx_t *ctx, const uint8_t *msg, size_t msg_len);

/**
 * @brief Finalizes the calculation, adds secret mask s, and produces the 16-byte tag.
 * @param ctx Pointer to active context structure (zeroized on completion).
 * @param tag Output buffer receiving the 16-byte authentication tag.
 */
void starsec_poly1305_finish(starsec_poly1305_ctx_t *ctx, uint8_t tag[16]);

/**
 * @brief One-shot helper computing Poly1305 MAC over complete buffer.
 * @param tag Output buffer receiving the 16-byte authentication tag.
 * @param msg Pointer to input message bytes.
 * @param msg_len Length of input message in bytes.
 * @param key Pointer to 32-byte one-time key buffer.
 */
void starsec_poly1305_auth(uint8_t tag[16], const uint8_t *msg, size_t msg_len, const uint8_t key[32]);

/**
 * @brief Constant-time verification of an expected 16-byte authentication tag.
 * @param expected_tag Pointer to expected 16-byte authentication tag.
 * @param msg Pointer to message bytes.
 * @param msg_len Length of message in bytes.
 * @param key Pointer to 32-byte one-time key buffer.
 * @return true if calculated tag matches expected_tag in constant time, false otherwise.
 */
bool starsec_poly1305_verify(const uint8_t expected_tag[16], const uint8_t *msg, size_t msg_len, const uint8_t key[32]);

#ifdef __cplusplus
}
#endif

#endif /* STARSEC_POLY1305_H */
