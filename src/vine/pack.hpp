#ifndef GRAPE_VINE_PACK_HPP
#define GRAPE_VINE_PACK_HPP

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace grape::vine {

inline constexpr uint64_t maskOf(unsigned width) {
    return width >= 64 ? ~0ull : ((1ull << width) - 1);
}

class BitWriter {
    std::vector<uint8_t> m_bytes;
    uint64_t m_bits = 0;
    unsigned m_count = 0;

    void putChunk(uint64_t value, unsigned width) {
        m_bits |= (value & maskOf(width)) << m_count;
        m_count += width;
        while (m_count >= 8) {
            m_bytes.push_back(static_cast<uint8_t>(m_bits & 0xFF));
            m_bits >>= 8;
            m_count -= 8;
        }
    }

   public:
    void reserve(size_t bytes) { m_bytes.reserve(bytes); }

    void put(uint64_t value, unsigned width) {
        while (width > 32) {
            putChunk(value & 0xFFFFFFFFull, 32);
            value >>= 32;
            width -= 32;
        }
        putChunk(value, width);
    }

    void putVarint(uint64_t value) {
        do {
            const uint64_t chunk = value & 0x7F;
            value >>= 7;
            put(chunk | (value ? 0x80 : 0), 8);
        } while (value != 0);
    }

    std::vector<uint8_t> finish() {
        if (m_count > 0) {
            m_bytes.push_back(static_cast<uint8_t>(m_bits & 0xFF));
            m_bits = 0;
            m_count = 0;
        }
        return std::move(m_bytes);
    }
};

class BitReader {
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    size_t m_pos = 0;
    uint64_t m_bits = 0;
    unsigned m_count = 0;
    bool m_overrun = false;

    uint64_t getChunk(unsigned width) {
        while (m_count < width) {
            if (m_pos >= m_size) {
                m_overrun = true;
                return 0;
            }
            m_bits |= static_cast<uint64_t>(m_data[m_pos++]) << m_count;
            m_count += 8;
        }
        const uint64_t value = m_bits & maskOf(width);
        m_bits >>= width;
        m_count -= width;
        return value;
    }

   public:
    BitReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    bool overrun() const { return m_overrun; }

    size_t bitsLeft() const { return (m_size - m_pos) * 8 + m_count; }

    uint64_t get(unsigned width) {
        uint64_t value = 0;
        unsigned shift = 0;
        while (width > 32) {
            value |= getChunk(32) << shift;
            shift += 32;
            width -= 32;
        }
        value |= getChunk(width) << shift;
        return value;
    }

    uint64_t getVarint() {
        uint64_t value = 0;
        unsigned shift = 0;
        for (;;) {
            const uint64_t byte = get(8);
            if (m_overrun) return 0;
            if (shift > 63) {
                m_overrun = true;
                return 0;
            }
            value |= (byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) return value;
            shift += 7;
        }
    }
};

inline constexpr unsigned PACK_TAG_BITS = 6;
inline constexpr unsigned PACK_DELTA_BITS = 6;
inline constexpr uint64_t PACK_DELTA_ESCAPE = (1ull << PACK_DELTA_BITS) - 1;
inline constexpr uint8_t PACK_HOLDING = 0x08;
inline constexpr unsigned PACK_PREDICT_SLOTS = 16;

}

#endif
