# Active work

_Updated: 2026-09-19_

## Now

**Vulkan API calls succeed; the shell splash covers the app.** The owner saw the
app background indefinitely in the new diagnostic run and manually closed the
app before the requested 90-second window ended. No menu visibility is claimed.

The current objective is the user's GPU menu acceptance (A1-A4 and B1-B4), with
`video_ps5` registered and selectable and `../PS5_Vulkan` strictly read-only.
The previous Next item suggested instrumenting the sibling driver; that conflicts
with the user's boundary and was not followed.

## Last verified step

Added frontend symbol-loader wrappers for every `vkEndCommandBuffer`,
`vkQueueSubmit` and `vkQueuePresentKHR` call, including per-swapchain results.
They forward arguments and results unchanged, log every non-success and sample
success totals. Menu probes record alpha, viewport and the complete matrix.

- `bash tools/verify.sh`: all five gates PASS; 21 tests pass before and after build.
- `bash tools/run-title.sh --no-build --watch 90`: uploaded and launched. Owner
  manually closed it; the runner's "exited on its own" verdict is incorrect.
- New startup segment: 537 lines, zero `vulkan: ` refusals. First four observed
  command-buffer, submission and presentation results are zero, as are the four
  per-swapchain results. Fewer than 600 calls reached the next sampled counter.
- Quad alpha is 1; viewport is (480,0), 2880x2160; matrix maps the unit quad into
  clip space. These observations do not establish displayed pixels.
- Evidence: `evidence/vulkan-api-results-splash/`; raw captures remain in `klog/`.
- Staged diagnostic eboot SHA256:
  `78eb1fa9fafe509f0e5b7e62bc4930b39bb88f5b238718805714f8bd002c8c56`.
- Sibling source was clean at `560e463a72df46056c068235de7079377c70fbb8`;
  its files were only read. Driver archive SHA256 is in the capture record.

## Next smallest step

Move title splash dismissal into startup independently of the video driver.
`src/display.cpp` calls `sceSystemServiceHideSplashScreen()` only when video_ps5
opens; the Vulkan route never invokes it. The sibling's diagnostic title calls it
as well. Kernel logs confirm SplashScreen.PPSA99169 remained the focused scene.
Build and verify the change, then obtain the shared-console permission for the
next upload/launch. Completion still requires zero refusals, successful command
recording and swapchain presentation over a full run, and owner-confirmed RGUI.

## Working notes

- `bash tools/run-title.sh` owns build/deploy/launch/watch/close/capture. The shared
  console policy in `docs/DEPLOYMENT.md` asks before each upload and launch.
- The trace is append-only. Split on the LAST `bss check=` substring, not only
  a line prefix: this run appended a startup after an earlier truncated line.
- The runner's process count cannot distinguish a manual close from an exit.
- Deployment checks markers for transformed binaries, not byte equality, despite
  the runner's current message. A new diagnostic marker confirms this build ran.
- A2 remains the matching 32-bit RGBA upload and plain copy path; do not reopen
  the settled choice. The traced menu dynamic/staging formats are both 37.
- CPU fallback sources and registration have not been changed.
- Patch count is 67 (three diagnostic edits added). `vendor/` is untouched.
- Fresh replay of all port patches succeeds. The configured shader file contains
  repeated 0023 sampler fallback blocks: its marker is invalidated by a later
  edit, so rebuilding inserts the block again. This reproducibility defect needs
  a separate idempotence fix; it is not evidence of the display fault.
- Audio remains outside this GPU task. C measurements wait until B4 passes.
