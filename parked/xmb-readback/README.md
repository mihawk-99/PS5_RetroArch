# Retired XMB framebuffer diagnostic

The owner authorized screenshots to diagnose XMB corruption. This hook captured
swapchain frames 120 and 121 after queue idle and shader/buffer/table resources
on frame 120. It is not part of the normal title: the large synchronous writes
pause rendering and contaminate the frontend's refresh estimate.

To repeat: apply `diagnostic.patch`, copy `vulkan_capture.cpp` into `src/`, run
`tools/verify.sh`, then `tools/run-title.sh --no-build --watch 45` on an idle
console. Retrieve `gpu-target-120.bin` and `gpu-target-121.bin` from the title's
FTP directory into ignored `klog/`. The captured resource filenames and byte
counts are listed in that launch's trace. XMB has no RGUI menu image, so the
zero-byte menu capture is expected. Decode the targets with
`python3 parked/xmb-readback/decode-targets.py <capture-directory>`.

Remove the hook/source again after diagnosis. Refresh the generated
`build/ra-conf/gfx/drivers/vulkan.c` from its pristine vendor counterpart, then
reapply `tools/apply-port-patches.py build/ra-conf` before rebuilding: the
patcher adds named edits but does not undo a retired edit automatically.
The driver debug API only reads
storage; the rendered image comes from Vulkan's swapchain, not a CPU renderer.
The original captures remain in ignored `klog/xmb-capture/` and the corrected
ones in `klog/xmb-textures-capture/`. Evidence is in `evidence/xmb-default/`.
