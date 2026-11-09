#include "Geode/binding/GJBaseGameLayer.hpp"

#ifdef GEODE_IS_WINDOWS
#include <Zydis/Zydis.h>
#endif

#include <Geode/Geode.hpp>
#include <Geode/binding/AdvancedFollowInstance.hpp>
#include <Geode/binding/GJGroundLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#ifdef GEODE_IS_WINDOWS
#include <safetyhook.hpp>
#endif
#include <array>
#include <vector>

#include "assist/cps.hpp"
#include "assist/hitboxes.hpp"
#include "assist/ufogap.hpp"

#include "checkpoint/practice.hpp"
#include "features/death_guard.hpp"
#include "engine/engine.hpp"
#include "engine/tick_patch.hpp"
#include "engine/timeline.hpp"
#include "physics/collisions.hpp"
#include "physics/gjbasegamelayer.hpp"
#include "trajectory/trajectory.hpp"
#include "engine/variance_tag.hpp"
#include "vine/session.hpp"
#include "util/input_trace.hpp"
#ifdef GEODE_IS_MOBILE
#include "engine/tick_patch.hpp"
#include "engine/tick_plan.hpp"
#include "render/renderer.hpp"
#endif
#if defined(GEODE_IS_MOBILE)
#include "scripting/script_engine.hpp"
#endif
#ifdef GEODE_IS_WINDOWS
#include "util/midhook.hpp"
#endif

using namespace geode::prelude;

#include <Geode/modify/GJBaseGameLayer.hpp>

namespace {

void dismissEndLayerForRestart(const grape::vine::ReplayDirector& session,
                                GJBaseGameLayer* self, uint32_t frame) {
    const auto* input = session.m_head.peek();
    if (!input) return;

    const uint32_t delayInFrames =
        GrapeEngine::get()->timeline().m_tps->inner() * 0.5;

    if (input->m_type != EventKind::RestartFull ||
        input->m_frame != frame + delayInFrames) {
        return;
    }

    if (auto* endLayer = self->getChildByID("EndLevelLayer"); endLayer) {
        FMODAudioEngine::sharedEngine()->clearAllAudio();
        FMODAudioEngine::sharedEngine()->playEffect("playSound_01.ogg", 1.0,
                                                    0.0, 0.3);
        ((GJDropDownLayer*)endLayer)->exitLayer(nullptr);
    }
}

}

#ifdef GEODE_IS_MOBILE
namespace {
int s_forcedTicks = -1;

double mobileTps() {
    if (!grape::ticks::patched()) return 240.0;
    return GrapeEngine::get()->timeline().getTps();
}
}
#endif

#if defined(GEODE_IS_WINDOWS) && GEODE_COMP_GD_VERSION == 22081
namespace {

struct BytePatch {
    uintptr_t m_offset;
    std::vector<uint8_t> m_bytes;
    const char* m_purpose;
};

std::vector<uint8_t> nops(std::size_t count) { return std::vector<uint8_t>(count, 0x90); }

void applyBytePatches() {
    const BytePatch patches[] = {
        {0x23B4EA, nops(24), "GJBaseGameLayer::resetLevelVariables"},
        {0x4CD95C, nops(19), "UILayer::handleKeypress"},
        {0x3B994C, {0xEB, 0x5E}, "PlayLayer::resetLevel"},
        {0x3BA508, nops(5), "PlayLayer::resumeAndRestart"},
    };
    for (const BytePatch& patch : patches) {
        void* where = reinterpret_cast<void*>(base::get() + patch.m_offset);
        if (!Mod::get()->patch(where, patch.m_bytes))
            geode::log::error("Could not patch {}", patch.m_purpose);
    }
}

}
#endif

struct GrapeGJBaseGameLayer : Modify<GrapeGJBaseGameLayer, GJBaseGameLayer> {
    struct GroundPose {
        CCPoint m_first;
        CCPoint m_second;

        void read(GJGroundLayer* ground) {
            if (!ground || !ground->m_ground1Sprite) return;
            m_first = ground->m_ground1Sprite->getPosition();
            if (ground->m_ground2Sprite) m_second = ground->m_ground2Sprite->getPosition();
        }

        void apply(GJGroundLayer* ground) const {
            if (!ground || !ground->m_ground1Sprite) return;
            ground->m_ground1Sprite->setPosition(m_first);
            if (ground->m_ground2Sprite) ground->m_ground2Sprite->setPosition(m_second);
        }
    };

    struct Fields {
        GJGameState m_settledState;
        std::array<float, 2> m_settledSpin{};
        std::array<GroundPose, 2> m_settledGround{};
        bool m_wasGrounded = true;
        std::array<bool, 2> m_rescued{};
        bool m_replayingInput = false;
    };

#ifdef GEODE_IS_MOBILE
   private:
    void pressDirect(PlayerButton button, bool down, bool player2) {
        if (GameManager::get()->getGameVariable("0010")) player2 = !player2;
        const auto press = down ? &PlayerObject::pushButton
                                : &PlayerObject::releaseButton;
        m_allowedButtons.clear();
        if (m_levelSettings->m_twoPlayerMode) {
            ((player2 ? m_player2 : m_player1)->*press)(button);
        } else {
            (m_player1->*press)(button);
            if (m_gameState.m_isDualMode) (m_player2->*press)(button);
        }
        m_effectManager->playerButton(down, !player2);
        if (!down) return;
        ++m_clicks;
        if (button == PlayerButton::Jump) m_jumping = true;
    }

   public:
#endif

    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("GJBaseGameLayer::handleButton",
                                     Priority::Last)) {
            geode::log::warn(
                "Failed to set priority for handleButton! Alt hook may not "
                "work as expected");
        }

        for (auto name : {"GJBaseGameLayer::collisionCheckObjects",
                          "GJBaseGameLayer::teleportPlayer",
                          "GJBaseGameLayer::flipGravity"}) {
            if (!self.setHookPriorityPre(name, Priority::VeryEarly)) {
                geode::log::warn(
                    "Failed to set priority for {}! Other mods may see "
                    "trajectory fake players and crash",
                    name);
            }
        }
    }

    static std::string buttonName(int button) {
        static constexpr const char* names[] = {"?", "jump", "left", "right"};
        if (button >= 1 && button <= 3) return names[button];
        return fmt::format("button{}", button);
    }

    static void traceInput(const char* source, uint64_t frame, int button,
                           bool down, bool player2) {
#ifdef GEODE_IS_MOBILE
        grape::trace::add(fmt::format("{} f={} {} {} p{}", source, frame,
                                      buttonName(button),
                                      down ? "down" : "up", player2 ? 2 : 1));
#else
        (void)source, (void)frame, (void)button, (void)down, (void)player2;
#endif
    }

    void logPlayerPress(const PlayerButtonCommand& cmd) {
        const int button = static_cast<int>(cmd.m_button);
        if (button < 1 || button > 3) return;
        auto* bot = GrapeEngine::get();
        traceInput("record", bot->timeline().getFrame(),
                   static_cast<int>(cmd.m_button), cmd.m_isPush, cmd.m_isPlayer2);
        bot->replay().recordPlayerInput(
            bot->timeline().getFrame(),
            static_cast<EventKind>(cmd.m_button), cmd.m_isPush,
            cmd.m_isPlayer2,
            this->m_levelSettings->m_twoPlayerMode,
            this->m_isPlatformer);
#ifdef GEODE_IS_MACOS
        geode::log::info("[grape-macro] record f={} btn={} {}", 
                         bot->timeline().getFrame(), button,
                         cmd.m_isPush ? "down" : "up");
#endif
    }

    void logQueuedPresses() {
        for (const auto& cmd : m_queuedButtons) {
            logPlayerPress(cmd);
        }
    }

    std::array<bool, 2> heldOnScreen() const {
        auto& director = GrapeEngine::get()->replay();
        std::array<bool, 2> held{
            m_uiLayer->m_p1Jumping || m_uiLayer->m_p1TouchId != -1,
            m_uiLayer->m_p2Jumping || m_uiLayer->m_p2TouchId != -1,
        };
        if (director.hasFlippedControls()) std::swap(held[0], held[1]);
        const auto [first, second] = director.resolveMirror(held[0], held[1]);
        return {first, second};
    }

    void keepJumpMatched(PlayerObject* player, bool holding, bool secondSlot) {
        const bool asPlayer2 = GrapeEngine::get()->replay().playerFlipped(secondSlot);
        while (!player->m_controlsDisabled) {
            const bool wanted = holding != player->m_isUpsideDown;
            if (player->m_jumpBuffered == wanted) break;
            logPlayerPress({.m_button = PlayerButton::Jump,
                            .m_isPush = wanted,
                            .m_isPlayer2 = asPlayer2,
                            .m_step = 0,
                            .m_timestamp = 0.0});
            GJBaseGameLayer::handleButton(wanted, 1, !asPlayer2);
        }
    }

    void reassertGravityHolds() {
        m_queuedButtons.clear();
        if (PlayLayer::get()->m_levelEndAnimationStarted) return;
        const auto held = heldOnScreen();
        keepJumpMatched(m_player1, held[0], false);
        if (m_gameState.m_isDualMode && m_levelSettings->m_twoPlayerMode)
            keepJumpMatched(m_player2, held[1], true);
    }

    void mirrorQueuedHolds() {
        auto& director = GrapeEngine::get()->replay();
        const bool twoPlayer = m_levelSettings->m_twoPlayerMode;
        const uint32_t frame = director.m_forceNextInput
                                   ? std::numeric_limits<uint32_t>::max()
                                   : GrapeEngine::get()->timeline().getFrame();

        const std::size_t queued = m_queuedButtons.size();
        for (std::size_t i = 0; i < queued; ++i) {
            const PlayerButtonCommand command = m_queuedButtons[i];
            const bool secondPlayer = command.m_isPlayer2 && twoPlayer;
            const int button = static_cast<int>(command.m_button);
            director.m_lastInputs.insert_or_assign(
                button + (secondPlayer ? 4 : 0),
                grape::vine::Event(frame, static_cast<EventKind>(button), command.m_isPush,
                                   secondPlayer));
            if (director.m_mirrorInputs)
                queueButton(button, command.m_isPush ^ director.m_mirrorInverted,
                            director.playerFlipped(!command.m_isPlayer2), 0.0);
        }
    }

    void handleButton(bool pressed, int button, bool player1) {
#if defined(GEODE_IS_MOBILE)
        grape::scripting::ScriptEngine::get().input(
            player1 ? 1 : 2, button, pressed);
#endif
        auto* bot = GrapeEngine::get();
#ifdef GEODE_IS_MOBILE
        if (grape::trace::enabled()) {
            std::string allowed;
            for (int id : m_allowedButtons)
                allowed += (allowed.empty() ? "" : ",") + std::to_string(id);
            grape::trace::add(fmt::format(
                "game f={} {} {} p{} allowed=[{}]", bot->timeline().getFrame(),
                buttonName(button), pressed ? "down" : "up", player1 ? 1 : 2,
                allowed));
        }
        if (bot->isEnabled() &&
            (bot->isRecording() || bot->isPlaying() ||
             GrapeSettings::get()->frameStepperOverlay))
            m_allowedButtons.clear();
        if (bot->isEnabled() && bot->isPlaying() &&
            !m_fields->m_replayingInput &&
            PlayLayer::get() == static_cast<GJBaseGameLayer*>(this) &&
            (bot->replay().m_ignoreInputs->inner() || button != 1))
            return;
#endif
        if (bot->isRecording()) {
            logPlayerPress({
                .m_button = static_cast<PlayerButton>(button),
                .m_isPush = pressed,
                .m_isPlayer2 = !player1,
                .m_step = 0,
                .m_timestamp = 0.0,
            });
        }
        return GJBaseGameLayer::handleButton(pressed, button, player1);
    }

    bool applyInputEvent(const grape::vine::Event& action) {
        auto* bot = GrapeEngine::get();
        auto& director = bot->replay();
        auto& updater = bot->timeline();

        switch (action.m_type) {
            case EventKind::Death:
                director.beginMacroReset();
                return false;
            case EventKind::RestartFull:
                director.beginMacroReset();
                ((PlayLayer*)this)->fullReset();
                return true;
            case EventKind::Restart:
                director.beginMacroReset();
                ((PlayLayer*)this)->resetLevel();
                return true;
            case EventKind::Tps:
                updater.m_tps->inner() = action.m_tps;
                updater.estimatedStepCount = 0;
                updater.totalStepCount = 0;
                return false;
            default:
                break;
        }

        const int button = static_cast<int>(action.m_type);
        if (button == 0 || button > 3) return false;

        if (button == 1 && action.m_holding) {
            bot->cps().pushAction(action.m_player2);
        }

#ifdef GEODE_IS_MOBILE
        const bool player2 = director.playerFlipped(action.m_player2);
        traceInput("replay", updater.getFrame(), button, action.m_holding,
                   player2);
        if (button == 1 && action.m_holding && easyBlackOrbsWanted()) {
            const int taps = rescueTaps(player2 ? m_player2 : m_player1);
            if (taps > 0) m_fields->m_rescued[player2 ? 1 : 0] = true;
            for (int tap = taps; tap > 0; --tap) {
                this->pressDirect(PlayerButton::Jump, true, player2);
                this->pressDirect(PlayerButton::Jump, false, player2);
            }
        }
        if (button == 1) {
            this->pressDirect(PlayerButton::Jump, action.m_holding, player2);
        } else {
            m_fields->m_replayingInput = true;
            for (int sent : {button, button + 2}) {
                m_allowedButtons.clear();
                GJBaseGameLayer::handleButton(action.m_holding, sent, !player2);
            }
            m_fields->m_replayingInput = false;
        }
#else
        this->queueButton(button, action.m_holding,
                          director.playerFlipped(action.m_player2), 0);
#endif
        return false;
    }

    static int pendingDropRings(PlayerObject* player) {
        if (!player || !player->m_touchingRings) return 0;
        int count = 0;
        auto* rings = player->m_touchingRings;
        for (unsigned int i = 0; i < rings->count(); ++i) {
            auto* ring = static_cast<RingObject*>(rings->objectAtIndex(i));
            if (!ring || ring->m_objectType != GameObjectType::DropRing)
                continue;
            if (!ring->m_isMultiActivate &&
                ring->hasBeenActivatedByPlayer(player))
                continue;
            ++count;
        }
        return std::min(count, 4);
    }

    static int rescueTaps(PlayerObject* player) {
        const bool rescue = player &&
            grape::assist::dropRingNeedsExtraPress(
                static_cast<float>(player->m_yVelocity),
                static_cast<bool>(player->m_isUpsideDown));
        return grape::assist::planClickBurst(
            rescue ? pendingDropRings(player) : 0, true).m_taps;
    }

    void expandDropRingPresses() {
        auto& session = GrapeEngine::get()->replay();
        std::vector<PlayerButtonCommand> expanded;
        expanded.reserve(m_queuedButtons.size());
        int added = 0;

        for (const auto& command : m_queuedButtons) {
            if (command.m_button == PlayerButton::Jump && command.m_isPush) {
                const bool rescuedPlayer2 =
                    session.playerFlipped(command.m_isPlayer2);
                const int taps =
                    rescueTaps(rescuedPlayer2 ? m_player2 : m_player1);
                if (taps > 0) m_fields->m_rescued[rescuedPlayer2 ? 1 : 0] = true;
                for (int i = 0; i < taps; ++i) {
                    PlayerButtonCommand extra{};
                    extra.m_button = PlayerButton::Jump;
                    extra.m_isPlayer2 = command.m_isPlayer2;
                    extra.m_step = 0;
                    extra.m_timestamp = 0.0;
                    extra.m_isPush = true;
                    expanded.push_back(extra);
                    extra.m_isPush = false;
                    expanded.push_back(extra);
                    ++added;
                }
            }
            expanded.push_back(command);
        }

        if (!added) return;
        m_queuedButtons.clear();
        for (const auto& command : expanded) m_queuedButtons.push_back(command);
    }

    bool playDueInputs() {
        auto& session = GrapeEngine::get()->replay();
        uint32_t frame = GrapeEngine::get()->timeline().getFrame();
        if (LevelEditorLayer::get() && !PlayLayer::get() && frame > 0) {
            --frame;
        }

        dismissEndLayerForRestart(session, this, frame);

        while (const auto* due = session.m_head.peekDue(frame)) {
            const grape::vine::Event action = *due;
            session.m_head.advance();
#ifdef GEODE_IS_MACOS
            geode::log::info("[grape-macro] apply at={} event f={} btn={} {}",
                             frame, action.m_frame,
                             static_cast<int>(action.m_type),
                             action.m_holding ? "down" : "up");
#endif
            if (this->applyInputEvent(action)) return true;
        }
        return false;
    }

    bool easyBlackOrbsWanted() const {
        return GrapeSettings::get()->autoclickerMovingGap;
    }

    void processQueuedButtons(float dt, bool clearInputQueue) {
        auto* bot = GrapeEngine::get();
        auto& session = bot->replay();
        if (!bot->isEnabled()) {
            return GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
        }

        bot->keeper().trackPlatformerInputs(m_queuedButtons);

        if (session.m_ignoreInputs->inner() && bot->isPlaying()) {
            m_queuedButtons.clear();
        }

        if (bot->isRecording()) {
            if (session.m_maintainGravity) {
                this->reassertGravityHolds();
                return;
            }

            this->mirrorQueuedHolds();
        }
#ifndef GEODE_IS_MOBILE
        else if (bot->isPlaying() && this->playDueInputs()) {
            return;
        }
#endif

        if (easyBlackOrbsWanted()) this->expandDropRingPresses();

#ifdef GEODE_IS_MOBILE
        if (!m_queuedButtons.empty()) {
            std::string queued;
            for (const auto& command : m_queuedButtons) {
                if (!queued.empty()) queued += ", ";
                queued += fmt::format(
                    "{} {} p{}", buttonName(static_cast<int>(command.m_button)),
                    command.m_isPush ? "down" : "up", command.m_isPlayer2 ? 2 : 1);
            }
            grape::trace::add(fmt::format("queue f={} [{}]",
                                          bot->timeline().getFrame(), queued));
        }
#endif
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);

#ifdef GEODE_IS_MOBILE
        if (bot->isPlaying() && this->playDueInputs()) return;
#endif

        if (bot->isRecording() &&
            bot->timeline().m_autoFlipOnDeath->inner()) {
            grape::death::releaseHeldJump(this);
            this->logQueuedPresses();
            GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
            return;
        }
    }

   private:
    std::array<PlayerObject*, 2> players() const { return {m_player1, m_player2}; }

    bool extrapolationPermitted() {
        return GrapeEngine::get()->timeline().estimatedStepCount == 0;
    }

    void snapshotVisualAnchors() {
        auto& fields = *m_fields.self();
        const auto both = players();
        for (int slot = 0; slot < 2; ++slot) fields.m_settledSpin[slot] = both[slot]->getRotation();
        fields.m_settledState = m_gameState;
        fields.m_settledGround[0].read(m_groundLayer);
        fields.m_settledGround[1].read(m_groundLayer2);
    }

    void restoreVisualAnchors() {
        auto& fields = *m_fields.self();
        m_gameState = fields.m_settledState;
        const auto both = players();
        for (int slot = 0; slot < 2; ++slot) both[slot]->setRotation(fields.m_settledSpin[slot]);
        fields.m_settledGround[0].apply(m_groundLayer);
        fields.m_settledGround[1].apply(m_groundLayer2);
    }

    void blendPlayerToPrediction(float dt) {
        auto& timeline = GrapeEngine::get()->timeline();
        auto* predictor = GrapeEngine::get()->trajectory().unsafeInner();
#ifdef GEODE_IS_MOBILE
        if (!predictor) {
            s_forcedTicks = 0;
            GJBaseGameLayer::update(0.0f);
            s_forcedTicks = -1;
            return;
        }
#endif
        using Mode = GrapeSettings::TrajectorySettings::Mode;
        const auto both = players();
        const auto& spin = m_fields->m_settledSpin;
        for (int slot = 0; slot < 2; ++slot) both[slot]->setRotation(spin[slot]);

        std::array<TrajectoryPlayerData, 2> ahead;
        for (int slot = 0; slot < 2; ++slot) {
            const int who = slot == 0 ? Mode::Player1 : Mode::Player2;
            const int hold = both[slot]->m_jumpBuffered ? Mode::Hold : Mode::Release;
            ahead[slot] = predictor->simulate(this, true, who | hold, true,
                                              {.m_bypassConfig = true, .m_maxLength = 0});
        }

        const float t = timeline.m_tpsOverflow / timeline.getPhysicsDt();
        std::array<CCPoint, 2> position;
        std::array<float, 2> rotation{};
        for (int slot = 0; slot < 2; ++slot) {
            position[slot] = ahead[slot].position * t + both[slot]->m_position * (1 - t);
            rotation[slot] = ahead[slot].rotation * t + spin[slot] * (1 - t);
        }
        for (int slot = 0; slot < 2; ++slot) both[slot]->setPosition(position[slot]);
        for (int slot = 0; slot < 2; ++slot) both[slot]->setRotation(rotation[slot]);

        const float ticksAt60 = dt * 60.0;
        updateCamera(ticksAt60);
        updateVisibility(dt);
        for (PlayerObject* player : both)
            if (player->m_isDart) player->m_waveTrail->setPosition(player->m_position);
        CCLayer::update(dt);
    }

   public:
#ifdef GEODE_IS_MOBILE
    void update(float dt) override {
        auto* bot = GrapeEngine::get();
        auto& updater = bot->timeline();
        auto* playLayer = PlayLayer::get();
        if (!bot->isEnabled() || playLayer != static_cast<GJBaseGameLayer*>(this)) {
            s_forcedTicks = -1;
            return GJBaseGameLayer::update(dt);
        }
        if (updater.m_onlyRefresh) {
            s_forcedTicks = 0;
            GJBaseGameLayer::update(0.0f);
            s_forcedTicks = -1;
            return;
        }

        const bool rendering = Renderer::get()->isRecording();
        const bool held = updater.isPaused() && !rendering &&
                          playLayer->m_started && !playLayer->m_isPaused;
        const bool stepWanted = held && updater.consumeStep();
        const int cap = updater.m_realTime->inner() || rendering
                            ? 0
                            : static_cast<int>(updater.m_maxUPR->inner());
        const double timestep =
            grape::ticks::timestep(mobileTps(), m_gameState.m_timeWarp);
        const auto plan = grape::ticks::plan(
            {updater.m_tpsOverflow, dt, timestep, held, stepWanted, cap});
        updater.m_tpsOverflow = plan.m_carry;

        int steps = plan.m_steps;
        if (updater.m_respawnTimer > 0) {
            updater.m_respawnTimer--;
            steps = std::min(steps, 1);
            updater.m_tpsOverflow = 0.0;
        }
        updater.totalStepCount = steps;
        updater.estimatedStepCount = steps;

        const bool extrapolate = updater.m_extrapolateFrames->inner() &&
                                 updater.getFrame() > updater.m_frameOnLastAttempt;
        if (extrapolate && steps == 0) {
            this->blendPlayerToPrediction(dt);
            return;
        }
        if (extrapolate) this->restoreVisualAnchors();

        const int batch =
            grape::ticks::batchSize(steps, timestep, m_resumeTimer > 0);
        int left = steps;
        do {
            const int ticks = std::min(left, batch);
            s_forcedTicks = ticks;
            GJBaseGameLayer::update(static_cast<float>(ticks * timestep));
            left -= ticks;
        } while (left > 0 && batch > 0);
        s_forcedTicks = -1;
        this->snapshotVisualAnchors();
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (PlayLayer::get() != static_cast<GJBaseGameLayer*>(this)) {
            auto* bot = GrapeEngine::get();
            if (bot->isEnabled() && !bot->timeline().m_onlyRefresh &&
                LevelEditorLayer::get() == static_cast<GJBaseGameLayer*>(this) &&
                m_playbackMode == PlaybackMode::Playing && !m_player1->m_isDead)
                bot->hitboxes().saveToTrail(this);
            return;
        }
        for (int i = 0; i < 2; ++i) {
            if (!m_fields->m_rescued[i]) continue;
            m_fields->m_rescued[i] = false;
            if (auto* player = i == 0 ? m_player1 : m_player2) {
                player->m_stateJumpBuffered = false;
                player->m_stateRingJump2 = false;
            }
        }
        if (m_player1) {
            const bool grounded = m_player1->m_isOnGround;
            if (m_fields->m_wasGrounded && !grounded)
                grape::trace::add(fmt::format(
                    "left ground f={}", GrapeEngine::get()->timeline().getFrame()));
            m_fields->m_wasGrounded = grounded;
        }
        GrapeEngine::get()->timeline().portableStepUpdate(PlayLayer::get());
    }

    double getModifiedDelta(float dt) {
        if (s_forcedTicks >= 0) {
            if (m_resumeTimer > 0) {
                --m_resumeTimer;
                grape::ticks::setCount(0);
                return 0.0;
            }
            grape::ticks::setCount(s_forcedTicks);
            return s_forcedTicks *
                   grape::ticks::timestep(mobileTps(), m_gameState.m_timeWarp);
        }

        if (m_resumeTimer > 0) {
            --m_resumeTimer;
            dt = 0.0f;
        }
        const auto vanilla = grape::ticks::vanilla(
            dt, m_extraDelta, grape::ticks::timestep(240.0, m_gameState.m_timeWarp));
        m_extraDelta = vanilla.m_extra;
        grape::ticks::setCount(vanilla.m_steps);
        return vanilla.m_delta;
    }
#else
    void update(float dt) override {
        auto& timeline = GrapeEngine::get()->timeline();
        if (timeline.m_onlyRefresh || !GrapeEngine::get()->isEnabled())
            return GJBaseGameLayer::update(dt);

        if (!timeline.isLockDelta() || !PlayLayer::get())
            timeline.calculateSteps(dt, timeline.getPhysicsDt());
        if (timeline.m_respawnTimer > 0) {
            --timeline.m_respawnTimer;
            timeline.totalStepCount = std::min(timeline.totalStepCount, 1);
            timeline.estimatedStepCount = std::min(timeline.estimatedStepCount, 1);
        }

        const bool smoothing = timeline.m_extrapolateFrames->inner() &&
                               timeline.getFrame() > timeline.m_frameOnLastAttempt;
        if (smoothing && extrapolationPermitted()) return blendPlayerToPrediction(dt);
        if (smoothing) restoreVisualAnchors();
        else if (!timeline.isLockDelta() && timeline.estimatedStepCount == 0) return;

        
        
        
        if (grape::ticks::patched())
            grape::ticks::setCount(timeline.m_forcedTicks);
        GJBaseGameLayer::update(dt);
        snapshotVisualAnchors();
    }
#endif

    void gameEventTriggered(GJGameEvent event, int first, int second) {
        auto* bot = GrapeEngine::get();
        if (bot->trajectory().drawing()) return;
        const bool unwantedRespawnEvent = event == GJGameEvent::CheckpointRespawn &&
                                          !bot->keeper().m_shouldLoadPlatformer;
        if (!unwantedRespawnEvent) GJBaseGameLayer::gameEventTriggered(event, first, second);
    }

    void destroyObject(GameObject* object) {
        auto* bot = GrapeEngine::get();
        if (bot->trajectory().drawing()) return;
        if (m_isPracticeMode) bot->keeper().noteBroken(object);
        GJBaseGameLayer::destroyObject(object);
    }

#ifndef GEODE_IS_MOBILE
    double getModifiedDelta(float dt) {
        if (!GrapeEngine::get()->isEnabled()) return GJBaseGameLayer::getModifiedDelta(dt);
        const float gameDelta = GJBaseGameLayer::getModifiedDelta(dt);
        if (LevelEditorLayer::get()) return gameDelta;

        auto& timeline = GrapeEngine::get()->timeline();
        if (timeline.m_onlyRefresh) return 0.0;
        const float tick = timeline.getPhysicsDt() * std::fmin(m_gameState.m_timeWarp, 1.0f);
        
        
        return tick * static_cast<float>(timeline.m_forcedTicks);
    }
#endif

    void collisionCheckObjects(PlayerObject* player, std::vector<GameObject*>* objects,
                               int count, float dt) {
        if (!simulated(player))
            return GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);
        phys::collisionCheckObjects(this, player, objects, count, dt);
    }

    void teleportPlayer(TeleportPortalObject* portal, PlayerObject* player) {
        if (simulated(player)) return phys::teleportPlayer(this, portal, player);
        GJBaseGameLayer::teleportPlayer(portal, player);
    }

    void flipGravity(PlayerObject* player, bool upsideDown, bool unk) {
        if (simulated(player)) return phys::flipGravity(player, upsideDown);
        const bool flips = player && player->m_isUpsideDown != upsideDown;
        GJBaseGameLayer::flipGravity(player, upsideDown, unk);
        if (flips && GrapeSettings::get()->physics21) player->m_yVelocity *= LEGACY_FLIP_BOOST;
    }

    void retagEffectGroups() {
        const uint64_t seed = GrapeEngine::get()->replay().m_rng.baseSeed();
        const auto retag = [&](int group) {
            cocos2d::CCArray* members = getGroup(group);
            for (int index = 0; index < static_cast<int>(members->count()); ++index) {
                auto* object = static_cast<GameObject*>(members->objectAtIndex(index));
                object->m_varianceIndex = grape::variance::tagFor(seed, object, group, index);
            }
        };
        for (auto& follow : m_gameState.m_advanceFollowInstances) retag(follow.m_group);
        for (auto& rotate : m_gameState.m_rotateEffectInstances) retag(rotate.m_targetID);
        for (auto& scale : m_gameState.m_scaleEffectInstances) retag(scale.m_targetID);
        for (auto& move : m_gameState.m_moveEffectInstances) retag(move.m_targetID);
    }

#ifdef GEODE_IS_MACOS
    
    
    
    void processMoveActions() {
        retagEffectGroups();
        GJBaseGameLayer::processMoveActions();
    }
#else
    void processMoveActionsStep(float dt, bool visibleFrame) {
        retagEffectGroups();
        GJBaseGameLayer::processMoveActionsStep(dt, visibleFrame);
    }
#endif

   private:
    static constexpr double LEGACY_FLIP_BOOST = 1.75 / 0.5;

    bool simulated(PlayerObject* player) {
        return GrapeEngine::get()->trajectory().isFakePlayer(player);
    }
};

#if defined(GEODE_IS_WINDOWS) && GEODE_COMP_GD_VERSION == 22081
namespace {

void drawShake(SafetyHookContext& context) {
    context.rax = GrapeEngine::get()->replay().m_rng.drawShake();
}

void drawTeleport(SafetyHookContext& context) {
    context.rax = GrapeEngine::get()->replay().m_rng.drawTeleport();
}

void queueCheckpointInstead(SafetyHookContext& context) {
    context.rip += 5;
    PlayLayer::get()->queueCheckpoint();
}

}

$execute {
    applyBytePatches();
    const auto at = [](uintptr_t offset) { return geode::base::get() + offset; };
    for (uintptr_t site : {0x23E173, 0x23E1A1, 0x23E1CB, 0x23E1E9})
        util::midhook(at(site), "shakeRandomOverride", drawShake);
    util::midhook(at(0x20FED3), "teleportRandomOverride", drawTeleport);
    util::midhook(at(0x3A3657), "checkpointPlacement", queueCheckpointInstead);
}
#endif

$execute {
    GrapeEngine::get()->timeline().m_lockDelta->handle(
        [](bool&) { GrapeEngine::get()->timeline().m_tpsOverflow = 0.0; });
}
