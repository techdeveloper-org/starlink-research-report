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
    stats->passed_frames = 0;
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
 * @brief Generates an authentic RFC 8439 Poly1305 128-bit MAC for an ISL frame.
 * @param hdr Pointer to StarSec packet header whose auth_tag field will be populated.
 * @param payload Pointer to packet payload bytes (may be NULL if payload_len == 0).
 * @param payload_len Length of payload in bytes.
 * @param key_32 Pointer to 32-byte shared optical link secret.
 */
void xdp_generate_isl_auth_tag(
    starsec_isl_header_t *hdr,
    const uint8_t        *payload,
    size_t                payload_len,
    const uint8_t        *key_32
) {
    if (hdr == NULL || key_32 == NULL) {
        return;
    }

    starsec_poly1305_ctx_t ctx;
    starsec_poly1305_init(&ctx, key_32);

    starsec_poly1305_update(&ctx, (const uint8_t *)hdr, offsetof(starsec_isl_header_t, auth_tag));
    starsec_poly1305_update(&ctx, (const uint8_t *)&hdr->payload_len, sizeof(hdr->payload_len));

    if (payload != NULL && payload_len > 0) {
        starsec_poly1305_update(&ctx, payload, payload_len);
    }

    starsec_poly1305_finish(&ctx, hdr->auth_tag);
}

/**
 * @brief Computes and validates 128-bit RFC 8439 Poly1305 authentication tag.
 * @param hdr Pointer to StarSec packet header.
 * @param payload Pointer to packet payload bytes (may be NULL if payload_len == 0).
 * @param payload_len Length of payload in bytes.
 * @param key_32 Pointer to 32-byte shared optical link secret.
 * @return true if authentication tag matches in constant time, false otherwise.
 */
bool xdp_verify_isl_auth_tag(
    const starsec_isl_header_t *hdr,
    const uint8_t              *payload,
    size_t                      payload_len,
    const uint8_t              *key_32
) {
    if (hdr == NULL || key_32 == NULL) {
        return false;
    }

    starsec_poly1305_ctx_t ctx;
    uint8_t calculated_tag[16];

    starsec_poly1305_init(&ctx, key_32);
    starsec_poly1305_update(&ctx, (const uint8_t *)hdr, offsetof(starsec_isl_header_t, auth_tag));
    starsec_poly1305_update(&ctx, (const uint8_t *)&hdr->payload_len, sizeof(hdr->payload_len));

    if (payload != NULL && payload_len > 0) {
        starsec_poly1305_update(&ctx, payload, payload_len);
    }

    starsec_poly1305_finish(&ctx, calculated_tag);

    volatile uint8_t diff = 0;
    for (size_t i = 0; i < 16; i++) {
        diff |= (calculated_tag[i] ^ hdr->auth_tag[i]);
    }

    for (size_t i = 0; i < 16; i++) {
        calculated_tag[i] = 0;
    }

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
    const uint8_t             *frame_bytes,
    size_t                     frame_len,
    const xdp_filter_config_t *config,
    xdp_filter_stats_t        *stats
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

    if (!xdp_validate_epoch_window(hdr->tx_epoch_ms, config->local_clock_epoch_ms, STARSEC_MAX_PROPAGATION_DRIFT_MS)) {
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

    const uint8_t *payload = frame_bytes + sizeof(starsec_isl_header_t);
    if (!xdp_verify_isl_auth_tag(hdr, payload, hdr->payload_len, config->isl_shared_key)) {
        stats->dropped_invalid_auth++;
        return STARSEC_XDP_DROP;
    }

    stats->passed_frames++;
    return STARSEC_XDP_PASS;
}
