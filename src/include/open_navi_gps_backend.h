#ifndef OPEN_NAVI_GPS_BACKEND_H
#define OPEN_NAVI_GPS_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OPEN_NAVI_GPS_RECORD_SIZE = 0x120,
    OPEN_NAVI_GPS_ALTITUDE_RAW_OFFSET = 0x00,
    OPEN_NAVI_GPS_FIX_TYPE_OFFSET = 0x03,
    OPEN_NAVI_GPS_HEADING_RAW_OFFSET = 0x04,
    OPEN_NAVI_GPS_HDOP_RAW_OFFSET = 0x10,
    OPEN_NAVI_GPS_ACCURACY_RAW_OFFSET = 0x14,
    OPEN_NAVI_GPS_LATITUDE_E7_OFFSET = 0x1c,
    OPEN_NAVI_GPS_LONGITUDE_E7_OFFSET = 0x20,
    OPEN_NAVI_GPS_SATS_USED_OFFSET = 0x26,
    OPEN_NAVI_GPS_SATS_IN_VIEW_OFFSET = 0x27,
    OPEN_NAVI_GPS_PDOP_RAW_OFFSET = 0x28,
    OPEN_NAVI_GPS_SATELLITES_OFFSET = 0x2c,
    OPEN_NAVI_GPS_SATELLITE_COUNT = 16,
    OPEN_NAVI_GPS_SATELLITE_SIZE = 12,
    OPEN_NAVI_GPS_SPEED_RAW_MASKED_OFFSET = 0xf2,
    OPEN_NAVI_GPS_VDOP_RAW_OFFSET = 0xf8,
    OPEN_NAVI_GPS_QUALITY_STATUS_OFFSET = 0xfa,
    OPEN_NAVI_GPS_SYNC_COUNTER_OFFSET = 0xfb,
    OPEN_NAVI_GPS_ELLIPSOID_ALTITUDE_RAW_OFFSET = 0xfe,
    OPEN_NAVI_GPS_UNKNOWN_100_OFFSET = 0x100,
    OPEN_NAVI_GPS_TRAVEL_DISTANCE_RAW_OFFSET = 0x108,
    OPEN_NAVI_GPS_ALTITUDE_CHANGE_RAW_OFFSET = 0x110,
    OPEN_NAVI_GPS_NOISE_RAW_OFFSET = 0x118
};

typedef struct {
    uint16_t altitude_raw;
    uint8_t fix_type;
    uint16_t heading_raw;
    uint16_t hdop_raw;
    uint32_t accuracy_raw;
    int32_t latitude_e7;
    int32_t longitude_e7;
    uint8_t sats_used;
    uint8_t sats_in_view;
    uint16_t pdop_raw;
    uint8_t satellites[OPEN_NAVI_GPS_SATELLITE_COUNT][OPEN_NAVI_GPS_SATELLITE_SIZE];
    uint16_t speed_raw_masked;
    uint16_t vdop_raw;
    uint8_t quality_status;
    uint8_t sync_counter;
    uint16_t ellipsoid_altitude_raw;
    uint16_t unknown_100;
    float travel_distance_raw;
    double altitude_change_raw;
    double noise_raw;
} OpenNaviGpsSnapshot;

void open_navi_gps_snapshot_parse(OpenNaviGpsSnapshot *snapshot,
                             const uint8_t record[OPEN_NAVI_GPS_RECORD_SIZE]);
void open_navi_gps_backend_submit(const OpenNaviGpsSnapshot *snapshot);
void open_navi_gps_backend_reset(void);

#if defined(OPEN_NAVI_TESTING)
uint32_t open_navi_gps_backend_test_generation(void);
int open_navi_gps_backend_test_latest(OpenNaviGpsSnapshot *snapshot_out);
#endif

#ifdef __cplusplus
}
#endif

#endif
