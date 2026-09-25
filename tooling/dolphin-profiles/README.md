# Dolphin acceptance profiles

Core-option files for the Dolphin acceptance runs: each is `key=value` lines the
core reads from `/app0/dolphin-options.txt` for one test run in place of the
frontend's options (the port's test aid, `patches/dolphin/ps5-port.patch`,
`DolphinLibretro/Main.cpp`); `#` lines are ignored. The title deletes that file on
any launch that is not a test run, so a profile never outlives its run.

| File | Profile |
| --- | --- |
| `p0-native.txt` | 0: native baseline -- 1x, no MSAA, JIT64, fastmem, 100% clock, synchronous shaders, EFB copies on the GPU |
| `p1-accurate.txt` | 1: accurate baseline -- safe texture cache, scaled EFB copies, GPU texture decoding, exact depth |
| `p2-ubershaders-1x.txt`, `p2-ubershaders-6x.txt` | 2: synchronous ubershaders at 1x and 6x |
| `p3-torture.txt` | 3: 6x, ubershaders, 16x AF, forced linear filtering, every frame presented |
| `p4-gpu-efb.txt` | 4: 6x EFB torture with copies on the GPU |
| `p5-ram-efb.txt` | 5: profile 4 with EFB copies to RAM (read back through the CPU) |
| `p6-scale-Nx.txt` | 6: the scaling ladder, 1x to 6x on the accurate baseline |
| `p7-msaa-N.txt`, `p7-max-torture.txt` | 7: 2x/4x/8x MSAA on the accurate baseline, and 6x with 8x MSAA and profile 3's settings |

Each file names the options its profile is defined by, rather than relying on the
core's defaults for them.
