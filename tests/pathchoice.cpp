
#include "assist/pathchoice.hpp"

#include <cstdio>
#include <string>

using namespace grape::assist;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        std::printf("FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

namespace {

constexpr int HORIZON = 60;

Situation ground(bool holding = false) {
    Situation state;
    state.m_holding = holding;
    state.m_groundJump = true;
    state.m_reactionFrames = 12;
    state.m_hazardWindow = 8.0f;
    state.m_blockWindow = 60.0f;
    return state;
}

Situation ship(bool holding = false, bool upsideDown = false) {
    Situation state;
    state.m_holding = holding;
    state.m_flying = true;
    state.m_upsideDown = upsideDown;
    state.m_reactionFrames = 8;
    return state;
}

}

int main() {
    {
        Proximity nearby;
        nearby.m_block = 20.0f;
        nearby.m_hazard = 5.0f;
        const auto out = chooseInput({HORIZON, HORIZON, HORIZON}, nearby,
                                     ground());
        check(out.m_choice == Choice::Keep,
              "an obstacle we are predicted to clear is not a reason to jump");
        check(std::string(out.m_reason) == "safe", "and it is reported as safe");
    }
    {
        Proximity nearby;
        nearby.m_block = 20.0f;
        const auto out =
            chooseInput({HORIZON, 9, HORIZON}, nearby, ground());
        check(out.m_choice == Choice::Hold,
              "an obstacle we would hit is jumped");
    }
    {
        Proximity nearby;
        nearby.m_block = 20.0f;
        const auto out = chooseInput({10, 9, HORIZON}, nearby, ground());
        check(out.m_choice == Choice::Keep,
              "a jump that does not survive longer is not taken");
    }
    {
        Proximity nearby;
        const auto out = chooseInput({10, 9, HORIZON}, nearby, ground());
        check(out.m_choice == Choice::Keep,
              "a one-frame improvement is below the switch margin");
        const auto worth = chooseInput({20, 9, HORIZON}, nearby, ground());
        check(worth.m_choice == Choice::Hold,
              "a clear improvement is taken");
    }

    {
        Proximity nearby;
        nearby.m_ring = 10.0f;
        const auto safe =
            chooseInput({HORIZON, HORIZON, HORIZON}, nearby, ground());
        check(safe.m_choice == Choice::Keep,
              "a ring in range is not clicked when nothing needs it");

        const auto needed = chooseInput({HORIZON, 8, HORIZON}, nearby, ground());
        check(needed.m_choice == Choice::Hold, "a ring that is needed is taken");
        check(std::string(needed.m_reason) == "escape" ||
                  std::string(needed.m_reason) == "orb",
              "and the reason names the rule that fired");
    }
    {
        Proximity nearby;
        nearby.m_ring = 200.0f;
        const auto out = chooseInput({30, 25, HORIZON}, nearby, ground());
        check(out.m_choice == Choice::Keep, "a distant ring is ignored");
    }

    {
        Proximity nearby;
        const auto out = chooseInput({HORIZON, 5, HORIZON}, nearby, ground());
        check(out.m_choice == Choice::Hold, "escape towards the safe input");
        const auto held =
            chooseInput({5, HORIZON, HORIZON}, nearby, ground(true));
        check(held.m_choice == Choice::Release,
              "escape works in the other direction too");
    }
    {
        Proximity nearby;
        const auto out = chooseInput({HORIZON, 40, HORIZON}, nearby, ground());
        check(out.m_choice == Choice::Keep,
              "a distant death is left to the planner");
    }

    {
        Proximity nearby;
        nearby.m_lowerClearance = 4.0f;
        nearby.m_upperClearance = 40.0f;
        const auto out =
            chooseInput({HORIZON, HORIZON, HORIZON}, nearby, ship());
        check(out.m_choice == Choice::Hold,
              "about to scrape the floor: climb, even when otherwise safe");
        const auto flipped = chooseInput({HORIZON, HORIZON, HORIZON}, nearby,
                                         ship(false, true));
        check(flipped.m_choice == Choice::Release,
              "inverted gravity reverses which button climbs");
    }
    {
        Proximity nearby;
        nearby.m_upperClearance = 4.0f;
        nearby.m_lowerClearance = 40.0f;
        const auto out =
            chooseInput({HORIZON, HORIZON, HORIZON}, nearby, ship(true));
        check(out.m_choice == Choice::Release,
              "about to scrape the ceiling: descend");
    }
    {
        Proximity nearby;
        nearby.m_upperClearance = 2.0f;
        nearby.m_lowerClearance = 2.0f;
        const auto out =
            chooseInput({HORIZON, HORIZON, HORIZON}, nearby, ground());
        check(out.m_choice == Choice::Keep,
              "clearance steering is only for flying gamemodes");
    }

    {
        Proximity nearby;
        nearby.m_block = 20.0f;
        Situation state = ground();
        state.m_groundJump = false;
        const auto out = chooseInput({HORIZON, 9, HORIZON}, nearby, state);
        check(out.m_choice == Choice::Hold,
              "non-ground modes are still rescued by the escape rule");
    }

    {
        Proximity nearby;
        const auto out = chooseInput({0, 0, 0}, nearby, ground());
        check(out.m_choice == Choice::Keep || out.m_choice == Choice::Hold,
              "a zero horizon does not divide by zero or assert");
    }

    if (g_failures) {
        std::printf("\npathchoice: %d of %d checks failed\n", g_failures,
                    g_checks);
        return 1;
    }
    std::printf("pathchoice: %d checks passed\n", g_checks);
    return 0;
}
