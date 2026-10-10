#include "mock_gps_backend.h"
#include "mock_counter.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t g_backend_lock = PTHREAD_MUTEX_INITIALIZER;
static MockGpsSnapshot g_latest_snapshot;
static uint32_t g_generation;
static int g_has_snapshot;

typedef char satellite_block_must_end_at_0xec[
    MOCK_GPS_SATELLITES_OFFSET +
            MOCK_GPS_SATELLITE_COUNT * MOCK_GPS_SATELLITE_SIZE == 0xec
        ? 1 : -1];
typedef char final_double_must_end_at_record_size[
    MOCK_GPS_NOISE_RAW_OFFSET + 8 == MOCK_GPS_RECORD_SIZE ? 1 : -1];

static uint16_t read_le16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8));
}

static uint32_t read_le32(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static uint64_t read_le64(const uint8_t *source)
{
    return (uint64_t)read_le32(source) |
           ((uint64_t)read_le32(source + 4) << 32);
}

static int32_t read_les32(const uint8_t *source)
{
    uint32_t bits = read_le32(source);
    int32_t value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

void mock_gps_snapshot_parse(MockGpsSnapshot *snapshot,
                             const uint8_t record[MOCK_GPS_RECORD_SIZE])
{
    uint32_t float_bits;
    uint64_t double_bits;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->altitude_raw =
        read_le16(record + MOCK_GPS_ALTITUDE_RAW_OFFSET);
    snapshot->fix_type = record[MOCK_GPS_FIX_TYPE_OFFSET];
    snapshot->heading_raw =
        read_le16(record + MOCK_GPS_HEADING_RAW_OFFSET);
    snapshot->hdop_raw = read_le16(record + MOCK_GPS_HDOP_RAW_OFFSET);
    snapshot->accuracy_raw = read_le32(record + MOCK_GPS_ACCURACY_RAW_OFFSET);
    snapshot->latitude_e7 = read_les32(record + MOCK_GPS_LATITUDE_E7_OFFSET);
    snapshot->longitude_e7 =
        read_les32(record + MOCK_GPS_LONGITUDE_E7_OFFSET);
    snapshot->sats_used = record[MOCK_GPS_SATS_USED_OFFSET];
    snapshot->sats_in_view = record[MOCK_GPS_SATS_IN_VIEW_OFFSET];
    snapshot->pdop_raw = read_le16(record + MOCK_GPS_PDOP_RAW_OFFSET);
    memcpy(snapshot->satellites, record + MOCK_GPS_SATELLITES_OFFSET,
           sizeof(snapshot->satellites));
    snapshot->speed_raw_masked =
        read_le16(record + MOCK_GPS_SPEED_RAW_MASKED_OFFSET);
    snapshot->vdop_raw = read_le16(record + MOCK_GPS_VDOP_RAW_OFFSET);
    snapshot->quality_status = record[MOCK_GPS_QUALITY_STATUS_OFFSET];
    snapshot->sync_counter = record[MOCK_GPS_SYNC_COUNTER_OFFSET];
    snapshot->ellipsoid_altitude_raw =
        read_le16(record + MOCK_GPS_ELLIPSOID_ALTITUDE_RAW_OFFSET);
    snapshot->unknown_100 = read_le16(record + MOCK_GPS_UNKNOWN_100_OFFSET);

    float_bits = read_le32(record + MOCK_GPS_TRAVEL_DISTANCE_RAW_OFFSET);
    memcpy(&snapshot->travel_distance_raw, &float_bits,
           sizeof(snapshot->travel_distance_raw));
    double_bits = read_le64(record + MOCK_GPS_ALTITUDE_CHANGE_RAW_OFFSET);
    memcpy(&snapshot->altitude_change_raw, &double_bits,
           sizeof(snapshot->altitude_change_raw));
    double_bits = read_le64(record + MOCK_GPS_NOISE_RAW_OFFSET);
    memcpy(&snapshot->noise_raw, &double_bits, sizeof(snapshot->noise_raw));
}

void mock_gps_backend_submit(const MockGpsSnapshot *snapshot)
{
    (void)pthread_mutex_lock(&g_backend_lock);
    g_latest_snapshot = *snapshot;
    mock_increment_saturating(&g_generation);
    g_has_snapshot = 1;
    (void)pthread_mutex_unlock(&g_backend_lock);
}

void mock_gps_backend_reset(void)
{
    (void)pthread_mutex_lock(&g_backend_lock);
    memset(&g_latest_snapshot, 0, sizeof(g_latest_snapshot));
    g_generation = 0u;
    g_has_snapshot = 0;
    (void)pthread_mutex_unlock(&g_backend_lock);
}

#if defined(MOCK_NAVICORE_TESTING)
uint32_t mock_gps_backend_test_generation(void)
{
    uint32_t generation;

    (void)pthread_mutex_lock(&g_backend_lock);
    generation = g_generation;
    (void)pthread_mutex_unlock(&g_backend_lock);
    return generation;
}

int mock_gps_backend_test_latest(MockGpsSnapshot *snapshot_out)
{
    int has_snapshot;

    (void)pthread_mutex_lock(&g_backend_lock);
    has_snapshot = g_has_snapshot;
    if (has_snapshot != 0 && snapshot_out != NULL) {
        *snapshot_out = g_latest_snapshot;
    }
    (void)pthread_mutex_unlock(&g_backend_lock);
    return has_snapshot;
}
#endif
