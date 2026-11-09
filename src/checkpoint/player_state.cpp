#include "player_state.hpp"

namespace grape::snap {

PlayerState PlayerState::read(PlayerObject* player) {
    PlayerState state;
    state.readFrom(player);
    return state;
}

void PlayerState::readFrom(PlayerObject* player) {
#define GRAPE_PLAYER_STATE_READ(name) name = player->name;
    GRAPE_PLAYER_STATE_FIELDS(GRAPE_PLAYER_STATE_READ)
#undef GRAPE_PLAYER_STATE_READ
    m_rotation = player->getRotation();
    auto* rings = player->m_touchingRings;
    m_touchingRings.clear();
    m_touchingRings.reserve(rings->count());
    for (unsigned int i = 0; i < rings->count(); ++i)
        m_touchingRings.push_back(rings->objectAtIndex(i));
    m_isDead = false;
}

void PlayerState::writeTo(PlayerObject* player) const {
    player->setPosition(m_position);
    player->setRotation(m_rotation);
#define GRAPE_PLAYER_STATE_WRITE(name) player->name = name;
    GRAPE_PLAYER_STATE_FIELDS(GRAPE_PLAYER_STATE_WRITE)
#undef GRAPE_PLAYER_STATE_WRITE
    auto* rings = player->m_touchingRings;
    rings->removeAllObjects();
    for (auto* ring : m_touchingRings) rings->addObject(ring);
}

}
