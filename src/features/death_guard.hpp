#pragma once

#include <functional>

class PlayLayer;
class PlayerObject;
class GameObject;
class GJBaseGameLayer;

namespace grape::death {

void handle(PlayLayer* layer, PlayerObject* player, GameObject* cause,
            const std::function<void()>& kill);

bool hidesDeathEffect();

void lookAhead(GJBaseGameLayer* layer);

void releaseHeldJump(GJBaseGameLayer* layer);

}
