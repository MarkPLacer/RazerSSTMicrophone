---
name: Hardware or capture report
about: Report a reproducible issue with minimal redacted diagnostics
title: ''
labels: ''
assignees: ''
---

**Hardware**
Laptop/board and BIOS version:
PCI vendor/device and subsystem IDs:

**Software**
macOS version/build:
Driver version and source commit:
Installation method and whether this is a freshly rebooted build:

**Failure**
Steps, expected result and actual result:
Normal microphone permission granted/denied:
Does ordinary CoreAudio capture start? Does existing speaker playback still work?

**Minimal status**
Relevant `Version`, `Phase`, `Failure`, `CaptureActive`, `CaptureSessions`,
`PMActualState`, `OwnedDMAsStopped`, `OwnedStreamsRestored`,
`DMAMemoryRetainedForSafety` and aggregate frame/invalid/clipping counts:

Do not attach recordings, full IORegistry dumps, serial numbers, boot UUIDs,
usernames, network identifiers, BIOS/Windows images or kernel collections.
Do not hot-unload the driver to investigate.
