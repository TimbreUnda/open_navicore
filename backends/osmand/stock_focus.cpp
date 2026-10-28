#include "stock_focus.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace navicore_osmand {
bool StockFocus::request() {
    release();
    struct sockaddr_un sa{};
    if (path_.size() >= sizeof(sa.sun_path)) return false;
    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    fcntl(fd_, F_SETFD, FD_CLOEXEC);
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
    sa.sun_family = AF_UNIX;
    std::memcpy(sa.sun_path, path_.c_str(), path_.size() + 1);
    // A local listener accepts without waiting for stock. The bounded wait below
    // is for its asynchronous allocation/preparation reply.
    if (connect(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0 ||
        send(fd_, "A", 1, MSG_NOSIGNAL) != 1) {
        std::fprintf(stderr, "voice: stock focus connect: %s\n", std::strerror(errno));
        release(); return false;
    }
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
    while (std::chrono::steady_clock::now() < end) {
        struct pollfd p = {fd_, POLLIN, 0};
        int rc = poll(&p, 1, 50);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) break;
        if (rc > 0) {
            char reply = 0;
            if (recv(fd_, &reply, 1, 0) == 1 && reply == 'G') {
                granted_ = true;
                std::fprintf(stderr, "voice: stock focus granted\n");
                return valid();
            }
            std::fprintf(stderr, "voice: stock focus rejected reply=%u\n", unsigned(reply));
            break;
        }
    }
    std::fprintf(stderr, "voice: stock focus unavailable\n");
    release(); return false;
}
bool StockFocus::valid() {
    if (!granted_ || fd_ < 0) return false;
    struct pollfd p = {fd_, POLLIN, 0};
    int rc = poll(&p, 1, 0);
    if (rc == 0 || (rc < 0 && errno == EINTR)) return true;
    // R, EOF, or any unexpected message invalidates the grant. Keep the fd open
    // until release(): the server must not acknowledge before PCM is closed.
    granted_ = false;
    std::fprintf(stderr, "voice: stock focus revoked/disconnected\n");
    return false;
}
void StockFocus::release() {
    granted_ = false;
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
}
}
