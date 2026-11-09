#pragma once

#include <Geode/Geode.hpp>

#include <filesystem>
#include <mutex>
#include <string>

#include "av_api.hpp"
#include "format.hpp"

namespace grape::render {

class Muxer {
   public:
    explicit Muxer(AvApi* api) : m_api(api) {}
    ~Muxer() { close(); }

    Muxer(Muxer const&) = delete;
    Muxer& operator=(Muxer const&) = delete;

    geode::Result<> open(std::filesystem::path const& path);

    geode::Result<int> addStream(AVCodecContext* codec);

    geode::Result<> writeHeader();

    geode::Result<> write(AVPacket* packet, int streamIndex,
                          AVRational sourceTimeBase);

    geode::Result<> finish();
    void close();

    bool isOpen() const { return m_formatCtx != nullptr; }
    AVFormatContext* context() const { return m_formatCtx; }

   private:
    AvApi* m_api = nullptr;
    AVFormatContext* m_formatCtx = nullptr;
    std::vector<AVStream*> m_streams;
    std::mutex m_writeMutex;
    bool m_headerWritten = false;
    bool m_trailerWritten = false;
};

class VideoEncoder {
   public:
    explicit VideoEncoder(AvApi* api) : m_api(api) {}
    ~VideoEncoder() { close(); }

    VideoEncoder(VideoEncoder const&) = delete;
    VideoEncoder& operator=(VideoEncoder const&) = delete;

    struct Options {
        std::string m_codecName;
        int m_width = 0;
        int m_height = 0;
        int m_fps = 60;
        int64_t m_bitrate = 0;
        AVPixelFormat m_pixelFormat = AV_PIX_FMT_NONE;
        std::string m_extraArgs;
        bool m_globalHeader = false;
    };

    geode::Result<> open(Options const& options);

    geode::Result<> submit(uint8_t* data, size_t size, int64_t pts,
                           FormatSpec const& format);

    geode::Result<> drain(Muxer& muxer, int streamIndex);

    geode::Result<> flush(Muxer& muxer, int streamIndex);

    void close();

    AVCodecContext* context() const { return m_codecCtx; }

   private:
    AvApi* m_api = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;
};

class AudioEncoder {
   public:
    explicit AudioEncoder(AvApi* api) : m_api(api) {}
    ~AudioEncoder() { close(); }

    AudioEncoder(AudioEncoder const&) = delete;
    AudioEncoder& operator=(AudioEncoder const&) = delete;

    struct Options {
        std::string m_codecName = "aac";
        int m_sampleRate = 48000;
        int m_channels = 2;
        int64_t m_bitrate = 320000;
        bool m_globalHeader = false;
    };

    geode::Result<> open(Options const& options);

    int frameSize() const;

    geode::Result<> submit(float const* samples, size_t sampleCount,
                           int sourceRate, int sourceChannels, int64_t pts);

    geode::Result<> drain(Muxer& muxer, int streamIndex);
    geode::Result<> flush(Muxer& muxer, int streamIndex);

    void close();

    AVCodecContext* context() const { return m_codecCtx; }

   private:
    geode::Result<> ensureResampler(int sourceRate, int sourceChannels);

    AvApi* m_api = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;
    SwrContext* m_swr = nullptr;
    int m_swrRate = 0;
    int m_swrChannels = 0;
};

}
