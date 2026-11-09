#include <Geode/Geode.hpp>

#include <chrono>
#include <utility>

#include "engine/engine.hpp"
#include "engine/clock.hpp"
#include "engine/timeline.hpp"
#include "render/renderer.hpp"
#include "shared/value/value.hpp"
#include "frame_fallback.hpp"

using namespace geode::prelude;

namespace grape::frame {
namespace {
bool s_drawSceneRan = false;
bool s_steppedWhileRecording = false;
CCScene* s_hiddenScene = nullptr;
std::chrono::steady_clock::time_point s_lastPresent{};
constexpr auto PRESENT_EVERY = std::chrono::microseconds(16'667);
int s_framesWithoutHook = 0;
int s_retries = 0;
constexpr int RETRY_EVERY_FRAMES = 120;
constexpr int MAX_RETRIES = 20;
constexpr const char* DRAW_SCENE_HOOK = "cocos2d::CCDirector::drawScene";

void retryDrawSceneHook() {
    if (s_retries >= MAX_RETRIES) return;
    if (++s_framesWithoutHook < RETRY_EVERY_FRAMES) return;
    s_framesWithoutHook = 0;
    ++s_retries;
    if (!GrapeEngine::get()->m_enabled->inner()) return;
    for (Hook* hook : Mod::get()->getHooks()) {
        if (hook->getDisplayName() != DRAW_SCENE_HOOK || hook->isEnabled()) continue;
        auto result = hook->enable();
        if (result.isOk())
            geode::log::info("Enabled {} on retry {}", DRAW_SCENE_HOOK, s_retries);
        else if (s_retries == MAX_RETRIES)
            geode::log::warn("{} is still unavailable ({}); using the swap fallback",
                             DRAW_SCENE_HOOK, result.unwrapErr());
    }
}
}

void markDrawScene() { s_drawSceneRan = true; }

bool drawSceneHooked() {
    for (Hook* hook : Mod::get()->getHooks())
        if (hook->getDisplayName() == DRAW_SCENE_HOOK) return hook->isEnabled();
    return false;
}

bool presentDue() {
    if (!Renderer::get()->isRecording()) return true;
    const auto now = std::chrono::steady_clock::now();
    if (now - s_lastPresent < PRESENT_EVERY) return false;
    s_lastPresent = now;
    return true;
}

bool levelRunning(PlayLayer* level, Renderer* renderer) {
    return level && !level->m_isPaused && (level->m_started || renderer->m_settings.m_firstAttemptPause);
}

bool holdForCapture() {
    auto* renderer = Renderer::get();
    s_steppedWhileRecording = false;
    if (!renderer->isRecording()) return false;
    if (renderer->m_halting || !renderer->canCapture()) return true;
    s_steppedWhileRecording = true;

    auto* scene = CCDirector::get()->getRunningScene();
    if (!s_drawSceneRan && !s_hiddenScene && scene && levelRunning(PlayLayer::get(), renderer)) {
        s_hiddenScene = scene;
        s_hiddenScene->retain();
        s_hiddenScene->setVisible(false);
    }
    return false;
}

bool beforeSwap() {
    if (s_hiddenScene) {
        s_hiddenScene->setVisible(true);
        std::exchange(s_hiddenScene, nullptr)->release();
    }
    if (s_drawSceneRan) return true;

    const bool present = presentDue();
    if (std::exchange(s_steppedWhileRecording, false) && GrapeEngine::get()->isEnabled()) {
        auto* level = PlayLayer::get();
        auto* renderer = Renderer::get();
        if (levelRunning(level, renderer)) {
            renderer->update(level);
            if (present) renderer->displayPreview();
        }
    }
    return present;
}

void afterSwap() {
    if (s_drawSceneRan) {
        s_drawSceneRan = false;
        return;
    }
    auto* settings = GrapeSettings::get();
    BindingManager::get()->pump(settings->keybindHoldDelay, settings->keybindRepeatFrequency);
    if (GrapeEngine::get()->isEnabled()) GrapeEngine::get()->timeline().runFrozenTick();
    retryDrawSceneHook();
}

}

#include <Geode/modify/CCDirector.hpp>

struct GrapeCCDirector : Modify<GrapeCCDirector, CCDirector> {
    void drawScene() {
        grape::frame::markDrawScene();
        const auto* settings = GrapeSettings::get();
        BindingManager::get()->pump(settings->keybindHoldDelay, settings->keybindRepeatFrequency);

        auto* bot = GrapeEngine::get();
        if (!bot->isEnabled()) return CCDirector::drawScene();
        bot->ticker().pump(getDeltaTime());
        bot->timeline().updateAudioSpeedhack();

        auto* level = PlayLayer::get();
        if (!level) return CCDirector::drawScene();

        auto* renderer = Renderer::get();
        auto& timeline = bot->timeline();
#ifdef GEODE_IS_MOBILE
        if (renderer->isRecording()) {
            CCDirector::drawScene();
            renderer->update(level);
            timeline.runFrozenTick();
            return;
        }
#endif

        const bool levelRunning = !level->m_isPaused && (level->m_started || renderer->m_settings.m_firstAttemptPause);
        if (!renderer->isRecording() || !levelRunning) {
            CCDirector::drawScene();
            timeline.runFrozenTick();
            return;
        }

        const bool advance = !renderer->m_halting && renderer->canCapture();
        if (advance) {
            if (!m_bPaused) m_pScheduler->update(renderer->getDt());
            if (m_pNextScene) setNextScene();
            renderer->update(level);
        }
        if (grape::frame::presentDue()) {
            renderer->displayPreview();
            m_pobOpenGLView->swapBuffers();
        }
        timeline.runFrozenTick();
    }
};
