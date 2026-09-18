# Controller icon assets

The five controller symbols were generated with OpenAI ImageGen as a single
high-contrast mask, then deterministically cropped, recolored, and exported as
36 x 36 uncompressed, straight-alpha TGA files for exact 1:1 PS5 rendering.

ImageGen prompt:

> Use case: ui-mockup; Asset type controller-button icon mask sprite sheet.
> Create a precise horizontal row of five isolated, crisp white controller
> symbols on pure black: Cross, Circle, Square, Triangle, Options. Use uniform
> optical weight, generous separation, centered geometry, no text, no labels,
> no glow, no shadow, no gradient, no texture, and no perspective.

`controller-imagegen-mask.png` is the generated source. The transparent PNG
files are reusable masters; the corresponding TGA files are the PS5 runtime
assets.

Runtime palette:

- Cross: `#70E1DC`
- Circle: `#FF8793`
- Square: `#D18BE5`
- Triangle: `#69D997`
- Options: `#F2F7F8`

The generated imagery is a project asset. The PlayStation name and controller
symbol conventions remain trademarks of their respective owner.

`play.svg` and `stop.svg` are deterministic project-authored playback symbols,
with matching 18 x 18 straight-alpha TGA runtime assets. They render directly
on the action button without an enclosing icon well and are not PlayStation
button symbols.

`page-up.svg` and `page-down.svg` are matching 18 x 18 paging chevrons shown
beside the available Previous page and Next page actions.

`status-online.svg` and `status-offline.svg` are project-authored 18 x 18
ring-and-core connection indicators. The matching TGA files are deterministic
exact-pixel runtime renders because the accepted PS5 texture loader supports
uncompressed TGA and intentionally does not include an SVG rasterizer.

`brand-medallion.svg`, `host-medallion.svg`, and `moon-crescent.svg` replace
the remaining RmlUi-generated circles and bordered shapes. Their exact-size
TGA renders prevent the horizontal geometry seams seen on the PS5 software
renderer while retaining editable vector masters.

`prosperolight-icon.tga` is a 56 x 56 top-origin runtime miniature generated
directly from `sce_sys/icon0.png`, so the ProsperoLight header and launcher tile
use the same artwork.
