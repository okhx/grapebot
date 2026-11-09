#include "features/death_plan.hpp"

#include <cstdio>

using namespace grape::death;

static int g_failures = 0;

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            std::printf("FAILED at line %d: %s\n", __LINE__, #condition); \
            ++g_failures;                                                 \
        }                                                                 \
    } while (false)

int main() {
    const Plan plain = planFor({});
    CHECK(plain.m_kill && plain.m_showCause && !plain.m_tint && !plain.m_stepBack);

    const Plan shielded = planFor({.m_noclipOn = true, .m_noclipCoversPlayer = true});
    CHECK(!shielded.m_kill && !shielded.m_showCause && shielded.m_tint);

    const Plan otherPlayer = planFor({.m_noclipOn = true, .m_noclipCoversPlayer = false,
                                      .m_preventDeath = true});
    CHECK(otherPlayer.m_kill && otherPlayer.m_showCause && !otherPlayer.m_tint);
    CHECK(!otherPlayer.m_stepBack);

    const Plan anticheat = planFor({.m_anticheat = true, .m_noclipOn = true,
                                    .m_noclipCoversPlayer = true, .m_preventDeath = true});
    CHECK(anticheat.m_kill && !anticheat.m_showCause && !anticheat.m_tint);
    CHECK(!anticheat.m_stepBack);

    const Plan guarded = planFor({.m_preventDeath = true});
    CHECK(guarded.m_kill && guarded.m_stepBack && !guarded.m_flipAfterStep);

    const Plan flipping = planFor({.m_preventDeath = true, .m_autoFlip = true});
    CHECK(flipping.m_stepBack && flipping.m_flipAfterStep);

    const Plan flipOnly = planFor({.m_autoFlip = true});
    CHECK(!flipOnly.m_stepBack && !flipOnly.m_flipAfterStep);

    if (g_failures == 0) std::printf("death_plan: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
