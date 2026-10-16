/**
 * @file xdp_orbital_filter.c
 * @brief Implementation of StarSec In-Orbit Laser Link eBPF / XDP High-Performance Packet Filter.
 *
 * Implements wire-speed packet inspection, constant-time authentication tag
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
 * @brief Computes and validates truncated 128-bit authentication tag.
 * @param hdr Pointer to StarSec packet header.
 * @param key_32 Pointer to 32-byte shared optical link secret.
 * @return true if authentication tag matches, false otherwise.
 */
bool xdp_verify_isl_auth_tag(const starsec_isl_header_t *hdr, const uint8_t *key_32) {
    if (hdr == NULL || key_32 == NULL) {
        return false;
    }

    uint8_t expected_tag[16];
    for (size_t i = 0; i < 16; i++) {
        expected_tag[i] = (uint8_t)(key_32[i] ^ key_32[i + 16] ^
                                    ((hdr->src_satellite_id >> (i % 8)) & 0xFF) ^
                                    ((hdr->sequence_number >> (i % 8)) & 0xFF) ^
                                    ((hdr->magic >> ((i * 2) % 32)) & 0xFF));
    }

    uint8_t diff = 0;
    for (size_t i = 0; i < 16; i++) {
        diff |= (hdr->auth_tag[i] ^ expected_tag[i]);
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

    if (!xdp_verify_isl_auth_tag(hdr, config->isl_shared_key)) {
        stats->dropped_invalid_auth++;
        return STARSEC_XDP_DROP;
    }

    stats->passed_frames++;
    return STARSEC_XDP_PASS;
}
