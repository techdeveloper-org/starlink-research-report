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

/** @brief Size of the sliding anti-replay window in bits / packets. */
#define STARSEC_SLIDING_WINDOW_SIZE      64U

/** @brief Maximum allowable orbital propagation jitter and clock drift in milliseconds. */
#define STARSEC_MAX_PROPAGATION_DRIFT_MS 200ULL

/** @brief Permitted forward clock skew tolerance in milliseconds (GPSDO sync bound). */
#define STARSEC_MAX_CLOCK_SKEW_AHEAD_MS  20ULL

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
 * @brief Fixed 48-byte header structure for StarSec laser crosslink packets (RFC-001 v1.1).
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;                      /**< 0x00: Must match STARSEC_ISL_MAGIC */
    uint8_t  protocol_type;              /**< 0x04: isl_proto_type_t */
    uint8_t  flags;                      /**< 0x05: Priority & encryption flags */
    uint16_t src_satellite_id;           /**< 0x06: Originating Constellation Node ID */
    uint16_t dst_satellite_id;           /**< 0x08: Destination Constellation Node ID */
    uint32_t session_id;                 /**< 0x0A: Ephemeral session / boot ID */
    uint64_t sequence_number;            /**< 0x0E: Monotonic 64-bit anti-replay counter */
    uint64_t tx_epoch_ms;                /**< 0x16: GPS UTC transmit timestamp */
    uint8_t  auth_tag[16];               /**< 0x1E: 128-bit RFC 8439 Poly1305 MAC */
    uint16_t payload_len;                /**< 0x2E: Payload length in bytes */
} starsec_isl_header_t;

#ifndef __uint
#define __uint(name, val) int (*name)[val]
#endif

#ifndef __type
#define __type(name, val) typeof(val) *name
#endif

/**
 * @brief Two-stage anti-replay state tracking 64-bit sliding window per peer node.
 */
typedef struct {
    uint64_t window_base_seq;
    uint64_t window_bitmap;
    uint32_t session_id;
    uint32_t lock;
    uint32_t session_auth_failures;
    uint32_t is_quarantined;
    uint8_t  reserved_padding[32];
} isl_anti_replay_state_t;

/**
 * @brief Spacecraft receiver local clock synchronization reference.
 */
typedef struct {
    uint64_t local_epoch_ms;
    uint64_t max_past_drift_ms;
    uint64_t max_future_skew_ms;
} starsec_time_sync_state_t;

/**
 * @brief Telemetry metric indices for per-CPU line-rate observability.
 */
enum isl_telemetry_metric {
    STARSEC_TELEM_PASSED_FRAMES               = 0,
    STARSEC_TELEM_DROPPED_BAD_MAGIC           = 1,
    STARSEC_TELEM_DROPPED_BAD_LEN             = 2,
    STARSEC_TELEM_DROPPED_UNAUTHORIZED        = 3,
    STARSEC_TELEM_DROPPED_REPLAY_SEQ          = 4,
    STARSEC_TELEM_DROPPED_EPOCH_DRIFT         = 5,
    STARSEC_TELEM_DROPPED_CLOCK_UNAVAILABLE   = 6,
    STARSEC_TELEM_DROPPED_STALE_SESSION       = 7,
    STARSEC_TELEM_DROPPED_REPLAY_INCONSISTENT = 8
};

/**
 * @brief Standard BPF helper function prototypes.
 */
static void *(*bpf_map_lookup_elem)(void *map, const void *key) __attribute__((unused)) = (void *)1;
static long (*bpf_map_update_elem)(void *map, const void *key, const void *value, uint64_t flags) __attribute__((unused)) = (void *)2;

/**
 * @brief BPF map storing IDs of cryptographically authorized constellation peer nodes.
 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, uint16_t);
    __type(value, uint32_t);
    __uint(max_entries, 256);
} authorized_satellites_map SEC(".maps");

/**
 * @brief BPF map tracking sliding window anti-replay state committed by Tier 2 auth engine.
 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, uint16_t);
    __type(value, isl_anti_replay_state_t);
    __uint(max_entries, 256);
} isl_replay_filter_map SEC(".maps");

/**
 * @brief BPF map storing spacecraft local clock reference updated by GPS/PTP daemon.
 */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, uint32_t);
    __type(value, starsec_time_sync_state_t);
    __uint(max_entries, 1);
} time_sync_map SEC(".maps");

/**
 * @brief Per-CPU diagnostic counters for line-rate observability without lock contention.
 */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __type(key, uint32_t);
    __type(value, uint64_t);
    __uint(max_entries, 16);
} isl_telemetry_map SEC(".maps");

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
    void *data = (void *)(uint64_t)ctx->data;
    void *data_end = (void *)(uint64_t)ctx->data_end;

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

    uint32_t time_key = 0;
    starsec_time_sync_state_t *time_ref = (starsec_time_sync_state_t *)bpf_map_lookup_elem(&time_sync_map, &time_key);
    if (time_ref == NULL || time_ref->local_epoch_ms == 0) {
        starsec_increment_telemetry(STARSEC_TELEM_DROPPED_CLOCK_UNAVAILABLE);
        return XDP_DROP;
    }

    if (hdr->tx_epoch_ms > time_ref->local_epoch_ms) {
        uint64_t diff_future = hdr->tx_epoch_ms - time_ref->local_epoch_ms;
        if (diff_future > time_ref->max_future_skew_ms) {
            starsec_increment_telemetry(STARSEC_TELEM_DROPPED_EPOCH_DRIFT);
            return XDP_DROP;
        }
    } else {
        uint64_t diff_past = time_ref->local_epoch_ms - hdr->tx_epoch_ms;
        if (diff_past > time_ref->max_past_drift_ms) {
            starsec_increment_telemetry(STARSEC_TELEM_DROPPED_EPOCH_DRIFT);
            return XDP_DROP;
        }
    }

    uint32_t *auth_status = (uint32_t *)bpf_map_lookup_elem(&authorized_satellites_map, &hdr->src_satellite_id);
    if (auth_status == NULL || *auth_status == 0) {
        starsec_increment_telemetry(STARSEC_TELEM_DROPPED_UNAUTHORIZED);
        return XDP_DROP;
    }

    isl_anti_replay_state_t *replay_state = (isl_anti_replay_state_t *)bpf_map_lookup_elem(&isl_replay_filter_map, &hdr->src_satellite_id);
    if (replay_state != NULL) {
        const volatile uint32_t *quarantine_p = (const volatile uint32_t *)&replay_state->is_quarantined;
        if (*quarantine_p != 0U) {
            starsec_increment_telemetry(STARSEC_TELEM_DROPPED_STALE_SESSION);
            return XDP_DROP;
        }

        uint32_t snap_session_id = 0;
        uint64_t snap_base_seq = 0;
        uint64_t snap_bitmap = 0;
        bool state_consistent = false;

        const volatile uint32_t *lock_p = (const volatile uint32_t *)&replay_state->lock;
        const volatile uint32_t *sess_p = (const volatile uint32_t *)&replay_state->session_id;
        const volatile uint64_t *base_p = (const volatile uint64_t *)&replay_state->window_base_seq;
        const volatile uint64_t *bmap_p = (const volatile uint64_t *)&replay_state->window_bitmap;

        uint32_t seq1 = *lock_p;
        if ((seq1 & 1U) == 0U) {
            asm volatile("" ::: "memory");
            snap_session_id = *sess_p;
            snap_base_seq = *base_p;
            snap_bitmap = *bmap_p;
            asm volatile("" ::: "memory");
            uint32_t seq2 = *lock_p;
            if (seq1 == seq2) {
                state_consistent = true;
            }
        }
        if (!state_consistent) {
            seq1 = *lock_p;
            if ((seq1 & 1U) == 0U) {
                asm volatile("" ::: "memory");
                snap_session_id = *sess_p;
                snap_base_seq = *base_p;
                snap_bitmap = *bmap_p;
                asm volatile("" ::: "memory");
                uint32_t seq2 = *lock_p;
                if (seq1 == seq2) {
                    state_consistent = true;
                }
            }
        }
        if (!state_consistent) {
            seq1 = *lock_p;
            if ((seq1 & 1U) == 0U) {
                asm volatile("" ::: "memory");
                snap_session_id = *sess_p;
                snap_base_seq = *base_p;
                snap_bitmap = *bmap_p;
                asm volatile("" ::: "memory");
                uint32_t seq2 = *lock_p;
                if (seq1 == seq2) {
                    state_consistent = true;
                }
            }
        }

        if (!state_consistent) {
            starsec_increment_telemetry(STARSEC_TELEM_DROPPED_REPLAY_INCONSISTENT);
            return XDP_DROP;
        }

        if (snap_base_seq != 0 || snap_bitmap != 0 || snap_session_id != 0) {
            if (hdr->session_id < snap_session_id) {
                starsec_increment_telemetry(STARSEC_TELEM_DROPPED_STALE_SESSION);
                return XDP_DROP;
            }
            if (hdr->session_id == snap_session_id) {
                if (hdr->sequence_number <= snap_base_seq) {
                    uint64_t diff = snap_base_seq - hdr->sequence_number;
                    if (diff >= STARSEC_SLIDING_WINDOW_SIZE || (snap_bitmap & (1ULL << diff)) != 0) {
                        starsec_increment_telemetry(STARSEC_TELEM_DROPPED_REPLAY_SEQ);
                        return XDP_DROP;
                    }
                }
            }
        }
    }

    starsec_increment_telemetry(STARSEC_TELEM_PASSED_FRAMES);
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
