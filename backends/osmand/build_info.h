#pragma once

namespace navicore_osmand {

// The backend's git description (commit, "-dirty" for local changes) and
// compile time, for the developer menu.
const char* build_git() noexcept;
const char* build_date() noexcept;

} // namespace navicore_osmand
