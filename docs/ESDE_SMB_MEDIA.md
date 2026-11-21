# ES-DE: progressive SMB media (PS5)

This opt-in integration allows ES-DE to browse games already listed in RetroArch
SMB playlists and to read their media from the saved server. It adds the missing
ES-DE transport/catalog foundation as well as asynchronous image loading. It is
not a theme, an automatic network scraper, or a ROM prefetcher.

## Prerequisites and catalog

Configure the server through the existing SMB settings. Credentials remain in
`/app0/config/smb.cfg`, never in media URLs or the catalog. For consoles affected
by the blocking-socket connection freeze, the separate fix in
[PR #111](https://github.com/mihawk-99/PS5_RetroArch/pull/111) is required too.

The small, local `/app0/es-de/ES-DE/smb-catalog.xml` is a metadata index, not a
copy of games or images. Version 1 has this format (replace example paths and
sizes with actual files on the saved share):

```xml
<smbCatalog version="1" root="smb://server/share">
  <file path="smb://server/share/nes/Demo.nes" size="24592"/>
  <file path="smb://server/share/nes/images/Demo.jpg" size="12000"/>
  <media game="smb://server/share/nes/Demo.nes" kind="covers"
         path="smb://server/share/nes/images/Demo.jpg"/>
</smbCatalog>
```

List every game and media file with its positive byte size. Parent directories
are inferred. Media kinds use ES-DE subdirectory names (`covers`, `screenshots`,
`marquees`, `videos`, etc.). Each mapping must reference two listed files.
All entries must remain under `root`. Paths are literal UTF-8 SMB paths (spaces,
`#` and `%` are not URL-encoded); XML special characters must be escaped normally.
The corresponding RetroArch playlist paths must use the same SMB URLs.

This PR does not generate/import a catalog or migrate playlists. Prepare those
metadata files separately; no console/library contents are deleted or converted.
The catalog is loaded once per frontend process: restart ES-DE after updating it.
Local content scanning, existing local media lookup and user settings are retained.
An absent catalog leaves local-only installations unchanged.

## Scheduling and limits

- Image requests are deferred until rendered or explicitly prefetched by the grid.
- One CPU worker performs network reads and decoding; no GPU operations run there.
- Selected/visible images precede upcoming rows; the grid considers two rows ahead
  and one behind. Queue changes cancel obsolete queued work, not in-flight I/O.
- Decoded LRU cache: 64 MiB; at most 128 queued requests and 256 cache entries.
  Failed requests retry after 5 seconds. These are cache limits, not a bound on
  all frontend memory, decoder intermediates or GPU textures.
- At most two texture installations and one new remote GPU upload per frame.
- Images above 2048 pixels on their longest edge are resized in memory; source
  dimensions remain available to layout. Source files are never changed.
- Video previews use read-only seekable SMB AVIO, independently of image prefetch.
- Shutdown joins the worker; an in-flight read may delay exit until the existing
  transport operation finishes. This PR does not fix transport-level hangs.

No grid spacing, row height, radius, palette or animation durations are changed.
The prefetch window uses the official grid's scroll position, not custom navigation.

## Validation

Host checks (after fetching the pinned ES-DE/pugixml sources with the build tools):

```sh
python3 -m unittest discover -s tests -p test_esde_smb.py -v
python3 -m unittest discover -s tests -p test_ps5_game.py -v
python3 -m unittest discover -s tests -p 'test_*library.py' -v
```

The decoder test needs host libwebp development files (`pkg-config libwebp`);
`ESDE_HOST_WEBP_FLAGS` can supply compiler/linker flags for an alternate prefix.
Cache/decoder tests use ASan/UBSan. Fixtures are synthetic: no private catalog,
credentials, copyrighted game assets or network share is required.

The isolated PS5 executable builds and links. The scheduler/decoder came from a
working custom PS5 setup, but the integration with the unmodified official grid
still needs a console pass before this draft is ready to merge:

1. Apply #111 where needed, configure a test share and matching catalog/playlists.
2. Browse beyond the first screen, reverse direction and jump to the final row.
3. Switch systems and open/close game details; check covers, proportions and videos.
4. Test a missing image and a disconnected server, then return to local content.
5. Launch a small homebrew/test ROM and return to ES-DE; check favorites/history.

The 10–15 s to 2–3 s user observation belongs to startup PR #112, not a measured
scrolling benchmark for this PR. No universal frame-rate claim is made here.
