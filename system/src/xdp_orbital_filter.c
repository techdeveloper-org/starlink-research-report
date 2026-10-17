/**
 * @file xdp_orbital_filter.c
 * @brief Implementation of StarSec In-Orbit Laser Link eBPF / XDP High-Performance Packet Filter.
 *
 * Implements wire-speed packet inspection, constant-time RFC 8439 Poly1305 MAC
 * validation, propagation window enforcement, and rogue node isolation.
 *
 * Standard: StarSec Constellation Security Framework (CSF v1.0)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#include "xdp_orbital.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief Zeroizes memory using a volatile pointer and compiler optimization barrier.
 *
 * Prevents Dead-Store Elimination (DSE) optimizations across all optimization levels
 * (-O2, -O3, -flto) to ensure cryptographic keys and sensitive nonces are wiped.
 *
 * @param v Pointer to memory buffer to zeroize.
 * @param n Size of buffer in bytes.
 */
static inline void starsec_secure_zero(void *v, size_t n) {
    if (v == NULL) {
        return;
    }
    volatile uint8_t *p = (volatile uint8_t *)v;
    while (n--) {
        *p++ = 0;
    }
    __asm__ __volatile__("" : : "r"(v) : "memory");
}

/**
 * @brief Resets all diagnostic telemetry counters to zero.
 * @param stats Pointer to telemetry statistics structure.
 */
void xdp_reset_filter_stats(xdp_filter_stats_t *stats) {
    if (stats == NULL) {
        return;
    }
    stats->dropped_bad_magic = 0;
    stats->dropped_epoch_drift = 0;
    stats->dropped_unauthorized_node = 0;
    stats->dropped_invalid_auth = 0;
    stats->dropped_clock_unavailable = 0;
    stats->dropped_stale_session = 0;
    stats->passed_frames = 0;
}

/**
 * @brief Stage 1 (Read-Only Pre-Filter): Speculatively verifies packet sequence against window.
 *
 * Checks if session ID and sequence number are eligible for Tier 2 crypto verification
 * without advancing the window base or mutating the bitmap. Prevents unauthenticated
 * spoofed frames from poisoning the sliding window state.
 *
 * @param state Pointer to current anti-replay state for peer node.
 * @param session_id Ingress packet session identifier.
 * @param seq_num Ingress packet sequence number.
 * @return true if packet is within or ahead of window and not a duplicate, false if obsolete/duplicate.
 */
bool xdp_check_replay_window_speculative(
    const isl_anti_replay_state_t *state,
    uint32_t                       session_id,
    uint64_t                       seq_num
) {
    if (state == NULL) {
        return false;
    }

    if (state->session_id != 0 && session_id < state->session_id) {
        return false;
    }

    if (session_id > state->session_id) {
        return true;
    }

    if (state->window_base_seq == 0 && state->window_bitmap == 0) {
        return true;
    }

    if (seq_num > state->window_base_seq) {
        return true;
    }

    uint64_t diff = state->window_base_seq - seq_num;
    if (diff >= STARSEC_SLIDING_WINDOW_SIZE) {
        return false;
    }

    if ((state->window_bitmap & (1ULL << diff)) != 0) {
        return false;
    }

    return true;
}

/**
 * @brief Stage 2 (Post-Auth Commit): Commits authenticated sequence into sliding window.
 *
 * Invoked ONLY after RFC 8439 Poly1305 MAC verification succeeds in constant time.
 * Advances window_base_seq, updates session_id on rollover, and shifts window_bitmap accordingly.
 *
 * @param state Pointer to anti-replay state to update.
 * @param session_id Authenticated packet session identifier.
 * @param seq_num Authenticated packet sequence number.
 */
void xdp_commit_replay_window_authenticated(
    isl_anti_replay_state_t *state,
    uint32_t                 session_id,
    uint64_t                 seq_num
) {
    if (state == NULL) {
        return;
    }

    if (session_id > state->session_id || (state->window_base_seq == 0 && state->window_bitmap == 0)) {
        state->session_id = session_id;
        state->window_base_seq = seq_num;
        state->window_bitmap = 1ULL;
        return;
    }

    if (session_id < state->session_id) {
        return;
    }

    if (seq_num > state->window_base_seq) {
        uint64_t delta = seq_num - state->window_base_seq;
        if (delta < STARSEC_SLIDING_WINDOW_SIZE) {
            state->window_bitmap = (state->window_bitmap << delta) | 1ULL;
        } else {
            state->window_bitmap = 1ULL;
        }
        state->window_base_seq = seq_num;
    } else {
        uint64_t diff = state->window_base_seq - seq_num;
        if (diff < STARSEC_SLIDING_WINDOW_SIZE) {
            state->window_bitmap |= (1ULL << diff);
        }
    }
}

/**
 * @brief Atomically commits authenticated sequence into sliding window state for multi-core SMP safety.
 * @param state Pointer to 64-byte aligned anti-replay state.
 * @param session_id Authenticated packet session identifier.
 * @param seq_num Authenticated packet sequence number.
 * @return true if state was committed atomically, false on conflict/failure.
 */
bool xdp_commit_replay_window_atomic(
    isl_anti_replay_state_t *state,
    uint32_t                 session_id,
    uint64_t                 seq_num
) {
    if (state == NULL) {
        return false;
    }

    starsec_spin_lock(&state->lock);

    if (session_id < state->session_id) {
        starsec_spin_unlock(&state->lock);
        return false;
    }

    if (session_id > state->session_id) {
        state->session_id = session_id;
        state->window_base_seq = seq_num;
        state->window_bitmap = 1ULL;
        starsec_spin_unlock(&state->lock);
        return true;
    }

    if (state->window_base_seq == 0 && state->window_bitmap == 0) {
        state->window_base_seq = seq_num;
        state->window_bitmap = 1ULL;
        starsec_spin_unlock(&state->lock);
        return true;
    }

    if (seq_num > state->window_base_seq) {
        uint64_t diff = seq_num - state->window_base_seq;
        if (diff < STARSEC_SLIDING_WINDOW_SIZE) {
            state->window_bitmap = (state->window_bitmap << diff) | 1ULL;
        } else {
            state->window_bitmap = 1ULL;
        }
        state->window_base_seq = seq_num;
        starsec_spin_unlock(&state->lock);
        return true;
    }

    uint64_t diff = state->window_base_seq - seq_num;
    if (diff >= STARSEC_SLIDING_WINDOW_SIZE) {
        starsec_spin_unlock(&state->lock);
        return false;
    }

    uint64_t mask = (1ULL << diff);
    if ((state->window_bitmap & mask) != 0) {
        starsec_spin_unlock(&state->lock);
        return false;
    }

    state->window_bitmap |= mask;
    starsec_spin_unlock(&state->lock);
    return true;
}

/**
 * @brief Validates packet transmit timestamp against receiver's trusted local clock.
 *
 * Ensures packet was transmitted within [-max_past_drift_ms, +max_future_skew_ms]
 * relative to local spacecraft receiver time. Rejects stale bursts and future timestamps.
 *
 * @param tx_epoch_ms Packet transmit epoch in milliseconds.
 * @param clock_ref Pointer to trusted local receiver clock state.
 * @return true if timestamp is fresh and within bounds, false otherwise.
 */
bool xdp_validate_local_freshness(
    uint64_t                         tx_epoch_ms,
    const starsec_time_sync_state_t *clock_ref
) {
    if (clock_ref == NULL || clock_ref->local_epoch_ms == 0) {
        return false;
    }

    if (tx_epoch_ms > clock_ref->local_epoch_ms) {
        uint64_t diff_future = tx_epoch_ms - clock_ref->local_epoch_ms;
        if (diff_future > clock_ref->max_future_skew_ms) {
            return false;
        }
    } else {
        uint64_t diff_past = clock_ref->local_epoch_ms - tx_epoch_ms;
        if (diff_past > clock_ref->max_past_drift_ms) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Verifies that transmit timestamp falls within acceptable propagation window.
 * @param tx_ms Transmit epoch timestamp in milliseconds.
 * @param local_ms Local spacecraft epoch timestamp in milliseconds.
 * @param max_drift_ms Maximum permitted temporal drift.
 * @return true if timestamp is valid and within bounds, false otherwise.
 */
bool xdp_validate_epoch_window(uint64_t tx_ms, uint64_t local_ms, uint64_t max_drift_ms) {
    uint64_t diff = (tx_ms > local_ms) ? (tx_ms - local_ms) : (local_ms - tx_ms);
    return (diff <= max_drift_ms);
}

/**
 * @brief Constructs a deterministic 96-bit (12-byte) nonce from packet header fields.
 *
 * Nonce layout (RFC 8439 Section 2.6):
 * - Bytes 0..3:  session_id (little-endian 32-bit)
 * - Bytes 4..11: sequence_number (little-endian 64-bit)
 *
 * @param hdr Pointer to StarSec packet header.
 * @param nonce Output buffer of at least 12 bytes receiving the nonce.
 * @return STARSEC_CRYPTO_SUCCESS on success,
 *         STARSEC_CRYPTO_ERR_REKEY_REQUIRED if sequence_number >= STARSEC_MAX_PACKET_COUNTER_PER_KEY,
 *         STARSEC_CRYPTO_ERR_NULL_PARAM if hdr or nonce is NULL.
 */
starsec_crypto_status_t xdp_derive_isl_nonce(
    const starsec_isl_header_t *hdr,
    uint8_t                     nonce[STARSEC_CHACHA20_NONCE_BYTES]
) {
    if (hdr == NULL || nonce == NULL) {
        if (nonce != NULL) {
            starsec_secure_zero(nonce, STARSEC_CHACHA20_NONCE_BYTES);
        }
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }

    starsec_crypto_status_t status = starsec_derive_nonce_v2(hdr->session_id, hdr->sequence_number, nonce);
    if (status != STARSEC_CRYPTO_SUCCESS) {
        starsec_secure_zero(nonce, STARSEC_CHACHA20_NONCE_BYTES);
    }
    return status;
}

/**
 * @brief Generates an authentic RFC 8439 Poly1305 128-bit MAC using a derived one-time key.
 *
 * Employs RFC 8439 Section 2.6 ChaCha20 block 0 key derivation to produce an
 * ephemeral, single-use 32-byte subkey from master_key_32 and the packet's
 * unique 96-bit transmission nonce. The one-time subkey is zeroized immediately.
 *
 * @param hdr Pointer to StarSec packet header whose auth_tag field will be populated.
 * @param payload Pointer to packet payload bytes (may be NULL if payload_len == 0).
 * @param payload_len Length of payload in bytes.
 * @param master_key_32 Pointer to 32-byte shared optical link master secret.
 * @return STARSEC_CRYPTO_SUCCESS on success,
 *         STARSEC_CRYPTO_ERR_REKEY_REQUIRED if sequence ceiling is reached,
 *         STARSEC_CRYPTO_ERR_NULL_PARAM on NULL inputs.
 */
/**
 * @brief Stores a 64-bit unsigned integer into little-endian byte stream.
 * @param p Pointer to output buffer of at least 8 bytes.
 * @param v 64-bit integer to store.
 */
static inline void starsec_store64_le_filter(uint8_t *p, uint64_t v) {
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
 * @brief Generates an authentic RFC 8439 Poly1305 128-bit MAC using a derived one-time key.
 *
 * Employs RFC 8439 Section 2.6 ChaCha20 block 0 key derivation and Section 2.8 AEAD framing:
 * - AAD: 32 bytes (Header fields 0..29 + payload_len 2 bytes).
 * - AAD Padding: 0 bytes (32 is multiple of 16).
 * - Payload: packet payload bytes.
 * - Payload Padding: 16 - (payload_len % 16) zero bytes if non-multiple.
 * - Length Block: 8 bytes LE aad_len + 8 bytes LE payload_len.
 * Ephemeral subkeys and internal contexts are zeroized upon completion.
 *
 * @param hdr Pointer to StarSec packet header whose auth_tag field will be populated.
 * @param payload Pointer to packet payload bytes (may be NULL if payload_len == 0).
 * @param payload_len Length of payload in bytes.
 * @param master_key_32 Pointer to 32-byte shared optical link master secret.
 * @return STARSEC_CRYPTO_SUCCESS on success,
 *         STARSEC_CRYPTO_ERR_REKEY_REQUIRED if sequence ceiling is reached,
 *         STARSEC_CRYPTO_ERR_NULL_PARAM on NULL inputs.
 */
starsec_crypto_status_t xdp_generate_isl_auth_tag(
    starsec_isl_header_t *hdr,
    const uint8_t        *payload,
    size_t                payload_len,
    const uint8_t        *master_key_32
) {
    if (hdr == NULL || master_key_32 == NULL) {
        if (hdr != NULL) {
            starsec_secure_zero(hdr->auth_tag, sizeof(hdr->auth_tag));
        }
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }

    uint8_t nonce[STARSEC_CHACHA20_NONCE_BYTES];
    starsec_crypto_status_t status = xdp_derive_isl_nonce(hdr, nonce);
    if (status != STARSEC_CRYPTO_SUCCESS) {
        starsec_secure_zero(hdr->auth_tag, sizeof(hdr->auth_tag));
        starsec_secure_zero(nonce, sizeof(nonce));
        return status;
    }

    uint8_t aad[32];
    memcpy(aad, hdr, offsetof(starsec_isl_header_t, auth_tag));
    memcpy(aad + offsetof(starsec_isl_header_t, auth_tag), &hdr->payload_len, sizeof(hdr->payload_len));

    status = starsec_aead_chacha20_poly1305_tag(
        master_key_32,
        nonce,
        aad,
        sizeof(aad),
        payload,
        payload_len,
        hdr->auth_tag
    );

    starsec_secure_zero(nonce, sizeof(nonce));
    starsec_secure_zero(aad, sizeof(aad));
    return status;
}

/**
 * @brief Validates 128-bit RFC 8439 Poly1305 authentication tag using derived one-time key.
 *
 * Implements constant-time RFC 8439 Section 2.8 AEAD framing verification.
 * Ephemeral key material and internal evaluation contexts are wiped immediately.
 *
 * @param hdr Pointer to StarSec packet header.
 * @param payload Pointer to packet payload bytes (may be NULL if payload_len == 0).
 * @param payload_len Length of payload in bytes.
 * @param master_key_32 Pointer to 32-byte shared optical link master secret.
 * @return true if authentication tag matches in constant time, false otherwise.
 */
bool xdp_verify_isl_auth_tag(
    const starsec_isl_header_t *hdr,
    const uint8_t              *payload,
    size_t                      payload_len,
    const uint8_t              *master_key_32
) {
    if (hdr == NULL || master_key_32 == NULL) {
        return false;
    }

    uint8_t nonce[STARSEC_CHACHA20_NONCE_BYTES];
    starsec_crypto_status_t status = xdp_derive_isl_nonce(hdr, nonce);
    if (status != STARSEC_CRYPTO_SUCCESS) {
        starsec_secure_zero(nonce, sizeof(nonce));
        return false;
    }

    uint8_t aad[32];
    memcpy(aad, hdr, offsetof(starsec_isl_header_t, auth_tag));
    memcpy(aad + offsetof(starsec_isl_header_t, auth_tag), &hdr->payload_len, sizeof(hdr->payload_len));

    uint8_t expected_tag[STARSEC_AEAD_TAG_BYTES];
    status = starsec_aead_chacha20_poly1305_tag(
        master_key_32,
        nonce,
        aad,
        sizeof(aad),
        payload,
        payload_len,
        expected_tag
    );

    starsec_secure_zero(nonce, sizeof(nonce));
    starsec_secure_zero(aad, sizeof(aad));

    if (status != STARSEC_CRYPTO_SUCCESS) {
        return false;
    }

    volatile uint8_t diff = 0;
    for (size_t i = 0; i < STARSEC_AEAD_TAG_BYTES; i++) {
        diff |= (expected_tag[i] ^ hdr->auth_tag[i]);
    }
    starsec_secure_zero(expected_tag, sizeof(expected_tag));

    return (diff == 0);
}


/**
 * @brief Seals an optical ISL frame using RFC 8439 Section 2.8 AEAD encryption.
 * @param hdr Pointer to template header.
 * @param plaintext Pointer to plaintext data bytes (may be NULL if plaintext_len == 0).
 * @param plaintext_len Length of plaintext in bytes.
 * @param out_frame Output buffer receiving the full sealed wire frame.
 * @param max_out_len Maximum capacity of out_frame buffer.
 * @param master_key_32 Pointer to 32-byte shared optical link master secret.
 * @return STARSEC_CRYPTO_SUCCESS on success, error code otherwise.
 */
starsec_crypto_status_t xdp_seal_isl_frame(
    starsec_isl_header_t *hdr,
    const uint8_t        *plaintext,
    size_t                plaintext_len,
    uint8_t              *out_frame,
    size_t                max_out_len,
    const uint8_t        *master_key_32
) {
    if (hdr == NULL || out_frame == NULL || master_key_32 == NULL) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }

    size_t total_frame_len = sizeof(starsec_isl_header_t) + plaintext_len;
    if (total_frame_len > max_out_len) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }

    hdr->payload_len = (uint16_t)plaintext_len;

    uint8_t nonce[STARSEC_CHACHA20_NONCE_BYTES];
    starsec_crypto_status_t status = xdp_derive_isl_nonce(hdr, nonce);
    if (status != STARSEC_CRYPTO_SUCCESS) {
        starsec_secure_zero(nonce, sizeof(nonce));
        return status;
    }

    uint8_t aad[32];
    memcpy(aad, hdr, offsetof(starsec_isl_header_t, auth_tag));
    memcpy(aad + offsetof(starsec_isl_header_t, auth_tag), &hdr->payload_len, sizeof(hdr->payload_len));

    uint8_t *ciphertext_dest = out_frame + sizeof(starsec_isl_header_t);
    status = starsec_aead_chacha20_poly1305_encrypt(
        master_key_32,
        nonce,
        aad,
        sizeof(aad),
        plaintext,
        plaintext_len,
        ciphertext_dest,
        hdr->auth_tag
    );

    if (status == STARSEC_CRYPTO_SUCCESS) {
        memcpy(out_frame, hdr, sizeof(starsec_isl_header_t));
    } else {
        starsec_secure_zero(out_frame, max_out_len);
    }

    starsec_secure_zero(nonce, sizeof(nonce));
    starsec_secure_zero(aad, sizeof(aad));
    return status;
}

/**
 * @brief Opens and authenticates a sealed optical ISL frame using RFC 8439 Section 2.8 AEAD.
 * @param frame_bytes Pointer to raw sealed frame bytes.
 * @param frame_len Total length of sealed frame in bytes.
 * @param out_plaintext Output buffer receiving decrypted plaintext payload.
 * @param out_plaintext_len Pointer receiving length of decrypted payload.
 * @param master_key_32 Pointer to 32-byte shared optical link master secret.
 * @return STARSEC_CRYPTO_SUCCESS on success, STARSEC_CRYPTO_ERR_AUTH_FAIL on tag mismatch.
 */
starsec_crypto_status_t xdp_open_isl_frame(
    const uint8_t *frame_bytes,
    size_t         frame_len,
    uint8_t       *out_plaintext,
    size_t        *out_plaintext_len,
    const uint8_t *master_key_32
) {
    if (frame_bytes == NULL || master_key_32 == NULL) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }

    if (frame_len < sizeof(starsec_isl_header_t)) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }

    const starsec_isl_header_t *hdr = (const starsec_isl_header_t *)frame_bytes;
    if (hdr->magic != STARSEC_ISL_MAGIC) {
        return STARSEC_CRYPTO_ERR_AUTH_FAIL;
    }

    if ((size_t)(sizeof(starsec_isl_header_t) + hdr->payload_len) != frame_len) {
        return STARSEC_CRYPTO_ERR_AUTH_FAIL;
    }

    if (hdr->payload_len > 0 && out_plaintext == NULL) {
        return STARSEC_CRYPTO_ERR_NULL_PARAM;
    }

    uint8_t nonce[STARSEC_CHACHA20_NONCE_BYTES];
    starsec_crypto_status_t status = xdp_derive_isl_nonce(hdr, nonce);
    if (status != STARSEC_CRYPTO_SUCCESS) {
        starsec_secure_zero(nonce, sizeof(nonce));
        return status;
    }

    uint8_t aad[32];
    memcpy(aad, hdr, offsetof(starsec_isl_header_t, auth_tag));
    memcpy(aad + offsetof(starsec_isl_header_t, auth_tag), &hdr->payload_len, sizeof(hdr->payload_len));

    const uint8_t *ciphertext = frame_bytes + sizeof(starsec_isl_header_t);

    status = starsec_aead_chacha20_poly1305_decrypt(
        master_key_32,
        nonce,
        aad,
        sizeof(aad),
        ciphertext,
        hdr->payload_len,
        hdr->auth_tag,
        out_plaintext
    );

    if (status == STARSEC_CRYPTO_SUCCESS) {
        if (out_plaintext_len != NULL) {
            *out_plaintext_len = hdr->payload_len;
        }
    } else {
        if (out_plaintext != NULL && hdr->payload_len > 0) {
            starsec_secure_zero(out_plaintext, hdr->payload_len);
        }
        if (out_plaintext_len != NULL) {
            *out_plaintext_len = 0;
        }
    }

    starsec_secure_zero(nonce, sizeof(nonce));
    starsec_secure_zero(aad, sizeof(aad));
    return status;
}


/**
 * @brief In-flight line-rate packet filter hook for optical inter-satellite links.
 * @param frame_bytes Pointer to raw packet memory.
 * @param frame_len Total length of packet in bytes.
 * @param config Pointer to current filter configuration.
 * @param stats Pointer to diagnostic telemetry statistics.
 * @return STARSEC_XDP_PASS or STARSEC_XDP_DROP.
 */
int xdp_starsec_filter_frame(
    const uint8_t       *frame_bytes,
    size_t               frame_len,
    xdp_filter_config_t *config,
    xdp_filter_stats_t  *stats
) {
    if (frame_bytes == NULL || config == NULL || stats == NULL) {
        if (stats != NULL) {
            stats->dropped_bad_magic++;
        }
        return STARSEC_XDP_DROP;
    }

    if (frame_len < sizeof(starsec_isl_header_t)) {
        stats->dropped_bad_magic++;
        return STARSEC_XDP_DROP;
    }

    const starsec_isl_header_t *hdr = (const starsec_isl_header_t *)frame_bytes;

    if (hdr->magic != STARSEC_ISL_MAGIC) {
        stats->dropped_bad_magic++;
        return STARSEC_XDP_DROP;
    }

    if ((size_t)(sizeof(starsec_isl_header_t) + hdr->payload_len) != frame_len) {
        stats->dropped_bad_magic++;
        return STARSEC_XDP_DROP;
    }

    if (config->clock_ref.local_epoch_ms == 0) {
        stats->dropped_clock_unavailable++;
        return STARSEC_XDP_DROP;
    }

    if (!xdp_validate_local_freshness(hdr->tx_epoch_ms, &config->clock_ref)) {
        stats->dropped_epoch_drift++;
        return STARSEC_XDP_DROP;
    }

    bool node_authorized = false;
    for (size_t i = 0; i < config->authorized_count; i++) {
        if (config->authorized_nodes[i] == hdr->src_satellite_id) {
            node_authorized = true;
            break;
        }
    }

    if (!node_authorized) {
        stats->dropped_unauthorized_node++;
        return STARSEC_XDP_DROP;
    }

    size_t replay_idx = 0;
    bool found_replay_slot = false;
    for (size_t i = 0; i < config->replay_count; i++) {
        if (config->authorized_nodes[i] == hdr->src_satellite_id) {
            replay_idx = i;
            found_replay_slot = true;
            break;
        }
    }

    if (!found_replay_slot && config->replay_count < 256) {
        replay_idx = config->replay_count++;
    }

    if (!xdp_check_replay_window_speculative(&config->replay_states[replay_idx], hdr->session_id, hdr->sequence_number)) {
        if (hdr->session_id < config->replay_states[replay_idx].session_id) {
            stats->dropped_stale_session++;
        } else {
            stats->dropped_epoch_drift++;
        }
        return STARSEC_XDP_DROP;
    }

    const uint8_t *payload = frame_bytes + sizeof(starsec_isl_header_t);
    if (!xdp_verify_isl_auth_tag(hdr, payload, hdr->payload_len, config->isl_shared_key)) {
        stats->dropped_invalid_auth++;
        return STARSEC_XDP_DROP;
    }

    if (!xdp_commit_replay_window_atomic(&config->replay_states[replay_idx], hdr->session_id, hdr->sequence_number)) {
        stats->dropped_epoch_drift++;
        return STARSEC_XDP_DROP;
    }

    stats->passed_frames++;
    return STARSEC_XDP_PASS;
}

/**
 * @brief Initializes the integrated two-tier kernel-to-userspace security pipeline.
 * @param pipeline Pointer to starsec_xdp_pipeline_t instance to initialize.
 * @param shared_key_32 Pointer to 32-byte shared ISL master secret.
 * @param clock_ref Pointer to trusted local spacecraft receiver clock reference.
 */
void xdp_pipeline_init(
    starsec_xdp_pipeline_t          *pipeline,
    const uint8_t                    shared_key_32[32],
    const starsec_time_sync_state_t *clock_ref
) {
    if (pipeline == NULL) {
        return;
    }
    memset(pipeline, 0, sizeof(*pipeline));
    if (shared_key_32 != NULL) {
        memcpy(pipeline->filter_config.isl_shared_key, shared_key_32, 32);
    }
    if (clock_ref != NULL) {
        pipeline->filter_config.clock_ref = *clock_ref;
    }
    xdp_reset_filter_stats(&pipeline->stats);
}

/**
 * @brief Registers an authorized satellite node in both Tier 1 BPF map and Tier 2 filter.
 * @param pipeline Pointer to active pipeline instance.
 * @param satellite_id 16-bit Constellation Node ID.
 * @return true on success, false if table is full.
 */
bool xdp_pipeline_authorize_node(
    starsec_xdp_pipeline_t *pipeline,
    uint16_t                satellite_id
) {
    if (pipeline == NULL) {
        return false;
    }
    if (pipeline->filter_config.authorized_count >= 256 || pipeline->kernel_bpf_map.count >= 256) {
        return false;
    }
    pipeline->filter_config.authorized_nodes[pipeline->filter_config.authorized_count++] = satellite_id;
    pipeline->kernel_bpf_map.satellite_ids[pipeline->kernel_bpf_map.count++] = satellite_id;
    return true;
}

/**
 * @brief Synchronizes committed userspace replay state directly into the in-kernel BPF map.
 * @param bpf_map Pointer to kernel BPF map instance.
 * @param satellite_id 16-bit Constellation Node ID.
 * @param committed_state Pointer to newly committed atomic replay state.
 * @return true on successful synchronization, false if node not found.
 */
bool xdp_sync_kernel_replay_map(
    starsec_kernel_bpf_map_t      *bpf_map,
    uint16_t                       satellite_id,
    const isl_anti_replay_state_t *committed_state
) {
    if (bpf_map == NULL || committed_state == NULL) {
        return false;
    }

    for (size_t i = 0; i < bpf_map->count; i++) {
        if (bpf_map->satellite_ids[i] == satellite_id) {
            starsec_spin_lock(&bpf_map->states[i].lock);
            bpf_map->states[i].session_id = committed_state->session_id;
            bpf_map->states[i].window_base_seq = committed_state->window_base_seq;
            bpf_map->states[i].window_bitmap = committed_state->window_bitmap;
            starsec_spin_unlock(&bpf_map->states[i].lock);
            return true;
        }
    }

    if (bpf_map->count < 256) {
        size_t idx = bpf_map->count++;
        bpf_map->satellite_ids[idx] = satellite_id;
        bpf_map->states[idx] = *committed_state;
        bpf_map->states[idx].lock = 0;
        return true;
    }

    return false;
}

/**
 * @brief Processes an ingress frame through the integrated two-tier kernel/userspace pipeline.
 * @param pipeline Pointer to integrated pipeline instance.
 * @param frame_bytes Pointer to raw ingress packet memory.
 * @param frame_len Total length of packet in bytes.
 * @param decrypted_payload Output buffer receiving decrypted plaintext payload.
 * @param decrypted_len Pointer receiving length of decrypted plaintext.
 * @return STARSEC_XDP_PASS on successful authentication and commit, STARSEC_XDP_DROP on failure.
 */
int xdp_pipeline_process_packet(
    starsec_xdp_pipeline_t *pipeline,
    const uint8_t          *frame_bytes,
    size_t                  frame_len,
    uint8_t                *decrypted_payload,
    size_t                 *decrypted_len
) {
    if (pipeline == NULL || frame_bytes == NULL) {
        if (pipeline != NULL) {
            pipeline->stats.dropped_bad_magic++;
        }
        return STARSEC_XDP_DROP;
    }

    if (frame_len < sizeof(starsec_isl_header_t)) {
        pipeline->stats.dropped_bad_magic++;
        return STARSEC_XDP_DROP;
    }

    const starsec_isl_header_t *hdr = (const starsec_isl_header_t *)frame_bytes;
    if (hdr->magic != STARSEC_ISL_MAGIC) {
        pipeline->stats.dropped_bad_magic++;
        return STARSEC_XDP_DROP;
    }

    if ((size_t)(sizeof(starsec_isl_header_t) + hdr->payload_len) != frame_len) {
        pipeline->stats.dropped_bad_magic++;
        return STARSEC_XDP_DROP;
    }

    if (pipeline->filter_config.clock_ref.local_epoch_ms == 0) {
        pipeline->stats.dropped_clock_unavailable++;
        return STARSEC_XDP_DROP;
    }

    if (!xdp_validate_local_freshness(hdr->tx_epoch_ms, &pipeline->filter_config.clock_ref)) {
        pipeline->stats.dropped_epoch_drift++;
        return STARSEC_XDP_DROP;
    }

    bool authorized = false;
    for (size_t i = 0; i < pipeline->kernel_bpf_map.count; i++) {
        if (pipeline->kernel_bpf_map.satellite_ids[i] == hdr->src_satellite_id) {
            authorized = true;
            break;
        }
    }
    if (!authorized) {
        pipeline->stats.dropped_unauthorized_node++;
        return STARSEC_XDP_DROP;
    }

    size_t bpf_idx = 0;
    bool found_bpf = false;
    for (size_t i = 0; i < pipeline->kernel_bpf_map.count; i++) {
        if (pipeline->kernel_bpf_map.satellite_ids[i] == hdr->src_satellite_id) {
            bpf_idx = i;
            found_bpf = true;
            break;
        }
    }
    if (found_bpf) {
        if (!xdp_check_replay_window_speculative(&pipeline->kernel_bpf_map.states[bpf_idx], hdr->session_id, hdr->sequence_number)) {
            if (hdr->session_id < pipeline->kernel_bpf_map.states[bpf_idx].session_id) {
                pipeline->stats.dropped_stale_session++;
            } else {
                pipeline->stats.dropped_epoch_drift++;
            }
            return STARSEC_XDP_DROP;
        }
    }

    starsec_crypto_status_t dec_stat = xdp_open_isl_frame(
        frame_bytes,
        frame_len,
        decrypted_payload,
        decrypted_len,
        pipeline->filter_config.isl_shared_key
    );

    if (dec_stat != STARSEC_CRYPTO_SUCCESS) {
        pipeline->stats.dropped_invalid_auth++;
        return STARSEC_XDP_DROP;
    }

    size_t user_idx = 0;
    bool found_user = false;
    for (size_t i = 0; i < pipeline->filter_config.replay_count; i++) {
        if (pipeline->filter_config.authorized_nodes[i] == hdr->src_satellite_id) {
            user_idx = i;
            found_user = true;
            break;
        }
    }
    if (!found_user && pipeline->filter_config.replay_count < 256) {
        user_idx = pipeline->filter_config.replay_count++;
        pipeline->filter_config.authorized_nodes[user_idx] = hdr->src_satellite_id;
    }

    if (!xdp_commit_replay_window_atomic(&pipeline->filter_config.replay_states[user_idx], hdr->session_id, hdr->sequence_number)) {
        pipeline->stats.dropped_epoch_drift++;
        return STARSEC_XDP_DROP;
    }

    xdp_sync_kernel_replay_map(
        &pipeline->kernel_bpf_map,
        hdr->src_satellite_id,
        &pipeline->filter_config.replay_states[user_idx]
    );

    pipeline->stats.passed_frames++;
    return STARSEC_XDP_PASS;
}

