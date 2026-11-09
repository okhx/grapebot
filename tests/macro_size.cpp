#include "vine/format.hpp"
#include "vine/slc.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>

using namespace grape::vine;

static constexpr size_t TARGET = 600'000;

class Generator {
    std::mt19937_64 m_rng;
    Inputs m_inputs;
    uint64_t m_frame = 0;

    uint64_t range(uint64_t low, uint64_t high) {
        return std::uniform_int_distribution<uint64_t>(low, high)(m_rng);
    }

    bool chance(double probability) {
        return std::bernoulli_distribution(probability)(m_rng);
    }

    bool full() const { return m_inputs.m_events.size() >= TARGET; }

    void press(uint64_t hold, bool player2) {
        const uint64_t start = m_frame;
        m_inputs.addEvent(start, EventKind::Jump, true, player2);
        if (full()) return;
        Event release(start + hold, EventKind::Jump, false, player2);
        release.m_swift = hold == 0;
        m_inputs.m_events.push_back(release);
    }

    void cube(int clicks) {
        for (int i = 0; i < clicks && !full(); ++i) {
            m_frame += range(15, 90);
            const uint64_t hold = chance(.03) ? 0 : range(3, 12);
            press(hold, false);
            m_frame += hold;
        }
    }

    void flight(int clicks) {
        for (int i = 0; i < clicks && !full(); ++i) {
            m_frame += range(3, 50);
            const uint64_t hold = range(5, 60);
            press(hold, false);
            m_frame += hold;
        }
    }

    void spam(int clicks, bool perfect) {
        const uint64_t hold = range(1, 4);
        const uint64_t gap = range(1, 4);
        for (int i = 0; i < clicks && !full(); ++i) {
            m_frame += perfect ? gap : gap + range(0, 2);
            const uint64_t held = perfect ? hold : hold + range(0, 1);
            press(held, false);
            m_frame += held;
        }
    }

    void dual(int clicks) {
        uint64_t p1 = m_frame;
        uint64_t p2 = m_frame + range(0, 20);
        std::vector<Event> burst;
        for (int i = 0; i < clicks; ++i) {
            p1 += range(10, 70);
            const uint64_t h1 = range(4, 30);
            burst.emplace_back(p1, EventKind::Jump, true, false);
            burst.emplace_back(p1 + h1, EventKind::Jump, false, false);
            p1 += h1;
            p2 += range(10, 70);
            const uint64_t h2 = range(4, 30);
            burst.emplace_back(p2, EventKind::Jump, true, true);
            burst.emplace_back(p2 + h2, EventKind::Jump, false, true);
            p2 += h2;
        }
        std::stable_sort(burst.begin(), burst.end(),
                         [](const Event& a, const Event& b) {
                             return a.m_frame < b.m_frame;
                         });
        for (const auto& event : burst) {
            if (full()) break;
            m_inputs.m_events.push_back(event);
        }
        m_frame = std::max(p1, p2);
    }

    void attempt() {
        m_frame += range(1, 30);
        m_inputs.m_events.emplace_back(m_frame, EventKind::Death, m_rng());
        if (full()) return;
        m_frame += range(10, 120);
        m_inputs.m_events.emplace_back(m_frame, EventKind::Restart, m_rng());
    }

   public:
    explicit Generator(uint64_t seed) : m_rng(seed) {
        m_inputs.m_seed = seed;
        m_inputs.m_tps = 240.0;
    }

    Inputs run() {
        while (!full()) {
            const uint64_t pick = range(0, 99);
            if (pick < 40) cube(static_cast<int>(range(5, 60)));
            else if (pick < 65) flight(static_cast<int>(range(5, 60)));
            else if (pick < 75) spam(static_cast<int>(range(20, 200)), true);
            else if (pick < 85) spam(static_cast<int>(range(20, 200)), false);
            else if (pick < 95) dual(static_cast<int>(range(10, 60)));
            else if (pick < 99) attempt();
            else if (!full()) m_inputs.setTps(m_frame, chance(.5) ? 240.0 : 480.0, false);
        }
        m_inputs.m_events.resize(TARGET);
        m_inputs.sort();
        return std::move(m_inputs);
    }
};

static size_t gdrJsonSize(const Inputs& inputs) {
    size_t size = 200;
    for (const auto& event : inputs.m_events) {
        size += std::snprintf(nullptr, 0,
                              "{\"frame\":%llu,\"btn\":1,\"2p\":%s,\"down\":%s},",
                              static_cast<unsigned long long>(event.m_frame),
                              event.m_player2 ? "true" : "false",
                              event.m_holding ? "true" : "false");
    }
    return size;
}

static size_t plainTextSize(const Inputs& inputs) {
    size_t size = 4;
    for (const auto& event : inputs.m_events) {
        size += std::snprintf(nullptr, 0, "%llu %d %d\n",
                              static_cast<unsigned long long>(event.m_frame),
                              event.m_holding ? 1 : 0, event.m_player2 ? 1 : 0);
    }
    return size;
}

static bool same(const Inputs& a, const Inputs& b) {
    if (a.m_events.size() != b.m_events.size()) return false;
    for (size_t i = 0; i < a.m_events.size(); ++i) {
        const auto& x = a.m_events[i];
        const auto& y = b.m_events[i];
        if (x.m_frame != y.m_frame || x.m_type != y.m_type ||
            x.m_holding != y.m_holding || x.m_player2 != y.m_player2 ||
            x.m_swift != y.m_swift || x.m_seed != y.m_seed || x.m_tps != y.m_tps)
            return false;
    }
    return true;
}

static void row(const char* name, size_t bytes, size_t events) {
    std::printf("  %-28s %10zu bytes  %8.1f KB  %6.2f bits/input\n", name, bytes,
                bytes / 1024.0, bytes * 8.0 / static_cast<double>(events));
}

int main(int argc, char** argv) {
    const Inputs inputs = Generator(0x5eed).run();
    const size_t count = inputs.m_events.size();

    const auto started = std::chrono::steady_clock::now();
    const auto squeezed = encode(inputs);
    const auto encoded = std::chrono::steady_clock::now();
    const auto back = decode(*squeezed);
    const auto decoded = std::chrono::steady_clock::now();
    const auto packed = encodePacked(inputs);

    if (!squeezed || !packed || !back || !same(inputs, *back)) {
        std::puts("FAILED: v4 round trip");
        return 1;
    }
    const auto packedBack = decode(*packed);
    if (!packedBack || !same(inputs, *packedBack)) {
        std::puts("FAILED: v3 round trip");
        return 1;
    }

    std::printf("Synthetic macro: %zu inputs, %llu frames (%.1f hours at 240 TPS)\n",
                count, static_cast<unsigned long long>(inputs.lastFrame()),
                inputs.lastFrame() / 240.0 / 3600.0);
    row("GDR JSON (estimate)", gdrJsonSize(inputs), count);
    row("Plain text", plainTextSize(inputs), count);
    row("In memory (std::vector)", count * sizeof(Event), count);
    row(".vine v3 (bit-packed)", packed->size(), count);
    row(".vine v4 (range coded)", squeezed->size(), count);
    std::printf("  v4 is %.1f%% of v3; encode %lld ms, decode %lld ms\n",
                100.0 * squeezed->size() / packed->size(),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(encoded - started).count()),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(decoded - encoded).count()));

    if (squeezed->size() >= packed->size()) {
        std::puts("FAILED: v4 is not smaller than v3");
        return 1;
    }

    if (argc > 1) {
        const std::string base = argv[1];
        std::ofstream(base + ".vine", std::ios::binary)
            .write(reinterpret_cast<const char*>(squeezed->data()),
                   static_cast<std::streamsize>(squeezed->size()));
        std::ofstream(base + ".v3.vine", std::ios::binary)
            .write(reinterpret_cast<const char*>(packed->data()),
                   static_cast<std::streamsize>(packed->size()));
        std::printf("Wrote %s.vine and %s.v3.vine\n", base.c_str(), base.c_str());
    }
    std::puts("macro_size ok");
}
