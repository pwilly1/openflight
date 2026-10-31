"""Exercise a built F Prime deployment through its real ground system."""

import argparse
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import re


def wait_for_text(path: Path, expected: str, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            content = path.read_text(errors="replace")
            if expected in content:
                return content
        time.sleep(0.2)
    content = path.read_text(errors="replace") if path.exists() else "<no log>"
    raise AssertionError(f"Timed out waiting for {expected!r} in {path}:\n{content}")


def wait_for_count(path: Path, expected: str, count: int, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        content = path.read_text(errors="replace") if path.exists() else ""
        if content.count(expected) >= count:
            return content
        time.sleep(0.2)
    content = path.read_text(errors="replace") if path.exists() else "<no log>"
    raise AssertionError(
        f"Timed out waiting for {count} occurrences of {expected!r} in {path}:\n{content}"
    )


def send_command(deployment: Path, command: str, *arguments: str) -> None:
    invocation = ["fprime-cli", "command-send", f"OpenFlight.statusManager.{command}"]
    if arguments:
        invocation.extend(["--arguments", *arguments])
    subprocess.run(
        invocation, cwd=deployment, check=True, timeout=15,
        stdout=subprocess.DEVNULL,
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("deployment", type=Path, help="Built FlightComputer source directory")
    parser.add_argument("--zephyr", type=Path, required=True, help="Built native Zephyr executable")
    args = parser.parse_args()
    deployment = args.deployment.resolve()
    zephyr = subprocess.Popen(
        [str(args.zephyr.resolve()), "--rt"], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, bufsize=1, start_new_session=True,
    )
    assert zephyr.stdout is not None
    first_line = zephyr.stdout.readline()
    match = re.search(r"(/dev/pts/\d+)", first_line)
    if not match:
        raise AssertionError(f"Zephyr did not announce its UART: {first_line!r}")

    with tempfile.TemporaryDirectory(prefix="openflight-gds-") as temp:
        log_path = Path(temp) / "OpenFlight_FlightComputer.log"
        process = subprocess.Popen(
            ["fprime-gds", "-g", "none", "--logs", temp, "--log-directly"],
            cwd=deployment,
            env={**os.environ, "OPENFLIGHT_SENSOR_PTY": match.group(1)},
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        try:
            wait_for_text(log_path, "Connected to", 20)
            for command, confirmation in (
                ("GET_STATUS", "StatusReported : Collection enabled: 0; status requests: 1"),
                ("START_COLLECTION", "CollectionChanged : Collection enabled: 1"),
                ("GET_STATUS", "StatusReported : Collection enabled: 1; status requests: 2"),
                ("STOP_COLLECTION", "CollectionChanged : Collection enabled: 0"),
            ):
                send_command(deployment, command)
                wait_for_text(log_path, confirmation, 12)
            log = wait_for_text(log_path, "CollectionChanged : Collection enabled: 0", 12)
            log = wait_for_text(log_path, "SensorReceived", 12)
            expected = (
                "StatusReported : Collection enabled: 0; status requests: 1",
                "CollectionChanged : Collection enabled: 1",
                "StatusReported : Collection enabled: 1; status requests: 2",
                "CollectionChanged : Collection enabled: 0",
            )
            positions = [log.find(item) for item in expected]
            if -1 in positions or positions != sorted(positions):
                raise AssertionError(f"Missing or out-of-order runtime events:\n{log}")
            send_command(deployment, "START_COLLECTION")
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                log = log_path.read_text(errors="replace")
                if log.count("CollectionChanged : Collection enabled: 1") >= 2:
                    break
                time.sleep(0.2)
            else:
                raise AssertionError("Second START_COLLECTION was not acknowledged")

            send_command(deployment, "INJECT_CORRUPT_PACKET")
            wait_for_text(log_path, "Fault injection code: 3; value: 1", 12)
            wait_for_text(log_path, "Sensor packet rejected: -5", 12)

            send_command(deployment, "INJECT_LOW_BATTERY")
            wait_for_text(log_path, "Fault injection code: 5; value: 1", 12)
            wait_for_text(log_path, "Safe mode entered; reason: 1", 12)
            send_command(deployment, "RECOVER_SAFE_MODE")
            wait_for_text(log_path, "Safe-mode recovery rejected", 12)
            send_command(deployment, "RESTORE_SENSOR")
            wait_for_text(log_path, "Fault injection code: 7; value: 0", 12)
            wait_for_text(log_path, "Fault cleared after healthy samples; reason: 1", 12)
            send_command(deployment, "RECOVER_SAFE_MODE")
            wait_for_count(log_path, "Safe mode exited by operator command", 1, 12)

            send_command(deployment, "INJECT_FREEZE_SENSOR")
            wait_for_text(log_path, "Fault injection code: 6; value: 1", 12)
            wait_for_text(log_path, "Safe mode entered; reason: 3", 12)
            send_command(deployment, "RESTORE_SENSOR")
            wait_for_text(log_path, "Fault cleared after healthy samples; reason: 3", 12)
            send_command(deployment, "RECOVER_SAFE_MODE")
            wait_for_count(log_path, "Safe mode exited by operator command", 2, 12)

            send_command(deployment, "INJECT_DROP_PACKETS", "8")
            wait_for_text(log_path, "Fault injection code: 4; value: 8", 12)
            wait_for_text(log_path, "Safe mode entered; reason: 2", 12)
            wait_for_text(log_path, "Sensor link restored", 12)
            wait_for_text(log_path, "Fault cleared after healthy samples; reason: 2", 12)
            send_command(deployment, "RECOVER_SAFE_MODE")
            wait_for_count(log_path, "Safe mode exited by operator command", 3, 12)

            os.killpg(zephyr.pid, signal.SIGTERM)
            zephyr.wait(timeout=5)
            wait_for_count(log_path, "SensorLinkLost", 2, 8)
            print("F Prime/Zephyr fault injection and safe-mode scenario passed")
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
            if zephyr.poll() is None:
                os.killpg(zephyr.pid, signal.SIGTERM)
            zephyr.wait(timeout=5)


if __name__ == "__main__":
    main()
