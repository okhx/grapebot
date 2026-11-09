#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace grape::arm {

inline constexpr uint32_t A64_NOP = 0xD503201Fu;
inline constexpr uint16_t T16_NOP = 0xBF00u;
inline constexpr uint16_t T16_LDR_R0_R1 = 0x6808u;
inline constexpr unsigned A64_SCRATCH = 9;

constexpr uint32_t a64MoveWide(bool keep, unsigned reg, uint16_t imm,
                               unsigned shift) {
    return (keep ? 0xF2800000u : 0xD2800000u) | ((shift / 16u) << 21) |
           (static_cast<uint32_t>(imm) << 5) | reg;
}

constexpr uint32_t a64LoadWord(unsigned rt, unsigned rn, unsigned offset) {
    return 0xB9400000u | ((offset / 4u) << 10) | (rn << 5) | rt;
}

constexpr uint32_t a64LoadSingle(unsigned rt, unsigned rn, unsigned offset) {
    return 0xBD400000u | ((offset / 4u) << 10) | (rn << 5) | rt;
}

constexpr uint32_t a64Branch(uint64_t from, uint64_t to) {
    const int64_t words = (static_cast<int64_t>(to) -
                           static_cast<int64_t>(from)) / 4;
    return 0x14000000u | (static_cast<uint32_t>(words) & 0x3FFFFFFu);
}

constexpr uint32_t a64PageAddress(unsigned rd, uint64_t pc, uint64_t target) {
    const int64_t pages = static_cast<int64_t>(target >> 12) -
                          static_cast<int64_t>(pc >> 12);
    const uint32_t imm = static_cast<uint32_t>(pages) & 0x1FFFFFu;
    return 0x90000000u | ((imm & 3u) << 29) | ((imm >> 2) << 5) | rd;
}

constexpr std::array<uint16_t, 2> t32MoveHalf(bool top, unsigned rd,
                                              uint16_t imm) {
    const uint16_t i = (imm >> 11) & 1u;
    const uint16_t imm4 = imm >> 12;
    const uint16_t imm3 = (imm >> 8) & 7u;
    const uint16_t imm8 = imm & 0xFFu;
    return {static_cast<uint16_t>((top ? 0xF2C0u : 0xF240u) | (i << 10) |
                                  imm4),
            static_cast<uint16_t>((imm3 << 12) | (rd << 8) | imm8)};
}

template <std::size_t N>
struct Emitter {
    std::array<uint8_t, N> m_bytes{};
    std::size_t m_size = 0;

    constexpr void word(uint32_t value) {
        for (int i = 0; i < 4; ++i)
            m_bytes[m_size++] = static_cast<uint8_t>(value >> (8 * i));
    }
    constexpr void half(uint16_t value) {
        m_bytes[m_size++] = static_cast<uint8_t>(value);
        m_bytes[m_size++] = static_cast<uint8_t>(value >> 8);
    }
    constexpr void padA64() {
        while (m_size + 4 <= N) word(A64_NOP);
    }
};

constexpr void a64LoadAddress(auto& out, unsigned reg, uint64_t address) {
    out.word(a64MoveWide(false, reg, static_cast<uint16_t>(address), 0));
    out.word(a64MoveWide(true, reg, static_cast<uint16_t>(address >> 16), 16));
    out.word(a64MoveWide(true, reg, static_cast<uint16_t>(address >> 32), 32));
    out.word(a64MoveWide(true, reg, static_cast<uint16_t>(address >> 48), 48));
}

constexpr std::array<uint8_t, 24> a64WordTickLoad(uint64_t patchAt,
                                                  uint64_t cell,
                                                  uint64_t resumeAt) {
    Emitter<24> out;
    a64LoadAddress(out, A64_SCRATCH, cell);
    out.word(a64LoadWord(0, A64_SCRATCH, 0));
    out.word(a64Branch(patchAt + out.m_size, resumeAt));
    return out.m_bytes;
}

constexpr std::array<uint8_t, 40> a64SingleTickLoad(uint64_t cell) {
    Emitter<40> out;
    a64LoadAddress(out, A64_SCRATCH, cell);
    out.word(a64LoadSingle(0, A64_SCRATCH, 0));
    out.padA64();
    return out.m_bytes;
}

constexpr std::array<uint8_t, 40> a64SingleTickLoadRelative(uint64_t patchAt,
                                                            uint64_t cell) {
    Emitter<40> out;
    out.word(a64PageAddress(A64_SCRATCH, patchAt, cell));
    out.word(a64LoadSingle(0, A64_SCRATCH, static_cast<unsigned>(cell & 0xFFFu)));
    out.padA64();
    return out.m_bytes;
}

constexpr std::array<uint8_t, 12> t32TickLoad(uint32_t cell) {
    Emitter<12> out;
    for (uint16_t part : t32MoveHalf(false, 1, static_cast<uint16_t>(cell)))
        out.half(part);
    for (uint16_t part : t32MoveHalf(true, 1, static_cast<uint16_t>(cell >> 16)))
        out.half(part);
    out.half(T16_LDR_R0_R1);
    out.half(T16_NOP);
    return out.m_bytes;
}

inline constexpr int16_t ANY = -1;

inline std::optional<std::size_t> findMasked(std::span<const uint8_t> haystack,
                                             std::span<const int16_t> pattern) {
    if (pattern.empty() || haystack.size() < pattern.size()) return std::nullopt;
    for (std::size_t at = 0; at + pattern.size() <= haystack.size(); ++at) {
        bool match = true;
        for (std::size_t i = 0; i < pattern.size() && match; ++i)
            match = pattern[i] == ANY || haystack[at + i] == pattern[i];
        if (match) return at;
    }
    return std::nullopt;
}

}
