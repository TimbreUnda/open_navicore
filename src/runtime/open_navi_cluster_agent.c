#define _POSIX_C_SOURCE 200112L

#include "open_navi_cluster_agent.h"

#ifdef OPEN_NAVI_CLUSTER_AGENT

#include <cagent/client.h>

#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { MARKER_RECHECK_SECONDS = 5 };

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *g_marker = OPEN_NAVI_CLUSTER_AGENT_MARKER;
static const char *g_socket;
static struct cagent_client *g_client;
static int g_active;
static time_t g_checked;
static int g_sent_any;

static time_t now_seconds(void)
{
    struct timespec ts;
    return clock_gettime(CLOCK_MONOTONIC, &ts) == 0 ? ts.tv_sec : 0;
}

/* Caller holds g_lock. */
static void refresh_locked(int force)
{
    time_t now = now_seconds();
    if (!force && g_checked != 0 && now - g_checked < MARKER_RECHECK_SECONDS) {
        return;
    }
    g_checked = now != 0 ? now : 1;
    g_active = access(g_marker, F_OK) == 0;
    if (g_active && g_client == NULL) {
        g_client = cagent_client_open_at(CAGENT_SRC_NAVI, g_socket);
        if (g_client == NULL) {
            g_active = 0;
        }
    }
}

int open_navi_cluster_agent_active(void)
{
    int active;
    (void)pthread_mutex_lock(&g_lock);
    refresh_locked(0);
    active = g_active;
    (void)pthread_mutex_unlock(&g_lock);
    return active;
}

/* Road text: UTF-8 (or ASCII) bytes; control characters become spaces and an over-long text is cut
 * on a character boundary. Returns the bytes copied (NUL terminated in dst). */
static size_t copy_road(const OpenNaviClusterText *text, char *dst, size_t cap)
{
    size_t n = text->bytes != NULL ? text->length : 0u;
    size_t i;
    if (n > cap - 1u) {
        n = cap - 1u;
        while (n > 0u && (((unsigned char)text->bytes[n]) & 0xc0u) == 0x80u) {
            --n; /* do not leave a split UTF-8 sequence */
        }
    }
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)text->bytes[i];
        dst[i] = (c < 0x20u || c == 0x7fu) ? ' ' : (char)c;
    }
    dst[n] = '\0';
    return n;
}

static void to_agent_state(const OpenNaviClusterNavigationState *s, struct cagent_state *out)
{
    memset(out, 0, sizeof(*out));
    /* The attribute bits are the same numbers on both sides (see cagent/proto.h). */
    out->valid = s->valid_attributes;
    out->route_state = s->route_guidance_state;
    out->nav_status = s->navigation_status;
    out->symbol = s->route_guidance_symbol;
    out->dest_unit = s->distance_destination_unit;
    out->dest_dist = s->distance_destination;
    out->eta_h = s->eta_hour;
    out->eta_m = s->eta_minute;
    out->rta_h = s->rta_hour;
    out->rta_m = s->rta_minute;
    out->man_dist_tenths = s->distance_maneuver_tenths;
    out->man_unit = s->distance_maneuver_unit;
    out->bar = s->bar_percent;
    out->compass = s->compass;
    out->speed_limit = s->speed_limit;
    out->speed_unit = s->speed_unit;
    out->legal_limit = s->legal_speed_limit;
    out->legal_unit = s->legal_speed_unit;
    out->lane_active = s->lane_active;
    if ((s->valid_attributes & OPEN_NAVI_CLUSTER_CURRENT_ROAD) != 0u) {
        (void)copy_road(&s->current_road, out->cur_road, sizeof(out->cur_road));
    }
    if ((s->valid_attributes & OPEN_NAVI_CLUSTER_NEXT_ROAD) != 0u) {
        (void)copy_road(&s->next_road, out->next_road, sizeof(out->next_road));
    }
}

uint32_t open_navi_cluster_agent_publish(const OpenNaviClusterNavigationState *state)
{
    struct cagent_state agent;
    struct cagent_client *client;
    uint32_t status;
    if (state == NULL) {
        return OPEN_NAVI_CLUSTER_INVALID_ARGUMENT;
    }
    to_agent_state(state, &agent);
    (void)pthread_mutex_lock(&g_lock);
    client = g_client;
    (void)pthread_mutex_unlock(&g_lock);
    status = client != NULL && cagent_client_update(client, &agent) == 0 ? 0u : 1u;
    if (status == 0u) {
        (void)pthread_mutex_lock(&g_lock);
        g_sent_any = 1;
        (void)pthread_mutex_unlock(&g_lock);
    }
    return status;
}

void open_navi_cluster_agent_release(void)
{
    struct cagent_client *client;
    int had;
    (void)pthread_mutex_lock(&g_lock);
    refresh_locked(1);
    client = g_client;
    had = g_sent_any;
    g_sent_any = 0;
    (void)pthread_mutex_unlock(&g_lock);
    if (client != NULL && had != 0) {
        (void)cagent_client_release(client);
    }
}

#if defined(OPEN_NAVI_TESTING)
void open_navi_cluster_agent_test_configure(const char *marker_path, const char *socket_path)
{
    (void)pthread_mutex_lock(&g_lock);
    g_marker = marker_path != NULL ? marker_path : OPEN_NAVI_CLUSTER_AGENT_MARKER;
    g_socket = socket_path;
    if (g_client != NULL) {
        cagent_client_close(g_client);
        g_client = NULL;
    }
    g_sent_any = 0;
    refresh_locked(1);
    (void)pthread_mutex_unlock(&g_lock);
}
#endif

#else /* built without the cluster agent client: the marker is ignored */

int open_navi_cluster_agent_active(void) { return 0; }

uint32_t open_navi_cluster_agent_publish(const OpenNaviClusterNavigationState *state)
{
    (void)state;
    return 1u;
}

void open_navi_cluster_agent_release(void) {}

#if defined(OPEN_NAVI_TESTING)
void open_navi_cluster_agent_test_configure(const char *marker_path, const char *socket_path)
{
    (void)marker_path;
    (void)socket_path;
}
#endif

#endif
