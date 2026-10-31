#!/usr/bin/env bash

set -euo pipefail

OPENFLIGHT_LINUX_USER="${USER:-prwil}"
OPENFLIGHT_SOURCE_DIR="${OPENFLIGHT_SOURCE_DIR:-/home/${OPENFLIGHT_LINUX_USER}/openflight-clean}"
OPENFLIGHT_ZEPHYR_EXECUTABLE="${OPENFLIGHT_ZEPHYR_EXECUTABLE:-/home/${OPENFLIGHT_LINUX_USER}/openflight-clean-embedded-build/zephyr/zephyr.exe}"
OPENFLIGHT_FPRIME_VENV="${OPENFLIGHT_FPRIME_VENV:-/home/${OPENFLIGHT_LINUX_USER}/.venvs/openflight}"
OPENFLIGHT_DEPLOYMENT="${OPENFLIGHT_DEPLOYMENT:-${OPENFLIGHT_SOURCE_DIR}/OpenFlight/FlightComputer}"

if [[ ! -x "${OPENFLIGHT_ZEPHYR_EXECUTABLE}" ]]; then
    echo "Zephyr executable not found: ${OPENFLIGHT_ZEPHYR_EXECUTABLE}" >&2
    exit 1
fi
if [[ ! -x "${OPENFLIGHT_FPRIME_VENV}/bin/fprime-gds" ]]; then
    echo "F Prime GDS not found: ${OPENFLIGHT_FPRIME_VENV}/bin/fprime-gds" >&2
    exit 1
fi
if [[ ! -d "${OPENFLIGHT_DEPLOYMENT}" ]]; then
    echo "FlightComputer deployment not found: ${OPENFLIGHT_DEPLOYMENT}" >&2
    exit 1
fi

OPENFLIGHT_RUN_DIR="$(mktemp -d /tmp/openflight-launch.XXXXXX)"
OPENFLIGHT_ZEPHYR_LOG="${OPENFLIGHT_RUN_DIR}/zephyr.log"
OPENFLIGHT_ZEPHYR_PID=""
OPENFLIGHT_GDS_PID=""

cleanup() {
    trap - EXIT INT TERM
    if [[ -n "${OPENFLIGHT_GDS_PID}" ]] && kill -0 "${OPENFLIGHT_GDS_PID}" 2>/dev/null; then
        kill -- "-${OPENFLIGHT_GDS_PID}" 2>/dev/null || true
        wait "${OPENFLIGHT_GDS_PID}" 2>/dev/null || true
    fi
    if [[ -n "${OPENFLIGHT_ZEPHYR_PID}" ]] && kill -0 "${OPENFLIGHT_ZEPHYR_PID}" 2>/dev/null; then
        kill -- "-${OPENFLIGHT_ZEPHYR_PID}" 2>/dev/null || true
        wait "${OPENFLIGHT_ZEPHYR_PID}" 2>/dev/null || true
    fi
    rm -f "${OPENFLIGHT_ZEPHYR_LOG}"
    rmdir "${OPENFLIGHT_RUN_DIR}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "Starting the Zephyr sensor simulator..."
setsid "${OPENFLIGHT_ZEPHYR_EXECUTABLE}" --rt > >(tee "${OPENFLIGHT_ZEPHYR_LOG}") 2>&1 &
OPENFLIGHT_ZEPHYR_PID=$!

OPENFLIGHT_SENSOR_PTY=""
for _ in {1..100}; do
    if [[ -f "${OPENFLIGHT_ZEPHYR_LOG}" ]]; then
        OPENFLIGHT_SENSOR_PTY="$(sed -n 's/.*\(\/dev\/pts\/[0-9][0-9]*\).*/\1/p' "${OPENFLIGHT_ZEPHYR_LOG}" | head -n 1)"
    fi
    [[ -n "${OPENFLIGHT_SENSOR_PTY}" ]] && break
    kill -0 "${OPENFLIGHT_ZEPHYR_PID}" 2>/dev/null || break
    sleep 0.1
done

if [[ -z "${OPENFLIGHT_SENSOR_PTY}" ]]; then
    echo "Zephyr did not create a virtual serial port." >&2
    exit 1
fi

echo "Sensor link discovered at ${OPENFLIGHT_SENSOR_PTY}"
echo "Starting F Prime GDS..."
export OPENFLIGHT_SENSOR_PTY
source "${OPENFLIGHT_FPRIME_VENV}/bin/activate"
cd "${OPENFLIGHT_DEPLOYMENT}"
setsid fprime-gds -g html --skip-browser-open &
OPENFLIGHT_GDS_PID=$!

OPENFLIGHT_GDS_READY=0
for _ in {1..100}; do
    if (echo > /dev/tcp/127.0.0.1/5000) 2>/dev/null; then
        OPENFLIGHT_GDS_READY=1
        break
    fi
    kill -0 "${OPENFLIGHT_GDS_PID}" 2>/dev/null || break
    sleep 0.1
done

if [[ "${OPENFLIGHT_GDS_READY}" -ne 1 ]]; then
    echo "F Prime GDS did not open http://127.0.0.1:5000." >&2
    exit 1
fi

echo "OpenFlight is ready at http://127.0.0.1:5000"
echo "Press Ctrl+C here to stop F Prime and Zephyr."
if [[ "${OPENFLIGHT_SKIP_BROWSER:-0}" != "1" ]]; then
    powershell.exe -NoProfile -Command "Start-Process 'http://127.0.0.1:5000'" >/dev/null 2>&1 || true
fi
wait "${OPENFLIGHT_GDS_PID}"
