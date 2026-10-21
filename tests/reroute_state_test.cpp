// Off-route timing, reroute back-off and the bench detour state machine.
#include "reroute_state.h"

#include <cstdio>
#include <cstdlib>

using namespace navicore_osmand;
using Clock = std::chrono::steady_clock;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,         \
                         __LINE__, #condition);                                 \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

namespace {

void test_off_route_needs_sustained_distance()
{
    RerouteState state;
    const auto t0 = Clock::now();
    // First off-route sample only starts the timer.
    CHECK(!state.off_route_long_enough(false, 80.0, t0));
    CHECK(!state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(3)));
    CHECK(state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(4)));
    // Back on the route resets the timer.
    CHECK(!state.off_route_long_enough(false, 10.0, t0 + std::chrono::seconds(5)));
    CHECK(!state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(6)));
    CHECK(!state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(9)));
    CHECK(state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(10)));
}

void test_arrival_and_threshold_clear_the_timer()
{
    RerouteState state;
    const auto t0 = Clock::now();
    CHECK(!state.off_route_long_enough(false, 80.0, t0));
    CHECK(!state.off_route_long_enough(true, 80.0, t0 + std::chrono::seconds(10)));
    CHECK(!state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(11)));
    CHECK(!state.off_route_long_enough(false, 50.0, t0 + std::chrono::seconds(12)));
    state.clear_off_route();
    CHECK(!state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(20)));
}

void test_failed_request_backs_off()
{
    RerouteState state;
    const auto t0 = Clock::now();
    CHECK(!state.retry_blocked(t0));
    state.request_failed(t0);
    CHECK(state.retry_blocked(t0 + std::chrono::seconds(9)));
    CHECK(!state.retry_blocked(t0 + std::chrono::seconds(10)));
}

void test_detour_starts_once_and_times_out()
{
    BenchDetour detour;
    const auto t0 = Clock::now();
    // Disabled: never starts.
    CHECK(!detour.advance(900.0, 1000.0, t0) && !detour.driving());
    detour.enable(true);
    CHECK(!detour.advance(300.0, 1000.0, t0) && !detour.driving());
    CHECK(detour.advance(400.0, 1000.0, t0));
    CHECK(detour.driving());
    // Starts only once.
    CHECK(!detour.advance(500.0, 1000.0, t0 + std::chrono::seconds(1)));
    CHECK(detour.driving());
    CHECK(!detour.advance(500.0, 1000.0, t0 + std::chrono::seconds(31)));
    CHECK(!detour.driving());
    CHECK(!detour.advance(900.0, 1000.0, t0 + std::chrono::seconds(40)));
    CHECK(!detour.driving());
}

void test_detour_ends_when_route_recovers()
{
    BenchDetour detour;
    detour.enable(true);
    const auto t0 = Clock::now();
    CHECK(detour.advance(400.0, 1000.0, t0));
    detour.route_recovered();
    CHECK(!detour.driving());
    CHECK(!detour.advance(900.0, 1000.0, t0 + std::chrono::seconds(1)));
}

} // namespace

int main()
{
    test_off_route_needs_sustained_distance();
    test_arrival_and_threshold_clear_the_timer();
    test_failed_request_backs_off();
    test_detour_starts_once_and_times_out();
    test_detour_ends_when_route_recovers();
    std::puts("reroute_state_test passed");
    return 0;
}
