#include <Geode/Geode.hpp>

#include <array>
#include <filesystem>

#include "Geode/utils/string.hpp"
#include "assist/autoclicker.hpp"
#include "assist/cps.hpp"
#include "assist/hitboxes.hpp"
#include "assist/pathfinder.hpp"
#include "checkpoint/practice.hpp"
#include "config/config.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "features/death_guard.hpp"
#include "features/intentional_death.hpp"
#include "features/noclip.hpp"
#include "label/label.hpp"
#include "render/renderer.hpp"
#include "trajectory/trajectory.hpp"
#include "util/crash_log.hpp"
#include "vine/session.hpp"
#ifdef GEODE_IS_WINDOWS
#include <Zydis/Zydis.h>
#include <safetyhook.hpp>
#include "util/midhook.hpp"
#endif
#ifdef GEODE_IS_MOBILE
#include "ui/touch_overlay.hpp"
#endif

using namespace geode::prelude;

#include <Geode/modify/PlayLayer.hpp>

namespace {

#ifdef GEODE_IS_MOBILE
constexpr bool DEATH_FRAME_INPUTS_INCLUDED = true;
#else
constexpr bool DEATH_FRAME_INPUTS_INCLUDED = false;
#endif

GrapeEngine& bot() { return *GrapeEngine::get(); }

void releaseBoth(PlayLayer* layer) {
    layer->m_player1->releaseAllButtons();
    layer->m_player2->releaseAllButtons();
}

void discardCheckpoint(PlayLayer* layer, CheckpointObject* checkpoint) {
    auto* marker = checkpoint->m_physicalCheckpointObject;
    layer->removeObjectFromSection(marker);
    if (auto* glow = marker->m_glowSprite) {
        glow->release();
        glow->removeMeAndCleanup();
    }
    marker->removeMeAndCleanup();
    checkpoint->release();
}

}

struct GrapePlayLayer : Modify<GrapePlayLayer, PlayLayer> {
#ifdef GEODE_IS_MACOS
    void postUpdate(float dt) override {
        PlayLayer::postUpdate(dt);
        bot().timeline().portableFrameUpdate(this, dt);
    }
#endif

    CheckpointObject* markCheckpoint() {
        if (!bot().isEnabled()) return PlayLayer::markCheckpoint();
        CheckpointObject* checkpoint = createCheckpoint();
        storeCheckpoint(checkpoint);
        return checkpoint;
    }

    void storeCheckpoint(CheckpointObject* checkpoint) {
        if (!bot().isEnabled()) return PlayLayer::storeCheckpoint(checkpoint);

        checkpoint->retain();
        addToSection(checkpoint->m_physicalCheckpointObject);
        auto& keeper = bot().keeper();
        if (m_activatedCheckpoint)
            keeper.m_platformer.push(checkpoint, m_activatedCheckpoint);
        else
            keeper.remember(checkpoint, bot().timeline().m_frameOnLastAttempt);
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        bot().hitboxes().clearDeath();
        if (!bot().isEnabled()) return PlayLayer::loadFromCheckpoint(checkpoint);
        bot().keeper().loadInto(checkpoint, [this](CheckpointObject* target) {
            PlayLayer::loadFromCheckpoint(target);
        });
    }

    void removeCheckpoint(bool first) {
        if (!bot().isEnabled()) return PlayLayer::removeCheckpoint(first);
        auto& keeper = bot().keeper();
        if (keeper.m_saved.empty()) return;
        discardCheckpoint(this, keeper.m_saved.back().m_respawn);
        keeper.forgetLatest();
    }

    void removeAllCheckpoints() override {
        if (!bot().isEnabled()) return PlayLayer::removeAllCheckpoints();
        if (grape::intentional::keepsCheckpoints()) return;
        while (!bot().keeper().m_saved.empty()) removeCheckpoint(false);
    }

    void delayedResetLevel() {
        const bool pathfinderOwnsDeath = bot().pathfinder().m_enabled->inner() &&
                                         m_player1 && !m_player1->m_isDead;
        if (pathfinderOwnsDeath) return;
        grape::intentional::noteDeath();
        PlayLayer::delayedResetLevel();
    }

    void fullReset() {
        if (!bot().isEnabled()) return PlayLayer::fullReset();
        if (grape::intentional::takeOverFullReset(this)) return;
        PlayLayer::fullReset();
        bot().keeper().unstackPlatformer(true);
        bot().timeline().m_frameOnLastAttempt = 0;
    }

    void resetLevel() override {
        grape::noclip::stopFlash(this);
        bot().hitboxes().clearDeath();
        if (!bot().isEnabled()) {
            releaseBoth(this);
            return PlayLayer::resetLevel();
        }

        auto& timeline = bot().timeline();
        auto& director = bot().replay();

        const bool replayingFinishedRun = (m_hasCompletedLevel || m_levelEndAnimationStarted) &&
                                          !bot().isRecording() && !director.m_inputs.m_events.empty();
        if (replayingFinishedRun) director.setMode(grape::vine::RunState::Playback);

        m_practiceMusicSync = true;
        timeline.m_tpsOverflow = 0.0;
        timeline.m_respawnTimer = 2;
        timeline.incrementFrame();
        const uint64_t deathFrame = timeline.getFrame();

        director.consumeScriptedDeath(deathFrame, DEATH_FRAME_INPUTS_INCLUDED);
        bot().pathfinder().onDeath(this, deathFrame);
        const bool respawnQueued = queueRespawn(deathFrame);

        timeline.resetFrame();
        bot().hitboxes().clearTrail();
        if (!respawnQueued && !bot().keeper().m_backstep) bot().cps().clearActions();
        director.beginAttempt(this);

        PlayLayer::resetLevel();

        if (respawnQueued) settleQueuedRespawn();
        finishRespawn(deathFrame);
    }

    void updateAttempts() {
        if (!bot().keeper().m_backstep) PlayLayer::updateAttempts();
    }

    void onQuit() {
        crash_log::breadcrumb("Leaving level");
#ifdef GEODE_IS_MOBILE
        if (auto* overlay = TouchOverlay::get()) overlay->hide();
#endif
        if (Renderer::get()->isRecording()) Renderer::get()->signalStop();

        auto& timeline = bot().timeline();
        timeline.setPaused(false);
        timeline.m_stepOnce->inner() = false;
        bot().trajectory().uninit();
        bot().hitboxes().destroy();

        PlayLayer::onQuit();

        bot().keeper().wipeHistory();
        bot().keeper().wipe();
        bot().replay().onLevelExit();
    }

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (level)
            crash_log::breadcrumb(fmt::format("Entering level \"{}\" (id {})",
                                              std::string(level->m_levelName),
                                              level->m_levelID.value()));
        if (!bot().isEnabled()) return PlayLayer::init(level, dontCreateObjects);

        bot().keeper().wipeHistory();
        if (bot().trajectory().exists()) bot().trajectory().uninit();
        bot().keeper().unstackPlatformer(false);

        if (Renderer::get()->m_shouldStart) {
            auto* audio = FMODAudioEngine::sharedEngine();
            audio->stopAllMusic(true);
            audio->stopAllEffects();
        }

        if (!PlayLayer::init(level, dontCreateObjects)) return false;

        
        
        
        auto& replay = GrapeEngine::get()->replay();
        if (replay.isPlaying()) {
            replay.applyInputMode(this);
        } else if (replay.isRecording()) {
            replay.captureInputMode(this);
        }

        bot().trajectory().init(this);
        bot().hitboxes().init(this);
        bot().hitboxes().clearTrail();

        auto& timeline = bot().timeline();
        timeline.m_frameOnLastAttempt = 0;
        timeline.m_lastTfp = 0.0f;
        timeline.m_stepOnce->inner() = false;
        bot().labels().m_requiresRefresh = true;

        auto* settings = GrapeSettings::get();
        if (settings->autoMacroName)
            bot().replay().m_replayName = geode::utils::string::replace(
                settings->macroNameTemplate, "%name%", level->m_levelName);
        return true;
    }

    void destroyPlayer(PlayerObject* player, GameObject* cause) override {
        grape::death::handle(this, player, cause,
                             [&] { PlayLayer::destroyPlayer(player, cause); });
    }

    void pauseGame(bool unfocused) {
        m_gameState.m_pauseCounter = 0;
        PlayLayer::pauseGame(unfocused);
    }

    void updateVisibility(float dt) override {
        if (bot().timeline().m_extrapolateFrames->inner())
            dt = CCDirector::get()->getDeltaTime();
#ifdef GEODE_IS_MOBILE
        if (auto* overlay = TouchOverlay::get()) overlay->updateVisibility();
#endif
        PlayLayer::updateVisibility(dt);
        bot().hitboxes().draw(this);
    }

    void levelComplete() {
        PlayLayer::levelComplete();

        auto& director = bot().replay();
#ifdef GEODE_IS_MOBILE
        if (bot().isRecording()) director.setMode(grape::vine::RunState::Idle);
#endif
        if (!director.m_autosaveAtLevelEnd->inner()) return;
        if (director.m_replayName.empty()) director.m_replayName = "autosave";
        const auto path = director.getCurrentPath();
        if (std::filesystem::exists(path))
            director.createBackup();
        else
            director.save(path, true);
    }

    void setupHasCompleted() {
        if (bot().isEnabled()) Renderer::get()->startIfQueued();
        PlayLayer::setupHasCompleted();
    }

   private:
    bool queueRespawn(uint64_t deathFrame) {
        auto& keeper = bot().keeper();
        auto& timeline = bot().timeline();

        CheckpointObject* respawn = nullptr;
        if (grape::intentional::respawnsFromStart()) {
            timeline.m_frameOnLastAttempt = deathFrame + 1;
            if (keeper.m_platformer.empty()) return false;
            respawn = keeper.m_platformer.latestRespawn();
            keeper.m_shouldLoadPlatformer = true;
        } else {
            respawn = keeper.respawnPoint();
            if (!respawn) {
                timeline.m_frameOnLastAttempt = 0;
                keeper.unstackPlatformer(true);
                return false;
            }
        }
        m_checkpointArray->addObject(respawn);
        return true;
    }

    void settleQueuedRespawn() {
        bot().keeper().m_shouldLoadPlatformer = false;
        m_checkpointArray->removeLastObject();

        auto& timeline = bot().timeline();
        timeline.m_onlyRefresh = true;
        m_extraDelta = 0.0f;
        CCScheduler::get()->update(0.0f);
        const bool hideLabels = Renderer::get()->isRecording() &&
                                !GrapeSettings::get()->renderLabelsWhileRecording;
        bot().labels().update(hideLabels);
        timeline.m_onlyRefresh = false;
    }

    void requeueHeldInputs() {
        auto& director = bot().replay();
        auto& keeper = bot().keeper();
        const bool twoPlayer = m_levelSettings->m_twoPlayerMode;

        std::array<bool, 2> jumping{
            m_uiLayer->m_p1Jumping || m_uiLayer->m_p1TouchId != -1,
            m_uiLayer->m_p2Jumping || m_uiLayer->m_p2TouchId != -1,
        };
        if (director.hasFlippedControls()) std::swap(jumping[0], jumping[1]);
        jumping[0] = jumping[0] || (jumping[1] && !twoPlayer);

        for (int slot = 0; slot < (twoPlayer ? 2 : 1); ++slot) {
            PlayerObject* player = slot == 0 ? m_player1 : m_player2;
            const bool flipped = director.playerFlipped(slot == 1);
            const auto match = [&](int button, bool wanted) {
                if (wanted != player->m_holdingButtons[button])
                    queueButton(button, wanted, flipped, 0);
            };
            match(1, jumping[slot]);
            if (!m_isPlatformer) continue;
            const auto& held = keeper.held(slot == 1);
            match(2, held.m_left);
            match(3, held.m_right);
        }
        director.m_lastInputs.clear();
    }

    void carryHeldButtons(uint64_t deathFrame, bool steppingBack) {
        auto& keeper = bot().keeper();
        keeper.trackPlatformerInputs(m_queuedButtons);
        m_queuedButtons.clear();
        if (steppingBack) return;

        if (bot().isPlaying()) {
            if (keeper.m_saved.empty()) releaseBoth(this);
            bot().replay().skipScriptedInputs(deathFrame, DEATH_FRAME_INPUTS_INCLUDED);
            grape::intentional::settle();
            processQueuedButtons(0.0, true);
            bot().timeline().m_tpsOverflow = 0.0;
            return;
        }
        if (grape::intentional::armed()) {
            releaseBoth(this);
            return;
        }
        requeueHeldInputs();
    }

    void finishRespawn(uint64_t deathFrame) {
        auto& keeper = bot().keeper();
        auto& timeline = bot().timeline();
        const bool steppingBack = keeper.m_backstep;

        if (!steppingBack) bot().replay().onRespawn(timeline.getFrame());
        bot().autoclicker().reset();
        bot().trajectory().update(this);

        carryHeldButtons(deathFrame, steppingBack);
        grape::intentional::writeResetEvent(deathFrame);
        if (!grape::intentional::armed() && !steppingBack) processQueuedButtons(0.0, true);

        grape::intentional::settle();
        timeline.m_tpsOverflow = 0.0;
        keeper.m_normalDeath = false;
        keeper.m_backstep = false;
        timeline.breakLoop();
    }
};

#if defined(GEODE_IS_WINDOWS) && GEODE_COMP_GD_VERSION == 22081
namespace {

constexpr uintptr_t QUEUE_CHECKPOINT = 0x4ce060;
constexpr uintptr_t RESET_LEVEL_SEED_SITE = 0x3b90fb;

using QueueCheckpointFn = void (*)(void*, void*);

void queueCheckpointDetour(void* self, void* payload) {
    if (!bot().isEnabled()) {
        auto* original = reinterpret_cast<QueueCheckpointFn>(geode::base::get() + QUEUE_CHECKPOINT);
        return original(self, payload);
    }
    auto* layer = PlayLayer::get();
    if (!layer || grape::snap::deathBlocksCheckpoint(layer)) return;
    bot().timeline().scheduleFrozenFunction([layer](float) {
        if (!grape::snap::deathBlocksCheckpoint(layer)) layer->markCheckpoint();
    });
}

void applyAttemptSeedAtReset(SafetyHookContext&) {
    if (bot().isEnabled()) bot().replay().applyAttemptSeed();
}

}

$execute {
    (void)Mod::get()->hook(reinterpret_cast<void*>(geode::base::get() + QUEUE_CHECKPOINT),
                           &queueCheckpointDetour, "PlayLayer::queueCheckpoint",
                           tulip::hook::TulipConvention::Default);
    util::midhook(geode::base::get() + RESET_LEVEL_SEED_SITE, "resetLevelSeed",
                  applyAttemptSeedAtReset);
}
#endif

$execute {
    bot().timeline().m_tps->handle([](double& tps) {
        if (tps <= 0.0) tps = 240.0;
        if (!GJBaseGameLayer::get()) return;

        bot().replay().recordTpsChange(bot().timeline().getFrame(), tps);
        bot().timeline().scheduleFrozenFunction([](float) {
            auto* layer = GJBaseGameLayer::get();
            if (!layer) return;
            if (auto* predictor = bot().trajectory().unsafeInner()) predictor->invalidateCache();
            bot().trajectory().update(layer);
        });
    });
}
