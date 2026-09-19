# Active work

_Updated: 2026-09-19_

## Now

**The requested GPU menu goal is verified.** RetroArch renders RGUI through
`video_vulkan` and statically linked libps5vk; the full 30-second run has zero
refusals and no recorded command-end, submit or present errors. The owner confirms:
"Colours correct; no flicker or triangles". `video_ps5` remains registered and
selectable. Performance/core measurements are outside this completed request.

The user initially made `../PS5_Vulkan` read-only, then explicitly authorized
necessary driver fixes with documentation and a commit there. Its compiler fix
is committed as **6f0ce0d**. No SDK source was modified.

## Verified cause and fix

RGUI's upload conversion was corrected separately in RetroArch **947134d**:
RGBA4444 expands into matching full-range RGBA8888 staging/dynamic textures,
preserving A2's plain-copy path. All 65,536 inputs pass the host regression.

The remaining corruption was the driver's standalone compiler omitting RADV's
fragment input-location assignment before shader-info gathering. Both texture
coordinates and vertex colour read slot 0; blue/alpha therefore read undefined
components. Captured menu texels and all six white vertex colours were correct.
PS5_Vulkan now applies `ac_nir_assign_fs_input_locations` in its compiler work
copy. Dense and sparse varying-location regressions fail before and pass after.

## Evidence and validation

- `evidence/vulkan-fragment-inputs/`: full console run, build identity, zero
  refusals, initial successful API counters, owner confirmation, pixel hashes.
- `../PS5_Vulkan/evidence/fragment-inputs/`: compiler regression and driver gates.
- GPU readback: 909/909 opaque-white samples render white (previously 0/909);
  consecutive completed frames are byte-identical (previously 6,214,876 changed pixels).
- Tested identity:
  `dc4d5d48081967fb97de05b14a0b5b27258ca35836e0f08297abb7361dab8265`.
- RetroArch: all five `tools/verify.sh` gates PASS, 25 tests, both with captures
  and after removing them. Final ELF has both video drivers and no capture hook.
- PS5_Vulkan: compiler/driver rebuilt; complete `tools/check-driver.sh` PASS
  (loader/direct/PS5-link/negative arms), `make test`, `make lint`, `make` PASS.
- The 30-second fixed run completed and its script closed the title. Later,
  the console held one running title on two checks. The capture-free final
  upload was skipped; the owner-confirmed fixed diagnostic build remains installed.

## Artifacts and operating notes

Temporary image/resource captures are retired under `parked/gpu-readback/`.
The unsuccessful ONE/ZERO stock-pass diagnostic is under
`parked/explicit-stock-blend/`; it did not fix either defect. Neither diagnostic
is active in the final source/build. Patch count is 68. `vendor/` is untouched.

`tools/run-title.sh` owns deployment/launch/watch/closure/capture. The user has
authorized uploads and runs without further confirmation, but shared-console
availability is checked before upload. Do not interrupt an already-running title
just to install the capture-free build.

Trace files append across launches: split on the LAST `bss check=` substring,
which may follow a truncated old line. Generic deployment markers cannot prove
identity; use the input-derived build marker. Runner process count 0 alone cannot
distinguish a manual close from an exit. Raw console data stays in ignored `klog/`.

CPU fallback is preserved; no partial rendering-route switch is needed.
Audio and frame-rate/core measurements remain separate work, not acceptance
claims of this GPU-menu fix.
