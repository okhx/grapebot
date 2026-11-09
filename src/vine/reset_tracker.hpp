#pragma once

#include <cstdint>

#include "format.hpp"

namespace grape::vine {

class ResetTracker {
   public:
    enum class Shape : uint8_t { Respawn, AfterDeath, FromScratch };

    void playerDied() { m_shape = Shape::AfterDeath; }

    void playerRestartedLevel() {
        if (m_shape == Shape::Respawn) m_shape = Shape::FromScratch;
        m_playerRestartedLevel = true;
    }

    void macroAskedForReset() { m_fromMacro = true; }

    bool fromMacro() const { return m_fromMacro; }
    bool levelWasRestarted() const { return m_playerRestartedLevel; }
    Shape shape() const { return m_shape; }

    bool restartsCleanly(bool playerArmed) const {
        return playerArmed || m_fromMacro;
    }

    EventKind eventToRecord() const {
        switch (m_shape) {
            case Shape::AfterDeath: return EventKind::Death;
            case Shape::FromScratch: return EventKind::RestartFull;
            default: return EventKind::Restart;
        }
    }

    void forget() { *this = {}; }

   private:
    Shape m_shape = Shape::Respawn;
    bool m_playerRestartedLevel = false;
    bool m_fromMacro = false;
};

}
