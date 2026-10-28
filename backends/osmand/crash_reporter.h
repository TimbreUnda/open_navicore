#pragma once

namespace navicore_osmand {

// Writes the faulting thread's PC, LR, SP and the stack words that look like
// shared-library or libc code addresses to `path` (and, when given, to
// `persistent_path`, which survives a reboot) on SIGSEGV/SIGBUS/SIGILL/SIGABRT,
// then restores the default action so the process still dies and System
// Manager sees the crash. std::terminate logs the exception type first. Pair
// the output with the "load_base" trace lines and addr2line to recover a
// backtrace when no core file is available (the radio's dumper skips
// processes over 10 MB). Safe to call more than once.
void install_crash_reporter(const char* path,
                            const char* persistent_path = nullptr) noexcept;

// Appends one timestamped line to the same files (Qt fatal messages, exit).
void crash_report_line(const char* text) noexcept;

} // namespace navicore_osmand
