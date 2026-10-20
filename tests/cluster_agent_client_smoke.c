#define _POSIX_C_SOURCE 200112L

/* Open NaviCore as a cluster-agent client: with the marker present the semantic cluster state goes
 * to the agent socket (decoded here with the agent's own decoder) and not to PAS; without it,
 * nothing changes. */
#include <open_navicore/cluster.h>

#include "open_navi_cluster_agent.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef OPEN_NAVI_CLUSTER_AGENT
#include <cagent/client.h>
#include <cagent/proto.h>
#endif

#ifdef OPEN_NAVI_CLUSTER_AGENT
#include <time.h>

static void usleep_us(long us)
{
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000;
    nanosleep(&ts, NULL);
}
#endif

static uint32_t g_pas_calls;
static uint8_t g_last_record[OPEN_NAVI_CLUSTER_RECORD_SIZE];

uint32_t open_navi_callback_send_cluster(const void *record_0xa5c)
{
    assert(record_0xa5c != NULL);
    memcpy(g_last_record, record_0xa5c, sizeof(g_last_record));
    ++g_pas_calls;
    return 0u;
}

#ifndef OPEN_NAVI_CLUSTER_AGENT
int main(void)
{
    /* No cluster-agent checkout at build time: the stub must leave PAS publishing intact. */
    OpenNaviClusterNavigationState state;
    memset(&state, 0, sizeof(state));
    assert(open_navi_cluster_agent_active() == 0);
    assert(open_navi_cluster_backend_publish(&state) == 0u && g_pas_calls == 1u);
    puts("cluster agent client smoke passed (stub)");
    return 0;
}
#else

static int g_rx = -1;
static char g_dir[256], g_marker[300], g_sock[300];

static void touch(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT, 0600);
    assert(fd >= 0);
    close(fd);
}

static void open_agent_socket(void)
{
    struct sockaddr_un sa;
    g_rx = socket(AF_UNIX, SOCK_DGRAM, 0);
    assert(g_rx >= 0);
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, g_sock);
    unlink(g_sock);
    assert(bind(g_rx, (struct sockaddr *)&sa, sizeof(sa)) == 0);
    fcntl(g_rx, F_SETFL, fcntl(g_rx, F_GETFL, 0) | O_NONBLOCK);
}

/* 0 = decoded, -2 = nothing queued */
static int receive(struct cagent_msg *m)
{
    uint8_t buf[CAGENT_MAX_DATAGRAM + 1];
    ssize_t n = recv(g_rx, buf, sizeof(buf), 0);
    if (n < 0) {
        return -2;
    }
    return cagent_unpack(buf, (size_t)n, m);
}

static OpenNaviClusterNavigationState sample(void)
{
    static const char road[] = "\xd0\x92\xd1\x83\xd0\xbb\xd0\xb8\xd1\x86\xd1\x8f\x01\x41"; /* Cyrillic + control + A */
    OpenNaviClusterNavigationState s;
    memset(&s, 0, sizeof(s));
    s.valid_attributes = OPEN_NAVI_CLUSTER_ROUTE_GUIDANCE_STATE | OPEN_NAVI_CLUSTER_ROUTE_SYMBOL |
                         OPEN_NAVI_CLUSTER_DISTANCE_MANEUVER | OPEN_NAVI_CLUSTER_CURRENT_ROAD |
                         OPEN_NAVI_CLUSTER_BAR_PERCENT | OPEN_NAVI_CLUSTER_NEXT_ROAD;
    s.route_guidance_state = OPEN_NAVI_CLUSTER_ROUTE_MANOEUVRE;
    s.route_guidance_symbol = 12;
    s.distance_maneuver_tenths = 3500u;
    s.distance_maneuver_unit = OPEN_NAVI_CLUSTER_DISTANCE_METRES;
    s.current_road.bytes = road;
    s.current_road.length = sizeof(road) - 1u;
    s.current_road.encoding = OPEN_NAVI_CLUSTER_TEXT_UTF8;
    s.next_road.bytes = "Next";
    s.next_road.length = 4u;
    s.next_road.encoding = OPEN_NAVI_CLUSTER_TEXT_ASCII;
    s.bar_percent = 50u;
    return s;
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    OpenNaviClusterNavigationState state = sample();
    struct cagent_msg m;
    uint32_t calls;

    snprintf(g_dir, sizeof(g_dir), "%s/onc-cagent-%ld", tmp != NULL ? tmp : "/tmp", (long)getpid());
    assert(mkdir(g_dir, 0700) == 0);
    snprintf(g_marker, sizeof(g_marker), "%s/marker", g_dir);
    snprintf(g_sock, sizeof(g_sock), "%s/agent.sock", g_dir);
    open_agent_socket();

    /* default: marker absent -> PAS path, nothing on the socket */
    open_navi_cluster_agent_test_configure(g_marker, g_sock);
    assert(open_navi_cluster_agent_active() == 0);
    assert(open_navi_cluster_backend_publish(&state) == 0u);
    assert(g_pas_calls == 1u);
    assert(receive(&m) == -2);

    /* marker present -> agent path, PAS untouched */
    touch(g_marker);
    open_navi_cluster_agent_test_configure(g_marker, g_sock);
    assert(open_navi_cluster_agent_active() == 1);
    calls = g_pas_calls;
    assert(open_navi_cluster_backend_publish(&state) == 0u);
    assert(g_pas_calls == calls);
    assert(receive(&m) == 0);
    assert(m.type == CAGENT_UPDATE && m.source == CAGENT_SRC_NAVI);
    assert(m.state.valid == state.valid_attributes);
    assert(m.state.route_state == OPEN_NAVI_CLUSTER_ROUTE_MANOEUVRE && m.state.symbol == 12);
    assert(m.state.man_dist_tenths == 3500u && m.state.man_unit == OPEN_NAVI_CLUSTER_DISTANCE_METRES);
    assert(m.state.bar == 50u);
    assert(strcmp(m.state.cur_road, "\xd0\x92\xd1\x83\xd0\xbb\xd0\xb8\xd1\x86\xd1\x8f A") == 0);
    assert(strcmp(m.state.next_road, "Next") == 0);

    /* a state the backend rejects is never sent */
    state.route_guidance_symbol = 99;
    assert(open_navi_cluster_backend_publish(&state) == OPEN_NAVI_CLUSTER_INVALID_VALUE);
    assert(receive(&m) == -2);
    state = sample();

    /* over-long road text is cut on a character boundary, not rejected */
    {
        char longroad[400];
        size_t i;
        for (i = 0; i + 1 < sizeof(longroad); i += 2) {
            longroad[i] = (char)0xd0;
            longroad[i + 1] = (char)0x96; /* 2-byte letter, 127 scalars max for the backend */
        }
        state.current_road.bytes = longroad;
        state.current_road.length = 254u; /* 127 scalars */
        assert(open_navi_cluster_backend_publish(&state) == 0u);
        assert(receive(&m) == 0 && strlen(m.state.cur_road) == 254u);
        state = sample();
    }

    /* reset tells the agent this source has nothing to show (only after something was sent) */
    open_navi_cluster_backend_reset();
    assert(receive(&m) == 0 && m.type == CAGENT_RELEASE && m.source == CAGENT_SRC_NAVI);
    open_navi_cluster_backend_reset();
    assert(receive(&m) == -2);

    /* agent gone: the update is dropped, reported as failure, nothing blocks or crashes */
    close(g_rx);
    unlink(g_sock);
    assert(open_navi_cluster_backend_publish(&state) != 0u);

    /* marker removed -> back to PAS */
    unlink(g_marker);
    open_navi_cluster_agent_test_configure(g_marker, g_sock);
    calls = g_pas_calls;
    assert(open_navi_cluster_backend_publish(&state) == 0u && g_pas_calls == calls + 1u);

    /* relay host: the agent's relay backend feeds the local PAS path, RELEASE clears it */
    {
        char relay_path[300];
        struct cagent_client *agent;
        struct cagent_state s;
        int i;
        snprintf(relay_path, sizeof(relay_path), "%s/relay.sock", g_dir);
        touch(g_marker);
        open_navi_cluster_agent_test_configure(g_marker, g_sock);
        open_navi_cluster_relay_test_configure(relay_path);
        open_navi_cluster_relay_start();
        agent = cagent_client_open_at(CAGENT_SRC_AA, relay_path);
        assert(agent != NULL);
        memset(&s, 0, sizeof(s));
        s.valid = CAGENT_V_SYMBOL | CAGENT_V_BAR | CAGENT_V_MAN_DIST;
        s.symbol = 4; s.bar = 7; s.man_dist_tenths = 120; s.man_unit = 2;
        calls = g_pas_calls;
        assert(cagent_client_update(agent, &s) == 0);
        for (i = 0; i < 200 && g_pas_calls == calls; ++i) usleep_us(10000);
        assert(g_pas_calls == calls + 1u);
        assert(g_last_record[3] == 4 && g_last_record[0xa4c] == 7 && g_last_record[0x14c] == 2);
        assert((g_last_record[0xa58] | (g_last_record[0xa59] << 8)) == (int)s.valid);
        assert(cagent_client_release(agent) == 0);
        for (i = 0; i < 200 && g_pas_calls == calls + 1u; ++i) usleep_us(10000);
        assert(g_pas_calls == calls + 2u);
        assert(g_last_record[3] == 0 && g_last_record[0xa58] == 0 && g_last_record[0xa59] == 0);
        /* the symbol, bar and distance bits are reported as changed (their validity went away) */
        assert(((g_last_record[0xa56] | (g_last_record[0xa57] << 8)) & (int)s.valid) == (int)s.valid);
        /* a malformed datagram is ignored */
        {
            struct sockaddr_un sa;
            int raw = socket(AF_UNIX, SOCK_DGRAM, 0);
            memset(&sa, 0, sizeof(sa));
            sa.sun_family = AF_UNIX;
            strcpy(sa.sun_path, relay_path);
            sendto(raw, "garbage", 7, 0, (struct sockaddr *)&sa, sizeof(sa));
            close(raw);
            usleep_us(100000);
            assert(g_pas_calls == calls + 2u);
        }
        open_navi_cluster_relay_stop();
        assert(access(relay_path, F_OK) != 0);
        cagent_client_close(agent);
        unlink(g_marker);
        open_navi_cluster_agent_test_configure(g_marker, g_sock);
    }

    rmdir(g_dir);
    puts("cluster agent client smoke passed");
    return 0;
}
#endif
