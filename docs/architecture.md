# Architecture

## Current system

```text
Ground operator -> F Prime GDS -> FlightComputer deployment -> StatusManager
                                                          commands, events, telemetry

Zephyr native_sim -> sensor thread -> bounded message queue -> telemetry thread
                                      health thread monitors counters
```

The F Prime deployment and Zephyr application currently run independently.
`StatusManager` owns only the collection flag and request count. The Zephyr
application owns synthetic sensor values and its local fixed-size queue.

## Intended integration

The flight computer will host a sensor-link component that validates and
parses framed messages from the embedded controller. That component will
publish accepted measurements through F Prime telemetry channels. The link
will use a virtual transport during `native_sim` testing and a UART-backed
transport for a selected Renode target. A state manager and fault monitor will
enforce safe-mode behavior after validated fault conditions.

The binary framing, byte order, length limits, checksum, sequence policy, and
timeouts will be specified before either side implements the link.

## Scope limits

No orbital dynamics, real sensor measurements, hard real-time guarantee, or
physical-hardware validation are claimed. The simulated values and timing
measurements characterize this software on its test environment only.
