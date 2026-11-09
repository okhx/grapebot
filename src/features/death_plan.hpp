#pragma once

namespace grape::death {

struct Situation {
    bool m_anticheat = false;
    bool m_noclipOn = false;
    bool m_noclipCoversPlayer = false;
    bool m_preventDeath = false;
    bool m_autoFlip = false;
};

struct Plan {
    bool m_kill = true;
    bool m_showCause = false;
    bool m_tint = false;
    bool m_stepBack = false;
    bool m_flipAfterStep = false;
};

inline Plan planFor(const Situation& now) {
    Plan plan;
    const bool shielded = !now.m_anticheat && now.m_noclipOn && now.m_noclipCoversPlayer;
    plan.m_kill = !shielded;
    plan.m_showCause = plan.m_kill && !now.m_anticheat;
    plan.m_tint = shielded;
    plan.m_stepBack = now.m_preventDeath && !now.m_noclipOn && !now.m_anticheat;
    plan.m_flipAfterStep = plan.m_stepBack && now.m_autoFlip;
    return plan;
}

}
