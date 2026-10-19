# SPDX-License-Identifier: BSD-3-Clause
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOCK = json.loads((ROOT / "dependencies.lock.json").read_text())


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_dependencies():
    firmware = ROOT / "third_party/firmware/sof-adl.ri"
    sdk = ROOT / "third_party/MacKernelSDK"
    if not firmware.is_file() or not (sdk / ".verified-files.json").is_file():
        raise RuntimeError("Run python3 tools/fetch-dependencies.py first; see docs/BUILD.md")
    if firmware.stat().st_size != LOCK["firmware"]["bytes"] or digest(firmware) != LOCK["firmware"]["sha256"]:
        raise RuntimeError("Pinned ADL IPC3 firmware checksum mismatch")
    if digest(ROOT / "licenses/SOF-LICENCE.Intel") != LOCK["firmware"]["license_sha256"]:
        raise RuntimeError("Firmware license checksum mismatch")
    metadata = json.loads((sdk / ".verified-files.json").read_text())
    if metadata["archive_sha256"] != LOCK["sdk"]["sha256"]:
        raise RuntimeError("Wrong MacKernelSDK archive")
    for relative, expected in metadata["files"].items():
        path = sdk / relative
        if path.is_symlink() or not path.is_file() or digest(path) != expected:
            raise RuntimeError("SDK changed: " + relative)
    return firmware, sdk
