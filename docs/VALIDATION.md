# Validation record

These are the results from **0.3.2** installed on my Blade 17. I carried out this development and validation with substantial help from **GPT-6.1 Sol**, using automated checks as well as my own Voice Memos test. I prepared the public package from that working source without installing a new kernel build. The package's build and host-test results are in `evidence/package-checks.json`.

## Hardware evidence already obtained

| Test | Result / boundary |
| --- | --- |
| DSP firmware ready | Exact Intel-signed ADL SOF 2.2 / IPC3 3.22.1 accepted; PDM1 pipeline configured |
| Initial low-level capture | Real DMA position advanced; both physical channels changed; no recorded DMA errors |
| Ordinary CoreAudio | Three approximately four-second open/capture/close cycles passed repeatedly across boots |
| My voice test | I recorded and heard my own voice in Voice Memos; I have kept the recording private |
| Acoustic correlation | A quiet 1200 Hz test tone produced approximately 4.26x / 3.27x band-response increases over adjacent baselines; no clipping in that acoustic run |
| Output coexistence | Recording tested during existing AppleHDA playback; observed SD0/SD7 descriptors unchanged |
| Close cleanup | STOP/FREE IPC replies successful; owned DMAs stopped, streams restored, core 0 powered down; idle PM state 0 |
| Permanent startup | Automatic delayed publication and fresh recording tests passed after reboot |
| Later update regression check | Three fresh capture cycles passed after I updated my laptop to NVIDIA driver 2.3.7 |

My latest three test cycles contain **201,728 / 192,512 / 195,072 frames**, changing on both channels, with status 0, zero invalid samples and zero clipped samples. These are short-run statistics. I did see transient clipping in an earlier test, so +30 dB gain should not be treated as clipping-proof.

The original installed executable SHA-256 is `e012eb76c5650daa371d139855a38e4083ce49351f6c9923c81990e04d920a93`. Its loaded Mach-O UUID was `CE39436B-8CDE-3A2B-A14A-5A5605800AD0`. These identify the tested original executable, not a promise that a different compiler/build produces identical bytes.

## Host tests

- Protocol: actual pinned firmware identity/window parsing; all truncated FW_READY/manifest lengths; malformed ABI/source/bounds/overlap rejection; allowed stream indices; mapped BDL coverage and overflow rejection; IPC reply shape.
- PCM: DC removal, software gain, mute, clipping bounds, stereo handling, ring boundaries, duplicate conversions, and timestamp wrap cases.
- Public packet regression: logical FIFO 0 distinct from physical PDM1, A+B enablement, sample rate/clock/duty fields, cleared reserved fields and exact hardware-tested message checksum.
- Sanitizers: AddressSanitizer and UndefinedBehaviorSanitizer for the C++ host tests. They do not exercise kernel hardware access.

The static analyzer reported a possible-uninitialized-value warning through a PCI-read/virtual-call path in cleanup. I reviewed it with AI assistance against the initialized stop messages. This was not an independent audit, and I am not claiming a warning-free result.

## Not established

Reliable full sleep/wake, active-call wake recovery, long-duration clock drift and CPU cost, multiple simultaneous recording applications, fault-injected teardown, measured acoustic latency, capsule channel geometry/polarity, battery improvement, other machines, other OS versions, Siri/Dictation/FaceTime/browser-specific behavior and Apple microphone processing modes.

After a wake attempt froze my display, I manually restarted the laptop. I have not established that the microphone driver caused it, and I have no evidence of an automatic reboot or a new kernel panic. I am keeping sleep/wake support unverified until I can test it successfully.
