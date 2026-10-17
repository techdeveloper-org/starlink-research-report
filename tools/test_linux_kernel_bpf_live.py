"""
Linux In-Kernel eBPF System Call Runtime Verification Harness.

Validates authentic Linux kernel bpf() system call (__NR_bpf = 321 on x86_64)
invocations directly against the host Linux kernel (BPF_MAP_CREATE, BPF_MAP_UPDATE_ELEM,
BPF_MAP_LOOKUP_ELEM) verifying true kernel/userspace boundary synchronization.

Standard: Linux Kernel bpf() syscall (kernel/bpf/syscall.c), StarSec CSF v1.0
Compliance: Level 2.2 Docstrings-Only, Zero Cloud Cost
"""

import os
import sys
import json
import struct
import ctypes
from pathlib import Path


class LinuxKernelBpfLiveVerifier:
    """
    Executes live in-kernel BPF map system call operations via ctypes.
    """

    SYS_BPF = 321  # x86_64 __NR_bpf
    BPF_MAP_CREATE = 0
    BPF_MAP_LOOKUP_ELEM = 1
    BPF_MAP_UPDATE_ELEM = 2
    BPF_MAP_TYPE_HASH = 1

    class MapCreateAttr(ctypes.Structure):
        """
        Kernel BPF attribute struct for BPF_MAP_CREATE.
        """
        _fields_ = [
            ("map_type", ctypes.c_uint32),
            ("key_size", ctypes.c_uint32),
            ("value_size", ctypes.c_uint32),
            ("max_entries", ctypes.c_uint32),
            ("map_flags", ctypes.c_uint32),
        ]

    class MapElemAttr(ctypes.Structure):
        """
        Kernel BPF attribute struct for BPF_MAP_UPDATE_ELEM and BPF_MAP_LOOKUP_ELEM.
        """
        _fields_ = [
            ("map_fd", ctypes.c_uint32),
            ("key", ctypes.c_uint64),
            ("value", ctypes.c_uint64),
            ("flags", ctypes.c_uint64),
        ]

    def __init__(self):
        """
        Initializes live verifier with libc reference.
        """
        self.libc = ctypes.CDLL(None, use_errno=True)

    def test_live_kernel_bpf_map(self) -> dict:
        """
        Creates real BPF hash map, writes replay state, and reads it back from kernel memory.

        :return: Verification telemetry report dictionary.
        """
        if sys.platform != "linux":
            return {
                "status": "SKIPPED_NON_LINUX",
                "message": f"Live bpf() syscall verification requires Linux host (current: {sys.platform})",
                "kernel_version": "N/A",
            }

        kernel_version = os.uname().release
        key_size = 2  # uint16 satellite_id
        val_size = 64  # isl_anti_replay_state_t
        max_entries = 256

        # Step 1: BPF_MAP_CREATE
        create_attr = self.MapCreateAttr(
            self.BPF_MAP_TYPE_HASH, key_size, val_size, max_entries, 0
        )
        fd = self.libc.syscall(
            self.SYS_BPF,
            self.BPF_MAP_CREATE,
            ctypes.byref(create_attr),
            ctypes.sizeof(create_attr),
        )
        if fd < 0:
            errno = ctypes.get_errno()
            return {
                "status": "FAIL_CREATE",
                "errno": errno,
                "kernel_version": kernel_version,
                "message": f"bpf(BPF_MAP_CREATE) failed with errno {errno}",
            }

        # Step 2: BPF_MAP_UPDATE_ELEM (Satellite 701, Sequence 5001, Session 1)
        test_key = ctypes.c_uint16(701)
        test_val = (ctypes.c_uint8 * 64)()
        struct.pack_into("<QQII", test_val, 0, 5001, 1, 1, 0)

        update_attr = self.MapElemAttr(
            fd, ctypes.addressof(test_key), ctypes.addressof(test_val), 0
        )
        res_update = self.libc.syscall(
            self.SYS_BPF,
            self.BPF_MAP_UPDATE_ELEM,
            ctypes.byref(update_attr),
            ctypes.sizeof(update_attr),
        )
        if res_update != 0:
            errno = ctypes.get_errno()
            os.close(fd)
            return {
                "status": "FAIL_UPDATE",
                "errno": errno,
                "kernel_version": kernel_version,
                "map_fd": fd,
            }

        # Step 3: BPF_MAP_LOOKUP_ELEM (Read back verified bytes from kernel memory)
        read_val = (ctypes.c_uint8 * 64)()
        lookup_attr = self.MapElemAttr(
            fd, ctypes.addressof(test_key), ctypes.addressof(read_val), 0
        )
        res_lookup = self.libc.syscall(
            self.SYS_BPF,
            self.BPF_MAP_LOOKUP_ELEM,
            ctypes.byref(lookup_attr),
            ctypes.sizeof(lookup_attr),
        )
        if res_lookup != 0:
            errno = ctypes.get_errno()
            os.close(fd)
            return {
                "status": "FAIL_LOOKUP",
                "errno": errno,
                "kernel_version": kernel_version,
                "map_fd": fd,
            }

        read_base_seq, read_bitmap, read_session = struct.unpack_from("<QQI", read_val, 0)
        os.close(fd)

        match = (read_base_seq == 5001) and (read_bitmap == 1) and (read_session == 1)
        return {
            "status": "PASS" if match else "FAIL_VERIFY",
            "kernel_version": kernel_version,
            "arch": os.uname().machine,
            "created_map_fd": fd,
            "tested_satellite_id": 701,
            "read_back_base_seq": read_base_seq,
            "read_back_bitmap": read_bitmap,
            "read_back_session_id": read_session,
            "kernel_fidelity_verified": match,
            "syscall_number": self.SYS_BPF,
        }


def main() -> int:
    """
    CLI execution entry point.
    """
    verifier = LinuxKernelBpfLiveVerifier()
    report = verifier.test_live_kernel_bpf_map()
    report_file = Path(__file__).resolve().parent / "ebpf_kernel_syscall_report.json"
    report_file.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print(json.dumps(report, indent=2))
    return 0 if report.get("status") in ("PASS", "SKIPPED_NON_LINUX") else 1


if __name__ == "__main__":
    sys.exit(main())
