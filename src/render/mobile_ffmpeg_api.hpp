#pragma once

#include <Geode/Geode.hpp>
#include <Geode/loader/Event.hpp>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace ffmpeg {
inline namespace v2 {
enum class HardwareAccelerationType : int { NONE = 0 };
enum class PixelFormat : int { NONE = -1, YUV420P = 0, YUYV422 = 1, RGB24 = 2 };

struct RenderSettings {
    HardwareAccelerationType m_hardwareAccelerationType =
        HardwareAccelerationType::NONE;
    PixelFormat m_pixelFormat = PixelFormat::RGB24;
    std::string m_codec;
    std::string m_colorspaceFilters;
    bool m_doVerticalFlip = true;
    int64_t m_bitrate = 30'000'000;
    uint32_t m_width = 1920;
    uint32_t m_height = 1080;
    uint16_t m_fps = 60;
    std::filesystem::path m_outputFile;
};
}
}

namespace ffmpeg::events::impl {
constexpr size_t VTABLE_VERSION = 1;

struct VTable {
    void* (*createRecorder)() = nullptr;
    void (*deleteRecorder)(void*) = nullptr;
    geode::Result<> (*initRecorder)(void*, ffmpeg::RenderSettings const&) =
        nullptr;
    void (*stopRecorder)(void*) = nullptr;
    geode::Result<> (*writeFrame)(void*, std::span<uint8_t const>) = nullptr;
    std::vector<std::string> (*getAvailableCodecs)() = nullptr;
    geode::Result<> (*mixVideoAudio)(std::filesystem::path const&,
                                    std::filesystem::path const&,
                                    std::filesystem::path const&) = nullptr;
    geode::Result<> (*mixVideoRaw)(std::filesystem::path const&,
                                  std::span<float>,
                                  std::filesystem::path const&) = nullptr;
};

struct FetchVTableEvent
    : geode::Event<FetchVTableEvent, bool(VTable&, size_t)> {
    using Event::Event;
};

inline VTable& getVTable() {
    static VTable vtable;
    static bool initialized = false;
    if (!initialized) {
        initialized = FetchVTableEvent().send(vtable, VTABLE_VERSION);
    }
    return vtable;
}
}

class MobileFFmpegRecorder {
    void* m_ptr = nullptr;
   public:
    MobileFFmpegRecorder() {
        auto& vtable = ffmpeg::events::impl::getVTable();
        if (vtable.createRecorder) m_ptr = vtable.createRecorder();
    }
    ~MobileFFmpegRecorder() {
        if (m_ptr) {
            auto& vtable = ffmpeg::events::impl::getVTable();
            if (vtable.deleteRecorder) vtable.deleteRecorder(m_ptr);
        }
    }
    bool valid() const { return m_ptr != nullptr; }
    geode::Result<> init(ffmpeg::RenderSettings const& settings) {
        auto& vtable = ffmpeg::events::impl::getVTable();
        if (!vtable.initRecorder) return geode::Err("FFmpeg API unavailable");
        return vtable.initRecorder(m_ptr, settings);
    }
    geode::Result<> writeFrame(std::vector<uint8_t> const& frame) {
        auto& vtable = ffmpeg::events::impl::getVTable();
        if (!vtable.writeFrame || !m_ptr)
            return geode::Err("FFmpeg API writer unavailable");
        return vtable.writeFrame(m_ptr, frame);
    }
    void stop() {
        if (m_ptr) {
            auto& vtable = ffmpeg::events::impl::getVTable();
            if (vtable.stopRecorder) vtable.stopRecorder(m_ptr);
        }
    }
};

inline bool mobileCanMixAudio() {
    return ffmpeg::events::impl::getVTable().mixVideoAudio != nullptr;
}

inline geode::Result<> mobileMixVideoAudio(
    std::filesystem::path const& video, std::filesystem::path const& audio,
    std::filesystem::path const& output) {
    auto& vtable = ffmpeg::events::impl::getVTable();
    if (!vtable.mixVideoAudio) return geode::Err("FFmpeg API mixer unavailable");
    return vtable.mixVideoAudio(video, audio, output);
}
