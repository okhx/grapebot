#include "player.hpp"

#include <Geode/Geode.hpp>

#include "config/config.hpp"

using namespace geode::prelude;

namespace phys {

namespace {

using Type = GameObjectType;

constexpr double DEGREES_TO_RADIANS = 0.01745329;
constexpr float LEGACY_RING_HEIGHT = 11.180032f;

float wrapHalfTurn(float angle) {
    if (angle < -180.0) return static_cast<float>(angle + 360.0);
    if (angle > 180.0) return static_cast<float>(angle - 360.0);
    return angle;
}

float clampDashAngle(float angle) {
    if (!(angle > 70.0 || angle < -70.0)) return angle;
    if (angle > 0.0) return angle >= 180.0 ? -70.0f : 70.0f;
    return angle > -180.0 ? -70.0f : 70.0f;
}

float ringHeading(DashRingObject* ring) {
    float heading = ring->getObjectRotation();
    if (ring->isFlipX()) heading = static_cast<float>(heading + 180.0);
    if (fabs((ring->getRotationX() - ring->getRotationY())) > 179.0)
        heading = static_cast<float>(heading + 180.0);
    return wrapHalfTurn(fmodf(-heading, 360.0));
}

float classicDashAngle(PlayerObject* player, float heading) {
    float offset = player->m_isSideways ? -90.0f : 0.0f;
    if (player->m_isGoingLeft) offset = static_cast<float>(offset + 180.0);

    float relative = offset != 0 ? wrapHalfTurn(heading + offset) : heading;
    if (fabs(relative) > 90.0) {
        const float mirrored = relative <= 0.0 ? -180.0f : 180.0f;
        relative = wrapHalfTurn(mirrored - relative);
    }
    const float clamped = clampDashAngle(relative);
    return offset != 0.0 ? wrapHalfTurn(clamped - offset) : clamped;
}

CCPoint unitVector(float degrees) {
#ifdef GEODE_IS_WINDOWS
    return cocos2d::ccpForAngle(degrees * DEGREES_TO_RADIANS);
#else
    const float radians = degrees * 0.01745329f;
    return CCPoint{static_cast<float>(std::cos(radians)), static_cast<float>(std::sin(radians))};
#endif
}

void stopSpin(PlayerObject* player) {
    player->m_isRotating = false;
    player->m_isBallRotating2 = false;
    player->m_isBallRotating = false;
    player->m_rotationSpeed = 0.0;
}

double boostFactor(Type ring, PlayerObject* player) {
    const bool regularSize = player->m_vehicleSize == 1.0;
    switch (ring) {
        case Type::GravityRing:
            return 0.8;
        case Type::GreenRing:
            return player->m_isShip ? 0.7 : 1.0;
        case Type::PinkJumpRing:
            if (player->m_isShip) return 0.37;
            if (player->m_isBird) return 0.42;
            if (player->m_isBall) return 0.77;
            return 0.72;
        case Type::RedJumpRing:
            if (player->m_isShip) return regularSize ? 1.0 : 1.4;
            if (player->m_isBird) return regularSize ? 1.02 : 1.36;
            if (player->m_isBall) return 1.34;
            if (player->m_isRobot) return 1.28;
            if (player->m_isSpider) return 1.34;
            return 1.38;
        default:
            return player->m_isRobot ? 0.9 : 1.0;
    }
}

void dropDown(PlayerObject* player) {
    const float side = player->m_isUpsideDown ? -1.0f : 1.0f;
    const bool flying = player->m_isShip || player->m_isBird || player->m_isDart || player->m_isSwing;
    float push = static_cast<float>(side * -15.0);
    if (flying) {
        push = static_cast<float>(side * -14.0);
        if (player->m_isBird) push = static_cast<float>(push * 0.8);
    } else if (!player->m_isRobot && player->m_isSpider) {
        push = static_cast<float>(push * 1.1);
    }
    player->setYVelocity(push, 1);

    if (!player->m_isBall && !player->m_isLocked && !player->m_isDashing) {
        stopSpin(player);
        player->runNormalRotation(0, 1.0);
    } else {
        player->runBallRotation2();
    }
    player->m_hasEverHitRing = true;
    player->m_isAccelerating = true;
    if (player->m_isBall || player->m_isSwing) player->m_jumpBuffered = false;
}

void launch(PlayerObject* player, Type ring) {
    player->m_maybeIsBoosted = true;
    player->m_isOnGround2 = false;
    player->m_isOnGround = false;

    const float sizeScale = player->m_vehicleSize != 1.0 ? 0.8f : 1.0f;
    float height = GrapeSettings::get()->physics21 ? LEGACY_RING_HEIGHT : player->m_yStart;
    if (ring == Type::RedJumpRing) player->m_isAccelerating = true;
    height = static_cast<float>(height * boostFactor(ring, player));
    if (ring == Type::GreenRing) flipGravity(player, !player->m_isUpsideDown);

    const float direction = player->m_isUpsideDown ? -1 : 1;
    const double velocity = direction * height * sizeScale;
    player->setYVelocity(velocity, direction);

    if (!player->m_isBall && !player->m_isLocked && !player->m_isDashing) {
        stopSpin(player);
#ifdef GEODE_IS_WINDOWS
        player->runNormalRotation(0, 1.0);
    } else {
        player->runBallRotation2();
#endif
    }

    player->m_lastGroundedPos = player->getPosition();
    player->m_hasEverHitRing = true;
    if (player->m_isSwing)
        player->m_yVelocity *= 0.6;
    else if (player->m_isBall || player->m_isSpider)
        player->m_yVelocity *= 0.7;
    if (ring == Type::GravityRing) flipGravity(player, !player->m_isUpsideDown);
    if (ring == Type::RedJumpRing) player->m_isAccelerating = true;
}

bool ringBlocked(PlayerObject* player, RingObject* ring, bool customOrTeleport) {
    if (!player->m_stateRingJump2 || player->m_isDashing || !player->m_stateJumpBuffered) return true;
    const bool usedRingThisJump = player->m_touchedRing || customOrTeleport;
    const bool customStillFresh = !player->m_touchedCustomRing && ring->m_objectType == Type::CustomRing;
    if (!usedRingThisJump || customStillFresh) return false;
    return player->m_touchedGravityPortal || ring->m_objectType != Type::TeleportOrb;
}

void markRingUsed(PlayerObject* player, RingObject* ring, bool customOrTeleport) {
    if (ring->m_isReverse) player->reversePlayer(ring);
    player->m_ringJumpRelated = true;
    player->m_usedRings.insert(ring->m_uniqueID);
    switch (ring->m_objectType) {
        case Type::CustomRing: player->m_touchedCustomRing = true; break;
        case Type::TeleportOrb: player->m_touchedGravityPortal = true; break;
        default: player->m_touchedRing = true; break;
    }
    player->m_touchingRings->removeObject(ring, true);
    if (!customOrTeleport) player->m_padRingRelated = true;
}

}

void startDashing(PlayerObject* player, DashRingObject* ring) {
    if (player->m_isDashing) return;

    player->m_isDashing = true;
    player->m_lastLandTime = 0.;
    stopSpin(player);
    player->m_dashRing = ring;
    player->m_dashStartTime = player->m_totalTime;

    const float heading = ringHeading(ring);
    const float angle = player->m_isPlatformer ? heading : classicDashAngle(player, heading);
    const float length = player->m_isPlatformer ? static_cast<float>(ring->m_dashSpeed * 5.77) : 1.0f;

    CCPoint dash = unitVector(angle) * length;
    if (player->m_isSideways) std::swap(dash.x, dash.y);

    if (player->m_isPlatformer) {
        player->m_dashX = dash.x;
        player->m_dashY = dash.y;
        const double sideways = dash.x;
        if (sideways > 0.0)
            player->doReversePlayer(false);
        else if (!(sideways >= 0.0))
            player->doReversePlayer(true);
    } else {
        const double slope = dash.y / fabs(dash.x);
        player->m_dashY = slope;
        player->m_dashX = fabs(slope);
    }

    player->m_dashAngle = angle;
    player->m_lastGroundedPos = cocos2d::CCPoint{0.0, 0.0};
}

void stopDashing(PlayerObject* player) {
    if (!player->m_isDashing) return;

    player->m_isDashing = false;
    player->m_lastLandTime = 0.0;
    if (player->m_isPlatformer && player->m_dashRing) {
        const float boost = player->m_dashRing->m_endBoost;
        player->m_isAccelerating = true;
        player->m_yVelocity = static_cast<float>(player->m_dashY) * boost;
        player->m_platformerXVelocity = static_cast<float>(player->m_dashX) * boost;
        player->m_affectedByForces = true;
        if (player->m_dashRing->m_stopSlide) {
            player->m_isAccelerating = false;
            player->m_affectedByForces = false;
        }
    }
    player->m_dashRing = nullptr;

    if (player->m_isPlatformer) {
        float reach = sqrtf(player->m_dashX * player->m_dashX + player->m_dashY * player->m_dashY);
        reach = reach <= 17.3 ? static_cast<float>((reach / 17.3) * 1.5 + 0.5) : 2.0f;
        const float sizeScale = player->m_vehicleSize == 1.0 ? 0.433333f : 0.333333f;
        const float sidewaysSign = player->m_isSideways ? -1 : 1;
        const int halfTurn = (player->m_isUpsideDown ? -180 : 180) * (player->m_isGoingLeft ? -1 : 1);
        player->m_rotationSpeed = (halfTurn * sidewaysSign * player->m_gravityMod * reach) / sizeScale;
        player->m_isRotating = true;
    }

    if (!player->m_isBall) return;
    if (player->m_isPlatformer) {
        player->m_isRotating = false;
        player->m_isBallRotating = false;
        player->m_rotationSpeed = 0.0;
    }
    player->runBallRotation(1.0);
}

void ringJump(PlayerObject* player, RingObject* ring) {
    if (player->m_isDead || !ring) return;
    if (player->m_usedRings.find(ring->m_uniqueID) != player->m_usedRings.end()) return;

    const Type type = ring->m_objectType;
    const bool customOrTeleport = type == Type::CustomRing || type == Type::TeleportOrb;
    if (ringBlocked(player, ring, customOrTeleport)) return;
    markRingUsed(player, ring, customOrTeleport);

    if (type == Type::CustomRing) return;
    if (type == Type::TeleportOrb) {
        teleportPlayer(player->m_gameLayer, static_cast<TeleportPortalObject*>(ring), player);
        return;
    }
    if (type == Type::SpiderOrb) {
        const bool facingAway = player->m_isSideways ? ring->isFacingLeft() : ring->isFacingDown();
        if (facingAway != player->m_isUpsideDown) flipGravity(player, !player->m_isUpsideDown);
        player->spiderTestJumpInternal(false);
    }
    if (type == Type::DashRing || type == Type::GravityDashRing) {
        if (type == Type::GravityDashRing) flipGravity(player, !player->m_isUpsideDown);
        startDashing(player, static_cast<DashRingObject*>(ring));
        return;
    }

    player->m_stateRingJump = false;
    if (type == Type::DropRing)
        dropDown(player);
    else
        launch(player, type);
}

void togglePlayerScale(PlayerObject* player, bool mini) {
    if (mini == (player->m_vehicleSize != 1.0)) return;
    if (mini) {
        player->m_vehicleSize = 0.6;
    } else {
        if (player->m_isPlatformer) player->m_stateScale = 2;
        player->m_vehicleSize = 1;
    }
    const float scale = player->m_vehicleSize;
    player->m_spriteWidthScale = scale;
    player->m_spriteHeightScale = scale;
    player->setScaleX(scale);
    player->setScaleY(scale);
}

}
