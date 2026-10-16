"""Comprehensive unit test harness for the Starlink & StarSec simulation suite.

This module executes automated unit test assertions across all scientific simulation modules:
1. orbit_sgp4_propagator: Keplerian period, velocity, J2 nodal precession, and Foster Pc.
2. phased_array_beamforming: 2D array factor, broadside gain, and Capon MVDR null depth.
3. rf_link_budget_calculator: EIRP, FSPL, received power, G/T, and positive operating margins.
4. pqc_kem_benchmark: ML-KEM polynomial ring operations, byte lengths, and key agreement.
5. stride_fault_injection_sim: CMOS delay expansion, timing violations, and crowbar clamp trips.
6. threat_score_calculator: CVSS v4 Space Profile calculations, SPARTA mappings, and bounds.
7. mpc_quorum_telecommand: Shamir (t, n) threshold secret sharing and telecommand verification.
"""

import hashlib
import math
import secrets
import unittest
import numpy as np

from simulations.orbit_sgp4_propagator import (
    OrbitalElements,
    StarlinkOrbitPropagator,
    ConjunctionGeometry,
    FosterCollisionProbabilitySolver,
    RADIUS_EARTH_KM,
)
from simulations.phased_array_beamforming import (
    PhasedArrayGeometry,
    PhasedArrayBeamformer,
    JammerEmitter,
)
from simulations.rf_link_budget_calculator import (
    LinkParameters,
    RFLinkBudgetCalculator,
)
from simulations.pqc_kem_benchmark import (
    PQCParameters,
    MLKEMSimulator,
)
from simulations.stride_fault_injection_sim import (
    ProcessorSiliconParameters,
    GlitchTransientProfile,
    DefenseConfiguration,
    VoltageGlitchSimulator,
)
from simulations.threat_score_calculator import (
    CONSTELLATION_CATALOG,
    OrbitalConjunctionImpact,
    LifeSafetyRisk,
    TelemetryBlackoutWindow,
    DeorbitHazard,
    SpaceVectorModifiers,
    evaluate_custom_space_vector,
)
from simulations.mpc_quorum_telecommand import (
    PRIME_256,
    CommandCriticality,
    SecretShare,
    TelecommandPayload,
    ShamirThresholdEngine,
    SatelliteFlightComputer,
)


class TestStarlinkOrbitPropagator(unittest.TestCase):
    """Test suite for orbital mechanics and Foster collision probability."""

    def setUp(self) -> None:
        """Sets up baseline Shell 1 orbital elements."""
        self.elements = OrbitalElements(
            semi_major_axis_km=RADIUS_EARTH_KM + 550.0,
            eccentricity=0.0001,
            inclination_deg=53.0,
            raan_deg=0.0,
            arg_perigee_deg=0.0,
            true_anomaly_deg=0.0,
        )
        self.propagator = StarlinkOrbitPropagator(self.elements)

    def test_orbital_period(self) -> None:
        """Validates that orbital period at 550 km is approximately 95.6 minutes."""
        period_min = self.propagator.compute_orbital_period_seconds() / 60.0
        self.assertAlmostEqual(period_min, 95.65, delta=0.5)

    def test_orbital_velocity(self) -> None:
        """Validates circular orbital velocity at 550 km is ~7.59 km/s."""
        velocity_km_s = self.propagator.compute_orbital_velocity_km_s()
        self.assertAlmostEqual(velocity_km_s, 7.586, delta=0.05)

    def test_j2_nodal_precession(self) -> None:
        """Validates secular nodal regression rate is negative and between -4.0 and -5.0 deg/day."""
        rate = self.propagator.compute_j2_nodal_precession_rate_deg_per_day()
        self.assertLess(rate, 0.0)
        self.assertAlmostEqual(rate, -4.489, delta=0.3)

    def test_foster_collision_probability(self) -> None:
        """Validates Foster collision integral returns a valid probability in [0, 1]."""
        conj = ConjunctionGeometry(
            miss_distance_km=0.085,
            sigma_x_km=0.120,
            sigma_y_km=0.045,
            correlation_rho=0.0,
            combined_radius_m=12.5,
        )
        pc = FosterCollisionProbabilitySolver.compute_collision_probability(conj)
        self.assertGreater(pc, 0.0)
        self.assertLess(pc, 1.0)
        self.assertGreater(pc, 1.0e-3)


class TestPhasedArrayBeamforming(unittest.TestCase):
    """Test suite for AESA 2D Array Factor and Capon MVDR spatial nulling."""

    def setUp(self) -> None:
        """Sets up 16x16 array geometry at 12 GHz for fast, robust unit test evaluation."""
        freq_hz = 12.0e9
        wavelength_m = 299792458.0 / freq_hz
        self.geometry = PhasedArrayGeometry(
            num_elements_x=16,
            num_elements_y=16,
            element_spacing_x_m=0.5 * wavelength_m,
            element_spacing_y_m=0.5 * wavelength_m,
            carrier_frequency_hz=freq_hz,
        )
        self.beamformer = PhasedArrayBeamformer(self.geometry)

    def test_boresight_gain(self) -> None:
        """Validates that broadside steering yields peak theoretical gain 10*log10(N)."""
        w_broadside = self.beamformer.compute_steering_vector(0.0, 0.0)
        gain = self.beamformer.evaluate_array_factor_dbi(w_broadside, 0.0, 0.0)
        expected_gain = 10.0 * math.log10(256)
        self.assertAlmostEqual(gain, expected_gain, delta=0.05)

    def test_capon_mvdr_null_depth(self) -> None:
        """Validates that Capon MVDR synthesizes >45 dB spatial suppression on jammer AoA."""
        target_theta, target_phi = 30.0, 45.0
        jammer = JammerEmitter(theta_deg=10.0, phi_deg=70.0, power_watts=1e-3)

        w_unadapted = self.beamformer.compute_steering_vector(target_theta, target_phi)
        unadapted_jammer_gain = self.beamformer.evaluate_array_factor_dbi(
            w_unadapted, jammer.theta_deg, jammer.phi_deg
        )

        w_mvdr, _ = self.beamformer.synthesize_capon_mvdr_weights(
            target_theta_deg=target_theta,
            target_phi_deg=target_phi,
            jammers=[jammer],
        )

        null_gain = self.beamformer.evaluate_array_factor_dbi(
            w_mvdr, jammer.theta_deg, jammer.phi_deg
        )
        suppression_db = unadapted_jammer_gain - null_gain
        self.assertGreaterEqual(suppression_db, 45.0)


class TestRFLinkBudgetCalculator(unittest.TestCase):
    """Test suite for space-to-ground decibel link budgets."""

    def test_ku_downlink_closure(self) -> None:
        """Validates that Starlink Ku-band downlink closes with >10 dB margin."""
        params = LinkParameters(
            link_name="Test Downlink",
            carrier_frequency_ghz=12.0,
            bandwidth_mhz=250.0,
            data_rate_mbps=220.0,
            slant_range_km=750.0,
            tx_power_watts=4.0,
            tx_antenna_gain_dbi=35.5,
            tx_cable_loss_db=0.8,
            tx_pointing_loss_db=0.5,
            rx_antenna_gain_dbi=34.2,
            rx_pointing_loss_db=0.3,
            rx_noise_figure_db=1.4,
            antenna_noise_temp_k=65.0,
            atmospheric_loss_db=0.45,
            rain_attenuation_db=1.20,
            required_eb_n0_db=6.5,
        )
        res = RFLinkBudgetCalculator.compute_link_budget(params)
        self.assertGreater(res.snr_margin_db, 10.0)
        self.assertGreater(res.c_over_n0_db_hz, 100.0)
        self.assertAlmostEqual(res.fspl_db, 171.53, delta=0.5)


class TestPQCKEMBenchmark(unittest.TestCase):
    """Test suite for NIST FIPS 203 ML-KEM polynomial ring simulation."""

    def test_shared_secret_agreement(self) -> None:
        """Validates that simulated Alice and Bob arrive at identical 256-bit shared secrets."""
        cfg = PQCParameters(
            name="Test ML-KEM-1024",
            k_dimension=4,
            eta1=2,
            eta2=2,
            du=11,
            dv=5,
            public_key_bytes=1568,
            ciphertext_bytes=1568,
            shared_secret_bytes=32,
            nist_security_level=5,
            quantum_bit_security=256,
        )
        sim = MLKEMSimulator(cfg)
        k_a, k_b, ciphertext, _ = sim.execute_handshake_simulation()
        self.assertEqual(k_a, k_b)
        self.assertEqual(len(k_a), 32)
        self.assertEqual(len(ciphertext), 1568)


class TestVoltageGlitchSimulator(unittest.TestCase):
    """Test suite for BootROM voltage glitching and crowbar clamp simulation."""

    def setUp(self) -> None:
        """Sets up silicon and glitch parameters."""
        self.silicon = ProcessorSiliconParameters(
            nominal_vcore_volts=0.85,
            threshold_voltage_volts=0.35,
            alpha_power_exponent=1.3,
            clock_frequency_ghz=1.2,
            nominal_prop_delay_ps=550.0,
            flip_flop_setup_time_ps=150.0,
        )
        self.glitch = GlitchTransientProfile(
            glitch_voltage_volts=0.42,
            glitch_duration_ns=6.5,
            glitch_fall_time_ps=180.0,
            injection_target_instruction="CMP R0, #0",
        )

    def test_unhardened_vulnerability(self) -> None:
        """Validates that unhardened silicon experiences setup violation under 0.42V glitch."""
        legacy = DefenseConfiguration(
            has_analog_crowbar=False,
            crowbar_reaction_time_ps=0.0,
            crowbar_voltage_threshold_mv=0.0,
            has_dual_core_lockstep=False,
            dcls_phase_delay_cycles=0,
            has_sram_puf=False,
        )
        sim = VoltageGlitchSimulator(self.silicon)
        res = sim.evaluate_timing_fault(self.glitch, legacy)
        self.assertTrue(res["setup_violation"])
        self.assertTrue(res["exploit_successful"])

    def test_crowbar_clamp_neutralization(self) -> None:
        """Validates that analog crowbar trips and neutralizes the exploit."""
        ztsa = DefenseConfiguration(
            has_analog_crowbar=True,
            crowbar_reaction_time_ps=250.0,
            crowbar_voltage_threshold_mv=35.0,
            has_dual_core_lockstep=True,
            dcls_phase_delay_cycles=2,
            has_sram_puf=True,
        )
        sim = VoltageGlitchSimulator(self.silicon)
        res = sim.evaluate_timing_fault(self.glitch, ztsa)
        self.assertTrue(res["crowbar_triggered"])
        self.assertFalse(res["exploit_successful"])


class TestThreatScoreCalculator(unittest.TestCase):
    """Triad test suite (Positive, Negative, Edge-Case) for Space CVSS v4 calculator."""

    def test_positive_catalog_scoring(self) -> None:
        """Positive test: validates scoring and severity for known catalog vectors."""
        rogue_vector = CONSTELLATION_CATALOG["TTC-ROGUE-001"]
        self.assertEqual(rogue_vector.calculate_space_score(), 10.0)
        self.assertEqual(rogue_vector.severity_label, "CRITICAL")
        self.assertEqual(rogue_vector.sparta_id, "IMP-0006")

        glitch_vector = CONSTELLATION_CATALOG["UT-GLITCH-001"]
        self.assertEqual(glitch_vector.calculate_space_score(), 6.2)
        self.assertEqual(glitch_vector.severity_label, "MEDIUM")

    def test_positive_custom_vector(self) -> None:
        """Positive test: calculates space score with custom valid modifiers."""
        multiplier, score, label = evaluate_custom_space_vector(
            base_score=7.0,
            oci="L",
            lsr="N",
            tbw="T",
            dh="N",
        )
        self.assertAlmostEqual(multiplier, 1.10, delta=0.01)
        self.assertEqual(score, 7.7)
        self.assertEqual(label, "HIGH")

    def test_negative_invalid_base_score(self) -> None:
        """Negative test: asserts ValueError raised when base score exceeds boundaries."""
        with self.assertRaises(ValueError):
            evaluate_custom_space_vector(11.5, "N", "N", "N", "N")
        with self.assertRaises(ValueError):
            evaluate_custom_space_vector(-1.0, "N", "N", "N", "N")

    def test_negative_invalid_modifier_enum(self) -> None:
        """Negative test: asserts ValueError raised when unknown enum string is provided."""
        with self.assertRaises(ValueError):
            evaluate_custom_space_vector(5.0, "INVALID", "N", "N", "N")

    def test_edge_case_score_boundaries(self) -> None:
        """Edge-case test: verifies minimum zero score and maximum score clamping at 10.0."""
        _, min_score, min_label = evaluate_custom_space_vector(0.0, "N", "N", "N", "N")
        self.assertEqual(min_score, 0.0)
        self.assertEqual(min_label, "LOW")

        _, max_score, max_label = evaluate_custom_space_vector(10.0, "C", "H", "P", "U")
        self.assertEqual(max_score, 10.0)
        self.assertEqual(max_label, "CRITICAL")


class TestShamirThresholdTelecommand(unittest.TestCase):
    """Triad test suite (Positive, Negative, Edge-Case) for TT&C Multi-Party Quorum."""

    def setUp(self) -> None:
        """Sets up baseline polynomial secret and flight computer."""
        self.secret: int = secrets.randbelow(PRIME_256)
        secret_bytes: bytes = self.secret.to_bytes(32, byteorder="big")
        self.expected_hash: str = hashlib.sha256(secret_bytes).hexdigest()

        self.sat_id: int = 1042
        self.engine = ShamirThresholdEngine(prime=PRIME_256)
        self.shares = self.engine.generate_polynomial_shares(
            secret=self.secret,
            threshold_t=3,
            signers_n=5,
        )
        self.flight_computer = SatelliteFlightComputer(
            satellite_id=self.sat_id,
            authorized_quorum_secret_hash=self.expected_hash,
            threshold_t=3,
        )

    def test_positive_secret_reconstruction(self) -> None:
        """Positive test: verifies exact secret reconstruction from any t distinct shares."""
        reconstructed = self.engine.reconstruct_secret_at_zero(
            [self.shares[0], self.shares[2], self.shares[4]]
        )
        self.assertEqual(reconstructed, self.secret)

    def test_positive_class_c_telecommand_execution(self) -> None:
        """Positive test: verifies Class C de-orbit authorized with valid t=3 quorum."""
        cmd = TelecommandPayload(
            command_id="CMD-DEORBIT-01",
            target_satellite_id=self.sat_id,
            criticality=CommandCriticality.CLASS_C_FLIGHT_CRITICAL,
            action_name="De-orbit Burn",
            delta_v_vector_mps=(-30.0, 0.0, -15.0),
        )
        res = self.flight_computer.verify_and_execute_command(
            command=cmd,
            shares=[self.shares[1], self.shares[3], self.shares[4]],
        )
        self.assertTrue(res.is_authorized)
        self.assertEqual(res.reconstructed_secret_hash, self.expected_hash)
        self.assertIsNone(res.rejection_reason)

    def test_positive_autonomous_collision_avoidance(self) -> None:
        """Positive test: verifies Class A autonomous maneuver executes without ground shares."""
        cmd = TelecommandPayload(
            command_id="CMD-AVOID-01",
            target_satellite_id=self.sat_id,
            criticality=CommandCriticality.CLASS_A_AUTONOMOUS,
            action_name="Autonomous Conjunction Burn",
            delta_v_vector_mps=(0.1, 0.0, 0.0),
        )
        res = self.flight_computer.verify_and_execute_command(command=cmd, shares=[])
        self.assertTrue(res.is_authorized)
        self.assertEqual(res.required_threshold, 0)

    def test_negative_sub_threshold_rejection(self) -> None:
        """Negative test: asserts Class C command rejected when presenting fewer than t shares."""
        cmd = TelecommandPayload(
            command_id="CMD-DEORBIT-02",
            target_satellite_id=self.sat_id,
            criticality=CommandCriticality.CLASS_C_FLIGHT_CRITICAL,
            action_name="Rogue De-orbit Attempt",
            delta_v_vector_mps=(-30.0, 0.0, -15.0),
        )
        res = self.flight_computer.verify_and_execute_command(
            command=cmd,
            shares=self.shares[:2],
        )
        self.assertFalse(res.is_authorized)
        self.assertIn("Sub-threshold presentation", str(res.rejection_reason))

    def test_negative_corrupted_share_rejection(self) -> None:
        """Negative test: asserts Class C command rejected when a share value is tampered."""
        corrupted_share = SecretShare(index_x=self.shares[0].index_x, value_y=(self.shares[0].value_y + 1) % PRIME_256)
        cmd = TelecommandPayload(
            command_id="CMD-DEORBIT-03",
            target_satellite_id=self.sat_id,
            criticality=CommandCriticality.CLASS_C_FLIGHT_CRITICAL,
            action_name="Tampered De-orbit",
            delta_v_vector_mps=(-30.0, 0.0, -15.0),
        )
        res = self.flight_computer.verify_and_execute_command(
            command=cmd,
            shares=[corrupted_share, self.shares[1], self.shares[2]],
        )
        self.assertFalse(res.is_authorized)
        self.assertIn("Cryptographic signature mismatch", str(res.rejection_reason))

    def test_negative_satellite_id_mismatch(self) -> None:
        """Negative test: asserts command rejected if target satellite ID does not match bus."""
        cmd = TelecommandPayload(
            command_id="CMD-DEORBIT-04",
            target_satellite_id=9999,
            criticality=CommandCriticality.CLASS_C_FLIGHT_CRITICAL,
            action_name="Wrong Target ID",
            delta_v_vector_mps=(-30.0, 0.0, -15.0),
        )
        res = self.flight_computer.verify_and_execute_command(command=cmd, shares=self.shares[:3])
        self.assertFalse(res.is_authorized)
        self.assertIn("Target mismatch", str(res.rejection_reason))

    def test_edge_case_unanimous_quorum(self) -> None:
        """Edge-case test: verifies (5, 5) unanimous threshold quorum where t == n."""
        engine_unanimous = ShamirThresholdEngine()
        shares_unanimous = engine_unanimous.generate_polynomial_shares(
            secret=self.secret,
            threshold_t=5,
            signers_n=5,
        )
        reconstructed = engine_unanimous.reconstruct_secret_at_zero(shares_unanimous)
        self.assertEqual(reconstructed, self.secret)

    def test_edge_case_duplicate_indices_rejected(self) -> None:
        """Edge-case test: asserts ValueError raised if duplicate share indices are supplied."""
        duplicate_shares = [self.shares[0], self.shares[0], self.shares[1]]
        with self.assertRaises(ValueError):
            self.engine.reconstruct_secret_at_zero(duplicate_shares)


if __name__ == "__main__":
    unittest.main()
