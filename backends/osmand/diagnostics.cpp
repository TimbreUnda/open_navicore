#include "diagnostics.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace navicore_osmand {
void trace(const char* message) noexcept
{
    const int fd = open("/tmp/open-navicore-osmand.log",
                        O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return;
    const ssize_t message_written = write(fd, message, std::strlen(message));
    const ssize_t newline_written = write(fd, "\n", 1u);
    (void)message_written;
    (void)newline_written;
    (void)close(fd);
}

void trace_pointer(const char* stage, uint32_t action, int32_t x,
                   int32_t y) noexcept
{
    char message[128] = {};
    (void)std::snprintf(message, sizeof(message),
                        "pointer=%s action=%u x=%d y=%d", stage,
                        static_cast<unsigned int>(action),
                        static_cast<int>(x), static_cast<int>(y));
    trace(message);
}

void trace_result(const char* stage, std::uint64_t revision,
                  std::size_t count) noexcept
{
    char message[128] = {};
    (void)std::snprintf(message, sizeof(message),
                        "%s revision=%llu count=%lu", stage,
                        static_cast<unsigned long long>(revision),
                        static_cast<unsigned long>(count));
    trace(message);
}
}
