# RetroArch connection privilege comparison

This is a frozen diagnostic from the owner's requested console experiment, not
an elevation feature or a release. PS5SX2 was not installed; the owner selected
comparison with its documented Lapy path instead of a live PS5SX2 process.
Reference source: Swordpdf/PS5SX2 at
`6ccf48047a1221b518dd2dbd2535b53d1f10a108`,
`ps5/coreorbis/main-boot.cpp` and `orbis-shims/ProsperoHenJailbreak.cpp`.

## Measured result

On the base PS5 (13.40), the same diagnostic RetroArch process was observed
before and after one cooperative elevation request. An independent, read-only
payload read credentials using the public SDK getters. The probe ran first in
`main`, before frontend workers, kept its log descriptor open through elevation,
and returned without starting a game or changing user settings.

- Before: one thread, real/effective/saved UID 1, title authid
  `4400001084c2052d`, caps `000000008003002000ff000000000000`, sandbox root.
- `socket()` and `sceNetSocket()` both created descriptors. `F_GETFL` returned 2.
  **`F_SETFL(flags | O_NONBLOCK)` returned -1 / EACCES (13).** No `connect()`
  happened. Earlier logging around the whole connection helper misidentified
  this as a failed connection.
- After: same PID, one thread, real/effective/saved UID 0, system authid
  `4801000000000013`, all capability bytes `ff`, system root. The jail directory
  remained non-null (the daemon's owned-root design retains that reference).
  Native `getgid()` returned 0, but `getegid()` still returned 1.
- The same nonblocking setup then passed. Both socket APIs connected to both
  frozen RetroAchievements/media IPv4 addresses on port 443: EINPROGRESS (36),
  poll writable, and `getsockopt(SO_ERROR)` equal to zero. Four TCP connections.
- `/app0` was readable before and inaccessible after. `/data` was readable
  even before elevation; visibility by itself was not proof of elevated identity.
- Lapy verified its client write/read proof, detached, reaped its donors, and
  returned root counters from 73/72 to 73/72 after the title exited.

A separate PC listener on port 19335 timed out on both elevated attempts. No
connections reached the listener. Host firewall inspection required unavailable
sudo credentials. This endpoint remains unqualified and is excluded from the
public-TCP acceptance; it establishes neither successful nor prohibited LAN
netplay. No router settings were changed.

No TLS handshake, account sign-in, badge download or netplay session was tested.
Elevation changed multiple credential fields and the filesystem root together;
this experiment does not identify the individual permission bit responsible.
Testing `ioctl(FIONBIO)` without elevation is a separate possible next step.

## Inputs and reproduction

Raw logs, full process inventories, endpoint addresses, the exact original
console executable and backup hashes remain in ignored `klog/network-elevation/`.
`capture.json` records their hashes, the tested executable hash, daemon identity,
and the exact restored executable hash. The endpoint addresses were resolved
once on the host and reused in every run; this test did not repeat DNS.

The adjacent C/C++ and Python files are the actual diagnostic sources used.
They are evidence fixtures, not a general-purpose deployment tool. In particular,
`run.py` deliberately refuses to overwrite an existing run directory, and an
operator must inspect failures and restore the backed-up executable afterwards.

Preparation on this workspace:

1. Copy `probe.hpp`, `inventory.c`, `console.py` and `run.py` to
   `build/network-elevation/`. Record the current diff. Save `src/main.cpp` and
   the console's exact `eboot.bin` before making changes; turn FTP `SELF` transfer
   mode off for backup/readback so the stored signed executable is captured.
2. Apply `title-probe.patch`; build with `bash tools/build-title.sh`. The hook
   does nothing without `tests/network-elevation.control`. It returns from main
   after a requested diagnostic. Build the independent inventory with:

   ```sh
   PS5_CLANG=clang .deps/native/ps5-payload-sdk/bin/prospero-clang \
     -std=c11 -O2 -Wall -Wextra -Werror build/network-elevation/inventory.c \
     -o build/network-elevation/inventory.elf
   ```

3. Copy the prepared `source/` and `tools/` directories from
   `../PS5_Proton/.deps/lapy-validation/source/` into
   `build/network-elevation/lapy/`. This is Lapy revision
   `5b8397b9f2b5f12a7bc2f9c8745a00d1c2dd01ad` with Proton's six recorded patches
   (`stdout-mirror`, `donor-wait`, `donor-release`, `service-lifecycle`,
   `deterministic-build`, `resident-log`). Apply `daemon-sdk-and-log.patch` there.
   It changes only the logging destination/availability and the modern SDK's
   attribute API: a 32-byte array with the same ptrace bit at byte 3. All
   preflight, transaction and teardown checks remain enabled. Build:

   ```sh
   PS5_CLANG=clang python3 build/network-elevation/lapy/tools/build_owned_daemon.py \
     --sdk "$PWD/.deps/native/ps5-payload-sdk" \
     --logging-client "$PWD/../prospero-win-main/native/ps5log" \
     --title PPSA99169
   ```

4. Use the existing ps5vkctl loader/control path. Confirm the console is idle
   and no elevation daemon is already running. The experiment used websrv's
   `/elfldr` because the console's TCP 9021 endpoint was not reachable from the
   host. `console.py inventory <capture-name>` reads process identity and exits.
5. With the original executable safely backed up, run
   `python3 build/network-elevation/run.py baseline`, then
   `python3 build/network-elevation/run.py elevate`. The second command starts
   exactly one one-shot daemon and requires its preflight baseline before
   launching the probe. It does not retry failed or uncertain daemon delivery.
6. Require `probe_complete`, the expected before/after observations and Lapy's
   `stage=complete error=0 ... root_balanced=1`. If the daemon enters a held
   state, follow its upstream recovery procedure; never force past its checks.
7. Restore the exact original console executable and compare its stored SHA-256.
   Restore/remove only the test controls and logs created for this experiment.
   Remove the temporary source hook, rebuild the normal staged tree, and leave
   the console idle. Never leave the diagnostic binary installed.

Replay the committed expectations with `python3 tools/evidence.py compare evidence/`.

## Resolved exploratory failures

The first title build hit sandbox cache/dependency write restrictions; the
normal approved build resolved them. A missing socket header in the temporary
probe was corrected before deployment. The format gate found two existing
unformatted diagnostic lines in `src/net_shims.c`; only formatting was applied.

The first daemon binary, built with Proton's older SDK, failed in CRT startup
with SIGILL at `ud2`, before its log or any elevation request. The failed payload
exited; the console stayed responsive. Rebuilding against the project's working
SDK required adapting the old scalar attribute API to the SDK's 32-byte API.
The resulting daemon passed every runtime preflight and completed the transaction.
The failed attempt and its disassembly remain in the ignored capture/build folders.

## Verification

`bash tools/verify.sh` passed all five gates after the source hook was removed:
192 unit tests, normal PS5 build, 192 integration tests, and 122 evidence captures.
`validation.json` records the gate log hash and absence of the probe in the normal
staged executable. The installed console executable was restored from its original
backup, not replaced by that newly built staged executable.
