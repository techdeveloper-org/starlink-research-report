/**
 * @file starsec_bench.h
 * @brief High-Resolution Cycle Counter and Latency Boundary Benchmark Suite.
 *
 * Implements cross-platform cycle counting (RDTSC on x86/x64, QPC on Windows,
 * CLOCK_MONOTONIC_RAW on POSIX) and formal dual-tier latency verification
 * for StarSec hardware crowbar circuits vs software driver execution.
 *
 * Standard: StarSec CSF v1.0, MISRA-C:2023 Principles
 * Compliance: Level 2.2 Docstrings-Only
 */

#ifndef STARSEC_BENCH_H
#define STARSEC_BENCH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(_MSC_VER)
    #include <intrin.h>
    #include <windows.h>
#elif defined(__GNUC__) || defined(__clang__)
    #if defined(__i386__) || defined(__x86_64__)
        #include <x86intrin.h>
    #endif
    #if defined(_WIN32) || defined(__CYGWIN__)
        #include <windows.h>
    #else
        #include <time.h>
    #endif
#endif

#include "tamper_zeroize.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Analog silicon crowbar specification ceiling in nanoseconds. */
#define STARSEC_HW_CROWBAR_LIMIT_NS  10ULL

/** @brief Software driver interrupt service routine maximum latency budget in nanoseconds. */
#define STARSEC_SW_DRIVER_BUDGET_NS  250ULL

/**
 * @brief Statistical distribution metrics for benchmark executions.
 */
typedef struct {
    uint64_t total_cycles;       /**< Sum of all recorded cycle counts */
    uint64_t total_nanoseconds;  /**< Sum of elapsed time in nanoseconds */
    uint64_t iterations;         /**< Iteration count sampled */
    uint64_t avg_cycles;         /**< Mean cycle count per invocation */
    uint64_t avg_ns;             /**< Mean nanosecond duration per invocation */
    uint64_t min_ns;             /**< Minimum observed nanosecond duration */
    uint64_t max_ns;             /**< Maximum observed nanosecond duration */
} starsec_bench_metric_t;

/**
 * @brief Formal verification artifact establishing the hardware vs driver boundary.
 */
typedef struct {
    uint64_t hw_crowbar_spec_limit_ns;   /**< 10 ns analog silicon crowbar specification limit */
    uint64_t sw_driver_budget_ns;        /**< 250 ns maximum allowed driver software budget */
    uint64_t measured_driver_avg_ns;     /**< Measured software driver execution average */
    bool     driver_within_budget;       /**< true if measured_driver_avg_ns <= budget */
} starsec_latency_boundary_t;

/**
 * @brief Reads the hardware CPU timestamp counter.
 * @return 64-bit cycle counter value.
 */
static inline uint64_t starsec_read_cycles(void) {
#if defined(_MSC_VER)
    return __rdtsc();
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
#else
    return 0ULL;
#endif
}

/**
 * @brief Reads high-resolution monotonic epoch timestamp in nanoseconds.
 * @return Nanoseconds elapsed from system epoch.
 */
static inline uint64_t starsec_get_monotonic_ns(void) {
#if defined(_WIN32) || defined(_MSC_VER) || defined(__CYGWIN__)
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / freq.QuadPart);
#elif defined(CLOCK_MONOTONIC_RAW)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
#endif
}

/**
 * @brief Benchmarks the tamper zeroize interrupt service routine over N iterations.
 * @param regs Pointer to test register structure.
 * @param iterations Number of benchmark iterations to execute.
 * @return starsec_bench_metric_t containing timing statistics.
 */
static inline starsec_bench_metric_t starsec_profile_zeroize_isr(
    tdc_hw_registers_t *regs,
    size_t              iterations
) {
    starsec_bench_metric_t result;
    result.total_cycles = 0;
    result.total_nanoseconds = 0;
    result.iterations = (uint64_t)iterations;
    result.min_ns = 0xFFFFFFFFFFFFFFFFULL;
    result.max_ns = 0;

    for (size_t i = 0; i < 10; i++) {
        tdc_trigger_zeroize_isr(regs, TAMPER_SRC_VOLTAGE_GLITCH);
    }

    for (size_t i = 0; i < iterations; i++) {
        uint64_t start_cycles = starsec_read_cycles();
        uint64_t start_ns = starsec_get_monotonic_ns();

        tdc_trigger_zeroize_isr(regs, TAMPER_SRC_VOLTAGE_GLITCH);

        uint64_t end_ns = starsec_get_monotonic_ns();
        uint64_t end_cycles = starsec_read_cycles();

        uint64_t delta_cycles = (end_cycles >= start_cycles) ? (end_cycles - start_cycles) : 0;
        uint64_t delta_ns = (end_ns >= start_ns) ? (end_ns - start_ns) : 0;

        result.total_cycles += delta_cycles;
        result.total_nanoseconds += delta_ns;

        if (delta_ns < result.min_ns) {
            result.min_ns = delta_ns;
        }
        if (delta_ns > result.max_ns) {
            result.max_ns = delta_ns;
        }
    }

    result.avg_cycles = (iterations > 0) ? (result.total_cycles / iterations) : 0;
    result.avg_ns = (iterations > 0) ? (result.total_nanoseconds / iterations) : 0;

    return result;
}

/**
 * @brief Asserts and validates the measured driver latency against architectural specifications.
 * @param measured_avg_ns Average measured software driver runtime in nanoseconds.
 * @return starsec_latency_boundary_t validation record.
 */
static inline starsec_latency_boundary_t starsec_evaluate_latency_boundary(uint64_t measured_avg_ns) {
    starsec_latency_boundary_t b;
    b.hw_crowbar_spec_limit_ns = STARSEC_HW_CROWBAR_LIMIT_NS;
    b.sw_driver_budget_ns = STARSEC_SW_DRIVER_BUDGET_NS;
    b.measured_driver_avg_ns = measured_avg_ns;
    b.driver_within_budget = (measured_avg_ns <= STARSEC_SW_DRIVER_BUDGET_NS);
    return b;
}

#ifdef __cplusplus
}
#endif

#endif /* STARSEC_BENCH_H */
