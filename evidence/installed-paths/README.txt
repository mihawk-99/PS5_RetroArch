# Optional elevation and existing installation paths

Acceptance is limited to RetroArch startup, filesystem compatibility and the
individual console results in capture.json. This is not a claim that every
online feature is working. The installed files stay in place. Saved /app0 paths
are translated at filesystem calls; no settings or playlists are migrated.

RetroArch requests cooperative SDK elevation before starting workers, but only
when the separately started, qualified Lapy daemon publishes a live readiness
marker. Without that marker the title starts normally in its sandbox. A failed
published request stops startup because the daemon may still be processing it.
UID 0 and the original executable's device/inode must both be verified before
the resolved installation is used. The tested root is
/mnt/sandbox/PPSA99169_000/app0. LoadExec must retain /app0 names because the shell
resolves its own title namespace. The picker and ES-DE remain unelevated.

## Reproduce

1. Follow evidence/network-elevation/README.txt to prepare the qualified Lapy
   source and console control path, without applying that old title probe.
   Apply daemon-readiness.patch to the prepared external daemon. It adds only
   application readiness and the resident log location; all transaction and
   kernel preflight guards remain enabled. Build its one-shot form as documented
   there, and its two-request validation form with the additional arguments
   --service --require-client-result --max-requests 2. No daemon or exploit is
   linked into the application. Do not run overlapping daemons or retry an
   ambiguous delivery; inspect the previous result first.
2. Build with the clean pinned PS5_VulkanTemplate checkout:

   PS5_FRONTENDS='es-de picker' PS5_VULKAN_TEMPLATE="$PWD/build/picker-template" \
     bash tools/verify.sh format unit build integration evidence

   Template revision: b577e950684e8bb835746ad0e38495ebcb154fd0.
   Regenerating this workspace's ignored picker tree exposed two existing
   bootstrap requirements: populate external/glm at its pinned
   1ad55c5016339b83b7eec98c31007e0aee57d2bf revision, and record that gitlink in
   the generated title's own Git metadata for stage-notices.py. The local build
   repair archived that exact revision from the existing sibling checkout and
   used git update-index --add --cacheinfo 160000,<revision>,external/glm followed
   by a commit in build/picker/title only. No dependency version was changed.
3. Copy console_test.py into build/installed-paths/ and the previous evidence's
   console.py into build/network-elevation/. Console connection details come
   only from ignored .env. Save exact original executable backups first; turn
   FTP SELF transfer mode off for byte-for-byte readback. Confirm the console is
   idle. The runner deploy command replaces only the three executables and
   verifies their stored SHA-256. Preserve the user's current configuration.
4. Run unique labels (the harness refuses an existing capture directory):

   python3 build/installed-paths/console_test.py deploy
   python3 build/installed-paths/console_test.py offline offline-final
   python3 build/installed-paths/console_test.py picker frontend-final es-de online
   python3 build/installed-paths/console_test.py picker return-final retroarch online

   The picker controls are one-launch controls. The harness restores the
   frontend preference afterwards. The ES-DE test records rendered frames.
   Inspect daemon_result and root samples after every run; do not interpret an
   exit-observation timeout as either balanced cleanup or a proven leak.
5. Optional online checks use a separately prepared PC RetroArch/FCEUmm and a
   generated diagnostic NES fixture in build/netplay-host/. For achievements,
   use the owner's existing supported content specified in ignored
   klog/installed-paths/achievement-game.json (keys core and path). Never copy
   account values or content names into committed evidence. Run labels beginning
   achievements, badge-download, netplay-host or netplay-join. The badge-only
   test uses an artificial username with no password/token and deletes only the
   temporary test placeholder, not the user's normal thumbnail cache.

## Limits and failures retained

- The first implementation required elevation unconditionally, causing a splash
  exit when the daemon was absent. Original executables were restored and the
  owner confirmed normal launch before the optional readiness fix was tested.
- Mapping LoadExec to a real filesystem path caused shell error 0x80aa001a.
  Removing that mapping restored frontend handover.
- Elevating ES-DE at main failed its single-thread preflight (threads=2,
  error=16), because its static texture loader already starts a worker. No
  kernel guard was weakened. Picker/ES-DE changes were removed.
- Long netplay tests exceeded the one-shot daemon's 60-second exit-observation
  window. Subsequent preflight samples returned to 73/72. Some shorter hosting
  runs accepted TCP without reaching player 2; the longer run did complete the
  handshake. Sustained interactive gameplay and WAN/lobby behavior are not
  qualified by that handshake.
- The owner confirmed the saved credentials work on the website, but the
  console API rejected them. Config files seen through the resolved folder
  matched the saved files; no stale token or truncation was found. Exactly one
  explicitly approved host sign-in attempt returned HTTPError; that harness
  discarded its response details, so it is inconclusive and was not repeated.
- FTP MLSD ignored its path argument. Earlier directory-based badge counts were
  invalid; use direct file readback and PNG validation instead.
- Marker PID/uptime checks reject common stale markers, but do not guarantee
  process identity after abnormal exit and PID reuse. No automatic daemon
  installation, autoload or router/firewall change is part of this step.

Raw captures, screenshots, settings and account data remain in ignored klog/.
The committed capture contains only selected non-sensitive records and results.
