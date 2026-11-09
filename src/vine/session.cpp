#include "session.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <vector>

#include "../assist/cps.hpp"
#include "../assist/autoclicker.hpp"
#include "../assist/pathfinder.hpp"
#include "../engine/engine.hpp"
#include "../engine/timeline.hpp"
#include "../features/intentional_death.hpp"
#include "../checkpoint/practice.hpp"
#include "importer.hpp"
#include "../util/storage.hpp"
#include "../util/input_trace.hpp"
#include "../engine/tick_patch.hpp"

using namespace geode::prelude;

namespace grape::vine {

namespace {

std::filesystem::path backupPath(const std::string& name) {
    namespace time = std::chrono;

    const time::time_point timestamp =
        time::floor<time::milliseconds>(time::system_clock::now());

    return grape::paths::directory("backups") /
           fmt::format("_backup_{:%Y%m%d_%H%M%S}_{}.vine", timestamp, name);
}

}

void ReplayDirector::setMode(RunState mode) {
    const bool modeChanged = m_mode != mode;
    const bool startedRecording = mode == RunState::Recording && modeChanged;
    const bool stoppedPlayback =
        m_mode == RunState::Playback && mode != RunState::Playback;
    const bool leftActiveMode = m_mode != RunState::Idle && modeChanged;

    m_mode = mode;

    if (modeChanged) {
        GrapeEngine::get()->autoclicker().reset();
        GrapeEngine::get()->pathfinder().reset();
    }
    if (stoppedPlayback) m_forceNextInput = false;
    if (mode == RunState::Playback && modeChanged)
        applyInputMode(GJBaseGameLayer::get());

    if (startedRecording) {
        const double tps = GrapeEngine::get()->timeline().m_tps->inner();
        const double valid = std::isfinite(tps) && tps > 0.0 ? tps : 240.0;
        if (m_inputs.m_events.empty()) m_inputs.m_tps = valid;
        m_liveTps = m_inputs.m_events.empty()
                        ? valid
                        : m_inputs.tpsAt(m_inputs.lastFrame());
        captureInputMode(PlayLayer::get());
#ifdef GEODE_IS_MOBILE
        auto& bot = *GrapeEngine::get();
        auto* layer = PlayLayer::get();
        grape::trace::add(fmt::format(
            "record start: tps={} tickpatch={} clickOnSteps={} "
            "clickBetweenSteps={} maintainGravity={} mirror={} autoclicker={} "
            "pathfinder={} autoflip={} platformer={}",
            valid, grape::ticks::patched(),
            layer && layer->m_clickOnSteps, layer && layer->m_clickBetweenSteps,
            m_maintainGravity, m_mirrorInputs,
            bot.autoclicker().m_enabled->inner(),
            bot.pathfinder().m_enabled->inner(),
            bot.timeline().m_autoFlipOnDeath->inner(),
            layer && layer->m_isPlatformer));
#endif
    } else if (modeChanged && mode != RunState::Recording) {
        m_liveTps = 0.0;
    }

    if (!leftActiveMode) return;

    if (auto* layer = GJBaseGameLayer::get()) {
        if (layer->m_player1) layer->m_player1->releaseAllButtons();
        if (layer->m_player2) layer->m_player2->releaseAllButtons();
    }
}

bool ReplayDirector::recordPlayerInput(uint64_t frame, EventKind kind,
                                       bool push, bool player2,
                                       bool twoPlayer, bool platformer) {
    if (player2 && !twoPlayer) player2 = false;
    if (!m_inputs.m_events.empty() && m_inputs.m_events.back().m_frame > frame)
        return false;

    m_inputs.m_platformer = platformer;
    m_inputs.addEvent(frame, kind, push, playerFlipped(player2));
    rebindHead();

    if (kind == EventKind::Jump && push)
        GrapeEngine::get()->cps().pushAction(player2);
    return true;
}

bool ReplayDirector::recordTpsChange(uint64_t frame, double tps) {
    if (!isRecording()) return false;
    if (!std::isfinite(tps) || tps <= 0.0) return false;

    const double current =
        m_liveTps > 0.0 ? m_liveTps : m_inputs.tpsAt(frame);
    if (std::abs(current - tps) < 1e-9) return false;

    m_liveTps = tps;
    const auto now = std::chrono::steady_clock::now();

    if (m_inputs.m_events.empty()) {
        m_inputs.m_tps = tps;
        m_lastTpsEditAt = now;
        return false;
    }

    const bool stillEditing = now - m_lastTpsEditAt < TPS_EDIT_SETTLE;
    m_inputs.setTps(frame, tps, stillEditing);
    m_lastTpsEditAt = now;
    rebindHead();
    return true;
}

void ReplayDirector::applyTpsForFrame(uint64_t frame) {
    if (m_inputs.m_events.empty()) return;

    const double wanted = m_inputs.tpsAt(frame);
    if (!std::isfinite(wanted) || wanted <= 0.0) return;

    m_liveTps = wanted;

    auto& updater = GrapeEngine::get()->timeline();
    if (std::abs(updater.m_tps->inner() - wanted) < 1e-9) return;

    updater.m_tps->inner() = wanted;
    updater.m_tps->notifyChange();
}

void ReplayDirector::beginMacroReset() {
    m_resets.macroAskedForReset();
    m_rng.setAttemptSeed(readLiveRandom());
}

bool ReplayDirector::consumeScriptedDeath(uint64_t deathFrame,
                                          bool inclusive) {
    const Event* event = m_head.peek();
    if (!event || event->m_type != EventKind::Death) return false;

    const bool isDue = inclusive ? event->m_frame <= deathFrame
                                 : event->m_frame == deathFrame;
    if (!isDue) return false;

    m_head.advance();
    m_resets.macroAskedForReset();
    return true;
}

void ReplayDirector::skipScriptedInputs(uint64_t deathFrame, bool inclusive) {
    if (m_resets.fromMacro()) m_head.skipAt(deathFrame, inclusive);
}

void ReplayDirector::onRespawn(uint32_t frame) {
    m_lastInputs.clear();
    m_forceNextInput = false;

    rebindHead();
    if (isRecording()) {
        m_inputs.clipFrom(frame);
        m_head.setPosition(m_inputs.length());
    } else {
        m_head.seek(frame);
        if (m_resets.fromMacro()) m_head.skipResetsAt(frame);
    }

    applyTpsForFrame(frame);
}

void ReplayDirector::beginAttempt(PlayLayer* layer) {
    auto& bot = *GrapeEngine::get();
    auto& fix = bot.keeper();

    if (!m_resets.fromMacro()) m_rng.setAttemptSeed(m_rng.baseSeed());

    if (isRecording()) {
        captureInputMode(layer);
        if (m_seedOverride) writeLiveRandom(m_overrideSeedValue);
        m_rng.setAttemptSeed(readLiveRandom());

        const bool freshAttempt = fix.m_saved.empty() &&
                                  !fix.m_pendingRestore &&
                                  !intentional::armed();
        if (freshAttempt) {
            const uint64_t advanced = RngCore::lcg(readLiveRandom());
            writeLiveRandom(advanced);
            m_rng.setBaseSeed(advanced);
            m_rng.setAttemptSeed(advanced);
            RngCore::applyVariance(layer->m_varianceValues, advanced);
        }
    } else {
        if (isPlaying()) applyInputMode(layer);
        writeLiveRandom(m_rng.attemptSeed());
        RngCore::applyVariance(layer->m_varianceValues, m_rng.attemptSeed());
    }
}

void ReplayDirector::applyAttemptSeed() {
    const uint64_t seed = m_rng.attemptSeed();
    m_rng.beginAttempt(seed);
    writeLiveRandom(m_rng.main());
    GrapeEngine::get()->keeper().reseedRandom(seed);
}

void ReplayDirector::onLevelExit() {
    m_head.setPosition(0);
    m_lastInputs.clear();
    m_forceNextInput = false;
}

void ReplayDirector::activate(Inputs&& inputs, double tps, uint64_t seed) {
    m_inputs = std::move(inputs);
    m_inputs.sort();
    rebindHead();
    applyInputMode(GJBaseGameLayer::get());

    m_rng.setBaseSeed(seed);
    m_rng.setAttemptSeed(seed);
#ifndef GEODE_IS_WINDOWS
    m_rng.setMain(seed);
#endif
    m_lastInputs.clear();

    auto& updater = GrapeEngine::get()->timeline();
    updater.resetFrame();
    updater.m_frameOnLastAttempt = 0;
    updater.m_tpsOverflow = 0.0;
    updater.m_tps->inner() = std::isfinite(tps) && tps > 0.0 ? tps : 240.0;
    updater.m_tps->notifyChange();

    m_resets.forget();
    setMode(RunState::Idle);
}

bool ReplayDirector::hasFlippedControls() const {
    return GameManager::get()->getGameVariable("0010");
}

bool ReplayDirector::playerFlipped(bool player) const {
    return player ^ hasFlippedControls();
}

std::pair<bool, bool> ReplayDirector::resolveMirror(bool p1Jumping,
                                                    bool p2Jumping) const {
    if (!m_mirrorInputs) return {p1Jumping, p2Jumping};
    const bool mirroredP1 = p1Jumping | (p2Jumping ^ m_mirrorInverted);
    return {mirroredP1, p2Jumping | (mirroredP1 ^ m_mirrorInverted)};
}

void ReplayDirector::captureInputMode(GJBaseGameLayer* layer) {
    if (!isRecording()) return;

    auto* cbf =
        Loader::get()->getInstalledMod("syzzi.click_between_frames");
    if (cbf) {
        m_inputs.m_cbf = !cbf->getSettingValue<bool>("soft-toggle");
        m_inputs.m_cbfOnSteps = cbf->getSettingValue<bool>("click-on-steps");
    } else {
        m_inputs.m_cbf = false;
        m_inputs.m_cbfOnSteps = false;
    }

    if (layer) {
        m_inputs.m_clickBetweenSteps = layer->m_clickBetweenSteps;
        m_inputs.m_clickOnSteps = layer->m_clickOnSteps;
    }

    
    if (m_inputs.m_cbf) {
        m_inputs.m_clickBetweenSteps = false;
        m_inputs.m_clickOnSteps = false;
    }
}

void ReplayDirector::applyInputMode(GJBaseGameLayer* layer) {
    auto* cbf =
        Loader::get()->getInstalledMod("syzzi.click_between_frames");
    if (cbf) {
        cbf->setSettingValue("physics-bypass", false);
        cbf->setSettingValue("soft-toggle", !m_inputs.m_cbf);
        cbf->setSettingValue("click-on-steps", m_inputs.m_cbfOnSteps);
    }

    if (!layer) return;
    layer->m_clickBetweenSteps =
        m_inputs.m_cbf ? false : m_inputs.m_clickBetweenSteps;
    layer->m_clickOnSteps =
        m_inputs.m_cbf ? false : m_inputs.m_clickOnSteps;
}

void ReplayDirector::save(std::filesystem::path path, bool noOverwrite) {
    m_lastOperationSucceeded = false;
    if (noOverwrite && std::filesystem::exists(path)) return;

    m_inputs.sort();
    m_inputs.m_seed = m_rng.baseSeed();
    if (!m_inputs.hasTpsEvents()) {
        const double tps = GrapeEngine::get()->timeline().m_tps->inner();
        m_inputs.m_tps = std::isfinite(tps) && tps > 0.0 ? tps : 240.0;
    }

    m_lastOperationSucceeded = storeInputs(path, m_inputs);
}

void ReplayDirector::load(std::filesystem::path path) {
    setMode(RunState::Idle);
    onLevelExit();
    m_lastOperationSucceeded = false;
    if (!std::filesystem::exists(path) && path.extension() == ".grape") {
        auto legacyPath = path;
        legacyPath.replace_extension(".slc");
        if (std::filesystem::exists(legacyPath)) path = legacyPath;
    }
    if (!std::filesystem::exists(path)) {
        geode::log::error(
            "Failed to load replay from {}; file does not exist", path);
        return;
    }

    auto foreign = loadForeign(path);
    if (foreign.isErr()) {
        geode::log::error("Failed to load replay from {}", path);
        return;
    }

    auto data = std::move(foreign).unwrap();
    activate(std::move(data.m_inputs), data.m_tps, data.m_seed);
    m_lastOperationSucceeded = true;
    m_replayName = path.stem().string();
}

geode::Result<size_t> ReplayDirector::loadSupported(
    std::filesystem::path path) {
    setMode(RunState::Idle);
    onLevelExit();

    auto foreign = loadForeign(path);
    if (foreign.isErr()) return geode::Err(foreign.unwrapErr());

    auto data = std::move(foreign).unwrap();
    m_lastImportFormat = data.m_format;
    const uint64_t seed = data.m_seeded ? data.m_seed : m_rng.baseSeed();
    activate(std::move(data.m_inputs), data.m_tps, seed);
    return geode::Ok(m_inputs.length());
}

geode::Result<size_t> ReplayDirector::convertAndPlay(
    std::filesystem::path path) {
    auto loaded = loadSupported(path);
    if (loaded.isErr()) return loaded;

    auto name = path.stem();
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (extension == ".json" && name.extension() == ".gdr")
        name = name.stem();
    m_replayName = name.string();

    const auto output = getCurrentPath();
    backupExisting(output);
    save(output);
    if (!m_lastOperationSucceeded)
        return geode::Err("Converted macro could not be saved");

    m_head.setPosition(0);
    setMode(RunState::Playback);
    if (auto* playLayer = PlayLayer::get();
        playLayer && !playLayer->m_hasCompletedLevel &&
        !playLayer->m_levelEndAnimationStarted)
        playLayer->resetLevel();
    return loaded;
}

void ReplayDirector::merge(std::filesystem::path path) {
    if (!std::filesystem::exists(path)) {
        geode::log::error("Merge failed: file does not exist at {}", path);
        return;
    }

    auto foreign = loadForeign(path);
    if (foreign.isErr()) {
        geode::log::error("Merge failed: could not parse replay");
        return;
    }
    auto other = std::move(foreign).unwrap().m_inputs;

    const auto hasPlayer = [](const auto& events, bool player2) {
        return std::ranges::any_of(events, [player2](const auto& event) {
            return event.isPlayer() && event.m_player2 == player2;
        });
    };
    const bool currentP1 = hasPlayer(m_inputs.m_events, false);
    const bool currentP2 = hasPlayer(m_inputs.m_events, true);
    const bool otherP1 = hasPlayer(other.m_events, false);
    const bool otherP2 = hasPlayer(other.m_events, true);
    const bool targetP2 = currentP1 && !currentP2;
    const bool targetP1 = currentP2 && !currentP1;

    std::vector<Event> merged = m_inputs.m_events;
    for (auto event : other.m_events) {
        if (event.isPlayer() && targetP2 && otherP1 && !otherP2)
            event.m_player2 = true;
        if (event.isPlayer() && targetP1 && otherP2 && !otherP1)
            event.m_player2 = false;
        merged.push_back(event);
    }

    m_inputs.m_events = std::move(merged);
    m_inputs.sort();
    rebindHead();

    geode::log::info("Macro merge complete. Total inputs after merge: {}",
                     m_inputs.m_events.size());
}

std::filesystem::path ReplayDirector::getCurrentPath() {
    return grape::paths::directory("replays") / (m_replayName + ".vine");
}

void ReplayDirector::backupExisting(std::filesystem::path path) {
    if (!std::filesystem::exists(path)) return;

    const std::string name = path.stem().string();
    std::filesystem::copy(path, backupPath(name),
                          std::filesystem::copy_options::skip_existing);
}

void ReplayDirector::createBackup() { save(backupPath(m_replayName), true); }

geode::utils::file::FilePickOptions ReplayDirector::converterFileOptions() {
    return foreignPickOptions();
}

uint64_t ReplayDirector::readLiveRandom() const {
#if defined(GEODE_IS_WINDOWS) && GEODE_COMP_GD_VERSION == 22081
    return *reinterpret_cast<const uint64_t*>(geode::base::get() + 0x6c2e90);
#else
    
    
    return m_rng.main();
#endif
}

void ReplayDirector::writeLiveRandom(uint64_t state) {
    m_rng.setMain(state);
#if defined(GEODE_IS_WINDOWS) && GEODE_COMP_GD_VERSION == 22081
    *reinterpret_cast<uint64_t*>(geode::base::get() + 0x6c2e90) = state;
#endif
}

}

$execute {
    auto bot = GrapeEngine::get();
    auto& director = bot->replay();

    director.m_autosaveId = bot->ticker().every(
        [&director]() {
            if (!PlayLayer::get()) return;
            if (!director.m_autosaveAtInterval->inner()) return;
            if (director.isRecording()) director.createBackup();
        },
        director.m_autosaveInterval->inner());

    director.m_autosaveInterval->handle(
        [bot, &director](double& interval) {
            bot->ticker().retune(director.m_autosaveId, interval);
        });
}
