#ifndef MOCK_RENDERER_PLATFORM_H
#define MOCK_RENDERER_PLATFORM_H

#include <stddef.h>

#include "mock_nav_backend.h"

int mock_renderer_platform_create(MockNavBackendRenderTargetV1 *target,
                                  char *diagnostic,
                                  size_t diagnostic_capacity);
void mock_renderer_platform_destroy(void);
int mock_renderer_platform_begin_frame(char *diagnostic,
                                       size_t diagnostic_capacity);
int mock_renderer_platform_end_frame(int present, char *diagnostic,
                                     size_t diagnostic_capacity);
/* Returns 1 for one decoded pointer event, 0 when no event is ready, 2 when
 * an unrelated Screen event was consumed, 3 for a transient Screen/property
 * failure, -2 when NAV_main was unrealized, and -1 for an internal failure.
 * This function never blocks. */
int mock_renderer_platform_poll_pointer(MockNavBackendPointerV1 *pointer_out,
                                        char *diagnostic,
                                        size_t diagnostic_capacity);

#endif
