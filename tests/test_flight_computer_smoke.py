"""Exercise a built F Prime deployment through its real ground system."""

import argparse
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time


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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("deployment", type=Path, help="Built FlightComputer source directory")
    args = parser.parse_args()
    deployment = args.deployment.resolve()

    with tempfile.TemporaryDirectory(prefix="openflight-gds-") as temp:
        log_path = Path(temp) / "OpenFlight_FlightComputer.log"
        process = subprocess.Popen(
            ["fprime-gds", "-g", "none", "--logs", temp, "--log-directly"],
            cwd=deployment,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        try:
            wait_for_text(log_path, "Connected to", 20)
            for command in (
                "GET_STATUS",
                "START_COLLECTION",
                "GET_STATUS",
                "STOP_COLLECTION",
            ):
                subprocess.run(
                    ["fprime-cli", "command-send", f"OpenFlight.statusManager.{command}"],
                    cwd=deployment,
                    check=True,
                    timeout=15,
                    stdout=subprocess.DEVNULL,
                )
            log = wait_for_text(
                log_path, "CollectionChanged : Collection enabled: 0", 10
            )
            expected = (
                "StatusReported : Collection enabled: 0; status requests: 1",
                "CollectionChanged : Collection enabled: 1",
                "StatusReported : Collection enabled: 1; status requests: 2",
                "CollectionChanged : Collection enabled: 0",
            )
            positions = [log.find(item) for item in expected]
            if -1 in positions or positions != sorted(positions):
                raise AssertionError(f"Missing or out-of-order runtime events:\n{log}")
            print("F Prime command and event smoke test passed")
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)


if __name__ == "__main__":
    main()
