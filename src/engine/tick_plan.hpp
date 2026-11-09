#pragma once

#include <algorithm>
#include <cmath>

namespace grape::ticks {

struct Plan {
    int m_steps = 0;
    double m_carry = 0.0;
};

struct Request {
    double m_carry = 0.0;
    double m_dt = 0.0;
    double m_timestep = 1.0 / 240.0;
    bool m_held = false;
    bool m_stepWanted = false;
    int m_cap = 0;
};

inline constexpr double ROUNDING_SLACK = 1e-7;

inline Plan plan(const Request& request) {
    if (request.m_held) return {request.m_stepWanted ? 1 : 0, 0.0};
    if (!(request.m_timestep > 0.0) || !std::isfinite(request.m_dt))
        return {0, request.m_carry};

    const double total =
        std::max(0.0, request.m_carry + std::max(0.0, request.m_dt));
    const int steps = static_cast<int>(
        std::floor(total / request.m_timestep + ROUNDING_SLACK));
    if (request.m_cap > 0 && steps >= request.m_cap)
        return {request.m_cap, 0.0};
    return {steps, std::max(0.0, total - steps * request.m_timestep)};
}

struct Vanilla {
    int m_steps = 0;
    double m_delta = 0.0;
    double m_extra = 0.0;
};

inline Vanilla vanilla(double dt, double extra, double timestep) {
    if (!(timestep > 0.0)) return {0, 0.0, extra};
    const double total = dt + extra;
    const double steps = std::round(total / timestep);
    return {static_cast<int>(steps), steps * timestep, total - steps * timestep};
}

inline double timestep(double tps, double timeWarp) {
    const double tickRate = std::isfinite(tps) && tps > 0.0 ? tps : 240.0;
    return std::min(timeWarp, 1.0) / tickRate;
}

inline bool batchesExact(double timestep) { return timestep == 1.0 / 240.0; }

inline float stepDelta60(double timestep) { return static_cast<float>(timestep * 60.0); }

inline constexpr double PREDICTION_BUDGET_TPS = 480.0;

inline int predictionSteps(double seconds, double tps, double timeWarp, int maxSteps) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) return 0;
    const double rate = std::isfinite(tps) && tps > 0.0 ? tps : 240.0;
    const double warp = std::max(std::abs(std::isfinite(timeWarp) ? timeWarp : 1.0), 0.001);
    const double budget = seconds * PREDICTION_BUDGET_TPS / warp;
    const double wanted = seconds * rate / warp;
    int steps = static_cast<int>(std::min({wanted, budget, 1.0e6}));
    if (maxSteps > 0) steps = std::min(steps, maxSteps);
    return std::max(steps, 0);
}

inline bool batchKeepsTick(int ticks, double timestep) {
    if (ticks <= 1) return true;
    const float single = static_cast<float>(timestep * 60.0);
    const float spread =
        static_cast<float>(ticks * timestep * 60.0) / static_cast<float>(ticks);
    return spread == single;
}

inline int batchSize(int steps, double timestep, bool resuming) {
    if (steps <= 1 || resuming || batchesExact(timestep)) return steps;
    for (int ticks = steps; ticks > 1; --ticks)
        if (batchKeepsTick(ticks, timestep)) return ticks;
    return 1;
}

}
