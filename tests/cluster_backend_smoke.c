#include <open_navicore/cluster.h>

#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum SendMode {
    SEND_NORMAL,
    SEND_REENTER,
    SEND_REENTER_TWICE,
    SEND_REENTER_INNER_FAIL,
    SEND_RESET,
    SEND_BLOCK_FIRST,
    SEND_BLOCK_FIRST_SECOND_FAIL
};

static uint8_t g_sent[MOCK_CLUSTER_RECORD_SIZE];
static uint32_t g_send_status;
static uint32_t g_send_count;
static enum SendMode g_send_mode;
static const MockClusterNavigationState *g_reentrant_state;
static const MockClusterNavigationState *g_reentrant_state_second;
static pthread_mutex_t g_send_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_send_condition = PTHREAD_COND_INITIALIZER;
static int g_first_entered;
static int g_release_first;

uint32_t mock_callback_send_cluster(const void *record_0xa5c)
{
    uint32_t call;
    uint32_t result;
    enum SendMode mode;

    assert(record_0xa5c != NULL);
    (void)pthread_mutex_lock(&g_send_lock);
    memcpy(g_sent, record_0xa5c, sizeof(g_sent));
    call = ++g_send_count;
    mode = g_send_mode;
    result = mode == SEND_BLOCK_FIRST_SECOND_FAIL && call == 2u
                 ? 73u
                 : g_send_status;
    if (mode == SEND_REENTER || mode == SEND_REENTER_TWICE ||
        mode == SEND_REENTER_INNER_FAIL ||
        mode == SEND_RESET) {
        g_send_mode = SEND_NORMAL;
    } else if ((mode == SEND_BLOCK_FIRST ||
                mode == SEND_BLOCK_FIRST_SECOND_FAIL) && call == 1u) {
        g_first_entered = 1;
        (void)pthread_cond_broadcast(&g_send_condition);
        while (g_release_first == 0) {
            (void)pthread_cond_wait(&g_send_condition, &g_send_lock);
        }
    }
    (void)pthread_mutex_unlock(&g_send_lock);

    if (mode == SEND_REENTER || mode == SEND_REENTER_TWICE ||
        mode == SEND_REENTER_INNER_FAIL) {
        uint32_t saved_status = g_send_status;
        assert(g_reentrant_state != NULL);
        if (mode == SEND_REENTER_INNER_FAIL) {
            g_send_status = 73u;
        }
        assert(mock_cluster_backend_publish(g_reentrant_state) ==
               (mode == SEND_REENTER_INNER_FAIL ? 73u : 0u));
        if (mode == SEND_REENTER_TWICE) {
            assert(g_reentrant_state_second != NULL);
            assert(mock_cluster_backend_publish(g_reentrant_state_second) ==
                   0u);
        }
        g_send_status = saved_status;
    } else if (mode == SEND_RESET) {
        mock_cluster_backend_reset();
    }
    return result;
}

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static MockClusterText ascii(const char *bytes)
{
    MockClusterText text;
    text.bytes = bytes;
    text.length = strlen(bytes);
    text.encoding = MOCK_CLUSTER_TEXT_ASCII;
    return text;
}

static MockClusterText bounded(const char *bytes, size_t length,
                               MockClusterTextEncoding encoding)
{
    MockClusterText text;
    text.bytes = bytes;
    text.length = length;
    text.encoding = encoding;
    return text;
}

static MockClusterNavigationState full_state(void)
{
    MockClusterNavigationState state;

    memset(&state, 0, sizeof(state));
    state.valid_attributes = MOCK_CLUSTER_SEMANTIC_SUPPORTED_MASK;
    state.route_guidance_state = MOCK_CLUSTER_ROUTE_MANOEUVRE;
    state.navigation_status = MOCK_CLUSTER_NAV_ROUTE_CALCULATION;
    state.distance_destination = 123u;
    state.distance_destination_unit = MOCK_CLUSTER_DISTANCE_KILOMETRES;
    state.eta_hour = 23u;
    state.eta_minute = 59u;
    state.rta_hour = 25u; /* ABI proves bytes, not clock bounds. */
    state.rta_minute = 61u;
    state.distance_maneuver_tenths = 2505u;
    state.distance_maneuver_unit = MOCK_CLUSTER_DISTANCE_METRES;
    state.current_road = ascii("Current Road");
    state.lane_active = 42u;
    state.next_road = ascii("Next Road");
    state.bar_percent = 88u;
    state.compass = MOCK_CLUSTER_COMPASS_SW;
    state.speed_limit = 50u;
    state.speed_unit = MOCK_CLUSTER_SPEED_KMH;
    state.legal_speed_limit = 45u;
    state.legal_speed_unit = MOCK_CLUSTER_SPEED_MPH;
    return state;
}

static void assert_zero_range(const uint8_t *record, size_t offset,
                              size_t length)
{
    size_t index;
    for (index = 0u; index < length; ++index) {
        assert(record[offset + index] == 0u);
    }
}

static void test_layout_and_serialization(void)
{
    MockClusterNavigationState state = full_state();
    uint8_t record[MOCK_CLUSTER_RECORD_SIZE];
    static const char utf8[] = "A\xc3\xa9\xf0\x9f\x98\x80";

    assert(sizeof(MockClusterDisplayRecord) == 0xa5cu);
    assert(offsetof(MockClusterDisplayRecord, distance_destination) == 0x104u);
    assert(offsetof(MockClusterDisplayRecord, distance_maneuver) == 0x12cu);
    assert(offsetof(MockClusterDisplayRecord, current_road) == 0x150u);
    assert(offsetof(MockClusterDisplayRecord, lane_length) == 0x54cu);
    assert(offsetof(MockClusterDisplayRecord, lane_active) == 0x64cu);
    assert(offsetof(MockClusterDisplayRecord, next_road) == 0x650u);
    assert(offsetof(MockClusterDisplayRecord, changed_attributes) == 0xa56u);
    assert(offsetof(MockClusterDisplayRecord, valid_attributes) == 0xa58u);

    memset(record, 0xa5, sizeof(record));
    assert(mock_cluster_backend_serialize(&state, 0x8041u, record) == 0u);
    assert(record[0x000] == MOCK_CLUSTER_ROUTE_MANOEUVRE);
    assert(record[0x001] == MOCK_CLUSTER_NAV_ROUTE_CALCULATION);
    assert(record[0x002] == 0u && record[0x003] == 0u);
    assert_zero_range(record, 0x004, 0x100);
    assert(read_le32(record + 0x104) == '1');
    assert(read_le32(record + 0x108) == '2');
    assert(read_le32(record + 0x10c) == '3');
    assert(read_le32(record + 0x110) == 0u);
    assert(record[0x124] == MOCK_CLUSTER_DISTANCE_KILOMETRES);
    assert_zero_range(record, 0x125, 3u);
    assert(record[0x128] == 23u && record[0x129] == 59u);
    assert(record[0x12a] == 25u && record[0x12b] == 61u);
    assert(read_le32(record + 0x12c) == '2');
    assert(read_le32(record + 0x138) == '.');
    assert(read_le32(record + 0x13c) == '5');
    assert(read_le32(record + 0x140) == 0u);
    assert(record[0x14c] == MOCK_CLUSTER_DISTANCE_METRES);
    assert_zero_range(record, 0x14d, 3u);
    assert(read_le32(record + 0x150) == 'C');
    assert_zero_range(record, 0x54c, 0x100);
    assert(read_le32(record + 0x64c) == 1u);
    assert(read_le32(record + 0x650) == 'N');
    assert(record[0xa4c] == 88u);
    assert(record[0xa4d] == MOCK_CLUSTER_COMPASS_SW);
    assert_zero_range(record, 0xa4e, 6u);
    assert(record[0xa54] == 50u && record[0xa55] == MOCK_CLUSTER_SPEED_KMH);
    assert(read_le16(record + 0xa56) == 0x8041u);
    assert(read_le16(record + 0xa58) == state.valid_attributes);
    assert(record[0xa5a] == 45u && record[0xa5b] == MOCK_CLUSTER_SPEED_MPH);

    memset(&state, 0, sizeof(state));
    state.valid_attributes = MOCK_CLUSTER_CURRENT_ROAD;
    state.current_road = bounded(utf8, sizeof(utf8) - 1u,
                                 MOCK_CLUSTER_TEXT_UTF8);
    assert(mock_cluster_backend_serialize(&state, 0u, record) == 0u);
    assert(read_le32(record + 0x150) == 0x41u);
    assert(read_le32(record + 0x154) == 0x00e9u);
    assert(read_le32(record + 0x158) == 0x1f600u);
    assert(read_le32(record + 0x15c) == 0u);
}

static void test_validation_and_bounds(void)
{
    MockClusterNavigationState state;
    uint8_t record[MOCK_CLUSTER_RECORD_SIZE];
    char road[128];
    char astral[64u * 4u];
    size_t index;
    static const char bad_continuation[] = "\xe2\x28\xa1";
    static const char overlong[] = "\xc0\x80";
    static const char surrogate[] = "\xed\xa0\x80";
    static const char out_of_range[] = "\xf4\x90\x80\x80";
    static const char embedded_nul[] = { 'a', '\0', 'b' };

    memset(&state, 0, sizeof(state));
    assert(mock_cluster_backend_serialize(NULL, 0u, record) ==
           MOCK_CLUSTER_INVALID_ARGUMENT);
    assert(mock_cluster_backend_serialize(&state, 0u, NULL) ==
           MOCK_CLUSTER_INVALID_ARGUMENT);

    state.valid_attributes = MOCK_CLUSTER_ROUTE_SYMBOL;
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_UNSUPPORTED_FIELD);
    state.valid_attributes = MOCK_CLUSTER_LANE;
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_UNSUPPORTED_FIELD);
    state.valid_attributes = MOCK_CLUSTER_UNKNOWN_12;
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_UNSUPPORTED_FIELD);
    state.valid_attributes = 0u;
    assert(mock_cluster_backend_serialize(&state, MOCK_CLUSTER_LANE, record) ==
           MOCK_CLUSTER_UNSUPPORTED_FIELD);

#define INVALID_FIELD(mask, member, bad)                                      \
    do {                                                                       \
        memset(&state, 0, sizeof(state));                                      \
        state.valid_attributes = (mask);                                       \
        state.member = (bad);                                                  \
        assert(mock_cluster_backend_serialize(&state, 0u, record) ==           \
               MOCK_CLUSTER_INVALID_VALUE);                                   \
    } while (0)
    INVALID_FIELD(MOCK_CLUSTER_ROUTE_GUIDANCE_STATE, route_guidance_state, 0u);
    INVALID_FIELD(MOCK_CLUSTER_ROUTE_GUIDANCE_STATE, route_guidance_state, 8u);
    INVALID_FIELD(MOCK_CLUSTER_NAVIGATION_STATUS, navigation_status, 0u);
    INVALID_FIELD(MOCK_CLUSTER_NAVIGATION_STATUS, navigation_status, 9u);
    INVALID_FIELD(MOCK_CLUSTER_BAR_PERCENT, bar_percent, 101u);
    INVALID_FIELD(MOCK_CLUSTER_COMPASS, compass, 0u);
    INVALID_FIELD(MOCK_CLUSTER_COMPASS, compass, 9u);
    INVALID_FIELD(MOCK_CLUSTER_SPEED_LIMIT, speed_unit, 0u);
    INVALID_FIELD(MOCK_CLUSTER_SPEED_LIMIT, speed_unit, 3u);
    INVALID_FIELD(MOCK_CLUSTER_LEGAL_LIMIT, legal_speed_unit, 0u);
    INVALID_FIELD(MOCK_CLUSTER_LEGAL_LIMIT, legal_speed_unit, 3u);
#undef INVALID_FIELD

    memset(&state, 0, sizeof(state));
    state.valid_attributes = MOCK_CLUSTER_DISTANCE_DESTINATION;
    state.distance_destination_unit = 0u;
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_INVALID_VALUE);
    state.distance_destination_unit = 5u;
    state.distance_destination = 65535u;
    assert(mock_cluster_backend_serialize(&state, 0u, record) == 0u);
    assert(read_le32(record + 0x104) == '6');
    assert(read_le32(record + 0x114) == '5');
    assert(read_le32(record + 0x118) == 0u);
    state.distance_destination = 65536u;
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_INVALID_VALUE);

    memset(&state, 0, sizeof(state));
    state.valid_attributes = MOCK_CLUSTER_DISTANCE_MANEUVER;
    state.distance_maneuver_unit = 5u;
    state.distance_maneuver_tenths = 65535u;
    assert(mock_cluster_backend_serialize(&state, 0u, record) == 0u);
    assert(read_le32(record + 0x12c) == '6');
    assert(read_le32(record + 0x13c) == '.');
    assert(read_le32(record + 0x140) == '5');
    state.distance_maneuver_tenths = 65536u;
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_INVALID_VALUE);

    memset(road, 'x', sizeof(road));
    memset(&state, 0, sizeof(state));
    state.valid_attributes = MOCK_CLUSTER_CURRENT_ROAD;
    state.current_road = bounded(road, 127u, MOCK_CLUSTER_TEXT_ASCII);
    assert(mock_cluster_backend_serialize(&state, 0u, record) == 0u);
    assert(read_le32(record + 0x348) == 'x');
    assert(read_le32(record + 0x34c) == 0u);
    state.current_road.length = 128u;
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_TEXT_TOO_LONG);

    for (index = 0u; index < 64u; ++index) {
        memcpy(astral + index * 4u, "\xf0\x9f\x98\x80", 4u);
    }
    state.current_road = bounded(astral, 63u * 4u, MOCK_CLUSTER_TEXT_UTF8);
    assert(mock_cluster_backend_serialize(&state, 0u, record) == 0u);
    state.current_road.length = 64u * 4u;
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_TEXT_TOO_LONG);

    state.current_road = bounded(NULL, 1u, MOCK_CLUSTER_TEXT_ASCII);
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_INVALID_ARGUMENT);
    state.current_road = bounded("x", 1u, (MockClusterTextEncoding)2);
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_INVALID_VALUE);
    state.current_road = bounded("\x80", 1u, MOCK_CLUSTER_TEXT_ASCII);
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_INVALID_TEXT);
    state.current_road = bounded(bad_continuation,
                                 sizeof(bad_continuation) - 1u,
                                 MOCK_CLUSTER_TEXT_UTF8);
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_INVALID_TEXT);
    state.current_road = bounded(overlong, sizeof(overlong) - 1u,
                                 MOCK_CLUSTER_TEXT_UTF8);
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_INVALID_TEXT);
    state.current_road = bounded(surrogate, sizeof(surrogate) - 1u,
                                 MOCK_CLUSTER_TEXT_UTF8);
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_INVALID_TEXT);
    state.current_road = bounded(out_of_range, sizeof(out_of_range) - 1u,
                                 MOCK_CLUSTER_TEXT_UTF8);
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_INVALID_TEXT);
    state.current_road = bounded(embedded_nul, sizeof(embedded_nul),
                                 MOCK_CLUSTER_TEXT_UTF8);
    assert(mock_cluster_backend_serialize(&state, 0u, record) ==
           MOCK_CLUSTER_INVALID_TEXT);

    memset(&state, 0xa5, sizeof(state));
    state.valid_attributes = 0u;
    assert(mock_cluster_backend_serialize(&state, 0u, record) == 0u);
    assert_zero_range(record, 0u, sizeof(record));
}

static uint16_t sent_changed(void)
{
    return read_le16(g_sent + 0xa56);
}

static void publish_ok(MockClusterNavigationState *state, uint16_t changed)
{
    uint32_t before = g_send_count;
    assert(mock_cluster_backend_publish(state) == 0u);
    assert(g_send_count == before + 1u);
    assert(sent_changed() == changed);
    assert(read_le16(g_sent + 0xa58) == state->valid_attributes);
}

static void test_change_masks_status_and_ownership(void)
{
    MockClusterNavigationState state = full_state();
    char current[] = "Current Road";
    char next[] = "Next Road";

    state.current_road = ascii(current);
    state.next_road = ascii(next);
    mock_cluster_backend_reset();
    g_send_mode = SEND_NORMAL;
    g_send_status = 0u;
    g_send_count = 0u;
    publish_ok(&state, MOCK_CLUSTER_SEMANTIC_SUPPORTED_MASK);
    publish_ok(&state, 0u);

    state.route_guidance_state = MOCK_CLUSTER_ROUTE_DESTINATION_REACHED;
    publish_ok(&state, MOCK_CLUSTER_ROUTE_GUIDANCE_STATE);
    state.navigation_status = MOCK_CLUSTER_NAV_NO_DATA;
    publish_ok(&state, MOCK_CLUSTER_NAVIGATION_STATUS);
    ++state.distance_destination;
    publish_ok(&state, MOCK_CLUSTER_DISTANCE_DESTINATION);
    ++state.eta_minute;
    publish_ok(&state, MOCK_CLUSTER_ETA);
    ++state.rta_minute;
    publish_ok(&state, MOCK_CLUSTER_RTA);
    ++state.distance_maneuver_tenths;
    publish_ok(&state, MOCK_CLUSTER_DISTANCE_MANEUVER);
    current[0] = 'X';
    publish_ok(&state, MOCK_CLUSTER_CURRENT_ROAD);
    state.lane_active = 0u;
    publish_ok(&state, MOCK_CLUSTER_LANE_ACTIVE);
    next[0] = 'Y';
    publish_ok(&state, MOCK_CLUSTER_NEXT_ROAD);
    ++state.bar_percent;
    publish_ok(&state, MOCK_CLUSTER_BAR_PERCENT);
    state.compass = MOCK_CLUSTER_COMPASS_W;
    publish_ok(&state, MOCK_CLUSTER_COMPASS);
    ++state.speed_limit;
    publish_ok(&state, MOCK_CLUSTER_SPEED_LIMIT);
    ++state.legal_speed_limit;
    publish_ok(&state, MOCK_CLUSTER_LEGAL_LIMIT);

    state.valid_attributes = 0u;
    publish_ok(&state, MOCK_CLUSTER_SEMANTIC_SUPPORTED_MASK);
    assert_zero_range(g_sent, 0u, 0xa56u);
    assert_zero_range(g_sent, 0xa5au, 2u);

    memset(&state, 0, sizeof(state));
    state.valid_attributes = MOCK_CLUSTER_ROUTE_GUIDANCE_STATE;
    state.route_guidance_state = MOCK_CLUSTER_ROUTE_FOLLOW_STREET;
    mock_cluster_backend_reset();
    publish_ok(&state, MOCK_CLUSTER_ROUTE_GUIDANCE_STATE);
    state.route_guidance_state = MOCK_CLUSTER_ROUTE_MANOEUVRE;
    g_send_status = 73u;
    assert(mock_cluster_backend_publish(&state) == 73u);
    assert(sent_changed() == MOCK_CLUSTER_ROUTE_GUIDANCE_STATE);
    state.route_guidance_state = MOCK_CLUSTER_ROUTE_FOLLOW_STREET;
    g_send_status = 0u;
    publish_ok(&state, 0u);

    memset(&state, 0, sizeof(state));
    state.valid_attributes = MOCK_CLUSTER_CURRENT_ROAD;
    current[0] = 'A';
    current[1] = '\0';
    state.current_road = ascii(current);
    mock_cluster_backend_reset();
    publish_ok(&state, MOCK_CLUSTER_CURRENT_ROAD);
    current[0] = 'B';
    {
        char same_value[] = "A";
        state.current_road = ascii(same_value);
        publish_ok(&state, 0u);
    }
}

typedef struct {
    MockClusterNavigationState state;
    uint32_t result;
    int started;
} PublishThread;

static void *publish_thread(void *opaque)
{
    PublishThread *thread = (PublishThread *)opaque;
    (void)pthread_mutex_lock(&g_send_lock);
    thread->started = 1;
    (void)pthread_cond_broadcast(&g_send_condition);
    (void)pthread_mutex_unlock(&g_send_lock);
    thread->result = mock_cluster_backend_publish(&thread->state);
    return NULL;
}

static MockClusterNavigationState route_state(uint8_t route)
{
    MockClusterNavigationState state;
    memset(&state, 0, sizeof(state));
    state.valid_attributes = MOCK_CLUSTER_ROUTE_GUIDANCE_STATE;
    state.route_guidance_state = route;
    return state;
}

static void test_reentrant_and_concurrent_ordering(void)
{
    MockClusterNavigationState outer =
        route_state(MOCK_CLUSTER_ROUTE_FOLLOW_STREET);
    MockClusterNavigationState inner = route_state(MOCK_CLUSTER_ROUTE_MANOEUVRE);
    PublishThread first;
    PublishThread second;
    pthread_t first_thread;
    pthread_t second_thread;

    mock_cluster_backend_reset();
    g_send_count = 0u;
    g_send_status = 0u;
    g_send_mode = SEND_NORMAL;
    publish_ok(&outer, MOCK_CLUSTER_ROUTE_GUIDANCE_STATE);
    g_send_count = 0u;
    g_reentrant_state = &outer;
    g_send_mode = SEND_REENTER;
    assert(mock_cluster_backend_publish(&inner) == 0u);
    assert(g_send_count == 2u);
    assert(sent_changed() == MOCK_CLUSTER_ROUTE_GUIDANCE_STATE);
    publish_ok(&outer, 0u);

    /* After a first nested success, the second nested call compares against
     * that committed record rather than the still-live outer frame. */
    mock_cluster_backend_reset();
    g_send_count = 0u;
    g_reentrant_state = &inner;
    g_reentrant_state_second = &outer;
    g_send_mode = SEND_REENTER_TWICE;
    assert(mock_cluster_backend_publish(&outer) == 0u);
    assert(g_send_count == 3u);
    assert(sent_changed() == MOCK_CLUSTER_ROUTE_GUIDANCE_STATE);
    publish_ok(&outer, 0u);

    /* A newer nested failure must not suppress the successful outer frame. */
    mock_cluster_backend_reset();
    g_send_count = 0u;
    g_reentrant_state = &inner;
    g_send_mode = SEND_REENTER_INNER_FAIL;
    assert(mock_cluster_backend_publish(&outer) == 0u);
    g_send_mode = SEND_NORMAL;
    publish_ok(&outer, 0u);

    mock_cluster_backend_reset();
    g_send_mode = SEND_RESET;
    assert(mock_cluster_backend_publish(&outer) == 0u);
    g_send_mode = SEND_NORMAL;
    publish_ok(&outer, MOCK_CLUSTER_ROUTE_GUIDANCE_STATE);

    /* Cross-thread calls serialize around PAS.  B computes its wire mask
     * after A commits, not from a stale pre-A baseline. */
    mock_cluster_backend_reset();
    g_send_mode = SEND_NORMAL;
    publish_ok(&outer, MOCK_CLUSTER_ROUTE_GUIDANCE_STATE);
    first.state = inner;
    first.result = UINT32_MAX;
    first.started = 0;
    second.state = outer;
    second.result = UINT32_MAX;
    second.started = 0;
    g_send_count = 0u;
    g_first_entered = 0;
    g_release_first = 0;
    g_send_mode = SEND_BLOCK_FIRST;
    assert(pthread_create(&first_thread, NULL, publish_thread, &first) == 0);
    (void)pthread_mutex_lock(&g_send_lock);
    while (g_first_entered == 0) {
        (void)pthread_cond_wait(&g_send_condition, &g_send_lock);
    }
    (void)pthread_mutex_unlock(&g_send_lock);
    assert(pthread_create(&second_thread, NULL, publish_thread, &second) == 0);
    (void)pthread_mutex_lock(&g_send_lock);
    while (second.started == 0) {
        (void)pthread_cond_wait(&g_send_condition, &g_send_lock);
    }
    assert(g_send_count == 1u);
    g_release_first = 1;
    (void)pthread_cond_broadcast(&g_send_condition);
    (void)pthread_mutex_unlock(&g_send_lock);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(first.result == 0u && second.result == 0u);
    assert(sent_changed() == MOCK_CLUSTER_ROUTE_GUIDANCE_STATE);
    g_send_mode = SEND_NORMAL;
    publish_ok(&outer, 0u);

    /* A queued failing publication must not replace the accepted baseline. */
    first.state = outer;
    first.result = UINT32_MAX;
    first.started = 0;
    second.state = inner;
    second.result = UINT32_MAX;
    second.started = 0;
    mock_cluster_backend_reset();
    g_send_count = 0u;
    g_first_entered = 0;
    g_release_first = 0;
    g_send_mode = SEND_BLOCK_FIRST_SECOND_FAIL;
    assert(pthread_create(&first_thread, NULL, publish_thread, &first) == 0);
    (void)pthread_mutex_lock(&g_send_lock);
    while (g_first_entered == 0) {
        (void)pthread_cond_wait(&g_send_condition, &g_send_lock);
    }
    (void)pthread_mutex_unlock(&g_send_lock);
    assert(pthread_create(&second_thread, NULL, publish_thread, &second) == 0);
    (void)pthread_mutex_lock(&g_send_lock);
    while (second.started == 0) {
        (void)pthread_cond_wait(&g_send_condition, &g_send_lock);
    }
    assert(g_send_count == 1u);
    g_release_first = 1;
    (void)pthread_cond_broadcast(&g_send_condition);
    (void)pthread_mutex_unlock(&g_send_lock);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(first.result == 0u && second.result == 73u);
    g_send_mode = SEND_NORMAL;
    publish_ok(&outer, 0u);
}

int main(void)
{
    test_layout_and_serialization();
    test_validation_and_bounds();
    test_change_masks_status_and_ownership();
    test_reentrant_and_concurrent_ordering();
    puts("cluster backend smoke passed");
    return 0;
}
