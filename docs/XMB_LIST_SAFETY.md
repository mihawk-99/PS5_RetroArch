# XMB list allocation safety

The owner confirmed crash-free navigation with the diagnostic build and then
confirmed the normal build eliminated the five-second hitches. Current logging
is preserved: RetroArch.log, trace and passive klog remain available, while the
expensive memory observer is opt-in. Evidence: `evidence/xmb-safe-list-run/`.

## Changes and rationale

- Patch 0080 changes XMB's inline 15,400-byte thumbnail-path structure into an
  optional pointer. Ordinary nodes shrink from 15,488 to 96 bytes. Paths are
  initialized on demand for visible icon thumbnails, released outside the visible
  range, and independently owned by animation copies. Node destruction also frees
  console_name. Image tasks copy their path; they do not retain the path structure.
- `ps5_menu_malloc` allocates XMB nodes and menu callback objects in 64 KiB anonymous
  mapped slabs, with 128- and 1,024-byte payload classes. A locked bitmap allocates
  slots; the last free unlinks and unmaps the slab. Native pointers are identified
  without reading their allocation headers and still return to libc. Ordinary
  `free` and `realloc` dispatch by ownership; failure preserves the old allocation.
  This avoids one mmap per object and leaves native heap room for strings, list
  arrays, playlist parsing, system libraries and the GPU driver. Allocations above
  1,024 bytes are rejected by this narrow API. The existing general 1 MiB threshold
  is unchanged. Diagnostic mapped counts now include requested slab-object bytes,
  excluding slab rounding, headers and size-class padding.
- File-list append/insert check growth and string allocations before publishing an
  entry. Menu append/prepend allocate callbacks first and reject missing XMB nodes,
  rolling back the newly inserted item. A per-list failure flag suppresses repeated
  attempts until clear. The playlist loop stops at the first rejected allocation.
  Existing complete entries remain valid; no incomplete callback/node is published.
- Animation copies check capacity, strings, node ownership and callback allocations.
  Failure discards the optional copy, preserves the source, and leaves an empty
  destination. Empty-list icon loops are bounded. Container allocations initialize
  the new flag and are checked. The patch set grows from 150 to 176 edits.
- The build fingerprint now includes patched headers, because file_list_t changes
  size. Every frontend consumer must rebuild against the same layout. No new SDK,
  dependency, compiler flag or version pin is introduced. PS5_Vulkan is unchanged.

## Host verification

Reproduction commands:

```sh
python3 -m unittest tests.test_menu_memory tests.test_xmb_safe_lists -v
PS5_MEMORY_DIAGNOSTICS=1 bash tools/verify.sh
```

The new tests compile and execute actual patched list/node/menu functions and the
native memory wrapper with AddressSanitizer and UndefinedBehaviorSanitizer. They
exercise 7,384 synthetic entries, every allocation position in append/prepend and
animation copy, failure retry suppression, non-LIFO release, realloc failure,
foreign allocations, concurrent use, and complete mapping/ownership cleanup.
LeakSanitizer is disabled for the sandbox; explicit live-byte/mapping accounting
checks leaks. GPU work, callback binding and menu geometry are stubbed in the
source-extracted test: host success does not establish native navigation/rendering.

The first full gate attempt failed only on the deliberately pinned patch count
(150); it was updated to 176 to cover the 26 new edits. The final run passed all five gates and 71 tests; 278/278 frontend sources
compiled. `evidence/xmb-safe-list-run/diagnostic-build.json` records the exact candidate and source hashes.
The diagnostic image was uploaded with runtime identity readback and unchanged
live config; the owner subsequently ran it manually. No launch was sent by the agent. Exact symbols and logs are in `klog/xmb-safe-lists/`.

## Future console regression checks

Preserve the current console logs and configuration before deployment. Save the
matching ELF, map, build identity, staged manifest and linked archive hashes in
ignored klog/ before another build. Check the console is idle before upload; verify
the uploaded runtime identity. Do not replace live configuration or user playlists.

After the owner authorizes launch, start klog before launch and keep collecting
memory-diagnostics.log, retroarch.log and trace.txt. Ask the owner to enter the same
7,384-entry custom playlist, navigate left/right naturally and rapidly, scroll its
entries, leave/revisit it several times, and verify icons/text/input remain correct.
Also check a small built-in tab, RGUI/XMB transition, and a game/menu transition.
Record whether the owner closes the title or it exits unexpectedly.

Pass requires no allocation failures or dropped diagnostic records, no Vulkan
refusals/API errors/fatal kernel signals, responsive clean navigation confirmed by
the owner, and node/callback bytes attributed to the mapped route. Native usage
must no longer show the old per-entry node/callback burst. Compare repeated returns
to a small tab for retained growth; inspect differences rather than assuming every
cache returns to an identical count. Keep the acceptance evidence with each verified build.

The unsafe PS5_Vulkan logger under genuine allocation failure is a separate known
issue. This frontend fix does not fix that driver code or guarantee recovery
from arbitrary process-wide exhaustion; it prevents this list workload from consuming
its native heap and makes the covered list construction/copy failures safe.

## Five-second stutter comparison

The diagnostic calls ps5::memory::tick before presentation. Every five seconds
it holds the observer mutex, scans 131,072 tracking slots, aggregates callers and
synchronously writes summary/caller rows. This matches the reported hitch cadence.
The owner confirmed the normal build fixed it; exact stall duration was not measured.

```sh
PS5_MEMORY_DIAGNOSTICS=0 PS5_VULKAN_DIR="$PWD/build/xmb-normal-vulkan" bash tools/verify.sh
```

All five gates passed, including 71 tests and compilation of 278 frontend sources.
The ignored mirror uses the same four archived driver libraries as the successful
diagnostic through the existing PS5_VULKAN_DIR override. Mesa utility object
changes were confined to debug compilation-directory information; stripped object
bytes match. No sibling driver sources were changed. Binary inspection confirms
observer symbols/hooks are absent, while ps5_menu_malloc and native wrappers remain.
Upload identity was read back and live configuration preserved. No launch or kill
was issued by the agent. The owner subsequently reported “That fixed it” and
requested committing on main with existing logging. No new normal-run logs were
captured; the normal acceptance is the owner's observation, not a measured FPS.
Exact identities, hashes, commands and scope are in diagnostic-build.json and
normal-build.json under `evidence/xmb-safe-list-run/`.
