/* nv_navigation_launcher: bench-only launcher for the stock nv_navigation.
 *
 * System Manager's Launch110 row can point here instead of
 * /pas/bin/nv_navigation (see README.md). Without the marker below it simply
 * execs the real binary with the same arguments (same pid, same name). With
 * /datastore/navicore-backend/enable-vpspy-mem present, and dev-tools'
 * libvpspy-mem.so mounted, it adds the heap tracker:
 *   LD_PRELOAD=/usr/lib/libvpspy-mem.so  VPSPY_OUT=/tmp/vpspy-mem.nav.log
 *   VPSPY_INTERVAL=30  VPSPY_TOP=40
 */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef REAL_BINARY
#define REAL_BINARY "/pas/bin/nv_navigation"
#endif
#ifndef MARKER
#define MARKER "/datastore/navicore-backend/enable-vpspy-mem"
#endif
#define HOOK "/usr/lib/libvpspy-mem.so"

int main(int argc, char **argv)
{
    (void)argc;
    if (access(MARKER, F_OK) == 0 && access(HOOK, R_OK) == 0) {
        (void)setenv("LD_PRELOAD", HOOK, 1);
        (void)setenv("VPSPY_OUT", "/tmp/vpspy-mem.nav.log", 1);
        (void)setenv("VPSPY_INTERVAL", "30", 1);
        (void)setenv("VPSPY_TOP", "40", 1);
    }
    execv(REAL_BINARY, argv);
    perror("nv_navigation_launcher: execv " REAL_BINARY);
    return 127;
}
