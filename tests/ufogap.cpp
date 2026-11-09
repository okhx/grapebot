
#include "assist/ufogap.hpp"

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
    float center(int frame) const override { return 200.0f + m_speed * frame; }
};

struct Zigzag : Corridor {
    float m_speed;
    int m_period;
    Zigzag(float speed, int period) : m_speed(speed), m_period(period) {}
    float center(int frame) const override {
        const int cycle = frame / m_period;
        const int within = frame % m_period;
        const float span = m_speed * m_period;
        const float leg = m_speed * within;
        return (cycle % 2 == 0) ? 200.0f + leg : 200.0f + span - leg;
    }
};

struct Sine : Corridor {
    float m_amplitude;
    float m_period;
    Sine(float amplitude, float period)
        : m_amplitude(amplitude), m_period(period) {}
    float center(int frame) const override {
        return 200.0f +
               m_amplitude * std::sin(6.2831853f * frame / m_period);
    }
};

float jitter(int frame) {
    const int mixed = (frame * 1103515245 + 12345) & 0x7fffffff;
    return static_cast<float>(mixed % 200 - 100) / 400.0f;
}

enum class Variant {
    Search,
    Centring,
};

struct Result {
    int m_deaths = 0;
    int m_presses = 0;
    float m_bias = 0.0f;
    float m_offset = 0.0f;
};

Result run(const Corridor& corridor, float gapHeight, float playerHeight,
           int frames, Variant variant) {
    const UfoPhysics physics{-0.36f, 3.4f, -3.4f, -5.5f, 12.0f};

    float bottom = corridor.center(0) - playerHeight * .5f;
    float top = bottom + playerHeight;
    float velocity = 0.0f;

    GapTracker tracker;
    Result result;
    double bias = 0.0;
    double offset = 0.0;
    int measured = 0;

    for (int frame = 0; frame < frames; ++frame) {
        const float center = corridor.center(frame);
        const float noise = jitter(frame);
        tracker.observe(center - gapHeight * .5f + noise,
                        center + gapHeight * .5f + noise, 1);

        bool press = false;
        if (variant == Variant::Search) {
            const UfoPlan plan = steerUfoForGap(
                tracker.state(), UfoState{bottom, top, velocity}, physics,
                OrbContact{});
            press = plan.presses();
        } else {
            press = (bottom + top) * .5f < tracker.m_center;
        }

        if (press) {
            velocity = physics.m_flapStep;
            ++result.m_presses;
        }
        velocity = std::clamp(velocity + physics.m_gravityStep,
                              -physics.m_terminal, physics.m_terminal);
        bottom += velocity;
        top += velocity;

        const float error = (bottom + top) * .5f - corridor.center(frame + 1);
        bias += error;
        offset += error < 0.0f ? -error : error;
        ++measured;

        const float trueLower = corridor.center(frame + 1) - gapHeight * .5f;
        const float trueUpper = corridor.center(frame + 1) + gapHeight * .5f;
        if (bottom < trueLower || top > trueUpper) {
            ++result.m_deaths;
            bottom = corridor.center(frame + 1) - playerHeight * .5f;
            top = bottom + playerHeight;
            velocity = 0.0f;
            tracker.reset();
        }
    }
    if (measured) {
        result.m_bias = static_cast<float>(bias / measured);
        result.m_offset = static_cast<float>(offset / measured);
    }
    return result;
}

struct OrbFlight {
    int m_deaths = 0;
    int m_presses = 0;
    int m_orbDrops = 0;
    float m_learnedFlap = 0.0f;
    float m_meanError = 0.0f;
    float m_deepest = 0.0f;
    float m_lineDrift = 0.0f;
};

struct SimPhysics {
    float m_gravity = -0.04f;
    float m_flap = 1.9f;
    float m_drop = -3.04f;
    float m_terminal = 4.5f;
    float m_scale = 0.2714f;
};

constexpr SimPhysics COARSE_PHYSICS{-0.36f, 3.4f, -5.5f, 12.0f, 5.5f / 11.2f};

OrbFlight flyBlackOrbGap(float gapHeight, float amplitude, int inputDelay,
                         int orbEvery = 46, int orbTouch = 40, bool follow = true,
                         SimPhysics sim = {}) {
    const float GRAVITY = sim.m_gravity;
    const float FLAP = sim.m_flap;
    const float DROP = sim.m_drop;
    const float TERMINAL = sim.m_terminal;
    const float SCALE = sim.m_scale;
    constexpr float PLAYER = 30.0f;
    const auto centerAt = [&](int frame) {
        return 300.0f + amplitude * std::sin(frame / 150.0f);
    };

    float bottom = centerAt(0) - PLAYER * .5f;
    float top = bottom + PLAYER;
    float travel = 0.0f;
    float lastTravel = 0.0f;
    bool lastPressOnOrb = false;
    bool pending[4] = {};
    int sincePress = 1000;
    int sinceBlack = 1000;
    int usedOrb = -1;
    bool holding = false;
    bool lastPressUp = true;
    float heldLine = 0.0f;
    float anchor = 0.0f;
    double error = 0.0;
    GapTracker tracker;
    UfoPhysicsTracker ufo;
    OrbFlight flight;

    for (int frame = 0; frame < 6000; ++frame) {
        const float center = centerAt(frame);
        tracker.observe(center - gapHeight * .5f, center + gapHeight * .5f, 1);
        if (frame > 0) {
            const bool credited = sincePress <= UFO_PRESS_CREDIT;
            ufo.observe(travel, travel / SCALE, lastTravel, credited,
                        credited && lastPressOnOrb);
        }
        lastTravel = travel;

        const int orb = orbEvery > 0 ? frame / orbEvery : -1;
        const bool touching =
            orbEvery > 0 && frame % orbEvery < orbTouch && usedOrb != orb;
        sinceBlack = touching ? 0 : sinceBlack + 1;
        bool press = false;
        if (ufo.ready()) {
            OrbContact contact =
                touching ? OrbContact{1, 1, false} : OrbContact{};
            contact.m_sinceBlack = sinceBlack;
            const UfoState state{bottom, top, travel};
            const UfoPhysics physics = ufo.physics(false, ufo.terminal());
            if (!inBlackOrbSection(contact)) {
                holding = false;
            } else if (!holding) {
                heldLine = state.center();
                anchor = shiftGap(tracker.state(), 1.0f).center();
                holding = true;
            }
            contact.m_holdLine = holding;
            contact.m_line =
                follow ? followGapLine(heldLine, anchor, tracker.state()) : heldLine;
            const UfoPlan plan =
                steerUfoForGap(tracker.state(), state, physics, contact, sincePress,
                               lastPressUp);
            press = plan.presses();
            if (press) lastPressUp = std::string(plan.m_reason) != "black";
        } else {
            press = (bottom + top) * .5f < center - 10.0f && travel < 0.0f;
        }
        sincePress = press ? 1 : sincePress + 1;
        if (press) {
            ++flight.m_presses;
            lastPressOnOrb = touching;
        }
        for (int i = 0; i < inputDelay; ++i) std::swap(press, pending[i]);

        float velocity = travel;
        if (press) {
            const bool orbTakesIt = touching && !dropRingNeedsExtraPress(
                                                    travel / SCALE, false);
            if (touching) usedOrb = orb;
            if (orbTakesIt) ++flight.m_orbDrops;
            velocity = orbTakesIt ? DROP : FLAP;
        }
        velocity = std::clamp(velocity + GRAVITY, -TERMINAL, TERMINAL);
        travel = velocity;
        bottom += velocity;
        top += velocity;

        const float next = centerAt(frame + 1);
        error += std::fabs((bottom + top) * .5f - next);
        if (frame > 600)
            flight.m_deepest =
                std::max(flight.m_deepest, next - (bottom + top) * .5f);
        if (holding && frame > 600)
            flight.m_lineDrift = std::max(
                flight.m_lineDrift,
                std::fabs((bottom + top) * .5f -
                          (follow ? heldLine + (next - (anchor)) : heldLine)));
        if (bottom < next - gapHeight * .5f || top > next + gapHeight * .5f) {
            ++flight.m_deaths;
            bottom = next - PLAYER * .5f;
            top = bottom + PLAYER;
            travel = 0.0f;
            lastTravel = 0.0f;
            tracker.reset();
            ufo.reset();
            sincePress = 1000;
            holding = false;
        }
    }
    flight.m_learnedFlap = ufo.m_flapStep;
    flight.m_meanError = static_cast<float>(error / 6000.0);
    return flight;
}

}

int main() {
    const Still still(200.0f);
    const Sweep slow(0.35f);
    const Sweep fast(0.9f);
    const Zigzag zigzag(0.8f, 40);
    const Sine sine(70.0f, 220.0f);

    struct Case {
        const char* m_name;
        const Corridor* m_corridor;
        float m_gapHeight;
    };
    const Case cases[] = {
        {"still", &still, 90.0f},
        {"slow sweep", &slow, 90.0f},
        {"fast sweep", &fast, 110.0f},
        {"zigzag", &zigzag, 110.0f},
        {"sine", &sine, 100.0f},
    };

    for (const auto& item : cases) {
        const Result searched =
            run(*item.m_corridor, item.m_gapHeight, 30.0f, 1200,
                Variant::Search);
        check(searched.m_deaths == 0,
              std::string(item.m_name) + ": corridor is never clipped");
        check(searched.m_presses > 0,
              std::string(item.m_name) + ": the controller is actually flying");
        check(searched.m_presses < 1200 / 2,
              std::string(item.m_name) + ": input rate stays sane");
        check(std::fabs(searched.m_bias) < 5.5f,
              std::string(item.m_name) + ": it flies the middle of the gap");
        check(searched.m_offset < 9.0f,
              std::string(item.m_name) + ": and stays near it");
    }

    {
        int searchDeaths = 0;
        int centringDeaths = 0;
        for (const auto& item : cases) {
            searchDeaths += run(*item.m_corridor, item.m_gapHeight, 30.0f, 1200,
                                Variant::Search).m_deaths;
            centringDeaths += run(*item.m_corridor, item.m_gapHeight, 30.0f,
                                  1200, Variant::Centring).m_deaths;
        }
        check(searchDeaths < centringDeaths,
              "threshold flapping beats centring on the estimate");
    }

    {
        const Sine narrow(40.0f, 300.0f);
        const Result tight = run(narrow, 65.0f, 30.0f, 1200, Variant::Search);
        check(tight.m_deaths == 0, "tight sweeping corridor is held");
    }

    {
        const Result searched = run(sine, 55.0f, 30.0f, 1200, Variant::Search);
        const Result centring =
            run(sine, 55.0f, 30.0f, 1200, Variant::Centring);
        check(searched.m_deaths * 3 < centring.m_deaths * 2,
              "an infeasible corridor still costs much less than centring");
    }

    {
        const GapState gap{170.0f, 260.0f, 0.0f, 0.0f};
        const UfoPhysics physics{-0.36f, 3.4f, -3.4f, -5.5f, 12.0f};
        const UfoPlan plan = steerUfoForGap(
            gap, UfoState{172.0f, 202.0f, -4.0f}, physics, OrbContact{});
        check(plan.presses(), "a flyer dropping out of the corridor presses");
        check(plan.m_emergency, "and says the press was not optional");
        check(plan.m_press, "as a single held press");
    }

    {
        const GapState gap{170.0f, 260.0f, 0.0f, 0.0f};
        const UfoPhysics physics{-0.36f, 3.4f, -3.4f, -5.5f, 12.0f};
        const UfoPlan plan = steerUfoForGap(
            gap, UfoState{228.0f, 258.0f, 2.0f}, physics, OrbContact{});
        check(!plan.presses(), "a flyer against the ceiling does not press");
    }

    {
        const UfoPhysics physics{-0.36f, 3.4f, -3.4f, -5.5f, 12.0f};
        const float rise = flapRise(physics);
        check(rise > 15.0f && rise < 17.0f, "one flap rises about 16 units");
        const float aim = ufoArcPeak(physics);
        const GapState roomy{130.0f, 270.0f, 0.0f, 0.0f};
        const float height = ufoFlapHeight(roomy, 15.0f, physics, aim);
        check(std::fabs(height - (200.0f - rise * UFO_ARC_MEAN)) < .01f,
              "a roomy corridor flaps two thirds of an arc below its centre");
        check(height < 200.0f - rise * .5f,
              "which is lower than aiming the peak of the arc would put it");
        const UfoPhysics inverted = physics.flipped();
        const float mirrored = ufoFlapHeight(roomy, 15.0f, inverted, aim);
        check(std::fabs(mirrored - (200.0f + rise * UFO_ARC_MEAN)) < .01f,
              "and upside down it mirrors");
        const float tight =
            ufoFlapHeight(GapState{190.0f, 240.0f, 0.0f, 0.0f}, 15.0f, physics,
                          aim);
        check(tight >= 190.0f + 15.0f,
              "a tight corridor keeps the flyer off the near wall");
        const GapState dropping{130.0f, 270.0f, -1.8f, 0.0f};
        check(ufoFlapHeight(dropping, 15.0f, physics, aim) <
                  height - rise * .5f,
              "a corridor sweeping down flaps lower than a still one");
    }

    {
        const GapState gap{170.0f, 260.0f, 0.0f, 0.0f};
        const UfoPhysics physics{-0.36f, 3.4f, -3.4f, -5.5f, 12.0f};
        const UfoState low{172.0f, 202.0f, -4.0f};
        check(steerUfoForGap(gap, low, physics, OrbContact{1, 1, false}).m_press,
              "a falling flyer presses with one black orb under it");
        check(steerUfoForGap(gap, low, physics, OrbContact{2, 2, false}).m_press,
              "and with two");
        const UfoState settled{205.0f, 235.0f, 0.0f};
        check(!steerUfoForGap(gap, settled, physics, OrbContact{1, 1, false})
                   .presses(),
              "an orb in contact is not a reason to press");
    }

    {
        const GapState gap{170.0f, 260.0f, 0.0f, 0.0f};
        const UfoPhysics physics{-0.36f, 3.4f, -3.4f, -5.5f, 12.0f};
        const UfoPlan blue = steerUfoForGap(
            gap, UfoState{190.0f, 220.0f, -4.0f}, physics,
            OrbContact{1, 0, true});
        check(blue.presses(), "a blue orb is taken to escape the floor");
        check(std::string(blue.m_reason) == "blue",
              "and is reported as the blue orb it was");
        check(blue.m_press, "as one press");

        const UfoPlan tooLow = steerUfoForGap(
            gap, UfoState{172.0f, 202.0f, -4.0f}, physics,
            OrbContact{1, 0, true});
        check(std::string(tooLow.m_reason) != "blue",
              "a blue orb whose dip hits the floor is declined");
    }

    {
        const UfoPhysics truth{-0.36f, 3.4f, -3.4f, -5.5f, 12.0f};
        const float scale = 0.03f;

        const auto fly = [&](bool honest, int frames) {
            UfoPhysicsTracker tracker;
            float velocity = 0.0f;
            float travel = 0.0f;
            for (int frame = 0; frame < frames; ++frame) {
                const bool press = frame % 20 == 0;
                if (press) velocity = truth.m_flapStep;
                velocity = std::clamp(velocity + truth.m_gravityStep,
                                      -truth.m_terminal, truth.m_terminal);
                const float previous = travel;
                travel = velocity;
                tracker.observe(travel, travel / scale, previous,
                                honest && press);
            }
            return tracker;
        };

        const UfoPhysicsTracker tracker = fly(true, 120);
        check(tracker.ready(), "the tracker learns gravity from coasting");
        check(std::fabs(tracker.m_gravityStep - truth.m_gravityStep) < .02f,
              "and gets it right");
        check(tracker.m_haveFlap, "and learns the flap from a press");
        check(std::fabs(tracker.m_flapStep - truth.m_flapStep) < .2f,
              "and gets that right too");

        const UfoPhysicsTracker blind = fly(false, 120);
        check(blind.ready(), "gravity is learned even from unannounced presses");
        check(std::fabs(blind.m_gravityStep - truth.m_gravityStep) < .02f,
              "and the impulse frames do not inflate it");

        const UfoPhysics learned = tracker.physics(false, truth.m_terminal);
        check(learned.m_gravityStep < 0.0f && learned.m_flapStep > 0.0f,
              "upright physics fall down and flap up");
        const UfoPhysics inverted = tracker.physics(true, truth.m_terminal);
        check(inverted.m_gravityStep > 0.0f && inverted.m_flapStep < 0.0f,
              "and upside down they are the mirror of that");
        check(std::fabs(learned.m_blackStep +
                        UfoPhysicsTracker::DROP_VELOCITY * scale) < .05f,
              "the drop ring is converted through the measured scale");

        UfoPhysicsTracker cold;
        for (int i = 1; i <= UfoPhysicsTracker::MIN_SAMPLES; ++i)
            cold.observe(-0.36f * i, -12.0f * i, -0.36f * (i - 1), false);
        const float coldRise = flapRise(cold.physics(false, 12.0f));
        check(coldRise > 30.0f && coldRise < 60.0f,
              "an unflapped tracker still guesses a block-and-a-half arc");
    }

    {
        UfoPhysicsTracker tracker;
        float travel = 0.0f;
        const auto coast = [&](float gravity, int frames) {
            for (int i = 0; i < frames; ++i) {
                const float previous = travel;
                travel += gravity;
                tracker.observe(travel, travel / .03f, previous, false);
            }
        };
        coast(-0.36f, 40);
        check(std::fabs(tracker.m_gravityStep + .36f) < .01f,
              "gravity settles on what it was given");
        travel = 0.0f;
        coast(-0.20f, UfoPhysicsTracker::SAMPLES);
        check(std::fabs(tracker.m_gravityStep + .20f) < .01f,
              "weaker gravity is followed once the window turns over");
        travel = 0.0f;
        coast(-0.40f, UfoPhysicsTracker::SAMPLES);
        check(std::fabs(tracker.m_gravityStep + .40f) < .01f,
              "and so is stronger");
    }

    {
        UfoPhysicsTracker tracker;
        float travel = 0.0f;
        const auto coast = [&](float gravity, int frames) {
            for (int i = 0; i < frames; ++i) {
                const float previous = travel;
                travel += gravity;
                tracker.observe(travel, travel / .03f, previous, false);
            }
        };

        coast(-0.36f, 20);
        tracker.observe(travel + .001f, (travel + .001f) / .03f, travel, false);
        travel += .001f;
        coast(-0.36f, 4);
        check(std::fabs(tracker.m_gravityStep + .36f) < .02f,
              "a single tiny sample does not become gravity");

        const UfoPhysics learned = tracker.physics(false, tracker.terminal());
        const float rise = flapRise(learned);
        check(rise > 10.0f && rise < 80.0f,
              "so the flap arc stays a believable size");
        const GapState gap{170.0f, 260.0f, 0.0f, 0.0f};
        const float height =
            ufoFlapHeight(gap, 15.0f, learned, ufoArcPeak(learned));
        check(height > 185.0f + 1.0f,
              "and the flap threshold is not pinned to the corridor floor");
    }

    {
        check(dropRingNeedsExtraPress(-4.0f, false),
              "falling into a drop ring earns the extra press");
        check(!dropRingNeedsExtraPress(4.0f, false),
              "but rising into one is left alone, so the orb still works");
        check(dropRingNeedsExtraPress(4.0f, true) &&
                  !dropRingNeedsExtraPress(-4.0f, true),
              "and upside down both mirror");
        check(dropRingNeedsExtraPress(0.0f, false),
              "stationary counts as falling, because it is about to be");

        check(planClickBurst(dropRingNeedsExtraPress(4.0f, false) ? 2 : 0, true)
                      .m_taps == 0,
              "a rising press stays a single press even on two rings");
        check(planClickBurst(dropRingNeedsExtraPress(-4.0f, false) ? 2 : 0, true)
                      .m_taps == 2,
              "and a falling one still gets one per ring");
    }

    {
        check(planClickBurst(0, true).m_taps == 0,
              "no orbs, no extra presses");
        check(planClickBurst(1, true).m_taps == 1 &&
                  planClickBurst(1, true).presses() == 2,
              "one black orb makes it two clicks");
        check(planClickBurst(2, true).m_taps == 2 &&
                  planClickBurst(2, true).presses() == 3,
              "two black orbs make it three, the last unchanged");
        check(planClickBurst(1, true).m_hold,
              "and the last press is always the held one, so a hold survives");
    }

    {
        const GapState gap{170.0f, 260.0f, 0.0f, 0.0f};
        const UfoPhysics physics{-0.36f, 3.4f, -3.4f, -5.5f, 12.0f};
        const UfoState climbing{172.0f, 202.0f, 1.0f};
        check(steerUfoForGap(gap, climbing, physics, OrbContact{}).presses(),
              "a flyer climbing too slowly re-flaps in open air");
        check(!steerUfoForGap(gap, climbing, physics, OrbContact{1, 1, false})
                   .presses(),
              "but not on a black orb, which would take the click and drop it");
        const UfoState low{172.0f, 202.0f, -4.0f};
        check(!steerUfoForGap(gap, low, physics, OrbContact{}, 1).presses(),
              "the frame after a press it waits for that press to land");
        check(steerUfoForGap(gap, low, physics, OrbContact{}, 2).presses(),
              "and presses again once it could have");
    }

    {
        struct OrbCase {
            float m_gap;
            float m_amplitude;
            int m_allowed;
        };
        const OrbCase orbCases[] = {
            {110.0f, 0.0f, 8}, {90.0f, 0.0f, 8}, {110.0f, 50.0f, 15},
        };
        for (const auto& item : orbCases) {
            const OrbFlight prompt = flyBlackOrbGap(item.m_gap, item.m_amplitude, 0);
            const OrbFlight late = flyBlackOrbGap(item.m_gap, item.m_amplitude, 1);
            std::printf("black orbs gap %3.0f sway %2.0f: deaths %d/%d, "
                        "presses %d/%d, orb drops %d/%d, flap %.2f/%.2f\n",
                        item.m_gap, item.m_amplitude, prompt.m_deaths,
                        late.m_deaths, prompt.m_presses, late.m_presses,
                        prompt.m_orbDrops, late.m_orbDrops,
                        prompt.m_learnedFlap, late.m_learnedFlap);
            const std::string name =
                "black orbs, gap " + std::to_string(static_cast<int>(item.m_gap)) +
                " sway " + std::to_string(static_cast<int>(item.m_amplitude));
            check(late.m_presses * 10 <= prompt.m_presses * 11,
                  name + ": a late-landing press is not clicked twice");
            check(prompt.m_deaths <= item.m_allowed &&
                      late.m_deaths <= item.m_allowed,
                  name + ": the gap is held");
            check(std::fabs(prompt.m_learnedFlap - 1.9f) < .1f &&
                      std::fabs(late.m_learnedFlap - 1.9f) < .1f,
                  name + ": the real flap is learned even when presses land late");
        }
    }

    {
        const GapState gap{170.0f, 260.0f, 0.0f, 0.0f};
        const UfoPhysics physics{-0.36f, 3.4f, -3.4f, -5.5f, 12.0f};
        OrbContact held{1, 1, false, 0};
        held.m_holdLine = true;
        held.m_line = 215.0f;
        const auto plan = [&](UfoState ufo, OrbContact contact) {
            return steerUfoForGap(gap, ufo, physics, contact);
        };
        check(plan(UfoState{190.0f, 220.0f, -2.0f}, held).presses(),
              "black orbs: below the held line and falling flaps");
        check(std::string(plan(UfoState{205.0f, 235.0f, 3.0f}, held).m_reason) ==
                  "black",
              "black orbs: above the held line and rising drops on the orb");
        OrbContact spent = held;
        spent.m_pending = 0;
        spent.m_black = 0;
        spent.m_sinceBlack = 5;
        check(!plan(UfoState{205.0f, 235.0f, 3.0f}, spent).presses(),
              "black orbs: with no unused orb to drop on it never flaps higher");
        check(!plan(UfoState{205.0f, 235.0f, -1.0f}, held).presses(),
              "black orbs: above the line and falling waits");
        check(!plan(UfoState{185.0f, 215.0f, 2.0f}, held).presses(),
              "black orbs: below the line and rising waits");
        check(plan(UfoState{190.0f, 220.0f, -2.0f}, held).presses() &&
                  plan(UfoState{189.0f, 219.0f, -2.2f}, held).presses(),
              "black orbs: it keeps clicking every frame while it needs to");
        check(!plan(UfoState{200.0f, 230.0f, 3.0f}, OrbContact{0, 0, false, 5000})
                   .presses(),
              "black orbs: long after the last one it is the plain gap assist");
    }

    {
        const OrbFlight plain = flyBlackOrbGap(110.0f, 0.0f, 0, 0, 0);
        const OrbFlight orbs = flyBlackOrbGap(110.0f, 0.0f, 0, 30, 26);
        const OrbFlight lateOrbs = flyBlackOrbGap(110.0f, 0.0f, 1, 30, 26);
        std::printf("black-orb run: deepest below centre %.1f (no orbs) vs %.1f "
                    "(orbs) vs %.1f (orbs, late presses); deaths %d/%d/%d\n",
                    plain.m_deepest, orbs.m_deepest, lateOrbs.m_deepest,
                    plain.m_deaths, orbs.m_deaths, lateOrbs.m_deaths);
        check(orbs.m_deepest < plain.m_deepest * .6f,
              "black orbs let the UFO hold higher instead of dipping");
        check(lateOrbs.m_deepest < plain.m_deepest * .6f,
              "and still do when presses land late");
        check(orbs.m_deaths == 0 && lateOrbs.m_deaths == 0,
              "without losing the gap");
    }

    {
        const OrbFlight steady = flyBlackOrbGap(110.0f, 0.0f, 0, 30, 26, true);
        const OrbFlight late = flyBlackOrbGap(110.0f, 0.0f, 1, 30, 26, true);
        const OrbFlight sway = flyBlackOrbGap(110.0f, 20.0f, 0, 30, 26, true);
        std::printf("straight black orbs: drift from the held line %.1f/%.1f, "
                    "deaths %d/%d; slow sway deaths %d\n",
                    steady.m_lineDrift, late.m_lineDrift, steady.m_deaths,
                    late.m_deaths, sway.m_deaths);
        check(steady.m_deaths == 0 && late.m_deaths == 0,
              "straight black orbs: the gap is held");
        for (int delay = 0; delay <= 1; ++delay) {
            const OrbFlight coarse =
                flyBlackOrbGap(110.0f, 0.0f, delay, 30, 26, true, COARSE_PHYSICS);
            const OrbFlight coarseSway =
                flyBlackOrbGap(110.0f, 20.0f, delay, 30, 26, true, COARSE_PHYSICS);
            std::printf("coarse physics, presses %s: deaths %d steady, %d swaying, "
                        "drift %.1f\n", delay ? "late" : "prompt", coarse.m_deaths,
                        coarseSway.m_deaths, coarse.m_lineDrift);
            check(coarse.m_deaths == 0 && coarseSway.m_deaths <= 1,
                  std::string("coarse physics: black orbs are held with ") +
                      (delay ? "late" : "prompt") + " presses");
        }
        check(steady.m_lineDrift < 40.0f && late.m_lineDrift < 40.0f,
              "straight black orbs: the UFO stays within one flap of the line "
              "it entered at");
    }

    {
        for (const float sway : {20.0f, 50.0f}) {
            for (int delay = 0; delay <= 1; ++delay) {
                const OrbFlight moving =
                    flyBlackOrbGap(110.0f, sway, delay, 30, 26, true);
                const OrbFlight fixed =
                    flyBlackOrbGap(110.0f, sway, delay, 30, 26, false);
                std::printf("moving black-orb gap, sway %.0f, presses %s: deaths %d "
                            "following vs %d fixed, drift from the moving line %.1f\n",
                            sway, delay ? "late" : "prompt", moving.m_deaths,
                            fixed.m_deaths, moving.m_lineDrift);
                check(moving.m_deaths <= fixed.m_deaths && moving.m_deaths <= 1,
                      "moving black-orb gap: following the gap never loses it");
            }
        }
        const OrbFlight coarse =
            flyBlackOrbGap(110.0f, 50.0f, 1, 30, 26, true, COARSE_PHYSICS);
        check(coarse.m_deaths == 0,
              "moving black-orb gap: held with coarse physics and late presses");
    }

    if (g_failures) {
        std::printf("\nufogap: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("\nufogap: all checks passed\n");
    return 0;
}
