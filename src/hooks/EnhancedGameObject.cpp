#include <Geode/Geode.hpp>
#include <engine/timeline.hpp>

#include "engine/engine.hpp"
#include "physics/collisions.hpp"
#include "trajectory/trajectory.hpp"

using namespace geode::prelude;

#include <Geode/modify/EnhancedGameObject.hpp>

struct GrapeEnhancedGameObject
    : Modify<GrapeEnhancedGameObject, EnhancedGameObject> {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("EnhancedGameObject::activatedByPlayer",
                                     Priority::VeryEarly)) {
            geode::log::warn(
                "Failed to set priority for activatedByPlayer! Other mods may "
                "see trajectory fake players and crash");
        }
    }

    void activatedByPlayer(PlayerObject* player) {
        if (!GrapeEngine::get()->trajectory().isFakePlayer(player)) {
            EnhancedGameObject::activatedByPlayer(player);
            return;
        }

        phys::activateForTrajectory(
            reinterpret_cast<EffectGameObject*>(this), player);
    }
};
