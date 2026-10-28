// Host stub for OsmAnd::LogPrintf (core-legacy Logging.h). Errors always go to stderr;
// OSMAND_HOST_LOG=1 adds warnings, OSMAND_HOST_LOG=debug adds Info/Debug too.
#include "Logging.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace OsmAnd {
void LogPrintf(LogSeverityLevel level, const char* format, ...)
{
    static const char* env = std::getenv("OSMAND_HOST_LOG");
    const bool verbose = env && std::strcmp(env, "debug") == 0;
    if (level != LogSeverityLevel::Error) {
        if (!env) return;
        if (level != LogSeverityLevel::Warning && !verbose) return;
    }
    const char* tag = level == LogSeverityLevel::Error ? "E"
                    : level == LogSeverityLevel::Warning ? "W"
                    : level == LogSeverityLevel::Info ? "I" : "D";
    std::fprintf(stderr, "[osmand %s] ", tag);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}
} // namespace OsmAnd
