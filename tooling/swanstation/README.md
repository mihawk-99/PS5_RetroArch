# Optional SwanStation core

Build the core with `bash tools/build-swanstation.sh`, or include it in a title
build with `PS5_WITH_SWANSTATION=1 bash tools/build-title.sh`. Beetle PSX HW remains
the default PlayStation core. No content, BIOS or user configuration is bundled.

The source and core-info inputs are pinned with SHA-256 in `sources.json`.
The first pin matches SwanStationPS5 1.3.4's upstream submodule. The core uses
Vulkan and the x64 recompiler, with LUT fastmem instead of the mmap/fault-handler
path. Its code buffer is allocated through the existing `ps5_exec_allocate` and
`ps5_exec_release` platform API. It retains the normal libretro symbol names;
the static application's symbol-renaming step is not used here.

`native.patch` and the xxhash/POSIX adjustments in `ps5.mk` are adapted from
AngelXex (darkxex), [SwanStationPS5 1.3.4](https://github.com/darkxex/SwanStationPS5/tree/1.3.4),
under GPL-3.0-or-later. The changes are limited to platform support; its custom
filters, embedded shaders and emulator enhancements are not included.
SwanStation is developed by the [libretro SwanStation contributors](https://github.com/libretro/swanstation)
and is licensed under GPL-3.0. Its license is staged with the optional core.

The build checks the shared-core ABI, imports and 16 KiB load segments. These
checks do not prove runtime compatibility: Vulkan context negotiation, JIT,
save loading, achievements, and repeated content load/unload must be tested on
the console. PGXP Depth Buffer remains an optional compatibility-sensitive core
setting; this port does not enable it by default.

The initial pinned build was tested on PS5 firmware 13.60 with Crash Team Racing
(USA), Vulkan, PGXP and 9x resolution. In-game save persistence and three content
launches, with unload/reload between them, passed. RetroAchievements identified
the game and initialized Hardcore successfully; a new achievement unlock was
not part of this core test. This is a limited compatibility check, not a claim
that every PlayStation game or enhancement is supported.
