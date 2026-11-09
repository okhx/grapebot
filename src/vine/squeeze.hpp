#ifndef GRAPE_VINE_SQUEEZE_HPP
#define GRAPE_VINE_SQUEEZE_HPP

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace grape::vine::squeeze {

inline constexpr unsigned PROB_BITS = 12;
inline constexpr uint16_t PROB_ONE = 1u << PROB_BITS;
inline constexpr uint16_t PROB_HALF = PROB_ONE / 2;
inline constexpr unsigned ADAPT_SHIFT = 4;
inline constexpr uint32_t RANGE_TOP = 1u << 24;
inline constexpr size_t OVERRUN_SLACK = 8;

class Encoder {
    std::vector<uint8_t> m_out;
    uint64_t m_low = 0;
    uint32_t m_range = 0xFFFFFFFFu;
    uint8_t m_cache = 0;
    uint64_t m_cacheSize = 1;

    void shiftLow() {
        if (static_cast<uint32_t>(m_low) < 0xFF000000u || (m_low >> 32) != 0) {
            const uint8_t carry = static_cast<uint8_t>(m_low >> 32);
            uint8_t pending = m_cache;
            do {
                m_out.push_back(static_cast<uint8_t>(pending + carry));
                pending = 0xFF;
            } while (--m_cacheSize != 0);
            m_cache = static_cast<uint8_t>(m_low >> 24);
        }
        ++m_cacheSize;
        m_low = (m_low & 0x00FFFFFFu) << 8;
    }

    void normalize() {
        while (m_range < RANGE_TOP) {
            m_range <<= 8;
            shiftLow();
        }
    }

   public:
    void reserve(size_t bytes) { m_out.reserve(bytes); }

    void bit(uint16_t& prob, unsigned value) {
        const uint32_t bound = (m_range >> PROB_BITS) * prob;
        if (value == 0) {
            m_range = bound;
            prob = static_cast<uint16_t>(prob + ((PROB_ONE - prob) >> ADAPT_SHIFT));
        } else {
            m_low += bound;
            m_range -= bound;
            prob = static_cast<uint16_t>(prob - (prob >> ADAPT_SHIFT));
        }
        normalize();
    }

    void direct(uint64_t value, unsigned width) {
        while (width-- > 0) {
            m_range >>= 1;
            if ((value >> width) & 1) m_low += m_range;
            normalize();
        }
    }

    std::vector<uint8_t> finish() {
        for (int i = 0; i < 5; ++i) shiftLow();
        return std::move(m_out);
    }
};

class Decoder {
    std::span<const uint8_t> m_in;
    size_t m_pos = 0;
    uint32_t m_range = 0xFFFFFFFFu;
    uint32_t m_code = 0;

    uint8_t next() {
        return m_pos < m_in.size() ? m_in[m_pos++] : (++m_pos, uint8_t{0});
    }

    void normalize() {
        while (m_range < RANGE_TOP) {
            m_range <<= 8;
            m_code = (m_code << 8) | next();
        }
    }

   public:
    explicit Decoder(std::span<const uint8_t> in) : m_in(in) {
        for (int i = 0; i < 5; ++i) m_code = (m_code << 8) | next();
    }

    bool overrun() const { return m_pos > m_in.size() + OVERRUN_SLACK; }
    bool finished() const { return m_pos == m_in.size(); }

    unsigned bit(uint16_t& prob) {
        const uint32_t bound = (m_range >> PROB_BITS) * prob;
        unsigned value = 0;
        if (m_code < bound) {
            m_range = bound;
            prob = static_cast<uint16_t>(prob + ((PROB_ONE - prob) >> ADAPT_SHIFT));
        } else {
            m_code -= bound;
            m_range -= bound;
            prob = static_cast<uint16_t>(prob - (prob >> ADAPT_SHIFT));
            value = 1;
        }
        normalize();
        return value;
    }

    uint64_t direct(unsigned width) {
        uint64_t value = 0;
        while (width-- > 0) {
            m_range >>= 1;
            unsigned bit = 0;
            if (m_code >= m_range) {
                m_code -= m_range;
                bit = 1;
            }
            value = (value << 1) | bit;
            normalize();
        }
        return value;
    }
};

inline constexpr unsigned TAG_BITS = 6;
inline constexpr unsigned TAG_VALUES = 1u << TAG_BITS;
inline constexpr unsigned WIDTH_BITS = 7;
inline constexpr unsigned WIDTH_NODES = 1u << WIDTH_BITS;
inline constexpr unsigned WIDTH_HINTS = 16;
inline constexpr unsigned MAX_WIDTH = 64;
inline constexpr unsigned HIGH_BITS = 3;
inline constexpr unsigned REP_HISTORY = 4;

struct Model {
    std::vector<uint16_t> m_tag =
        std::vector<uint16_t>(TAG_VALUES * TAG_VALUES * TAG_VALUES, PROB_HALF);
    std::vector<uint16_t> m_isRep0 =
        std::vector<uint16_t>(TAG_VALUES * REP_HISTORY, PROB_HALF);
    std::vector<uint16_t> m_isRep1 =
        std::vector<uint16_t>(TAG_VALUES * REP_HISTORY, PROB_HALF);
    std::vector<uint16_t> m_width =
        std::vector<uint16_t>(TAG_VALUES * WIDTH_HINTS * WIDTH_NODES, PROB_HALF);
    std::vector<uint16_t> m_high =
        std::vector<uint16_t>((MAX_WIDTH + 1) * (1u << HIGH_BITS), PROB_HALF);
    std::vector<uint16_t> m_low =
        std::vector<uint16_t>((MAX_WIDTH + 1) * MAX_WIDTH, PROB_HALF);
    std::vector<uint16_t> m_seedWidth = std::vector<uint16_t>(WIDTH_NODES, PROB_HALF);
    uint16_t m_sameTps = PROB_HALF;

    uint64_t m_rep[TAG_VALUES][2] = {};
    uint8_t m_repHistory[TAG_VALUES] = {};
    unsigned m_prev = 0;
    unsigned m_prev2 = 0;
    uint64_t m_lastTps = std::bit_cast<uint64_t>(240.0);

    uint16_t* tagTree() {
        return &m_tag[((m_prev2 << TAG_BITS) | m_prev) * TAG_VALUES];
    }

    uint16_t* widthTree(unsigned tag) {
        const unsigned hint = std::min<unsigned>(
            static_cast<unsigned>(std::bit_width(m_rep[tag][0])), WIDTH_HINTS - 1);
        return &m_width[(tag * WIDTH_HINTS + hint) * WIDTH_NODES];
    }

    void pushTag(unsigned tag) {
        m_prev2 = m_prev;
        m_prev = tag;
    }

    void pushDelta(unsigned tag, uint64_t delta, unsigned outcome) {
        if (delta != m_rep[tag][0]) {
            m_rep[tag][1] = m_rep[tag][0];
            m_rep[tag][0] = delta;
        }
        m_repHistory[tag] =
            static_cast<uint8_t>(((m_repHistory[tag] << 1) | outcome) & (REP_HISTORY - 1));
    }
};

template <class Coder>
void tree(Coder& coder, uint16_t* probs, unsigned& value, unsigned bits) {
    unsigned node = 1;
    for (unsigned i = bits; i-- > 0;) {
        unsigned bit = 0;
        if constexpr (std::is_same_v<Coder, Encoder>) {
            bit = (value >> i) & 1;
            coder.bit(probs[node], bit);
        } else {
            bit = coder.bit(probs[node]);
        }
        node = (node << 1) | bit;
    }
    if constexpr (!std::is_same_v<Coder, Encoder>) value = node - (1u << bits);
}

inline void putMantissa(Encoder& coder, Model& model, uint64_t value,
                        unsigned width) {
    if (width < 2) return;
    unsigned remaining = width - 1;
    const unsigned high = std::min(remaining, HIGH_BITS);
    unsigned top = static_cast<unsigned>((value >> (remaining - high)) &
                                         ((1u << high) - 1));
    tree(coder, &model.m_high[width * (1u << HIGH_BITS)], top, high);
    remaining -= high;
    for (unsigned i = remaining; i-- > 0;)
        coder.bit(model.m_low[width * MAX_WIDTH + i], (value >> i) & 1);
}

inline uint64_t getMantissa(Decoder& coder, Model& model, unsigned width) {
    if (width == 0) return 0;
    if (width == 1) return 1;
    unsigned remaining = width - 1;
    const unsigned high = std::min(remaining, HIGH_BITS);
    unsigned top = 0;
    tree(coder, &model.m_high[width * (1u << HIGH_BITS)], top, high);
    uint64_t value = (uint64_t{1} << high) | top;
    remaining -= high;
    for (unsigned i = remaining; i-- > 0;)
        value = (value << 1) | coder.bit(model.m_low[width * MAX_WIDTH + i]);
    return value;
}

struct Item {
    unsigned m_tag = 0;
    uint64_t m_delta = 0;
    bool m_seeded = false;
    uint64_t m_seed = 0;
    bool m_timed = false;
    uint64_t m_tpsBits = 0;
};

inline void put(Encoder& coder, Model& model, const Item& item) {
    unsigned tag = item.m_tag & (TAG_VALUES - 1);
    tree(coder, model.tagTree(), tag, TAG_BITS);

    const uint64_t delta = item.m_delta;
    const unsigned history = model.m_repHistory[tag];
    const bool rep0 = delta == model.m_rep[tag][0];
    coder.bit(model.m_isRep0[tag * REP_HISTORY + history], rep0 ? 0 : 1);
    if (!rep0) {
        const bool rep1 = delta == model.m_rep[tag][1];
        coder.bit(model.m_isRep1[tag * REP_HISTORY + history], rep1 ? 0 : 1);
        if (!rep1) {
            unsigned width = static_cast<unsigned>(std::bit_width(delta));
            tree(coder, model.widthTree(tag), width, WIDTH_BITS);
            putMantissa(coder, model, delta, width);
        }
    }
    model.pushDelta(tag, delta, rep0 ? 1 : 0);
    model.pushTag(tag);

    if (item.m_seeded) {
        unsigned width = static_cast<unsigned>(std::bit_width(item.m_seed));
        tree(coder, model.m_seedWidth.data(), width, WIDTH_BITS);
        if (width > 1) coder.direct(item.m_seed, width - 1);
    }
    if (item.m_timed) {
        const bool same = item.m_tpsBits == model.m_lastTps;
        coder.bit(model.m_sameTps, same ? 0 : 1);
        if (!same) coder.direct(item.m_tpsBits, 64);
        model.m_lastTps = item.m_tpsBits;
    }
}

template <class CarriesSeed, class CarriesTps>
bool get(Decoder& coder, Model& model, Item& item, CarriesSeed carriesSeed,
         CarriesTps carriesTps) {
    unsigned tag = 0;
    tree(coder, model.tagTree(), tag, TAG_BITS);
    item.m_tag = tag;

    const unsigned history = model.m_repHistory[tag];
    const bool rep0 = coder.bit(model.m_isRep0[tag * REP_HISTORY + history]) == 0;
    uint64_t delta = model.m_rep[tag][0];
    if (!rep0) {
        if (coder.bit(model.m_isRep1[tag * REP_HISTORY + history]) == 0) {
            delta = model.m_rep[tag][1];
        } else {
            unsigned width = 0;
            tree(coder, model.widthTree(tag), width, WIDTH_BITS);
            if (width > MAX_WIDTH) return false;
            delta = getMantissa(coder, model, width);
        }
    }
    item.m_delta = delta;
    model.pushDelta(tag, delta, rep0 ? 1 : 0);
    model.pushTag(tag);

    item.m_seeded = carriesSeed(tag);
    item.m_seed = 0;
    if (item.m_seeded) {
        unsigned width = 0;
        tree(coder, model.m_seedWidth.data(), width, WIDTH_BITS);
        if (width > MAX_WIDTH) return false;
        item.m_seed = width == 0 ? 0
                      : (uint64_t{1} << (width - 1)) |
                            (width > 1 ? coder.direct(width - 1) : 0);
    }
    item.m_timed = carriesTps(tag);
    if (item.m_timed) {
        if (coder.bit(model.m_sameTps) != 0) model.m_lastTps = coder.direct(64);
        item.m_tpsBits = model.m_lastTps;
    }
    return !coder.overrun();
}

}

#endif
