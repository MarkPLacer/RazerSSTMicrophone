# Pre-publication check

On 2026-10-19, I checked the files intended for publication and the earlier versions in my local Git history, with help from GPT-6.1 Sol.

## Privacy and contents

I found no private credentials, private user paths, device serials, network identifiers, microphone recordings or raw system logs in the reviewed source. The repository excludes BIOS images, raw NHLT tables, Windows drivers, firmware binaries, downloaded SDK files, kernel collections, machine-specific installers and recovery backups.

The remaining identifiers have a purpose: my public GitHub handle identifies the project, PCI/subsystem IDs describe the supported hardware, and the Mach-O UUID identifies a driver build rather than my laptop. Commit authors use my GitHub noreply address. Public upstream author credits and license text are retained.

I checked both file contents and the exact publication inventory. Pattern scans supplement that review; they cannot prove that every possible secret has been detected.

## Build and correctness

I exported a clean source tree containing only the reviewed Git files, fetched the checksum-pinned dependencies, and ran the protocol/PCM sanitizer tests, DAI packet test, build, plist check and strict ad-hoc signature verification. They passed. The rebuilt executable matched my working installed executable byte for byte after removing signatures from temporary copies.

I also checked local documentation links, first-person wording, the GPT-6.1 Sol disclosure and the preserved upstream license notices. The public build still depends on the exact hardware and installation constraints documented in [INSTALL](INSTALL.md).

## Remaining limits

This is an experimental source preview tested on my own Blade 17. Full sleep/wake, other hardware, long calls, multi-client recording, measured latency and battery impact remain unverified. I have not treated the privacy check, successful build or automated tests as an independent security audit or proof of stable operation on other systems.
