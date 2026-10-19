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
    OPEN_NAVI_CLUSTER_ROUTE_SYMBOL | OPEN_NAVI_CLUSTER_DISTANCE_DESTINATION | OPEN_NAVI_CLUSTER_ETA |                 \
    OPEN_NAVI_CLUSTER_DISTANCE_MANEUVER | OPEN_NAVI_CLUSTER_CURRENT_ROAD |           \
    OPEN_NAVI_CLUSTER_LANE_ACTIVE | OPEN_NAVI_CLUSTER_NEXT_ROAD |                    \
    OPEN_NAVI_CLUSTER_BAR_PERCENT | OPEN_NAVI_CLUSTER_COMPASS |                      \
    OPEN_NAVI_CLUSTER_SPEED_LIMIT | OPEN_NAVI_CLUSTER_RTA | OPEN_NAVI_CLUSTER_LEGAL_LIMIT))

/* Route-guidance symbol (+0x003, valid bit 2): the picture of the cluster's
 * turn view. Live table, 2026-10-19 bench (analysis Cluster_TBT_Symbols.md):
 * the cluster uses symbol & 0x3f; 1..61 are pictures, 0 leaves the
 * navigation page and 62/63 draw nothing, so the semantic API accepts
 * 1..61 only. The turn view is drawn only while bit 2 and bar-fill bit 10
 * are both valid. RHT = right-hand traffic (counter-clockwise roundabout),
 * LHT = left-hand traffic. Not every picture has a name here. */
enum OpenNaviClusterRouteSymbol {
    OPEN_NAVI_CLUSTER_SYMBOL_MIN = 1,
    OPEN_NAVI_CLUSTER_SYMBOL_LEFT = 1,
    OPEN_NAVI_CLUSTER_SYMBOL_RIGHT = 2,
    OPEN_NAVI_CLUSTER_SYMBOL_SLIGHT_LEFT = 3,
    OPEN_NAVI_CLUSTER_SYMBOL_SLIGHT_RIGHT = 4,
    OPEN_NAVI_CLUSTER_SYMBOL_SHARP_LEFT = 5,
    OPEN_NAVI_CLUSTER_SYMBOL_SHARP_RIGHT = 6,
    OPEN_NAVI_CLUSTER_SYMBOL_FORK_LEFT = 7,
    OPEN_NAVI_CLUSTER_SYMBOL_FORK_RIGHT = 8,
    OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT = 9,
    OPEN_NAVI_CLUSTER_SYMBOL_U_TURN_LEFT = 10,
    OPEN_NAVI_CLUSTER_SYMBOL_EXIT_LEFT = 11,
    OPEN_NAVI_CLUSTER_SYMBOL_EXIT_RIGHT = 12,
    OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT_PAST_LEFT = 13,
    OPEN_NAVI_CLUSTER_SYMBOL_STRAIGHT_PAST_RIGHT = 14,
    OPEN_NAVI_CLUSTER_SYMBOL_APPROACHING_DESTINATION = 15,
    OPEN_NAVI_CLUSTER_SYMBOL_RECALCULATING = 16,
    OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_12 = 19,   /* straight */
    OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_U = 20,    /* U-turn */
    OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_3 = 21,    /* right */
    OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_9 = 22,    /* left */
    OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_1 = 23,
    OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_11 = 24,
    OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_4 = 25,
    OPEN_NAVI_CLUSTER_SYMBOL_RHT_ROUNDABOUT_7 = 26,
    OPEN_NAVI_CLUSTER_SYMBOL_DESTINATION_REACHED = 27,
    OPEN_NAVI_CLUSTER_SYMBOL_U_TURN_RIGHT = 28,
    OPEN_NAVI_CLUSTER_SYMBOL_LHT_ROUNDABOUT_12 = 36,   /* same order as RHT */
    OPEN_NAVI_CLUSTER_SYMBOL_LHT_ROUNDABOUT_U = 37,
    OPEN_NAVI_CLUSTER_SYMBOL_LHT_ROUNDABOUT_3 = 38,
    OPEN_NAVI_CLUSTER_SYMBOL_LHT_ROUNDABOUT_9 = 39,
    OPEN_NAVI_CLUSTER_SYMBOL_LHT_ROUNDABOUT_1 = 40,
    OPEN_NAVI_CLUSTER_SYMBOL_LHT_ROUNDABOUT_11 = 41,
    OPEN_NAVI_CLUSTER_SYMBOL_LHT_ROUNDABOUT_4 = 42,
    OPEN_NAVI_CLUSTER_SYMBOL_LHT_ROUNDABOUT_7 = 43,
    OPEN_NAVI_CLUSTER_SYMBOL_T_TURN_RIGHT = 49,        /* road joins from right */
    OPEN_NAVI_CLUSTER_SYMBOL_T_TURN_LEFT = 50,         /* road joins from left */
    OPEN_NAVI_CLUSTER_SYMBOL_DIVIDED_U_TURN_LEFT = 51,
    OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_RIGHT = 53,
    OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_STRAIGHT = 54,
    OPEN_NAVI_CLUSTER_SYMBOL_SPLIT_LEFT = 55,
    OPEN_NAVI_CLUSTER_SYMBOL_CROSSING_STRAIGHT = 56,
    OPEN_NAVI_CLUSTER_SYMBOL_DIVIDED_U_TURN_RIGHT = 57,
    OPEN_NAVI_CLUSTER_SYMBOL_T_END_LEFT = 60,          /* road ends */
    OPEN_NAVI_CLUSTER_SYMBOL_T_END_RIGHT = 61,
    OPEN_NAVI_CLUSTER_SYMBOL_MAX = 61
};

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
    /* Appended: route-guidance symbol, 1..61 when bit 2 is valid. */
    uint8_t route_guidance_symbol;
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
