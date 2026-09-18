# LVGL Montserrat bitmap fonts

These files are deterministically generated from LVGL's built-in, uncompressed 4-bpp Montserrat Medium C fonts. Each single-page, 32-bit, top-origin TGA declares 8 alpha bits and preserves the original 16 alpha levels; the BMFont XML preserves LVGL's line metrics, offsets, integer advances, and expanded class kerning for ASCII U+0020-U+007E, degree U+00B0, and bullet U+2022.

Regenerate from the repository root with `python tools/generate_lvgl_bitmap_fonts.py`; verify without writing with `python tools/generate_lvgl_bitmap_fonts.py --check`. Font Awesome PUA glyphs present in the LVGL source are intentionally excluded.

Montserrat is Copyright 2011 The Montserrat Project Authors and is distributed under the SIL Open Font License 1.1; see `OFL.txt`. Generated `.fnt` files use the RmlUi `Samples/basic/bitmap_font` XML schema.
