#ifndef OPEN_NAVI_RENDERER_PLATFORM_H
#define OPEN_NAVI_RENDERER_PLATFORM_H

#include <stddef.h>

#include <open_navicore/backend.h>

int open_navi_renderer_platform_create(OpenNaviBackendRenderTargetV1 *target,
                                  char *diagnostic,
                                  size_t diagnostic_capacity);
void open_navi_renderer_platform_destroy(void);
int open_navi_renderer_platform_begin_frame(char *diagnostic,
                                       size_t diagnostic_capacity);
int open_navi_renderer_platform_end_frame(int present, char *diagnostic,
                                     size_t diagnostic_capacity);
/* Returns 1 for one decoded pointer event, 0 when no event is ready, 2 when
 * an unrelated Screen event was consumed, 3 for a transient Screen/property
 * failure, -2 when NAV_main was unrealized, and -1 for an internal failure.
 * This function never blocks. */
/* contacts: decode a second touch contact too (the backend wants
 * OpenNaviBackendPointerV2 contacts); otherwise only the first. */
/* Nonzero when poll_pointer should be called now (input may be queued).
 * Platforms without event notification always return 1. */
int open_navi_renderer_platform_input_pending(void);
int open_navi_renderer_platform_poll_pointer(OpenNaviBackendPointerV2 *pointer_out,
                                        int contacts,
                                        char *diagnostic,
                                        size_t diagnostic_capacity);

#endif
