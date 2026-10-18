# 🛰️ StarSec Constellation Threat Matrix (SPARTA & MITRE ATT&CK Aligned)

> **Matrix Designation:** `STARSEC-MATRIX-1.0`  
> **Status:** Normative Threat Catalog & Countermeasure Mapping  
> **Classification:** Aerospace Cybersecurity & Constellation Defense  
> **Governing Standards:** Aerospace Corp SPARTA, MITRE ATT&CK for Space, StarSec CSF v1.0  

---

## 1. Matrix Overview

This matrix catalogs 32 high-impact threat vectors targeting LEO mega-constellations. Each vector is cross-mapped to Aerospace Corporation **SPARTA** technique IDs, **MITRE ATT&CK for Space** identifiers, the governing **StarSec Defense Layer** (Layers 1–4), and the mandatory normative control standard.

---

## 2. Master Constellation Threat Matrix

### Domain A: Physical Terminal & User Segment (UT)

| Vector ID | Threat Vector Name | SPARTA ID | ATT&CK ID | StarSec Layer | Mandatory Control | Detection Mechanism | Mitigation Standard |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `UT-GLITCH-001` | Sub-10 ns Voltage Glitching on BootROM | `EX-0005` | `T1542` | Layer 1 | `CTRL-HW-001` | High-speed analog voltage threshold comparators ($\Delta V > 12\%$). | Sub-250 ps crowbar clamps and DCLS 2-cycle lockstep ([RFC-002](../spec/RFC-002-UT-Hardware-Hardening.md)). |
| `UT-PROBE-002` | Silicon Decapping & Micro-probing | `EX-0008` | `T1200` | Layer 1 | `CTRL-HW-002` | Active capacitive and continuity sensing across PCB serpentine mesh. | Sub-10 ns BBRAM power dump and master key zeroization ([RFC-002](../spec/RFC-002-UT-Hardware-Hardening.md)). |
| `UT-CLONE-003` | Flash Memory Key Extraction & Cloning | `EXF-0002`| `T1005` | Layer 1 | `CTRL-HW-003` | Periodic challenge-response verification against hardware PUF fingerprint. | SRAM Physically Unclonable Function (PUF) with BCH(255, 71, 29) ([RFC-002](../spec/RFC-002-UT-Hardware-Hardening.md)). |
| `UT-EMFI-004` | Electromagnetic Fault Injection (EMFI) | `EX-0012` | `T1588` | Layer 1 | `CTRL-HW-004` | On-die eddy current loop sensors and differential lockstep comparators. | Dual-Core Lockstep CPU halt and cold reset on core execution mismatch. |
| `UT-FW-005` | Unsigned Firmware Injection via eMMC | `DEF-0009`| `T1547` | Layer 1 | `CTRL-HW-005` | Hardware-enforced root-of-trust hash check (`dm-verity`) at boot. | Cryptographic signature validation anchored in read-only Mask ROM. |

---

### Domain B: RF & Wireless PHY Layer (Uplink, Downlink & Array Electromagnetics)

| Vector ID | Threat Vector Name | SPARTA ID | ATT&CK ID | StarSec Layer | Mandatory Control | Detection Mechanism | Mitigation Standard |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `RF-JAM-001` | High-Power Ground Barrage Jamming | `EX-0001` | `T1498` | Layer 2 | `CTRL-RF-001` | Array covariance eigenvalue analysis detecting directional power surge. | Capon MVDR digital null-steering achieving $>45\text{ dB}$ cancellation ([RFC-003](../spec/RFC-003-RF-PHY-Jamming-Defense.md)). |
| `RF-SPOOF-002`| Downlink Mezzanine Carrier Spoofing | `EX-0002` | `T1565` | Layer 2 | `CTRL-RF-002` | Doppler-trajectory drift cross-correlation against SGP4 ephemeris. | Automatic quarantine of signals with $\Delta f > 150\text{ Hz}$ or $\dot{f} \approx 0$ ([RFC-003](../spec/RFC-003-RF-PHY-Jamming-Defense.md)). |
| `RF-SWEEP-003`| Smart Agile Sweep Jamming | `EX-0003` | `T1499` | Layer 2 | `CTRL-RF-003` | Fast Fourier Transform (FFT) spectral occupancy tracking. | Cognitive Frequency Hopping (CFHSS $>10,000\text{ hops/s}$) via PQC seed ([RFC-003](../spec/RFC-003-RF-PHY-Jamming-Defense.md)). |
| `RF-REPLAY-004`| Downlink Frame Replay & Telemetry Loop | `IMP-0004`| `T1589` | Layer 2 | `CTRL-RF-004` | Monotonic 64-bit frame sequence counter verification. | Frame rejection and session teardown on duplicate or out-of-order counter. |
| `RF-EAVES-005`| Ground-Level RF Downlink Interception | `REC-0001`| `T1040` | Layer 2 | `CTRL-RF-005` | None (passive adversary listening). | Universal link-layer AES-256-GCM encryption with ephemeral session rotation. |

---

### Domain C: Optical ISL & In-Orbit Space Mesh

| Vector ID | Threat Vector Name | SPARTA ID | ATT&CK ID | StarSec Layer | Mandatory Control | Detection Mechanism | Mitigation Standard |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `ISL-HNDL-001` | Harvest-Now-Decrypt-Later Eavesdropping | `REC-0005`| `T1557` | Layer 3 | `CTRL-ISL-001` | Co-orbital proximity sensors and laser power loss monitoring. | NIST FIPS 203 (ML-KEM-1024) lattice-based post-quantum key exchange ([RFC-001](../spec/RFC-001-ISL-Mutual-Auth.md)). |
| `ISL-DAZZLE-002`| Ground-to-Space Laser Sensor Blinding | `EX-0015` | `T1499` | Layer 3 | `CTRL-ISL-002` | Quadrant photodiode saturation sensor triggering $>10\text{ kW/cm}^2$. | Sub-50 ns mechanical MEMS shutter closure and automated RF alert. |
| `ISL-POISON-003`| Rogue In-Orbit BGP Route Poisoning | `LAT-0002`| `T1090` | Layer 3 | `CTRL-ISL-003` | Dynamic mesh topology anomaly tracking and RPKI Route Origin validation. | BGP RPKI strict invalid-drop filtering and Byzantine peer quarantine. |
| `ISL-SPOOF-004`| Malicious Spacecraft Peer Impersonation | `IMP-0002`| `T1586` | Layer 3 | `CTRL-ISL-004` | Cryptographic signature validation failure during handshake. | Mutual authentication using NIST FIPS 204 (ML-DSA-87) certificates ([RFC-001](../spec/RFC-001-ISL-Mutual-Auth.md)). |
| `ISL-REPLAY-005`| Laser Beam-Break Session Ticket Replay | `DEF-0014`| `T1550` | Layer 3 | `CTRL-ISL-005` | Monotonic counter and epoch timestamp check on resumption tickets. | Anti-replay ticket cache and epoch-bound STEK rotation ([RFC-001](../spec/RFC-001-ISL-Mutual-Auth.md)). |

---

### Domain D: Spacecraft Bus & Avionics

| Vector ID | Threat Vector Name | SPARTA ID | ATT&CK ID | StarSec Layer | Mandatory Control | Detection Mechanism | Mitigation Standard |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `BUS-WIPER-001`| Flight Software Wiper (AcidRain-style) | `IMP-0001`| `T1485` | Layer 1 | `CTRL-BUS-001` | Integrity monitoring daemon detecting unsigned write calls to flash. | Immutable read-only root filesystems (`dm-verity`) and hardware write-protect pins. |
| `BUS-SEU-002` | Radiation Single Event Upset Injection | `EX-0010` | `T1588` | Layer 1 | `CTRL-BUS-002` | EDAC (Error Detection and Correction) parity syndrome checking. | Triple Modular Redundancy (TMR) on registers and radiation-tolerant SRAM. |
| `BUS-ATT-003` | Attitude Reaction-Wheel Spin Saturation | `IMP-0005`| `T1489` | Layer 4 | `CTRL-BUS-003` | Angular momentum drift monitor detecting monotonic wheel acceleration. | Command rejected unless authorized via (t, n) threshold signature ([RFC-004](../spec/RFC-004-TTC-Multi-Party-Quorum.md)). |
| `BUS-ROLL-004`| Firmware Anti-Rollback Exploit | `DEF-0005`| `T1542` | Layer 1 | `CTRL-BUS-004` | Non-volatile security version counter check during bootloader execution. | Monotonic hardware eFuse anti-rollback counter incrementation on updates. |

---

### Domain E: Ground Segment, Feeder Gateways & TT&C

| Vector ID | Threat Vector Name | SPARTA ID | ATT&CK ID | StarSec Layer | Mandatory Control | Detection Mechanism | Mitigation Standard |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `TTC-ROGUE-001`| Unilateral Rogue Atmospheric De-Orbit | `IMP-0006`| `T1485` | Layer 4 | `CTRL-TTC-001` | Quorum signature counter check ($|\mathcal{T}| < t$). | Shamir (t, n) Threshold Secret Sharing and FROST multi-signatures ([RFC-004](../spec/RFC-004-TTC-Multi-Party-Quorum.md)). |
| `TTC-FORGE-002`| Ground Station Command Frame Forgery | `IMP-0003`| `T1588` | Layer 4 | `CTRL-TTC-002` | Frame cryptographic MAC authentication failure. | HMAC-SHA256 frame authentication and ML-DSA-87 operator signatures. |
| `TTC-SDN-003` | Centralized SDN Controller Hijack | `LAT-0005`| `T1078` | Layer 4 | `CTRL-TTC-003` | Multi-controller consensus disagreement and telemetry divergence. | Byzantine Fault Tolerant (BFT) distributed consensus across 5+ ground nodes. |
| `TTC-SUPPLY-004`| Supply Chain Dependency Backdoor | `INIT-0001`| `T1195` | Layer 4 | `CTRL-TTC-004` | Automated binary Software Bill of Materials (SBOM) and hash attestation. | In-toto reproducible build pipelines and IETF RFC 9019 SUIT manifests. |

---

### Domain F: Geopolitical, Macro Space & Kinetic-Cyber Convergence

| Vector ID | Threat Vector Name | SPARTA ID | ATT&CK ID | StarSec Layer | Mandatory Control | Detection Mechanism | Mitigation Standard |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `GEO-KILL-001` | Unilateral Sovereign Kill Switch Coercion | `IMP-0008`| `T1489` | Layer 4 | `CTRL-GEO-001` | Multi-sovereign quorum threshold validation failure. | Multi-lateral Sovereign Quorum Escrow requiring consensus to deactivate regions ([RFC-004](../spec/RFC-004-TTC-Multi-Party-Quorum.md)). |
| `GEO-ASAT-002` | Cyber-Induced Orbital Collision (ASAT) | `IMP-0007`| `T1485` | Layer 4 | `CTRL-GEO-002` | SGP4 Foster-Estes conjunction assessment ($P_c > 10^{-4}$). | Autonomous flight computer collision avoidance burn override ([RFC-004](../spec/RFC-004-TTC-Multi-Party-Quorum.md)). |
| `GEO-COORB-003`| Co-Orbital Inspection Interceptor Eavesdrop| `REC-0004`| `T1557` | Layer 3 | `CTRL-GEO-003` | Space Domain Awareness (SDA) radar tracking relative range $<50\text{ km}$. | Automatic optical power attenuation and beam spatial jitter scrambling. |
| `GEO-PRIV-004` | Sovereign Subscriber Payload Surveillance | `EXF-0004`| `T1040` | Layer 4 | `CTRL-GEO-004` | Gateway routing policy inspection. | Zero-Knowledge regulatory compliance verification isolating payload transit ([RFC-004](../spec/RFC-004-TTC-Multi-Party-Quorum.md)). |

---

## 3. Mandatory Implementation Verification Matrix

All 32 threat vectors MUST be validated against the automated simulation suite:
- Vectors `UT-GLITCH-001`, `UT-EMFI-004` $\to$ `simulations/stride_fault_injection_sim.py`
- Vectors `RF-JAM-001`, `RF-SPOOF-002`, `RF-SWEEP-003` $\to$ `simulations/phased_array_beamforming.py`
- Vectors `ISL-HNDL-001`, `ISL-SPOOF-004` $\to$ `simulations/pqc_kem_benchmark.py`
- Vectors `TTC-ROGUE-001`, `GEO-KILL-001`, `GEO-ASAT-002` $\to$ `simulations/mpc_quorum_telecommand.py`
- Cross-vector risk scoring $\to$ `simulations/threat_score_calculator.py`
