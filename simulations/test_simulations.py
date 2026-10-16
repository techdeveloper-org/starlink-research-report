"""Comprehensive unit test harness for the Starlink simulation suite.

This module executes automated unit test assertions across all five scientific simulation modules:
1. orbit_sgp4_propagator: Keplerian period, velocity, J2 nodal precession, and Foster Pc.
2. phased_array_beamforming: 2D array factor, broadside gain, and Capon MVDR null depth.
3. rf_link_budget_calculator: EIRP, FSPL, received power, G/T, and positive operating margins.
4. pqc_kem_benchmark: ML-KEM polynomial ring operations, byte lengths, and key agreement.
5. stride_fault_injection_sim: CMOS delay expansion, timing violations, and crowbar clamp trips.
"""

import math
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
        k_a, k_b, ciphertext, latency = sim.execute_handshake_simulation()
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


if __name__ == "__main__":
    unittest.main()
