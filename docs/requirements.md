# Requirements and traceability

| ID | Requirement | Current verification | State |
| --- | --- | --- | --- |
| OF-001 | The flight computer accepts a ground status command. | GDS command-list and runtime command test | Implemented |
| OF-002 | A status command publishes collection state and request count. | Runtime event and channel observation | Implemented |
| OF-003 | Ground commands can enable and disable the local collection flag. | Runtime event sequence | Implemented |
| OF-004 | Embedded firmware produces synthetic sensor samples on a schedule. | Zephyr `native_sim` run | Implemented |
| OF-005 | Embedded data passes through a bounded queue. | Zephyr health output showed queue use; queue-pressure test remains | Partially verified |
| OF-005A | Embedded sensor samples are encoded in bounded, CRC-protected frames. | Host codec test and live native_sim frame checks | Implemented |
| OF-006 | Flight computer receives and validates embedded packets. | Protocol integration test | Planned |
| OF-007 | Critical faults cause safe-mode entry. | Fault injection test | Planned |
| OF-008 | Recovery requires valid conditions and an explicit command. | Recovery integration test | Planned |

“Implemented” refers only to the listed slice, not the full proposed system.
