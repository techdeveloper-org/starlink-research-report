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

/** @brief Hard cryptographic packet counter limit per session key before mandatory rekeying. */
#define STARSEC_MAX_PACKET_COUNTER_PER_KEY  4294957296ULL

/**
 * @brief Cryptographic operation status and lifecycle codes.
 */
typedef enum {
    STARSEC_CRYPTO_SUCCESS             = 0,
    STARSEC_CRYPTO_ERR_REKEY_REQUIRED  = 1,
    STARSEC_CRYPTO_ERR_NULL_PARAM      = 2,
    STARSEC_CRYPTO_ERR_AUTH_FAIL       = 3
} starsec_crypto_status_t;

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

/**
 * @brief Derives a collision-free 96-bit transmission nonce adhering to RFC 8439.
 *
 * Packs 32-bit session/boot identifier and 64-bit monotonic packet sequence:
 * Nonce[0..3] = Little-Endian 32-bit session_id
 * Nonce[4..11] = Little-Endian 64-bit sequence_number
 *
 * @param session_id Monotonic boot counter or negotiated session identifier.
 * @param sequence_number Monotonic 64-bit packet sequence number.
 * @param nonce Output buffer receiving 12-byte (96-bit) RFC 8439 nonce.
 * @return STARSEC_CRYPTO_SUCCESS on successful construction,
 *         STARSEC_CRYPTO_ERR_REKEY_REQUIRED if sequence_number exceeds ceiling,
 *         STARSEC_CRYPTO_ERR_NULL_PARAM if nonce pointer is NULL.
 */
starsec_crypto_status_t starsec_derive_nonce_v2(
    uint32_t session_id,
    uint64_t sequence_number,
    uint8_t  nonce[STARSEC_CHACHA20_NONCE_BYTES]
);

#ifdef __cplusplus
}
#endif

#endif /* STARSEC_CHACHA20_H */
