/*
 * screen_survey: read-only report of Screen displays and windows.
 *
 * Default: an application context (cannot disturb other clients) reports
 * every display's id, size, type, attached state and the windows this
 * context can see. --display-manager uses a display-manager context, which
 * may see more display-level state; it still creates no windows. It never
 * creates a window-manager context: only one may exist and the HMI owns it.
 * For other processes' windows, also run survey_debugfs.sh.
 */
#define _POSIX_C_SOURCE 200112L

#include <screen/screen.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void window_line(screen_window_t w)
{
    int pipeline = -1, usage = -1, format = -1, transparency = -1, zorder = -1;
    int visible = -1, alpha = -1, pid = -1, buffers = -1;
    int size[2] = {-1, -1}, pos[2] = {-1, -1};
    char id[64] = "", klass[64] = "";
    screen_get_window_property_iv(w, SCREEN_PROPERTY_PIPELINE, &pipeline);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_USAGE, &usage);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_FORMAT, &format);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_TRANSPARENCY, &transparency);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_ZORDER, &zorder);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_VISIBLE, &visible);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_GLOBAL_ALPHA, &alpha);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_OWNER_PID, &pid);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_BUFFER_COUNT, &buffers);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_SIZE, size);
    screen_get_window_property_iv(w, SCREEN_PROPERTY_POSITION, pos);
    screen_get_window_property_cv(w, SCREEN_PROPERTY_ID_STRING, sizeof(id) - 1, id);
    screen_get_window_property_cv(w, SCREEN_PROPERTY_CLASS, sizeof(klass) - 1, klass);
    printf("  window id=%s class=%s pid=%d pipeline=%d usage=0x%x format=%d "
           "transparency=%d zorder=%d visible=%d alpha=%d buffers=%d "
           "size=%dx%d pos=%d,%d\n",
           id[0] ? id : "-", klass[0] ? klass : "-", pid, pipeline,
           (unsigned)usage, format, transparency, zorder, visible, alpha,
           buffers, size[0], size[1], pos[0], pos[1]);
}

int main(int argc, char **argv)
{
    const int manager = argc > 1 && !strcmp(argv[1], "--display-manager");
    screen_context_t ctx = NULL;
    int count = 0, i;

    if (screen_create_context(&ctx, manager ? SCREEN_DISPLAY_MANAGER_CONTEXT
                                            : SCREEN_APPLICATION_CONTEXT) != 0) {
        printf("screen_create_context(%s) FAILED errno=%d (%s)\n",
               manager ? "display-manager" : "application", errno, strerror(errno));
        return 1;
    }
    printf("context=%s\n", manager ? "display-manager" : "application");

    if (screen_get_context_property_iv(ctx, SCREEN_PROPERTY_DISPLAY_COUNT, &count) == 0 &&
        count > 0) {
        screen_display_t *displays = calloc((size_t)count, sizeof(*displays));
        if (displays &&
            screen_get_context_property_pv(ctx, SCREEN_PROPERTY_DISPLAYS,
                                           (void **)displays) == 0) {
            for (i = 0; i < count; ++i) {
                int id = -1, type = -1, attached = -1, size[2] = {-1, -1};
                char name[64] = "";
                screen_get_display_property_iv(displays[i], SCREEN_PROPERTY_ID, &id);
                screen_get_display_property_iv(displays[i], SCREEN_PROPERTY_TYPE, &type);
                screen_get_display_property_iv(displays[i], SCREEN_PROPERTY_ATTACHED,
                                               &attached);
                screen_get_display_property_iv(displays[i], SCREEN_PROPERTY_SIZE, size);
                screen_get_display_property_cv(displays[i], SCREEN_PROPERTY_ID_STRING,
                                               sizeof(name) - 1, name);
                printf("display index=%d id=%d name=%s type=%d attached=%d size=%dx%d\n",
                       i, id, name[0] ? name : "-", type, attached, size[0], size[1]);
            }
        }
        free(displays);
    } else {
        printf("display count unavailable errno=%d\n", errno);
    }

    count = 0;
    if (screen_get_context_property_iv(ctx, SCREEN_PROPERTY_WINDOW_COUNT, &count) == 0) {
        printf("windows visible to this context=%d\n", count);
        if (count > 0) {
            screen_window_t *windows = calloc((size_t)count, sizeof(*windows));
            if (windows &&
                screen_get_context_property_pv(ctx, SCREEN_PROPERTY_WINDOWS,
                                               (void **)windows) == 0)
                for (i = 0; i < count; ++i) window_line(windows[i]);
            free(windows);
        }
    } else {
        printf("window list unavailable errno=%d (%s)\n", errno, strerror(errno));
    }
    screen_destroy_context(ctx);
    return 0;
}
