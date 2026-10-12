#include "thread_priority.h"

#include <pthread.h>
#include <sched.h>

#include <algorithm>

namespace navicore_osmand {

bool lower_current_thread_priority(int steps) noexcept
{
    int policy = 0;
    sched_param parameters{};
    if (pthread_getschedparam(pthread_self(), &policy, &parameters) != 0)
        return false;
    const int minimum = std::max(1, sched_get_priority_min(policy));
    parameters.sched_priority =
        std::max(minimum, parameters.sched_priority - steps);
    return pthread_setschedparam(pthread_self(), policy, &parameters) == 0;
}

} // namespace navicore_osmand
