# Kiprim DC310S Power Studio

A high-performance, native **GNOME / GTK4 + Libadwaita** desktop application for controlling **Kiprim DC310S** (and compatible **Owon SPE / SPS series**) programmable DC power supplies.

Written in modern **C++20** with low memory footprint, instant startup, and zero runtime garbage collection pauses.

---

## Features

### ⚡ Comprehensive Power Supply Control
* **Main Output Toggle**: High-visibility glowing power switch with live status badge (`STANDBY` vs `ACTIVE`).
* **Target Voltage Control**: 0.000 V to 31.000 V (1 mV precision) with numeric spin entry, smooth range slider, and fine/coarse steppers (`-1V`, `-0.1V`, `-0.01V`, `+0.01V`, `+0.1V`, `+1V`).
* **Current Limit Control**: 0.000 A to 10.000 A (1 mA precision) with numeric spin entry, smooth slider, and steppers (`-1A`, `-0.1A`, `-0.01A`, `+0.01A`, `+0.1A`, `+1A`).
* **Digital Multimeter Readouts**: Ultra-clear hero displays for:
  * **Measured Voltage** ($V$)
  * **Measured Current** ($A$)
  * **Real-time Power** ($W$)
* **Regulation Status Badge**: Dynamic detection of **CV** (Constant Voltage) vs **CC** (Constant Current) mode.
* **Integrated Energy & Charge Counters**: Live computation of cumulative energy ($Wh$) and charge ($Ah$).
* **Active Run Timer**: Elapsed output-on runtime tracker ($HH:MM:SS$).
* **Lab Quick Presets**: One-click standard voltages: `1.8V (Core)`, `3.3V (Logic)`, `5.0V (USB)`, `9.0V (Battery)`, `12.0V (Standard)`, `24.0V (Industrial)`.
* **Memory Channels (M1 – M5)**: Rapid recall of 5 customizable memory slots.
* **Safety & Protection (OVP / OCP)**: Configurable Over-Voltage Protection and Over-Current Protection thresholds.

---

### 📈 Modern Multi-Channel Oscilloscope & Waveform Graphs
* **Hardware-Accelerated Cairo 2D Vector Rendering**:
  * **Voltage Curve (V)**: Cyan / Electric Blue trace with gradient glow.
  * **Current Curve (A)**: Vibrant Amber / Gold trace with fill.
  * **Power Curve (W)**: Emerald Green trace.
* **Dual Y-Axes**: Left axis for Voltage/Power, Right axis for Current with calibrated grid lines.
* **Interactive Hover Crosshair**: Hover cursor displays an exact readout pill (`t`, `V`, `A`, `W`) and highlights points on curves.
* **Configurable Rolling Time Windows**: `5s`, `10s`, `30s`, `60s`, `5m`, `10m`.
* **Auto-Scaling & Manual Headroom**: Dynamically expands Y-axis based on signal envelope.
* **Live Statistical Metrics**: Instant display of $V_{max}$, $V_{min}$, $V_{avg}$, $I_{max}$, $I_{min}$, $I_{avg}$, $P_{peak}$, $P_{avg}$.
* **Export Options**: Export waveform data to CSV or capture graph to high-res PNG.

---

### ⏱️ Automated Sequence / List Mode (Dwell Testing)
* Multi-step automated test profile runner with customizable dwell durations per step.
* **Cycle Controls**: Single run, $N$ loops, or continuous infinite repeat.
* **Live Visual Progress**: Animated step countdown bar and active step status.
* **Built-in Waveform Generators**:
  * **Ramp Generator**: Automated linear voltage sweeps (e.g., 0V to 12V in 1V increments).
  * **Pulse Train Generator**: Square pulse generator with configurable high/low voltages and pulse widths.
  * **Staircase Generator**: Multi-step step-up and step-down automated profiles.
* **Profile Persistence**: Save and load automated test profiles to CSV files.

---

### 📝 Continuous CSV Data Logger
* Background measurement logging with customizable sampling rates (`100ms`, `250ms`, `500ms`, `1s`, `2s`).
* Formatted CSV export including ISO8601 timestamps, elapsed seconds, setpoints, measured values, power, energy, output state, and regulation mode.
* Real-time metrics on written samples, elapsed recording duration, and file size.

---

### ⚙️ Diagnostics, Safety & SCPI Console
* **Auto-Detection**: Scans `/dev/ttyUSB*` and `/dev/ttyACM*` serial devices (specifically identifying QinHeng CH340 / Kiprim DC310S hardware).
* **Front-Panel Keypad Lock**: Remotely lock front-panel knobs/buttons via `SYST:LOCK`.
* **Safe Shutdown on Exit**: Automatically disables output terminals upon closing application.
* **Built-in Realistic Simulator**: Toggle simulation mode to test waveforms, sequences, and UI even without physical hardware plugged in.
* **Interactive SCPI Terminal**: Execute raw SCPI queries (`*IDN?`, `MEAS:ALL?`, `VOLT?`, `CURR?`, `OUTP?`, `*RST`) with live colored terminal log.

---

## Building & Installation

### Requirements
* Fedora / RHEL / Debian / Ubuntu with:
  * `g++` (supporting C++20)
  * `libadwaita-1` & `gtk4`
  * `pkg-config`

### Build with Make
```bash
make
```

### Scripts
* **Build**:
  ```bash
  ./build.sh
  ```
* **Install**:
  ```bash
  ./install.sh
  ```
* **Uninstall**:
  ```bash
  ./uninstall.sh
  ```
* **Clean Build Binaries**:
  ```bash
  ./clean.sh
  ```

---

## Serial Port Permissions (Linux)

To access USB serial ports (e.g. `/dev/ttyUSB0`) without root privileges, make sure your user is in the `dialout` (or `uucp`) group:
```bash
sudo usermod -aG dialout $USER
```
*(Log out and back in for group changes to take effect).*

---

## SCPI Protocol Summary

| Command | Response | Description |
|:---|:---|:---|
| `*IDN?` | `KIPRIM,DC310S,<serial>,FV:V5.2.0` | Queries device identity and firmware |
| `VOLT <v>` | - | Sets target voltage (e.g. `VOLT 12.000`) |
| `VOLT?` | `<val>` | Queries set target voltage |
| `CURR <a>` | - | Sets current limit (e.g. `CURR 2.000`) |
| `CURR?` | `<val>` | Queries set current limit |
| `OUTP <ON\|OFF>` | - | Toggles output terminals |
| `OUTP?` | `ON` or `OFF` | Queries output state |
| `MEAS:ALL?` | `<volt>,<curr>` | Fast single-command dual measurement |
| `VOLT:LIM <v>` | - | Sets Over-Voltage Protection threshold |
| `CURR:LIM <a>` | - | Sets Over-Current Protection threshold |
| `SYST:LOCK <ON\|OFF>` | - | Locks/unlocks physical keypad |

---

## License
MIT License.
