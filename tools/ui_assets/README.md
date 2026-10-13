# Navigation UI assets

Fonts and icons for the LVGL navigation UI, generated from the Uconnect
navigation design (`Uconnect Media — Navigation.html`, Nav component). The
generated C files live in `backends/osmand/ui/assets/` and are committed, so
a normal build needs none of these tools.

| Script | Output | Needs |
|---|---|---|
| `make_fonts.py LV_FONT_CONV_JS` | `nav_font_<size>_<weight>.c` | fontTools, brotli, node, lv_font_conv 1.5 |
| `make_icons.py` | `nav_icons.{c,h}` | Pillow, node + playwright-core (`NODE_PATH`), Chromium (`CHROME`) |
| `compare.py RENDER DESIGN OUT` | per-state diff images | numpy, Pillow |

`compare.py` checks the implementation against the mockups: build
`make ui-render-tool`, run it into a directory, and compare with screenshots
of the design boards (`<state>_full.png`, and `<state>_map.png` with the UI
hidden) for `free`, `preview` and `guidance`.

## Fonts

`fonts/Montserrat-{latin,cyrillic}.woff2` are the variable Montserrat subsets
the design loads from Google Fonts. Montserrat is Copyright 2011 The
Montserrat Project Authors (https://github.com/JulietaUla/Montserrat) and is
licensed under the SIL Open Font License 1.1
(https://openfontlicense.org). The generated bitmap fonts are derived works
under the same licence.
