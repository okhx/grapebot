#ifndef GRAPE_VINE_ZSTD_HPP
#define GRAPE_VINE_ZSTD_HPP

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace grape::vine::zstd {

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

namespace detail {

inline unsigned highBit(uint32_t value) { return 31u - static_cast<unsigned>(std::countl_zero(value)); }

class ForwardBits {
    std::span<const uint8_t> m_bytes;
    uint64_t m_bit = 0;

   public:
    explicit ForwardBits(std::span<const uint8_t> bytes) : m_bytes(bytes) {}

    uint32_t peek(unsigned count) const {
        uint32_t value = 0;
        for (unsigned i = 0; i < count; ++i) {
            const uint64_t at = m_bit + i;
            const size_t byte = static_cast<size_t>(at >> 3);
            if (byte >= m_bytes.size()) continue;
            value |= static_cast<uint32_t>((m_bytes[byte] >> (at & 7)) & 1u) << i;
        }
        return value;
    }
    void skip(unsigned count) { m_bit += count; }
    uint32_t read(unsigned count) {
        const uint32_t value = peek(count);
        skip(count);
        return value;
    }
    size_t bytesUsed() const { return static_cast<size_t>((m_bit + 7) >> 3); }
};

class BackwardBits {
    std::span<const uint8_t> m_bytes;
    int64_t m_pos = 0;

   public:
    explicit BackwardBits(std::span<const uint8_t> bytes) : m_bytes(bytes) {
        if (bytes.empty() || bytes.back() == 0) throw Failure("Bad bitstream padding");
        m_pos = static_cast<int64_t>(8 * (bytes.size() - 1) + highBit(bytes.back()));
    }

    uint64_t read(unsigned count) {
        if (count == 0) return 0;
        m_pos -= count;
        uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i) {
            const int64_t at = m_pos + i;
            if (at < 0) continue;
            value |= static_cast<uint64_t>((m_bytes[static_cast<size_t>(at >> 3)] >> (at & 7)) & 1u) << i;
        }
        return value;
    }
    uint64_t peek(unsigned count) {
        const int64_t saved = m_pos;
        const uint64_t value = read(count);
        m_pos = saved;
        return value;
    }
    void skip(unsigned count) { m_pos -= count; }
    bool overflowed() const { return m_pos < 0; }
    bool finished() const { return m_pos == 0; }
};

struct FseEntry {
    uint16_t m_symbol = 0;
    uint8_t m_bits = 0;
    uint32_t m_base = 0;
};

struct FseTable {
    unsigned m_log = 0;
    std::vector<FseEntry> m_entries;

    static FseTable rle(uint16_t symbol) {
        FseTable table;
        table.m_entries = {{symbol, 0, 0}};
        return table;
    }

    static FseTable build(const std::vector<int16_t>& counts, unsigned log) {
        FseTable table;
        table.m_log = log;
        const uint32_t size = 1u << log;
        table.m_entries.assign(size, {});
        std::vector<uint32_t> next(counts.size(), 0);
        uint32_t high = size - 1;
        for (size_t symbol = 0; symbol < counts.size(); ++symbol) {
            if (counts[symbol] == -1) {
                table.m_entries[high--].m_symbol = static_cast<uint16_t>(symbol);
                next[symbol] = 1;
            } else {
                next[symbol] = static_cast<uint32_t>(std::max<int16_t>(counts[symbol], 0));
            }
        }
        const uint32_t step = (size >> 1) + (size >> 3) + 3;
        uint32_t pos = 0;
        for (size_t symbol = 0; symbol < counts.size(); ++symbol) {
            for (int16_t i = 0; i < counts[symbol]; ++i) {
                table.m_entries[pos].m_symbol = static_cast<uint16_t>(symbol);
                do pos = (pos + step) & (size - 1);
                while (pos > high);
            }
        }
        if (pos != 0) throw Failure("Bad FSE distribution");
        for (auto& entry : table.m_entries) {
            const uint32_t state = next[entry.m_symbol]++;
            entry.m_bits = static_cast<uint8_t>(log - highBit(state));
            entry.m_base = (state << entry.m_bits) - size;
        }
        return table;
    }
};

inline FseTable readFse(std::span<const uint8_t> bytes, size_t& used, unsigned maxLog,
                        unsigned maxSymbol) {
    ForwardBits bits(bytes);
    const unsigned log = bits.read(4) + 5;
    if (log > maxLog) throw Failure("FSE accuracy too large");
    std::vector<int16_t> counts;
    int32_t remaining = (1 << log) + 1;
    int32_t threshold = 1 << log;
    unsigned width = log + 1;
    bool previousZero = false;
    while (remaining > 1 && counts.size() <= maxSymbol) {
        if (previousZero) {
            unsigned zeros = 0;
            for (;;) {
                const unsigned repeat = bits.read(2);
                zeros += repeat;
                if (repeat != 3) break;
            }
            counts.insert(counts.end(), zeros, 0);
            if (counts.size() > maxSymbol) break;
        }
        const int32_t max = (2 * threshold - 1) - remaining;
        int32_t count = 0;
        const uint32_t low = bits.peek(width - 1);
        if (static_cast<int32_t>(low & (threshold - 1)) < max) {
            count = static_cast<int32_t>(low & (threshold - 1));
            bits.skip(width - 1);
        } else {
            count = static_cast<int32_t>(bits.peek(width) & (2 * threshold - 1));
            if (count >= threshold) count -= max;
            bits.skip(width);
        }
        --count;
        remaining -= count < 0 ? -count : count;
        counts.push_back(static_cast<int16_t>(count));
        previousZero = count == 0;
        while (remaining < threshold) {
            --width;
            threshold >>= 1;
        }
    }
    if (remaining != 1 || counts.size() > maxSymbol + 1) throw Failure("Bad FSE table description");
    used = bits.bytesUsed();
    if (used > bytes.size()) throw Failure("Truncated table description");
    return FseTable::build(counts, log);
}

struct FseState {
    const FseTable* m_table = nullptr;
    uint32_t m_state = 0;

    void init(const FseTable& table, BackwardBits& bits) {
        m_table = &table;
        m_state = static_cast<uint32_t>(bits.read(table.m_log));
    }
    uint16_t symbol() const { return m_table->m_entries[m_state].m_symbol; }
    void update(BackwardBits& bits) {
        const auto& entry = m_table->m_entries[m_state];
        m_state = entry.m_base + static_cast<uint32_t>(bits.read(entry.m_bits));
    }
};

struct Huffman {
    unsigned m_maxBits = 0;
    std::vector<uint8_t> m_symbols;
    std::vector<uint8_t> m_lengths;

    bool empty() const { return m_symbols.empty(); }

    static Huffman fromWeights(std::vector<uint8_t> weights) {
        uint32_t total = 0;
        for (const uint8_t weight : weights) {
            if (weight > 11) throw Failure("Bad Huffman weight");
            if (weight) total += 1u << (weight - 1);
        }
        if (total == 0) throw Failure("Empty Huffman tree");
        const unsigned maxBits = highBit(total) + 1;
        const uint32_t left = (1u << maxBits) - total;
        if (!std::has_single_bit(left)) throw Failure("Bad Huffman tree");
        weights.push_back(static_cast<uint8_t>(highBit(left) + 1));
        if (maxBits > 11) throw Failure("Huffman tree too deep");

        Huffman tree;
        tree.m_maxBits = maxBits;
        tree.m_symbols.assign(size_t{1} << maxBits, 0);
        tree.m_lengths.assign(size_t{1} << maxBits, 0);
        size_t pos = 0;
        for (unsigned weight = 1; weight <= maxBits; ++weight) {
            for (size_t symbol = 0; symbol < weights.size(); ++symbol) {
                if (weights[symbol] != weight) continue;
                const size_t span = size_t{1} << (weight - 1);
                for (size_t i = 0; i < span; ++i) {
                    tree.m_symbols[pos + i] = static_cast<uint8_t>(symbol);
                    tree.m_lengths[pos + i] = static_cast<uint8_t>(maxBits + 1 - weight);
                }
                pos += span;
            }
        }
        if (pos != tree.m_symbols.size()) throw Failure("Bad Huffman tree");
        return tree;
    }

    uint8_t decode(BackwardBits& bits) const {
        const size_t index = static_cast<size_t>(bits.peek(m_maxBits));
        bits.skip(m_lengths[index]);
        return m_symbols[index];
    }
};

inline Huffman readHuffman(std::span<const uint8_t> bytes, size_t& used) {
    if (bytes.empty()) throw Failure("Missing Huffman tree");
    const uint8_t header = bytes[0];
    std::vector<uint8_t> weights;
    if (header >= 128) {
        const size_t count = header - 127;
        const size_t length = (count + 1) / 2;
        if (bytes.size() < 1 + length) throw Failure("Truncated Huffman weights");
        for (size_t i = 0; i < count; ++i) {
            const uint8_t byte = bytes[1 + i / 2];
            weights.push_back(i % 2 == 0 ? byte >> 4 : byte & 15);
        }
        used = 1 + length;
        return Huffman::fromWeights(std::move(weights));
    }
    if (bytes.size() < 1u + header) throw Failure("Truncated Huffman weights");
    const auto body = bytes.subspan(1, header);
    size_t tableBytes = 0;
    const FseTable table = readFse(body, tableBytes, 6, 255);
    if (tableBytes > body.size()) throw Failure("Bad Huffman weights");
    BackwardBits bits(body.subspan(tableBytes));
    FseState first;
    FseState second;
    first.init(table, bits);
    second.init(table, bits);
    for (;;) {
        if (weights.size() > 254) throw Failure("Too many Huffman weights");
        weights.push_back(static_cast<uint8_t>(first.symbol()));
        first.update(bits);
        if (bits.overflowed()) {
            weights.push_back(static_cast<uint8_t>(second.symbol()));
            break;
        }
        weights.push_back(static_cast<uint8_t>(second.symbol()));
        second.update(bits);
        if (bits.overflowed()) {
            weights.push_back(static_cast<uint8_t>(first.symbol()));
            break;
        }
    }
    used = 1 + header;
    return Huffman::fromWeights(std::move(weights));
}

inline constexpr std::array<uint32_t, 36> LL_BASE = {
    0,  1,  2,   3,   4,   5,    6,    7,    8,    9,     10,    11,
    12, 13, 14,  15,  16,  18,   20,   22,   24,   28,    32,    40,
    48, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
inline constexpr std::array<uint8_t, 36> LL_BITS = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
inline constexpr std::array<uint32_t, 53> ML_BASE = {
    3,  4,  5,  6,  7,  8,  9,  10,  11,  12,   13,   14,   15,   16,    17,    18,    19,   20,
    21, 22, 23, 24, 25, 26, 27, 28,  29,  30,   31,   32,   33,   34,    35,    37,    39,   41,
    43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};
inline constexpr std::array<uint8_t, 53> ML_BITS = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

inline const FseTable& defaultLiteralLengths() {
    static const FseTable table = FseTable::build(
        {4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2,
         2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1},
        6);
    return table;
}

inline const FseTable& defaultMatchLengths() {
    static const FseTable table = FseTable::build(
        {1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1,
         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
         1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1},
        6);
    return table;
}

inline const FseTable& defaultOffsets() {
    static const FseTable table = FseTable::build(
        {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1,
         1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1},
        5);
    return table;
}

struct FrameState {
    Huffman m_huffman;
    FseTable m_literalLengths;
    FseTable m_offsets;
    FseTable m_matchLengths;
    bool m_haveLiteralLengths = false;
    bool m_haveOffsets = false;
    bool m_haveMatchLengths = false;
    std::array<uint64_t, 3> m_repeat = {1, 4, 8};
};

inline std::vector<uint8_t> readLiterals(std::span<const uint8_t> block, size_t& used,
                                         FrameState& state) {
    if (block.empty()) throw Failure("Empty block");
    const uint8_t first = block[0];
    const unsigned type = first & 3;
    const unsigned format = (first >> 2) & 3;
    const auto byte = [&](size_t index) -> uint32_t {
        if (index >= block.size()) throw Failure("Truncated literals header");
        return block[index];
    };

    if (type == 0 || type == 1) {
        size_t size = 0;
        size_t header = 1;
        if (format == 0 || format == 2) {
            size = first >> 3;
        } else if (format == 1) {
            size = (first >> 4) | (byte(1) << 4);
            header = 2;
        } else {
            size = (first >> 4) | (byte(1) << 4) | (byte(2) << 12);
            header = 3;
        }
        if (type == 0) {
            if (block.size() < header + size) throw Failure("Truncated raw literals");
            used = header + size;
            return {block.begin() + static_cast<std::ptrdiff_t>(header),
                    block.begin() + static_cast<std::ptrdiff_t>(header + size)};
        }
        used = header + 1;
        return std::vector<uint8_t>(size, static_cast<uint8_t>(byte(header)));
    }

    size_t regenerated = 0;
    size_t compressed = 0;
    size_t header = 0;
    bool single = false;
    if (format <= 1) {
        const uint32_t bits = first | (byte(1) << 8) | (byte(2) << 16);
        regenerated = (bits >> 4) & 0x3ff;
        compressed = (bits >> 14) & 0x3ff;
        header = 3;
        single = format == 0;
    } else if (format == 2) {
        const uint32_t bits = first | (byte(1) << 8) | (byte(2) << 16) | (byte(3) << 24);
        regenerated = (bits >> 4) & 0x3fff;
        compressed = bits >> 18;
        header = 4;
    } else {
        const uint64_t bits = first | (byte(1) << 8) | (byte(2) << 16) |
                              (static_cast<uint64_t>(byte(3)) << 24) |
                              (static_cast<uint64_t>(byte(4)) << 32);
        regenerated = static_cast<size_t>((bits >> 4) & 0x3ffff);
        compressed = static_cast<size_t>((bits >> 22) & 0x3ffff);
        header = 5;
    }
    if (block.size() < header + compressed) throw Failure("Truncated compressed literals");
    auto body = block.subspan(header, compressed);
    used = header + compressed;

    if (type == 2) {
        size_t treeBytes = 0;
        state.m_huffman = readHuffman(body, treeBytes);
        body = body.subspan(treeBytes);
    } else if (state.m_huffman.empty()) {
        throw Failure("Treeless literals without a previous tree");
    }

    std::vector<uint8_t> out;
    out.reserve(regenerated);
    const auto decodeStream = [&](std::span<const uint8_t> stream, size_t count) {
        BackwardBits bits(stream);
        for (size_t i = 0; i < count; ++i) out.push_back(state.m_huffman.decode(bits));
        if (!bits.finished()) throw Failure("Bad Huffman stream length");
    };
    if (single) {
        decodeStream(body, regenerated);
        return out;
    }
    if (body.size() < 6) throw Failure("Truncated jump table");
    const size_t sizes[3] = {
        static_cast<size_t>(body[0] | (body[1] << 8)),
        static_cast<size_t>(body[2] | (body[3] << 8)),
        static_cast<size_t>(body[4] | (body[5] << 8)),
    };
    const size_t total = sizes[0] + sizes[1] + sizes[2];
    if (6 + total > body.size()) throw Failure("Bad jump table");
    const size_t quarter = (regenerated + 3) / 4;
    if (quarter * 3 > regenerated) throw Failure("Bad literal count");
    size_t offset = 6;
    for (int i = 0; i < 3; ++i) {
        decodeStream(body.subspan(offset, sizes[i]), quarter);
        offset += sizes[i];
    }
    decodeStream(body.subspan(offset), regenerated - quarter * 3);
    return out;
}

inline const FseTable& pickTable(unsigned mode, std::span<const uint8_t> bytes, size_t& offset,
                                 FseTable& slot, bool& have, const FseTable& fallback,
                                 unsigned maxLog, unsigned maxSymbol) {
    switch (mode) {
        case 0:
            slot = fallback;
            break;
        case 1:
            if (offset >= bytes.size()) throw Failure("Truncated RLE table");
            if (bytes[offset] > maxSymbol) throw Failure("Bad RLE symbol");
            slot = FseTable::rle(bytes[offset]);
            ++offset;
            break;
        case 2: {
            size_t used = 0;
            slot = readFse(bytes.subspan(offset), used, maxLog, maxSymbol);
            offset += used;
            break;
        }
        default:
            if (!have) throw Failure("Repeated table without a previous one");
            break;
    }
    have = true;
    return slot;
}

inline void decodeBlock(std::span<const uint8_t> block, FrameState& state,
                        std::vector<uint8_t>& out, size_t limit) {
    size_t used = 0;
    const std::vector<uint8_t> literals = readLiterals(block, used, state);
    auto rest = block.subspan(used);
    size_t literalAt = 0;

    if (rest.empty()) throw Failure("Missing sequences header");
    size_t sequences = rest[0];
    size_t offset = 1;
    if (sequences >= 128) {
        if (rest.size() < 2) throw Failure("Truncated sequences header");
        if (sequences < 255) {
            sequences = ((sequences - 128) << 8) + rest[1];
            offset = 2;
        } else {
            if (rest.size() < 3) throw Failure("Truncated sequences header");
            sequences = rest[1] + (static_cast<size_t>(rest[2]) << 8) + 0x7f00;
            offset = 3;
        }
    }

    if (sequences > 0) {
        if (offset >= rest.size()) throw Failure("Truncated sequence modes");
        const uint8_t modes = rest[offset++];
        if (modes & 3) throw Failure("Reserved sequence mode bits");
        const FseTable& literalTable =
            pickTable(modes >> 6, rest, offset, state.m_literalLengths, state.m_haveLiteralLengths,
                      defaultLiteralLengths(), 9, 35);
        const FseTable& offsetTable =
            pickTable((modes >> 4) & 3, rest, offset, state.m_offsets, state.m_haveOffsets,
                      defaultOffsets(), 8, 31);
        const FseTable& matchTable =
            pickTable((modes >> 2) & 3, rest, offset, state.m_matchLengths, state.m_haveMatchLengths,
                      defaultMatchLengths(), 9, 52);
        if (offset > rest.size()) throw Failure("Truncated sequence tables");

        BackwardBits bits(rest.subspan(offset));
        FseState literalState;
        FseState offsetState;
        FseState matchState;
        literalState.init(literalTable, bits);
        offsetState.init(offsetTable, bits);
        matchState.init(matchTable, bits);

        for (size_t n = 0; n < sequences; ++n) {
            const unsigned offsetCode = offsetState.symbol();
            const unsigned matchCode = matchState.symbol();
            const unsigned literalCode = literalState.symbol();
            if (offsetCode > 31 || matchCode > 52 || literalCode > 35) throw Failure("Bad sequence code");

            const uint64_t offsetValue = (uint64_t{1} << offsetCode) + bits.read(offsetCode);
            const uint64_t matchLength = ML_BASE[matchCode] + bits.read(ML_BITS[matchCode]);
            const uint64_t literalLength = LL_BASE[literalCode] + bits.read(LL_BITS[literalCode]);

            uint64_t distance = 0;
            auto& repeat = state.m_repeat;
            if (offsetValue > 3) {
                distance = offsetValue - 3;
                repeat = {distance, repeat[0], repeat[1]};
            } else {
                const unsigned index = static_cast<unsigned>(offsetValue) + (literalLength == 0 ? 1 : 0);
                if (index == 1) {
                    distance = repeat[0];
                } else {
                    distance = index == 4 ? repeat[0] - 1 : repeat[index - 1];
                    if (index == 2) repeat = {distance, repeat[0], repeat[2]};
                    else repeat = {distance, repeat[0], repeat[1]};
                }
            }

            if (literalAt + literalLength > literals.size()) throw Failure("Literal length overruns");
            if (out.size() + literalLength + matchLength > limit) throw Failure("Output too large");
            out.insert(out.end(), literals.begin() + static_cast<std::ptrdiff_t>(literalAt),
                       literals.begin() + static_cast<std::ptrdiff_t>(literalAt + literalLength));
            literalAt += static_cast<size_t>(literalLength);
            if (distance == 0 || distance > out.size()) throw Failure("Bad match offset");
            const size_t from = out.size() - static_cast<size_t>(distance);
            for (uint64_t i = 0; i < matchLength; ++i) out.push_back(out[from + i]);

            if (n + 1 < sequences) {
                literalState.update(bits);
                matchState.update(bits);
                offsetState.update(bits);
            }
        }
        if (!bits.finished()) throw Failure("Bad sequence bitstream length");
    }

    if (out.size() + (literals.size() - literalAt) > limit) throw Failure("Output too large");
    out.insert(out.end(), literals.begin() + static_cast<std::ptrdiff_t>(literalAt), literals.end());
}

inline uint32_t le32(std::span<const uint8_t> bytes, size_t at) {
    if (at + 4 > bytes.size()) throw Failure("Truncated frame");
    return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16) |
           (static_cast<uint32_t>(bytes[at + 3]) << 24);
}

inline size_t decodeFrame(std::span<const uint8_t> in, std::vector<uint8_t>& out, size_t limit) {
    const uint32_t magic = le32(in, 0);
    if ((magic & 0xfffffff0u) == 0x184d2a50u) return 8 + le32(in, 4);
    if (magic != 0xfd2fb528u) throw Failure("Not a zstd frame");
    if (in.size() < 5) throw Failure("Truncated frame header");
    const uint8_t descriptor = in[4];
    if (descriptor & 0x08) throw Failure("Reserved frame bit set");
    const unsigned dictionaryBytes = (descriptor & 3) == 3 ? 4 : (descriptor & 3);
    const bool singleSegment = (descriptor & 0x20) != 0;
    const unsigned sizeFlag = descriptor >> 6;
    const unsigned sizeBytes = sizeFlag == 0 ? (singleSegment ? 1 : 0) : (1u << sizeFlag);
    size_t at = 5 + (singleSegment ? 0 : 1);
    for (unsigned i = 0; i < dictionaryBytes; ++i)
        if (at + i < in.size() && in[at + i] != 0) throw Failure("Dictionaries are not supported");
    at += dictionaryBytes + sizeBytes;
    if (at > in.size()) throw Failure("Truncated frame header");

    FrameState state;
    for (;;) {
        if (at + 3 > in.size()) throw Failure("Truncated block header");
        const uint32_t header = in[at] | (in[at + 1] << 8) | (in[at + 2] << 16);
        at += 3;
        const bool last = header & 1;
        const unsigned type = (header >> 1) & 3;
        const size_t size = header >> 3;
        if (type == 0) {
            if (at + size > in.size()) throw Failure("Truncated raw block");
            if (out.size() + size > limit) throw Failure("Output too large");
            out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(at),
                       in.begin() + static_cast<std::ptrdiff_t>(at + size));
            at += size;
        } else if (type == 1) {
            if (at >= in.size()) throw Failure("Truncated RLE block");
            if (out.size() + size > limit) throw Failure("Output too large");
            out.insert(out.end(), size, in[at]);
            at += 1;
        } else if (type == 2) {
            if (at + size > in.size()) throw Failure("Truncated compressed block");
            decodeBlock(in.subspan(at, size), state, out, limit);
            at += size;
        } else {
            throw Failure("Reserved block type");
        }
        if (last) break;
    }
    if (descriptor & 0x04) at += 4;
    if (at > in.size()) throw Failure("Truncated checksum");
    return at;
}

}

inline std::expected<std::vector<uint8_t>, std::string> decompress(std::span<const uint8_t> in,
                                                                   size_t limit) {
    std::vector<uint8_t> out;
    try {
        size_t at = 0;
        while (at < in.size()) at += detail::decodeFrame(in.subspan(at), out, limit);
    } catch (const std::exception& error) {
        return std::unexpected(std::string(error.what()));
    }
    return out;
}

}

#endif
