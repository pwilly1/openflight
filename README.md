# OpenFlight

OpenFlight is a hardware-free CubeSat flight-computer project. It combines a
[NASA F Prime](https://fprime.jpl.nasa.gov/) flight-software deployment with a
[Zephyr](https://docs.zephyrproject.org/) simulated sensor controller. The
current milestone establishes each runtime independently. The link between
them, fault management, and hardware emulation are subsequent milestones.

## Current milestone

- `OpenFlight/FlightComputer`: F Prime deployment with its standard command,
  telemetry, and event services.
- `OpenFlight/Components/StatusManager`: handles `GET_STATUS`,
  `START_COLLECTION`, and `STOP_COLLECTION`. The collection flag is currently
  internal state; it does not yet control the Zephyr process.
- `embedded`: Zephyr `native_sim` application with synthetic temperature,
  attitude, and battery values. It emits CRC-protected diagnostic frames.
  Values are demonstration data, not sensor measurements.
- `protocol`: bounded packet codec and a provisional sensor payload format.
  The programs do not yet exchange these packets over a link.

## Build the flight computer in WSL2 Ubuntu

Use Ubuntu's native filesystem for the checkout and build products. Building
under `/mnt/c` works but is much slower. The clone command below applies after
this repository has been pushed to GitHub. On the current machine, a matching
Ubuntu build copy is already at `~/openflight-build`.

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

Open `http://127.0.0.1:5000` if the GDS browser page does not open itself.
Send `OpenFlight.statusManager.GET_STATUS`, `START_COLLECTION`, and
`STOP_COLLECTION` from the Commanding tab. Inspect `StatusReported` and
`CollectionChanged` in Events, and `CollectionEnabled` and `StatusRequests` in
Channels. For a terminal-only GDS, run `fprime-gds -g none` and use
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

Stop the running simulation with Ctrl+C. Its `SAMPLE` and `HEALTH` lines
show synthetic values, queue use, and dropped sample count.

## Run the milestone smoke tests

After building both programs in WSL2 Ubuntu:

```bash
source ~/openflight/fprime-venv/bin/activate
python3 ~/openflight/tests/test_flight_computer_smoke.py ~/openflight/OpenFlight/FlightComputer
deactivate
python3 ~/openflight/tests/test_embedded_smoke.py ~/openflight-embedded-build/zephyr/zephyr.exe
cc -std=c11 -Wall -Wextra -Werror -I ~/openflight/protocol \
  ~/openflight/protocol/packet.c ~/openflight/tests/packet_test.c \
  -o /tmp/openflight-packet-test
/tmp/openflight-packet-test
```

The first test starts the real GDS and flight computer, sends four commands,
checks the ordered runtime events, then stops both processes. The second runs
Zephyr for four real-time seconds and checks increasing samples, health
reports, the nominal dropped-sample count, and packet CRCs. The final test
checks packet round trips and rejection of corrupted or malformed frames.

## Project plan

See [architecture](docs/architecture.md), [requirements](docs/requirements.md),
[packet format](protocol/packet_spec.md), and [verification](docs/verification.md).
The next milestone connects the two programs using the packet format; then
fault injection and safe mode will be added. Renode processor emulation is a
stretch goal.
