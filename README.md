# OpenFlight

OpenFlight is a hardware-free CubeSat flight-computer project. It combines a
[NASA F Prime](https://fprime.jpl.nasa.gov/) flight-software deployment with a
[Zephyr](https://docs.zephyrproject.org/) simulated sensor controller. The
current milestone links both runtimes through a simulated serial connection
and demonstrates fault detection, safe-mode entry, and guarded recovery.

## Current milestone

- `OpenFlight/FlightComputer`: F Prime deployment with its standard command,
  telemetry, and event services.
- `OpenFlight/Components/StatusManager`: handles collection, sensor telemetry,
  controlled fault injection, safe-mode entry, and operator-approved recovery.
- `embedded`: Zephyr `native_sim` application with synthetic temperature,
  attitude, and battery values. It sends CRC-protected packets over a simulated
  UART and prints diagnostic frames.
  Values are demonstration data, not sensor measurements.
- `protocol`: bounded packet codec and a provisional sensor payload format.
  Both programs use this codec over the serial link.
- `dashboard`: local mission-control interface backed by live F Prime channels,
  events, and commands.

## Quick start on the current Windows machine

From PowerShell in this repository, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\Start-OpenFlight.ps1
```

The launcher starts Zephyr and F Prime in WSL, discovers the changing
`/dev/pts/...` serial path, and opens the mission dashboard at
`http://127.0.0.1:8000`. Keep the PowerShell window open and press Ctrl+C
there to stop all processes. The standard F Prime engineering interface
remains available at `http://127.0.0.1:5000`.

The dashboard shows live battery, temperature, attitude, packet counts,
operating mode, active fault, and the F Prime event timeline. Its five scenario
buttons use the same command path as the automated mission tests. See
[dashboard operations](docs/dashboard.md) for the control behavior and safety
boundaries.

## Build the flight computer in WSL2 Ubuntu

Use Ubuntu's native filesystem for the checkout and build products. Building
under `/mnt/c` works but is much slower. The clone command below applies after
this repository has been pushed to GitHub. On the current machine, a matching
Ubuntu build copy is already at `~/openflight-clean`.

```bash
sudo apt update
sudo apt install git cmake g++ make python3-pip python3-venv default-jre
git clone --recurse-submodules https://github.com/pwilly1/openflight.git ~/openflight
cd ~/openflight
python3 -m venv fprime-venv
source fprime-venv/bin/activate
pip install -r requirements.txt
fprime-util generate
cd OpenFlight/FlightComputer
fprime-util build
fprime-gds
```

Start Zephyr first as shown below. It prints a UART path such as `/dev/pts/5`.
In the F Prime terminal, set `export OPENFLIGHT_SENSOR_PTY=/dev/pts/5` before
running `fprime-gds`. The path changes each time Zephyr starts. Open
`http://127.0.0.1:5000` if the GDS browser page does not open itself.
Send `OpenFlight.statusManager.GET_STATUS`, `START_COLLECTION`, and
`STOP_COLLECTION` from the Commanding tab. Inspect `StatusReported` and
`CollectionChanged` in Events, and `CollectionEnabled` and `StatusRequests` in
Channels. Sensor temperature and attitude channels use centi units; battery
uses millivolts. `OperatingMode` is 0 for normal and 1 for safe mode;
`ActiveFault` identifies the current fault. The fault commands are
`INJECT_CORRUPT_PACKET`, `INJECT_DROP_PACKETS`, `INJECT_LOW_BATTERY`,
`INJECT_FREEZE_SENSOR`, and `RESTORE_SENSOR`. Safe mode exits only after three
healthy samples and `RECOVER_SAFE_MODE`. For a terminal-only GDS, run
`fprime-gds -g none` and use
`fprime-cli command-send OpenFlight.statusManager.GET_STATUS` in a second
activated terminal.

## Build the sensor controller in WSL2 Ubuntu

Install Zephyr v4.4.1 and its Python requirements using the
[official getting-started guide](https://docs.zephyrproject.org/latest/develop/getting_started/).
Build the application using the host compiler and the 64-bit native target:

```bash
source ~/zephyrproject/.venv/bin/activate
cd ~/zephyrproject
export ZEPHYR_TOOLCHAIN_VARIANT=host/gnu
west build -b native_sim/native/64 ~/openflight/embedded -d ~/openflight-embedded-build
~/openflight-embedded-build/zephyr/zephyr.exe --rt
```

Stop the running simulation with Ctrl+C. Collection begins disabled and starts
after the flight computer sends `START_COLLECTION`. Its `SAMPLE` and `HEALTH`
lines show synthetic values, queue use, and dropped sample count.

## Run the mission scenarios

After building both programs in WSL2 Ubuntu:

```bash
source ~/openflight/fprime-venv/bin/activate
python3 ~/openflight/tests/run_mission_scenarios.py ~/openflight/OpenFlight/FlightComputer \
  --zephyr ~/openflight-embedded-build/zephyr/zephyr.exe

# Run one scenario and save a Markdown report
python3 ~/openflight/tests/run_mission_scenarios.py ~/openflight/OpenFlight/FlightComputer \
  --zephyr ~/openflight-embedded-build/zephyr/zephyr.exe \
  --scenario low-battery --report ~/openflight/build-artifacts/mission-report.md

# The previous command remains available as a compatibility wrapper
python3 ~/openflight/tests/test_flight_computer_smoke.py ~/openflight/OpenFlight/FlightComputer \
  --zephyr ~/openflight-embedded-build/zephyr/zephyr.exe

deactivate
python3 ~/openflight/tests/test_embedded_smoke.py ~/openflight-embedded-build/zephyr/zephyr.exe
cc -std=c11 -Wall -Wextra -Werror -I ~/openflight/protocol \
  ~/openflight/protocol/packet.c ~/openflight/tests/packet_test.c \
  -o /tmp/openflight-packet-test
/tmp/openflight-packet-test
```

The scenario runner starts one real GDS and flight computer session and reports
separate results for nominal operation, CRC rejection, low battery, a frozen
sensor, and a communication outage. The Zephyr smoke test checks increasing
samples, health reports, nominal queue behavior, and packet CRCs. The C test
checks packet round trips, stream recovery, and rejection of corrupted frames.
GitHub Actions repeats the builds and scenarios for pushes and pull requests.

## Project plan

See [architecture](docs/architecture.md), [requirements](docs/requirements.md),
[packet format](protocol/packet_spec.md), and [verification](docs/verification.md).
See [mission scenarios](docs/mission-scenarios.md) for the expected behavior and
fault reason mapping. The next milestone adds architecture visuals, a short
demonstration recording, and an application-ready project write-up. Renode
processor emulation remains a stretch goal.
