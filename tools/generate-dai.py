#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Generate the exact tested IPC3 computed-DMIC message, without accessing hardware."""
import hashlib
import json
import struct
import sys
from pathlib import Path
from common import ROOT, LOCK


def generate():
    c = json.loads((ROOT / "hardware/razer-blade17-2022.json").read_text())["capture"]
    if (c["physical_pdm_controller"], c["microphones"], c["channels"], c["sample_rate_hz"]) != (1, ["A", "B"], 2, 48000):
        raise ValueError("This generator only supports the tested PDM1 stereo configuration")
    clock = c["io_clock_hz"] // c["clock_divisor"]
    if clock != c["pdm_clock_hz"] or clock != 2400000:
        raise ValueError("Unexpected microphone clock")
    packet = bytearray(216)
    struct.pack_into("<IIIIHBB", packet, 0, len(packet), 0x80010000, 2, 0, 6, 0, 0)
    # SOF v2.2 dai.h: the union starts at 52. dai_index=0 is the logical
    # FIFO; the physical PDM controller ID is a separate field at byte 124.
    struct.pack_into("<IIIIIIHHHHIIII", packet, 52,
                     0, 1, clock, clock, c["sample_rate_hz"], 0,
                     32, 32, c["duty_min_percent"], c["duty_max_percent"], 1, 0, 0, 0)
    struct.pack_into("<I10H", packet, 120, 0, 1, 1, 1, 0, 0,
                     c["clock_edge"], c["clock_skew"], 0, 0, 0)
    if hashlib.sha256(packet).hexdigest() != LOCK["dai_config_sha256"]:
        raise ValueError("Generated DAI message differs from the hardware-tested bytes")
    return bytes(packet)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python3 tools/generate-dai.py OUTPUT.bin")
    Path(sys.argv[1]).write_bytes(generate())
    print("Generated exact tested 216-byte PDM1 A+B IPC3 message.")
