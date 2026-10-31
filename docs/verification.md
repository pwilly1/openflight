# Verification record

## Fault management milestone (2026-10-30)

Environment: Ubuntu 24.04 under WSL2, F Prime v4.4.1, Zephyr v4.4.1,
`native_sim/native/64`.

- F Prime and Zephyr rebuilt successfully with the fault-management commands,
  events, channels, and operating-mode logic.
- The end-to-end scenario injected a bad CRC and observed
  `SensorPacketRejected` in the real GDS event log.
- Persistent low battery and frozen sensor values each caused safe-mode entry
  with their distinct fault reason.
- Dropping eight packets caused the three-second link timeout, safe-mode entry,
  link restoration, and recovery qualification.
- A recovery command sent while low battery was active was rejected. After
  `RESTORE_SENSOR`, three healthy samples cleared each active fault, but safe
  mode persisted until the explicit `RECOVER_SAFE_MODE` command.
- The scenario completed three guarded recoveries and then verified that
  terminating Zephyr caused a second link-loss event.
- Repeated-invalid-frame safe-mode entry is implemented with a threshold of
  three consecutive parser errors; this specific threshold path was not
  exercised by the end-to-end scenario.
- All fault values and timing are simulated; this is not physical battery,
  sensor, UART, or real-time validation.

## Serial integration milestone (2026-10-30)

Environment: Ubuntu 24.04 under WSL2, F Prime v4.4.1, Zephyr v4.4.1,
`native_sim/native/64`.

- Both applications rebuilt from the current source in the Ubuntu build copy.
- The integration test started Zephyr, discovered its pseudoterminal, started
  F Prime GDS with that serial path, and observed sensor packets at F Prime.
- `START_COLLECTION` and `STOP_COLLECTION` were acknowledged by Zephyr before
  F Prime changed its collection state; ordered GDS events passed.
- After collection was restarted, terminating Zephyr caused `SensorLinkLost`
  within the test's eight-second limit.
- The strict C packet test passed round-trip, corrupt-CRC rejection, and
  stream recovery checks. The later fault scenario also injected corruption
  into the live virtual serial link.
- These tests exercise a simulated Linux UART, not physical hardware or
  real spacecraft sensors.

## Flight-computer slice

Environment: Ubuntu 24.04 under WSL2, F Prime v4.4.1, Python 3.12, GNU C++ 13.

- `fprime-util generate` completed in an Ubuntu-native build copy.
- `fprime-util build` completed for `StatusManager` and `FlightComputer`.
- `fprime-cli command-send --list` exposed `GET_STATUS`,
  `START_COLLECTION`, and `STOP_COLLECTION`.
- Runtime event log showed `StatusReported` with collection disabled and
  request count 1, `CollectionChanged` with collection enabled, then
  `StatusReported` with collection enabled and request count 2.
- The GDS CLI observed `CollectionEnabled` equal to 1 after starting
  collection.
- `tests/test_flight_computer_smoke.py` passed against the built deployment,
  checking all four runtime events in order.

After commit `5022f6b`, a fresh Ubuntu clone with recursive submodules was
built and `tests/test_flight_computer_smoke.py` passed again. This verifies
that the committed flight-computer sources and pinned F Prime submodule
reproduce on this Ubuntu environment.

## Embedded slice

Environment: Ubuntu 24.04 under WSL2, Zephyr v4.4.1,
`native_sim/native/64`, GNU C 13 with the host toolchain.

- `west build` completed without a Zephyr SDK or physical hardware.
- A three-second real-time run printed four `SAMPLE` records and two
  `HEALTH` records.
- The final health record reported 19 produced samples, 3 reported samples,
  no dropped samples, and 5 queued samples. This is a short smoke test, not a
  queue-pressure or timing-guarantee test.
- The sample values were synthetic: temperature 24.55–24.74 °C,
  battery 3.8 V, and fixed-point attitude values.
- `tests/test_embedded_smoke.py` passed against the running Zephyr executable.
- The runtime smoke test checked the diagnostic packet frames for message
  type, sequence, payload length, and CRC.
- `tests/packet_test.c` passed with `-Wall -Wextra -Werror`; it checked the
  CRC-16/CCITT-FALSE reference value, round trips, and rejection of corrupted,
  truncated, and oversized frames.
- The embedded application was rebuilt from the fresh Ubuntu clone and its
  smoke test passed again. Zephyr was installed separately at v4.4.1; it is
  not vendored into this repository.
- After commit `ce44669`, the fresh clone was updated, the packet-enabled
  Zephyr application rebuilt, and both the live frame test and strict C codec
  test passed again.
