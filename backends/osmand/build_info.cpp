#include "build_info.h"

#ifndef OPEN_NAVICORE_GIT
#define OPEN_NAVICORE_GIT "unknown"
#endif

namespace navicore_osmand {

const char* build_git() noexcept { return OPEN_NAVICORE_GIT; }
const char* build_date() noexcept { return __DATE__ " " __TIME__; }

} // namespace navicore_osmand
