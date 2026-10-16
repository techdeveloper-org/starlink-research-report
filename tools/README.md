# 🛠️ StarSec & Constellation Simulation Tooling Suite

> **Interactive Reproducible Tooling for Astrodynamics, Phased Arrays, Post-Quantum Cryptography, Hardware Glitching, and Space Cyber Risk Scoring**

This directory documents the executable Python tools accompanying the StarSec Constellation Security Framework (CSF v1.0) and the research monograph. All tools are located in [`simulations/`](../simulations/).

---

## 🚀 Unified Zero-Cloud-Cost Local Test Runner

To verify the entire framework—including native C hardware drivers and all 23 Python simulation tests—without consuming any cloud CI runner minutes or third-party services:

```bash
python tools/run_all_tests.py
```

### Verification Pipeline:
1. **Native C Hardware Drivers (Phase 1):** Compiles `system/src/*.c` using local MinGW GCC (`-O2 -Wall -Wextra`) and executes `tools/test_system_drivers.exe` validating sub-10 ns memory zeroization, SRAM-PUF BCH key recovery, DCLS SEU lockstep state machine, and in-orbit eBPF/XDP optical packet filter.
2. **Python Simulation Test Suite (Phase 2):** Executes all 23 triad unit tests (`simulations/test_simulations.py`) covering SGP4 orbital mechanics, Capon MVDR beamforming, NIST FIPS 203 ML-KEM arithmetic, and Shamir $(t, n)$ secret sharing.
3. **Tooling CLI Parity (Phase 3):** Validates interface contracts for `threat_score_calculator.py` and `mpc_quorum_telecommand.py`.

**Total Execution Time:** `<0.4 seconds` | **Cloud Cost:** `$0.00` | **CI Consumption:** `0 minutes`

---

## 🛠️ Standalone C Driver Test Harness Compilation

You can also compile and run the native C test harness independently with GCC:
```bash
gcc -O2 -Wall -Wextra -I./system/include ./system/src/tamper_zeroize_driver.c ./system/src/sram_puf_fuzzy_extractor.c ./system/src/dcls_fault_monitor.c ./system/src/xdp_orbital_filter.c ./system/src/main_system_test.c -o ./tools/test_system_drivers.exe ; ./tools/test_system_drivers.exe
```

---

## 📦 Python Simulation Tool Catalog

| Tool Script | Domain | Standards & Mathematics | Execution Command |
| :--- | :--- | :--- | :--- |
| **[`threat_score_calculator.py`](../simulations/threat_score_calculator.py)** | Space Risk Scoring | CVSS v4.0 Space Profile, Aerospace SPARTA, Orbital Conjunction ($P_c$) multipliers. | `python simulations/threat_score_calculator.py --demo` |
| **[`mpc_quorum_telecommand.py`](../simulations/mpc_quorum_telecommand.py)** | TT&C Quorum Safety | Shamir $(t, n)$ polynomial secret sharing over $\mathbb{F}_{2^{256}-189}$, FROST multisignatures. | `python simulations/mpc_quorum_telecommand.py --demo` |
| **[`phased_array_beamforming.py`](../simulations/phased_array_beamforming.py)** | RF / PHY Resiliency | 2D Array Factor $AF(\theta, \phi)$, Capon MVDR spatial null-steering ($>80\text{ dB}$ jammer cancellation). | `python simulations/phased_array_beamforming.py` |
| **[`pqc_kem_benchmark.py`](../simulations/pqc_kem_benchmark.py)** | Optical ISL Mesh | NIST FIPS 203 (ML-KEM-1024) polynomial ring arithmetic over $\mathbb{Z}_{3329}[X]/(X^{256}+1)$. | `python simulations/pqc_kem_benchmark.py` |
| **[`stride_fault_injection_sim.py`](../simulations/stride_fault_injection_sim.py)** | Hardware / UT Hardening | Sub-10 ns BootROM voltage glitching, Dual-Core Lockstep (DCLS), <250 ps crowbar clamps. | `python simulations/stride_fault_injection_sim.py` |
| **[`orbit_sgp4_propagator.py`](../simulations/orbit_sgp4_propagator.py)** | Astrodynamics & Safety | SGP4 Keplerian motion, $J_2$ precession, Foster & Estes (1992) B-plane collision probability $P_c$. | `python simulations/orbit_sgp4_propagator.py` |
| **[`rf_link_budget_calculator.py`](../simulations/rf_link_budget_calculator.py)** | RF Telecommunications | Decibel link budget, Free-Space Path Loss ($FSPL$), ITU-R P.618 rain attenuation, DVB-S2X MODCOD margins. | `python simulations/rf_link_budget_calculator.py` |

---

## 🚀 Quickstart & Automated Unit Test Suite

### 1. Requirements
Ensure you have Python 3.10+ installed with NumPy and SciPy:
```bash
pip install -r simulations/requirements.txt
```

### 2. Run the Full Unit Test Harness
All simulation tools are covered by automated unit tests enforcing the Triad Testing pattern (Positive, Negative, Edge-Case):
```bash
python -m unittest simulations.test_simulations
```

---

## 📊 Detailed Tool Usage & Sample Outputs

### 1. Threat Score Calculator (`threat_score_calculator.py`)
Calculates the composite Space CVSS v4.0 score and SPARTA threat severity:
```bash
# Evaluate a specific vector
python simulations/threat_score_calculator.py --vector TTC-ROGUE-001

# Export in structured JSON format
python simulations/threat_score_calculator.py --vector RF-JAM-001 --json

# List all 32 cataloged constellation threat vectors
python simulations/threat_score_calculator.py --list
```

### 2. Multi-Party Telecommand Quorum Simulator (`mpc_quorum_telecommand.py`)
Demonstrates how Shamir $(t, n)$ secret sharing protects flight-critical telecommands from unilateral rogue commands while allowing autonomous collision avoidance:
```bash
python simulations/mpc_quorum_telecommand.py --demo
```

### 3. Phased Array Capon MVDR Beamformer (`phased_array_beamforming.py`)
Simulates 1280-element patch array and generates deep spatial nulls in hostile jammer directions:
```bash
python simulations/phased_array_beamforming.py
```
Expected output:
```text
Array Geometry             : 32 x 40 (1280 patch elements)
Carrier Frequency          : 12.00 GHz (Ku-band Downlink)
Boresight Peak Gain        : 31.07 dBi
Hostile Jammer 1 (AoA)     : (theta=15.0 deg, phi=85.0 deg)
Spatial Null Attenuation   : 81.51 dB (Cancellation Factor: >100,000,000x)
ECCM Evaluation            : PASS (>45 dB Jammer Suppression)
```
