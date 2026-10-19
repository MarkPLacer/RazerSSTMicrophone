#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Compile and ad-hoc sign locally. Never install, load, publish or reboot."""
import json
import os
import shutil
import subprocess
import sys
from common import ROOT, LOCK, digest, verify_dependencies


def run(args):
    subprocess.run([str(x) for x in args], check=True, cwd=ROOT)


def main():
    if len(sys.argv) != 1:
        raise SystemExit("Usage: ./Build.command (no installation options)")
    if os.geteuid() == 0:
        raise SystemExit("Build as a regular user.")
    firmware, sdk = verify_dependencies()
    out = ROOT / "build"
    bundle = out / "RazerSSTMicrophone.kext"
    binary = bundle / "Contents/MacOS/RazerSSTMicrophone"
    binary.parent.mkdir(parents=True, exist_ok=True)
    clang = subprocess.check_output(["/usr/bin/xcrun", "--find", "clang++"], text=True).strip()
    run([clang, "--version"])
    run([sys.executable, ROOT / "tools/generate-dai.py", out / "DAIConfig.bin"])
    # Keep the original symbols/layout, without embedding developer paths.
    shutil.copyfile(firmware, out / "sof-adl.ri")
    assembly = ".section __TEXT,__const\n.p2align 4\n.globl _razer_sof_fw\n_razer_sof_fw:\n.incbin \"sof-adl.ri\"\n.globl _razer_sof_fw_end\n_razer_sof_fw_end:\n.p2align 4\n.globl _razer_dmic_dai\n_razer_dmic_dai:\n.incbin \"DAIConfig.bin\"\n"
    (out / "Firmware.s").write_text(assembly)
    target = ["-target", "x86_64-apple-macos10.9"]
    kernel = ["-O2", "-mkernel", "-DKERNEL", "-DKERNEL_PRIVATE", "-fno-exceptions", "-fno-rtti", "-fno-stack-protector", "-nostdinc", "-isystem", sdk / "Headers"]
    run([clang, *target, "-std=c++14", *kernel, "-Wall", "-Wextra", "-DDRIVER_PRIVATE", "-DAPPLE_KEXT_VTABLE_PADDING", "-fno-common", "-fno-builtin", "-isystem", sdk / "Headers/libkern", "-c", ROOT / "src/RazerSSTMicrophone.cpp", "-o", out / "RazerSSTMicrophone.o"])
    run([clang, *target, "-std=c++14", *kernel, "-c", ROOT / "src/ModuleInfo.cpp", "-o", out / "ModuleInfo.o"])
    subprocess.run([clang, *target, "-c", "Firmware.s", "-o", "Firmware.o"], cwd=out, check=True)
    run([clang, *target, "-nostdlib", "-Wl,-kext", "-Wl,-undefined,dynamic_lookup", out / "RazerSSTMicrophone.o", out / "ModuleInfo.o", out / "Firmware.o", sdk / "Library/x86_64/libkmod.a", "-o", binary])
    shutil.copyfile(ROOT / "src/Info.plist", bundle / "Contents/Info.plist")
    resources = bundle / "Contents/Resources/Licenses"
    resources.mkdir(parents=True, exist_ok=True)
    for path in [ROOT / "LICENSE", ROOT / "NOTICE", *(ROOT / "licenses").iterdir()]:
        if path.is_file():
            shutil.copyfile(path, resources / path.name)
    run(["/usr/bin/plutil", "-lint", bundle / "Contents/Info.plist"])
    run(["/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", bundle])
    run(["/usr/bin/codesign", "--verify", "--strict", "--verbose=2", bundle])
    cc = subprocess.check_output(["/usr/bin/xcrun", "--find", "clang"], text=True).strip()
    for name, frameworks in [("publish", ["IOKit", "CoreFoundation"]), ("mic-input-check", ["CoreAudio", "CoreFoundation"])]:
        flags = [flag for framework in frameworks for flag in ("-framework", framework)]
        run([cc, "-arch", "x86_64", "-O2", "-Wall", "-Wextra", ROOT / ("tools/" + name + ".c"), *flags, "-o", out / name])
    manifest = {"driver_version": "0.3.2", "architecture": "x86_64", "tested_os_only": "26.7.1 (25G241)", "compiler": subprocess.check_output([clang, "--version"], text=True), "dependencies": LOCK, "files": {p.relative_to(out).as_posix(): digest(p) for p in bundle.rglob("*") if p.is_file()}, "installed_or_loaded": False}
    (out / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Built and verified build/RazerSSTMicrophone.kext; no system changes. Read docs/INSTALL.md before any integration.")


if __name__ == "__main__":
    main()
