"""StarSec CVSS v4.0 Space Profile and SPARTA Risk Scoring Calculator.

This module implements the StarSec CVSS v4 Space Profile (STARSEC-CVSS4-SPACE-1.0).
It calculates the composite orbital risk score by combining the standard CVSS v4.0
Base Score with space-domain environmental modifiers: Orbital Conjunction Impact (OCI),
Life Safety Risk (LSR) for manned orbital habitats (ISS/Tiangong), Telemetry Blackout
Window (TBW), and De-orbit Hazard (DH).
"""

from dataclasses import dataclass
from enum import Enum
import argparse
import json
import math
import sys
from typing import Dict, List, Optional, Tuple


MIN_SCORE: float = 0.0
MAX_SCORE: float = 10.0

DELTA_OCI_NONE: float = 0.0
DELTA_OCI_LOW: float = 0.05
DELTA_OCI_CRITICAL: float = 0.25

DELTA_LSR_NONE: float = 0.0
DELTA_LSR_LOW: float = 0.10
DELTA_LSR_HIGH: float = 0.35

DELTA_TBW_NONE: float = 0.0
DELTA_TBW_TRANSIENT: float = 0.05
DELTA_TBW_PERSISTENT: float = 0.15

DELTA_DH_NONE: float = 0.0
DELTA_DH_CONTROLLED: float = 0.10
DELTA_DH_UNCONTROLLED: float = 0.30


class OrbitalConjunctionImpact(Enum):
    """Orbital conjunction collision probability category."""

    NONE = "N"
    LOW = "L"
    CRITICAL = "C"


class LifeSafetyRisk(Enum):
    """Risk to human life in manned orbital space stations."""

    NONE = "N"
    LOW = "L"
    HIGH = "H"


class TelemetryBlackoutWindow(Enum):
    """Duration of ground telemetry command link loss."""

    NONE = "N"
    TRANSIENT = "T"
    PERSISTENT = "P"


class DeorbitHazard(Enum):
    """Risk of premature or uncontrolled atmospheric re-entry."""

    NONE = "N"
    CONTROLLED = "C"
    UNCONTROLLED = "U"


@dataclass(frozen=True)
class SpaceVectorModifiers:
    """Space domain modifiers extending standard CVSS v4.0.

    Attributes:
        conjunction_impact: Collision probability level (None, Low, Critical).
        life_safety_risk: Proximity risk to manned habitats (None, Low, High).
        blackout_window: Ground telecommand loss window (None, Transient, Persistent).
        deorbit_hazard: Atmospheric re-entry severity (None, Controlled, Uncontrolled).
    """

    conjunction_impact: OrbitalConjunctionImpact
    life_safety_risk: LifeSafetyRisk
    blackout_window: TelemetryBlackoutWindow
    deorbit_hazard: DeorbitHazard

    def compute_space_multiplier(self) -> float:
        """Computes the multiplicative space modifier factor.

        Returns:
            Computed floating-point multiplier greater than or equal to 1.0.
        """
        multiplier: float = 1.0

        if self.conjunction_impact == OrbitalConjunctionImpact.LOW:
            multiplier += DELTA_OCI_LOW
        elif self.conjunction_impact == OrbitalConjunctionImpact.CRITICAL:
            multiplier += DELTA_OCI_CRITICAL

        if self.life_safety_risk == LifeSafetyRisk.LOW:
            multiplier += DELTA_LSR_LOW
        elif self.life_safety_risk == LifeSafetyRisk.HIGH:
            multiplier += DELTA_LSR_HIGH

        if self.blackout_window == TelemetryBlackoutWindow.TRANSIENT:
            multiplier += DELTA_TBW_TRANSIENT
        elif self.blackout_window == TelemetryBlackoutWindow.PERSISTENT:
            multiplier += DELTA_TBW_PERSISTENT

        if self.deorbit_hazard == DeorbitHazard.CONTROLLED:
            multiplier += DELTA_DH_CONTROLLED
        elif self.deorbit_hazard == DeorbitHazard.UNCONTROLLED:
            multiplier += DELTA_DH_UNCONTROLLED

        return multiplier


@dataclass(frozen=True)
class SpaceThreatVector:
    """Represents a cataloged constellation threat vector.

    Attributes:
        vector_id: Canonical StarSec identifier (e.g., TTC-ROGUE-001).
        name: Plain language name of the threat.
        sparta_id: Aerospace Corporation SPARTA technique ID.
        attck_id: MITRE ATT&CK for Space identifier.
        base_cvss_v4: Base CVSS v4.0 score in range [0.0, 10.0].
        modifiers: Space-domain environmental modifiers.
        remediation_rfc: Governing StarSec RFC standard.
    """

    vector_id: str
    name: str
    sparta_id: str
    attck_id: str
    base_cvss_v4: float
    modifiers: SpaceVectorModifiers
    remediation_rfc: str

    def calculate_space_score(self) -> float:
        """Calculates final composite CVSS v4 Space Profile score.

        Returns:
            Composite score clamped strictly within [0.0, 10.0].
        """
        raw_score: float = self.base_cvss_v4 * self.modifiers.compute_space_multiplier()
        return min(MAX_SCORE, max(MIN_SCORE, round(raw_score, 1)))

    @property
    def severity_label(self) -> str:
        """Returns qualitative severity label matching CVSS specification.

        Returns:
            Severity string: LOW, MEDIUM, HIGH, or CRITICAL.
        """
        score: float = self.calculate_space_score()
        if score >= 9.0:
            return "CRITICAL"
        if score >= 7.0:
            return "HIGH"
        if score >= 4.0:
            return "MEDIUM"
        return "LOW"


CONSTELLATION_CATALOG: Dict[str, SpaceThreatVector] = {
    "TTC-ROGUE-001": SpaceThreatVector(
        vector_id="TTC-ROGUE-001",
        name="Unilateral Rogue Atmospheric De-Orbit Command",
        sparta_id="IMP-0006",
        attck_id="T1485",
        base_cvss_v4=9.4,
        modifiers=SpaceVectorModifiers(
            conjunction_impact=OrbitalConjunctionImpact.CRITICAL,
            life_safety_risk=LifeSafetyRisk.HIGH,
            blackout_window=TelemetryBlackoutWindow.PERSISTENT,
            deorbit_hazard=DeorbitHazard.UNCONTROLLED,
        ),
        remediation_rfc="STARSEC-RFC-004",
    ),
    "UT-GLITCH-001": SpaceThreatVector(
        vector_id="UT-GLITCH-001",
        name="User Terminal Sub-10 ns Voltage Glitching",
        sparta_id="EX-0005",
        attck_id="T1542",
        base_cvss_v4=6.2,
        modifiers=SpaceVectorModifiers(
            conjunction_impact=OrbitalConjunctionImpact.NONE,
            life_safety_risk=LifeSafetyRisk.NONE,
            blackout_window=TelemetryBlackoutWindow.NONE,
            deorbit_hazard=DeorbitHazard.NONE,
        ),
        remediation_rfc="STARSEC-RFC-002",
    ),
    "RF-JAM-001": SpaceThreatVector(
        vector_id="RF-JAM-001",
        name="High-Power Ground Barrage Jamming on Uplink",
        sparta_id="EX-0001",
        attck_id="T1498",
        base_cvss_v4=7.5,
        modifiers=SpaceVectorModifiers(
            conjunction_impact=OrbitalConjunctionImpact.LOW,
            life_safety_risk=LifeSafetyRisk.NONE,
            blackout_window=TelemetryBlackoutWindow.TRANSIENT,
            deorbit_hazard=DeorbitHazard.NONE,
        ),
        remediation_rfc="STARSEC-RFC-003",
    ),
    "ISL-HNDL-001": SpaceThreatVector(
        vector_id="ISL-HNDL-001",
        name="Harvest-Now-Decrypt-Later Inter-Satellite Optical Intercept",
        sparta_id="REC-0005",
        attck_id="T1557",
        base_cvss_v4=8.1,
        modifiers=SpaceVectorModifiers(
            conjunction_impact=OrbitalConjunctionImpact.NONE,
            life_safety_risk=LifeSafetyRisk.NONE,
            blackout_window=TelemetryBlackoutWindow.NONE,
            deorbit_hazard=DeorbitHazard.NONE,
        ),
        remediation_rfc="STARSEC-RFC-001",
    ),
    "GEO-KILL-001": SpaceThreatVector(
        vector_id="GEO-KILL-001",
        name="Unilateral Sovereign Regional Kill-Switch Injection",
        sparta_id="IMP-0008",
        attck_id="T1489",
        base_cvss_v4=8.8,
        modifiers=SpaceVectorModifiers(
            conjunction_impact=OrbitalConjunctionImpact.LOW,
            life_safety_risk=LifeSafetyRisk.NONE,
            blackout_window=TelemetryBlackoutWindow.PERSISTENT,
            deorbit_hazard=DeorbitHazard.NONE,
        ),
        remediation_rfc="STARSEC-RFC-004",
    ),
}


def evaluate_custom_space_vector(
    base_score: float,
    oci: str,
    lsr: str,
    tbw: str,
    dh: str,
) -> Tuple[float, float, str]:
    """Calculates space score for ad-hoc user parameters.

    Args:
        base_score: Base CVSS score between 0.0 and 10.0.
        oci: Orbital Conjunction Impact ('N', 'L', 'C').
        lsr: Life Safety Risk ('N', 'L', 'H').
        tbw: Telemetry Blackout Window ('N', 'T', 'P').
        dh: De-orbit Hazard ('N', 'C', 'U').

    Returns:
        Tuple of (multiplier, final_score, severity_label).

    Raises:
        ValueError: If base_score is out of bounds or parameter strings are invalid.
    """
    if not (MIN_SCORE <= base_score <= MAX_SCORE):
        raise ValueError(f"Base score must be between 0.0 and 10.0, received {base_score}")

    modifiers = SpaceVectorModifiers(
        conjunction_impact=OrbitalConjunctionImpact(oci.upper()),
        life_safety_risk=LifeSafetyRisk(lsr.upper()),
        blackout_window=TelemetryBlackoutWindow(tbw.upper()),
        deorbit_hazard=DeorbitHazard(dh.upper()),
    )
    multiplier: float = modifiers.compute_space_multiplier()
    final_score: float = min(MAX_SCORE, max(MIN_SCORE, round(base_score * multiplier, 1)))

    if final_score >= 9.0:
        label = "CRITICAL"
    elif final_score >= 7.0:
        label = "HIGH"
    elif final_score >= 4.0:
        label = "MEDIUM"
    else:
        label = "LOW"

    return multiplier, final_score, label


def format_vector_report(vector: SpaceThreatVector) -> str:
    """Formats a detailed terminal report for a threat vector.

    Args:
        vector: SpaceThreatVector instance to format.

    Returns:
        Formatted multi-line report string.
    """
    score: float = vector.calculate_space_score()
    mult: float = vector.modifiers.compute_space_multiplier()
    lines = [
        "================================================================================",
        f"  STARSEC CVSS v4.0 SPACE PROFILE REPORT: {vector.vector_id}",
        "================================================================================",
        f"  Threat Name            : {vector.name}",
        f"  SPARTA ID              : {vector.sparta_id}",
        f"  MITRE ATT&CK for Space : {vector.attck_id}",
        f"  Mandatory Control RFC  : {vector.remediation_rfc}",
        "--------------------------------------------------------------------------------",
        f"  Base CVSS v4.0 Score   : {vector.base_cvss_v4:.1f} / 10.0",
        f"  Space Multiplier Factor: {mult:.2f}x",
        f"  - Conjunction Impact   : {vector.modifiers.conjunction_impact.name} ({vector.modifiers.conjunction_impact.value})",
        f"  - Life Safety Risk     : {vector.modifiers.life_safety_risk.name} ({vector.modifiers.life_safety_risk.value})",
        f"  - Telemetry Blackout   : {vector.modifiers.blackout_window.name} ({vector.modifiers.blackout_window.value})",
        f"  - De-orbit Hazard      : {vector.modifiers.deorbit_hazard.name} ({vector.modifiers.deorbit_hazard.value})",
        "--------------------------------------------------------------------------------",
        f"  FINAL SPACE CVSS SCORE : {score:.1f} / 10.0",
        f"  SEVERITY CLASSIFICATION: {vector.severity_label}",
        "================================================================================",
    ]
    return "\n".join(lines)


def export_json_report(vector: SpaceThreatVector) -> str:
    """Exports threat vector metrics in structured JSON format.

    Args:
        vector: SpaceThreatVector to serialize.

    Returns:
        Indented JSON string representation.
    """
    payload = {
        "vector_id": vector.vector_id,
        "name": vector.name,
        "sparta_id": vector.sparta_id,
        "attck_id": vector.attck_id,
        "base_cvss_v4": vector.base_cvss_v4,
        "space_multiplier": round(vector.modifiers.compute_space_multiplier(), 3),
        "final_space_score": vector.calculate_space_score(),
        "severity": vector.severity_label,
        "remediation_rfc": vector.remediation_rfc,
        "modifiers": {
            "orbital_conjunction_impact": vector.modifiers.conjunction_impact.value,
            "life_safety_risk": vector.modifiers.life_safety_risk.value,
            "telemetry_blackout_window": vector.modifiers.blackout_window.value,
            "deorbit_hazard": vector.modifiers.deorbit_hazard.value,
        },
    }
    return json.dumps(payload, indent=2)


def main() -> None:
    """Entry point for the threat score calculator CLI."""
    parser = argparse.ArgumentParser(
        description="StarSec CVSS v4.0 Space Profile and SPARTA Risk Calculator"
    )
    parser.add_argument(
        "--vector",
        type=str,
        default=None,
        help="Threat vector ID to evaluate (e.g., TTC-ROGUE-001)",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List all cataloged space threat vectors",
    )
    parser.add_argument(
        "--demo",
        action="store_true",
        help="Run comprehensive demonstration of all vectors",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output results in JSON format",
    )

    args = parser.parse_args()

    if args.list:
        print("\nCataloged StarSec Space Threat Vectors:")
        for vid, v in CONSTELLATION_CATALOG.items():
            print(f"  [{vid}] {v.name} (Base: {v.base_cvss_v4}, SPARTA: {v.sparta_id})")
        return

    if args.demo or (not args.vector and not args.list):
        for vid, v in CONSTELLATION_CATALOG.items():
            if args.json:
                print(export_json_report(v))
            else:
                print(format_vector_report(v))
                print()
        return

    if args.vector:
        v_upper = args.vector.upper()
        if v_upper not in CONSTELLATION_CATALOG:
            print(f"Error: Unknown vector '{args.vector}'. Use --list to see available vectors.")
            sys.exit(1)

        v = CONSTELLATION_CATALOG[v_upper]
        if args.json:
            print(export_json_report(v))
        else:
            print(format_vector_report(v))


if __name__ == "__main__":
    main()
