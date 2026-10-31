# Mission dashboard

The local dashboard turns the F Prime channel, event, and command streams into
a focused demonstration interface. It is served only on `127.0.0.1` and does
not expose the simulator to the network.

Start the complete system from PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\Start-OpenFlight.ps1
```

The launcher opens the dashboard at `http://127.0.0.1:8000`. The standard
F Prime interface remains available at `http://127.0.0.1:5000` for dictionary,
channel, event, and command inspection.

## Controls

The five scenario buttons start collection and then dispatch the selected
stimulus through `OpenFlight.statusManager`:

| Dashboard scenario | F Prime stimulus | Expected behavior |
| --- | --- | --- |
| Nominal acquisition | None | Valid sensor telemetry continues in normal mode. |
| Corrupted packet | `INJECT_CORRUPT_PACKET` | One invalid frame is rejected without entering safe mode. |
| Low-battery emergency | `INJECT_LOW_BATTERY` | Repeated 3.10 V samples cause safe mode with reason 1. |
| Frozen sensor | `INJECT_FREEZE_SENSOR` | Repeated values cause safe mode with reason 3. |
| Communication outage | `INJECT_DROP_PACKETS 8` | Link timeout causes safe mode with reason 2. |

**Restore sensors and recover** dispatches `RESTORE_SENSOR`, waits four seconds
for at least three healthy samples, and then dispatches `RECOVER_SAFE_MODE`.
The flight software still makes the final recovery decision. A dashboard
button cannot bypass its guard conditions.

## Implementation boundary

`dashboard/server.py` is a standard-library HTTP server. It subscribes to
newline-delimited F Prime channel JSON and to the F Prime text event stream.
F Prime 4.4.1 event JSON is not used because typed event arguments can fail
serialization in that version. Commands are restricted to a fixed allowlist;
the browser cannot execute arbitrary shell commands.

All displayed spacecraft values, faults, and recovery actions are simulated.
The dashboard does not prove physical sensor accuracy, flight timing, or
hardware behavior.
