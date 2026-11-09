#include "vine/slc.hpp"

#include <bit>
#include <cstdio>
#include <cstring>

using namespace grape::vine;

static int g_checks = 0;

#define CHECK(condition)                                       \
    do {                                                       \
        ++g_checks;                                            \
        if (!(condition)) {                                    \
            std::printf("FAILED at line %d: %s\n", __LINE__,   \
                        #condition);                           \
            return 1;                                          \
        }                                                      \
    } while (false)

static void put64(std::vector<uint8_t>& out, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

static void put32(std::vector<uint8_t>& out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

static void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

static void putReal(std::vector<uint8_t>& out, double value) {
    const uint64_t raw = std::bit_cast<uint64_t>(value);
    put64(out, raw);
}

static void putState(std::vector<uint8_t>& out, uint64_t width,
                     uint64_t delta, uint8_t button, bool player2,
                     bool holding) {
    const uint64_t state =
        (delta << 4) | (static_cast<uint64_t>(button) << 2) |
        (static_cast<uint64_t>(player2) << 1) | static_cast<uint64_t>(holding);
    for (uint64_t i = 0; i < width; ++i)
        out.push_back(static_cast<uint8_t>(state >> (i * 8)));
}

static void putV2State(std::vector<uint8_t>& out, uint64_t width,
                       uint64_t delta, uint8_t button, bool player2,
                       bool holding) {
    const uint64_t state =
        (delta << 5) | (static_cast<uint64_t>(button) << 2) |
        (static_cast<uint64_t>(player2) << 1) | static_cast<uint64_t>(holding);
    for (uint64_t i = 0; i < width; ++i)
        out.push_back(static_cast<uint8_t>(state >> (i * 8)));
}

static void putV3Input(std::vector<uint8_t>& out, uint16_t deltaExp,
                       uint16_t countExp) {
    put16(out, static_cast<uint16_t>((deltaExp << 12) | (countExp << 8)));
}

static void putV3Repeat(std::vector<uint8_t>& out, uint16_t deltaExp,
                        uint16_t countExp, uint16_t repeatsExp) {
    put16(out, static_cast<uint16_t>((1u << 14) | (deltaExp << 12) |
                                     (countExp << 8) | (repeatsExp << 3)));
}

static void putV3Special(std::vector<uint8_t>& out, uint8_t type,
                         uint16_t deltaExp, uint64_t delta) {
    put16(out, static_cast<uint16_t>((2u << 14) |
                                     (static_cast<uint16_t>(type) << 10) |
                                     (deltaExp << 8)));
    for (uint64_t i = 0; i < (1ull << deltaExp); ++i)
        out.push_back(static_cast<uint8_t>(delta >> (i * 8)));
}

static std::vector<uint8_t> wrapV3(std::vector<uint8_t> body, uint32_t id) {
    std::vector<uint8_t> out;
    put32(out, id);
    put64(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

static std::vector<uint8_t> makeV3(std::vector<uint8_t> atoms, double tps,
                                   uint64_t seed) {
    std::vector<uint8_t> out = {'S', 'L', 'C', '3', 'R', 'P', 'L', 'Y'};
    put16(out, 64);
    putReal(out, tps);
    put64(out, seed);
    put32(out, 2);
    put32(out, 81);
    put32(out, 0);
    out.insert(out.end(), 36, 0);
    out.insert(out.end(), atoms.begin(), atoms.end());
    out.push_back(0xCC);
    return out;
}

static std::vector<uint8_t> makeV2(
    double tps, uint64_t seed, bool withMeta,
    const std::vector<std::vector<uint8_t>>& payloads,
    const std::vector<std::array<uint64_t, 3>>& blobs, uint64_t inputCount) {
    std::vector<uint8_t> out = {'S', 'I', 'L', 'L'};
    putReal(out, tps);
    put64(out, withMeta ? 64 : 0);
    if (withMeta) {
        put64(out, seed);
        out.insert(out.end(), 56, 0);
    }
    put64(out, inputCount);
    put64(out, blobs.size());
    for (const auto& blob : blobs) {
        put64(out, blob[0]);
        put64(out, blob[1]);
        put64(out, blob[2]);
    }
    for (const auto& payload : payloads) {
        out.insert(out.end(), payload.begin(), payload.end());
    }
    out.push_back('E');
    out.push_back('O');
    out.push_back('M');
    return out;
}

static bool samePlayer(const Event& event, uint64_t frame, EventKind kind,
                       bool holding, bool player2, bool swift) {
    return event.m_frame == frame && event.m_type == kind &&
           event.m_holding == holding && event.m_player2 == player2 &&
           event.m_swift == swift && event.m_seed == 0;
}

static int testV3Inputs() {
    std::vector<uint8_t> body;
    put64(body, 4);
    putV3Input(body, 0, 2);
    putState(body, 1, 3, 1, false, true);
    putState(body, 1, 0, 2, false, true);
    putState(body, 1, 7, 3, true, false);
    putState(body, 1, 1, 1, false, false);

    const auto file = makeV3(wrapV3(body, 1), 480.0, 0xBEEFULL);
    const auto data = decodeSlc(file);
    CHECK(data.has_value());
    CHECK(data->m_seed == 0xBEEFULL);
    CHECK(data->m_tps == 480.0);
    CHECK(data->m_events.size() == 4);
    CHECK(samePlayer(data->m_events[0], 3, EventKind::Jump, true, false,
                     false));
    CHECK(samePlayer(data->m_events[1], 3, EventKind::Left, true, false,
                     false));
    CHECK(samePlayer(data->m_events[2], 10, EventKind::Right, false, true,
                     false));
    CHECK(samePlayer(data->m_events[3], 11, EventKind::Jump, false, false,
                     false));
    return 0;
}

static int testV3WideDelta() {
    std::vector<uint8_t> body;
    put64(body, 2);
    putV3Input(body, 2, 1);
    putState(body, 4, 70000, 1, false, true);
    putState(body, 4, 5, 3, false, false);

    const auto file = makeV3(wrapV3(body, 1), 240.0, 1);
    const auto data = decodeSlc(file);
    CHECK(data.has_value());
    CHECK(data->m_events.size() == 2);
    CHECK(samePlayer(data->m_events[0], 70000, EventKind::Jump, true, false,
                     false));
    CHECK(samePlayer(data->m_events[1], 70005, EventKind::Right, false, false,
                     false));
    return 0;
}

static int testV3Swift() {
    std::vector<uint8_t> body;
    put64(body, 2);
    putV3Input(body, 0, 0);
    putState(body, 1, 9, 0, true, true);

    const auto file = makeV3(wrapV3(body, 1), 240.0, 0);
    const auto data = decodeSlc(file);
    CHECK(data.has_value());
    CHECK(data->m_events.size() == 2);
    CHECK(samePlayer(data->m_events[0], 9, EventKind::Jump, true, true, true));
    CHECK(samePlayer(data->m_events[1], 9, EventKind::Jump, false, true,
                     true));
    return 0;
}

static int testV3Repeat() {
    std::vector<uint8_t> body;
    put64(body, 4);
    putV3Repeat(body, 0, 1, 1);
    putState(body, 1, 2, 1, false, true);
    putState(body, 1, 1, 3, false, false);

    const auto file = makeV3(wrapV3(body, 1), 240.0, 0);
    const auto data = decodeSlc(file);
    CHECK(data.has_value());
    CHECK(data->m_events.size() == 4);
    CHECK(samePlayer(data->m_events[0], 2, EventKind::Jump, true, false,
                     false));
    CHECK(samePlayer(data->m_events[1], 3, EventKind::Right, false, false,
                     false));
    CHECK(samePlayer(data->m_events[2], 5, EventKind::Jump, true, false,
                     false));
    CHECK(samePlayer(data->m_events[3], 6, EventKind::Right, false, false,
                     false));
    return 0;
}

static int testV3Specials() {
    std::vector<uint8_t> body;
    put64(body, 8);
    putV3Special(body, 0, 0, 4);
    put64(body, 111);
    putV3Special(body, 2, 1, 300);
    put64(body, 222);
    putV3Special(body, 4, 0, 2);
    putV3Special(body, 3, 0, 5);
    putReal(body, 90.0);
    putV3Special(body, 1, 0, 3);
    put64(body, 333);

    const auto file = makeV3(wrapV3(body, 1), 240.0, 7);
    const auto data = decodeSlc(file);
    CHECK(data.has_value());
    CHECK(data->m_events.size() == 8);
    CHECK(data->m_events[0].m_type == EventKind::Restart);
    CHECK(data->m_events[0].m_frame == 4);
    CHECK(data->m_events[0].m_seed == 111);
    CHECK(data->m_events[1].m_type == EventKind::Bugpoint);
    CHECK(data->m_events[1].m_frame == 4);
    CHECK(data->m_events[2].m_type == EventKind::Death);
    CHECK(data->m_events[2].m_frame == 304);
    CHECK(data->m_events[2].m_seed == 222);
    CHECK(data->m_events[3].m_type == EventKind::Bugpoint);
    CHECK(data->m_events[3].m_frame == 304);
    CHECK(data->m_events[4].m_type == EventKind::Bugpoint);
    CHECK(data->m_events[4].m_frame == 306);
    CHECK(data->m_events[5].m_type == EventKind::Tps);
    CHECK(data->m_events[5].m_frame == 311);
    CHECK(data->m_events[5].m_tps == 90.0);
    CHECK(data->m_events[6].m_type == EventKind::RestartFull);
    CHECK(data->m_events[6].m_frame == 314);
    CHECK(data->m_events[6].m_seed == 333);
    CHECK(data->m_events[7].m_type == EventKind::Bugpoint);
    CHECK(data->m_events[7].m_frame == 314);
    return 0;
}

static int testV3ForeignAtom() {
    std::vector<uint8_t> marker;
    marker.insert(marker.end(), {'m', 'a', 'r', 'k'});

    std::vector<uint8_t> body;
    put64(body, 1);
    putV3Input(body, 0, 0);
    putState(body, 1, 4, 1, false, true);

    std::vector<uint8_t> atoms;
    const auto wrappedMarker = wrapV3(marker, 2);
    const auto wrappedActions = wrapV3(body, 1);
    atoms.insert(atoms.end(), wrappedMarker.begin(), wrappedMarker.end());
    atoms.insert(atoms.end(), wrappedActions.begin(), wrappedActions.end());

    const auto file = makeV3(atoms, 240.0, 0);
    const auto data = decodeSlc(file);
    CHECK(data.has_value());
    CHECK(data->m_events.size() == 1);
    CHECK(samePlayer(data->m_events[0], 4, EventKind::Jump, true, false,
                     false));
    return 0;
}

static int testV3MetaFallback() {
    std::vector<uint8_t> body;
    put64(body, 1);
    putV3Input(body, 0, 0);
    putState(body, 1, 1, 1, false, true);

    const auto nanFile = makeV3(wrapV3(body, 1),
                                std::bit_cast<uint64_t>(0.0 / 0.0) * 0.0, 5);
    const auto nanData = decodeSlc(nanFile);
    CHECK(nanData.has_value());
    CHECK(nanData->m_tps == 240.0);
    CHECK(nanData->m_seed == 5);

    const auto zeroFile = makeV3(wrapV3(body, 1), 0.0, 6);
    const auto zeroData = decodeSlc(zeroFile);
    CHECK(zeroData.has_value());
    CHECK(zeroData->m_tps == 240.0);
    return 0;
}

static int testV3Corruption() {
    std::vector<uint8_t> body;
    put64(body, 2);
    putV3Input(body, 0, 1);
    putState(body, 1, 3, 1, false, true);
    putState(body, 1, 1, 2, false, false);
    const auto file = makeV3(wrapV3(body, 1), 240.0, 0);
    CHECK(decodeSlc(file).has_value());

    for (size_t cut = 1; cut < file.size(); ++cut) {
        const auto truncated = decodeSlc(
            std::span<const uint8_t>(file.data(), file.size() - cut));
        CHECK(!truncated.has_value());
    }

    std::vector<uint8_t> badMagic = file;
    badMagic[0] = 'X';
    CHECK(!decodeSlc(badMagic).has_value());

    std::vector<uint8_t> badFooter = file;
    badFooter.back() = 0xCD;
    CHECK(!decodeSlc(badFooter).has_value());

    std::vector<uint8_t> trailing = file;
    trailing.insert(trailing.end() - 1, 0);
    CHECK(!decodeSlc(trailing).has_value());

    std::vector<uint8_t> shortMeta = file;
    shortMeta[8] = 63;
    shortMeta[9] = 0;
    CHECK(!decodeSlc(shortMeta).has_value());

    std::vector<uint8_t> bigCount = file;
    bigCount[86] = 0xFF;
    CHECK(!decodeSlc(bigCount).has_value());

    std::vector<uint8_t> hugeAtom = file;
    hugeAtom[78] = 0xFF;
    CHECK(!decodeSlc(hugeAtom).has_value());

    return 0;
}

static int testV3UnknownSectionId() {
    std::vector<uint8_t> body;
    put64(body, 1);
    put16(body, static_cast<uint16_t>(3u << 14));

    const auto file = makeV3(wrapV3(body, 1), 240.0, 0);
    CHECK(!decodeSlc(file).has_value());
    return 0;
}

static int testV3TpsValidation() {
    std::vector<uint8_t> body;
    put64(body, 1);
    putV3Special(body, 3, 0, 4);
    putReal(body, -1.0);

    const auto file = makeV3(wrapV3(body, 1), 240.0, 0);
    CHECK(!decodeSlc(file).has_value());

    std::vector<uint8_t> nanBody;
    put64(nanBody, 1);
    putV3Special(nanBody, 3, 0, 4);
    putReal(nanBody, std::bit_cast<double>(0x7FF8000000000000ull));
    const auto nanFile = makeV3(wrapV3(nanBody, 1), 240.0, 0);
    CHECK(!decodeSlc(nanFile).has_value());
    return 0;
}

static int testV2MixedBlobs() {
    std::vector<std::vector<uint8_t>> payloads;
    std::vector<uint8_t> one;
    putV2State(one, 1, 2, 1, false, true);
    putV2State(one, 1, 3, 3, true, false);
    payloads.push_back(one);

    std::vector<uint8_t> two;
    putV2State(two, 2, 40, 2, false, true);
    payloads.push_back(two);

    std::vector<uint8_t> four;
    putV2State(four, 4, 1000, 1, false, false);
    payloads.push_back(four);

    std::vector<uint8_t> eight;
    putV2State(eight, 8, 70000, 1, false, true);
    payloads.push_back(eight);

    const std::vector<std::array<uint64_t, 3>> blobs = {
        {{1, 0, 2}}, {{2, 2, 1}}, {{4, 3, 1}}, {{8, 4, 1}}};
    const auto file = makeV2(240.0, 0xCAFE, true, payloads, blobs, 5);
    const auto data = decodeSlc(file);
    CHECK(data.has_value());
    CHECK(data->m_seed == 0xCAFE);
    CHECK(data->m_tps == 240.0);
    CHECK(data->m_events.size() == 5);
    CHECK(samePlayer(data->m_events[0], 2, EventKind::Jump, true, false,
                     false));
    CHECK(samePlayer(data->m_events[1], 5, EventKind::Right, false, true,
                     false));
    CHECK(samePlayer(data->m_events[2], 45, EventKind::Left, true, false,
                     false));
    CHECK(samePlayer(data->m_events[3], 1045, EventKind::Jump, false, false,
                     false));
    CHECK(samePlayer(data->m_events[4], 71045, EventKind::Jump, true, false,
                     false));
    return 0;
}

static int testV2SpecialInputs() {
    std::vector<uint8_t> payload;
    putV2State(payload, 1, 1, 4, false, false);
    putV2State(payload, 1, 3, 6, false, false);
    putV2State(payload, 1, 2, 7, false, false);
    putReal(payload, 360.0);
    putV2State(payload, 1, 5, 0, false, false);
    putV2State(payload, 1, 4, 1, false, true);

    const std::vector<std::array<uint64_t, 3>> blobs = {{{1, 0, 5}}};
    const auto file = makeV2(240.0, 0x1234, true, {payload}, blobs, 5);
    const auto data = decodeSlc(file);
    CHECK(data.has_value());
    CHECK(data->m_events.size() == 4);
    CHECK(data->m_events[0].m_type == EventKind::Restart);
    CHECK(data->m_events[0].m_frame == 1);
    CHECK(data->m_events[0].m_seed == 0x1234);
    CHECK(data->m_events[1].m_type == EventKind::Death);
    CHECK(data->m_events[1].m_frame == 4);
    CHECK(data->m_events[1].m_seed == 0x1234);
    CHECK(data->m_events[2].m_type == EventKind::Tps);
    CHECK(data->m_events[2].m_frame == 6);
    CHECK(data->m_events[2].m_tps == 360.0);
    CHECK(samePlayer(data->m_events[3], 15, EventKind::Jump, true, false,
                     false));
    return 0;
}

static int testV2NoMeta() {
    std::vector<uint8_t> payload;
    putV2State(payload, 1, 5, 1, false, true);

    const std::vector<std::array<uint64_t, 3>> blobs = {{{1, 0, 1}}};
    const auto file = makeV2(300.0, 0, false, {payload}, blobs, 1);
    const auto data = decodeSlc(file);
    CHECK(data.has_value());
    CHECK(data->m_seed == 0);
    CHECK(data->m_tps == 300.0);
    CHECK(data->m_events.size() == 1);
    CHECK(samePlayer(data->m_events[0], 5, EventKind::Jump, true, false,
                     false));
    return 0;
}

static int testV2Corruption() {
    std::vector<uint8_t> payload;
    putV2State(payload, 1, 3, 1, false, true);
    const std::vector<std::array<uint64_t, 3>> blobs = {{{1, 0, 1}}};
    const auto file = makeV2(240.0, 1, true, {payload}, blobs, 1);
    CHECK(decodeSlc(file).has_value());

    for (size_t cut = 1; cut < file.size(); ++cut) {
        const auto truncated = decodeSlc(
            std::span<const uint8_t>(file.data(), file.size() - cut));
        CHECK(!truncated.has_value());
    }

    std::vector<uint8_t> badFooter = file;
    badFooter[badFooter.size() - 1] = 'X';
    CHECK(!decodeSlc(badFooter).has_value());

    std::vector<uint8_t> badMagic = file;
    badMagic[1] = 'A';
    CHECK(!decodeSlc(badMagic).has_value());

    std::vector<uint8_t> wrongMeta = file;
    wrongMeta[12] = 63;
    CHECK(!decodeSlc(wrongMeta).has_value());

    std::vector<uint8_t> badWidth = file;
    const size_t widthOffset = 4 + 8 + 8 + 64 + 8 + 8;
    badWidth[widthOffset] = 3;
    CHECK(!decodeSlc(badWidth).has_value());

    std::vector<uint8_t> overrun = file;
    const size_t lengthOffset = widthOffset + 16;
    overrun[lengthOffset] = 9;
    CHECK(!decodeSlc(overrun).has_value());

    std::vector<uint8_t> hugeBlob = file;
    hugeBlob[108] = 0xFF;
    CHECK(!decodeSlc(hugeBlob).has_value());
    return 0;
}

static int testUnknownBytes() {
    const std::vector<uint8_t> junk = {1, 2, 3, 4, 5};
    CHECK(!decodeSlc(junk).has_value());
    CHECK(!decodeSlc(std::span<const uint8_t>()).has_value());
    return 0;
}

int main() {
    if (testV3Inputs()) return 1;
    if (testV3WideDelta()) return 1;
    if (testV3Swift()) return 1;
    if (testV3Repeat()) return 1;
    if (testV3Specials()) return 1;
    if (testV3ForeignAtom()) return 1;
    if (testV3MetaFallback()) return 1;
    if (testV3Corruption()) return 1;
    if (testV3TpsValidation()) return 1;
    if (testV3UnknownSectionId()) return 1;
    if (testV2MixedBlobs()) return 1;
    if (testV2SpecialInputs()) return 1;
    if (testV2NoMeta()) return 1;
    if (testV2Corruption()) return 1;
    if (testUnknownBytes()) return 1;
    std::printf("vine_slc: %d checks passed\n", g_checks);
    return 0;
}
