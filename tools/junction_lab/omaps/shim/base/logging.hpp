// Shim for Organic Maps base/logging.hpp. LOG(LWARNING/LERROR, (...)) prints only the
// source location to stderr, and only when JUNCTION_LAB_OMAPS_LOG is set; the message
// arguments are not evaluated. LDEBUG/LINFO are dropped.
#pragma once

#include <cstdio>
#include <cstdlib>

namespace omaps_shim
{
enum LogLevel
{
  LDEBUG,
  LINFO,
  LWARNING,
  LERROR,
  LCRITICAL
};

inline void Log(LogLevel level, char const * file, int line)
{
  static bool const enabled = std::getenv("JUNCTION_LAB_OMAPS_LOG") != nullptr;
  if (enabled && level >= LWARNING)
    std::fprintf(stderr, "omaps %s %s:%d\n", level == LWARNING ? "warning" : "error", file, line);
}
}  // namespace omaps_shim

using omaps_shim::LCRITICAL;
using omaps_shim::LDEBUG;
using omaps_shim::LERROR;
using omaps_shim::LINFO;
using omaps_shim::LWARNING;

#define LOG(level, msg) ::omaps_shim::Log(level, __FILE__, __LINE__)
