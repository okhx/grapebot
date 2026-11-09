#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <ui/mobile_menu.hpp>
#ifndef GEODE_IS_MOBILE
#include <ui/manager.hpp>
#endif

#include "checkpoint/practice.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "render/renderer.hpp"
#include "vine/session.hpp"

#ifdef GEODE_IS_WINDOWS
#include "util/midhook.hpp"
#endif

using namespace geode::prelude;

struct GrapePauseLayer : Modify<GrapePauseLayer, PauseLayer> {
    void goEdit() {
        auto* bot = GrapeEngine::get();
        if (auto* renderer = Renderer::get(); renderer->isRecording()) renderer->signalStop();
        bot->timeline().setPaused(false);
        PauseLayer::goEdit();
        bot->keeper().wipe();
        bot->replay().onLevelExit();
    }

    void customSetup() override {
        PauseLayer::customSetup();

        CCNode* menu = getChildByID("right-button-menu");
        auto* icon = CCSprite::create("grape.png"_spr);
        if (!menu || !icon) return;
        icon->setScale(0.35f);
        menu->addChild(CCMenuItemSpriteExtra::create(icon, this, menu_selector(GrapePauseLayer::onGrapeOpen)));
        menu->updateLayout();
    }

    void onGrapeOpen(CCObject*) {
#ifdef GEODE_IS_MOBILE
        MobileMenu::open();
#else
        GrapeEngine::get()->ui().m_state.m_visible->inner() = true;
#endif
    }
};

#if defined(GEODE_IS_WINDOWS) && GEODE_COMP_GD_VERSION == 22081
namespace {

void keepHeldButtons(SafetyHookContext& ctx) {
    if (GrapeEngine::get()->isPlaying()) ctx.rip += 5;
}

}

$execute {
    constexpr std::pair<uintptr_t, const char*> releaseCalls[] = {
        {0x3BA239, "pauseReleaseButtons"},
        {0x3BA27C, "pauseReleaseButtons"},
        {0x3BA879, "resumeReleaseButtons"},
    };
    for (const auto& [offset, label] : releaseCalls) util::midhook(geode::base::get() + offset, label, keepHeldButtons);
}
#endif
