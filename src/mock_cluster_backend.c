#include <open_navicore/cluster.h>

#include "mock_callback_backend.h"

#include <pthread.h>
#include <string.h>

typedef struct {
    uint16_t bit;
    uint16_t offset;
    uint16_t size;
} MockClusterField;

typedef struct MockClusterPublishFrame {
    struct MockClusterPublishFrame *previous;
    uint64_t epoch;
    uint64_t sequence;
    uint8_t *record;
} MockClusterPublishFrame;

static const MockClusterField g_fields[] = {
    { MOCK_CLUSTER_ROUTE_GUIDANCE_STATE, 0x000, 1 },
    { MOCK_CLUSTER_NAVIGATION_STATUS, 0x001, 1 },
    { MOCK_CLUSTER_DISTANCE_DESTINATION, 0x104, 0x21 },
    { MOCK_CLUSTER_ETA, 0x128, 2 },
    { MOCK_CLUSTER_RTA, 0x12a, 2 },
    { MOCK_CLUSTER_DISTANCE_MANEUVER, 0x12c, 0x21 },
    { MOCK_CLUSTER_CURRENT_ROAD, 0x150, 0x3fc },
    { MOCK_CLUSTER_LANE_ACTIVE, 0x64c, 4 },
    { MOCK_CLUSTER_NEXT_ROAD, 0x650, 0x3fc },
    { MOCK_CLUSTER_BAR_PERCENT, 0xa4c, 1 },
    { MOCK_CLUSTER_COMPASS, 0xa4d, 1 },
    { MOCK_CLUSTER_SPEED_LIMIT, 0xa54, 2 },
    { MOCK_CLUSTER_LEGAL_LIMIT, 0xa5a, 2 }
};

static pthread_mutex_t g_cluster_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cluster_condition = PTHREAD_COND_INITIALIZER;
static uint8_t g_previous[MOCK_CLUSTER_RECORD_SIZE];
static int g_has_previous;
static uint64_t g_history_epoch;
static uint64_t g_next_sequence;
static uint64_t g_committed_sequence;
static pthread_t g_publish_owner;
static int g_publish_owner_valid;
static MockClusterPublishFrame *g_publish_top;

#if defined(__GNUC__)
extern int mock_navicore_producer_begin(uint64_t *epoch_out)
    __attribute__((weak));
extern int mock_navicore_producer_current(uint64_t epoch)
    __attribute__((weak));
extern void mock_navicore_producer_end(uint64_t epoch)
    __attribute__((weak));
#else
extern int mock_navicore_producer_begin(uint64_t *epoch_out);
extern int mock_navicore_producer_current(uint64_t epoch);
extern void mock_navicore_producer_end(uint64_t epoch);
#endif

static int producer_begin(uint64_t *epoch)
{
    if (mock_navicore_producer_begin == NULL) {
        *epoch = 0u;
        return 1;
    }
    return mock_navicore_producer_begin(epoch);
}

static int producer_current(uint64_t epoch)
{
    return mock_navicore_producer_current == NULL ||
           mock_navicore_producer_current(epoch);
}

static void producer_end(uint64_t epoch)
{
    if (mock_navicore_producer_end != NULL) {
        mock_navicore_producer_end(epoch);
    }
}

static void write_le16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static uint16_t read_le16(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      ((uint16_t)source[1] << 8));
}

static uint32_t decode_utf8(const uint8_t *bytes, size_t length,
                            size_t *cursor, uint32_t *code_point)
{
    uint8_t first = bytes[*cursor];
    uint32_t value;
    size_t count;
    size_t index;

    if (first < 0x80u) {
        if (first == 0u) {
            return MOCK_CLUSTER_INVALID_TEXT;
        }
        *code_point = first;
        ++*cursor;
        return MOCK_CLUSTER_OK;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
        value = first & 0x1fu;
        count = 2u;
    } else if (first >= 0xe0u && first <= 0xefu) {
        value = first & 0x0fu;
        count = 3u;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        value = first & 0x07u;
        count = 4u;
    } else {
        return MOCK_CLUSTER_INVALID_TEXT;
    }
    if (length - *cursor < count) {
        return MOCK_CLUSTER_INVALID_TEXT;
    }
    for (index = 1u; index < count; ++index) {
        uint8_t continuation = bytes[*cursor + index];
        if ((continuation & 0xc0u) != 0x80u) {
            return MOCK_CLUSTER_INVALID_TEXT;
        }
        value = (value << 6) | (uint32_t)(continuation & 0x3fu);
    }
    if ((count == 3u && value < 0x800u) ||
        (count == 4u && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu) {
        return MOCK_CLUSTER_INVALID_TEXT;
    }
    *cursor += count;
    *code_point = value;
    return MOCK_CLUSTER_OK;
}

static uint32_t encode_text(const MockClusterText *text,
                            uint8_t *destination, size_t storage_code_units,
                            size_t max_scalars, size_t max_utf16_units)
{
    const uint8_t *bytes;
    size_t cursor = 0u;
    size_t output = 0u;
    size_t utf16_units = 0u;

    if (text->length != 0u && text->bytes == NULL) {
        return MOCK_CLUSTER_INVALID_ARGUMENT;
    }
    if (text->encoding != MOCK_CLUSTER_TEXT_UTF8 &&
        text->encoding != MOCK_CLUSTER_TEXT_ASCII) {
        return MOCK_CLUSTER_INVALID_VALUE;
    }
    bytes = (const uint8_t *)text->bytes;
    while (cursor < text->length) {
        uint32_t code_point;
        uint32_t status;

        if (text->encoding == MOCK_CLUSTER_TEXT_ASCII) {
            if (bytes[cursor] == 0u || bytes[cursor] > 0x7fu) {
                return MOCK_CLUSTER_INVALID_TEXT;
            }
            code_point = bytes[cursor++];
        } else {
            status = decode_utf8(bytes, text->length, &cursor, &code_point);
            if (status != MOCK_CLUSTER_OK) {
                return status;
            }
        }
        if (output >= max_scalars ||
            utf16_units + (code_point > 0xffffu ? 2u : 1u) >
                max_utf16_units ||
            output + 1u >= storage_code_units) {
            return MOCK_CLUSTER_TEXT_TOO_LONG;
        }
        write_le32(destination + output * 4u, code_point);
        utf16_units += code_point > 0xffffu ? 2u : 1u;
        ++output;
    }
    /* The caller zeroed the whole record, so the next code unit is the
     * required terminator and all unused capacity remains deterministic. */
    return MOCK_CLUSTER_OK;
}

static void encode_decimal(uint8_t *destination, uint32_t value,
                           int value_is_tenths)
{
    char reversed[10];
    size_t count = 0u;
    size_t output = 0u;
    uint32_t whole = value_is_tenths != 0 ? value / 10u : value;
    uint32_t fraction = value_is_tenths != 0 ? value % 10u : 0u;

    do {
        reversed[count++] = (char)('0' + whole % 10u);
        whole /= 10u;
    } while (whole != 0u);
    while (count != 0u) {
        write_le32(destination + output * 4u,
                   (uint32_t)(uint8_t)reversed[--count]);
        ++output;
    }
    if (value_is_tenths != 0 && fraction != 0u) {
        write_le32(destination + output++ * 4u, (uint32_t)'.');
        write_le32(destination + output * 4u,
                   (uint32_t)('0' + fraction));
    }
}

static int valid_range(uint8_t value, uint8_t low, uint8_t high)
{
    return value >= low && value <= high;
}

uint32_t mock_cluster_backend_serialize(
    const MockClusterNavigationState *state,
    uint16_t changed_attributes,
    uint8_t record_out[MOCK_CLUSTER_RECORD_SIZE])
{
    uint16_t valid;
    uint32_t status;

    if (state == NULL || record_out == NULL) {
        return MOCK_CLUSTER_INVALID_ARGUMENT;
    }
    valid = state->valid_attributes;
    memset(record_out, 0, MOCK_CLUSTER_RECORD_SIZE);
    if (((valid | changed_attributes) &
         (uint16_t)~MOCK_CLUSTER_SEMANTIC_SUPPORTED_MASK) != 0u) {
        return MOCK_CLUSTER_UNSUPPORTED_FIELD;
    }

    if ((valid & MOCK_CLUSTER_ROUTE_GUIDANCE_STATE) != 0u) {
        if (!valid_range(state->route_guidance_state, 1u, 7u)) {
            return MOCK_CLUSTER_INVALID_VALUE;
        }
        record_out[0x000] = state->route_guidance_state;
    }
    if ((valid & MOCK_CLUSTER_NAVIGATION_STATUS) != 0u) {
        if (!valid_range(state->navigation_status, 1u, 8u)) {
            return MOCK_CLUSTER_INVALID_VALUE;
        }
        record_out[0x001] = state->navigation_status;
    }
    if ((valid & MOCK_CLUSTER_DISTANCE_DESTINATION) != 0u) {
        if (!valid_range(state->distance_destination_unit, 1u, 5u)) {
            return MOCK_CLUSTER_INVALID_VALUE;
        }
        if (state->distance_destination > UINT16_MAX) {
            return MOCK_CLUSTER_INVALID_VALUE;
        }
        encode_decimal(record_out + 0x104,
                       state->distance_destination, 0);
        record_out[0x124] = state->distance_destination_unit;
    }
    if ((valid & MOCK_CLUSTER_ETA) != 0u) {
        record_out[0x128] = state->eta_hour;
        record_out[0x129] = state->eta_minute;
    }
    if ((valid & MOCK_CLUSTER_RTA) != 0u) {
        record_out[0x12a] = state->rta_hour;
        record_out[0x12b] = state->rta_minute;
    }
    if ((valid & MOCK_CLUSTER_DISTANCE_MANEUVER) != 0u) {
        if (!valid_range(state->distance_maneuver_unit, 1u, 5u)) {
            return MOCK_CLUSTER_INVALID_VALUE;
        }
        if (state->distance_maneuver_tenths > UINT16_MAX) {
            return MOCK_CLUSTER_INVALID_VALUE;
        }
        encode_decimal(record_out + 0x12c,
                       state->distance_maneuver_tenths, 1);
        record_out[0x14c] = state->distance_maneuver_unit;
    }
    if ((valid & MOCK_CLUSTER_CURRENT_ROAD) != 0u) {
        status = encode_text(&state->current_road, record_out + 0x150,
                             MOCK_CLUSTER_ROAD_CODE_UNITS,
                             MOCK_CLUSTER_ROAD_MAX_SCALARS,
                             MOCK_CLUSTER_ROAD_MAX_UTF16_UNITS);
        if (status != MOCK_CLUSTER_OK) {
            return status;
        }
    }
    if ((valid & MOCK_CLUSTER_LANE_ACTIVE) != 0u) {
        write_le32(record_out + 0x64c, state->lane_active != 0u ? 1u : 0u);
    }
    if ((valid & MOCK_CLUSTER_NEXT_ROAD) != 0u) {
        status = encode_text(&state->next_road, record_out + 0x650,
                             MOCK_CLUSTER_ROAD_CODE_UNITS,
                             MOCK_CLUSTER_ROAD_MAX_SCALARS,
                             MOCK_CLUSTER_ROAD_MAX_UTF16_UNITS);
        if (status != MOCK_CLUSTER_OK) {
            return status;
        }
    }
    if ((valid & MOCK_CLUSTER_BAR_PERCENT) != 0u) {
        if (state->bar_percent > 100u) {
            return MOCK_CLUSTER_INVALID_VALUE;
        }
        record_out[0xa4c] = state->bar_percent;
    }
    if ((valid & MOCK_CLUSTER_COMPASS) != 0u) {
        if (!valid_range(state->compass, 1u, 8u)) {
            return MOCK_CLUSTER_INVALID_VALUE;
        }
        record_out[0xa4d] = state->compass;
    }
    if ((valid & MOCK_CLUSTER_SPEED_LIMIT) != 0u) {
        if (!valid_range(state->speed_unit, 1u, 2u)) {
            return MOCK_CLUSTER_INVALID_VALUE;
        }
        record_out[0xa54] = state->speed_limit;
        record_out[0xa55] = state->speed_unit;
    }
    if ((valid & MOCK_CLUSTER_LEGAL_LIMIT) != 0u) {
        if (!valid_range(state->legal_speed_unit, 1u, 2u)) {
            return MOCK_CLUSTER_INVALID_VALUE;
        }
        record_out[0xa5a] = state->legal_speed_limit;
        record_out[0xa5b] = state->legal_speed_unit;
    }
    write_le16(record_out + 0xa56, changed_attributes);
    write_le16(record_out + 0xa58, valid);
    return MOCK_CLUSTER_OK;
}

static uint16_t calculate_changed(const uint8_t *current,
                                  const uint8_t *previous,
                                  int has_previous)
{
    uint16_t current_valid = read_le16(current + 0xa58);
    uint16_t previous_valid = has_previous != 0
                                  ? read_le16(previous + 0xa58)
                                  : 0u;
    uint16_t changed = 0u;
    size_t index;

    for (index = 0u; index < sizeof(g_fields) / sizeof(g_fields[0]); ++index) {
        const MockClusterField *field = &g_fields[index];
        int current_has = (current_valid & field->bit) != 0u;
        int previous_has = (previous_valid & field->bit) != 0u;

        if (current_has != previous_has ||
            (current_has != 0 &&
             memcmp(current + field->offset, previous + field->offset,
                    field->size) != 0)) {
            changed = (uint16_t)(changed | field->bit);
        }
    }
    return changed;
}

uint32_t mock_cluster_backend_publish(const MockClusterNavigationState *state)
{
    uint8_t record[MOCK_CLUSTER_RECORD_SIZE];
    uint16_t changed;
    uint32_t status;
    MockClusterPublishFrame frame;
    pthread_t self;
    const uint8_t *baseline;
    int has_baseline;
    uint64_t lifecycle_epoch;

    if (!producer_begin(&lifecycle_epoch)) {
        return 1u;
    }

    status = mock_cluster_backend_serialize(state, 0u, record);
    if (status != MOCK_CLUSTER_OK) {
        producer_end(lifecycle_epoch);
        return status;
    }

    self = pthread_self();
    (void)pthread_mutex_lock(&g_cluster_lock);
    while (g_publish_owner_valid != 0 &&
           !pthread_equal(g_publish_owner, self)) {
        (void)pthread_cond_wait(&g_cluster_condition, &g_cluster_lock);
    }
    if (g_publish_owner_valid == 0) {
        g_publish_owner = self;
        g_publish_owner_valid = 1;
    }
    ++g_next_sequence;
    if (g_next_sequence == 0u) {
        /* A 64-bit wrap would make ordering ambiguous.  Treat it as a reset
         * epoch instead of permitting an old in-flight request to commit. */
        ++g_history_epoch;
        if (g_history_epoch == 0u) {
            ++g_history_epoch;
        }
        g_next_sequence = 1u;
        g_committed_sequence = 0u;
        memset(g_previous, 0, sizeof(g_previous));
        g_has_previous = 0;
    }
    frame.epoch = g_history_epoch;
    frame.sequence = g_next_sequence;
    frame.record = record;
    frame.previous = g_publish_top;
    if (g_publish_top != NULL &&
        g_publish_top->epoch == g_history_epoch &&
        g_publish_top->sequence > g_committed_sequence) {
        baseline = g_publish_top->record;
        has_baseline = 1;
    } else {
        baseline = g_previous;
        has_baseline = g_has_previous;
    }
    changed = calculate_changed(record, baseline, has_baseline);
    write_le16(record + 0xa56, changed);
    g_publish_top = &frame;
    (void)pthread_mutex_unlock(&g_cluster_lock);

    status = mock_callback_send_cluster(record);

    (void)pthread_mutex_lock(&g_cluster_lock);
    if (status == 0u && producer_current(lifecycle_epoch) &&
        frame.epoch == g_history_epoch &&
        frame.sequence > g_committed_sequence) {
        memcpy(g_previous, record, sizeof(g_previous));
        g_has_previous = 1;
        g_committed_sequence = frame.sequence;
    }
    /* Synchronous same-thread reentry is necessarily LIFO. */
    if (g_publish_top == &frame) {
        g_publish_top = frame.previous;
    }
    if (g_publish_top == NULL) {
        g_publish_owner_valid = 0;
        (void)pthread_cond_broadcast(&g_cluster_condition);
    }
    (void)pthread_mutex_unlock(&g_cluster_lock);
    if (!producer_current(lifecycle_epoch)) {
        status = 1u;
    }
    producer_end(lifecycle_epoch);
    return status;
}

void mock_cluster_backend_reset(void)
{
    (void)pthread_mutex_lock(&g_cluster_lock);
    ++g_history_epoch;
    if (g_history_epoch == 0u) {
        ++g_history_epoch;
    }
    g_next_sequence = 0u;
    g_committed_sequence = 0u;
    memset(g_previous, 0, sizeof(g_previous));
    g_has_previous = 0;
    (void)pthread_mutex_unlock(&g_cluster_lock);
}
