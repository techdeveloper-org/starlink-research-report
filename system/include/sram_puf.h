/**
 * @file sram_puf.h
 * @brief StarSec SRAM Physically Unclonable Function (PUF) Fuzzy Extractor Header.
 *
 * Implements hardware key enrollment, syndrome generation, error-correcting
 * code reconstruction, and SHA-256 key derivation without non-volatile storage.
 *
 * Standard: StarSec Constellation Security Framework (CSF v1.0)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#ifndef STARSEC_SRAM_PUF_H
#define STARSEC_SRAM_PUF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "tamper_zeroize.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Total uninitialized startup SRAM bits sampled at power-on. */
#define PUF_RAW_BITS                 4096UL

/** @brief Total raw SRAM bytes (4096 bits / 8). */
#define PUF_RAW_BYTES                512UL

/** @brief Length of the derived symmetric cryptographic key in bytes. */
#define PUF_KEY_BYTES                32UL

/** @brief Error-correcting block size in bytes (128 bits per codeword). */
#define PUF_BLOCK_SIZE_BYTES         16UL

/** @brief Total repetition blocks across raw SRAM array (512 / 16). */
#define PUF_NUM_BLOCKS               32UL

/** @brief Maximum bit flips correctable per 128-bit block under majority decoding. */
#define PUF_MAX_CORRECTABLE_BITS     3UL

/**
 * @brief Public helper data structure stored in public flash.
 */
typedef struct {
    uint8_t  syndrome[PUF_RAW_BYTES];           /**< Public helper data P = w XOR c */
    uint8_t  helper_hash[32];                   /**< SHA-256 integrity tag */
    int32_t  enrollment_temp_milli_celsius;     /**< Calibration temperature */
} puf_helper_data_t;

/**
 * @brief Sampled startup uninitialized SRAM state.
 */
typedef struct {
    uint8_t  raw_sram_bytes[PUF_RAW_BYTES];     /**< 512 bytes sampled at power-on */
} puf_startup_state_t;

/**
 * @brief Extracted cryptographic key and reconstruction metrics.
 */
typedef struct {
    uint8_t  derived_key_256[PUF_KEY_BYTES];    /**< Extracted 256-bit symmetric key */
    uint32_t total_corrected_bits;              /**< Total bit flips corrected */
    bool     is_reconstructed;                  /**< Extraction success flag */
} puf_key_output_t;

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
);

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
);

/**
 * @brief Computes the Hamming distance between two binary byte buffers.
 * @param buf_a Pointer to first buffer.
 * @param buf_b Pointer to second buffer.
 * @param len_bytes Buffer length in bytes.
 * @return Total number of differing bit positions.
 */
uint32_t puf_calculate_hamming_distance(const uint8_t *buf_a, const uint8_t *buf_b, size_t len_bytes);

/**
 * @brief Constant-time population count for 32-bit unsigned integer.
 * @param value Input integer.
 * @return Number of set bits (Hamming weight).
 */
uint32_t puf_constant_time_popcount32(uint32_t value);

/**
 * @brief Constant-time SHA-256 key derivation function.
 * @param input Pointer to input byte buffer.
 * @param input_len Length of input buffer in bytes.
 * @param out_digest_32 Pointer to 32-byte output digest buffer.
 */
void puf_sha256_kdf(const uint8_t *input, size_t input_len, uint8_t *out_digest_32);

#ifdef __cplusplus
}
#endif

#endif /* STARSEC_SRAM_PUF_H */
