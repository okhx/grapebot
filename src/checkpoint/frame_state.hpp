#pragma once

#include <Geode/Geode.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "player_state.hpp"
#include "vine/rng.hpp"

namespace grape::snap {

struct FrameState {
    struct Clock {
        uint64_t m_attemptStart = 0;
        uint64_t m_frame = 0;
        std::size_t m_inputCursor = 0;
        double m_tps = 240.0;
        double m_timewarp = 0.0;
    };

    struct World {
        std::unordered_map<int, int> m_persistentItems{};
        std::array<float, 2000> m_variance{};
        std::vector<GameObject*> m_nonEffectObjects;
        int m_nonEffectCount = 0;
    };

    std::array<PlayerState, 2> m_players{};
    CheckpointObject* m_respawn = nullptr;
    std::size_t m_platformerDepth = 0;
    Clock m_clock;
    vine::RngWords m_rng;
    World m_world;
    std::vector<GameObject*> m_brokenObjects;
    std::vector<uint64_t> m_randomTriggerStates;
};

}
