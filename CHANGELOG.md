# Changelog

## 0.3.2 source preview — 2026-10-19

I kept the working kernel source from my laptop: ADL SOF IPC3 PDM1 stereo capture through IOAudioFamily, mapped DMA, deferred root publication, on-demand PCI power and bounded capture cleanup. The driver was largely written with help from **GPT-6.1 Sol**.

I added relative build paths, pinned dependency downloads, generated DAI configuration, protocol/PCM/packet tests, complete notices, aggregate evidence and research for the public package. I have not installed the repackaged build. Sleep/wake remains unresolved, and I have only validated the driver on my own hardware.

With AI assistance, I corrected the 0.3.1 capture-ring synchronization call to use `kIODirectionOut`; `kIODirectionInOut` had been rejected before hardware capture. I have left the earlier bring-up/probe versions and their machine-specific installers out of this source preview.
