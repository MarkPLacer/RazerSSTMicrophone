# Integration and recovery

This is a **developer source preview**. It is not a one-click end-user installer. Building successfully does not install the device or demonstrate compatibility with another machine.

## My installation

On my laptop, I use `RazerSSTMicrophone.kext` in `/Library/Extensions`, included in the auxiliary kernel collection. A root-owned one-shot startup publisher waits for a normal console login and at least 70 seconds of boot uptime. It verifies its own files and the exact bundle hashes, performs the normal kext load, checks the expected service version, then sends the single `PublishMicrophone=true` property through the included `publish` helper. Publication itself does not begin audio capture. IOAudioFamily opens the hardware only when an application starts recording.

I have kept my machine-specific installer, recovery archives, kernel collections and launcher metadata out of this package. They contain installation state for unrelated system extensions and private local paths. Reusing them on another computer could remove extensions from its collection.

The helper is built as `build/publish`. It requires root and only publishes an already-loaded matching service; it does not install the driver, rebuild a collection, bypass system approval, or handle the startup wait by itself. It is provided for a reviewed integrator, not for running automatically from an untrusted checkout. Any installed helper, launcher and configuration must be root-owned and unwritable by ordinary users.

## Requirements before an integration trial

1. Verify the exact PCI/subsystem/function, BAR lengths, firmware identity, microphone wiring, shared HDA ownership and working output configuration. Do not broaden the hardware match just to make a service appear.
2. Preserve a known-good boot path and a complete recovery copy of the current auxiliary collection and installed extensions. Include other third-party extensions when constructing a candidate collection.
3. Stage the candidate and inspect its dependencies, versions, hashes and architecture before activation. This source depends on `com.apple.iokit.IOAudioFamily` and `com.apple.iokit.IOPCIFamily`; EFI injection alone has not been validated.
4. Use the system's normal extension authorization process and reboot into a reviewed candidate. Do not automatically relax SIP, AMFI or TCC. My laptop already has modified system policy for other drivers, so I cannot claim this driver needs no policy changes on another configuration.
5. Verify the loaded UUID/version after reboot, not merely the file on disk. Then publish the input and test through ordinary microphone permission controls.

## Updates and rollback

Never hot-unload this driver. Never truncate or overwrite an auxiliary kernel collection that may still be mapped. I previously encountered an OSKext/kernelmanagerd panic during collection replacement. That was a separate event from the later frozen wake on my laptop.

A reviewed update must stage a separate complete collection, preserve its backup, use an atomic replacement with a new inode when activating the collection, and reboot. Rollback must also preserve the mapped-file boundary. The exact recovery commands depend on the installed system and collection contents, so a generic destructive script is deliberately absent.

Removing a startup publisher stops future publication; it does not unload an already running kernel extension. Remove or restore staged components through the same reviewed reboot workflow.

## Recording verification

Select **Razer Internal Microphone**, not an AppleHDA input with a similar name. The exact UID is:

```text
RazerSSTMicrophone:8086-51C8:1A58-3005:PDM1
```

The optional `build/mic-input-check` opens only that UID for three short capture cycles and prints aggregate frame, RMS, peak and clipping statistics. It does not select a different input or save samples. Run it only when intentionally testing capture, with normal macOS permission. A permission denial is not a DSP diagnosis; do not edit the TCC database to work around it.
