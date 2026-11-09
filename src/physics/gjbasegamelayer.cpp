#include "gjbasegamelayer.hpp"

#include <Geode/Geode.hpp>

#include <cstdlib>

namespace phys {

namespace {

constexpr int MAX_GROUP = 9999;
constexpr int SCREEN_LINKED_PORTAL = 747;

cocos2d::CCArray* groupFor(GJBaseGameLayer* layer, int id) {
    id = std::clamp(id, 0, MAX_GROUP);
    cocos2d::CCArray*& slot = layer->m_groups.at(id);
    if (!slot) {
        slot = cocos2d::CCArray::create();
        layer->m_groupDict->setObject(slot, id);
    }
    return slot;
}

TeleportPortalObject* pickFromGroup(GJBaseGameLayer* layer, int group) {
    if (group <= 0) return nullptr;
    cocos2d::CCArray* members = groupFor(layer, group);
    const int size = members->count();
    if (size <= 0) return nullptr;
    float roll = 0.0f;
    if (size > 1) {
        roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        if (roll == 1.0) roll = 0.0f;
    }
    return static_cast<TeleportPortalObject*>(members->objectAtIndex(static_cast<int>(roll * size)));
}

CCPoint landingSpot(TeleportPortalObject* portal, TeleportPortalObject* exit, PlayerObject* player,
                    const CCPoint& from) {
    CCPoint spot;
    if (portal->m_objectID == SCREEN_LINKED_PORTAL) {
        const float offset = portal->m_teleportYOffset;
        spot = CCPoint{player->getPosition().x, portal->getRealPosition().y + offset};
    } else {
        spot = exit->getPosition();
    }
    if (portal->m_saveOffset) {
        CCPoint carried = portal->getRealPosition();
        carried -= from;
        spot -= carried;
    }
    if (portal->m_ignoreX) spot.x = from.x;
    if (portal->m_ignoreY) spot.y = from.y;
    return spot;
}

bool facesSideways(int objectId) {
    switch (objectId) {
        case 38:
        case 747:
        case 749:
        case 2064:
        case 2902:
            return true;
        default:
            return false;
    }
}

float exitHeading(TeleportPortalObject* portal, TeleportPortalObject* exit) {
    const float flip = static_cast<float>(portal->isFlipX() ? 180 : 1);
    if (!exit) return flip - portal->getRotationX();
    const float base = facesSideways(portal->m_objectID) ? 180.0f : 90.0f;
    return flip + (base - exit->getRotationX());
}

void steerVelocity(PlayerObject* player, float heading) {
    CCPoint velocity{static_cast<float>(player->m_platformerXVelocity),
                     static_cast<float>(player->m_yVelocity)};
    const float turn = heading * 0.017453292f - std::atan2(velocity.y, velocity.x);
    if (turn != 0.0f) {
        const float sine = std::sin(turn);
        const float cosine = std::cos(turn);
        velocity = CCPoint{velocity.x * sine - velocity.y * cosine,
                           velocity.x * cosine + velocity.y * sine};
    }
    if (player->m_isSideways) std::swap(velocity.x, velocity.y);

    player->m_yVelocity = velocity.y;
    player->m_isAccelerating = true;
    if (!player->m_isPlatformer) return;
    player->m_platformerXVelocity = velocity.x;
    player->m_affectedByForces = true;
}

CCPoint headingVector(float heading) {
    const float radians = heading * 0.01745329;
#ifdef GEODE_IS_WINDOWS
    return cocos2d::ccpForAngle(radians);
#else
    return CCPoint{static_cast<float>(std::cos(radians)), static_cast<float>(std::sin(radians))};
#endif
}

void pushWithStaticForce(PlayerObject* player, TeleportPortalObject* portal, float heading) {
    const float strength = portal->m_staticForce;
    const bool additive = portal->m_staticForceAdditive;
    if (strength == 0.0 && !additive) {
        player->m_isAccelerating = false;
        player->m_yVelocity = 0.0;
        if (player->m_isPlatformer) {
            player->m_platformerXVelocity = 0.0;
            player->m_affectedByForces = false;
        }
        return;
    }

    const CCPoint direction = headingVector(heading);
    const float length = direction.getLength();
    if (!(length > 0.0)) return;
    CCPoint push = direction * (strength / length);
    if (player->m_isSideways) std::swap(push.x, push.y);

    player->m_isAccelerating = true;
    player->m_yVelocity = additive ? push.y + player->m_yVelocity : push.y;
    if (!player->m_isPlatformer) return;
    player->m_platformerXVelocity = additive ? push.x + player->m_platformerXVelocity : push.x;
    player->m_affectedByForces = true;
}

}

void teleportPlayer(GJBaseGameLayer* layer, TeleportPortalObject* portal, PlayerObject* player) {
    player->m_wasTeleported = true;
    if (auto* linked = portal->m_orangePortal)
        portal->m_teleportYOffset = linked->getStartPos().y - portal->getStartPos().y;

    const CCPoint from = player->getPosition();
    TeleportPortalObject* exit = portal->m_orangePortal;
    if (!exit) exit = pickFromGroup(layer, portal->m_targetGroupID);

    if (exit) {
        player->m_lastPortalPos = portal->getPosition();
        player->m_lastActivatedPortal = portal;
        player->m_fallStartY = 0.0;
        player->setPosition(landingSpot(portal, exit, player, from));
    }

    if (portal->m_gravityMode > 0) {
        const bool upsideDown = portal->m_gravityMode == 2 ||
                                (portal->m_gravityMode == 3 && !player->m_isUpsideDown);
        flipGravity(player, upsideDown);
    }

    const float heading = exitHeading(portal, exit);
    if (portal->m_redirectForceEnabled)
        steerVelocity(player, heading);
    else if (portal->m_staticForceEnabled)
        pushWithStaticForce(player, portal, heading);

    if (portal->m_snapGround) player->m_lastGroundedPos = player->getPosition();
}

}
