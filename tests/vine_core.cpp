#include "vine/playhead.hpp"
#include "vine/rng.hpp"

#include <cstdio>
#include <vector>

using grape::vine::Event;
using grape::vine::EventKind;
using grape::vine::Playhead;
using grape::vine::RngCore;
using grape::vine::RngWords;

static int g_checks = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (cond) {                                                     \
            ++g_checks;                                                 \
        } else {                                                        \
            std::printf("FAILED at line %d\n", __LINE__);               \
            return 1;                                                   \
        }                                                               \
    } while (0)

static std::vector<Event> sampleEvents() {
    return {
        Event(10, EventKind::Jump, true, false),
        Event(10, EventKind::Jump, false, false),
        Event(25, EventKind::Left, true, true),
        Event(40, EventKind::Death, 123u),
        Event(40, EventKind::Jump, true, false),
        Event(90, EventKind::Tps, 480.0),
    };
}

static int testPlayheadBasics() {
    auto events = sampleEvents();
    Playhead head;
    head.attach(events);

    CHECK(head.exhausted() == false);
    CHECK(head.position() == 0);
    CHECK(head.remaining() == events.size());

    CHECK(head.peek()->m_frame == 10);
    CHECK(head.peekDue(9) == nullptr);
    CHECK(head.peekDue(10) != nullptr);

    head.advance();
    CHECK(head.peek()->m_frame == 10);
    head.advance();
    CHECK(head.peek()->m_frame == 25);

    CHECK(head.framesUntilNext(20).value() == 5);
    head.advance();
    CHECK(head.framesUntilNext(30).value() == 10);
    CHECK(head.framesUntilNext(40).value() == 0);

    while (!head.exhausted()) head.advance();
    CHECK(head.peek() == nullptr);
    CHECK(head.framesUntilNext(1000) == std::nullopt);
    head.advance();
    CHECK(head.exhausted());

    Playhead bare;
    CHECK(bare.exhausted());
    CHECK(bare.peek() == nullptr);
    CHECK(bare.remaining() == 0);
    bare.advance();
    CHECK(bare.exhausted());
    bare.seek(10);
    CHECK(bare.position() == 0);
    bare.seekAfter(10);
    CHECK(bare.position() == 0);
    return 0;
}

static int testPlayheadSeek() {
    auto events = sampleEvents();
    Playhead head;
    head.attach(events);

    head.seek(25);
    CHECK(head.peek()->m_frame == 25);
    head.seek(26);
    CHECK(head.peek()->m_frame == 40);
    head.seek(0);
    CHECK(head.peek()->m_frame == 10);
    head.seek(1000);
    CHECK(head.exhausted());

    head.seekAfter(10);
    CHECK(head.peek()->m_frame == 25);
    head.seekAfter(40);
    CHECK(head.peek()->m_frame == 90);
    head.seekAfter(41);
    CHECK(head.peek()->m_frame == 90);
    return 0;
}

static int testPlayheadSkipAt() {
    auto events = sampleEvents();
    Playhead head;
    head.attach(events);

    head.setPosition(3);
    head.skipAt(40, false);
    CHECK(head.peek() != nullptr && head.peek()->m_frame == 90);

    head.seek(0);
    head.skipAt(40, false);
    CHECK(head.peek()->m_frame == 10);

    head.setPosition(3);
    head.skipAt(40, true);
    CHECK(head.peek() != nullptr && head.peek()->m_frame == 90);

    head.seek(0);
    head.skipAt(7, true);
    CHECK(head.peek()->m_frame == 10);
    return 0;
}

static int testPlayheadReattach() {
    auto events = sampleEvents();
    Playhead head;
    head.attach(events);
    head.seek(40);

    events.erase(events.begin());
    head.attach(events);
    head.setPosition(0);
    CHECK(head.peek()->m_frame == 10);

    head.setPosition(0);
    events.clear();
    head.attach(events);
    CHECK(head.exhausted());
    return 0;
}

static uint64_t oldShakeFormula(uint64_t state) {
    return (uint64_t)(int)((214013 * state + 2531011) >> 16) & 0x7FFF;
}

static int testRngDraws() {
    RngCore rng;
    rng.restore({0, 12345, 999});

    uint64_t expected = 12345;
    for (int i = 0; i < 5; ++i) {
        expected = oldShakeFormula(expected);
        CHECK(rng.drawShake() == expected);
    }
    uint64_t teleExpected = 999;
    for (int i = 0; i < 5; ++i) {
        teleExpected = oldShakeFormula(teleExpected);
        CHECK(rng.drawTeleport() == teleExpected);
    }
    return 0;
}

static int testRngWords() {
    RngCore rng;
    rng.restore({11, 22, 33});
    rng.setBaseSeed(444);
    rng.setAttemptSeed(555);

    const RngWords words = rng.capture();
    CHECK(words.m_main == 11);
    CHECK(words.m_shake == 22);
    CHECK(words.m_teleport == 33);

    rng.restore({99, 88, 77});
    rng.restore(words);
    CHECK(rng.capture().m_main == 11);
    CHECK(rng.drawShake() == oldShakeFormula(22));

    rng.beginAttempt(0xABCD);
    CHECK(rng.main() == 0xABCD);
    CHECK(rng.shake() == (0xABCD & 0x7FFF));
    CHECK(rng.attemptSeed() == 0xABCD);
    return 0;
}

static int testRngLcgAndVariance() {
    CHECK(RngCore::lcg(0) == 2531011u);
    CHECK(RngCore::lcg(1) == 214013 * 1 + 2531011u);

    uint64_t seed = 987654321;
    std::vector<float> expected(8);
    uint64_t state = seed;
    for (auto& v : expected) {
        v = (float)(state & 0xFFFF) / 32768.0 - 1.0;
        state = 214013 * state + 2531011;
    }

    std::vector<float> actual(8);
    RngCore::applyVariance(actual, seed);
    for (size_t i = 0; i < expected.size(); ++i)
        CHECK(actual[i] == expected[i]);
    return 0;
}

static int testPlayheadSkipResetsAt() {
    std::vector<Event> events{
        Event(10, EventKind::Jump, true, false),
        Event(40, EventKind::Restart, 7u),
        Event(40, EventKind::Jump, true, false),
        Event(55, EventKind::RestartFull, 9u),
        Event(70, EventKind::Death, 11u),
        Event(70, EventKind::Death, 12u),
        Event(70, EventKind::Left, true, false),
    };
    Playhead head;
    head.attach(events);

    head.seek(40);
    CHECK(head.peek() != nullptr);
    CHECK(head.peek()->m_type == EventKind::Restart);
    head.skipResetsAt(40);
    CHECK(head.peek() != nullptr);
    CHECK(head.peek()->m_type == EventKind::Jump);
    CHECK(head.peek()->m_frame == 40);
    CHECK(head.peekDue(40) != nullptr);

    head.seek(55);
    head.skipResetsAt(55);
    CHECK(head.peek() != nullptr);
    CHECK(head.peek()->m_frame == 70);

    head.seek(70);
    head.skipResetsAt(70);
    CHECK(head.peek() != nullptr);
    CHECK(head.peek()->m_type == EventKind::Left);

    head.seek(10);
    head.skipResetsAt(10);
    CHECK(head.peek() != nullptr);
    CHECK(head.peek()->m_type == EventKind::Jump);
    CHECK(head.peek()->m_frame == 10);

    head.seek(40);
    head.skipResetsAt(39);
    CHECK(head.peek() != nullptr);
    CHECK(head.peek()->m_type == EventKind::Restart);
    return 0;
}

int main() {
    if (testPlayheadBasics()) return 1;
    if (testPlayheadSeek()) return 1;
    if (testPlayheadSkipAt()) return 1;
    if (testPlayheadSkipResetsAt()) return 1;
    if (testPlayheadReattach()) return 1;
    if (testRngDraws()) return 1;
    if (testRngWords()) return 1;
    if (testRngLcgAndVariance()) return 1;
    std::printf("vine_core: %d checks passed\n", g_checks);
    return 0;
}
