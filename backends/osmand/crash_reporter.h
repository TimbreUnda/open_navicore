#pragma once

namespace navicore_osmand {

// Writes the faulting thread's PC, LR, SP and the stack words that look like
// shared-library code addresses to `path` on SIGSEGV/SIGBUS/SIGILL, then
// restores the default action so the process still dies and System Manager
// sees the crash. Pair the output with the "load_base" trace lines and
// addr2line to recover a backtrace when no core file is available (the
// radio's dumper skips processes over 10 MB). Safe to call more than once.
void install_crash_reporter(const char* path) noexcept;

} // namespace navicore_osmand
