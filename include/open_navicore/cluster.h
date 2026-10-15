#ifndef OPEN_NAVI_CLUSTER_BACKEND_H
#define OPEN_NAVI_CLUSTER_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define OPEN_NAVI_CLUSTER_EXPORT __attribute__((visibility("default")))
#else
#define OPEN_NAVI_CLUSTER_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OPEN_NAVI_CLUSTER_RECORD_SIZE = 0x0a5c,
    OPEN_NAVI_CLUSTER_DISTANCE_CODE_UNITS = 8,
    OPEN_NAVI_CLUSTER_ROAD_CODE_UNITS = 255,
    OPEN_NAVI_CLUSTER_ROAD_MAX_SCALARS = 127,
    OPEN_NAVI_CLUSTER_ROAD_MAX_UTF16_UNITS = 127,
    OPEN_NAVI_CLUSTER_LANE_CAPACITY = 255
};

enum OpenNaviClusterAttributeMask {
    OPEN_NAVI_CLUSTER_ROUTE_GUIDANCE_STATE = 1u << 0,
    OPEN_NAVI_CLUSTER_NAVIGATION_STATUS = 1u << 1,
    OPEN_NAVI_CLUSTER_ROUTE_SYMBOL = 1u << 2,
    OPEN_NAVI_CLUSTER_DISTANCE_DESTINATION = 1u << 3,
    OPEN_NAVI_CLUSTER_ETA = 1u << 4,
    OPEN_NAVI_CLUSTER_DISTANCE_MANEUVER = 1u << 5,
    OPEN_NAVI_CLUSTER_CURRENT_ROAD = 1u << 6,
    OPEN_NAVI_CLUSTER_LANE = 1u << 7,
    OPEN_NAVI_CLUSTER_LANE_ACTIVE = 1u << 8,
    OPEN_NAVI_CLUSTER_NEXT_ROAD = 1u << 9,
    OPEN_NAVI_CLUSTER_BAR_PERCENT = 1u << 10,
    OPEN_NAVI_CLUSTER_COMPASS = 1u << 11,
    OPEN_NAVI_CLUSTER_UNKNOWN_12 = 1u << 12,
    OPEN_NAVI_CLUSTER_SPEED_LIMIT = 1u << 13,
    OPEN_NAVI_CLUSTER_RTA = 1u << 14,
    OPEN_NAVI_CLUSTER_LEGAL_LIMIT = 1u << 15
};

#define OPEN_NAVI_CLUSTER_SEMANTIC_SUPPORTED_MASK ((uint16_t)(                  \
    OPEN_NAVI_CLUSTER_ROUTE_GUIDANCE_STATE | OPEN_NAVI_CLUSTER_NAVIGATION_STATUS |   \
    OPEN_NAVI_CLUSTER_DISTANCE_DESTINATION | OPEN_NAVI_CLUSTER_ETA |                 \
    OPEN_NAVI_CLUSTER_DISTANCE_MANEUVER | OPEN_NAVI_CLUSTER_CURRENT_ROAD |           \
    OPEN_NAVI_CLUSTER_LANE_ACTIVE | OPEN_NAVI_CLUSTER_NEXT_ROAD |                    \
    OPEN_NAVI_CLUSTER_BAR_PERCENT | OPEN_NAVI_CLUSTER_COMPASS |                      \
    OPEN_NAVI_CLUSTER_SPEED_LIMIT | OPEN_NAVI_CLUSTER_RTA | OPEN_NAVI_CLUSTER_LEGAL_LIMIT))

enum OpenNaviClusterRouteGuidanceState {
    OPEN_NAVI_CLUSTER_ROUTE_FOLLOW_STREET = 1,
    OPEN_NAVI_CLUSTER_ROUTE_MANOEUVRE_ANNOUNCEMENT = 2,
    OPEN_NAVI_CLUSTER_ROUTE_MANOEUVRE = 3,
    OPEN_NAVI_CLUSTER_ROUTE_DIRECTION_OF_DESTINATION = 4,
    OPEN_NAVI_CLUSTER_ROUTE_RECALCULATION = 5,
    OPEN_NAVI_CLUSTER_ROUTE_DIRECTION_OF_TRAVEL = 6,
    OPEN_NAVI_CLUSTER_ROUTE_DESTINATION_REACHED = 7
};

enum OpenNaviClusterNavigationStatus {
    OPEN_NAVI_CLUSTER_NAV_OFF_ROAD = 1,
    OPEN_NAVI_CLUSTER_NAV_OFF_MAP = 2,
    OPEN_NAVI_CLUSTER_NAV_NO_ROUTE = 3,
    OPEN_NAVI_CLUSTER_NAV_ROUTE_CALCULATION = 4,
    OPEN_NAVI_CLUSTER_NAV_DESTINATION_AREA = 5,
    OPEN_NAVI_CLUSTER_NAV_INITIALIZING = 6,
    OPEN_NAVI_CLUSTER_NAV_NO_DATA = 7,
    OPEN_NAVI_CLUSTER_NAV_DIRECTION_NOT_AVAILABLE = 8
};

enum OpenNaviClusterDistanceUnit {
    OPEN_NAVI_CLUSTER_DISTANCE_KILOMETRES = 1,
    OPEN_NAVI_CLUSTER_DISTANCE_METRES = 2,
    OPEN_NAVI_CLUSTER_DISTANCE_MILES = 3,
    OPEN_NAVI_CLUSTER_DISTANCE_FEET = 4,
    OPEN_NAVI_CLUSTER_DISTANCE_YARDS = 5
};

enum OpenNaviClusterCompassDirection {
    OPEN_NAVI_CLUSTER_COMPASS_N = 1,
    OPEN_NAVI_CLUSTER_COMPASS_NE = 2,
    OPEN_NAVI_CLUSTER_COMPASS_E = 3,
    OPEN_NAVI_CLUSTER_COMPASS_SE = 4,
    OPEN_NAVI_CLUSTER_COMPASS_S = 5,
    OPEN_NAVI_CLUSTER_COMPASS_SW = 6,
    OPEN_NAVI_CLUSTER_COMPASS_W = 7,
    OPEN_NAVI_CLUSTER_COMPASS_NW = 8
};

enum OpenNaviClusterSpeedUnit {
    OPEN_NAVI_CLUSTER_SPEED_KMH = 1,
    OPEN_NAVI_CLUSTER_SPEED_MPH = 2
};

typedef enum {
    OPEN_NAVI_CLUSTER_TEXT_UTF8 = 0,
    OPEN_NAVI_CLUSTER_TEXT_ASCII = 1
} OpenNaviClusterTextEncoding;

typedef enum {
    OPEN_NAVI_CLUSTER_OK = 0,
    OPEN_NAVI_CLUSTER_INVALID_ARGUMENT = 0xffff0001u,
    OPEN_NAVI_CLUSTER_INVALID_VALUE = 0xffff0002u,
    OPEN_NAVI_CLUSTER_INVALID_TEXT = 0xffff0003u,
    OPEN_NAVI_CLUSTER_TEXT_TOO_LONG = 0xffff0004u,
    OPEN_NAVI_CLUSTER_UNSUPPORTED_FIELD = 0xffff0005u
} OpenNaviClusterStatus;

typedef struct {
    const char *bytes;
    size_t length;
    OpenNaviClusterTextEncoding encoding;
} OpenNaviClusterText;

typedef struct {
    uint16_t valid_attributes;
    uint8_t route_guidance_state;
    uint8_t navigation_status;
    uint32_t distance_destination;          /* integer, must be <= 65535 */
    uint8_t distance_destination_unit;
    uint8_t eta_hour;
    uint8_t eta_minute;
    uint8_t rta_hour;
    uint8_t rta_minute;
    uint32_t distance_maneuver_tenths;      /* tenths, must be <= 65535 */
    uint8_t distance_maneuver_unit;
    OpenNaviClusterText current_road;
    uint32_t lane_active;
    OpenNaviClusterText next_road;
    uint8_t bar_percent;
    uint8_t compass;
    uint8_t speed_limit;
    uint8_t speed_unit;
    uint8_t legal_speed_limit;
    uint8_t legal_speed_unit;
} OpenNaviClusterNavigationState;

#pragma pack(push, 1)
typedef struct {
    uint8_t route_guidance_state;          /* 0x000 */
    uint8_t navigation_status;             /* 0x001 */
    uint8_t unknown_002;                   /* 0x002 */
    uint8_t route_guidance_symbol;         /* 0x003 */
    uint8_t opaque_004_103[0x100];         /* 0x004 */
    uint8_t distance_destination[0x20];     /* 0x104: LE UTF-32 */
    uint8_t distance_destination_unit;     /* 0x124 */
    uint8_t pad_125_127[3];                /* 0x125 */
    uint8_t eta_hour;                      /* 0x128 */
    uint8_t eta_minute;                    /* 0x129 */
    uint8_t rta_hour;                      /* 0x12a */
    uint8_t rta_minute;                    /* 0x12b */
    uint8_t distance_maneuver[0x20];        /* 0x12c: LE UTF-32 */
    uint8_t distance_maneuver_unit;        /* 0x14c */
    uint8_t pad_14d_14f[3];                /* 0x14d */
    uint8_t current_road[0x3fc];            /* 0x150: LE UTF-32 */
    uint8_t lane_length;                   /* 0x54c */
    uint8_t lane[0xff];                    /* 0x54d */
    uint32_t lane_active;                  /* 0x64c */
    uint8_t next_road[0x3fc];               /* 0x650: LE UTF-32 */
    uint8_t bar_percent;                   /* 0xa4c */
    uint8_t compass;                       /* 0xa4d */
    uint8_t reserved_a4e_a53[6];           /* 0xa4e */
    uint8_t speed_limit;                   /* 0xa54 */
    uint8_t speed_unit;                    /* 0xa55 */
    uint16_t changed_attributes;           /* 0xa56 */
    uint16_t valid_attributes;             /* 0xa58 */
    uint8_t legal_speed_limit;             /* 0xa5a */
    uint8_t legal_speed_unit;              /* 0xa5b */
} OpenNaviClusterDisplayRecord;
#pragma pack(pop)

/* Serialize one canonical semantic record without changing publication
 * history.  Valid and changed masks may contain only
 * OPEN_NAVI_CLUSTER_SEMANTIC_SUPPORTED_MASK bits. */
OPEN_NAVI_CLUSTER_EXPORT uint32_t open_navi_cluster_backend_serialize(
    const OpenNaviClusterNavigationState *state,
    uint16_t changed_attributes,
    uint8_t record_out[OPEN_NAVI_CLUSTER_RECORD_SIZE]);

/* Build, calculate changed_attributes, synchronously publish, and copy state
 * only after the receiver returns success.  Validation errors use the
 * OPEN_NAVI_CLUSTER_* status range; otherwise the receiver status is returned. */
OPEN_NAVI_CLUSTER_EXPORT uint32_t open_navi_cluster_backend_publish(
    const OpenNaviClusterNavigationState *state);
OPEN_NAVI_CLUSTER_EXPORT void open_navi_cluster_backend_reset(void);

#define OPEN_NAVI_CLUSTER_OFFSET_ASSERT(field, offset) \
    typedef char open_navi_cluster_offset_##field[                         \
        offsetof(OpenNaviClusterDisplayRecord, field) == (offset) ? 1 : -1]
OPEN_NAVI_CLUSTER_OFFSET_ASSERT(distance_destination, 0x104);
OPEN_NAVI_CLUSTER_OFFSET_ASSERT(distance_maneuver, 0x12c);
OPEN_NAVI_CLUSTER_OFFSET_ASSERT(current_road, 0x150);
OPEN_NAVI_CLUSTER_OFFSET_ASSERT(lane_length, 0x54c);
OPEN_NAVI_CLUSTER_OFFSET_ASSERT(lane_active, 0x64c);
OPEN_NAVI_CLUSTER_OFFSET_ASSERT(next_road, 0x650);
OPEN_NAVI_CLUSTER_OFFSET_ASSERT(changed_attributes, 0xa56);
OPEN_NAVI_CLUSTER_OFFSET_ASSERT(valid_attributes, 0xa58);
typedef char open_navi_cluster_record_size[
    sizeof(OpenNaviClusterDisplayRecord) == OPEN_NAVI_CLUSTER_RECORD_SIZE ? 1 : -1];
#undef OPEN_NAVI_CLUSTER_OFFSET_ASSERT

#ifdef __cplusplus
}
#endif

#endif
