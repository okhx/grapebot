#include "death_guard.hpp"

#include <Geode/Geode.hpp>

#include "assist/hitboxes.hpp"
#include "checkpoint/practice.hpp"
#include "death_plan.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "noclip.hpp"
#include "trajectory/trajectory.hpp"

namespace grape::death {

void handle(PlayLayer* layer, PlayerObject* player, GameObject* cause,
            const std::function<void()>& kill) {
    auto* bot = GrapeEngine::get();
    if (bot->trajectory().hasDied(player)) return;

    auto& timeline = bot->timeline();
    auto& keeper = bot->keeper();
    const bool noclipOn = timeline.m_noclip->inner();
    const Plan plan = planFor({
        .m_anticheat = cause == layer->m_anticheatSpike,
        .m_noclipOn = noclipOn,
        .m_noclipCoversPlayer =
            noclipOn && noclip::covers(timeline.m_noclipType, player,
                                       layer->m_player1, layer->m_player2),
        .m_preventDeath = timeline.m_preventDeath->inner(),
        .m_autoFlip = timeline.m_autoFlipOnDeath->inner(),
    });

    if (plan.m_tint) noclip::flash(layer);
    if (plan.m_showCause) noclip::stopFlash(layer);
    if (plan.m_kill) kill();
    if (plan.m_showCause) bot->hitboxes().showDeath(player, cause);

    if (plan.m_stepBack) {
        timeline.backwardsStep();
        if (plan.m_flipAfterStep && !keeper.m_backstep) {
            const bool second = player != layer->m_player1;
            layer->queueButton(1, !player->m_jumpBuffered, second, 0.0);
            timeline.setPaused(false);
        }
    }

    if (!keeper.m_pendingRestore && !keeper.m_backstep) keeper.m_normalDeath = true;
}

#ifdef GEODE_IS_WINDOWS
namespace {

void steer(GJBaseGameLayer* layer, bool second) {
    auto* predictor = GrapeEngine::get()->trajectory().unsafeInner();
    PlayerObject* player = second ? layer->m_player2 : layer->m_player1;
    if (!predictor || !player) return;

    using Mode = GrapeSettings::TrajectorySettings::Mode;
    const int mode = (second ? Mode::Player2 : Mode::Player1) |
                     (player->m_jumpBuffered ? Mode::Hold : Mode::Release);
    const auto outcome =
        predictor->simulate(layer, !second, mode, false, {.m_bypassConfig = true});

    auto& timeline = GrapeEngine::get()->timeline();
    if (outcome.score <= 2) timeline.setPaused(true);
    if (!timeline.m_autoFlipOnDeath->inner()) return;
    layer->queueButton(1, !player->m_jumpBuffered, second, 0.0);
    timeline.setPaused(false);
}

}
#endif

void lookAhead(GJBaseGameLayer* layer) {
#ifdef GEODE_IS_WINDOWS
    steer(layer, false);
    if (layer->m_gameState.m_isDualMode) steer(layer, true);
#else
    (void)layer;
#endif
}

void releaseHeldJump(GJBaseGameLayer* layer) {
    auto* level = PlayLayer::get();
    if (!level || level->m_levelEndAnimationStarted) return;
    if (!layer->m_player1->m_jumpBuffered) return;
    layer->m_queuedButtons.push_back({.m_button = PlayerButton::Jump,
                                      .m_isPush = false,
                                      .m_isPlayer2 = false,
                                      .m_step = 0,
                                      .m_timestamp = 0.0});
}

bool hidesDeathEffect() {
    auto& timeline = GrapeEngine::get()->timeline();
    return timeline.m_preventDeath->inner() || timeline.m_predicting;
}

}
