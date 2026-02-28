# Pendulum EEG Suite

## This directory/software won't be nessesary a review from HackClub Reviewer
> Note for reviwer.

This directory includes software for EEG reading from USB port, not an firmware part.

Pendulum is the host software for EEGFrontier hardware. It follows the firmware
protocol from `firmware/` and provides:

- A local Reflex web dashboard (raw data, diagnostics, export tools).
- A desktop pyqtgraph app focused on real-time focus analysis.
- A simulation mode for development without hardware connected.

## Stack

- Python 3.11+
- Reflex
- pyserial
- numpy
- scipy
- pyqtgraph + PyQt6
- mne

## Setup

```bash
cd Pendulum
python -m venv .venv
.venv\Scripts\activate
pip install -U pip
pip install -e .
```

## Run

### 1) Web dashboard (Reflex)

```bash
cd Pendulum
reflex run
```

Open the localhost URL shown by Reflex.

If you hit frontend bootstrap issues on Windows (bun/npm path), use:

```powershell
.\run_reflex.ps1
```

or:

```bat
run_reflex.bat
```

If you need to stop all running Reflex instances:

```powershell
.\stop_reflex.ps1
```

### 2) Desktop focus monitor (pyqtgraph)

With hardware:

```bash
python -m pendulum_eeg.pyqt_focus --port COM5
```

Simulation:

```bash
python -m pendulum_eeg.pyqt_focus --simulate
```

## Firmware Protocol (BIN mode)

Packet format:

- `packet = COBS(raw_packet) + 0x00`
- `raw_packet = [type][ver][payload...][crc16_le]`

Packet types:

- `0x01`: sample
- `0x02`: event
- `0x7F`: error

Sample payload:

- `sample_index` `u32`
- `t_us` `u32`
- `status24` `u32`
- `ch1..ch4` `i32`
- `flags` `u32`
- `missed_drdy_frame` `u32`
- `recoveries_total` `u32`

## Export formats

- `exports/*.csv`
- `exports/*.npz`
- `exports/*.json`
- `exports/*.fif` (MNE)

## Notes

- Default serial baud: `921600`
- Default firmware sample rate: `250 SPS`
- Focus score is a real-time heuristic from EEG bands and should be calibrated
  per user/protocol for serious studies.
