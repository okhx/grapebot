#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>

class GameScheduler {
   public:
    using Work = std::function<void()>;
    using Ticket = uint64_t;

    GameScheduler() = default;
    GameScheduler(const GameScheduler&) = delete;
    GameScheduler& operator=(const GameScheduler&) = delete;

    Ticket after(const Work& work, double seconds);
    Ticket every(const Work& work, double seconds);
    void retune(Ticket ticket, double seconds);
    void drop(Ticket ticket);
    void pump(float dt);

   private:
    struct Job {
        double m_spacing = 0.0;
        double m_banked = 0.0;
        bool m_repeats = false;
        Work m_work;
    };

    void attach(const Work& work, double seconds, bool repeats);

    Ticket m_nextTicket = 0;
    std::unordered_map<Ticket, Job> m_queue;
};
