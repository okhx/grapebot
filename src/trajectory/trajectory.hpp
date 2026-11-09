#ifndef TRAJECTORY_TRAJECTORY_HPP
#define TRAJECTORY_TRAJECTORY_HPP

#include <Geode/Geode.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <unordered_set>

#include "../config/config.hpp"
#include "../shared/value/value.hpp"

class TrajectoryDrawNode : public cocos2d::CCDrawNode {
   public:
    static TrajectoryDrawNode* create();
};

struct TrajectoryPlayerData {
    cocos2d::CCPoint position;
    cocos2d::CCRect hitbox;
    cocos2d::CCRect innerHitbox;
    float rotation;
    bool p1;
    bool holding;
    bool dead;
    int score;
    float minY;
    float maxY;
};

struct TrajectoryState {
    ConfigValuePtr<bool> m_enabled =
        ConfigValue<bool>::create("trajectory.enabled", &GrapeSettings::get()->trajectory.enabled);
    ConfigValuePtr<double> m_width =
        ConfigValue<double>::create("trajectory.width", &GrapeSettings::get()->trajectory.width);
    ConfigValuePtr<double> m_length =
        ConfigValue<double>::create("trajectory.length", &GrapeSettings::get()->trajectory.length);
};

struct FlipPlan {
    static constexpr int MAX_FLIPS = 16;

    std::array<int16_t, MAX_FLIPS> m_steps{};
    int8_t m_count = 0;

    bool empty() const { return m_count == 0; }
    int size() const { return m_count; }
    int firstStep() const { return m_count ? m_steps[0] : -1; }

    bool push(int step) {
        if (m_count >= MAX_FLIPS) return false;
        m_steps[m_count++] = static_cast<int16_t>(step);
        return true;
    }

    bool flipsAt(int step) const {
        const auto wanted = static_cast<int16_t>(step);
        return std::find(m_steps.begin(), m_steps.begin() + m_count, wanted) != m_steps.begin() + m_count;
    }
};

struct PredictionConfig {
    bool m_bypassConfig = false;
    int m_maxLength = 10'000'000;
    double m_overridenTPS = 0.0;
    bool m_draw = true;
    bool m_autoClickOrbs = false;
    FlipPlan m_flipPlan{};
    float* m_colors = nullptr;
    float* m_player2Colors = nullptr;
    std::function<bool(int, PlayerObject*)> m_doomed = nullptr;
};

class Trajectory {
   public:
    using TrajectoryMode = GrapeSettings::TrajectorySettings::Mode;

    static constexpr int CLICK_MASK = 0b111;
    static constexpr int DIRECTION_MASK = 0b11000;

    static Trajectory* create(GJBaseGameLayer* layer);
    ~Trajectory();

    TrajectoryState* m_state = nullptr;

    bool isFor(GJBaseGameLayer* layer) const { return m_layer == layer; }
    bool drawing() const { return m_drawing; }
    void setDelta(float delta) { m_delta = delta; }

    bool isFakePlayer(PlayerObject* player) const { return slotOf(player) >= 0; }
    bool isFakePlayer2(PlayerObject* player) const { return slotOf(player) == 1; }
    PlayerObject* getOtherPlayer(PlayerObject* player) const { return m_fake[player == m_fake[0] ? 1 : 0]; }

    bool hasDied(PlayerObject* player);
    void handleButton(bool p1, bool holding) { m_holding[p1 ? 0 : 1] = holding; }

    void rememberActivatedObject(EnhancedGameObject* object, PlayerObject* player);
    bool playerHasActivated(PlayerObject* player, EnhancedGameObject* object);
    bool realPlayerHasActivated(PlayerObject* player, EnhancedGameObject* object);

    int getPredictionLength(GJBaseGameLayer* layer);
    void invalidateCache() { m_valid = false; }
    TrajectoryPlayerData simulate(GJBaseGameLayer* layer, bool p1, int mode, bool clickBothPlayers,
                                  PredictionConfig config = PredictionConfig());
    void update(GJBaseGameLayer* layer);

   private:
    struct Snapshot {
        uint32_t m_frame = 0;
        std::array<std::array<float, 7>, 2> m_motion{};
        std::array<uint64_t, 2> m_flags{};
        float m_timeWarp = 0.0f;
        float m_zoom = 0.0f;
        float m_width = 0.0f;
        float m_length = 0.0f;
        int m_maxSteps = 0;
        uint32_t m_switches = 0;
        size_t m_colors = 0;

        bool operator==(const Snapshot&) const = default;
    };

    class Pen;

    Trajectory() = default;

    int slotOf(PlayerObject* player) const;
    Snapshot snapshot(GJBaseGameLayer* layer);
    bool advance(GJBaseGameLayer* layer, PlayerObject* player, int mode, Pen& pen, int& steps,
                 const PredictionConfig& config);
    void applyStartingInput(GJBaseGameLayer* layer, PlayerObject* player, int mode);
    TrajectoryPlayerData runPrediction(GJBaseGameLayer* layer, PlayerObject* player, PlayerObject* other,
                                       int mode, float* player1Colors, float* player2Colors, bool both,
                                       const PredictionConfig& config);
    void rebuild(GJBaseGameLayer* layer);
    void drawHitbox(GJBaseGameLayer* layer, PlayerObject* player);
    float lineWidth(GJBaseGameLayer* layer) const;

    GJBaseGameLayer* m_layer = nullptr;
    TrajectoryDrawNode* m_node = nullptr;
    std::array<PlayerObject*, 2> m_fake{};
    std::array<std::unordered_set<uintptr_t>, 2> m_used;
    std::array<bool, 2> m_dead{};
    std::array<bool, 2> m_holding{};
    Snapshot m_shown;
    bool m_valid = false;
    bool m_drawing = false;
    float m_delta = 0.0f;
};

class TrajectoryManager {
   public:
    TrajectoryState m_state;

    bool exists() const { return m_trajectory != nullptr; }
    Trajectory* unsafeInner() { return m_trajectory; }
    bool enabled() const { return m_state.m_enabled->inner(); }

    void setEnabled(bool enabled) {
        m_state.m_enabled->inner() = enabled;
        m_state.m_enabled->notifyChange();
    }
    void toggle() { setEnabled(!enabled()); }

    void init(GJBaseGameLayer* layer);
    void uninit(GJBaseGameLayer* layer = nullptr);
    void update(GJBaseGameLayer* layer);

    bool drawing() const { return m_trajectory && m_trajectory->drawing(); }
    bool isFakePlayer(PlayerObject* player) { return m_trajectory && m_trajectory->isFakePlayer(player); }
    bool isFakePlayer2(PlayerObject* player) { return m_trajectory && m_trajectory->isFakePlayer2(player); }
    bool hasDied(PlayerObject* player) { return m_trajectory && m_trajectory->hasDied(player); }
    PlayerObject* getOtherPlayer(PlayerObject* player) {
        return m_trajectory ? m_trajectory->getOtherPlayer(player) : nullptr;
    }
    bool playerHasActivated(PlayerObject* player, EnhancedGameObject* object) {
        return m_trajectory && m_trajectory->playerHasActivated(player, object);
    }
    bool realPlayerHasActivated(PlayerObject* player, EnhancedGameObject* object) {
        return m_trajectory && m_trajectory->realPlayerHasActivated(player, object);
    }

    void handleButton(bool p1, bool holding) {
        if (m_trajectory) m_trajectory->handleButton(p1, holding);
    }
    void setDelta(float delta) {
        if (m_trajectory) m_trajectory->setDelta(delta);
    }
    void rememberActivatedObject(EnhancedGameObject* object, PlayerObject* player) {
        if (m_trajectory) m_trajectory->rememberActivatedObject(object, player);
    }

   private:
    Trajectory* m_trajectory = nullptr;
};

#endif
