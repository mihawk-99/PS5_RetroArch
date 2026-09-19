# Quiet Vulkan frame timing

Build with `bash tools/verify.sh`, confirm the shared console is idle, then run
`bash tools/run-title.sh --no-build --watch 45`. The runner clears stale capture
arguments and closes the title at the end. No image readback hook is active.
Extract only the last launch from the appended trace, starting at the last
`bss check=` substring, and verify its input-derived build identity.

Patch 0056 limits the formerly unbounded RGUI texture-handover, Vulkan
texture-update, texture-creation and staging-copy diagnostics to their first four calls. Existing
startup, sparse progress and error/refusal reporting stays enabled. Timing adds
no per-frame file writes or allocation: one summary appears after each window
of at least five seconds. The first four completed video callbacks are warmup.

`src/vulkan_trace.cpp` uses `CLOCK_MONOTONIC` around the real Vulkan function
pointers and around `vulkan_frame`, from its initial diagnostic marker through
the return from the context's swap callback. Results and arguments are preserved.
The current title's video loop is single-threaded; this instrumentation is for
that configuration and the ordinary menu path, not threaded rendering or BFI.

Each `gpu timing:` line records completed frame count, measured seconds,
`fps = frames / seconds`, and **average/maximum milliseconds per frame**:

| Field | Measurement |
| --- | --- |
| `interval` | Previous completed callback through this completed callback; includes frontend work between them. |
| `outside` | Previous callback completion through this callback entry, excluding `texture`: frontend/menu/input/pacing work, previous callback tail, and summary I/O. |
| `texture` | Menu `vulkan_set_texture_frame` callback between video frames, including resource creation/reuse, CPU conversion and memory mapping/flushes. |
| `prepare` | Callback wall time minus the four explicitly timed API categories below; includes command recording, uploads and other frontend/driver calls. |
| `end` | Sum of real `vkEndCommandBuffer` calls inside the callback. |
| `submit` | Sum of real `vkQueueSubmit` calls inside the callback, including synchronous driver work/waits. |
| `present` | Sum of real `vkQueuePresentKHR` calls inside the callback, including display waits. |
| `wait` | Sum of real `vkWaitForFences` and `vkAcquireNextImageKHR` calls inside the callback. |

Average `outside + texture + prepare + end + submit + present + wait` equals average
`interval` apart from printed rounding. Maxima can belong to different frames;
they must not be added. An early return without a completion hook invalidates
that interval rather than charging its gap to the next completed frame.

These are CPU-observed **elapsed times**, not CPU utilization or GPU execution
timestamps. A long submission locates the stall inside the driver call but does
not distinguish cache maintenance, CPU copies, kernel submission and waiting for
the GPU. A long `prepare` likewise requires further separation before assigning
blame to RetroArch rather than driver command-recording operations. Measurements
at the driver's current 3840x2160 swapchain are not a like-for-like comparison
with the CPU fallback's 1920x1080 output.

The fake-clock regression in `tests/test_vulkan_trace.py` checks partition sums,
warmup exclusion, five-second windows, reset behavior and dispatch/result
preservation, including timeout/suboptimal acquire results. Measured run records
live under `evidence/`; current findings belong in `docs/ACTIVE.md`.
