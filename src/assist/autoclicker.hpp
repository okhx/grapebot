#pragma once
#include <Geode/Geode.hpp>

#include <array>
#include <limits>

#include "../config/config.hpp"
#include "gapsteer.hpp"
#include "ufogap.hpp"
#include "../shared/value/value.hpp"

using namespace geode::prelude;

class Autoclicker {
   public:
    enum class PlayerToggle : int {
        Player1,
        Player2,
        Both,
    };

   private:
    struct UfoTrace {
        uint64_t frame = 0;
        float center = 0.0f;
        float velocity = 0.0f;
        float line = 0.0f;
        int black = 0;
        bool press = false;
        const char* reason = "";
    };

    struct GapState {
        grape::assist::GapTracker tracker;
        grape::assist::UfoPhysicsTracker ufo;
        float lastY = 0.0f;
        float lastTravel = 0.0f;
        bool hasLastY = false;
        bool touchingOrb = false;
        bool lastPressOnOrb = false;
        bool lastPressUp = true;
        uint64_t lastPress = UINT64_MAX;
        uint64_t lastBlackOrb = UINT64_MAX;
        uint64_t frame = UINT64_MAX;
        bool holdingLine = false;
        float heldLine = 0.0f;
        bool anchored = false;
        float gapAnchor = 0.0f;
        std::array<UfoTrace, 24> trace{};
        int traceCount = 0;
    };

    uint64_t m_lastFrame = UINT64_MAX;
    uint64_t m_p1LastToggle = UINT64_MAX;
    uint64_t m_p2LastToggle = UINT64_MAX;
    bool m_p1Clicked = false;
    bool m_p2Clicked = false;
    GapState m_p1Gap;
    GapState m_p2Gap;
   public:
    int m_holdFrames = GrapeSettings::get()->autoclickerHoldFrames;
    int m_releaseFrames = GrapeSettings::get()->autoclickerReleaseFrames;
    int m_p1ClicksPerTick = GrapeSettings::get()->autoclickerP1ClicksPerTick;
    int m_p2HoldFrames = GrapeSettings::get()->autoclickerP2HoldFrames;
    int m_p2ReleaseFrames = GrapeSettings::get()->autoclickerP2ReleaseFrames;
    int m_p2ClicksPerTick = GrapeSettings::get()->autoclickerP2ClicksPerTick;
    bool m_syncPlayers = GrapeSettings::get()->autoclickerSyncPlayers;
    bool m_performSwifts = GrapeSettings::get()->autoclickerSwifts;
    bool m_movingGap = GrapeSettings::get()->autoclickerMovingGap;
    int m_movingGapLookahead = std::clamp(
        GrapeSettings::get()->autoclickerMovingGapLookahead, 1, 30);

    ConfigValuePtr<bool> m_enabled = ConfigValue<bool>::create(
        "autoclicker.enabled", &GrapeSettings::get()->autoclickerEnabled);
    ConfigValuePtr<bool> m_performSwiftsValue = ConfigValue<bool>::create(
        "autoclicker.swift_clicks", &m_performSwifts);
    PlayerToggle m_player = static_cast<PlayerToggle>(
        std::clamp(GrapeSettings::get()->autoclickerPlayer, 0, 2));

    Autoclicker() {
        m_performSwiftsValue->handle([this](bool&) { saveSettings(); });
    }

    int holdFrames(bool player2) const {
        return player2 && !m_syncPlayers ? m_p2HoldFrames : m_holdFrames;
    }
    int releaseFrames(bool player2) const {
        return player2 && !m_syncPlayers ? m_p2ReleaseFrames
                                         : m_releaseFrames;
    }
    int clicksPerTick(bool player2) const {
        return player2 && !m_syncPlayers ? m_p2ClicksPerTick
                                         : m_p1ClicksPerTick;
    }

    void saveSettings() {
        m_holdFrames = std::max(1, m_holdFrames);
        m_releaseFrames = std::max(1, m_releaseFrames);
        m_p1ClicksPerTick = std::max(1, m_p1ClicksPerTick);
        m_p2HoldFrames = std::max(1, m_p2HoldFrames);
        m_p2ReleaseFrames = std::max(1, m_p2ReleaseFrames);
        m_p2ClicksPerTick = std::max(1, m_p2ClicksPerTick);
        GrapeSettings::get()->autoclickerHoldFrames = m_holdFrames;
        GrapeSettings::get()->autoclickerReleaseFrames = m_releaseFrames;
        GrapeSettings::get()->autoclickerP1ClicksPerTick = m_p1ClicksPerTick;
        GrapeSettings::get()->autoclickerP2HoldFrames = m_p2HoldFrames;
        GrapeSettings::get()->autoclickerP2ReleaseFrames = m_p2ReleaseFrames;
        GrapeSettings::get()->autoclickerP2ClicksPerTick = m_p2ClicksPerTick;
        GrapeSettings::get()->autoclickerSyncPlayers = m_syncPlayers;
        GrapeSettings::get()->autoclickerSwifts = m_performSwifts;
        GrapeSettings::get()->autoclickerMovingGap = m_movingGap;
        m_movingGapLookahead = std::clamp(m_movingGapLookahead, 1, 30);
        GrapeSettings::get()->autoclickerMovingGapLookahead =
            m_movingGapLookahead;
        GrapeSettings::get()->autoclickerPlayer = static_cast<int>(m_player);

    }

    void reset() {
        m_lastFrame = UINT64_MAX;
        m_p1LastToggle = UINT64_MAX;
        m_p2LastToggle = UINT64_MAX;
        m_p1Clicked = false;
        m_p2Clicked = false;
        m_p1Gap = {};
        m_p2Gap = {};
    }
    void update(PlayLayer* pl);
    bool performPlayer1() {
        return m_player == PlayerToggle::Player1 ||
               m_player == PlayerToggle::Both;
    }
    bool performPlayer2() {
        return m_player == PlayerToggle::Player2 ||
               m_player == PlayerToggle::Both;
    }
};
