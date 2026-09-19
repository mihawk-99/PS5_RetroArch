# Active work

_Updated: 2026-09-19_

## Now

**Quiet GPU profiling is verified.** The new performance request supersedes the
previous note that performance was outside the completed menu-rendering task.
The final 45-second run produced eight timing windows: **53.76–57.94 FPS**, or
**55.92 FPS** over 2,239 measured frames / 40.039 seconds. There was no progressive
collapse. This is not yet steady 60 FPS: isolated frame spikes remain.

The owner reports "Noticeable improvement" and an estimated refresh-rate display
that starts near 40 Hz and rises. That display averages observed frame intervals
(`video_monitor_fps_statistics`), not the fixed 60 Hz output mode. Slower early
frames can depress it while later faster frames raise it.

## What the measurements distinguish

Final weighted averages, milliseconds per frame:

| Work | Time |
| --- | ---: |
| Video callback preparation/other recording work | 5.344 |
| Command-buffer finalization | 0.020 |
| `vkQueueSubmit` | 3.525 |
| `vkQueuePresentKHR` | 8.075 |
| Fence/image acquisition waits | 0.041 |
| Menu texture update outside the callback | 0.153 |
| Other time between video callbacks | 0.724 |
| Total frame interval | 17.882 |

The preliminary run quieted three logs but left texture-creation/staging-ask
logging active. Its rate fell from 35.91 to 1.63–1.98 FPS; the growing delay was
outside the video callback (up to 958 ms), while submit stayed near 3.5 ms.
Limiting the other two logs removed that collapse in the final run. This strongly
implicates this port's unbuffered diagnostic I/O, rather than long submission or
presentation stalls, in the severe slowdown. No driver code changed.

These are CPU-observed elapsed times, not GPU timestamps or CPU utilization.
The final trace still has occasional spikes (maximum interval 282 ms, including
early work; later windows reach 167–184 ms). Sparse progress logs and the timing
summary itself remain possible contributors, not measured root causes of every
spike. Definitions and reproduction: `docs/GPU_TIMING.md`.

## Evidence and validation

- `evidence/vulkan-quiet-timing/`: both run windows, identities, sanitized API
  counters, owner observations and timing audit. Final trace has zero refusals,
  no recorded API errors and no CPU fallback initialization.
- Final tested and installed identity:
  `38ada4be3b74b8230ec13aae97b7d3618c4635760c30e90b29a54f0008e14450`.
- All five `tools/verify.sh` gates PASS, 26 tests. Fake-clock regression verifies
  phase partitions, warmup exclusion, window reset and unchanged API results.
- Fresh vendor patch application and second-pass idempotence PASS: 77 edits.
- Both 45-second runs completed and were closed by their scripts. All five
  formerly unbounded texture diagnostics emit only their first four calls.
- PS5_Vulkan remains at **6f0ce0d**, unmodified in this profiling step.

## Preserved GPU acceptance and operating notes

The owner-confirmed correct GPU menu remains recorded in
`evidence/vulkan-fragment-inputs/`: RGBA conversion **947134d**, fragment compiler
fix **6f0ce0d**, no flicker/triangles. `video_vulkan` uses statically linked libps5vk;
`video_ps5` stays registered and selectable. No partial rendering-route switch.
Temporary image/resource captures remain retired under `parked/gpu-readback/`;
the failed blend experiment remains under `parked/explicit-stock-blend/`.

The user authorized necessary driver changes with documentation and a commit in
PS5_Vulkan, and console uploads/runs without further confirmation. Check the shared
console is idle **before uploading**; never interrupt an existing title to test.
`tools/run-title.sh` owns deployment, identity readback, launch, watch and closure.
Raw console data stays in ignored `klog/`. Extract the last `bss check=` substring
from appended traces and check the build identity before interpreting results.

Further performance work should isolate remaining spikes and summary/progress
I/O before changing driver synchronization. Core/audio work is a separate scope.
