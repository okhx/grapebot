#pragma once
#include <Geode/Geode.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

#include "../config/config.hpp"
#include "foresight.hpp"
#include "../shared/value/value.hpp"

using namespace geode::prelude;

class Trajectory;

class Pathfinder {
   public:
    ConfigValuePtr<bool> m_enabled = ConfigValue<bool>::create(
        "pathfinder.enabled", &GrapeSettings::get()->pathfinderEnabled);
    int m_stuckDeaths =
        std::max(1, GrapeSettings::get()->pathfinderStuckDeaths);
    ConfigValuePtr<bool> m_foresight = ConfigValue<bool>::create(
        "pathfinder.foresight", &GrapeSettings::get()->pathfinderForesight);

   public:
    struct Plan {
        static constexpr int MAX_TOGGLES = 4;

        std::array<uint64_t, MAX_TOGGLES> m_frames{};
        int m_count = 0;
        int m_next = 0;
        uint64_t m_expiresAt = 0;

        bool active(uint64_t frame) const {
            return m_next < m_count && frame <= m_expiresAt;
        }
        bool dueAt(uint64_t frame) const {
            return m_next < m_count && m_frames[m_next] <= frame;
        }
        void clear() {
            m_count = 0;
            m_next = 0;
            m_expiresAt = 0;
        }
    };

   private:
    uint64_t m_lastFrame = UINT64_MAX;
    bool m_p1Clicked = false;
    bool m_p2Clicked = false;
    uint64_t m_p1LastToggle = UINT64_MAX;
    uint64_t m_p2LastToggle = UINT64_MAX;
    float m_p1SwiftAcc = 0.0f;
    float m_p2SwiftAcc = 0.0f;

    Plan m_p1Plan;
    Plan m_p2Plan;

    uint64_t m_lastDeathFrame = UINT64_MAX;
    int m_consecutiveDeaths = 0;

    bool m_awaitingRespawn = false;

    grape::assist::foresight::Controller m_p1Foresight;
    grape::assist::foresight::Controller m_p2Foresight;
    int m_pendingRewind = 0;
    uint64_t m_pendingDeadEnd = 0;
    bool m_pendingFromP1 = true;
    bool m_rewinding = false;
    int m_levelId = 0;
    void* m_levelLayer = nullptr;

    void decide(PlayLayer* pl, Trajectory* traj, bool p1, bool& clicked);
    void decideForesight(PlayLayer* pl, Trajectory* traj, bool p1, bool& clicked);
    void performRewind(PlayLayer* pl);
    bool foresightActive() const;
    int rewindableFrames() const;

   public:
    bool m_proportional = GrapeSettings::get()->autoclickerProportional;
    float m_holdPct = GrapeSettings::get()->autoclickerHoldPct;
    float m_releasePct = GrapeSettings::get()->autoclickerReleasePct;
    float m_swiftPct = GrapeSettings::get()->autoclickerSwiftPct;
    int m_cycleFrames =
        std::max(2, GrapeSettings::get()->autoclickerCycleFrames);

    void update(PlayLayer* pl);

    void onDeath(PlayLayer* pl, uint64_t deathFrame);

    bool wantsHistory() const;
    int m_rewinds = 0;

    void reset() {
        m_lastFrame = UINT64_MAX;
        m_p1Clicked = false;
        m_p2Clicked = false;
        m_p1LastToggle = UINT64_MAX;
        m_p2LastToggle = UINT64_MAX;
        m_p1SwiftAcc = 0.0f;
        m_p2SwiftAcc = 0.0f;
        m_awaitingRespawn = false;
        m_p1Plan.clear();
        m_p2Plan.clear();
        m_p1Foresight.reset();
        m_p2Foresight.reset();
        m_pendingRewind = 0;
    }

    void saveSettings() {
        m_stuckDeaths = std::max(1, m_stuckDeaths);
        GrapeSettings::get()->pathfinderStuckDeaths = m_stuckDeaths;
        const float sum = m_holdPct + m_releasePct + m_swiftPct;
        if (sum > 0.0f) {
            m_holdPct = m_holdPct / sum * 100.0f;
            m_releasePct = m_releasePct / sum * 100.0f;
            m_swiftPct = m_swiftPct / sum * 100.0f;
        } else {
            m_holdPct = m_releasePct = m_swiftPct = 100.0f / 3.0f;
        }
        m_cycleFrames = std::max(2, m_cycleFrames);
        GrapeSettings::get()->autoclickerProportional = m_proportional;
        GrapeSettings::get()->autoclickerHoldPct = m_holdPct;
        GrapeSettings::get()->autoclickerReleasePct = m_releasePct;
        GrapeSettings::get()->autoclickerSwiftPct = m_swiftPct;
        GrapeSettings::get()->autoclickerCycleFrames = m_cycleFrames;
    }
};
