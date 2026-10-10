# Bundled Lapy service

RetroArch starts this external service through the existing console-local ELF
loader before its workers, then waits for preflight readiness and requests SDK
elevation. Subsequent launches reuse the resident service. There is no boot hook
or Payload Manager configuration. Without a loader/service, offline startup is
still available; a published but unverified elevation request refuses startup.

`python3 tools/build-lapy.py` exports Lapy revision
`5b8397b9f2b5f12a7bc2f9c8745a00d1c2dd01ad` from the sibling
`../PS5-Lapy-JB-Daemon` repository. It applies the six qualified Proton patches
in this folder, then the SDK/log and readiness adaptations already recorded in
`evidence/network-elevation` and `evidence/installed-paths`. Runtime source is
identical to the previously qualified service; all preflight, transaction and
teardown guards remain enabled. Upstream source stays in ignored `build/lapy`.

The existing `../prospero-win-main/native/ps5log/ps5log.h` input is hash-pinned;
the project's current SDK builds the ELF, and its import contract is checked.
The normal title build stages `lapy-root-daemon.elf` and its licence notices.
Only title PPSA99169 is accepted. Resident diagnostics live beside the title in
`lapy-service.log`; readiness is `lapy-ready`. Both are runtime files.

Console acceptance for automatic startup, reuse and online play is recorded in
`evidence/lapy-autostart/`.
