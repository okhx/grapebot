#include <Geode/Geode.hpp>
#include <Geode/modify/RandTriggerGameObject.hpp>

#include <cmath>
#include <limits>
#include <unordered_map>

#include "checkpoint/practice.hpp"
#include "engine/engine.hpp"
#include "vine/session.hpp"

using namespace geode::prelude;

namespace {

constexpr int RANDOM_TRIGGER_ID = 2068;
constexpr uint64_t UNSEEDED = std::numeric_limits<uint64_t>::max();

std::unordered_map<RandTriggerGameObject*, uint64_t> g_randomStates;

uint64_t& randomState(RandTriggerGameObject* object) {
    auto [it, inserted] = g_randomStates.try_emplace(object, UNSEEDED);
    return it->second;
}

int rollUpTo(uint64_t& state, int total) {
    const uint64_t drawn = state;
    state = drawn * 214013 + 2531011;
    const float unit = static_cast<float>((drawn >> 16) & 0x7FFF);
    return static_cast<int>(roundf(unit / 32767.0 * static_cast<float>(total)));
}

}

struct SLRandTriggerGameObject : Modify<SLRandTriggerGameObject, RandTriggerGameObject> {
    
    
    void customObjectSetup(std::vector<std::string>& values, std::vector<void*>& exists) override {
        RandTriggerGameObject::customObjectSetup(values, exists);
        GrapeEngine::get()->keeper().trackRandomTrigger(&randomState(this), m_uniqueID);
    }

    void triggerObject(GJBaseGameLayer* layer, int uniqueID, std::vector<int> const* remapKeys) override {
        if (m_objectID != RANDOM_TRIGGER_ID) return RandTriggerGameObject::triggerObject(layer, uniqueID, remapKeys);

        uint64_t& state = randomState(this);
        if (state == UNSEEDED) state = GrapeEngine::get()->replay().m_rng.attemptSeed() ^ (m_uniqueID * 2137);

        int total = 0;
        for (const ChanceObject& chance : m_chanceObjects) total += chance.m_chance;
        if (total <= 0 || m_chanceObjects.empty()) return;

        const int roll = rollUpTo(state, total);
        int reached = 0;
        int group = m_chanceObjects.back().m_groupID;
        for (const ChanceObject& chance : m_chanceObjects) {
            reached += chance.m_chance;
            if (reached < roll) continue;
            group = chance.m_groupID;
            break;
        }

        std::vector<int> keys = remapKeys ? *remapKeys : std::vector<int>{};
        layer->spawnGroup(group, false, 0.0, keys, 0, 0);
    }
};
