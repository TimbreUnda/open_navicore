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
    // Exactly at the threshold counts as on the route: it resets the timer
    // even though the car has been "off" for well over 4 s.
    CHECK(!state.off_route_long_enough(false, 50.0, t0 + std::chrono::seconds(20)));
    CHECK(!state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(21)));
    CHECK(!state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(24)));
    CHECK(state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(25)));
    // clear_off_route() restarts the timer, so a long gap does not count.
    state.clear_off_route();
    CHECK(!state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(60)));
    CHECK(state.off_route_long_enough(false, 80.0, t0 + std::chrono::seconds(64)));
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

void test_detour_boundaries()
{
    const auto t0 = Clock::now();
    const double route = 2000.0;
    // Exactly at the start fraction does not start; just past it does.
    BenchDetour early;
    early.enable(true);
    CHECK(!early.advance(BenchDetour::kStartRouteFraction * route, route, t0));
    CHECK(early.advance(BenchDetour::kStartRouteFraction * route + 1.0, route, t0));
    // The timeout is exactly 30 s: still driving at 30 s, over just after.
    BenchDetour timed;
    timed.enable(true);
    CHECK(timed.advance(route, route, t0));
    timed.advance(route, route, t0 + std::chrono::seconds(29));
    CHECK(timed.driving());
    timed.advance(route, route, t0 + std::chrono::seconds(30));
    CHECK(timed.driving());
    timed.advance(route, route, t0 + std::chrono::seconds(30) + std::chrono::milliseconds(1));
    CHECK(!timed.driving());
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

void test_wrong_way_needs_sustained_reverse_driving()
{
    RerouteState state;
    const auto t0 = Clock::now();
    const auto at = [&](int seconds) { return t0 + std::chrono::seconds(seconds); };
    // Along the route, sideways or not moving: nothing.
    CHECK(!state.wrong_way_long_enough(false, 10.0, at(0)));
    CHECK(!state.wrong_way_long_enough(false, 90.0, at(10)));
    CHECK(!state.wrong_way_long_enough(false, std::nullopt, at(20)));
    // Against it (either sign, any wrap) for 5 s.
    CHECK(!state.wrong_way_long_enough(false, -175.0, at(30)));
    CHECK(!state.wrong_way_long_enough(false, 535.0, at(34)));
    CHECK(state.wrong_way_long_enough(false, 180.0, at(35)));
    // Then not again on the new route until the car has driven along one.
    CHECK(!state.wrong_way_long_enough(false, 180.0, at(36)));
    CHECK(!state.wrong_way_long_enough(false, 180.0, at(60)));
    state.clear_off_route();
    CHECK(!state.wrong_way_long_enough(false, 180.0, at(61)));
    CHECK(!state.wrong_way_long_enough(false, 180.0, at(70)));
    CHECK(!state.wrong_way_long_enough(false, 0.0, at(71)));
    CHECK(!state.wrong_way_long_enough(false, 180.0, at(72)));
    CHECK(state.wrong_way_long_enough(false, 180.0, at(77)));
    // Stopping, arriving or a turn back along it restart the timer.
    CHECK(!state.wrong_way_long_enough(false, 0.0, at(80)));
    CHECK(!state.wrong_way_long_enough(false, 180.0, at(81)));
    CHECK(!state.wrong_way_long_enough(false, std::nullopt, at(84)));
    CHECK(!state.wrong_way_long_enough(false, 180.0, at(85)));
    CHECK(!state.wrong_way_long_enough(true, 180.0, at(88)));
    CHECK(!state.wrong_way_long_enough(false, 180.0, at(89)));
    CHECK(!state.wrong_way_long_enough(false, 180.0, at(93)));
    CHECK(state.wrong_way_long_enough(false, 180.0, at(94)));
}

void test_heading_memory_keeps_a_moving_heading_for_a_while()
{
    HeadingMemory memory;
    const auto t0 = Clock::now();
    CHECK(!memory.heading(t0));
    // Too slow or no fix: no heading.
    memory.observe(true, 1.0, 90.0, t0);
    memory.observe(false, 10.0, 90.0, t0);
    CHECK(!memory.heading(t0));
    memory.observe(true, 10.0, 90.0, t0);
    CHECK(memory.heading(t0) && *memory.heading(t0) == 90.0);
    // Stopped at lights: the moving heading stays, the standstill one is ignored.
    memory.observe(true, 0.2, 200.0, t0 + std::chrono::seconds(10));
    CHECK(*memory.heading(t0 + std::chrono::seconds(30)) == 90.0);
    CHECK(!memory.heading(t0 + std::chrono::seconds(31)));
    // A shorter age asks whether the car moves now.
    CHECK(!memory.heading(t0 + std::chrono::seconds(3), std::chrono::seconds(2)));
    memory.observe(true, 5.0, 180.0, t0 + std::chrono::seconds(40));
    CHECK(*memory.heading(t0 + std::chrono::seconds(41)) == 180.0);
}

void test_heading_memory_keeps_a_parked_heading_at_its_spot()
{
    HeadingMemory memory;
    const auto t0 = Clock::now();
    const double lat = 50.4501, lon = 30.5234;
    memory.observe(true, 10.0, 41.0, t0, &lat, &lon);
    // Stopped: saved once, and kept at the spot long after the 30 s.
    memory.observe(true, 0.0, 0.0, t0 + std::chrono::seconds(5));
    HeadingMemory::Parked parked;
    CHECK(memory.take_save(parked) && parked.heading_degrees == 41.0 && parked.latitude == lat);
    CHECK(!memory.take_save(parked));
    memory.observe(true, 0.0, 0.0, t0 + std::chrono::seconds(6));
    CHECK(!memory.take_save(parked));   // still the same stop
    const auto later = t0 + std::chrono::hours(3);
    CHECK(!memory.heading(later));
    CHECK(memory.start_heading(later, lat + 0.0001, lon) &&
          *memory.start_heading(later, lat + 0.0001, lon) == 41.0);   // ~11 m: GPS noise
    // Moved away (slowly, no heading): forgotten there.
    CHECK(!memory.start_heading(later, lat + 0.001, lon));            // ~111 m
    // After a restart the saved spot gives the heading, but only there.
    HeadingMemory restarted;
    restarted.restore(parked);
    CHECK(!restarted.heading(t0));
    CHECK(restarted.start_heading(t0, lat, lon) && *restarted.start_heading(t0, lat, lon) == 41.0);
    CHECK(!restarted.start_heading(t0, lat + 0.001, lon));
    CHECK(!restarted.take_save(parked));
    // Real motion replaces the restored record.
    restarted.observe(true, 8.0, 222.0, t0, &lat, &lon);
    CHECK(*restarted.start_heading(t0, lat + 0.001, lon) == 222.0);
}

int main()
{
    test_off_route_needs_sustained_distance();
    test_heading_memory_keeps_a_parked_heading_at_its_spot();
    test_arrival_and_threshold_clear_the_timer();
    test_failed_request_backs_off();
    test_detour_starts_once_and_times_out();
    test_detour_boundaries();
    test_detour_ends_when_route_recovers();
    test_wrong_way_needs_sustained_reverse_driving();
    test_heading_memory_keeps_a_moving_heading_for_a_while();
    std::puts("reroute_state_test passed");
    return 0;
}
