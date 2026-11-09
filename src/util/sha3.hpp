#ifndef UTIL_SHA3_HPP
#define UTIL_SHA3_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace grape::hashing {
namespace detail {

inline constexpr std::size_t kLanes = 25;
inline constexpr std::size_t kRounds = 24;
inline constexpr std::size_t kSha3_512Rate = 72;

inline constexpr std::array<std::uint64_t, kRounds> kRoundConstants{
    0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808aull,
    0x8000000080008000ull, 0x000000000000808bull, 0x0000000080000001ull,
    0x8000000080008081ull, 0x8000000000008009ull, 0x000000000000008aull,
    0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000aull,
    0x000000008000808bull, 0x800000000000008bull, 0x8000000000008089ull,
    0x8000000000008003ull, 0x8000000000008002ull, 0x8000000000000080ull,
    0x000000000000800aull, 0x800000008000000aull, 0x8000000080008081ull,
    0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull,
};

inline constexpr std::array<unsigned, kLanes> kRotations{
    0,  1,  62, 28, 27, 36, 44, 6,  55, 20, 3, 10, 43,
    25, 39, 41, 45, 15, 21, 8,  18, 2,  61, 56, 14};
inline constexpr std::array<std::size_t, kLanes> kPiLane{
    0,  6,  12, 18, 24, 3, 9,  10, 16, 22, 1, 7, 13,
    19, 20, 4,  5,  11, 17, 23, 2,  8,  14, 15, 21};

constexpr std::uint64_t rotateLeft(std::uint64_t value, unsigned bits) {
    return bits == 0 ? value : (value << bits) | (value >> (64u - bits));
}

inline void keccakF(std::array<std::uint64_t, kLanes>& state) {
    for (std::size_t round = 0; round < kRounds; ++round) {
        std::array<std::uint64_t, 5> column{};
        for (std::size_t x = 0; x < 5; ++x)
            column[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^
                        state[x + 20];
        for (std::size_t x = 0; x < 5; ++x) {
            const auto delta =
                column[(x + 4) % 5] ^ rotateLeft(column[(x + 1) % 5], 1);
            for (std::size_t y = 0; y < 25; y += 5) state[y + x] ^= delta;
        }

        std::array<std::uint64_t, kLanes> permuted{};
        for (std::size_t i = 0; i < kLanes; ++i)
            permuted[i] = rotateLeft(state[kPiLane[i]], kRotations[kPiLane[i]]);

        for (std::size_t y = 0; y < 25; y += 5)
            for (std::size_t x = 0; x < 5; ++x)
                state[y + x] = permuted[y + x] ^
                               (~permuted[y + (x + 1) % 5] & permuted[y + (x + 2) % 5]);

        state[0] ^= kRoundConstants[round];
    }
}

}

[[nodiscard]] inline std::array<std::uint8_t, 64> sha3_512(
    std::span<const std::uint8_t> data) {
    std::array<std::uint64_t, detail::kLanes> state{};
    constexpr auto rate = detail::kSha3_512Rate;

    std::size_t offset = 0;
    std::array<std::uint8_t, rate> block{};
    for (bool done = false; !done;) {
        block.fill(0);
        std::size_t taken = 0;
        while (taken < rate && offset < data.size()) block[taken++] = data[offset++];
        if (taken < rate) {
            block[taken] = 0x06;
            block[rate - 1] |= 0x80;
            done = true;
        }
        for (std::size_t lane = 0; lane < rate / 8; ++lane) {
            std::uint64_t value = 0;
            for (std::size_t byte = 0; byte < 8; ++byte)
                value |= static_cast<std::uint64_t>(block[lane * 8 + byte]) << (8 * byte);
            state[lane] ^= value;
        }
        detail::keccakF(state);
    }

    std::array<std::uint8_t, 64> digest{};
    for (std::size_t i = 0; i < digest.size(); ++i)
        digest[i] = static_cast<std::uint8_t>(state[i / 8] >> (8 * (i % 8)));
    return digest;
}

[[nodiscard]] inline std::string sha3_512Hex(std::span<const std::uint8_t> data) {
    static constexpr char kDigits[] = "0123456789abcdef";
    const auto digest = sha3_512(data);
    std::string result(digest.size() * 2, '0');
    for (std::size_t i = 0; i < digest.size(); ++i) {
        result[i * 2] = kDigits[digest[i] >> 4];
        result[i * 2 + 1] = kDigits[digest[i] & 15];
    }
    return result;
}

}

#endif
