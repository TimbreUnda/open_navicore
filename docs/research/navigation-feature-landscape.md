# Navigation feature landscape

Research snapshot: **2026-10-10**. External sources were accessed on that date.
Repository evidence was reviewed at `9d87881` on `main`. This is a product and
design research document, not a statement of implemented feature parity, a
benchmark, or a proposed screen layout.

## Executive synthesis

Open NaviCore should first become a dependable, native, offline car navigator:
find a destination, choose a comprehensible route, follow timely instructions,
recover from a missed turn, and remain usable when connectivity or position
quality changes. The repository already offers the stock-facing lifecycle and
integration boundary, but the initial OsmAnd backend uses a fixed Kyiv viewport
and ignores live GPS. A rendered map is therefore an important foundation, not
yet a usable navigator. See [current status](../../README.md#current-status) and
the [OsmAnd milestone](../../backends/osmand/README.md#osmand-backend).

The most useful ideas to borrow are concrete:

- Make offline coverage and missing resources understandable. Google documents
  reduced offline capability, Apple supports several offline travel modes, and
  HERE WeGo documents an explicit offline-mode recovery path. These are different
  promises, not interchangeable “offline” checkboxes.
  [Google offline maps](https://support.google.com/maps/answer/6291838?hl=en),
  [Apple offline maps](https://support.apple.com/en-ie/105084),
  [HERE recovery guidance](https://help.here.com/faq/routing_and_navigation).
- Explain what is ahead: TomTom's route bar, Garmin's upcoming services, and
  lane/junction guidance address the next decision before it becomes urgent.
  [TomTom GO design lineage, 2020](https://corporate.tomtom.com/node/26431/pdf),
  [Garmin map tools](https://www8.garmin.com/manuals/webhelp/GUID-6E5DFD2E-EEE4-4EF3-89BC-AACD1B4936C7/EN-US/GUID-9DCBF296-FA93-4679-9B32-3EC299623D74.html).
- Use OsmAnd's offline search, routing preferences, and map styles selectively.
  Its mobile application is a capability reference; those features do not arrive
  automatically with the QNX rendering library.
  [Search](https://osmand.net/docs/user/search/search-all/),
  [car routing](https://osmand.net/docs/user/navigation/routing/car-based-routing/),
  [styles](https://osmand.net/docs/user/map/vector-maps/),
  [local backend boundary](../backend-sdk/README.md#backend-sdk).
- Treat voice, the center display, and the cluster as presentations of one route
  state. The recovered cluster contract has useful fields but explicitly rejects
  unresolved route-symbol and lane encodings. Design around proven semantics
  while those gaps are resolved.
  [Cluster contract and limits](../technical.md#instrument-cluster-backend-and-osmand-mapping).

Live traffic, collaborative reports, range-aware EV routing, imagery, and cloud
handoff are valuable extensions. They bring distinct data, connectivity,
privacy, hardware, and service dependencies. They should not delay the first
complete offline journey.

## Method and evidence labels

The comparison samples complementary approaches: broad place discovery
(Google/Apple), driver reports (Waze), offline consumer and supplier navigation
(HERE/TomTom), a dedicated automotive device (Garmin), extensible offline
navigation (OsmAnd), privacy-focused simplicity (Organic Maps), and the actual
stock integration target. Google built-in is included as a native-vehicle
reference. This is not a popularity ranking or an exhaustive specification.

Only first-party product help, manuals, policies, release notes, and SDK docs
support external claims. Availability may depend on country, language, OS,
vehicle, license, subscription, or data plan. A vendor publication establishes
documented capability, not successful operation on this bench unit. Older
manuals and release notes are dated explicitly; search-engine crawl dates are
not treated as release dates. No apps were road-tested and no radio was accessed
for this research.

| Label | Evidence strength and permitted interpretation |
|---|---|
| **D — documented product** | Official help/manual describes the feature for the stated product and scope. Not independently tested here. |
| **SDK — documented engine** | A supplier API describes a capability. Does not prove that its consumer app exposes it or that ARM/QNX is supported. |
| **O — observed stock boundary** | Committed runtime evidence records a particular call or payload from the stock integration. Does not establish an entire user journey. |
| **B — recorded replacement bench result** | Repository documentation reports a bounded replacement test. No new bench observation was made for this document. |
| **A — recovered interface** | Reviewed ABI/layout/caller-consumer evidence establishes an integration contract; feature behavior needs separate evidence. |
| **C — configured, unproven** | A configuration or request exists, but its resulting behavior was not demonstrated. |
| **I — inference/recommendation** | Design judgment derived from the cited evidence. Not an implementation or vendor claim. |
| **NE — not established** | The reviewed sources do not answer the question. This does not mean the feature is absent. |

These are evidence classes, not numeric scores. For example, **O** can be strong
evidence that a subscription request happened and no evidence that useful traffic
arrived. The stock section uses repository-relative links with stable section
anchors or source line anchors. It follows the repository's
[evidence rules](../../CONTRIBUTING.md#abi-evidence-rules).

## Feature taxonomy for design decisions

The tiers below are recommendations (**I**), defined in
[capability tiers](#recommended-capability-tiers). “Baseline” means a priority
for this project, not that every comparator has the same implementation.

### Find, inspect, and plan

| Capability / user job | Evidence that informs the decision | Open NaviCore implication |
|---|---|---|
| Browse, pan, zoom, return to position | Garmin distinguishes track-up 2D, north-up 2D, and 3D; Google built-in has compass and recenter actions. [Garmin views](https://www8.garmin.com/manuals/webhelp/GUID-6E5DFD2E-EEE4-4EF3-89BC-AACD1B4936C7/EN-US/GUID-7714E17E-4BE8-4797-80FE-B707A03409E0.html), [Google built-in](https://support.google.com/built-in/answer/16537773?hl=en) | Foundation: distinguish following from browsing; make orientation and recentering predictable. |
| Day/night and map themes | OsmAnd provides day/night and custom vector styles. [Vector maps](https://osmand.net/docs/user/map/vector-maps/) | Foundation: readable day/night maps; connect to vehicle theme only after the raw values are understood. Custom styles are optional. |
| Destination entry, categories, coordinates, history, favorites | OsmAnd documents offline address, POI, coordinate, favorite, and history search; Apple CarPlay exposes recent/saved destinations and service categories. [OsmAnd search](https://osmand.net/docs/user/search/search-all/), [Apple CarPlay](https://support.apple.com/en-mide/guide/iphone/iph215b053f6/ios) | First usable navigator: address/POI search, map-selected destination, recents, favorites, home/work. Coordinates provide a useful fallback for incomplete addresses. |
| Contacts and contextual destinations | Apple CarPlay can suggest destinations from contacts, calendars, messages, and email. [CarPlay destination entry](https://support.apple.com/en-mide/guide/iphone/iph215b053f6/ios) | Competitive/optional: preserve permission and source context. A stock phone adapter does not prove contact-address access. |
| Route calculation, alternatives, avoidances | Google offers alternate routes and route options; OsmAnd documents toll, unpaved-road, ferry, border, and individual-road avoidance. [Google routes](https://support.google.com/maps/answer/144339?co=GENIE.Platform%3DAndroid&hl=en), [OsmAnd car routing](https://osmand.net/docs/user/navigation/routing/car-based-routing/) | First usable: one valid route with visible distance/ETA and essential preferences. Competitive: meaningful alternatives with reasons for differences. |
| Waypoints and multi-stop planning | Apple's driving planner permits up to 14 added stops; Waze permits one intermediate stop. These are materially different trip-planning models. [Apple stops](https://support.apple.com/en-ae/guide/iphone/iph837d13d03/ios), [Waze stop limit](https://support.google.com/waze/answer/6262564?hl=en) | First usable: add/remove an intermediate destination. Competitive: reorder multiple stops and distinguish a stop from a shaping point. |
| Fuel, parking, charging discovery | Garmin exposes services ahead; HERE WeGo includes parking and charging-point discovery, with live charging availability limited by region. [Garmin Up Ahead](https://www8.garmin.com/manuals/webhelp/GUID-6E5DFD2E-EEE4-4EF3-89BC-AACD1B4936C7/EN-US/GUID-033B9E44-1B66-4923-9E36-F2DB57CA85A4.html), [HERE routing FAQ](https://help.here.com/faq/routing_and_navigation) | First usable: basic local POI categories. Competitive: along-route detours and destination parking. Availability/prices need a separate online source. |

### Follow, recover, and integrate

| Capability / user job | Evidence that informs the decision | Open NaviCore implication |
|---|---|---|
| Traffic, incidents, closures, ETA, dynamic rerouting | Google uses live traffic and offers better routes; Waze uses driver activity and reports. [Google navigation](https://support.google.com/maps/answer/3273406?hl=en), [About Waze](https://support.google.com/waze/answer/6071177?hl=en) | Competitive: separate route computation from traffic freshness; explain a reroute and its expected benefit. Do not label a historical-speed ETA “live.” |
| Maneuvers, lanes, junctions, signposts | Garmin documents a junction simulation when available; HERE Navigate lists lane assistance and junction visuals with signposts. [Garmin lanes](https://www8.garmin.com/manuals/webhelp/GUID-6E5DFD2E-EEE4-4EF3-89BC-AACD1B4936C7/EN-US/GUID-E6EA81D1-5E15-4F3D-8021-4A8421F0BD0C.html), [HERE SDK](https://docs.here.com/here-sdk/docs/android-introduction-feature-list) | First usable: next maneuver, distance, street, and turn list. Competitive: lanes/signposts when data exists; photo-like junction artwork is optional. |
| Speed limits and camera warnings | OsmAnd derives speed limits from OSM and documents configurable prompts and camera handling. [Voice and warnings](https://osmand.net/docs/user/navigation/guidance/voice-navigation/) | Competitive: show unknown limits as unknown; keep warning preferences distinct. Camera availability and enablement require a regional policy. |
| Voice input versus spoken output | Siri can select destinations and control guidance; OsmAnd supports TTS and, on Android, limited recorded prompts. [Siri directions](https://support.apple.com/guide/iphone/use-siri-to-get-directions-iph639bc4696/26/ios/26), [OsmAnd speech](https://osmand.net/docs/user/navigation/guidance/voice-navigation/) | First usable: spoken maneuvers, repeat, mute, prompt volume. Voice search is a separate competitive feature with its own engine and language requirements. |
| Offline maps, search, routing, updates | HERE Navigate documents offline search/routing/guidance; OsmAnd separates full map updates from incremental OSM updates. [HERE offline SDK](https://docs.here.com/here-sdk/docs/android-offline-maps), [OsmAnd resources](https://osmand.net/docs/user/personal/maps-resources/) | First usable: whole-trip resource coverage and offline rerouting. Foundation: resource/version inventory. Competitive: resumable updates and recoverable replacement of map files. |
| GPS quality, map matching, tunnels | TomTom SDK describes map matching and software dead reckoning; Waze tunnel positioning requires installed BLE beacons and enabled Bluetooth. [TomTom positioning](https://docs.tomtom.com/navigation/android/guides/navigation/map-matching), [Waze Beacons](https://support.google.com/waze/partners/answer/9416071?hl=en) | Foundation: distinguish fresh, stale, and unavailable position. Competitive: tested map matching. Tunnel prediction is optional and must be identifiable as estimated. |
| EV range and charging-aware routing | Google built-in can estimate arrival battery and insert charging, including charging time; Apple EV routing depends on compatible vehicles/regions. [Google EV](https://support.google.com/maps/answer/9773205?hl=en), [Apple EV](https://support.apple.com/en-mide/guide/iphone/iphc5e3a4b4b/ios) | Advanced: charger search alone is not range-aware routing. Require trustworthy vehicle charge, consumption, connector, and charging data before making an arrival-range promise. |
| Sharing, reports, planning, handoff | Google location sharing is explicit and account-based; Apple supports Mac-to-iPhone planning; Waze offers traffic-based departure reminders. [Google sharing](https://support.google.com/maps/answer/15437054?hl=en), [Apple directions](https://support.apple.com/en-gb/guide/iphone/iph02f94fc1c/ios), [Waze planning](https://support.google.com/waze/answer/6378906?hl=en) | Competitive: import a destination or trip without retyping. Advanced: expiring ETA sharing and collaborative reports, with consent and service ownership. |
| CarPlay, Android Auto, native display, cluster, HUD | Projection has app-specific limitations; native Google built-in can use the driver dashboard in some cars. [Waze CarPlay](https://support.google.com/waze/answer/9123774?hl=en), [Google native](https://support.google.com/built-in/answer/16537773?hl=en) | Preserve stock native integration first. Projection support is not proof of cluster/HUD output. A HUD path for this VP4R is **NE**. |
| Accessibility and localization | Google documents TalkBack, scalable labels, and detailed walking guidance. OsmAnd's accessibility plugin is Android-specific. [Google accessibility](https://support.google.com/accessibility/answer/6396990?hl=en), [OsmAnd plugins](https://osmand.net/docs/user/plugins/) | Foundation: contrast, legible text, non-color cues, predictable focus/touch behavior, Unicode and units. QNX accessibility and locale mappings need their own validation. |
| Privacy, telemetry, accounts | Organic Maps states no tracking or registration; OsmAnd's policy separately describes download telemetry, optional screen analytics, and Cloud. [Organic Maps](https://organicmaps.app/), [OsmAnd policy](https://osmand.net/docs/legal/privacy-policy/) | Foundation: core navigation without an account; local history with deletion. Any optional network or telemetry path should state what it sends and why. |
| Failure and recovery | OsmAnd can announce GPS loss/recovery; HERE documents restarting a trip in offline mode after a failed reconnect. [OsmAnd prompts](https://osmand.net/docs/user/navigation/guidance/voice-navigation/), [HERE recovery](https://help.here.com/faq/routing_and_navigation) | First usable: lost fix, missing map, no route, audio unavailable, and interrupted guidance are designed states. Recovery must not silently invent a current position or route. |

## Product comparison

All external cells below are **D** unless marked **SDK**. Tradeoffs are design
interpretations (**I**) of the stated dependencies, not measured quality scores.

### Consumer and dedicated-device references

| Product / scope | Distinctive capability worth studying | Limitation or dependency that matters here |
|---|---|---|
| **Google Maps — phone/web** | Broad destination-to-route workflow, live traffic and spoken lane guidance; route preview can include Street View. [Navigation](https://support.google.com/maps/answer/3273406?hl=en), [route preview](https://support.google.com/maps/answer/144339?hl=en) | Offline driving requires coverage of the entire route; offline traffic/alternatives and walking/cycling/transit directions are unavailable. Maps need refreshing. [Offline rules](https://support.google.com/maps/answer/6291838?hl=en) |
| **Apple Maps — iPhone/CarPlay** | Multi-stop planning, Siri, saved/contextual destinations, ETA sharing, and a parked-car continuation make the whole trip coherent. [Stops](https://support.apple.com/en-ae/guide/iphone/iph837d13d03/ios), [CarPlay](https://support.apple.com/en-mide/guide/iphone/iph215b053f6/ios) | Offline maps require iOS 17+ and supported regions; EV integration is vehicle-specific. [Offline support](https://support.apple.com/en-ie/105084), [EV compatibility](https://support.apple.com/en-mide/guide/iphone/iphc5e3a4b4b/ios) |
| **Waze — phone, with projection/native variants** | Driver reports and travel-speed observations support incident-aware routing; departure reminders respond to traffic. [Service model](https://support.google.com/waze/answer/6071177?hl=en), [planned drives](https://support.google.com/waze/answer/6378906?hl=en) | Designed around a data connection; one intermediate stop; car-screen capabilities differ. [Connectivity](https://support.google.com/waze/answer/6071177?hl=en), [stops](https://support.google.com/waze/answer/6262564?hl=en) |
| **HERE WeGo — consumer phone app** | Downloaded-region navigation, route preferences, and an explicit parked-car/walking handoff address door-to-door travel. [Offline maps](https://help.here.com/faq/offline_maps), [routing FAQ](https://help.here.com/faq/routing_and_navigation) | Offline mode must be selected; some in-trip stop/preferences actions are unavailable in Android Auto, and CarPlay is also limited. [Routing FAQ](https://help.here.com/faq/routing_and_navigation) |
| **TomTom GO Navigation → GO Expert — car profile** | Established GO design uses an ahead-of-vehicle route bar, moving lane guidance, and downloaded maps. [GO feature lineage, 2020](https://corporate.tomtom.com/node/26431/pdf) | Current branding is GO Expert; car use remains available, with subscription dependence. The separate TomTom app has different technology/features. [Rename](https://help.tomtom.com/hc/en-gb/articles/29231703543314-GO-Navigation-becoming-GO-Expert-app), [current app comparison](https://help.tomtom.com/hc/en-gb/articles/32565859177746-Comparing-the-GO-Expert-app-and-the-TomTom-app) |
| **Garmin DriveSmart 66/76/86 — dedicated automotive device** | Upcoming services, route shaping, saved trips, and lane/junction presentation suit an in-car task focus. [Manual v11, August 2024](https://www8.garmin.com/manuals/webhelp/GUID-6E5DFD2E-EEE4-4EF3-89BC-AACD1B4936C7/EN-US/DriveSmart_66-76-86_with_Alexa_OM_EN-US.pdf), [lane guidance](https://www8.garmin.com/manuals/webhelp/GUID-6E5DFD2E-EEE4-4EF3-89BC-AACD1B4936C7/EN-US/GUID-E6EA81D1-5E15-4F3D-8021-4A8421F0BD0C.html) | Live traffic requires incoming data, here via Garmin Drive on a phone, with regional coverage. Map/software updates require maintenance and storage. [Traffic](https://www8.garmin.com/manuals/webhelp/GUID-6E5DFD2E-EEE4-4EF3-89BC-AACD1B4936C7/EN-US/GUID-D13706CA-CFA3-45EF-8648-03BF4F9722B5.html), [Express updates](https://www8.garmin.com/manuals/webhelp/GUID-6E5DFD2E-EEE4-4EF3-89BC-AACD1B4936C7/EN-GB/GUID-AB01F4C7-F1B1-4169-BC92-A222FF8344EC.html) |
| **OsmAnd — Android/iOS application** | Offline search/navigation, explicit routing rules, vector styles, GPX workflows, and plugins offer the closest capability reference for our engine direction. [Navigation](https://osmand.net/docs/user/navigation/), [search](https://osmand.net/docs/user/search/search-all/), [plugins](https://osmand.net/docs/user/plugins/) | Mobile features, OS speech engines, and plugin UI are not a ready-made QNX application. Android Auto requires a paid offering; resources and features vary by platform/plan. [Android Auto](https://osmand.net/docs/user/navigation/auto-car/), [purchases](https://osmand.net/docs/user/purchases/) |
| **Organic Maps — offline/privacy reference** | Offline search and voice navigation, bookmarks/tracks, dark mode, and phone projection with no mandatory registration. [Features](https://organicmaps.app/) | Live traffic, traffic-aware ETA, and range-aware EV routing are **NE** in this reviewed feature set. Its OSM data and separate engine are not interchangeable with the OsmAnd backend. |

### Supplier and native-vehicle references

| Product / scope | What the evidence establishes | What it does not establish |
|---|---|---|
| **HERE SDK Navigate — Android documentation** | **SDK:** offline maps/search/routing/guidance, alternatives, lane assistance, map matching with confidence, and tunnel extrapolation. [Feature list](https://docs.here.com/here-sdk/docs/android-introduction-feature-list), [offline guide](https://docs.here.com/here-sdk/docs/android-offline-maps) | These capabilities are not all WeGo features. Explore and Navigate licenses differ. No HERE port or entitlement for this QNX project is established. |
| **TomTom Maps and Navigation SDK — automotive engine reference** | **SDK:** configurable online/offline behavior and explicit map-matched/dead-reckoned locations. An onboard map changes whether new route planning works offline. [Configuration](https://docs.tomtom.com/navigation/android/getting-started/initializing-and-configuring-the-sdk), [routing FAQ](https://docs.tomtom.com/navigation/android/faqs/faqs), [map matching](https://docs.tomtom.com/navigation/android/guides/navigation/map-matching) | A cached route is not a complete offline map package. SDK configuration, version, production map licensing, and OEM integration matter; GO behavior must not be inferred from an SDK page. |
| **Google Maps built into vehicles** | **D:** manufacturer-dependent dashboard display, automatic offline-map management, and vehicle-aware EV routes/arrival battery. [Navigation](https://support.google.com/built-in/answer/16537773?hl=en), [offline](https://support.google.com/maps/answer/9770624?hl=en), [EV](https://support.google.com/maps/answer/9773205?hl=en) | This is a native Google-services deployment, distinct from Android Auto projection. Features depend on manufacturer, region, language, and data plan; it is not an available QNX backend here. |

### Ideas to borrow, with their tradeoffs

**Google Maps:** preserve fast destination selection and a clear comparison of
routes. Keep imagery as a planning aid, not a prerequisite for navigation.
Incognito disables several conveniences, including saved/offline maps, and does
not govern all other services' data use; “private mode” needs a precise promise.
[Incognito scope](https://support.google.com/maps/answer/9430563?hl=en).

**Apple Maps:** borrow continuity from preparation to driving to arrival, plus
controlled information density around maneuvers. Detailed city roads and Look
Around are useful orientation references, but coverage is selective. Apple says
Maps can be used without sign-in; synchronization and some contributions have
different account/data rules. Avoid equating privacy with no network traffic.
[City/imagery features](https://www.apple.com/maps/),
[Maps privacy](https://www.apple.com/legal/privacy/data/en/apple-maps/).

**Waze:** borrow quick, contextual hazard information and departure-time thinking.
Its 2024 release described roundabout help and advance warnings for decreasing
speed limits; treat that as dated feature provenance, not proof of universal
coverage. Community value also requires participants, fresh observations, and
moderation. Privacy controls distinguish public visibility, history, and account
deletion; contributed data may remain after being detached from an account.
[Release](https://blog.google/waze/6-updates-to-waze-to-help-you-get-around-safely-and-conveniently/),
[privacy controls](https://support.google.com/waze/answer/7680098?hl=en).

**HERE:** borrow offline resource planning and arrival context. Keep supplier
capabilities separate from consumer-app promises. The SDK's confidence-bearing
position model is especially useful inspiration: a navigator should know whether
it is showing a measured, matched, or predicted position.
[SDK positioning features](https://docs.here.com/here-sdk/docs/android-introduction-feature-list).

**TomTom:** borrow anticipation: what maneuver, delay, or service comes next.
Current GO Expert onboarding documents map downloads, voice-language resources,
and a consent tradeoff: routes remain available if information sharing is
declined, but TomTom services do not. That is a useful reminder to separate
local navigation from online enrichment. The older GO feature release is used
only for design lineage, not a current subscription or platform specification.
[GO Expert setup](https://help.tomtom.com/hc/en-gb/articles/31031674105362-Get-started-with-the-GO-Expert-app).

**Garmin:** borrow predictable controls for stop, detour/skip, guidance mute, and
services ahead. Guidance mute does not mute media or calls. This separation is
particularly relevant to the stock radio's shared audio ownership. Dedicated
device experience does not itself prove cluster, HUD, or vehicle-sensor access.
[Map tools](https://www8.garmin.com/manuals/webhelp/GUID-6E5DFD2E-EEE4-4EF3-89BC-AACD1B4936C7/EN-US/GUID-9DCBF296-FA93-4679-9B32-3EC299623D74.html).

**OsmAnd:** borrow explicit resource ownership, configurable road preferences,
and importable user data. Standard maps update monthly; Live Updates are
incremental OSM changes available under the documented subscription/contributor
conditions, not a live traffic feed. The extensive configuration surface creates
a design selection problem: expose the few decisions needed in the car and
leave advanced setup for stationary use. Its privacy policy documents map
download metadata and optional screen analytics; Cloud has separate account and
storage behavior. Do not inherit a “no telemetry” claim from “offline.”
[Map update model](https://osmand.net/docs/user/personal/maps-resources/),
[privacy policy, updated August 2026](https://osmand.net/docs/legal/privacy-policy/).

**Organic Maps:** borrow a usable local-first experience without account setup
and straightforward portable bookmarks/tracks. Its policy states that it does
not collect location, statistical, or personal data. This is the vendor's policy
statement, not an audit performed here. The August 2026 release adds CarPlay
dashboard support and lists Android Auto/Automotive fixes, illustrating why
projection behavior must be tracked by release rather than assumed equal.
[Privacy](https://organicmaps.app/privacy/),
[August 2026 release](https://organicmaps.app/news/2026-08-31/multi-selection-bookmarks-tracks-carplay-dashboard-hiding-tracks-share-links-august-2026/).

## Offline and failure behavior deserves its own comparison

| Product / evidence | What can be promised from these sources | Degradation / recovery boundary |
|---|---|---|
| Google phone maps (**D**) | Downloaded areas support driving within coverage. | No offline live traffic or alternatives; maps expire and need updates. [Offline help](https://support.google.com/maps/answer/6291838?hl=en) |
| Apple phone maps (**D**) | Downloaded areas support place information, ETA, and driving/walking/cycling/transit guidance in supported regions. | iOS 17+; download size/coverage and automatic updates matter. This does not promise live traffic offline. [Offline help](https://support.apple.com/en-ie/105084) |
| Waze (**D**) | The service expects connectivity; intermittent failures may prevent reliable live information. | Official help does not offer a full offline-routing contract; reports are not queued for later submission. Do not promise cached-route behavior as offline parity. [Service limits](https://support.google.com/waze/answer/6071177?hl=en) |
| HERE WeGo (**D**) | Download regions and select offline mode. | Help recommends ending/restarting navigation offline if online reconnection fails. [Offline setup](https://help.here.com/faq/offline_maps), [recovery](https://help.here.com/faq/routing_and_navigation) |
| TomTom SDK (**SDK**) | Distinguishes a local cache from provisioned onboard maps. | Documentation distinguishes continued active-route progress from new searches/routes; verify the selected version/configuration. [Map-data quickstart](https://docs.tomtom.com/navigation/android/guides/map-data/map-data-quickstart), [routing FAQ](https://docs.tomtom.com/navigation/android/faqs/faqs) |
| Garmin DriveSmart (**D**) | Maps live on the device and have a separate update workflow. | Live traffic still requires a data source; an update can need extra microSD storage. [Traffic](https://www8.garmin.com/manuals/webhelp/GUID-6E5DFD2E-EEE4-4EF3-89BC-AACD1B4936C7/EN-US/GUID-D13706CA-CFA3-45EF-8648-03BF4F9722B5.html), [updates](https://www8.garmin.com/manuals/webhelp/GUID-6E5DFD2E-EEE4-4EF3-89BC-AACD1B4936C7/EN-GB/GUID-AB01F4C7-F1B1-4169-BC92-A222FF8344EC.html) |
| OsmAnd (**D**) | Downloaded vector data supports local search and navigation. | Missing map/resource coverage must be resolved. Voice guidance depends on installed speech resources; GPS loss/recovery can be announced. [Search](https://osmand.net/docs/user/search/search-all/), [route preparation](https://osmand.net/docs/user/navigation/setup/route-navigation/), [speech](https://osmand.net/docs/user/navigation/guidance/voice-navigation/) |
| Organic Maps (**D**) | Downloaded maps support local search and voice navigation. | This evidence does not establish traffic freshness, tunnel prediction, or crash-resume behavior. [Features](https://organicmaps.app/) |
| Open NaviCore (**B/A/NE**) | Recorded provider lifecycle/GPS/rendering results and typed integration seams. | Fixed-viewport OsmAnd milestone; route persistence, offline rerouting, and end-to-end prompt recovery remain **NE**. [Status](../../README.md#current-status), [backend](../../backends/osmand/README.md#osmand-backend) |

Network loss and position loss are independent. Downloading a map does not
restore GNSS. Software prediction is also not a fresh measurement: TomTom
explicitly describes possible position pauses/jumps when tunnel estimates differ
from actual travel. Waze's beacon solution instead requires infrastructure and
Bluetooth. Neither establishes vehicle-sensor dead reckoning on this unit.
[TomTom dead reckoning](https://docs.tomtom.com/navigation/android/guides/navigation/map-matching),
[Waze beacon prerequisites](https://support.google.com/waze/partners/answer/9416071?hl=en).

## Stock VP4R/Uconnect evidence profile

### Family context is not unit qualification

The official **2018 Uconnect 4C/4C NAV 8.4-inch** manual is a family-level
comparison only. It is not a QR39 build identifier or proof of this unit's
regional entitlements. It documents address, POI, favorite, home, work, and
coordinate entry and adding destinations (printed pp. 91–111); north-up/2D/3D views, ETA,
turn list, repeat/mute guidance, and GPS information (pp. 112–114); map detail,
terrain, signposts, route options, and traffic-detour settings (pp. 119–124).
SiriusXM traffic/incident capabilities are equipment/service dependent
(pp. 133–134). These provide stock-experience questions to verify later, not
features to mark implemented or observed on the bench.
[Official family manual](https://vehicleinfo.mopar.com/assets/publications/en-us/Ram/2018/1500/9353.pdf).

### What the local evidence actually establishes

The following profile uses only bounded repository documentation and checked
contracts; no firmware tree was scanned. The observation matrices identify the
QR39.6 trace `stage1-v11-20261008-attempt1`. Their historical `current_mock`
columns describe that capture-era inventory, not necessarily today's emitted
callback set. Use current status and reviewed contracts for current replacement
claims. [Observation provenance](../../contracts/qr39/observations/README.md#qr39-runtime-observation-inputs),
[current boundary](../../README.md#current-status).

| Surface | Evidence and scope | Design consequence / remaining gap |
|---|---|---|
| Stock host and display lifecycle | **O/A:** stock HMI requests, screen identifiers for displays 0–2, and asynchronous replies are recorded. **B:** README reports replacement initialization, GLES rendering, GPS delivery, and Apps → Navigation hide/show on a stationary bench. [HMI trace](../../contracts/qr39/observations/callback-api-matrix.csv#L4), [bench summary](../../README.md#current-status) | Preserve tile, visibility, surface, and request/reply ownership. Three display IDs do not establish three usable navigation screens or a HUD. |
| Map renderer | **B/C:** README records the initial OsmAnd Kyiv-map bench milestone; backend documentation defines a fixed viewport that deliberately ignores GPS. [Roadmap result](../../README.md#roadmap), [backend milestone](../../backends/osmand/README.md#osmand-backend) | Current-position tracking, pan/follow behavior, route overlays, and navigation remain separate milestones. |
| GPS input | **A:** a copied `0x120`-byte record, signed latitude/longitude scaled by `1e7`, and raw fix/quality/heading/speed/satellite fields are recovered. Other units/masks remain unproven. [GPS record](../technical.md#provider-object-model) | Coordinates can seed the position model. Do not label raw accuracy, speed, or fix values until their semantics are established. |
| Map matching and dead reckoning | **O/A:** the GPS subscription requests a map-matched feedback interval; the documented observed value is 1000 ms. [Trace row](../../contracts/qr39/observations/provider-api-matrix.csv#L11), [scalar seam](../technical.md#typed-scalar-backend-seam) | A pacing request is not proof that useful matched positions were emitted. Tunnel performance and wheel/gyro fusion are **NE**. |
| Vehicle theme and speed | **O/A:** day/night status and setting arrive as bytes; vehicle speed uses a recovered soft-float call shape. [Trace rows](../../contracts/qr39/observations/provider-api-matrix.csv#L5), [scalar seam](../technical.md#typed-scalar-backend-seam) | Inputs exist for native adaptation. Actual theme switching and semantic interpretation must be demonstrated. |
| Localization and distraction | **O/A:** internationalization input and distraction status are observed. **C:** locale/time-format dictionaries are unknown; only distraction value `0` was observed. [Versioned seam](../technical.md#versioned-map-backend-seam) | Do not infer a named language or “safe to type” from an uninterpreted enum. Define behavior after mapping and validating these values. |
| Cluster/TBT | **A:** semantic builder covers route/navigation state, distances, ETA/RTA bytes, bounded road text, compass, and speed limits. Lane-data and route-symbol validity are rejected pending encoding evidence. **O/C:** one TBT enable value `1` was observed. [Cluster contract](../technical.md#instrument-cluster-backend-and-osmand-mapping), [enable input](../technical.md#versioned-map-backend-seam) | Useful native output seam; successful end-to-end route guidance, lane arrows, and HUD output are **NE**. A synthetic no-route publication is not a guided trip. |
| Audio and speech | **O/A:** source/volume requests and audio availability transport are documented. **NE:** audible route prompts, ducking/recovery across calls, recognition quality, and installed voices are not established by this boundary. [Audio trace](../../contracts/qr39/observations/provider-api-matrix.csv#L12), [outbound services](../technical.md#versioned-map-backend-seam) | Audio-source signaling and speech generation are separate capabilities. Plan a dedicated prompt pipeline and integration verification. |
| Voice-named interfaces | **A:** callback catalog corrects Voice 25/19 to `replyLocationGet`, despite a route-guidance timer caller name. [Callback catalog](../technical.md#reverse-callback-catalog-coverage) | Names cannot establish navigation semantics. A polling request does not prove voice input or spoken maneuver output. |
| Connectivity, traffic, parking, phone | **O:** connection/service/subscription calls occur in the trace. **A/NE:** service IDs/status sentinels are not fully interpreted; optional behavior is not claimed implemented. [Trace rows](../../contracts/qr39/observations/provider-api-matrix.csv#L24), [connectivity limits](../technical.md#versioned-map-backend-seam), [status caveat](../../README.md#current-status) | Do not show live traffic, parking availability, contact destinations, or named network strength merely because an adapter exists. |
| Search, saved places, routes, arrival, updates | **O:** navigation database version/status requests were captured. **NE:** this inventory does not demonstrate address/POI search, a computed route, waypoint editing, rerouting, arrival, map update installation, or restored trip state in the replacement. [Database requests](../../contracts/qr39/observations/provider-api-matrix.csv#L19), [ABI caveat](../technical.md#current-safety-boundary) | These require engine services, application state, and observable journeys. Version queries alone do not qualify offline map lifecycle. |
| Telemetry and retention | **A:** GPS logging is bounded and pointer storage is copied; lifecycle resets discard pending backend state. [GPS handling](../technical.md#provider-object-model), [lifecycle seam](../technical.md#versioned-map-backend-seam) | This is implementation evidence, not a complete privacy audit of the stock radio or connected services. Account dependence and full stock retention policy remain **NE**. |

The repository reports **140/140 provider calls** and **199/199 typed application
callbacks**, with **80 emitted and 119 dormant** at this snapshot. Those counts
qualify the recovered boundary, not 199 user features. Older prose under
`docs/technical.md`'s process-lifetime plugin section still describes pre-renderer
OsmAnd work; the newer backend README and main README record the rendering
milestone. This document uses the narrower fixed-viewport description and does
not reinterpret historical text as a new test.
[Current status](../../README.md#current-status),
[historical plugin section](../technical.md#process-lifetime-backend-plugin),
[current OsmAnd scope](../../backends/osmand/README.md#osmand-backend).

### Stock experience: advantage and constraint

The project can preserve the existing navigation tile, HMI lifecycle, and
vehicle integration rather than requiring the driver to start a phone app.
That is its strongest distinctive direction. It also inherits constrained
display/input/audio ownership and incomplete semantic dictionaries. The
versioned C seam must remain the boundary: C++/Qt/OsmAnd objects stay inside the
backend. The Android/iOS app's search controllers, TTS services, accounts, and
plugins are separate integration work.
[Architecture](../../README.md#architecture),
[backend ownership rules](../backend-sdk/README.md#backend-sdk).

## Recommended capability tiers

These are proposed capability gates (**I**), not dates or delivery promises.
Native ARMv7 QNX 6.6, GLES2, bounded memory, local OBF resources, and stock HMI
ownership constrain the recommendations. The platform is established in the
[build documentation](../../README.md#building); performance and memory budgets
still need measurement. No phone-app benchmark is substituted for target data.

| Tier | Capabilities to complete | Evidence of completion to ask for |
|---|---|---|
| **1. Foundation** | Stable map surface and lifecycle; reproducible map/style/font assets; pan/zoom and explicit follow state; position freshness; readable day/night; persistent local settings; resource/version status; basic localization/accessibility; bounded diagnostics. | Repeatable startup/hide/show and recovery; a position update visibly moves the correct map state; missing/corrupt resources and no-fix states are understandable; measured memory/frame behavior on target. Fixed Kyiv rendering is an input to this tier, not its completion. |
| **2. First usable navigator** | Offline address/POI/map-point destination entry; recents/favorites; route calculation with essential avoidances; distance/ETA; route overlay, maneuver and turn list; one intermediate stop; off-route recalculation; spoken prompts/repeat/mute/volume; arrival/end route; explicit suspend/restart and error behavior. | One coherent destination-to-arrival journey using real map/routing data; wrong turn, GNSS loss, coverage boundary, missing speech resource, and navigation-process restart have defined outcomes. Use deterministic recorded/simulated scenarios and bounded stationary-bench checks; no moving-vehicle experiment is implied. |
| **3. Competitive native navigator** | Route alternatives with reasons; editable multi-stop trips; along-route services and destination parking; lane/signpost/speed-limit data; verified cluster presentation; vehicle-aware theme/audio behavior; usable language/unit selection; recoverable map updates; optional fresh traffic and explained rerouting; destination import/handoff. | Center display, prompts, and cluster agree on route state; mapped OEM enums have evidence; stale traffic and unavailable lane data degrade cleanly; updates preserve a working resource set; long trips fit measured target budgets. Traffic depends on an identified provider and service terms. |
| **4. Advanced / optional** | Range-aware EV journeys; live charger/parking/fuel data; collaborative hazards; ETA/location sharing; terrain/satellite/street imagery; GPX recording/custom layers/plugins; walking/cycling/transit; tunnel prediction; HUD or new display integrations. | Each feature has a demonstrated user benefit, available data/rights, privacy behavior, resource budget, and target integration evidence. Each can fail or be disabled without losing baseline car navigation. |

The ordering follows the comparison: offline-first products demonstrate that a
complete local journey is viable; Garmin/TomTom show useful anticipation aids;
Google/Apple/Waze show the value and dependencies of connected services. The
current stock boundary makes native lifecycle, audio, and cluster integration a
better early differentiator than replicating a phone's discovery feed.

## Advanced references outside the initial automotive scope

| Capability | Relevant documented reference | Why it is optional here |
|---|---|---|
| Terrain, elevation, custom styles and raster layers | OsmAnd vector styles and topography/online-map plugins. [Styles](https://osmand.net/docs/user/map/vector-maps/), [plugins](https://osmand.net/docs/user/plugins/) | Valuable for outdoor travel; extra rendering/data load and visual density need a separate use case. |
| Satellite, street imagery, AR | Google navigation has satellite/3D options; Apple has Look Around; Google's Lens accessibility flow uses a phone camera. [Google map layers](https://support.google.com/maps/answer/3273406?hl=en), [Apple imagery](https://www.apple.com/maps/), [Lens accessibility](https://support.google.com/accessibility/answer/6396990?hl=en) | Coverage, licensing, connectivity, and camera/display requirements are distinct from the base vector map. Phone AR is not a recovered radio capability. |
| GPX, portable tracks, custom layers/plugins | Organic Maps imports/exports bookmarks/tracks; OsmAnd offers track navigation and extension mechanisms. [Organic Maps formats](https://organicmaps.app/), [OsmAnd navigation](https://osmand.net/docs/user/navigation/), [plugins](https://osmand.net/docs/user/plugins/) | Destination/trip import can precede recording or a plugin ecosystem. Native backend plugins and OsmAnd mobile plugins are different interfaces. |
| Walking, cycling, public transit | Apple documents offline modes in supported regions; OsmAnd documents separate routing profiles. [Apple offline](https://support.apple.com/en-ie/105084), [OsmAnd profiles](https://osmand.net/docs/user/navigation/) | A head unit's primary user remains in the vehicle. A future phone handoff may serve the final walking segment better than extending the in-car interaction model. |

## Principles and questions for future design mockups

Design the states and decisions before selecting a visual style. These
recommendations do not prescribe a new UI or assume that every stock control can
be changed.

1. **Keep location, camera, and route separate.** A driver can browse while a
   route continues. Recenter must not imply recalculation. Predicted or stale
   position must not look like a fresh fix.
2. **Make the next driving decision dominant.** Maneuver, distance, lane/exit,
   and street compete with optional POIs and imagery. Missing lane or signpost
   data must have a deliberate fallback.
3. **Explain consequential changes.** A route changed because of a missed turn,
   preference, closure, or estimated time saving should not appear arbitrary.
   ETA needs a clear distinction between traffic-informed and local estimates.
4. **Make offline readiness actionable.** Show the missing region, voice, or
   resource and whether the current trip can proceed. A network error should
   not erase a valid local route.
5. **Preserve native consistency.** Center display, cluster, audio focus,
   day/night, language, and units should agree once the underlying semantics
   have been established. Do not expose raw ABI identifiers in product flows.
6. **Offer useful defaults with reversible choices.** Prioritize car navigation;
   keep detailed map/routing/plugin configuration out of the active driving
   sequence. Local history and optional sharing need clear deletion/stop paths.

Questions the mockups should make reviewable:

- Which surfaces are renderer-owned versus fixed stock HMI? What exact touch,
  key/rotary, and voice actions are available on this unit?
- What information persists when the user browses away, opens another radio
  app, mutes prompts, loses GNSS, or restarts navigation?
- Which initial countries, languages, address formats, units, and maps must
  work? What is the acceptable fallback when an address is missing from OSM?
- Should the first route planner expose one simple avoidance set or named
  driving profiles? When is an alternative meaningful enough to present?
- How should unavailable traffic, uncertain position, missing lane data, and
  pending map updates be communicated without obscuring the next maneuver?
- Which cluster fields can be reviewed now, and which need encoding recovery?
  Is HUD integration actually in scope for this hardware?
- What is the offline voice strategy on QNX, and how should prompts interact
  with calls, music, volume changes, and unavailable audio output?
- What destination handoff is useful without requiring a proprietary account
  or a full companion app? Which optional data services have an owner?

Useful review scenarios are a first launch with no maps; a normal local trip;
an ambiguous search result; a complex interchange; browsing during guidance;
a missed turn; a tunnel/no-fix interval; a coverage boundary; a call interrupting
a prompt; arrival at parking rather than the building; and resuming after a
process restart. Mockups should show the outcome for each, not only a populated
map in ideal conditions.

## Evidence gaps and maintenance

- No comparative route quality, ETA error, search success, distraction, boot
  time, frame rate, memory use, or accessibility audit was performed. These
  require representative regional data and target measurements.
- The reviewed local evidence does not qualify stock or replacement
  destination-to-arrival behavior, live traffic entitlement, dead reckoning,
  speech, actual cluster guidance, map-update installation, or HUD output.
- The Uconnect family manual is historical and US-market documentation; it
  must not stand in for QR39/region/model-specific observation.
- SDK references explain capability/dependency models; no commercial SDK is
  selected, licensed, or qualified for QNX here. OsmAnd mobile feature availability
  does not establish which components are present in this port.
- Product plans, projection limitations, service coverage, and policies can
  change. Recheck the linked official pages when implementing a dependent
  feature. Retain this access date and add new evidence rather than silently
  promoting **NE**, **C**, or **A** into observed behavior.

The optional machine-readable companion is deliberately a small set of design
signals rather than a yes/no checklist:
[navigation-feature-matrix.csv](navigation-feature-matrix.csv).
Its schema is one evidence-backed observation per row: `id`, `product`,
`scope`, `capability`, `finding`, `evidence_class`, `source_url_or_repo_path`,
`accessed_or_reviewed`, `suggested_tier`, and `design_implication`.
External paths are direct URLs; local paths are repository-root-relative.
Evidence class describes the finding; tier and implication are always **I**.
Rows are not a complete feature inventory and should not be counted as scores.
