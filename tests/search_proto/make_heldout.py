#!/usr/bin/env python3
"""Held-out query set for the search prototypes, drawn from the address dump
with a fixed seed (neither prototype is tuned on it).

    make_heldout.py addresses.tsv > heldout.tsv
"""
import random
import sys

from build_index import STOPWORDS, fold_word, split

OBLAST = {'kharkiv': 'Харківська', 'dnipro': 'Дніпропетровська', 'kyiv_': 'Київська',
          'lviv': 'Львівська', 'odesa': 'Одеська', 'zaporizhzhya': 'Запорізька',
          'poltava': 'Полтавська', 'sumy': 'Сумська', 'chernihiv': 'Чернігівська',
          'cherkasy': 'Черкаська', 'chernivtsi': 'Чернівецька', 'zhytomyr': 'Житомирська',
          'vinnytsya': 'Вінницька', 'khmelnytskyy': 'Хмельницька', 'ternopil': 'Тернопільська',
          'rivne': 'Рівненська', 'volyn': 'Волинська', 'transcarpathia': 'Закарпатська',
          'ivano-frankivsk': 'Івано-Франківська', 'kropyvnytskyi': 'Кіровоградська',
          'mykolayiv': 'Миколаївська', 'kherson': 'Херсонська'}
CYRILLIC = 'абвгдежзиіклмнопрстуфхцчшщюя'


def spoken(street):
    """The street as a person types it: no type word, lower case."""
    words = [w for w in split(street) if fold_word(w) not in STOPWORDS]
    return ' '.join(words).lower()


def km(a, b):
    import math
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * 6371 * math.asin(math.sqrt(h))


def main():
    files, cities, streets, houses = {}, {}, {}, {}
    for line in open(sys.argv[1], encoding='utf-8'):
        f = line.rstrip('\n').split('\t')
        if f[0] == 'F':
            files[int(f[1])] = f[2]
        elif f[0] == 'C':
            cities[f[1]] = (int(f[2]), int(f[3]), f[5] or f[4], float(f[6]), float(f[7]))
        elif f[0] == 'S':
            streets[f[1]] = (f[2], f[4] or f[3])
        elif f[0] == 'B':
            houses.setdefault(f[1], []).append((f[2], float(f[3]), float(f[4])))

    # (city, typed street, number) -> house positions; keep unambiguous ones.
    by_key, city_by_name = {}, {}
    for sid, (cid, title) in streets.items():
        if cid not in cities:
            continue
        typed = spoken(title)
        if not typed or any(ch.isdigit() for ch in typed):
            continue
        for number, lat, lon in houses.get(sid, []):
            by_key.setdefault((cid, typed, number), []).append((lat, lon))
    for cid, c in cities.items():
        city_by_name.setdefault(fold_word(c[2]), []).append(cid)
    good = [k for k, pts in by_key.items() if all(km(pts[0], p) < 0.3 for p in pts)]
    good.sort()
    rng = random.Random(20261020)
    rng.shuffle(good)

    def region_key(cid):
        name = files[cities[cid][0]]
        return next((k for k in OBLAST if k in name), None)

    out = []

    def add(query, lat, lon, kind, tol, group):
        out.append('%s\t%.5f\t%.5f\t%s\t%s\t%s' % (query, lat, lon, kind, tol, group))

    towns = [k for k in good if cities[k[0]][1] in (0, 1)
             and len(city_by_name[fold_word(cities[k[0]][2])]) == 1]
    villages = [k for k in good if cities[k[0]][1] == 2 and region_key(k[0])]
    kyiv = [k for k in good if cities[k[0]][2] == 'Київ']
    for cid, typed, number in towns[:30]:
        lat, lon = by_key[(cid, typed, number)][0]
        add('%s %s %s' % (cities[cid][2].lower(), typed, number), lat, lon, 'H', 0.5, 'town')
    for cid, typed, number in villages[:15]:
        lat, lon = by_key[(cid, typed, number)][0]
        add('%s %s %s %s область' % (cities[cid][2], typed, number, OBLAST[region_key(cid)]),
            lat, lon, 'H', 0.5, 'village')
    typo = 0
    for cid, typed, number in towns[30:]:
        word = max(typed.split(), key=len)
        if len(word) < 7 or typo >= 15:
            continue
        i = rng.randrange(2, len(word) - 1)
        wrong = word[:i] + rng.choice([c for c in CYRILLIC if c != word[i]]) + word[i + 1:]
        lat, lon = by_key[(cid, typed, number)][0]
        add('%s %s %s' % (cities[cid][2].lower(), typed.replace(word, wrong), number),
            lat, lon, 'H', 0.5, 'typo')
        typo += 1
    for cid, typed, number in kyiv[:10]:
        lat, lon = by_key[(cid, typed, number)][0]
        add('%s %s' % (typed, number), lat, lon, 'H', 0.5, 'kyiv-street')
    big = sorted({cid for cid, c in cities.items() if c[1] in (0, 1)
                  and len(city_by_name[fold_word(c[2])]) == 1})
    rng.shuffle(big)
    for cid in big[:10]:
        add(cities[cid][2], cities[cid][3], cities[cid][4], 'C', 2, 'town-name')
    print('# query\tlat\tlon\tkind\ttolerance_km\tgroup (held out, seed 20261020)')
    print('\n'.join(out))


if __name__ == '__main__':
    main()
