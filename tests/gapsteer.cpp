
#include "assist/gapsteer.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace grape::assist;

static int g_failures = 0;

static void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

namespace {

struct Corridor {
    virtual float center(int frame) const = 0;
    virtual ~Corridor() = default;
};

struct Still : Corridor {
    float m_at;
    explicit Still(float at) : m_at(at) {}
    float center(int) const override { return m_at; }
};

struct Sweep : Corridor {
    float m_speed;
    explicit Sweep(float speed) : m_speed(speed) {}
    float center(int frame) const override { return 60.0f + m_speed * frame; }
};

struct Zigzag : Corridor {
    float m_speed;
    int m_period;
    Zigzag(float speed, int period) : m_speed(speed), m_period(period) {}
    float center(int frame) const override {
        const int cycle = frame / m_period;
        const int within = frame % m_period;
        const float base = 60.0f;
        const float span = m_speed * m_period;
        const float leg = m_speed * within;
        return (cycle % 2 == 0) ? base + leg : base + span - leg;
    }
};

struct Result {
    int m_deaths = 0;
    float m_worstError = 0.0f;
    int m_flips = 0;
};

float jitter(int frame) {
    const int h = (frame * 1103515245 + 12345) & 0x7fffffff;
    return ((h % 2001) / 1000.0f - 1.0f) * .18f;
}

float legacyFutureGapOffset(float velocity, float acceleration, int frames) {
    const float linear = velocity * frames;
    const float curve = acceleration * frames * frames * .125f;
    const float limit = std::fabs(linear) * .5f;
    return linear + std::clamp(curve, -limit, limit);
}

bool legacySteerByClearance(float playerBottom, float playerTop,
                            float gapBottom, float gapTop, bool upsideDown,
                            bool pressed) {
    const float lower = playerBottom - gapBottom;
    const float upper = gapTop - playerTop;
    const float total = lower + upper;
    const float floorBand = std::min(.25f, std::max(0.0f, total) * .5f);
    const float deadband = std::clamp(total * .12f, floorBand, 1.5f);
    if (lower + deadband < upper) return !upsideDown;
    if (upper + deadband < lower) return upsideDown;
    return !pressed;
}

bool legacyAdaptive(GapState gap, WaveState wave, int lookahead, bool pressed) {
    const float freeSpace = std::max(0.0f, gap.height() - wave.height());
    const float marginFloor = std::min(.1f, freeSpace * .5f);
    const float margin = std::clamp(freeSpace * .15f, marginFloor, 2.0f);

    int safetyFrames = std::clamp(lookahead, 1, 30);
    if (freeSpace < 6.0f) {
        const float tightness = 1.0f + (6.0f - freeSpace) / 6.0f;
        safetyFrames = std::clamp(
            static_cast<int>(safetyFrames * tightness + 0.5f), 1, 30);
    }
    const float gapSpeed = std::fabs(gap.m_velocity);
    if (gapSpeed > 4.0f) {
        const float speedScale = std::min(2.25f, 1.0f + (gapSpeed - 4.0f) / 10.0f);
        safetyFrames = std::clamp(
            static_cast<int>(safetyFrames * speedScale + 0.5f), 1, 30);
    }
    const float safetyGapOffset =
        legacyFutureGapOffset(gap.m_velocity, gap.m_acceleration, safetyFrames);
    const float safetyPlayerOffset = wave.m_step * std::min(safetyFrames, 2);
    const float futureLower =
        wave.m_bottom + safetyPlayerOffset - (gap.m_lower + safetyGapOffset);
    const float futureUpper =
        gap.m_upper + safetyGapOffset - (wave.m_top + safetyPlayerOffset);
    if (futureLower <= margin && futureLower < futureUpper)
        return !wave.m_upsideDown;
    if (futureUpper <= margin && futureUpper < futureLower)
        return wave.m_upsideDown;

    const float nextGapOffset =
        legacyFutureGapOffset(gap.m_velocity, gap.m_acceleration, 1);
    return legacySteerByClearance(
        wave.m_bottom + wave.m_step, wave.m_top + wave.m_step,
        gap.m_lower + nextGapOffset, gap.m_upper + nextGapOffset,
        wave.m_upsideDown, pressed);
}

enum class Variant {
    Legacy,
    TrackerOnly,
    Full,
};

Result run(const Corridor& corridor, float gapHeight, float waveHeight,
           float climbRate, int frames, int lookahead, Variant variant) {
    float waveCenter = corridor.center(0);
    float direction = 1.0f;
    bool pressed = true;
    Result result;

    GapTracker tracker;
    float legacyCenter = 0.0f, legacyVelocity = 0.0f, legacyAccel = 0.0f;
    bool legacyValid = false;

    for (int frame = 1; frame < frames; ++frame) {
        const float trueCenter = corridor.center(frame);
        const float seen = trueCenter + jitter(frame);
        const float lower = seen - gapHeight * .5f;
        const float upper = seen + gapHeight * .5f;

        GapState gap{};
        if (variant == Variant::Legacy) {
            if (!legacyValid) {
                legacyCenter = seen;
                legacyValid = true;
            } else {
                const float raw = seen - legacyCenter;
                const float velocity =
                    legacyVelocity + (raw - legacyVelocity) * .6f;
                legacyAccel = std::clamp(
                    legacyAccel + ((velocity - legacyVelocity) - legacyAccel) * .6f,
                    -15.0f, 15.0f);
                legacyVelocity = std::clamp(velocity, -60.0f, 60.0f);
                legacyCenter = seen;
            }
            gap = GapState{lower, upper, legacyVelocity, legacyAccel};
        } else {
            tracker.observe(lower, upper, 1);
            gap = tracker.state();
        }

        const WaveState wave{waveCenter - waveHeight * .5f,
                             waveCenter + waveHeight * .5f,
                             direction * climbRate, climbRate, false};

        bool want;
        if (variant == Variant::Full) {
            want = pressed;
            const auto steer = steerForGap(gap, wave, lookahead);
            if (steer.m_action == Steer::Up) want = true;
            else if (steer.m_action == Steer::Down) want = false;
            else want = !pressed;
        } else {
            want = legacyAdaptive(gap, wave, lookahead, pressed);
        }

        if (want != pressed) ++result.m_flips;
        pressed = want;
        direction = pressed ? 1.0f : -1.0f;
        waveCenter += direction * climbRate;

        const float error = std::fabs(waveCenter - trueCenter);
        result.m_worstError = std::max(result.m_worstError, error);
        if (error > (gapHeight - waveHeight) * .5f) ++result.m_deaths;
    }
    return result;
}

}

int main() {
    check(pursuitLead(10.0f, 2.0f, 0.0f, 30.0f) == 5.0f,
          "still corridor: lead is time to cover the error");
    check(pursuitLead(10.0f, 2.0f, 1.0f, 30.0f) > 5.0f,
          "corridor running away needs a longer lead");
    check(pursuitLead(10.0f, 2.0f, -1.0f, 30.0f) < 5.0f,
          "corridor coming towards us needs a shorter lead");
    check(pursuitLead(10.0f, 2.0f, 5.0f, 8.0f) == 8.0f, "lead is capped");
    check(pursuitLead(0.0f, 2.0f, 0.0f, 30.0f) == 0.0f, "no error, no lead");
    check(pursuitLead(10.0f, 0.0f, 0.0f, 30.0f) == 0.0f,
          "a wave that cannot climb yields no lead instead of dividing by 0");
    check(climbRateOf(0.0) == .35f, "climb rate is floored");
    check(climbRateOf(100.0) == 3.0f, "climb rate scales like the game's step");
    check(climbRateOf(-100.0) == 3.0f, "climb rate is a magnitude");

    {
        const auto up = steerForGap(GapState{100.0f, 110.0f, 0.0f, 0.0f},
                                    WaveState{0.0f, 5.0f, 0.0f, 1.0f, false}, 8);
        check(up.m_action == Steer::Up, "steers up towards a corridor above");
        const auto down = steerForGap(GapState{0.0f, 10.0f, 0.0f, 0.0f},
                                      WaveState{100.0f, 105.0f, 0.0f, 1.0f, false},
                                      8);
        check(down.m_action == Steer::Down, "steers down towards one below");
    }
    {
        const auto keep = steerForGap(GapState{0.0f, 40.0f, 0.0f, 0.0f},
                                      WaveState{15.0f, 25.0f, 0.0f, 2.0f, false},
                                      8);
        check(keep.m_action == Steer::Dither,
              "a perfectly centred, motionless wave dithers");
    }
    {
        float waveCenter = 20.0f;
        bool pressed = true;
        int flips = 0;
        for (int frame = 0; frame < 60; ++frame) {
            const float step = pressed ? 1.4f : -1.4f;
            const auto steer = steerForGap(
                GapState{8.0f, 32.0f, 0.0f, 0.0f},
                WaveState{waveCenter - 5.0f, waveCenter + 5.0f, step, 1.4f,
                          false},
                6);
            bool want = pressed;
            if (steer.m_action == Steer::Up) want = true;
            else if (steer.m_action == Steer::Down) want = false;
            else want = !pressed;
            if (want != pressed) ++flips;
            pressed = want;
            waveCenter += pressed ? 1.4f : -1.4f;
        }
        check(flips > 20, "holding station keeps the input changing");
        check(std::fabs(waveCenter - 20.0f) < 4.0f,
              "and the wave stays near the corridor centre while doing it");
    }
    {
        const auto up = steerForGap(GapState{100.0f, 110.0f, 0.0f, 0.0f},
                                    WaveState{0.0f, 5.0f, 0.0f, 1.0f, true}, 8);
        check(up.m_action == Steer::Up, "world-space action ignores gravity");
    }
    {
        const auto panic = steerForGap(GapState{0.0f, 12.0f, 0.0f, 0.0f},
                                       WaveState{0.2f, 10.2f, -2.0f, 2.0f, false},
                                       8);
        check(panic.m_action == Steer::Up, "clipping the floor steers up");
        check(panic.m_emergency, "clipping is reported as an emergency");
    }

    struct Case {
        const char* m_name;
        const Corridor* m_corridor;
        float m_gapHeight;
    };
    const Still still(60.0f);
    const Sweep slow(0.4f);
    const Sweep fast(1.1f);
    const Zigzag wide(0.9f, 14);
    const Zigzag sharp(1.1f, 7);
    const Zigzag sharpTight(1.2f, 6);

    const std::vector<Case> cases = {
        {"still corridor", &still, 16.0f},
        {"slow sweep", &slow, 16.0f},
        {"fast sweep", &fast, 16.0f},
        {"wide zigzag", &wide, 15.0f},
        {"sharp zigzag", &sharp, 15.0f},
        {"sharp tight zigzag", &sharpTight, 14.0f},
    };

    std::printf("%-22s %8s %8s %8s\n", "corridor", "legacy", "tracker", "full");
    int improved = 0;
    for (const auto& item : cases) {
        const Result old = run(*item.m_corridor, item.m_gapHeight, 10.0f, 1.4f,
                               900, 6, Variant::Legacy);
        const Result mid = run(*item.m_corridor, item.m_gapHeight, 10.0f, 1.4f,
                               900, 6, Variant::TrackerOnly);
        const Result now = run(*item.m_corridor, item.m_gapHeight, 10.0f, 1.4f,
                               900, 6, Variant::Full);
        std::printf("%-22s %8d %8d %8d   err %.2f->%.2f  flips %d->%d\n",
                    item.m_name, old.m_deaths, mid.m_deaths, now.m_deaths,
                    old.m_worstError, now.m_worstError, old.m_flips,
                    now.m_flips);
        check(now.m_deaths <= old.m_deaths,
              std::string(item.m_name) + ": not worse than the old controller");
        if (now.m_deaths < old.m_deaths) ++improved;
    }
    check(improved >= 2,
          "the new controller wins on the corridors that actually move");

    const Result stillNew =
        run(still, 16.0f, 10.0f, 1.4f, 900, 6, Variant::Full);
    check(stillNew.m_deaths == 0, "still corridor is never clipped");
    check(stillNew.m_flips > 400,
          "and the controller is still clicking while it holds that corridor");

    for (const auto& item : cases) {
        const Result old = run(*item.m_corridor, item.m_gapHeight, 10.0f, 1.4f,
                               900, 6, Variant::Legacy);
        const Result now = run(*item.m_corridor, item.m_gapHeight, 10.0f, 1.4f,
                               900, 6, Variant::Full);
        check(now.m_flips <= old.m_flips + old.m_flips / 4 + 8,
              std::string(item.m_name) + ": input rate stays in line");
    }

    if (g_failures) {
        std::printf("\ngapsteer: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("\ngapsteer: all checks passed\n");
    return 0;
}
