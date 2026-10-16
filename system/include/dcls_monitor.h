/**
 * @file dcls_monitor.h
 * @brief StarSec Dual-Core Lockstep (DCLS) Radiation Single-Event Upset Monitor Header.
 *
 * Implements hardware comparator register monitoring, pipeline resynchronization,
 * transient retry counting, and orbital failover state machine.
 *
 * Standard: StarSec Constellation Security Framework (CSF v1.0)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#ifndef STARSEC_DCLS_MONITOR_H
#define STARSEC_DCLS_MONITOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "tamper_zeroize.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pipeline execution stagger between Primary and Checker cores in clock cycles. */
#define DCLS_CYCLE_DELAY_CYCLES      2UL

/** @brief Maximum allowable transient SEU retries before triggering permanent lockdown. */
#define DCLS_MAX_TRANSIENT_RETRIES   3UL

/** @brief Fault status bitmask indicating program counter mismatch. */
#define DCLS_FAULT_PC_MISMATCH       (1UL << 0)

/** @brief Fault status bitmask indicating stack pointer mismatch. */
#define DCLS_FAULT_SP_MISMATCH       (1UL << 1)

/** @brief Fault status bitmask indicating system bus address mismatch. */
#define DCLS_FAULT_BUS_MISMATCH      (1UL << 2)

/**
 * @brief Lifecycle states of the Dual-Core Lockstep Fault Containment Unit.
 */
typedef enum {
    DCLS_STATE_HEALTHY               = 0,
    DCLS_STATE_SEU_DETECTED          = 1,
    DCLS_STATE_RETRY_PIPELINE        = 2,
    DCLS_STATE_PERMANENT_LOCKDOWN    = 3,
    DCLS_STATE_ORBITAL_FAILOVER      = 4
} dcls_state_t;

/**
 * @brief Hardware MMIO register bank for the DCLS comparator macro.
 */
typedef struct {
    volatile uint32_t CORE0_PC;         /**< 0x00: Primary Core Program Counter */
    volatile uint32_t CORE1_PC;         /**< 0x04: Checker Core Program Counter */
    volatile uint32_t CORE0_SP;         /**< 0x08: Primary Core Stack Pointer */
    volatile uint32_t CORE1_SP;         /**< 0x0C: Checker Core Stack Pointer */
    volatile uint32_t FAULT_STATUS;     /**< 0x10: Bitmask of active mismatch faults */
    volatile uint32_t FAULT_COUNTER;    /**< 0x14: Cumulative mismatch counter */
    volatile uint32_t CONTROL;          /**< 0x18: Reset / Resync trigger register */
} dcls_hw_registers_t;

/**
 * @brief Diagnostic telemetry snapshot for radiation single-event upset monitoring.
 */
typedef struct {
    uint32_t     total_seu_events;
    uint32_t     successful_recoveries;
    uint32_t     fatal_divergences;
    dcls_state_t current_state;
    uint32_t     last_divergent_pc;
} dcls_diagnostics_t;

/**
 * @brief Initializes the Dual-Core Lockstep comparator registers.
 * @param regs Pointer to DCLS MMIO register bank.
 * @return STARSEC_SUCCESS on success, STARSEC_ERR_NULL_PTR on invalid pointer.
 */
starsec_status_t dcls_init_monitor(dcls_hw_registers_t *regs);

/**
 * @brief Hardware comparator Fast Interrupt Handler invoked upon core mismatch.
 * @param regs Pointer to DCLS MMIO register bank.
 */
void dcls_comparator_isr(dcls_hw_registers_t *regs);

/**
 * @brief Evaluates and transitions the fault containment state machine.
 * @param regs Pointer to DCLS MMIO register bank.
 * @param fault_mask Active fault flags from FAULT_STATUS.
 * @return The updated dcls_state_t state.
 */
dcls_state_t dcls_step_recovery_state_machine(dcls_hw_registers_t *regs, uint32_t fault_mask);

/**
 * @brief Reads the current DCLS diagnostics and telemetry record.
 * @param regs Pointer to DCLS MMIO register bank.
 * @return dcls_diagnostics_t telemetry snapshot.
 */
dcls_diagnostics_t dcls_read_diagnostics(const dcls_hw_registers_t *regs);

/**
 * @brief Injects a synthetic single-event upset bit-flip for hardware-in-the-loop testing.
 * @param regs Pointer to DCLS MMIO register bank.
 * @param bit_index Bit position in program counter to invert.
 */
void dcls_inject_synthetic_seu(dcls_hw_registers_t *regs, uint32_t bit_index);

#ifdef __cplusplus
}
#endif

#endif /* STARSEC_DCLS_MONITOR_H */
