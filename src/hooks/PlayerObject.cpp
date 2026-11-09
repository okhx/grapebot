#include <Geode/Geode.hpp>

#include "checkpoint/practice.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "features/death_guard.hpp"
#include "features/intentional_death.hpp"
#include "label/label.hpp"
#include "physics/player.hpp"
#include "physics/gravity.hpp"
#include "render/renderer.hpp"
#include "trajectory/trajectory.hpp"

using namespace geode::prelude;

#include <Geode/modify/PlayerObject.hpp>

namespace {

constexpr float LEGACY_RING_Y_START = 11.180032f;
constexpr double QUICK_CHECKPOINT_GAP = 0.2;
constexpr double CHECKPOINT_GAP = 1.0;

constexpr const char* SIMULATED_HOOKS[] = {
    "PlayerObject::ringJump",      "PlayerObject::bumpPlayer",
    "PlayerObject::propellPlayer", "PlayerObject::startDashing",
#ifdef GEODE_IS_WINDOWS
    "PlayerObject::stopDashing",
#endif
};

GrapeEngine& bot() { return *GrapeEngine::get(); }

}

struct GrapePlayerObject : Modify<GrapePlayerObject, PlayerObject> {
    static void onModify(auto& self) {
        for (const char* name : SIMULATED_HOOKS) {
            if (self.setHookPriorityPre(name, Priority::VeryEarly)) continue;
            geode::log::warn("Could not run {} first; other mods may see trajectory players", name);
        }
    }

    void update(float dt) {
        PlayerObject::update(dt);
        if (!simulated()) bot().trajectory().setDelta(dt);
    }

    void playSpawnEffect() {
        if (!bot().keeper().m_pendingRestore) PlayerObject::playSpawnEffect();
    }

    void playDeathEffect() {
        if (!grape::death::hidesDeathEffect()) PlayerObject::playDeathEffect();
    }

    void handleButton(bool down, int button, bool player1) {
        if (button == 1) bot().trajectory().handleButton(player1, down);
    }

    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (!bot().trajectory().drawing()) PlayerObject::playSpiderDashEffect(from, to);
    }

    void incrementJumps() {
        if (!bot().trajectory().drawing()) PlayerObject::incrementJumps();
    }

#ifndef GEODE_IS_IOS
    bool levelFlipping() {
        return !LevelEditorLayer::get() && PlayerObject::levelFlipping();
    }
#endif

    void ringJump(RingObject* ring, bool unk) {
        if (simulated()) return phys::ringJump(this, ring);

        const bool legacyHeight = GrapeSettings::get()->physics21;
        const float yStart = m_yStart;
        if (legacyHeight) m_yStart = LEGACY_RING_Y_START;
        PlayerObject::ringJump(ring, unk);
        if (legacyHeight) m_yStart = yStart;
        bot().labels().update(Renderer::get()->isRecording());
    }

    void bumpPlayer(float force, int type, bool effect, GameObject* object) {
        if (simulated()) return phys::bumpPlayer(this, force, type, effect, object);
        PlayerObject::bumpPlayer(force, type, effect, object);
    }

    void propellPlayer(float force, bool noEffect, int type) {
        if (simulated()) return phys::propellPlayer(this, force, noEffect, type);
        PlayerObject::propellPlayer(force, noEffect, type);
    }

    void startDashing(DashRingObject* ring) {
        if (simulated()) return phys::startDashing(this, ring);
        PlayerObject::startDashing(ring);
    }

#ifdef GEODE_IS_WINDOWS
    void stopDashing() {
        if (simulated()) return phys::stopDashing(this);
        PlayerObject::stopDashing();
    }
#endif

    void removePendingCheckpoint() {}

    void tryPlaceCheckpoint() {
        if (!GameManager::get()->getGameVariable("0027")) return;
        const double gap = m_quickCheckpointMode ? QUICK_CHECKPOINT_GAP : CHECKPOINT_GAP;
        const double elapsed = m_gameLayer->m_gameState.m_totalTime - m_lastCheckpointTime;
        if (!(elapsed > gap)) return;
        m_gameLayer->m_uiLayer->onCheck(nullptr);
        m_shouldTryPlacingCheckpoint = false;
        m_lastCheckpointTime = m_totalTime;
    }

    void releaseAllButtons() {
        if (!bot().isEnabled()) return PlayerObject::releaseAllButtons();
        auto* layer = GJBaseGameLayer::get();
        const bool idleSecondPlayer = this == layer->m_player2 && !layer->m_gameState.m_isDualMode;
        if (idleSecondPlayer || grape::intentional::armed() || bot().isPlaying())
            PlayerObject::releaseAllButtons();
    }

   private:
    bool simulated() { return bot().trajectory().isFakePlayer(this); }
};
