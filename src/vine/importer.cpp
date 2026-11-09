#include "importer.hpp"

#include "foreign.hpp"
#include "slc.hpp"
#include "util/atomic_file.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace grape::vine {

namespace {

constexpr size_t MAX_PACK_DEPTH = 64;

struct ImportFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class PackReader {
    std::span<const uint8_t> m_data;
    size_t m_pos = 0;

    uint8_t byte() {
        if (m_pos >= m_data.size()) throw ImportFailure("Unexpected EOF");
        return m_data[m_pos++];
    }

    template <class T>
    T big() {
        if (m_data.size() - m_pos < sizeof(T))
            throw ImportFailure("Unexpected EOF");
        T value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value = static_cast<T>((value << 8) | byte());
        return value;
    }

    size_t length(uint8_t tag, uint8_t fixMask, uint8_t fixValue,
                  uint8_t tag16, uint8_t tag32) {
        if ((tag & fixMask) == fixValue) return tag & ~fixMask;
        if (tag == tag16) return big<uint16_t>();
        if (tag == tag32) return big<uint32_t>();
        throw ImportFailure("Unexpected MessagePack container");
    }

    void skipBytes(size_t count) {
        if (count > m_data.size() - m_pos)
            throw ImportFailure("Unexpected EOF");
        m_pos += count;
    }

   public:
    explicit PackReader(std::span<const uint8_t> data) : m_data(data) {}

    size_t mapLength() { return length(byte(), 0xf0, 0x80, 0xde, 0xdf); }

    size_t arrayLength() { return length(byte(), 0xf0, 0x90, 0xdc, 0xdd); }

    std::string_view string() {
        const uint8_t tag = byte();
        size_t size = 0;
        if ((tag & 0xe0) == 0xa0)
            size = tag & 0x1f;
        else if (tag == 0xd9)
            size = byte();
        else if (tag == 0xda)
            size = big<uint16_t>();
        else if (tag == 0xdb)
            size = big<uint32_t>();
        else
            throw ImportFailure("Expected MessagePack string");
        if (size > m_data.size() - m_pos)
            throw ImportFailure("Unexpected EOF");
        const char* data = reinterpret_cast<const char*>(m_data.data() + m_pos);
        m_pos += size;
        return {data, size};
    }

    double number() {
        const uint8_t tag = byte();
        if (tag <= 0x7f) return tag;
        if (tag >= 0xe0) return static_cast<int8_t>(tag);
        switch (tag) {
            case 0xca:
                return std::bit_cast<float>(big<uint32_t>());
            case 0xcb:
                return std::bit_cast<double>(big<uint64_t>());
            case 0xcc:
                return byte();
            case 0xcd:
                return big<uint16_t>();
            case 0xce:
                return big<uint32_t>();
            case 0xcf:
                return static_cast<double>(big<uint64_t>());
            case 0xd0:
                return static_cast<int8_t>(byte());
            case 0xd1:
                return static_cast<int16_t>(big<uint16_t>());
            case 0xd2:
                return static_cast<int32_t>(big<uint32_t>());
            case 0xd3:
                return static_cast<double>(static_cast<int64_t>(big<uint64_t>()));
            default:
                throw ImportFailure("Expected MessagePack number");
        }
    }

    bool boolean() {
        const uint8_t tag = byte();
        if (tag == 0xc2) return false;
        if (tag == 0xc3) return true;
        throw ImportFailure("Expected MessagePack bool");
    }

    void skip(size_t depth = 0) {
        if (depth > MAX_PACK_DEPTH) throw ImportFailure("MessagePack nesting too deep");
        const uint8_t tag = byte();
        if (tag <= 0x7f || tag >= 0xe0 || tag == 0xc0 || tag == 0xc2 ||
            tag == 0xc3)
            return;
        if ((tag & 0xe0) == 0xa0) return skipBytes(tag & 0x1f);
        if ((tag & 0xf0) == 0x90) {
            for (size_t i = 0; i < (tag & 0x0f); ++i) skip(depth + 1);
            return;
        }
        if ((tag & 0xf0) == 0x80) {
            for (size_t i = 0; i < (tag & 0x0f); ++i) {
                skip(depth + 1);
                skip(depth + 1);
            }
            return;
        }
        switch (tag) {
            case 0xc4:
                return skipBytes(byte());
            case 0xc5:
                return skipBytes(big<uint16_t>());
            case 0xc6:
                return skipBytes(big<uint32_t>());
            case 0xca:
                return skipBytes(4);
            case 0xcb:
                return skipBytes(8);
            case 0xcc:
            case 0xd0:
                return skipBytes(1);
            case 0xcd:
            case 0xd1:
                return skipBytes(2);
            case 0xce:
            case 0xd2:
                return skipBytes(4);
            case 0xcf:
            case 0xd3:
                return skipBytes(8);
            case 0xd9:
                return skipBytes(byte());
            case 0xda:
                return skipBytes(big<uint16_t>());
            case 0xdb:
                return skipBytes(big<uint32_t>());
            case 0xdc: {
                const size_t count = big<uint16_t>();
                for (size_t i = 0; i < count; ++i) skip(depth + 1);
                return;
            }
            case 0xdd: {
                const size_t count = big<uint32_t>();
                for (size_t i = 0; i < count; ++i) skip(depth + 1);
                return;
            }
            case 0xde: {
                const size_t count = big<uint16_t>();
                for (size_t i = 0; i < count; ++i) {
                    skip(depth + 1);
                    skip(depth + 1);
                }
                return;
            }
            case 0xdf: {
                const size_t count = big<uint32_t>();
                for (size_t i = 0; i < count; ++i) {
                    skip(depth + 1);
                    skip(depth + 1);
                }
                return;
            }
            default:
                throw ImportFailure("Unsupported MessagePack value");
        }
    }
};

EventKind buttonKind(int button) {
    if (button == 2) return EventKind::Left;
    if (button == 3) return EventKind::Right;
    return EventKind::Jump;
}

using Bytes = std::span<const uint8_t>;

std::string_view textView(Bytes bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::optional<matjson::Value> jsonOf(Bytes bytes) {
    std::string_view text = textView(bytes);
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos || (text[first] != '{' && text[first] != '['))
        return std::nullopt;
    auto parsed = matjson::parse(text);
    if (parsed.isErr()) return std::nullopt;
    return std::move(parsed).unwrap();
}

uint64_t frameOf(const matjson::Value& value) {
    const auto number = value.asDouble();
    if (number.isErr() || !std::isfinite(number.unwrap()) || number.unwrap() < 0.0)
        throw ImportFailure("Invalid frame");
    return static_cast<uint64_t>(std::llround(number.unwrap()));
}

bool truthy(const matjson::Value& value) {
    if (auto flag = value.asBool(); flag.isOk()) return flag.unwrap();
    if (auto number = value.asDouble(); number.isOk()) return number.unwrap() != 0.0;
    return false;
}

ForeignInputs parseGdrJson(const matjson::Value& json) {
    auto inputs = json["inputs"].asArray();
    if (inputs.isErr()) throw ImportFailure("GDR has no inputs");
    ForeignInputs foreign;
    foreign.m_format = "GDR JSON";
    foreign.m_tps = json["framerate"].asDouble().unwrapOr(240.0);
    for (const auto& input : inputs.unwrap()) {
        if (!input.contains("frame") || !input.contains("down"))
            throw ImportFailure("Invalid GDR input");
        foreign.m_inputs.addEvent(frameOf(input["frame"]),
                                  buttonKind(static_cast<int>(
                                      input["btn"].asInt().unwrapOr(1))),
                                  truthy(input["down"]),
                                  truthy(input["2p"]));
    }
    return foreign;
}

ForeignInputs parseMhrJson(const matjson::Value& json) {
    auto events = json["events"].asArray();
    if (events.isErr() || !json["meta"]["fps"].isNumber())
        throw ImportFailure("Not MHR JSON");
    ForeignInputs foreign;
    foreign.m_format = "Mega Hack Replay JSON";
    foreign.m_tps = json["meta"]["fps"].asDouble().unwrapOr(240.0);
    for (const auto& event : events.unwrap()) {
        if (!event["frame"].isNumber() || !event.contains("down")) continue;
        foreign.m_inputs.addEvent(frameOf(event["frame"]), EventKind::Jump,
                                  truthy(event["down"]), truthy(event["p2"]));
    }
    return foreign;
}

ForeignInputs parseTasBot(const matjson::Value& json) {
    auto macro = json["macro"].asArray();
    if (macro.isErr() || !json["fps"].isNumber()) throw ImportFailure("Not TASBot");
    ForeignInputs foreign;
    foreign.m_format = "TASBot";
    foreign.m_tps = json["fps"].asDouble().unwrapOr(240.0);
    for (const auto& step : macro.unwrap()) {
        if (!step["frame"].isNumber() || !step.contains("player_1") ||
            !step.contains("player_2"))
            throw ImportFailure("Not TASBot");
        const uint64_t frame = frameOf(step["frame"]) + 1;
        for (const bool player2 : {false, true}) {
            const auto click =
                step[player2 ? "player_2" : "player_1"]["click"].asInt().unwrapOr(0);
            if (click == 1 || click == 2)
                foreign.m_inputs.addEvent(frame, EventKind::Jump, click == 1, player2);
        }
    }
    return foreign;
}

ForeignInputs parseEchoJson(const matjson::Value& json) {
    ForeignInputs foreign;
    if (auto inputs = json["inputs"].asArray(); inputs.isOk() && json["fps"].isNumber()) {
        foreign.m_format = "Echo";
        foreign.m_tps = std::round(json["fps"].asDouble().unwrapOr(240.0));
        for (const auto& input : inputs.unwrap()) {
            if (!input.contains("holding") || !input.contains("frame"))
                throw ImportFailure("Not Echo");
            foreign.m_inputs.addEvent(frameOf(input["frame"]), EventKind::Jump,
                                      truthy(input["holding"]),
                                      truthy(input["player_2"]));
        }
        return foreign;
    }
    auto replay = json["Echo Replay"].asArray();
    if (replay.isErr() || !json["FPS"].isNumber()) throw ImportFailure("Not Echo");
    foreign.m_format = "Echo (old)";
    foreign.m_tps = std::round(json["FPS"].asDouble().unwrapOr(240.0));
    const uint64_t start =
        static_cast<uint64_t>(std::max<double>(0.0, json["Starting Frame"].asDouble().unwrapOr(0.0)));
    for (const auto& input : replay.unwrap())
        foreign.m_inputs.addEvent(frameOf(input["Frame"]) + start, EventKind::Jump,
                                  truthy(input["Hold"]), truthy(input["Player 2"]));
    return foreign;
}

ForeignInputs parseGdrBinary(Bytes bytes) {
    PackReader reader(bytes);
    ForeignInputs foreign;
    foreign.m_format = "GDR";
    const size_t fields = reader.mapLength();
    for (size_t i = 0; i < fields; ++i) {
        const auto key = reader.string();
        if (key == "framerate") {
            foreign.m_tps = reader.number();
        } else if (key == "inputs") {
            const size_t count = reader.arrayLength();
            foreign.m_inputs.m_events.reserve(
                foreign.m_inputs.m_events.size() + count);
            for (size_t j = 0; j < count; ++j) {
                uint64_t frame = 0;
                int button = 1;
                bool player2 = false;
                bool down = false;
                const size_t inputFields = reader.mapLength();
                for (size_t k = 0; k < inputFields; ++k) {
                    const auto inputKey = reader.string();
                    if (inputKey == "frame")
                        frame = static_cast<uint64_t>(reader.number());
                    else if (inputKey == "btn")
                        button = static_cast<int>(reader.number());
                    else if (inputKey == "2p")
                        player2 = reader.boolean();
                    else if (inputKey == "down")
                        down = reader.boolean();
                    else
                        reader.skip();
                }
                foreign.m_inputs.addEvent(frame, buttonKind(button), down,
                                        player2);
            }
        } else {
            reader.skip();
        }
    }
    return foreign;
}

uint32_t little32(Bytes bytes, size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4)
        throw ImportFailure("Unexpected YBot EOF");
    return static_cast<uint32_t>(bytes[offset]) |
           static_cast<uint32_t>(bytes[offset + 1]) << 8 |
           static_cast<uint32_t>(bytes[offset + 2]) << 16 |
           static_cast<uint32_t>(bytes[offset + 3]) << 24;
}

ForeignInputs parseYbot(Bytes bytes) {
    if (bytes.size() < 16 || textView(bytes).substr(0, 4) != "ybot")
        throw ImportFailure("Invalid YBot header");

    const uint32_t metaLength = little32(bytes, 8);
    const uint32_t blobCount = little32(bytes, 12);
    size_t pos = 16;
    if (metaLength > bytes.size() - pos)
        throw ImportFailure("Invalid YBot metadata");

    ForeignInputs foreign;
    foreign.m_format = "yBot 2";
    if (metaLength >= 28) {
        const uint32_t rawFps = little32(bytes, pos + 24);
        foreign.m_tps = std::bit_cast<float>(rawFps);
    }
    pos += metaLength;
    for (uint32_t i = 0; i < blobCount; ++i) {
        const uint32_t size = little32(bytes, pos);
        pos += 4;
        if (size > bytes.size() - pos)
            throw ImportFailure("Invalid YBot blob");
        pos += size;
    }

    uint64_t frame = 0;
    while (pos < bytes.size()) {
        uint64_t packed = 0;
        int shift = 0;
        uint8_t part = 0;
        do {
            if (pos >= bytes.size() || shift >= 64)
                throw ImportFailure("Invalid YBot action");
            part = bytes[pos++];
            packed |= static_cast<uint64_t>(part & 0x7f) << shift;
            shift += 7;
        } while (part & 0x80);

        const uint8_t flags = packed & 0x0f;
        frame += packed >> 4;
        if (flags == 0x0f) {
            if (pos + 4 > bytes.size())
                throw ImportFailure("Invalid YBot FPS action");
            pos += 4;
            continue;
        }
        const int button = flags >> 2;
        if (button < 1 || button > 3)
            throw ImportFailure("Invalid YBot button");
        foreign.m_inputs.addEvent(frame, buttonKind(button), (flags & 2) != 0,
                                (flags & 1) == 0);
    }
    return foreign;
}

ForeignInputs parseNative(Bytes bytes) {
    auto decoded = decode(bytes);
    if (!decoded.has_value()) throw ImportFailure(decoded.error());
    ForeignInputs foreign;
    foreign.m_format = "Grape";
    foreign.m_inputs = std::move(decoded.value());
    foreign.m_seed = foreign.m_inputs.m_seed;
    foreign.m_tps = foreign.m_inputs.m_tps;
    foreign.m_seeded = true;
    return foreign;
}

ForeignInputs parseSlc(Bytes bytes) {
    auto decoded = decodeSlc(bytes);
    if (!decoded.has_value()) throw ImportFailure(decoded.error());
    ForeignInputs foreign;
    foreign.m_format = "Legacy";
    foreign.m_inputs.m_events = std::move(decoded.value().m_events);
    foreign.m_inputs.m_seed = decoded.value().m_seed;
    foreign.m_inputs.m_tps = decoded.value().m_tps;
    foreign.m_seed = decoded.value().m_seed;
    foreign.m_tps = decoded.value().m_tps;
    foreign.m_seeded = true;
    return foreign;
}

ForeignInputs fromClicks(foreign::Detected detected) {
    ForeignInputs converted;
    converted.m_format = foreign::nameOf(detected.m_format);
    converted.m_warning = std::move(detected.m_parsed.m_warning);
    converted.m_tps = detected.m_parsed.m_tps.value_or(240.0);
    converted.m_inputs.m_events.reserve(detected.m_parsed.m_clicks.size());
    for (const auto& click : detected.m_parsed.m_clicks)
        converted.m_inputs.addEvent(click.m_frame, EventKind::Jump, click.m_down,
                                    click.m_player2);
    return converted;
}

std::string lowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return extension;
}

bool startsWith(Bytes bytes, std::string_view prefix) {
    return textView(bytes).starts_with(prefix);
}

ForeignInputs parseJson(const matjson::Value& json) {
    std::string firstError;
    for (auto* parser : {&parseGdrJson, &parseMhrJson, &parseTasBot, &parseEchoJson}) {
        try {
            auto foreign = parser(json);
            if (!foreign.m_inputs.m_events.empty()) return foreign;
        } catch (const std::exception& error) {
            if (firstError.empty()) firstError = error.what();
        }
    }
    throw ImportFailure(firstError.empty() ? "Unrecognised JSON macro" : firstError);
}

ForeignInputs parseBytes(Bytes bytes, const std::string& extension) {
    if (startsWith(bytes, "VINE")) return parseNative(bytes);
    if (startsWith(bytes, "SLC3RPLY") || startsWith(bytes, "SILL"))
        return parseSlc(bytes);
    if (auto json = jsonOf(bytes)) return parseJson(*json);
    if (!bytes.empty() && ((bytes[0] & 0xf0) == 0x80 || bytes[0] == 0xde ||
                           bytes[0] == 0xdf)) {
        try {
            auto foreign = parseGdrBinary(bytes);
            if (!foreign.m_inputs.m_events.empty()) return foreign;
        } catch (const std::exception&) {
        }
    }
    if (startsWith(bytes, "ybot") && extension != ".ybf") {
        try {
            return parseYbot(bytes);
        } catch (const std::exception&) {
        }
    }
    auto detected = foreign::detect(bytes, extension);
    if (!detected.has_value()) throw ImportFailure(detected.error());
    return fromClicks(std::move(detected.value()));
}

}

geode::Result<ForeignInputs> loadForeign(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path))
        return geode::Err("Macro file does not exist");

    auto bytes = geode::utils::file::readBinary(path.string());
    if (bytes.isErr()) return geode::Err("Could not read the macro file");

    ForeignInputs foreign;
    try {
        foreign = parseBytes(bytes.unwrap(), lowerExtension(path));
    } catch (const std::exception& error) {
        return geode::Err("Macro conversion failed: {}", error.what());
    }

    if (foreign.m_inputs.m_events.empty())
        return geode::Err("Converted macro contains no inputs");
    if (!std::isfinite(foreign.m_tps) || foreign.m_tps <= 0.0)
        foreign.m_tps = 240.0;

    foreign.m_inputs.sort();
    return geode::Ok(std::move(foreign));
}

bool storeInputs(const std::filesystem::path& path, const Inputs& inputs) {
    auto bytes = encode(inputs);
    if (!bytes.has_value()) {
        geode::log::error("Failed to encode replay: {}", bytes.error());
        return false;
    }
    std::error_code error;
    if (!grape::files::writeAtomically(
            path,
            std::string_view(reinterpret_cast<const char*>(bytes->data()),
                             bytes->size()),
            error)) {
        geode::log::error("Failed to save replay: {} ({})", path,
                          error.message());
        return false;
    }
    return true;
}

geode::utils::file::FilePickOptions foreignPickOptions() {
    return {
        .defaultPath = std::nullopt,
        .filters = {{
            .description = "Macro files",
            .files = {"*.vine", "*.grape", "*.slc", "*.gdr", "*.json",
                      "*.ybot", "*.ybf", "*.mhr", "*.echo", "*.replay",
                      "*.tcm", "*.xd", "*.xbot", "*.kd", "*.rsh", "*.zbf",
                      "*.freplay", "*.brrr", "*.gbr", "*.gbr6", "*.skk", "*.txt",
                      "*.csv"},
        }},
    };
}

}
