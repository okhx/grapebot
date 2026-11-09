#pragma once

#include <Geode/platform/cplatform.h>

#ifdef GEODE_IS_IOS

#include <Geode/Result.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

std::filesystem::path iosVisibleVideosDirectory();

class IOSVideoWriter {
    struct Impl;
    std::unique_ptr<Impl> m_impl;

   public:
    IOSVideoWriter();
    ~IOSVideoWriter();

    geode::Result<> open(const std::filesystem::path& output, int width,
                         int height, int fps, uint32_t bitrate,
                         int sampleRate, int channels, bool includeAudio);
    geode::Result<bool> appendRGBA(const std::vector<uint8_t>& rgba,
                                   int repeat = 1);
    geode::Result<bool> appendAudio(const std::vector<float>& pcm);
    geode::Result<> finish();
};

#endif
