# OpenFlight::StatusManager

CubeSat sensor-link, status, and fault manager

## Introduction

`StatusManager` connects the F Prime deployment to the Zephyr sensor
simulator. It owns the pseudoterminal adapter, validates bounded packets,
publishes sensor telemetry, sends acknowledged control commands, and enforces
the safe-mode recovery gate.

## Requirements

| Name | Description | Rationale | Validation |
|---|---|---|---|
| SM-001 | Reject malformed or bad-CRC sensor frames. | Prevent corrupted measurements from entering telemetry. | Packet and serial integration tests. |
| SM-002 | Enter safe mode for critical or persistent sensor faults. | Preserve a deterministic response to loss of trustworthy data. | Low-battery, frozen-sensor, and link-timeout scenarios. |
| SM-003 | Require three healthy samples and an operator command to recover. | Prevent automatic return to normal operation after a transient recovery. | Early-recovery rejection and guarded-recovery scenarios. |

## Design

The 1 Hz scheduled handler reads all available serial bytes through the shared
stream parser. Sensor values use fixed-size fields and no packet-facing dynamic
allocation. Fault reasons are numbered in telemetry: 0 none, 1 low battery,
2 link loss, 3 frozen sensor, and 4 repeated bad packets.

Safe mode does not disable sensor monitoring because the component needs fresh
measurements to establish recovery. A fault clears after three consecutive
healthy sensor packets, but the operating mode remains safe until
`RECOVER_SAFE_MODE` succeeds.

## Configuration

Set `OPENFLIGHT_SENSOR_PTY` to the pseudoterminal path printed by Zephyr before
starting F Prime GDS. The current adapter is Linux-specific.
