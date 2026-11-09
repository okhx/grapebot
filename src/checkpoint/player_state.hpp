#pragma once

#include <Geode/Geode.hpp>

#include <type_traits>
#include <utility>
#include <vector>

#define GRAPE_PLAYER_STATE_FIELDS(FIELD) \
    FIELD(m_accelerationOrSpeed) FIELD(m_affectedByForces) FIELD(m_alwaysShowStreak) \
    FIELD(m_audioScale) FIELD(m_blackOrbRelated) FIELD(m_canPlaceCheckpoint) \
    FIELD(m_changedDirectionsTime) FIELD(m_checkpointTimeout) FIELD(m_collidedBottomMaxY) \
    FIELD(m_collidedLeftMaxX) FIELD(m_collidedObject) FIELD(m_collidedRightMinX) \
    FIELD(m_collidedTopMinY) FIELD(m_collidingWithLeft) FIELD(m_collidingWithRight) \
    FIELD(m_collidingWithSlopeId) FIELD(m_controlsDisabled) FIELD(m_currentPotentialSlope) \
    FIELD(m_currentSlope) FIELD(m_currentSlope2) FIELD(m_currentSlopeYVelocity) \
    FIELD(m_dashAngle) FIELD(m_dashFireFrame) FIELD(m_dashRing) \
    FIELD(m_dashStartTime) FIELD(m_dashX) FIELD(m_dashY) \
    FIELD(m_decreaseBoostSlide) FIELD(m_defaultMiniIcon) FIELD(m_disablePlayerSqueeze) \
    FIELD(m_disableStreakTint) FIELD(m_enable22Changes) FIELD(m_fadeOutStreak) \
    FIELD(m_fallSpeed) FIELD(m_fallStartY) FIELD(m_fixGravityBug) \
    FIELD(m_fixRobotJump) FIELD(m_flashDelay) FIELD(m_flashDuration) \
    FIELD(m_flashTime) FIELD(m_followRelated) FIELD(m_gameModeChangedTime) \
    FIELD(m_gravity) FIELD(m_gravityMod) FIELD(m_groundObjectMaterial) \
    FIELD(m_groundYVelocity) FIELD(m_gv0123) FIELD(m_hasCustomGlowColor) \
    FIELD(m_hasEverHitRing) FIELD(m_hasEverJumped) FIELD(m_hasGlow) \
    FIELD(m_hasGroundParticles) FIELD(m_hasShipParticles) FIELD(m_holdingButtons) \
    FIELD(m_holdingLeft) FIELD(m_holdingRight) FIELD(m_iconRequestID) \
    FIELD(m_ignoreDamage) FIELD(m_inputsLocked) FIELD(m_isAccelerating) \
    FIELD(m_isBall) FIELD(m_isBallRotating) FIELD(m_isBallRotating2) \
    FIELD(m_isBeingSpawnedByDualPortal) FIELD(m_isBird) FIELD(m_isCollidingWithSlope) \
    FIELD(m_isCurrentSlopeTop) FIELD(m_isDart) FIELD(m_isDashing) \
    FIELD(m_isDead) FIELD(m_isGoingLeft) FIELD(m_isHidden) \
    FIELD(m_isLocked) FIELD(m_isMoving) FIELD(m_isOnGround) \
    FIELD(m_isOnGround2) FIELD(m_isOnGround3) FIELD(m_isOnGround4) \
    FIELD(m_isOnIce) FIELD(m_isOnSlope) FIELD(m_isOutOfBounds) \
    FIELD(m_isPlatformer) FIELD(m_isRobot) FIELD(m_isRotating) \
    FIELD(m_isSecondPlayer) FIELD(m_isShip) FIELD(m_isSideways) \
    FIELD(m_isSliding) FIELD(m_isSlidingRight) FIELD(m_isSpider) \
    FIELD(m_isSwing) FIELD(m_isUpsideDown) FIELD(m_jumpBuffered) \
    FIELD(m_jumpRelatedAC2) FIELD(m_justPlacedStreak) \
    FIELD(m_landParticleRelatedY) FIELD(m_landParticlesAngle) FIELD(m_lastActivatedPortal) \
    FIELD(m_lastCheckpointTime) FIELD(m_lastCollisionBottom) FIELD(m_lastCollisionLeft) \
    FIELD(m_lastCollisionRight) FIELD(m_lastCollisionTop) FIELD(m_lastFlipTime) \
    FIELD(m_lastGroundedPos) FIELD(m_lastGroundObject) FIELD(m_lastJumpTime) \
    FIELD(m_lastLandTime) FIELD(m_lastPortalPos) FIELD(m_lastSpiderFlipTime) \
    FIELD(m_leftPressedFirst) FIELD(m_maybeCanRunIntoBlocks) FIELD(m_maybeChangedDirectionAngle) \
    FIELD(m_maybeGoingCorrectSlopeDirection) FIELD(m_maybeHasStopped) FIELD(m_maybeIsBoosted) \
    FIELD(m_maybeIsColliding) FIELD(m_maybeIsFalling) FIELD(m_maybeIsVehicleGlowing) \
    FIELD(m_maybeLastGroundObject) FIELD(m_maybeReducedEffects) FIELD(m_maybeReverseAcceleration) \
    FIELD(m_maybeReverseSpeed) FIELD(m_maybeSavedPlayerFrame) FIELD(m_maybeSlidingStartTime) \
    FIELD(m_maybeSlidingTime) FIELD(m_maybeSlopeForce) FIELD(m_maybeSpriteRelated) \
    FIELD(m_maybeStateForce2) FIELD(m_maybeTouchedBreakableBlock) FIELD(m_maybeUpsideDownSlope) \
    FIELD(m_objectSnappedTo) FIELD(m_onFlyCheckpointTries) FIELD(m_padRingRelated) \
    FIELD(m_pendingCheckpoint) FIELD(m_physDeltaRelated) FIELD(m_platformerMovingLeft) \
    FIELD(m_platformerMovingRight) FIELD(m_platformerVelocityRelated) FIELD(m_platformerXVelocity) \
    FIELD(m_playEffects) FIELD(m_playerFollowFloats) FIELD(m_playerSpeed) \
    FIELD(m_playerSpeedAC) FIELD(m_playerStreak) FIELD(m_position) \
    FIELD(m_potentialSlopeMap) FIELD(m_practiceDeathEffect) FIELD(m_preLastGroundObject) \
    FIELD(m_quickCheckpointMode) FIELD(m_reverseRelated) FIELD(m_reverseSync) \
    FIELD(m_ringJumpRelated) FIELD(m_usedRings) FIELD(m_robotAnimation1Enabled) \
    FIELD(m_robotAnimation2Enabled) FIELD(m_rotateObjectsRelated) FIELD(m_rotateSpeed) \
    FIELD(m_rotationSpeed) FIELD(m_scaleXRelated) FIELD(m_scaleXRelated2) \
    FIELD(m_scaleXRelated3) FIELD(m_scaleXRelated4) FIELD(m_scaleXRelated5) \
    FIELD(m_scaleXRelatedTime) FIELD(m_shipRotation) FIELD(m_shipStreakType) \
    FIELD(m_shouldTryPlacingCheckpoint) FIELD(m_slopeAngle) FIELD(m_slopeAngleRadians) \
    FIELD(m_slopeEndTime) FIELD(m_slopeFlipGravityRelated) FIELD(m_slopeRotation) \
    FIELD(m_slopeSlidingMaybeRotated) FIELD(m_slopeStartTime) FIELD(m_slopeVelocity) \
    FIELD(m_snapDistance) FIELD(m_somethingPlayerSpeedTime) FIELD(m_speedMultiplier) \
    FIELD(m_spiderAnimationEnabled) FIELD(m_stateBoostX) FIELD(m_stateBoostY) \
    FIELD(m_stateDartSlide) FIELD(m_stateFlipGravity) FIELD(m_stateForce) \
    FIELD(m_stateForceVector) FIELD(m_stateHitHead) FIELD(m_stateJumpBuffered) \
    FIELD(m_stateNoAutoJump) FIELD(m_stateNoStickX) FIELD(m_stateNoStickY) \
    FIELD(m_stateOnGround) FIELD(m_stateRingJump) FIELD(m_stateRingJump2) \
    FIELD(m_stateScale) FIELD(m_stateUnk) FIELD(m_stateUnk2) \
    FIELD(m_streakStrokeWidth) FIELD(m_swapColors) FIELD(m_switchDashFireColor) \
    FIELD(m_switchWaveTrailColor) FIELD(m_totalTime) FIELD(m_touchedCustomRing) \
    FIELD(m_touchedGravityPortal) FIELD(m_touchedPad) FIELD(m_touchedRing) \
    FIELD(m_touchedRings) FIELD(m_trailingParticleLife) FIELD(m_unk3d0) \
    FIELD(m_unk3e0) FIELD(m_unk3e1) FIELD(m_unk50C) \
    FIELD(m_unk510) FIELD(m_unk648) FIELD(m_unk669) \
    FIELD(m_unk838) FIELD(unk_584) FIELD(m_unkA29) \
    FIELD(m_unkA99) FIELD(m_unkAngle1) FIELD(m_unkBool5) \
    FIELD(m_unkUnused) FIELD(m_unkUnused2) FIELD(m_unkUnused3) \
    FIELD(m_useLandParticles0) FIELD(m_vehicleSize) FIELD(m_wasJumpBuffered) \
    FIELD(m_wasOnSlope) FIELD(m_wasRobotJump) FIELD(m_wasTeleported) \
    FIELD(m_xVelocityRelated) FIELD(m_xVelocityRelated2) FIELD(m_yStart) \
    FIELD(m_yVelocity) FIELD(m_yVelocityBeforeSlope) FIELD(m_yVelocityRelated) \
    FIELD(m_yVelocityRelated3)

namespace grape::snap {

template <class Member>
using Stored = std::remove_cvref_t<Member>;

struct PlayerState {
#define GRAPE_PLAYER_STATE_SLOT(name) \
    Stored<decltype(std::declval<PlayerObject&>().name)> name{};
    GRAPE_PLAYER_STATE_FIELDS(GRAPE_PLAYER_STATE_SLOT)
#undef GRAPE_PLAYER_STATE_SLOT

    float m_rotation = 0.0f;
    std::vector<cocos2d::CCObject*> m_touchingRings;

    static PlayerState read(PlayerObject* player);
    void readFrom(PlayerObject* player);
    void writeTo(PlayerObject* player) const;
};

}
