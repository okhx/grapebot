#include "autoclicker.hpp"

#include "assist/gapsteer.hpp"
#include "assist/ufogap.hpp"
#include "assist/nearby_objects.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "vine/session.hpp"

#include <limits>
#include <optional>
#include <string_view>

namespace {

void applyClick(PlayLayer* pl, bool pressed, bool player2) {
#ifdef GEODE_IS_MOBILE
    pl->handleButton(pressed, 1, !player2);
#else
    pl->queueButton(1, pressed, player2, 0.0);
#endif
}

constexpr float distanceAhead(float x, float minimum, float maximum,
                              float direction) {
    if ((direction > 0 && maximum < x - 20.0f) ||
        (direction < 0 && minimum > x + 20.0f))
        return -1.0f;
    return direction > 0 ? std::max(0.0f, minimum - x)
                         : std::max(0.0f, x - maximum);
}

constexpr bool intervalElapsed(uint64_t frame, uint64_t last, int interval) {
    return last == UINT64_MAX || frame < last ||
           frame - last >= static_cast<uint64_t>(interval);
}

static_assert(distanceAhead(100, 50, 150, 1) == 0);
static_assert(distanceAhead(100, 130, 150, 1) == 30);
static_assert(intervalElapsed(11, 10, 1));
static_assert(!intervalElapsed(11, 10, 2));

bool holdingJump(PlayerObject* player) {
    if (!player) return false;
    const auto held = player->m_holdingButtons.find(
        static_cast<int>(PlayerButton::Jump));
    return held != player->m_holdingButtons.end() && held->second;
}

bool touchingRing(PlayerObject* player) {
    return player && player->m_touchingRings &&
           player->m_touchingRings->count() != 0;
}

constexpr int BLUE_RING_ID = 84;

grape::assist::OrbContact orbContact(PlayerObject* player) {
    grape::assist::OrbContact contact{};
    if (!player || !player->m_touchingRings) return contact;
    auto* rings = player->m_touchingRings;
    for (unsigned int i = 0; i < rings->count(); ++i) {
        auto* ring = static_cast<RingObject*>(rings->objectAtIndex(i));
        if (!ring || ring->hasBeenActivatedByPlayer(player)) continue;
        ++contact.m_pending;
        if (ring->m_objectType == GameObjectType::DropRing) ++contact.m_black;
        else if (ring->m_objectID == BLUE_RING_ID) contact.m_blue = true;
    }
    return contact;
}

struct GapBounds {
    float center;
    float lower;
    float upper;
};

std::optional<GapBounds> movingGap(PlayLayer* layer, PlayerObject* player,
                                   float referenceY) {
    const float x = player->getPositionX();
    const float direction = player->m_isGoingLeft ? -1.0f : 1.0f;
    float lowerScore = std::numeric_limits<float>::max();
    float upperScore = std::numeric_limits<float>::max();
    float lower = 0.0f;
    float upper = 0.0f;
    grape::assist::forEachNearbyObject(layer, [&](GameObject* object) {
        if (object->m_objectType != GameObjectType::Solid &&
            object->m_objectType != GameObjectType::Breakable &&
            object->m_objectType != GameObjectType::Hazard &&
            object->m_objectType != GameObjectType::AnimatedHazard)
            return;
        const auto rect = object->getObjectRect();
        const float ahead = distanceAhead(
            x, rect.getMinX(), rect.getMaxX(), direction);
        if (ahead < 0.0f || ahead > 180.0f) return;
        if (rect.getMaxY() <= referenceY + 1.5f) {
            const float score = std::max(0.0f, referenceY - rect.getMaxY()) +
                                ahead * .1f;
            if (score >= lowerScore) return;
            lowerScore = score;
            lower = rect.getMaxY();
        } else if (rect.getMinY() >= referenceY - 1.5f) {
            const float score = std::max(0.0f, rect.getMinY() - referenceY) +
                                ahead * .1f;
            if (score >= upperScore) return;
            upperScore = score;
            upper = rect.getMinY();
        }
    });
    if (lowerScore == std::numeric_limits<float>::max() ||
        upperScore == std::numeric_limits<float>::max())
        return std::nullopt;
    if (upper <= lower) {
        const float mid = (lower + upper) * .5f;
        return GapBounds{mid, mid, mid};
    }
    return GapBounds{(lower + upper) * .5f, lower, upper};
}

}

void Autoclicker::update(PlayLayer* pl) {
    if (!pl) {
        m_p1Clicked = false;
        m_p2Clicked = false;
        return;
    }

    auto bot = GrapeEngine::get();

    if (!m_enabled->inner()) {
        if (m_p1Clicked) {
            applyClick(pl, false, false);
            m_p1Clicked = false;
        }
        if (m_p2Clicked) {
            applyClick(pl, false, true);
            m_p2Clicked = false;
        }
        return;
    }

    auto frame = bot->timeline().getFrame();
    const bool independentPlayer2 = pl->m_gameState.m_isDualMode &&
                                    pl->m_player2;
    const bool doPlayer1 = !independentPlayer2 || performPlayer1();
    const bool doPlayer2 = independentPlayer2 && performPlayer2();
    if (!doPlayer1 && m_p1Clicked) {
        applyClick(pl, false, false);
        m_p1Clicked = false;
    }
    if (!doPlayer2 && m_p2Clicked) {
        applyClick(pl, false, true);
        m_p2Clicked = false;
    }

    if (frame == m_lastFrame) {
        return;
    }

    if (!bot->isRecording()) {
        if (m_p1Clicked)
            applyClick(pl, false, false);
        if (m_p2Clicked)
            applyClick(pl, false, true);
        m_p1Clicked = false;
        m_p2Clicked = false;
        return;
    }

    const auto apply = [&](bool player2, bool& pressed,
                           std::optional<bool> desired) {
        if (!desired || *desired == pressed) return false;
        pressed = *desired;
        applyClick(pl, pressed, player2);
        return true;
    };

    if (m_movingGap) {
        const auto steered = [](PlayerObject* player) {
            return player && (player->m_isDart || player->m_isBird);
        };
        if (doPlayer1 && steered(pl->m_player1))
            m_p1Clicked = holdingJump(pl->m_player1);
        if (doPlayer2 && steered(pl->m_player2))
            m_p2Clicked = holdingJump(pl->m_player2);
    }
    const auto trackGap = [&](PlayerObject* player, auto& state) {
        if (!m_movingGap || !player || player->m_isDead) {
            if (player && player->m_isDead && state.traceCount > 0) {
                geode::log::info("UFO assist: last {} frames before death "
                                 "(frame centre velocity line orbs press reason)",
                                 state.traceCount);
                const int count = state.traceCount;
                const int size = static_cast<int>(state.trace.size());
                for (int i = std::max(0, count - size); i < count; ++i) {
                    const auto& entry = state.trace[i % size];
                    geode::log::info("  {} {:.1f} {:.2f} {:.1f} {} {} {}", entry.frame,
                                     entry.center, entry.velocity, entry.line,
                                     entry.black, entry.press, entry.reason);
                }
            }
            state = {};
            return;
        }
        const int elapsed = state.frame == UINT64_MAX || frame <= state.frame
            ? 1 : static_cast<int>(std::min<uint64_t>(frame - state.frame, 30));
        const float predictedY = player->getPositionY() +
            std::clamp(static_cast<float>(player->m_yVelocity * .03),
                       -20.0f, 20.0f);
        auto& tracker = state.tracker;
        const float referenceY = tracker.m_valid
            ? tracker.m_center + grape::assist::futureGapOffset(
                  tracker.m_velocity, tracker.m_acceleration,
                  static_cast<float>(elapsed))
            : predictedY;

        auto current = movingGap(pl, player, referenceY);
        if (!current && tracker.m_valid) {
            current = movingGap(
                pl, player,
                tracker.m_center + grape::assist::futureGapOffset(
                    tracker.m_velocity, tracker.m_acceleration,
                    static_cast<float>(elapsed + 2)));
        }
        if (!current) current = movingGap(pl, player, predictedY);

        if (current) {
            tracker.observe(current->lower, current->upper, elapsed);
        } else {
            tracker.coast(elapsed);
        }

        const float y = player->getPositionY();
        const float travel = state.hasLastY ? (y - state.lastY) / elapsed
                                            : 0.0f;
        if (state.hasLastY && player->m_isBird && !player->m_isOnGround &&
            !player->m_isDashing) {
            const bool credited =
                state.lastPress != UINT64_MAX && frame > state.lastPress &&
                frame - state.lastPress <= grape::assist::UFO_PRESS_CREDIT;
            state.ufo.observe(travel, static_cast<float>(player->m_yVelocity),
                              state.lastTravel, credited,
                              credited && state.lastPressOnOrb);
        }
        state.lastY = y;
        state.lastTravel = travel;
        state.hasLastY = true;
        state.frame = frame;
    };
    if (doPlayer1) trackGap(pl->m_player1, m_p1Gap);
    if (doPlayer2) trackGap(pl->m_player2, m_p2Gap);

    const auto ufoSteer = [&](PlayerObject* player, auto& gap)
        -> std::optional<grape::assist::UfoPlan> {
        if (!m_movingGap || !player || player->m_isDead) return std::nullopt;
        if (!player->m_isBird) {
            gap.holdingLine = false;
            return std::nullopt;
        }
        const auto hitbox = player->getObjectRect();
        auto contact = orbContact(player);
        gap.touchingOrb = contact.m_pending > 0;
        if (contact.m_black > 0) gap.lastBlackOrb = frame;
        const auto framesSince = [&](uint64_t then, int none) {
            return then == UINT64_MAX || frame < then
                ? none
                : static_cast<int>(std::min<uint64_t>(frame - then, 1000));
        };
        contact.m_sinceBlack = framesSince(gap.lastBlackOrb, 1000);
        const int sincePress = framesSince(gap.lastPress, 1000);
        const auto record = [&](const grape::assist::UfoPlan& plan, float velocity) {
            auto& entry = gap.trace[gap.traceCount++ % gap.trace.size()];
            entry = {frame, static_cast<float>(hitbox.getMidY()), velocity,
                     plan.m_flapHeight, contact.m_black, plan.presses(), plan.m_reason};
            return plan;
        };

        if (grape::assist::inBlackOrbSection(contact) && !contact.m_blue) {
            if (!gap.holdingLine) {
                gap.heldLine = hitbox.getMidY();
                gap.holdingLine = true;
                gap.anchored = false;
            }
            if (gap.tracker.m_valid && !gap.anchored) {
                gap.gapAnchor = grape::assist::shiftGap(gap.tracker.state(), 1.0f).center();
                gap.anchored = true;
            }
            contact.m_holdLine = true;
            contact.m_line = gap.tracker.m_valid
                ? grape::assist::followGapLine(gap.heldLine, gap.gapAnchor,
                                               gap.tracker.state())
                : gap.heldLine;
            const float velocity = static_cast<float>(player->m_yVelocity);
            const grape::assist::UfoState ufo{hitbox.getMinY(), hitbox.getMaxY(),
                                              velocity};
            if (gap.tracker.m_valid && gap.ufo.ready())
                contact.m_line = grape::assist::clampBlackOrbLine(
                    contact.m_line, gap.tracker.state(), ufo.height() * .5f,
                    gap.ufo.physics(static_cast<bool>(player->m_isUpsideDown),
                                    gap.ufo.terminal()));
            return record(grape::assist::holdStraightOnBlackOrbs(
                              ufo, player->m_isUpsideDown ? 1.0f : -1.0f, contact,
                              {sincePress, gap.lastPressUp}),
                          velocity);
        }
        gap.holdingLine = false;
        if (!gap.ufo.ready() || !gap.tracker.m_valid) return std::nullopt;

        const auto physics = gap.ufo.physics(
            static_cast<bool>(player->m_isUpsideDown), gap.ufo.terminal());
        const float velocity =
            gap.ufo.m_scale > 0.0f
                ? static_cast<float>(player->m_yVelocity) * gap.ufo.m_scale
                : gap.lastTravel;
        const grape::assist::UfoState ufo{hitbox.getMinY(), hitbox.getMaxY(),
                                          velocity};
        return record(grape::assist::steerUfoForGap(gap.tracker.state(), ufo,
                                                    physics, contact, sincePress),
                      velocity);
    };

    const auto sendUfoPress = [&](bool player2, bool& pressed, bool press) {
        if (pressed) {
            applyClick(pl, false, player2);
            pressed = false;
        }
        if (!press) return;
        applyClick(pl, true, player2);
        pressed = true;
    };

    const auto adaptive = [&](PlayerObject* player, bool pressed,
                              const auto& gap) -> std::optional<bool> {
        if (!m_movingGap || !player || player->m_isDead)
            return std::nullopt;
        if (!player->m_isDart) return std::nullopt;
        if (!gap.tracker.m_valid) return std::nullopt;

        const auto hitbox = player->getObjectRect();
        const grape::assist::GapState corridor = gap.tracker.state();
        const grape::assist::WaveState wave{
            hitbox.getMinY(), hitbox.getMaxY(),
            std::clamp(static_cast<float>(player->m_yVelocity * .03), -20.0f,
                       20.0f),
            grape::assist::climbRateOf(player->m_yVelocity),
            static_cast<bool>(player->m_isUpsideDown)};

        const auto steer =
            grape::assist::steerForGap(corridor, wave, m_movingGapLookahead);

        if (!steer.m_emergency && touchingRing(player)) return pressed;

        switch (steer.m_action) {
            case grape::assist::Steer::Up:
                return !player->m_isUpsideDown;
            case grape::assist::Steer::Down:
                return static_cast<bool>(player->m_isUpsideDown);
            case grape::assist::Steer::Dither:
                break;
        }
        return !pressed;
    };
    const auto p1Ufo = doPlayer1 ? ufoSteer(pl->m_player1, m_p1Gap)
                                 : std::nullopt;
    const auto p2Ufo = doPlayer2 ? ufoSteer(pl->m_player2, m_p2Gap)
                                 : std::nullopt;
    const auto pressUfo = [&](bool player2, bool& pressed, GapState& gap,
                              const std::optional<grape::assist::UfoPlan>& plan) {
        if (!plan) return;
        sendUfoPress(player2, pressed, plan->presses());
        if (!plan->presses()) return;
        gap.lastPress = frame;
        gap.lastPressOnOrb = gap.touchingOrb;
        gap.lastPressUp = std::string_view(plan->m_reason) != "black";
    };
    pressUfo(false, m_p1Clicked, m_p1Gap, p1Ufo);
    pressUfo(true, m_p2Clicked, m_p2Gap, p2Ufo);

    const auto p1Adaptive = doPlayer1 && !p1Ufo
        ? adaptive(pl->m_player1, m_p1Clicked, m_p1Gap)
        : std::nullopt;
    const auto p2Adaptive = doPlayer2 && !p2Ufo
        ? adaptive(pl->m_player2, m_p2Clicked, m_p2Gap)
        : std::nullopt;
    if (p1Adaptive)
        apply(false, m_p1Clicked, p1Adaptive);
    if (p2Adaptive)
        apply(true, m_p2Clicked, p2Adaptive);
    if ((!doPlayer1 || p1Adaptive || p1Ufo) &&
        (!doPlayer2 || p2Adaptive || p2Ufo)) {
        m_lastFrame = frame;
        return;
    }

    const auto due = [&](bool pressed, uint64_t lastToggle, bool player2) {
        return intervalElapsed(
            frame, lastToggle,
            pressed ? holdFrames(player2) : releaseFrames(player2));
    };
    if (doPlayer1 && !p1Adaptive && !p1Ufo &&
        due(m_p1Clicked, m_p1LastToggle, false)) {
        m_p1LastToggle = frame;
        if (m_p1Clicked) {
            m_p1Clicked = false;
            applyClick(pl, false, false);
        } else if (clicksPerTick(false) > 1) {
            for (int i = 0; i < clicksPerTick(false); ++i) {
                applyClick(pl, true, false);
                applyClick(pl, false, false);
            }
        } else {
            applyClick(pl, true, false);
            m_p1Clicked = true;
            if (m_performSwifts) {
                m_p1Clicked = false;
                applyClick(pl, false, false);
            }
        }
    }

    if (doPlayer2 && !p2Adaptive && !p2Ufo &&
        due(m_p2Clicked, m_p2LastToggle, true)) {
        m_p2LastToggle = frame;
        if (m_p2Clicked) {
            m_p2Clicked = false;
            applyClick(pl, false, true);
        } else if (clicksPerTick(true) > 1) {
            for (int i = 0; i < clicksPerTick(true); ++i) {
                applyClick(pl, true, true);
                applyClick(pl, false, true);
            }
        } else {
            applyClick(pl, true, true);
            m_p2Clicked = true;
            if (m_performSwifts) {
                m_p2Clicked = false;
                applyClick(pl, false, true);
            }
        }
    }
    m_lastFrame = frame;
}
