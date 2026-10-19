#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Run protocol/PCM sanitizer tests and the exact DAI ABI regression, without audio capture."""
import os
import subprocess
import sys
from common import ROOT, verify_dependencies


def main():
    if os.geteuid() == 0:
        raise SystemExit("Run host tests as a regular user.")
    firmware, _ = verify_dependencies()
    out = ROOT / "build/tests"
    out.mkdir(parents=True, exist_ok=True)
    clang = subprocess.check_output(["/usr/bin/xcrun", "--find", "clang++"], text=True).strip()
    results = []
    for name in ("ProtocolTests", "AudioPCMTests"):
        executable = out / name
        subprocess.run([clang, "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(ROOT / "src"), str(ROOT / ("tests/" + name + ".cpp")), "-o", str(executable)], check=True)
        completed = subprocess.run([str(executable), *([str(firmware)] if name == "ProtocolTests" else [])], check=True, capture_output=True, text=True)
        print(completed.stdout, end="")
        results.append(completed.stdout)
    completed = subprocess.run([sys.executable, str(ROOT / "tests/test_dai.py")], check=True, capture_output=True, text=True)
    print(completed.stdout, end="")
    results.append(completed.stdout)
    (out / "results.txt").write_text("".join(results))
    print("All host tests passed. No driver load or microphone capture was performed.")


if __name__ == "__main__":
    main()
