# XMB allocation diagnostic build

This is an opt-in diagnostic, not a fix for the XMB crash. It observes allocation
lifetimes without increasing the heap, redirecting additional allocations to
`mmap`, or suppressing Vulkan errors. PS5_Vulkan is read only for this work.

## Build and host verification

```sh
PS5_MEMORY_DIAGNOSTICS=1 bash tools/verify.sh
```

The normal build (variable absent or `0`) compiles out the observer. The diagnostic
adds `PS5_MEMORY_DIAGNOSTICS` to the title and frontend definitions and
`--wrap=posix_memalign` to the existing malloc/calloc/realloc/free wrappers because
Mesa's default Vulkan host allocator uses aligned native allocations. Mode and
archive contents participate in the runtime build identity.

The diagnostic build copies the four driver archives into
`build/memory-diagnostic-inputs/` and links those copies. `archives.json` records
their SHA-256 digests. A copy that changes during the read is rejected. This does
not rebuild or write into the sibling project; it also does not imply the copied
archives are the same driver as an earlier console run. Mesa utility sources are
still read from the configured driver source tree, and the resulting object
bytes participate in the identity.

Output is `dist/PPSA99169/`. Preserve `build/llvm-pie.elf`, `build/title.map`,
`build/title_build_identity.h`, the manifest, and the archive digests before any
subsequent build. Caller addresses must be decoded against the exact matching ELF.

Host tests exercise allocation/free accounting, mapped/native realloc migration,
failed realloc retention, foreign pointers, aligned allocation semantics,
concurrency, metadata saturation, five-second sampling with an injected clock,
logging while the wrapped allocator fails, and 10,006 repeated failures producing
only five failure records with the clock controlled by the test. Vulkan mocks check that image and
queue-idle observers preserve arguments, return values and repeat registration.
These tests do not establish console stability or identify the memory consumer.

## Manual console test — only when the owner is ready

Uploads are authorized after checking the console is idle. A launch requires
owner intervention; do not change live settings or stop another title automatically.
Start a passive klog listener before the owner's reproduction and preserve the
existing logs first. Deploy the diagnostic using the project's normal folder
procedure, preserving live configuration and user content. The owner launches it.

1. Leave XMB idle for 30 seconds.
2. Browse rapidly for 30 seconds, then leave it idle for 30 seconds. Repeat.
3. Repeat the RGUI to XMB transition that triggered the crash. Note the approximate
   elapsed time of each phase, including whether switching required restarting.
4. On a crash, leave the title closed until logs have been downloaded.

Download `/data/homebrew/PPSA99169/memory-diagnostics.log`, `retroarch.log`,
`trace.txt`, and the live configuration, along with the kernel capture. Keep raw
files under ignored `klog/`; they may contain user paths and process information.
In the title the same new log is `/app0/memory-diagnostics.log`. It appends a
`session` line with build identity and PID each launch rather than erasing the
previous run. No runner or deployment behavior is changed by this diagnostic.

## Reading the capture

`initial`, `sample`, `failure-summary` and `final` rows contain:

- `native_bytes/count/peak`: requested live bytes, live count and peak live bytes
  from observed ordinary native allocations.
- `aligned_bytes/count/peak`: the same for `posix_memalign`; this is also native
  memory, not a separate heap. Native pressure includes both native and aligned.
- `mapped_bytes/count/peak`: requests routed through the existing large-buffer
  mappings. These exclude mapping headers, page rounding and allocator overhead.
- `failures`: cumulative failures; the first four `failure` rows immediately
  record operation, requested bytes, alignment (or element count for calloc
  overflow), error and caller return address (`pc`). Further details and failure
  summaries are limited to one per five seconds, even when presentation stops.
  `failure_records` and `failure_suppressed` expose that limit. The `error` field
  is a native error return or observed errno; a failed allocator may leave stale
  errno, so a NULL result alone does not establish a particular error code. `calloc-overflow` is an arithmetic rejection,
  not measured heap exhaustion.
- `dropped`: allocation records omitted because the fixed table was full. Any
  nonzero value makes live totals incomplete for the rest of that session.
- `foreign_frees/reallocs`: pointers whose allocation was not observed (including
  records dropped on saturation). Never inspect private libc allocation headers.
- `image_create/destroy/failed`: successful frontend Vulkan image creations,
  non-null destruction calls and failed creations. These include images other
  than menu textures and exclude calls internal to the driver.
- `idle_begin/end/failed`: queue-idle calls started, returned and returned failure.
  A missing completion in a failure summary helps locate the failing operation.

Every five seconds while Vulkan presents, a summary and up to eight largest live
allocation caller groups are emitted. Groups contain `pc`, requested live bytes
and count; `site_records_omitted` reports overflow of the separate 2,048-site
aggregation table. They are not full stack traces. No frame means no periodic
sample; rate-limited allocation failures still write synchronously. Clean frontend return
writes `final`; a crash need not.

### First-failure allocation owners and XMB state

The first observed allocation failure additionally emits, once per process:

- `first-failure-caller`: up to 16 largest live caller groups **per route**,
  with `route=0` native, `1` mapped and `2` aligned. These separate rankings keep
  large mappings from hiding the native consumers. Counts/bytes are live requests,
  not cumulative allocation traffic. Groups beyond the top 16 are not printed;
  `site_records_omitted` separately indicates aggregation-table overflow.
- `first-failure-xmb`: the last cached numeric context and insertion progress,
  current node counters, and native requested bytes/count at failure.
- `first-failure-xmb-history`: the last eight boundary events, oldest first,
  including native requested bytes/count as observed at each event. Navigation
  itself only updates a fixed in-memory ring: there is no per-entry file I/O.

`tab` is the horizontal category index, `kind` the configured source's
`XMB_SYSTEM_TAB_*` enum (UINT_MAX for a custom tab). Optional compiled features
affect enum numbering; decode against the matching configured source. `current`,
`old` and `horizontal` are the latest observed selection-list, animation-list and
horizontal-list sizes. `list_size` refers to the list at the recorded operation.
`index` is the insertion offset, copied-entry count for copy phases, or selection
index for cache/populate phases. No strings, entry names or content paths are
captured. A zero phase means no XMB context has been observed yet.

Phases: 1 cache begin, 2 destination selected, 3 copy begin, 4 copy end,
5 clear begin, 6 clear end, 7 before insert, 8 populated. Clear boundaries
surround XMB node cleanup; callbacks/entry strings are freed subsequently by
the menu layer. Sizes are observations, not pointers reread from an allocation
failure callback. In particular, clear-end size can still be the old entry
count, and current/old sizes remain cached until the next context hook.
`ms` is the last stage observation's timestamp; the enclosing `failure` row
gives the actual failure time. For failure on another thread this is the last
observed frontend state, not an assertion that that thread was rebuilding XMB.

`nodes` counts successful XMB node allocations/copies minus observed node frees;
`created`, `copied` and `freed` are cumulative. `unmatched_frees` marks incomplete
node accounting. This supplements allocator ownership; it does not replace it
or classify GPU allocations. The failure path never calls frontend code or
dereferences cached frontend pointers. Existing failure rate limits still apply;
the expanded snapshot is not repeated during the failure storm.

Host coverage wraps the eight-event ring, injects insertion context, separates
same-address callers across all three routes, caps output at 48 owner rows, and
checks that 10,006 failures emit only one expanded snapshot and five ordinary
failure records (<20 KiB total in the test). It also verifies that navigation
hooks write no bytes and normal-build C macros do not evaluate their arguments.
The linked diagnostic inspection checks that the actual frontend XMB object
references all three hooks. These checks are not console acceptance.

The first console run showed why failure logging must be bounded: menu-entry
allocation retries produced 12,793 failure records and summaries (5.4 MB) before
the owner closed a frozen title. That run is not a valid performance measurement,
and rate limiting alone does not resolve the underlying allocation failures.

Logging uses stack formatting, a preopened descriptor and `write`, without stdio
or heap allocation. A fixed 131,072-entry pointer table plus state bytes uses
about 4.13 MiB of static storage. Mutexes, table operations, scans and synchronous
writes add overhead, so use this build to diagnose lifetimes, not benchmark FPS.
The allocation route and payload size remain unchanged.

Coverage is title/core/static-library references intercepted by the linker.
Allocations performed internally by system libraries, direct mappings outside the
existing large-buffer wrapper, GPU direct memory, and allocator bookkeeping are
not measured. Requested live bytes are not the console's total memory usage.

For a `pc` from this title, subtract the image load base recorded in klog (normally
`0x400000`) and one byte for a return address, then use
`llvm-addr2line -C -f -e <matching-symbols.elf> <offset>`. A fault instruction
address is decoded without the one-byte adjustment. Keep symbols locally; never
publish raw memory addresses or local paths as sanitized evidence by accident.

Compare settled idle samples from the same session. Sustained growth is a reason
to investigate retained allocations; it alone does not prove a leak. Stable
observed totals plus allocation failures leave unobserved native usage,
fragmentation, heap limits and corruption open. The crash's Mesa null dereference
while reporting an allocation failure remains unfixed by this instrumentation.
