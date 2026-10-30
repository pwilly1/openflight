"""Run the Zephyr native simulator and check its live sample pipeline."""

import argparse
import binascii
import os
import re
import signal
import subprocess
import time


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", help="Path to zephyr.exe from the native_sim build")
    args = parser.parse_args()

    process = subprocess.Popen(
        [args.executable, "--rt"], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, start_new_session=True,
    )
    assert process.stdout is not None
    first_line = process.stdout.readline()
    match = re.search(r"(/dev/pts/\d+)", first_line)
    if not match:
        raise AssertionError(f"No Zephyr UART: {first_line}")
    frame = bytes.fromhex("a55a020001000101")
    command = frame + binascii.crc_hqx(frame[2:], 0xFFFF).to_bytes(2, "big")
    with open(match.group(1), "wb", buffering=0) as serial:
        serial.write(command)
    time.sleep(4)
    os.killpg(process.pid, signal.SIGTERM)
    output = first_line + process.communicate(timeout=5)[0]
    if process.returncode not in (0, -signal.SIGTERM):
        raise AssertionError(f"native_sim exited unexpectedly: {output}")
    if "COMMAND code=1 accepted=1" not in output:
        raise AssertionError(f"Zephyr did not accept START_COLLECTION: {output}")

    samples = [int(value) for value in re.findall(r"SAMPLE seq=(\d+)", output)]
    frame_hex = re.findall(r"^FRAME ([0-9a-f]+)$", output, re.MULTILINE)
    health = re.findall(
        r"HEALTH produced=(\d+) reported=(\d+) dropped=(\d+) queue=(\d+)",
        output,
    )
    if len(samples) < 3 or samples != sorted(set(samples)):
        raise AssertionError(f"Expected increasing synthetic sample sequences: {output}")
    if len(frame_hex) != len(samples):
        raise AssertionError(f"Expected one packet per reported sample: {output}")
    for sequence, encoded in zip(samples, frame_hex):
        frame = bytes.fromhex(encoded)
        if len(frame) != 19 or frame[:3] != bytes.fromhex("a55a01"):
            raise AssertionError(f"Invalid sensor packet header: {encoded}")
        if int.from_bytes(frame[3:5], "big") != sequence % 65536:
            raise AssertionError(f"Packet sequence differs from sample: {encoded}")
        if int.from_bytes(frame[5:7], "big") != 10:
            raise AssertionError(f"Invalid sensor payload length: {encoded}")
        if binascii.crc_hqx(frame[2:-2], 0xFFFF) != int.from_bytes(frame[-2:], "big"):
            raise AssertionError(f"Invalid sensor packet CRC: {encoded}")
    if len(health) < 2:
        raise AssertionError(f"Expected periodic health reports: {output}")
    if int(health[-1][0]) <= int(health[0][0]):
        raise AssertionError(f"Sensor production did not advance: {output}")
    if int(health[-1][2]) != 0:
        raise AssertionError(f"Samples dropped during nominal smoke test: {output}")
    print("Zephyr native_sim smoke test passed")


if __name__ == "__main__":
    main()
