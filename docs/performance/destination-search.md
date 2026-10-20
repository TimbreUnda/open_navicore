# Destination search: options A and B

Status 2026-10-20: prototypes and an offline comparison. The radio runs of
both prototypes are still to do (commands at the end); every radio number
below is either measured with the OsmAnd calls on the radio (2026-10-19) or
an estimate built from those measurements, and says which.

## The problem

The search worker passes the whole query to OsmAnd's
`AddressesByNameSearch` and `AmenitiesByNameSearch` as one name, over all
26 regional OBFs. Measured on the radio (`osmand_search_smoke`):

| Query | Time | Result |
|---|---|---|
| `дніпро червоний камінь 4` | 4.4 s addresses + 9.0 s POIs | none |
| `Лозова Харківська область` | 2.9 s + 9.0 s | none |
| `Лозова` (Kharkiv file only) | 0.12 s | the town, after `Лиманівка` |
| first search after start | +11.1 s collecting OBF headers | |

No OSM object is called "Дніпро Червоний Камінь 4", so the phrase can never
match; the search also starts while the user is still typing.

## The two options

Both share `tests/search_proto/address_query.h`: query parsing (house
number, oblast adjectives, street-type words dropped, Latin
transliteration), name normalisation (і/ї/и, є/е, apostrophes, ь) and
ranking (settlement before same-named street, exact before prefix words,
settlement size, distance, typos). They differ in the data path only.

- **A** (`tests/qnx_osmand_search_a_smoke.cpp`): OsmAnd OBFs. All
  settlements (33,423) are loaded into memory at start; a settlement's
  streets and a street's houses are read from the OBF on demand and cached;
  a street without a settlement ("хрещатик 5") uses OsmAnd's name index in
  the user's region, all regions only as a last resort. Typos can be
  corrected in settlement names only: there is no street-word dictionary.
- **B** (`tests/search_proto/search_b.cpp`): a prebuilt index, memory
  mapped. `osmand_address_dump_smoke` dumps the OBF addresses on the radio
  (3 min for Ukraine, streamed over ssh), `build_index.py` builds the index
  (5 s): sorted token dictionaries for settlements and streets, settlements →
  streets → houses as ranges. Typos are corrected against both dictionaries
  (1 edit, 2 for long words). No OsmAnd at run time.

## Results

Query sets: `queries.tsv` (18 queries, the ranking was tuned on it) and
`heldout.tsv` (80 queries drawn from the dump with a fixed seed, not tuned
on): a query passes when the top result is of the expected kind (settlement,
street, house) within 0.5 km (houses) or 2-3 km.

B is run on the workstation. A cannot run off the radio (QNX OsmAnd), so
`search_b --model-a` reproduces its behaviour on the same data (typo
correction for settlement words only, A's street-only lookup order) and
counts the OsmAnd operations A would do, priced with the radio
measurements: a settlement's streets 0.6 s for Дніпро (scaled by street
count), a street's houses 5 ms (1-11 ms measured), one region's name index
scan 200 ms (120-260 ms measured).

| Held-out group | A (model) | A radio, estimated median | B | B workstation |
|---|---|---|---|---|
| town + street + house (30) | 30/30 | 0.29 s first, 0.20 s again, max 1.0 s | 30/30 | <= 5 ms |
| village + oblast (15) | 15/15 | 0.41 s | 15/15 | <= 5 ms |
| Kyiv street + house, no town (10) | 10/10 | 0.21 s | 10/10 | <= 5 ms |
| town name only (10) | 10/10 | 0.20 s | 10/10 | <= 5 ms |
| one typo in the street (15) | 3/15 | 10.4 s (all regions, twice) | 15/15 | <= 15 ms |
| **all (80)** | **68/80** | mean 2.2 s | **80/80** | mean 1 ms |

Tuned set: both 18/18 (A's typo cases pass only because the misspelt word
happens to be close to a settlement name).

Cost: B's index is 49 MB (21 MB gzip) for Ukraine: 33,423 settlements,
280,654 streets, 2.0 M houses; heap < 0.1 MB. A keeps the settlement list
and loaded cities in the heap and the OBFs open (not measured on the radio).

## Conclusion

The query understanding (shared code) fixes the wrong answers; that part
is needed whichever data path is used. Between the data paths **B is the
better one**: every held-out query found, typo tolerance on street names
(which A cannot provide, and where it is slowest: ~10 s to find nothing),
and milliseconds instead of 0.2-1 s per query. Its costs are 49 MB of
storage and an index build whenever the maps change (dump on the radio +
build, or the builder ported to run on the radio after a map install).

A is a usable interim step (no new data files) if the index is not wanted
yet: 65/65 correctly typed held-out queries at an estimated 0.2-0.4 s.

## Still to do on the radio

Built by `make` (`build/qnx/tests/`): `osmand_search_a_smoke`, `search_b`.
Copy both, `ukraine.idx` and the two query files to `/tmp`, then

    ./search_b /tmp/ukraine.idx /tmp/heldout.tsv
    ./osmand_search_a_smoke /navi_map/osm /tmp/heldout.tsv

(the same with `queries.tsv`). This replaces the A estimates with
measurements, gives B's radio latency and A's start-up time and memory.
