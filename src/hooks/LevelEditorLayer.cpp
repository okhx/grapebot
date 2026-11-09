#include "Geode/binding/LevelEditorLayer.hpp"
#include <Geode/binding/EditorUI.hpp>

#include <Geode/Geode.hpp>

#include "assist/autoclicker.hpp"
#include "assist/hitboxes.hpp"
#include "features/intentional_death.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "render/renderer.hpp"
#include "trajectory/trajectory.hpp"
#include "vine/session.hpp"
#include "util/crash_log.hpp"
#ifdef GEODE_IS_MOBILE
#include "ui/mobile_menu.hpp"
#endif

using namespace geode::prelude;

#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>

namespace {
void refreshEditorOverlays(LevelEditorLayer* layer, bool saveTrail) {
    auto* bot = GrapeEngine::get();
    if (saveTrail) {
        bot->hitboxes().saveToTrail(layer);
    }
    bot->hitboxes().draw(layer);
    bot->trajectory().update(layer);
}
}

struct GrapeEditorUI : Modify<GrapeEditorUI, EditorUI> {
    GameObject* createObject(int objectID, CCPoint position) {
        auto* object = EditorUI::createObject(objectID, position);
        auto* bot = GrapeEngine::get();
        if (object && bot->isEnabled()) bot->hitboxes().snapPlaced(object);
        return object;
    }
};

struct GrapeLevelEditorLayer : Modify<GrapeLevelEditorLayer, LevelEditorLayer> {
   private:
    void restartPlayback() {
        auto* bot = GrapeEngine::get();
        auto& replay = bot->replay();
#ifdef GEODE_IS_MOBILE
        bot->timeline().m_editorStartProgress =
            static_cast<uint32_t>(std::max(0, static_cast<int>(m_gameState.m_currentProgress)));
        replay.onRespawn(0);
        if (bot->isPlaying()) {
            replay.writeLiveRandom(replay.m_rng.baseSeed());
            replay.m_rng.setAttemptSeed(replay.m_rng.baseSeed());
        }
#else
        bot->timeline().m_editorStartProgress = 0;
        replay.onRespawn(bot->timeline().getFrame());
#endif
        bot->autoclicker().reset();
        bot->trajectory().update(this);
        bot->hitboxes().clearTrail();
#ifdef GEODE_IS_MOBILE
        bot->hitboxes().saveToTrail(this);
        m_queuedButtons.clear();
        if (bot->isPlaying())
            for (PlayerObject* player : {m_player1, m_player2}) player->releaseAllButtons();
#else
        for (bool firstPlayer : {true, false}) handleButton(false, static_cast<int>(PlayerButton::Jump), firstPlayer);
#endif
    }

   public:
    void onPlaytest() {
        auto* bot = GrapeEngine::get();
        if (!bot->isEnabled()) return LevelEditorLayer::onPlaytest();

        auto& timeline = bot->timeline();
        timeline.m_tpsOverflow = 0.0;
        timeline.m_frameOnLastAttempt = 0;
        timeline.setPaused(false);
        timeline.resetFrame();
#ifndef GEODE_IS_MOBILE
        if (auto* ui = EditorUI::get(); ui && ui->m_isPlayingMusic) ui->onPlayback(nullptr);
        m_playbackActive = false;
#endif
        LevelEditorLayer::onPlaytest();
        restartPlayback();
        grape::intentional::settle();
    }

    bool init(GJGameLevel* level, bool p1) {
        crash_log::breadcrumb("Opening the editor");
        auto& trajectory = GrapeEngine::get()->trajectory();
#ifdef GEODE_IS_MOBILE
        trajectory.uninit();
#endif

        if (!LevelEditorLayer::init(level, p1)) return false;

        auto* bot = GrapeEngine::get();
        bot->hitboxes().init(this);
#ifndef GEODE_IS_MOBILE
        trajectory.uninit();
#endif
        trajectory.init(this);

#ifdef GEODE_IS_MOBILE
        if (m_editorUI) {
            auto* menu = CCMenu::create();
            menu->setPosition({0.f, 0.f});
            menu->setID("grape-editor-menu");
            auto* sprite = CCSprite::create("grape.png"_spr);
            if (!sprite) return true;
            sprite->setScale(.31f);
            auto* button = geode::cocos::CCMenuItemExt::createSpriteExtra(
                sprite, [](CCMenuItemSpriteExtra*) { MobileMenu::open(); });
            if (!button) return true;
            const auto winSize = CCDirector::get()->getWinSize();
            button->setPosition({winSize.width - 112.f,
                                 winSize.height - 20.f});
            menu->addChild(button);
            m_editorUI->addChild(menu, 1000);
        }
#endif

        return true;
    }

    void update(float dt) override {
        LevelEditorLayer::update(dt);

        if (m_playbackMode == PlaybackMode::Playing) {
            refreshEditorOverlays(this, true);
        }
    }

    void updateVisibility(float dt) override {
        LevelEditorLayer::updateVisibility(dt);

        refreshEditorOverlays(this, false);
    }

    void updateEditor(float dt) {
        auto* bot = GrapeEngine::get();
        const bool heldForPlayback =
            m_playbackActive && bot->timeline().isPaused() &&
            !Renderer::get()->isRecording() && !bot->isPlaying();

        if (heldForPlayback && !bot->timeline().consumeStep()) {
            bot->hitboxes().draw(this);
            bot->trajectory().update(this);
            return;
        }

        LevelEditorLayer::updateEditor(dt);

        if (m_playbackMode == PlaybackMode::Playing) {
            refreshEditorOverlays(this, true);
        }
    }
};
