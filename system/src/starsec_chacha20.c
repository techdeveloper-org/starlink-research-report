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
#include "starsec_poly1305.h"
#include <string.h>

/**
 * @brief Securely scrubs sensitive memory buffers using compiler memory barriers.
 * @param v Pointer to target memory buffer.
 * @param n Size of buffer in bytes.
 */
static void starsec_chacha20_secure_zero(void *v, size_t n) {
    if (v == NULL || n == 0) {
        return;
    }
    volatile uint8_t *p = (volatile uint8_t *)v;
    while (n--) {
        *p++ = 0U;
    }
}

/**
 * @brief Constant-time byte array comparison routine resisting timing side-channels.
 * @param a Pointer to first memory buffer.
 * @param b Pointer to second memory buffer.
 * @param len Length in bytes to compare.
 * @return 0 if buffers are bitwise identical, -1 otherwise.
 */
static int starsec_chacha20_const_time_memcmp(const uint8_t *a, const uint8_t *b, size_t len) {
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= (a[i] ^ b[i]);
    }
    return (diff == 0) ? 0 : -1;
}

/**
 * @brief Stores a 64-bit unsigned integer into little-endian byte stream.
 * @param p Pointer to output buffer of at least 8 bytes.
 * @param v 64-bit integer to store.
 */
static inline void starsec_store64_le(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
    p[4] = (uint8_t)((v >> 32) & 0xFFU);
    p[5] = (uint8_t)((v >> 40) & 0xFFU);
    p[6] = (uint8_t)((v >> 48) & 0xFFU);
    p[7] = (uint8_t)((v >> 56) & 0xFFU);
}

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

    starsec_chacha20_secure_zero(x, sizeof(x));
    starsec_chacha20_secure_zero(init_state, sizeof(init_state));
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

    starsec_chacha20_secure_zero(block, sizeof(block));
}

/**
 * @brief Derives a collision-free 96-bit transmission nonce adhering to RFC 8439.
 * @param session_id Monotonic boot counter or negotiated session identifier.
 * @param sequence_number Monotonic 64-bit packet sequence number.
 * @param nonce Output buffer receiving 12-byte (96-bit) RFC 8439 nonce.
 * @return STARSEC_CRYPTO_SUCCESS on success, error code otherwise.
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

/**
 * @brief Performs multi-block ChaCha20 keystream encryption/decryption (XOR).
 * @param key Pointer to 32-byte secret master key.
 * @param counter Initial 32-bit block sequence counter.
 * @param nonce Pointer to 12-byte (96-bit) transmission nonce.
 * @param in Pointer to input plaintext/ciphertext buffer (may be NULL if len == 0).
 * @param out Output buffer of at least len bytes receiving XORed result.
 * @param len Length of input/output stream in bytes.
 */
void starsec_chacha20_stream_xor(
    const uint8_t  key[STARSEC_CHACHA20_KEY_BYTES],
    uint32_t       counter,
    const uint8_t  nonce[STARSEC_CHACHA20_NONCE_BYTES],
    const uint8_t *in,
    uint8_t       *out,
    size_t         len
) {
    if (len == 0 || out == NULL || key == NULL || nonce == NULL) {
        return;
    }

    uint8_t block[STARSEC_CHACHA20_BLOCK_BYTES];
    size_t offset = 0;

    while (offset < len) {
        starsec_chacha20_block(block, key, counter, nonce);
        counter++;
        size_t chunk = len - offset;
        if (chunk > STARSEC_CHACHA20_BLOCK_BYTES) {
            chunk = STARSEC_CHACHA20_BLOCK_BYTES;
        }
        for (size_t i = 0; i < chunk; i++) {
            uint8_t in_byte = (in != NULL) ? in[offset + i] : 0U;
            out[offset + i] = in_byte ^ block[i];
        }
        offset += chunk;
    }

    starsec_chacha20_secure_zero(block, sizeof(block));
}

/**
 * @brief Executes RFC 8439 Section 2.8 ChaCha20-Poly1305 AEAD Authenticated Encryption.
 * @param key Pointer to 32-byte master secret key.
 * @param nonce Pointer to 12-byte (96-bit) unique transmission nonce.
 * @param aad Pointer to Additional Authenticated Data (may be NULL if aad_len == 0).
 * @param aad_len Length of AAD in bytes.
 * @param plaintext Pointer to plaintext data (may be NULL if plaintext_len == 0).
 * @param plaintext_len Length of plaintext in bytes.
 * @param ciphertext Output buffer receiving encrypted bytes.
 * @param tag Output buffer of at least 16 bytes receiving authentic Poly1305 tag.
 * @return STARSEC_CRYPTO_SUCCESS on success, STARSEC_CRYPTO_ERR_NULL_PARAM on invalid pointers.
 */
starsec_crypto_status_t starsec_aead_chacha20_poly1305_encrypt(
    const uint8_t  key[STARSEC_AEAD_KEY_BYTES],
    const uint8_t  nonce[STARSEC_AEAD_NONCE_BYTES],
    const uint8_t *aad,
    size_t         aad_len,
    const uint8_t *plaintext,
    size_t         plaintext_len,
    uint8_t       *ciphertext,
    uint8_t        tag[STARSEC_AEAD_TAG_BYTES]
) {
    if (key == NULL || nonce == NULL || tag == NULL) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }
    if (plaintext_len > 0 && (plaintext == NULL || ciphertext == NULL)) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }
    if (aad_len > 0 && aad == NULL) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }

    uint8_t otk[STARSEC_ONE_TIME_KEY_BYTES];
    starsec_chacha20_poly1305_key_gen(key, nonce, otk);

    if (plaintext_len > 0) {
        starsec_chacha20_stream_xor(key, 1U, nonce, plaintext, ciphertext, plaintext_len);
    }

    starsec_poly1305_ctx_t poly_ctx;
    starsec_poly1305_init(&poly_ctx, otk);

    static const uint8_t zero_padding[16] = {0};

    if (aad_len > 0) {
        starsec_poly1305_update(&poly_ctx, aad, aad_len);
        size_t pad_aad = (16U - (aad_len % 16U)) % 16U;
        if (pad_aad > 0) {
            starsec_poly1305_update(&poly_ctx, zero_padding, pad_aad);
        }
    }

    if (plaintext_len > 0) {
        starsec_poly1305_update(&poly_ctx, ciphertext, plaintext_len);
        size_t pad_ct = (16U - (plaintext_len % 16U)) % 16U;
        if (pad_ct > 0) {
            starsec_poly1305_update(&poly_ctx, zero_padding, pad_ct);
        }
    }

    uint8_t len_block[16];
    starsec_store64_le(len_block + 0, (uint64_t)aad_len);
    starsec_store64_le(len_block + 8, (uint64_t)plaintext_len);
    starsec_poly1305_update(&poly_ctx, len_block, 16U);

    starsec_poly1305_finish(&poly_ctx, tag);

    starsec_chacha20_secure_zero(otk, sizeof(otk));
    starsec_chacha20_secure_zero(len_block, sizeof(len_block));
    starsec_chacha20_secure_zero(&poly_ctx, sizeof(poly_ctx));

    return STARSEC_CRYPTO_SUCCESS;
}

/**
 * @brief Executes RFC 8439 Section 2.8 ChaCha20-Poly1305 AEAD Authenticated Decryption.
 * @param key Pointer to 32-byte master secret key.
 * @param nonce Pointer to 12-byte (96-bit) unique transmission nonce.
 * @param aad Pointer to Additional Authenticated Data (may be NULL if aad_len == 0).
 * @param aad_len Length of AAD in bytes.
 * @param ciphertext Pointer to ciphertext data (may be NULL if ciphertext_len == 0).
 * @param ciphertext_len Length of ciphertext in bytes.
 * @param tag Expected 16-byte Poly1305 tag.
 * @param plaintext Output buffer receiving decrypted bytes on successful authentication.
 * @return STARSEC_CRYPTO_SUCCESS on success, STARSEC_CRYPTO_ERR_AUTH_FAIL on tag mismatch.
 */
starsec_crypto_status_t starsec_aead_chacha20_poly1305_decrypt(
    const uint8_t  key[STARSEC_AEAD_KEY_BYTES],
    const uint8_t  nonce[STARSEC_AEAD_NONCE_BYTES],
    const uint8_t *aad,
    size_t         aad_len,
    const uint8_t *ciphertext,
    size_t         ciphertext_len,
    const uint8_t  tag[STARSEC_AEAD_TAG_BYTES],
    uint8_t       *plaintext
) {
    if (key == NULL || nonce == NULL || tag == NULL) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }
    if (ciphertext_len > 0 && (ciphertext == NULL || plaintext == NULL)) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }
    if (aad_len > 0 && aad == NULL) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }

    uint8_t otk[STARSEC_ONE_TIME_KEY_BYTES];
    starsec_chacha20_poly1305_key_gen(key, nonce, otk);

    starsec_poly1305_ctx_t poly_ctx;
    starsec_poly1305_init(&poly_ctx, otk);

    static const uint8_t zero_padding[16] = {0};

    if (aad_len > 0) {
        starsec_poly1305_update(&poly_ctx, aad, aad_len);
        size_t pad_aad = (16U - (aad_len % 16U)) % 16U;
        if (pad_aad > 0) {
            starsec_poly1305_update(&poly_ctx, zero_padding, pad_aad);
        }
    }

    if (ciphertext_len > 0) {
        starsec_poly1305_update(&poly_ctx, ciphertext, ciphertext_len);
        size_t pad_ct = (16U - (ciphertext_len % 16U)) % 16U;
        if (pad_ct > 0) {
            starsec_poly1305_update(&poly_ctx, zero_padding, pad_ct);
        }
    }

    uint8_t len_block[16];
    starsec_store64_le(len_block + 0, (uint64_t)aad_len);
    starsec_store64_le(len_block + 8, (uint64_t)ciphertext_len);
    starsec_poly1305_update(&poly_ctx, len_block, 16U);

    uint8_t calculated_tag[STARSEC_AEAD_TAG_BYTES];
    starsec_poly1305_finish(&poly_ctx, calculated_tag);

    int cmp = starsec_chacha20_const_time_memcmp(calculated_tag, tag, STARSEC_AEAD_TAG_BYTES);

    starsec_chacha20_secure_zero(otk, sizeof(otk));
    starsec_chacha20_secure_zero(len_block, sizeof(len_block));
    starsec_chacha20_secure_zero(calculated_tag, sizeof(calculated_tag));
    starsec_chacha20_secure_zero(&poly_ctx, sizeof(poly_ctx));

    if (cmp != 0) {
        if (plaintext != NULL && ciphertext_len > 0) {
            starsec_chacha20_secure_zero(plaintext, ciphertext_len);
        }
        return STARSEC_CRYPTO_ERR_AUTH_FAIL;
    }

    if (ciphertext_len > 0) {
        starsec_chacha20_stream_xor(key, 1U, nonce, ciphertext, plaintext, ciphertext_len);
    }

    return STARSEC_CRYPTO_SUCCESS;
}


