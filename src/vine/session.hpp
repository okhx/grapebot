#ifndef GRAPE_VINE_SESSION_HPP
#define GRAPE_VINE_SESSION_HPP

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <unordered_map>

#include "../config/config.hpp"
#include "../engine/clock.hpp"
#include "format.hpp"
#include "playhead.hpp"
#include "reset_tracker.hpp"
#include "rng.hpp"

using grape::vine::EventKind;
using grape::vine::Inputs;

class PlayLayer;
class GJBaseGameLayer;

namespace grape::vine {

enum class RunState { Idle, Recording, Playback };

class ReplayDirector {
   public:
    RunState m_mode = RunState::Idle;

    Inputs m_inputs;
    Playhead m_head;
    RngCore m_rng;

    bool recordPlayerInput(uint64_t frame, EventKind kind, bool push,
                           bool player2, bool twoPlayer, bool platformer);

    bool recordTpsChange(uint64_t frame, double tps);

    
    void captureInputMode(GJBaseGameLayer* layer);
    
    void applyInputMode(GJBaseGameLayer* layer);

    void applyTpsForFrame(uint64_t frame);

    ResetTracker& resets() { return m_resets; }

    void beginMacroReset();

    bool consumeScriptedDeath(uint64_t deathFrame, bool inclusive);

    void skipScriptedInputs(uint64_t deathFrame, bool inclusive);

    void onRespawn(uint32_t frame);

    void beginAttempt(PlayLayer* layer);

    void applyAttemptSeed();

    void onLevelExit();

    void activate(Inputs&& inputs, double tps, uint64_t seed);

    double m_liveTps = 0.0;
    std::chrono::steady_clock::time_point m_lastTpsEditAt{};
    static constexpr std::chrono::milliseconds TPS_EDIT_SETTLE{600};

    bool hasFlippedControls() const;
    bool playerFlipped(bool player) const;

    std::pair<bool, bool> resolveMirror(bool p1Jumping, bool p2Jumping) const;

    std::unordered_map<int, Event> m_lastInputs;
    bool m_forceNextInput = false;

    bool m_seedOverride = false;
    uint64_t m_overrideSeedValue = 0;

    void load(std::filesystem::path path);
    void save(std::filesystem::path path, bool noOverwrite = false);
    geode::Result<size_t> loadSupported(std::filesystem::path path);
    geode::Result<size_t> convertAndPlay(std::filesystem::path path);
    std::string m_lastImportFormat;
    static geode::utils::file::FilePickOptions converterFileOptions();
    void merge(std::filesystem::path path);

    std::filesystem::path getCurrentPath();
    void backupExisting(std::filesystem::path path);
    void createBackup();

    std::string m_replayName;
    bool m_lastOperationSucceeded = false;

    ConfigValuePtr<bool> m_ignoreInputs = ConfigValue<bool>::create(
        "replay.ignore_inputs", &GrapeSettings::get()->blockInputs);
    ConfigValuePtr<bool> m_autosaveAtLevelEnd = ConfigValue<bool>::create(
        "replay.autosave_at_level_end",
        &GrapeSettings::get()->autosaveAtLevelEnd);
    GameScheduler::Ticket m_autosaveId;
    ConfigValuePtr<bool> m_autosaveAtInterval = ConfigValue<bool>::create(
        "replay.autosave_at_interval",
        &GrapeSettings::get()->autosaveAtInterval);
    ConfigValuePtr<double> m_autosaveInterval = ConfigValue<double>::create(
        "replay.autosave_interval", &GrapeSettings::get()->autosaveInterval);

    bool m_mirrorInputs = false;
    bool m_mirrorInverted = false;
    bool m_maintainGravity = false;
    ConfigValuePtr<bool> m_mirrorInputsValue = ConfigValue<bool>::create(
        "replay.mirror_inputs", &m_mirrorInputs);
    ConfigValuePtr<bool> m_mirrorInvertedValue = ConfigValue<bool>::create(
        "replay.mirror_inverted", &m_mirrorInverted);
    ConfigValuePtr<bool> m_maintainGravityValue = ConfigValue<bool>::create(
        "replay.maintain_gravity", &m_maintainGravity);
    ConfigValuePtr<bool> m_useAlternateHook = ConfigValue<bool>::create(
        "replay.althook", &GrapeSettings::get()->useAlternateHook);

    void setMode(RunState mode);
    bool isRecording() const { return m_mode == RunState::Recording; }
    bool isPlaying() const { return m_mode == RunState::Playback; }
    bool isIdle() const { return m_mode == RunState::Idle; }

    uint64_t readLiveRandom() const;
    void writeLiveRandom(uint64_t state);

    void rebindHead() { m_head.attach(m_inputs.m_events); }

   private:
    ResetTracker m_resets;
};

}

#endif
