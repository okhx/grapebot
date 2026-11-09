#include "gravity.hpp"

#include <Geode/Geode.hpp>

#include "config/config.hpp"
#include "engine/engine.hpp"
#include "trajectory/trajectory.hpp"
#include "vine/session.hpp"

using namespace geode::prelude;

namespace phys {

namespace {

constexpr double FLIP_KEEPS = 0.5;
constexpr double LEGACY_FLIP_KEEPS = 1.75;
constexpr double PAD_VELOCITY_SCALE = 16.0;
constexpr double ROLLING_PAD_DAMPING = 0.6;

void stopSpinning(PlayerObject* player) {
    player->m_isRotating = false;
    player->m_isBallRotating = false;
    player->m_isBallRotating2 = false;
    player->m_rotationSpeed = 0.;
}

void forgetCollisions(PlayerObject* player) {
    for (auto* log : {player->m_collisionLogTop, player->m_collisionLogBottom,
                      player->m_collisionLogLeft, player->m_collisionLogRight})
        log->removeAllObjects();
    player->m_unk50C = -1;
    player->m_unk510 = -1;
    player->m_lastCollisionBottom = -1;
    player->m_lastCollisionTop = -1;
    player->m_lastCollisionLeft = -1;
    player->m_lastCollisionRight = -1;
}

void turnOver(PlayerObject* player, bool upsideDown) {
    const bool waveKeepsSide = player->m_isDart && GrapeEngine::get()->replay().m_maintainGravity;
    if (!waveKeepsSide) player->m_isUpsideDown = upsideDown;

    player->m_lastFlipTime = player->m_totalTime;
    player->m_collidedBottomMaxY = 0.;
    player->m_collidedTopMinY = 0.;
    player->m_unkA29 = false;
    if (player->m_wasOnSlope || player->m_isOnSlope)
        player->m_slopeFlipGravityRelated = !player->m_slopeFlipGravityRelated;
    forgetCollisions(player);

    player->m_yVelocity *= GrapeSettings::get()->physics21 ? LEGACY_FLIP_KEEPS : FLIP_KEEPS;
    player->m_isOnGround = false;
    if (!player->m_isBall) return;
    stopSpinning(player);
    player->runBallRotation2();
}

bool sameVehicle(PlayerObject* a, PlayerObject* b) {
    return a->m_isShip == b->m_isShip && a->m_isBall == b->m_isBall && a->m_isBird == b->m_isBird &&
           a->m_isSpider == b->m_isSpider && a->m_isRobot == b->m_isRobot &&
           a->m_isSwing == b->m_isSwing;
}

}

void flipGravity(PlayerObject* player, bool upsideDown) {
    if (player->m_isUpsideDown == upsideDown) return;

    auto* layer = GJBaseGameLayer::get();
    turnOver(player, upsideDown);

    auto& trajectory = GrapeEngine::get()->trajectory();
    const auto& state = layer->m_gameState;
    const bool mirroredDual = state.m_isDualMode && !state.m_unkBool31 &&
                              !layer->m_levelSettings->m_twoPlayerMode;
    if (!mirroredDual || !trajectory.isFakePlayer(player)) return;

    PlayerObject* partner = trajectory.getOtherPlayer(player);
    if (sameVehicle(player, partner)) turnOver(partner, !upsideDown);
}

void propellPlayer(PlayerObject* player, float force, bool, int) {
    player->m_maybeIsBoosted = true;
    player->m_isOnGround2 = false;
    player->m_isOnGround = false;
    player->m_isOnSlope = false;
    player->m_wasOnSlope = false;

    const float sizeScale = player->m_vehicleSize != 1.0 ? 0.8f : 1.0f;
    const float direction = player->m_isUpsideDown ? -1.0f : 1.0f;
    const double velocity = static_cast<double>(force * sizeScale * direction) * PAD_VELOCITY_SCALE;
    player->setYVelocity(velocity, 0);
    if (player->m_isBall || player->m_isSpider || player->m_isSwing)
        player->m_yVelocity *= ROLLING_PAD_DAMPING;

    if (!player->m_isLocked && !player->m_isDashing) {
        stopSpinning(player);
        if (player->m_isBall)
            player->runNormalRotation(0, 1.0);
        else
            player->runBallRotation(velocity);
    }
    player->m_lastGroundedPos = player->getPosition();
}

void bumpPlayer(PlayerObject* player, float force, int type, bool playEffect, GameObject* object) {
    if (player->m_isPlatformer || !player->m_fixRobotJump) player->m_touchedPad = true;

    if (type != static_cast<int>(GameObjectType::SpiderPad)) {
        propellPlayer(player, force, playEffect, type);
        player->m_isAccelerating = type == static_cast<int>(GameObjectType::RedJumpPad);
        if (player->m_isAccelerating) player->m_lastGroundedPos = CCPoint{0.0, 0.0};
        return;
    }

    if (object) {
        const bool facingAway = player->m_isSideways ? object->isFacingLeft() : object->isFacingDown();
        if (player->m_isUpsideDown != facingAway) flipGravity(player, !player->m_isUpsideDown);
    }
    player->spiderTestJumpInternal(false);
}

}
