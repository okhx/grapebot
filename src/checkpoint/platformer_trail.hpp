#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace grape::snap {

template <class Respawn, class Marker>
class PlatformerTrail {
   public:
    struct Stop {
        Respawn m_respawn;
        Marker m_marker;
    };

    void push(Respawn respawn, Marker marker) { m_stops.push_back({respawn, marker}); }

    bool empty() const { return m_stops.empty(); }
    std::size_t depth() const { return m_stops.size(); }
    Respawn latestRespawn() const { return m_stops.back().m_respawn; }

    template <class Deactivate>
    void rewindTo(std::size_t depth, Deactivate&& deactivate) {
        while (m_stops.size() > depth) {
            const Marker leaving = m_stops.back().m_marker;
            m_stops.pop_back();
            const bool stillReached = std::any_of(
                m_stops.begin(), m_stops.end(),
                [&](const Stop& stop) { return stop.m_marker == leaving; });
            if (!stillReached) deactivate(leaving);
        }
    }

    template <class Deactivate>
    void abandon(bool markersWereLoaded, Deactivate&& deactivate) {
        while (!m_stops.empty()) {
            if (markersWereLoaded) deactivate(m_stops.back().m_marker);
            m_stops.pop_back();
        }
    }

   private:
    std::vector<Stop> m_stops;
};

}
