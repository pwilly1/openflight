#!/usr/bin/env python3
"""Serve the OpenFlight dashboard and bridge it to a running F Prime GDS."""

from __future__ import annotations

import argparse
from collections import deque
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import threading
import time
from typing import Any, Callable


COMPONENT = "OpenFlight.statusManager"
STATIC_DIRECTORY = Path(__file__).with_name("static")
FAULT_NAMES = {
    0: "None",
    1: "Low battery",
    2: "Communication outage",
    3: "Frozen sensor",
    4: "Invalid packets",
}
COMMAND_ARGUMENTS: dict[str, list[str]] = {
    "GET_STATUS": [],
    "START_COLLECTION": [],
    "STOP_COLLECTION": [],
    "INJECT_CORRUPT_PACKET": [],
    "INJECT_DROP_PACKETS": ["8"],
    "INJECT_LOW_BATTERY": [],
    "INJECT_FREEZE_SENSOR": [],
    "RESTORE_SENSOR": [],
    "RECOVER_SAFE_MODE": [],
}
SCENARIO_COMMANDS = {
    "nominal": None,
    "corrupted-packet": "INJECT_CORRUPT_PACKET",
    "low-battery": "INJECT_LOW_BATTERY",
    "frozen-sensor": "INJECT_FREEZE_SENSOR",
    "communication-outage": "INJECT_DROP_PACKETS",
}
EVENT_PATTERN = re.compile(
    r"^(?P<time>\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?)"
    r": (?P<name>\S+) \(\d+\) .* EventSeverity\.(?P<severity>\S+) : (?P<message>.*)$"
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def parse_channel_line(line: str) -> tuple[str, Any, str] | None:
    """Parse one observed ``fprime-cli channels -j`` output line."""
    line = line.strip()
    if not line.startswith("{"):
        return None
    try:
        payload = json.loads(line)
    except json.JSONDecodeError:
        return None
    name = payload.get("name")
    if not isinstance(name, str) or not name.startswith(f"{COMPONENT}."):
        return None
    return name.rsplit(".", 1)[-1], payload.get("display_text"), str(payload.get("time", ""))


def parse_event_line(line: str) -> dict[str, str] | None:
    """Parse the stable text form used because F Prime 4.4 event JSON can fail."""
    match = EVENT_PATTERN.match(line.strip())
    if match is None:
        return None
    name = match.group("name")
    if not name.startswith(f"{COMPONENT}."):
        return None
    return {
        "time": match.group("time"),
        "name": name.rsplit(".", 1)[-1],
        "severity": match.group("severity"),
        "message": match.group("message"),
        "source": "F Prime",
    }


class DashboardState:
    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.telemetry: dict[str, Any] = {}
        self.events: deque[dict[str, Any]] = deque(maxlen=80)
        self.streams = {"channels": False, "events": False}
        self.last_update: str | None = None
        self.active_scenario = "standby"
        self.event_sequence = 0

    def set_stream(self, stream: str, active: bool) -> None:
        with self.lock:
            self.streams[stream] = active

    def update_channel(self, parsed: tuple[str, Any, str]) -> None:
        name, value, timestamp = parsed
        with self.lock:
            self.telemetry[name] = value
            self.last_update = timestamp or utc_now()

    def add_event(self, event: dict[str, Any]) -> None:
        if event.get("name") == "SensorReceived":
            return
        with self.lock:
            self.event_sequence += 1
            event = {"id": self.event_sequence, **event}
            self.events.appendleft(event)

    def add_ground_event(self, name: str, message: str, severity: str = "ACTIVITY_HI") -> None:
        self.add_event(
            {
                "time": utc_now(),
                "name": name,
                "severity": severity,
                "message": message,
                "source": "Dashboard",
            }
        )

    def set_scenario(self, scenario: str) -> None:
        with self.lock:
            self.active_scenario = scenario

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            telemetry = dict(self.telemetry)
            mode_value = telemetry.get("OperatingMode")
            fault_value = telemetry.get("ActiveFault")
            return {
                "connected": all(self.streams.values()),
                "streams": dict(self.streams),
                "last_update": self.last_update,
                "active_scenario": self.active_scenario,
                "mode": "SAFE" if mode_value == 1 else "NOMINAL" if mode_value == 0 else "UNKNOWN",
                "fault": FAULT_NAMES.get(fault_value, "Unknown") if fault_value is not None else "Unknown",
                "telemetry": telemetry,
                "events": list(self.events),
            }


class StreamWorker(threading.Thread):
    def __init__(
        self,
        name: str,
        command: list[str],
        deployment: Path,
        parser: Callable[[str], Any],
        consumer: Callable[[Any], None],
        state: DashboardState,
        stopping: threading.Event,
    ) -> None:
        super().__init__(name=f"openflight-{name}", daemon=True)
        self.stream_name = name
        self.command = command
        self.deployment = deployment
        self.parser = parser
        self.consumer = consumer
        self.state = state
        self.stopping = stopping
        self.process: subprocess.Popen[str] | None = None

    def run(self) -> None:
        while not self.stopping.is_set():
            try:
                environment = {**os.environ, "PYTHONUNBUFFERED": "1"}
                self.process = subprocess.Popen(
                    self.command,
                    cwd=self.deployment,
                    env=environment,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                )
                self.state.set_stream(self.stream_name, True)
                assert self.process.stdout is not None
                for line in self.process.stdout:
                    if self.stopping.is_set():
                        break
                    parsed = self.parser(line)
                    if parsed is not None:
                        self.consumer(parsed)
            except (OSError, subprocess.SubprocessError) as error:
                self.state.add_ground_event(
                    "StreamError", f"{self.stream_name.title()} stream: {error}", "WARNING_HI"
                )
            finally:
                self.state.set_stream(self.stream_name, False)
                if self.process is not None and self.process.poll() is None:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                self.process = None
            if not self.stopping.wait(1):
                self.state.add_ground_event(
                    "StreamReconnect", f"Reconnecting {self.stream_name} stream", "WARNING_LO"
                )

    def stop(self) -> None:
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()


class GroundBridge:
    def __init__(self, deployment: Path, state: DashboardState) -> None:
        self.deployment = deployment
        self.state = state
        self.stopping = threading.Event()
        self.command_lock = threading.Lock()
        self.scenario_lock = threading.Lock()
        self.workers = [
            StreamWorker(
                "channels",
                ["fprime-cli", "channels", "-c", COMPONENT, "-j"],
                deployment,
                parse_channel_line,
                state.update_channel,
                state,
                self.stopping,
            ),
            StreamWorker(
                "events",
                ["fprime-cli", "events", "-c", COMPONENT],
                deployment,
                parse_event_line,
                state.add_event,
                state,
                self.stopping,
            ),
        ]

    def start(self) -> None:
        for worker in self.workers:
            worker.start()
        time.sleep(0.5)
        try:
            self.send_command("GET_STATUS")
        except RuntimeError as error:
            self.state.add_ground_event("Startup", str(error), "WARNING_LO")

    def stop(self) -> None:
        self.stopping.set()
        for worker in self.workers:
            worker.stop()
        for worker in self.workers:
            worker.join(timeout=3)

    def send_command(self, name: str) -> None:
        if name not in COMMAND_ARGUMENTS:
            raise ValueError(f"Unsupported command: {name}")
        command = ["fprime-cli", "command-send", f"{COMPONENT}.{name}"]
        arguments = COMMAND_ARGUMENTS[name]
        if arguments:
            command.extend(["--arguments", *arguments])
        with self.command_lock:
            completed = subprocess.run(
                command,
                cwd=self.deployment,
                text=True,
                capture_output=True,
                timeout=15,
                check=False,
            )
        if completed.returncode != 0:
            detail = completed.stderr.strip() or completed.stdout.strip() or "unknown GDS error"
            raise RuntimeError(f"{name} failed: {detail}")
        self.state.add_ground_event("CommandDispatched", name.replace("_", " ").title())

    def run_scenario(self, scenario: str) -> None:
        if scenario not in SCENARIO_COMMANDS:
            raise ValueError(f"Unsupported scenario: {scenario}")
        if not self.scenario_lock.acquire(blocking=False):
            raise RuntimeError("Another scenario action is still running")
        try:
            self.state.set_scenario(scenario)
            self.state.add_ground_event("ScenarioStarted", scenario.replace("-", " ").title())
            self.send_command("START_COLLECTION")
            stimulus = SCENARIO_COMMANDS[scenario]
            if stimulus is not None:
                time.sleep(1.25)
                self.send_command(stimulus)
        finally:
            self.scenario_lock.release()

    def recover(self) -> None:
        if not self.scenario_lock.acquire(blocking=False):
            raise RuntimeError("Another scenario action is still running")
        try:
            self.state.add_ground_event("RecoveryStarted", "Restoring healthy sensor values")
            self.send_command("RESTORE_SENSOR")
            time.sleep(4)
            self.send_command("RECOVER_SAFE_MODE")
            self.state.set_scenario("standby")
        finally:
            self.scenario_lock.release()


class DashboardHandler(SimpleHTTPRequestHandler):
    bridge: GroundBridge
    state: DashboardState

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        super().__init__(*args, directory=str(STATIC_DIRECTORY), **kwargs)

    def log_message(self, format: str, *args: Any) -> None:
        return

    def send_json(self, payload: Any, status: HTTPStatus = HTTPStatus.OK) -> None:
        encoded = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self) -> None:
        if self.path == "/api/state":
            self.send_json(self.state.snapshot())
            return
        if self.path == "/health":
            self.send_json({"status": "ok"})
            return
        super().do_GET()

    def do_POST(self) -> None:
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 4096:
                raise ValueError("Invalid request body")
            payload = json.loads(self.rfile.read(length))
            if not isinstance(payload, dict):
                raise ValueError("Request body must be a JSON object")
            if self.path == "/api/command":
                self.bridge.send_command(str(payload.get("command", "")))
            elif self.path == "/api/scenario":
                self.bridge.run_scenario(str(payload.get("scenario", "")))
            elif self.path == "/api/recover":
                self.bridge.recover()
            else:
                self.send_json({"error": "Not found"}, HTTPStatus.NOT_FOUND)
                return
            self.send_json({"ok": True, "state": self.state.snapshot()})
        except (ValueError, RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as error:
            self.state.add_ground_event("ActionFailed", str(error), "WARNING_HI")
            self.send_json({"error": str(error)}, HTTPStatus.BAD_REQUEST)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deployment", type=Path, required=True)
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    deployment = args.deployment.resolve()
    if not deployment.is_dir():
        raise SystemExit(f"Deployment directory not found: {deployment}")
    if shutil.which("fprime-cli") is None:
        raise SystemExit("fprime-cli is not available; activate the OpenFlight F Prime environment")

    state = DashboardState()
    bridge = GroundBridge(deployment, state)
    DashboardHandler.bridge = bridge
    DashboardHandler.state = state
    server = ThreadingHTTPServer(("127.0.0.1", args.port), DashboardHandler)
    server.daemon_threads = True

    def stop_server(signum: int, frame: Any) -> None:
        threading.Thread(target=server.shutdown, daemon=True).start()

    signal.signal(signal.SIGTERM, stop_server)
    signal.signal(signal.SIGINT, stop_server)

    bridge.start()
    print(f"OpenFlight dashboard ready at http://127.0.0.1:{args.port}", flush=True)
    try:
        server.serve_forever(poll_interval=0.25)
    finally:
        server.server_close()
        bridge.stop()


if __name__ == "__main__":
    main()
