# Navigation UI/UX: features and user flow

Brief for the visual designer. It describes what the in-car navigation UI does
**today** (branch `feat/production-offline`, commit a2d6be7), how a driver gets
from "I want to go somewhere" to "I arrived", and where the current flow has
gaps or placeholders. Mockups should cover every screen and state listed here;
the gaps section is where design input is most wanted.

Source of truth for behaviour: `backends/osmand/ui/navigation_model.*`
(screens and transitions), `backends/osmand/ui/navigation_view.cpp` (layout),
`backends/osmand/osmand_engine.cpp` (map, camera, taps). Hard platform limits
are in [api-ui-restrictions.md](api-ui-restrictions.md).

## 1. Canvas and constraints

| Item | Value |
|---|---|
| Canvas | 1024 x 574 px navigation band inside the stock HMI (layout adapts to the size we are given) |
| Stock chrome | Top and bottom bars belong to the radio; we cannot change or overlap them |
| Left rail | 336 px, opaque; the map fills the remaining ~688 px |
| Touch targets | at least 64 px, 8 px spacing, 16 px from edges |
| Corner radius | 6 to 8 px (12 px on floating map buttons) |
| Input | tap, double tap, drag, long press, and two-finger pinch/rotate (Pointer V2 contacts). No hardware keys, no stock keyboard |
| Theme | UI panels are dark (night palette). The **map** has a day and a night palette, switched by a button |
| Audio | none: no voice prompts, no chimes |
| Text | Montserrat 14/20/24/28; English only; Cyrillic map labels come from the map renderer |
| Maps | offline only; no traffic, no online search |

Current UI colours (tokens to keep or replace):
`base #0d0f12`, `surface #16191e`, `control #1f232a`, `line #2a2f37`,
`text #eef0f3 / #a4abb5 / #6f7782`, `accent #3d7bff`, `maneuver #1f4fd1`,
`warning #f6c86a`, `danger #ff8a80 on #3a1614`.

## 2. Screens

There are four screens. The map is always rendered behind them, except on Search.

### 2.1 Free drive (home)

The default screen with no destination set.

- **Rail**, top to bottom:
  - **Where to?** button: opens Search.
  - **Home** and **Work** tiles, 80 px tall. They are *placeholders with no
    action yet* (see gaps).
  - **RECENT**: up to 5 recent destinations, newest first, one tap each. Empty
    state text: "No recent destinations".
  - Footer: "Offline maps".
- **Map area**:
  - Vehicle marker: a blue dot with a white ring, a halo and a heading pointer.
  - Floating control column at the top right: **+**, **−**, **orientation**
    (shows an up arrow for heading-up, "N" for north-up), **2D/3D**,
    **Day/Night**.
  - **Recenter** button (GPS icon, round, accent colour), bottom right. It is
    shown only when the camera is not following the car.
- **Map content**:
  - vector map: roads, areas, buildings, water;
  - street names, place names;
  - driver POI icons with names. Priority order: fuel, charging, services, rest
    areas, hospitals, speed cameras, parking, car repair, car wash, police,
    then toilets, hotels, food, ATMs, then sights. Lower priorities appear only
    when zoomed in.
  - Labels avoid the rail and the floating buttons.

### 2.2 Search

Full screen. The map is hidden.

- **Header**: back button **<** and a one-line text field, placeholder "Street,
  house or place".
- **Body**:
  - Results list on the left, 420 px wide, 82 px rows:
    - each row shows the title plus a subtitle (type or address) and a
      distance;
    - the first row is highlighted;
    - at most 8 addresses followed by 4 POIs.
  - On-screen keyboard (LVGL) on the right.
- **Messages in the list area**:
  - "Type to search installed maps" (empty query);
  - "Searching installed maps..." (while searching);
  - "No offline results";
  - "Calculating route..." (after a result is tapped);
  - "No route available in installed maps".
- **Driving lock**: above about 9 km/h, the keyboard is replaced by "Typing is
  paused while driving" and the RECENT list. The text field becomes read-only.
  The lock releases below about 3.6 km/h.

### 2.3 Route preview

- **Rail**:
  - Destination title and subtitle.
  - Route cards, 86 px each: duration and distance, plus a reason label.
    **Currently there is always exactly one route, labelled "Fastest".**
  - The selected card has an accent border.
  - **Start** button at the bottom, in the maneuver blue.
- **Map**:
  - The whole route is drawn and the camera fits it (overview, north-up).
  - Only **+ / −** are shown. There is no recenter, orientation, tilt or night
    button here.
- **Leaving the preview**: there is **no Back/Cancel button** here today (see
  gaps).

### 2.4 Guidance (active navigation)

- **Rail, normal state**:
  - **Maneuver card**, blue, 168 px. Top to bottom:
    - "ROUTE SIMULATION" tag, shown only in bench simulation;
    - distance to the next maneuver, in 28 pt;
    - action, e.g. "Turn right";
    - road name, or the destination name when the road is unknown.
    - A maneuver glyph sits on the left. Today it is a text placeholder
      (`< > ^ U O X`); real arrow icons are wanted.
  - **Then** strip, 48 px: "Then Turn left Street" or "Continue to
    destination".
  - **Lanes** strip, 44 px: lanes are not available, so it shows "Lane info not
    available here".
  - **Trip summary**: remaining time and distance, e.g. "12 min 8.4 km". There
    is no clock ETA.
  - Bottom buttons: **Overview** and **End** (End is red).
- **Rail, no GPS state**:
  - Amber card: "No GPS fix" / "Guidance is paused until a position is
    available."
  - "NEXT ON ROUTE" with the next instruction.
  - Summary "--:-- ETA paused".
- **Map**:
  - Route line: blue ahead, **grey for the part already driven**.
  - A **turn arrow** is drawn on the road from 400 m before each turn.
  - Vehicle marker.
  - Full control column and recenter.
- **Arrival**:
  - Arrival triggers within 25 m of the end.
  - The card shows "Arriving at", 0 m and 0 min.
  - **The app stays in Guidance until the driver taps End.** There is no
    arrival screen.

## 3. Map interactions (all screens with a map)

| Gesture / control | Result |
|---|---|
| Drag with one finger | Map follows the finger exactly (also when rotated or tilted); camera becomes **free**, recenter button appears |
| Release after drag | Map stays; after **10 s idle** it smoothly returns to following the car |
| Tap (moved < 10 px, released < 0.5 s) on a POI icon | Starts routing to that POI (see flow B). Ignored during Guidance |
| Tap on empty map | Nothing |
| + / − | Zoom one step; while following, the zoom offset is kept (max +3) |
| Orientation | Heading-up (car low on screen, map rotates) ↔ north-up (car centred) |
| 2D / 3D | Tilted 60° perspective; only in heading-up or free mode |
| Day / Night | Map palette switch (UI panels stay dark) |
| Recenter | Follow the car again |
| Overview (guidance) | Fit the whole route north-up; recenter returns to following |

The camera zooms out automatically with speed. At 30 km/h or below it holds
zoom 17; it zooms out progressively, down to 14.5, at high speed.

## 4. User flow: "I want to drive somewhere"

```
                         ┌──────────────────────────────┐
                         │          FREE DRIVE          │
                         │  map follows car, rail with  │
                         │  Where to? / Home / Work /   │
                         │  Recent                      │
                         └──┬──────────┬─────────┬──────┘
             tap "Where to?"│          │tap Recent│tap POI icon on map
                            │          │  row     │(not in guidance)
                            ▼          │          │
                ┌──────────────────┐   │          │
          ┌─────│      SEARCH      │   │          │
          │ "<" │ type query       │   │          │
          │back │  (driving: typing│   │          │
          │     │  locked → Recent │   │          │
          │     │  list instead) ──┼───┤          │
          │     └────────┬─────────┘   │          │
          │              │ results arrive (8 addr + 4 POI)
          │              │  ├─ none → "No offline results" (stay, edit query)
          │              ▼             │          │
          │     tap a result           │          │
          │              │             │          │
          │              ▼             ▼          ▼
          │     ┌─────────────────────────────────────────┐
          │     │  CALCULATING ROUTE  (shown on Search    │
          │     │  screen: "Calculating route...")        │
          │     └───────┬────────────────────────┬────────┘
          │             │ route found            │ no route
          │             │                        ▼
          │             │        "No route available in installed maps"
          │             │         (stay on Search: pick another / "<")
          │             ▼
          │     ┌──────────────────┐
          │     │  ROUTE PREVIEW   │  map fits whole route
          │     │  dest + 1 route  │  + / − only
          │     │  card + Start    │  (no back button today)
          │     └────────┬─────────┘
          │              │ tap Start  → destination saved to Recent
          │              ▼
          │     ┌──────────────────────────────────────────┐
          │     │              GUIDANCE                    │
          │     │  maneuver card · then · lanes · ETA      │
          │     │  Overview / End · turn arrow on map      │
          │     │                                          │
          │     │  GPS lost ⇄ "No GPS fix" paused card     │
          │     │  Overview ⇄ recenter / 10 s idle         │
          │     │  drag map ⇄ recenter / 10 s idle         │
          │     │  off route → (nothing yet, no reroute)   │
          │     └───────┬──────────────────────┬───────────┘
          │             │ within 25 m of end   │ tap End
          │             ▼                      │
          │     "Arriving at …" 0 m, stays     │
          │      until End is tapped ──────────┤
          │                                    ▼
          └──────────────────────────────► FREE DRIVE
```

### Flow A: search (parked)

1. Free drive → tap **Where to?**
2. Type in the field. Every keystroke starts a new offline search, and older
   results are discarded.
3. Tap a result. The list shows "Calculating route...".
4. The route preview opens with the route drawn and fitted.
5. Tap **Start**. Guidance begins and the destination goes to Recent.
6. Drive. On arrival, tap **End** to return to free drive.

### Flow B: tap a POI on the map

1. In free drive or route preview, tap a POI icon, e.g. a fuel station. The
   hit radius is about 24 px.
2. The app goes **straight to route calculation**. There is no "place card"
   step: it opens the Search screen, showing "Calculating route...", with the
   POI as the only result.
3. Then continue as in flow A, from step 4. The subtitle is the POI category,
   e.g. "Fuel".

### Flow C: recent destination (works while driving)

1. Free drive: tap a RECENT row. Alternatively, while driving, open Search and
   tap a row in the locked Recent list.
2. Route calculation, then preview, then Start, as in flow A.

### Flow D: look around during guidance

- Drag the map, or tap **Overview**. Guidance continues in the rail. Tap
  **Recenter**, or wait 10 s, to return to following.

## 5. State inventory for mockups

Please design each of these:

1. **Free drive**:
   - with Recent filled and with "No recent destinations";
   - camera following and camera free (recenter visible);
   - map day and night;
   - 2D and 3D.
2. **Search**:
   - empty query;
   - searching;
   - results;
   - no results;
   - calculating route;
   - no route;
   - driving lock.
3. **Route preview**:
   - one route, as today;
   - **2 to 3 alternatives**, for the future;
   - long destination names.
4. **Guidance**:
   - normal, far from the turn;
   - turn within 400 m, with the arrow on the map;
   - roundabout with exit number (the data exists: `roundabout_exit`);
   - "then" strip, present and absent;
   - no GPS fix;
   - simulation tag;
   - overview mode;
   - arriving.
5. **The map layer itself**:
   - vehicle marker, in follow and free modes;
   - route line, ahead and driven;
   - turn arrow;
   - POI icon set and POI label style;
   - day and night palettes.
6. **Glyphs**: maneuver icons for Straight, Left, Slight left, Sharp left,
   Right, Slight right, Sharp right, Keep left, Keep right, U-turn,
   Roundabout and Arrive.

## 6. Gaps and placeholders (design input wanted)

| # | Gap today | Suggested direction |
|---|---|---|
| G1 | **Home / Work** tiles do nothing; no way to save a place | "Set Home/Work" flow (long press on a recent, or from place card) |
| G2 | **POI tap jumps straight into routing** via the Search screen (keyboard flashes up) | A **place card** over the map: name, category, distance, *Navigate* / *Close* |
| G3 | **No Back/Cancel on Route preview** | Back button to the previous screen (Search or Free drive) |
| G4 | **One route only** ("Fastest"); card design assumes alternatives | Design for 1–3 routes; tap a card or the line on the map to select |
| G5 | **No arrival screen**; Guidance stays until End | Arrival card: "You have arrived", auto-dismiss to Free drive |
| G6 | **No ETA clock**, only remaining time/distance | Show arrival time (needs a trustworthy clock: the radio clock is unreliable) |
| G7 | **Maneuver glyph is a text character** | Proper arrow icon set, roundabout with exit number |
| G8 | **No off-route handling / rerouting** | "Recalculating…" state in the maneuver card |
| G9 | **Lanes strip** always shows "not available" | Hide when there is no data, or design for future lane arrows |
| G10 | **No current speed / speed limit / current street name** | Small speed and street widgets on the map |
| G11 | Search result **distance is measured from a fixed Kyiv point**, not the car | Distance from the car; sort by distance |
| G12 | No **search categories** (fuel nearby, parking nearby) | One-tap category chips: these work while driving, unlike typing |
| G13 | No **cancel while calculating** a long route | Progress and cancel state |
| G14 | No **favourites list** beyond 5 recents | Favourites/saved places |
| G15 | UI panels are always dark; only the map follows Day/Night | Light variant of panels for day |
| G16 | No voice, so the turn card must carry everything visually | Strong distance and countdown emphasis near the turn |
| G17 | ~~No pinch zoom (single-touch API)~~ Done: pinch/rotate (Pointer V2) and double tap | Keep +/− as well |

## 7. Out of scope (platform limits)

The stock top and bottom bars and any stock dialogs cannot be changed. The
following are not available on this platform:

- more than two touch contacts;
- the stock keyboard;
- voice prompts;
- online traffic;
- instrument-cluster graphics.

See [api-ui-restrictions.md](api-ui-restrictions.md) for details.
