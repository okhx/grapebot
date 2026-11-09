#ifndef GRAPE_VINE_SLC_HPP
#define GRAPE_VINE_SLC_HPP

#include "format.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace grape::vine {

struct SlcData {
    std::vector<Event> m_events;
    uint64_t m_seed = 0;
    double m_tps = 240.0;
};

std::expected<SlcData, std::string> decodeSlc(std::span<const uint8_t> bytes);

namespace slcraw {

struct Scanner {
    std::span<const uint8_t> m_bytes;
    size_t m_at = 0;

    size_t left() const { return m_bytes.size() - m_at; }
    bool drained() const { return m_at == m_bytes.size(); }

    std::expected<uint64_t, std::string> low(uint64_t width) {
        if (width > left())
            return std::unexpected(std::string("slc replay truncated"));
        uint64_t value = 0;
        for (uint64_t i = 0; i < width; ++i)
            value |= static_cast<uint64_t>(m_bytes[m_at + i]) << (i * 8);
        m_at += width;
        return value;
    }

    std::expected<double, std::string> real() {
        const auto raw = low(8);
        if (!raw.has_value()) return std::unexpected(raw.error());
        return std::bit_cast<double>(raw.value());
    }

    std::expected<std::span<const uint8_t>, std::string> carve(
        uint64_t width) {
        if (width > left())
            return std::unexpected(std::string("slc replay truncated"));
        const auto out = m_bytes.subspan(m_at, static_cast<size_t>(width));
        m_at += static_cast<size_t>(width);
        return out;
    }
};

inline uint64_t lastEventFrame(const std::vector<Event>& events) {
    return events.empty() ? 0 : events.back().m_frame;
}

inline std::expected<void, std::string> appendPlayer(
    std::vector<Event>& events, uint64_t delta, uint8_t button, bool holding,
    bool player2) {
    const uint64_t frame = lastEventFrame(events) + delta;
    if (button == 0) {
        Event press(frame, EventKind::Jump, true, player2);
        press.m_swift = true;
        events.push_back(press);
        Event lift(frame, EventKind::Jump, false, player2);
        lift.m_swift = true;
        events.push_back(lift);
        return {};
    }
    const EventKind kind =
        button == 2 ? EventKind::Left
                    : (button == 3 ? EventKind::Right : EventKind::Jump);
    events.emplace_back(frame, kind, holding, player2);
    return {};
}

inline std::expected<void, std::string> decodeCluster(
    Scanner& atom, std::vector<Event>& events, uint64_t count,
    uint16_t deltaExp, uint64_t repeats) {
    const uint64_t width = 1ull << deltaExp;
    if (count > atom.left() / width)
        return std::unexpected(std::string("slc input count exceeds data"));

    std::vector<std::pair<uint64_t, uint8_t>> cluster;
    cluster.reserve(count);
    for (uint64_t i = 0; i < count; ++i) {
        const auto raw = atom.low(width);
        if (!raw.has_value()) return std::unexpected(raw.error());
        cluster.emplace_back(raw.value() >> 4,
                             static_cast<uint8_t>(raw.value() & 0xF));
    }
    for (uint64_t round = 0; round < repeats; ++round) {
        for (const auto& [delta, bits] : cluster) {
            const auto placed =
                appendPlayer(events, delta, (bits >> 2) & 3, (bits & 1) != 0,
                             (bits & 2) != 0);
            if (!placed.has_value()) return placed;
        }
    }
    return {};
}

inline std::expected<void, std::string> decodeSection(
    Scanner& atom, std::vector<Event>& events) {
    const auto header = atom.low(2);
    if (!header.has_value()) return std::unexpected(header.error());
    const auto field = static_cast<uint16_t>(header.value());

    switch (field >> 14) {
        case 0: {
            const uint16_t deltaExp = (field >> 12) & 0x3;
            const uint64_t count = 1ull << ((field >> 8) & 0xF);
            return decodeCluster(atom, events, count, deltaExp, 1);
        }
        case 1: {
            const uint16_t deltaExp = (field >> 12) & 0x3;
            const uint64_t count = 1ull << ((field >> 8) & 0xF);
            const uint64_t repeats = 1ull << ((field >> 3) & 0x1F);
            return decodeCluster(atom, events, count, deltaExp, repeats);
        }
        case 2: {
            const uint8_t special = (field >> 10) & 0xF;
            const uint16_t deltaExp = (field >> 8) & 0x3;
            const auto delta = atom.low(1ull << deltaExp);
            if (!delta.has_value()) return std::unexpected(delta.error());
            const uint64_t frame = lastEventFrame(events) + delta.value();
            switch (special) {
                case 3: {
                    const auto tps = atom.real();
                    if (!tps.has_value())
                        return std::unexpected(tps.error());
                    if (!std::isfinite(tps.value()) || tps.value() <= 0.0)
                        return std::unexpected(
                            std::string("slc replay has invalid tps"));
                    events.emplace_back(frame, tps.value());
                    return {};
                }
                case 0:
                case 1:
                case 2: {
                    const auto seed = atom.low(8);
                    if (!seed.has_value())
                        return std::unexpected(seed.error());
                    const EventKind kind = special == 0
                                               ? EventKind::Restart
                                               : (special == 1
                                                      ? EventKind::RestartFull
                                                      : EventKind::Death);
                    events.emplace_back(frame, kind, seed.value());
                    events.emplace_back(frame, EventKind::Bugpoint);
                    return {};
                }
                case 4:
                    events.emplace_back(frame, EventKind::Bugpoint);
                    return {};
                default:
                    return std::unexpected(
                        std::string("slc replay has unknown special"));
            }
        }
        default:
            return std::unexpected(
                std::string("slc replay has unknown section id"));
    }
}

inline std::expected<void, std::string> decodeActionAtom(
    const std::span<const uint8_t> body, std::vector<Event>& events) {
    Scanner atom(body);
    const auto declared = atom.low(8);
    if (!declared.has_value()) return std::unexpected(declared.error());
    if (declared.value() > body.size() - 8)
        return std::unexpected(
            std::string("slc action count exceeds atom size"));
    while (events.size() < declared.value()) {
        const auto section = decodeSection(atom, events);
        if (!section.has_value()) return section;
    }
    return {};
}

inline std::expected<SlcData, std::string> decodeV3(
    std::span<const uint8_t> bytes) {
    Scanner rest(bytes.subspan(8));
    const auto metaWidth = rest.low(2);
    if (!metaWidth.has_value()) return std::unexpected(metaWidth.error());
    if (metaWidth.value() != 64)
        return std::unexpected(std::string("slc replay has bad meta size"));

    SlcData data;
    const auto tps = rest.real();
    if (!tps.has_value()) return std::unexpected(tps.error());
    const auto seed = rest.low(8);
    if (!seed.has_value()) return std::unexpected(seed.error());
    const auto unused = rest.carve(48);
    if (!unused.has_value()) return std::unexpected(unused.error());
    data.m_seed = seed.value();
    data.m_tps =
        std::isfinite(tps.value()) && tps.value() > 0.0 ? tps.value() : 240.0;

    while (rest.left() > 1) {
        const auto id = rest.low(4);
        if (!id.has_value()) return std::unexpected(id.error());
        const auto packed = rest.low(8);
        if (!packed.has_value()) return std::unexpected(packed.error());
        const auto body = rest.carve(packed.value() & 0x00FF'FFFF'FFFF'FFFF);
        if (!body.has_value()) return std::unexpected(body.error());
        if (id.value() == 1) {
            const auto actions = decodeActionAtom(body.value(), data.m_events);
            if (!actions.has_value())
                return std::unexpected(actions.error());
        }
    }
    if (rest.left() == 0 || rest.carve(1).value().front() != 0xCC)
        return std::unexpected(std::string("slc replay has bad footer"));
    return data;
}

inline std::expected<SlcData, std::string> decodeV2(
    std::span<const uint8_t> bytes) {
    Scanner rest(bytes.subspan(4));
    SlcData data;
    const auto tps = rest.real();
    if (!tps.has_value()) return std::unexpected(tps.error());
    data.m_tps =
        std::isfinite(tps.value()) && tps.value() > 0.0 ? tps.value() : 240.0;

    const auto metaWidth = rest.low(8);
    if (!metaWidth.has_value()) return std::unexpected(metaWidth.error());
    if (metaWidth.value() == 64) {
        const auto seed = rest.low(8);
        if (!seed.has_value()) return std::unexpected(seed.error());
        data.m_seed = seed.value();
        const auto unused = rest.carve(56);
        if (!unused.has_value()) return std::unexpected(unused.error());
    } else if (metaWidth.value() != 0) {
        return std::unexpected(std::string("slc replay has bad meta size"));
    }

    const auto declared = rest.low(8);
    if (!declared.has_value()) return std::unexpected(declared.error());
    const auto blobCount = rest.low(8);
    if (!blobCount.has_value()) return std::unexpected(blobCount.error());

    struct Run {
        uint64_t m_width;
        uint64_t m_start;
        uint64_t m_count;
    };
    std::vector<Run> runs;
    runs.reserve(blobCount.value());
    for (uint64_t i = 0; i < blobCount.value(); ++i) {
        const auto width = rest.low(8);
        if (!width.has_value()) return std::unexpected(width.error());
        const auto start = rest.low(8);
        if (!start.has_value()) return std::unexpected(start.error());
        const auto count = rest.low(8);
        if (!count.has_value()) return std::unexpected(count.error());
        const uint64_t stored = width.value();
        if ((stored != 1 && stored != 2 && stored != 4 && stored != 8) ||
            start.value() > declared.value() ||
            count.value() > declared.value() - start.value())
            return std::unexpected(std::string("slc replay has bad blob"));
        runs.push_back({stored, start.value(), count.value()});
    }

    uint64_t frame = 0;
    for (const Run& run : runs) {
        if (run.m_count > rest.left() / run.m_width)
            return std::unexpected(
                std::string("slc blob exceeds remaining data"));
        for (uint64_t i = 0; i < run.m_count; ++i) {
            const auto state = rest.low(run.m_width);
            if (!state.has_value()) return std::unexpected(state.error());
            const uint64_t delta = state.value() >> 5;
            const uint8_t button =
                static_cast<uint8_t>((state.value() >> 2) & 0x7);
            const bool player2 = (state.value() & 2) != 0;
            const bool holding = (state.value() & 1) != 0;
            frame += delta;
            if (button == 0) continue;
            if (button < 4) {
                const EventKind kind =
                    button == 2 ? EventKind::Left
                                : (button == 3 ? EventKind::Right
                                               : EventKind::Jump);
                data.m_events.emplace_back(frame, kind, holding, player2);
            } else if (button < 7) {
                const EventKind kind =
                    button == 4 ? EventKind::Restart
                                : (button == 5 ? EventKind::RestartFull
                                               : EventKind::Death);
                data.m_events.emplace_back(frame, kind, data.m_seed);
            } else {
                const auto change = rest.real();
                if (!change.has_value())
                    return std::unexpected(change.error());
                if (!std::isfinite(change.value()) || change.value() <= 0.0)
                    return std::unexpected(
                        std::string("slc replay has invalid tps"));
                data.m_events.emplace_back(frame, change.value());
            }
        }
    }

    if (rest.left() != 3)
        return std::unexpected(std::string("slc replay has bad footer"));
    const auto tail = rest.carve(3).value();
    if (tail[0] != 'E' || tail[1] != 'O' || tail[2] != 'M')
        return std::unexpected(std::string("slc replay has bad footer"));
    return data;
}

}

inline std::expected<SlcData, std::string> decodeSlc(
    std::span<const uint8_t> bytes) {
    constexpr std::array<uint8_t, 8> V3_MARK = {'S', 'L', 'C', '3', 'R',
                                                'P', 'L', 'Y'};
    if (bytes.size() >= 8 &&
        std::equal(V3_MARK.begin(), V3_MARK.end(), bytes.begin()))
        return slcraw::decodeV3(bytes);
    if (bytes.size() >= 4 && bytes[0] == 'S' && bytes[1] == 'I' &&
        bytes[2] == 'L' && bytes[3] == 'L')
        return slcraw::decodeV2(bytes);
    return std::unexpected(std::string("not an slc replay"));
}

}

#endif
