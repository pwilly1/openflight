# Architecture

## Current system

```text
Ground operator -> F Prime GDS -> StatusManager -> framed command -> virtual UART
                                      ^                              |
                                      |                              v
                         events and telemetry <- validated frame <- Zephyr native_sim
                                                              sensor thread
                                                              bounded queue
                                                              fault controls
```

Zephyr produces synthetic fixed-point sensor values and sends bounded,
CRC-protected frames through a pseudoterminal UART. `StatusManager` validates
the stream, publishes sensor channels, sends collection and fault-injection
commands, and changes collection state only after a matching acknowledgment.

## Fault management

Zephyr can corrupt the next frame, drop a bounded number of frames, hold its
sensor values, or report low battery voltage. F Prime rejects bad CRCs and
enters safe mode for a critical low battery, a link timeout, a frozen sensor,
or three consecutive invalid frames. Sensor monitoring continues in safe mode
so recovery conditions can be measured.

Clearing the injected condition does not leave safe mode. F Prime first
requires three healthy samples, clears the active fault, and then requires an
explicit `RECOVER_SAFE_MODE` ground command. This separates evidence that the
sensor recovered from operator approval to resume normal operation.

The virtual transport can later be replaced by a UART-backed driver for a
selected Renode or physical target without changing the packet format.

## Scope limits

No orbital dynamics, real sensor measurements, hard real-time guarantee, or
physical-hardware validation are claimed. The simulated values and timing
measurements characterize this software on its test environment only.
