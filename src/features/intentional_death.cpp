#include "intentional_death.hpp"

#include <Geode/Geode.hpp>

#include "checkpoint/practice.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "vine/session.hpp"

using namespace geode::prelude;

namespace grape::intentional {

namespace {

vine::ResetTracker& tracker() { return GrapeEngine::get()->replay().resets(); }

void restartWithMacroKept(PlayLayer* layer) {
    layer->m_attempts = 0;
    layer->m_jumps = 0;
    for (auto* player : {layer->m_player1, layer->m_player2})
        player->m_totalTime = 0.0;
    auto& state = layer->m_gameState;
    state.m_levelTime = 0.0;
    state.m_totalTime = 0.0;
    state.m_bestTime = 0.0;
    layer->m_freezeStartCamera = true;
    layer->m_objectsDeactivated = true;

    GrapeEngine::get()->keeper().unstackPlatformer(true);
    layer->resetLevel();

    const float width = CCDirector::get()->getWinSize().width;
    layer->m_attemptLabel->setPosition(
        {width, width / 2.f + static_cast<float>(state.m_cameraPosition.x)});
}

}

bool armed() { return GrapeEngine::get()->timeline().m_canDie->inner(); }

bool respawnsFromStart() { return tracker().restartsCleanly(armed()); }

void noteDeath() {
    if (armed()) tracker().playerDied();
}

bool takeOverFullReset(PlayLayer* layer) {
    if (armed()) {
        tracker().playerRestartedLevel();
    } else if (tracker().fromMacro()) {
        if (auto* results = layer->getChildByID("EndLevelLayer"))
            results->removeFromParent();
    } else {
        return false;
    }
    restartWithMacroKept(layer);
    return true;
}

bool keepsCheckpoints() { return tracker().levelWasRestarted(); }

void writeResetEvent(uint64_t frame) {
    if (!armed()) return;
    auto& director = GrapeEngine::get()->replay();
    director.m_inputs.addEvent(frame, tracker().eventToRecord(),
                               director.m_rng.attemptSeed());
    director.rebindHead();
}

void settle() {
    auto& toggle = GrapeEngine::get()->timeline().m_canDie;
    if (toggle->inner()) {
        toggle->inner() = false;
        toggle->notifyChange();
    }
    tracker().forget();
}

}
