#pragma once

#include <Geode/Geode.hpp>

#include "engine/engine.hpp"
#include "object.hpp"
#include "trajectory/trajectory.hpp"

namespace phys {

void activateForTrajectory(EffectGameObject* object, PlayerObject* player);

void collisionCheckObjects(GJBaseGameLayer* layer, PlayerObject* player,
                           std::vector<GameObject*>* objects, int count, float dt);

void triggerObject(EffectGameObject* object, GJBaseGameLayer* layer, PlayerObject* player);

void checkSpawnObjects(GJBaseGameLayer* layer, PlayerObject* player);

}
