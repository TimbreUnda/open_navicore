#pragma once
#include "qnx_pcm_out.h"
#include <string>
namespace navicore_osmand {
// The socket is owned only by the speech worker. No detached watcher or polling
// thread: each PCM chunk checks for stock revocation or a lost provider.
class StockFocus : public AudioFocus {
public:
    explicit StockFocus(std::string path = "/tmp/navicore-audio.sock",
                        unsigned timeout_ms = 9000)
        : path_(std::move(path)), timeout_ms_(timeout_ms) {}
    ~StockFocus() override { release(); }
    bool request() override;
    bool valid() override;
    void release() override;
private:
    std::string path_;
    unsigned timeout_ms_;
    int fd_ = -1;
    bool granted_ = false;
};
}
