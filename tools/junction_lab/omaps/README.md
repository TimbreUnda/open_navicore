# Organic Maps car turn generator (vendored for the junction lab)

Source: https://github.com/organicmaps/organicmaps, commit
`303ef27fbc99d238965d7ced471c8920944110e6` (2026-10-27), `libs/routing`.
License: Apache-2.0 (`LICENSE`, from the upstream `LICENSES/Apache-2.0.txt`;
the upstream files carry no per-file headers).

`../omaps_adapter.cpp` builds OM's `IRoutingResult` from an OsmAnd route and runs
`CarDirectionsEngine::MakeTurnAnnotation` → `GetTurnDirection` → `FixupCarTurns`
→ `SelectRecommendedLanes`. Output: the `"omaps"` array of `junction_lab_tool`.
Debug: `JUNCTION_LAB_OMAPS_DEBUG=1` dumps OM's loaded segments, candidates and
turn angles to stderr; `JUNCTION_LAB_OMAPS_LOG=1` prints OM's LOG(LWARNING) sites.

The code is C++17 (no C++20 features), so it can later build with QNX GCC 8.5.

## Files

Unchanged turn/lane logic: `routing/car_directions.*`, `turns.*`,
`turns_generator.*`, `turns_generator_utils.*`, `turn_candidate.hpp`,
`routing_result_graph.hpp`, `loaded_path_segment.hpp`,
`lanes/lane_info.*`, `lanes/lanes_recommendation.*`.

Edited (no behavioural change intended):

- `lanes/lane_way.hpp/.cpp`: `constexpr` dropped from `LaneWays` members that use
  `std::bitset` operations (constexpr only in C++23); `using enum` replaced by
  qualified names.
- `lanes/lanes_parser.cpp`: `std::views::split`/`filter`/`transform` replaced by a
  C++17 splitter with the same semantics (every delimiter ends a part, trailing
  empty part kept; ASCII spaces removed, ASCII lower-casing).
- `routing_settings.cpp`: C++20 designated initializers replaced by a helper with
  the same values; include of `routing_helpers.hpp` replaced by the
  `measurement_utils` shim.
- `directions_engine.hpp/.cpp`: trimmed to the interface plus `MakeTurnAnnotation`
  (loop unchanged). It is public and takes the `IRoutingResult` directly; the
  feature-reading parts (`Generate`, `FillPathSegmentsAndAdjacentEdgesMap`,
  `GetSegmentRangeAndAdjacentEdges`, `LoadPathAttributes`) are re-implemented on
  OsmAnd data in `../omaps_adapter.cpp` with the same control flow.

Shims (`shim/`): `base/assert.hpp` (ASSERT compiled out as in OM release builds;
CHECK throws instead of aborting), `base/logging.hpp`, `base/math.hpp`,
`base/stl_helpers.hpp`, `base/string_utils.hpp`, `base/internal/message.hpp`,
`geometry/*` (point, mercator, distance, angles: upstream formulas),
`indexer/feature_decl.hpp` (FeatureID = dense OsmAnd road number),
`indexer/ftypes_matcher.hpp` (HighwayClass enum only), `platform/*`,
`routing_common/num_mwm_id.hpp` (one mwm), `routing/segment.hpp` and
`routing/route.hpp` (trimmed `Segment`/`RouteSegment`; kept members copied
unchanged), `routing/vehicle_mask.hpp`.

## OsmAnd → OM mapping (omaps_adapter.cpp)

- Path points: OsmAnd 31-bit points → lat/lon → OM mercator. Layout as OM's router:
  fake start loop, jump from the requested start, the route, jump to the requested
  finish, fake finish loop. Route edges use the map road's point numbering (OsmAnd
  routes on road copies with an inserted precise point; matched by coordinates).
- Loaded segments are split at OM joints (`IsJoint`, same logic).
- Edges at a node: every road `loadRouteSegment(x31, y31)` returns; direction per
  the car profile's `isOneWay`; U-turn onto the same segment only at a joint or a
  road end (`IndexGraph::IsUTurnAndRestricted`); OsmAnd turn restrictions without a
  via way (no_* to the road; only_* whose target is at the node). Ingoing count =
  edges that can enter the route's outgoing segment, including the reversed
  outgoing one, as in OM.
- Highway class: OM's table on the OsmAnd `highway` tag; `service` +
  `service=driveway|parking_aisle` → ServiceMinor (Service if OsmAnd's map has no
  `service` tag); `route=ferry|shuttle_train` → Transported. Link = `*_link` of
  motorway..tertiary. Roundabout = OsmAnd `roundabout()`. One-way = car profile.
- Lanes: `turn:lanes` (one-way) or `turn:lanes:forward/backward`, falling back to
  `turn:lanes` (OM's fallback). Name/ref: OsmAnd default `name`/`ref`;
  destination, destination:ref, junction:ref from OsmAnd's tags (display only).
- Not fed (defaults): OM's access-conditional / road-access checks (OsmAnd's car
  profile already filtered the roads), via-way restrictions, altitudes (0),
  mwm-border twin filtering (one map set, not needed).
