#pragma once

#include <algorithm>
#include <cmath>

#include "gapsteer.hpp"

namespace grape::assist {

struct UfoPhysics {
    float m_gravityStep = -0.36f;
    float m_flapStep = 3.4f;
    float m_blueStep = -3.4f;
    float m_blackStep = -5.5f;
    float m_terminal = 12.0f;

    constexpr UfoPhysics flipped() const {
        return {-m_gravityStep, -m_flapStep, -m_blueStep, -m_blackStep,
                m_terminal};
    }
    constexpr float pull() const { return m_gravityStep < 0.0f ? -1.0f : 1.0f; }
};

struct UfoState {
    float m_bottom = 0.0f;
    float m_top = 0.0f;
    float m_velocity = 0.0f;

    constexpr float center() const { return (m_bottom + m_top) * .5f; }
    constexpr float height() const { return m_top - m_bottom; }
};

struct OrbContact {
    int m_pending = 0;
    int m_black = 0;
    bool m_blue = false;
    int m_sinceBlack = 1000;
    bool m_holdLine = false;
    float m_line = 0.0f;
};

constexpr float flapRise(const UfoPhysics& physics) {
    const float gravity = physics.m_gravityStep < 0.0f ? -physics.m_gravityStep
                                                       : physics.m_gravityStep;
    const float flap =
        physics.m_flapStep < 0.0f ? -physics.m_flapStep : physics.m_flapStep;
    if (!(gravity > 0.0f)) return 0.0f;
    return flap * flap / (2.0f * gravity);
}

constexpr float ufoArcPeak(const UfoPhysics& physics) {
    const float gravity = physics.m_gravityStep < 0.0f ? -physics.m_gravityStep
                                                       : physics.m_gravityStep;
    const float flap =
        physics.m_flapStep < 0.0f ? -physics.m_flapStep : physics.m_flapStep;
    if (!(gravity > 0.0f)) return 0.0f;
    return flap / gravity;
}

constexpr int ufoArcFrames(const UfoPhysics& physics) {
    return std::clamp(static_cast<int>(ufoArcPeak(physics) * 2.0f + .5f), 1,
                      60);
}

constexpr float ufoFlapTravel(const UfoPhysics& physics, int steps) {
    return physics.m_flapStep * steps +
           physics.m_gravityStep * steps * (steps + 1) * .5f;
}

constexpr GapState shiftGap(GapState gap, float frames) {
    const float offset =
        futureGapOffset(gap.m_velocity, gap.m_acceleration, frames);
    return {gap.m_lower + offset, gap.m_upper + offset,
            gap.m_velocity + gap.m_acceleration * frames, gap.m_acceleration};
}

constexpr float UFO_ARC_MEAN = 2.0f / 3.0f;

constexpr float ufoFlapHeight(GapState gap, float halfHeight,
                              const UfoPhysics& physics, float aim,
                              float arcMean = UFO_ARC_MEAN) {
    const float pull = physics.pull();
    const float rise = flapRise(physics);
    const int arc = ufoArcFrames(physics);
    const GapState aimed = shiftGap(gap, aim);

    float height =
        (aimed.m_lower + aimed.m_upper) * .5f + pull * rise * arcMean;

    for (int step = 1; step <= arc; ++step) {
        const GapState ahead = shiftGap(gap, static_cast<float>(step));
        const float travel = ufoFlapTravel(physics, step);
        if (pull < 0.0f) {
            height = std::min(height, ahead.m_upper - halfHeight - travel);
        } else {
            height = std::max(height, ahead.m_lower + halfHeight - travel);
        }
    }

    const float margin = std::min(1.5f, std::max(0.0f,
        ((gap.m_upper - gap.m_lower) - halfHeight * 2.0f) * .1f));
    if (pull < 0.0f) {
        height = std::max(height, gap.m_lower + halfHeight + margin);
    } else {
        height = std::min(height, gap.m_upper - halfHeight - margin);
    }
    return height;
}

constexpr bool ufoCrossesFlapHeight(GapState gap, UfoState ufo,
                                    const UfoPhysics& physics, int lead,
                                    float aim) {
    const float half = ufo.height() * .5f;
    const float pull = physics.pull();
    float velocity = ufo.m_velocity;
    float center = ufo.center();
    for (int step = 1; step <= lead; ++step) {
        velocity = std::clamp(velocity + physics.m_gravityStep,
                              -physics.m_terminal, physics.m_terminal);
        center += velocity;
        const float height = ufoFlapHeight(
            shiftGap(gap, static_cast<float>(step)), half, physics, aim);
        if (pull < 0.0f ? center <= height : center >= height) return true;
    }
    return false;
}

constexpr bool ufoOvershootsFarWall(GapState gap, UfoState ufo,
                                    const UfoPhysics& physics, int lead) {
    const float pull = physics.pull();
    float velocity = ufo.m_velocity;
    float bottom = ufo.m_bottom;
    float top = ufo.m_top;
    if (pull < 0.0f ? velocity <= 0.0f : velocity >= 0.0f) return false;
    for (int step = 1; step <= lead; ++step) {
        velocity = std::clamp(velocity + physics.m_gravityStep,
                              -physics.m_terminal, physics.m_terminal);
        bottom += velocity;
        top += velocity;
        const float offset = futureGapOffset(gap.m_velocity, gap.m_acceleration,
                                             static_cast<float>(step));
        if (pull < 0.0f ? top > gap.m_upper + offset
                        : bottom < gap.m_lower + offset)
            return true;
    }
    return false;
}

constexpr int UFO_CROSS_LEAD = 1;
constexpr int UFO_PRESS_SETTLE = 2;
constexpr int UFO_BLACK_ORB_MEMORY = 90;
constexpr int UFO_PRESS_CREDIT = 2;

struct ClickBurst {
    int m_taps = 0;
    bool m_hold = false;

    constexpr int presses() const { return m_taps + (m_hold ? 1 : 0); }
    constexpr bool empty() const { return presses() == 0; }
};

constexpr bool dropRingNeedsExtraPress(float velocity, bool upsideDown) {
    return upsideDown ? velocity >= 0.0f : velocity <= 0.0f;
}

static_assert(dropRingNeedsExtraPress(-4.0f, false), "falling wants the flap");
static_assert(!dropRingNeedsExtraPress(4.0f, false), "rising wants the drop");
static_assert(dropRingNeedsExtraPress(4.0f, true), "and upside down mirrors");
static_assert(!dropRingNeedsExtraPress(-4.0f, true));
static_assert(dropRingNeedsExtraPress(0.0f, false));

constexpr ClickBurst planClickBurst(int orbsInContact, bool wantFlap) {
    const int orbs = std::clamp(orbsInContact, 0, 8);
    if (!wantFlap) return orbs > 0 ? ClickBurst{orbs - 1, true} : ClickBurst{};
    return ClickBurst{orbs, true};
}

static_assert(planClickBurst(0, true).presses() == 1);
static_assert(planClickBurst(1, true).presses() == 2);
static_assert(planClickBurst(1, true).m_taps == 1);
static_assert(planClickBurst(1, true).m_hold);
static_assert(planClickBurst(2, true).presses() == 3);
static_assert(planClickBurst(2, true).m_taps == 2);
static_assert(planClickBurst(1, false).presses() == 1);
static_assert(planClickBurst(0, false).presses() == 0);

struct UfoPlan {
    bool m_press = false;
    float m_flapHeight = 0.0f;
    bool m_emergency = false;
    const char* m_reason = "coast";

    constexpr bool presses() const { return m_press; }
};

constexpr bool inBlackOrbSection(OrbContact contact) {
    return contact.m_black > 0 || contact.m_sinceBlack <= UFO_BLACK_ORB_MEMORY;
}

constexpr float clampBlackOrbLine(float line, GapState gap, float halfHeight,
                                  const UfoPhysics& physics) {
    const float pull = physics.pull();
    const int arc = ufoArcFrames(physics);
    for (int step = 1; step <= arc; ++step) {
        const GapState ahead = shiftGap(gap, static_cast<float>(step));
        const float travel = ufoFlapTravel(physics, step);
        line = pull < 0.0f ? std::min(line, ahead.m_upper - halfHeight - travel)
                           : std::max(line, ahead.m_lower + halfHeight - travel);
    }
    return pull < 0.0f ? std::max(line, gap.m_lower + halfHeight)
                       : std::min(line, gap.m_upper - halfHeight);
}

constexpr float followGapLine(float enteredAt, float anchor, GapState gap) {
    return enteredAt + (shiftGap(gap, 1.0f).center() - anchor);
}

static_assert(followGapLine(110.0f, 100.0f, {70.0f, 130.0f, 0.0f, 0.0f}) == 110.0f,
              "a still gap keeps the line where the UFO entered");
static_assert(followGapLine(110.0f, 100.0f, {90.0f, 150.0f, 2.0f, 0.0f}) == 132.0f,
              "a gap that moved and keeps moving carries the line with it");

struct LastPress {
    int m_since = 1000;
    bool m_up = true;
};

constexpr UfoPlan holdStraightOnBlackOrbs(UfoState ufo, float pull, OrbContact contact,
                                          LastPress last = {}) {
    UfoPlan plan{};
    const float line = contact.m_holdLine ? contact.m_line : ufo.center();
    plan.m_flapHeight = line;
    const float height = -pull * (ufo.center() - line);
    const float climb = -pull * ufo.m_velocity;
    const bool landed = last.m_up ? climb > 0.0f : climb < 0.0f;
    if (last.m_since < UFO_PRESS_SETTLE && !landed) {
        plan.m_reason = "settle";
        return plan;
    }
    if (height < 0.0f && climb <= 0.0f) {
        plan.m_press = true;
        plan.m_reason = "line";
    } else if (height > 0.0f && climb > 0.0f && contact.m_black > 0) {
        plan.m_press = true;
        plan.m_reason = "black";
    }
    return plan;
}

static_assert(holdStraightOnBlackOrbs({90, 120, -1}, -1, {1, 1, false, 0, true, 110})
                  .presses(),
              "below the line and falling it flaps");
static_assert(holdStraightOnBlackOrbs({100, 130, 1}, -1, {1, 1, false, 0, true, 110})
                  .presses(),
              "above the line and rising it drops on the orb");
static_assert(!holdStraightOnBlackOrbs({100, 130, 1}, -1, {0, 0, false, 5, true, 110})
                   .presses(),
              "with no orb to drop on it never flaps higher");
static_assert(!holdStraightOnBlackOrbs({100, 130, -1}, -1, {1, 1, false, 0, true, 110})
                   .presses(),
              "above the line and falling it waits");
static_assert(holdStraightOnBlackOrbs({110, 140, 1}, 1, {1, 1, false, 0, true, 120})
                  .presses(),
              "upside down mirrors: above the line is low, rising is falling");
static_assert(!holdStraightOnBlackOrbs({90, 120, -1}, -1, {1, 1, false, 0, true, 110},
                                       {1, true})
                   .presses(),
              "a flap that has not shown yet is not clicked again");
static_assert(holdStraightOnBlackOrbs({90, 120, -1}, -1, {1, 1, false, 0, true, 110},
                                      {2, true})
                  .presses(),
              "but a click that never showed is retried");
static_assert(holdStraightOnBlackOrbs({90, 120, -1}, -1, {1, 1, false, 0, true, 110},
                                      {1, false})
                  .presses(),
              "a drop that already showed lets it flap straight away");

constexpr UfoPlan steerUfoForGap(GapState gap, UfoState ufo,
                                 UfoPhysics physics, OrbContact contact,
                                 int framesSincePress = UFO_PRESS_SETTLE,
                                 bool lastPressUp = true) {
    if (inBlackOrbSection(contact)) {
        const float half = ufo.height() * .5f;
        contact.m_line = contact.m_holdLine
            ? clampBlackOrbLine(contact.m_line, gap, half, physics)
            : ufoFlapHeight(shiftGap(gap, 1.0f), half, physics, 0.0f, 0.0f);
        contact.m_holdLine = true;
    }
    if (inBlackOrbSection(contact) && !contact.m_blue)
        return holdStraightOnBlackOrbs(ufo, physics.pull(), contact,
                                       {framesSincePress, lastPressUp});

    const int lead = UFO_CROSS_LEAD;
    const float aim = ufoArcPeak(physics);
    const float half = ufo.height() * .5f;
    UfoPlan plan{};
    plan.m_flapHeight = ufoFlapHeight(gap, half, physics, aim);

    if (framesSincePress < UFO_PRESS_SETTLE) return plan;

    if (contact.m_blue) {
        const bool falling = ufoCrossesFlapHeight(gap, ufo, physics, lead, aim);
        const UfoPhysics after = physics.flipped();
        const UfoState flipped{ufo.m_bottom, ufo.m_top, physics.m_blueStep};
        const bool worseFlipped =
            ufoCrossesFlapHeight(gap, flipped, after, lead, ufoArcPeak(after)) ||
            ufoOvershootsFarWall(gap, flipped, after, lead);
        if (falling && !worseFlipped) {
            plan.m_press = true;
            plan.m_emergency = true;
            plan.m_reason = "blue";
            return plan;
        }
    }

    if (inBlackOrbSection(contact))
        return holdStraightOnBlackOrbs(ufo, physics.pull(), contact,
                                       {framesSincePress, lastPressUp});

    if (!ufoCrossesFlapHeight(gap, ufo, physics, lead, aim)) return plan;

    plan.m_press = true;
    plan.m_emergency = ufoCrossesFlapHeight(gap, ufo, physics, 1, aim);
    plan.m_reason = contact.m_black > 0 ? "black"
        : contact.m_pending > 0 ? "orb" : "flap";
    return plan;
}

struct UfoPhysicsTracker {
    static constexpr int SAMPLES = 16;
    static constexpr int MIN_SAMPLES = 4;
    static constexpr float QUIET = .0005f;
    static constexpr float BOOTSTRAP_RISE = 45.0f;
    static constexpr float DROP_VELOCITY = 14.0f * .8f;

    float m_gravityStep = 0.0f;
    float m_flapStep = 0.0f;
    float m_scale = 0.0f;
    float m_maxTravel = 0.0f;
    float m_samples[SAMPLES] = {};
    int m_sampleCount = 0;
    int m_sampleAt = 0;
    bool m_haveGravity = false;
    bool m_haveFlap = false;

    constexpr void reset() { *this = UfoPhysicsTracker{}; }

    constexpr bool ready() const { return m_haveGravity; }

    constexpr void observe(float travel, float velocity, float previousTravel,
                           bool pressed, bool pressedOnOrb = false) {
        const float absVelocity = velocity < 0.0f ? -velocity : velocity;
        const float absTravel = travel < 0.0f ? -travel : travel;
        if (absTravel > m_maxTravel) m_maxTravel = absTravel;
        if (absVelocity > .01f && absTravel > .0001f) {
            const float scale = absTravel / absVelocity;
            m_scale = m_scale > 0.0f ? m_scale + (scale - m_scale) * .35f
                                     : scale;
        }

        const float delta = travel - previousTravel;
        const float magnitude = delta < 0.0f ? -delta : delta;
        if (pressed) {
            if (!pressedOnOrb && m_haveGravity &&
                magnitude > absGravity() * 2.0f) {
                const float flap = travel - m_gravityStep;
                m_flapStep = m_haveFlap
                    ? m_flapStep + (flap - m_flapStep) * .35f : flap;
                m_haveFlap = true;
            }
            return;
        }

        if (magnitude < QUIET) return;
        m_samples[m_sampleAt] = delta;
        m_sampleAt = (m_sampleAt + 1) % SAMPLES;
        if (m_sampleCount < SAMPLES) ++m_sampleCount;
        if (m_sampleCount < MIN_SAMPLES) return;

        float sorted[SAMPLES] = {};
        for (int i = 0; i < m_sampleCount; ++i) sorted[i] = m_samples[i];
        for (int i = 1; i < m_sampleCount; ++i) {
            const float value = sorted[i];
            int at = i - 1;
            while (at >= 0 && sorted[at] > value) {
                sorted[at + 1] = sorted[at];
                --at;
            }
            sorted[at + 1] = value;
        }
        m_gravityStep = sorted[m_sampleCount / 2];
        m_haveGravity = true;
    }

    constexpr float absGravity() const {
        return m_gravityStep < 0.0f ? -m_gravityStep : m_gravityStep;
    }

    constexpr float terminal() const {
        const float flap = m_flapStep < 0.0f ? -m_flapStep : m_flapStep;
        return std::max(m_maxTravel, std::max(flap * 4.0f, 1.0f));
    }

    constexpr UfoPhysics physics(bool upsideDown, float cap) const {
        const float sign = upsideDown ? 1.0f : -1.0f;
        const float gravity = absGravity();
        float flap = m_flapStep < 0.0f ? -m_flapStep : m_flapStep;
        if (!m_haveFlap || !(flap > 0.0f)) {
            flap = std::sqrt(2.0f * BOOTSTRAP_RISE * gravity);
        }
        const float drop = m_scale > 0.0f ? DROP_VELOCITY * m_scale : flap;
        return UfoPhysics{
            sign * gravity,
            -sign * flap,
            sign * flap,
            sign * drop,
            cap,
        };
    }
};

}
