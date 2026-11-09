#pragma once

#include <array>
#include <deque>

class CPSCounter {
   public:
    int queryCPS(int player = 1);
    int queryMaxCPS(int player = 1);

    void clearActions();
    void pushAction(bool player2);
    void update();

   private:
    struct Clicks {
        std::deque<double> m_times;
        int m_peak = 0;
    };

    Clicks* clicksOf(int player);

    std::array<Clicks, 2> m_players;
};
