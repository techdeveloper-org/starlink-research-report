/**
 * @file starsec_chacha20.h
 * @brief RFC 8439 ChaCha20 Stream Cipher and Poly1305 One-Time Key Generator.
 *
 * Implements wire-speed, constant-time 20-round ChaCha20 block evaluation and
 * cryptographically secure one-time Poly1305 subkey generation (RFC 8439 Section 2.6)
 * for StarSec optical inter-satellite link (ISL) packet authentication.
 *
 * Standard: RFC 8439 Section 2.6, StarSec Constellation Security Framework (CSF v1.0)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#ifndef STARSEC_CHACHA20_H
#define STARSEC_CHACHA20_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Master symmetric key size in bytes for ChaCha20 (256-bit). */
#define STARSEC_CHACHA20_KEY_BYTES      32U

/** @brief Standard RFC 8439 nonce size in bytes (96-bit). */
#define STARSEC_CHACHA20_NONCE_BYTES    12U

/** @brief ChaCha20 block output size in bytes (512-bit). */
#define STARSEC_CHACHA20_BLOCK_BYTES    64U

/** @brief Size of derived one-time Poly1305 key (16-byte r + 16-byte s). */
#define STARSEC_ONE_TIME_KEY_BYTES      32U

/**
 * @brief Internal 512-bit ChaCha20 state structure holding 16 32-bit words.
 */
typedef struct {
    uint32_t state[16];
} starsec_chacha20_state_t;

/**
 * @brief Evaluates 20 rounds of ChaCha20 producing a 64-byte pseudorandom block.
 * @param out Output buffer of at least 64 bytes receiving the generated block.
 * @param key Pointer to 32-byte master secret key.
 * @param counter 32-bit block sequence counter.
 * @param nonce Pointer to 12-byte (96-bit) unique transmission nonce.
 */
void starsec_chacha20_block(
    uint8_t        out[STARSEC_CHACHA20_BLOCK_BYTES],
    const uint8_t  key[STARSEC_CHACHA20_KEY_BYTES],
    uint32_t       counter,
    const uint8_t  nonce[STARSEC_CHACHA20_NONCE_BYTES]
);

/**
 * @brief Generates a cryptographically unique 32-byte one-time Poly1305 key.
 *
 * Executes RFC 8439 Section 2.6 key generation:
 * block = chacha20_block(master_key, counter=0, nonce)
 * one_time_key = block[0..31]
 *
 * @param master_key Pointer to 32-byte shared ISL master secret.
 * @param nonce Pointer to 12-byte unique transmission nonce.
 * @param one_time_key Output buffer receiving 32-byte one-time Poly1305 key.
 */
void starsec_chacha20_poly1305_key_gen(
    const uint8_t master_key[STARSEC_CHACHA20_KEY_BYTES],
    const uint8_t nonce[STARSEC_CHACHA20_NONCE_BYTES],
    uint8_t       one_time_key[STARSEC_ONE_TIME_KEY_BYTES]
);

#ifdef __cplusplus
}
#endif

#endif /* STARSEC_CHACHA20_H */
