#!/usr/bin/env bash
# ==============================================================================
# StarSec CSF v1.0: Linux Kernel eBPF Runtime Verifier Test Harness
#
# Standard: Linux Kernel bpf() syscall (kernel/bpf/verifier.c)
# Purpose: Compiles system/src/xdp_isl_kern.c with Clang BPF backend and loads
#          the resulting ELF object into the Linux kernel via bpftool to obtain
#          in-kernel verifier logs and proof of termination/bounds compliance.
#
# Prerequisites (Linux host or WSL2):
#   - Linux Kernel >= 5.10 with CONFIG_BPF=y, CONFIG_BPF_SYSCALL=y
#   - clang >= 11, llvm, libbpf-dev, linux-headers-$(uname -r)
#   - bpftool (from linux-tools-generic or bpf-tools)
#   - Root privileges (sudo) or CAP_BPF / CAP_NET_ADMIN
#
# Usage:
#   chmod +x tools/test_ebpf_linux_verifier.sh
#   sudo ./tools/test_ebpf_linux_verifier.sh
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

SOURCE_FILE="${REPO_ROOT}/system/src/xdp_isl_kern.c"
INCLUDE_DIR="${REPO_ROOT}/system/include"
OBJ_OUTPUT="${SCRIPT_DIR}/xdp_isl_kern.o"
LOG_OUTPUT="${SCRIPT_DIR}/ebpf_kernel_verifier_log.txt"
REPORT_OUTPUT="${SCRIPT_DIR}/ebpf_linux_verifier_report.json"
BPF_PIN_PATH="/sys/fs/bpf/starsec_isl_test"

echo "=================================================================="
echo "    STARSEC CSF v1.0: LINUX KERNEL eBPF RUNTIME VERIFIER TEST    "
echo "=================================================================="

# Check OS and kernel
if [[ "$(uname -s)" != "Linux" ]]; then
    echo "[ERROR] This script requires a Linux kernel (detected: $(uname -s))."
    echo "For static AST and invariant checks on non-Linux hosts, run python tools/verify_ebpf_kernel.py."
    exit 1
fi

echo "[INFO] Linux Kernel Version: $(uname -r)"

# Check toolchain prerequisites
for tool in clang bpftool; do
    if ! command -v "${tool}" &>/dev/null; then
        echo "[ERROR] Required tool '${tool}' is not installed."
        echo "Install via: sudo apt-get update && sudo apt-get install -y clang llvm libbpf-dev linux-tools-$(uname -r) bpftool"
        exit 1
    fi
done

# Step 1: Compile xdp_isl_kern.c to BPF bytecode ELF object
echo "[1/4] Compiling ${SOURCE_FILE} to BPF bytecode with Clang..."
clang -O2 -target bpf \
    -D__KERNEL__ \
    -D__BPF_TRACING__ \
    -Wno-unused-value \
    -Wno-pointer-sign \
    -Wno-compare-distinct-pointer-types \
    -I"${INCLUDE_DIR}" \
    -c "${SOURCE_FILE}" \
    -o "${OBJ_OUTPUT}"

echo "  -> Object compiled successfully: ${OBJ_OUTPUT}"

# Step 2: Verify ELF Sections
echo "[2/4] Inspecting BPF ELF sections..."
if command -v llvm-objdump &>/dev/null; then
    llvm-objdump -h "${OBJ_OUTPUT}"
fi

# Step 3: Test In-Kernel BPF Verifier Loading
echo "[3/4] Loading BPF program into Linux kernel verifier..."
# Clean up prior pinned object if present
if [[ -e "${BPF_PIN_PATH}" ]]; then
    rm -f "${BPF_PIN_PATH}"
fi

# Attempt bpftool load with canonical global debug option
VERIFIER_START_TS=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
LOAD_STATUS="PASS"
INSNS_PROCESSED=0

if bpftool --debug prog load "${OBJ_OUTPUT}" "${BPF_PIN_PATH}" type xdp > "${LOG_OUTPUT}" 2>&1; then
    LOAD_STATUS="PASS"
    echo "  -> [PASS] In-kernel verifier accepted BPF bytecode without violation."
    if grep -q "processed .* insns" "${LOG_OUTPUT}"; then
        INSNS_PROCESSED=$(grep -oE "processed [0-9]+ insns" "${LOG_OUTPUT}" | head -n1 | awk '{print $2}')
        echo "  -> In-Kernel Verifier Metrics: ${INSNS_PROCESSED} instructions verified, 0 unbounded loops."
    fi
elif bpftool -d prog load "${OBJ_OUTPUT}" "${BPF_PIN_PATH}" type xdp > "${LOG_OUTPUT}" 2>&1; then
    LOAD_STATUS="PASS"
    echo "  -> [PASS] In-kernel verifier accepted BPF bytecode (-d mode)."
    if grep -q "processed .* insns" "${LOG_OUTPUT}"; then
        INSNS_PROCESSED=$(grep -oE "processed [0-9]+ insns" "${LOG_OUTPUT}" | head -n1 | awk '{print $2}')
        echo "  -> In-Kernel Verifier Metrics: ${INSNS_PROCESSED} instructions verified, 0 unbounded loops."
    fi
elif bpftool prog load "${OBJ_OUTPUT}" "${BPF_PIN_PATH}" type xdp > "${LOG_OUTPUT}" 2>&1; then
    LOAD_STATUS="PASS"
    echo "  -> [PASS] In-kernel verifier accepted BPF bytecode (standard mode)."
else
    LOAD_STATUS="FAIL"
    echo "  -> [FAIL] In-kernel verifier rejected program. Log excerpt:"
    tail -n 25 "${LOG_OUTPUT}"
fi

# Clean up pinned test object
if [[ -e "${BPF_PIN_PATH}" ]]; then
    rm -f "${BPF_PIN_PATH}"
fi

# Step 4: Emit JSON Report
echo "[4/4] Writing runtime verification report to ${REPORT_OUTPUT}..."
cat <<EOF > "${REPORT_OUTPUT}"
{
  "test_name": "Linux In-Kernel eBPF Verifier Runtime Load",
  "kernel_version": "$(uname -r)",
  "arch": "$(uname -m)",
  "compiler": "$(clang --version | head -n1)",
  "timestamp": "${VERIFIER_START_TS}",
  "source_file": "${SOURCE_FILE}",
  "object_file": "${OBJ_OUTPUT}",
  "verifier_status": "${LOAD_STATUS}",
  "instructions_verified": ${INSNS_PROCESSED:-0},
  "log_file": "${LOG_OUTPUT}"
}
EOF

echo "=================================================================="
if [[ "${LOAD_STATUS}" == "PASS" ]]; then
    echo "[PASS] LINUX KERNEL eBPF RUNTIME VERIFICATION SUCCESSFUL"
    exit 0
else
    echo "[FAIL] LINUX KERNEL eBPF RUNTIME VERIFICATION FAILED"
    exit 1
fi
