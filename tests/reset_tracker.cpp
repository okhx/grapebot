#include "vine/reset_tracker.hpp"

#include <cstdio>

using grape::vine::EventKind;
using grape::vine::ResetTracker;

static int g_failures = 0;

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            std::printf("FAILED at line %d: %s\n", __LINE__, #condition); \
            ++g_failures;                                                 \
        }                                                                 \
    } while (false)

int main() {
    ResetTracker plain;
    CHECK(plain.eventToRecord() == EventKind::Restart);
    CHECK(!plain.restartsCleanly(false));
    CHECK(plain.restartsCleanly(true));
    CHECK(!plain.levelWasRestarted());

    ResetTracker died;
    died.playerDied();
    CHECK(died.eventToRecord() == EventKind::Death);

    ResetTracker full;
    full.playerRestartedLevel();
    CHECK(full.eventToRecord() == EventKind::RestartFull);
    CHECK(full.levelWasRestarted());

    ResetTracker deathThenFull;
    deathThenFull.playerDied();
    deathThenFull.playerRestartedLevel();
    CHECK(deathThenFull.eventToRecord() == EventKind::Death);

    ResetTracker fullThenDeath;
    fullThenDeath.playerRestartedLevel();
    fullThenDeath.playerDied();
    CHECK(fullThenDeath.eventToRecord() == EventKind::Death);

    ResetTracker macro;
    macro.macroAskedForReset();
    CHECK(macro.fromMacro());
    CHECK(macro.restartsCleanly(false));
    CHECK(!macro.levelWasRestarted());
    macro.forget();
    CHECK(!macro.fromMacro());
    CHECK(macro.eventToRecord() == EventKind::Restart);

    if (g_failures == 0) std::printf("reset_tracker: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
