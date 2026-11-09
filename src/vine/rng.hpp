#ifndef GRAPE_VINE_RNG_HPP
#define GRAPE_VINE_RNG_HPP

#include <cstdint>

namespace grape::vine {

struct RngWords {
    uint64_t m_main = 0;
    uint64_t m_shake = 0;
    uint64_t m_teleport = 0;
};

class RngCore {
    uint64_t m_main = 0;
    uint64_t m_shake = 0;
    uint64_t m_teleport = 0;

    uint64_t m_baseSeed = 0;
    uint64_t m_attemptSeed = 0;

   public:
    static constexpr uint64_t lcg(uint64_t state) {
        return 214013 * state + 2531011;
    }

    uint64_t drawShake() { return m_shake = (lcg(m_shake) >> 16) & 0x7FFF; }

    uint64_t drawTeleport() {
        return m_teleport = (lcg(m_teleport) >> 16) & 0x7FFF;
    }

    RngWords capture() const { return {m_main, m_shake, m_teleport}; }

    void restore(const RngWords& words) {
        m_main = words.m_main;
        m_shake = words.m_shake;
        m_teleport = words.m_teleport;
    }

    void beginAttempt(uint64_t seed) {
        m_attemptSeed = seed;
        m_main = seed;
        m_shake = seed & 0x7FFF;
    }

    template <class Range>
    static void applyVariance(Range&& variance, uint64_t seed) {
        uint64_t state = seed;
        for (auto& value : variance) {
            value = static_cast<float>(state & 0xFFFF) / 32768.0f - 1.0f;
            state = lcg(state);
        }
    }

    uint64_t main() const { return m_main; }
    void setMain(uint64_t state) { m_main = state; }
    uint64_t shake() const { return m_shake; }
    uint64_t teleport() const { return m_teleport; }

    uint64_t baseSeed() const { return m_baseSeed; }
    void setBaseSeed(uint64_t seed) { m_baseSeed = seed; }

    uint64_t attemptSeed() const { return m_attemptSeed; }
    void setAttemptSeed(uint64_t seed) { m_attemptSeed = seed; }
};

}

#endif
