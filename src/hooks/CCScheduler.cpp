#include <Geode/Geode.hpp>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "hooks/frame_fallback.hpp"
#include "render/renderer.hpp"
#ifdef GEODE_IS_MOBILE
#include "scripting/script_engine.hpp"
#include "ui/touch_overlay.hpp"
#endif

using namespace geode::prelude;

#include <Geode/modify/CCScheduler.hpp>

namespace {
float resolveSchedulerDelta(float dt) {
    if (dt <= 0.0f) return dt;

    auto* renderer = Renderer::get();
    if (renderer->isRecording()) {
        return renderer->getDt();
    }
    return dt;
}
}

struct GrapeCCScheduler : Modify<GrapeCCScheduler, CCScheduler> {
#ifdef GEODE_IS_ANDROID
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("cocos2d::CCScheduler::update",
                                      Priority::VeryLate);
    }
#endif

    void update(float dt) override {
#ifdef GEODE_IS_MOBILE
        TouchOverlay::get()->update(dt);
        struct ScriptFrame {
            float m_dt;
            ~ScriptFrame() {
                auto& scripts = grape::scripting::ScriptEngine::get();
                scripts.update(m_dt);
                scripts.overlay();
            }
        } scriptFrame{dt};
#endif
        dt = resolveSchedulerDelta(dt);
#ifndef GEODE_IS_MOBILE
        if (grape::frame::holdForCapture()) return;
#endif

        const auto bot = GrapeEngine::get();

        if (!bot->isEnabled() || bot->timeline().m_onlyRefresh) {
            CCScheduler::update(dt);
            return;
        }

#ifdef GEODE_IS_MOBILE
        bot->timeline().runMobileFrame(
            [this](float step) { this->CCScheduler::update(step); }, dt);
#else
        bot->timeline().runUpdates(
            [this](float step) { this->CCScheduler::update(step); }, dt,
            false);
#endif
    }
};
