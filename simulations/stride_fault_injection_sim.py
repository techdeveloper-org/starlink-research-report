"""Hardware voltage glitching, CMOS timing perturbation, and crowbar clamp defense simulator.

This module models the physics and instruction-level timing of the Lennert Wouters (Black Hat 2022)
voltage glitching attack on the Starlink User Terminal (UT) quad-core ARM Cortex BootROM.
It models CMOS propagation delay expansion via the alpha-power law, calculates setup-time violation
probabilities during Secure Boot signature verification, and evaluates the efficacy of analog crowbar
slew-rate clamps (<250 ps response) and Dual-Core Lockstep (DCLS) 2-cycle staggered cores in
reducing exploit probability to P_glitch < 10^-11.
"""

from dataclasses import dataclass
from typing import Dict, List, Tuple
import math


@dataclass(frozen=True)
class ProcessorSiliconParameters:
    """FinFET CMOS transistor process and clock parameters for terminal SoC.

    Attributes:
        nominal_vcore_volts: Nominal operational core voltage rail (0.85 V).
        threshold_voltage_volts: Transistor threshold voltage V_th (0.35 V).
        alpha_power_exponent: FinFET velocity saturation exponent (typically 1.2 - 1.4).
        clock_frequency_ghz: CPU core clock frequency in GHz (1.2 GHz).
        nominal_prop_delay_ps: Inverter chain propagation delay at nominal voltage (ps).
        flip_flop_setup_time_ps: Flip-flop data setup time requirement (ps).
    """

    nominal_vcore_volts: float
    threshold_voltage_volts: float
    alpha_power_exponent: float
    clock_frequency_ghz: float
    nominal_prop_delay_ps: float
    flip_flop_setup_time_ps: float

    @property
    def clock_period_ps(self) -> float:
        """Returns clock period in picoseconds."""
        return (1.0 / (self.clock_frequency_ghz * 1.0e9)) * 1.0e12


@dataclass(frozen=True)
class GlitchTransientProfile:
    """Parameters of an injected ground-short voltage glitch transient.

    Attributes:
        glitch_voltage_volts: Minimum depressed voltage level during fault injection (V).
        glitch_duration_ns: Duration of the voltage glitch pulse in nanoseconds.
        glitch_fall_time_ps: Slew-rate transition fall time in picoseconds.
        injection_target_instruction: Target assembly instruction opcode.
    """

    glitch_voltage_volts: float
    glitch_duration_ns: float
    glitch_fall_time_ps: float
    injection_target_instruction: str


@dataclass(frozen=True)
class DefenseConfiguration:
    """Hardware countermeasure mechanisms deployed on SoC.

    Attributes:
        has_analog_crowbar: Whether sub-nanosecond analog crowbar clamp is enabled.
        crowbar_reaction_time_ps: Reaction time of analog comparator clamp (ps).
        crowbar_voltage_threshold_mv: Slew rate / voltage trip threshold (mV).
        has_dual_core_lockstep: Whether Dual-Core Lockstep (DCLS) is active.
        dcls_phase_delay_cycles: Staggered instruction execution delay in clock cycles.
        has_sram_puf: Whether SRAM-PUF volatile key zeroization is active.
    """

    has_analog_crowbar: bool
    crowbar_reaction_time_ps: float
    crowbar_voltage_threshold_mv: float
    has_dual_core_lockstep: bool
    dcls_phase_delay_cycles: int
    has_sram_puf: bool


class VoltageGlitchSimulator:
    """Simulates physical timing perturbations and hardware countermeasure responses."""

    def __init__(self, silicon: ProcessorSiliconParameters) -> None:
        """Initializes the simulator with process silicon parameters.

        Args:
            silicon: CMOS FinFET electrical and timing parameters.
        """
        self.silicon = silicon

    def compute_perturbed_propagation_delay_ps(self, v_depressed_volts: float) -> float:
        """Evaluates CMOS propagation delay under depressed supply voltage using alpha-power law.

        t_pd(V) = t_pd0 * (V_nom / V) * ((V - V_th) / (V_nom - V_th))^-alpha

        Args:
            v_depressed_volts: Depressed core voltage during glitch transient.

        Returns:
            Perturbed propagation delay in picoseconds. Returns inf if V <= V_th.
        """
        if v_depressed_volts <= self.silicon.threshold_voltage_volts:
            return float("inf")

        v_nom = self.silicon.nominal_vcore_volts
        v_th = self.silicon.threshold_voltage_volts
        alpha = self.silicon.alpha_power_exponent
        t0 = self.silicon.nominal_prop_delay_ps

        voltage_ratio = v_nom / v_depressed_volts
        overdrive_ratio = (v_depressed_volts - v_th) / (v_nom - v_th)
        t_pd = t0 * voltage_ratio * (overdrive_ratio ** (-alpha))
        return float(t_pd)

    def evaluate_timing_fault(
        self,
        glitch: GlitchTransientProfile,
        defense: DefenseConfiguration
    ) -> Dict[str, object]:
        """Evaluates whether an injected glitch causes a successful exploit or hardware reset.

        Args:
            glitch: Voltage glitch transient parameters.
            defense: Active hardware defenses.

        Returns:
            Dictionary containing timing analysis, defense response, and exploit outcome.
        """
        t_clk = self.silicon.clock_period_ps
        t_setup = self.silicon.flip_flop_setup_time_ps
        t_pd_perturbed = self.compute_perturbed_propagation_delay_ps(glitch.glitch_voltage_volts)

        # Total signal arrival time
        total_delay = t_pd_perturbed + t_setup
        timing_violation = total_delay > t_clk

        # Check Analog Crowbar Clamp
        delta_v_mv = (self.silicon.nominal_vcore_volts - glitch.glitch_voltage_volts) * 1000.0
        crowbar_triggered = False
        if defense.has_analog_crowbar and delta_v_mv >= defense.crowbar_voltage_threshold_mv:
            if defense.crowbar_reaction_time_ps < (glitch.glitch_duration_ns * 1000.0):
                crowbar_triggered = True

        # Check Dual-Core Lockstep (DCLS)
        # Core 1 perturbed at t, Core 2 executed at t + delay_cycles
        single_core_bypass_prob = 0.018 if (timing_violation and not crowbar_triggered) else 0.0
        if defense.has_dual_core_lockstep and not crowbar_triggered:
            # Glitch must perturb both staggered instructions without crashing CPU
            effective_bypass_prob = single_core_bypass_prob * 1.5e-9
            dcls_interlock_active = True
        else:
            effective_bypass_prob = 0.0 if crowbar_triggered else single_core_bypass_prob
            dcls_interlock_active = False

        exploit_success = (effective_bypass_prob > 1e-10)

        return {
            "clock_period_ps": t_clk,
            "nominal_delay_ps": self.silicon.nominal_prop_delay_ps,
            "perturbed_delay_ps": t_pd_perturbed,
            "total_arrival_ps": total_delay,
            "setup_violation": timing_violation,
            "crowbar_triggered": crowbar_triggered,
            "dcls_interlock": dcls_interlock_active,
            "effective_bypass_prob": effective_bypass_prob,
            "exploit_successful": exploit_success
        }


def execute_fault_injection_simulation() -> None:
    """Executes verification simulation of Wouters voltage glitch and ZTSA defenses."""
    # Custom SpaceX / STMicroelectronics quad-core ARM Cortex-A53 silicon parameters
    silicon = ProcessorSiliconParameters(
        nominal_vcore_volts=0.85,
        threshold_voltage_volts=0.35,
        alpha_power_exponent=1.3,
        clock_frequency_ghz=1.2,
        nominal_prop_delay_ps=550.0,
        flip_flop_setup_time_ps=150.0
    )

    # Lennert Wouters 2022 Attack Profile: 6.5 ns glitch down to 0.42 V during CMP instruction
    wouters_glitch = GlitchTransientProfile(
        glitch_voltage_volts=0.42,
        glitch_duration_ns=6.5,
        glitch_fall_time_ps=180.0,
        injection_target_instruction="CMP R0, #0 ; BNE halt (BL1 Signature Check)"
    )

    # 1. Unhardened Terminal (2022 Gen 2 Hardware)
    legacy_defense = DefenseConfiguration(
        has_analog_crowbar=False,
        crowbar_reaction_time_ps=0.0,
        crowbar_voltage_threshold_mv=0.0,
        has_dual_core_lockstep=False,
        dcls_phase_delay_cycles=0,
        has_sram_puf=False
    )

    # 2. Hardened ZTSA Terminal (Gen 3 / Starlink Mini / ZTSA Spec)
    ztsa_defense = DefenseConfiguration(
        has_analog_crowbar=True,
        crowbar_reaction_time_ps=250.0,
        crowbar_voltage_threshold_mv=35.0,
        has_dual_core_lockstep=True,
        dcls_phase_delay_cycles=2,
        has_sram_puf=True
    )

    sim = VoltageGlitchSimulator(silicon)
    res_legacy = sim.evaluate_timing_fault(wouters_glitch, legacy_defense)
    res_ztsa = sim.evaluate_timing_fault(wouters_glitch, ztsa_defense)

    print("======================================================================")
    print("      STARLINK USER TERMINAL BOOTROM VOLTAGE GLITCH SIMULATION        ")
    print("======================================================================")
    print(f"Target CPU Architecture    : Quad-Core ARM Cortex-A53 (1.20 GHz)")
    print(f"Clock Period (T_clk)       : {silicon.clock_period_ps:.1f} ps (833.3 ps)")
    print(f"Nominal V_core / V_th      : {silicon.nominal_vcore_volts:.2f} V / {silicon.threshold_voltage_volts:.2f} V")
    print(f"Nominal Propagation Delay  : {silicon.nominal_prop_delay_ps:.1f} ps (Slack: {silicon.clock_period_ps - silicon.nominal_prop_delay_ps - silicon.flip_flop_setup_time_ps:.1f} ps)")
    print("----------------------------------------------------------------------")
    print("         LENNERT WOUTERS (2022) FAULT INJECTION TRANSIENT             ")
    print("----------------------------------------------------------------------")
    print(f"Targeted Instruction       : {wouters_glitch.injection_target_instruction}")
    print(f"Injected Glitch Voltage    : {wouters_glitch.glitch_voltage_volts:.2f} V (Drop: -{(silicon.nominal_vcore_volts - wouters_glitch.glitch_voltage_volts)*1000:.0f} mV)")
    print(f"Glitch Pulse Duration      : {wouters_glitch.glitch_duration_ns:.2f} ns ({wouters_glitch.glitch_duration_ns * 1000 / silicon.clock_period_ps:.1f} clock cycles)")
    print(f"Perturbed Delay (alpha law): {res_legacy['perturbed_delay_ps']:.1f} ps (Delay Expansion: x{res_legacy['perturbed_delay_ps'] / silicon.nominal_prop_delay_ps:.2f})")
    print(f"Total Arrival Time         : {res_legacy['total_arrival_ps']:.1f} ps (> {silicon.clock_period_ps:.1f} ps Clock Period)")
    print("----------------------------------------------------------------------")
    print("SCENARIO 1: UNHARDENED TERMINAL (GEN 2 DISHY — 2022):")
    print(f"  • Setup Time Violation   : {'YES (Instruction register corrupted)' if res_legacy['setup_violation'] else 'NO'}")
    print(f"  • Single-Attempt Bypass  : P_glitch = {res_legacy['effective_bypass_prob']:.2e} (~1.8% success rate)")
    print(f"  • Outcome                : {'ROOT COMPROMISE: BootROM signature check skipped!' if res_legacy['exploit_successful'] else 'HALT'}")
    print("----------------------------------------------------------------------")
    print("SCENARIO 2: ZTSA HARDENED SILICON (ANALOG CROWBAR + DCLS + SRAM-PUF):")
    print(f"  • Analog Crowbar Clamp   : {'ACTIVE (Tripped in <250 ps, forced reset)' if res_ztsa['crowbar_triggered'] else 'INACTIVE'}")
    print(f"  • Dual-Core Lockstep     : {'ACTIVE (2-cycle phase delay parity trap)' if res_ztsa['dcls_interlock'] else 'INACTIVE'}")
    print(f"  • Effective Bypass Prob  : P_glitch = {res_ztsa['effective_bypass_prob']:.2e} (Zeroized key registers)")
    print(f"  • Outcome                : {'EXPLOIT NEUTRALIZED: Zeroized keys & hard reboot' if not res_ztsa['exploit_successful'] else 'COMPROMISED'}")
    print("======================================================================")


if __name__ == "__main__":
    execute_fault_injection_simulation()
