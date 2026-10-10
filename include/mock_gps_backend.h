#ifndef MOCK_GPS_BACKEND_H
#define MOCK_GPS_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MOCK_GPS_RECORD_SIZE = 0x120,
    MOCK_GPS_ALTITUDE_RAW_OFFSET = 0x00,
    MOCK_GPS_FIX_TYPE_OFFSET = 0x03,
    MOCK_GPS_HEADING_RAW_OFFSET = 0x04,
    MOCK_GPS_HDOP_RAW_OFFSET = 0x10,
    MOCK_GPS_ACCURACY_RAW_OFFSET = 0x14,
    MOCK_GPS_LATITUDE_E7_OFFSET = 0x1c,
    MOCK_GPS_LONGITUDE_E7_OFFSET = 0x20,
    MOCK_GPS_SATS_USED_OFFSET = 0x26,
    MOCK_GPS_SATS_IN_VIEW_OFFSET = 0x27,
    MOCK_GPS_PDOP_RAW_OFFSET = 0x28,
    MOCK_GPS_SATELLITES_OFFSET = 0x2c,
    MOCK_GPS_SATELLITE_COUNT = 16,
    MOCK_GPS_SATELLITE_SIZE = 12,
    MOCK_GPS_SPEED_RAW_MASKED_OFFSET = 0xf2,
    MOCK_GPS_VDOP_RAW_OFFSET = 0xf8,
    MOCK_GPS_QUALITY_STATUS_OFFSET = 0xfa,
    MOCK_GPS_SYNC_COUNTER_OFFSET = 0xfb,
    MOCK_GPS_ELLIPSOID_ALTITUDE_RAW_OFFSET = 0xfe,
    MOCK_GPS_UNKNOWN_100_OFFSET = 0x100,
    MOCK_GPS_TRAVEL_DISTANCE_RAW_OFFSET = 0x108,
    MOCK_GPS_ALTITUDE_CHANGE_RAW_OFFSET = 0x110,
    MOCK_GPS_NOISE_RAW_OFFSET = 0x118
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
    uint8_t satellites[MOCK_GPS_SATELLITE_COUNT][MOCK_GPS_SATELLITE_SIZE];
    uint16_t speed_raw_masked;
    uint16_t vdop_raw;
    uint8_t quality_status;
    uint8_t sync_counter;
    uint16_t ellipsoid_altitude_raw;
    uint16_t unknown_100;
    float travel_distance_raw;
    double altitude_change_raw;
    double noise_raw;
} MockGpsSnapshot;

void mock_gps_snapshot_parse(MockGpsSnapshot *snapshot,
                             const uint8_t record[MOCK_GPS_RECORD_SIZE]);
void mock_gps_backend_submit(const MockGpsSnapshot *snapshot);
void mock_gps_backend_reset(void);

#if defined(MOCK_NAVICORE_TESTING)
uint32_t mock_gps_backend_test_generation(void);
int mock_gps_backend_test_latest(MockGpsSnapshot *snapshot_out);
#endif

#ifdef __cplusplus
}
#endif

#endif
