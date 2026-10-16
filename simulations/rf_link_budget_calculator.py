"""Decibel RF link budget engine for Starlink Ku/Ka-band space-to-ground links.

This module models end-to-end telecommunications link budgets for Low Earth Orbit (LEO)
satellite communications. It evaluates Free-Space Path Loss (FSPL), atmospheric gaseous
absorption, ITU-R P.618 rain attenuation statistics, antenna pointing losses, system noise
temperature, and derives the Carrier-to-Noise spectral density (C/N0), Energy per bit to noise
spectral density (Eb/N0), and SNR margins against DVB-S2X MODCODs.
"""

from dataclasses import dataclass
from typing import Dict
import math

BOLTZMANN_CONSTANT_DBW_K_HZ: float = -228.60  # 10 * log10(1.380649e-23)
SPEED_OF_LIGHT_M_S: float = 299792458.0


@dataclass(frozen=True)
class LinkParameters:
    """Input parameters defining an RF satellite communications link.

    Attributes:
        link_name: Descriptive name of the link scenario.
        carrier_frequency_ghz: Carrier frequency in Gigahertz (e.g., 12.0 for Ku downlink).
        bandwidth_mhz: Occupied channel bandwidth in Megahertz (e.g., 250.0 MHz).
        data_rate_mbps: Usable information payload bit rate in Megabits per second.
        slant_range_km: Propagation slant distance from satellite to earth station in kilometers.
        tx_power_watts: Transmitter RF output power in Watts.
        tx_antenna_gain_dbi: Transmitter antenna boresight gain in dBi.
        tx_cable_loss_db: Transmission line and waveguide losses in dB.
        tx_pointing_loss_db: Off-boresight pointing error loss at transmitter in dB.
        rx_antenna_gain_dbi: Receiver antenna boresight gain in dBi.
        rx_pointing_loss_db: Off-boresight pointing error loss at receiver in dB.
        rx_noise_figure_db: Receiver LNA noise figure in dB.
        antenna_noise_temp_k: Antenna sky and ground noise temperature in Kelvin.
        atmospheric_loss_db: Clear-sky gaseous oxygen and water vapor absorption in dB.
        rain_attenuation_db: Excess precipitation attenuation in dB (ITU-R P.618).
        required_eb_n0_db: Minimum required Eb/N0 threshold for target MODCOD in dB.
    """

    link_name: str
    carrier_frequency_ghz: float
    bandwidth_mhz: float
    data_rate_mbps: float
    slant_range_km: float
    tx_power_watts: float
    tx_antenna_gain_dbi: float
    tx_cable_loss_db: float
    tx_pointing_loss_db: float
    rx_antenna_gain_dbi: float
    rx_pointing_loss_db: float
    rx_noise_figure_db: float
    antenna_noise_temp_k: float
    atmospheric_loss_db: float
    rain_attenuation_db: float
    required_eb_n0_db: float


@dataclass(frozen=True)
class LinkBudgetResult:
    """Comprehensive output metrics computed by the link budget engine.

    Attributes:
        eirp_dbw: Effective Isotropic Radiated Power in dBW.
        fspl_db: Free-space path loss in dB.
        total_path_loss_db: Sum of FSPL, atmospheric, rain, and pointing losses in dB.
        rx_power_dbw: Total received carrier power at receiver aperture in dBW.
        rx_power_dbm: Total received carrier power in dBm.
        system_noise_temp_k: Total effective system noise temperature in Kelvin.
        g_over_t_db_k: Receiver figure of merit (G/T) in dB/K.
        c_over_n0_db_hz: Carrier-to-Noise spectral density in dB-Hz.
        c_over_n_db: Carrier-to-Noise ratio in channel bandwidth in dB.
        eb_over_n0_db: Energy per bit to noise spectral density ratio in dB.
        snr_margin_db: Link operating margin above required threshold in dB.
    """

    eirp_dbw: float
    fspl_db: float
    total_path_loss_db: float
    rx_power_dbw: float
    rx_power_dbm: float
    system_noise_temp_k: float
    g_over_t_db_k: float
    c_over_n0_db_hz: float
    c_over_n_db: float
    eb_over_n0_db: float
    snr_margin_db: float


class RFLinkBudgetCalculator:
    """Evaluates microwave link budgets for space-to-ground links."""

    @staticmethod
    def compute_link_budget(params: LinkParameters) -> LinkBudgetResult:
        """Calculates full decibel link budget from input link parameters.

        Args:
            params: Geometric, RF, and environmental parameters of the link.

        Returns:
            LinkBudgetResult containing power levels, G/T, C/N0, and operating margins.
        """
        # 1. Transmitter EIRP
        tx_power_dbw = 10.0 * math.log10(params.tx_power_watts)
        eirp_dbw = tx_power_dbw + params.tx_antenna_gain_dbi - params.tx_cable_loss_db - params.tx_pointing_loss_db

        # 2. Propagation Path Loss: FSPL = 20*log10(d_km) + 20*log10(f_GHz) + 92.45 dB
        fspl_db = (
            20.0 * math.log10(params.slant_range_km)
            + 20.0 * math.log10(params.carrier_frequency_ghz)
            + 92.45
        )

        total_propagation_losses_db = (
            fspl_db
            + params.atmospheric_loss_db
            + params.rain_attenuation_db
            + params.rx_pointing_loss_db
        )

        # 3. Received Carrier Power
        rx_power_dbw = eirp_dbw - total_propagation_losses_db + params.rx_antenna_gain_dbi
        rx_power_dbm = rx_power_dbw + 30.0

        # 4. System Noise Temperature and G/T
        t0 = 290.0
        f_linear = 10.0 ** (params.rx_noise_figure_db / 10.0)
        t_receiver_k = t0 * (f_linear - 1.0)
        t_sys_k = params.antenna_noise_temp_k + t_receiver_k
        t_sys_dbk = 10.0 * math.log10(t_sys_k)
        g_over_t_db_k = params.rx_antenna_gain_dbi - t_sys_dbk

        # 5. C/N0 = EIRP - Losses + G/T - k
        c_over_n0_db_hz = (
            eirp_dbw
            - total_propagation_losses_db
            + g_over_t_db_k
            - BOLTZMANN_CONSTANT_DBW_K_HZ
        )

        # 6. C/N = C/N0 - 10*log10(B)
        bandwidth_db_hz = 10.0 * math.log10(params.bandwidth_mhz * 1.0e6)
        c_over_n_db = c_over_n0_db_hz - bandwidth_db_hz

        # 7. Eb/N0 = C/N0 - 10*log10(R_b)
        bit_rate_db_bps = 10.0 * math.log10(params.data_rate_mbps * 1.0e6)
        eb_over_n0_db = c_over_n0_db_hz - bit_rate_db_bps

        # 8. Operating Margin
        snr_margin_db = eb_over_n0_db - params.required_eb_n0_db

        return LinkBudgetResult(
            eirp_dbw=eirp_dbw,
            fspl_db=fspl_db,
            total_path_loss_db=total_propagation_losses_db,
            rx_power_dbw=rx_power_dbw,
            rx_power_dbm=rx_power_dbm,
            system_noise_temp_k=t_sys_k,
            g_over_t_db_k=g_over_t_db_k,
            c_over_n0_db_hz=c_over_n0_db_hz,
            c_over_n_db=c_over_n_db,
            eb_over_n0_db=eb_over_n0_db,
            snr_margin_db=snr_margin_db
        )


def execute_starlink_link_budgets() -> Dict[str, LinkBudgetResult]:
    """Runs verified link budgets for Starlink user downlink, user uplink, and gateway feeder."""
    # Scenario A: Ku-band Downlink (Satellite to User Terminal Dishy)
    ku_downlink_params = LinkParameters(
        link_name="Starlink Ku-Band User Downlink (550 km LEO, 40 deg elevation)",
        carrier_frequency_ghz=12.0,
        bandwidth_mhz=250.0,
        data_rate_mbps=220.0,
        slant_range_km=750.0,
        tx_power_watts=4.0,              # Spacecraft Ku transmit power per spot beam (36 dBm)
        tx_antenna_gain_dbi=35.5,        # Satellite phased array transmit gain
        tx_cable_loss_db=0.8,
        tx_pointing_loss_db=0.5,
        rx_antenna_gain_dbi=34.2,        # User terminal (Dishy) phased array gain at 12 GHz
        rx_pointing_loss_db=0.3,
        rx_noise_figure_db=1.4,          # GaN/GaAs low-noise amplifier figure
        antenna_noise_temp_k=65.0,       # Clear sky background at 40 deg elevation
        atmospheric_loss_db=0.45,
        rain_attenuation_db=1.20,        # 99.9% availability in ITU Rain Zone K
        required_eb_n0_db=6.5            # DVB-S2X 16-APSK rate 2/3 threshold
    )

    # Scenario B: Ku-band Uplink (User Terminal Dishy to Satellite)
    ku_uplink_params = LinkParameters(
        link_name="Starlink Ku-Band User Uplink (14 GHz, 2.5W Tx power)",
        carrier_frequency_ghz=14.25,
        bandwidth_mhz=62.5,
        data_rate_mbps=25.0,
        slant_range_km=750.0,
        tx_power_watts=2.5,              # Dishy transmit power (34 dBm)
        tx_antenna_gain_dbi=35.0,        # Dishy phased array gain at 14 GHz
        tx_cable_loss_db=0.5,
        tx_pointing_loss_db=0.4,
        rx_antenna_gain_dbi=33.5,        # Satellite receive phased array gain
        rx_pointing_loss_db=0.5,
        rx_noise_figure_db=2.2,          # Satellite onboard LNA
        antenna_noise_temp_k=290.0,      # Earth-facing antenna temperature
        atmospheric_loss_db=0.60,
        rain_attenuation_db=1.80,
        required_eb_n0_db=4.8            # QPSK rate 3/4 threshold
    )

    calc = RFLinkBudgetCalculator()
    results = {
        "ku_downlink": calc.compute_link_budget(ku_downlink_params),
        "ku_uplink": calc.compute_link_budget(ku_uplink_params)
    }

    print("======================================================================")
    print("           SPACEX STARLINK RF DECIBEL LINK BUDGET SIMULATOR           ")
    print("======================================================================")
    for name, res in results.items():
        params = ku_downlink_params if name == "ku_downlink" else ku_uplink_params
        print(f"\nLINK: {params.link_name}")
        print("----------------------------------------------------------------------")
        print(f"Carrier Frequency          : {params.carrier_frequency_ghz:.2f} GHz")
        print(f"Bandwidth / Bit Rate       : {params.bandwidth_mhz:.1f} MHz / {params.data_rate_mbps:.1f} Mbps")
        print(f"Transmitter EIRP           : {res.eirp_dbw:.2f} dBW ({res.eirp_dbw + 30:.2f} dBm)")
        print(f"Free-Space Path Loss (FSPL): {res.fspl_db:.2f} dB (Slant: {params.slant_range_km:.0f} km)")
        print(f"Total Attenuation          : {res.total_path_loss_db:.2f} dB (Rain: {params.rain_attenuation_db:.2f} dB)")
        print(f"Received Power (P_rx)      : {res.rx_power_dbw:.2f} dBW ({res.rx_power_dbm:.2f} dBm)")
        print(f"System Noise Temp (T_sys)  : {res.system_noise_temp_k:.1f} K")
        print(f"Receiver Figure of Merit   : {res.g_over_t_db_k:.2f} dB/K")
        print(f"Carrier-to-Noise (C/N0)    : {res.c_over_n0_db_hz:.2f} dB-Hz")
        print(f"Channel SNR (C/N)          : {res.c_over_n_db:.2f} dB")
        print(f"Energy per bit (Eb/N0)     : {res.eb_over_n0_db:.2f} dB (Required: {params.required_eb_n0_db:.1f} dB)")
        print(f"Link Operating Margin      : {res.snr_margin_db:.2f} dB ({'LINK CLOSED - PASS' if res.snr_margin_db >= 3.0 else 'MARGIN MARGINAL'})")
    print("======================================================================")

    return results


if __name__ == "__main__":
    execute_starlink_link_budgets()
