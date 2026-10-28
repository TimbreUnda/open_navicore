#ifndef OPEN_NAVI_AUDIO_FOCUS_H
#define OPEN_NAVI_AUDIO_FOCUS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One stream connection owns one prompt. Client sends A; server replies G only
 * after stock allocation and preparation, D on failure, R on revocation.
 * Closing the connection releases/acknowledges the stock source. */
#define OPEN_NAVI_AUDIO_SOCKET "/tmp/navicore-audio.sock"
int open_navi_audio_focus_start(const char *path);
void open_navi_audio_focus_stop(void);
uint32_t open_navi_audio_focus_event(uint32_t slot, uint32_t handle,
                                   uint32_t source, uint32_t value);
/* Provider-owned lifecycle-gated transport; never called under the state lock. */
uint32_t open_navi_audio_focus_dispatch(uint32_t slot, uint32_t handle,
                                      uint32_t source, uint32_t value);
uint32_t open_navi_audio_focus_next_handle(void);
void open_navi_audio_focus_trace(const char *event, uint32_t handle, uint32_t value);
#ifdef __cplusplus
}
#endif
#endif
