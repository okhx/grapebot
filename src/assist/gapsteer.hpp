#pragma once

#include <algorithm>
#include <cmath>

namespace grape::assist {

struct GapState {
    float m_lower = 0.0f;
    float m_upper = 0.0f;
    float m_velocity = 0.0f;
    float m_acceleration = 0.0f;

    constexpr float center() const { return (m_lower + m_upper) * .5f; }
    constexpr float height() const { return m_upper - m_lower; }
};

struct WaveState {
    float m_bottom = 0.0f;
    float m_top = 0.0f;
    float m_step = 0.0f;
    float m_climbRate = 1.0f;
    bool m_upsideDown = false;

    constexpr float center() const { return (m_bottom + m_top) * .5f; }
    constexpr float height() const { return m_top - m_bottom; }
};

enum class Steer {
    Dither,
    Up,
    Down,
};

struct SteerResult {
    Steer m_action = Steer::Dither;
    bool m_emergency = false;
    int m_lead = 0;
};

constexpr float futureGapOffset(float velocity, float acceleration,
                                float frames) {
    const float linear = velocity * frames;
    const float curve = acceleration * frames * frames * .125f;
    const float limit = (linear < 0.0f ? -linear : linear) * .5f;
    return linear + std::clamp(curve, -limit, limit);
}

constexpr float pursuitLead(float error, float climbRate, float gapVelocity,
                            float maxLead) {
    if (!(climbRate > 0.0f) || !(maxLead > 0.0f)) return 0.0f;
    const float absError = error < 0.0f ? -error : error;
    float lead = absError / climbRate;
    if (lead > maxLead) lead = maxLead;
    for (int i = 0; i < 2; ++i) {
        const float aim = error + gapVelocity * lead;
        const float absAim = aim < 0.0f ? -aim : aim;
        lead = absAim / climbRate;
        if (lead > maxLead) lead = maxLead;
    }
    return lead;
}

constexpr float climbRateOf(double yVelocity) {
    const float step = static_cast<float>(yVelocity * .03);
    const float magnitude = step < 0.0f ? -step : step;
    return std::clamp(magnitude, .35f, 20.0f);
}

struct GapTracker {
    static constexpr float BLEND = .6f;
    static constexpr float REVERSAL_NOISE = .35f;

    float m_center = 0.0f;
    float m_lower = 0.0f;
    float m_upper = 0.0f;
    float m_velocity = 0.0f;
    float m_acceleration = 0.0f;
    bool m_valid = false;
    int m_against = 0;

    constexpr void reset() { *this = GapTracker{}; }

    constexpr GapState state() const {
        return {m_lower, m_upper, m_velocity, m_acceleration};
    }

    constexpr void observe(float lower, float upper, int elapsed) {
        const float center = (lower + upper) * .5f;
        const int steps = elapsed < 1 ? 1 : elapsed;
        if (!m_valid) {
            m_center = center;
            m_lower = lower;
            m_upper = upper;
            m_velocity = 0.0f;
            m_acceleration = 0.0f;
            m_against = 0;
            m_valid = true;
            return;
        }

        const float raw = (center - m_center) / steps;
        const bool disagrees = raw * m_velocity < 0.0f &&
                               (raw < 0.0f ? -raw : raw) > REVERSAL_NOISE;
        m_against = disagrees ? m_against + 1 : 0;

        float velocity;
        if (m_against >= 2) {
            velocity = raw * .5f;
            m_acceleration = 0.0f;
            m_against = 0;
        } else {
            velocity = m_velocity + (raw - m_velocity) * BLEND;
            const float rawAccel = (velocity - m_velocity) / steps;
            m_acceleration = std::clamp(
                m_acceleration + (rawAccel - m_acceleration) * BLEND, -15.0f,
                15.0f);
        }

        m_center = center;
        m_lower = lower;
        m_upper = upper;
        m_velocity = std::clamp(velocity, -60.0f, 60.0f);
    }

    constexpr void coast(int elapsed) {
        if (!m_valid) return;
        const int steps = elapsed < 1 ? 1 : elapsed;
        const float offset =
            futureGapOffset(m_velocity, m_acceleration, static_cast<float>(steps));
        m_velocity = std::clamp(m_velocity + m_acceleration * steps, -60.0f,
                                60.0f);
        m_center += offset;
        m_lower += offset;
        m_upper += offset;
    }
};

constexpr SteerResult steerForGap(GapState gap, WaveState wave,
                                  int lookahead) {
    const float freeSpace = std::max(0.0f, gap.height() - wave.height());
    const float marginFloor = std::min(.1f, freeSpace * .5f);
    const float margin = std::clamp(freeSpace * .15f, marginFloor, 2.0f);

    int safetyFrames = std::clamp(lookahead, 1, 30);
    if (freeSpace < 6.0f) {
        const float tightness = 1.0f + (6.0f - freeSpace) / 6.0f;
        safetyFrames = std::clamp(
            static_cast<int>(safetyFrames * tightness + 0.5f), 1, 30);
    }

    const float centerError = gap.center() - wave.center();
    const float lead =
        pursuitLead(centerError, wave.m_climbRate, gap.m_velocity, 30.0f);
    safetyFrames =
        std::clamp(std::max(safetyFrames, static_cast<int>(lead + .999f)), 1,
                   30);

    const float safetyGapOffset = futureGapOffset(
        gap.m_velocity, gap.m_acceleration, static_cast<float>(safetyFrames));
    const float safetyWaveOffset = wave.m_step * std::min(safetyFrames, 2);
    const float futureLower =
        wave.m_bottom + safetyWaveOffset - (gap.m_lower + safetyGapOffset);
    const float futureUpper =
        gap.m_upper + safetyGapOffset - (wave.m_top + safetyWaveOffset);

    if (futureLower <= margin && futureLower < futureUpper)
        return {Steer::Up, true, safetyFrames};
    if (futureUpper <= margin && futureUpper < futureLower)
        return {Steer::Down, true, safetyFrames};

    const float aimOffset =
        futureGapOffset(gap.m_velocity, gap.m_acceleration, 1.0f);
    const float aimError =
        gap.center() + aimOffset - (wave.center() + wave.m_step);

    if (aimError > 0.0f) return {Steer::Up, false, safetyFrames};
    if (aimError < 0.0f) return {Steer::Down, false, safetyFrames};
    return {Steer::Dither, false, safetyFrames};
}

}
