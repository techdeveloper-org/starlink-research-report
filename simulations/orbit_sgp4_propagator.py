"""Starlink orbital mechanics, J2 secular precession, and Foster collision probability simulator.

This module models Low Earth Orbit (LEO) astrodynamics characteristic of the SpaceX
Starlink mega-constellation (Shell 1: 550 km altitude, 53.0 degree inclination). It computes
first-order two-body orbital motion, J2 gravitational zonal harmonic secular perturbations,
Walker Delta constellation geometries, and evaluates the Foster & Estes (1992) B-plane
covariance collision probability integral for autonomous conjunction avoidance.
"""

from dataclasses import dataclass
from typing import Tuple
import math
import numpy as np
from scipy import integrate

MU_EARTH: float = 398600.4418
RADIUS_EARTH_KM: float = 6378.137
J2_COEFFICIENT: float = 1.08262668e-3


@dataclass(frozen=True)
class OrbitalElements:
    """Classical Keplerian orbital elements for a spacecraft.

    Attributes:
        semi_major_axis_km: Semi-major axis of the orbit in kilometers.
        eccentricity: Orbital eccentricity (dimensionless, 0 <= e < 1).
        inclination_deg: Orbital plane inclination in degrees relative to the equator.
        raan_deg: Right Ascension of the Ascending Node in degrees.
        arg_perigee_deg: Argument of perigee in degrees.
        true_anomaly_deg: True anomaly in degrees defining position along the orbit.
    """

    semi_major_axis_km: float
    eccentricity: float
    inclination_deg: float
    raan_deg: float
    arg_perigee_deg: float
    true_anomaly_deg: float


@dataclass(frozen=True)
class ConjunctionGeometry:
    """B-plane encounter geometry and error covariance for conjunction analysis.

    Attributes:
        miss_distance_km: Projected miss distance in the encounter B-plane (km).
        sigma_x_km: Semi-major standard deviation of combined positional covariance (km).
        sigma_y_km: Semi-minor standard deviation of combined positional covariance (km).
        correlation_rho: Correlation coefficient between B-plane error axes (-1 <= rho <= 1).
        combined_radius_m: Hard-body collision cross-section radius in meters.
    """

    miss_distance_km: float
    sigma_x_km: float
    sigma_y_km: float
    correlation_rho: float
    combined_radius_m: float


class StarlinkOrbitPropagator:
    """Propagator and perturbation analyzer for mega-constellation spacecraft."""

    def __init__(self, elements: OrbitalElements) -> None:
        """Initializes the propagator with initial Keplerian elements.

        Args:
            elements: Initial classical orbital elements.
        """
        self.elements = elements

    def compute_orbital_period_seconds(self) -> float:
        """Computes the orbital period using Kepler's third law.

        Returns:
            Orbital period in seconds.
        """
        return 2.0 * math.pi * math.sqrt(
            (self.elements.semi_major_axis_km ** 3) / MU_EARTH
        )

    def compute_mean_motion_rad_per_sec(self) -> float:
        """Computes the mean motion (n) of the satellite.

        Returns:
            Mean motion in radians per second.
        """
        return math.sqrt(MU_EARTH / (self.elements.semi_major_axis_km ** 3))

    def compute_orbital_velocity_km_s(self) -> float:
        """Computes orbital velocity for circular or near-circular orbit at perigee.

        Returns:
            Orbital velocity in kilometers per second.
        """
        r = self.elements.semi_major_axis_km * (1.0 - self.elements.eccentricity)
        return math.sqrt(MU_EARTH * (2.0 / r - 1.0 / self.elements.semi_major_axis_km))

    def compute_j2_nodal_precession_rate_deg_per_day(self) -> float:
        """Calculates secular nodal regression rate (dOmega/dt) due to Earth's oblateness.

        Returns:
            Nodal precession rate in degrees per solar day.
        """
        n = self.compute_mean_motion_rad_per_sec()
        p = self.elements.semi_major_axis_km * (1.0 - self.elements.eccentricity ** 2)
        inc_rad = math.radians(self.elements.inclination_deg)

        d_omega_dt_rad_s = -1.5 * n * J2_COEFFICIENT * ((RADIUS_EARTH_KM / p) ** 2) * math.cos(inc_rad)
        return math.degrees(d_omega_dt_rad_s) * 86400.0

    def compute_j2_apsidal_precession_rate_deg_per_day(self) -> float:
        """Calculates secular rate of perigee rotation (domega/dt) due to J2 oblateness.

        Returns:
            Apsidal rotation rate in degrees per solar day.
        """
        n = self.compute_mean_motion_rad_per_sec()
        p = self.elements.semi_major_axis_km * (1.0 - self.elements.eccentricity ** 2)
        inc_rad = math.radians(self.elements.inclination_deg)

        factor = 5.0 * (math.cos(inc_rad) ** 2) - 1.0
        d_perigee_dt_rad_s = 0.75 * n * J2_COEFFICIENT * ((RADIUS_EARTH_KM / p) ** 2) * factor
        return math.degrees(d_perigee_dt_rad_s) * 86400.0

    def propagate_state_vector(self, delta_t_seconds: float) -> Tuple[np.ndarray, np.ndarray]:
        """Propagates state vector to ECI J2000 cartesian coordinates at t + delta_t.

        Args:
            delta_t_seconds: Elapsed propagation time in seconds.

        Returns:
            Tuple of (position_vector_km, velocity_vector_km_s) in Earth-Centered Inertial frame.
        """
        n = self.compute_mean_motion_rad_per_sec()
        m_initial = math.radians(self.elements.true_anomaly_deg)
        m_current = (m_initial + n * delta_t_seconds) % (2.0 * math.pi)

        # Solve Kepler's equation M = E - e*sin(E) via Newton-Raphson iteration
        e_ecc = m_current
        for _ in range(12):
            f_val = e_ecc - self.elements.eccentricity * math.sin(e_ecc) - m_current
            f_prime = 1.0 - self.elements.eccentricity * math.cos(e_ecc)
            delta = f_val / f_prime
            e_ecc -= delta
            if abs(delta) < 1e-12:
                break

        # True anomaly from eccentric anomaly
        sin_nu = (math.sqrt(1.0 - self.elements.eccentricity ** 2) * math.sin(e_ecc)) / (
            1.0 - self.elements.eccentricity * math.cos(e_ecc)
        )
        cos_nu = (math.cos(e_ecc) - self.elements.eccentricity) / (
            1.0 - self.elements.eccentricity * math.cos(e_ecc)
        )
        nu = math.atan2(sin_nu, cos_nu)

        r_mag = self.elements.semi_major_axis_km * (1.0 - self.elements.eccentricity * math.cos(e_ecc))

        # Position in perifocal frame (PQW)
        p_pqw = np.array([r_mag * math.cos(nu), r_mag * math.sin(nu), 0.0])
        h_mag = math.sqrt(MU_EARTH * self.elements.semi_major_axis_km * (1.0 - self.elements.eccentricity ** 2))
        v_pqw = np.array([
            -(MU_EARTH / h_mag) * math.sin(nu),
            (MU_EARTH / h_mag) * (self.elements.eccentricity + math.cos(nu)),
            0.0
        ])

        # Direction cosine matrix PQW -> ECI
        raan = math.radians(self.elements.raan_deg + self.compute_j2_nodal_precession_rate_deg_per_day() * (delta_t_seconds / 86400.0))
        inc = math.radians(self.elements.inclination_deg)
        omega = math.radians(self.elements.arg_perigee_deg + self.compute_j2_apsidal_precession_rate_deg_per_day() * (delta_t_seconds / 86400.0))

        r_z_raan = np.array([
            [math.cos(raan), -math.sin(raan), 0.0],
            [math.sin(raan), math.cos(raan), 0.0],
            [0.0, 0.0, 1.0]
        ])
        r_x_inc = np.array([
            [1.0, 0.0, 0.0],
            [0.0, math.cos(inc), -math.sin(inc)],
            [0.0, math.sin(inc), math.cos(inc)]
        ])
        r_z_omega = np.array([
            [math.cos(omega), -math.sin(omega), 0.0],
            [math.sin(omega), math.cos(omega), 0.0],
            [0.0, 0.0, 1.0]
        ])

        q_matrix = r_z_raan @ r_x_inc @ r_z_omega
        r_eci = q_matrix @ p_pqw
        v_eci = q_matrix @ v_pqw

        return r_eci, v_eci


class FosterCollisionProbabilitySolver:
    """Evaluates the Foster & Estes (1992) B-plane collision probability integral."""

    @staticmethod
    def compute_collision_probability(conjunction: ConjunctionGeometry) -> float:
        """Integrates 2D bivariate normal error density over combined hard-body cross-section.

        Args:
            conjunction: Conjunction geometry and covariance properties.

        Returns:
            Dimensionless probability of collision (0 <= Pc <= 1).
        """
        ra_km = conjunction.combined_radius_m / 1000.0
        sig_x = conjunction.sigma_x_km
        sig_y = conjunction.sigma_y_km
        x0 = conjunction.miss_distance_km
        y0 = 0.0

        def integrand(y: float, x: float) -> float:
            factor = 1.0 / (2.0 * math.pi * sig_x * sig_y)
            exponent = -0.5 * (((x - x0) / sig_x) ** 2 + ((y - y0) / sig_y) ** 2)
            return factor * math.exp(exponent)

        prob, _ = integrate.dblquad(
            integrand,
            -ra_km,
            ra_km,
            lambda x: -math.sqrt(max(0.0, ra_km ** 2 - x ** 2)),
            lambda x: math.sqrt(max(0.0, ra_km ** 2 - x ** 2))
        )
        return float(prob)


def calculate_j2_secular_precession(elements: OrbitalElements) -> Tuple[float, float]:
    """Calculates RAAN and perigee secular precession rates in degrees per solar day.

    Args:
        elements: Keplerian orbital elements of the spacecraft.

    Returns:
        Tuple of (d_raan_dt_deg_day, d_arg_perigee_dt_deg_day).
    """
    prop = StarlinkOrbitPropagator(elements)
    return prop.compute_j2_nodal_precession_rate_deg_per_day(), prop.compute_j2_apsidal_precession_rate_deg_per_day()


def keplerian_to_cartesian_state(elements: OrbitalElements) -> Tuple[np.ndarray, np.ndarray]:
    """Converts Keplerian orbital elements to Cartesian position and velocity state vectors in ECI.

    Args:
        elements: Keplerian orbital elements of the spacecraft.

    Returns:
        Tuple of (r_vec_km, v_vec_km_s) 3D numpy arrays.
    """
    prop = StarlinkOrbitPropagator(elements)
    return prop.propagate_state_vector(0.0)


def generate_walker_delta_constellation(
    total_satellites: int,
    num_planes: int,
    phasing_parameter: int,
    altitude_km: float,
    inclination_deg: float
) -> list:
    """Synthesizes a symmetric Walker Delta constellation geometry (total:planes/sats/phase).

    Args:
        total_satellites: Total number of satellites across all planes.
        num_planes: Total number of orbital planes.
        phasing_parameter: Relative inter-plane phase spacing parameter.
        altitude_km: Circular orbital altitude in kilometers above Earth surface.
        inclination_deg: Orbital plane inclination in degrees.

    Returns:
        List of OrbitalElements objects representing the constellation mesh.
    """
    sats_per_plane = total_satellites // num_planes
    semi_major_axis = RADIUS_EARTH_KM + altitude_km
    raan_spacing_deg = 360.0 / num_planes
    mean_anomaly_spacing_deg = 360.0 / sats_per_plane
    delta_f_deg = (phasing_parameter * 360.0) / total_satellites

    constellation = []
    for p in range(num_planes):
        plane_raan = p * raan_spacing_deg
        for s in range(sats_per_plane):
            true_anom = (s * mean_anomaly_spacing_deg + p * delta_f_deg) % 360.0
            elem = OrbitalElements(
                semi_major_axis_km=semi_major_axis,
                eccentricity=0.0001,
                inclination_deg=inclination_deg,
                raan_deg=plane_raan,
                arg_perigee_deg=0.0,
                true_anomaly_deg=true_anom
            )
            constellation.append(elem)
    return constellation


def foster_collision_probability(conjunction: ConjunctionGeometry) -> float:
    """Computes Foster & Estes (1992) B-plane conjunction collision probability.

    Args:
        conjunction: Conjunction encounter geometry and combined covariance.

    Returns:
        Collision probability Pc (dimensionless).
    """
    return FosterCollisionProbabilitySolver.compute_collision_probability(conjunction)


def execute_starlink_shell1_simulation() -> None:
    """Runs verification simulation for Starlink Shell 1 orbital parameters."""
    shell1_elements = OrbitalElements(
        semi_major_axis_km=RADIUS_EARTH_KM + 550.0,
        eccentricity=0.0001,
        inclination_deg=53.0,
        raan_deg=0.0,
        arg_perigee_deg=0.0,
        true_anomaly_deg=0.0
    )

    propagator = StarlinkOrbitPropagator(shell1_elements)
    period = propagator.compute_orbital_period_seconds()
    velocity = propagator.compute_orbital_velocity_km_s()
    nodal_rate = propagator.compute_j2_nodal_precession_rate_deg_per_day()
    apsidal_rate = propagator.compute_j2_apsidal_precession_rate_deg_per_day()

    print("======================================================================")
    print("      SPACEX STARLINK SHELL 1 ASTRODYNAMICS & ORBIT SIMULATION        ")
    print("======================================================================")
    print(f"Semi-Major Axis (a)        : {shell1_elements.semi_major_axis_km:.3f} km")
    print(f"Orbital Altitude (h)       : {shell1_elements.semi_major_axis_km - RADIUS_EARTH_KM:.1f} km")
    print(f"Inclination (i)            : {shell1_elements.inclination_deg:.1f} deg")
    print(f"Orbital Period (T)         : {period / 60.0:.2f} minutes ({period:.1f} s)")
    print(f"Mean Orbital Velocity (v)  : {velocity:.3f} km/s ({velocity * 3600.0:.1f} km/h)")
    print(f"J2 Nodal Precession Rate   : {nodal_rate:.3f} deg/solar day")
    print(f"J2 Apsidal Precession Rate : {apsidal_rate:.3f} deg/solar day")

    r_vec, v_vec = propagator.propagate_state_vector(period / 4.0)
    print(f"Position vector at T/4     : [{r_vec[0]:.1f}, {r_vec[1]:.1f}, {r_vec[2]:.1f}] km")
    print(f"Velocity vector at T/4     : [{v_vec[0]:.3f}, {v_vec[1]:.3f}, {v_vec[2]:.3f}] km/s")

    conjunction = ConjunctionGeometry(
        miss_distance_km=0.085,
        sigma_x_km=0.120,
        sigma_y_km=0.045,
        correlation_rho=0.0,
        combined_radius_m=12.5
    )
    pc = FosterCollisionProbabilitySolver.compute_collision_probability(conjunction)
    print("----------------------------------------------------------------------")
    print("         FOSTER (1992) B-PLANE CONJUNCTION RISK ASSESSMENT            ")
    print("----------------------------------------------------------------------")
    print(f"Conjunction Miss Distance  : {conjunction.miss_distance_km * 1000.0:.1f} m")
    print(f"Combined Error Ellipse     : 1-sigma ({conjunction.sigma_x_km*1000:.0f}m x {conjunction.sigma_y_km*1000:.0f}m)")
    print(f"Hard-Body Collision Radius : {conjunction.combined_radius_m:.1f} m")
    print(f"Collision Probability (Pc) : {pc:.6e}")
    print(f"Maneuver Threshold (1e-4)  : {'ALERT: TRIGGER AVOIDANCE BURN' if pc > 1e-4 else 'NOMINAL: NO ACTION'}")
    print("======================================================================")


if __name__ == "__main__":
    execute_starlink_shell1_simulation()

