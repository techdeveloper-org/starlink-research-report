/**
 * @file tamper_zeroize_driver.c
 * @brief Implementation of StarSec Hardware Tamper Detection and Memory Zeroization Driver.
 *
 * Implements register-level MMIO manipulation, hardware memory barriers,
 * constant-time volatile zeroization routines, and fast interrupt handling.
 *
 * Standard: StarSec Constellation Security Framework (CSF v1.0)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#include "tamper_zeroize.h"
#include <string.h>

/**
 * @brief Internal static state maintaining driver telemetry across tamper events.
 */
static tdc_telemetry_t g_tdc_telemetry = {
    .zeroize_cycle_count = 0,
    .last_fault_source = TAMPER_SRC_NONE,
    .total_fault_count = 0,
    .is_zeroized = false,
    .is_hardware_locked = false
};

/**
 * @brief Constant-time compiler-barrier secure memory wiper.
 * @param ptr Volatile pointer to target memory buffer.
 * @param size_bytes Number of bytes to eradicate.
 */
void starsec_secure_zeroize_mem(volatile void *ptr, size_t size_bytes) {
    if (ptr == NULL || size_bytes == 0) {
        return;
    }

    volatile uint8_t *byte_ptr = (volatile uint8_t *)ptr;
    while (size_bytes > 0) {
        *byte_ptr = 0x00;
        byte_ptr++;
        size_bytes--;
    }

    /* Architectural memory barrier: forces CPU store buffers to drain and flushes pipeline */
    #if defined(__arm__) || defined(__aarch64__)
    __asm__ volatile ("dsb sy\nisb" ::: "memory");
    #elif defined(__x86_64__) || defined(_M_X64)
    __asm__ volatile ("mfence" ::: "memory");
    #else
    __asm__ volatile ("" ::: "memory");
    #endif
}

/**
 * @brief Initializes the Tamper Detection Controller with sensor enable flags.
 * @param regs Pointer to MMIO register bank.
 * @param enable_flags Bitwise OR of TDC_CR_* enable flags.
 * @return STARSEC_SUCCESS on valid initialization, STARSEC_ERR_NULL_PTR on NULL pointer.
 */
starsec_status_t tdc_init(tdc_hw_registers_t *regs, uint32_t enable_flags) {
    if (regs == NULL) {
        return STARSEC_ERR_NULL_PTR;
    }

    regs->CR = enable_flags;
    regs->SR = 0x00000000UL;
    regs->IER = enable_flags;
    regs->ICR = 0xFFFFFFFFUL;

    g_tdc_telemetry.is_zeroized = false;
    g_tdc_telemetry.is_hardware_locked = ((enable_flags & TDC_CR_WRITE_LOCK) != 0);

    return STARSEC_SUCCESS;
}

/**
 * @brief Loads the cryptographic master storage key into Battery-Backed RAM.
 * @param regs Pointer to MMIO register bank.
 * @param key_material Pointer to 32-byte key buffer.
 * @param key_len Length of key in bytes (must equal 32).
 * @return STARSEC_SUCCESS on success, STARSEC_ERR_LOCKED if locked, STARSEC_ERR_INVALID_LEN on size mismatch.
 */
starsec_status_t tdc_load_key(tdc_hw_registers_t *regs, const uint8_t *key_material, size_t key_len) {
    if (regs == NULL || key_material == NULL) {
        return STARSEC_ERR_NULL_PTR;
    }

    if (key_len != STARSEC_BBRAM_KEY_LEN_BYTES) {
        return STARSEC_ERR_INVALID_LEN;
    }

    if (regs->CR & TDC_CR_WRITE_LOCK) {
        return STARSEC_ERR_LOCKED;
    }

    for (size_t i = 0; i < STARSEC_BBRAM_KEY_LEN_BYTES; i++) {
        regs->BBRAM_KEY[i] = key_material[i];
    }

    g_tdc_telemetry.is_zeroized = false;
    return STARSEC_SUCCESS;
}

/**
 * @brief Fast Interrupt Handler executed when tamper sensors or crowbar circuits trip.
 * @param regs Pointer to MMIO register bank.
 * @param source Classification of the detected tamper event.
 */
void tdc_trigger_zeroize_isr(tdc_hw_registers_t *regs, tamper_source_t source) {
    if (regs == NULL) {
        return;
    }

    regs->CR |= TDC_CR_INSTANT_ZEROIZE;

    switch (source) {
        case TAMPER_SRC_VOLTAGE_GLITCH:
            regs->SR |= TDC_SR_VOLTAGE_FAULT;
            break;
        case TAMPER_SRC_CLOCK_ANOMALY:
            regs->SR |= TDC_SR_CLOCK_GLITCH;
            break;
        case TAMPER_SRC_PHYSICAL_MESH:
            regs->SR |= TDC_SR_MESH_BROKEN;
            break;
        case TAMPER_SRC_LOCKSTEP_DIVERGENCE:
            regs->SR |= (TDC_SR_VOLTAGE_FAULT | TDC_SR_CLOCK_GLITCH);
            break;
        case TAMPER_SRC_MANUAL_TRIGGER:
        case TAMPER_SRC_NONE:
        default:
            break;
    }

    starsec_secure_zeroize_mem(regs->BBRAM_KEY, sizeof(regs->BBRAM_KEY));
    starsec_secure_zeroize_mem(regs->BBRAM_SEC, sizeof(regs->BBRAM_SEC));

    regs->SR |= TDC_SR_ZEROIZED;

    g_tdc_telemetry.zeroize_cycle_count++;
    g_tdc_telemetry.last_fault_source = source;
    g_tdc_telemetry.total_fault_count++;
    g_tdc_telemetry.is_zeroized = true;
}

/**
 * @brief Verifies that all BBRAM memory cells have been erased to zero.
 * @param regs Pointer to MMIO register bank.
 * @return true if all bytes equal 0x00 and status bit confirmed, false otherwise.
 */
bool tdc_verify_zeroized(const tdc_hw_registers_t *regs) {
    if (regs == NULL) {
        return false;
    }

    if ((regs->SR & TDC_SR_ZEROIZED) == 0) {
        return false;
    }

    uint8_t accumulator = 0;
    for (size_t i = 0; i < sizeof(regs->BBRAM_KEY); i++) {
        accumulator |= regs->BBRAM_KEY[i];
    }
    for (size_t i = 0; i < sizeof(regs->BBRAM_SEC); i++) {
        accumulator |= regs->BBRAM_SEC[i];
    }

    return (accumulator == 0);
}

/**
 * @brief Reads the current diagnostic telemetry from the tamper controller.
 * @param regs Pointer to MMIO register bank.
 * @return Current tdc_telemetry_t structure.
 */
tdc_telemetry_t tdc_get_telemetry(const tdc_hw_registers_t *regs) {
    if (regs != NULL) {
        g_tdc_telemetry.is_hardware_locked = ((regs->CR & TDC_CR_WRITE_LOCK) != 0);
    }
    return g_tdc_telemetry;
}
