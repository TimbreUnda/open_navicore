# OsmAnd backend

This optional backend renders OsmAnd map data into the EGL surface owned by
Open NaviCore. It is intentionally kept behind the stable C plugin ABI so the
QNX/C++ runtime used by OsmAnd never crosses into the proprietary provider
boundary.

The first milestone uses a fixed Kyiv viewport and deliberately ignores live
GPS updates. Position tracking belongs to the next navigation-state milestone.
Map data and the OsmAnd binary/resource bundles are runtime assets and are not
committed to this repository.

## Build

Set these variables to the corresponding outputs of an ARMv7 QNX OsmAnd build:

- `OSMAND_QNX_ROOT`: build workspace containing `source`, `gcc-out`, and the
  PIC Qt prefix;
- `OSMAND_CORE_LIB_DIR`: directory containing `libOsmAndCore_shared.so`;
- `OSMAND_QT_PREFIX`: PIC Qt installation used to build OsmAnd.

Then run:

```sh
make osmand-backend \
  QNX_SDK=/path/to/qnx660-sdk \
  QNX_HOST_LIBS=/path/to/qnx-host-libs \
  OSMAND_QNX_ROOT=/path/to/osmand-qnx-build
```

No workstation path is compiled into the plugin. On the target, the defaults
are:

| Asset | Target path |
|---|---|
| backend | `/datastore/navicore-backend/libMockNavBackendOsmAnd.so` |
| core | `/datastore/navicore-backend/libOsmAndCore_shared.so` |
| resources | `/datastore/navicore-backend/libOsmAndCore_ResourcesBundle_shared.so` |
| map | `/datastore/navicore-backend/maps/kyiv.obf` |
| fonts | `/datastore/navicore-backend/fonts/` |

The resources, map, and font paths can be overridden for a one-shot test with
`OPEN_NAVICORE_OSMAND_RESOURCES`, `OPEN_NAVICORE_OSMAND_MAP`, and
`OPEN_NAVICORE_OSMAND_FONTS` in the navigation process environment.

The backend carries a QNX RPATH only for the fixed target asset directory
`/datastore/navicore-backend`; it never embeds a workstation or SDK path.
