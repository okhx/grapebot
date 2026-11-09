#pragma once

#include <Geode/Geode.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/av_api.hpp"
#include "core/av_output.hpp"
#include "core/format.hpp"
#include "core/gpu_capture.hpp"

namespace grape::render {

class PcPipeline {
   public:
    struct Config {
        std::filesystem::path m_outputPath;
        std::string m_videoCodec;
        std::string m_audioCodec = "aac";
        std::string m_extraArgs;
        int m_width = 1920;
        int m_height = 1080;
        int m_fps = 60;
        int64_t m_bitrate = 0;
        AVPixelFormat m_pixelFormat = AV_PIX_FMT_NV12;
        bool m_audio = true;
        int m_sampleRate = 48000;
        int m_channels = 2;
    };

    ~PcPipeline() { shutdown(); }

    PcPipeline(PcPipeline const&) = delete;
    PcPipeline& operator=(PcPipeline const&) = delete;
    PcPipeline() = default;

    geode::Result<> start(Config const& config);

    bool submitFrame(int64_t pts);

    geode::Result<> submitAudio(float const* samples, size_t sampleCount,
                                int64_t pts);

    int audioFrameSize() const;

    bool canCapture() const {
        if (!m_running.load(std::memory_order_acquire)) return true;
        return m_capture.hasCapacity();
    }

    void requestStop();
    void shutdown();

    bool running() const { return m_running.load(std::memory_order_acquire); }
    bool draining() const { return m_draining.load(std::memory_order_acquire); }
    int framesEncoded() const {
        return m_framesEncoded.load(std::memory_order_relaxed);
    }
    size_t backlog() const;
    int sceneFramebuffer() const { return m_capture.sceneFramebuffer(); }
    void displayPreview() { m_capture.displayPreview(); }
    void setFade(float amount) { m_capture.setFade(amount); }
    std::string const& failure() const { return m_failure; }

   private:
    struct Pending {
        GpuCapture::Lease m_lease;
        int64_t m_pts = 0;
    };

    void encoderLoop();
    void teardown();
    void recordFailure(std::string message);

    AvApi m_api;
    Muxer m_muxer{&m_api};
    VideoEncoder m_video{&m_api};
    AudioEncoder m_audio{&m_api};
    GpuCapture m_capture;

    FormatSpec const* m_format = nullptr;
    int m_videoStream = -1;
    int m_audioStream = -1;
    bool m_hasAudio = false;
    int m_sourceRate = 0;
    int m_sourceChannels = 0;

    std::thread m_encoderThread;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<Pending> m_queue;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_draining{false};
    std::atomic<int> m_framesEncoded{0};

    std::mutex m_failureMutex;
    std::string m_failure;
};

}
