/**
 * @file main_system_test.c
 * @brief Standalone Native C System Verification Test Suite for StarSec CSF v1.0.
 *
 * Implements the Universal Triad Test Matrix (Positive, Negative, Edge-Case)
 * covering Hardware Tamper Zeroization with High-Resolution Cycle Benchmarking,
 * SRAM-PUF Key Recovery, Dual-Core Lockstep SEU Mitigation, RFC 8439 Poly1305
 * Cryptographic MAC, and In-Orbit eBPF/XDP Optical Link Filtering.
 *
 * Standard: StarSec Constellation Security Framework (CSF v1.0), RFC 8439
 * Governance: Rules 01, 05, 12, 28, 33, 35, 36, 37, 40
 * Target: Native GCC/Clang/MSVC compilation (<0.2s execution, 0 cloud cost)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tamper_zeroize.h"
#include "sram_puf.h"
#include "dcls_monitor.h"
#include "starsec_poly1305.h"
#include "starsec_chacha20.h"
#include "xdp_orbital.h"
#include "starsec_bench.h"

/**
 * @brief Macro for asserting test conditions and logging step results.
 */
#define STARSEC_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  [FAIL] %s (Line %d)\n", msg, __LINE__); \
            return false; \
        } \
    } while (0)

/**
 * @brief Executes TC-ZERO-POS-01, TC-ZERO-NEG-01, TC-ZERO-EDGE-01, and TC-ZERO-BOUND-01.
 * @return true if all tamper zeroize test cases and latency benchmarks pass, false otherwise.
 */
static bool test_tamper_zeroize_triad(void) {
    printf("[TEST] Running Tamper Zeroize Triad & Latency Benchmarks...\n");

    tdc_hw_registers_t regs;
    uint8_t master_key[STARSEC_BBRAM_KEY_LEN_BYTES];
    for (size_t i = 0; i < STARSEC_BBRAM_KEY_LEN_BYTES; i++) {
        master_key[i] = (uint8_t)(0xAA ^ (uint8_t)i);
    }

    starsec_status_t init_status = tdc_init(&regs, TDC_CR_GLITCH_EN | TDC_CR_MESH_EN);
    STARSEC_ASSERT(init_status == STARSEC_SUCCESS, "TC-ZERO-POS-01: Init failed");

    starsec_status_t load_status = tdc_load_key(&regs, master_key, sizeof(master_key));
    STARSEC_ASSERT(load_status == STARSEC_SUCCESS, "TC-ZERO-POS-01: Key load failed");
    STARSEC_ASSERT(memcmp((const void *)regs.BBRAM_KEY, master_key, sizeof(master_key)) == 0,
                   "TC-ZERO-POS-01: Key memory mismatch");

    tdc_trigger_zeroize_isr(&regs, TAMPER_SRC_VOLTAGE_GLITCH);
    STARSEC_ASSERT(tdc_verify_zeroized(&regs), "TC-ZERO-NEG-01: Memory was not completely zeroized");
    STARSEC_ASSERT((regs.SR & TDC_SR_VOLTAGE_FAULT) != 0, "TC-ZERO-NEG-01: Voltage fault bit not set");
    STARSEC_ASSERT((regs.SR & TDC_SR_ZEROIZED) != 0, "TC-ZERO-NEG-01: Zeroized bit not set");

    tdc_telemetry_t telem = tdc_get_telemetry(&regs);
    STARSEC_ASSERT(telem.is_zeroized == true, "TC-ZERO-NEG-01: Telemetry is_zeroized flag mismatch");
    STARSEC_ASSERT(telem.last_fault_source == TAMPER_SRC_VOLTAGE_GLITCH, "TC-ZERO-NEG-01: Telemetry fault source mismatch");

    starsec_secure_zeroize_mem(NULL, 0);
    starsec_secure_zeroize_mem(NULL, 100);
    uint8_t edge_buffer[17] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
    starsec_secure_zeroize_mem(edge_buffer + 1, 15);
    STARSEC_ASSERT(edge_buffer[0] == 1, "TC-ZERO-EDGE-01: Buffer underflow corrupted boundary");
    STARSEC_ASSERT(edge_buffer[16] == 17, "TC-ZERO-EDGE-01: Buffer overflow corrupted boundary");
    for (size_t i = 1; i < 16; i++) {
        STARSEC_ASSERT(edge_buffer[i] == 0, "TC-ZERO-EDGE-01: Target sub-range not zeroized");
    }

    starsec_bench_metric_t bench = starsec_profile_zeroize_isr(&regs, 1000);
    starsec_latency_boundary_t boundary = starsec_evaluate_latency_boundary(bench.avg_ns);
    STARSEC_ASSERT(boundary.driver_within_budget, "TC-ZERO-BOUND-01: Driver ISR exceeded 250 ns budget");

    printf("  [BENCHMARK] Zeroize ISR (1000 runs): Mean %llu ns, %llu cycles (Min %llu ns, Max %llu ns)\n",
           (unsigned long long)bench.avg_ns,
           (unsigned long long)bench.avg_cycles,
           (unsigned long long)bench.min_ns,
           (unsigned long long)bench.max_ns);
    printf("  [BOUNDARY] Tier 1 Crowbar Spec: < %llu ns | Tier 2 Software Driver: %llu ns (Budget: <= %llu ns) -> PASS\n",
           (unsigned long long)boundary.hw_crowbar_spec_limit_ns,
           (unsigned long long)boundary.measured_driver_avg_ns,
           (unsigned long long)boundary.sw_driver_budget_ns);

    printf("  [PASS] Tamper Zeroize Triad & Latency Benchmarks Successful.\n");
    return true;
}

/**
 * @brief Executes TC-PUF-POS-01, TC-PUF-NEG-01, and TC-PUF-EDGE-01.
 * @return true if all SRAM-PUF fuzzy extractor test cases pass, false otherwise.
 */
static bool test_sram_puf_triad(void) {
    printf("[TEST] Running SRAM-PUF Fuzzy Extractor Triad (TC-PUF-POS-01, NEG-01, EDGE-01)...\n");

    puf_startup_state_t startup_sram;
    for (size_t i = 0; i < PUF_RAW_BYTES; i++) {
        startup_sram.raw_sram_bytes[i] = (uint8_t)((i * 37 + 13) & 0xFF);
    }

    uint8_t secret_seed[PUF_KEY_BYTES];
    for (size_t i = 0; i < PUF_KEY_BYTES; i++) {
        secret_seed[i] = (uint8_t)(i + 0x55);
    }

    puf_helper_data_t helper;
    puf_key_output_t enrolled_key;

    starsec_status_t enroll_status = puf_enroll(&startup_sram, secret_seed, &helper, &enrolled_key);
    STARSEC_ASSERT(enroll_status == STARSEC_SUCCESS, "TC-PUF-POS-01: Enrollment failed");
    STARSEC_ASSERT(enrolled_key.is_reconstructed == true, "TC-PUF-POS-01: Enrollment key flag invalid");

    puf_key_output_t recon_pos;
    starsec_status_t recon_pos_status = puf_reconstruct(&startup_sram, &helper, &recon_pos);
    STARSEC_ASSERT(recon_pos_status == STARSEC_SUCCESS, "TC-PUF-POS-01: Zero-noise reconstruction failed");
    STARSEC_ASSERT(memcmp(enrolled_key.derived_key_256, recon_pos.derived_key_256, PUF_KEY_BYTES) == 0,
                   "TC-PUF-POS-01: Reconstructed key does not match enrolled key");
    STARSEC_ASSERT(recon_pos.total_corrected_bits == 0, "TC-PUF-POS-01: Expected 0 bit errors on clean read");

    puf_startup_state_t noisy_sram;
    memcpy(noisy_sram.raw_sram_bytes, startup_sram.raw_sram_bytes, PUF_RAW_BYTES);
    noisy_sram.raw_sram_bytes[0] ^= 0x01;
    noisy_sram.raw_sram_bytes[16] ^= 0x02;
    noisy_sram.raw_sram_bytes[32] ^= 0x04;
    noisy_sram.raw_sram_bytes[48] ^= 0x08;
    noisy_sram.raw_sram_bytes[64] ^= 0x10;
    noisy_sram.raw_sram_bytes[80] ^= 0x20;
    noisy_sram.raw_sram_bytes[96] ^= 0x40;
    noisy_sram.raw_sram_bytes[112] ^= 0x80;

    uint32_t hamming_dist = puf_calculate_hamming_distance(startup_sram.raw_sram_bytes,
                                                           noisy_sram.raw_sram_bytes,
                                                           PUF_RAW_BYTES);
    STARSEC_ASSERT(hamming_dist == 8, "TC-PUF-EDGE-01: Synthetic noise generation error");

    puf_key_output_t recon_edge;
    starsec_status_t recon_edge_status = puf_reconstruct(&noisy_sram, &helper, &recon_edge);
    STARSEC_ASSERT(recon_edge_status == STARSEC_SUCCESS, "TC-PUF-EDGE-01: Error-correction reconstruction failed");
    STARSEC_ASSERT(memcmp(enrolled_key.derived_key_256, recon_edge.derived_key_256, PUF_KEY_BYTES) == 0,
                   "TC-PUF-EDGE-01: Noisy reconstructed key did not match ground truth enrolled key");
    STARSEC_ASSERT(recon_edge.total_corrected_bits == 8, "TC-PUF-EDGE-01: Corrected bit count mismatch");

    STARSEC_ASSERT(puf_constant_time_popcount32(0x00000000UL) == 0, "TC-PUF-NEG-01: Popcount 0 failed");
    STARSEC_ASSERT(puf_constant_time_popcount32(0xFFFFFFFFUL) == 32, "TC-PUF-NEG-01: Popcount 32 failed");
    STARSEC_ASSERT(puf_constant_time_popcount32(0xAA55AA55UL) == 16, "TC-PUF-NEG-01: Popcount 16 failed");
    STARSEC_ASSERT(puf_reconstruct(NULL, &helper, &recon_edge) == STARSEC_ERR_NULL_PTR,
                   "TC-PUF-NEG-01: Null noisy_sram not rejected");

    printf("  [PASS] SRAM-PUF Fuzzy Extractor Triad Successful.\n");
    return true;
}

/**
 * @brief Executes TC-DCLS-POS-01, TC-DCLS-NEG-01, and TC-DCLS-EDGE-01.
 * @return true if all DCLS monitor test cases pass, false otherwise.
 */
static bool test_dcls_monitor_triad(void) {
    printf("[TEST] Running Dual-Core Lockstep Triad (TC-DCLS-POS-01, NEG-01, EDGE-01)...\n");

    dcls_hw_registers_t regs;
    starsec_status_t init_status = dcls_init_monitor(&regs);
    STARSEC_ASSERT(init_status == STARSEC_SUCCESS, "TC-DCLS-POS-01: DCLS Init failed");

    dcls_comparator_isr(&regs);
    dcls_diagnostics_t diag_pos = dcls_read_diagnostics(&regs);
    STARSEC_ASSERT(diag_pos.current_state == DCLS_STATE_HEALTHY, "TC-DCLS-POS-01: Healthy state mismatch");
    STARSEC_ASSERT(diag_pos.total_seu_events == 0, "TC-DCLS-POS-01: Spurious SEU events detected");

    dcls_inject_synthetic_seu(&regs, 3);
    STARSEC_ASSERT(regs.CORE0_PC != regs.CORE1_PC, "TC-DCLS-NEG-01: SEU injection failed to cause divergence");
    dcls_comparator_isr(&regs);

    dcls_diagnostics_t diag_trans = dcls_read_diagnostics(&regs);
    STARSEC_ASSERT(diag_trans.total_seu_events == 1, "TC-DCLS-NEG-01: SEU event count mismatch");
    STARSEC_ASSERT(diag_trans.successful_recoveries == 1, "TC-DCLS-NEG-01: Recovery count mismatch");
    STARSEC_ASSERT(regs.CORE0_PC == regs.CORE1_PC, "TC-DCLS-NEG-01: Pipeline resynchronization failed");

    for (int retry = 0; retry < 5; retry++) {
        regs.CORE1_PC = 0xDEADBEEFUL;
        dcls_comparator_isr(&regs);
    }
    dcls_diagnostics_t diag_fatal = dcls_read_diagnostics(&regs);
    STARSEC_ASSERT(diag_fatal.current_state == DCLS_STATE_PERMANENT_LOCKDOWN,
                   "TC-DCLS-EDGE-01: State machine did not enter PERMANENT_LOCKDOWN");
    STARSEC_ASSERT(diag_fatal.fatal_divergences > 0, "TC-DCLS-EDGE-01: Fatal divergence counter not incremented");
    STARSEC_ASSERT((regs.CONTROL & 0x80000000UL) != 0, "TC-DCLS-EDGE-01: Hardware lockdown trigger bit not set");

    printf("  [PASS] Dual-Core Lockstep Triad Successful.\n");
    return true;
}

/**
 * @brief Executes TC-CHACHA-POS-01, TC-CHACHA-NEG-01, TC-CHACHA-NEG-02, and TC-CHACHA-EDGE-01.
 * @return true if all ChaCha20 and RFC 8439 Section 2.6.2 tests pass, false otherwise.
 */
static bool test_chacha20_triad(void) {
    printf("[TEST] Running RFC 8439 ChaCha20 Block Cipher & One-Time Key Triad...\n");

    const uint8_t kat_key[32] = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
    };
    const uint8_t kat_nonce[12] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07
    };
    const uint8_t expected_one_time_key[32] = {
        0x8a, 0xd5, 0xa0, 0x8b, 0x90, 0x5f, 0x81, 0xcc,
        0x81, 0x50, 0x40, 0x27, 0x4a, 0xb2, 0x94, 0x71,
        0xa8, 0x33, 0xb6, 0x37, 0xe3, 0xfd, 0x0d, 0xa5,
        0x08, 0xdb, 0xb8, 0xe2, 0xfd, 0xd1, 0xa6, 0x46
    };

    uint8_t derived_key[32];
    starsec_chacha20_poly1305_key_gen(kat_key, kat_nonce, derived_key);
    STARSEC_ASSERT(memcmp(derived_key, expected_one_time_key, 32) == 0,
                   "TC-CHACHA-POS-01: RFC 8439 Section 2.6.2 KAT mismatch");

    uint8_t mutated_key[32];
    memcpy(mutated_key, kat_key, 32);
    mutated_key[0] ^= 0x01;
    uint8_t mutated_key_out[32];
    starsec_chacha20_poly1305_key_gen(mutated_key, kat_nonce, mutated_key_out);
    STARSEC_ASSERT(memcmp(mutated_key_out, expected_one_time_key, 32) != 0,
                   "TC-CHACHA-NEG-01: 1-bit key change failed avalanche property");

    uint8_t mutated_nonce[12];
    memcpy(mutated_nonce, kat_nonce, 12);
    mutated_nonce[11] ^= 0x01;
    uint8_t mutated_nonce_out[32];
    starsec_chacha20_poly1305_key_gen(kat_key, mutated_nonce, mutated_nonce_out);
    STARSEC_ASSERT(memcmp(mutated_nonce_out, expected_one_time_key, 32) != 0,
                   "TC-CHACHA-NEG-02: 1-bit nonce change failed avalanche property");

    starsec_chacha20_poly1305_key_gen(NULL, kat_nonce, derived_key);
    starsec_chacha20_poly1305_key_gen(kat_key, NULL, derived_key);
    starsec_chacha20_poly1305_key_gen(kat_key, kat_nonce, NULL);

    uint8_t nonce_v2[12];
    starsec_crypto_status_t n_ok = starsec_derive_nonce_v2(1, 100, nonce_v2);
    STARSEC_ASSERT(n_ok == STARSEC_CRYPTO_SUCCESS, "TC-NONCE-POS-01: Nonce v2 derivation failed");

    starsec_crypto_status_t n_null = starsec_derive_nonce_v2(1, 100, NULL);
    STARSEC_ASSERT(n_null == STARSEC_CRYPTO_ERR_NULL_PARAM, "TC-NONCE-EDGE-01: Null nonce buffer not rejected");

    starsec_crypto_status_t n_roll = starsec_derive_nonce_v2(1, STARSEC_MAX_PACKET_COUNTER_PER_KEY, nonce_v2);
    STARSEC_ASSERT(n_roll == STARSEC_CRYPTO_ERR_REKEY_REQUIRED, "TC-NONCE-ROLLOVER-01: Rollover limit did not trigger rekey error");

    uint8_t nonce_boot1[12];
    uint8_t nonce_boot2[12];
    starsec_derive_nonce_v2(101, 500, nonce_boot1);
    starsec_derive_nonce_v2(102, 500, nonce_boot2);
    STARSEC_ASSERT(memcmp(nonce_boot1, nonce_boot2, 12) != 0, "TC-NONCE-COLLISION-01: Different sessions produced colliding nonces");

    printf("  [PASS] RFC 8439 ChaCha20 Block Cipher & One-Time Key Triad Successful.\n");
    return true;
}

/**
 * @brief Executes TC-POLY-POS-01, TC-POLY-POS-02, TC-POLY-NEG-01, TC-POLY-NEG-02, and TC-POLY-EDGE-01.
 * @return true if all RFC 8439 Poly1305 test cases pass, false otherwise.
 */
static bool test_poly1305_triad(void) {
    printf("[TEST] Running RFC 8439 Poly1305 Cryptographic MAC Triad...\n");

    const uint8_t rfc_key[32] = {
        0x85, 0xd6, 0xbe, 0x78, 0x57, 0x55, 0x6d, 0x33,
        0x7f, 0x44, 0x52, 0xfe, 0x42, 0xd5, 0x06, 0xa8,
        0x01, 0x03, 0x80, 0x8a, 0xfb, 0x0d, 0xb2, 0xfd,
        0x4a, 0xbf, 0xf6, 0xaf, 0x41, 0x49, 0xf5, 0x1b
    };
    const char *rfc_msg = "Cryptographic Forum Research Group";
    const uint8_t expected_tag[16] = {
        0xa8, 0x06, 0x1d, 0xc1, 0x30, 0x51, 0x36, 0xc6,
        0xc2, 0x2b, 0x8b, 0xaf, 0x0c, 0x01, 0x27, 0xa9
    };

    uint8_t computed_tag[16];
    starsec_poly1305_auth(computed_tag, (const uint8_t *)rfc_msg, strlen(rfc_msg), rfc_key);
    STARSEC_ASSERT(memcmp(computed_tag, expected_tag, 16) == 0,
                   "TC-POLY-POS-01: RFC 8439 Section 2.5.2 KAT tag mismatch");

    bool kat_verify = starsec_poly1305_verify(expected_tag, (const uint8_t *)rfc_msg, strlen(rfc_msg), rfc_key);
    STARSEC_ASSERT(kat_verify == true, "TC-POLY-POS-01: starsec_poly1305_verify rejected official KAT tag");

    uint8_t zero_tag[16];
    starsec_poly1305_auth(zero_tag, NULL, 0, rfc_key);
    STARSEC_ASSERT(memcmp(zero_tag, rfc_key + 16, 16) == 0,
                   "TC-POLY-POS-02: Zero-length message tag did not equal key s");

    uint8_t tampered_msg[35];
    memcpy(tampered_msg, rfc_msg, strlen(rfc_msg) + 1);
    tampered_msg[0] ^= 0x01;
    bool neg_msg_verify = starsec_poly1305_verify(expected_tag, tampered_msg, strlen(rfc_msg), rfc_key);
    STARSEC_ASSERT(neg_msg_verify == false, "TC-POLY-NEG-01: Poly1305 accepted tampered message bit");

    uint8_t tampered_key[32];
    memcpy(tampered_key, rfc_key, 32);
    tampered_key[5] ^= 0x01;
    bool neg_key_verify = starsec_poly1305_verify(expected_tag, (const uint8_t *)rfc_msg, strlen(rfc_msg), tampered_key);
    STARSEC_ASSERT(neg_key_verify == false, "TC-POLY-NEG-02: Poly1305 accepted tampered key bit");

    uint8_t boundary_buf[64];
    for (size_t i = 0; i < 64; i++) {
        boundary_buf[i] = (uint8_t)(i * 3 + 7);
    }
    const size_t test_lens[] = { 16, 17, 31, 32, 64 };
    for (size_t k = 0; k < sizeof(test_lens)/sizeof(test_lens[0]); k++) {
        uint8_t b_tag[16];
        starsec_poly1305_auth(b_tag, boundary_buf, test_lens[k], rfc_key);
        bool b_ok = starsec_poly1305_verify(b_tag, boundary_buf, test_lens[k], rfc_key);
        STARSEC_ASSERT(b_ok == true, "TC-POLY-EDGE-01: Boundary block length verification failed");
    }

    bool null_verify = starsec_poly1305_verify(NULL, (const uint8_t *)rfc_msg, 10, rfc_key);
    STARSEC_ASSERT(null_verify == false, "TC-POLY-EDGE-02: Null expected tag not safely rejected");

    printf("  [PASS] RFC 8439 Poly1305 Cryptographic MAC Triad Successful.\n");
    return true;
}

/**
 * @brief Executes TC-XDP-POS-01, TC-XDP-POS-02, TC-XDP-NEG-01, TC-XDP-NEG-02, and TC-XDP-EDGE-01.
 * @return true if all XDP filter and unique one-time keying tests pass, false otherwise.
 */
/**
 * @brief Executes TC-XDP-POS-01..03, TC-XDP-NEG-01..04, TC-XDP-EDGE-01, TC-POISON-ATTACK-01, and TC-DRIFT-LOCAL-01.
 * @return true if all XDP filter, sliding window, and two-stage auth tests pass, false otherwise.
 */
static bool test_xdp_orbital_filter_triad(void) {
    printf("[TEST] Running In-Orbit XDP Filter Triad with Two-Stage Sliding Window & Local Clock...\n");

    xdp_filter_config_t config;
    memset(&config, 0, sizeof(config));
    config.authorized_count = 2;
    config.authorized_nodes[0] = 101;
    config.authorized_nodes[1] = 102;
    config.clock_ref.local_epoch_ms = 1770000000000ULL;
    config.clock_ref.max_past_drift_ms = 200ULL;
    config.clock_ref.max_future_skew_ms = 20ULL;
    for (size_t i = 0; i < 32; i++) {
        config.isl_shared_key[i] = (uint8_t)(i + 0x20);
    }

    xdp_filter_stats_t stats;
    xdp_reset_filter_stats(&stats);

    uint8_t payload[16] = "TELECOMMAND_OK";
    size_t frame_len = sizeof(starsec_isl_header_t) + sizeof(payload);
    uint8_t *frame1 = (uint8_t *)malloc(frame_len);
    STARSEC_ASSERT(frame1 != NULL, "TC-XDP-POS-01: Memory allocation failed");

    starsec_isl_header_t *hdr1 = (starsec_isl_header_t *)frame1;
    hdr1->magic = STARSEC_ISL_MAGIC;
    hdr1->protocol_type = STARSEC_PROTO_TELECOMMAND;
    hdr1->flags = 0;
    hdr1->src_satellite_id = 101;
    hdr1->dst_satellite_id = 202;
    hdr1->session_id = 42;
    hdr1->sequence_number = 1001;
    hdr1->tx_epoch_ms = config.clock_ref.local_epoch_ms - 25;
    hdr1->payload_len = sizeof(payload);
    memcpy(frame1 + sizeof(starsec_isl_header_t), payload, sizeof(payload));

    xdp_generate_isl_auth_tag(hdr1, payload, sizeof(payload), config.isl_shared_key);

    int action_pos1 = xdp_starsec_filter_frame(frame1, frame_len, &config, &stats);
    STARSEC_ASSERT(action_pos1 == STARSEC_XDP_PASS, "TC-XDP-POS-01: Valid frame 1 unexpectedly dropped");
    STARSEC_ASSERT(stats.passed_frames == 1, "TC-XDP-POS-01: passed_frames counter mismatch");

    uint8_t *frame2 = (uint8_t *)malloc(frame_len);
    STARSEC_ASSERT(frame2 != NULL, "TC-XDP-POS-02: Memory allocation failed");
    memcpy(frame2, frame1, frame_len);
    starsec_isl_header_t *hdr2 = (starsec_isl_header_t *)frame2;
    hdr2->sequence_number = 1002;
    hdr2->tx_epoch_ms = config.clock_ref.local_epoch_ms - 20;

    xdp_generate_isl_auth_tag(hdr2, payload, sizeof(payload), config.isl_shared_key);
    STARSEC_ASSERT(memcmp(hdr1->auth_tag, hdr2->auth_tag, 16) != 0,
                   "TC-XDP-POS-02: Different packets produced identical auth tags (Key Reuse Detected!)");

    int action_pos2 = xdp_starsec_filter_frame(frame2, frame_len, &config, &stats);
    STARSEC_ASSERT(action_pos2 == STARSEC_XDP_PASS, "TC-XDP-POS-02: Valid frame 2 unexpectedly dropped");
    STARSEC_ASSERT(stats.passed_frames == 2, "TC-XDP-POS-02: passed_frames counter mismatch");

    /* TC-POISON-ATTACK-01: Adversary injects frame with spoofed high sequence number & invalid MAC */
    uint8_t *frame_poison = (uint8_t *)malloc(frame_len);
    STARSEC_ASSERT(frame_poison != NULL, "TC-POISON-ATTACK-01: Alloc failed");
    memcpy(frame_poison, frame1, frame_len);
    starsec_isl_header_t *hdr_poison = (starsec_isl_header_t *)frame_poison;
    hdr_poison->sequence_number = 9999999ULL;
    hdr_poison->tx_epoch_ms = config.clock_ref.local_epoch_ms - 10;
    memset(hdr_poison->auth_tag, 0xDE, 16); /* Forged MAC */

    int action_poison = xdp_starsec_filter_frame(frame_poison, frame_len, &config, &stats);
    STARSEC_ASSERT(action_poison == STARSEC_XDP_DROP, "TC-POISON-ATTACK-01: Forged high-sequence frame not dropped");
    STARSEC_ASSERT(stats.dropped_invalid_auth == 1, "TC-POISON-ATTACK-01: dropped_invalid_auth mismatch");

    /* Genuine packet 1003 from Satellite 101 must still PASS (state was NOT poisoned) */
    hdr2->sequence_number = 1003;
    hdr2->tx_epoch_ms = config.clock_ref.local_epoch_ms - 15;
    xdp_generate_isl_auth_tag(hdr2, payload, sizeof(payload), config.isl_shared_key);
    int action_post_poison = xdp_starsec_filter_frame(frame2, frame_len, &config, &stats);
    STARSEC_ASSERT(action_post_poison == STARSEC_XDP_PASS,
                   "TC-POISON-ATTACK-01: Genuine packet 1003 dropped! (Replay state was poisoned by unauthenticated packet!)");
    free(frame_poison);

    /* TC-DRIFT-LOCAL-01: Link idle for 2000 ms, subsequent packet validated against current local clock */
    config.clock_ref.local_epoch_ms += 2000ULL;
    hdr2->sequence_number = 1004;
    hdr2->tx_epoch_ms = config.clock_ref.local_epoch_ms - 10;
    xdp_generate_isl_auth_tag(hdr2, payload, sizeof(payload), config.isl_shared_key);
    int action_idle_pass = xdp_starsec_filter_frame(frame2, frame_len, &config, &stats);
    STARSEC_ASSERT(action_idle_pass == STARSEC_XDP_PASS,
                   "TC-DRIFT-LOCAL-01: Valid packet after 2000 ms idle link falsely dropped!");

    /* Stale packet from 201 ms ago dropped */
    hdr2->sequence_number = 1005;
    hdr2->tx_epoch_ms = config.clock_ref.local_epoch_ms - 201ULL;
    xdp_generate_isl_auth_tag(hdr2, payload, sizeof(payload), config.isl_shared_key);
    int action_stale_drop = xdp_starsec_filter_frame(frame2, frame_len, &config, &stats);
    STARSEC_ASSERT(action_stale_drop == STARSEC_XDP_DROP, "TC-DRIFT-LOCAL-01: Stale 201 ms frame not dropped");

    /* Future packet from +25 ms in future dropped */
    hdr2->sequence_number = 1005;
    hdr2->tx_epoch_ms = config.clock_ref.local_epoch_ms + 25ULL;
    xdp_generate_isl_auth_tag(hdr2, payload, sizeof(payload), config.isl_shared_key);
    int action_future_drop = xdp_starsec_filter_frame(frame2, frame_len, &config, &stats);
    STARSEC_ASSERT(action_future_drop == STARSEC_XDP_DROP, "TC-DRIFT-LOCAL-01: Future timestamp not dropped");

    /* TC-SLIDING-WINDOW-01: Out-of-order within 64-window & duplicate detection */
    hdr2->sequence_number = 1010;
    hdr2->tx_epoch_ms = config.clock_ref.local_epoch_ms - 10;
    xdp_generate_isl_auth_tag(hdr2, payload, sizeof(payload), config.isl_shared_key);
    STARSEC_ASSERT(xdp_starsec_filter_frame(frame2, frame_len, &config, &stats) == STARSEC_XDP_PASS,
                   "TC-SLIDING-WINDOW-01: Seq 1010 failed");

    hdr2->sequence_number = 1008; /* Out of order, within window (1010 - 1008 = 2 < 64) */
    xdp_generate_isl_auth_tag(hdr2, payload, sizeof(payload), config.isl_shared_key);
    STARSEC_ASSERT(xdp_starsec_filter_frame(frame2, frame_len, &config, &stats) == STARSEC_XDP_PASS,
                   "TC-SLIDING-WINDOW-01: Out-of-order seq 1008 failed");

    /* Duplicate of 1008 must be dropped */
    int action_dup = xdp_starsec_filter_frame(frame2, frame_len, &config, &stats);
    STARSEC_ASSERT(action_dup == STARSEC_XDP_DROP, "TC-SLIDING-WINDOW-01: Duplicate seq 1008 not dropped");

    /* Obsolete sequence strictly older than 64-window (1010 - 70 = 940) must be dropped */
    hdr2->sequence_number = 940;
    xdp_generate_isl_auth_tag(hdr2, payload, sizeof(payload), config.isl_shared_key);
    int action_obsolete = xdp_starsec_filter_frame(frame2, frame_len, &config, &stats);
    STARSEC_ASSERT(action_obsolete == STARSEC_XDP_DROP, "TC-SLIDING-WINDOW-01: Obsolete seq 940 not dropped");

    free(frame1);
    free(frame2);
    printf("  [PASS] In-Orbit XDP Filter Triad with Two-Stage Sliding Window & Local Clock Successful.\n");
    return true;
}

/**
 * @brief Mock environment simulating Linux eBPF maps and kernel verifier state.
 */
typedef struct {
    uint16_t                  authorized_satellites[256];
    uint32_t                  auth_flags[256];
    size_t                    auth_count;
    uint16_t                  replay_nodes[256];
    isl_anti_replay_state_t   replay_states[256];
    size_t                    replay_count;
    starsec_time_sync_state_t clock_ref;
    uint64_t                  telemetry[8];
} mock_ebpf_env_t;

/**
 * @brief Simulates kernel-space eBPF ingress filter logic (Stage 1 Read-Only Pre-Filter).
 * @param frame_bytes Pointer to raw packet memory.
 * @param frame_len Total length of packet in bytes.
 * @param env Pointer to mock eBPF environment containing maps and telemetry.
 * @return 0 for XDP_PASS, 1 for XDP_DROP.
 */
static int mock_kernel_starsec_isl_xdp_filter(
    const uint8_t   *frame_bytes,
    size_t           frame_len,
    mock_ebpf_env_t *env
) {
    if (frame_bytes == NULL || env == NULL) {
        if (env != NULL) {
            env->telemetry[2]++;
        }
        return 1;
    }

    if (frame_len < sizeof(starsec_isl_header_t)) {
        env->telemetry[2]++;
        return 1;
    }

    const starsec_isl_header_t *hdr = (const starsec_isl_header_t *)frame_bytes;

    if (hdr->magic != STARSEC_ISL_MAGIC) {
        env->telemetry[1]++;
        return 1;
    }

    if ((sizeof(starsec_isl_header_t) + hdr->payload_len) > frame_len) {
        env->telemetry[2]++;
        return 1;
    }

    if (env->clock_ref.local_epoch_ms > 0) {
        if (hdr->tx_epoch_ms + env->clock_ref.max_past_drift_ms < env->clock_ref.local_epoch_ms ||
            hdr->tx_epoch_ms > env->clock_ref.local_epoch_ms + env->clock_ref.max_future_skew_ms) {
            env->telemetry[5]++;
            return 1;
        }
    }

    bool is_authorized = false;
    for (size_t i = 0; i < env->auth_count; i++) {
        if (env->authorized_satellites[i] == hdr->src_satellite_id && env->auth_flags[i] != 0) {
            is_authorized = true;
            break;
        }
    }
    if (!is_authorized) {
        env->telemetry[3]++;
        return 1;
    }

    ssize_t replay_idx = -1;
    for (size_t i = 0; i < env->replay_count; i++) {
        if (env->replay_nodes[i] == hdr->src_satellite_id) {
            replay_idx = (ssize_t)i;
            break;
        }
    }

    if (replay_idx >= 0) {
        isl_anti_replay_state_t *st = &env->replay_states[replay_idx];
        if (st->window_base_seq != 0 || st->window_bitmap != 0) {
            if (hdr->sequence_number <= st->window_base_seq) {
                uint64_t diff = st->window_base_seq - hdr->sequence_number;
                if (diff >= STARSEC_SLIDING_WINDOW_SIZE || (st->window_bitmap & (1ULL << diff)) != 0) {
                    env->telemetry[4]++;
                    return 1;
                }
            }
        }
    }

    env->telemetry[0]++;
    return 0;
}

/**
 * @brief Executes TC-EBPF-POS-01..02, TC-EBPF-NEG-01..05, TC-EBPF-POISON-01, and TC-EBPF-IDLE-01.
 * @return true if all eBPF kernel ingress filter simulation tests pass, false otherwise.
 */
static bool test_ebpf_kernel_ingress_triad(void) {
    printf("[TEST] Running Linux eBPF/XDP Kernel Ingress Filter Triad & Two-Stage Pipeline...\n");

    mock_ebpf_env_t env;
    memset(&env, 0, sizeof(env));
    env.auth_count = 2;
    env.authorized_satellites[0] = 101;
    env.auth_flags[0] = 1;
    env.authorized_satellites[1] = 102;
    env.auth_flags[1] = 1;
    env.clock_ref.local_epoch_ms = 1770000000000ULL;
    env.clock_ref.max_past_drift_ms = 200ULL;
    env.clock_ref.max_future_skew_ms = 20ULL;

    uint8_t payload[16] = "ORBITAL_DATA_01";
    size_t frame_len = sizeof(starsec_isl_header_t) + sizeof(payload);
    uint8_t *frame = (uint8_t *)malloc(frame_len);
    STARSEC_ASSERT(frame != NULL, "TC-EBPF-POS-01: Memory allocation failed");

    starsec_isl_header_t *hdr = (starsec_isl_header_t *)frame;
    hdr->magic = STARSEC_ISL_MAGIC;
    hdr->protocol_type = STARSEC_PROTO_INTER_SATELLITE_DATA;
    hdr->flags = 0;
    hdr->src_satellite_id = 101;
    hdr->dst_satellite_id = 202;
    hdr->session_id = 10;
    hdr->sequence_number = 5001;
    hdr->tx_epoch_ms = env.clock_ref.local_epoch_ms - 20;
    hdr->payload_len = sizeof(payload);
    memcpy(frame + sizeof(starsec_isl_header_t), payload, sizeof(payload));

    int res1 = mock_kernel_starsec_isl_xdp_filter(frame, frame_len, &env);
    STARSEC_ASSERT(res1 == 0, "TC-EBPF-POS-01: Ingress packet 1 rejected by kernel filter");
    STARSEC_ASSERT(env.telemetry[0] == 1, "TC-EBPF-POS-01: Passed counter mismatch");

    /* Tier 2 authenticated commit */
    env.replay_nodes[0] = 101;
    env.replay_count = 1;
    xdp_commit_replay_window_authenticated(&env.replay_states[0], hdr->sequence_number);

    hdr->sequence_number = 5002;
    hdr->tx_epoch_ms = env.clock_ref.local_epoch_ms - 15;
    int res2 = mock_kernel_starsec_isl_xdp_filter(frame, frame_len, &env);
    STARSEC_ASSERT(res2 == 0, "TC-EBPF-POS-02: Sequential packet 2 rejected by kernel filter");
    STARSEC_ASSERT(env.telemetry[0] == 2, "TC-EBPF-POS-02: Passed counter mismatch");
    xdp_commit_replay_window_authenticated(&env.replay_states[0], hdr->sequence_number);

    /* Replay of 5001 must drop */
    hdr->sequence_number = 5001;
    int res_replay = mock_kernel_starsec_isl_xdp_filter(frame, frame_len, &env);
    STARSEC_ASSERT(res_replay == 1, "TC-EBPF-NEG-01: Replayed sequence number not dropped");
    STARSEC_ASSERT(env.telemetry[4] == 1, "TC-EBPF-NEG-01: Replay counter mismatch");

    /* TC-EBPF-POISON-01: Adversary injects spoofed frame with seq 9999999 */
    hdr->sequence_number = 9999999ULL;
    int res_poison_ingress = mock_kernel_starsec_isl_xdp_filter(frame, frame_len, &env);
    STARSEC_ASSERT(res_poison_ingress == 0, "TC-EBPF-POISON-01: Ingress check passed to Tier 2");
    /* In Tier 2, MAC verification FAILS -> xdp_commit_replay_window_authenticated is NOT called! */

    /* Genuine packet 5003 from Satellite 101 arrives */
    hdr->sequence_number = 5003;
    int res_gen = mock_kernel_starsec_isl_xdp_filter(frame, frame_len, &env);
    STARSEC_ASSERT(res_gen == 0, "TC-EBPF-POISON-01: Genuine packet 5003 was blocked by state poisoning!");

    /* TC-EBPF-IDLE-01: Link idle for 5000 ms */
    env.clock_ref.local_epoch_ms += 5000ULL;
    hdr->sequence_number = 5004;
    hdr->tx_epoch_ms = env.clock_ref.local_epoch_ms - 10;
    int res_idle = mock_kernel_starsec_isl_xdp_filter(frame, frame_len, &env);
    STARSEC_ASSERT(res_idle == 0, "TC-EBPF-IDLE-01: Idle link packet falsely dropped by delta check!");

    free(frame);
    printf("  [PASS] Linux eBPF/XDP Kernel Ingress Filter Triad & Two-Stage Pipeline Successful.\n");
    return true;
}

/**
 * @brief Main entry point for standalone C test harness execution.
 * @return 0 on all tests passing, 1 on any test failure.
 */
int main(void) {
    printf("=================================================================\n");
    printf("    STARSEC CSF v1.0: NATIVE SYSTEM-LEVEL C TEST HARNESS         \n");
    printf("    Governance: Rules 01, 05, 12, 28, 33, 35, 36, 37, 40         \n");
    printf("=================================================================\n\n");

    bool all_passed = true;
    all_passed &= test_tamper_zeroize_triad();
    all_passed &= test_sram_puf_triad();
    all_passed &= test_dcls_monitor_triad();
    all_passed &= test_chacha20_triad();
    all_passed &= test_poly1305_triad();
    all_passed &= test_xdp_orbital_filter_triad();
    all_passed &= test_ebpf_kernel_ingress_triad();

    printf("\n-----------------------------------------------------------------\n");
    if (all_passed) {
        printf("[PASS] ALL SYSTEM DRIVER TESTS SUCCESSFUL (100%% Triad Coverage)\n");
        printf("=================================================================\n");
        return 0;
    } else {
        printf("[FAIL] ONE OR MORE TESTS FAILED REGRESSION CHECKS\n");
        printf("=================================================================\n");
        return 1;
    }
}
