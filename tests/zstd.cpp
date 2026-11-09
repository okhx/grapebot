#include "vine/zstd.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace grape::vine;

static int g_checks = 0;

#define CHECK(condition)                                     \
    do {                                                     \
        ++g_checks;                                          \
        if (!(condition)) {                                  \
            std::printf("FAILED at line %d: %s\n", __LINE__, \
                        #condition);                         \
            return 1;                                        \
        }                                                    \
    } while (false)

static std::vector<uint8_t> readFile(const char* name) {
    const auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures" / "zstd" / name;
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

int main() {
    const struct {
        const char* m_compressed;
        const char* m_raw;
    } vectors[] = {
        {"text-1.zst", "text.raw"},
        {"text-19.zst", "text.raw"},
        {"binary-22.zst", "binary.raw"},
        {"two-frames.zst", "two-frames.raw"},
    };
    for (const auto& vector : vectors) {
        const auto compressed = readFile(vector.m_compressed);
        const auto raw = readFile(vector.m_raw);
        CHECK(!compressed.empty() && !raw.empty());
        const auto out = zstd::decompress(compressed, 1u << 24);
        if (!out) std::printf("%s: %s\n", vector.m_compressed, out.error().c_str());
        CHECK(out.has_value());
        CHECK(*out == raw);

        const auto tooSmall = zstd::decompress(compressed, raw.size() / 2);
        CHECK(!tooSmall.has_value());

        for (size_t cut = 1; cut < compressed.size(); cut += 97) {
            const auto truncated = zstd::decompress(std::span(compressed.data(), cut), 1u << 24);
            CHECK(!truncated.has_value() || truncated->size() < raw.size());
        }
        auto flipped = compressed;
        uint32_t state = 5;
        for (int round = 0; round < 300; ++round) {
            state = state * 1664525u + 1013904223u;
            flipped[(state >> 8) % flipped.size()] ^= static_cast<uint8_t>(1u << (state & 7));
            (void)zstd::decompress(flipped, 1u << 24);
        }
    }

    const std::vector<uint8_t> notZstd = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(!zstd::decompress(notZstd, 1024).has_value());
    CHECK(zstd::decompress({}, 1024).has_value());

    std::printf("zstd: %d checks passed\n", g_checks);
}
