# Active work

_Updated: 2026-09-19_

## Now

**XMB is the default menu and is verified on the Vulkan GPU path.** The owner
reported "It's flawless!" after the texture-coordinate and single-level texture
correction. GPU readback shows readable text, intact icons and the full-screen
blue gradient/ribbon. The final normal build removes screenshot instrumentation
and completed its own 45-second console run with zero refusals or API failures.
No further performance investigation or new feature is assigned.

## Verified build

- Final identity: `2df0e277df59faf661a37920988d1b804394c8ffe509c2b57296270f3dcaf7b3`.
- Command: `tools/run-title.sh --no-build --watch 45`; it stayed alive and the
  script closed it. Current `retroarch.log` identity verified; XMB reports
  `Assets missing: no; fonts ready: yes`. Successful swapchain presentations,
  no command-buffer errors, no framebuffer capture writes.
- All five `tools/verify.sh` gates PASS; 31 unit tests. Fresh source replay and
  second-pass idempotence PASS: 100 named edits. Evidence replay: 13 records PASS.
- Evidence and unsuccessful diagnostic runs: `evidence/xmb-default/`.
  Raw logs/images stay ignored in `klog/`; the final run is `xmb-final-run.log`.
- PS5_Vulkan is unchanged at **6f0ce0d**; this title statically links libps5vk.
  `video_ps5` and RGUI remain registered/selectable as fallbacks.

## What XMB required

Configure enables XMB and retains RGUI. The configure-argument fingerprint avoids
silently keeping an old RGUI-only build. Official monochrome assets are pinned
in `assets/xmb/source.json`: 120 fixed icons, M+ 1p font and licenses.

Named patches 0060–0063 and 0065–0066 address the ribbon's uniform padding,
unused descriptor slots, incorrect strip expansion/counts, the title's asset
path, and padded-image coordinates. The null platform frontend does not run
Unix asset initialization, so the port seeds `/app0/assets` directly. Important
XMB path/loading diagnostics remain in the frontend log.

Static/menu textures use one mip level: full mipmapped icons were corrupted
under the tested driver path. General mip-chain correctness remains unverified.
`docs/REFERENCE.md` describes these compatibility constraints. The screenshot
hook is retired in `parked/xmb-readback/`; it is absent from normal builds.

## Logging and performance scope

Logging work was committed as **d46495d** before starting XMB. Startup diagnostics,
frontend/core INFO/WARN/ERROR, driver refusals, `trace.txt`, `retroarch.log` and
host kernel capture remain. Routine frame-success/input chatter stays suppressed.
Profiling is off by default; the opt-in procedure is in `docs/GPU_TIMING.md`.

The owner saw an initial 25 Hz estimate in the successful screenshot build.
That build synchronously wrote large framebuffer/resource captures after startup;
its refresh estimate is not a steady-state performance measurement. Those writes
are removed. **Steady-state XMB performance has not been profiled.** Prior RGUI
59.94 FPS evidence remains in `evidence/vulkan-buffered-logging/`.

Existing audio initialization errors and the unset configuration-save directory
remain visible and out of scope. Configuration persistence, cores, audio and
per-system playlist artwork are not claimed complete by this menu step.

## Operating notes

The user authorized console uploads/runs without further permission and necessary
driver fixes with documentation plus a PS5_Vulkan commit. Never interrupt an
existing title. The runner checks idle state before upload and launch. For appended
trace files, select the last `bss check=` substring and verify its build identity.
Never substitute the CPU route to conceal a GPU refusal.
