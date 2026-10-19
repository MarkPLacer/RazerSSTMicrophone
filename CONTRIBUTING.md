# Contributing

I welcome reproducible reports and carefully tested improvements. Start with `docs/DRIVER-REVIEW.md`, `docs/PROVENANCE.md` and the host tests. Please keep hardware additions separate from fixes for my tested target. A matching codec name or a silent input is insufficient evidence for broad PCI matching.

Changes to kernel behavior need their own version and hardware validation. Do not attach a “working” label to a build that only compiles. Include cold-boot publication, repeated ordinary CoreAudio capture/close, output coexistence, DMA/stream cleanup, idle PM state, and separate idle/active-capture sleep results where relevant. Preserve the previous recovery path. Never test by hot-unloading the running driver.

Keep upstream attribution and license headers. Explain which files or interfaces were adapted. Do not submit proprietary firmware, OEM BIOS tables, Windows drivers, recordings or full IORegistry/system dumps.

For an issue, report the OS/build, driver source version, PCI/subsystem IDs, board/BIOS version, exact step that fails, and a small redacted set of relevant status fields. Remove serial numbers, UUIDs, usernames and network identifiers. A normal permission denial should be reported separately from DSP or DMA failure.

I developed this driver with substantial AI assistance from GPT-6.1 Sol. Please describe AI assistance in contributions where relevant, keep upstream attribution, and respect each upstream project's contribution policy when submitting work there.
