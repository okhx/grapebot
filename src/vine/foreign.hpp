#ifndef GRAPE_VINE_FOREIGN_HPP
#define GRAPE_VINE_FOREIGN_HPP

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "zstd.hpp"

namespace grape::vine::foreign {

struct Click {
    uint64_t m_frame = 0;
    bool m_player2 = false;
    bool m_down = false;
};

struct Parsed {
    std::optional<double> m_tps;
    std::vector<Click> m_clicks;
    std::string m_warning;
};

enum class Format {
    Skk3,
    GucciBot,
    MhrBinary,
    EchoBinary,
    ReplayBot,
    Fembot,
    Tcm,
    YBot1,
    XBot,
    XdBot,
    OmegaBot2,
    OmegaBot3,
    OmegaBot1,
    Slc1,
    KdBot,
    Rush,
    ZBot,
    Text,
};

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Reader {
    std::span<const uint8_t> m_bytes;

    void need(size_t count) const {
        if (count > left()) throw Failure("Unexpected end of file");
    }

    template <class T>
    T little() {
        need(sizeof(T));
        T value{};
        std::memcpy(&value, m_bytes.data() + m_pos, sizeof(T));
        m_pos += sizeof(T);
        return value;
    }

   public:
    size_t m_pos = 0;

    explicit Reader(std::span<const uint8_t> bytes) : m_bytes(bytes) {}

    size_t left() const { return m_pos <= m_bytes.size() ? m_bytes.size() - m_pos : 0; }
    size_t size() const { return m_bytes.size(); }

    uint8_t u8() {
        need(1);
        return m_bytes[m_pos++];
    }
    uint16_t u16() { return little<uint16_t>(); }
    uint32_t u32() { return little<uint32_t>(); }
    uint64_t u64() { return little<uint64_t>(); }
    int16_t i16() { return little<int16_t>(); }
    int32_t i32() { return little<int32_t>(); }
    float f32() { return little<float>(); }
    double f64() { return little<double>(); }

    uint64_t readN(unsigned count) {
        need(count);
        uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= static_cast<uint64_t>(m_bytes[m_pos++]) << (8 * i);
        return value;
    }

    void skip(size_t count) {
        need(count);
        m_pos += count;
    }

    uint64_t leb() {
        uint64_t value = 0;
        for (unsigned shift = 0; shift < 70; shift += 7) {
            const uint8_t byte = u8();
            value |= static_cast<uint64_t>(byte & 0x7f) << shift;
            if ((byte & 0x80) == 0) return value;
        }
        throw Failure("Bad varint");
    }

    uint64_t prefixVarint(bool wide) {
        const uint8_t prefix = u8();
        unsigned ones = 0;
        while (ones < 8 && (prefix & (0x80u >> ones))) ++ones;
        const unsigned inline_ = wide ? 6 : 3;
        if (ones <= inline_) {
            const uint64_t rest = readN(ones);
            return (prefix & (0x7fu >> ones)) + (rest << (7 - ones));
        }
        return readN(wide ? (ones == 7 ? 7 : 8) : 4);
    }
};

inline bool startsWith(std::span<const uint8_t> bytes, std::string_view prefix) {
    return bytes.size() >= prefix.size() &&
           std::memcmp(bytes.data(), prefix.data(), prefix.size()) == 0;
}

inline double cleanFps(double fps) { return std::round(fps * 1000.0) / 1000.0; }

inline std::string_view textOf(std::span<const uint8_t> bytes) {
    const size_t probe = std::min<size_t>(bytes.size(), 4096);
    for (size_t i = 0; i < probe; ++i)
        if (bytes[i] == 0) throw Failure("Binary data");
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    return text;
}

inline std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || std::isspace(static_cast<unsigned char>(line.back()))))
            line.remove_suffix(1);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())))
            line.remove_prefix(1);
        out.push_back(line);
        start = end + 1;
    }
    return out;
}

inline std::vector<std::string_view> split(std::string_view line, std::string_view separators) {
    std::vector<std::string_view> out;
    size_t pos = 0;
    while (pos < line.size()) {
        const size_t start = line.find_first_not_of(separators, pos);
        if (start == std::string_view::npos) break;
        size_t end = line.find_first_of(separators, start);
        if (end == std::string_view::npos) end = line.size();
        out.push_back(line.substr(start, end - start));
        pos = end;
    }
    return out;
}

inline bool digits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return c >= '0' && c <= '9';
    });
}

inline uint64_t number(std::string_view text) {
    if (!digits(text) || text.size() > 18) throw Failure("Bad number");
    uint64_t value = 0;
    for (const char c : text) value = value * 10 + static_cast<uint64_t>(c - '0');
    return value;
}

inline std::optional<double> decimal(std::string_view text) {
    const size_t dot = text.find('.');
    const auto whole = text.substr(0, dot);
    if (!digits(whole)) return std::nullopt;
    double value = static_cast<double>(number(whole));
    if (dot != std::string_view::npos) {
        const auto fraction = text.substr(dot + 1);
        if (!digits(fraction)) return std::nullopt;
        double scale = 0.1;
        for (const char c : fraction) {
            value += (c - '0') * scale;
            scale /= 10.0;
        }
    }
    return value;
}

inline std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

inline void add(Parsed& parsed, uint64_t frame, bool player2, bool down) {
    parsed.m_clicks.push_back({frame, player2, down});
}

inline Parsed mhrBinary(std::span<const uint8_t> bytes) {
    if (!startsWith(bytes, std::string_view("HACKPRO\x07", 8))) throw Failure("Not MHR");
    Reader r(bytes);
    r.skip(8);
    const int32_t metaSize = r.i32();
    const int32_t fps = r.i32();
    if (metaSize < 4) throw Failure("Bad MHR meta");
    r.skip(static_cast<size_t>(metaSize) - 4);
    r.skip(8);
    const uint32_t eventSize = r.u32();
    const uint32_t count = r.u32();
    if (eventSize < 8 || static_cast<uint64_t>(count) * eventSize > r.left())
        throw Failure("Bad MHR events");
    Parsed parsed;
    parsed.m_tps = fps;
    for (uint32_t i = 0; i < count; ++i) {
        r.skip(2);
        const uint8_t hold = r.u8();
        const uint8_t player = r.u8();
        const int32_t frame = r.i32();
        r.skip(eventSize - 8);
        if (frame < 0) throw Failure("Bad MHR frame");
        add(parsed, static_cast<uint64_t>(frame), player == 1, hold == 1);
    }
    return parsed;
}

inline void gucciStream(std::span<const uint8_t> bytes, bool player2, Parsed& parsed,
                        unsigned& skipped) {
    uint64_t frame = 0;
    uint64_t pending = 0;
    const auto tap = [&](uint64_t hold) {
        add(parsed, frame, player2, true);
        frame += hold;
        add(parsed, frame, player2, false);
    };
    size_t i = 0;
    while (i + 1 < bytes.size()) {
        const uint8_t a = bytes[i];
        const uint8_t b = bytes[i + 1];
        i += 2;
        if (a == 0x00) {
            if (i + 1 >= bytes.size()) break;
            const unsigned cycles = bytes[i] | (bytes[i + 1] << 8);
            i += 2;
            const unsigned hold = b >> 4;
            const unsigned release = b & 0x0f;
            if (!hold || !release || !cycles) continue;
            frame += pending;
            pending = 0;
            for (unsigned c = 0; c < cycles; ++c) {
                tap(hold);
                frame += release;
            }
            continue;
        }
        if (b == 0xff) {
            pending += 255;
            if (!(a & 0x80)) {
                frame += pending;
                pending = 0;
                tap(a & 0x7f);
            }
            continue;
        }
        frame += pending + b;
        pending = 0;
        if (!(a & 0x80)) {
            tap(a & 0x7f);
            continue;
        }
        if (a == 0x80) continue;
        const unsigned button = (a >> 1) & 0x07;
        if (button > 1) {
            ++skipped;
            continue;
        }
        add(parsed, frame, player2 || (a & 0x10) != 0, (a & 0x01) != 0);
    }
}

inline constexpr uint32_t SKK3_COMPRESSED = 1u << 10;
inline constexpr size_t SKK3_MAX_PAYLOAD = 256u * 1024u * 1024u;

inline int64_t zigzag(uint64_t value) {
    return static_cast<int64_t>(value >> 1) ^ -static_cast<int64_t>(value & 1);
}

inline Parsed skk3(std::span<const uint8_t> bytes) {
    if (!startsWith(bytes, "SKK3")) throw Failure("Not SKK3");
    Reader header(bytes);
    header.skip(4);
    const uint16_t version = header.u16();
    if (version != 6 && version != 7) throw Failure("Unsupported SKK3 version");
    header.u16();
    const uint32_t flags = header.u32();
    const uint32_t headerLength = header.u32();
    if (headerLength < 16 || headerLength > bytes.size()) throw Failure("Bad SKK3 header");

    Reader meta(bytes.first(headerLength));
    meta.m_pos = 16;
    meta.skip(20);
    meta.skip(meta.leb());
    meta.skip(meta.leb());
    double tps = meta.f64();
    if (!(std::isfinite(tps) && tps > 0.0 && tps <= 1'000'000.0)) tps = 240.0;

    Reader table(bytes);
    table.m_pos = headerLength;
    const uint16_t sections = table.u16();
    uint64_t inputsOffset = 0;
    uint64_t inputsSize = 0;
    bool haveInputs = false;
    uint64_t payloadSize = 0;
    for (uint16_t i = 0; i < sections; ++i) {
        const uint8_t kind = table.u8();
        table.skip(3);
        const uint64_t offset = table.u64();
        const uint64_t size = table.u64();
        if (size > SKK3_MAX_PAYLOAD || offset > SKK3_MAX_PAYLOAD) throw Failure("Bad SKK3 section");
        payloadSize = std::max(payloadSize, offset + size);
        if (kind == 1) {
            haveInputs = true;
            inputsOffset = offset;
            inputsSize = size;
        }
    }
    if (!haveInputs) throw Failure("SKK3 has no inputs section");

    std::vector<uint8_t> unpacked;
    std::span<const uint8_t> payload = bytes.subspan(table.m_pos);
    if (flags & SKK3_COMPRESSED) {
        if (payload.size() < 8 || payload[0] != 'Z' || payload[1] != 'D' || payload[3] != 0)
            throw Failure("Bad SKK3 compression header");
        uint32_t original = 0;
        std::memcpy(&original, payload.data() + 4, 4);
        if (original == 0 || original > SKK3_MAX_PAYLOAD) throw Failure("Bad SKK3 payload size");
        auto decoded = zstd::decompress(payload.subspan(8), original);
        if (!decoded || decoded->size() != original) throw Failure("Could not decompress SKK3 payload");
        unpacked = std::move(*decoded);
        payload = unpacked;
    }
    if (payloadSize > payload.size()) throw Failure("SKK3 section outside payload");

    Reader inputs(payload.subspan(static_cast<size_t>(inputsOffset), static_cast<size_t>(inputsSize)));
    const uint64_t count = inputs.leb();
    if (count > inputs.left() / 2) throw Failure("Bad SKK3 input count");

    struct Timed {
        double m_time;
        uint8_t m_button;
        bool m_player2;
        bool m_down;
        bool m_swift;
    };
    std::vector<Timed> timed;
    timed.reserve(static_cast<size_t>(count));
    const double unit = 1.0 / tps;
    int64_t tick = 0;
    int64_t lastDelta = 0;
    int64_t lastResidue = 0;
    for (uint64_t i = 0; i < count; ++i) {
        const uint8_t button = inputs.u8();
        const uint8_t bits = inputs.u8();
        Timed entry{};
        if (button == 0) {
            if (timed.empty() || (bits & ~0x02u)) throw Failure("Bad SKK3 repeat");
            tick += lastDelta;
            entry = {0.0, timed.back().m_button, timed.back().m_player2, (bits & 2) != 0, false};
        } else {
            if (button > 3 || (bits & ~0x07u)) throw Failure("Bad SKK3 input");
            lastDelta = zigzag(inputs.leb());
            lastResidue = zigzag(inputs.leb());
            tick += lastDelta;
            entry = {0.0, button, (bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0};
        }
        entry.m_time = static_cast<double>(tick) * unit + static_cast<double>(lastResidue) / 1e12;
        if (!std::isfinite(entry.m_time) || entry.m_time < 0.0) throw Failure("Bad SKK3 input time");
        timed.push_back(entry);
    }
    for (size_t i = 0; i + 1 < timed.size(); ++i)
        if (timed[i].m_swift) timed[i + 1].m_time = timed[i].m_time;

    Parsed parsed;
    parsed.m_tps = tps;
    unsigned skipped = 0;
    for (const auto& entry : timed) {
        if (entry.m_button != 1) {
            ++skipped;
            continue;
        }
        const double frame = std::floor(entry.m_time * tps + 0.000001);
        if (frame < 0.0 || frame > 2147483647.0) throw Failure("Bad SKK3 input time");
        add(parsed, static_cast<uint64_t>(frame), entry.m_player2, entry.m_down);
    }
    if (skipped) parsed.m_warning = std::to_string(skipped) + " platformer inputs skipped";
    return parsed;
}

inline Parsed gucciBot(std::span<const uint8_t> bytes) {
    if (!startsWith(bytes, "GBR6")) throw Failure("Not GucciBot");
    Reader r(bytes);
    r.skip(6);
    const float tps = r.f32();
    r.skip(16);
    const uint32_t first = r.u32();
    const uint32_t second = r.u32();
    r.skip(r.u16());
    if (static_cast<uint64_t>(first) + second > r.left()) throw Failure("Bad GBR6 streams");
    Parsed parsed;
    parsed.m_tps = cleanFps(tps);
    unsigned skipped = 0;
    gucciStream(bytes.subspan(r.m_pos, first), false, parsed, skipped);
    gucciStream(bytes.subspan(r.m_pos + first, second), true, parsed, skipped);
    std::stable_sort(parsed.m_clicks.begin(), parsed.m_clicks.end(),
                     [](const Click& x, const Click& y) { return x.m_frame < y.m_frame; });
    if (skipped) parsed.m_warning = std::to_string(skipped) + " platformer inputs skipped";
    return parsed;
}

inline Parsed echoBinary(std::span<const uint8_t> bytes) {
    if (!startsWith(bytes, "META")) throw Failure("Not Echo");
    const bool debug = bytes.size() >= 8 && std::memcmp(bytes.data() + 4, "DBG\0", 4) == 0;
    const size_t size = debug ? 34 : 6;
    Reader r(bytes);
    r.m_pos = 24;
    const float fps = r.f32();
    r.m_pos = 48;
    Parsed parsed;
    parsed.m_tps = cleanFps(fps);
    while (r.left() >= size) {
        const uint32_t frame = r.u32();
        const uint8_t down = r.u8();
        const uint8_t player = r.u8();
        if (debug) r.skip(28);
        add(parsed, frame, player == 1, down == 1);
    }
    return parsed;
}

inline Parsed omegaBot3(std::span<const uint8_t> bytes) {
    Reader r(bytes);
    const float fps = r.f32();
    r.f32();
    const uint64_t length = r.prefixVarint(true);
    if (length > r.left()) throw Failure("Not OmegaBot 3");
    Parsed parsed;
    parsed.m_tps = cleanFps(fps);
    for (uint64_t i = 0; i < length; ++i) {
        const uint64_t frame = r.prefixVarint(false);
        const uint64_t value = r.prefixVarint(false);
        if (value > 5) throw Failure("Not OmegaBot 3");
        if (value == 5) {
            r.f32();
            continue;
        }
        if (value >= 1) add(parsed, frame, value >= 3, value == 1 || value == 3);
    }
    r.prefixVarint(true);
    if (r.left() != 0) throw Failure("Not OmegaBot 3");
    return parsed;
}

inline Parsed omegaBot2(std::span<const uint8_t> bytes, bool autoOffset) {
    Reader r(bytes);
    const float fps = r.f32();
    r.f32();
    if (r.u32() > 1) throw Failure("Not OmegaBot 2");
    r.u64();
    const uint64_t length = r.u64();
    if (length > r.left() / 12) throw Failure("Not OmegaBot 2");
    const uint64_t offset = autoOffset ? 1 : 0;
    Parsed parsed;
    parsed.m_tps = cleanFps(fps);
    for (uint64_t i = 0; i < length; ++i) {
        const uint32_t location = r.u32();
        if (location > 1) throw Failure("Not OmegaBot 2");
        const uint32_t value = r.u32();
        const uint32_t click = r.u32();
        if (click > 5) throw Failure("Not OmegaBot 2");
        if (click == 1) {
            r.f32();
            continue;
        }
        if (location == 0) throw Failure("Position based OmegaBot 2 macros are not supported");
        if (click >= 2) add(parsed, value + offset, click >= 4, click == 2 || click == 4);
    }
    if (r.left() != 0) throw Failure("Not OmegaBot 2");
    return parsed;
}

inline Parsed omegaBot1(std::span<const uint8_t> bytes) {
    Reader r(bytes);
    const float fps = r.f32();
    const uint8_t type = r.u8();
    if (type > 2) throw Failure("Not URL");
    if (type == 0) throw Failure("Position based macros are not supported");
    const size_t size = type == 2 ? 9 : 5;
    if ((bytes.size() - 5) % size != 0) throw Failure("Not URL");
    Parsed parsed;
    parsed.m_tps = cleanFps(fps);
    while (r.left() >= size) {
        const uint8_t state = r.u8();
        if (type == 2) r.skip(4);
        const uint32_t frame = r.u32();
        if (state > 3) throw Failure("Not URL");
        add(parsed, frame, (state >> 1) == 1, (state & 1) == 1);
    }
    return parsed;
}

inline Parsed replayBot(std::span<const uint8_t> bytes) {
    if (!startsWith(bytes, "RPLY")) throw Failure("Not ReplayBot");
    Reader r(bytes);
    r.skip(4);
    if (r.u8() != 2 || r.u8() != 1) throw Failure("Unsupported ReplayBot version");
    Parsed parsed;
    parsed.m_tps = cleanFps(r.f32());
    while (r.left() >= 5) {
        const uint32_t frame = r.u32();
        const uint8_t state = r.u8();
        add(parsed, frame, (state & 2) != 0, (state & 1) != 0);
    }
    return parsed;
}

inline Parsed kdBot(std::span<const uint8_t> bytes) {
    Reader r(bytes);
    const float fps = r.f32();
    if ((bytes.size() - 4) % 6 != 0) throw Failure("Not KD-Bot");
    Parsed parsed;
    parsed.m_tps = cleanFps(fps);
    while (r.left() >= 6) {
        const uint32_t frame = r.u32();
        const uint8_t hold = r.u8();
        const uint8_t player = r.u8();
        if (hold > 1 || player > 1) throw Failure("Not KD-Bot");
        add(parsed, frame, player == 1, hold == 1);
    }
    return parsed;
}

inline Parsed rush(std::span<const uint8_t> bytes) {
    Reader r(bytes);
    const int16_t fps = r.i16();
    if ((bytes.size() - 2) % 5 != 0) throw Failure("Not Rush");
    Parsed parsed;
    parsed.m_tps = fps;
    while (r.left() >= 5) {
        const uint32_t frame = r.u32();
        const uint8_t state = r.u8();
        if (state > 3) throw Failure("Not Rush");
        add(parsed, frame, (state & 2) != 0, (state & 1) != 0);
    }
    return parsed;
}

inline Parsed zBot(std::span<const uint8_t> bytes) {
    Reader r(bytes);
    const float delta = r.f32();
    float speed = r.f32();
    if (speed == 0.f) speed = 1.f;
    if ((bytes.size() - 8) % 6 != 0) throw Failure("Not zBot");
    Parsed parsed;
    parsed.m_tps = std::round(1.0 / (static_cast<double>(delta) * speed));
    while (r.left() >= 6) {
        const int32_t frame = r.i32();
        const uint8_t hold = r.u8();
        const uint8_t player1 = r.u8();
        if ((hold != '0' && hold != '1') || (player1 != '0' && player1 != '1'))
            throw Failure("Not zBot");
        if (frame < 0) throw Failure("Not zBot");
        add(parsed, static_cast<uint64_t>(frame), player1 != '1', hold == '1');
    }
    return parsed;
}

inline Parsed fembot(std::span<const uint8_t> bytes) {
    if (!startsWith(bytes, "FBRP")) throw Failure("Not Fembot");
    Reader r(bytes);
    r.skip(4);
    Parsed parsed;
    parsed.m_tps = cleanFps(r.f32());
    while (r.left() >= 65) {
        const uint8_t state = r.u8();
        const uint32_t frame = r.u32();
        r.skip(60);
        add(parsed, frame, (state & 2) != 0, (state & 1) != 0);
    }
    return parsed;
}

inline Parsed xBot(std::span<const uint8_t> bytes) {
    const auto all = lines(textOf(bytes));
    const std::string head = lower(all.empty() ? std::string_view{} : all[0]);
    if (!head.starts_with("fps")) throw Failure("Not xBot");
    const size_t colon = head.find(':');
    if (colon == std::string::npos) throw Failure("Not xBot");
    std::string_view value(head);
    value.remove_prefix(colon + 1);
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    const auto fps = decimal(value);
    if (!fps) throw Failure("Not xBot");
    Parsed parsed;
    parsed.m_tps = *fps;
    for (size_t i = 1; i < all.size(); ++i) {
        const std::string line = lower(all[i]);
        if (line.empty() || line == "frames" || line == "positions") continue;
        const auto parts = split(line, " \t");
        if (parts.size() < 2 || !digits(parts[0]) || !digits(parts[1]) || number(parts[0]) > 3)
            throw Failure("Not xBot");
        const uint64_t state = number(parts[0]);
        add(parsed, number(parts[1]), state > 1, state % 2 == 1);
    }
    return parsed;
}

inline Parsed xdBot(std::span<const uint8_t> bytes) {
    const auto all = lines(textOf(bytes));
    size_t i = 0;
    while (i < all.size() && all[i].empty()) ++i;
    const auto fps = i < all.size() ? decimal(all[i]) : std::nullopt;
    if (!fps) throw Failure("Not XDBot");
    Parsed parsed;
    parsed.m_tps = *fps;
    for (++i; i < all.size(); ++i) {
        if (all[i].empty()) continue;
        const auto parts = split(all[i], "|");
        if (parts.size() < 4 || !digits(parts[0]) || !digits(parts[1]) ||
            !digits(parts[2]) || !digits(parts[3]))
            throw Failure("Not XDBot");
        if (number(parts[2]) != 1) continue;
        add(parsed, number(parts[0]), number(parts[3]) != 1, number(parts[1]) == 1);
    }
    return parsed;
}

inline Parsed yBot1(std::span<const uint8_t> bytes) {
    if (!startsWith(bytes, "ybot")) throw Failure("Not yBot 1");
    Reader r(bytes);
    r.skip(4);
    const float fps = r.f32();
    const int32_t count = r.i32();
    if (count < 0 || bytes.size() != 12 + 8 * static_cast<uint64_t>(count))
        throw Failure("Not yBot 1");
    Parsed parsed;
    parsed.m_tps = cleanFps(fps);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t frame = r.u32();
        const uint32_t state = r.u32();
        add(parsed, frame, (state & 1) != 0, (state & 2) != 0);
    }
    return parsed;
}

inline Parsed slc1(std::span<const uint8_t> bytes) {
    Reader r(bytes);
    const double fps = r.f64();
    const uint32_t count = r.u32();
    if (!(fps >= 1.0 && fps <= 100000.0) || bytes.size() != 12 + 4 * static_cast<uint64_t>(count))
        throw Failure("Unsupported legacy macro version");
    Parsed parsed;
    parsed.m_tps = cleanFps(fps);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t state = r.u32();
        if (((state & 6) >> 1) != 1) continue;
        add(parsed, state >> 4, (state & 8) != 0, (state & 1) != 0);
    }
    return parsed;
}

inline constexpr uint8_t TCM_MAGIC[16] = {0x9f, 0x88, 0x89, 0x84, 0x9f, 0x3b, 0x1d, 0xd8,
                                          0xcc, 0xa1, 0x86, 0x8a, 0x88, 0x99, 0x84, 0x00};

inline Parsed tcm(std::span<const uint8_t> bytes) {
    if (bytes.size() < 16 + 64 || std::memcmp(bytes.data(), TCM_MAGIC, 16) != 0)
        throw Failure("Not TCM");
    const uint8_t version = bytes[16];
    if (version != 1 && version != 2) throw Failure("Unsupported TCM version");
    float stored = 0.f;
    std::memcpy(&stored, bytes.data() + 20, 4);
    const double fps = version == 2 && !(bytes[18] & 2) ? 1.0 / stored : stored;
    Reader r(bytes);
    r.m_pos = 16 + 64;
    Parsed parsed;
    parsed.m_tps = cleanFps(fps);
    unsigned restarts = 0;
    if (version == 1) {
        const uint64_t count = r.leb();
        for (uint64_t i = 0; i < count; ++i) {
            const uint64_t frame = r.leb();
            const uint8_t byte = r.u8();
            const unsigned input = byte & 7;
            if (input > 5) throw Failure("Bad TCM input");
            if (input == 0) add(parsed, frame, (byte & 0x40) != 0, (byte & 0x80) != 0);
            else if (input >= 3) ++restarts;
        }
    } else {
        uint64_t current = r.leb();
        uint64_t lastDelta = 0;
        unsigned state = 0;
        unsigned blob = 0;
        bool magic = false;
        uint64_t magicBase = 0;
        for (;;) {
            if (state == 0) {
                if (r.left() < 1) break;
                const uint8_t byte = r.u8();
                const unsigned descriptor = (byte >> 5) & 7;
                blob = (descriptor >> 1) & 3;
                magic = (descriptor & 1) != 0;
                magicBase = lastDelta;
                const unsigned input = byte & 3;
                if (input > 0) {
                    const bool push = (byte & 4) != 0;
                    const bool player2 = (byte & 8) != 0;
                    const bool swift = (byte & 16) != 0;
                    if (input == 1) {
                        add(parsed, current, player2, push);
                        if (swift) add(parsed, current, player2, !push);
                    }
                    state = 1;
                } else {
                    const unsigned command = (byte >> 2) & 3;
                    const bool extra = (byte & 16) != 0;
                    if (command == 3) {
                        state = extra ? 1 : 2;
                    } else {
                        state = extra ? 3 : 1;
                        current = 0;
                        ++restarts;
                    }
                }
            } else if (state == 1) {
                uint64_t delta = 0;
                if (blob == 0) {
                    delta = magic ? magicBase : 0;
                } else {
                    const unsigned size = blob == 1 ? 1 : blob == 2 ? 2 : 4;
                    if (r.left() < size) break;
                    delta = (magic ? magicBase : 0) + r.readN(size);
                    if (delta != 0) lastDelta = delta;
                }
                current += delta;
                state = 0;
            } else if (state == 2) {
                r.skip(4);
                state = 1;
            } else {
                r.skip(8);
                state = 1;
            }
        }
    }
    if (restarts)
        parsed.m_warning = std::to_string(restarts) +
                           " restarts found, frames after a restart count from 0";
    return parsed;
}

inline bool isWord(std::string_view word, std::initializer_list<std::string_view> options) {
    return std::find(options.begin(), options.end(), word) != options.end();
}

inline Parsed text(std::span<const uint8_t> bytes) {
    Parsed parsed;
    unsigned bad = 0;
    for (const auto raw : lines(textOf(bytes))) {
        const std::string line = lower(raw);
        if (line.empty() || line[0] == '#' || line.starts_with("//")) continue;
        if (line.starts_with("fps") || line.starts_with("tps")) {
            std::string_view rest(line);
            rest.remove_prefix(3);
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == ':' ||
                                     rest.front() == '=' || rest.front() == ','))
                rest.remove_prefix(1);
            if (const auto fps = decimal(rest)) {
                parsed.m_tps = *fps;
                continue;
            }
        }
        if (!parsed.m_tps && parsed.m_clicks.empty()) {
            if (const auto fps = decimal(line)) {
                parsed.m_tps = *fps;
                continue;
            }
        }
        const auto parts = split(line, " \t,;|");
        if (parts.empty() || !digits(parts[0])) {
            if (!parsed.m_clicks.empty()) ++bad;
            continue;
        }
        const uint64_t frame = number(parts[0]);
        std::optional<bool> down;
        std::optional<bool> player2;
        const auto flag = [](std::string_view word) {
            return isWord(word, {"0", "1", "true", "false"});
        };
        const auto truthy = [](std::string_view word) { return word == "1" || word == "true"; };
        if (parts.size() == 3 && flag(parts[1]) && flag(parts[2])) {
            down = truthy(parts[1]);
            player2 = truthy(parts[2]);
        } else {
            std::vector<std::string_view> leftover;
            for (size_t i = 1; i < parts.size(); ++i) {
                const auto word = parts[i];
                if (isWord(word, {"down", "press", "pressed", "click", "hold", "d"})) down = true;
                else if (isWord(word, {"up", "release", "released", "r", "u"})) down = false;
                else if (isWord(word, {"p1", "1p", "player1", "plr1"})) player2 = false;
                else if (isWord(word, {"p2", "2p", "player2", "plr2"})) player2 = true;
                else leftover.push_back(word);
            }
            for (const auto word : leftover) {
                if (!player2 && down && (word == "1" || word == "2")) player2 = word == "2";
                else if (!down && flag(word)) down = truthy(word);
                else if (!player2 && (word == "1" || word == "2")) player2 = word == "2";
            }
        }
        if (!down) {
            ++bad;
            continue;
        }
        add(parsed, frame, player2.value_or(false), *down);
    }
    if (parsed.m_clicks.empty()) throw Failure("Not a text macro");
    if (bad) parsed.m_warning = std::to_string(bad) + " unreadable lines skipped";
    return parsed;
}

inline Parsed parse(Format format, std::span<const uint8_t> bytes, bool autoOffset = true) {
    switch (format) {
        case Format::Skk3: return skk3(bytes);
        case Format::GucciBot: return gucciBot(bytes);
        case Format::MhrBinary: return mhrBinary(bytes);
        case Format::EchoBinary: return echoBinary(bytes);
        case Format::ReplayBot: return replayBot(bytes);
        case Format::Fembot: return fembot(bytes);
        case Format::Tcm: return tcm(bytes);
        case Format::YBot1: return yBot1(bytes);
        case Format::XBot: return xBot(bytes);
        case Format::XdBot: return xdBot(bytes);
        case Format::OmegaBot2: return omegaBot2(bytes, autoOffset);
        case Format::OmegaBot3: return omegaBot3(bytes);
        case Format::OmegaBot1: return omegaBot1(bytes);
        case Format::Slc1: return slc1(bytes);
        case Format::KdBot: return kdBot(bytes);
        case Format::Rush: return rush(bytes);
        case Format::ZBot: return zBot(bytes);
        case Format::Text: return text(bytes);
    }
    throw Failure("Unknown format");
}

inline const char* nameOf(Format format) {
    switch (format) {
        case Format::Skk3: return "SKK3";
        case Format::GucciBot: return "GucciBot";
        case Format::MhrBinary: return "Mega Hack Replay";
        case Format::EchoBinary: return "Echo";
        case Format::ReplayBot: return "ReplayBot";
        case Format::Fembot: return "Fembot";
        case Format::Tcm: return "TCBot";
        case Format::YBot1: return "yBot 1";
        case Format::XBot: return "xBot";
        case Format::XdBot: return "XDBot";
        case Format::OmegaBot2: return "OmegaBot 2";
        case Format::OmegaBot3: return "OmegaBot 3";
        case Format::OmegaBot1: return "OmegaBot 1";
        case Format::Slc1: return "Legacy v1";
        case Format::KdBot: return "KD-Bot";
        case Format::Rush: return "Rush";
        case Format::ZBot: return "zBot";
        case Format::Text: return "Plain text";
    }
    return "Unknown";
}

inline bool sortedOnly(Format format) {
    switch (format) {
        case Format::OmegaBot2:
        case Format::OmegaBot3:
        case Format::OmegaBot1:
        case Format::Slc1:
        case Format::KdBot:
        case Format::Rush:
        case Format::ZBot:
            return true;
        default:
            return false;
    }
}

inline bool plausible(const Parsed& parsed, Format format) {
    if (parsed.m_clicks.empty()) return false;
    if (parsed.m_tps && !(*parsed.m_tps >= 1.0 && *parsed.m_tps <= 1'000'000.0)) return false;
    uint64_t previous = 0;
    for (const auto& click : parsed.m_clicks) {
        if (click.m_frame > 2147483647ull) return false;
        if (sortedOnly(format)) {
            if (click.m_frame < previous) return false;
            previous = click.m_frame;
        }
    }
    return true;
}

inline constexpr Format DETECTION_ORDER[] = {
    Format::Skk3,      Format::GucciBot,  Format::MhrBinary, Format::ReplayBot, Format::Fembot,    Format::EchoBinary,
    Format::Tcm,       Format::YBot1,     Format::XBot,      Format::XdBot,
    Format::OmegaBot2, Format::OmegaBot3, Format::OmegaBot1, Format::Slc1,
    Format::KdBot,     Format::Rush,      Format::ZBot,      Format::Text,
};

inline std::vector<Format> hintsFor(std::string_view extension) {
    const std::string ext = lower(extension);
    if (ext == ".brrr" || ext == ".gbr" || ext == ".gbr6") return {Format::GucciBot};
    if (ext == ".skk" || ext == ".skk3") return {Format::Skk3};
    if (ext == ".kd") return {Format::KdBot};
    if (ext == ".rsh") return {Format::Rush};
    if (ext == ".zbf") return {Format::ZBot};
    if (ext == ".freplay") return {Format::Fembot};
    if (ext == ".ybf" || ext == ".ybot") return {Format::YBot1};
    if (ext == ".xd") return {Format::XdBot};
    if (ext == ".xbot") return {Format::XBot};
    if (ext == ".slc") return {Format::Slc1};
    if (ext == ".tcm") return {Format::Tcm};
    if (ext == ".mhr") return {Format::MhrBinary};
    if (ext == ".echo") return {Format::EchoBinary};
    if (ext == ".replay")
        return {Format::OmegaBot3, Format::OmegaBot2, Format::ReplayBot, Format::OmegaBot1};
    if (ext == ".txt" || ext == ".csv") return {Format::Text};
    return {};
}

struct Detected {
    Format m_format;
    Parsed m_parsed;
};

inline std::expected<Detected, std::string> detect(std::span<const uint8_t> bytes,
                                                   std::string_view extension,
                                                   bool autoOffset = true) {
    std::vector<Format> order = hintsFor(extension);
    const size_t hinted = order.size();
    for (const Format format : DETECTION_ORDER)
        if (std::find(order.begin(), order.end(), format) == order.end())
            order.push_back(format);

    std::string firstError;
    for (size_t i = 0; i < order.size(); ++i) {
        try {
            Parsed parsed = parse(order[i], bytes, autoOffset);
            if (!plausible(parsed, order[i])) throw Failure("No plausible inputs");
            return Detected{order[i], std::move(parsed)};
        } catch (const std::exception& error) {
            if (i < hinted && firstError.empty()) firstError = error.what();
        }
    }
    return std::unexpected(firstError.empty() ? std::string("Could not recognise this format")
                                              : firstError);
}

}

#endif
