# 🛰️ StarSec CVSS v4.0 Space Profile: Orbital Risk & Mission Criticality Metric

> **Specification Designation:** `STARSEC-CVSS4-SPACE-1.0`  
> **Status:** Normative Scoring Metric  
> **Classification:** Space Cyber Risk Quantification & Aerospace Safety  
> **Base Standard:** FIRST CVSS v4.0 Specification (June 2023)  

---

## 1. Abstract & Scope

The FIRST Common Vulnerability Scoring System (CVSS v4.0) provides standardized metrics for enterprise IT, software libraries, and physical industrial hardware. However, standard CVSS v4.0 lacks metrics for physical spaceflight dynamics, orbital conjunction risk, human life-safety in low Earth orbit (e.g., International Space Station and Tiangong proximity), and telemetry command blackouts. 

This specification defines the **StarSec CVSS v4 Space Profile (CVSS-Space)**. It introduces dedicated environmental and mission-impact metrics that quantify the kinetic, orbital, and geopolitical blast radius of vulnerabilities affecting satellite mega-constellations.

---

## 2. Space Domain Metric Extensions

The CVSS-Space Profile appends five domain-specific metrics to the standard CVSS v4.0 vector string:

```
CVSS:4.0/AV:.../AC:.../AT:.../PR:.../UI:.../VC:.../VI:.../VA:.../SC:.../SI:.../SA:...
/AV_SPACE:.../OCI:.../LSR:.../TBW:.../DH:...
```

### 2.1 Space Attack Vector (`AV_SPACE`)
Measures the physical and electromagnetic accessibility of the target system:
- **Ground Local (`G_LOC`):** Requires physical or console access to ground gateway or user terminal.
- **Terrestrial Wireless (`T_RF`):** Exploitable via line-of-sight RF transmission from ground jammers/emitters ($0\text{ to }1000\text{ km}$).
- **Orbital Laser Crosslink (`S_ISL`):** Exploitable only across Free-Space Optical laser links from co-orbital space assets.
- **Space RF Broadcast (`S_RF`):** Exploitable via satellite-to-satellite inter-orbital RF links.

### 2.2 Orbital Conjunction Impact (`OCI`)
Quantifies the risk of inducing kinetic collisions in congested orbital shells (e.g., 550 km Shell 1):
- **None (`N`):** Vulnerability does not impact trajectory, altitude, or attitude determination ($P_c = 0$).
- **Low (`L`):** Introduces minor attitude drift or delayed maneuvers without triggering orbital conjunction thresholds ($P_c < 10^{-5}$).
- **Critical (`C`):** Capable of inducing uncommanded burns or disabling collision avoidance thrusters, triggering collision probability exceeding Foster & Estes threshold ($P_c > 10^{-4}$).

### 2.3 Life Safety Risk (`LSR`)
Measures the potential threat to manned spaceflight habitats:
- **None (`N`):** Spacecraft operational perigee and apogee remain completely decoupled from crewed orbits ($h > 450\text{ km}$ or inclination disjoint from $51.6^\circ$).
- **Low (`L`):** Debris or re-entry footprint passes near human flight paths but maintains $>50\text{ km}$ spatial clearance.
- **High (`H`):** Trajectory deviation directly intersects the orbital altitude and inclination plane of the International Space Station (ISS, $\sim 415\text{ km}, 51.6^\circ$) or Tiangong ($\sim 380\text{ km}, 41.5^\circ$).

### 2.4 Telemetry Blackout Window (`TBW`)
Quantifies the duration of ground command authority loss:
- **None (`N`):** Command links remain fully responsive during exploitation.
- **Transient (`T`):** Command link disrupted for less than one orbital period ($<95\text{ minutes}$).
- **Persistent (`P`):** Satellite command link severed across multiple orbital passes ($>95\text{ minutes}$), requiring autonomous fail-safe recovery.

### 2.5 De-Orbit Hazard (`DH`)
Measures the risk of catastrophic or uncontrolled atmospheric re-entry:
- **None (`N`):** Spacecraft remains in nominal orbital station-keeping.
- **Controlled (`C`):** Triggers premature or uncoordinated but controlled targeted re-entry into oceanic unpopulated zones.
- **Uncontrolled (`U`):** Triggers unguided orbital decay with potential survival of titanium tank fragments impacting terrestrial landmasses.

---

## 3. Mathematical Scoring Formulation

The composite Space Profile Score $S_{space} \in [0.0, 10.0]$ is derived from the CVSS v4.0 Base Score $S_{base}$ modulated by space impact multipliers:

$$S_{space} = \min\left(10.0, \, S_{base} \times M_{space}\right)$$

where the Space Multiplier $M_{space}$ is defined as:

$$M_{space} = 1.0 + \Delta_{OCI} + \Delta_{LSR} + \Delta_{TBW} + \Delta_{DH}$$

### Multiplier Weights:
- **$\Delta_{OCI}$:** $\text{None} = 0.0$, $\text{Low} = 0.05$, $\text{Critical} = 0.25$
- **$\Delta_{LSR}$:** $\text{None} = 0.0$, $\text{Low} = 0.10$, $\text{High} = 0.35$
- **$\Delta_{TBW}$:** $\text{None} = 0.0$, $\text{Transient} = 0.05$, $\text{Persistent} = 0.15$
- **$\Delta_{DH}$:** $\text{None} = 0.0$, $\text{Controlled} = 0.10$, $\text{Uncontrolled} = 0.30$

---

## 4. Worked Benchmark Examples

### Example 1: `TTC-ROGUE-001` (Unilateral Rogue De-Orbit Command)
- **Base Vector:** `CVSS:4.0/AV:N/AC:L/AT:N/PR:H/UI:N/VC:H/VI:H/VA:H/SC:H/SI:H/SA:H` ($S_{base} = 9.4$)
- **Space Extensions:** `/AV_SPACE:G_LOC/OCI:C/LSR:H/TBW:P/DH:U`
- **Multiplier Calculation:**
  $$M_{space} = 1.0 + 0.25 (\text{OCI:C}) + 0.35 (\text{LSR:H}) + 0.15 (\text{TBW:P}) + 0.30 (\text{DH:U}) = 2.05$$
- **Final Space Score:**
  $$S_{space} = \min(10.0, \, 9.4 \times 2.05) = \mathbf{10.0} \quad \text{(CRITICAL)}$$

### Example 2: `UT-GLITCH-001` (User Terminal Voltage Glitching)
- **Base Vector:** `CVSS:4.0/AV:P/AC:H/AT:P/PR:N/UI:N/VC:H/VI:H/VA:N/SC:L/SI:L/SA:N` ($S_{base} = 6.2$)
- **Space Extensions:** `/AV_SPACE:G_LOC/OCI:N/LSR:N/TBW:N/DH:N`
- **Multiplier Calculation:**
  $$M_{space} = 1.0 + 0.0 + 0.0 + 0.0 + 0.0 = 1.0$$
- **Final Space Score:**
  $$S_{space} = \min(10.0, \, 6.2 \times 1.0) = \mathbf{6.2} \quad \text{(MEDIUM)}$$

---

## 5. Automated Evaluation CLI Interface

The Space CVSS v4 Profile is programmatically evaluated via the Python CLI tool:
```bash
python simulations/threat_score_calculator.py --vector TTC-ROGUE-001
```
Output format: JSON and formatted ANSI terminal summary.
