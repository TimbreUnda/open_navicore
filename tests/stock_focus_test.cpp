#include "stock_focus.h"
#include "open_navi_audio_focus.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <unistd.h>
using namespace navicore_osmand;
static std::atomic<unsigned> next_id{100}, request_id{0}, prepare_id{0}, releases{0}, acks{0};
static std::atomic<bool> auto_grant{false}, auto_prepare{false};
extern "C" uint32_t open_navi_audio_focus_next_handle() { return ++next_id; }
extern "C" void open_navi_audio_focus_trace(const char*, uint32_t, uint32_t) {}
extern "C" uint32_t open_navi_audio_focus_dispatch(uint32_t slot, uint32_t handle,
                                                  uint32_t source, uint32_t value) {
    assert(source == 1);
    if (slot == 6) {
        request_id = handle;
        if (auto_grant) open_navi_audio_focus_event(5, handle, source, 1);
    } else if (slot == 7) {
        prepare_id = handle;
        if (auto_prepare) open_navi_audio_focus_event(7, handle, source, 1);
    } else if (slot == 8) {
        ++releases;
    } else if (slot == 9) {
        assert(handle == 999 && value == 1); ++acks;
    } else assert(false);
    return 0;
}
template<class Predicate> static void await(Predicate ready) {
    for (int i = 0; i < 200 && !ready(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(ready());
}
int main() {
    char temp[] = "/tmp/onc-focus-XXXXXX";
    assert(mkdtemp(temp));
    std::string path = std::string(temp) + "/focus.sock";
    StockFocus missing(path, 100);
    assert(!missing.request());
    assert(open_navi_audio_focus_start(path.c_str()));
    StockFocus focus(path, 1500);
    std::atomic<bool> finished{false}, result{false};
    std::thread client([&] { result = focus.request(); finished = true; });
    await([] { return request_id.load() != 0; });
    unsigned h = request_id;
    assert(open_navi_audio_focus_event(5, h + 1, 1, 1) == 1);
    assert(open_navi_audio_focus_event(5, h, 8, 1) == 1);
    assert(!finished);
    assert(open_navi_audio_focus_event(5, h, 1, 1) == 0);
    await([&] { return prepare_id == h; });
    assert(!finished);
    assert(open_navi_audio_focus_event(7, h, 1, 1) == 0);
    client.join(); assert(result && focus.valid());
    assert(open_navi_audio_focus_event(8, 999, 1, 1) == 0);
    await([&] { return !focus.valid(); });
    assert(acks == 0);
    focus.release();
    await([] { return acks == 1; });
    request_id = 0; finished = false;
    std::thread denied([&] { result = focus.request(); finished = true; });
    await([] { return request_id.load() != 0; });
    assert(open_navi_audio_focus_event(5, request_id, 1, 0) == 0);
    denied.join(); assert(!result);
    await([] { return releases == 1; });
    request_id = 0;
    StockFocus impatient(path, 100);
    assert(!impatient.request());
    await([] { return releases == 2; });
    auto_grant = auto_prepare = true;
    assert(focus.request() && focus.valid());
    focus.release();
    await([] { return releases == 3; });
    assert(focus.request());
    open_navi_audio_focus_stop();
    assert(!focus.valid()); focus.release();
    assert(releases == 4);
    rmdir(temp);
}
