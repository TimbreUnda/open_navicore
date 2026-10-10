#include "diagnostics.h"

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
    (void)write(fd, message, std::strlen(message));
    (void)write(fd, "\n", 1u);
    (void)close(fd);
}
}
