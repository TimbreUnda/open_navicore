#!/usr/bin/env python3
"""Rasterizes the navigation design's SVG icons for LVGL.

Icons are the design's own paths, sizes and stroke widths (Uconnect Media -
Navigation, Nav component), rendered by Chromium exactly as the mockups are:

  NODE_PATH=/path/to/node_modules CHROME=/path/to/chrome-headless-shell \\
      python3 tools/ui_assets/make_icons.py

Monochrome icons become A8 masks (LVGL recolors them); the vehicle marker is
ARGB8888. Output: backends/osmand/ui/assets/nav_icons.{c,h}.
"""
import os
import subprocess
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'backends', 'osmand', 'ui', 'assets')

UI = {
    'search': 'M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14M20 20l-4-4',
    'clock': 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18M12 7v5l3 2',
    'back': 'm15 5-7 7 7 7',
    'plus': 'M12 5v14M5 12h14',
    'minus': 'M5 12h14',
    'gps': 'M12 2v3M12 19v3M2 12h3M19 12h3M12 6a6 6 0 1 0 0 12 6 6 0 0 0 0-12M12 10a2 2 0 1 0 0 4 2 2 0 0 0 0-4',
    'sun': 'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.4 1.4M17.6 17.6 19 19M5 19l1.4-1.4M17.6 6.4 19 5',
    'moon': 'M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z',
    'overview': 'M4 4h6v6H4zM14 14h6v6h-6zM10 7h4a3 3 0 0 1 3 3v4',
    'route': 'M6 19a2 2 0 1 0 0-4 2 2 0 0 0 0 4M18 9a2 2 0 1 0 0-4 2 2 0 0 0 0 4M6 15V9a4 4 0 0 1 4-4h2M18 9v6a4 4 0 0 1-4 4h-2',
    'lock': 'M6 11h12v10H6zM8 11V7a4 4 0 0 1 8 0v4',
    'pin': 'M12 22s7-6.5 7-12a7 7 0 0 0-14 0c0 5.5 7 12 7 12zM12 7a3 3 0 1 0 0 6 3 3 0 0 0 0-6',
    'fuel': 'M4 21V5a2 2 0 0 1 2-2h6a2 2 0 0 1 2 2v16M3 21h12M4 10h10M14 8l3 3v6a2 2 0 0 0 4 0V9l-3-3',
    'nores': 'M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14M20 20l-4-4M8.5 8.5l5 5M13.5 8.5l-5 5',
    'spin': 'M12 3a9 9 0 1 0 9 9',
    'alert': 'M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0zM12 9v4M12 17h.01',
}
GLYPH = {
    'straight': 'M24 42V10M13 21 24 10l11 11',
    'left': 'M30 42V25a7 7 0 0 0-7-7H11M18 11l-7 7 7 7',
    'right': 'M18 42V25a7 7 0 0 1 7-7h12M30 11l7 7-7 7',
    'slight_left': 'M28 42V28L15 15M15 25V15h10',
    'slight_right': 'M20 42V28l13-13M33 25V15H23',
    'sharp_left': 'M32 42V20a6 6 0 0 0-10-4L11 29M11 19v10h10',
    'sharp_right': 'M16 42V20a6 6 0 0 1 10-4l11 13M37 19v10H27',
    'keep_left': 'M24 42V30l-8-9V9M10 15l6-6 6 6',
    'keep_right': 'M24 42V30l8-9V9M26 15l6-6 6 6',
    'uturn': 'M32 42V20a8 8 0 0 0-16 0v12M10 26l6 6 6-6',
    'roundabout': 'M24 42V31M24 15a8 8 0 1 0 .01 0M29.6 16.4 37 9M30 9h7v7',
    'arrive': 'M14 43V7M14 8h20l-6 7 6 7H14',
}
GHOST = {'keep_left': 'M24 30l8-9V10', 'keep_right': 'M24 30l-8-9V10'}
NO_GPS = ('<path d="M9.5 8.5 12.5 5.5l5 5-3 3zM8 10 5 7 3 9l3 3M16 18l-3-3M15.5 18.5l2.5 '
          '2.5 2-2-2.5-2.5M5 15.5a3.5 3.5 0 0 0 3.5 3.5M4 18.5a2 2 0 0 0 1.5 1.5"/>'
          '<path d="M3 3l18 18" stroke-width="2.6"/>')


def stroked(paths, width, view=24):
    return ('<svg viewBox="0 0 %d %d" fill="none" stroke="#fff" stroke-width="%s" '
            'stroke-linecap="round" stroke-linejoin="round">%s</svg>' % (view, view, width, paths))


def path(d, extra=''):
    return '<path d="%s"%s/>' % (d, extra)


# name -> (size px, svg markup, A8?, y offset px within the design layout)
ICONS = {
    'search_26': (26, stroked(path(UI['search']), 2.4), True, 0),
    'clock_22': (22, stroked(path(UI['clock']), 2), True, 0.5),
    'clock_40': (40, stroked(path(UI['clock']), 1.6), True, 0),
    'back_26': (26, stroked(path(UI['back']), 2.4), True, 0),
    'plus_28': (28, stroked(path(UI['plus']), 2.6), True, 0),
    'minus_28': (28, stroked(path(UI['minus']), 2.6), True, 0),
    'heading_28': (28, '<svg viewBox="0 0 24 24"><path d="M12 3l6 16-6-3.5L6 19z" fill="#fff"/></svg>', True, 0),
    'sun_26': (26, stroked(path(UI['sun']), 2), True, 0),
    'moon_26': (26, stroked(path(UI['moon']), 2), True, 0),
    'gps_30': (30, stroked(path(UI['gps']), 2.2), True, 0),
    'overview_26': (26, stroked(path(UI['overview']), 2), True, 0),
    'route_24': (24, stroked(path(UI['route']), 2.2), True, 0),
    'no_gps_38': (38, stroked(NO_GPS, 2), True, 0),
    # Search screen.
    'back_28': (28, stroked(path(UI['back']), 2.4), True, 0),
    'search_24': (24, stroked(path(UI['search']), 2.2), True, 0),
    'lock_24': (24, stroked(path(UI['lock']), 2), True, 0),
    'lock_30': (30, stroked(path(UI['lock']), 2), True, 0),
    'pin_24': (24, stroked(path(UI['pin']), 2), True, 0),
    'fuel_24': (24, stroked(path(UI['fuel']), 2), True, 0),
}
# Place card category badges (design POI_CAT) and the arrival flag.
BADGES = {
    'fuel': UI['fuel'],
    'charging': 'M13 2 4 14h7l-1 8 9-12h-7z',
    'parking': 'M7 21V3h6a5 5 0 0 1 0 10H7',
    'hospital': 'M12 5v14M5 12h14',
    'food': 'M7 2v9M4 2v5a3 3 0 0 0 6 0V2M7 11v11M17 2c-2 2-3 5-3 8h4v12',
    'hotel': 'M3 18V8M3 14h18v4M21 18v-4a3 3 0 0 0-3-3h-7v3M7 11a1.5 1.5 0 1 0 0-.01',
    'sight': 'm12 3 2.7 5.6 6.1.9-4.4 4.3 1 6.1-5.4-2.9-5.4 2.9 1-6.1L3.2 9.5l6.1-.9z',
    'place': UI['pin'],
}
for key, d in BADGES.items():
    ICONS['badge_%s_26' % key] = (26, stroked(path(d), 2), True, 0)
# Lane arrows (design G9: 40 px, stroke 4.4, 48 viewBox). Left, straight,
# right and straight+right are the mockup's; the rest follow the glyphs.
LANES = {
    'straight': 'M24 42V10M13 21 24 10l11 11',
    'left': 'M24 42V25a7 7 0 0 0-7-7H8M15 11l-7 7 7 7',
    'right': 'M18 42V25a7 7 0 0 1 7-7h12M30 11l7 7-7 7',
    'straight_right': 'M20 42V10M9 21 20 10l11 11M20 32a8 8 0 0 1 8-8h10M33 19l5 5-5 5',
    'straight_left': 'M28 42V10M17 21 28 10l11 11M28 32a8 8 0 0 0-8-8H10M15 19l-5 5 5 5',
    'slight_left': GLYPH['slight_left'], 'slight_right': GLYPH['slight_right'],
    'sharp_left': GLYPH['sharp_left'], 'sharp_right': GLYPH['sharp_right'],
    'uturn': GLYPH['uturn'],
}
for key, d in LANES.items():
    ICONS['lane_%s_40' % key] = (40, stroked(path(d), 4.4, 48), True, 0)
ICONS['recalc_38'] = (38, stroked(path('M20 12a8 8 0 1 1-2.3-5.6M20 4v5h-5') +
                                  path('M12 8v4l2.5 1.5'), 2.2), True, 0)
ICONS['flag_38'] = (38, stroked(path('M6 21V4M6 4h11l-3 4 3 4H6'), 2.2), True, 0)
for key in ('search', 'spin', 'nores', 'route', 'alert'):
    ICONS['message_%s_32' % key] = (32, stroked(path(UI[key]), 1.8), True, 0)
# Key labels Montserrat lacks; the design shows them in the browser's
# fallback font, so they are rendered the same way (600 24px, 36 px cell).
for key, char in (('shift', '\u21e7'), ('backspace', '\u232b')):
    ICONS['key_%s_36' % key] = (36, '<div style="width:36px;text-align:center;color:#fff;'
                                'font:600 24px/29px Montserrat,system-ui,sans-serif">%s</div>' % char,
                                True, 0)
for key, d in GLYPH.items():
    ghost = path(GHOST[key], ' opacity="0.35"') if key in GHOST else ''
    ICONS['glyph_%s_72' % key] = (72, stroked(ghost + path(d), 3.6, 48), True, 0)
    ICONS['glyph_%s_32' % key] = (32, stroked(path(d), 4, 48), True, 0)
# Vehicle marker (design: map layer car), 64x64 centred, pointer up.
CAR = ('<svg viewBox="-32 -32 64 64"><circle r="30" fill="#3d7bff" opacity="0.18"/>'
       '<path d="M0 -27 L9 -14 L-9 -14 Z" fill="%s" stroke="#ffffff" stroke-width="2" '
       'stroke-linejoin="round"/><circle r="11" fill="%s" stroke="#ffffff" stroke-width="4"/></svg>')
ICONS['vehicle_64'] = (64, CAR % ('#3d7bff', '#3d7bff'), False, 0)

CELL = 80


def main():
    names = list(ICONS)
    columns = 12
    rows = (len(names) + columns - 1) // columns
    width, height = columns * CELL, rows * CELL
    cells = []
    for index, name in enumerate(names):
        size, svg, _, offset = ICONS[name]
        x, y = (index % columns) * CELL, (index // columns) * CELL
        cells.append('<div style="position:absolute;left:%dpx;top:%spx;width:%dpx;height:%dpx">'
                     '%s</div>' % (x, y + offset, size, size,
                                   svg.replace('<svg ', '<svg width="%d" height="%d" ' % (size, size), 1)))
    html = ('<!doctype html><html><body style="margin:0;background:transparent">%s</body></html>'
            % ''.join(cells))
    with tempfile.TemporaryDirectory() as workdir:
        sheet = os.path.join(workdir, 'sheet.html')
        png = os.path.join(workdir, 'sheet.png')
        open(sheet, 'w').write(html)
        subprocess.run(['node', os.path.join(os.path.dirname(__file__), 'render_sheet.js'),
                        sheet, png, str(width), str(height)], check=True)
        image = Image.open(png).convert('RGBA')

    header = ['/* Generated by tools/ui_assets/make_icons.py. Do not edit. */',
              '#pragma once', '', '#include "lvgl.h"', '',
              '#ifdef __cplusplus', 'extern "C" {', '#endif', '']
    source = ['/* Generated by tools/ui_assets/make_icons.py from the navigation design\'s',
              ' * SVG icons. Do not edit. */', '#include "nav_icons.h"', '']
    for index, name in enumerate(names):
        size, _, a8, offset = ICONS[name]
        x, y = (index % columns) * CELL, (index // columns) * CELL
        h = size + (1 if offset else 0)
        tile = image.crop((x, y, x + size, y + h))
        if a8:
            data = tile.tobytes()[3::4]
            fmt, stride = 'LV_COLOR_FORMAT_A8', size
        else:
            # ARGB8888 is stored B, G, R, A (little-endian 0xAARRGGBB).
            rgba = tile.tobytes()
            data = bytearray(len(rgba))
            data[0::4], data[1::4], data[2::4], data[3::4] = rgba[2::4], rgba[1::4], rgba[0::4], rgba[3::4]
            fmt, stride = 'LV_COLOR_FORMAT_ARGB8888', size * 4
        symbol = 'nav_icon_' + name
        rows_hex = []
        for i in range(0, len(data), 24):
            rows_hex.append('    ' + ', '.join('0x%02x' % b for b in data[i:i + 24]) + ',')
        source.append('static const uint8_t %s_map[] = {\n%s\n};' % (symbol, '\n'.join(rows_hex)))
        source.append('const lv_image_dsc_t %s = {\n'
                      '    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = %s, .w = %d, .h = %d, .stride = %d},\n'
                      '    .data_size = sizeof(%s_map),\n'
                      '    .data = %s_map,\n};\n' % (symbol, fmt, size, h, stride, symbol, symbol))
        header.append('extern const lv_image_dsc_t %s;' % symbol)
    header += ['', '#ifdef __cplusplus', '}', '#endif', '']
    open(os.path.join(OUT, 'nav_icons.h'), 'w').write('\n'.join(header))
    open(os.path.join(OUT, 'nav_icons.c'), 'w').write('\n'.join(source))
    print(len(names), 'icons')


if __name__ == '__main__':
    main()
