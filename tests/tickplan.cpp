#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>

#include "engine/arm_encode.hpp"
#include "engine/tick_plan.hpp"

using namespace grape;

static long long simulate(double tps, double fps, double seconds, double jitter,
                          int cap = 0, double* spent = nullptr) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> wobble(-jitter, jitter);
    const double step = ticks::timestep(tps, 1.0);
    double carry = 0.0;
    double elapsed = 0.0;
    long long total = 0;
    const int frames = static_cast<int>(fps * seconds);
    for (int i = 0; i < frames; ++i) {
        double dt = 1.0 / fps;
        if (jitter > 0.0 && i + 1 < frames) dt *= 1.0 + wobble(rng);
        if (i + 1 == frames && seconds > elapsed) dt = seconds - elapsed;
        elapsed += dt;
        const auto plan = ticks::plan({carry, dt, step, false, false, cap});
        assert(plan.m_steps >= 0);
        assert(plan.m_carry >= 0.0 && plan.m_carry < step + 1e-9);
        carry = plan.m_carry;
        total += plan.m_steps;
    }
    if (spent) *spent = elapsed;
    return total;
}

int main() {
    assert(simulate(240, 60, 10, 0) == 2400);
    assert(simulate(240, 120, 10, 0) == 2400);
    assert(simulate(1000, 60, 10, 0) == 10000);
    double spent = 0.0;
    const long long jittered = simulate(360, 144, 20, .3, 0, &spent);
    assert(std::llabs(jittered - static_cast<long long>(spent * 360)) <= 1);
    const long long coarse = simulate(2000, 30, 5, .5, 0, &spent);
    assert(std::llabs(coarse - static_cast<long long>(spent * 2000)) <= 1);

    const double step = ticks::timestep(240, 1.0);
    auto held = ticks::plan({.5, 1.0, step, true, false, 0});
    assert(held.m_steps == 0 && held.m_carry == 0.0);
    auto stepped = ticks::plan({.5, 1.0, step, true, true, 0});
    assert(stepped.m_steps == 1 && stepped.m_carry == 0.0);

    auto capped = ticks::plan({0.0, 1.0, step, false, false, 8});
    assert(capped.m_steps == 8 && capped.m_carry == 0.0);

    auto nan = ticks::plan({.001, NAN, step, false, false, 0});
    assert(nan.m_steps == 0 && nan.m_carry == .001);

    auto slow = ticks::timestep(240, .5);
    assert(std::abs(slow - .5 / 240.0) < 1e-15);
    assert(ticks::timestep(0, 1.0) == 1.0 / 240.0);

    assert(ticks::batchesExact(ticks::timestep(240, 1.0)));
    assert(ticks::batchesExact(ticks::timestep(240, 2.0)));
    assert(!ticks::batchesExact(ticks::timestep(240, .5)));
    assert(!ticks::batchesExact(ticks::timestep(360, 1.0)));
    assert(!ticks::batchesExact(ticks::timestep(1000, 1.0)));
    assert(ticks::batchSize(7, ticks::timestep(240, 1.0), false) == 7);
    assert(ticks::batchSize(7, ticks::timestep(360, 1.0), false) >= 4);
    assert(ticks::batchSize(7, ticks::timestep(360, 1.0), true) == 7);
    assert(ticks::batchSize(0, ticks::timestep(360, 1.0), false) == 0);

    for (double tps : {360.0, 500.0, 600.0, 1000.0, 2000.0, 6000.0, 10000.0, 37.5}) {
        for (double warp : {1.0, 0.5, 0.37}) {
            const double tick = ticks::timestep(tps, warp);
            const float single = static_cast<float>(tick * 60.0);
            for (int steps = 1; steps <= 400; ++steps) {
                int left = steps;
                int calls = 0;
                while (left > 0) {
                    const int batch = ticks::batchSize(left, tick, false);
                    assert(batch >= 1 && batch <= left);
                    const float perTick =
                        static_cast<float>(batch * tick * 60.0) / static_cast<float>(batch);
                    assert(perTick == single);
                    left -= batch;
                    ++calls;
                }
                assert(calls <= 9);
            }
        }
    }

    for (double tps : {240.0, 360.0, 1000.0}) {
        for (double warp : {1.0, 0.5}) {
            const double tick = ticks::timestep(tps, warp);
            const int batch = ticks::batchSize(1, tick, false);
            const float game = static_cast<float>(batch * tick * 60.0) / static_cast<float>(batch);
            assert(ticks::stepDelta60(tick) == game);
        }
    }
    assert(ticks::stepDelta60(ticks::timestep(240, 1.0)) == 0.25f);
    assert(ticks::predictionSteps(2.0, 240, 1.0, 0) == 480);
    assert(ticks::predictionSteps(2.0, 360, 1.0, 0) == 720);
    assert(ticks::predictionSteps(2.0, 1000, 1.0, 0) == 960);
    assert(ticks::predictionSteps(2.0, 60, 1.0, 0) == 120);
    assert(ticks::predictionSteps(2.0, 240, 0.5, 0) == 960);
    assert(ticks::predictionSteps(2.0, 240, 1.0, 100) == 100);
    assert(ticks::predictionSteps(0.0, 240, 1.0, 0) == 0);
    assert(ticks::predictionSteps(2.0, NAN, 1.0, 0) == 480);

    double extra = 0.0;
    int vanillaTicks = 0;
    for (int i = 0; i < 600; ++i) {
        auto v = ticks::vanilla(1.0 / 60.0, extra, step);
        extra = v.m_extra;
        vanillaTicks += v.m_steps;
    }
    assert(vanillaTicks == 2400);

    using namespace grape::arm;
    static_assert(a64MoveWide(false, 9, 0x1234, 0) == 0xd2824689u);
    static_assert(a64MoveWide(true, 9, 0xbeef, 16) == 0xf2b7dde9u);
    static_assert(a64MoveWide(true, 9, 0x7fff, 32) == 0xf2cfffe9u);
    static_assert(a64MoveWide(true, 9, 0x0001, 48) == 0xf2e00029u);
    static_assert(a64LoadWord(0, 9, 0) == 0xb9400120u);
    static_assert(a64LoadSingle(0, 9, 8) == 0xbd400920u);
    static_assert(a64Branch(0x1000, 0x1014) == 0x14000005u);
    static_assert(a64Branch(0x1000, 0x0ff0) == 0x17fffffcu);
    static_assert(a64PageAddress(9, 0x1fe724, 0x8c4008) == 0xd0003629u);
    static_assert(t32MoveHalf(false, 1, 0xabcd)[0] == 0xf64a &&
                  t32MoveHalf(false, 1, 0xabcd)[1] == 0x31cd);
    static_assert(t32MoveHalf(true, 1, 0x1234)[0] == 0xf2c1 &&
                  t32MoveHalf(true, 1, 0x1234)[1] == 0x2134);

    constexpr auto wordLoad = a64WordTickLoad(0x5000, 0x7fff'beef'1234, 0x5028);
    static_assert(wordLoad[20] == 0x05 && wordLoad[23] == 0x14);
    constexpr auto jitless = a64SingleTickLoadRelative(0x1fe724, 0x8c4008);
    static_assert(jitless[0] == 0x29 && jitless[3] == 0xd0);
    static_assert(jitless[4] == 0x20 && jitless[7] == 0xbd);
    static_assert(jitless[36] == 0x1f && jitless[39] == 0xd5);
    constexpr auto thumb = t32TickLoad(0x1234abcd);
    static_assert(thumb[8] == 0x08 && thumb[9] == 0x68 && thumb[10] == 0x00 &&
                  thumb[11] == 0xbf);

    const uint8_t haystack[] = {1, 0xEE, 2, 3, 4, 0xEE, 5, 6, 0xF7, 0xEE, 9,
                                0x7B, 0x17, 0xEE, 0x90, 0x0A};
    const int16_t pattern[] = {0xEE, ANY,  ANY,  ANY,  0xEE, ANY,  ANY, 0xF7,
                               0xEE, ANY,  0x7B, 0x17, 0xEE, 0x90, 0x0A};
    auto found = findMasked(haystack, pattern);
    assert(found && *found == 1);

    std::puts("tickplan ok");
}
