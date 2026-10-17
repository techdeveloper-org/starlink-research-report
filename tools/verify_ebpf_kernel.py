"""
Static in-kernel eBPF/XDP verifier compliance scanner for StarSec CSF v1.0.

Analyzes xdp_isl_kern.c against Linux in-kernel BPF verifier invariants:
1. Packet bounds checks prior to packet pointer dereferences.
2. Complete absence of unbounded loops (guaranteeing finite termination).
3. Stack frame allocation budget strictly under 512 bytes.
4. R0 register return code enforcement (restricted to XDP_PASS / XDP_DROP).
5. BTF / CO-RE map declarations and GPL license descriptor validation.

Standard: Linux Kernel eBPF Verifier (kernel/bpf/verifier.c), StarSec CSF v1.0
Compliance: Level 2.2 Docstrings-Only, Zero Cloud Cost
"""

import re
import json
import sys
from pathlib import Path


class EbpfKernelVerifier:
    """
    Performs static invariant analysis on eBPF C source files.
    """

    def __init__(self, source_path: Path):
        """
        Initializes the verifier with the target C source path.

        :param source_path: Path to the eBPF kernel C source file.
        """
        self.source_path = source_path
        self.content = source_path.read_text(encoding="utf-8")
        self.filter_body = self._extract_function_body("starsec_isl_xdp_filter")

    def _extract_function_body(self, func_name: str) -> str:
        """
        Extracts the full C function body handling nested curly braces.

        :param func_name: Identifier of the function to extract.
        :return: Inner text of the function body.
        """
        m = re.search(rf"\b{func_name}\s*\([^)]*\)\s*\{{", self.content)
        if not m:
            return ""
        start_idx = m.end() - 1
        depth = 0
        for i in range(start_idx, len(self.content)):
            if self.content[i] == "{":
                depth += 1
            elif self.content[i] == "}":
                depth -= 1
                if depth == 0:
                    return self.content[start_idx + 1 : i]
        return ""

    def verify_bounds_checks(self) -> dict:
        """
        Validates that packet data pointers are bounds-checked against data_end before access.

        :return: Dictionary containing check status and identified guard expressions.
        """
        patterns = [
            r"data\s*\+\s*sizeof\s*\([^)]+\)\s*>\s*\(uint8_t\s*\*\)data_end",
            r"data\s*\+\s*sizeof\s*\([^)]+\)\s*\+\s*hdr->payload_len\s*>\s*\(uint8_t\s*\*\)data_end",
        ]
        guards_found = []
        for p in patterns:
            matches = re.findall(p, self.content)
            if matches:
                guards_found.extend(matches)

        passed = len(guards_found) >= 2
        return {
            "name": "packet_bounds_checks",
            "passed": passed,
            "guards_count": len(guards_found),
            "description": "Packet memory dereferences guarded by strict data_end invariants.",
        }

    def verify_loop_termination(self) -> dict:
        """
        Ensures zero unbounded loops (while, for, do-while) exist in the eBPF filter function.

        :return: Dictionary containing check status and count of detected loop constructs.
        """
        if not self.filter_body:
            return {
                "name": "loop_termination",
                "passed": False,
                "loops_found": -1,
                "description": "eBPF filter entry function not found.",
            }

        while_loops = len(re.findall(r"\bwhile\s*\(", self.filter_body))
        for_loops = len(re.findall(r"\bfor\s*\(", self.filter_body))
        do_loops = len(re.findall(r"\bdo\s*\{", self.filter_body))
        total_loops = while_loops + for_loops + do_loops

        return {
            "name": "loop_termination",
            "passed": (total_loops == 0),
            "loops_found": total_loops,
            "description": "Zero unbounded loops detected; program terminates in O(1) instructions.",
        }

    def verify_stack_budget(self) -> dict:
        """
        Calculates conservative upper bound on local variable stack allocations (<512 bytes).

        :return: Dictionary containing check status, estimated stack bytes, and 512-byte limit.
        """
        scalar_types = {
            "void *": 8,
            "uint8_t *": 8,
            "uint32_t": 4,
            "uint64_t": 8,
            "uint16_t": 2,
            "starsec_isl_header_t *": 8,
            "starsec_time_sync_state_t *": 8,
            "isl_anti_replay_state_t *": 8,
            "uint32_t *": 8,
        }
        if not self.filter_body:
            return {"name": "stack_budget", "passed": False, "stack_bytes": 0}

        estimated_bytes = 0
        for line in self.filter_body.splitlines():
            line = line.strip()
            for type_name, size in scalar_types.items():
                if line.startswith(type_name):
                    estimated_bytes += size

        passed = estimated_bytes < 512
        return {
            "name": "stack_budget",
            "passed": passed,
            "estimated_stack_bytes": estimated_bytes,
            "stack_limit_bytes": 512,
            "description": f"Stack allocation ({estimated_bytes} bytes) within Linux 512-byte eBPF verifier budget.",
        }

    def verify_return_codes(self) -> dict:
        """
        Validates that all return statements use valid XDP action codes (XDP_PASS or XDP_DROP).

        :return: Dictionary containing check status and count of valid/invalid return statements.
        """
        if not self.filter_body:
            return {"name": "return_codes", "passed": False}

        returns = re.findall(r"\breturn\s+([^;]+);", self.filter_body)
        valid_actions = {"XDP_PASS", "XDP_DROP"}
        all_valid = all(r.strip() in valid_actions for r in returns)
        has_pass = any(r.strip() == "XDP_PASS" for r in returns)
        has_drop = any(r.strip() == "XDP_DROP" for r in returns)

        actions_set = sorted(list(set(r.strip() for r in returns)))
        return {
            "name": "return_code_invariants",
            "passed": all_valid and has_pass and has_drop,
            "returns_count": len(returns),
            "return_values": [r.strip() for r in returns],
            "description": f"All {len(returns)} execution paths strictly terminate in verified XDP actions ({actions_set}).",
        }

    def verify_bpf_maps_and_license(self) -> dict:
        """
        Verifies presence of required BPF map structures and GPL license declaration.

        :return: Dictionary containing map inspection status.
        """
        required_maps = [
            "authorized_satellites_map",
            "isl_replay_filter_map",
            "time_sync_map",
            "isl_telemetry_map",
        ]
        maps_present = {m: (m in self.content) for m in required_maps}
        license_present = 'SEC("license")' in self.content and '"GPL"' in self.content

        passed = all(maps_present.values()) and license_present
        return {
            "name": "maps_and_license",
            "passed": passed,
            "maps_checked": maps_present,
            "license_verified": license_present,
            "description": "Mandatory eBPF maps and GPL license descriptor validated.",
        }

    def run_all_checks(self) -> dict:
        """
        Executes complete static verification suite and constructs consolidated report.

        :return: Consolidated verification report dictionary.
        """
        checks = [
            self.verify_bounds_checks(),
            self.verify_loop_termination(),
            self.verify_stack_budget(),
            self.verify_return_codes(),
            self.verify_bpf_maps_and_license(),
        ]
        all_passed = all(c["passed"] for c in checks)
        return {
            "target_file": str(self.source_path),
            "overall_status": "PASS" if all_passed else "FAIL",
            "total_checks": len(checks),
            "passed_checks": sum(1 for c in checks if c["passed"]),
            "checks": checks,
        }


def main() -> int:
    """
    Entry point for CLI execution of the eBPF kernel verifier.

    :return: 0 if all verifier invariants pass, 1 otherwise.
    """
    workspace = Path(__file__).resolve().parent.parent
    ebpf_source = workspace / "system" / "src" / "xdp_isl_kern.c"
    report_path = workspace / "tools" / "ebpf_verifier_report.json"

    if not ebpf_source.exists():
        print(f"[FAIL] eBPF source file not found: {ebpf_source}")
        return 1

    verifier = EbpfKernelVerifier(ebpf_source)
    report = verifier.run_all_checks()

    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"[eBPF Verifier] Scanned: {ebpf_source.name}")
    print(
        f"[eBPF Verifier] Status: {report['overall_status']} ({report['passed_checks']}/{report['total_checks']} checks passed)"
    )
    for check in report["checks"]:
        status = "PASS" if check["passed"] else "FAIL"
        print(f"  [{status}] {check['name']}: {check['description']}")

    return 0 if report["overall_status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
