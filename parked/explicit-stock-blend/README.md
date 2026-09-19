# Explicit opaque blend diagnostic (2026-09-19)

Equivalent ONE/ZERO ADD on the stock shader pass forced explicit blend registers
and FP16 exports without changing menu source-alpha blending. All five gates and
25 tests passed; the full 30-second console run completed and the script closed
it. The owner reported both blue flicker and black triangles unchanged. This is
not a verified fix. Raw capture: `klog/gpu-explicit-blend-run.log` (ignored).
Tested build identity: 8958ff2aee490a89b788269d647223e713483781f588b5e4b3f23452da1ac481.
To retest, apply diagnostic.patch, run tools/verify.sh and run-title.sh --watch 30,
and collect owner feedback. The driver was unchanged for this test.
