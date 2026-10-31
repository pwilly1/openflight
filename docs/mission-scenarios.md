# Mission scenarios

The scenario runner exercises the real Zephyr simulator, virtual UART, F Prime
deployment, command path, event log, and safe-mode logic in one session.

| Scenario | Stimulus | Required result |
| --- | --- | --- |
| `nominal` | Start and stop sensor collection | Sensor telemetry arrives and collection changes are acknowledged. |
| `corrupted-packet` | Corrupt one frame after its CRC is calculated | F Prime rejects the frame and remains in normal mode. |
| `low-battery` | Hold battery voltage at 3100 mV | F Prime enters safe mode with reason 1, rejects early recovery, and requires three healthy samples plus operator recovery. |
| `frozen-sensor` | Hold every sensor value constant while sequences advance | F Prime enters safe mode with reason 3 and requires guarded recovery. |
| `communication-outage` | Drop eight consecutive sensor frames | F Prime detects the timeout, enters safe mode with reason 2, detects link restoration, and requires guarded recovery. |

Run every scenario:

```bash
python3 tests/run_mission_scenarios.py OpenFlight/FlightComputer \
  --zephyr /path/to/zephyr.exe \
  --report build-artifacts/mission-scenario-report.md
```

Use `--scenario` with one of the names in the table to run a single case. The
runner returns a nonzero exit code if a scenario fails, prints a compact table,
and can save the same results as Markdown for CI or application evidence.

Fault reason 4 represents three consecutive invalid packets. The codec and
single-corruption path are tested, but the three-packet threshold remains a
separate verification item.
