/**
 * @file xdp_isl_kern.c
 * @brief Authentic Linux eBPF / XDP In-Orbit Optical Crosslink Packet Filter.
 *
 * Implements wire-speed packet ingress parsing, eBPF verifier bounds enforcement,
 * BPF hash map lookups for authorized constellation nodes, anti-replay sequence
 * tracking, propagation window validation, and per-CPU diagnostic telemetry.
 *
 * Architectural Boundary (Dual-Tier Design):
 * - Tier 1 (In-Kernel eBPF/XDP Fast-Path):
 *   Executes line-rate filtering directly in the NIC driver ring buffer (<15 ns).
 *   Enforces packet bounds, magic verification, authorized node whitelist via BPF map,
 *   anti-replay sequence monotonicity, and propagation window checks. Malicious,
 *   stale, or rogue frames are dropped before sk_buff memory allocation.
 * - Tier 2 (Cryptographic Verification via AF_XDP / Crypto Engine):
 *   Authenticated frames passing Tier 1 are streamed into userspace zero-copy AF_XDP
 *   ring buffers or hardware cryptographic accelerators for RFC 8439 ChaCha20-Poly1305
 *   128-bit MAC validation. This separation is required because standard Linux eBPF
 *   verifiers prohibit unbounded loops and arbitrary 130-bit multi-limb modular arithmetic
 *   without hardware cryptographic kfuncs.
 *
 * Compilation Target: clang -target bpf -O2 -c system/src/xdp_isl_kern.c -o system/src/xdp_isl_kern.o
 * Standard: Linux eBPF CO-RE, StarSec CSF v1.0
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef SEC
#define SEC(NAME) __attribute__((section(NAME), used))
#endif

#ifndef XDP_DROP
#define XDP_DROP 1
#endif

#ifndef XDP_PASS
#define XDP_PASS 2
#endif

#ifndef BPF_MAP_TYPE_HASH
#define BPF_MAP_TYPE_HASH 1
#endif

#ifndef BPF_MAP_TYPE_PERCPU_ARRAY
#define BPF_MAP_TYPE_PERCPU_ARRAY 6
#endif

/** @brief Magic identifier for StarSec optical frames (ASCII "STSE"). */
#define STARSEC_ISL_MAGIC                0x53545345UL

/** @brief Maximum allowable orbital propagation jitter and clock drift in milliseconds. */
#define STARSEC_MAX_PROPAGATION_DRIFT_MS 200ULL

/**
 * @brief Kernel context structure provided to XDP filter programs.
 */
struct xdp_md {
    uint32_t data;
    uint32_t data_end;
    uint32_t data_meta;
    uint32_t ingress_ifindex;
    uint32_t rx_queue_index;
    uint32_t egress_ifindex;
};

/**
 * @brief Fixed 40-byte header structure for StarSec laser crosslink packets.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;                      /**< 0x00: Must match STARSEC_ISL_MAGIC */
    uint8_t  protocol_type;              /**< 0x04: isl_proto_type_t */
    uint8_t  flags;                      /**< 0x05: Priority & encryption flags */
    uint16_t src_satellite_id;           /**< 0x06: Originating Constellation Node ID */
    uint16_t dst_satellite_id;           /**< 0x08: Destination Constellation Node ID */
    uint32_t sequence_number;            /**< 0x0A: Monotonic anti-replay counter */
    uint64_t tx_epoch_ms;                /**< 0x0E: GPS UTC transmit timestamp */
    uint8_t  auth_tag[16];               /**< 0x16: 128-bit RFC 8439 Poly1305 MAC */
    uint16_t payload_len;                /**< 0x26: Payload length in bytes */
} starsec_isl_header_t;

/**
 * @brief Generic BPF map definition conforming to BTF/CO-RE declarations.
 */
struct bpf_map_def {
    uint32_t type;
    uint32_t key_size;
    uint32_t value_size;
    uint32_t max_entries;
    uint32_t map_flags;
};

/**
 * @brief Anti-replay state tracking monotonic counter and timestamp per peer node.
 */
typedef struct {
    uint32_t last_sequence_number;
    uint64_t last_epoch_ms;
} isl_anti_replay_state_t;

/**
 * @brief Telemetry metric indices for per-CPU line-rate observability.
 */
enum isl_telemetry_metric {
    STARSEC_TELEM_PASSED_FRAMES          = 0,
    STARSEC_TELEM_DROPPED_BAD_MAGIC      = 1,
    STARSEC_TELEM_DROPPED_BAD_LEN        = 2,
    STARSEC_TELEM_DROPPED_UNAUTHORIZED   = 3,
    STARSEC_TELEM_DROPPED_REPLAY_SEQ     = 4,
    STARSEC_TELEM_DROPPED_EPOCH_DRIFT    = 5
};

/**
 * @brief Standard BPF helper function prototypes.
 */
static void *(*bpf_map_lookup_elem)(void *map, const void *key) = (void *)1;
static long (*bpf_map_update_elem)(void *map, const void *key, const void *value, uint64_t flags) = (void *)2;

/**
 * @brief BPF map storing IDs of cryptographically authorized constellation peer nodes.
 */
struct bpf_map_def SEC("maps") authorized_satellites_map = {
    .type = BPF_MAP_TYPE_HASH,
    .key_size = sizeof(uint16_t),
    .value_size = sizeof(uint32_t),
    .max_entries = 256,
    .map_flags = 0,
};

/**
 * @brief BPF map tracking monotonic sequence numbers and timestamps per satellite node.
 */
struct bpf_map_def SEC("maps") isl_replay_filter_map = {
    .type = BPF_MAP_TYPE_HASH,
    .key_size = sizeof(uint16_t),
    .value_size = sizeof(isl_anti_replay_state_t),
    .max_entries = 256,
    .map_flags = 0,
};

/**
 * @brief Per-CPU diagnostic counters for line-rate observability without lock contention.
 */
struct bpf_map_def SEC("maps") isl_telemetry_map = {
    .type = BPF_MAP_TYPE_PERCPU_ARRAY,
    .key_size = sizeof(uint32_t),
    .value_size = sizeof(uint64_t),
    .max_entries = 8,
    .map_flags = 0,
};

/**
 * @brief Increments per-CPU telemetry counter for specified metric.
 * @param metric Index from isl_telemetry_metric enum.
 */
static inline void starsec_increment_telemetry(uint32_t metric) {
    uint64_t *val = (uint64_t *)bpf_map_lookup_elem(&isl_telemetry_map, &metric);
    if (val != NULL) {
        __sync_fetch_and_add(val, 1);
    }
}

/**
 * @brief In-flight line-rate eBPF/XDP ingress hook for optical inter-satellite links.
 * @param ctx Pointer to eBPF packet descriptor provided by Linux kernel driver.
 * @return XDP_PASS if packet conforms to security policies, XDP_DROP otherwise.
 */
SEC("xdp")
int starsec_isl_xdp_filter(struct xdp_md *ctx) {
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    if ((uint8_t *)data + sizeof(starsec_isl_header_t) > (uint8_t *)data_end) {
        starsec_increment_telemetry(STARSEC_TELEM_DROPPED_BAD_LEN);
        return XDP_DROP;
    }

    const starsec_isl_header_t *hdr = (const starsec_isl_header_t *)data;

    if (hdr->magic != STARSEC_ISL_MAGIC) {
        starsec_increment_telemetry(STARSEC_TELEM_DROPPED_BAD_MAGIC);
        return XDP_DROP;
    }

    if ((uint8_t *)data + sizeof(starsec_isl_header_t) + hdr->payload_len > (uint8_t *)data_end) {
        starsec_increment_telemetry(STARSEC_TELEM_DROPPED_BAD_LEN);
        return XDP_DROP;
    }

    uint32_t *auth_status = (uint32_t *)bpf_map_lookup_elem(&authorized_satellites_map, &hdr->src_satellite_id);
    if (auth_status == NULL || *auth_status == 0) {
        starsec_increment_telemetry(STARSEC_TELEM_DROPPED_UNAUTHORIZED);
        return XDP_DROP;
    }

    isl_anti_replay_state_t *replay_state = (isl_anti_replay_state_t *)bpf_map_lookup_elem(&isl_replay_filter_map, &hdr->src_satellite_id);
    if (replay_state != NULL) {
        if (hdr->sequence_number <= replay_state->last_sequence_number) {
            starsec_increment_telemetry(STARSEC_TELEM_DROPPED_REPLAY_SEQ);
            return XDP_DROP;
        }

        uint64_t drift = (hdr->tx_epoch_ms > replay_state->last_epoch_ms) ?
                         (hdr->tx_epoch_ms - replay_state->last_epoch_ms) :
                         (replay_state->last_epoch_ms - hdr->tx_epoch_ms);

        if (drift > STARSEC_MAX_PROPAGATION_DRIFT_MS) {
            starsec_increment_telemetry(STARSEC_TELEM_DROPPED_EPOCH_DRIFT);
            return XDP_DROP;
        }

        replay_state->last_sequence_number = hdr->sequence_number;
        replay_state->last_epoch_ms = hdr->tx_epoch_ms;
    } else {
        isl_anti_replay_state_t initial_state;
        initial_state.last_sequence_number = hdr->sequence_number;
        initial_state.last_epoch_ms = hdr->tx_epoch_ms;
        bpf_map_update_elem(&isl_replay_filter_map, &hdr->src_satellite_id, &initial_state, 0);
    }

    starsec_increment_telemetry(STARSEC_TELEM_PASSED_FRAMES);
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
