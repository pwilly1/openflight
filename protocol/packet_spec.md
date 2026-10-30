# OpenFlight link packet, version 0

This is the proposed binary framing for the future flight-computer to sensor
controller link. The current programs do not yet exchange packets.

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
structure. Stream resynchronization, sequence-gap handling, command payload
definitions, and timeout policy will be added with the transport adapter.

The sensor payload is currently 10 bytes: signed temperature in centi-°C,
signed roll/pitch/yaw in centi-degrees, and unsigned battery voltage in
millivolts, each a big-endian 16-bit integer. The Zephyr application emits
hexadecimal `FRAME` lines for inspection. Those lines are diagnostic output,
not yet a UART transport to the flight computer.
