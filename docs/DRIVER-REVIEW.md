# PDM1 and driver source review

On 2026-10-19, I reviewed the seven kernel source files recorded in `evidence/working-source.json`, the generated DAI packet, pinned firmware, all 36 comments of SOF issue 5989, its two topology commits and the hardware-test results. I did this with substantial assistance from **GPT-6.1 Sol**. This is my development review and local validation, not an independent security audit.

## What the SOF report establishes

[Issue 5989](https://github.com/thesofproject/sof/issues/5989) identifies two microphones on PDM1. Four-channel capture was diagnostic; the working solution selected PDM1 in a two-channel topology. [Another Blade 17 user confirmed the fix](https://github.com/thesofproject/sof/issues/5989#issuecomment-1846917264). Later speaker channel inversion was discussed separately. I use this report as evidence for the routing choice, not as validation of macOS power management or my complete driver.

The actual [PDM selection commit](https://github.com/thesofproject/sof/commit/8d36e77cd780e533dbd75977e2d8204db2bd586a) replaces the default stereo PDM0 selection with a PDM1-selectable macro. The [topology-generation commit](https://github.com/thesofproject/sof/commit/a1f1b2ec5db94f48e6e829e48bdfe6ca985ee331) adds separate PDM1 topology variants. Neither requires a four-channel device as the final user-facing format. The macros configure 48 kHz, 32-bit words, a 2.4–4.8 MHz allowed PDM clock range and 40–60% duty limits. I use a narrower 2.4 MHz / 48–52% configuration based on my board's decoded hardware parameters; it has already produced real audio.

## Mapping into this implementation

| Check | Code/evidence | Assessment |
| --- | --- | --- |
| Physical route | `tools/generate-dai.py`, decoded packet byte 124 = PDM ID 1, A/B enabled | Matches the intended route; logical DAI index 0 is not physical PDM0 |
| Channel count | `capture()` sets 2 channels and channel map 0/1; IOAudio format is stereo | No silent PDM0 pair exposed ahead of the real pair |
| Clock | 38.4 MHz source / 16 = 2.4 MHz; 48 kHz output | Consistent with decoded board settings and successful captures |
| DAI ABI | SOF v2.2 `dai.h` / `dai-intel.h`: union at 52, DMIC header 68, PDM entry 24 bytes | Exact packet tested; reserved fields cleared |
| Firmware | Build hash/size checks, runtime 2.2/IPC3 ABI/source identity checks | Does not silently accept a different generation |
| Mailboxes | Runtime XMan/FW_READY window validation, no Comet Lake address fallback | Bounds and truncated records covered by host tests |
| Output coexistence | SD0/SD7 read-only; SD5/6/8 unused checks before claims; no complete HDA reset or codec writes | Capture with AppleHDA playback passed on my laptop; not a universal ownership guarantee |
| Core ownership | Host core mask 1, not donor mask 0xf | Matches ADL-specific source adaptation |
| DMA | Prepared mapped buffers, full BDL coverage, exact one-way synchronization | Tests cover coverage/overflow; real DMA progression separately measured |
| Close/failure | STOP/FREE IPC, host DMA stopped before restoration, claimed mappings retained if stop cannot be proven | Successful cleanup measured; fault injection remains open |
| Privilege | Root-only, one-property publication; no raw MMIO user client | Narrow user-space interface |
| Idle power | Shared PCI power requested through IOKit; software audio device does not pin parent power | Idle state 0 measured; battery saving not quantified |

Protocol references: [SOF v2.2 DAI header](https://github.com/thesofproject/sof/blob/57864296b07c5ffd673214dd5b1e3d07ee274a22/src/include/ipc/dai.h), [Intel DMIC ABI](https://github.com/thesofproject/sof/blob/57864296b07c5ffd673214dd5b1e3d07ee274a22/src/include/ipc/dai-intel.h), [DMIC implementation](https://github.com/thesofproject/sof/blob/57864296b07c5ffd673214dd5b1e3d07ee274a22/src/drivers/intel/dmic/dmic.c).

## Findings and remaining work

**I have not verified full sleep/wake.** My display froze after a wake attempt with the microphone installed, and I manually restarted the laptop. I have not established the cause. The driver pauses capture on a sleep notification and attempts bounded resume, but that alone does not establish successful S3 recovery. I still need to test idle and active-capture sleep separately, including desktop/GPU survival and recording after wake, before calling this stable.

**I have only validated stream coexistence on my setup.** The driver checks stream ownership rather than reserving descriptors through AppleHDA. Two unused snapshots and a final recheck reduce conflicts on my laptop, but they do not coordinate allocation atomically with a different AppleHDA implementation. I will need more evidence before extending PCI matching.

**I still need longer acoustic and timing tests.** The declared 240-frame input latency is provisional. The fixed DC filter, gain, cached conversion and clock-wrap logic pass host tests. I have not yet validated multi-client overlaps, long sessions, clipping under speech at different distances, absolute polarity or measured acoustic delay. I also have not identified the microphone capsule part number or datasheet, so successful routing does not establish every acoustic parameter.

**I found a stale diagnostic field: `MicrophoneCaptureWorking`.** It is initialized false and never updated, so it is not a live failure or success indicator. I have documented it here while preserving the tested kernel bytes. I plan to replace it with more precise status in a future version. For actual capture evidence, I use the CoreAudio checker.

**I corrected the public packaging:** my original assembler used private absolute paths, the old NOTICE still described a firmware probe, and the build relied on sibling directories. The public build now generates relative assembly and a DAI packet, verifies pinned inputs and preserves license notices. I have excluded my machine-specific installer and raw diagnostics, and kept the seven kernel source files unchanged.

I found no support for the missing-manual-power-ACK theory: XNU's `pmTellClientWithResponse` accepts a successful kernel callback with zero requested delay as an implicit response. I have not applied a speculative ACK or routing patch to my installed driver. An unhandled `SystemWillNotSleep` message alone also does not explain the failure; the root-domain code describes different cancellation stages. I need event evidence before choosing a fix.

## Verdict

My PDM1 implementation matches the relevant upstream fix and produces actual stereo capture on my laptop. I found no routing correction required by issue 5989. I am sharing this as an experimental source preview for this specific hardware, with the limitations above. I am not claiming universal compatibility or reliable sleep/wake yet.
