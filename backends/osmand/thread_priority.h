#pragma once

namespace navicore_osmand {

// Lowers the calling thread's scheduling priority by `steps` (never below 1)
// so background workers (pre-cache, symbol extraction) yield to the render
// thread on the two-core radio. Returns false if the scheduler refused.
bool lower_current_thread_priority(int steps) noexcept;

} // namespace navicore_osmand
