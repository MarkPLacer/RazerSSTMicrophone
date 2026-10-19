# Building the source preview

Use an unprivileged account on macOS with Xcode Command Line Tools and Python 3. I verified the build with Apple clang 21 from CLT 26.6. I have not hardware-tested builds from other toolchains. The `macos10.9` compiler target preserves the flags from my working build; I am not claiming macOS 10.9 support.

The host tests and user-space helpers explicitly select the macOS SDK reported by `xcrun`. This also supports full Xcode installations where calling the compiler directly does not supply the SDK's standard C++ and framework headers. The kernel build continues to use the pinned MacKernelSDK.

```sh
python3 tools/fetch-dependencies.py
./Test.command
./Build.command
```

The dependency lock pins:

| Input | Commit | Integrity |
| --- | --- | --- |
| SOF ADL firmware | `fcd5ae85ead7ecd67ff199335b61fac825843ca0` | SHA-256 `294e5a51b1d3fc4ceda57780927c2a2ec95bb5d23477f879a9fb648c6c01cb30` |
| MacKernelSDK | `3f750085caa17ec3a7880f11c11bf4f48cd6a164` | Archive SHA-256 `8931e622b6d16a7052cb2c2dabe16be06963a34b747f19e6cda2bbe8bc7e2557` |

Downloads are checked before extraction. The SDK archive extractor rejects path traversal and links. Subsequent builds verify the extracted files against the download manifest. To refetch a damaged input, move aside the local ignored `third_party/` directory and run the downloader again. Do not substitute an IPC4 or Comet Lake firmware file.

The DAI generator uses public scalar hardware parameters in `hardware/razer-blade17-2022.json`. It produces exactly the tested 216-byte IPC3 message, SHA-256 `f0d487e93ab56e79ee52f89b353330b8978e633ef143f3a1c3640d643bc9593f`. No raw NHLT/BIOS blob is needed.

`Test.command` runs AddressSanitizer and UndefinedBehaviorSanitizer on the protocol and PCM tests, then independently decodes the generated DAI packet against the IPC3 field offsets. These are host tests; they cannot establish live DMA, sleep/wake or hardware compatibility.

`Build.command` produces the x86_64 kext, an ad-hoc signature, a build manifest, and two command-line helpers. Ad-hoc signing does not confer Apple approval or notarization. It also does not grant microphone permission. Build output is ignored by Git.

I kept the working 0.3.2 kernel source unchanged; its hashes are in `evidence/working-source.json`. I updated the packaging, license resources and build locations. With my toolchain, the rebuilt executable was **byte-identical to my installed original after removing signatures from temporary copies**; see `evidence/rebuild-comparison.json`. The signed files differ because the public bundle includes its distribution notices. I have not installed the public-package rebuild.

I included a GitHub workflow for the source audit, host tests and build on a macOS runner. It does not install the driver, capture audio or publish a binary release. The repository's Actions tab shows remote run status; `evidence/package-checks.json` records my local validation before publication.
