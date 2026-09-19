# Active work

_Updated: 2026-09-19_

## Now

**The user ended further performance investigation and requested committing the
logging work, then enabling XMB as the default menu and testing it on PS5.**
That is the next task; do not continue optimizing RGUI frame timing.

## Logging step

Routine periodic frame/render/API-success messages and successful input-button
traces are removed; bounded startup diagnostics and all errors remain. Menu
handover/staging-ask probes now log once. `retroarch.log` is explicitly enabled
after argument/config processing and records this build's identity; the old file
had been stale because this port rebuilds arguments. INFO/WARN/ERROR output,
Vulkan refusals/assertions in `trace.txt`, and host kernel capture remain enabled.

Profiling is off by default. `tools/run-title.sh --gpu-profile 60 --watch 80`
opts into 120 warmup frames followed by up to 60 seconds of in-memory timing.
The report is written only after measurement and the one-shot control is consumed.
The runner checks idle state before upload/launch, retrieves `retroarch.log` and
optional timing, and rejects a frontend log without the expected build identity.
Definitions/replay: `docs/GPU_TIMING.md`, `tools/analyze-gpu-profile.py`.

## Verified results and limits

- First buffered run: 3,597 frames / 60.0098 seconds, **59.940238 FPS**;
  maximum frame interval **16.905813 ms**, no presentation-return gap above 20 ms.
  Owner: "Very smooth", stable estimated refresh **59.941 Hz** after startup.
- Restoring fresh frontend logging exposed three ~100 ms texture-update stalls
  from remaining first-four-call diagnostics. They were reduced to one initial
  message. The logger now contains this build's identity and Vulkan startup.
- Final refined build launched with zero driver refusals, logged each menu
  handover probe once, and returned from `rarch_main` with status 0 before the
  profile completed. The runner rejected the missing report. **No final timing
  result is claimed.** The user requested proceeding to XMB instead of more tests.
- `evidence/vulkan-buffered-logging/` contains both completed timing analyses and
  the final startup/logging evidence, including that incomplete final capture.
- All five `tools/verify.sh` gates PASS; 28 tests. Fresh vendor patch replay and
  second-pass idempotence PASS: 86 edits.
- Installed identity:
  `a6063c61145fb42ac409a1d78026f29865673e4575e9a6afc3500e4abccc4f66`.
- Fresh frontend logs retain the existing audio-driver initialization errors and
  missing configuration-save directory error. These are outside this logging/menu
  step, not Vulkan failures; audio/config persistence are not claimed complete.

Presentation measurements are CPU timestamps at this driver's synchronous
present return, not hardware vblank counters. RGUI's refresh estimate averages
frame intervals; startup frames depress it. No guarantee for other workloads.

## Preserved GPU acceptance and operating notes

Correct GPU RGUI is recorded in `evidence/vulkan-fragment-inputs/`: RGBA conversion
**947134d**, driver compiler fix **6f0ce0d**, owner-confirmed correct colours and no
flicker/triangles. PS5_Vulkan was not modified in this logging step. `video_vulkan`
uses statically linked libps5vk; `video_ps5` stays registered/selectable.

The user authorized necessary driver fixes with documentation and a commit in
PS5_Vulkan, and console uploads/runs without further permission. Never interrupt
an existing title to test. Raw captures remain in ignored `klog/`. Extract the
last `bss check=` substring and verify identity before reading appended traces.
No partial switch to the CPU route if the GPU refusal gate cannot be satisfied.

XMB preparation: the current build disables HAVE_XMB, only RGUI fonts are staged.
Official assets were fetched into ignored `vendor/retroarch-assets` at
`73106363e14e34c08a5854b4cfbc29f184e3b783`; monochrome icons/font are available.
No XMB implementation change has landed yet.
