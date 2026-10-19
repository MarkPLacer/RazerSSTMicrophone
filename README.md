# RazerSSTMicrophone

I built RazerSSTMicrophone to get the internal digital microphones working on my **Razer Blade 17 (2022), RZ09-0423**, using the Intel Alder Lake Smart Sound DSP and Sound Open Firmware IPC3.

**AI assistance:** This driver was largely written with the help of AI, specifically **GPT-6.1 Sol**. I developed and tested it on my own laptop with that assistance, building on the upstream projects credited below.

**Status: working on my laptop; experimental source preview.** I recorded and heard my own voice in Voice Memos, and repeated CoreAudio open/capture/close tests passed across several boots. I still need to resolve sleep/wake: one wake attempt left my display frozen, so I manually restarted the laptop. I have not established whether the microphone driver caused that failure.

## Supported hardware

| Item | Tested configuration |
| --- | --- |
| Laptop / BIOS | Blade 17 (2022), DI780 rev 4, BIOS 2.01 |
| Intel audio function | `8086:51c8`, subsystem `1a58:3005`, `00:1f.3` |
| DSP / firmware | Alder Lake cAVS 2.5 / Intel-signed SOF 2.2 IPC3 |
| Microphone path | Physical PDM1, microphones A+B, two channels |
| Capture | 48 kHz, 32-bit signed containers; CoreAudio float conversion |
| Tested system | macOS 26.7.1 (25G241), Intel x86_64 |

I have not identified the physical microphone capsule model. “Intel SST microphone” describes the audio path, not a verified microphone part number. Matching the Realtek ALC274 codec or laptop marketing name alone does not establish compatibility, so I have kept the driver's hardware checks narrow.

## How it works

The driver boots signed SOF firmware into volatile DSP memory on demand and creates a PDM1 capture pipeline. An IOAudioFamily input engine exposes **Razer Internal Microphone** to normal CoreAudio applications. Playback remains on the existing AppleHDA/AppleALC path.

It checks unused stream descriptors before using SD5/SD6/SD8, leaves the observed AppleHDA SD0/SD7 descriptors read-only, uses only DSP core mask 1, and does not reset the complete HDA controller. Capture stops and frees the PCM pipeline when the last audio client closes. Root publication is separate from starting capture.

## Build and test

Requires macOS, Xcode Command Line Tools and Python 3. The downloader fetches fixed, checksum-verified SOF and MacKernelSDK inputs into the ignored `third_party/` directory.

```sh
python3 tools/fetch-dependencies.py
./Test.command
./Build.command
```

These commands build locally. They do not install or load a kext, change EFI, request microphone access, capture audio, or restart the computer. Output is in `build/`. Read [build details](docs/BUILD.md) and the [integration requirements](docs/INSTALL.md).

## Evidence and limitations

I have documented what I tested and what remains open in the [validation record](docs/VALIDATION.md). The [aggregate results](evidence/runtime-summary.json) contain no recordings, serial numbers or full system logs.

I also reviewed the source contents and Git history before publication; see my [pre-publication check](docs/PUBLICATION-CHECK.md).

- I have verified ordinary CoreAudio stereo capture, repeated open/close, my voice in Voice Memos, simultaneous AppleHDA playback, return to idle, and automatic publication after reboot.
- I still need to test reliable full sleep/wake, long calls, multiple simultaneous recording apps, acoustic latency, battery impact, other OS versions and other machines.
- Software input gain defaults to +30 dB and can be reduced. Gain can clip loud signals. I have not implemented automatic gain control, beamforming, acoustic echo cancellation or noise suppression.
- The 240-frame latency declaration is provisional. I have not validated Apple Voice Isolation or Siri compatibility.
- `MicrophoneCaptureWorking` is a legacy registry field initialized false; it is not a live health result. Use actual CoreAudio frame statistics and `CaptureActive`/`Failure` instead.

Do not hot-unload or overwrite a running driver. On my laptop, I use a root-owned, delayed publisher and a staged auxiliary kernel collection. I have documented that setup in [INSTALL](docs/INSTALL.md); I have not yet validated a general-purpose automatic installer.

## Research and attribution

I researched [related hardware and drivers](docs/RESEARCH.md) and documented the [PDM1/source review](docs/DRIVER-REVIEW.md). This project adapts BSD-licensed work from **DexterSLamb/CmlSOFAudio**, **Shubham Banekar/LatSOFAudio**, Intel's Linux SOF driver and SOF IPC definitions. I am not claiming this is the first macOS digital microphone driver. Full attribution is in [credits and provenance](docs/PROVENANCE.md).

Project code is BSD-3-Clause; third-party terms are preserved in [licenses/](licenses/) and [NOTICE](NOTICE). SOF firmware and MacKernelSDK are fetched for a local build rather than committed. This is a community project, without vendor affiliation.
