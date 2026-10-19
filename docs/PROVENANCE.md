# Credits, source provenance and licensing

I'm **MarkPLacer**. This driver was **largely written with the help of AI, specifically GPT-6.1 Sol**, including substantial help with the implementation, debugging, research and documentation. I tested it on my own Razer Blade 17, including recording and listening to my voice in Voice Memos.

I distribute my project changes under BSD-3-Clause and retain the upstream authors' attribution and license terms. The AI assistance disclosure does not replace their credit or relicense their work.

| Source | Pin / use | Terms / notices |
| --- | --- | --- |
| DexterSLamb/CmlSOFAudio, through Shubham Banekar/LatSOFAudio | LatSOFAudio `41fda77bd5321464fef5ba71a44437f993215d55`; DSP/pipeline approach, IOAudioFamily publication/timing and PCM conditioning | BSD-3-Clause; full original license in `licenses/LatSOFAudio-BSD-3-Clause.txt` |
| Intel Linux SOF | Linux `58161ead29e50d70a317ea26b472e0c7508e69da`; ADL ROM/stream/IPC sequences and register definitions | Relevant files offer GPL-2.0-only OR BSD-3-Clause; I use the BSD alternative and retain Intel credit |
| SOF source | `57864296b07c5ffd673214dd5b1e3d07ee274a22` (v2.2); IPC3 packet layout reference | BSD-3-Clause; Intel notices preserved |
| SOF firmware | sof-bin `fcd5ae85ead7ecd67ff199335b61fac825843ca0`, `v2.2.x/sof-v2.2/intel-signed/sof-adl.ri` | Complete release license, including component notices and patent terms, in `licenses/SOF-LICENCE.Intel` |
| MacKernelSDK | `3f750085caa17ec3a7880f11c11bf4f48cd6a164`; fetched build headers and linked `libkmod.a` | SDK's `LICENSE.txt` is preserved in `licenses/MacKernelSDK-APSL-2.0.txt`; individual files retain their notices in the fetched source |

Intel authors credited by the adapted Linux SOF files include Liam Girdwood, Ranjani Sridharan, Rander Wang and Keyon Jie. Copyright years vary by file, including 2017–2021. The BSD terms are reproduced in `licenses/Intel-SOF-BSD-3-Clause.txt`.

I have not included the donor's retired user-space HAL plugin, Apple NullAudio sample implementation or Comet Lake firmware. I preserved the donor license verbatim, so it still mentions those components. I used Apple IOAudioFamily/XNU as system interface references rather than copying them into the project source.

The firmware downloader checks the actual upstream ADL firmware and exact license hashes. A local build embeds that firmware in the kext; binary distributors must carry its complete notices, plus notices for the linked SDK runtime. The build places licenses in the bundle resources. Before distributing a binary, review the complete applicable terms rather than labeling the whole artifact “BSD only.”

The hardware file contains decoded scalar facts, not an OEM table or filter coefficients. The generated message is byte-identical to the tested computed-DMIC IPC3 command. BIOS images, raw NHLT, Windows drivers, recordings, private paths, serials, account credentials, full system logs, kernel collections and installation-specific backups are excluded.

I kept the working 0.3.2 kernel source unchanged while preparing this preview. I added portable build scripts, generated assembly locations, packet generation, documentation and distribution metadata. I retained version 0.3.2 and the existing bundle identifier so the packaging changes are not confused with a new kernel implementation.
