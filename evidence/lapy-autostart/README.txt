# RetroArch starts its bundled Lapy service

Acceptance: from an idle console with no Lapy readiness marker or running Lapy
service, launch the packaged title. Its startup log must record the complete
bundled ELF delivery, verified UID 0 and the same installation executable.
RetroAchievements must authenticate and load its session/achievement set.
A second launch must reuse the same service PID (no second payload delivery),
pass the same account checks, and quit normally. The resident daemon must record
successful transactions and client proofs. No Payload Manager configuration or
boot hook is involved; the console's existing local ELF loader is required.

Build: PS5_FRONTENDS="es-de picker" PS5_VULKAN_TEMPLATE="$PWD/build/picker-template" \
       bash tools/verify.sh
The existing build/picker-template checkout is at the pin in tools/build-picker.sh.
This builds all targets, including tools/build-lapy.py, and stages the service,
licences and source metadata. The service source is identical to the qualified
prepared source recorded by evidence/network-elevation and installed-paths.

Console reproduction uses the private connection helper and installed-path runner
prepared as documented in evidence/installed-paths/README.txt. Keep all raw
captures, account data and game paths outside the repository. Upload the packaged
eboot.bin and lapy-root-daemon.elf with stored-byte verification, preserving the
console's configuration. Run the installed-path achievement fixture twice with
runner.run(label, daemon=False). Do not inject a payload. Remove temporary test
controls afterward. Capture startup-paths.log, lapy-ready, lapy-service.log,
retroarch.log and the kernel stream for each run; verify normal exit and account
fields unchanged. Preserve the original binary for rollback before deployment.

The netplay regression reuses the prior original diagnostic NROM and PC peer.
Copy console_netplay.py and analyse_netplay.py from evidence/online-features to
a private temporary folder, then apply netplay-resident.patch and
netplay-analyser.patch respectively. Run the console script from the repository
root with a new /tmp capture folder and relay-join; run the analyser on that folder.
The adaptations reuse the already-running service and capture its client proof.
They deliberately do not claim a resident daemon has exited or that final root
counts were sampled after exit. Traffic, controller input and matching-state
requirements remain unchanged.

Replay: python3 tools/evidence.py compare evidence/
