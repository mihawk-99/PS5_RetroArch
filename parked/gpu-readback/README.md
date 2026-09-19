# Bounded GPU readback diagnostic (2026-09-19)

This retired diagnostic established the fragment-input compiler defect fixed in
PS5_Vulkan. Apply `diagnostic.patch` to this repository and copy
`vulkan_capture.cpp` into `src/`, then run `tools/verify.sh` and the normal
`tools/run-title.sh --no-build --watch 30` after checking console availability.
It saves menu/target storage on frames 8 and 9 and shader/buffer/table storage on
frame 8 through the driver's existing debug API, after `vkQueueWaitIdle`.
It does not change GPU state or draw pixels on the CPU. Remove the hook and source
again after diagnosis; these captures are not a shipping feature.

Retrieve `/app0/gpu-*.bin` over the configured FTP into ignored `klog/`.
`decode-gpu-captures.py` decodes this run's 320x240 RGBA menu and 3840x2160 BGRA
swapchain storage using the driver's measured tile map. `inspect-gpu-resources.py`
is specific to the captured addresses/layout in `gpu-resources-latest-trace.txt`;
it reads copied binary files, never live GPU memory. Raw files stay ignored.

Before the driver fix, all six menu vertices had white colour and the menu texture
was correct, but no opaque-white sample rendered white. Both fragment varyings
compiled with base 0. After the fix, 909/909 samples are white and frames 8 and 9
are byte-identical. Evidence: `evidence/vulkan-fragment-inputs/` and
`../PS5_Vulkan/evidence/fragment-inputs/`. Owner confirms no flicker or triangles.
