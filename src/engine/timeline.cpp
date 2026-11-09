#include "timeline.hpp"

#ifdef GEODE_IS_WINDOWS
#include <Zydis/Zydis.h>
#include <safetyhook.hpp>
#endif

#include <Geode/Geode.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <chrono>

#include "Geode/cocos/CCScheduler.h"
#include "assist/autoclicker.hpp"
#include "assist/hitboxes.hpp"
#include "assist/pathfinder.hpp"
#include "checkpoint/practice.hpp"
#include "engine.hpp"
#include "features/death_guard.hpp"
#include "label/label.hpp"
#include "render/renderer.hpp"
#include "step_budget.hpp"
#include "tick_patch.hpp"
#include "tick_plan.hpp"
#include "trajectory/trajectory.hpp"
#include "vine/session.hpp"
#ifdef GEODE_IS_WINDOWS
#include "util/midhook.hpp"
#endif

using namespace geode::prelude;

namespace {

using Update = std::function<void(float)>;
using Clock = std::chrono::high_resolution_clock;

constexpr uint32_t MAX_FRAMES_PER_DRAW = 1000;
constexpr double ROUND_UP_FROM = 0.99999;

constexpr uint32_t takeWholeFrames(double gained, double& carry) {
    const double total = gained + carry;
    if (total >= MAX_FRAMES_PER_DRAW) {
        carry = 0.0;
        return MAX_FRAMES_PER_DRAW;
    }
    uint32_t whole = static_cast<uint32_t>(total);
    carry = total - whole;
    if (carry > ROUND_UP_FROM) {
        carry = 0.0;
        ++whole;
    }
    return whole;
}

static_assert([] {
    double carry = 0.0;
    const bool first = takeWholeFrames(0.25, carry) == 0;
    const bool second = takeWholeFrames(0.25, carry) == 0;
    const bool third = takeWholeFrames(0.50, carry) == 1;
    return first && second && third && carry == 0.0;
}());

GrapeEngine& bot() { return *GrapeEngine::get(); }

bool wantsFrameHistory() {
    auto& timeline = bot().timeline();
    return (timeline.m_backwardsStepping->inner() || bot().pathfinder().wantsHistory()) &&
           !Renderer::get()->isRecording();
}

void keepHistoryFrame(PlayLayer* layer) {
    CheckpointObject* checkpoint = layer->createCheckpoint();
    if (!checkpoint) return;
    checkpoint->retain();
    bot().keeper().stash(checkpoint, bot().timeline().m_frameOnLastAttempt);
}

float scaledRealDt(float realDt) {
    auto& timeline = bot().timeline();
    return realDt * timeline.getTimeWarp() * timeline.m_speedhack->inner();
}

void runToNextInputs(float realDt, const Update& update) {
    auto& timeline = bot().timeline();
    auto& director = bot().replay();

    timeline.calculateSteps(scaledRealDt(realDt), timeline.getPhysicsDt());
    const double tick = timeline.getPhysicsDt();

    if (!director.m_head.peek()) {
        if (timeline.estimatedStepCount >= 1) update(tick * timeline.estimatedStepCount);
        return;
    }

    const auto runBatch = [&](int count) {
        timeline.estimatedStepCount = count;
        
        
        
        timeline.m_forcedTicks = std::max(1, count);
        update(static_cast<float>(tick * count));
        timeline.m_forcedTicks = 1;
    };
    for (int left = timeline.totalStepCount; left > 0;) {
        const auto* next = director.m_head.peek();
        const uint64_t frame = timeline.getFrame();
        uint64_t untilInput = static_cast<uint64_t>(left);
        if (next) untilInput = next->m_frame <= frame ? 0 : next->m_frame - frame;
        const int run = static_cast<int>(std::min<uint64_t>(untilInput, left));
        if (run > 0) {
            runBatch(run);
            left -= run;
        }
        if (left > 0) {
            runBatch(1);
            --left;
        }
    }
}

float distanceTicked(GJBaseGameLayer* level) {
    auto& timeline = bot().timeline();
    const float now = level->timeForPos(level->m_player1->m_position, 0,
                                        level->m_gameState.m_currentChannel, true, 0);
    const float moved = std::abs(now - timeline.m_lastTfp);
    timeline.m_lastTfp = now;
    return moved <= 0 ? static_cast<float>(timeline.getPhysicsDt()) : moved;
}

void runPaced(float realDt, const Update& update, bool followScroll) {
    GJBaseGameLayer* level = PlayLayer::get();
    if (!level) level = LevelEditorLayer::get();

    auto& timeline = bot().timeline();
    const float tick = static_cast<float>(timeline.getPhysicsDt());
    const float scrolled = distanceTicked(level);
    timeline.calculateSteps(static_cast<float>(scaledRealDt(realDt)), followScroll ? scrolled : tick);

    if (grape::ticks::patched() && !followScroll) {
        
        
        
        timeline.m_shouldRender = true;
        const int steps = timeline.estimatedStepCount;
        if (steps > 0) {
            timeline.m_forcedTicks = steps;
            update(static_cast<float>(tick * steps));
            timeline.m_forcedTicks = 1;
        }
        return;
    }

    timeline.m_shouldRender = false;
    for (int done = 0; done < timeline.estimatedStepCount - 1; ++done) {
        update(tick);
        const bool rescale = followScroll && timeline.estimatedStepCount > 0 &&
                             !static_cast<PlayLayer*>(level)->m_hasCompletedLevel;
        if (rescale) timeline.calculateSteps(scaledRealDt(realDt), distanceTicked(level));
    }
    timeline.m_shouldRender = true;
    if (timeline.estimatedStepCount > 0) update(tick);
}

void runFree(float realDt, const Update& update, GJBaseGameLayer* level) {
    auto& timeline = bot().timeline();
    timeline.m_shouldRender = true;
    if (timeline.m_respawnTimer > 0) {
        timeline.m_tpsOverflow = 0.0;
        level->m_extraDelta = 0.0;
    }
    update(realDt * timeline.m_speedhack->inner());
}

void refreshOnly(const Update& update, float realDt) {
    auto& timeline = bot().timeline();
    timeline.m_onlyRefresh = true;
    update(realDt);
    timeline.m_onlyRefresh = false;
    if (auto* play = PlayLayer::get()) bot().trajectory().update(play);
}

void retuneStepLimit(Clock::time_point began) {
    auto& timeline = bot().timeline();
    const double perStep =
        std::chrono::duration<double>(Clock::now() - began).count() / timeline.totalStepCount;
    const double budget = timeline.m_fpsTarget->inner() * perStep;
    if (std::isfinite(budget) && budget > 0.0)
        timeline.m_stepLimit = std::max(1, static_cast<int>(std::floor(1.0 / budget)));
}

void rewindFrames(int count) {
    auto& keeper = bot().keeper();
    auto* play = PlayLayer::get();
    if (!play) {
        keeper.m_backstep = false;
        return;
    }

    const std::size_t extra = count > 1 ? static_cast<std::size_t>(count - 1) : 0;
    const std::size_t droppable = std::min(extra, keeper.m_history.size() - 1);
    for (std::size_t i = 0; i < droppable; ++i) keeper.dropNewest();

    if (keeper.m_history.size() <= 1) {
        keeper.m_backstep = false;
        return;
    }
    keeper.m_pendingRestore = true;
    play->resetLevel();
    keeper.m_pendingRestore = false;
}

}

float FrameEngine::playerSpeedUnits(const float speed) {
    static constexpr float speeds[] = {
        251.1601f, 311.5801f, 387.4201f, 468.0002f, 576.0002f};
    const int index = speed == .7f ? 0 : speed == .9f ? 1 :
                      speed == 1.1f ? 2 : speed == 1.3f ? 3 : 4;
    return speeds[index];
}

float FrameEngine::getSSB() {
    auto* layer = GJBaseGameLayer::get();
    if (layer && layer->m_player1) {
        const float x = layer->m_player1->getPositionX();
        const float step = playerSpeedUnits(layer->m_player1->m_playerSpeed) /
                           std::max(getTps(), 1.0);
        const float factor = ((x + step) - x) / step;
        if (factor > 0.0f && !layer->m_levelEndAnimationStarted) {
            return factor;
        }
    }
    return 1.0f;
}

float FrameEngine::getTimeWarp() {
    auto* play = PlayLayer::get();
    return play ? play->m_gameState.m_timeWarp : 1.0f;
}

int FrameEngine::visualCeiling(int base) {
    return grape::steps::visualCeiling(base, {
        .m_visualUpdates = m_useVisualUpdates->inner(),
        .m_dynamicUpr = m_dynamicUpr->inner(),
        .m_fpsTarget = GameManager::get()->m_customFPSTarget,
        .m_tps = m_tps->inner(),
    });
}

void FrameEngine::calculateSteps(float dt, float targetDt) {
    if (targetDt * std::fmin(getTimeWarp(), 1.0f) == 0.0f) return;
    if (!m_realTime->inner() && !m_dynamicUpr->inner()) m_stepLimit = m_maxUPR->inner();

    const auto budget = grape::steps::budgetFor({
        .m_dt = dt,
        .m_carry = m_tpsOverflow,
        .m_tick = targetDt,
        .m_timeWarp = getTimeWarp(),
        .m_realTime = m_realTime->inner(),
        .m_rendering = Renderer::get()->isRecording(),
        .m_paused = isPaused(),
        .m_ceiling = visualCeiling(m_stepLimit),
    });
    m_tpsOverflow = budget.m_carry;
    m_shouldRender = budget.m_render;
    if (budget.m_clearExtraDelta)
        if (auto* play = PlayLayer::get()) play->m_extraDelta = 0.0f;
    totalStepCount = budget.m_steps;
    estimatedStepCount = budget.m_steps;
}

void FrameEngine::runUpdates(std::function<void(float)> update, float realDt, bool frozen) {
    if (frozen) return refreshOnly(update, realDt);

    auto* play = PlayLayer::get();
    if (!play || play->m_isPaused || !play->m_started) {
        m_tpsOverflow = 0;
        if (play && !play->m_started) consumeStep();
        update(realDt);
        return;
    }

    auto* renderer = Renderer::get();
    if (renderer->m_collectAudio && renderer->isRecording()) realDt = renderer->getDt();

    if (isPaused() && !renderer->isRecording()) {
        m_tpsOverflow = 0;
        m_respawnTimer = 0;
        if (!consumeStep()) return;
        m_shouldRender = true;
        
        
        
        
        const double stepTime = grape::ticks::timestep(
            240.0, play->m_gameState.m_timeWarp);
        update(static_cast<float>(stepTime));
        bot().labels().update(renderer->isRecording());
        bot().trajectory().update(PlayLayer::get());
        return;
    }

    const bool followScroll = !play->m_hasCompletedLevel && !play->m_isPlatformer &&
                              m_ssbFix->inner() && renderer->isRecording();
    const auto began = Clock::now();
    if (!m_lockDelta->inner())
        runFree(realDt, update, play);
    else if (useFastLockDelta())
        runToNextInputs(realDt, update);
    else
        runPaced(realDt, update, followScroll);

    bot().labels().update(renderer->isRecording());
    bot().trajectory().update(PlayLayer::get());
    if (m_dynamicUpr->inner() && totalStepCount > 0) retuneStepLimit(began);
#ifdef GEODE_IS_MACOS
    {
        static double lastSample = 0.0;
        static uint64_t lastFrame = 0;
        const double now = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - lastSample >= 1.0) {
            const uint64_t frame = getFrame();
            geode::log::info(
                "[grape-tps] tps={:g} physicsDt={:.6f} steps={} frameRate={} "
                "paused={} realTime={} lockDelta={}",
                m_tps->inner(), getPhysicsDt(), totalStepCount,
                frame - lastFrame, isPaused(), m_realTime->inner(),
                m_lockDelta->inner());
            lastSample = now;
            lastFrame = frame;
        }
    }
#endif
}

void FrameEngine::runFrozenTick() {
    if (m_frozenScheduledFunctions.empty()) return;
    for (auto& scheduled : m_frozenScheduledFunctions) scheduled(0.0f);
    runUpdates([](float dt) { CCDirector::sharedDirector()->getScheduler()->update(dt); },
               0.0f, true);
    m_frozenScheduledFunctions.clear();
}

void FrameEngine::breakLoop() {
    estimatedStepCount = 0;
    totalStepCount = 0;
    m_tpsOverflow = 0.0;
}

bool FrameEngine::isLockDelta() { return m_lockDelta->inner(); }

void FrameEngine::updateAudioSpeedhack() {
    FMOD::ChannelGroup* master = nullptr;
    FMODAudioEngine::get()->m_system->getMasterChannelGroup(&master);
    const float pitch = m_speedhackAudio->inner() ? static_cast<float>(m_speedhack->inner()) : 1.0f;
    master->setPitch(pitch);
}

uint32_t FrameEngine::getFrame() {
    if (PlayLayer::get()) return m_frame + m_frameOnLastAttempt;
    auto* editor = LevelEditorLayer::get();
    if (!editor) return 0u;

    const auto progress = static_cast<uint32_t>(
        std::max(0, static_cast<int>(editor->m_gameState.m_currentProgress)));
    const uint32_t elapsed = progress > m_editorStartProgress ? progress - m_editorStartProgress : 0;
    return static_cast<uint32_t>(std::max(0, static_cast<int>(elapsed)) / 2);
}

uint32_t FrameEngine::getDisplayFrame() {
    if (auto* lel = LevelEditorLayer::get(); lel) {
        const int doubled =
            static_cast<int>(lel->m_gameState.m_currentProgress * 2) - 1;
        return static_cast<uint32_t>(std::max(0, doubled));
    }
    return getFrame();
}

bool FrameEngine::useFastLockDelta() {
#ifdef GEODE_IS_MOBILE
    return false;
#else
    const bool performanceMode = m_lockDeltaMode->inner() == LockDeltaMode::Performance;
    return m_lockDelta->inner() && performanceMode && bot().isPlaying() &&
           !Renderer::get()->isRecording();
#endif
}

void FrameEngine::backwardsStep(int n) {
    m_paused->inner() = true;
    if (n <= 0 || !m_backwardsStepping->inner()) return;
    if (!bot().keeper().canStepBack()) return;
    bot().keeper().m_backstep = true;
    scheduleFrozenFunction([n](float) { rewindFrames(n); });
}

void FrameEngine::findBestFrameCandidate() {
    auto* play = PlayLayer::get();
    auto* predictor = bot().trajectory().unsafeInner();
    if (!play || !m_backwardsStepping->inner() || !predictor) return;

    setPaused(true);
    const int goodEnough = static_cast<int>(
        static_cast<float>(predictor->getPredictionLength(play)) * m_acceptablePrediction->inner());
    const uint32_t attempts = std::max<uint32_t>(1u, bot().keeper().m_historyLimit->inner());

    const auto scoreFlipped = [&] {
        using Mode = GrapeSettings::TrajectorySettings::Mode;
        const Mode flipped = play->m_player1->m_jumpBuffered ? Mode::Release : Mode::Hold;
        return predictor->simulate(play, true, Mode::Player1 | flipped, false, {.m_bypassConfig = true})
            .score;
    };

    m_predicting = true;
    int bestScore = 0;
    uint32_t bestFrame = 0;
    uint32_t tried = 0;
    while (!play->m_playerDied && tried < attempts) {
        const int score = scoreFlipped();
        if (score > bestScore) {
            bestScore = score;
            bestFrame = getFrame();
        }
        if (bestScore >= goodEnough) break;
        stepOnce();
        CCScheduler::get()->update(getPhysicsDt());
        ++tried;
    }
    backwardsStep(std::min(getFrame() - bestFrame, tried));
    m_predicting = false;
}

void FrameEngine::portableFrameUpdate(PlayLayer* playLayer, float visualDt) {
    if (!playLayer || !bot().isEnabled() || m_onlyRefresh || playLayer->m_resumeTimer > 0) return;

    if (!playLayer->m_playerDied) {
        uint32_t frames = 1;
        if (m_paused->inner()) {
            m_portableFrameRemainder = 0.0;
        } else {
            const double gained = m_tps->inner() * visualDt;
            if (!std::isfinite(gained) || gained <= 0.0) return;
            frames = takeWholeFrames(gained, m_portableFrameRemainder);
            if (frames == 0) return;
        }
        if (wantsFrameHistory()) keepHistoryFrame(playLayer);
        incrementFrame(frames);
        bot().hitboxes().saveToTrail(playLayer);
    }

    bot().autoclicker().update(playLayer);
    bot().pathfinder().update(playLayer);
}

void FrameEngine::portableStepUpdate(PlayLayer* playLayer) {
    if (!playLayer || !bot().isEnabled() || m_onlyRefresh) return;

    if (!playLayer->m_playerDied) {
        incrementFrame();
        bot().hitboxes().saveToTrail(playLayer);
        if (wantsFrameHistory()) keepHistoryFrame(playLayer);
    }

    bot().autoclicker().update(playLayer);
    bot().pathfinder().update(playLayer);
}

void FrameEngine::runMobileFrame(std::function<void(float)> update, float realDt) {
    auto* playLayer = PlayLayer::get();
    const bool live = playLayer && playLayer->m_started && !playLayer->m_isPaused;
    update(live ? realDt * static_cast<float>(m_speedhack->inner()) : realDt);
    if (!live) return;
    bot().labels().update(Renderer::get()->isRecording());
    bot().trajectory().update(playLayer);
}

#if defined(GEODE_IS_WINDOWS) && GEODE_COMP_GD_VERSION == 22081
namespace {

constexpr uintptr_t STEP_DELTA_SITE = 0x237A7C;
constexpr uintptr_t STEP_COUNT_SITE = 0x237DCE;
constexpr uintptr_t STEP_DELTA_RESTORE_SITE = 0x238F6E;
constexpr uintptr_t BEFORE_STEP_SITE = 0x237E42;
constexpr uintptr_t AFTER_STEP_SITE = 0x238BAA;

bool botDrivesPlayLayer() {
    return GJBaseGameLayer::get() && !LevelEditorLayer::get() && bot().isEnabled();
}

bool batchesOwnSteps() {
    auto& timeline = bot().timeline();
    return timeline.useFastLockDelta() || !timeline.m_lockDelta->inner() || !PlayLayer::get();
}

void widenStepDelta(SafetyHookContext& context) {
    if (!botDrivesPlayLayer() || bot().timeline().m_onlyRefresh) return;
    context.xmm1.f64[0] *= bot().timeline().m_tps->inner() / 60.0;
    context.rip += 0x08;
}

void forceStepCount(SafetyHookContext& context) {
    if (LevelEditorLayer::get() || !bot().isEnabled() || !batchesOwnSteps()) return;
    context.rdx = 2 - bot().timeline().estimatedStepCount;
}

void rebuildStepDelta(SafetyHookContext& context) {
    if (!botDrivesPlayLayer() || !batchesOwnSteps()) return;
    auto& timeline = bot().timeline();
    context.xmm9.f64[0] = timeline.getPhysicsDt() * timeline.estimatedStepCount;
}

void beforeStep(SafetyHookContext&) {
    if (bot().timeline().m_onlyRefresh) return;
    auto* play = PlayLayer::get();
    if (play && !play->m_playerDied && wantsFrameHistory()) keepHistoryFrame(play);
}

void afterStep(SafetyHookContext&) {
    if (!bot().isEnabled()) return;
    auto* level = GJBaseGameLayer::get();
    if (!level || level->m_resumeTimer > 0) return;

    auto* play = PlayLayer::get();
    auto& timeline = bot().timeline();
    if (!level->m_playerDied) {
        if (play) timeline.incrementFrame();
        bot().hitboxes().saveToTrail(level);
        if (play && timeline.m_fullGamePrediction->inner() && timeline.m_preventDeath->inner())
            grape::death::lookAhead(level);
    }
    if (play) {
        bot().autoclicker().update(play);
        bot().pathfinder().update(play);
    }
}

}

$execute {
    const auto at = [](uintptr_t offset) { return geode::base::get() + offset; };
    util::midhook(at(STEP_DELTA_SITE), "physDt", widenStepDelta);
    util::midhook(at(STEP_COUNT_SITE), "physStepCount", forceStepCount);
    util::midhook(at(STEP_DELTA_RESTORE_SITE), "restorePhysDt", rebuildStepDelta);
    util::midhook(at(BEFORE_STEP_SITE), "earlyUpdate", beforeStep);
    util::midhook(at(AFTER_STEP_SITE), "frameUpdate", afterStep);
}
#endif

$execute {
    auto& timeline = bot().timeline();

    timeline.m_speedhack->handle([](double) { bot().timeline().updateAudioSpeedhack(); });

    timeline.m_paused->handle([](bool& paused) {
        auto* play = PlayLayer::get();
        if (play && (play->m_isPaused || Renderer::get()->isRecording())) paused = false;
    });

    timeline.m_stepOnce->handle([](bool&) {
        auto& paused = bot().timeline().m_paused;
        paused->inner() = true;
        paused->notifyChange();
    });

    timeline.m_stepBackwards->handle([](bool& requested) {
        const bool allowed = requested && PlayLayer::get() && bot().keeper().canStepBack() &&
                             !Renderer::get()->isRecording();
        if (allowed) bot().timeline().backwardsStep();
        requested = false;
    });

    timeline.m_predictBestPath->handle([](bool&) { bot().timeline().findBestFrameCandidate(); });
}
