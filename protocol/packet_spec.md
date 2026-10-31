# OpenFlight link packet, version 0

This binary frame is exchanged between the F Prime flight computer and the
Zephyr simulated sensor controller over a pseudoterminal UART.

| Field | Bytes | Encoding |
| --- | ---: | --- |
| Start marker | 2 | `A5 5A` |
| Message type | 1 | 1 sensor, 2 command, 3 heartbeat, 4 acknowledgment |
| Sequence | 2 | Unsigned, big-endian; wraps after 65535 |
| Payload length | 2 | Unsigned, big-endian; 0–32 bytes |
| Payload | 0–32 | Type-specific bytes |
| CRC | 2 | CRC-16/CCITT-FALSE, big-endian |

The CRC uses polynomial `0x1021`, initialization `0xFFFF`, no reflection,
and no final XOR. It covers the type, sequence, length, and payload; it does
not cover the start marker. The standard check string `123456789` yields
`0x29B1`.

Decoders reject unknown types, overlong or incomplete frames, mismatched
lengths, and bad CRCs before copying payload bytes into the fixed-size packet
structure. The stream parser searches for the marker and rejects bad lengths
or CRCs. Sequence-gap accounting is future work. A sensor timeout is reported
after three seconds while collection is enabled.

The first command payload byte is a command code:

| Code | Command | Additional payload |
| ---: | --- | --- |
| 1 | Start collection | None |
| 2 | Stop collection | None |
| 3 | Corrupt the next sensor packet | None |
| 4 | Drop sensor packets | One byte containing a nonzero packet count |
| 5 | Report low battery voltage | None |
| 6 | Freeze sensor values | None |
| 7 | Restore nominal sensor behavior | None |

Zephyr replies with an acknowledgment using the command sequence and a
two-byte payload: command code, then `1` if accepted or `0` if rejected.
F Prime changes collection or fault-injection state only after a matching
acknowledgment.

The sensor payload is currently 10 bytes: signed temperature in centi-°C,
signed roll/pitch/yaw in centi-degrees, and unsigned battery voltage in
millivolts, each a big-endian 16-bit integer. Zephyr sends frames on its UART
and prints hexadecimal `FRAME` lines for inspection.
