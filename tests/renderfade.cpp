#include "render/fade.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace grape::render;

static int g_failures = 0;
static int g_checks = 0;

static void near(float value, float expected, const std::string& what,
                 float tolerance = 0.001f) {
    ++g_checks;
    if (std::abs(value - expected) > tolerance) {
        std::printf("FAIL: %s (got %.4f, wanted %.4f)\n", what.c_str(), value,
                    expected);
        ++g_failures;
    }
}

static void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        std::printf("FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

static void offByDefault() {
    const FadePlan plan{0.0f, 0.0f, 5.0f};
    near(plan.amountAt(0.0f, 0.0f), 0.0f, "the first frame is not darkened");
    near(plan.amountAt(0.5f, 0.0f), 0.0f, "nor any later frame");
    near(plan.amountAt(30.0f, 4.9f), 0.0f, "nor the end of the tail");
    near(plan.tail(), 5.0f, "the tail is left alone");
}

static void fadesInFromBlack() {
    const FadePlan plan{2.0f, 0.0f, 5.0f};
    near(plan.amountAt(0.0f, 0.0f), 1.0f, "the first frame is black");
    near(plan.amountAt(1.0f, 0.0f), 0.5f, "halfway through it is half dark");
    near(plan.amountAt(2.0f, 0.0f), 0.0f, "at the end it is clear");
    near(plan.amountAt(9.0f, 0.0f), 0.0f, "and stays clear");
}

static void fadesOutIntoBlack() {
    const FadePlan plan{0.0f, 2.0f, 5.0f};
    near(plan.amountAt(30.0f, 0.0f), 0.0f, "nothing happens before the end");
    near(plan.amountAt(30.0f, 2.9f), 0.0f, "nor while the tail has room left");
    near(plan.amountAt(30.0f, 4.0f), 0.5f, "one second from the end, half dark");
    near(plan.amountAt(30.0f, 5.0f), 1.0f, "the last frame is black");
    near(plan.amountAt(30.0f, 6.0f), 1.0f, "and it cannot go past black");
}

static void tailMakesRoomForTheFade() {
    const FadePlan plan{0.0f, 4.0f, 1.0f};
    near(plan.tail(), 4.0f,
         "a fade longer than the tail stretches the tail to fit");
    near(plan.amountAt(30.0f, 0.5f), 0.125f, "the fade starts at the end");
    near(plan.amountAt(30.0f, 4.0f), 1.0f, "and finishes black");

    const FadePlan roomy{0.0f, 1.0f, 6.0f};
    near(roomy.tail(), 6.0f, "a tail longer than the fade is kept");
    near(roomy.amountAt(30.0f, 4.0f), 0.0f, "so the fade waits for the end");
    near(roomy.amountAt(30.0f, 6.0f), 1.0f, "and still finishes black");
}

static void bothEndsOnAShortRecording() {
    const FadePlan plan{3.0f, 3.0f, 3.0f};
    near(plan.amountAt(0.0f, 0.0f), 1.0f, "it opens black");
    near(plan.amountAt(1.5f, 0.0f), 0.5f, "and clears");
    check(plan.amountAt(1.5f, 1.5f) >= 0.5f,
          "an overlap takes whichever end is darker");
    near(plan.amountAt(6.0f, 3.0f), 1.0f, "and closes black");
}

int main() {
    offByDefault();
    fadesInFromBlack();
    fadesOutIntoBlack();
    tailMakesRoomForTheFade();
    bothEndsOnAShortRecording();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
