/**
 * @file starsec_chacha20.c
 * @brief Implementation of RFC 8439 ChaCha20 Block Cipher and One-Time Key Generator.
 *
 * Implements constant-time 20-round ChaCha20 block evaluation and RFC 8439 Section 2.6
 * one-time Poly1305 subkey generation for optical crosslink packet authentication.
 *
 * Standard: RFC 8439 Section 2.6, StarSec Constellation Security Framework (CSF v1.0)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#include "starsec_chacha20.h"
#include <string.h>

/**
 * @brief 32-bit left rotation primitive.
 * @param v Value to rotate.
 * @param n Number of bit positions to rotate left.
 * @return Rotated 32-bit integer.
 */
static inline uint32_t starsec_rotl32(uint32_t v, unsigned int n) {
    return (v << n) | (v >> (32U - n));
}

/**
 * @brief Loads a 32-bit unsigned integer from little-endian byte stream.
 * @param p Pointer to at least 4 contiguous bytes.
 * @return 32-bit unsigned integer in host representation.
 */
static inline uint32_t starsec_load32_le(const uint8_t *p) {
    return ((uint32_t)p[0])       |
           (((uint32_t)p[1]) << 8)  |
           (((uint32_t)p[2]) << 16) |
           (((uint32_t)p[3]) << 24);
}

/**
 * @brief Stores a 32-bit unsigned integer into little-endian byte stream.
 * @param p Pointer to output buffer of at least 4 bytes.
 * @param v 32-bit integer to store.
 */
static inline void starsec_store32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

/**
 * @brief Evaluates a single ChaCha20 Quarter-Round operation across 4 state words.
 * @param a Pointer to first state word.
 * @param b Pointer to second state word.
 * @param c Pointer to third state word.
 * @param d Pointer to fourth state word.
 */
static inline void starsec_chacha20_quarter_round(
    uint32_t *a,
    uint32_t *b,
    uint32_t *c,
    uint32_t *d
) {
    *a += *b; *d ^= *a; *d = starsec_rotl32(*d, 16U);
    *c += *d; *b ^= *c; *b = starsec_rotl32(*b, 12U);
    *a += *b; *d ^= *a; *d = starsec_rotl32(*d, 8U);
    *c += *d; *b ^= *c; *b = starsec_rotl32(*b, 7U);
}

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
) {
    if (out == NULL || key == NULL || nonce == NULL) {
        return;
    }

    uint32_t init_state[16];
    init_state[0] = 0x61707865U;
    init_state[1] = 0x3320646eU;
    init_state[2] = 0x79622d32U;
    init_state[3] = 0x6b206574U;

    for (size_t i = 0; i < 8; i++) {
        init_state[4 + i] = starsec_load32_le(key + (i * 4));
    }

    init_state[12] = counter;
    init_state[13] = starsec_load32_le(nonce + 0);
    init_state[14] = starsec_load32_le(nonce + 4);
    init_state[15] = starsec_load32_le(nonce + 8);

    uint32_t x[16];
    for (size_t i = 0; i < 16; i++) {
        x[i] = init_state[i];
    }

    for (size_t round = 0; round < 10; round++) {
        starsec_chacha20_quarter_round(&x[0], &x[4], &x[8],  &x[12]);
        starsec_chacha20_quarter_round(&x[1], &x[5], &x[9],  &x[13]);
        starsec_chacha20_quarter_round(&x[2], &x[6], &x[10], &x[14]);
        starsec_chacha20_quarter_round(&x[3], &x[7], &x[11], &x[15]);

        starsec_chacha20_quarter_round(&x[0], &x[5], &x[10], &x[15]);
        starsec_chacha20_quarter_round(&x[1], &x[6], &x[11], &x[12]);
        starsec_chacha20_quarter_round(&x[2], &x[7], &x[8],  &x[13]);
        starsec_chacha20_quarter_round(&x[3], &x[4], &x[9],  &x[14]);
    }

    for (size_t i = 0; i < 16; i++) {
        starsec_store32_le(out + (i * 4), x[i] + init_state[i]);
    }

    volatile uint32_t *p_x = (volatile uint32_t *)x;
    volatile uint32_t *p_init = (volatile uint32_t *)init_state;
    for (size_t i = 0; i < 16; i++) {
        p_x[i] = 0;
        p_init[i] = 0;
    }
}

/**
 * @brief Generates a cryptographically unique 32-byte one-time Poly1305 key.
 * @param master_key Pointer to 32-byte shared ISL master secret.
 * @param nonce Pointer to 12-byte unique transmission nonce.
 * @param one_time_key Output buffer receiving 32-byte one-time Poly1305 key.
 */
void starsec_chacha20_poly1305_key_gen(
    const uint8_t master_key[STARSEC_CHACHA20_KEY_BYTES],
    const uint8_t nonce[STARSEC_CHACHA20_NONCE_BYTES],
    uint8_t       one_time_key[STARSEC_ONE_TIME_KEY_BYTES]
) {
    if (master_key == NULL || nonce == NULL || one_time_key == NULL) {
        return;
    }

    uint8_t block[STARSEC_CHACHA20_BLOCK_BYTES];
    starsec_chacha20_block(block, master_key, 0U, nonce);

    memcpy(one_time_key, block, STARSEC_ONE_TIME_KEY_BYTES);

    volatile uint8_t *p_block = (volatile uint8_t *)block;
    for (size_t i = 0; i < STARSEC_CHACHA20_BLOCK_BYTES; i++) {
        p_block[i] = 0;
    }
}

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
) {
    if (nonce == NULL) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }

    if (sequence_number >= STARSEC_MAX_PACKET_COUNTER_PER_KEY) {
        return STARSEC_CRYPTO_ERR_REKEY_REQUIRED;
    }

    starsec_store32_le(nonce + 0, session_id);
    starsec_store32_le(nonce + 4, (uint32_t)(sequence_number & 0xFFFFFFFFULL));
    starsec_store32_le(nonce + 8, (uint32_t)((sequence_number >> 32) & 0xFFFFFFFFULL));

    return STARSEC_CRYPTO_SUCCESS;
}

