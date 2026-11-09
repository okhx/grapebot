#ifndef GRAPE_VINE_FORMAT_HPP
#define GRAPE_VINE_FORMAT_HPP

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "pack.hpp"
#include "squeeze.hpp"

namespace grape::vine {

enum class EventKind : uint8_t {
    Jump = 1,
    Left = 2,
    Right = 3,
    Restart = 4,
    RestartFull = 5,
    Death = 6,
    Tps = 7,
    Bugpoint = 8,
};

constexpr bool isPlayerKind(EventKind kind) {
    const auto value = static_cast<uint8_t>(kind);
    return value >= static_cast<uint8_t>(EventKind::Jump) &&
           value <= static_cast<uint8_t>(EventKind::Right);
}

struct Event {
    uint64_t m_frame = 0;
    EventKind m_type = EventKind::Jump;
    bool m_holding = false;
    bool m_player2 = false;
    bool m_swift = false;
    uint64_t m_seed = 0;
    double m_tps = 240.0;

    constexpr bool isPlayer() const { return isPlayerKind(m_type); }

    Event() = default;
    Event(uint64_t frame, EventKind type, bool holding, bool player2)
      : m_frame(frame), m_type(type), m_holding(holding), m_player2(player2) {}
    Event(uint64_t frame, EventKind type, uint64_t seed)
      : m_frame(frame), m_type(type), m_seed(seed) {}
    Event(uint64_t frame, double tps)
      : m_frame(frame), m_type(EventKind::Tps), m_tps(tps) {}
    explicit Event(uint64_t frame, EventKind type)
      : m_frame(frame), m_type(type) {}
};

struct Inputs {
    std::vector<Event> m_events;
    uint64_t m_seed = 0;
    double m_tps = 240.0;
    bool m_platformer = false;
    
    
    bool m_cbf = false;
    bool m_cbfOnSteps = false;
    bool m_clickBetweenSteps = false;
    bool m_clickOnSteps = false;

    size_t length() const { return m_events.size(); }

    void clipFrom(uint64_t frame) {
        std::erase_if(m_events,
                      [frame](const Event& event) {
                          return event.m_frame >= frame;
                      });
    }

    void addEvent(uint64_t frame, EventKind type, bool holding, bool player2) {
        m_events.emplace_back(frame, type, holding, player2);
    }
    void addEvent(uint64_t frame, EventKind type, uint64_t seed) {
        m_events.emplace_back(frame, type, seed);
    }
    void addEvent(uint64_t frame, double tps) {
        m_events.emplace_back(frame, tps);
    }

    void sort() {
        std::stable_sort(m_events.begin(), m_events.end(),
                         [](const Event& a, const Event& b) {
                             return a.m_frame < b.m_frame;
                         });
    }

    void setTps(uint64_t frame, double tps, bool coalesce) {
        if (coalesce && !m_events.empty() &&
            m_events.back().m_type == EventKind::Tps) {
            m_events.back().m_tps = tps;
            return;
        }
        const bool outOfOrder =
            !m_events.empty() && m_events.back().m_frame > frame;
        addEvent(frame, tps);
        if (outOfOrder) sort();
    }

    bool hasTpsEvents() const {
        for (const auto& event : m_events)
            if (event.m_type == EventKind::Tps) return true;
        return false;
    }

    double tpsAt(uint64_t frame) const {
        double tps = m_tps;
        for (const auto& event : m_events) {
            if (event.m_frame > frame) break;
            if (event.m_type == EventKind::Tps) tps = event.m_tps;
        }
        return tps;
    }

    uint64_t lastFrame() const {
        uint64_t last = 0;
        for (const auto& event : m_events) last = std::max(last, event.m_frame);
        return last;
    }
};

inline constexpr uint64_t MAX_EVENTS = 10'000'000;
inline constexpr std::array<uint8_t, 4> FILE_MAGIC = {'V', 'I', 'N', 'E'};
inline constexpr uint16_t FILE_VERSION = 4;
inline constexpr uint16_t FILE_VERSION_PACKED = 3;
inline constexpr uint16_t FILE_VERSION_PLAIN = 2;
inline constexpr uint16_t FILE_VERSION_LEGACY = 1;
inline constexpr uint8_t META_PLATFORMER = 0x01;
inline constexpr uint8_t META_CBF = 0x02;
inline constexpr uint8_t META_CBF_ON_STEPS = 0x04;
inline constexpr uint8_t META_CLICK_BETWEEN_STEPS = 0x08;
inline constexpr uint8_t META_CLICK_ON_STEPS = 0x10;
inline constexpr uint8_t META_FLAG_MASK =
    META_PLATFORMER | META_CBF | META_CBF_ON_STEPS |
    META_CLICK_BETWEEN_STEPS | META_CLICK_ON_STEPS;
inline constexpr uint16_t SECTION_TOTAL = 3;
inline constexpr uint8_t SECTION_META = 1;
inline constexpr uint8_t SECTION_FRAMES = 2;
inline constexpr uint8_t SECTION_RECORDS = 3;
inline constexpr uint8_t TAG_KIND_MASK = 0x07;
inline constexpr uint8_t TAG_HOLDING = 0x08;
inline constexpr uint8_t TAG_PLAYER2 = 0x10;
inline constexpr uint8_t TAG_SWIFT = 0x20;
inline constexpr uint8_t TAG_SEED = 0x40;
inline constexpr uint8_t TAG_TPS = 0x80;

consteval std::array<uint32_t, 256> buildCrcTable() {
    std::array<uint32_t, 256> table{};
    for (uint32_t index = 0; index < 256; ++index) {
        uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0x82F63B78u & (0u - (value & 1u)));
        table[index] = value;
    }
    return table;
}

inline constexpr auto CRC_TABLE = buildCrcTable();

inline uint32_t crc32c(std::span<const uint8_t> bytes, uint32_t seed = 0) {
    uint32_t crc = ~seed;
    for (const uint8_t byte : bytes)
        crc = CRC_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

inline void writeU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

inline void writeU32(std::vector<uint8_t>& out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

inline void writeU64(std::vector<uint8_t>& out, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

inline void writeVarint(std::vector<uint8_t>& out, uint64_t value) {
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7F);
        value >>= 7;
        if (value != 0) byte |= 0x80;
        out.push_back(byte);
    } while (value != 0);
}

inline uint32_t readU32(std::span<const uint8_t> bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
           static_cast<uint32_t>(bytes[offset + 1]) << 8 |
           static_cast<uint32_t>(bytes[offset + 2]) << 16 |
           static_cast<uint32_t>(bytes[offset + 3]) << 24;
}

inline uint64_t readU64(std::span<const uint8_t> bytes, size_t offset) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8);
    return value;
}

namespace detail {

struct ByteReader {
    std::span<const uint8_t> m_bytes;
    size_t m_pos = 0;

    bool done() const { return m_pos == m_bytes.size(); }
    size_t left() const { return m_bytes.size() - m_pos; }

    uint8_t take8() {
        if (m_pos >= m_bytes.size()) throw 1;
        return m_bytes[m_pos++];
    }

    uint64_t take64() {
        if (left() < 8) throw 1;
        const uint64_t value = readU64(m_bytes, m_pos);
        m_pos += 8;
        return value;
    }

    uint64_t takeVarint() {
        uint64_t value = 0;
        int shift = 0;
        for (;;) {
            const uint8_t byte = take8();
            if (shift == 63 && (byte & 0xFE) != 0) throw 2;
            if (shift > 63) throw 2;
            value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) return value;
            shift += 7;
        }
    }
};

}

inline uint8_t wireCode(EventKind kind) {
    switch (kind) {
        case EventKind::Jump: return 0;
        case EventKind::Left: return 1;
        case EventKind::Right: return 2;
        case EventKind::Restart: return 3;
        case EventKind::RestartFull: return 4;
        case EventKind::Death: return 5;
        case EventKind::Tps: return 6;
        case EventKind::Bugpoint: return 7;
    }
    return 0;
}

inline EventKind kindOfWire(uint8_t code) {
    switch (code) {
        case 0: return EventKind::Jump;
        case 1: return EventKind::Left;
        case 2: return EventKind::Right;
        case 3: return EventKind::Restart;
        case 4: return EventKind::RestartFull;
        case 5: return EventKind::Death;
        case 6: return EventKind::Tps;
    }
    return EventKind::Bugpoint;
}

inline bool wireCarriesSeed(uint8_t code) {
    return code == 3 || code == 4 || code == 5;
}

inline bool wireCarriesTps(uint8_t code) { return code == 6; }

inline void appendSection(std::vector<uint8_t>& out, uint8_t id,
                          const std::vector<uint8_t>& payload) {
    out.push_back(id);
    writeU32(out, static_cast<uint32_t>(payload.size()));
    const size_t start = out.size() - 5;
    out.insert(out.end(), payload.begin(), payload.end());
    writeU32(out, crc32c(std::span<const uint8_t>(out).subspan(
                      start, out.size() - start)));
}

inline uint8_t wireTag(const Event& event) {
    uint8_t tag = wireCode(event.m_type);
    if (event.m_holding) tag |= TAG_HOLDING;
    if (event.m_player2) tag |= TAG_PLAYER2;
    if (event.m_swift) tag |= TAG_SWIFT;
    return tag;
}

inline void applyWireTag(Event& event, uint8_t tag) {
    event.m_type = kindOfWire(tag & TAG_KIND_MASK);
    event.m_holding = (tag & TAG_HOLDING) != 0;
    event.m_player2 = (tag & TAG_PLAYER2) != 0;
    event.m_swift = (tag & TAG_SWIFT) != 0;
    event.m_seed = 0;
    event.m_tps = 240.0;
}

inline std::vector<uint8_t> encodeMeta(const Inputs& inputs) {
    std::vector<uint8_t> meta;
    meta.reserve(31);
    writeVarint(meta, inputs.m_seed);
    writeU64(meta, std::bit_cast<uint64_t>(inputs.m_tps));
    writeVarint(meta, inputs.m_events.size());
    writeVarint(meta, inputs.lastFrame());
    uint8_t flags = 0;
    if (inputs.m_platformer) flags |= META_PLATFORMER;
    if (inputs.m_cbf) flags |= META_CBF;
    if (inputs.m_cbfOnSteps) flags |= META_CBF_ON_STEPS;
    if (inputs.m_clickBetweenSteps) flags |= META_CLICK_BETWEEN_STEPS;
    if (inputs.m_clickOnSteps) flags |= META_CLICK_ON_STEPS;
    meta.push_back(flags);
    return meta;
}

inline std::vector<uint8_t> assemble(uint16_t version,
                                     const std::vector<uint8_t>& meta,
                                     const std::vector<uint8_t>& frames,
                                     const std::vector<uint8_t>& records) {
    std::vector<uint8_t> out;
    out.reserve(8 + 3 * 9 + meta.size() + frames.size() + records.size());
    out.insert(out.end(), FILE_MAGIC.begin(), FILE_MAGIC.end());
    writeU16(out, version);
    writeU16(out, SECTION_TOTAL);
    appendSection(out, SECTION_META, meta);
    appendSection(out, SECTION_FRAMES, frames);
    appendSection(out, SECTION_RECORDS, records);
    return out;
}

inline std::expected<std::vector<uint8_t>, std::string> encodePacked(
    const Inputs& inputs) {
    if (inputs.m_events.size() > MAX_EVENTS)
        return std::unexpected(std::string("inputs exceed the event limit"));

    const std::vector<uint8_t> meta = encodeMeta(inputs);

    BitWriter frameBits;
    frameBits.reserve(inputs.m_events.size());
    uint64_t previous = 0;
    for (const auto& event : inputs.m_events) {
        if (event.m_frame < previous)
            return std::unexpected(std::string("input events are not sorted"));
        const uint64_t delta = event.m_frame - previous;
        previous = event.m_frame;
        if (delta < PACK_DELTA_ESCAPE) {
            frameBits.put(delta, PACK_DELTA_BITS);
        } else {
            frameBits.put(PACK_DELTA_ESCAPE, PACK_DELTA_BITS);
            frameBits.putVarint(delta);
        }
    }
    const std::vector<uint8_t> frames = frameBits.finish();

    BitWriter recordBits;
    recordBits.reserve(inputs.m_events.size());
    uint8_t lastTag = 0;
    bool seenTag = false;
    for (const auto& event : inputs.m_events) {
        const uint8_t tag = wireTag(event);
        const uint8_t code = tag & TAG_KIND_MASK;

        const bool predicted =
            seenTag && static_cast<uint8_t>(lastTag ^ PACK_HOLDING) == tag;
        recordBits.put(predicted ? 1u : 0u, 1);
        if (!predicted) recordBits.put(tag, PACK_TAG_BITS);
        lastTag = tag;
        seenTag = true;

        if (wireCarriesSeed(code)) recordBits.putVarint(event.m_seed);
        if (wireCarriesTps(code))
            recordBits.put(std::bit_cast<uint64_t>(event.m_tps), 64);
    }
    const std::vector<uint8_t> records = recordBits.finish();

    return assemble(FILE_VERSION_PACKED, meta, frames, records);
}

inline std::expected<std::vector<uint8_t>, std::string> encode(
    const Inputs& inputs) {
    if (inputs.m_events.size() > MAX_EVENTS)
        return std::unexpected(std::string("inputs exceed the event limit"));

    squeeze::Encoder coder;
    coder.reserve(inputs.m_events.size() / 2 + 64);
    squeeze::Model model;
    uint64_t previous = 0;
    for (const auto& event : inputs.m_events) {
        if (event.m_frame < previous)
            return std::unexpected(std::string("input events are not sorted"));
        const uint8_t tag = wireTag(event);
        const uint8_t code = tag & TAG_KIND_MASK;
        squeeze::put(coder, model,
                     {.m_tag = tag,
                      .m_delta = event.m_frame - previous,
                      .m_seeded = wireCarriesSeed(code),
                      .m_seed = event.m_seed,
                      .m_timed = wireCarriesTps(code),
                      .m_tpsBits = std::bit_cast<uint64_t>(event.m_tps)});
        previous = event.m_frame;
    }

    return assemble(FILE_VERSION, encodeMeta(inputs), coder.finish(), {});
}

inline std::expected<Inputs, std::string> decode(std::span<const uint8_t> bytes) {
    if (bytes.size() < 8) return std::unexpected(std::string("file too small"));
    if (!std::equal(FILE_MAGIC.begin(), FILE_MAGIC.end(), bytes.begin()))
        return std::unexpected(std::string("not a vine inputs file"));
    const uint16_t version =
        static_cast<uint16_t>(bytes[4]) | static_cast<uint16_t>(bytes[5]) << 8;
    if (version != FILE_VERSION && version != FILE_VERSION_PACKED &&
        version != FILE_VERSION_PLAIN && version != FILE_VERSION_LEGACY)
        return std::unexpected(std::string("unsupported vine version"));
    const bool squeezed = version == FILE_VERSION;
    const bool packed = version == FILE_VERSION_PACKED;
    const uint16_t sections =
        static_cast<uint16_t>(bytes[6]) | static_cast<uint16_t>(bytes[7]) << 8;
    if (sections != SECTION_TOTAL)
        return std::unexpected(std::string("bad section layout"));

    std::span<const uint8_t> payloads[3];
    size_t pos = 8;
    for (uint16_t index = 0; index < SECTION_TOTAL; ++index) {
        if (bytes.size() - pos < 9)
            return std::unexpected(std::string("truncated section header"));
        if (bytes[pos] != index + 1)
            return std::unexpected(std::string("section out of order"));
        const uint32_t length = readU32(bytes, pos + 1);
        if (length > bytes.size() - pos - 9)
            return std::unexpected(std::string("truncated section body"));
        const size_t sectionEnd = pos + 5 + length + 4;
        if (sectionEnd > bytes.size())
            return std::unexpected(std::string("truncated section body"));
        const uint32_t stored = readU32(bytes, pos + 5 + length);
        const uint32_t actual =
            crc32c(bytes.subspan(pos, 5 + length));
        if (stored != actual) {
            return std::unexpected(std::string("checksum mismatch in section ") +
                                   std::to_string(index + 1));
        }
        payloads[index] = bytes.subspan(pos + 5, length);
        pos = sectionEnd;
    }
    if (pos != bytes.size())
        return std::unexpected(std::string("trailing data after sections"));

    Inputs inputs;
    try {
        detail::ByteReader meta(payloads[0]);
        inputs.m_seed = meta.takeVarint();
        inputs.m_tps = std::bit_cast<double>(meta.take64());
        const uint64_t count = meta.takeVarint();
        const uint64_t lastFrame = meta.takeVarint();
        if (version != FILE_VERSION_LEGACY) {
            const uint8_t flags = meta.take8();
            if ((flags & ~META_FLAG_MASK) != 0)
                return std::unexpected(std::string("unknown meta flags"));
            inputs.m_platformer = (flags & META_PLATFORMER) != 0;
            inputs.m_cbf = (flags & META_CBF) != 0;
            inputs.m_cbfOnSteps = (flags & META_CBF_ON_STEPS) != 0;
            inputs.m_clickBetweenSteps =
                (flags & META_CLICK_BETWEEN_STEPS) != 0;
            inputs.m_clickOnSteps = (flags & META_CLICK_ON_STEPS) != 0;
        }
        if (!meta.done())
            return std::unexpected(std::string("malformed meta section"));
        if (count > MAX_EVENTS)
            return std::unexpected(std::string("too many events"));
        if (!std::isfinite(inputs.m_tps) || inputs.m_tps <= 0.0)
            return std::unexpected(std::string("invalid tps"));
        if (squeezed) {
            if (!payloads[2].empty())
                return std::unexpected(std::string("malformed record section"));
            squeeze::Decoder coder(payloads[1]);
            squeeze::Model model;
            inputs.m_events.reserve(static_cast<size_t>(
                std::min<uint64_t>(count, uint64_t{1} << 20)));
            uint64_t frame = 0;
            squeeze::Item item;
            const auto seeded = [](unsigned tag) {
                return wireCarriesSeed(tag & TAG_KIND_MASK);
            };
            const auto timed = [](unsigned tag) {
                return wireCarriesTps(tag & TAG_KIND_MASK);
            };
            for (uint64_t i = 0; i < count; ++i) {
                if (!squeeze::get(coder, model, item, seeded, timed))
                    return std::unexpected(std::string("malformed frame section"));
                if (item.m_delta > UINT64_MAX - frame)
                    return std::unexpected(std::string("frame overflow"));
                frame += item.m_delta;
                Event& event = inputs.m_events.emplace_back();
                event.m_frame = frame;
                applyWireTag(event, static_cast<uint8_t>(item.m_tag));
                event.m_seed = item.m_seed;
                if (item.m_timed) {
                    event.m_tps = std::bit_cast<double>(item.m_tpsBits);
                    if (!std::isfinite(event.m_tps) || event.m_tps <= 0.0)
                        return std::unexpected(std::string("invalid tps event"));
                }
            }
            if (!coder.finished())
                return std::unexpected(std::string("malformed frame section"));
            if (frame != lastFrame)
                return std::unexpected(std::string("last frame mismatch"));
            return inputs;
        }

        const uint64_t frameBudget = packed
            ? static_cast<uint64_t>(payloads[1].size()) * 8 / PACK_DELTA_BITS
            : static_cast<uint64_t>(payloads[1].size());
        const uint64_t recordBudget = packed
            ? static_cast<uint64_t>(payloads[2].size()) * 8
            : static_cast<uint64_t>(payloads[2].size());
        if (count > frameBudget || count > recordBudget)
            return std::unexpected(std::string("event count exceeds section data"));

        inputs.m_events.resize(static_cast<size_t>(count));

        uint64_t frame = 0;
        if (packed) {
            BitReader frames(payloads[1].data(), payloads[1].size());
            for (uint64_t i = 0; i < count; ++i) {
                uint64_t delta = frames.get(PACK_DELTA_BITS);
                if (delta == PACK_DELTA_ESCAPE) delta = frames.getVarint();
                if (frames.overrun())
                    return std::unexpected(std::string("malformed frame section"));
                if (delta > UINT64_MAX - frame)
                    return std::unexpected(std::string("frame overflow"));
                frame += delta;
                inputs.m_events[static_cast<size_t>(i)].m_frame = frame;
            }
            if (frames.overrun() || frames.bitsLeft() >= 8)
                return std::unexpected(std::string("malformed frame section"));
        } else {
            detail::ByteReader frames(payloads[1]);
            for (uint64_t i = 0; i < count; ++i) {
                const uint64_t delta = frames.takeVarint();
                if (delta > UINT64_MAX - frame)
                    return std::unexpected(std::string("frame overflow"));
                frame += delta;
                inputs.m_events[static_cast<size_t>(i)].m_frame = frame;
            }
            if (!frames.done())
                return std::unexpected(std::string("malformed frame section"));
        }
        if (frame != lastFrame)
            return std::unexpected(std::string("last frame mismatch"));

        if (packed) {
            BitReader records(payloads[2].data(), payloads[2].size());
            uint8_t lastTag = 0;
            bool seenTag = false;
            for (uint64_t i = 0; i < count; ++i) {
                uint8_t tag = 0;
                if (records.get(1) != 0) {
                    if (!seenTag)
                        return std::unexpected(
                            std::string("malformed record section"));
                    tag = static_cast<uint8_t>(lastTag ^ PACK_HOLDING);
                } else {
                    tag = static_cast<uint8_t>(records.get(PACK_TAG_BITS));
                }
                if (records.overrun())
                    return std::unexpected(
                        std::string("malformed record section"));
                lastTag = tag;
                seenTag = true;

                const uint8_t code = tag & TAG_KIND_MASK;
                Event& event = inputs.m_events[static_cast<size_t>(i)];
                event.m_type = kindOfWire(code);
                event.m_holding = (tag & TAG_HOLDING) != 0;
                event.m_player2 = (tag & TAG_PLAYER2) != 0;
                event.m_swift = (tag & TAG_SWIFT) != 0;
                event.m_seed = 0;
                event.m_tps = 240.0;
                if (wireCarriesSeed(code)) event.m_seed = records.getVarint();
                if (wireCarriesTps(code)) {
                    event.m_tps = std::bit_cast<double>(records.get(64));
                    if (!std::isfinite(event.m_tps) || event.m_tps <= 0.0)
                        return std::unexpected(std::string("invalid tps event"));
                }
                if (records.overrun())
                    return std::unexpected(
                        std::string("malformed record section"));
            }
            if (records.overrun() || records.bitsLeft() >= 8)
                return std::unexpected(std::string("malformed record section"));
            return inputs;
        }

        detail::ByteReader records(payloads[2]);
        for (uint64_t i = 0; i < count; ++i) {
            const uint8_t tag = records.take8();
            const uint8_t code = tag & TAG_KIND_MASK;
            const bool wantsSeed = (tag & TAG_SEED) != 0;
            const bool wantsTps = (tag & TAG_TPS) != 0;
            if (wantsSeed != wireCarriesSeed(code))
                return std::unexpected(std::string("seed flag mismatch"));
            if (wantsTps != wireCarriesTps(code))
                return std::unexpected(std::string("tps flag mismatch"));

            Event& event = inputs.m_events[static_cast<size_t>(i)];
            event.m_type = kindOfWire(code);
            event.m_holding = (tag & TAG_HOLDING) != 0;
            event.m_player2 = (tag & TAG_PLAYER2) != 0;
            event.m_swift = (tag & TAG_SWIFT) != 0;
            event.m_seed = 0;
            event.m_tps = 240.0;
            if (wantsSeed) event.m_seed = records.takeVarint();
            if (wantsTps) {
                event.m_tps = std::bit_cast<double>(records.take64());
                if (!std::isfinite(event.m_tps) || event.m_tps <= 0.0)
                    return std::unexpected(std::string("invalid tps event"));
            }
        }
        if (!records.done())
            return std::unexpected(std::string("malformed record section"));
    } catch (int code) {
        return std::unexpected(std::string(
            code == 2 ? "varint overflow" : "section ran out of bytes"));
    }

    return inputs;
}

}

#endif
