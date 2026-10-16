# 🔬 Starlink Scientific Simulation Suite

> **Reproducible First-Principles Astrodynamics, Electromagnetics, Decibel Link Budgets, and Post-Quantum Cryptography Tooling**

This directory contains standalone, reproducible scientific Python simulation tools designed to accompany the technical monograph and R&D whitepapers of the `techdeveloper-org/starlink-research-report`.

---

## 📦 Simulation Modules Overview

| Script | Domain & Physics | Key Mathematical Equations | Execution Command |
| :--- | :--- | :--- | :--- |
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
An automated unit test suite verifies all mathematical models and assertions in $<0.25\text{ seconds}$:

```bash
python -m unittest simulations.test_simulations
```

Expected output:
```text
..........
----------------------------------------------------------------------
Ran 10 tests in 0.213s

OK
```

---

## 📊 Sample Execution Outputs

### 1. Phased Array Capon MVDR Null-Steering:
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

### 2. RF Decibel Link Budget (Ku-band Downlink):
```text
Carrier Frequency          : 12.00 GHz
Bandwidth / Bit Rate       : 250.0 MHz / 220.0 Mbps
Transmitter EIRP           : 40.22 dBW (70.22 dBm)
Free-Space Path Loss (FSPL): 171.53 dB (Slant: 750 km)
Total Attenuation          : 173.48 dB (Rain: 1.20 dB)
Received Power (P_rx)      : -99.06 dBW (-69.06 dBm)
System Noise Temp (T_sys)  : 175.3 K
Carrier-to-Noise (C/N0)    : 107.10 dB-Hz
Channel SNR (C/N)          : 23.12 dB
Energy per bit (Eb/N0)     : 23.67 dB (Required: 6.5 dB)
Link Operating Margin      : 17.17 dB (LINK CLOSED - PASS)
```

### 3. Astrodynamics & Conjunction Assessment (Shell 1):
```text
Semi-Major Axis (a)        : 6928.137 km
Orbital Altitude (h)       : 550.0 km
Inclination (i)            : 53.0 deg
Orbital Period (T)         : 95.65 minutes (5739.0 s)
Mean Orbital Velocity (v)  : 7.586 km/s (27309.0 km/h)
J2 Nodal Precession Rate   : -4.489 deg/solar day
Conjunction Miss Distance  : 85.0 m
Collision Probability (Pc) : 1.114250e-02
Maneuver Threshold (1e-4)  : ALERT: TRIGGER AVOIDANCE BURN
```

---

## 🏛️ Code Standards & Architecture
All simulation scripts adhere to:
- **Level 2.2 Docstrings-Only:** Contract docstrings on every public class, method, and function; zero inline narrative comments.
- **Strict Typing:** 100% type annotations (`dataclass`, `Tuple`, `List`, `Dict`).
- **Reproducible Science:** Deterministic parameter inputs grounded in official SpaceX FCC filings and peer-reviewed aerospace literature.
