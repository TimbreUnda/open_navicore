#ifndef OPEN_NAVICORE_VP4R_FLAGS_H
#define OPEN_NAVICORE_VP4R_FLAGS_H

/*
 * Flags of the vp4r layer (ifs-build docs/LAYOUT.md): everything is on by default and a file
 * $VP4R_HOME/flags/<package>.d/<name> only switches a feature off ("no-<feature>") or carries a
 * value. $VP4R_HOME is /vp4r when the unit booted from the SD card (/vp4r/slots exists), else
 * /datastore/vp4r. Header-only so the provider (C) and the OsmAnd plugin (C++) share it.
 */

#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline const char *open_navi_vp4r_home(void)
{
    return access("/vp4r/slots", F_OK) == 0 ? "/vp4r" : "/datastore/vp4r";
}

/* Writes $VP4R_HOME/flags/<package>.d/<name> to out. Returns 0, or -1 when it does not fit. */
static inline int open_navi_vp4r_flag_path(const char *package, const char *name, char *out,
                                           size_t cap)
{
    int n = snprintf(out, cap, "%s/flags/%s.d/%s", open_navi_vp4r_home(), package, name);
    return n > 0 && (size_t)n < cap ? 0 : -1;
}

static inline int open_navi_vp4r_flag_exists(const char *package, const char *name)
{
    char path[256];
    return open_navi_vp4r_flag_path(package, name, path, sizeof(path)) == 0 &&
           access(path, F_OK) == 0;
}

#ifdef __cplusplus
}
#endif

#endif
