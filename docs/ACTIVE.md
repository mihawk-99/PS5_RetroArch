# Active work

_Updated: 2026-09-19_

## Now

**Native `audio_ps5` output is registered, default and verified on the console.**
The owner reported: "I heard left right left right, then retroarch started!"
The diagnostic verified buffering and port lifecycle; a normal follow-up stayed
alive for 45 seconds and was closed by the script. XMB still presents through
RetroArch's Vulkan driver and statically linked libps5vk without refusals.

This follows the user's audio assignment; the previous active file's statement
that audio was outside the XMB step does not restrict this new step. No core
implementation or configuration-persistence work is assigned here.

## Verified build and evidence

- Identity: `801df0ac6a5754c3e6a6ea688f1e49e8ddd2041aeb131a637b8c8b59f4030d55`.
- `tools/run-title.sh --no-build --audio-test --watch 45`: audible channel test,
  193,536 accepted/played frames, zero discarded frames or output errors,
  1,536-frame peak/capacity, 6,144-byte partial nonblocking write. Pause/resume,
  native drain/close, and normal frontend port reopen succeeded. The owner closed
  this first run manually; its early exit is explained.
- `tools/run-title.sh --no-build --no-deploy --watch 45`: no test tones, native
  frontend audio initialized, XMB assets/fonts ready, zero Vulkan refusals/API
  failures, title alive for the full window and script-closed.
- All five `tools/verify.sh` gates PASS; 32 unit tests. Fresh patch replay: 101
  edits applied and one existing upstream Vulkan marker; second pass has 102
  present markers with unchanged bytes. Evidence: 14 records PASS.
- Sanitized evidence/report: `evidence/native-audio/`. Raw captures remain ignored:
  `klog/audio-native-run.log`, `klog/audio-normal-run.log`,
  `klog/audio-test-123431.json`, `klog/retroarch-{123431,123631}.log`.
- PS5_Vulkan unchanged at **6f0ce0d**. ProsperoLight is a read-only reference.
  `video_ps5` and RGUI remain registered/selectable fallbacks.

## Native audio scope

The backend opens 48 kHz signed 16-bit stereo in 256-frame blocks. RetroArch
resamples source PCM to the negotiated device rate. A bounded ring plus output
worker supports blocking/nonblocking writes, byte-based capacity reporting,
pause/resume, error wakeup and cleanup. No SDL or Opus dependency is introduced.
Startup/failure/close diagnostics remain; there is no successful-block logging.
See `docs/REFERENCE.md` for the buffer/lifecycle contract and `docs/TESTING.md`
for the one-shot tone test. Normal launches clear leftover test controls.

**Real core audio, long-duration A/V synchronization and streaming underrun
behavior remain unverified.** Idle/menu silence and partial-grain padding are
not themselves underruns. Configuration saving still reports its known unset
directory; persistence and core integration are separate steps.

## Preserved XMB and logging state

XMB's owner-confirmed rendering and final ordinary build are recorded in
`evidence/xmb-default/`. Patches 0060–0063 and 0065–0066 handle uniform padding,
descriptors, strip geometry, asset paths and padded image coordinates. Static
menu textures use a single mip level pending general mip-chain verification.
Official monochrome assets/font are pinned in `assets/xmb/source.json`.
The screenshot hook remains retired in `parked/xmb-readback/`.

Startup, frontend/core INFO/WARN/ERROR, driver refusals, `trace.txt`,
`retroarch.log` and kernel capture remain. Routine frame-success/input chatter
stays suppressed. GPU profiling is opt-in (`docs/GPU_TIMING.md`). No new XMB
steady-state performance measurement is claimed by this audio step.

## Operating notes

The user authorized console uploads/runs without further permission and necessary
driver fixes with documentation plus a PS5_Vulkan commit. Never interrupt an
existing title. The runner checks idle state before upload and launch. For appended
trace files, select the last `bss check=` substring and verify its build identity.
Never substitute the CPU route to conceal a GPU refusal.
