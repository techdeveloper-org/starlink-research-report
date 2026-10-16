/**
 * @file tamper_zeroize.h
 * @brief StarSec Hardware Tamper Detection Controller and Sub-10 ns Memory Zeroize Driver Header.
 *
 * Provides register maps, constant definitions, bitfields, telemetry structures,
 * and function prototypes for instant volatile memory eradication upon physical
 * or electrical attack detection.
 *
 * Standard: StarSec Constellation Security Framework (CSF v1.0)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#ifndef STARSEC_TAMPER_ZEROIZE_H
#define STARSEC_TAMPER_ZEROIZE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Base address of Tamper Detection Controller MMIO registers. */
#define STARSEC_TDC_BASE_ADDR        0x50000000UL

/** @brief Base address of Battery-Backed RAM secure key storage. */
#define STARSEC_BBRAM_BASE_ADDR      0x50001000UL

/** @brief Size of the primary 256-bit AES master storage key in bytes. */
#define STARSEC_BBRAM_KEY_LEN_BYTES  32UL

/** @brief Total size of the Battery-Backed RAM storage perimeter in bytes. */
#define STARSEC_BBRAM_TOTAL_BYTES    256UL

/** @brief TDC_CR Bit 0: Enable high-speed analog voltage crowbar comparator (<250 ps). */
#define TDC_CR_GLITCH_EN             (1UL << 0)

/** @brief TDC_CR Bit 1: Enable active enclosure conductive tamper mesh impedance monitor. */
#define TDC_CR_MESH_EN               (1UL << 1)

/** @brief TDC_CR Bit 2: Enable thermal anomaly detector (-45 C to +95 C limits). */
#define TDC_CR_TEMP_EN               (1UL << 2)

/** @brief TDC_CR Bit 3: Software trigger for instant hardware memory wipe strobe. */
#define TDC_CR_INSTANT_ZEROIZE       (1UL << 3)

/** @brief TDC_CR Bit 4: Trigger automatic zeroize upon fatal Dual-Core Lockstep divergence. */
#define TDC_CR_DCLS_TRIP_EN          (1UL << 4)

/** @brief TDC_CR Bit 31: Write-once lock bit preventing register modification until reset. */
#define TDC_CR_WRITE_LOCK            (1UL << 31)

/** @brief TDC_SR Bit 0: Voltage glitch detected below core operating threshold. */
#define TDC_SR_VOLTAGE_FAULT         (1UL << 0)

/** @brief TDC_SR Bit 1: Clock anomaly or pulse-insertion glitch detected. */
#define TDC_SR_CLOCK_GLITCH          (1UL << 1)

/** @brief TDC_SR Bit 2: Enclosure conductive tamper mesh discontinuity detected. */
#define TDC_SR_MESH_BROKEN           (1UL << 2)

/** @brief TDC_SR Bit 3: Memory zeroization sequence confirmed complete. */
#define TDC_SR_ZEROIZED              (1UL << 3)

/**
 * @brief Status and error return codes for StarSec hardware drivers.
 */
typedef enum {
    STARSEC_SUCCESS                  =  0,
    STARSEC_ERR_TAMPER_DETECTED      = -1,
    STARSEC_ERR_LOCKED               = -2,
    STARSEC_ERR_NULL_PTR             = -3,
    STARSEC_ERR_INVALID_LEN          = -4,
    STARSEC_ERR_ZEROIZE_FAILED       = -5
} starsec_status_t;

/**
 * @brief Originating source classifications for hardware tamper events.
 */
typedef enum {
    TAMPER_SRC_NONE                  = 0,
    TAMPER_SRC_VOLTAGE_GLITCH        = 1,
    TAMPER_SRC_CLOCK_ANOMALY         = 2,
    TAMPER_SRC_PHYSICAL_MESH         = 3,
    TAMPER_SRC_MANUAL_TRIGGER        = 4,
    TAMPER_SRC_LOCKSTEP_DIVERGENCE   = 5
} tamper_source_t;

/**
 * @brief Memory-Mapped I/O Register structure for Tamper Detection Controller and BBRAM.
 */
typedef struct {
    volatile uint32_t CR;            /**< 0x00: Control Register */
    volatile uint32_t SR;            /**< 0x04: Status Register */
    volatile uint32_t IER;           /**< 0x08: Interrupt Enable Register */
    volatile uint32_t ICR;           /**< 0x0C: Interrupt Clear Register */
    uint32_t          RESERVED[12];  /**< 0x10 - 0x3F: Bus alignment padding */
    volatile uint8_t  BBRAM_KEY[32]; /**< 0x40 - 0x5F: 256-bit Secure Key Area */
    volatile uint8_t  BBRAM_SEC[224];/**< 0x60 - 0x13F: Ephemeral Secrets Area */
} tdc_hw_registers_t;

/**
 * @brief Runtime diagnostic telemetry record for the tamper controller.
 */
typedef struct {
    uint32_t          zeroize_cycle_count;
    tamper_source_t   last_fault_source;
    uint32_t          total_fault_count;
    bool              is_zeroized;
    bool              is_hardware_locked;
} tdc_telemetry_t;

/**
 * @brief Initializes the Tamper Detection Controller with sensor enable flags.
 * @param regs Pointer to MMIO register bank.
 * @param enable_flags Bitwise OR of TDC_CR_* enable flags.
 * @return STARSEC_SUCCESS on valid initialization, STARSEC_ERR_NULL_PTR on NULL pointer.
 */
starsec_status_t tdc_init(tdc_hw_registers_t *regs, uint32_t enable_flags);

/**
 * @brief Loads the cryptographic master storage key into Battery-Backed RAM.
 * @param regs Pointer to MMIO register bank.
 * @param key_material Pointer to 32-byte key buffer.
 * @param key_len Length of key in bytes (must equal 32).
 * @return STARSEC_SUCCESS on success, STARSEC_ERR_LOCKED if locked, STARSEC_ERR_INVALID_LEN on size mismatch.
 */
starsec_status_t tdc_load_key(tdc_hw_registers_t *regs, const uint8_t *key_material, size_t key_len);

/**
 * @brief Fast Interrupt Handler executed when tamper sensors or crowbar circuits trip.
 * @param regs Pointer to MMIO register bank.
 * @param source Classification of the detected tamper event.
 */
void tdc_trigger_zeroize_isr(tdc_hw_registers_t *regs, tamper_source_t source);

/**
 * @brief Constant-time compiler-barrier secure memory wiper.
 * @param ptr Volatile pointer to memory target.
 * @param size_bytes Number of bytes to overwrite with zero.
 */
void starsec_secure_zeroize_mem(volatile void *ptr, size_t size_bytes);

/**
 * @brief Verifies that all BBRAM memory cells have been erased to zero.
 * @param regs Pointer to MMIO register bank.
 * @return true if all bytes equal 0x00 and status bit confirmed, false otherwise.
 */
bool tdc_verify_zeroized(const tdc_hw_registers_t *regs);

/**
 * @brief Reads the current diagnostic telemetry from the tamper controller.
 * @param regs Pointer to MMIO register bank.
 * @return Current tdc_telemetry_t structure.
 */
tdc_telemetry_t tdc_get_telemetry(const tdc_hw_registers_t *regs);

#ifdef __cplusplus
}
#endif

#endif /* STARSEC_TAMPER_ZEROIZE_H */
