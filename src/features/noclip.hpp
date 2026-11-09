#pragma once

#include <Geode/Geode.hpp>

#include "engine/timeline.hpp"

namespace grape::noclip {

bool covers(FrameEngine::NoclipType type, PlayerObject* who,
            PlayerObject* player1, PlayerObject* player2);
void flash(cocos2d::CCNode* level);
void stopFlash(cocos2d::CCNode* level);

}
