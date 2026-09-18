# Troubleshooting

Symptom first, then the cause that was actually found, then the fix. Written for
the agent that hits this at 3am with no memory of the last session, so the entry
starts with the text the machine prints, not with the diagnosis.

Append new entries at the end. When a cause stops being true (a version bump, a
platform change), append a short entry that says so rather than deleting the old
one — the old entry is still what someone with an old lockfile will see.

## The title appears in the launcher, but nothing happens when it is selected

**Cause.** The ELF imports `libkernel_sys.sprx`. That library is not present in
the process the console's homebrew loader creates for a payload application, and
the loader refuses the module without printing anything the user can see. A link
against the wrong kernel stub produces this, and so does a core built by a
makefile that picked the `sys` variant on its own.

**Fix.** `readelf -dW dist/<TITLE_ID>/eboot.bin | grep NEEDED` must list
`libkernel_web.sprx` and must not list `libkernel_sys.sprx`. `prospero-clang`
links the `web` variant by default, so a `sys` entry means a flag, a makefile
variable or a prebuilt archive pulled it in: find the archive with
`prospero-nm -D` and rebuild it through `$CC`, not with a hardcoded compiler.

**Tell it apart from.** A missing `sce_sys/param.json` looks the same from the
launcher's side but the console's log shows the title directory being rejected
before any module is read.

## `check-ps5-object: not a PS5 object; the toolchain was bypassed`

**Cause.** The file was compiled by the host compiler. Prospero output and a host
Linux object are both ELF x86-64 with the System V ABI, so nothing else tells
them apart — the import table is the only reliable signal. This happens when a
makefile hardcodes `CC = gcc` or `CC = clang` in a branch that does not read the
environment.

**Fix.** Pass the toolchain as make *variables* (`CC="$CC" CXX="$CXX" AR="$AR"
RANLIB="$RANLIB"` on the command line), because a command-line variable overrides
even an unconditional assignment in the makefile. Then rebuild from clean: a
stale object from the host build survives in the same tree and will be linked
again if the timestamps say it is current.

## `error: <name>_libretro.so does not export retro_api_version`

**Cause.** The core was built with its symbols hidden or stripped from the
dynamic table. RetroArch loads cores with `dlopen` and resolves them with
`dlsym`, so a core whose entry points exist only in the static symbol table links
fine, stages fine, and fails to load on the console.

**Fix.** Build the core with `-fPIC` and without a version script or `--strip-all`
that removes dynamic symbols, then re-check with
`prospero-nm -D --defined-only <core>`. Note that `nm --defined-only` without
`-D` reports "no symbols" on a perfectly good core, because the static table is
gone; that is not a failure.

## A `make` target in `vendor/` cannot find a header the patch was supposed to add

**Cause.** `vendor/retroarch` is a patched tree, and a partial re-fetch, an
interrupted patch or a checkout that skipped `tools/fetch-retroarch.sh` leaves it
half-applied. Nothing in `vendor/` is committed, so the tree on disk is the only
copy.

**Fix.** `rm -rf vendor && tools/fetch-retroarch.sh`. It verifies the pinned
digest before extracting, so a re-fetch is cheap and cannot silently pick up a
different upstream. Never edit a file under `vendor/` to get unblocked: put the
change in `patches/` and re-run the fetcher, or the next fetch discards it.

## The title closes immediately and the log says `PRX_SCE_MODULE_LOAD_ERROR`

**Cause.** The console's loader could not load a module the application needs, and
it names the reason itself:

```text
# exception: 0xa0020102 (PRX_SCE_MODULE_LOAD_ERROR)
# === Lack of a .prx file in /app0/sce_module is detected!!! ===
# Copy the file (e.g. libc.prx) from target/sce_module.
```

`/app0` is the title's own directory as the application sees it, so this means the
file at `sce_module/libc.prx` inside the title folder is missing, misnamed, or
not a module the loader accepts. The size settles which: the signed module is
`1,284,674` bytes, and a copy that is `1,335,962` bytes is the module's raw ELF
rather than its signed container, which the loader refuses — and then reports as
absent.

**Fix.** Put the signed module in place, with that exact lower-case name:

```text
sce_module/libc.prx   1,284,674 bytes   sha256 e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036
```

`dist/<TITLE_ID>/sce_module/libc.prx` in this repository is that file, and it is
byte-identical to the sibling project's `../PS5_Vulkan/runtime/libc.prx`.

**Tell it apart from.** A null-pointer crash inside a *raw* `eboot.bin` looks
similar from the sofa — the title closes at once — but its log shows `signal: 11
(SIGSEGV)` with `fault address: 0000000000000001` and no `PRX_SCE_MODULE_LOAD_ERROR`
line. That failure is the image, not the module; see `docs/FINDINGS.md`.

## Known benign

Messages that are expected and safe, with the exact text to match. Anything not
on this list is a real failure until it is understood.

| Message | Why it is safe | Seen in |
| --- | --- | --- |
| `Fontconfig error: Cannot load default config file: No such file: (null)` | The payload starts with no font configuration file, so fontconfig falls back to its built-in defaults. Text still renders through the font this build loads directly, which is why the menu is readable. The message is printed once, at startup, before any driver line. | The Option 1 baseline run, `evidence/m1-baseline-loads/` |
| An empty list of captures under `evidence/` | The evidence gate has nothing to replay before the first console run lands. The gate prints `0 captures replayed` and passes. | `tools/verify.sh evidence` during M0 |
