#define _POSIX_C_SOURCE 200809L
#include "open_navi_audio_focus.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* PanaNav source 1 -> navigation source 21. Not as_audio_service's NAVI=8. */
#define PANA_SOURCE 1u
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t thread;
static int running, stopping, listener = -1;
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static uint32_t active_handle, deactivation_handle;
static int allocation = -1, preparation = -1;

static long long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int stopped(void)
{
    int result;
    pthread_mutex_lock(&lock); result = stopping; pthread_mutex_unlock(&lock);
    return result;
}
static void nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
}
uint32_t open_navi_audio_focus_event(uint32_t slot, uint32_t handle,
                                   uint32_t source, uint32_t value)
{
    uint32_t result = 1u;
    pthread_mutex_lock(&lock);
    if (active_handle != 0u && source == PANA_SOURCE) {
        if (slot == 5u && handle == active_handle) {
            allocation = value == 1u ? 1 : 0; result = 0u;
        } else if (slot == 7u && handle == active_handle) {
            preparation = value == 1u ? 1 : 0; result = 0u;
        } else if (slot == 8u && handle != 0u) {
            deactivation_handle = handle; result = 0u;
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}
static int send_byte(int fd, char c)
{
    return send(fd, &c, 1, MSG_NOSIGNAL) == 1;
}
static void serve_prompt(int fd)
{
    struct pollfd p = {fd, POLLIN, 0};
    long long deadline = now_ms() + 1000;
    char command = 0;
    uint32_t handle, revoke;
    int alloc, prep, preparing = 0, granted = 0, revoked = 0;
    while (!stopped() && now_ms() < deadline) {
        if (poll(&p, 1, 50) > 0) {
            if (recv(fd, &command, 1, 0) != 1) return;
            break;
        }
    }
    if (command != 'A' || stopped()) return;
    handle = open_navi_audio_focus_next_handle();
    if (handle == 0u) return;
    pthread_mutex_lock(&lock);
    active_handle = handle; deactivation_handle = 0u;
    allocation = preparation = -1;
    pthread_mutex_unlock(&lock);
    open_navi_audio_focus_trace("request", handle, PANA_SOURCE);
    if (open_navi_audio_focus_dispatch(6u, handle, PANA_SOURCE, 0u) != 0u) {
        (void)send_byte(fd, 'D');
        goto done;
    }
    deadline = now_ms() + 8000;
    while (!stopped()) {
        pthread_mutex_lock(&lock);
        alloc = allocation; prep = preparation; revoke = deactivation_handle;
        pthread_mutex_unlock(&lock);
        if (revoke != 0u && !revoked) {
            revoked = 1;
            open_navi_audio_focus_trace("revoke", handle, revoke);
            if (!send_byte(fd, 'R')) break;
            /* The worker closes PCM before disconnecting. Bound a dead client. */
            deadline = now_ms() + 2000;
        }
        if (!revoked && !granted) {
            if (alloc == 0 || prep == 0) {
                open_navi_audio_focus_trace("denied", handle, 0u);
                (void)send_byte(fd, 'D'); break;
            }
            if (alloc == 1 && !preparing) {
                preparing = 1;
                if (open_navi_audio_focus_dispatch(7u, handle, PANA_SOURCE, 0u) != 0u) {
                    (void)send_byte(fd, 'D'); break;
                }
            }
            if (alloc == 1 && prep == 1) {
                if (!send_byte(fd, 'G')) break;
                granted = 1;
                deadline = now_ms() + 60000;
                open_navi_audio_focus_trace("granted", handle, PANA_SOURCE);
            }
        }
        if (now_ms() >= deadline) {
            open_navi_audio_focus_trace("expired", handle, (uint32_t)granted);
            (void)send_byte(fd, granted ? 'R' : 'D'); break;
        }
        p.revents = 0;
        if (poll(&p, 1, 50) > 0) {
            char c;
            ssize_t n = recv(fd, &c, 1, 0);
            if (n == 0 || n > 0 || (errno != EAGAIN && errno != EINTR)) break;
        }
    }
done:
    pthread_mutex_lock(&lock);
    revoke = deactivation_handle;
    active_handle = deactivation_handle = 0u;
    pthread_mutex_unlock(&lock);
    if (revoke != 0u)
        (void)open_navi_audio_focus_dispatch(9u, revoke, PANA_SOURCE, 1u);
    else
        (void)open_navi_audio_focus_dispatch(8u, handle, PANA_SOURCE, 0u);
    open_navi_audio_focus_trace("released", handle, revoke);
}
static void *serve(void *unused)
{
    (void)unused;
    while (!stopped()) {
        struct pollfd p = {listener, POLLIN, 0};
        int fd;
        if (poll(&p, 1, 50) <= 0) continue;
        fd = accept(listener, NULL, NULL);
        if (fd < 0) continue;
        nonblock(fd);
        serve_prompt(fd);
        close(fd);
    }
    return NULL;
}
int open_navi_audio_focus_start(const char *path)
{
    struct sockaddr_un sa;
    int fd;
    if (path == NULL) path = OPEN_NAVI_AUDIO_SOCKET;
    if (strlen(path) >= sizeof(sa.sun_path)) return 0;
    pthread_mutex_lock(&lock);
    if (running) { pthread_mutex_unlock(&lock); return 1; }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { pthread_mutex_unlock(&lock); return 0; }
    nonblock(fd);
    memset(&sa, 0, sizeof(sa)); sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, path);
    (void)unlink(path);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(fd, 1) != 0) {
        close(fd); pthread_mutex_unlock(&lock); return 0;
    }
    (void)chmod(path, 0666);
    strcpy(socket_path, path);
    listener = fd; stopping = 0;
    if (pthread_create(&thread, NULL, serve, NULL) != 0) {
        close(fd); unlink(path); listener = -1;
        pthread_mutex_unlock(&lock); return 0;
    }
    running = 1;
    pthread_mutex_unlock(&lock);
    open_navi_audio_focus_trace("listening", 0u, 0u);
    return 1;
}
void open_navi_audio_focus_stop(void)
{
    pthread_mutex_lock(&lock);
    if (!running) { pthread_mutex_unlock(&lock); return; }
    stopping = 1;
    pthread_mutex_unlock(&lock);
    pthread_join(thread, NULL);
    close(listener); unlink(socket_path);
    pthread_mutex_lock(&lock);
    listener = -1; running = 0;
    pthread_mutex_unlock(&lock);
}
