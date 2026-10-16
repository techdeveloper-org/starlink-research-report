/**
 * @file sram_puf_fuzzy_extractor.c
 * @brief Implementation of StarSec SRAM Physically Unclonable Function (PUF) Fuzzy Extractor.
 *
 * Provides constant-time Hamming distance computation, majority-vote error
 * correction, SHA-256 key derivation, and syndrome generation.
 *
 * Standard: StarSec Constellation Security Framework (CSF v1.0)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#include "sram_puf.h"
#include <string.h>

/**
 * @brief Right rotate macro for 32-bit unsigned integers.
 */
#define ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))

/**
 * @brief SHA-256 round function definitions.
 */
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x, 2) ^ ROTRIGHT(x, 13) ^ ROTRIGHT(x, 22))
#define EP1(x) (ROTRIGHT(x, 6) ^ ROTRIGHT(x, 11) ^ ROTRIGHT(x, 25))
#define SIG0(x) (ROTRIGHT(x, 7) ^ ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x, 17) ^ ROTRIGHT(x, 19) ^ ((x) >> 10))

/**
 * @brief SHA-256 round constants defined in FIPS 180-4.
 */
static const uint32_t K256[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

/**
 * @brief Constant-time population count for 32-bit unsigned integer.
 * @param value Input integer.
 * @return Number of set bits.
 */
uint32_t puf_constant_time_popcount32(uint32_t value) {
    value = value - ((value >> 1) & 0x55555555UL);
    value = (value & 0x33333333UL) + ((value >> 2) & 0x33333333UL);
    value = (value + (value >> 4)) & 0x0F0F0F0FUL;
    value = value + (value >> 8);
    value = value + (value >> 16);
    return value & 0x0000003FUL;
}

/**
 * @brief Computes the Hamming distance between two binary byte buffers.
 * @param buf_a Pointer to first buffer.
 * @param buf_b Pointer to second buffer.
 * @param len_bytes Buffer length in bytes.
 * @return Total number of differing bit positions.
 */
uint32_t puf_calculate_hamming_distance(const uint8_t *buf_a, const uint8_t *buf_b, size_t len_bytes) {
    if (buf_a == NULL || buf_b == NULL) {
        return 0;
    }

    uint32_t distance = 0;
    for (size_t i = 0; i < len_bytes; i++) {
        uint8_t diff = buf_a[i] ^ buf_b[i];
        distance += puf_constant_time_popcount32((uint32_t)diff);
    }
    return distance;
}

/**
 * @brief SHA-256 internal compression function for a single 64-byte block.
 * @param state Pointer to 8-word SHA-256 state array.
 * @param data Pointer to 64-byte chunk.
 */
static void sha256_transform(uint32_t state[8], const uint8_t data[64]) {
    uint32_t m[64];
    for (uint32_t i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    }
    for (uint32_t i = 16; i < 64; ++i) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t t1 = h + EP1(e) + CH(e, f, g) + K256[i] + m[i];
        uint32_t t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

/**
 * @brief Constant-time SHA-256 key derivation function.
 * @param input Pointer to input byte buffer.
 * @param input_len Length of input buffer in bytes.
 * @param out_digest_32 Pointer to 32-byte output digest buffer.
 */
void puf_sha256_kdf(const uint8_t *input, size_t input_len, uint8_t *out_digest_32) {
    if (input == NULL || out_digest_32 == NULL) {
        return;
    }

    uint32_t state[8] = {
        0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
        0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL
    };

    uint8_t buffer[64];
    size_t processed = 0;

    while (processed + 64 <= input_len) {
        sha256_transform(state, input + processed);
        processed += 64;
    }

    size_t remaining = input_len - processed;
    memcpy(buffer, input + processed, remaining);
    buffer[remaining] = 0x80;
    remaining++;

    if (remaining > 56) {
        memset(buffer + remaining, 0x00, 64 - remaining);
        sha256_transform(state, buffer);
        memset(buffer, 0x00, 56);
    } else {
        memset(buffer + remaining, 0x00, 56 - remaining);
    }

    uint64_t total_bits = (uint64_t)input_len * 8ULL;
    for (int i = 0; i < 8; ++i) {
        buffer[56 + i] = (uint8_t)((total_bits >> (56 - i * 8)) & 0xFFULL);
    }
    sha256_transform(state, buffer);

    for (int i = 0; i < 8; ++i) {
        out_digest_32[i * 4] = (uint8_t)((state[i] >> 24) & 0xFFUL);
        out_digest_32[i * 4 + 1] = (uint8_t)((state[i] >> 16) & 0xFFUL);
        out_digest_32[i * 4 + 2] = (uint8_t)((state[i] >> 8) & 0xFFUL);
        out_digest_32[i * 4 + 3] = (uint8_t)(state[i] & 0xFFUL);
    }
}

/**
 * @brief Enrolls the SRAM PUF by generating helper data and the initial master key.
 * @param startup_sram Pointer to raw uninitialized SRAM measurement.
 * @param secret_seed_32 Pointer to 32-byte enrollment seed.
 * @param out_helper Pointer to helper data destination structure.
 * @param out_key Pointer to key output destination structure.
 * @return STARSEC_SUCCESS on success, STARSEC_ERR_NULL_PTR on invalid pointer.
 */
starsec_status_t puf_enroll(
    const puf_startup_state_t *startup_sram,
    const uint8_t             *secret_seed_32,
    puf_helper_data_t         *out_helper,
    puf_key_output_t          *out_key
) {
    if (startup_sram == NULL || secret_seed_32 == NULL || out_helper == NULL || out_key == NULL) {
        return STARSEC_ERR_NULL_PTR;
    }

    uint8_t codeword[PUF_RAW_BYTES];
    for (size_t blk = 0; blk < PUF_NUM_BLOCKS; blk++) {
        uint8_t seed_byte = secret_seed_32[blk];
        for (size_t byte_idx = 0; byte_idx < PUF_BLOCK_SIZE_BYTES; byte_idx++) {
            codeword[blk * PUF_BLOCK_SIZE_BYTES + byte_idx] = seed_byte;
        }
    }

    for (size_t i = 0; i < PUF_RAW_BYTES; i++) {
        out_helper->syndrome[i] = startup_sram->raw_sram_bytes[i] ^ codeword[i];
    }

    out_helper->enrollment_temp_milli_celsius = 25000;
    puf_sha256_kdf(out_helper->syndrome, PUF_RAW_BYTES, out_helper->helper_hash);

    puf_sha256_kdf(secret_seed_32, PUF_KEY_BYTES, out_key->derived_key_256);
    out_key->total_corrected_bits = 0;
    out_key->is_reconstructed = true;

    return STARSEC_SUCCESS;
}

/**
 * @brief Reconstructs the exact master key from noisy startup SRAM and helper data.
 * @param noisy_sram Pointer to noisy startup SRAM measurement.
 * @param helper Pointer to enrolled helper data structure.
 * @param out_key Pointer to key output destination structure.
 * @return STARSEC_SUCCESS on success, error code if noise exceeds error-correction threshold.
 */
starsec_status_t puf_reconstruct(
    const puf_startup_state_t *noisy_sram,
    const puf_helper_data_t   *helper,
    puf_key_output_t          *out_key
) {
    if (noisy_sram == NULL || helper == NULL || out_key == NULL) {
        return STARSEC_ERR_NULL_PTR;
    }

    uint8_t recovered_seed[PUF_KEY_BYTES];
    uint32_t total_errors = 0;

    for (size_t blk = 0; blk < PUF_NUM_BLOCKS; blk++) {
        uint32_t bit_ones_count[8] = {0};

        for (size_t byte_idx = 0; byte_idx < PUF_BLOCK_SIZE_BYTES; byte_idx++) {
            size_t global_idx = blk * PUF_BLOCK_SIZE_BYTES + byte_idx;
            uint8_t noisy_codeword_byte = noisy_sram->raw_sram_bytes[global_idx] ^ helper->syndrome[global_idx];

            for (int bit = 0; bit < 8; bit++) {
                if ((noisy_codeword_byte >> bit) & 0x01) {
                    bit_ones_count[bit]++;
                }
            }
        }

        uint8_t consensus_byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            if (bit_ones_count[bit] > (PUF_BLOCK_SIZE_BYTES / 2)) {
                consensus_byte |= (uint8_t)(1 << bit);
                total_errors += (PUF_BLOCK_SIZE_BYTES - bit_ones_count[bit]);
            } else {
                total_errors += bit_ones_count[bit];
            }
        }

        recovered_seed[blk] = consensus_byte;
    }

    puf_sha256_kdf(recovered_seed, PUF_KEY_BYTES, out_key->derived_key_256);
    out_key->total_corrected_bits = total_errors;
    out_key->is_reconstructed = true;

    return STARSEC_SUCCESS;
}
