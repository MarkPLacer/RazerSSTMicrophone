# Related hardware and drivers

I researched these related projects and hardware reports on 2026-10-19 with help from **GPT-6.1 Sol**. My sources include project code and documentation, maintainer discussions, vendor documentation and firsthand user reports. I treat working Linux audio as a useful lead, not proof of macOS compatibility, and I am not claiming exhaustive discovery or “first ever” support.

## Same and nearby hardware

| Source | Relationship | What can be carried over |
| --- | --- | --- |
| [Razer RZ09-0423 support page](https://mysupport.razer.com/app/answers/detail/a_id/5896) | Exact laptop family; vendor identifies an array microphone | Product identity, not capsule part number or macOS support |
| [SOF issue 5989](https://github.com/thesofproject/sof/issues/5989), including [Blade 17 confirmation](https://github.com/thesofproject/sof/issues/5989#issuecomment-1846917264) | Same-generation Blade 15 and a Blade 17 user | PDM1 routing; detailed comparison in [DRIVER-REVIEW](DRIVER-REVIEW.md) |
| [Blade 17 Linux Mint firsthand report](https://www.reddit.com/r/linuxmint/comments/1j248sr/) | RZ09-0423 / DI780 rev 4, i7-12800H, `8086:51c8`; BIOS 1.09 | Closely matching controller and silent input symptom; subsystem identity and a successful fix are not established by that report |
| [Voxtype/Omarchy author's guide](https://github.com/hjbarraza/voxtype-omarchy-guide) | Author lists Blade 17 (2022) as tested and describes Linux PDM1 topology selection | Additional route evidence; its “empty NHLT” explanation differs from what I found in my BIOS |
| [SOF issue 2460](https://github.com/thesofproject/linux/issues/2460) | Lenovo ThinkBook 13S, Comet Lake | Earlier example of the same PDM1-vs-PDM0 wiring distinction; not the same chipset |
| [SOF issue 6116](https://github.com/thesofproject/sof/issues/6116) | Razer Blade 14 (2022), AMD | Same brand and symptom do not imply an Intel SST solution |

With AI assistance, I recovered a nonempty 7,578-byte NHLT table from my BIOS 2.01 and decoded PDM1 A+B. That differs from the Omarchy guide's reported empty-table situation. I would not assume every firmware revision exposes identical runtime ACPI data, or repeat the guide's broad claim that the generic two-channel topology has no DMIC path: upstream's selection commits explicitly distinguish two-channel PDM0 and PDM1 variants.

I have not identified the physical MEMS capsule manufacturer, model, array spacing or datasheet limits. What I have confirmed is the controller, board/subsystem and electrical route. This distinction matters when considering ports and acoustic tuning.

## Comparable macOS drivers

| Project | Design / target | Relevance to this project |
| --- | --- | --- |
| [LatSOFAudio](https://github.com/shubhambanekar/LatSOFAudio) | Dell Latitude 3410, Comet Lake `8086:02c8`, IPC3, IOAudioFamily input; playback coexists with AppleHDA | Closest direct donor; BSD-3-Clause. Reference for publication, conditioning, timing and DSP pipeline. Its Comet Lake firmware, stream borrowing and reported app/sleep compatibility cannot be adopted as ADL validation |
| [CmlSOFAudio](https://github.com/DexterSLamb/CmlSOFAudio) | HP Chromebook C1030/Jinlon, Comet Lake, DMIC plus I2S codecs, kext and user-space HAL component | Ancestor of LatSOFAudio. Different codec, four-channel array and output ownership. Its full audio stack is not installed here |
| [AMDMicrophone](https://github.com/qhuyduong/AMDMicrophone) | AMD Renoir Audio Co-Processor microphone driver | Useful example of a separate digital-microphone driver coexisting with codec playback. Different registers/DMA/hardware; not an Intel replacement |
| [AppleALC](https://github.com/acidanthera/AppleALC) | AppleHDA codec/layout patching; [changelog](https://github.com/acidanthera/AppleALC/blob/master/Changelog.md) includes `51c8` controller support | Working codec output does not show that the SOF DSP was booted or a PDM1 capture pipeline was created. Keep the working output configuration |
| [VoodooHDA](https://github.com/CloverHackyColor/VoodooHDA) | HDA codec driver | HDA codec support alone is not evidence of this board's separate DSP/PDM capture support. No direct replacement was established in the reviewed material |

I use the pinned LatSOFAudio source at `41fda77bd5321464fef5ba71a44437f993215d55`. Its reported sleep recovery, Siri and app support belong to its tested machine; I have not assumed those results apply to mine. I have not included the retired donor HAL plugin or its Apple sample-code implementation.

## Protocol and implementation references

- [Linux Intel SOF, pinned source](https://github.com/torvalds/linux/tree/58161ead29e50d70a317ea26b472e0c7508e69da/sound/soc/sof/intel): ADL/TGL PCI selection, host-managed core mask, ROM loader, stream and IPC sequencing. Adapted files identify a BSD alternative; attribution is preserved. The GPL-only NHLT parsing header is not bundled or incorporated as a parser.
- [SOF v2.2 firmware source](https://github.com/thesofproject/sof/tree/57864296b07c5ffd673214dd5b1e3d07ee274a22): the relevant IPC3 DAI/stream structures and computed-DMIC implementation. Matching version matters more than copying current IPC4 examples.
- [Pinned SOF ADL firmware](https://github.com/thesofproject/sof-bin/blob/fcd5ae85ead7ecd67ff199335b61fac825843ca0/v2.2.x/sof-v2.2/intel-signed/sof-adl.ri) and its [complete license](https://github.com/thesofproject/sof-bin/blob/fcd5ae85ead7ecd67ff199335b61fac825843ca0/LICENCE.Intel): exact build input and redistribution notices. I verified that my local `LICENSE.sof` was byte-identical to this upstream file.
- [SOF DMIC tuning guide](https://thesofproject.github.io/latest/developer_guides/tuning/dmic_tuning.html): clocking, channel edge/polarity, geometry and acoustic tuning concepts. Current IPC4 configuration examples are not drop-in messages for the pinned IPC3 firmware.
- [SOF IPC infrastructure](https://thesofproject.github.io/latest/developer_guides/firmware/ipc_infrastructure.html): IPC generations have different control interfaces. A newer firmware number is not an interchangeable update.
- [Apple IOAudioFamily](https://github.com/apple-oss-distributions/IOAudioFamily/tree/6fdf514055a9868fe20e4dc3845b78dfa5e27110): device/engine, input conversion and power-management behavior. This project does not vendor that system extension.
- [Apple XNU power management](https://github.com/apple-oss-distributions/xnu/tree/main/iokit/Kernel): I reviewed `IOServicePM.cpp` and `IOPMrootDomain.cpp` with AI assistance to distinguish callback acknowledgments and sleep phases. I have kept the downloaded reference privately and have not treated a notification name alone as evidence for a sleep fix.
- [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK/tree/3f750085caa17ec3a7880f11c11bf4f48cd6a164): pinned kernel build input, with its own license terms.

## Publication decisions from the research

I am sharing a narrowly matched, experimental input driver with reproducible inputs and the results from my laptop. I have credited the donor projects, pinned the firmware and protocol, and generated the DAI message from scalar settings. I have excluded Windows drivers, BIOS images, recordings, whole IORegistry dumps, private collections and my machine-specific privileged installer. I am preserving the working kernel source until a separately versioned and tested change is needed.

Before I call this stable or extend support, I need to resolve full sleep/wake, validate stream ownership on each added target, measure acoustic latency, run longer calls and multi-client tests, and establish microphone-specific electrical/acoustic limits where possible. I treat a matching symptom in a forum as a lead for testing, not a compatibility guarantee.
