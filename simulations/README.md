# 🔬 StarSec & Starlink Scientific Simulation Suite

> **Reproducible First-Principles Astrodynamics, Electromagnetics, Decibel Link Budgets, Post-Quantum Cryptography, and Space Cyber Risk Tooling**

This directory contains standalone, reproducible scientific Python simulation tools designed to accompany the StarSec Constellation Security Framework (CSF v1.0), the technical monograph, and R&D whitepapers of `techdeveloper-org/starlink-research-report`.

---

## 📦 Simulation Modules Overview

| Script | Domain & Physics | Key Mathematical Equations | Execution Command |
| :--- | :--- | :--- | :--- |
| **[`threat_score_calculator.py`](./threat_score_calculator.py)** | Space Cyber Risk Scoring | CVSS v4.0 Space Profile, Aerospace SPARTA, Orbital Conjunction ($P_c$) multipliers, and automated JSON export. | `python simulations/threat_score_calculator.py --demo` |
| **[`mpc_quorum_telecommand.py`](./mpc_quorum_telecommand.py)** | Space TT&C Quorum Safety | Shamir $(t, n)$ polynomial secret sharing over $\mathbb{F}_{2^{256}-189}$, FROST multisignatures, and flight-critical de-orbit authorization. | `python simulations/mpc_quorum_telecommand.py --demo` |
| **[`orbit_sgp4_propagator.py`](./orbit_sgp4_propagator.py)** | Astrodynamics & Conjunctions | Keplerian motion, $J_2$ nodal precession ($\dot{\Omega} = -4.49^\circ/\text{day}$), Foster & Estes (1992) B-plane collision probability integral $P_c$. | `python simulations/orbit_sgp4_propagator.py` |
| **[`phased_array_beamforming.py`](./phased_array_beamforming.py)** | Phased Array Electromagnetics | 2D Array Factor $AF(\theta, \phi)$, True Time Delay (TTD), and Capon MVDR adaptive spatial null-steering ($\mathbf{w}_{opt} = \frac{\mathbf{R}_{xx}^{-1}\mathbf{a}}{\mathbf{a}^H\mathbf{R}_{xx}^{-1}\mathbf{a}}$) with $>80\text{ dB}$ jammer cancellation. | `python simulations/phased_array_beamforming.py` |
| **[`rf_link_budget_calculator.py`](./rf_link_budget_calculator.py)** | RF Telecommunications | Decibel link budget, Free-Space Path Loss ($FSPL$), ITU-R P.618 rain attenuation, system noise temperature $T_{sys}$, $C/N_0$, and SNR margin against DVB-S2X MODCODs. | `python simulations/rf_link_budget_calculator.py` |
| **[`pqc_kem_benchmark.py`](./pqc_kem_benchmark.py)** | Post-Quantum Cryptography | NIST FIPS 203 (ML-KEM-1024 / Kyber-1024) polynomial ring arithmetic over $\mathbb{Z}_{3329}[X]/(X^{256} + 1)$, key encapsulation overhead, and 100 Gbps laser crosslink latency benchmarking. | `python simulations/pqc_kem_benchmark.py` |
| **[`stride_fault_injection_sim.py`](./stride_fault_injection_sim.py)** | Hardware Cyber Defense | Lennert Wouters sub-10 ns BootROM voltage glitching, CMOS alpha-power law propagation delay expansion, and analog crowbar clamp (<250 ps) / Dual-Core Lockstep (DCLS) neutralization. | `python simulations/stride_fault_injection_sim.py` |

---

## 🚀 Quickstart & Prerequisites

### 1. Requirements:
- Python 3.10, 3.11, 3.12, or 3.13+
- NumPy $\ge 1.24.0$
- SciPy $\ge 1.10.0$

```bash
pip install -r simulations/requirements.txt
```

### 2. Run the Full Test Harness:
An automated unit test suite verifies all mathematical models and assertions in $<0.25\text{ seconds}$ across 23 comprehensive tests:

```bash
python -m unittest simulations.test_simulations
```

Expected output:
```text
.......................
----------------------------------------------------------------------
Ran 23 tests in 0.105s

OK
```

---

## 📊 Sample Execution Outputs

### 1. Threat Score Calculator (CVSS v4 Space Profile):
```text
================================================================================
  STARSEC CVSS v4.0 SPACE PROFILE REPORT: TTC-ROGUE-001
================================================================================
  Threat Name            : Unilateral Rogue Atmospheric De-Orbit Command
  SPARTA ID              : IMP-0006
  MITRE ATT&CK for Space : T1485
  Mandatory Control RFC  : STARSEC-RFC-004
--------------------------------------------------------------------------------
  Base CVSS v4.0 Score   : 9.4 / 10.0
  Space Multiplier Factor: 2.05x
  - Conjunction Impact   : CRITICAL (C)
  - Life Safety Risk     : HIGH (H)
  - Telemetry Blackout   : PERSISTENT (P)
  - De-orbit Hazard      : UNCONTROLLED (U)
--------------------------------------------------------------------------------
  FINAL SPACE CVSS SCORE : 10.0 / 10.0
  SEVERITY CLASSIFICATION: CRITICAL
================================================================================
```

### 2. Multi-Party Telecommand Quorum Verification:
```text
Spacecraft ID             : SAT-1042
Quorum Architecture       : (3, 5) Threshold over F_(2^256 - 189)
Authorization Master Hash : ede1cc1399aefafc...4ebfe9fdbdd3ca37

[SCENARIO 1] Single Rogue Ground Operator (Compromised 1 Share):
  Command ID      : CMD-DEORBIT-9901
  Authorized      : False
  Rejection Reason: Sub-threshold presentation: received 1 shares, strictly requires t=3

[SCENARIO 3] Valid Multi-Party Quorum (3 Authorized Entities):
  Command ID      : CMD-DEORBIT-9901
  Authorized      : True
  Quorum Hash     : ede1cc1399aefafc...
  Maneuver Status : PASS - ARMING PLASMA THRUSTERS FOR DE-ORBIT BURN
```

### 3. Phased Array Capon MVDR Null-Steering:
```text
Array Geometry             : 32 x 40 (1280 patch elements)
Carrier Frequency          : 12.00 GHz (Ku-band Downlink)
Boresight Peak Gain        : 31.07 dBi (Theoretical: 10*log10(1280) = 31.07 dBi)
Hostile Jammer 1 (AoA)     : (theta=15.0 deg, phi=85.0 deg)
Unadapted Sidelobe at J1   : -38.49 dBi
MVDR Synthesized Null 1    : -120.00 dBi
Spatial Null Attenuation   : 81.51 dB (Cancellation Factor: >100,000,000x)
Target Main Beam Gain (MVDR): 31.07 dBi (Distortion: 0.000 dB)
ECCM Evaluation            : PASS (>45 dB Jammer Suppression)
```

---

## 🏛️ Code Standards & Architecture
All simulation scripts adhere to:
- **Level 2.2 Docstrings-Only:** Contract docstrings on every public class, method, and function; zero inline narrative comments.
- **Strict Typing:** 100% type annotations (`dataclass`, `Tuple`, `List`, `Dict`, `Optional`).
- **Reproducible Science:** Deterministic parameter inputs grounded in official SpaceX FCC filings and peer-reviewed aerospace literature.
