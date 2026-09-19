# XMB horizontal-navigation allocation investigation

## Scope

The owner reproduced the crash by holding left/right between XMB tabs. This
investigation uses the existing console capture and the exact configured source;
it does not launch a title, change allocation policy, or modify PS5_Vulkan.
There is no accepted crash fix yet.

## What the capture establishes

See `evidence/xmb-memory-crash/` for the symbolized console failure. Between
5,004 ms and the first failure at 6,429 ms, tracked live native requests grow
from 4,625,679 to 11,616,951 bytes: a 6,991,272-byte increase. The failing
15,488-byte allocation belongs to `xmb_list_insert`, through `xmb_alloc_node`.
The following three logged failures are the same size/caller, with successful
648-byte callback allocations between them. XMB returns from the failed node
allocation without stopping the surrounding entry-building loop.

The last caller breakdown predates the rise. A failed allocation's caller is
not necessarily the owner of the memory already occupying the heap. Therefore
the capture does **not** establish that XMB nodes account for that entire rise,
nor identify the destination tab or its list length. Native accounting excludes
allocations internal to system libraries. Neither total PS5 memory exhaustion
nor the native heap's exact limit, fragmentation or corruption is established.

The later fatal fault is separate: texture unloading enters `vkQueueWaitIdle`,
sync allocation fails, and Mesa's allocation-error logger dereferences NULL.
Image creations minus destructions stay at 131; that rules out growth in the
observed live-image count over these snapshots, not driver-internal leaks or
growth in bytes per image. The allocation-log flood was already fixed before
this capture.

## Why tab switching allocates

1. `xmb_list_cache` optionally copies the visible outgoing entries into one
   animation list. `xmb_list_deep_copy` frees that list's previous nodes and
   callbacks before replacing it. It does not keep a queue of old lists.
2. `menu_driver_deferred_push_content_list` dispatches the destination tab to
   its display-list builder. The inspected main/settings/history/favourites,
   contentless-core, playlist-directory and horizontal-playlist cases clear the
   previous selection list before repopulating it.
3. `menu_entries_append` allocates an XMB node and a callback object per entry.
   Ordinary nodes contain an embedded thumbnail-path structure even when the
   entry has no thumbnail. The x86-64 layout matches the console request:

   | Component | Bytes |
   | --- | ---: |
   | Seven path arrays and four name arrays | 15,360 |
   | Complete thumbnail-path structure | 15,400 |
   | Complete XMB node | 15,488 |
   | Menu callback object | 648 |
   | Node + callback per entry, excluding strings/list storage | 16,136 |

4. These requests are below the native wrapper's 1 MiB mapping threshold.
   Repeated medium-sized list allocations therefore remain on the native heap,
   unlike large core buffers. Five hundred entries need 8,068,000 requested
   bytes for nodes and callbacks alone. **500 is an illustrative size, not the
   measured length of the owner's tab.**
5. Held-button tab repeats have a 99,000-microsecond delay when scroll
   acceleration is active. Each accepted switch can repeat the work above.
   Speed increases allocation turnover; this alone does not prove retained
   memory growth.

## Wallpaper and error-path work

`xmb_update_dynamic_wallpaper` calls `xmb_load_image(..., MENU_IMAGE_NONE)`
when a changed candidate wallpaper is invalid. It then sets `bg_file_path` to
NULL, so a nonempty missing candidate is not remembered as handled. The next
update can take the same branch. `xmb_load_image` destroys the background and
white fallback texture, then creates the white texture again. This happens
even when there was already no background image to replace.

The captured configuration enables dynamic wallpapers. The crash stack reaches
this wallpaper-update/white-texture-unload path; the capture does not include
the resolved wallpaper candidate. Missing-wallpaper churn is thus a confirmed
source behavior and a plausible repeat trigger, not a measured explanation for
the 6,991,272-byte rise. It explains why changing tabs can enter a GPU queue
wait while the native allocator is already failing.

## Local experiment and its limits

`tools/probe-xmb-allocations.py` extracts the actual node allocation/copy/free,
list copying/clearing and file-list function bodies from `build/ra-conf`, and
uses the real structure headers. It compiles a host harness with AddressSanitizer
and UndefinedBehaviorSanitizer. GPU/animation boundaries are inert and nodes
have no textures; list construction and its chosen lengths are synthetic.
This is not a complete frontend, timing, task-queue, native-heap or GPU test.

The experiment alternates list lengths 12, 20, 1, 500 and 6, copying up to eight
visible entries each time. Across 10,000 replacements / 4,566,034 allocation
calls, end-of-cycle live requested bytes stay at 259,288, peak at 8,236,400, and
return to zero after cleanup. The exercised ordinary-node lifecycle therefore
does not accumulate old lists. Native fragmentation remains outside this test.

A separate ownership check finds that `xmb_free_node` does not release its
`console_name` string. This is a real small leak for named nodes; the ordinary
selection nodes in this experiment have NULL names. It does not establish the
cause of the console spike. LeakSanitizer cannot run under the desktop sandbox's
ptrace; explicit allocation accounting verifies cleanup instead. The first
attempt failed on that tooling limitation; rerunning with only leak scanning
disabled passed both other sanitizers and all accounting assertions.

Replay the committed result after preparing the configured source:

```sh
python3 tools/probe-xmb-allocations.py --expect evidence/xmb-allocation-investigation/host-probe.json
bash tools/verify.sh format evidence
```

## Next discriminating measurement

Before changing allocator policy, record the top live allocation callers once
on the **first** failure, plus bounded numeric tab/list counters around clear
and populate: destination kind, list size, live node count, copied node count,
and native bytes before/after. Avoid entry names, content paths and per-entry
logging. This distinguishes a large destination list, retained nodes, other
allocation owners, and failure despite stable live requests. It needs another
owner-authorized console run; no new diagnostic binary was built in this step.

Candidate fixes to evaluate after attribution are lazy/shared thumbnail-path
storage for XMB entries, eliminating redundant white-texture recreation, and
proper allocation-failure handling in the frontend and driver logger. Merely
slowing navigation or increasing the heap would not establish the root cause.

Evidence recorded 2026-09-19 on `codex/xmb-allocation-diagnostics`.
