#include <Geode/Geode.hpp>

#include <algorithm>
#include <utility>

#include "assist/cps.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "label.hpp"
#include "vine/session.hpp"

using namespace geode::prelude;

namespace grape::labels {

namespace {

constexpr int RAPID_CPS = 16;
constexpr ccColor3B WARNING_TINT{255, 128, 128};
constexpr ccColor3B NORMAL_TINT{255, 255, 255};

GrapeEngine& bot() { return *GrapeEngine::get(); }

struct Players {
    PlayerObject* m_first;
    PlayerObject* m_second;
    bool m_dual;
};

Players players() {
    auto* level = PlayLayer::get();
    return {level->m_player1, level->m_player2, level->m_gameState.m_isDualMode && level->m_player2};
}

template <class Read>
std::string bothPlayers(const char* separator, Read read) {
    const Players who = players();
    if (!who.m_dual) return read(who.m_first);
    return read(who.m_first) + separator + read(who.m_second);
}

template <class Read>
GrapeLabel::Text playerNumber(const char* title, int precision, Read read) {
    return [title, precision, read](GrapeLabel&) {
        return fmt::format("{}: {}", title, bothPlayers(" / ", [&](PlayerObject* player) {
            return fmt::format("{:.{}f}", read(player), precision);
        }));
    };
}

int yVelocityPrecision() {
    auto* unrounding = Loader::get()->getLoadedMod("chizz.velocity-unrounding");
    return unrounding && unrounding->getSettingValue<bool>("mod-enabled") ? 8 : 3;
}

const char* orbName(int objectId) {
    static constexpr std::pair<int, const char*> names[] = {
        {36, "Yellow"},  {84, "Blue"},    {141, "Pink"},        {1022, "Green"},
        {1330, "Black"}, {1333, "Red"},   {1594, "Toggle"},     {1704, "Green Dash"},
        {1751, "Pink Dash"}, {3004, "Spider"}, {3027, "Teleport"},
    };
    for (const auto& [id, name] : names)
        if (id == objectId) return name;
    return "Unknown";
}

bool holdingJump(PlayerObject* player) {
    if (!player) return false;
    if (player->m_jumpBuffered) return true;
    const auto held = player->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
    return held != player->m_holdingButtons.end() && held->second;
}

std::string orbQueue(PlayerObject* player) {
    if (!player || !player->m_touchingRings) return "None";

    bool nextOneConsumed = holdingJump(player);
    std::vector<std::pair<std::string, int>> counted;
    auto* rings = player->m_touchingRings;
    for (uint32_t i = 0; i < rings->count(); ++i) {
        auto* ring = static_cast<RingObject*>(rings->objectAtIndex(i));
        if (ring->hasBeenActivatedByPlayer(player)) continue;
        if (nextOneConsumed && !ring->m_isMultiActivate) {
            nextOneConsumed = false;
            continue;
        }

        std::string name = orbName(ring->m_objectID);
        if (ring->m_objectID == 1594) name += fmt::format("({})", ring->m_targetGroupID);
        if (ring->m_isMultiActivate) name += "*";

        auto same = std::find_if(counted.begin(), counted.end(),
                                 [&](const auto& entry) { return entry.first == name; });
        if (same == counted.end())
            counted.emplace_back(std::move(name), 1);
        else
            ++same->second;
    }
    if (counted.empty()) return "None";

    std::string text;
    for (const auto& [name, count] : counted) {
        if (!text.empty()) text += ", ";
        text += name;
        if (count > 1) text += fmt::format(" x{}", count);
    }
    return text;
}

std::string cpsLine(GrapeLabel& label, const char* title, bool max, bool split) {
    const int first = max ? bot().cps().queryMaxCPS(1) : bot().cps().queryCPS(1);
    const int second = max ? bot().cps().queryMaxCPS(2) : bot().cps().queryCPS(2);
    label.setColor(first > RAPID_CPS || second > RAPID_CPS ? WARNING_TINT : NORMAL_TINT);
    return split ? fmt::format("{}: {} / {}", title, first, second) : fmt::format("{}: {}", title, first);
}

std::string sinceLastInput() {
    const uint64_t tick = bot().timeline().getFrame();
    auto& director = bot().replay();
    const auto& events = director.m_inputs.m_events;
    const auto since = [&](const grape::vine::Event& event) {
        return fmt::format("Ticks Since Last Input: {}", tick - event.m_frame);
    };

    if (bot().isRecording()) return events.empty() ? "No Inputs In Replay" : since(events.back());
    if (director.m_inputs.length() == 0) return "No Inputs In Replay";
    const size_t played = director.m_head.position();
    return played == 0 ? "Waiting For First Input" : since(events[played - 1]);
}

std::string uprLine() {
    auto& timeline = bot().timeline();
    if (timeline.m_realTime->inner()) return "Dynamic UPR: Uncapped";
    const char* kind = timeline.m_dynamicUpr->inner() ? "Dynamic" : "Static";
    return fmt::format("{} UPR: {}", kind, timeline.m_stepLimit);
}

std::string intentionalDeathLine() {
    if (bot().isRecording())
        return fmt::format("Intentional Death: {}",
                           bot().timeline().m_canDie->inner() ? "Enabled" : "Disabled");
    return fmt::format("Intentional Death: {}",
                       bot().replay().resets().fromMacro() ? "Expects death" : "Nothing");
}

}

void registerBuiltins(LabelManager& labels) {
    const auto simple = [&](const char* id, const char* title, auto line) {
        labels.addLabel(id, title, [line](GrapeLabel&) { return std::string(line()); });
    };

    simple("frame", "Tick", [] { return fmt::format("Tick: {}", bot().timeline().getFrame()); });
    simple("internal_frame", "Internal Game Tick",
           [] { return fmt::format("Game tick: {}", PlayLayer::get()->m_gameState.m_currentProgress); });
    simple("tps", "TPS", [] { return fmt::format("TPS: {}", bot().timeline().getTps()); });

    labels.addLabel("player_x", "Player X",
                    playerNumber("X", 6, [](PlayerObject* p) { return p->m_position.x; }));
    labels.addLabel("player_y", "Player Y",
                    playerNumber("Y", 6, [](PlayerObject* p) { return p->m_position.y; }));
    labels.addLabel("player_xvel", "Player X Velocity",
                    playerNumber("X Velocity", 6, [](PlayerObject* p) { return p->m_platformerXVelocity; }));
    labels.addLabel("player_yvel", "Player Y Velocity", [](GrapeLabel& label) {
        const int digits = yVelocityPrecision();
        return playerNumber("Y Velocity", digits, [](PlayerObject* p) { return p->m_yVelocity; })(label);
    });
    labels.addLabel("player_rot", "Player Rotation",
                    playerNumber("Rotation", 3, [](PlayerObject* p) { return p->m_fRotationX; }));
    labels.addLabel("player_speed", "Player Speed",
                    playerNumber("Speed", 2, [](PlayerObject* p) { return p->m_playerSpeed; }));

    simple("player_grav", "Player Gravity", [] {
        return "Gravity: " + bothPlayers(" / ", [](PlayerObject* p) {
            return std::string(p->m_isUpsideDown ? "Flipped" : "Normal");
        });
    });
    simple("player_alive", "Player Dead/Alive", [] {
        return bothPlayers(" / ", [](PlayerObject* p) { return std::string(p->m_isDead ? "Dead" : "Alive"); });
    });

    simple("bot_state", "Bot State",
           [] { return fmt::format("Bot State: {}", bot().isPlaying() ? "Playing" : "Recording"); });
    simple("random_state", "Random Seed State",
           [] { return fmt::format("Random State: {}", bot().replay().readLiveRandom()); });
    simple("shake_state", "Random Shake State",
           [] { return fmt::format("Shake Random State: {}", bot().replay().m_rng.shake()); });
    simple("action_index", "Action Index", [] {
        auto& director = bot().replay();
        return fmt::format("Action Index: {}/{}", director.m_head.position(), director.m_inputs.length());
    });
    simple("intentional_death", "Intentional Death", intentionalDeathLine);
    simple("ssb_factor", "Scroll Speed Bug Factor",
           [] { return fmt::format("SSB Factor: {}", bot().timeline().getSSB()); });
    simple("touching_orbs", "Orb Priority", [] { return "Orb Priority: " + bothPlayers(" / ", orbQueue); });
    simple("max_upr", "Dynamic UPR", uprLine);

    labels.addLabel("cps", "CPS", [](GrapeLabel& label) {
        auto* level = GJBaseGameLayer::get();
        const bool split = level->m_gameState.m_isDualMode && level->m_levelSettings->m_twoPlayerMode;
        return cpsLine(label, "CPS", false, split);
    });
    labels.addLabel("max_cps", "Max CPS", [](GrapeLabel& label) {
        return cpsLine(label, "Max CPS", true, GJBaseGameLayer::get()->m_levelSettings->m_twoPlayerMode);
    });
    simple("last_input", "Ticks Since Last Input", sinceLastInput);
}

}
