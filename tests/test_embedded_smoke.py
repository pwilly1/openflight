"""Run the Zephyr native simulator and check its live sample pipeline."""

import argparse
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
    health = re.findall(
        r"HEALTH produced=(\d+) reported=(\d+) dropped=(\d+) queue=(\d+)",
        output,
    )
    if len(samples) < 3 or samples != sorted(set(samples)):
        raise AssertionError(f"Expected increasing synthetic sample sequences: {output}")
    if len(health) < 2:
        raise AssertionError(f"Expected periodic health reports: {output}")
    if int(health[-1][0]) <= int(health[0][0]):
        raise AssertionError(f"Sensor production did not advance: {output}")
    if int(health[-1][2]) != 0:
        raise AssertionError(f"Samples dropped during nominal smoke test: {output}")
    print("Zephyr native_sim smoke test passed")


if __name__ == "__main__":
    main()
