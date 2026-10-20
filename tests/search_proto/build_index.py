#!/usr/bin/env python3
"""Builds the search index of option B from osmand_address_dump_smoke TSV.

    build_index.py addresses.tsv out.idx

Normalisation mirrors address_query.h (fold_word, name_tokens). Layout,
little endian, all sections 4-byte aligned:

  header   magic "ONSX0001", 9 counts, 9 section offsets (u32)
  files    name_off u32, name_len u32, min/max lat/lon i32 (1e-6 deg)   24 B
  cities   file u16, subtype i16, title_off u32, title_len u16, tok_len u16,
           tok_off u32, lat i32, lon i32, street_first u32, street_count u32  32 B
  streets  city u32, title_off u32, title_len u16, tok_len u16, tok_off u32,
           lat i32, lon i32, house_first u32, house_count u32            32 B
  houses   number_off u32, number_len u32, lat i32, lon i32              16 B
  city_tokens / street_tokens   tok_off u32, tok_len u32, first u32, count u32
  city_postings / street_postings   u32 ids
  pool     UTF-8 strings
"""
import struct
import sys

LATIN = {'a': 0x430, 'c': 0x441, 'e': 0x435, 'i': 0x438, 'o': 0x43E, 'p': 0x440,
         'x': 0x445, 'y': 0x443, 'k': 0x43A, 'm': 0x43C, 't': 0x442}
DROP = {0x44C, 0x44A, ord("'"), ord('`'), 0x2019, 0x2018, 0x2BC, 0x2032}
SEPARATORS = {' ', '\t', ',', ';', '.', '-', '/', '(', ')', '"', '«', '»',
              '–', '—', ':', '№'}


def fold_cp(cp):
    if ord('A') <= cp <= ord('Z'):
        cp += 0x20
    elif 0x410 <= cp <= 0x42F:
        cp += 0x20
    elif 0x400 <= cp <= 0x40F:
        cp += 0x50
    elif cp == 0x490:
        cp = 0x491
    if cp in (0x456, 0x457, 0x44B):
        return 0x438
    if cp in (0x454, 0x44D, 0x451):
        return 0x435
    if cp == 0x491:
        return 0x433
    if cp in DROP:
        return 0
    return cp


def fold_word(word):
    cps = [c for c in (fold_cp(ord(ch)) for ch in word) if c]
    cyrillic = any(0x400 <= c <= 0x4FF for c in cps)
    if cyrillic:
        cps = [LATIN.get(chr(c), c) if c < 0x80 else c for c in cps]
    return ''.join(map(chr, cps))


def split(text):
    out, current = [], []
    for ch in text:
        if ch in SEPARATORS:
            if current:
                out.append(''.join(current))
            current = []
        else:
            current.append(ch)
    if current:
        out.append(''.join(current))
    return out


STOPWORDS = {fold_word(w) for w in (
    "вул вулиця вулица ул улица просп проспект пр пров провулок переулок пл площа "
    "площадь бульв бульвар б р н шосе узвіз набережна тупик проїзд житловий масив жм "
    "ж мкр мікрорайон область обл район рн місто м село с смт селище україна").split()}


def name_tokens(name):
    return [f for f in (fold_word(p) for p in split(name)) if f and f not in STOPWORDS]


def deg(value):
    return int(round(float(value) * 1e6))


class Pool:
    def __init__(self):
        self.data = bytearray()
        self.seen = {}

    def add(self, text):
        raw = text.encode('utf-8')
        if raw not in self.seen:
            self.seen[raw] = len(self.data)
            self.data += raw
        return self.seen[raw], len(raw)


def main():
    source, target = sys.argv[1], sys.argv[2]
    files, cities, streets, houses = [], {}, {}, {}
    with open(source, encoding='utf-8') as tsv:
        for line in tsv:
            f = line.rstrip('\n').split('\t')
            if f[0] == 'F':
                files.append(f[2].rsplit('/', 1)[-1])
            elif f[0] == 'C':
                cities[int(f[1])] = (int(f[2]) - 1, int(f[3]), f[5] or f[4], deg(f[6]), deg(f[7]))
            elif f[0] == 'S':
                streets[int(f[1])] = (int(f[2]), f[4] or f[3], deg(f[5]), deg(f[6]))
            elif f[0] == 'B':
                houses.setdefault(int(f[1]), []).append((f[2], deg(f[3]), deg(f[4])))

    pool = Pool()
    city_ids = sorted(cities)
    city_index = {cid: i for i, cid in enumerate(city_ids)}
    by_city = {}
    for sid, s in streets.items():
        if s[0] in city_index:
            by_city.setdefault(s[0], []).append(sid)

    bbox = [[90e6, 180e6, -90e6, -180e6] for _ in files]
    city_rows, street_rows, house_rows = bytearray(), bytearray(), bytearray()
    city_tokens, street_tokens = {}, {}
    street_count = 0
    for i, cid in enumerate(city_ids):
        file, subtype, title, lat, lon = cities[cid]
        box = bbox[file]
        box[0], box[1] = min(box[0], lat), min(box[1], lon)
        box[2], box[3] = max(box[2], lat), max(box[3], lon)
        tokens = name_tokens(title)
        for t in set(tokens):
            city_tokens.setdefault(t, []).append(i)
        t_off, t_len = pool.add(title)
        k_off, k_len = pool.add(' '.join(tokens))
        sids = by_city.get(cid, [])
        city_rows += struct.pack('<HhIHHIiiII', file, subtype, t_off, t_len, k_len, k_off,
                                 lat, lon, street_count, len(sids))
        for sid in sids:
            _, s_title, s_lat, s_lon = streets[sid]
            s_tokens = name_tokens(s_title)
            for t in set(s_tokens):
                street_tokens.setdefault(t, []).append(street_count)
            t_off, t_len = pool.add(s_title)
            k_off, k_len = pool.add(' '.join(s_tokens))
            own = houses.get(sid, [])
            street_rows += struct.pack('<IIHHIiiII', i, t_off, t_len, k_len, k_off, s_lat, s_lon,
                                       len(house_rows) // 16, len(own))
            for number, h_lat, h_lon in own:
                n_off, n_len = pool.add(number)
                house_rows += struct.pack('<IIii', n_off, n_len, h_lat, h_lon)
            street_count += 1

    file_rows = bytearray()
    for name, box in zip(files, bbox):
        n_off, n_len = pool.add(name)
        file_rows += struct.pack('<IIiiii', n_off, n_len, int(box[0]), int(box[1]),
                                 int(box[2]), int(box[3]))

    def token_section(tokens):
        rows, postings = bytearray(), bytearray()
        for token in sorted(tokens, key=lambda t: t.encode('utf-8')):
            ids = tokens[token]
            t_off, t_len = pool.add(token)
            rows += struct.pack('<IIII', t_off, t_len, len(postings) // 4, len(ids))
            postings += struct.pack('<%dI' % len(ids), *ids)
        return rows, postings

    ct_rows, ct_post = token_section(city_tokens)
    st_rows, st_post = token_section(street_tokens)
    sections = [file_rows, city_rows, street_rows, house_rows, ct_rows, st_rows,
                ct_post, st_post, pool.data]
    counts = [len(files), len(city_ids), street_count, len(house_rows) // 16,
              len(city_tokens), len(street_tokens), len(ct_post) // 4, len(st_post) // 4,
              len(pool.data)]
    offset = 8 + 4 * 18
    offsets = []
    for s in sections:
        offsets.append(offset)
        offset += (len(s) + 3) & ~3
    with open(target, 'wb') as out:
        out.write(b'ONSX0001' + struct.pack('<18I', *counts, *offsets))
        for s in sections:
            out.write(s + b'\0' * (((len(s) + 3) & ~3) - len(s)))
    print('files=%d cities=%d streets=%d houses=%d city_tokens=%d street_tokens=%d '
          'pool=%d bytes=%d' % (*counts[:6], counts[8], offset))


if __name__ == '__main__':
    main()
