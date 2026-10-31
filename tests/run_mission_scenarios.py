"""Run named OpenFlight mission scenarios against the real F Prime GDS."""

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import select
import signal
import subprocess
import tempfile
import time
from typing import Callable


COMMAND_PREFIX = "OpenFlight.statusManager."


@dataclass
class ScenarioResult:
    name: str
    passed: bool
    duration_seconds: float
    detail: str


class MissionHarness:
    def __init__(self, deployment: Path, zephyr_executable: Path):
        self.deployment = deployment.resolve()
        self.zephyr_executable = zephyr_executable.resolve()
        self.zephyr: subprocess.Popen[str] | None = None
        self.gds: subprocess.Popen[str] | None = None
        self.temp_directory: tempfile.TemporaryDirectory[str] | None = None
        self.log_path: Path | None = None
        self.log_offset = 0

    def __enter__(self) -> "MissionHarness":
        try:
            self.zephyr = subprocess.Popen(
                [str(self.zephyr_executable), "--rt"],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                start_new_session=True,
            )
            assert self.zephyr.stdout is not None
            ready, _, _ = select.select([self.zephyr.stdout], [], [], 10)
            if not ready:
                raise AssertionError("Zephyr did not announce its UART within 10 seconds")
            first_line = self.zephyr.stdout.readline()
            match = re.search(r"(/dev/pts/\d+)", first_line)
            if not match:
                raise AssertionError(f"Zephyr did not announce its UART: {first_line!r}")

            self.temp_directory = tempfile.TemporaryDirectory(prefix="openflight-gds-")
            self.log_path = Path(self.temp_directory.name) / "OpenFlight_FlightComputer.log"
            self.gds = subprocess.Popen(
                [
                    "fprime-gds",
                    "-g",
                    "none",
                    "--logs",
                    self.temp_directory.name,
                    "--log-directly",
                ],
                cwd=self.deployment,
                env={**os.environ, "OPENFLIGHT_SENSOR_PTY": match.group(1)},
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            self.wait_for("Connected to", 20)
            return self
        except Exception:
            self.close()
            raise

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close()

    def close(self) -> None:
        self._stop_process(self.gds)
        self._stop_process(self.zephyr)
        if self.temp_directory is not None:
            self.temp_directory.cleanup()
            self.temp_directory = None
        time.sleep(0.3)

    @staticmethod
    def _stop_process(process: subprocess.Popen[str] | None) -> None:
        if process is None:
            return
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=5)

    def log(self) -> str:
        assert self.log_path is not None
        return self.log_path.read_text(errors="replace") if self.log_path.exists() else ""

    def wait_for(self, expected: str, timeout: float = 12) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            content = self.log()
            if expected in content[self.log_offset:]:
                return content
            if self.gds is not None and self.gds.poll() is not None:
                raise AssertionError(
                    f"F Prime GDS exited with code {self.gds.returncode} while waiting "
                    f"for {expected!r}:\n{content}"
                )
            time.sleep(0.2)
        raise AssertionError(f"Timed out waiting for {expected!r}:\n{self.log()}")

    def begin_scenario(self) -> None:
        self.log_offset = len(self.log())

    def scenario_log(self) -> str:
        return self.log()[self.log_offset:]

    def send(self, command: str, *arguments: str) -> None:
        invocation = ["fprime-cli", "command-send", f"{COMMAND_PREFIX}{command}"]
        if arguments:
            invocation.extend(["--arguments", *arguments])
        subprocess.run(
            invocation,
            cwd=self.deployment,
            check=True,
            timeout=15,
            stdout=subprocess.DEVNULL,
        )

    def start_collection(self) -> None:
        self.send("START_COLLECTION")
        self.wait_for("CollectionChanged : Collection enabled: 1")
        self.wait_for("SensorReceived")


def nominal_mission(harness: MissionHarness) -> None:
    harness.send("GET_STATUS")
    harness.wait_for("StatusReported : Collection enabled: 0; status requests: 1")
    harness.start_collection()
    harness.send("STOP_COLLECTION")
    harness.wait_for("CollectionChanged : Collection enabled: 0")


def corrupted_packet(harness: MissionHarness) -> None:
    harness.start_collection()
    harness.send("INJECT_CORRUPT_PACKET")
    harness.wait_for("Fault injection code: 3; value: 1")
    harness.wait_for("Sensor packet rejected: -5")
    if "Safe mode entered; reason: 4" in harness.scenario_log():
        raise AssertionError("A single corrupt packet incorrectly entered safe mode")
    harness.send("STOP_COLLECTION")
    harness.wait_for("CollectionChanged : Collection enabled: 0")


def low_battery_emergency(harness: MissionHarness) -> None:
    harness.start_collection()
    harness.send("INJECT_LOW_BATTERY")
    harness.wait_for("Safe mode entered; reason: 1")
    harness.send("RECOVER_SAFE_MODE")
    harness.wait_for("Safe-mode recovery rejected")
    harness.send("RESTORE_SENSOR")
    harness.wait_for("Fault cleared after healthy samples; reason: 1")
    harness.send("RECOVER_SAFE_MODE")
    harness.wait_for("Safe mode exited by operator command")
    harness.send("STOP_COLLECTION")
    harness.wait_for("CollectionChanged : Collection enabled: 0")


def frozen_sensor(harness: MissionHarness) -> None:
    harness.start_collection()
    harness.send("INJECT_FREEZE_SENSOR")
    harness.wait_for("Safe mode entered; reason: 3")
    harness.send("RESTORE_SENSOR")
    harness.wait_for("Fault cleared after healthy samples; reason: 3")
    harness.send("RECOVER_SAFE_MODE")
    harness.wait_for("Safe mode exited by operator command")
    harness.send("STOP_COLLECTION")
    harness.wait_for("CollectionChanged : Collection enabled: 0")


def communication_outage(harness: MissionHarness) -> None:
    harness.start_collection()
    harness.send("INJECT_DROP_PACKETS", "8")
    harness.wait_for("Safe mode entered; reason: 2")
    harness.wait_for("Sensor link restored")
    harness.wait_for("Fault cleared after healthy samples; reason: 2")
    harness.send("RECOVER_SAFE_MODE")
    harness.wait_for("Safe mode exited by operator command")
    harness.send("STOP_COLLECTION")
    harness.wait_for("CollectionChanged : Collection enabled: 0")


SCENARIOS: dict[str, Callable[[MissionHarness], None]] = {
    "nominal": nominal_mission,
    "corrupted-packet": corrupted_packet,
    "low-battery": low_battery_emergency,
    "frozen-sensor": frozen_sensor,
    "communication-outage": communication_outage,
}


def render_report(results: list[ScenarioResult]) -> str:
    generated = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    lines = [
        "# OpenFlight mission scenario report",
        "",
        f"Generated: {generated}",
        "",
        "| Scenario | Result | Duration | Detail |",
        "| --- | --- | ---: | --- |",
    ]
    for result in results:
        status = "PASS" if result.passed else "FAIL"
        detail = result.detail.replace("|", "\\|").replace("\n", " ")
        lines.append(
            f"| {result.name} | {status} | {result.duration_seconds:.1f}s | {detail} |"
        )
    passed = sum(result.passed for result in results)
    lines.extend(["", f"Summary: {passed}/{len(results)} scenarios passed.", ""])
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("deployment", type=Path, help="Built FlightComputer source directory")
    parser.add_argument("--zephyr", type=Path, required=True, help="Built native Zephyr executable")
    parser.add_argument(
        "--scenario",
        choices=["all", *SCENARIOS],
        default="all",
        help="Mission scenario to run",
    )
    parser.add_argument("--report", type=Path, help="Optional Markdown report path")
    args = parser.parse_args()

    selected = list(SCENARIOS) if args.scenario == "all" else [args.scenario]
    results: list[ScenarioResult] = []
    try:
        with MissionHarness(args.deployment, args.zephyr) as harness:
            for index, name in enumerate(selected):
                harness.begin_scenario()
                started = time.monotonic()
                try:
                    SCENARIOS[name](harness)
                    result = ScenarioResult(name, True, time.monotonic() - started, "Completed")
                except Exception as error:
                    result = ScenarioResult(name, False, time.monotonic() - started, str(error))
                results.append(result)
                print(f"[{('PASS' if result.passed else 'FAIL')}] {name} ({result.duration_seconds:.1f}s)")
                if not result.passed:
                    for skipped in selected[index + 1:]:
                        results.append(ScenarioResult(skipped, False, 0.0, "Not run after earlier failure"))
                    break
    except Exception as error:
        if not results:
            results = [ScenarioResult(name, False, 0.0, f"Harness startup failed: {error}") for name in selected]

    report = render_report(results)
    print("\n" + report)
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
        print(f"Report written to {args.report}")
    if not all(result.passed for result in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
