#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <array>
#include <cstddef>
#include <vector>

#include "config/config.hpp"
#include "shared/value/value.hpp"

struct PlayerBoxes {
    cocos2d::CCRect outer;
    cocos2d::CCRect inner;
    std::array<cocos2d::CCPoint, 4> turned{};
    bool turnedValid = false;
    bool holding = false;

    static PlayerBoxes of(PlayerObject* player);
    bool sameSpot(const PlayerBoxes& other) const;
};

class BoxTrail {
   public:
    void push(const PlayerBoxes& boxes, std::size_t limit);
    void clear();
    bool empty() const { return m_samples.empty(); }

    template <class Visit>
    void each(Visit&& visit) const {
        const std::size_t count = m_samples.size();
        for (std::size_t i = 0; i < count; ++i)
            visit(m_samples[(m_oldest + i) % count]);
    }

   private:
    void straighten();

    std::vector<PlayerBoxes> m_samples;
    std::size_t m_oldest = 0;
};

class Hitboxes {
    bool m_showWanted = false;
    bool m_isolateWanted = false;
    bool m_snapWanted = false;

   public:
    bool m_initialized = false;

    ConfigValuePtr<bool> m_enabled =
        ConfigValue<bool>::create("hitboxes.enabled", &m_showWanted);
    ConfigValuePtr<bool> m_deathOnly =
        ConfigValue<bool>::create("hitboxes.death_only", &m_isolateWanted);
    ConfigValuePtr<bool> m_trailEnabled = ConfigValue<bool>::create(
        "hitboxes.trail_enabled", &GrapeSettings::get()->hitboxes.trailEnabled);
    ConfigValuePtr<double> m_width = ConfigValue<double>::create(
        "hitboxes.width", &GrapeSettings::get()->hitboxes.width);
    ConfigValuePtr<bool> m_trailSnap =
        ConfigValue<bool>::create("hitboxes.trail_snap", &m_snapWanted);

    void init(GJBaseGameLayer* layer);
    void clearTrail();
    void showDeath(PlayerObject* player, GameObject* object);
    void clearDeath();
    void saveToTrail(GJBaseGameLayer* layer);
    void snapToTrail();
    void snapPlaced(GameObject* object);
    void draw(GJBaseGameLayer* layer);
    void destroy(GJBaseGameLayer* layer = nullptr);

   private:
    struct SnapResult {
        int m_moved = 0;
        int m_settled = 0;
        int m_unreachable = 0;
        int m_eligible = 0;
    };
    SnapResult snapObjects(EditorUI* ui, const std::vector<GameObject*>& objects,
                           bool recordUndo);

    void paintTrail(GJBaseGameLayer* layer);
    void paintLive(GJBaseGameLayer* layer);
    bool isolating() const;

    cocos2d::CCDrawNode* m_liveLayer = nullptr;
    cocos2d::CCDrawNode* m_trailLayer = nullptr;
    GJBaseGameLayer* m_layer = nullptr;

    PlayerObject* m_deathPlayer = nullptr;
    GameObject* m_deathObject = nullptr;

    std::array<BoxTrail, 2> m_trails;
    bool m_trailStale = false;
    int m_stepsSinceTrailPaint = 0;
};
