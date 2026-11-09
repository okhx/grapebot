#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace grape::assist::foresight {

inline constexpr int MAX_TOGGLES = 16;

struct Schedule {
    std::array<int16_t, MAX_TOGGLES> m_steps{};
    int m_count = 0;

    bool push(int step) {
        if (m_count >= MAX_TOGGLES) return false;
        if (m_count && step <= m_steps[m_count - 1]) return false;
        m_steps[m_count++] = static_cast<int16_t>(step);
        return true;
    }
    int size() const { return m_count; }
    int first() const { return m_count ? m_steps[0] : -1; }
    int last() const { return m_count ? m_steps[m_count - 1] : -1; }
    bool togglesAt(int step) const {
        for (int i = 0; i < m_count; ++i)
            if (m_steps[i] == step) return true;
        return false;
    }
    uint64_t key() const {
        uint64_t hash = 1469598103934665603ull ^ static_cast<uint64_t>(m_count);
        for (int i = 0; i < m_count; ++i)
            hash = (hash ^ static_cast<uint16_t>(m_steps[i])) * 1099511628211ull;
        return hash;
    }
};

struct Outcome {
    int m_survived = 0;
    bool m_dead = false;
};

struct Candidate {
    Schedule m_plan;
    Outcome m_outcome;
};

constexpr bool safe(const Outcome& outcome, int horizon) {
    return !outcome.m_dead && outcome.m_survived >= horizon;
}

inline bool better(const Candidate& a, const Candidate& b, int horizon) {
    const int survivedA = std::min(a.m_outcome.m_survived, horizon);
    const int survivedB = std::min(b.m_outcome.m_survived, horizon);
    if (survivedA != survivedB) return survivedA > survivedB;
    const int firstA = a.m_plan.size() ? a.m_plan.first() : horizon;
    const int firstB = b.m_plan.size() ? b.m_plan.first() : horizon;
    if (firstA != firstB) return firstA > firstB;
    return a.m_plan.size() < b.m_plan.size();
}

inline constexpr std::array<int, 18> DEATH_LEADS = {
    1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 18, 22, 27, 33, 40, 48, 58, 70};

struct Limits {
    int m_horizon = 200;
    int m_budget = 64;
    int m_beam = 4;
    int m_commit = 100;
};

struct Result {
    Candidate m_best;
    bool m_safe = false;
    int m_simulations = 0;
};

template <class Simulate, class Allowed>
Result search(Simulate&& simulate, const Limits& limits, Allowed&& allowed,
              const std::vector<Schedule>& seeds = {}) {
    Result result;
    const int horizon = std::max(1, limits.m_horizon);
    std::unordered_set<uint64_t> seen;
    std::vector<Candidate> frontier;
    bool haveBest = false;

    const auto evaluate = [&](const Schedule& plan) -> bool {
        if (result.m_simulations >= limits.m_budget) return false;
        if (!seen.insert(plan.key()).second) return false;
        if (!allowed(plan)) return false;
        Candidate candidate{plan, simulate(plan)};
        ++result.m_simulations;
        if (!haveBest || better(candidate, result.m_best, horizon)) {
            result.m_best = candidate;
            haveBest = true;
        }
        frontier.push_back(candidate);
        return true;
    };

    for (const auto& seed : seeds) evaluate(seed);
    evaluate(Schedule{});
    if (haveBest && safe(result.m_best.m_outcome, horizon)) {
        result.m_safe = true;
        return result;
    }

    std::vector<Candidate> expanded;
    while (result.m_simulations < limits.m_budget && !frontier.empty()) {
        std::sort(frontier.begin(), frontier.end(), [&](const Candidate& a, const Candidate& b) {
            return better(a, b, horizon);
        });
        std::vector<Candidate> parents;
        for (auto& candidate : frontier) {
            if (static_cast<int>(parents.size()) >= limits.m_beam) break;
            if (safe(candidate.m_outcome, horizon)) continue;
            if (candidate.m_plan.size() >= MAX_TOGGLES) continue;
            parents.push_back(candidate);
        }
        frontier.clear();
        if (parents.empty()) break;

        bool foundSafe = false;
        for (const auto& parent : parents) {
            const int death = std::min(parent.m_outcome.m_survived, horizon - 1);
            for (int lead : DEATH_LEADS) {
                const int step = death - lead;
                if (step < 0) break;
                if (step <= parent.m_plan.last()) break;
                Schedule child = parent.m_plan;
                if (!child.push(step)) continue;
                if (!evaluate(child)) {
                    if (result.m_simulations >= limits.m_budget) break;
                    continue;
                }
                if (safe(frontier.back().m_outcome, horizon)) foundSafe = true;
            }
            if (result.m_simulations >= limits.m_budget) break;
        }
        if (foundSafe) break;
    }

    result.m_safe = haveBest && safe(result.m_best.m_outcome, horizon);
    return result;
}

class Tabu {
    uint64_t m_from = 0;
    uint64_t m_until = 0;
    std::vector<std::vector<uint64_t>> m_forbidden;

   public:
    bool active(uint64_t frame) const { return !m_forbidden.empty() && frame <= m_until; }
    uint64_t from() const { return m_from; }
    uint64_t until() const { return m_until; }
    size_t count() const { return m_forbidden.size(); }

    void clear() {
        m_forbidden.clear();
        m_from = m_until = 0;
    }

    void forbid(uint64_t from, uint64_t until, std::vector<uint64_t> toggles) {
        if (m_forbidden.empty() || from != m_from) {
            m_forbidden.clear();
            m_from = from;
        }
        m_until = std::max(m_until, until);
        std::erase_if(toggles, [&](uint64_t frame) { return frame < from || frame > until; });
        m_forbidden.push_back(std::move(toggles));
    }

    bool allows(const std::vector<uint64_t>& executed, uint64_t now, const Schedule& plan) const {
        if (m_forbidden.empty() || now > m_until) return true;
        std::vector<uint64_t> path;
        for (uint64_t frame : executed)
            if (frame >= m_from && frame <= m_until && frame < now) path.push_back(frame);
        for (int i = 0; i < plan.size(); ++i) {
            const uint64_t frame = now + static_cast<uint64_t>(plan.m_steps[i]);
            if (frame <= m_until) path.push_back(frame);
        }
        for (const auto& forbidden : m_forbidden) {
            const bool samePrefix = std::equal(path.begin(), path.end(), forbidden.begin(),
                                               forbidden.begin() + std::min(path.size(), forbidden.size()));
            if (samePrefix && path.size() == forbidden.size()) return false;
        }
        return true;
    }
};

class DoomMemory {
    std::unordered_set<uint64_t> m_states;

   public:
    static constexpr size_t MAX_STATES = 1 << 16;

    bool add(uint64_t key) {
        if (m_states.size() >= MAX_STATES) return false;
        return m_states.insert(key).second;
    }
    bool contains(uint64_t key) const { return m_states.count(key) != 0; }
    size_t size() const { return m_states.size(); }
    void clear() { m_states.clear(); }
};

struct RewindPolicy {
    uint64_t m_spot = UINT64_MAX;
    int m_attempts = 0;

    int depthFor(uint64_t deadEndFrame, int baseFrames, int historyFrames, uint64_t sameSpotWindow) {
        const bool sameSpot = m_spot != UINT64_MAX &&
                              (deadEndFrame > m_spot ? deadEndFrame - m_spot : m_spot - deadEndFrame) <=
                                  sameSpotWindow;
        m_attempts = sameSpot ? m_attempts + 1 : 0;
        if (!sameSpot) m_spot = deadEndFrame;
        const int depth = std::max(2, baseFrames) << std::min(m_attempts, 6);
        return std::min(depth, historyFrames - 1);
    }

    void reset() {
        m_spot = UINT64_MAX;
        m_attempts = 0;
    }
};

class Controller {
    std::vector<uint64_t> m_plan;
    std::vector<uint64_t> m_executed;
    Tabu m_tabu;
    RewindPolicy m_policy;
    DoomMemory m_doom;

   public:
    static constexpr int MAX_ATTEMPTS_PER_SPOT = 96;
    static constexpr int ATTEMPTS_WITHOUT_LEARNING = 8;
    static constexpr int FOCUS_FACTOR = 4;

    struct Step {
        bool m_toggle = false;
        int m_rewind = 0;
        uint64_t m_deadEnd = 0;
        bool m_planned = false;
        bool m_focused = false;
    };

    const std::vector<uint64_t>& plan() const { return m_plan; }
    const Tabu& tabu() const { return m_tabu; }
    const DoomMemory& doom() const { return m_doom; }
    bool doomed(uint64_t stateKey) const { return m_doom.contains(stateKey); }

    void reset() {
        m_plan.clear();
        m_executed.clear();
        m_tabu.clear();
        m_policy.reset();
    }

    void forget() {
        reset();
        m_doom.clear();
    }

    void toggled(uint64_t frame) {
        m_executed.push_back(frame);
        if (m_executed.size() > 4096)
            m_executed.erase(m_executed.begin(), m_executed.begin() + 1024);
    }

    void rewound(uint64_t restored, uint64_t deadEnd) {
        std::vector<uint64_t> window;
        for (uint64_t frame : m_executed)
            if (frame >= restored && frame <= deadEnd) window.push_back(frame);
        m_tabu.forbid(restored, deadEnd, std::move(window));
        std::erase_if(m_executed, [restored](uint64_t frame) { return frame >= restored; });
        m_plan.clear();
    }

    template <class Simulate>
    Step step(uint64_t now, Simulate&& simulate, const Limits& limits, int historyFrames,
              int rewindBase, uint64_t stateKey) {
        Step result;
        std::erase_if(m_plan, [now](uint64_t frame) { return frame < now; });
        if (m_tabu.count() && now > m_tabu.until() + static_cast<uint64_t>(limits.m_horizon)) {
            m_tabu.clear();
            m_policy.reset();
        }

        const auto relative = [&](const std::vector<uint64_t>& absolute) {
            Schedule schedule;
            for (uint64_t frame : absolute) {
                const uint64_t offset = frame - now;
                if (offset >= static_cast<uint64_t>(limits.m_horizon)) break;
                schedule.push(static_cast<int>(offset));
            }
            return schedule;
        };
        const auto allowed = [&](const Schedule& plan) { return m_tabu.allows(m_executed, now, plan); };

        const Schedule current = relative(m_plan);
        const bool currentAllowed = allowed(current);
        const Outcome currentOutcome = currentAllowed ? simulate(current) : Outcome{0, true};
        if (!safe(currentOutcome, limits.m_horizon)) {
            std::vector<Schedule> seeds;
            if (currentAllowed) {
                Schedule trimmed;
                for (int i = 0; i < current.size(); ++i)
                    if (current.m_steps[i] < currentOutcome.m_survived) trimmed.push(current.m_steps[i]);
                seeds.push_back(trimmed);
            }
            Result found = search(simulate, limits, allowed, seeds);
            result.m_planned = true;
            const auto usableResult = [&](const Result& candidate) {
                return candidate.m_safe || candidate.m_best.m_outcome.m_survived >= limits.m_commit;
            };
            if (!usableResult(found)) {
                Limits focused = limits;
                focused.m_budget = limits.m_budget * FOCUS_FACTOR;
                focused.m_beam = limits.m_beam * 2;
                seeds.push_back(found.m_best.m_plan);
                const Result deeper = search(simulate, focused, allowed, seeds);
                result.m_focused = true;
                if (better(deeper.m_best, found.m_best, limits.m_horizon)) found = deeper;
            }
            const bool usable = usableResult(found);
            if (usable) {
                m_plan.clear();
                for (int i = 0; i < found.m_best.m_plan.size(); ++i)
                    m_plan.push_back(now + static_cast<uint64_t>(found.m_best.m_plan.m_steps[i]));
            } else {
                const bool learned = m_doom.add(stateKey);
                const uint64_t deadEnd = now + static_cast<uint64_t>(std::max(0, found.m_best.m_outcome.m_survived));
                const int depth = m_policy.depthFor(deadEnd, rewindBase, historyFrames,
                                                    static_cast<uint64_t>(limits.m_horizon));
                const bool worthIt = learned || m_policy.m_attempts < ATTEMPTS_WITHOUT_LEARNING;
                if (depth >= 1 && worthIt && m_policy.m_attempts < MAX_ATTEMPTS_PER_SPOT) {
                    result.m_rewind = depth;
                    result.m_deadEnd = deadEnd;
                    return result;
                }
                m_plan.clear();
                for (int i = 0; i < found.m_best.m_plan.size(); ++i)
                    m_plan.push_back(now + static_cast<uint64_t>(found.m_best.m_plan.m_steps[i]));
            }
        }

        if (!m_plan.empty() && m_plan.front() == now) {
            m_plan.erase(m_plan.begin());
            result.m_toggle = true;
            toggled(now);
        }
        return result;
    }
};

}
