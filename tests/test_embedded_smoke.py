"""Run the Zephyr native simulator and check its live sample pipeline."""

import argparse
import binascii
import re
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", help="Path to zephyr.exe from the native_sim build")
    args = parser.parse_args()

    result = subprocess.run(
        ["timeout", "4s", args.executable, "--rt"],
        capture_output=True,
        text=True,
        timeout=8,
        check=False,
    )
    output = result.stdout + result.stderr
    if result.returncode not in (0, 124):
        raise AssertionError(f"native_sim exited unexpectedly: {output}")

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
