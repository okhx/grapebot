#include "cps.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>

CPSCounter::Clicks* CPSCounter::clicksOf(int player) {
    return player == 1 || player == 2 ? &m_players[player - 1] : nullptr;
}

int CPSCounter::queryCPS(int player) {
    const Clicks* clicks = clicksOf(player);
    return clicks ? static_cast<int>(clicks->m_times.size()) : -1;
}

int CPSCounter::queryMaxCPS(int player) {
    const Clicks* clicks = clicksOf(player);
    return clicks ? clicks->m_peak : -1;
}

void CPSCounter::clearActions() { m_players = {}; }

void CPSCounter::pushAction(bool player2) {
    auto* layer = GJBaseGameLayer::get();
    if (!layer) return;
    Clicks& clicks = m_players[player2 ? 1 : 0];
    clicks.m_times.push_back(layer->m_gameState.m_levelTime);
    clicks.m_peak = std::max(clicks.m_peak, static_cast<int>(clicks.m_times.size()));
}

void CPSCounter::update() {
    const auto idle = [](const Clicks& clicks) { return clicks.m_times.empty(); };
    if (std::ranges::all_of(m_players, idle)) return;
    auto* layer = GJBaseGameLayer::get();
    if (!layer) return;

    const double now = layer->m_gameState.m_levelTime;
    for (Clicks& clicks : m_players)
        while (!clicks.m_times.empty() && now - clicks.m_times.front() > 1.0) clicks.m_times.pop_front();
}
