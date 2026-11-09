#ifndef FRAME_ENGINE_HPP
#define FRAME_ENGINE_HPP

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

#include <algorithm>
#include <forward_list>
#include <functional>
#include <utility>

#include "config/config.hpp"
#include "shared/value/value.hpp"

class FrameEngine {
    template <class T>
    static ConfigValuePtr<T> option(const char* key, T* backing) {
        return ConfigValue<T>::create(key, backing);
    }

    static GrapeSettings& saved() { return *GrapeSettings::get(); }

    uint32_t m_frame = 0;

    struct Toggles {
        bool m_stepOnce = false;
        bool m_stepBack = false;
        bool m_frameAdvance = false;
        bool m_intentionalDeath = false;
        bool m_noMirror = false;
        bool m_preventDeath = false;
        bool m_flipOnDeath = false;
        bool m_predictBestPath = false;
        bool m_extrapolate = false;
        bool m_layoutMode = false;
        bool m_noclip = false;
    } m_toggles;

    std::forward_list<std::function<void(float)>> m_frozenScheduledFunctions;

   public:
    enum LockDeltaMode { Performance, Accuracy };
    enum class NoclipType { Player1, Player2, Both };

    static float playerSpeedUnits(float speed);

    float getSSB();
    float getTimeWarp();
    double getTps() const { return m_tps->inner(); }
    double getPhysicsDt() { return 1. / m_tps->inner(); }
    void setTps(double tps) {
        if (tps > 0.0) m_tps->inner() = tps;
    }

    [[nodiscard]] uint32_t getFrame();
    [[nodiscard]] uint32_t getDisplayFrame();
    void incrementFrame(uint32_t count = 1) { m_frame += count; }
    void resetFrame() { setFrame(0); }
    void setFrame(uint32_t frame) {
        m_frame = frame;
        m_portableFrameRemainder = 0.0;
    }

    void calculateSteps(float dt, float targetDt);
    int visualCeiling(int base);
    bool isLockDelta();
    bool useFastLockDelta();
    void breakLoop();

    void runUpdates(std::function<void(float)> update, float realDt, bool frozen);
    void runFrozenTick();
    void runMobileFrame(std::function<void(float)> update, float realDt);
    void portableFrameUpdate(PlayLayer* playLayer, float visualDt);
    void portableStepUpdate(PlayLayer* playLayer);
    void scheduleFrozenFunction(std::function<void(float)> task) {
        m_frozenScheduledFunctions.push_front(std::move(task));
    }

    bool isPaused() { return m_paused->inner(); }
    void setPaused(bool paused) { m_paused->inner() = paused; }
    void stepOnce() { m_stepOnce->inner() = true; }
    bool consumeStep() { return std::exchange(m_stepOnce->inner(), false); }
    void backwardsStep(int n = 1);
    void findBestFrameCandidate();

    void updateAudioSpeedhack();

    uint64_t m_frameOnLastAttempt = 0;
    uint32_t m_editorStartProgress = 0;
    int m_respawnTimer = 0;
    bool m_onlyRefresh = false;
    bool m_predicting = false;
    bool m_shouldRender = true;
    double m_timewarpTime = 0.0;
    double m_tpsOverflow = 0.0;
    double m_portableFrameRemainder = 0.0;
    float m_lastTfp = 0.0f;

    int totalStepCount = 0;
    int estimatedStepCount = 1;
    uint32_t m_stepLimit = 1;
    
    
    int m_forcedTicks = 1;

    NoclipType m_noclipType =
        static_cast<NoclipType>(std::clamp(GrapeSettings::get()->noclipPlayer, 0, 2));

    ConfigValuePtr<double> m_tps = option("updater.tps", &saved().tps);
    ConfigValuePtr<double> m_speedhack = option("updater.speedhack", &saved().speed);
    ConfigValuePtr<bool> m_speedhackAudio = option("updater.speedhack_audio", &saved().speedhackAudio);
    ConfigValuePtr<bool> m_realTime = option("updater.real_time", &saved().realTime);
    ConfigValuePtr<uint32_t> m_maxUPR = option("updater.max_upr", &saved().maxUpr);
    ConfigValuePtr<bool> m_dynamicUpr = option("updater.dynamic_upr", &saved().dynamicUpr);
    ConfigValuePtr<double> m_fpsTarget = option("updater.fps_target", &saved().fpsTarget);
    ConfigValuePtr<bool> m_useVisualUpdates = option("updater.visual_updates", &saved().useVisualUpdates);
    ConfigValuePtr<bool> m_lockDelta = option("updater.lock_delta", &saved().lockDelta);
    ConfigValuePtr<int> m_lockDeltaMode = option("updater.lock_delta_mode", &saved().lockDeltaMode);
    ConfigValuePtr<bool> m_ssbFix = option("updater.ssb_fix", &saved().scrollSpeedBugFix);

    ConfigValuePtr<bool> m_paused = option("updater.frame_advance", &m_toggles.m_frameAdvance);
    ConfigValuePtr<bool> m_stepOnce = option("updater.advance_one", &m_toggles.m_stepOnce);
    ConfigValuePtr<bool> m_stepBackwards = option("updater.advance_back", &m_toggles.m_stepBack);
    ConfigValuePtr<bool> m_backwardsStepping =
        option("updater.backwards_stepping", &saved().backwardsStepping);
    ConfigValuePtr<bool> m_extrapolateFrames =
        option("updater.frame_extrapolation", &m_toggles.m_extrapolate);

    ConfigValuePtr<bool> m_canDie = option("updater.intentional_death", &m_toggles.m_intentionalDeath);
    ConfigValuePtr<bool> m_noclip = option("updater.noclip", &m_toggles.m_noclip);
    ConfigValuePtr<bool> m_preventDeath = option("updater.prevent_death", &m_toggles.m_preventDeath);
    ConfigValuePtr<bool> m_autoFlipOnDeath =
        option("updater.auto_flip_on_death", &m_toggles.m_flipOnDeath);
    ConfigValuePtr<bool> m_predictBestPath =
        option("updater.predict_best_path", &m_toggles.m_predictBestPath);
    ConfigValuePtr<bool> m_fullGamePrediction =
        option("updater.full_game_prediction", &saved().fullGamePrediction);
    ConfigValuePtr<float> m_acceptablePrediction =
        option("updater.acceptable_prediction", &saved().acceptablePrediction);

    ConfigValuePtr<bool> m_noMirror = option("updater.no_mirror", &m_toggles.m_noMirror);
    ConfigValuePtr<bool> m_layoutMode = option("updater.layout_mode", &m_toggles.m_layoutMode);
    ConfigValuePtr<bool> m_useRegularBg = option("updater.use_regular_bg", &saved().useRegularBg);
};

#endif
