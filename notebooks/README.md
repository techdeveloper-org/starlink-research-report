# 📓 Starlink Interactive Reproducible Jupyter Notebooks Suite

This directory contains standalone, reproducible Jupyter notebooks providing hands-on tooling and computational models for researchers, aerospace engineers, and telecommunications architects.

---

## 🛰️ Notebook Catalog

| Notebook | Discipline & Topic | Core Equations & Simulation Focus |
|:---|:---|:---|
| **[`01_orbit_sgp4_propagation.ipynb`](./01_orbit_sgp4_propagation.ipynb)** | `spacecraft-orbital-mechanics-engineer`<br>Astrodynamics | SGP4 satellite propagation, $J_2$ Earth oblateness secular nodal precession ($-4.49^\circ/\text{day}$), Walker Delta $53^\circ:72/22/1$ geometry, and Foster collision probability integral. |
| **[`02_phased_array_beamforming.ipynb`](./02_phased_array_beamforming.ipynb)** | `radio-frequency-specialist`<br>Phased Arrays | 1,280-element Ku-band User Terminal (Dishy) AESA antenna, 2D Array Factor $AF(\theta, \phi)$, main lobe steering, and Capon MVDR spatial null-steering ($>45\text{ dB}$ cancellation). |
| **[`03_rf_link_budget_analysis.ipynb`](./03_rf_link_budget_analysis.ipynb)** | `telecom-mathematics-expert`<br>RF Telecom | End-to-end decibel link budgets, Free-Space Path Loss ($FSPL \approx 171\text{ dB}$), atmospheric and ITU-R P.618 rain attenuation, Receiver $G/T$, and cascaded power waterfall plots. |

---

## 🚀 Setup & Execution

### Prerequisites
- Python 3.10+
- Scientific libraries: `numpy`, `scipy`, `matplotlib`

### Running Locally
```bash
# Install dependencies from project root
pip install -r simulations/requirements.txt

# Launch Jupyter Lab or Notebook
jupyter notebook notebooks/
```

All notebooks are pre-configured to run with zero cloud dependencies and execute deterministically in seconds.
