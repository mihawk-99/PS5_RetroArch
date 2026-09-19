# Active work

_Updated: 2026-09-19_

## Now

**The Vulkan menu is visible after dismissing the shell splash.** The owner
confirmed visibility, but reports buggy colours and flicker. These visual defects
are part of the current work; the overall GPU objective remains incomplete.

The objective is the user's GPU acceptance (A1-A4, B1-B4), preserving selectable
`video_ps5` and keeping `../PS5_Vulkan` read-only. The prior instruction to instrument
the sibling was not followed because it conflicts with that boundary.

## Last verified step

`src/main.cpp` now calls `sceSystemServiceHideSplashScreen()` before entering the
frontend. Previously only the CPU video driver's open method called it.

- `bash tools/verify.sh`: all five gates PASS for the splash build (21 tests).
- `bash tools/run-title.sh --no-build --watch 90`: uploaded and launched; the
  splash call returned 0. The owner saw RGUI with colour corruption and flicker.
- Retrieved new startup: 471 lines, zero refusal lines. The first four observed
  end/submit/present/per-image results all returned VK_SUCCESS. A complete
  90-second run is still unverified: final process count was 0, because the owner
  manually closed it (confirmed). The owner describes blue colours and black
  triangles appearing and disappearing. No fatal signal was captured.
- Evidence: `evidence/vulkan-splash-dismissal/`. Earlier API diagnostic evidence:
  `evidence/vulkan-api-results-splash/`. Raw console data stays in ignored `klog/`.
- Tested splash eboot SHA256:
  `917611986e416f5eaac5aad821b4c5c31b81a534e4795fa37989439f001e9ec7`.
- The linked ELF defines video_vulkan, video_ps5, vkGetInstanceProcAddr,
  ps5vk_CmdDraw and ps5vk_QueuePresentKHR; title.map sources the driver from the
  static archive. Sibling source/archive remain unchanged from the diagnostic.

## Current diagnosis

The owner now clarifies that the menu is green, with blue flickers everywhere and
black triangles appearing/disappearing in fixed positions. The RGBA4444 conversion
correction is exhaustive over all 65,536 inputs. The 30-second colour run completed
and the script closed it, with zero refusals or recorded API errors. Evidence:
`evidence/vulkan-menu-rgba/`. Flicker remains unresolved.

The identity build proves the console runs the intended input set:
`df3a593a85388d58bc9fcc847dbd1deac6d9f1de8e4170493e84a84f87e362e3`.
`evidence/vulkan-build-identity/` records local/remote/trace agreement. All five
gates and 25 tests passed. Generic startup markers alone cannot prove a build.

Read-only driver inspection found a relevant historical blue-channel corruption:
`../PS5_Vulkan/docs/HARDWARE_FINDINGS.md` records source-alpha blending needing
FP16_ABGR pixel exports. The current driver chooses that for blend-enabled
pipelines, while opaque pipelines use 32_ABGR and omit explicit blend registers.
This is a hypothesis for mixed opaque/blended draws, not a proven root cause.

## Next smallest step

Test a frontend-only, equivalent ONE/ZERO blend state for the opaque stock shader
pass, to force explicit blend programming and FP16 exports. Compare the same menu
on the same unchanged driver; keep source-alpha menu blending unchanged. If that
does not resolve the defect, retire the diagnostic and report the remaining
uncertainty rather than claiming a driver fix. Public debug image readback is
another possible way to separate correct upload pixels from corrupt rendering.

## Working notes

- `bash tools/run-title.sh` owns build/deploy/launch/watch/close/capture. The shared
  console is checked before use. The user explicitly authorized uploads/runs
  without further confirmation in this session; that overrides the older
  ask-each-time rule in `docs/DEPLOYMENT.md`.
- The trace is append-only. Split on the LAST `bss check=` substring, not only
  a line prefix: this run appended a startup after an earlier truncated line.
- The runner's process count cannot distinguish a manual close from an exit.
- Deployment checks markers for transformed binaries, not byte equality, despite
  the runner's current message. A new diagnostic marker confirms this build ran.
- A2 remains the matching 32-bit RGBA upload and plain copy path; do not reopen
  the settled choice. The traced menu dynamic/staging formats are both 37.
- CPU fallback sources and registration have not been changed.
- Patch count is 68 including the RGBA conversion correction. `vendor/` is untouched.
- Fresh replay of all port patches succeeds. The configured shader file contains
  only the intended 0023 sampler fallback block after regeneration. Its marker
  now appears in its replacement, so repeated builds cannot accumulate it.
- Audio remains outside this GPU task. C measurements wait until B4 passes.
