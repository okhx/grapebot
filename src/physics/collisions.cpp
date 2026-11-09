#include "collisions.hpp"

#include <Geode/Enums.hpp>
#include <Geode/Geode.hpp>

#include "engine/engine.hpp"
#include "gravity.hpp"
#include "player.hpp"
#include "trajectory/trajectory.hpp"

namespace phys {

namespace {

using Type = GameObjectType;

auto& trajectoryState() { return GrapeEngine::get()->trajectory(); }

bool alreadyUsedBy(PlayerObject* player, EffectGameObject* object) {
    auto& state = trajectoryState();
    return state.playerHasActivated(player, object) || state.realPlayerHasActivated(player, object);
}

bool ignoredForCollision(GameObject* object) {
    switch (object->m_objectType) {
        case Type::Decoration:
        case Type::CollisionObject:
        case Type::SecretCoin:
        case Type::UserCoin:
        case Type::Collectible:
        case Type::EnterEffectObject:
            return true;
        default:
            break;
    }
    return object->m_objectID == 286 || object->m_objectID == 287 ||
           object->m_isGroupDisabled || object->m_isDisabled;
}

void addToBucket(std::vector<GameObject*>& bucket, int& used, int& capacity, GameObject* object) {
    if (used < capacity) {
        bucket.at(used) = object;
    } else {
        bucket.push_back(object);
        ++capacity;
    }
    ++used;
}

void notePortal(PlayerObject* player, EffectGameObject* portal) {
    player->m_lastPortalPos = portal->getPosition();
    player->m_lastActivatedPortal = portal;
    activateForTrajectory(portal, player);
}

void refreshBounds(PlayerObject* player) { (void)player->getObjectRect(); }

float padForce(Type pad, PlayerObject* player) {
    const bool belowRegular = !(player->m_vehicleSize >= 1.0);
    if (pad == Type::PinkJumpPad) {
        if (player->m_isShip) return 0.35f;
        if (player->m_isBird) return 0.4f;
        if (player->m_isBall || player->m_isSpider) return 0.7f;
        return 0.65f;
    }
    if (pad == Type::RedJumpPad) {
        if (player->m_isShip) return belowRegular ? 0.95f : 0.63f;
        if (player->m_isBird) return belowRegular ? 0.98f : 0.6f;
        return 1.25f;
    }
    return 1.0f;
}

void hitJumpPad(GJBaseGameLayer* layer, PlayerObject* player, EffectGameObject* pad) {
    if (!layer->canBeActivatedByPlayer(player, pad)) return;
    player->m_lastPortalPos = pad->getPosition();
    activateForTrajectory(pad, player);
    const float force = padForce(pad->m_objectType, player);
    player->m_lastActivatedPortal = pad;
    bumpPlayer(player, force, static_cast<int>(pad->m_objectType), false, pad);
}

void hitGravityPad(GJBaseGameLayer* layer, PlayerObject* player, EffectGameObject* pad) {
    const bool pointsDown = player->m_isSideways ? pad->isFacingLeft() : pad->isFacingDown();
    const bool usable = layer->canBeActivatedByPlayer(player, pad);
    if (player->m_isUpsideDown != pointsDown || !usable) return;
    if (pad->m_isReverse) player->reversePlayer(pad);
    notePortal(player, pad);
    propellPlayer(player, 0.8f, false, 10);
    flipGravity(player, !pointsDown);
    player->m_padRingRelated = true;
}

void touchRing(PlayerObject* player, GameObject* object, EffectGameObject* effect) {
    if (!player->m_touchingRings->containsObject(object)) player->m_touchingRings->addObject(object);
    player->m_touchedRings.insert(object->m_uniqueID);

    const bool flying = player->m_isShip || player->m_isBird || player->m_isDart || player->m_isSwing;
    auto* ring = static_cast<RingObject*>(object);
    if (flying || ring->m_isSpawnOnly) return;
    ringJump(player, ring);
    activateForTrajectory(effect, player);
}

void enterSizePortal(GJBaseGameLayer* layer, PlayerObject* player, EffectGameObject* portal, bool mini) {
    if (!layer->canBeActivatedByPlayer(player, portal)) return;
    notePortal(player, portal);
    togglePlayerScale(player, mini);
}

void enterModePortal(GJBaseGameLayer* layer, PlayerObject* player, EffectGameObject* portal) {
    if (!layer->canBeActivatedByPlayer(player, portal)) return;
    if (trajectoryState().playerHasActivated(player, portal)) return;
#ifndef GEODE_IS_MOBILE
    layer->playerWillSwitchMode(player, portal);
#endif
    const CCPoint anchor = player->getPosition();
    player->switchedToMode(portal->m_objectType);
    switch (portal->m_objectType) {
        case Type::ShipPortal: player->toggleFlyMode(true, true); break;
        case Type::BallPortal: player->toggleRollMode(true, true); break;
        case Type::UfoPortal: player->toggleBirdMode(true, true); break;
        case Type::WavePortal: player->toggleDartMode(true, true); break;
        case Type::SpiderPortal: player->toggleSpiderMode(true, true); break;
        case Type::SwingPortal: player->toggleSwingMode(true, true); break;
        case Type::RobotPortal: player->toggleRobotMode(true, true); break;
        default: break;
    }
    player->setPosition(anchor);
    player->m_iconSprite->setPosition(anchor);
    player->m_vehicleSprite->setPosition(anchor);
    notePortal(player, portal);
}

void pushedByForceBlock(PlayerObject* player, GameObject* object) {
    player->m_stateForce = 2;
    auto* block = static_cast<ForceBlockGameObject*>(object);
    const int id = block->m_forceID;
    if (id > 0) {
        auto found = player->m_touchedForceIds.find(id);
        if (found != player->m_touchedForceIds.end() && found->second) return;
        player->m_touchedForceIds.insert({id, true});
    }
    player->m_stateForceVector += block->calculateForceToTarget(player);
}

void touchSpecialBlock(PlayerObject* player, GameObject* object) {
    switch (object->m_objectID) {
        case 1859: player->m_stateHitHead = 2; break;
        case 1755: player->m_stateDartSlide = 2; break;
        case 1813: player->m_stateNoAutoJump = 2; break;
        case 1829:
            if (!player->m_isDashing) break;
            stopDashing(player);
            player->m_jumpBuffered = false;
            break;
        case 2866: player->m_stateFlipGravity = 2; break;
        case 2069:
        case 3645: pushedByForceBlock(player, object); break;
        default: break;
    }
}

bool touches(GJBaseGameLayer* layer, PlayerObject* player, const CCRect& playerBounds,
             GameObject* object) {
    const bool slope = object->m_objectType == Type::Slope;
    const CCRect bounds = slope ? object->getObjectRect(2.0, 2.0) : object->getObjectRect();
    const bool round = object->m_objectRadius > 0.0;
    if (round ? !layer->playerCircleCollision(player, object) : !playerBounds.intersectsRect(bounds))
        return false;

    bool overlapping = true;
    const bool orientedCheck = object->m_shouldUseOuterOb &&
                               (!layer->m_levelSettings->m_fixRadiusCollision || !round);
    if (orientedCheck) {
        OBB2D* objectBox = object->getOrientedBox();
        player->updateOrientedBox();
        overlapping = objectBox->overlaps1Way(static_cast<GameObject*>(player)->getOrientedBox());
    }
    if (slope) (void)object->getObjectRect();
    return overlapping;
}

void react(GJBaseGameLayer* layer, PlayerObject* player, GameObject* object, float dt) {
    auto* effect = static_cast<EffectGameObject*>(object);
    switch (object->m_objectType) {
        case Type::InverseGravityPortal:
        case Type::NormalGravityPortal:
            notePortal(player, effect);
            flipGravity(player, object->m_objectType == Type::InverseGravityPortal);
            refreshBounds(player);
            break;
        case Type::GravityTogglePortal:
            notePortal(player, effect);
            flipGravity(player, !player->m_isUpsideDown);
            break;
        case Type::TeleportPortal:
            if (!layer->canBeActivatedByPlayer(player, effect)) break;
            teleportPlayer(layer, static_cast<TeleportPortalObject*>(object), player);
            activateForTrajectory(effect, player);
            break;
        case Type::Slope:
            if (player->m_isSideways) {
                CCRect none{0.0, 0.0, 0.0, 0.0};
                player->handleRotatedCollisionInternal(dt, object, none, false, false, true);
            } else {
                player->collidedWithSlopeInternal(dt, object, false);
            }
            refreshBounds(player);
            break;
        case Type::CustomRing:
        case Type::DashRing:
        case Type::DropRing:
        case Type::GravityDashRing:
        case Type::GravityRing:
        case Type::GreenRing:
        case Type::PinkJumpRing:
        case Type::RedJumpRing:
        case Type::SpiderOrb:
        case Type::YellowJumpRing:
        case Type::TeleportOrb:
            touchRing(player, object, effect);
            break;
        case Type::YellowJumpPad:
        case Type::PinkJumpPad:
        case Type::RedJumpPad:
        case Type::SpiderPad:
            hitJumpPad(layer, player, effect);
            break;
        case Type::GravityPad:
            hitGravityPad(layer, player, effect);
            break;
        case Type::MiniSizePortal:
        case Type::RegularSizePortal:
            enterSizePortal(layer, player, effect, object->m_objectType == Type::MiniSizePortal);
            break;
        case Type::Special:
            touchSpecialBlock(player, object);
            break;
        case Type::CubePortal:
        case Type::ShipPortal:
        case Type::BallPortal:
        case Type::UfoPortal:
        case Type::WavePortal:
        case Type::SpiderPortal:
        case Type::SwingPortal:
        case Type::RobotPortal:
            enterModePortal(layer, player, effect);
            break;
        case Type::EnterEffectObject:
        case Type::Modifier:
            effect->activatedByPlayer(player);
            if (effect->m_isTouchTriggered) triggerObject(effect, layer, player);
            break;
        default:
            break;
    }
}

float speedPortalValue(int objectId) {
    switch (objectId) {
        case 200: return 0.7f;
        case 201: return 0.9f;
        case 202: return 1.1f;
        case 203: return 1.3f;
        case 1334: return 1.6f;
        default: return 0.0f;
    }
}

bool spawnTriggerTracked(int objectId) {
    return objectId == 2066 || objectId == 2900 || objectId == 3022 || objectId == 901;
}

void rotateGameplay(GJBaseGameLayer* layer, PlayerObject* player, RotateGameplayGameObject* trigger) {
    auto& state = layer->m_gameState;
    if (trigger->m_changeChannel) {
        state.m_currentChannel = trigger->m_targetChannelID;
        state.m_spawnChannelRelated1[trigger->m_targetChannelID] =
            trigger->m_groundDirection == 2 || trigger->m_groundDirection == 3;
    }
    if (trigger->m_channelOnly) return;

    const auto turn = [trigger](PlayerObject* who) {
        who->rotateGameplay(trigger->m_moveDirection, trigger->m_groundDirection,
                            trigger->m_editVelocity, trigger->m_velocityModX,
                            trigger->m_velocityModY, trigger->m_overrideVelocity,
                            trigger->m_dontSlide);
    };
    turn(player);

    auto& state2 = trajectoryState();
    if (!state.m_isDualMode || !state2.isFakePlayer(player)) return;
    PlayerObject* partner = state2.getOtherPlayer(player);
    if (!partner || partner == player || alreadyUsedBy(partner, trigger)) return;
    turn(partner);
    activateForTrajectory(trigger, partner);
}

}

void activateForTrajectory(EffectGameObject* object, PlayerObject* player) {
    trajectoryState().rememberActivatedObject(object, player);
}

void collisionCheckObjects(GJBaseGameLayer* layer, PlayerObject* player,
                           std::vector<GameObject*>* objects, int count, float dt) {
    if (count <= 0) return;
    const CCRect playerBounds = player->getObjectRect();

    for (int i = 0; i < count; ++i) {
        GameObject* object = objects->at(i);
        if (!object || ignoredForCollision(object)) continue;

        const Type type = object->m_objectType;
        if (type == Type::Solid || type == Type::Breakable) {
            addToBucket(layer->m_solidCollisionObjects, layer->m_solidCollisionObjectsCount,
                        layer->m_solidCollisionObjectsIndex, object);
            continue;
        }
        if (object == layer->m_anticheatSpike) continue;
        if (type == Type::Hazard || type == Type::AnimatedHazard) {
            addToBucket(layer->m_hazardCollisionObjects, layer->m_hazardCollisionObjectsCount,
                        layer->m_hazardCollisionObjectsIndex, object);
            continue;
        }

        auto* effect = static_cast<EffectGameObject*>(object);
        if (type != Type::Slope && alreadyUsedBy(player, effect)) continue;
        if (!touches(layer, player, playerBounds, object)) continue;
        react(layer, player, object, dt);
    }
}

void triggerObject(EffectGameObject* object, GJBaseGameLayer* layer, PlayerObject* player) {
    const int id = object->m_objectID;
    if (const float speed = speedPortalValue(id); speed != 0.0f) {
        *reinterpret_cast<float*>(&layer->m_gameState.m_timeModRelated) = speed;
    } else if (id == 2066 && !object->m_followCPP) {
        const bool second = trajectoryState().isFakePlayer2(player);
        if (object->m_targetPlayer2 == second) player->m_gravityMod = object->m_gravityValue;
    } else if (id == 2900) {
        rotateGameplay(layer, player, static_cast<RotateGameplayGameObject*>(object));
    } else if (id == 3022) {
        teleportPlayer(layer, static_cast<TeleportPortalObject*>(object), player);
    }
    activateForTrajectory(object, player);
}

void checkSpawnObjects(GJBaseGameLayer* layer, PlayerObject* player) {
    const int channel = layer->m_gameState.m_currentChannel;
    auto* objects = static_cast<cocos2d::CCArray*>(layer->m_spawnObjects->objectForKey(channel));
    if (!objects) return;

    const auto& starts = layer->m_gameState.m_spawnChannelRelated0;
    const auto& reversed = layer->m_gameState.m_spawnChannelRelated1;
    const auto start = starts.find(channel);
    const auto back = reversed.find(channel);
    const int first = start == starts.end() ? 0 : start->second;
    const bool backwards = back == reversed.end() ? false : back->second;

    const CCPoint here = player->getPosition();
    const auto passed = [&](const CCPoint& at) {
        const float mine = player->m_isSideways ? here.y : here.x;
        const float theirs = player->m_isSideways ? at.y : at.x;
        return backwards ? !(theirs < mine) : !(theirs > mine);
    };

    for (unsigned int i = static_cast<unsigned int>(first); i < objects->count(); ++i) {
        auto* trigger = static_cast<SpawnTriggerGameObject*>(objects->objectAtIndex(i));
        if (!spawnTriggerTracked(trigger->m_objectID)) continue;
        if (!passed(trigger->m_speedStart)) break;
        if (trigger->m_isGroupDisabled || alreadyUsedBy(player, trigger)) continue;
        if (!trigger->m_isTouchTriggered) triggerObject(trigger, layer, player);
    }
}

}
