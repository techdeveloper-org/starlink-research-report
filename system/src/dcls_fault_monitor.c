/**
 * @file dcls_fault_monitor.c
 * @brief Implementation of StarSec Dual-Core Lockstep (DCLS) Radiation Fault Monitor.
 *
 * Implements hardware comparator interrupt handling, transient radiation
 * single-event upset mitigation, pipeline flushing, and lockdown triggering.
 *
 * Standard: StarSec Constellation Security Framework (CSF v1.0)
 * Compliance: Level 2.2 Docstrings-Only, MISRA-C:2023 Principles
 */

#include "dcls_monitor.h"

/**
 * @brief Static driver telemetry state tracking radiation SEU events.
 */
static dcls_diagnostics_t g_dcls_diag = {
    .total_seu_events = 0,
    .successful_recoveries = 0,
    .fatal_divergences = 0,
    .current_state = DCLS_STATE_HEALTHY,
    .last_divergent_pc = 0x00000000UL
};

/**
 * @brief Initializes the Dual-Core Lockstep comparator registers.
 * @param regs Pointer to DCLS MMIO register bank.
 * @return STARSEC_SUCCESS on success, STARSEC_ERR_NULL_PTR on invalid pointer.
 */
starsec_status_t dcls_init_monitor(dcls_hw_registers_t *regs) {
    if (regs == NULL) {
        return STARSEC_ERR_NULL_PTR;
    }

    regs->CORE0_PC = 0x00020000UL;
    regs->CORE1_PC = 0x00020000UL;
    regs->CORE0_SP = 0x0005F000UL;
    regs->CORE1_SP = 0x0005F000UL;
    regs->FAULT_STATUS = 0x00000000UL;
    regs->FAULT_COUNTER = 0x00000000UL;
    regs->CONTROL = 0x00000000UL;

    g_dcls_diag.total_seu_events = 0;
    g_dcls_diag.successful_recoveries = 0;
    g_dcls_diag.fatal_divergences = 0;
    g_dcls_diag.current_state = DCLS_STATE_HEALTHY;
    g_dcls_diag.last_divergent_pc = 0;

    return STARSEC_SUCCESS;
}

/**
 * @brief Evaluates and transitions the fault containment state machine.
 * @param regs Pointer to DCLS MMIO register bank.
 * @param fault_mask Active fault flags from FAULT_STATUS.
 * @return The updated dcls_state_t state.
 */
dcls_state_t dcls_step_recovery_state_machine(dcls_hw_registers_t *regs, uint32_t fault_mask) {
    if (regs == NULL) {
        return DCLS_STATE_PERMANENT_LOCKDOWN;
    }

    if (fault_mask == 0) {
        regs->FAULT_COUNTER = 0;
        g_dcls_diag.current_state = DCLS_STATE_HEALTHY;
        return DCLS_STATE_HEALTHY;
    }

    regs->FAULT_COUNTER++;
    g_dcls_diag.total_seu_events++;
    g_dcls_diag.last_divergent_pc = regs->CORE0_PC;

    if (regs->FAULT_COUNTER <= DCLS_MAX_TRANSIENT_RETRIES) {
        g_dcls_diag.current_state = DCLS_STATE_RETRY_PIPELINE;
        regs->CORE1_PC = regs->CORE0_PC;
        regs->CORE1_SP = regs->CORE0_SP;
        regs->FAULT_STATUS = 0x00000000UL;
        g_dcls_diag.successful_recoveries++;
    } else {
        g_dcls_diag.current_state = DCLS_STATE_PERMANENT_LOCKDOWN;
        g_dcls_diag.fatal_divergences++;
        regs->CONTROL |= 0x80000000UL;
    }

    return g_dcls_diag.current_state;
}

/**
 * @brief Hardware comparator Fast Interrupt Handler invoked upon core mismatch.
 * @param regs Pointer to DCLS MMIO register bank.
 */
void dcls_comparator_isr(dcls_hw_registers_t *regs) {
    if (regs == NULL) {
        return;
    }

    uint32_t fault_mask = 0;
    if (regs->CORE0_PC != regs->CORE1_PC) {
        fault_mask |= DCLS_FAULT_PC_MISMATCH;
    }
    if (regs->CORE0_SP != regs->CORE1_SP) {
        fault_mask |= DCLS_FAULT_SP_MISMATCH;
    }

    regs->FAULT_STATUS |= fault_mask;
    dcls_step_recovery_state_machine(regs, fault_mask);
}

/**
 * @brief Reads the current DCLS diagnostics and telemetry record.
 * @param regs Pointer to DCLS MMIO register bank.
 * @return dcls_diagnostics_t telemetry snapshot.
 */
dcls_diagnostics_t dcls_read_diagnostics(const dcls_hw_registers_t *regs) {
    if (regs != NULL && regs->FAULT_COUNTER > DCLS_MAX_TRANSIENT_RETRIES) {
        g_dcls_diag.current_state = DCLS_STATE_PERMANENT_LOCKDOWN;
    }
    return g_dcls_diag;
}

/**
 * @brief Injects a synthetic single-event upset bit-flip for hardware-in-the-loop testing.
 * @param regs Pointer to DCLS MMIO register bank.
 * @param bit_index Bit position in program counter to invert.
 */
void dcls_inject_synthetic_seu(dcls_hw_registers_t *regs, uint32_t bit_index) {
    if (regs == NULL || bit_index >= 32) {
        return;
    }
    regs->CORE1_PC ^= (1UL << bit_index);
}
