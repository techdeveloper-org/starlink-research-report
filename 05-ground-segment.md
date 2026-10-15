# 📡 Ground Segment & Network Technology — Deep Dive

---

## Ground Segment

### 1. Gateway Earth Stations

```
GATEWAY INFRASTRUCTURE:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Global gateway count     : 100+ major facilities
  USA gateways            : 100+
  Typical location        : Rural areas near fiber trunk lines
  
  Each gateway contains:
    → Multiple large Ka-band dish antennas (in radomes)
    → High-power amplifiers for uplink
    → Low-noise amplifiers for downlink
    → Core routing hardware (border routers)
    → Fiber connections to internet exchange points
    → Redundant power (diesel generators + UPS)
    → 24/7 monitoring systems
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

### 2. Points of Presence (PoPs)

```
STARLINK NETWORK ARCHITECTURE:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Gateway → PoP → Internet Exchange → Global Internet
  
  At PoP level:
    → CGNAT (Carrier-Grade NAT) for IPv4 management
    → BGP routing to internet backbone
    → DDoS mitigation and firewall
    → Traffic shaping and QoS policies
    → IPv6 dual-stack support
    → Peering agreements with major CDNs
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

---

## Network Technology

### 1. RF Frequency Details

```
COMPLETE STARLINK FREQUENCY PLAN:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  USER LINK (Terminal ↔ Satellite):
    Downlink (space→earth): 10.7–12.7 GHz (Ku-band)
    Uplink (earth→space)  : 14.0–14.5 GHz (Ku-band)
    
  FEEDER LINK (Gateway ↔ Satellite):  
    Downlink (space→earth): 17.8–20.2 GHz (Ka-band)
    Uplink (earth→space)  : 27.5–30.0 GHz (Ka-band)
    
  HIGH CAPACITY (V2 Mini and V3):
    V-band downlink       : 37.5–42.0 GHz
    V-band uplink         : 47.2–51.4 GHz
    
  INTER-SATELLITE LINKS (ISL):
    Wavelength            : ~1,550 nm (infrared optical)
    Modulation            : DPSK or coherent QAM
    Estimated rate        : 100+ Gbps per link pair
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

### 2. Modulation & Access Schemes

```
AIR INTERFACE TECHNOLOGY:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Uplink (terminal → satellite):
    Scheme    : SC-FDMA (Single Carrier FDMA)
    Why       : Lower peak-to-average power ratio
                → More power-efficient for user terminal
                → Smaller battery/amplifier needed
                
  Downlink (satellite → terminal):
    Scheme    : SC-TDM (Single Carrier TDM)
    Why       : Efficient for satellite broadcast
    
  Error correction: LDPC codes + pilot-based channel estimation
  
  Adaptive modulation:
    → Satellite continuously measures link quality
    → Switches between QPSK, 8PSK, 16QAM, 64QAM
    → Higher order modulation in clear weather
    → Falls back to QPSK in rain or obstruction
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

### 3. Beam Scheduling

```
BEAM ARCHITECTURE:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Each Starlink satellite creates multiple spot beams:
    → V1.0: ~8 independent beams
    → V2 Mini: ~20+ independent beams (advanced arrays)
    → V3: Estimated 50+ beams
    
  Beam scheduling determines:
    → Which users served in each beam at what time
    → Frequency reuse between non-adjacent beams
    → Power allocation based on demand
    → Handoff coordination with adjacent satellites
    
  Frequency reuse:
    Colors in satellite world = different frequency blocks
    Non-adjacent beams use same frequencies safely
    Maximizes spectral efficiency
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

---

*Previous: [Satellite Constellation ←](./04-satellite-constellation.md) | Next: [Security Risks →](./07-security-risks.md)*
