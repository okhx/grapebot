#include "pathfinder.hpp"

#include "assist/nearby_objects.hpp"
#include "assist/pathchoice.hpp"
#include "checkpoint/practice.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "trajectory/trajectory.hpp"
#include "vine/session.hpp"

#include <cmath>
#include <limits>

namespace {

constexpr int timingFrames(int cycle, float percent) {
    return std::max(1, static_cast<int>(cycle * percent * .01f + .5f));
}

static_assert(timingFrames(10, 40.0f) == 4);

bool isRing(GameObjectType type) {
    return type == GameObjectType::CustomRing ||
           type == GameObjectType::DashRing ||
           type == GameObjectType::DropRing ||
           type == GameObjectType::GravityDashRing ||
           type == GameObjectType::GravityRing ||
           type == GameObjectType::GreenRing ||
           type == GameObjectType::PinkJumpRing ||
           type == GameObjectType::RedJumpRing ||
           type == GameObjectType::SpiderOrb ||
           type == GameObjectType::YellowJumpRing ||
           type == GameObjectType::TeleportOrb;
}

constexpr std::array<int, 9> FLIP_OFFSETS = {0, 1, 2, 3, 5, 8, 12, 17, 23};

constexpr int BEAM_WIDTH = 3;
constexpr int MAX_SIMULATIONS = 24;

constexpr bool betterPlan(int scoreA, int firstA, int countA, int scoreB,
                          int firstB, int countB) {
    if (scoreA != scoreB) return scoreA > scoreB;
    if (firstA != firstB) return firstA > firstB;
    return countA < countB;
}

static_assert(betterPlan(20, 0, 1, 10, 5, 1), "survival dominates");
static_assert(betterPlan(20, 7, 1, 20, 3, 1), "commit as late as possible");
static_assert(betterPlan(20, 5, 1, 20, 5, 2), "prefer fewer inputs");
static_assert(!betterPlan(20, 5, 1, 20, 5, 1), "not better than itself");
static_assert(!betterPlan(10, 9, 1, 20, 0, 4), "a longer survivor still wins");

struct NearbyObjects {
    float hazard = std::numeric_limits<float>::max();
    float block = std::numeric_limits<float>::max();
    float ring = std::numeric_limits<float>::max();
    float lowerClearance = std::numeric_limits<float>::max();
    float upperClearance = std::numeric_limits<float>::max();
};

bool holdingJump(PlayerObject* player) {
    const auto held = player->m_holdingButtons.find(
        static_cast<int>(PlayerButton::Jump));
    return held != player->m_holdingButtons.end() && held->second;
}

NearbyObjects inspectNearby(PlayLayer* pl, PlayerObject* player) {
    NearbyObjects nearby;
    const float dir = player->m_isGoingLeft ? -1.0f : 1.0f;
    const auto playerRect = player->getObjectRect();
    const float centerY = playerRect.getMidY();
    const float horizontalSpeed =
        FrameEngine::playerSpeedUnits(player->m_playerSpeed);
    const float emergencyAhead = horizontalSpeed * .06f + 12.0f;
    grape::assist::forEachNearbyObject(pl, [&](GameObject* object) {
        const auto type = object->m_objectType;
        const auto rect = object->getObjectRect();
        const float ahead = dir > 0 ? rect.getMinX() - playerRect.getMaxX()
                                    : playerRect.getMinX() - rect.getMaxX();
        if (ahead < -20.0f || ahead > 300.0f) return;

        if (isRing(type)) {
            if (rect.getMaxY() >= playerRect.getMinY() - 30.0f &&
                rect.getMinY() <= playerRect.getMaxY() + 30.0f)
                nearby.ring = std::min(nearby.ring, std::max(0.0f, ahead));
            return;
        }

        const bool hazard = type == GameObjectType::Hazard ||
                            type == GameObjectType::AnimatedHazard;
        const bool solid = type == GameObjectType::Solid ||
                           type == GameObjectType::Breakable;
        if (!hazard && !solid) return;

        const bool verticalThreat =
            hazard
                ? rect.getMaxY() >= playerRect.getMinY() - 8.0f &&
                      rect.getMinY() <= playerRect.getMaxY() + 8.0f
                : (!player->m_isUpsideDown
                       ? rect.getMaxY() > playerRect.getMinY() + 4.0f &&
                             rect.getMinY() <= playerRect.getMaxY() + 8.0f
                       : rect.getMinY() < playerRect.getMaxY() - 4.0f &&
                             rect.getMaxY() >= playerRect.getMinY() - 8.0f);
        if (verticalThreat) {
            auto& distance = hazard ? nearby.hazard : nearby.block;
            distance = std::min(distance, std::max(0.0f, ahead));
        }

        if (ahead <= emergencyAhead) {
            if (rect.getMaxY() <= centerY)
                nearby.lowerClearance = std::min(
                    nearby.lowerClearance,
                    std::max(0.0f, playerRect.getMinY() - rect.getMaxY()));
            if (rect.getMinY() >= centerY)
                nearby.upperClearance = std::min(
                    nearby.upperClearance,
                    std::max(0.0f, rect.getMinY() - playerRect.getMaxY()));
        }
    });
    return nearby;
}

uint64_t mixKey(uint64_t key, uint64_t value) {
    return key ^ (value + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2));
}

uint64_t doomKey(PlayerObject* player) {
    const auto bucket = [](double value, double size) {
        return static_cast<uint64_t>(static_cast<int64_t>(std::floor(value / size)));
    };
    const auto position = player->getPosition();
    uint64_t key = mixKey(0x51ed2701f3a5c1b9ull, bucket(position.x, 10.0));
    key = mixKey(key, bucket(position.y, 8.0));
    key = mixKey(key, bucket(player->m_yVelocity, 1.0));
    const uint64_t flags =
        (player->m_isShip ? 1u : 0u) | (player->m_isBird ? 2u : 0u) |
        (player->m_isBall ? 4u : 0u) | (player->m_isDart ? 8u : 0u) |
        (player->m_isRobot ? 16u : 0u) | (player->m_isSpider ? 32u : 0u) |
        (player->m_isSwing ? 64u : 0u) | (player->m_isUpsideDown ? 128u : 0u) |
        (player->m_vehicleSize != 1.0f ? 256u : 0u) |
        (holdingJump(player) ? 512u : 0u) | (player->m_isGoingLeft ? 1024u : 0u);
    return mixKey(key, flags);
}

constexpr double FORESIGHT_SECONDS = 0.6;
constexpr double HISTORY_SECONDS = 1.5;
constexpr int FORESIGHT_BUDGET = 64;
constexpr int FORESIGHT_BEAM = 4;

struct Candidate {
    FlipPlan m_flips{};
    int m_score = 0;
};

Candidate searchPlan(Trajectory* traj, PlayLayer* pl, bool p1, int mode,
                     int horizon, int baselineScore) {
    Candidate best{};
    best.m_score = baselineScore;

    int budget = MAX_SIMULATIONS;
    const auto evaluate = [&](FlipPlan const& flips) {
        --budget;
        return traj
            ->simulate(pl, p1, mode, false,
                       {
                           .m_bypassConfig = true,
                           .m_draw = false,
                           .m_flipPlan = flips,
                       })
            .score;
    };

    const auto consider = [&](Candidate const& candidate) {
        if (betterPlan(candidate.m_score, candidate.m_flips.firstStep(),
                       candidate.m_flips.size(), best.m_score,
                       best.m_flips.firstStep(), best.m_flips.size()))
            best = candidate;
    };

    std::array<Candidate, FLIP_OFFSETS.size()> firstPly{};
    int firstPlyCount = 0;

    for (int offset : FLIP_OFFSETS) {
        if (offset >= horizon || budget <= 0) break;
        Candidate candidate;
        candidate.m_flips.push(offset);
        candidate.m_score = evaluate(candidate.m_flips);
        consider(candidate);
        firstPly[firstPlyCount++] = candidate;
    }

    if (firstPlyCount == 0) return best;

    const int survivors = std::min(BEAM_WIDTH, firstPlyCount);
    std::partial_sort(
        firstPly.begin(), firstPly.begin() + survivors,
        firstPly.begin() + firstPlyCount,
        [](Candidate const& a, Candidate const& b) {
            return betterPlan(a.m_score, a.m_flips.firstStep(),
                              a.m_flips.size(), b.m_score,
                              b.m_flips.firstStep(), b.m_flips.size());
        });

    for (int i = 0; i < survivors && budget > 0; ++i) {
        const int firstStep = firstPly[i].m_flips.firstStep();
        for (int offset : FLIP_OFFSETS) {
            if (budget <= 0) break;
            if (offset <= firstStep + 1 || offset >= horizon) continue;

            Candidate candidate = firstPly[i];
            if (!candidate.m_flips.push(offset)) continue;
            candidate.m_score = evaluate(candidate.m_flips);
            consider(candidate);
        }
    }

    return best;
}

}

void Pathfinder::decide(PlayLayer* pl, Trajectory* traj, bool p1,
                        bool& clicked) {
    using Mode = Trajectory::TrajectoryMode;
    PlayerObject* player = p1 ? pl->m_player1 : pl->m_player2;
    if (!player || player->m_isDead) return;

    const uint64_t frame = GrapeEngine::get()->timeline().getFrame();
    auto& lastToggle = p1 ? m_p1LastToggle : m_p2LastToggle;
    const bool actualHolding = holdingJump(player);
    if (clicked != actualHolding) {
        clicked = actualHolding;
        lastToggle = frame;
    }

    const int who = p1 ? Mode::Player1 : Mode::Player2;
    const bool flying = player->m_isShip || player->m_isBird ||
                        player->m_isDart || player->m_isSwing;
    const bool groundJump = !(flying || player->m_isBall ||
                              player->m_isSpider);

    auto& ts = GrapeSettings::get()->trajectory;
    const double savedLength = ts.length;
    const int savedMaxSteps = ts.maxSteps;
    ts.length = .35;
    ts.maxSteps = 0;
    const int horizon = std::max(1, traj->getPredictionLength(pl));
    const auto predict = [&](bool hold) {
        const int mode = who | (hold ? Mode::Hold : Mode::Release);
        return traj->simulate(pl, p1, mode, false,
                              {.m_bypassConfig = true});
    };

    const auto nearby = inspectNearby(pl, player);
    const auto holdPrediction = predict(true);
    const auto releasePrediction = predict(false);
    const int holdScore = holdPrediction.score;
    const int releaseScore = releasePrediction.score;
    const int currentScore = clicked ? holdScore : releaseScore;
    const double tps = std::max(1.0, GrapeEngine::get()->timeline().getTps());
    const double reactionTime = flying ? .08 : (player->m_isRobot ? .20 : .16);
    const int reactionFrames = std::min(
        horizon,
        std::clamp(static_cast<int>(std::lround(tps * reactionTime)), 6, 48));
    const float hazardDistance =
        std::abs(static_cast<float>(player->m_playerSpeed)) *
            static_cast<float>(reactionTime) + 8.0f;
    const float blockDistance =
        FrameEngine::playerSpeedUnits(player->m_playerSpeed) *
            static_cast<float>(reactionTime) + 8.0f;
    const grape::assist::Prediction prediction{holdScore, releaseScore, horizon};
    const grape::assist::Proximity proximity{
        nearby.hazard, nearby.block, nearby.ring,
        nearby.lowerClearance, nearby.upperClearance};
    const grape::assist::Situation situation{
        clicked,     groundJump,     flying, static_cast<bool>(player->m_isUpsideDown),
        reactionFrames, hazardDistance, blockDistance};
    const auto reactive =
        grape::assist::chooseInput(prediction, proximity, situation);

    bool wantHold = clicked;
    if (lastToggle == UINT64_MAX) lastToggle = frame;
    const int minimumFrames = m_proportional
        ? timingFrames(std::max(2, m_cycleFrames),
                       clicked ? m_holdPct : m_releasePct)
        : (clicked && player->m_isRobot
               ? std::clamp(static_cast<int>(std::lround(tps * .12)), 8, 36)
               : (clicked && groundJump ? 2 : 1));

    Plan& plan = p1 ? m_p1Plan : m_p2Plan;
    bool decided = false;

    if (plan.active(frame)) {
        if (currentScore >= horizon) {
            plan.clear();
        } else {
            if (plan.dueAt(frame)) {
                wantHold = !clicked;
                ++plan.m_next;
            }
            decided = true;
        }
    }

    const bool threatened = currentScore < horizon &&
                            currentScore <= reactionFrames * 2;

    if (!decided && threatened) {
        const int searchMode = who | (clicked ? Mode::Hold : Mode::Release);
        const Candidate found =
            searchPlan(traj, pl, p1, searchMode, horizon, currentScore);

        if (found.m_flips.size() > 0 && found.m_score > currentScore) {
            plan.clear();
            for (int i = 0; i < found.m_flips.size() &&
                            plan.m_count < Plan::MAX_TOGGLES;
                 ++i)
                plan.m_frames[plan.m_count++] =
                    frame + static_cast<uint64_t>(found.m_flips.m_steps[i]);
            plan.m_expiresAt = frame + static_cast<uint64_t>(horizon);

            if (plan.dueAt(frame)) {
                wantHold = !clicked;
                ++plan.m_next;
            }
            decided = true;
        }
    }

    if (decided) {
    } else if (reactive.m_choice != grape::assist::Choice::Keep) {
        wantHold = reactive.m_choice == grape::assist::Choice::Hold;
    }
    else if (player->m_isShip && holdScore == releaseScore &&
             frame - lastToggle >= static_cast<uint64_t>(minimumFrames)) {
        const double velocity = player->m_yVelocity;
        if ((!player->m_isUpsideDown && velocity < -1.0) ||
            (player->m_isUpsideDown && velocity > 1.0))
            wantHold = true;
        else if ((!player->m_isUpsideDown && velocity > 1.0) ||
                 (player->m_isUpsideDown && velocity < -1.0))
            wantHold = false;
    }
    else if (clicked && groundJump &&
             frame - lastToggle >= static_cast<uint64_t>(minimumFrames) &&
             releaseScore >= holdScore && nearby.ring > 45.0f)
        wantHold = false;

    ts.length = savedLength;
    ts.maxSteps = savedMaxSteps;

    if (wantHold != clicked) {
        auto& lastToggle = p1 ? m_p1LastToggle : m_p2LastToggle;
        auto& swiftAcc = p1 ? m_p1SwiftAcc : m_p2SwiftAcc;
        clicked = wantHold;
        pl->queueButton(1, clicked, !p1, 0.0);
        lastToggle = frame;
        if (clicked && groundJump && nearby.ring <= 1.0f &&
            m_proportional && m_swiftPct > 0.0f) {
            swiftAcc += m_swiftPct;
            if (swiftAcc >= 100.0f) {
                swiftAcc -= 100.0f;
                clicked = false;
                pl->queueButton(1, false, !p1, 0.0);
            }
        }
    }
}

bool Pathfinder::foresightActive() const {
    return m_enabled->inner() && m_foresight->inner() &&
           GrapeEngine::get()->isRecording();
}

bool Pathfinder::wantsHistory() const { return foresightActive(); }

int Pathfinder::rewindableFrames() const {
    auto* bot = GrapeEngine::get();
    if (bot->timeline().isPaused()) return 0;
    const auto& keeper = bot->keeper();
    const uint64_t floor = keeper.m_saved.empty() ? 0 : keeper.m_saved.back().m_clock.m_frame;
    int count = 0;
    for (auto it = keeper.m_history.rbegin(); it != keeper.m_history.rend(); ++it) {
        if (it->m_clock.m_frame < floor) break;
        ++count;
    }
    return count;
}

void Pathfinder::decideForesight(PlayLayer* pl, Trajectory* traj, bool p1,
                                 bool& clicked) {
    using Mode = Trajectory::TrajectoryMode;
    namespace fs = grape::assist::foresight;
    PlayerObject* player = p1 ? pl->m_player1 : pl->m_player2;
    if (!player || player->m_isDead) return;

    auto& timeline = GrapeEngine::get()->timeline();
    const uint64_t frame = timeline.getFrame();
    auto& lastToggle = p1 ? m_p1LastToggle : m_p2LastToggle;
    const bool actualHolding = holdingJump(player);
    if (clicked != actualHolding) {
        clicked = actualHolding;
        lastToggle = frame;
    }

    auto& controller = p1 ? m_p1Foresight : m_p2Foresight;
    const double tps = std::max(1.0, timeline.getTps());

    auto& ts = GrapeSettings::get()->trajectory;
    const double savedLength = ts.length;
    const int savedMaxSteps = ts.maxSteps;
    ts.length = FORESIGHT_SECONDS;
    ts.maxSteps = 0;
    const int horizon = std::max(8, traj->getPredictionLength(pl));

    const int who = p1 ? Mode::Player1 : Mode::Player2;
    const int mode = who | (clicked ? Mode::Hold : Mode::Release);
    const auto doomed = [&controller](int, PlayerObject* fake) {
        return controller.doomed(doomKey(fake));
    };
    const auto simulate = [&](const fs::Schedule& plan) {
        FlipPlan flips;
        for (int i = 0; i < plan.size(); ++i) flips.push(plan.m_steps[i]);
        const auto result = traj->simulate(pl, p1, mode, false,
                                           {.m_bypassConfig = true,
                                            .m_maxLength = horizon,
                                            .m_draw = false,
                                            .m_flipPlan = flips,
                                            .m_doomed = doomed});
        return fs::Outcome{result.score, result.dead};
    };

    const int history = rewindableFrames();
    const int rewindBase = std::clamp(static_cast<int>(std::lround(tps * 0.08)), 8, 60);
    const fs::Limits limits{horizon, FORESIGHT_BUDGET, FORESIGHT_BEAM, horizon / 2};
    const auto step = controller.step(frame, simulate, limits, history, rewindBase,
                                      doomKey(player));

    ts.length = savedLength;
    ts.maxSteps = savedMaxSteps;

    if (step.m_rewind > 0) {
        if (step.m_rewind > m_pendingRewind) {
            m_pendingRewind = step.m_rewind;
            m_pendingDeadEnd = step.m_deadEnd;
            m_pendingFromP1 = p1;
        }
        return;
    }
    if (step.m_toggle) {
        clicked = !clicked;
        pl->queueButton(1, clicked, !p1, 0.0);
        lastToggle = frame;
    }
}

void Pathfinder::performRewind(PlayLayer* pl) {
    auto* bot = GrapeEngine::get();
    auto& keeper = bot->keeper();
    const int depth = m_pendingRewind;
    m_pendingRewind = 0;
    const int allowed = rewindableFrames();
    if (allowed < 2 || keeper.m_history.size() < 2) return;

    const size_t toDrop = std::min<size_t>(static_cast<size_t>(std::max(depth - 1, 0)),
                                           static_cast<size_t>(allowed - 1));
    for (size_t i = 0; i < toDrop; ++i) keeper.dropNewest();

    m_rewinding = true;
    keeper.m_backstep = true;
    keeper.m_pendingRestore = true;
    pl->resetLevel();
    keeper.m_pendingRestore = false;
    m_rewinding = false;

    const uint64_t restored = bot->timeline().getFrame();
    auto& replay = bot->replay();
    replay.m_inputs.clipFrom(restored);
    replay.rebindHead();
    replay.m_head.setPosition(replay.m_inputs.length());

    auto& origin = m_pendingFromP1 ? m_p1Foresight : m_p2Foresight;
    auto& other = m_pendingFromP1 ? m_p2Foresight : m_p1Foresight;
    origin.rewound(restored, m_pendingDeadEnd);
    other.rewound(restored, restored);
    m_lastFrame = UINT64_MAX;
    ++m_rewinds;
}

void Pathfinder::update(PlayLayer* pl) {
    if (!pl || !m_enabled->inner()) return;

    auto bot = GrapeEngine::get();
    if (pl != m_levelLayer || (pl->m_level && pl->m_level->m_levelID.value() != m_levelId)) {
        m_levelLayer = pl;
        m_levelId = pl->m_level ? pl->m_level->m_levelID.value() : 0;
        m_p1Foresight.forget();
        m_p2Foresight.forget();
        m_rewinds = 0;
    }
    bot->keeper().m_minimumHistory =
        foresightActive()
            ? static_cast<size_t>(std::max(120.0, bot->timeline().getTps() * HISTORY_SECONDS))
            : 0;

    if (!bot->isRecording()) return;
    if (!pl->m_player1) return;

    const bool dead = pl->m_player1->m_isDead ||
                      (pl->m_player2 && pl->m_player2->m_isDead);
    if (dead) {
        if (!m_awaitingRespawn) {
            m_awaitingRespawn = true;
            pl->resetLevel();
        }
        return;
    }
    m_awaitingRespawn = false;

    auto traj = bot->trajectory().unsafeInner();
    if (!traj) return;

    const uint64_t frame = bot->timeline().getFrame();
    if (frame == m_lastFrame) return;
    m_lastFrame = frame;

    if (foresightActive()) {
        decideForesight(pl, traj, true, m_p1Clicked);
        if (pl->m_gameState.m_isDualMode && pl->m_player2)
            decideForesight(pl, traj, false, m_p2Clicked);
        if (m_pendingRewind > 0) performRewind(pl);
        return;
    }

    decide(pl, traj, true, m_p1Clicked);
    if (pl->m_gameState.m_isDualMode && pl->m_player2)
        decide(pl, traj, false, m_p2Clicked);
}

void Pathfinder::onDeath(PlayLayer*, uint64_t deathFrame) {
    if (!m_enabled->inner() || m_rewinding) return;

    constexpr uint64_t tolerance = 30;
    const uint64_t delta =
        m_lastDeathFrame == UINT64_MAX
            ? UINT64_MAX
            : (deathFrame > m_lastDeathFrame ? deathFrame - m_lastDeathFrame
                                             : m_lastDeathFrame - deathFrame);
    if (delta <= tolerance)
        m_consecutiveDeaths++;
    else
        m_consecutiveDeaths = 1;
    m_lastDeathFrame = deathFrame;

    if (m_consecutiveDeaths >= std::max(1, m_stuckDeaths)) {
        GrapeEngine::get()->keeper().forgetLatest();
        m_consecutiveDeaths = 0;
        m_lastDeathFrame = UINT64_MAX;
    }

    m_p1Clicked = false;
    m_p2Clicked = false;
    m_p1LastToggle = UINT64_MAX;
    m_p2LastToggle = UINT64_MAX;
    m_p1SwiftAcc = 0.0f;
    m_p2SwiftAcc = 0.0f;
    m_lastFrame = UINT64_MAX;
    m_p1Plan.clear();
    m_p2Plan.clear();
    m_p1Foresight.reset();
    m_p2Foresight.reset();
    m_pendingRewind = 0;
}
