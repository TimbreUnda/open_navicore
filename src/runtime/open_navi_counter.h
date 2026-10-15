#ifndef OPEN_NAVI_COUNTER_H
#define OPEN_NAVI_COUNTER_H

#include <stdint.h>

/* Internal telemetry counters saturate; request handles and sequence numbers
 * have their own wraparound rules. The caller owns any required locking. */
static inline void open_navi_increment_saturating(uint32_t *value)
{
    if (*value != UINT32_MAX) {
        ++*value;
    }
}

#endif
