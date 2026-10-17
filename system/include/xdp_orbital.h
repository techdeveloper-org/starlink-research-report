/**
 * @file xdp_orbital.h
 * @brief StarSec In-Orbit Laser Link eBPF / XDP High-Performance Packet Filter Header.
 *
 * Implements wire-speed packet inspection, cryptographic header verification via
 * RFC 8439 Poly1305, anti-replay epoch window checking, and rogue BGP route isolation.
 *
 * Standard: StarSec Constellation Security Framework (CSF v1.0)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#ifndef STARSEC_XDP_ORBITAL_H
#define STARSEC_XDP_ORBITAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "starsec_poly1305.h"
#include "starsec_chacha20.h"

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

/** @brief Fixed 48-byte header structure for StarSec laser crosslink packets (RFC-001 v1.1). */
typedef struct __attribute__((packed)) {
    uint32_t magic;                      /**< 0x00: Must match STARSEC_ISL_MAGIC (0x53545345) */
    uint8_t  protocol_type;              /**< 0x04: isl_proto_type_t */
    uint8_t  flags;                      /**< 0x05: Priority & encryption flags */
    uint16_t src_satellite_id;           /**< 0x06: Originating Constellation Node ID */
    uint16_t dst_satellite_id;           /**< 0x08: Destination Constellation Node ID */
    uint32_t session_id;                 /**< 0x0A: Ephemeral session / boot counter ID */
    uint64_t sequence_number;            /**< 0x0E: Monotonic 64-bit packet sequence number */
    uint64_t tx_epoch_ms;                /**< 0x16: GPS UTC transmit timestamp */
    uint8_t  auth_tag[16];               /**< 0x1E: 128-bit RFC 8439 Poly1305 MAC */
    uint16_t payload_len;                /**< 0x2E: Payload length in bytes */
} starsec_isl_header_t;

/** @brief Size of the sliding anti-replay window in bits / packets. */
#define STARSEC_SLIDING_WINDOW_SIZE      64U

/** @brief Permitted forward clock skew tolerance in milliseconds (GPSDO sync bound). */
#define STARSEC_MAX_CLOCK_SKEW_AHEAD_MS  20ULL

/**
 * @brief Two-stage anti-replay state with RFC 6479 64-bit sliding window.
 *
 * Aligned to 64-byte hardware cache line boundary to prevent false sharing
 * across multi-core SMP packet processing threads.
 */
typedef struct __attribute__((aligned(64))) {
    uint64_t window_base_seq;            /**< Highest authenticated sequence committed to date */
    uint64_t window_bitmap;              /**< 64-bit bitmap of authenticated packets (bit 0 = base) */
    uint32_t session_id;                 /**< Active authenticated session identifier */
    uint32_t lock;                       /**< Cacheline-embedded atomic test-and-set spinlock */
    uint8_t  reserved_padding[40];       /**< Explicit padding guaranteeing full 64-byte cacheline isolation */
} isl_anti_replay_state_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(isl_anti_replay_state_t) == 64, "isl_anti_replay_state_t must be exactly 64 bytes");
_Static_assert(_Alignof(isl_anti_replay_state_t) == 64, "isl_anti_replay_state_t must be 64-byte aligned");
_Static_assert(offsetof(isl_anti_replay_state_t, window_base_seq) == 0, "window_base_seq offset mismatch");
_Static_assert(offsetof(isl_anti_replay_state_t, window_bitmap) == 8, "window_bitmap offset mismatch");
_Static_assert(offsetof(isl_anti_replay_state_t, session_id) == 16, "session_id offset mismatch");
_Static_assert(offsetof(isl_anti_replay_state_t, lock) == 20, "lock offset mismatch");
_Static_assert(offsetof(isl_anti_replay_state_t, reserved_padding) == 24, "reserved_padding offset mismatch");
#endif

/**
 * @brief Acquires the cacheline-embedded atomic spinlock and advances Seqlock counter to odd.
 * @param lock Pointer to 32-bit atomic lock word.
 */
static inline void starsec_spin_lock(volatile uint32_t *lock) {
    uint32_t expected = __atomic_load_n(lock, __ATOMIC_RELAXED);
    for (;;) {
        if ((expected & 1U) != 0U) {
            #if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
            #elif defined(__aarch64__)
            __asm__ __volatile__("yield");
            #endif
            expected = __atomic_load_n(lock, __ATOMIC_RELAXED);
            continue;
        }
        if (__atomic_compare_exchange_n(lock, &expected, expected + 1U, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            break;
        }
    }
}

/**
 * @brief Releases the cacheline-embedded atomic spinlock and advances Seqlock counter to even.
 * @param lock Pointer to 32-bit atomic lock word.
 */
static inline void starsec_spin_unlock(volatile uint32_t *lock) {
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_add_fetch(lock, 1U, __ATOMIC_RELEASE);
}

/**
 * @brief Begins a Seqlock write sequence by advancing the generation counter to an odd value.
 * @param lock Pointer to 32-bit Seqlock sequence generation counter word.
 */
static inline void starsec_seqlock_write_begin(volatile uint32_t *lock) {
    starsec_spin_lock(lock);
}

/**
 * @brief Completes a Seqlock write sequence by advancing the generation counter to an even value.
 * @param lock Pointer to 32-bit Seqlock sequence generation counter word.
 */
static inline void starsec_seqlock_write_end(volatile uint32_t *lock) {
    starsec_spin_unlock(lock);
}

/**
 * @brief Spacecraft receiver local clock synchronization reference.
 */
typedef struct {
    uint64_t local_epoch_ms;             /**< Trusted local UTC/GPS epoch in milliseconds */
    uint64_t max_past_drift_ms;          /**< Permitted backward flight+drift window (200 ms) */
    uint64_t max_future_skew_ms;         /**< Permitted forward clock skew tolerance (20 ms) */
} starsec_time_sync_state_t;

/**
 * @brief Diagnostic traffic counters for the in-orbit XDP filter.
 */
typedef struct {
    uint64_t dropped_bad_magic;
    uint64_t dropped_epoch_drift;
    uint64_t dropped_unauthorized_node;
    uint64_t dropped_invalid_auth;
    uint64_t dropped_clock_unavailable;
    uint64_t dropped_stale_session;
    uint64_t dropped_sync_failure;
    uint64_t dropped_replay_inconsistent;
    uint64_t passed_frames;
} xdp_filter_stats_t;

/**
 * @brief Configuration rules for the active optical link filter instance.
 */
typedef struct {
    uint16_t                  authorized_nodes[256];
    size_t                    authorized_count;
    starsec_time_sync_state_t clock_ref;
    uint8_t                   isl_shared_key[32];
    isl_anti_replay_state_t   replay_states[256];
    size_t                    replay_count;
} xdp_filter_config_t;

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
);

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
);

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
);

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
);

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
    xdp_filter_config_t       *config,
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
);

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
);

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
);

/**
 * @brief Identifies the active backend driver for BPF map synchronization.
 */
typedef enum {
    /** @brief Emulated in-memory table for testing, non-privileged or non-Linux hosts. */
    STARSEC_BPF_BACKEND_EMULATED       = 0,
    /** @brief Real Linux kernel BPF map accessed via bpf() syscall and map file descriptor. */
    STARSEC_BPF_BACKEND_KERNEL_SYSCALL = 1,
    /** @brief Real Linux kernel BPF map shared via pinned bpffs node (/sys/fs/bpf/...). */
    STARSEC_BPF_BACKEND_KERNEL_PINNED  = 2
} starsec_bpf_backend_type_t;

/**
 * @brief Real Linux kernel BPF syscall attribute wrapper matching Linux UAPI linux/bpf.h.
 */
union starsec_bpf_attr {
    struct {
        uint32_t map_type;               /**< BPF_MAP_TYPE_HASH = 1 */
        uint32_t key_size;               /**< sizeof(uint16_t) */
        uint32_t value_size;             /**< sizeof(isl_anti_replay_state_t) */
        uint32_t max_entries;            /**< 256 */
        uint32_t map_flags;              /**< 0 */
        uint32_t inner_map_fd;          /**< 0 */
        uint32_t numa_node;              /**< 0 */
        char     map_name[16];           /**< "isl_replay_map" */
        uint32_t map_ifindex;            /**< 0 */
        uint32_t btf_fd;                 /**< 0 */
        uint32_t btf_key_type_id;        /**< 0 */
        uint32_t btf_value_type_id;      /**< 0 */
    } map_create;

    struct {
        uint32_t map_fd;                 /**< Active kernel map file descriptor */
        uint64_t key;                    /**< Pointer cast to uint64_t */
        union {
            uint64_t value;              /**< Pointer cast to uint64_t */
            uint64_t next_key;
        };
        uint64_t flags;                  /**< BPF_ANY = 0 */
    } map_elem;

    struct {
        uint64_t pathname;               /**< Pointer to const char* path in bpffs cast to uint64_t */
        uint32_t bpf_fd;                 /**< Map or program file descriptor to pin */
        uint32_t file_flags;             /**< File flags (0) */
    } obj_pin_get;
};

/**
 * @brief Immutable snapshot of anti-replay state captured under spinlock.
 */
typedef struct {
    uint64_t window_base_seq;            /**< Highest authenticated sequence number */
    uint64_t window_bitmap;              /**< 64-bit sliding window bitmap */
    uint32_t session_id;                 /**< Session identifier */
    uint32_t reserved;                   /**< 32-bit alignment padding */
} starsec_replay_snapshot_t;

/**
 * @brief Attempts to read a consistent snapshot from a Seqlock-protected state without blocking.
 * @param state Pointer to source state protected by Seqlock.
 * @param out_snapshot Pointer to snapshot buffer receiving untorn state fields.
 * @return true if snapshot was captured consistently without torn reads, false if write in progress.
 */
static inline bool starsec_seqlock_read_try(
    const isl_anti_replay_state_t *state,
    starsec_replay_snapshot_t     *out_snapshot
) {
    if (state == NULL || out_snapshot == NULL) {
        return false;
    }
    uint32_t seq1 = __atomic_load_n(&state->lock, __ATOMIC_ACQUIRE);
    if ((seq1 & 1U) != 0U) {
        return false;
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    out_snapshot->window_base_seq = state->window_base_seq;
    out_snapshot->window_bitmap = state->window_bitmap;
    out_snapshot->session_id = state->session_id;
    out_snapshot->reserved = 0;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    uint32_t seq2 = __atomic_load_n(&state->lock, __ATOMIC_ACQUIRE);
    return (seq1 == seq2);
}

/**
 * @brief In-kernel eBPF BPF map storage abstraction with dual-backend support.
 */
typedef struct {
    starsec_bpf_backend_type_t backend_type;        /**< Active driver backend */
    int                        map_fd;              /**< Valid kernel map fd (>= 0), or -1 */
    char                       pinned_path[128];    /**< Pinned BPF filesystem path (/sys/fs/bpf/...) */
    uint32_t                   map_lock;            /**< Spinlock protecting map operations */
    uint64_t                   kernel_sync_count;   /**< Total successful bpf() syscall operations */
    uint64_t                   kernel_sync_errors;  /**< Failed bpf() syscall invocations */
    uint16_t                   satellite_ids[256];
    isl_anti_replay_state_t    states[256];
    size_t                     count;
} starsec_kernel_bpf_map_t;

/**
 * @brief Takes a concurrency-safe atomic snapshot of an anti-replay state.
 * @param state Pointer to source anti-replay state.
 * @param snapshot Output buffer receiving consistent snapshot.
 */
void xdp_replay_take_snapshot(
    isl_anti_replay_state_t   *state,
    starsec_replay_snapshot_t *snapshot
);

/**
 * @brief Creates an authentic in-kernel BPF hash map using the Linux bpf() system call.
 * @param map Pointer to starsec_kernel_bpf_map_t to configure.
 * @param map_name String identifier for the BPF map (up to 15 chars).
 * @param max_entries Maximum capacity of the map (typically 256).
 * @return 0 on success, negative errno on failure.
 */
int starsec_bpf_map_create_kernel(
    starsec_kernel_bpf_map_t *map,
    const char               *map_name,
    uint32_t                  max_entries
);

/**
 * @brief Pins a BPF file descriptor to bpffs using the Linux bpf() system call (BPF_OBJ_PIN = 6).
 * @param fd Kernel BPF object file descriptor (map or program).
 * @param pathname Absolute path in BPF filesystem (/sys/fs/bpf/...).
 * @return 0 on success, negative errno on failure.
 */
int starsec_bpf_obj_pin(int fd, const char *pathname);

/**
 * @brief Opens an existing pinned BPF object from bpffs using Linux bpf() system call (BPF_OBJ_GET = 7).
 * @param pathname Absolute path in BPF filesystem (/sys/fs/bpf/...).
 * @return Valid file descriptor (>= 0) on success, negative errno on failure.
 */
int starsec_bpf_obj_get(const char *pathname);

/**
 * @brief Pins the active in-kernel BPF map into bpffs for sharing with eBPF programs.
 * @param map Pointer to active BPF map instance.
 * @param pinned_path Absolute path destination in /sys/fs/bpf.
 * @return 0 on success, negative errno on failure.
 */
int starsec_bpf_map_pin(
    starsec_kernel_bpf_map_t *map,
    const char               *pinned_path
);

/**
 * @brief Connects to an existing pinned in-kernel BPF map loaded by XDP / bpftool.
 * @param map Pointer to starsec_kernel_bpf_map_t instance.
 * @param pinned_path Path to pinned BPF map node (/sys/fs/bpf/starsec_maps/isl_replay_filter_map).
 * @return 0 on success, negative errno on failure.
 */
int starsec_bpf_map_open_pinned(
    starsec_kernel_bpf_map_t *map,
    const char               *pinned_path
);

/**
 * @brief Verifies memory layout equivalence, struct offsets, and cacheline alignment at runtime.
 * @return true if all struct sizes, alignments, and offsets strictly match specifications.
 */
bool starsec_verify_memory_layout(void);

/**
 * @brief Updates an element in the Linux kernel BPF map via BPF_MAP_UPDATE_ELEM syscall.
 * @param map Pointer to active BPF map instance.
 * @param satellite_id 16-bit Constellation Node ID (key).
 * @param state Pointer to consistent anti-replay state snapshot (value).
 * @return true if kernel BPF map updated successfully, false on syscall error.
 */
bool starsec_bpf_map_update_elem_kernel(
    starsec_kernel_bpf_map_t      *map,
    uint16_t                       satellite_id,
    const isl_anti_replay_state_t *state
);

/**
 * @brief Closes the kernel BPF map file descriptor and resets map backend.
 * @param map Pointer to active BPF map instance.
 */
void starsec_bpf_map_close(starsec_kernel_bpf_map_t *map);

/**
 * @brief Integrated Two-Tier In-Orbit Ingress Security Pipeline.
 *
 * Couples Tier 1 in-kernel eBPF fast-path filter with Tier 2 userspace / AF_XDP
 * RFC 8439 AEAD cryptographic authentication and synchronized atomic replay commit.
 */
typedef struct {
    starsec_kernel_bpf_map_t kernel_bpf_map;
    xdp_filter_config_t      filter_config;
    xdp_filter_stats_t       stats;
} starsec_xdp_pipeline_t;

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
);

/**
 * @brief Initializes the two-tier pipeline connecting to an existing pinned in-kernel BPF map.
 * @param pipeline Pointer to starsec_xdp_pipeline_t instance to initialize.
 * @param pinned_path Path to pinned BPF map node in /sys/fs/bpf.
 * @param shared_key_32 Pointer to 32-byte shared ISL master secret.
 * @param clock_ref Pointer to trusted local spacecraft receiver clock reference.
 * @return 0 on success, negative errno on failure.
 */
int xdp_pipeline_init_pinned(
    starsec_xdp_pipeline_t          *pipeline,
    const char                      *pinned_path,
    const uint8_t                    shared_key_32[32],
    const starsec_time_sync_state_t *clock_ref
);

/**
 * @brief Registers an authorized satellite node in both Tier 1 BPF map and Tier 2 filter.
 * @param pipeline Pointer to active pipeline instance.
 * @param satellite_id 16-bit Constellation Node ID.
 * @return true on success, false if table is full.
 */
bool xdp_pipeline_authorize_node(
    starsec_xdp_pipeline_t *pipeline,
    uint16_t                satellite_id
);

/**
 * @brief Synchronizes committed userspace replay state directly into the in-kernel BPF map.
 * @param bpf_map Pointer to kernel BPF map instance.
 * @param satellite_id 16-bit Constellation Node ID.
 * @param committed_state Pointer to newly committed atomic replay state.
 * @return true on successful synchronization, false if node not found or kernel sync error.
 */
bool xdp_sync_kernel_replay_map(
    starsec_kernel_bpf_map_t *bpf_map,
    uint16_t                  satellite_id,
    isl_anti_replay_state_t  *committed_state
);

/**
 * @brief Seals an optical ISL frame using RFC 8439 Section 2.8 AEAD encryption.
 *
 * Implements wire-speed packet envelope construction:
 * - Packs header fields into frame buffer.
 * - Extracts 32-byte AAD from header (fields up to auth_tag + payload_len).
 * - Derives deterministic 96-bit nonce from session_id and sequence_number.
 * - Encrypts plaintext payload into ciphertext in the frame buffer.
 * - Computes authentic 16-byte Poly1305 MAC tag and writes directly into hdr->auth_tag.
 *
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
);

/**
 * @brief Opens and authenticates a sealed optical ISL frame using RFC 8439 Section 2.8 AEAD.
 *
 * Validates 128-bit Poly1305 authentication tag over header AAD and payload ciphertext in constant time.
 * If valid, decrypts ciphertext into out_plaintext. If invalid or tampered, wipes plaintext output
 * and returns STARSEC_CRYPTO_ERR_AUTH_FAIL.
 *
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
);

/**
 * @brief Processes an ingress frame through the integrated two-tier kernel/userspace pipeline.
 *
 * 1. Tier 1 (eBPF in-kernel fast-path): Validates frame length, magic, local clock freshness,
 *    authorized satellite ID, and speculative replay against BPF map. Drops at wire speed on failure.
 * 2. Tier 2 (AF_XDP crypto engine): Executes RFC 8439 AEAD authenticated decryption (xdp_open_isl_frame).
 * 3. Atomic State Commit: On successful auth, executes xdp_commit_replay_window_atomic() and
 *    synchronizes the updated replay state directly into the Tier 1 BPF map via xdp_sync_kernel_replay_map().
 *
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
);

/**
 * @brief Resets all diagnostic telemetry counters to zero.
 * @param stats Pointer to telemetry statistics structure.
 */
void xdp_reset_filter_stats(xdp_filter_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* STARSEC_XDP_ORBITAL_H */

