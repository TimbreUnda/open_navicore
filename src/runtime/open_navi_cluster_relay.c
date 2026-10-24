#define _POSIX_C_SOURCE 200112L

#include "open_navi_cluster_agent.h"

#ifdef OPEN_NAVI_CLUSTER_AGENT

#include <cagent/proto.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
static int g_running;
static volatile int g_stop;
static int g_fd = -1;
static const char *g_path = CAGENT_RELAY_DEFAULT_SOCKET;

static void publish_update(const struct cagent_state *s)
{
    OpenNaviClusterNavigationState state;
    memset(&state, 0, sizeof(state));
    state.valid_attributes = s->valid;
    state.route_guidance_state = s->route_state;
    state.navigation_status = s->nav_status;
    state.route_guidance_symbol = s->symbol;
    state.distance_destination = s->dest_dist;
    state.distance_destination_unit = s->dest_unit;
    state.eta_hour = s->eta_h;
    state.eta_minute = s->eta_m;
    state.rta_hour = s->rta_h;
    state.rta_minute = s->rta_m;
    state.distance_maneuver_tenths = s->man_dist_tenths;
    state.distance_maneuver_unit = s->man_unit;
    state.current_road.bytes = s->cur_road;
    state.current_road.length = strlen(s->cur_road);
    state.current_road.encoding = OPEN_NAVI_CLUSTER_TEXT_UTF8;
    state.next_road.bytes = s->next_road;
    state.next_road.length = strlen(s->next_road);
    state.next_road.encoding = OPEN_NAVI_CLUSTER_TEXT_UTF8;
    state.lane_active = s->lane_active;
    state.bar_percent = s->bar;
    state.compass = s->compass;
    state.speed_limit = s->speed_limit;
    state.speed_unit = s->speed_unit;
    state.legal_speed_limit = s->legal_limit;
    state.legal_speed_unit = s->legal_unit;
    (void)open_navi_cluster_backend_publish_pas(&state);
}

static void *serve(void *unused)
{
    (void)unused;
    while (g_stop == 0) {
        struct pollfd pfd;
        int burst;
        pfd.fd = g_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 500) <= 0) {
            continue;
        }
        for (burst = 0; burst < 16; ++burst) {
            uint8_t buf[CAGENT_MAX_DATAGRAM + 1];
            struct cagent_msg m;
            ssize_t n = recv(g_fd, buf, sizeof(buf), 0);
            if (n < 0) {
                break;
            }
            if (cagent_unpack(buf, (size_t)n, &m) != 0) {
                continue;
            }
            if (m.type == CAGENT_UPDATE) {
                publish_update(&m.state);
            } else {
                OpenNaviClusterNavigationState empty;
                memset(&empty, 0, sizeof(empty));
                (void)open_navi_cluster_backend_publish_pas(&empty);
            }
        }
    }
    return NULL;
}

void open_navi_cluster_relay_start(void)
{
    struct sockaddr_un sa;
    int fd, fl;
    (void)pthread_mutex_lock(&g_lock);
    if (g_running != 0 || open_navi_cluster_agent_enabled() == 0 ||
        strlen(g_path) >= sizeof(sa.sun_path)) {
        (void)pthread_mutex_unlock(&g_lock);
        return;
    }
    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd >= 0) {
        fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) {
            (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        }
        (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
        memset(&sa, 0, sizeof(sa));
        sa.sun_family = AF_UNIX;
        strcpy(sa.sun_path, g_path);
        (void)unlink(g_path);
        if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
            (void)chmod(g_path, 0666);
            g_fd = fd;
            g_stop = 0;
            if (pthread_create(&g_thread, NULL, serve, NULL) == 0) {
                g_running = 1;
            } else {
                g_fd = -1;
            }
        }
        if (g_running == 0) {
            close(fd);
        }
    }
    (void)pthread_mutex_unlock(&g_lock);
}

void open_navi_cluster_relay_stop(void)
{
    pthread_t thread;
    int fd;
    (void)pthread_mutex_lock(&g_lock);
    if (g_running == 0) {
        (void)pthread_mutex_unlock(&g_lock);
        return;
    }
    g_stop = 1;
    thread = g_thread;
    fd = g_fd;
    g_running = 0;
    (void)pthread_mutex_unlock(&g_lock);
    (void)pthread_join(thread, NULL); /* the poll timeout bounds this at 500 ms */
    (void)close(fd);
    (void)unlink(g_path);
    g_fd = -1;
}

#if defined(OPEN_NAVI_TESTING)
void open_navi_cluster_relay_test_configure(const char *socket_path)
{
    g_path = socket_path != NULL ? socket_path : CAGENT_RELAY_DEFAULT_SOCKET;
}
#endif

#else /* no cluster-agent checkout at build time */

void open_navi_cluster_relay_start(void) {}
void open_navi_cluster_relay_stop(void) {}

#if defined(OPEN_NAVI_TESTING)
void open_navi_cluster_relay_test_configure(const char *socket_path) { (void)socket_path; }
#endif

#endif
