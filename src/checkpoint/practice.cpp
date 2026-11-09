#include "practice.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/UILayer.hpp>

#include <algorithm>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "vine/session.hpp"

using namespace geode::prelude;

namespace grape::snap {

namespace {

constexpr uint64_t RANDOM_TRIGGER_SPREAD = 2137;
constexpr uint32_t MAX_HISTORY = 100'000;

void deactivateMarker(CheckpointGameObject* marker) {
    marker->m_checkpointActivated = false;
    marker->resetCheckpoint();
}

void hideBroken(const std::vector<GameObject*>& objects) {
    for (GameObject* object : objects) {
        if (!object) continue;
        object->m_isDisabled = true;
        object->m_isDisabled2 = true;
        object->setOpacity(0.0f);
    }
}

}

bool deathBlocksCheckpoint(PlayLayer* layer) {
    if (!layer) return false;
    const auto dead = [](PlayerObject* player) { return player && player->m_isDead; };
    return layer->m_playerDied || dead(layer->m_player1) || dead(layer->m_player2);
}

#ifdef GEODE_IS_MOBILE
struct GrapeUILayer : Modify<GrapeUILayer, UILayer> {
    void onCheck(CCObject* sender) {
        if (GrapeEngine::get()->isEnabled() && deathBlocksCheckpoint(PlayLayer::get()))
            return;
        UILayer::onCheck(sender);
    }
};
#endif

void ReleaseRespawn::operator()(FrameState& state) const {
    if (state.m_respawn) state.m_respawn->release();
}

FrameState PracticeKeeper::snapshot(CheckpointObject* respawn, uint64_t attemptStart) {
    auto* layer = PlayLayer::get();
    if (!layer) return {};

    auto& timeline = GrapeEngine::get()->timeline();
    auto& director = GrapeEngine::get()->replay();

    FrameState state;
    state.m_players[0].readFrom(layer->m_player1);
    state.m_players[1].readFrom(layer->m_player2);
    state.m_respawn = respawn;
    state.m_platformerDepth = m_platformer.depth();
    state.m_clock = {
        .m_attemptStart = attemptStart,
        .m_frame = timeline.getFrame(),
        .m_inputCursor = director.m_head.position(),
        .m_tps = timeline.m_tps->inner(),
        .m_timewarp = timeline.m_timewarpTime,
    };
    state.m_rng = {director.readLiveRandom(), director.m_rng.shake(),
                   director.m_rng.teleport()};
    state.m_world.m_persistentItems = layer->m_effectManager->m_persistentItemCountMap;
    state.m_world.m_variance = layer->m_varianceValues;
    state.m_world.m_nonEffectObjects = layer->m_calcNonEffectObjects;
    state.m_world.m_nonEffectCount = layer->m_calcNonEffectObjectsSize;
    state.m_brokenObjects = m_brokenObjects;
    state.m_randomTriggerStates.reserve(m_randomTriggers.size());
    for (const RandomTrigger& trigger : m_randomTriggers)
        state.m_randomTriggerStates.push_back(*trigger.m_state);
    return state;
}

void PracticeKeeper::restore(FrameState& state) {
    auto* layer = PlayLayer::get();
    if (!layer) return;

    state.m_players[0].writeTo(layer->m_player1);
    state.m_players[1].writeTo(layer->m_player2);

    layer->m_effectManager->m_persistentItemCountMap = state.m_world.m_persistentItems;
    layer->m_varianceValues = state.m_world.m_variance;
    layer->m_calcNonEffectObjects = state.m_world.m_nonEffectObjects;
    layer->m_calcNonEffectObjectsSize = state.m_world.m_nonEffectCount;

    m_platformer.rewindTo(state.m_platformerDepth, deactivateMarker);

    auto& timeline = GrapeEngine::get()->timeline();
    auto& director = GrapeEngine::get()->replay();
    const auto& clock = state.m_clock;
    timeline.m_frameOnLastAttempt = clock.m_attemptStart;
    timeline.m_timewarpTime = clock.m_timewarp;
    timeline.setFrame(clock.m_frame - clock.m_attemptStart);
    director.rebindHead();
    director.m_head.setPosition(std::min(clock.m_inputCursor, director.m_inputs.m_events.size()));
    director.m_rng.restore(state.m_rng);
    director.writeLiveRandom(state.m_rng.m_main);
    if (clock.m_tps != timeline.m_tps->inner()) {
        timeline.m_tps->inner() = clock.m_tps;
        timeline.m_tps->notifyChange();
    }

    m_brokenObjects = state.m_brokenObjects;
    hideBroken(m_brokenObjects);

    const std::size_t known = std::min(state.m_randomTriggerStates.size(), m_randomTriggers.size());
    for (std::size_t i = 0; i < known; ++i)
        *m_randomTriggers[i].m_state = state.m_randomTriggerStates[i];
}

std::size_t PracticeKeeper::historyCapacity() {
    return std::max<std::size_t>(m_historyLimit->inner(), m_minimumHistory);
}

void PracticeKeeper::stash(CheckpointObject* respawn, uint64_t attemptStart) {
    m_history.push(snapshot(respawn, attemptStart), historyCapacity());
}

bool PracticeKeeper::canStepBack() {
    return PlayLayer::get() && m_history.size() >= 2;
}

void PracticeKeeper::stepBack(const LoadCheckpoint& load) {
    if (m_history.size() < 2) return;
    if (!m_normalDeath) m_history.dropNewest();
    FrameState& target = m_history.newest();
    load(target.m_respawn);
    restore(target);
}

void PracticeKeeper::resetTo(const FrameState& state) {
    m_forcedState = const_cast<FrameState*>(&state);
    PlayLayer::get()->resetLevel();
    m_forcedState = nullptr;
}

void PracticeKeeper::remember(CheckpointObject* respawn, uint64_t attemptStart) {
    m_saved.push_back(snapshot(respawn, attemptStart));
}

void PracticeKeeper::forgetLatest() {
    if (!m_saved.empty()) m_saved.pop_back();
}

void PracticeKeeper::restoreLatest() {
    if (m_saved.empty()) {
        unstackPlatformer(true);
        return;
    }
    restore(m_saved.back());
}

void PracticeKeeper::wipe() {
    m_brokenObjects.clear();
    m_randomTriggers.clear();
    m_saved.clear();
}

void PracticeKeeper::loadInto(CheckpointObject* requested, const LoadCheckpoint& load) {
    if (m_forcedState) {
        load(m_forcedState->m_respawn);
        restore(*m_forcedState);
        return;
    }
    if (m_pendingRestore) {
        stepBack(load);
        m_normalDeath = false;
        return;
    }
    if (m_saved.empty()) {
        load(requested);
        return;
    }
    if (m_shouldLoadPlatformer) {
        load(m_platformer.latestRespawn());
        return;
    }
    CheckpointObject* latest = m_saved.back().m_respawn;
    wipeHistory();
    load(latest);
    restoreLatest();
}

CheckpointObject* PracticeKeeper::respawnPoint() {
    if (!m_saved.empty()) return m_saved.back().m_respawn;
    if (m_forcedState) return m_forcedState->m_respawn;
    if (m_pendingRestore && !m_history.empty()) return m_history.newest().m_respawn;
    return nullptr;
}

void PracticeKeeper::unstackPlatformer(bool markersWereLoaded) {
    m_platformer.abandon(markersWereLoaded, deactivateMarker);
}

void PracticeKeeper::reseedRandom(uint64_t attemptSeed) {
    for (const RandomTrigger& trigger : m_randomTriggers)
        *trigger.m_state =
            attemptSeed ^ (static_cast<uint64_t>(trigger.m_uniqueId) * RANDOM_TRIGGER_SPREAD);
}

void PracticeKeeper::trackRandomTrigger(uint64_t* state, int uniqueId) {
    m_randomTriggers.push_back({state, uniqueId});
}

void PracticeKeeper::trackPlatformerInputs(std::vector<PlayerButtonCommand>& inputs) {
    for (const auto& input : inputs) {
        HeldDirections& held = m_held[input.m_isPlayer2 ? 1 : 0];
        if (input.m_button == PlayerButton::Left) held.m_left = input.m_isPush;
        else if (input.m_button == PlayerButton::Right) held.m_right = input.m_isPush;
    }
}

}

$execute {
    GrapeEngine::get()->keeper().m_historyLimit->handle([](uint32_t& frames) {
        if (frames == 0 || frames > grape::snap::MAX_HISTORY) frames = 1;
        GrapeEngine::get()->keeper().m_history.shrinkTo(frames);
    });
}
