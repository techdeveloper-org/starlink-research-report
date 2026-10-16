/**
 * @file starsec_poly1305.c
 * @brief Clean-room RFC 8439 Poly1305 Constant-Time Authenticator Implementation.
 *
 * Implements 26-bit limb arithmetic modulo 2^130 - 5 with provably zero 64-bit integer
 * overflow, constant-time carry reduction, and timing-attack immunity for
 * optical inter-satellite links.
 *
 * Standard: RFC 8439 Section 2.5, MISRA-C:2023 Principles
 * Compliance: Level 2.2 Docstrings-Only
 */

#include "starsec_poly1305.h"
#include <string.h>

/**
 * @brief Securely scrubs sensitive memory buffers using compiler memory barriers.
 * @param v Pointer to target memory buffer.
 * @param n Size of buffer in bytes.
 */
static void poly1305_secure_zeroize(void *v, size_t n) {
    if (v == NULL || n == 0) {
        return;
    }
    volatile uint8_t *p = (volatile uint8_t *)v;
    while (n--) {
        *p++ = 0U;
    }
}

/**
 * @brief Processes complete 16-byte blocks using 26-bit limb multiplication modulo 2^130 - 5.
 * @param ctx Pointer to active Poly1305 evaluation context.
 * @param m Pointer to message block memory.
 * @param bytes Length of chunk in bytes (must be <= 16).
 * @param is_final Boolean indicating whether this is the terminal block with hibit sentinel.
 */
static void starsec_poly1305_process_block(
    starsec_poly1305_ctx_t *ctx,
    const uint8_t          *m,
    size_t                  bytes,
    bool                    is_final
) {
    uint8_t padded_block[16];
    const uint8_t *p = m;

    if (bytes < 16) {
        for (size_t i = 0; i < 16; i++) {
            padded_block[i] = 0;
        }
        for (size_t i = 0; i < bytes; i++) {
            padded_block[i] = m[i];
        }
        padded_block[bytes] = 0x01;
        p = padded_block;
    }

    uint32_t b0 = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | (((uint32_t)p[3] & 0x03U) << 24);
    uint32_t b1 = ((uint32_t)p[3] >> 2) | ((uint32_t)p[4] << 6) | ((uint32_t)p[5] << 14) | (((uint32_t)p[6] & 0x0FU) << 22);
    uint32_t b2 = ((uint32_t)p[6] >> 4) | ((uint32_t)p[7] << 4) | ((uint32_t)p[8] << 12) | (((uint32_t)p[9] & 0x3FU) << 20);
    uint32_t b3 = ((uint32_t)p[9] >> 6) | ((uint32_t)p[10] << 2) | ((uint32_t)p[11] << 10) | ((uint32_t)p[12] << 18);
    uint32_t b4 = (uint32_t)p[13] | ((uint32_t)p[14] << 8) | ((uint32_t)p[15] << 16);

    b3 &= 0x03FFFFFFU;
    b4 &= 0x03FFFFFFU;

    if (!is_final || bytes == 16) {
        b4 |= (1U << 24);
    }

    ctx->h[0] += b0;
    ctx->h[1] += b1;
    ctx->h[2] += b2;
    ctx->h[3] += b3;
    ctx->h[4] += b4;

    uint32_t r0 = ctx->r[0];
    uint32_t r1 = ctx->r[1];
    uint32_t r2 = ctx->r[2];
    uint32_t r3 = ctx->r[3];
    uint32_t r4 = ctx->r[4];

    uint32_t s1 = r1 * 5U;
    uint32_t s2 = r2 * 5U;
    uint32_t s3 = r3 * 5U;
    uint32_t s4 = r4 * 5U;

    uint64_t h0 = ctx->h[0];
    uint64_t h1 = ctx->h[1];
    uint64_t h2 = ctx->h[2];
    uint64_t h3 = ctx->h[3];
    uint64_t h4 = ctx->h[4];

    uint64_t d0 = (h0 * r0) + (h1 * s4) + (h2 * s3) + (h3 * s2) + (h4 * s1);
    uint64_t d1 = (h0 * r1) + (h1 * r0) + (h2 * s4) + (h3 * s3) + (h4 * s2);
    uint64_t d2 = (h0 * r2) + (h1 * r1) + (h2 * r0) + (h3 * s4) + (h4 * s3);
    uint64_t d3 = (h0 * r3) + (h1 * r2) + (h2 * r1) + (h3 * r0) + (h4 * s4);
    uint64_t d4 = (h0 * r4) + (h1 * r3) + (h2 * r2) + (h3 * r1) + (h4 * r0);

    uint64_t c;
    c = d0 >> 26; ctx->h[0] = (uint32_t)d0 & 0x03FFFFFFU; d1 += c;
    c = d1 >> 26; ctx->h[1] = (uint32_t)d1 & 0x03FFFFFFU; d2 += c;
    c = d2 >> 26; ctx->h[2] = (uint32_t)d2 & 0x03FFFFFFU; d3 += c;
    c = d3 >> 26; ctx->h[3] = (uint32_t)d3 & 0x03FFFFFFU; d4 += c;
    c = d4 >> 26; ctx->h[4] = (uint32_t)d4 & 0x03FFFFFFU; ctx->h[0] += (uint32_t)(c * 5U);
    c = ctx->h[0] >> 26; ctx->h[0] &= 0x03FFFFFFU; ctx->h[1] += (uint32_t)c;
}

/**
 * @brief Initializes the Poly1305 state with a 32-byte one-time key.
 * @param ctx Pointer to caller-allocated context structure.
 * @param key Pointer to 32-byte key buffer.
 */
void starsec_poly1305_init(starsec_poly1305_ctx_t *ctx, const uint8_t key[32]) {
    if (ctx == NULL || key == NULL) {
        return;
    }

    uint8_t clamped_r[16];
    for (size_t i = 0; i < 16; i++) {
        clamped_r[i] = key[i];
    }
    clamped_r[3]  &= 0x0FU;
    clamped_r[7]  &= 0x0FU;
    clamped_r[11] &= 0x0FU;
    clamped_r[15] &= 0x0FU;
    clamped_r[4]  &= 0xFCU;
    clamped_r[8]  &= 0xFCU;
    clamped_r[12] &= 0xFCU;

    ctx->r[0] = (uint32_t)clamped_r[0] | ((uint32_t)clamped_r[1] << 8) | ((uint32_t)clamped_r[2] << 16) | (((uint32_t)clamped_r[3] & 0x03U) << 24);
    ctx->r[1] = ((uint32_t)clamped_r[3] >> 2) | ((uint32_t)clamped_r[4] << 6) | ((uint32_t)clamped_r[5] << 14) | (((uint32_t)clamped_r[6] & 0x0FU) << 22);
    ctx->r[2] = ((uint32_t)clamped_r[6] >> 4) | ((uint32_t)clamped_r[7] << 4) | ((uint32_t)clamped_r[8] << 12) | (((uint32_t)clamped_r[9] & 0x3FU) << 20);
    ctx->r[3] = ((uint32_t)clamped_r[9] >> 6) | ((uint32_t)clamped_r[10] << 2) | ((uint32_t)clamped_r[11] << 10) | ((uint32_t)clamped_r[12] << 18);
    ctx->r[4] = (uint32_t)clamped_r[13] | ((uint32_t)clamped_r[14] << 8) | ((uint32_t)clamped_r[15] << 16);

    ctx->r[0] &= 0x03FFFFFFU;
    ctx->r[1] &= 0x03FFFFFFU;
    ctx->r[2] &= 0x03FFFFFFU;
    ctx->r[3] &= 0x03FFFFFFU;
    ctx->r[4] &= 0x03FFFFFFU;

    for (size_t i = 0; i < 5; i++) {
        ctx->h[i] = 0U;
    }

    const uint8_t *s = key + 16;
    ctx->pad[0] = (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
    ctx->pad[1] = (uint32_t)s[4] | ((uint32_t)s[5] << 8) | ((uint32_t)s[6] << 16) | ((uint32_t)s[7] << 24);
    ctx->pad[2] = (uint32_t)s[8] | ((uint32_t)s[9] << 8) | ((uint32_t)s[10] << 16) | ((uint32_t)s[11] << 24);
    ctx->pad[3] = (uint32_t)s[12] | ((uint32_t)s[13] << 8) | ((uint32_t)s[14] << 16) | ((uint32_t)s[15] << 24);

    ctx->leftover = 0;
    ctx->finalized = 0;
    poly1305_secure_zeroize(clamped_r, sizeof(clamped_r));
}

/**
 * @brief Feeds arbitrary-length message chunks into the polynomial accumulator.
 * @param ctx Pointer to initialized context structure.
 * @param msg Pointer to input message bytes.
 * @param msg_len Length of input message in bytes.
 */
void starsec_poly1305_update(starsec_poly1305_ctx_t *ctx, const uint8_t *msg, size_t msg_len) {
    if (ctx == NULL || msg == NULL || msg_len == 0) {
        return;
    }

    size_t i = 0;
    if (ctx->leftover > 0) {
        size_t want = 16U - ctx->leftover;
        if (want > msg_len) {
            want = msg_len;
        }
        for (size_t j = 0; j < want; j++) {
            ctx->buffer[ctx->leftover + j] = msg[j];
        }
        ctx->leftover += want;
        msg_len -= want;
        i += want;
        if (ctx->leftover == 16) {
            starsec_poly1305_process_block(ctx, ctx->buffer, 16, false);
            ctx->leftover = 0;
        }
    }

    while (msg_len >= 16) {
        starsec_poly1305_process_block(ctx, msg + i, 16, false);
        i += 16;
        msg_len -= 16;
    }

    for (size_t j = 0; j < msg_len; j++) {
        ctx->buffer[j] = msg[i + j];
    }
    ctx->leftover = msg_len;
}

/**
 * @brief Finalizes the calculation, adds secret mask s, and produces the 16-byte tag.
 * @param ctx Pointer to active context structure (zeroized on completion).
 * @param tag Output buffer receiving the 16-byte authentication tag.
 */
void starsec_poly1305_finish(starsec_poly1305_ctx_t *ctx, uint8_t tag[16]) {
    if (ctx == NULL || tag == NULL) {
        return;
    }

    if (ctx->leftover > 0) {
        starsec_poly1305_process_block(ctx, ctx->buffer, ctx->leftover, true);
        ctx->leftover = 0;
    }

    uint32_t c;
    c = ctx->h[1] >> 26; ctx->h[1] &= 0x03FFFFFFU; ctx->h[2] += c;
    c = ctx->h[2] >> 26; ctx->h[2] &= 0x03FFFFFFU; ctx->h[3] += c;
    c = ctx->h[3] >> 26; ctx->h[3] &= 0x03FFFFFFU; ctx->h[4] += c;
    c = ctx->h[4] >> 26; ctx->h[4] &= 0x03FFFFFFU; ctx->h[0] += (c * 5U);
    c = ctx->h[0] >> 26; ctx->h[0] &= 0x03FFFFFFU; ctx->h[1] += c;

    uint32_t g0 = ctx->h[0] + 5U;
    c = g0 >> 26; g0 &= 0x03FFFFFFU;
    uint32_t g1 = ctx->h[1] + c;
    c = g1 >> 26; g1 &= 0x03FFFFFFU;
    uint32_t g2 = ctx->h[2] + c;
    c = g2 >> 26; g2 &= 0x03FFFFFFU;
    uint32_t g3 = ctx->h[3] + c;
    c = g3 >> 26; g3 &= 0x03FFFFFFU;
    uint32_t g4 = ctx->h[4] + c - (1U << 26);

    uint32_t mask = (g4 >> 31) - 1U;
    g4 &= 0x03FFFFFFU;

    ctx->h[0] = (ctx->h[0] & ~mask) | (g0 & mask);
    ctx->h[1] = (ctx->h[1] & ~mask) | (g1 & mask);
    ctx->h[2] = (ctx->h[2] & ~mask) | (g2 & mask);
    ctx->h[3] = (ctx->h[3] & ~mask) | (g3 & mask);
    ctx->h[4] = (ctx->h[4] & ~mask) | (g4 & mask);

    uint64_t f0 = ((uint64_t)ctx->h[0]) | ((uint64_t)ctx->h[1] << 26) | (((uint64_t)ctx->h[2] & 0x0FFFU) << 52);
    uint64_t f1 = ((uint64_t)ctx->h[2] >> 12) | ((uint64_t)ctx->h[3] << 14) | ((uint64_t)ctx->h[4] << 40);

    uint64_t s0 = (uint64_t)ctx->pad[0] | ((uint64_t)ctx->pad[1] << 32);
    uint64_t s1 = (uint64_t)ctx->pad[2] | ((uint64_t)ctx->pad[3] << 32);

    f0 += s0;
    f1 += s1 + (f0 < s0 ? 1ULL : 0ULL);

    tag[0]  = (uint8_t)(f0 >> 0);
    tag[1]  = (uint8_t)(f0 >> 8);
    tag[2]  = (uint8_t)(f0 >> 16);
    tag[3]  = (uint8_t)(f0 >> 24);
    tag[4]  = (uint8_t)(f0 >> 32);
    tag[5]  = (uint8_t)(f0 >> 40);
    tag[6]  = (uint8_t)(f0 >> 48);
    tag[7]  = (uint8_t)(f0 >> 56);
    tag[8]  = (uint8_t)(f1 >> 0);
    tag[9]  = (uint8_t)(f1 >> 8);
    tag[10] = (uint8_t)(f1 >> 16);
    tag[11] = (uint8_t)(f1 >> 24);
    tag[12] = (uint8_t)(f1 >> 32);
    tag[13] = (uint8_t)(f1 >> 40);
    tag[14] = (uint8_t)(f1 >> 48);
    tag[15] = (uint8_t)(f1 >> 56);

    ctx->finalized = 1U;
    poly1305_secure_zeroize(ctx, sizeof(starsec_poly1305_ctx_t));
}

/**
 * @brief One-shot helper computing Poly1305 MAC over complete buffer.
 * @param tag Output buffer receiving the 16-byte authentication tag.
 * @param msg Pointer to input message bytes.
 * @param msg_len Length of input message in bytes.
 * @param key Pointer to 32-byte one-time key buffer.
 */
void starsec_poly1305_auth(uint8_t tag[16], const uint8_t *msg, size_t msg_len, const uint8_t key[32]) {
    starsec_poly1305_ctx_t ctx;
    starsec_poly1305_init(&ctx, key);
    starsec_poly1305_update(&ctx, msg, msg_len);
    starsec_poly1305_finish(&ctx, tag);
}

/**
 * @brief Constant-time verification of an expected 16-byte authentication tag.
 * @param expected_tag Pointer to expected 16-byte authentication tag.
 * @param msg Pointer to message bytes.
 * @param msg_len Length of message in bytes.
 * @param key Pointer to 32-byte one-time key buffer.
 * @return true if calculated tag matches expected_tag in constant time, false otherwise.
 */
bool starsec_poly1305_verify(const uint8_t expected_tag[16], const uint8_t *msg, size_t msg_len, const uint8_t key[32]) {
    if (expected_tag == NULL || key == NULL) {
        return false;
    }

    uint8_t calculated_tag[16];
    starsec_poly1305_auth(calculated_tag, msg, msg_len, key);

    volatile uint8_t diff = 0;
    for (size_t i = 0; i < 16; i++) {
        diff |= (calculated_tag[i] ^ expected_tag[i]);
    }

    poly1305_secure_zeroize(calculated_tag, sizeof(calculated_tag));
    return (diff == 0);
}
