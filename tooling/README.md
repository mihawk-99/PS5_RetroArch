# Native host tooling

All repository-owned format tooling is C or C++. Make/Bash and PowerShell are
thin platform-specific orchestrators. No C# or .NET runtime is required.

The root build performs four native stages:

1. `prospero-clang18` compiles application sources and `native/app_crt.cpp`.
2. LLVM lld resolves objects, static archives, public SDK import stubs,
   constructors, TLS, and unwind information into an intermediate PIE.
3. `native/ps5-native-tool` converts that PIE to the PS5 ELF layout and wraps
   it in a deterministic development FSELF.
4. The requested folder or optional filesystem image is assembled.

`native/libc_builder.cpp` independently reproduces the generated clean-room
runtime from the two manifests under `native/runtime/`. Run `make libc` on
Linux/WSL or `../tools/rebuild-libc.ps1` on Windows to prove both hashes.

`make deps` fetches the pinned public payload SDK and static zlib archive into
`.deps/native/`; neither is installed globally. The shell and PowerShell
bootstrappers share the same cache layout.

See [`../docs/NATIVE_TOOLING.md`](../docs/NATIVE_TOOLING.md) for the format
boundary and low-level commands.
