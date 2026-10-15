# 📡 Whitepaper 02 — RF Engineering, Phased Arrays & Link Budget Analysis

> **Author Discipline:** Radio Frequency Engineering & Wireless Communications  
> **Source Agent Authority:** `radio-frequency-specialist` & `telecom-mathematics-expert`  
> **Mandatory Skills:** `networking-fundamentals-core`, `audio-spectral-math`, `5g-radio-access-core`  
> **Repository:** `techdeveloper-org/starlink-research-report`  
> **Classification:** Technical Engineering Whitepaper

---

## Executive Summary

Starlink's core terrestrial-to-space communications interface relies on Active Electronically Scanned Arrays (AESA) operating across Ku-band (10.7–14.5 GHz) and Ka-band (17.8–30.0 GHz), with next-generation V-band (37.5–51.4 GHz) feeder links rolling out. Connecting a ground terminal to a satellite moving at $7.56\text{ km/s}$ across the sky without moving parts requires microsecond-level electronic beam steering, dynamic Doppler frequency compensation reaching $\pm 350\text{ kHz}$ with Doppler rates exceeding $4.8\text{ kHz/s}$, and link budgets designed to survive atmospheric absorption and severe rain attenuation. This paper derives the electromagnetic array factors, wideband beam squint physics, Doppler compensation loops, and full decibel link budgets.

---

## 1. Phased Array Antenna (AESA) Electromagnetics

Traditional parabolic reflector dishes require mechanical gimbals to steer a beam, introducing mechanical inertia, motor wear, and slow tracking speeds ($<5^\circ/\text{s}$). Starlink's user terminal ("Dishy") and satellite payload employ active electronically scanned planar arrays containing thousands of radiating patch elements.

```
       Incoming Wavefront (Angle θ)
             \       \       \
              \       \       \
           ────┼───────┼───────┼────
              [E₀]    [E₁]    [E₂]    Radiating Patch Elements
               │       │       │
              [ΔΦ₀]   [ΔΦ₁]   [ΔΦ₂]   Phase Shifters / TTD Units
               │       │       │
              [LNA]   [LNA]   [LNA]   Low Noise Amplifiers
               └───────┼───────┘
                       ▼
                 RF Combiner (Constructive Interference Peak)
```

### 1.1 Two-Dimensional Planar Array Factor Derivation
Consider a planar rectangular array of $M \times N$ elements positioned on the $xy$-plane with inter-element spacing $d_x$ and $d_y$. The far-field radiation pattern is the product of the individual element pattern $EP(\theta, \phi)$ and the Array Factor $AF(\theta, \phi)$:

$$E_{total}(\theta, \phi) = EP(\theta, \phi) \cdot AF(\theta, \phi)$$

The normalized 2D Array Factor is derived from vector wave superposition:

$$AF(\theta, \phi) = \sum_{m=0}^{M-1} \sum_{n=0}^{N-1} A_{mn} \exp\left( j \left[ m (k d_x \sin\theta \cos\phi + \beta_x) + n (k d_y \sin\theta \sin\phi + \beta_y) \right] \right)$$

Where:
- $k = \frac{2\pi}{\lambda}$ is the electromagnetic wavenumber.
- $A_{mn}$ is the amplitude excitation weight of element $(m,n)$ (tapered via Taylor or Chebyshev distributions to suppress sidelobes).
- $\theta$ is the polar angle measured from array normal (boresight).
- $\phi$ is the azimuthal angle.
- $\beta_x, \beta_y$ are the progressive inter-element phase shifts.

To steer the main beam peak toward the target direction $(\theta_0, \phi_0)$, the phase terms must cancel the spatial path delay:

$$\beta_x = -k d_x \sin\theta_0 \cos\phi_0, \quad \beta_y = -k d_y \sin\theta_0 \sin\phi_0$$

Setting element spacing $d_x = d_y = \frac{\lambda}{2}$ avoids grating lobes within the visible hemisphere:

$$\beta_x = -\pi \sin\theta_0 \cos\phi_0, \quad \beta_y = -\pi \sin\theta_0 \sin\phi_0$$

### 1.2 Wideband Beam Squint & True Time Delay (TTD)
When phase shifters are used across a wide bandwidth channel ($\Delta f = 250\text{ MHz}$ or $500\text{ MHz}$), the beam pointing angle shifts with frequency—a phenomenon known as **beam squint**:

$$\sin\theta(f) = \frac{f_0}{f} \sin\theta_0$$

Differentiating with respect to frequency:

$$\Delta\theta \approx -\frac{\Delta f}{f_0} \tan\theta_0$$

At an extreme scan angle $\theta_0 = 50^\circ$ and carrier $f_0 = 12\text{ GHz}$ with $\Delta f = 250\text{ MHz}$:
$$\Delta\theta \approx -\frac{250 \times 10^6}{12 \times 10^9} \tan(50^\circ) = -0.0208 \times 1.1918 = -0.0248\text{ rad} \approx -1.42^\circ$$

Because Starlink's 3 dB beamwidth is narrow ($\approx 2.5^\circ$), a $1.42^\circ$ squint causes an edge-of-channel gain loss exceeding $3\text{ dB}$, degrading throughput. SpaceX resolves this by dividing the array into sub-arrays fed by switched **True Time Delay (TTD)** delay lines rather than narrowband phase shifters, ensuring frequency-independent steering:

$$\tau_x = \frac{d_x \sin\theta_0 \cos\phi_0}{c}, \quad \tau_y = \frac{d_y \sin\theta_0 \sin\phi_0}{c}$$

---

## 2. Dynamic Doppler Shift Mechanics

Because Starlink satellites orbit at $v \approx 7.56\text{ km/s}$ at an altitude of $550\text{ km}$, the line-of-sight velocity vector $v_{los}(t)$ relative to a stationary ground terminal changes continuously throughout an overhead pass.

```
       Orbit Path ────► Satellite (v = 7.56 km/s)
                       /│\
                      / │ \
     Approaching     /  │  \    Receding
    (Blue Shift)    /   │   \   (Red Shift)
                   /    │h   \
                  /     │     \
                 ▼      ▼      ▼
                ─────────────────── Ground Station (Terminal)
```

### 2.1 Doppler Frequency Derivation
The relativistic Doppler shift simplifies to the classical first-order form since $\frac{v}{c} \approx 2.52 \times 10^{-5} \ll 1$:

$$f_d(t) = \frac{v_{los}(t)}{c} f_0 = \frac{\mathbf{v}(t) \cdot (\mathbf{r}_{sat}(t) - \mathbf{r}_{term})}{c \|\mathbf{r}_{sat}(t) - \mathbf{r}_{term}\|} f_0$$

Let $x$ be the along-track distance from the closest approach (zenith) point, where $x(t) = v \cdot t$:
$$v_{los}(t) = -\frac{v^2 t}{\sqrt{h^2 + (v t)^2}}$$

1. **Maximum Doppler Frequency (Horizon Acquisition, $\epsilon = 25^\circ$, $d \approx 1,123\text{ km}$):**
   $$v_{los, max} \approx v \cos(25^\circ) \approx 7.56\text{ km/s} \times 0.9063 \approx 6.85\text{ km/s}$$
   For the Ku-band uplink carrier ($f_0 = 14.25\text{ GHz}$):
   $$|f_{d, max}| = \frac{6.85 \times 10^3}{3.0 \times 10^8} \times 14.25 \times 10^9 \approx 325.4\text{ kHz}$$

2. **Maximum Doppler Rate of Change (Passing Through Zenith, $t = 0$):**
   $$\left.\frac{df_d}{dt}\right|_{zenith} = -\frac{v^2 f_0}{c h}$$
   Substituting numerical values:
   $$\left.\frac{df_d}{dt}\right|_{zenith} = -\frac{(7560)^2 \times (14.25 \times 10^9)}{(3.0 \times 10^8) \times (550 \times 10^3)} = -\frac{5.715 \times 10^7 \times 1.425 \times 10^{10}}{1.65 \times 10^{14}} \approx -4.93\text{ kHz/s}$$

### 2.2 Receiver Carrier Tracking Loop Architecture
To prevent cycle slips and loss of symbol lock under a $-4.93\text{ kHz/s}$ frequency ramp, Starlink modems utilize a **3rd-Order Phase-Locked Loop (PLL)** coupled with **Feedforward Ephemeris Aided Doppler Compensation**:

```mermaid
graph LR
    RFIn[Raw RF Input] --> Mixer[Complex IQ Mixer]
    Mixer --> ADC[High-Speed ADC]
    ADC --> Demod[Baseband Processing]
    
    GPS[Onboard GPS / Ephemeris] --> DopplerModel[Orbit Doppler Calculator]
    DopplerModel --> NCO[Numerically Controlled Oscillator]
    NCO --> Mixer
    
    Demod --> PhaseDiscrim[Phase Detector]
    PhaseDiscrim --> LoopFilter[3rd Order Loop Filter]
    LoopFilter --> NCO
```

By predicting $>98\%$ of the Doppler curve using satellite ephemeris, the residual frequency error entering the digital PLL is $<50\text{ Hz}$, allowing narrow tracking loop bandwidths ($B_L \approx 100\text{ Hz}$) that maximize noise rejection.

---

## 3. Comprehensive Decibel Link Budget Worksheets

The fundamental link budget equation calculates the received carrier power-to-noise spectral density ratio ($C/N_0$):

$$\left[\frac{C}{N_0}\right] = [EIRP] - [FSPL] - [L_{atm}] - [L_{rain}] + \left[\frac{G}{T}\right] - [k_B]$$

Where:
- $[EIRP] = [P_{tx}] + [G_{tx}] - [L_{feed}]$ is Equivalent Isotropically Radiated Power ($\text{dBW}$).
- $[FSPL] = 20\log_{10}(d) + 20\log_{10}(f) + 92.45$ is Free Space Path Loss ($\text{dB}$).
- $[L_{atm}]$ is gaseous atmospheric absorption ($\text{dB}$) per ITU-R P.676.
- $[L_{rain}]$ is rain attenuation exceeded for $p = 0.01\%$ of the year per ITU-R P.618 ($\text{dB}$).
- $[G/T] = [G_{rx}] - 10\log_{10}(T_{sys})$ is Receiver Figure of Merit ($\text{dB/K}$).
- $[k_B] = -228.6\text{ dBW/(Hz}\cdot\text{K)}$ is Boltzmann's constant.

### 3.1 Starlink Ku-Band Downlink & Uplink Decibel Chains

| Parameter | User Downlink (Sat $\to$ Terminal) | User Uplink (Terminal $\to$ Sat) | Gateway Downlink (Sat $\to$ Gateway) |
| :--- | :--- | :--- | :--- |
| **Frequency ($f$)** | $11.70\text{ GHz}$ (Ku) | $14.25\text{ GHz}$ (Ku) | $19.00\text{ GHz}$ (Ka) |
| **Slant Range ($d$)** | $750\text{ km}$ ($45^\circ$ elev.) | $750\text{ km}$ ($45^\circ$ elev.) | $750\text{ km}$ ($45^\circ$ elev.) |
| **Transmit Power ($P_{tx}$)** | $10.0\text{ dBW}$ ($10\text{ W}$) | $4.0\text{ dBW}$ ($2.5\text{ W}$) | $16.0\text{ dBW}$ ($40\text{ W}$) |
| **Transmit Antenna Gain ($G_{tx}$)**| $38.5\text{ dBi}$ | $34.0\text{ dBi}$ | $44.0\text{ dBi}$ |
| **EIRP** | **$48.5\text{ dBW}$** | **$38.0\text{ dBW}$** | **$60.0\text{ dBW}$** |
| **Free Space Path Loss ($FSPL$)** | $-171.3\text{ dB}$ | $-173.0\text{ dB}$ | $-175.5\text{ dB}$ |
| **Atmospheric Loss ($L_{atm}$)** | $-0.4\text{ dB}$ | $-0.6\text{ dB}$ | $-1.1\text{ dB}$ |
| **Rain Fade ($p=0.01\%$)** | $-2.8\text{ dB}$ | $-3.8\text{ dB}$ | $-7.2\text{ dB}$ |
| **Receive Antenna Gain ($G_{rx}$)**| $34.5\text{ dBi}$ | $36.0\text{ dBi}$ | $58.0\text{ dBi}$ |
| **System Noise Temp ($T_{sys}$)** | $220\text{ K}$ ($23.4\text{ dBK}$) | $450\text{ K}$ ($26.5\text{ dBK}$) | $180\text{ K}$ ($22.6\text{ dBK}$) |
| **Receiver $G/T$** | **$+11.1\text{ dB/K}$** | **$+9.5\text{ dB/K}$** | **$+35.4\text{ dB/K}$** |
| **Boltzmann Constant ($-k_B$)** | $+228.6\text{ dBW/(Hz}\cdot\text{K)}$ | $+228.6\text{ dBW/(Hz}\cdot\text{K)}$ | $+228.6\text{ dBW/(Hz}\cdot\text{K)}$ |
| **Received $C/N_0$** | **$113.7\text{ dB-Hz}$** | **$98.7\text{ dB-Hz}$** | **$140.2\text{ dB-Hz}$** |
| **Channel Bandwidth ($B$)** | $250\text{ MHz}$ ($84.0\text{ dB-Hz}$)| $60\text{ MHz}$ ($77.8\text{ dB-Hz}$) | $500\text{ MHz}$ ($87.0\text{ dB-Hz}$)|
| **Carrier-to-Noise Ratio ($SNR$)** | **$+29.7\text{ dB}$** | **$+20.9\text{ dB}$** | **$+53.2\text{ dB}$** |

### 3.2 Achievable Throughput via Shannon-Hartley Capacity
The theoretical maximum capacity for the Ku-band downlink channel under AWGN conditions is:

$$C = B \log_2(1 + SNR) = 250 \times 10^6 \times \log_2(1 + 10^{2.97}) \approx 250 \times 10^6 \times 9.87 \approx 2.46\text{ Gbps}$$

Using realistic DVB-S2X / 3GPP NR-adapted modulation and coding (MODCOD) with LDPC code rate $r = 5/6$ and 64QAM (spectral efficiency $\approx 4.8\text{ bits/s/Hz}$):

$$R_{practical} = 250\text{ MHz} \times 4.8\text{ bps/Hz} \approx 1.20\text{ Gbps (per beam)}$$

Divided across active user terminals within a single spot beam cell, each terminal reliably achieves **150 to 350 Mbps download**.

---

## 4. Modulation & Air Interface Efficiency

### 4.1 SC-FDMA (Uplink) vs SC-TDM (Downlink)
1. **Downlink (Satellite to Terminal):** Uses **Single-Carrier Time-Division Multiplexing (SC-TDM)**. The satellite's high-efficiency traveling wave tube or Gallium Nitride (GaN) solid-state power amplifier operates near saturation without severe intermodulation distortion because signal amplitude is relatively constant.
2. **Uplink (Terminal to Satellite):** Uses **Single-Carrier Frequency-Division Multiple Access (SC-FDMA)**. Standard OFDM has a high Peak-to-Average Power Ratio ($PAPR \approx 10-12\text{ dB}$), which would force the terminal's GaN power amplifier to back off power by $8\text{ dB}$, overheating the unit. SC-FDMA passes the data through a Discrete Fourier Transform (DFT) pre-coder, reducing PAPR to $<4\text{ dB}$ and maximizing terminal battery/thermal efficiency.

---

## 5. Summary Findings
- Starlink's AESA antennas utilize sub-array True Time Delay architectures to eliminate wideband beam squint across 250 MHz carriers.
- Doppler shifts of $\pm 325\text{ kHz}$ with acceleration rates of $-4.93\text{ kHz/s}$ are neutralized via orbit ephemeris feedforward aiding into 3rd-order tracking loops.
- Link budgets confirm a healthy $+29.7\text{ dB}$ downlink SNR in clear sky and $+20.9\text{ dB}$ uplink SNR, preserving high-order QAM throughput even during moderate rain fade.
