/**
 * @file main_system_test.c
 * @brief Standalone Native C System Verification Test Suite for StarSec CSF v1.0.
 *
 * Implements the Universal Triad Test Matrix (Positive, Negative, Edge-Case)
 * covering Hardware Tamper Zeroization, SRAM-PUF Key Recovery, Dual-Core Lockstep
 * SEU Mitigation, and In-Orbit eBPF/XDP Optical Link Filtering.
 *
 * Standard: StarSec Constellation Security Framework (CSF v1.0)
 * Governance: Rules 01, 05, 12, 28, 33, 35, 36, 37, 40
 * Target: Native MinGW GCC compilation (<0.2s execution, 0 cloud cost)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tamper_zeroize.h"
#include "sram_puf.h"
#include "dcls_monitor.h"
#include "xdp_orbital.h"

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
 * @brief Executes TC-ZERO-POS-01, TC-ZERO-NEG-01, and TC-ZERO-EDGE-01.
 * @return true if all tamper zeroize test cases pass, false otherwise.
 */
static bool test_tamper_zeroize_triad(void) {
    printf("[TEST] Running Tamper Zeroize Triad (TC-ZERO-POS-01, NEG-01, EDGE-01)...\n");

    tdc_hw_registers_t regs;
    uint8_t master_key[STARSEC_BBRAM_KEY_LEN_BYTES];
    for (size_t i = 0; i < STARSEC_BBRAM_KEY_LEN_BYTES; i++) {
        master_key[i] = (uint8_t)(0xAA ^ (uint8_t)i);
    }

    /* TC-ZERO-POS-01: Positive Test - Clean Init & Key Load */
    starsec_status_t init_status = tdc_init(&regs, TDC_CR_GLITCH_EN | TDC_CR_MESH_EN);
    STARSEC_ASSERT(init_status == STARSEC_SUCCESS, "TC-ZERO-POS-01: Init failed");

    starsec_status_t load_status = tdc_load_key(&regs, master_key, sizeof(master_key));
    STARSEC_ASSERT(load_status == STARSEC_SUCCESS, "TC-ZERO-POS-01: Key load failed");
    STARSEC_ASSERT(memcmp((const void *)regs.BBRAM_KEY, master_key, sizeof(master_key)) == 0,
                   "TC-ZERO-POS-01: Key memory mismatch");

    /* TC-ZERO-NEG-01: Negative Test - Fast Voltage Glitch Crowbar Trip & Sub-10 ns Zeroize */
    tdc_trigger_zeroize_isr(&regs, TAMPER_SRC_VOLTAGE_GLITCH);
    STARSEC_ASSERT(tdc_verify_zeroized(&regs), "TC-ZERO-NEG-01: Memory was not completely zeroized");
    STARSEC_ASSERT((regs.SR & TDC_SR_VOLTAGE_FAULT) != 0, "TC-ZERO-NEG-01: Voltage fault bit not set");
    STARSEC_ASSERT((regs.SR & TDC_SR_ZEROIZED) != 0, "TC-ZERO-NEG-01: Zeroized bit not set");

    tdc_telemetry_t telem = tdc_get_telemetry(&regs);
    STARSEC_ASSERT(telem.is_zeroized == true, "TC-ZERO-NEG-01: Telemetry is_zeroized flag mismatch");
    STARSEC_ASSERT(telem.last_fault_source == TAMPER_SRC_VOLTAGE_GLITCH, "TC-ZERO-NEG-01: Telemetry fault source mismatch");

    /* TC-ZERO-EDGE-01: Edge-Case Test - Unaligned Pointer & Zero Length Eradication */
    starsec_secure_zeroize_mem(NULL, 0);
    starsec_secure_zeroize_mem(NULL, 100);
    uint8_t edge_buffer[17] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
    starsec_secure_zeroize_mem(edge_buffer + 1, 15);
    STARSEC_ASSERT(edge_buffer[0] == 1, "TC-ZERO-EDGE-01: Buffer underflow corrupted boundary");
    STARSEC_ASSERT(edge_buffer[16] == 17, "TC-ZERO-EDGE-01: Buffer overflow corrupted boundary");
    for (size_t i = 1; i < 16; i++) {
        STARSEC_ASSERT(edge_buffer[i] == 0, "TC-ZERO-EDGE-01: Target sub-range not zeroized");
    }

    printf("  [PASS] Tamper Zeroize Triad Successful.\n");
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

    /* TC-PUF-POS-01: Positive Test - Clean Enrollment and Zero-Noise Key Reconstruction */
    starsec_status_t enroll_status = puf_enroll(&startup_sram, secret_seed, &helper, &enrolled_key);
    STARSEC_ASSERT(enroll_status == STARSEC_SUCCESS, "TC-PUF-POS-01: Enrollment failed");
    STARSEC_ASSERT(enrolled_key.is_reconstructed == true, "TC-PUF-POS-01: Enrollment key flag invalid");

    puf_key_output_t recon_pos;
    starsec_status_t recon_pos_status = puf_reconstruct(&startup_sram, &helper, &recon_pos);
    STARSEC_ASSERT(recon_pos_status == STARSEC_SUCCESS, "TC-PUF-POS-01: Zero-noise reconstruction failed");
    STARSEC_ASSERT(memcmp(enrolled_key.derived_key_256, recon_pos.derived_key_256, PUF_KEY_BYTES) == 0,
                   "TC-PUF-POS-01: Reconstructed key does not match enrolled key");
    STARSEC_ASSERT(recon_pos.total_corrected_bits == 0, "TC-PUF-POS-01: Expected 0 bit errors on clean read");

    /* TC-PUF-EDGE-01: Edge-Case Test - Radiation Noise with Multiple Correctable Bit Flips */
    puf_startup_state_t noisy_sram;
    memcpy(noisy_sram.raw_sram_bytes, startup_sram.raw_sram_bytes, PUF_RAW_BYTES);
    /* Invert 8 specific bits across different blocks (tolerable within repetition code) */
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

    /* TC-PUF-NEG-01: Negative Test - Popcount and Null Parameter Safety */
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

    /* TC-DCLS-POS-01: Positive Test - Synchronous Core Execution */
    dcls_comparator_isr(&regs);
    dcls_diagnostics_t diag_pos = dcls_read_diagnostics(&regs);
    STARSEC_ASSERT(diag_pos.current_state == DCLS_STATE_HEALTHY, "TC-DCLS-POS-01: Healthy state mismatch");
    STARSEC_ASSERT(diag_pos.total_seu_events == 0, "TC-DCLS-POS-01: Spurious SEU events detected");

    /* TC-DCLS-NEG-01: Negative Test - Transient Radiation SEU Injection & Resync */
    dcls_inject_synthetic_seu(&regs, 3);
    STARSEC_ASSERT(regs.CORE0_PC != regs.CORE1_PC, "TC-DCLS-NEG-01: SEU injection failed to cause divergence");
    dcls_comparator_isr(&regs);

    dcls_diagnostics_t diag_trans = dcls_read_diagnostics(&regs);
    STARSEC_ASSERT(diag_trans.total_seu_events == 1, "TC-DCLS-NEG-01: SEU event count mismatch");
    STARSEC_ASSERT(diag_trans.successful_recoveries == 1, "TC-DCLS-NEG-01: Recovery count mismatch");
    STARSEC_ASSERT(regs.CORE0_PC == regs.CORE1_PC, "TC-DCLS-NEG-01: Pipeline resynchronization failed");

    /* TC-DCLS-EDGE-01: Edge-Case Test - Persistent Multi-Cycle Divergence Exceeding Retry Limit */
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
 * @brief Executes TC-XDP-POS-01, TC-XDP-NEG-01, TC-XDP-NEG-02, and TC-XDP-EDGE-01.
 * @return true if all XDP orbital filter test cases pass, false otherwise.
 */
static bool test_xdp_orbital_filter_triad(void) {
    printf("[TEST] Running In-Orbit XDP Laser Filter Triad (TC-XDP-POS-01, NEG-01, NEG-02, EDGE-01)...\n");

    xdp_filter_config_t config;
    config.authorized_count = 2;
    config.authorized_nodes[0] = 101;
    config.authorized_nodes[1] = 102;
    config.local_clock_epoch_ms = 1770000000000ULL;
    memset(config.isl_shared_key, 0x42, sizeof(config.isl_shared_key));

    xdp_filter_stats_t stats;
    xdp_reset_filter_stats(&stats);

    /* Construct Valid Frame */
    uint8_t payload[16] = "TELECOMMAND_OK";
    size_t frame_len = sizeof(starsec_isl_header_t) + sizeof(payload);
    uint8_t *valid_frame = (uint8_t *)malloc(frame_len);
    STARSEC_ASSERT(valid_frame != NULL, "TC-XDP-POS-01: Memory allocation failed");

    starsec_isl_header_t *hdr = (starsec_isl_header_t *)valid_frame;
    hdr->magic = STARSEC_ISL_MAGIC;
    hdr->protocol_type = STARSEC_PROTO_TELECOMMAND;
    hdr->flags = 0;
    hdr->src_satellite_id = 101;
    hdr->dst_satellite_id = 202;
    hdr->sequence_number = 1001;
    hdr->tx_epoch_ms = config.local_clock_epoch_ms - 25; /* 25 ms propagation latency */
    hdr->payload_len = sizeof(payload);
    memcpy(valid_frame + sizeof(starsec_isl_header_t), payload, sizeof(payload));

    /* Compute Authentic Tag */
    for (size_t i = 0; i < 16; i++) {
        hdr->auth_tag[i] = (uint8_t)(config.isl_shared_key[i] ^ config.isl_shared_key[i + 16] ^
                                     ((hdr->src_satellite_id >> (i % 8)) & 0xFF) ^
                                     ((hdr->sequence_number >> (i % 8)) & 0xFF) ^
                                     ((hdr->magic >> ((i * 2) % 32)) & 0xFF));
    }

    /* TC-XDP-POS-01: Positive Test - Valid Optical Telecommand Frame */
    int action_pos = xdp_starsec_filter_frame(valid_frame, frame_len, &config, &stats);
    STARSEC_ASSERT(action_pos == STARSEC_XDP_PASS, "TC-XDP-POS-01: Valid frame was unexpectedly dropped");
    STARSEC_ASSERT(stats.passed_frames == 1, "TC-XDP-POS-01: passed_frames counter not incremented");

    /* TC-XDP-NEG-01: Negative Test - Corrupted Magic Constant */
    hdr->magic = 0xDEADBEEFUL;
    int action_bad_magic = xdp_starsec_filter_frame(valid_frame, frame_len, &config, &stats);
    STARSEC_ASSERT(action_bad_magic == STARSEC_XDP_DROP, "TC-XDP-NEG-01: Bad magic frame was not dropped");
    STARSEC_ASSERT(stats.dropped_bad_magic == 1, "TC-XDP-NEG-01: dropped_bad_magic counter not incremented");
    hdr->magic = STARSEC_ISL_MAGIC;

    /* TC-XDP-NEG-02: Negative Test - Forged Authentication Tag */
    hdr->auth_tag[0] ^= 0xFF;
    int action_bad_auth = xdp_starsec_filter_frame(valid_frame, frame_len, &config, &stats);
    STARSEC_ASSERT(action_bad_auth == STARSEC_XDP_DROP, "TC-XDP-NEG-02: Forged auth frame was not dropped");
    STARSEC_ASSERT(stats.dropped_invalid_auth == 1, "TC-XDP-NEG-02: dropped_invalid_auth counter not incremented");
    hdr->auth_tag[0] ^= 0xFF;

    /* TC-XDP-EDGE-01: Edge-Case Test - Epoch Replay Window Boundary */
    /* Exact boundary: 200 ms drift should PASS */
    hdr->tx_epoch_ms = config.local_clock_epoch_ms - 200ULL;
    int action_edge_pass = xdp_starsec_filter_frame(valid_frame, frame_len, &config, &stats);
    STARSEC_ASSERT(action_edge_pass == STARSEC_XDP_PASS, "TC-XDP-EDGE-01: Boundary 200 ms frame dropped");

    /* Exceeded boundary: 201 ms drift must DROP */
    hdr->tx_epoch_ms = config.local_clock_epoch_ms - 201ULL;
    int action_edge_drop = xdp_starsec_filter_frame(valid_frame, frame_len, &config, &stats);
    STARSEC_ASSERT(action_edge_drop == STARSEC_XDP_DROP, "TC-XDP-EDGE-01: 201 ms expired frame not dropped");
    STARSEC_ASSERT(stats.dropped_epoch_drift == 1, "TC-XDP-EDGE-01: dropped_epoch_drift counter not incremented");

    free(valid_frame);
    printf("  [PASS] In-Orbit XDP Laser Filter Triad Successful.\n");
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
    all_passed &= test_xdp_orbital_filter_triad();

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
