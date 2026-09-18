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

**Fix.** `readelf -dW dist/<TITLE_ID>/retroarch.elf | grep NEEDED` must list
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
interrupted patch or a checkout that skipped `tools/fetch-upstream.sh` leaves it
half-applied. Nothing in `vendor/` is committed, so the tree on disk is the only
copy.

**Fix.** `rm -rf vendor && tools/fetch-upstream.sh`. It verifies the pinned
digest before extracting, so a re-fetch is cheap and cannot silently pick up a
different upstream. Never edit a file under `vendor/` to get unblocked: put the
change in `patches/` and re-run the fetcher, or the next fetch discards it.

## Known benign

Messages that are expected and safe, with the exact text to match. Anything not
on this list is a real failure until it is understood.

| Message | Why it is safe | Seen in |
| --- | --- | --- |
| `Fontconfig error: Cannot load default config file: No such file: (null)` | The payload starts with no font configuration file, so fontconfig falls back to its built-in defaults. Text still renders through the font this build loads directly, which is why the menu is readable. The message is printed once, at startup, before any driver line. | The Option 1 baseline run, `evidence/m1-baseline-loads/` |
| An empty list of captures under `evidence/` | The evidence gate has nothing to replay before the first console run lands. The gate prints `0 captures replayed` and passes. | `tools/verify.sh evidence` during M0 |
