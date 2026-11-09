#pragma once

#include <algorithm>
#include <cmath>

namespace grape::steps {

struct CeilingInput {
    bool m_visualUpdates = false;
    bool m_dynamicUpr = false;
    float m_fpsTarget = 0.0f;
    double m_tps = 240.0;
};

inline int visualCeiling(int base, const CeilingInput& in) {
    if (!in.m_visualUpdates || in.m_dynamicUpr) return base;
    const float fps = in.m_fpsTarget <= 10.0f ? 240.0f : in.m_fpsTarget;
    const int perVisualFrame = static_cast<int>(in.m_tps) / static_cast<int>(fps);
    return base * std::max(1, perVisualFrame);
}

struct BudgetInput {
    float m_dt = 0.0f;
    double m_carry = 0.0;
    float m_tick = 0.0f;
    float m_timeWarp = 1.0f;
    bool m_realTime = false;
    bool m_rendering = false;
    bool m_paused = false;
    int m_ceiling = 1;
};

struct Budget {
    bool m_valid = false;
    int m_steps = 0;
    double m_carry = 0.0;
    bool m_render = false;
    bool m_clearExtraDelta = false;
};

inline Budget budgetFor(const BudgetInput& in) {
    Budget out;
    const float available = static_cast<float>(in.m_dt + in.m_carry);
    const float tick = in.m_tick * std::fmin(in.m_timeWarp, 1.0f);
    if (tick == 0.0f) return out;
    out.m_valid = true;

    const bool capped = !in.m_realTime && !in.m_rendering;
    int steps = static_cast<int>(std::floor(available / tick));
    if (capped) steps = std::min(steps, in.m_ceiling);

    const float leftover = available - static_cast<float>(steps) * tick;
    out.m_carry = capped && steps == in.m_ceiling ? 0.0 : leftover;
    out.m_steps = steps;

    if (in.m_paused && !in.m_rendering) {
        out.m_steps = 1;
        out.m_carry = 0.0;
        out.m_render = true;
        out.m_clearExtraDelta = true;
    }
    return out;
}

}
