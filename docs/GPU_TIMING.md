# Buffered Vulkan frame timing and development logs

Normal launches keep logging enabled but do not run the profiler. Keep
`/app0/retroarch.log` for frontend/core/menu lifecycle, INFO/WARN/ERROR messages;
`/app0/trace.txt` for build identity, startup milestones, assertions and driver
refusals/API errors; and host `klog/` captures for kernel failures. Errors are
not buffered away. Successful button transitions and periodic frame/render
markers are routine chatter and no longer write files. Startup probes remain
bounded to their initial calls.

Patch 0058 reasserts the frontend file logger after config/argument processing:
this port's argument rebuilding previously lost the launcher's logging request.
Each frontend startup/core reload records the title's input-derived build identity
in `retroarch.log`. The run tool refuses to accept a log without the expected
identity. This is distinct from the identity check on the uploaded executable.

## Capture and reproduce

Build with `bash tools/verify.sh`, then:

```sh
bash tools/run-title.sh --no-build --gpu-profile 60 --watch 80
python3 tools/analyze-gpu-profile.py klog/gpu-profile-<run-stamp>.tsv
```

The runner requires the shared console to be idle before uploading, checks again
before launching, clears stale capture controls, and closes its title afterward.
It saves `retroarch-<stamp>.log`, `gpu-profile-<stamp>.tsv` and the kernel capture
in ignored `klog/`. Trace output stays in the run transcript. Extract only the
last `bss check=` substring from appended traces and verify its build identity.

`--gpu-profile` accepts 1..60 seconds. Allow at least 15 extra watch seconds for
startup and reporting. The runner writes `/app0/gpu-profile.txt`; title startup
consumes this one-shot opt-in. The runner also removes stale controls on later
runs, including runs without profiling. Neither control nor results are deployed
as part of the normal title. There is no image readback hook.

The profiler skips 120 completed callbacks, then collects nanosecond records in
a bounded 8,192-frame array and aggregates five-second windows **without file
I/O**. When the requested interval finishes, it stops sampling and writes one
buffered `gpu-profile.tsv` report. That reporting pause is outside the sample
and occurs only in an opted-in diagnostic run. A killed/failed run may have no
report; a full buffer is explicitly flagged and rejected by the analyzer rather
than being presented as a complete measurement.

## Meaning of the measurements

`src/vulkan_trace.cpp` uses `CLOCK_MONOTONIC` around real Vulkan function pointers
and around `vulkan_frame`, from its initial diagnostic marker through the return
from the context's swap callback. Arguments and results remain unchanged.
Measurements assume the title's single-threaded video loop and ordinary menu
path, not threaded video or BFI/multiple presentations per callback.

Each `gpu timing:` window in the result gives completed frames, measured seconds,
FPS, and average/maximum milliseconds per frame. TSV records preserve every
frame's phase times and presentation-completion interval in nanoseconds.

| Field | Measurement |
| --- | --- |
| `interval` | Previous completed callback through this completed callback. |
| `outside` | Time between callbacks excluding the menu texture-update callback: menu/input/pacing work and the previous callback's tail. |
| `texture` | Menu `vulkan_set_texture_frame` between video callbacks, including resource creation/reuse, conversion and memory operations. |
| `prepare` | Video callback wall time minus the four explicitly timed API categories below; includes command recording, uploads and other calls. |
| `end` | Real `vkEndCommandBuffer` calls inside the callback. |
| `submit` | Real `vkQueueSubmit` calls, including synchronous driver work/waits. |
| `present` | Real `vkQueuePresentKHR` calls, including display waits. |
| `wait` | Real `vkWaitForFences` and `vkAcquireNextImageKHR` calls. |
| `present_interval` | Time between successful synchronous `vkQueuePresentKHR` returns. |

`outside + texture + prepare + end + submit + present + wait` equals `interval`
for each recorded frame. Maxima can belong to different frames and cannot be
added. An incomplete callback invalidates the next interval instead of charging
an unmeasured gap to it.

These are CPU-observed elapsed times, not CPU utilization or GPU timestamps.
This driver's present call returns after its flip marker reaches VideoOut, so
return intervals provide useful presentation-completion cadence. They are not
hardware scanout timestamps or exact missed-vblank counts. The analyzer reports
intervals above 20/25 ms and rounded estimates of extra 60 Hz intervals, explicitly
labelled as estimates. Small scheduling jitter around 16.67 ms is not proof of a
missed refresh. A cadence near 16.683 ms corresponds to about 59.94 Hz.

The menu's "estimated screen refresh rate" averages observed frame intervals
(`video_monitor_fps_statistics`); startup delays can initially depress it. It
is separate from the driver's declared 60 Hz display mode. This measurement
cannot guarantee other cores/workloads, nor compare 4K Vulkan with the 1080p CPU
fallback as if they rendered the same number of pixels.

Fake-clock tests cover phase partitions, presentation spacing, warmup, delayed
output, capacity limits, stop behavior and dispatch/result preservation. Analyzer
tests reject truncated/error captures and inconsistent timing partitions. Run
`bash tools/verify.sh` for the complete gates. Current results and remaining
limitations belong in `docs/ACTIVE.md` and the committed evidence.
