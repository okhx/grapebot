#include "engine/step_budget.hpp"

#include <cstdio>

using namespace grape::steps;

static int g_failures = 0;

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            std::printf("FAILED at line %d: %s\n", __LINE__, #condition); \
            ++g_failures;                                                 \
        }                                                                 \
    } while (false)

int main() {
    const float tick = 1.0f / 256.0f;

    const Budget even = budgetFor({.m_dt = 1.0f / 64.0f, .m_tick = tick, .m_ceiling = 100});
    CHECK(even.m_valid && even.m_steps == 4 && !even.m_render);

    const Budget carry = budgetFor({.m_dt = 1.0f / 64.0f, .m_carry = 0.003, .m_tick = tick, .m_ceiling = 100});
    CHECK(carry.m_steps == 4 && carry.m_carry > 0.0 && carry.m_carry < tick);

    const Budget capped = budgetFor({.m_dt = 1.0f, .m_tick = tick, .m_ceiling = 10});
    CHECK(capped.m_steps == 10 && capped.m_carry == 0.0);

    const Budget realTime = budgetFor({.m_dt = 1.0f, .m_tick = tick, .m_realTime = true, .m_ceiling = 10});
    CHECK(realTime.m_steps == 256);

    const Budget rendering = budgetFor({.m_dt = 1.0f, .m_tick = tick, .m_rendering = true,
                                        .m_paused = true, .m_ceiling = 10});
    CHECK(rendering.m_steps == 256 && !rendering.m_render);

    const Budget paused = budgetFor({.m_dt = 1.0f, .m_carry = 0.5, .m_tick = tick, .m_paused = true, .m_ceiling = 10});
    CHECK(paused.m_steps == 1 && paused.m_carry == 0.0 && paused.m_render && paused.m_clearExtraDelta);

    const Budget slowWarp = budgetFor({.m_dt = 1.0f / 64.0f, .m_tick = tick, .m_timeWarp = 0.5f, .m_ceiling = 100});
    CHECK(slowWarp.m_steps == 8);

    const Budget fastWarp = budgetFor({.m_dt = 1.0f / 64.0f, .m_tick = tick, .m_timeWarp = 2.0f, .m_ceiling = 100});
    CHECK(fastWarp.m_steps == 4);

    CHECK(!budgetFor({.m_dt = 1.0f, .m_tick = tick, .m_timeWarp = 0.0f}).m_valid);

    CHECK(visualCeiling(3, {}) == 3);
    CHECK(visualCeiling(3, {.m_visualUpdates = true, .m_fpsTarget = 60.0f, .m_tps = 1000.0}) == 48);
    CHECK(visualCeiling(3, {.m_visualUpdates = true, .m_dynamicUpr = true, .m_fpsTarget = 60.0f, .m_tps = 1000.0}) == 3);
    CHECK(visualCeiling(3, {.m_visualUpdates = true, .m_fpsTarget = 5.0f, .m_tps = 480.0}) == 6);
    CHECK(visualCeiling(3, {.m_visualUpdates = true, .m_fpsTarget = 144.0f, .m_tps = 60.0}) == 3);

    if (g_failures == 0) std::printf("step_budget: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
