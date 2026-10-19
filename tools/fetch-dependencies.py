#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Fetch checksum-pinned build inputs into this checkout. Never install or load."""
import io
import json
import os
import shutil
import tarfile
import tempfile
import urllib.request
from pathlib import Path, PurePosixPath
from common import ROOT, LOCK, digest, verify_dependencies


def fetch(item):
    request = urllib.request.Request(item["url"], headers={"User-Agent": "RazerSSTMicrophone-source-build"})
    with urllib.request.urlopen(request, timeout=45) as response:
        data = response.read(item["bytes"] + 1)
    import hashlib
    if len(data) != item["bytes"] or hashlib.sha256(data).hexdigest() != item["sha256"]:
        raise RuntimeError("Download size/checksum mismatch: " + item["url"])
    return data


def main():
    if os.geteuid() == 0:
        raise SystemExit("Run this build-input downloader as a regular user.")
    target = ROOT / "third_party"
    if target.exists():
        verify_dependencies()
        print("Existing pinned inputs verified.")
        return
    # Extract manually; reject links, traversal, device files and oversized data.
    # A failed download/extraction never replaces the completed input directory.
    with tempfile.TemporaryDirectory(prefix=".dependencies-", dir=ROOT) as temporary:
        stage = Path(temporary)
        (stage / "firmware").mkdir()
        (stage / "firmware/sof-adl.ri").write_bytes(fetch(LOCK["firmware"]))
        sdk = stage / "MacKernelSDK"
        sdk.mkdir()
        hashes = {}
        total = 0
        expected_root = "MacKernelSDK-" + LOCK["sdk"]["commit"]
        with tarfile.open(fileobj=io.BytesIO(fetch(LOCK["sdk"])), mode="r:gz") as archive:
            for member in archive:
                parts = PurePosixPath(member.name).parts
                if not parts or parts[0] != expected_root or ".." in parts or member.name.startswith("/"):
                    raise RuntimeError("Unsafe SDK archive member")
                if len(parts) == 1:
                    if not member.isdir():
                        raise RuntimeError("SDK archive root is not a directory")
                    continue
                destination = sdk.joinpath(*parts[1:])
                if member.isdir():
                    destination.mkdir(parents=True, exist_ok=True)
                elif member.isfile() and member.size <= 8 * 1024 * 1024:
                    total += member.size
                    if total > 64 * 1024 * 1024:
                        raise RuntimeError("Expanded SDK exceeds limit")
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    with archive.extractfile(member) as source, destination.open("xb") as output:
                        shutil.copyfileobj(source, output)
                    hashes[destination.relative_to(sdk).as_posix()] = digest(destination)
                else:
                    raise RuntimeError("Unsupported SDK archive member: " + member.name)
        (sdk / ".verified-files.json").write_text(json.dumps({"archive_sha256": LOCK["sdk"]["sha256"], "files": hashes}, indent=2) + "\n")
        os.rename(stage, target)
        # TemporaryDirectory tolerates its directory having been renamed.
    verify_dependencies()
    print("Pinned SOF ADL firmware and MacKernelSDK downloaded and verified. No system changes.")


if __name__ == "__main__":
    main()
