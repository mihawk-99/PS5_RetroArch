# Native dynamic core loading: unresolved

FCEUmm is cross-built and its metadata is discovered. It is not a working core
port yet. Evidence: `evidence/fceumm-build/`. No loader implementation or untested
crash fix is included here. The failed target gate remains open.

The native title cannot currently open the generated shared core. Both manual
runs log `Failed to open libretro core` and `Error(s): (null)`. The final ELF is
byte-identical on FTP readback, so this is not a stale upload or missing `.info`.
The later content attempt has no core path after the failed selection, tears down
the frontend, fails initialization, then reaches `vulkan_alive(NULL)` and faults
on the video-width read at address 0x80. This is a frontend lifecycle crash;
it does not establish a Vulkan driver failure or execution inside FCEUmm.

## Next implementation and acceptance

1. Add native dynamic-loader diagnostics around failed open/symbol resolution,
   retaining the core path and error status without printing user content paths.
   Establish whether a native module/export conversion or an application ELF
   loader is the supported route. The current native module converter explicitly
   rejects application exports; the websrv SDK environment is not interchangeable.
2. Fix failed-core selection/content startup so it returns to a valid menu or
   exits cleanly before polling a destroyed video context. Reproduce the current
   failure in a regression test. A null check alone must not conceal a broken
   frontend reinitialization or select a fallback video driver.
3. Test load, all required libretro symbols, API version and FCEUmm identity on
   console, then the owner's manually selected game. Capture current frontend
   identity, exact core SHA-256, retroarch.log, trace and klog. Require no crash.
4. Confirm visible gameplay, audio, controls, repeated load/unload and preserved
   menu. Saves/states are separate acceptance. No static-only core substitution
   or donor-SDK swap is implied by this build step.
