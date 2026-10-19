#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Decode the produced wire packet independently against SOF v2.2's IPC3 ABI."""
import importlib.util
import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / "tools"))
spec = importlib.util.spec_from_file_location("dai_generator", root / "tools/generate-dai.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
packet = generator.generate()


def u32(offset):
    return struct.unpack_from("<I", packet, offset)[0]


assert len(packet) == 216 and u32(0) == len(packet)
assert u32(4) == 0x80010000 and u32(8) == 2
assert u32(12) == 0  # Logical DAI FIFO 0, distinct from physical PDM ID 1.
assert not any(packet[20:52])
assert u32(56) == 1
assert (u32(60), u32(64), u32(68)) == (2400000, 2400000, 48000)
assert struct.unpack_from("<4H", packet, 76) == (32, 32, 48, 52)
assert u32(84) == 1  # One physical controller with two enabled capsules.
assert (u32(88), u32(92), u32(96)) == (0, 0, 0)
assert not any(packet[100:120])
assert u32(120) == 0
assert struct.unpack_from("<7H", packet, 124) == (1, 1, 1, 0, 0, 0, 0)
assert not any(packet[138:])
print("PASS: IPC3 DAI ABI; logical FIFO0 / physical PDM1 A+B; 48kHz stereo; exact 2.4MHz; reserved fields clear; no extra PDM controllers")
