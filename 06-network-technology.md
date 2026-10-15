# 🌐 Network Technology — RF, Protocols & Software Architecture

This document covers the detailed technical networking aspects. See [03-solution-architecture.md](./03-solution-architecture.md) for the big picture, and [05-ground-segment.md](./05-ground-segment.md) for ground infrastructure.

---

## 1. Latency Deep Dive

```
LATENCY BUDGET BREAKDOWN (Residential user, 2025):
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Component                    Time (ms)
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  User terminal processing     : ~1ms
  Ku-band uplink (550 km)      : 1.8ms
  Satellite processing         : ~1ms
  Ka-band downlink to gateway  : 1.8ms
  Gateway processing           : ~1ms
  Fiber to destination         : ~5-15ms (depends on distance)
  Return path (same)           : ~12ms
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  TOTAL (round trip)           : ~25-35ms typical
  Best case (ISL routing)      : ~20ms
  Worst case (distant gateway) : ~60ms
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

## 2. Bandwidth Management

```
QOS & PRIORITY DATA SYSTEM:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Data tiers:
    Priority Data: 
      → Guaranteed bandwidth allocation
      → Not throttled during congestion  
      → Business/Priority plans include this
      
    Standard Data:
      → Best-effort during normal conditions
      → Deprioritized vs Priority users during peak
      → After priority cap hit, falls to standard
      
    Background Data:
      → Used by app for updates, non-time-critical
      → Lowest priority in queue
      
  Peak hours management:
    → Per-cell (geographic) congestion management
    → Standard users may see 20-50% speed reduction
    → Priority users maintain near-full speed
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

## 3. IPv4 vs IPv6

```
IP ADDRESSING ON STARLINK:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Default: CGNAT (Carrier-Grade NAT)
    → Shared IPv4 public addresses
    → 100.64.0.0/10 private range to terminal
    → Cannot host servers (no inbound connections)
    → Cannot use IPv4-based VPN server hosting
    
  IPv6: Supported and preferred
    → Native /56 IPv6 prefix per terminal
    → End-to-end reachability (no NAT)
    → Recommended for business use
    
  Static IP option:
    → Available on Business plans
    → Additional $5–10/month
    → Required for server hosting, remote access VPNs
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

---

*Previous: [Ground Segment ←](./05-ground-segment.md) | Next: [Security Risks →](./07-security-risks.md)*
