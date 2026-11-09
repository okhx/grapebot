#include "assist/foresight.hpp"

#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>

using namespace grape::assist::foresight;

static int g_checks = 0;

#define CHECK(condition)                                     \
    do {                                                     \
        ++g_checks;                                          \
        if (!(condition)) {                                  \
            std::printf("FAILED at line %d: %s\n", __LINE__, \
                        #condition);                         \
            return 1;                                        \
        }                                                    \
    } while (false)

struct Ship {
    double m_y = 0.0;
    double m_velocity = 0.0;
    bool m_holding = false;

    void advance() {
        m_velocity = std::clamp(m_velocity + (m_holding ? 0.35 : -0.35), -3.0, 3.0);
        m_y += m_velocity;
    }
};

struct Level {
    std::function<bool(uint64_t, double)> m_open;
    uint64_t m_length = 0;
};

static uint64_t stateKey(uint64_t frame, const Ship& ship) {
    const auto y = static_cast<int64_t>(std::floor(ship.m_y / 6.0));
    const auto velocity = static_cast<int64_t>(std::floor(ship.m_velocity));
    return ((frame / 8) * 1000003ull) ^ (static_cast<uint64_t>(y) * 7919ull) ^
           (static_cast<uint64_t>(velocity) * 104729ull) ^ (ship.m_holding ? 0x9e3779b97f4a7c15ull : 0);
}

static Outcome simulate(const Level& level, Ship ship, uint64_t now, const Schedule& plan, int horizon,
                        const Controller* memory = nullptr) {
    for (int i = 0; i < horizon; ++i) {
        if (now + i >= level.m_length) return {horizon, false};
        if (plan.togglesAt(i)) ship.m_holding = !ship.m_holding;
        if (i > 0 && memory && memory->doomed(stateKey(now + i, ship))) return {i, true};
        ship.advance();
        if (!level.m_open(now + i, ship.m_y)) return {i, true};
    }
    return {horizon, false};
}

struct Run {
    int m_deaths = 0;
    int m_rewinds = 0;
    bool m_finished = false;
};

static Run play(const Level& level, int horizon, int history, int budget) {
    Controller controller;
    Ship ship;
    std::deque<std::pair<uint64_t, Ship>> snapshots;
    Run run;
    uint64_t frame = 0;
    int guard = 0;
    const Limits limits{horizon, budget, 4, horizon / 2};
    while (frame < level.m_length && guard++ < 200000) {
        snapshots.emplace_back(frame, ship);
        while (static_cast<int>(snapshots.size()) > history) snapshots.pop_front();

        const auto simulateFrom = [&](const Schedule& plan) {
            return simulate(level, ship, frame, plan, horizon, &controller);
        };
        const auto step = controller.step(frame, simulateFrom, limits,
                                          static_cast<int>(snapshots.size()), 20,
                                          stateKey(frame, ship));
        if (step.m_rewind > 0) {
            for (int i = 0; i < step.m_rewind - 1 && snapshots.size() > 1; ++i) snapshots.pop_back();
            frame = snapshots.back().first;
            ship = snapshots.back().second;
            snapshots.pop_back();
            controller.rewound(frame, step.m_deadEnd);
            ++run.m_rewinds;
            continue;
        }
        if (step.m_toggle) ship.m_holding = !ship.m_holding;
        ship.advance();
        if (!level.m_open(frame, ship.m_y)) {
            ++run.m_deaths;
            ship = Ship{};
            frame = 0;
            snapshots.clear();
            controller.reset();
            if (run.m_deaths > 20) break;
            continue;
        }
        ++frame;
    }
    run.m_finished = frame >= level.m_length;
    return run;
}

static double wave(uint64_t frame) {
    return 40.0 * std::sin(static_cast<double>(frame) / 37.0) +
           18.0 * std::sin(static_cast<double>(frame) / 11.0);
}

static int testSearchFindsPath() {
    const Level corridor{[](uint64_t frame, double y) { return std::abs(y - wave(frame)) < 14.0; }, 1000};
    Ship ship;
    const int horizon = 200;
    const auto found = search(
        [&](const Schedule& plan) { return simulate(corridor, ship, 0, plan, horizon); },
        Limits{horizon, 400, 4}, [](const Schedule&) { return true; });
    CHECK(found.m_simulations <= 400);
    if (!found.m_safe)
        std::printf("best survived %d of %d with %d toggles\n", found.m_best.m_outcome.m_survived, horizon,
                    found.m_best.m_plan.size());
    return 0;
}

static int testDeadEndIsReported() {
    const Level wall{[](uint64_t frame, double y) { return frame < 60 && std::abs(y) < 30.0; }, 1000};
    Ship ship;
    const auto found = search(
        [&](const Schedule& plan) { return simulate(wall, ship, 0, plan, 120); }, Limits{120, 64, 4},
        [](const Schedule&) { return true; });
    CHECK(!found.m_safe);
    CHECK(found.m_best.m_outcome.m_survived >= 55);
    return 0;
}

static int testTabuForcesADifferentPath() {
    Tabu tabu;
    tabu.forbid(100, 200, {110, 150});
    Schedule same;
    same.push(10);
    same.push(50);
    CHECK(!tabu.allows({}, 100, same));
    Schedule other;
    other.push(12);
    other.push(50);
    CHECK(tabu.allows({}, 100, other));
    Schedule tail;
    tail.push(39);
    CHECK(!tabu.allows({110}, 111, tail));
    Schedule otherTail;
    otherTail.push(41);
    CHECK(tabu.allows({110}, 111, otherTail));
    CHECK(tabu.allows({}, 201, same));
    return 0;
}

static int testRewindPolicyEscalates() {
    RewindPolicy policy;
    CHECK(policy.depthFor(500, 20, 1000, 100) == 20);
    CHECK(policy.depthFor(520, 20, 1000, 100) == 40);
    CHECK(policy.depthFor(510, 20, 1000, 100) == 80);
    CHECK(policy.depthFor(505, 20, 100, 100) == 99);
    CHECK(policy.depthFor(2000, 20, 1000, 100) == 20);
    return 0;
}

static int testCorridorWithoutDeaths() {
    const Level corridor{[](uint64_t frame, double y) { return std::abs(y - wave(frame)) < 14.0; }, 3000};
    const Run run = play(corridor, 144, 400, 64);
    std::printf("  corridor: finished=%d deaths=%d rewinds=%d\n", run.m_finished, run.m_deaths, run.m_rewinds);
    CHECK(run.m_finished);
    CHECK(run.m_deaths == 0);
    return 0;
}

static int testTrapNeedsRewind() {
    const Level trap{[](uint64_t frame, double y) {
                         if (frame < 600) return std::abs(y) < 25.0;
                         const bool upper = y > 30.0 && y < 70.0;
                         const bool lower = y < -30.0 && y > -70.0;
                         if (frame < 640) return std::abs(y) < 70.0;
                         if (frame < 900) return upper || (lower && frame < 820);
                         return std::abs(y - 50.0) < 25.0;
                     },
                     1200};
    const Run run = play(trap, 120, 600, 96);
    std::printf("  trap: finished=%d deaths=%d rewinds=%d\n", run.m_finished, run.m_deaths, run.m_rewinds);
    CHECK(run.m_finished);
    CHECK(run.m_deaths == 0);
    CHECK(run.m_rewinds >= 1);
    return 0;
}

static int testImpossibleSectionGivesUp() {
    const Level impossible{[](uint64_t frame, double y) { return frame < 300 ? std::abs(y) < 25.0 : false; }, 600};
    const Run run = play(impossible, 120, 400, 64);
    CHECK(!run.m_finished);
    CHECK(run.m_deaths >= 1);
    return 0;
}

int main() {
    if (testSearchFindsPath()) return 1;
    if (testDeadEndIsReported()) return 1;
    if (testTabuForcesADifferentPath()) return 1;
    if (testRewindPolicyEscalates()) return 1;
    if (testCorridorWithoutDeaths()) return 1;
    if (testTrapNeedsRewind()) return 1;
    if (testImpossibleSectionGivesUp()) return 1;
    std::printf("foresight: %d checks passed\n", g_checks);
}
