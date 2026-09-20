# Thumbnail colours and configuration reset

The owner reported swapped save-state thumbnail colours and loss of input after
Reset to Defaults, then confirmed the uploaded fixes with “Perfect. Commit”.
Build, deployment and acceptance evidence: `evidence/thumbnail-input-defaults/`.
Uploads are authorized; do not launch or interrupt a title without owner intervention.

## Findings and changes

- The asynchronous image task receives `supports_rgba` but initializes its texture
  with false. Its completion conversion consequently leaves ARGB words unchanged,
  while this port's Vulkan texture uploader consumes RGBA bytes. Patch 0081 passes
  the requested flag into the image. PNG files/core framebuffer formats and the
  synchronous asset loader are unchanged. Existing correctly saved thumbnails
  should display correctly when loaded again; no file rewriting is needed.
- Compiled input defaults already select ps5, but joypad defaults select null.
  Patch 0082 selects ps5 for the joypad default as well.
- Reset to Defaults clears auto-binds and device identity without restarting input.
  The driver remembers its connected pad and would not announce it again. The reset
  command now marks that announcement stale; the next poll re-requests the built-in
  controller profile once, even with no fresh native pad sample. This avoids a full
  driver teardown or per-frame autoconfiguration tasks.
- No PS5_Vulkan edits or logging changes. Build against the previously accepted
  four driver archives in build/xmb-normal-vulkan through the existing override.
  No new dependency, toolchain option or source pin is introduced.

## Host checks

`python3 -m unittest tests.test_thumbnail_defaults tests.test_input_ps5 -v`
executes the real task initializer, upload callback and pixel conversion with
stubbed file decode/scheduling. Known red, blue and partially transparent pixels
must match both RGBA and the existing non-RGBA path. It executes the compiled
default selectors and the Reset to Defaults command arm. Native input mocks verify
one reannouncement with no reopen, including no new sample and no active driver.

All three new regressions were observed failing before the fixes: wrong red pixel,
null joypad default, and absent binding refresh. All four focused tests now pass.
Patch inventory grows from 176 to 179 edits, including a fifth configuration.c
edit for the joypad default. The first full run stopped on the old four-edit
configuration inventory assertion; that expected count now includes the added
default. All 74 host tests pass. Full verification command:

```
PS5_MEMORY_DIAGNOSTICS=0 PS5_VULKAN_DIR="$PWD/build/xmb-normal-vulkan" bash tools/verify.sh
```

## Regression checklist

Preserve old logs, live config, and the exact ELF/map/build identity before upload.
Check console idle, upload the verified eboot only, read back runtime identity and
confirm live config unchanged. Start passive klog before the owner launches.

1. Load a game with recognizable red/blue colours; save a state and inspect its
   thumbnail. Revisit an existing thumbnail and confirm both match gameplay.
2. Return to XMB; use Configuration File -> Reset to Defaults. Immediately check
   buttons and left-stick navigation, then input binding capture.
3. Save the reset defaults and restart manually. Confirm native input still works.
4. Confirm gameplay and Close Content/menu colours remain correct.

Collect retroarch.log/trace/klog for each run and match the uploaded identity.
No console success is claimed by the host tests; no launch is authorized by this plan.

## Verified build and upload

All five gates passed: format, 74 host tests, build (278 frontend sources),
integration (74 tests and manifest) and 30 replayed evidence captures. Normal
identity: `e11018a79a62ce91fa723deaad6939c1e48a47bace21b3527ae84f4452148451`.
Exact ELF/map/title/manifest/logs: klog/thumbnail-input-build/; host-report.json
in `evidence/thumbnail-input-defaults/` records their hashes. The same four driver archives and all three Mesa
utility objects match the previously accepted normal XMB build. The expensive
memory observer is absent and the reset hook is linked into the frontend/title.

Upload: klog/thumbnail-input-upload-20260919-205531/. Console idle checked before
upload; runtime identity read back; only eboot replaced. Live configuration is
byte-identical. Its input backend is ps5 and joypad selection is empty (automatic),
so no stale null selection needed repair. No title launch or kill was sent.
Owner confirmation accepts the reported fixes. Individual checklist results were
not separately reported, and no fresh console logs were captured after acceptance.
Existing logs/settings were preserved.
