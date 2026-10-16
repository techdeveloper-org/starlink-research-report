"""Shamir (t, n) Threshold Cryptography and Multi-Party Telecommand Quorum Simulator.

This module models the on-orbit telecommand verification architecture defined in
StarSec RFC-004 (STARSEC-RFC-004). It implements Shamir's Secret Sharing over a 256-bit
prime finite field F_p, validates flight-critical orbital maneuvers against multi-party
threshold quorums, rejects unauthorized single-operator de-orbit attempts, and permits
autonomous safety collision avoidance burns without ground latency.
"""

from dataclasses import dataclass
from enum import Enum
import argparse
import hashlib
import json
import secrets
from typing import Dict, List, Optional, Set, Tuple


PRIME_256: int = (1 << 256) - 189

MAGIC_HEADER_QUORUM: int = 0x5155
PROTOCOL_VERSION_V1: int = 0x01

DEFAULT_THRESHOLD_T: int = 3
DEFAULT_SIGNERS_N: int = 5


class CommandCriticality(Enum):
    """Categorization of satellite telecommand impact."""

    CLASS_A_AUTONOMOUS = "CLASS_A"
    CLASS_B_ROUTINE = "CLASS_B"
    CLASS_C_FLIGHT_CRITICAL = "CLASS_C"


class QuorumVerificationError(Exception):
    """Raised when telecommand cryptographic threshold verification fails."""


@dataclass(frozen=True)
class SecretShare:
    """Represents a private Shamir polynomial evaluation share (x, f(x)).

    Attributes:
        index_x: Non-zero integer participant identity (1 <= x < p).
        value_y: Polynomial evaluation point modulo p.
    """

    index_x: int
    value_y: int


@dataclass(frozen=True)
class TelecommandPayload:
    """Payload definition for an orbital command frame.

    Attributes:
        command_id: Unique string identifier for command execution.
        target_satellite_id: 32-bit constellation spacecraft ID.
        criticality: Criticality class (Class A, B, or C).
        action_name: Descriptive name of maneuver or software update.
        delta_v_vector_mps: Three-axis velocity change vector in m/s (vx, vy, vz).
    """

    command_id: str
    target_satellite_id: int
    criticality: CommandCriticality
    action_name: str
    delta_v_vector_mps: Tuple[float, float, float]

    def compute_command_hash(self) -> int:
        """Computes 256-bit cryptographic digest of the telecommand frame.

        Returns:
            Integer digest reduced modulo p.
        """
        serialized = (
            f"{self.command_id}:{self.target_satellite_id}:"
            f"{self.criticality.value}:{self.action_name}:"
            f"{self.delta_v_vector_mps}"
        ).encode("utf-8")
        raw_digest = hashlib.sha256(serialized).digest()
        return int.from_bytes(raw_digest, byteorder="big") % PRIME_256


@dataclass(frozen=True)
class QuorumVerificationResult:
    """Outcome of on-orbit telecommand cryptographic verification.

    Attributes:
        command_id: Evaluated command identifier.
        is_authorized: Boolean flag indicating if maneuver is permitted.
        criticality: Evaluated command criticality class.
        presented_shares_count: Number of distinct signature shares supplied.
        required_threshold: Threshold t required for approval.
        reconstructed_secret_hash: SHA-256 digest of reconstructed quorum secret.
        rejection_reason: Descriptive explanation if authorization failed.
    """

    command_id: str
    is_authorized: bool
    criticality: CommandCriticality
    presented_shares_count: int
    required_threshold: int
    reconstructed_secret_hash: Optional[str]
    rejection_reason: Optional[str]


class ShamirThresholdEngine:
    """Implements polynomial secret sharing and Lagrange interpolation over F_p."""

    def __init__(self, prime: int = PRIME_256) -> None:
        """Initializes the engine with the governing prime finite field modulus.

        Args:
            prime: Large prime modulus p.
        """
        self.prime: int = prime

    def generate_polynomial_shares(
        self,
        secret: int,
        threshold_t: int,
        signers_n: int,
    ) -> List[SecretShare]:
        """Splits a master secret into n distinct polynomial shares.

        Args:
            secret: Secret integer to be shared (0 <= secret < p).
            threshold_t: Minimum shares required to reconstruct (t <= n).
            signers_n: Total number of shares to generate.

        Returns:
            List of n SecretShare instances.

        Raises:
            ValueError: If threshold_t > signers_n or threshold_t < 1.
        """
        if threshold_t < 1 or threshold_t > signers_n:
            raise ValueError(f"Invalid threshold parameters: t={threshold_t}, n={signers_n}")

        coefficients: List[int] = [secret % self.prime]
        for _ in range(threshold_t - 1):
            coefficients.append(secrets.randbelow(self.prime))

        shares: List[SecretShare] = []
        for x in range(1, signers_n + 1):
            y = 0
            x_pow = 1
            for coeff in coefficients:
                y = (y + coeff * x_pow) % self.prime
                x_pow = (x_pow * x) % self.prime
            shares.append(SecretShare(index_x=x, value_y=y))

        return shares

    def reconstruct_secret_at_zero(self, shares: List[SecretShare]) -> int:
        """Reconstructs polynomial value f(0) using Lagrange interpolation.

        Args:
            shares: List of at least t distinct SecretShare instances.

        Returns:
            Reconstructed integer secret modulo p.

        Raises:
            ValueError: If duplicate x coordinates are provided or list is empty.
        """
        if not shares:
            raise ValueError("Cannot reconstruct secret from empty share list")

        distinct_x: Set[int] = {s.index_x for s in shares}
        if len(distinct_x) != len(shares):
            raise ValueError("Duplicate participant indices detected in share set")

        secret_f0: int = 0
        k = len(shares)

        for i in range(k):
            x_i = shares[i].index_x
            y_i = shares[i].value_y

            numerator = 1
            denominator = 1

            for j in range(k):
                if i == j:
                    continue
                x_j = shares[j].index_x
                numerator = (numerator * (-x_j)) % self.prime
                denominator = (denominator * (x_i - x_j)) % self.prime

            lagrange_weight = (numerator * pow(denominator, self.prime - 2, self.prime)) % self.prime
            secret_f0 = (secret_f0 + y_i * lagrange_weight) % self.prime

        return secret_f0


class SatelliteFlightComputer:
    """Models on-orbit satellite flight computer telecommand validation logic."""

    def __init__(
        self,
        satellite_id: int,
        authorized_quorum_secret_hash: str,
        threshold_t: int = DEFAULT_THRESHOLD_T,
        prime: int = PRIME_256,
    ) -> None:
        """Initializes satellite flight computer security state.

        Args:
            satellite_id: Spacecraft constellation identifier.
            authorized_quorum_secret_hash: Expected SHA-256 hash of valid secret.
            threshold_t: Required quorum threshold for Class C commands.
            prime: Finite field prime modulus.
        """
        self.satellite_id: int = satellite_id
        self.expected_hash: str = authorized_quorum_secret_hash
        self.threshold_t: int = threshold_t
        self.engine: ShamirThresholdEngine = ShamirThresholdEngine(prime=prime)

    def verify_and_execute_command(
        self,
        command: TelecommandPayload,
        shares: List[SecretShare],
    ) -> QuorumVerificationResult:
        """Validates incoming command frame and authorizes thruster/avionics execution.

        Args:
            command: TelecommandPayload containing action and velocity vector.
            shares: List of signature shares presented with the command frame.

        Returns:
            QuorumVerificationResult summarizing validation outcome.
        """
        if command.target_satellite_id != self.satellite_id:
            return QuorumVerificationResult(
                command_id=command.command_id,
                is_authorized=False,
                criticality=command.criticality,
                presented_shares_count=len(shares),
                required_threshold=self.threshold_t,
                reconstructed_secret_hash=None,
                rejection_reason=f"Target mismatch: satellite {self.satellite_id} != command {command.target_satellite_id}",
            )

        if command.criticality == CommandCriticality.CLASS_A_AUTONOMOUS:
            return QuorumVerificationResult(
                command_id=command.command_id,
                is_authorized=True,
                criticality=command.criticality,
                presented_shares_count=len(shares),
                required_threshold=0,
                reconstructed_secret_hash=None,
                rejection_reason=None,
            )

        if command.criticality == CommandCriticality.CLASS_B_ROUTINE:
            if len(shares) >= 1:
                return QuorumVerificationResult(
                    command_id=command.command_id,
                    is_authorized=True,
                    criticality=command.criticality,
                    presented_shares_count=len(shares),
                    required_threshold=1,
                    reconstructed_secret_hash=None,
                    rejection_reason=None,
                )
            return QuorumVerificationResult(
                command_id=command.command_id,
                is_authorized=False,
                criticality=command.criticality,
                presented_shares_count=0,
                required_threshold=1,
                reconstructed_secret_hash=None,
                rejection_reason="Routine Class B command requires at least 1 ground signature",
            )

        if len(shares) < self.threshold_t:
            return QuorumVerificationResult(
                command_id=command.command_id,
                is_authorized=False,
                criticality=command.criticality,
                presented_shares_count=len(shares),
                required_threshold=self.threshold_t,
                reconstructed_secret_hash=None,
                rejection_reason=(
                    f"Sub-threshold presentation: received {len(shares)} shares, "
                    f"strictly requires t={self.threshold_t}"
                ),
            )

        try:
            reconstructed_secret = self.engine.reconstruct_secret_at_zero(shares[: self.threshold_t])
            reconstructed_bytes = reconstructed_secret.to_bytes(32, byteorder="big")
            reconstructed_hash = hashlib.sha256(reconstructed_bytes).hexdigest()

            if reconstructed_hash != self.expected_hash:
                return QuorumVerificationResult(
                    command_id=command.command_id,
                    is_authorized=False,
                    criticality=command.criticality,
                    presented_shares_count=len(shares),
                    required_threshold=self.threshold_t,
                    reconstructed_secret_hash=reconstructed_hash,
                    rejection_reason="Cryptographic signature mismatch: invalid or corrupted shares",
                )

            return QuorumVerificationResult(
                command_id=command.command_id,
                is_authorized=True,
                criticality=command.criticality,
                presented_shares_count=len(shares),
                required_threshold=self.threshold_t,
                reconstructed_secret_hash=reconstructed_hash,
                rejection_reason=None,
            )

        except Exception as err:
            return QuorumVerificationResult(
                command_id=command.command_id,
                is_authorized=False,
                criticality=command.criticality,
                presented_shares_count=len(shares),
                required_threshold=self.threshold_t,
                reconstructed_secret_hash=None,
                rejection_reason=f"Verification failure exception: {str(err)}",
            )


def run_quorum_demonstration() -> None:
    """Executes an end-to-end multi-party telecommand quorum demonstration."""
    print("================================================================================")
    print("  STARSEC RFC-004 MULTI-PARTY TELECOMMAND QUORUM DEMONSTRATION")
    print("================================================================================")

    master_secret: int = secrets.randbelow(PRIME_256)
    secret_bytes: bytes = master_secret.to_bytes(32, byteorder="big")
    expected_hash: str = hashlib.sha256(secret_bytes).hexdigest()

    threshold_t: int = 3
    signers_n: int = 5
    sat_id: int = 1042

    engine = ShamirThresholdEngine()
    shares = engine.generate_polynomial_shares(
        secret=master_secret,
        threshold_t=threshold_t,
        signers_n=signers_n,
    )

    flight_computer = SatelliteFlightComputer(
        satellite_id=sat_id,
        authorized_quorum_secret_hash=expected_hash,
        threshold_t=threshold_t,
    )

    print(f"Spacecraft ID             : SAT-{sat_id}")
    print(f"Quorum Architecture       : ({threshold_t}, {signers_n}) Threshold over F_(2^256 - 189)")
    print(f"Authorization Master Hash : {expected_hash[:16]}...{expected_hash[-16:]}")
    print("Generated 5 Private Ground Shares:")
    for s in shares:
        print(f"  - Entity {s.index_x}: KeyShare=0x{s.value_y:064x}...")
    print("--------------------------------------------------------------------------------")

    cmd_deorbit = TelecommandPayload(
        command_id="CMD-DEORBIT-9901",
        target_satellite_id=sat_id,
        criticality=CommandCriticality.CLASS_C_FLIGHT_CRITICAL,
        action_name="Atmospheric Re-entry Burn",
        delta_v_vector_mps=(-25.0, 0.0, -10.0),
    )

    print("\n[SCENARIO 1] Single Rogue Ground Operator (Compromised 1 Share):")
    res1 = flight_computer.verify_and_execute_command(cmd_deorbit, shares[:1])
    print(f"  Command ID      : {res1.command_id}")
    print(f"  Authorized      : {res1.is_authorized}")
    print(f"  Rejection Reason: {res1.rejection_reason}")

    print("\n[SCENARIO 2] Collusion of 2 Compromised Nodes (Sub-threshold t-1):")
    res2 = flight_computer.verify_and_execute_command(cmd_deorbit, shares[:2])
    print(f"  Command ID      : {res2.command_id}")
    print(f"  Authorized      : {res2.is_authorized}")
    print(f"  Rejection Reason: {res2.rejection_reason}")

    print("\n[SCENARIO 3] Valid Multi-Party Quorum (3 Authorized Entities):")
    res3 = flight_computer.verify_and_execute_command(cmd_deorbit, [shares[0], shares[2], shares[4]])
    print(f"  Command ID      : {res3.command_id}")
    print(f"  Authorized      : {res3.is_authorized}")
    print(f"  Quorum Hash     : {res3.reconstructed_secret_hash[:16]}...")
    print(f"  Maneuver Status : PASS - ARMING PLASMA THRUSTERS FOR DE-ORBIT BURN")

    print("\n[SCENARIO 4] Autonomous Collision Avoidance Burn (Class A):")
    cmd_collision = TelecommandPayload(
        command_id="CMD-AVOID-0042",
        target_satellite_id=sat_id,
        criticality=CommandCriticality.CLASS_A_AUTONOMOUS,
        action_name="Debris Conjunction Avoidance Burn",
        delta_v_vector_mps=(0.15, 0.05, 0.0),
    )
    res4 = flight_computer.verify_and_execute_command(cmd_collision, [])
    print(f"  Command ID      : {res4.command_id}")
    print(f"  Authorized      : {res4.is_authorized}")
    print(f"  Maneuver Status : PASS - AUTONOMOUS REACTION WHEEL/THRUSTER SLEW (Zero Ground Latency)")
    print("================================================================================")


def main() -> None:
    """Command-line interface entry point."""
    parser = argparse.ArgumentParser(
        description="StarSec RFC-004 Multi-Party Telecommand Quorum Simulator"
    )
    parser.add_argument(
        "--demo",
        action="store_true",
        help="Run comprehensive telecommand threshold demonstration",
    )
    args = parser.parse_args()
    run_quorum_demonstration()


if __name__ == "__main__":
    main()
