#include "vine/format.hpp"

#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

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

static bool sameEvent(const Event& a, const Event& b) {
    return a.m_frame == b.m_frame && a.m_type == b.m_type &&
           a.m_holding == b.m_holding && a.m_player2 == b.m_player2 &&
           a.m_swift == b.m_swift && a.m_seed == b.m_seed &&
           a.m_tps == b.m_tps;
}

static Event mk(uint64_t frame, EventKind type, bool holding, bool player2,
                bool swift, uint64_t seed, double tps = 240.0) {
    Event event;
    event.m_frame = frame;
    event.m_type = type;
    event.m_holding = holding;
    event.m_player2 = player2;
    event.m_swift = swift;
    event.m_seed = seed;
    event.m_tps = tps;
    return event;
}

static Inputs sampleInputs() {
    Inputs inputs;
    inputs.m_seed = 0xdeadbeefcafe1234ULL;
    inputs.m_tps = 480.0;

    inputs.m_events = {
        mk(10, EventKind::Jump, true, false, false, 0),
        mk(10, EventKind::Left, true, false, true, 0),
        mk(42, EventKind::Right, false, true, false, 0),
        mk(1000, EventKind::Death, false, false, false, 987654321ULL),
        mk(1000, EventKind::Restart, false, false, false, 0),
        mk(123456789, EventKind::RestartFull, false, false, false,
           0xffffffffffffffffULL),
        mk(123456790, EventKind::Tps, false, false, false, 0, 1e12),
        mk(123456791, EventKind::Bugpoint, false, false, false, 0),
        mk(123456791, EventKind::Jump, false, true, false, 0),
    };
    return inputs;
}

static int testRoundTrip() {
    const auto bytes = encode(sampleInputs());
    CHECK(bytes.has_value());

    auto decoded = decode(*bytes);
    CHECK(decoded.has_value());
    CHECK(decoded->m_seed == 0xdeadbeefcafe1234ULL);
    CHECK(decoded->m_tps == 480.0);
    CHECK(decoded->length() == 9);
    for (size_t i = 0; i < 9; ++i)
        CHECK(sameEvent(decoded->m_events[i], sampleInputs().m_events[i]));

    Inputs empty;
    empty.m_seed = 1;
    auto emptyBytes = encode(empty);
    CHECK(emptyBytes.has_value());
    auto emptyDecoded = decode(*emptyBytes);
    CHECK(emptyDecoded.has_value());
    CHECK(emptyDecoded->m_events.empty());
    CHECK(emptyDecoded->m_seed == 1);

    Inputs one;
    one.m_tps = 1e9;
    one.m_events = {mk(0, EventKind::Jump, true, false, true, 0)};
    auto oneBytes = encode(one);
    CHECK(oneBytes.has_value());
    auto oneDecoded = decode(*oneBytes);
    CHECK(oneDecoded.has_value());
    CHECK(sameEvent(oneDecoded->m_events[0], one.m_events[0]));
    return 0;
}

static int testCorruption() {
    const auto bytes = encode(sampleInputs());
    CHECK(bytes.has_value());
    const auto ok = decode(*bytes);
    CHECK(ok.has_value());

    for (size_t cut = 0; cut < bytes->size(); ++cut) {
        const auto truncated = decode(std::span<const uint8_t>(
            bytes->data(), bytes->size() - cut));
        if (cut == 0) {
            CHECK(truncated.has_value());
        } else {
            CHECK(!truncated.has_value());
        }
    }

    for (size_t extra = 1; extra <= 4; ++extra) {
        std::vector<uint8_t> padded = *bytes;
        padded.insert(padded.end(), extra, 0);
        CHECK(!decode(padded).has_value());
    }

    std::vector<uint8_t> badMagic = *bytes;
    badMagic[0] = 'X';
    CHECK(!decode(badMagic).has_value());

    std::vector<uint8_t> badVersion = *bytes;
    badVersion[4] = FILE_VERSION + 1;
    CHECK(!decode(badVersion).has_value());

    std::vector<uint8_t> badSections = *bytes;
    badSections[6] = 4;
    CHECK(!decode(badSections).has_value());

    for (size_t i = 8; i < bytes->size(); ++i) {
        std::vector<uint8_t> flipped = *bytes;
        flipped[i] ^= 0x01;
        const auto result = decode(flipped);
        CHECK(!result.has_value());
    }

    CHECK(!decode(std::span<const uint8_t>()).has_value());
    CHECK(!decode(std::span<const uint8_t>(bytes->data(), 4)).has_value());

    std::vector<uint8_t> empty;
    CHECK(!decode(empty).has_value());

    std::vector<uint8_t> tiny = {'V', 'I', 'N', 'E', 1, 0, 3, 0};
    CHECK(!decode(tiny).has_value());
    return 0;
}

static int testSortedRequirement() {
    Inputs inputs;
    inputs.m_events = {mk(10, EventKind::Jump, true, false, false, 0),
                     mk(5, EventKind::Jump, true, false, false, 0)};
    CHECK(!encode(inputs).has_value());

    inputs.sort();
    CHECK(encode(inputs).has_value());
    return 0;
}

static void patchU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

static bool repairSectionCrc(std::vector<uint8_t>& bytes, size_t target) {
    size_t pos = 8;
    while (pos < bytes.size()) {
        const uint32_t length = readU32(bytes, pos + 1);
        const size_t bodyEnd = pos + 5 + length;
        if (target < bodyEnd && target >= pos) {
            patchU32(bytes, bodyEnd,
                     crc32c(std::span<const uint8_t>(bytes.data() + pos,
                                                    5 + length)));
            return true;
        }
        pos = bodyEnd + 4;
    }
    return false;
}

static size_t payloadOffset(const std::vector<uint8_t>& file, int index) {
    size_t pos = 8;
    for (int i = 0; i < index; ++i) pos += 5 + readU32(file, pos + 1) + 4;
    return pos + 5;
}

static std::vector<size_t> recordTagOffsets(const std::vector<uint8_t>& file) {
    std::vector<size_t> offsets;
    const size_t base = payloadOffset(file, 2);
    const size_t end = base + readU32(file, base - 4);
    size_t pos = base;
    while (pos < end) {
        offsets.push_back(pos);
        const uint8_t tag = file[pos];
        ++pos;
        if (tag & TAG_SEED) {
            while (pos < end && (file[pos] & 0x80)) ++pos;
            ++pos;
        }
        if (tag & TAG_TPS) pos += 8;
    }
    return offsets;
}

static bool patchMetaTps(std::vector<uint8_t>& file, double value) {
    size_t pos = payloadOffset(file, 0);
    while (file[pos] & 0x80) ++pos;
    ++pos;
    const uint64_t raw = std::bit_cast<uint64_t>(value);
    for (unsigned i = 0; i < 8; ++i)
        file[pos + i] = static_cast<uint8_t>(raw >> (i * 8));
    return repairSectionCrc(file, pos);
}

static std::vector<uint8_t> craftFile(const std::vector<uint8_t>& meta,
                                      const std::vector<uint8_t>& frames,
                                      const std::vector<uint8_t>& records) {
    std::vector<uint8_t> out;
    out.insert(out.end(), FILE_MAGIC.begin(), FILE_MAGIC.end());
    writeU16(out, FILE_VERSION_PLAIN);
    writeU16(out, SECTION_TOTAL);
    appendSection(out, SECTION_META, meta);
    appendSection(out, SECTION_FRAMES, frames);
    appendSection(out, SECTION_RECORDS, records);
    return out;
}

static std::vector<uint8_t> craftLegacyMeta(uint64_t seed, double tps,
                                            uint64_t count,
                                            uint64_t lastFrame) {
    std::vector<uint8_t> meta;
    writeVarint(meta, seed);
    writeU64(meta, std::bit_cast<uint64_t>(tps));
    writeVarint(meta, count);
    writeVarint(meta, lastFrame);
    return meta;
}

static std::vector<uint8_t> craftMeta(uint64_t seed, double tps, uint64_t count,
                                      uint64_t lastFrame, uint8_t flags = 0) {
    std::vector<uint8_t> meta = craftLegacyMeta(seed, tps, count, lastFrame);
    meta.push_back(flags);
    return meta;
}

static std::vector<uint8_t> craftLegacyFile(const std::vector<uint8_t>& meta,
                                            const std::vector<uint8_t>& frames,
                                            const std::vector<uint8_t>& records) {
    std::vector<uint8_t> out;
    out.insert(out.end(), FILE_MAGIC.begin(), FILE_MAGIC.end());
    writeU16(out, FILE_VERSION_LEGACY);
    writeU16(out, SECTION_TOTAL);
    appendSection(out, SECTION_META, meta);
    appendSection(out, SECTION_FRAMES, frames);
    appendSection(out, SECTION_RECORDS, records);
    return out;
}

static std::vector<uint8_t> craftFrames(
    const std::vector<uint64_t>& frameDeltas) {
    std::vector<uint8_t> frames;
    for (const uint64_t delta : frameDeltas) writeVarint(frames, delta);
    return frames;
}

static std::vector<uint8_t> craftRecords(
    const std::vector<uint8_t>& tags) {
    std::vector<uint8_t> records;
    for (const uint8_t tag : tags) records.push_back(tag);
    return records;
}

static bool appendToPayload(std::vector<uint8_t>& file, int section,
                            uint8_t value) {
    size_t pos = 8;
    for (int i = 0; i < section; ++i) pos += 5 + readU32(file, pos + 1) + 4;
    const uint32_t length = readU32(file, pos + 1);
    file.insert(file.begin() + pos + 5 + length, value);
    patchU32(file, pos + 1, length + 1);
    return repairSectionCrc(file, pos + 5);
}

static int testSemanticCorruption() {
    const auto bytes = encode(sampleInputs());
    CHECK(bytes.has_value());

    size_t metaPos = 13;
    while ((*bytes)[metaPos] & 0x80) ++metaPos;
    ++metaPos;
    metaPos += 8;
    const size_t countByte = metaPos;
    CHECK((*bytes)[countByte] == sampleInputs().m_events.size());

    std::vector<uint8_t> wrongCount = *bytes;
    wrongCount[countByte] += 1;
    CHECK(repairSectionCrc(wrongCount, countByte));
    CHECK(!decode(wrongCount).has_value());

    std::vector<uint8_t> zeroCount = *bytes;
    zeroCount[countByte] = 0;
    CHECK(repairSectionCrc(zeroCount, countByte));
    CHECK(!decode(zeroCount).has_value());

    const size_t frameByte = countByte + 1;
    std::vector<uint8_t> wrongLastFrame = *bytes;
    wrongLastFrame[frameByte] ^= 0x01;
    CHECK(repairSectionCrc(wrongLastFrame, frameByte));
    CHECK(!decode(wrongLastFrame).has_value());
    return 0;
}

static std::vector<uint8_t> encodePlain(const Inputs& inputs) {
    std::vector<uint8_t> meta;
    writeVarint(meta, inputs.m_seed);
    writeU64(meta, std::bit_cast<uint64_t>(inputs.m_tps));
    writeVarint(meta, inputs.m_events.size());
    writeVarint(meta, inputs.lastFrame());
    meta.push_back(inputs.m_platformer ? META_PLATFORMER : uint8_t{0});

    std::vector<uint8_t> frames;
    uint64_t previous = 0;
    for (const auto& event : inputs.m_events) {
        writeVarint(frames, event.m_frame - previous);
        previous = event.m_frame;
    }

    std::vector<uint8_t> records;
    for (const auto& event : inputs.m_events) {
        const uint8_t code = wireCode(event.m_type);
        uint8_t tag = code;
        if (event.m_holding) tag |= TAG_HOLDING;
        if (event.m_player2) tag |= TAG_PLAYER2;
        if (event.m_swift) tag |= TAG_SWIFT;
        if (wireCarriesSeed(code)) tag |= TAG_SEED;
        if (wireCarriesTps(code)) tag |= TAG_TPS;
        records.push_back(tag);
        if (wireCarriesSeed(code)) writeVarint(records, event.m_seed);
        if (wireCarriesTps(code))
            writeU64(records, std::bit_cast<uint64_t>(event.m_tps));
    }

    std::vector<uint8_t> out;
    out.insert(out.end(), FILE_MAGIC.begin(), FILE_MAGIC.end());
    writeU16(out, FILE_VERSION_PLAIN);
    writeU16(out, SECTION_TOTAL);
    appendSection(out, SECTION_META, meta);
    appendSection(out, SECTION_FRAMES, frames);
    appendSection(out, SECTION_RECORDS, records);
    return out;
}

static int testTagFlagCorruption() {
    const auto plain = encodePlain(sampleInputs());
    const std::vector<uint8_t>* bytes = &plain;
    CHECK(decode(plain).has_value());
    const auto tags = recordTagOffsets(*bytes);
    CHECK(tags.size() == 9);

    std::vector<uint8_t> seedOnJump = *bytes;
    seedOnJump[tags[0]] |= TAG_SEED;
    CHECK(repairSectionCrc(seedOnJump, tags[0]));
    auto result = decode(seedOnJump);
    CHECK(!result.has_value());
    CHECK(result.error().find("seed flag mismatch") != std::string::npos);

    std::vector<uint8_t> seedMissing = *bytes;
    seedMissing[tags[4]] &= ~TAG_SEED;
    CHECK(repairSectionCrc(seedMissing, tags[4]));
    result = decode(seedMissing);
    CHECK(!result.has_value());
    CHECK(result.error().find("seed flag mismatch") != std::string::npos);

    std::vector<uint8_t> tpsOnJump = *bytes;
    tpsOnJump[tags[0]] |= TAG_TPS;
    CHECK(repairSectionCrc(tpsOnJump, tags[0]));
    result = decode(tpsOnJump);
    CHECK(!result.has_value());
    CHECK(result.error().find("tps flag mismatch") != std::string::npos);

    const size_t tpsValue = tags[6] + 1;
    for (const double bad : {0.0, -1.0,
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        std::vector<uint8_t> badTpsEvent = *bytes;
        const uint64_t raw = std::bit_cast<uint64_t>(bad);
        for (unsigned i = 0; i < 8; ++i)
            badTpsEvent[tpsValue + i] = static_cast<uint8_t>(raw >> (i * 8));
        CHECK(repairSectionCrc(badTpsEvent, tpsValue));
        result = decode(badTpsEvent);
        CHECK(!result.has_value());
        CHECK(result.error().find("invalid tps event") != std::string::npos);
    }
    return 0;
}

static int testMetaCorruption() {
    const auto bytes = encode(sampleInputs());
    CHECK(bytes.has_value());

    for (const double bad : {0.0, -0.5,
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
        std::vector<uint8_t> file = *bytes;
        CHECK(patchMetaTps(file, bad));
        const auto result = decode(file);
        CHECK(!result.has_value());
        CHECK(result.error().find("invalid tps") != std::string::npos);
    }

    std::vector<uint8_t> metaTrailing = *bytes;
    CHECK(appendToPayload(metaTrailing, 0, 0));
    auto result = decode(metaTrailing);
    CHECK(!result.has_value());
    CHECK(result.error().find("malformed meta section") !=
          std::string::npos);

    std::vector<uint8_t> recordsTrailing = *bytes;
    CHECK(appendToPayload(recordsTrailing, 2, 0));
    result = decode(recordsTrailing);
    CHECK(!result.has_value());
    CHECK(result.error().find("malformed record section") !=
          std::string::npos);

    std::vector<uint8_t> wrongOrderId = *bytes;
    const size_t recordsHeader = payloadOffset(*bytes, 2) - 5;
    wrongOrderId[recordsHeader] = SECTION_FRAMES;
    CHECK(repairSectionCrc(wrongOrderId, recordsHeader));
    result = decode(wrongOrderId);
    CHECK(!result.has_value());
    CHECK(result.error().find("section out of order") != std::string::npos);
    return 0;
}

static int testTpsSwitching() {
    Inputs inputs;
    inputs.m_seed = 5;
    inputs.m_tps = 240.0;
    inputs.m_events.emplace_back(0, EventKind::Jump, true, false);
    inputs.m_events.emplace_back(10, 480.25);
    inputs.m_events.emplace_back(12, EventKind::Jump, false, false);
    inputs.m_events.emplace_back(40, 60.5);
    inputs.m_events.emplace_back(41, EventKind::Jump, true, false);

    CHECK(inputs.hasTpsEvents());
    CHECK(inputs.tpsAt(0) == 240.0);
    CHECK(inputs.tpsAt(9) == 240.0);
    CHECK(inputs.tpsAt(10) == 480.25);
    CHECK(inputs.tpsAt(39) == 480.25);
    CHECK(inputs.tpsAt(40) == 60.5);
    CHECK(inputs.tpsAt(1000) == 60.5);

    const auto bytes = encode(inputs);
    CHECK(bytes.has_value());
    auto decoded = decode(*bytes);
    CHECK(decoded.has_value());
    CHECK(decoded->m_tps == 240.0);
    CHECK(decoded->m_events.size() == inputs.m_events.size());
    for (size_t i = 0; i < inputs.m_events.size(); ++i)
        CHECK(sameEvent(decoded->m_events[i], inputs.m_events[i]));
    CHECK(decoded->tpsAt(10) == 480.25);
    CHECK(decoded->tpsAt(40) == 60.5);

    Inputs plain;
    plain.m_tps = 120.0;
    plain.m_events.emplace_back(3, EventKind::Jump, true, false);
    CHECK(!plain.hasTpsEvents());
    CHECK(plain.tpsAt(0) == 120.0);
    CHECK(plain.tpsAt(99999) == 120.0);

    Inputs together;
    together.m_tps = 100.0;
    together.m_events.emplace_back(5, EventKind::Jump, true, false);
    together.m_events.emplace_back(5, 200.0);
    CHECK(together.tpsAt(4) == 100.0);
    CHECK(together.tpsAt(5) == 200.0);

    return 0;
}

static int testTpsCoalescing() {
    Inputs inputs;
    inputs.m_tps = 240.0;
    inputs.m_events.emplace_back(0, EventKind::Jump, true, false);

    inputs.setTps(100, 3.0, false);
    inputs.setTps(101, 33.0, true);
    inputs.setTps(102, 333.0, true);
    CHECK(inputs.m_events.size() == 2);
    CHECK(inputs.m_events[1].m_type == EventKind::Tps);
    CHECK(inputs.m_events[1].m_tps == 333.0);
    CHECK(inputs.m_events[1].m_frame == 100);
    CHECK(inputs.tpsAt(99) == 240.0);
    CHECK(inputs.tpsAt(100) == 333.0);

    inputs.setTps(400, 120.0, false);
    CHECK(inputs.m_events.size() == 3);
    CHECK(inputs.tpsAt(399) == 333.0);
    CHECK(inputs.tpsAt(400) == 120.0);

    inputs.m_events.emplace_back(410, EventKind::Jump, false, false);
    inputs.setTps(420, 60.0, true);
    CHECK(inputs.m_events.size() == 5);
    CHECK(inputs.tpsAt(420) == 60.0);

    Inputs fresh;
    fresh.setTps(5, 480.0, true);
    CHECK(fresh.m_events.size() == 1);
    CHECK(fresh.m_events[0].m_type == EventKind::Tps);
    CHECK(fresh.m_events[0].m_tps == 480.0);

    Inputs unordered;
    unordered.m_events.emplace_back(50, EventKind::Jump, true, false);
    unordered.setTps(10, 200.0, false);
    CHECK(unordered.m_events.size() == 2);
    CHECK(unordered.m_events[0].m_frame <= unordered.m_events[1].m_frame);
    const auto encoded = encode(unordered);
    CHECK(encoded.has_value());

    return 0;
}

static int testPlatformerFlag() {
    Inputs inputs;
    inputs.m_seed = 9;
    inputs.m_tps = 240.0;
    inputs.m_platformer = true;
    inputs.m_events.emplace_back(0, EventKind::Left, true, false);
    inputs.m_events.emplace_back(7, EventKind::Right, true, true);
    inputs.m_events.emplace_back(9, EventKind::Left, false, false);

    const auto bytes = encode(inputs);
    CHECK(bytes.has_value());
    auto decoded = decode(*bytes);
    CHECK(decoded.has_value());
    CHECK(decoded->m_platformer);
    CHECK(decoded->m_events.size() == inputs.m_events.size());
    for (size_t i = 0; i < inputs.m_events.size(); ++i)
        CHECK(sameEvent(decoded->m_events[i], inputs.m_events[i]));

    inputs.m_cbf = true;
    inputs.m_cbfOnSteps = true;
    inputs.m_clickBetweenSteps = true;
    inputs.m_clickOnSteps = true;
    const auto modeBytes = encode(inputs);
    CHECK(modeBytes.has_value());
    auto modeDecoded = decode(*modeBytes);
    CHECK(modeDecoded.has_value());
    CHECK(modeDecoded->m_cbf);
    CHECK(modeDecoded->m_cbfOnSteps);
    CHECK(modeDecoded->m_clickBetweenSteps);
    CHECK(modeDecoded->m_clickOnSteps);

    inputs.m_cbf = false;
    inputs.m_cbfOnSteps = false;
    inputs.m_clickBetweenSteps = false;
    inputs.m_clickOnSteps = false;
    inputs.m_platformer = false;
    const auto classic = encode(inputs);
    CHECK(classic.has_value());
    auto decodedClassic = decode(*classic);
    CHECK(decodedClassic.has_value());
    CHECK(!decodedClassic->m_platformer);

    const auto legacy = craftLegacyFile(craftLegacyMeta(7, 240.0, 1, 15),
                                        craftFrames({15}), craftRecords({0}));
    auto decodedLegacy = decode(legacy);
    CHECK(decodedLegacy.has_value());
    CHECK(!decodedLegacy->m_platformer);
    CHECK(decodedLegacy->m_seed == 7);
    CHECK(decodedLegacy->m_events.size() == 1);

    const auto unknownFlags = craftFile(craftMeta(7, 240.0, 1, 15, 0x80),
                                        craftFrames({15}), craftRecords({0}));
    auto rejected = decode(unknownFlags);
    CHECK(!rejected.has_value());
    CHECK(rejected.error() == "unknown meta flags");

    const auto truncated = craftFile(craftLegacyMeta(7, 240.0, 1, 15),
                                     craftFrames({15}), craftRecords({0}));
    CHECK(!decode(truncated).has_value());
    return 0;
}

static int testCraftedCounts() {
    const auto valid = craftFile(craftMeta(7, 240.0, 1, 15),
                                 craftFrames({15}), craftRecords({0}));
    CHECK(decode(valid).has_value());

    const auto tooMany =
        craftFile(craftMeta(7, 240.0, 10'000'001, 15),
                  craftFrames({15}), craftRecords({0}));
    auto result = decode(tooMany);
    CHECK(!result.has_value());
    CHECK(result.error().find("too many events") != std::string::npos);

    const auto streamsTooShort =
        craftFile(craftMeta(7, 240.0, 1'000'000, 15),
                  craftFrames({15}), craftRecords({0}));
    result = decode(streamsTooShort);
    CHECK(!result.has_value());
    CHECK(result.error().find("event count exceeds section data") !=
          std::string::npos);
    return 0;
}

static int testVarintBoundaries() {
    const auto bigFrames =
        craftFile(craftMeta(7, 240.0, 2, 0xFFFFFFFFFFFFFFFFULL),
                  craftFrames({1ULL << 63, (1ULL << 63) - 1}),
                  craftRecords({0, 0}));
    auto result = decode(bigFrames);
    CHECK(result.has_value());
    CHECK(result->m_events[0].m_frame == 1ULL << 63);
    CHECK(result->m_events[1].m_frame == 0xFFFFFFFFFFFFFFFFULL);

    std::vector<uint8_t> overflowFrames;
    for (int i = 0; i < 9; ++i) overflowFrames.push_back(0xFF);
    overflowFrames.push_back(0x02);
    CHECK(overflowFrames.size() == 10);
    const auto overflowFile =
        craftFile(craftMeta(7, 240.0, 1, 15), overflowFrames,
                  craftRecords({0}));
    result = decode(overflowFile);
    CHECK(!result.has_value());
    CHECK(result.error().find("varint overflow") != std::string::npos);

    std::vector<uint8_t> elevenBytes;
    for (int i = 0; i < 11; ++i) elevenBytes.push_back(0x80);
    elevenBytes.push_back(0x00);
    const auto elevenFile =
        craftFile(craftMeta(7, 240.0, 1, 15), elevenBytes, craftRecords({0}));
    result = decode(elevenFile);
    CHECK(!result.has_value());
    CHECK(result.error().find("varint overflow") != std::string::npos);
    return 0;
}

static int testPerf() {
    Inputs inputs;
    inputs.m_tps = 240.0;
    inputs.m_events.reserve(1'000'000);
    uint64_t frame = 0;
    for (int i = 0; i < 1'000'000; ++i) {
        frame += 1 + (i % 3);
        inputs.m_events.push_back(
            mk(frame, EventKind::Jump, (i & 1) == 0, (i & 2) == 0, false, 0));
    }

    const auto start = std::chrono::steady_clock::now();
    const auto bytes = encode(inputs);
    CHECK(bytes.has_value());
    auto decoded = decode(*bytes);
    CHECK(decoded.has_value());
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count();
    CHECK(decoded->length() == 1'000'000);
    CHECK(decoded->m_events.back().m_frame == frame);
    std::printf("1M events encode+decode: %lld ms (%zu bytes)\n",
                static_cast<long long>(elapsed), bytes->size());
    CHECK(elapsed < 2000);
    return 0;
}

static Inputs realisticMacro(unsigned clicks, bool platformer) {
    Inputs inputs;
    inputs.m_tps = 240.0;
    inputs.m_platformer = platformer;
    uint64_t frame = 0;
    uint64_t state = 12345;
    const auto next = [&state](uint64_t lo, uint64_t hi) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return lo + (state >> 33) % (hi - lo + 1);
    };
    for (unsigned i = 0; i < clicks; ++i) {
        frame += next(6, 45);
        inputs.m_events.emplace_back(frame, EventKind::Jump, true, false);
        frame += next(2, 14);
        inputs.m_events.emplace_back(frame, EventKind::Jump, false, false);
        if (platformer && (i % 3) == 0) {
            frame += next(6, 45);
            inputs.m_events.emplace_back(frame, EventKind::Right, true, false);
            frame += next(8, 56);
            inputs.m_events.emplace_back(frame, EventKind::Right, false, false);
        }
    }
    inputs.sort();
    return inputs;
}

static int testPackedCompression() {
    for (const bool platformer : {false, true}) {
        const auto inputs = realisticMacro(4000, platformer);
        const auto packed = encode(inputs);
        CHECK(packed.has_value());
        const auto plain = encodePlain(inputs);

        const auto decoded = decode(*packed);
        CHECK(decoded.has_value());
        CHECK(decoded->m_events.size() == inputs.m_events.size());
        CHECK(decoded->m_platformer == platformer);
        for (size_t i = 0; i < inputs.m_events.size(); ++i)
            CHECK(sameEvent(decoded->m_events[i], inputs.m_events[i]));

        CHECK(packed->size() * 100 < plain.size() * 70);
        std::printf("  %-10s %6zu events  plain %7zu  packed %7zu  (%.0f%%)\n",
                    platformer ? "platformer" : "classic",
                    inputs.m_events.size(), plain.size(), packed->size(),
                    100.0 * double(packed->size()) / double(plain.size()));
    }
    return 0;
}

static int testSqueezedBeatsPacked() {
    for (const bool platformer : {false, true}) {
        const auto inputs = realisticMacro(4000, platformer);
        const auto squeezed = encode(inputs);
        const auto packed = encodePacked(inputs);
        CHECK(squeezed.has_value() && packed.has_value());
        CHECK((*squeezed)[4] == FILE_VERSION && (*packed)[4] == FILE_VERSION_PACKED);
        CHECK(squeezed->size() < packed->size());
        const auto decoded = decode(*packed);
        CHECK(decoded.has_value());
        CHECK(decoded->m_platformer == platformer);
        CHECK(decoded->m_events.size() == inputs.m_events.size());
        for (size_t i = 0; i < inputs.m_events.size(); ++i)
            CHECK(sameEvent(decoded->m_events[i], inputs.m_events[i]));
        std::printf("  %-10s v3 %7zu  v4 %7zu  (%.0f%%)\n",
                    platformer ? "platformer" : "classic", packed->size(),
                    squeezed->size(),
                    100.0 * double(squeezed->size()) / double(packed->size()));
    }

    const auto sample = sampleInputs();
    const auto packedSample = encodePacked(sample);
    CHECK(packedSample.has_value());
    const auto sampleBack = decode(*packedSample);
    CHECK(sampleBack.has_value());
    for (size_t i = 0; i < sample.m_events.size(); ++i)
        CHECK(sameEvent(sampleBack->m_events[i], sample.m_events[i]));

    Inputs spam;
    for (uint64_t i = 0; i < 100'000; ++i)
        spam.m_events.emplace_back(i * 2, EventKind::Jump, (i & 1) == 0, false);
    const auto spamBytes = encode(spam);
    CHECK(spamBytes.has_value());
    CHECK(spamBytes->size() < 2'000);
    const auto spamBack = decode(*spamBytes);
    CHECK(spamBack.has_value() && spamBack->m_events.size() == 100'000);
    return 0;
}

static int testPlainFilesStillDecode() {
    const auto inputs = sampleInputs();
    const auto plain = encodePlain(inputs);
    const auto decoded = decode(plain);
    CHECK(decoded.has_value());
    CHECK(decoded->m_seed == inputs.m_seed);
    CHECK(decoded->m_tps == inputs.m_tps);
    CHECK(decoded->m_events.size() == inputs.m_events.size());
    for (size_t i = 0; i < inputs.m_events.size(); ++i)
        CHECK(sameEvent(decoded->m_events[i], inputs.m_events[i]));
    return 0;
}

int main() {
    if (testRoundTrip()) return 1;
    if (testCorruption()) return 1;
    if (testSortedRequirement()) return 1;
    if (testSemanticCorruption()) return 1;
    if (testTagFlagCorruption()) return 1;
    if (testMetaCorruption()) return 1;
    if (testPlatformerFlag()) return 1;
    if (testTpsSwitching()) return 1;
    if (testTpsCoalescing()) return 1;
    if (testPlainFilesStillDecode()) return 1;
    if (testPackedCompression()) return 1;
    if (testSqueezedBeatsPacked()) return 1;
    if (testCraftedCounts()) return 1;
    if (testVarintBoundaries()) return 1;
    if (testPerf()) return 1;
    std::printf("vine_format: %d checks passed\n", g_checks);
    return 0;
}
