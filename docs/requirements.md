# Requirements and traceability

| ID | Requirement | Current verification | State |
| --- | --- | --- | --- |
| OF-001 | The flight computer accepts a ground status command. | GDS command-list and runtime command test | Implemented |
| OF-002 | A status command publishes collection state and request count. | Runtime event and channel observation | Implemented |
| OF-003 | Ground commands can enable and disable the local collection flag. | Runtime event sequence | Implemented |
| OF-004 | Embedded firmware produces synthetic sensor samples on a schedule. | Zephyr `native_sim` run | Implemented |
| OF-005 | Embedded data passes through a bounded queue. | Zephyr health output showed queue use; queue-pressure test remains | Partially verified |
| OF-005A | Embedded sensor samples are encoded in bounded, CRC-protected frames. | Host codec test and live native_sim frame checks | Implemented |
| OF-006 | Flight computer receives and validates embedded packets. | Serial integration test and bad-CRC event | Implemented |
| OF-007 | Critical faults cause safe-mode entry. | Low-battery, frozen-sensor, and link-timeout scenarios | Implemented |
| OF-008 | Recovery requires valid conditions and an explicit command. | Rejected early recovery and three successful guarded recoveries | Implemented |
| OF-009 | Ground commands can inject bounded, repeatable sensor faults. | Corrupt, drop, low-battery, freeze, and restore command acknowledgments | Implemented |
| OF-010 | Mission behavior is repeatable as independently reported scenarios. | Five-scenario runner and Markdown report | Implemented |
| OF-011 | Pushes and pull requests repeat host builds and mission scenarios. | GitHub Actions workflow | Implemented; first remote run pending |

“Implemented” refers only to the listed slice, not the full proposed system.
