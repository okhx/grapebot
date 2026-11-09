#include "vine/foreign.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>
#include <vector>

using namespace grape::vine::foreign;

static int g_checks = 0;

#define CHECK(condition)                                     \
    do {                                                     \
        ++g_checks;                                          \
        if (!(condition)) {                                  \
            std::printf("FAILED at line %d: %s\n", __LINE__, \
                        #condition);                         \
            return 1;                                        \
        }                                                    \
    } while (false)

static std::filesystem::path fixtures() {
    return std::filesystem::path(__FILE__).parent_path() / "fixtures" / "foreign";
}

static std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

using Key = std::tuple<uint64_t, bool, bool>;

static std::vector<Key> keys(const std::vector<Click>& clicks) {
    std::vector<Key> out;
    for (const auto& click : clicks) out.emplace_back(click.m_frame, click.m_player2, click.m_down);
    std::sort(out.begin(), out.end());
    return out;
}

static std::vector<Key> expected() {
    std::ifstream file(fixtures() / "expected.txt");
    std::vector<Key> out;
    uint64_t frame = 0;
    int player2 = 0;
    int down = 0;
    while (file >> frame >> player2 >> down) out.emplace_back(frame, player2 != 0, down != 0);
    std::sort(out.begin(), out.end());
    return out;
}

struct Fixture {
    const char* m_file;
    Format m_format;
    double m_tps;
};

static int testFixtures() {
    const auto want = expected();
    CHECK(want.size() == 400);
    const Fixture all[] = {
        {"mhrbin.mhr", Format::MhrBinary, 240.0},  {"echobin.echo", Format::EchoBinary, 240.0},
        {"obot3.replay", Format::OmegaBot3, 240.0}, {"obot2.replay", Format::OmegaBot2, 240.0},
        {"url.replay", Format::OmegaBot1, 240.0},   {"replaybot.replay", Format::ReplayBot, 240.0},
        {"kdbot.kd", Format::KdBot, 240.0},         {"rush.rsh", Format::Rush, 240.0},
        {"zbot.zbf", Format::ZBot, 240.0},          {"fembot.freplay", Format::Fembot, 240.0},
        {"xbot.xbot", Format::XBot, 240.0},         {"xdbot.xd", Format::XdBot, 240.0},
        {"ybot1.ybf", Format::YBot1, 240.0},        {"slc1.slc", Format::Slc1, 240.0},
        {"tcm.tcm", Format::Tcm, 240.0},            {"txt.txt", Format::Text, 240.0},
        {"csv.csv", Format::Text, 240.0},          {"gbr6.brrr", Format::GucciBot, 240.0},
        {"skk3.skk", Format::Skk3, 240.0},         {"skk3-compressed.skk", Format::Skk3, 240.0},
    };
    for (const auto& fixture : all) {
        const auto path = fixtures() / fixture.m_file;
        const auto bytes = readFile(path);
        CHECK(!bytes.empty());

        Parsed parsed;
        try {
            parsed = parse(fixture.m_format, bytes);
        } catch (const std::exception& error) {
            std::printf("%s: %s\n", fixture.m_file, error.what());
            CHECK(false);
        }
        CHECK(parsed.m_tps.has_value());
        CHECK(std::abs(*parsed.m_tps - fixture.m_tps) < 1e-6);
        if (keys(parsed.m_clicks) != want) std::printf("mismatch in %s\n", fixture.m_file);
        CHECK(keys(parsed.m_clicks) == want);

        const auto detected = detect(bytes, path.extension().string());
        if (!detected) std::printf("%s: %s\n", fixture.m_file, detected.error().c_str());
        CHECK(detected.has_value());
        if (detected->m_format != fixture.m_format)
            std::printf("%s detected as %s\n", fixture.m_file, nameOf(detected->m_format));
        CHECK(detected->m_format == fixture.m_format);
        CHECK(keys(detected->m_parsed.m_clicks) == want);
    }
    return 0;
}

static int testGucciRecords() {
    std::vector<uint8_t> file = {'G', 'B', 'R', '6', 1, 0};
    const float tps = 480.f;
    const auto* raw = reinterpret_cast<const uint8_t*>(&tps);
    file.insert(file.end(), raw, raw + 4);
    file.insert(file.end(), 16, 0);
    const std::vector<uint8_t> stream = {
        0x80, 0x0a, 0x00, 0x23, 0x03, 0x00,
        0x83, 0xff, 0x83, 0x01,
        0x82, 0x04,
        0x05, 0xff,
        0x87, 0x02,
        0x04, 0x07,
    };
    const uint32_t size = static_cast<uint32_t>(stream.size());
    for (int i = 0; i < 4; ++i) file.push_back(static_cast<uint8_t>(size >> (8 * i)));
    file.insert(file.end(), {0, 0, 0, 0, 0, 0});
    file.insert(file.end(), stream.begin(), stream.end());

    const auto parsed = parse(Format::GucciBot, file);
    CHECK(parsed.m_tps && *parsed.m_tps == 480.0);
    const std::vector<Key> want = {
        {10, false, true},  {12, false, false}, {15, false, true},  {17, false, false},
        {20, false, true},  {22, false, false}, {281, false, true}, {285, false, false},
        {540, false, true}, {545, false, false}, {554, false, true}, {558, false, false},
    };
    CHECK(keys(parsed.m_clicks) == want);
    CHECK(!parsed.m_warning.empty());
    const auto detected = detect(file, ".toosii");
    CHECK(detected && detected->m_format == Format::GucciBot);
    return 0;
}

static int testHostileInput() {
    const auto mhr = readFile(fixtures() / "mhrbin.mhr");
    for (size_t cut = 1; cut < mhr.size(); cut += 7) {
        const std::span<const uint8_t> truncated(mhr.data(), cut);
        try {
            (void)parse(Format::MhrBinary, truncated);
        } catch (const std::exception&) {
        }
        (void)detect(truncated, ".mhr");
    }

    std::vector<uint8_t> noise(4096);
    uint32_t state = 99;
    for (auto& byte : noise) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<uint8_t>(state >> 24);
    }
    for (const Format format : DETECTION_ORDER) {
        try {
            (void)parse(format, noise);
        } catch (const std::exception&) {
        }
    }

    const std::string text = "240\n10 1 0\n20 0 0\n30 down p2\n40 up p2\nnonsense\n";
    const auto plain = parse(Format::Text, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    CHECK(plain.m_clicks.size() == 4);
    CHECK(plain.m_clicks[2].m_player2 && plain.m_clicks[2].m_down);
    CHECK(!plain.m_warning.empty());
    return 0;
}

int main() {
    if (testFixtures()) return 1;
    if (testGucciRecords()) return 1;
    if (testHostileInput()) return 1;
    std::printf("foreign: %d checks passed\n", g_checks);
}
