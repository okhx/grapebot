#pragma once

#include <Geode/Geode.hpp>

#include "gjbasegamelayer.hpp"
#include "gravity.hpp"

namespace phys {

void startDashing(PlayerObject* player, DashRingObject* ring);
void stopDashing(PlayerObject* player);
void ringJump(PlayerObject* player, RingObject* ring);
void togglePlayerScale(PlayerObject* player, bool mini);

}
