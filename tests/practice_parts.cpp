#include "checkpoint/bounded_history.hpp"
#include "checkpoint/platformer_trail.hpp"

#include <cstdio>
#include <vector>

using namespace grape::snap;

static int g_failures = 0;

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            std::printf("FAILED at line %d: %s\n", __LINE__, #condition); \
            ++g_failures;                                                 \
        }                                                                 \
    } while (false)

static std::vector<int> g_released;

struct Recorder {
    void operator()(int& value) const { g_released.push_back(value); }
};

static void history() {
    g_released.clear();
    BoundedHistory<int, Recorder> frames;
    for (int i = 1; i <= 5; ++i) frames.push(i, 3);
    CHECK(frames.size() == 3);
    CHECK(frames.newest() == 5);
    CHECK((g_released == std::vector<int>{1, 2}));

    frames.push(6, 1);
    CHECK(frames.size() == 1 && frames.newest() == 6);
    CHECK((g_released == std::vector<int>{1, 2, 3, 4, 5}));

    frames.push(7, 0);
    CHECK(frames.size() == 1 && frames.newest() == 7);

    frames.push(8, 10);
    frames.push(9, 10);
    frames.shrinkTo(2);
    CHECK(frames.size() == 2 && *frames.rbegin() == 9 && *(frames.rbegin() + 1) == 8);

    frames.dropNewest();
    CHECK(frames.size() == 1 && frames.newest() == 8);
    frames.clear();
    CHECK(frames.empty());
    CHECK(g_released.back() == 8);
    frames.dropNewest();
    CHECK(frames.empty());
}

static void trail() {
    std::vector<char> reset;
    const auto deactivate = [&](char marker) { reset.push_back(marker); };

    PlatformerTrail<int, char> stops;
    stops.push(1, 'a');
    stops.push(2, 'b');
    stops.push(3, 'a');
    stops.push(4, 'c');
    CHECK(stops.depth() == 4 && stops.latestRespawn() == 4);

    stops.rewindTo(1, deactivate);
    CHECK(stops.depth() == 1 && stops.latestRespawn() == 1);
    CHECK((reset == std::vector<char>{'c', 'b'}));

    reset.clear();
    stops.rewindTo(5, deactivate);
    CHECK(stops.depth() == 1 && reset.empty());

    stops.push(2, 'a');
    stops.abandon(false, deactivate);
    CHECK(stops.empty() && reset.empty());

    stops.push(1, 'x');
    stops.push(2, 'x');
    stops.abandon(true, deactivate);
    CHECK(stops.empty() && (reset == std::vector<char>{'x', 'x'}));
}

int main() {
    history();
    trail();
    if (g_failures == 0) std::printf("practice_parts: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
