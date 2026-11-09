#include "clock.hpp"

void GameScheduler::attach(const Work& work, const double seconds,
                           const bool repeats) {
    m_queue.insert_or_assign(++m_nextTicket,
                              Job{seconds, 0.0, repeats, work});
}

GameScheduler::Ticket GameScheduler::after(const Work& work,
                                           const double seconds) {
    attach(work, seconds, false);
    return m_nextTicket;
}

GameScheduler::Ticket GameScheduler::every(const Work& work,
                                           const double seconds) {
    attach(work, seconds, true);
    return m_nextTicket;
}

void GameScheduler::retune(const Ticket ticket, const double seconds) {
    const auto found = m_queue.find(ticket);
    if (found != m_queue.end()) {
        found->second.m_spacing = seconds;
    }
}

void GameScheduler::drop(const Ticket ticket) {
    m_queue.erase(ticket);
}

void GameScheduler::pump(const float dt) {
    for (auto it = m_queue.begin(); it != m_queue.end();) {
        Job& job = it->second;

        job.m_banked += dt;
        if (job.m_banked < job.m_spacing) {
            ++it;
            continue;
        }

        job.m_banked = 0.0;
        if (job.m_work) job.m_work();

        it = job.m_repeats ? std::next(it) : m_queue.erase(it);
    }
}
