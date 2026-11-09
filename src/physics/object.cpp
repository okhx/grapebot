#include "object.hpp"

namespace phys {

bool hasBeenActivatedByPlayer(PlayerObject* player, EnhancedGameObject* object) {
    const bool reusable = player->m_isPlatformer ? object->m_isNoMultiActivate : object->m_isMultiActivate;
    if (reusable) return false;
    return player->m_isSecondPlayer ? object->m_activatedByPlayer2 : object->m_activatedByPlayer1;
}

}
