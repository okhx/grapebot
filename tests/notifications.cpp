#include "ui/notifications.hpp"
#include <cstdio>
using namespace grape::ui;
static int fails = 0;
static void check(bool c, const char* what) { if (!c) { std::printf("FAIL: %s\n", what); ++fails; } }

static void advance(NotificationCenter& center, float seconds) {
    for (float spent = 0.0f; spent < seconds; spent += 1.0f / 60.0f)
        center.update(1.0f / 60.0f);
}
int main() {
    auto& center = NotificationCenter::get();
    center.clear();
    const unsigned id = center.push("Update Available!", "Version 1.4 is out.");
    check(center.notices().size() == 1, "a toast is queued");
    check(center.notices().front().slide() == 1.0f, "and starts off screen");
    advance(center, NOTIFICATION_SLIDE_IN);
    check(center.notices().front().slide() == 0.0f, "slides fully in");
    check(center.notices().front().opacity() == 1.0f, "at full opacity");
    advance(center, NOTIFICATION_DEFAULT_LIFE);
    check(center.empty(), "and retires itself");

    center.clear();
    for (int i = 0; i < 7; ++i) center.push("T", "B");
    center.update(0.001f);
    int live = 0;
    for (const auto& n : center.notices()) if (n.m_life > NOTIFICATION_SLIDE_OUT + .01f) ++live;
    check(live == static_cast<int>(NOTIFICATION_MAX_VISIBLE),
          "an overflowing stack retires its oldest instead of growing");

    center.clear();
    const unsigned edited = center.push("Old", "Body");
    center.find(edited)->m_title = "New";
    check(center.notices().front().m_title == "New", "a raised toast can be edited");
    check(center.dismiss(edited), "and dismissed");
    advance(center, NOTIFICATION_SLIDE_OUT + .05f);
    check(center.empty(), "which finishes the exit");

    center.clear();
    center.push("A", "B");
    center.update(100.0f);
    check(!center.empty(), "one enormous frame does not skip the animation");
    check(center.notices().front().m_age <= .1f,
          "because the step it is allowed to take is capped");
    if (fails) { std::printf("\nnotifications: %d check(s) failed\n", fails); return 1; }
    std::printf("\nnotifications: all checks passed\n");
    return 0;
}
