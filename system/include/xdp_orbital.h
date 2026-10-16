/**
 * @file xdp_orbital.h
 * @brief StarSec In-Orbit Laser Link eBPF / XDP High-Performance Packet Filter Header.
 *
 * Implements wire-speed packet inspection, cryptographic header verification,
 * anti-replay epoch window checking, and rogue BGP route isolation.
 *
 * Standard: StarSec Constellation Security Framework (CSF v1.0)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#ifndef STARSEC_XDP_ORBITAL_H
#define STARSEC_XDP_ORBITAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief XDP Action Code: Allow frame to pass to forwarder ring buffer. */
#define STARSEC_XDP_PASS                 0

/** @brief XDP Action Code: Drop frame immediately at NIC driver layer. */
#define STARSEC_XDP_DROP                 1

/** @brief Magic identifier for StarSec optical frames (ASCII "STSE"). */
#define STARSEC_ISL_MAGIC                0x53545345UL

/** @brief Maximum allowable orbital propagation jitter and clock drift in milliseconds. */
#define STARSEC_MAX_PROPAGATION_DRIFT_MS 200ULL

/**
 * @brief Protocol payload classifications for optical inter-satellite links.
 */
typedef enum {
    STARSEC_PROTO_TELECOMMAND            = 0x01,
    STARSEC_PROTO_MESH_BGP_ROUTING       = 0x02,
    STARSEC_PROTO_INTER_SATELLITE_DATA   = 0x03,
    STARSEC_PROTO_EMERGENCY_DEORBIT      = 0x04
} isl_proto_type_t;

/**
 * @brief Fixed 36-byte header structure for StarSec laser crosslink packets.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;                      /**< 0x00: Must match STARSEC_ISL_MAGIC */
    uint8_t  protocol_type;              /**< 0x04: isl_proto_type_t */
    uint8_t  flags;                      /**< 0x05: Priority & encryption flags */
    uint16_t src_satellite_id;           /**< 0x06: Originating Constellation Node ID */
    uint16_t dst_satellite_id;           /**< 0x08: Destination Constellation Node ID */
    uint32_t sequence_number;            /**< 0x0A: Monotonic anti-replay counter */
    uint64_t tx_epoch_ms;                /**< 0x0E: GPS UTC transmit timestamp */
    uint8_t  auth_tag[16];               /**< 0x16: Truncated 128-bit authentication tag */
    uint16_t payload_len;                /**< 0x26: Payload length in bytes */
} starsec_isl_header_t;

/**
 * @brief Diagnostic traffic counters for the in-orbit XDP filter.
 */
typedef struct {
    uint64_t dropped_bad_magic;
    uint64_t dropped_epoch_drift;
    uint64_t dropped_unauthorized_node;
    uint64_t dropped_invalid_auth;
    uint64_t passed_frames;
} xdp_filter_stats_t;

/**
 * @brief Configuration rules for the active optical link filter instance.
 */
typedef struct {
    uint16_t authorized_nodes[256];
    size_t   authorized_count;
    uint64_t local_clock_epoch_ms;
    uint8_t  isl_shared_key[32];
} xdp_filter_config_t;

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
);

/**
 * @brief Verifies that transmit timestamp falls within acceptable propagation window.
 * @param tx_ms Transmit epoch timestamp in milliseconds.
 * @param local_ms Local spacecraft epoch timestamp in milliseconds.
 * @param max_drift_ms Maximum permitted temporal drift.
 * @return true if timestamp is valid and within bounds, false otherwise.
 */
bool xdp_validate_epoch_window(uint64_t tx_ms, uint64_t local_ms, uint64_t max_drift_ms);

/**
 * @brief Computes and validates truncated 128-bit authentication tag.
 * @param hdr Pointer to StarSec packet header.
 * @param key_32 Pointer to 32-byte shared optical link secret.
 * @return true if authentication tag matches, false otherwise.
 */
bool xdp_verify_isl_auth_tag(const starsec_isl_header_t *hdr, const uint8_t *key_32);

/**
 * @brief Resets all diagnostic telemetry counters to zero.
 * @param stats Pointer to telemetry statistics structure.
 */
void xdp_reset_filter_stats(xdp_filter_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* STARSEC_XDP_ORBITAL_H */
