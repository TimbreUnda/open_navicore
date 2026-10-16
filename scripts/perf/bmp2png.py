"""BMP from `vp4r-screenshot -c dm` to PNG: bmp2png.py in.bmp out.png

Needs no imaging library. The capture stores red and blue swapped (water blue
looks peach); this converter keeps the stored channels, so colours in the PNG
are swapped too. Swap them back when judging colours.
"""
import struct, sys, zlib
src, dst = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()
off = struct.unpack('<I', d[10:14])[0]
w, h = struct.unpack('<ii', d[18:26]); bpp = struct.unpack('<H', d[28:30])[0]
bpr = ((w * bpp + 31) // 32) * 4
rows = []
for y in range(abs(h)):
    r = d[off + y * bpr: off + y * bpr + w * (bpp // 8)]
    px = bytearray()
    for x in range(w):
        b, g, rr = r[x*(bpp//8)], r[x*(bpp//8)+1], r[x*(bpp//8)+2]
        px += bytes((rr, g, b))
    rows.append(b'\x00' + bytes(px))
if h > 0: rows.reverse()
raw = b''.join(rows)
def chunk(t, c): return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
open(dst, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, abs(h), 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
print('ok', w, h, bpp)
