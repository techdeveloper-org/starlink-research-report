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

    isl_anti_replay_state_t current_state;
    isl_anti_replay_state_t next_state;

    do {
        current_state.window_base_seq = __atomic_load_n(&state->window_base_seq, __ATOMIC_ACQUIRE);
        current_state.window_bitmap   = __atomic_load_n(&state->window_bitmap, __ATOMIC_ACQUIRE);
        current_state.session_id      = __atomic_load_n(&state->session_id, __ATOMIC_ACQUIRE);
        current_state.reserved_padding = 0;

        next_state = current_state;
        xdp_commit_replay_window_authenticated(&next_state, session_id, seq_num);

        if (next_state.window_base_seq == current_state.window_base_seq &&
            next_state.window_bitmap == current_state.window_bitmap &&
            next_state.session_id == current_state.session_id) {
            return false;
        }

        if (__atomic_compare_exchange_n(&state->window_base_seq, &current_state.window_base_seq,
                                        next_state.window_base_seq, false,
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            __atomic_store_n(&state->window_bitmap, next_state.window_bitmap, __ATOMIC_RELEASE);
            __atomic_store_n(&state->session_id, next_state.session_id, __ATOMIC_RELEASE);
            return true;
        }
    } while (1);
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
    uint8_t one_time_key[STARSEC_ONE_TIME_KEY_BYTES];

    starsec_crypto_status_t status = xdp_derive_isl_nonce(hdr, nonce);
    if (status != STARSEC_CRYPTO_SUCCESS) {
        starsec_secure_zero(hdr->auth_tag, sizeof(hdr->auth_tag));
        starsec_secure_zero(nonce, sizeof(nonce));
        return status;
    }

    starsec_chacha20_poly1305_key_gen(master_key_32, nonce, one_time_key);

    starsec_poly1305_ctx_t ctx;
    starsec_poly1305_init(&ctx, one_time_key);

    starsec_poly1305_update(&ctx, (const uint8_t *)hdr, offsetof(starsec_isl_header_t, auth_tag));
    starsec_poly1305_update(&ctx, (const uint8_t *)&hdr->payload_len, sizeof(hdr->payload_len));

    if (payload != NULL && payload_len > 0) {
        starsec_poly1305_update(&ctx, payload, payload_len);
    }

    starsec_poly1305_finish(&ctx, hdr->auth_tag);

    starsec_secure_zero(one_time_key, sizeof(one_time_key));
    starsec_secure_zero(nonce, sizeof(nonce));
    starsec_secure_zero(&ctx, sizeof(ctx));

    return STARSEC_CRYPTO_SUCCESS;
}

/**
 * @brief Validates 128-bit RFC 8439 Poly1305 authentication tag using derived one-time key.
 *
 * Derives the unique single-use 32-byte Poly1305 key via ChaCha20 block 0 and
 * executes constant-time tag verification. Ephemeral key material is wiped immediately.
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
    uint8_t one_time_key[STARSEC_ONE_TIME_KEY_BYTES];

    starsec_crypto_status_t status = xdp_derive_isl_nonce(hdr, nonce);
    if (status != STARSEC_CRYPTO_SUCCESS) {
        starsec_secure_zero(nonce, sizeof(nonce));
        return false;
    }

    starsec_chacha20_poly1305_key_gen(master_key_32, nonce, one_time_key);

    starsec_poly1305_ctx_t ctx;
    uint8_t calculated_tag[16];

    starsec_poly1305_init(&ctx, one_time_key);
    starsec_poly1305_update(&ctx, (const uint8_t *)hdr, offsetof(starsec_isl_header_t, auth_tag));
    starsec_poly1305_update(&ctx, (const uint8_t *)&hdr->payload_len, sizeof(hdr->payload_len));

    if (payload != NULL && payload_len > 0) {
        starsec_poly1305_update(&ctx, payload, payload_len);
    }

    starsec_poly1305_finish(&ctx, calculated_tag);

    starsec_secure_zero(one_time_key, sizeof(one_time_key));
    starsec_secure_zero(nonce, sizeof(nonce));
    starsec_secure_zero(&ctx, sizeof(ctx));

    volatile uint8_t diff = 0;
    for (size_t i = 0; i < 16; i++) {
        diff |= (calculated_tag[i] ^ hdr->auth_tag[i]);
    }

    starsec_secure_zero(calculated_tag, sizeof(calculated_tag));

    return (diff == 0);
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

    xdp_commit_replay_window_authenticated(&config->replay_states[replay_idx], hdr->session_id, hdr->sequence_number);

    stats->passed_frames++;
    return STARSEC_XDP_PASS;
}
