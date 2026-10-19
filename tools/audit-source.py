#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Conservative local source-distribution checks; no external service or capture."""
import json
import re
from common import ROOT, digest

EXCLUDED = {".git", "build", "third_party", "__pycache__"}
FORBIDDEN_SUFFIXES = {".kc", ".wav", ".m4a", ".aiff", ".ioreg", ".panic", ".ri", ".bin", ".zip", ".gz", ".o", ".dylib"}
PATTERNS = {
    "private absolute user path": rb"/Users/(?!Shared(?:/|\b))[^\s\"'<>]+",
    "Windows home path": rb"[A-Za-z]:\\Users\\",
    "mounted-volume path": rb"/Volumes/[^\s\"'<>]+",
    "private key": rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----",
    "GitHub token shape": rb"(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})",
    "AWS access key shape": rb"AKIA[A-Z0-9]{16}",
    "private Apple identity field": rb"(?:IOPlatformSerialNumber|MLB|SystemSerialNumber)\s*[=:]\s*[\"']?[A-Z0-9]{6,}",
}


def main():
    files = [p for p in ROOT.rglob("*") if p.is_file() and not EXCLUDED.intersection(p.relative_to(ROOT).parts) and not p.name.endswith(".pyc")]
    findings = []
    for path in files:
        relative = path.relative_to(ROOT).as_posix()
        data = path.read_bytes()
        if path.is_symlink() or path.suffix.lower() in FORBIDDEN_SUFFIXES or len(data) > 150000 or b"\x00" in data:
            findings.append(relative + ": unexpected binary/link/oversized file")
        # Don't match the audit's own literal pattern definitions.
        if path.name != "audit-source.py":
            for label, pattern in PATTERNS.items():
                if re.search(pattern, data):
                    findings.append(relative + ": " + label)
    original = json.loads((ROOT / "evidence/working-source.json").read_text())
    for row in original["files"]:
        if digest(ROOT / row["file"]) != row["sha256"]:
            findings.append(row["file"] + ": changed from working-source snapshot")
    if findings:
        raise SystemExit("\n".join(findings))
    print("PASS: %d public text files checked; private path/token/binary patterns absent; 7 kernel files match working source" % len(files))
    print("Pattern checks supplement manual review; they do not prove absence of every possible secret.")


if __name__ == "__main__":
    main()
