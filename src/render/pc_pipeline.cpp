#include "pc_pipeline.hpp"

#include <utility>

namespace grape::render {

geode::Result<> PcPipeline::start(Config const& config) {
    if (m_running.load(std::memory_order_acquire))
        return geode::Err("Recorder is already running");

    shutdown();

    {
        std::lock_guard<std::mutex> lock(m_failureMutex);
        m_failure.clear();
    }

    m_format = formatFor(config.m_pixelFormat);
    if (!m_format) return geode::Err("Unsupported pixel format");

    GEODE_UNWRAP(m_api.load());

    std::error_code ignored;
    std::filesystem::create_directories(config.m_outputPath.parent_path(),
                                        ignored);

    const auto abort = [this, &config](std::string message) -> geode::Result<> {
        teardown();
        std::error_code removeError;
        std::filesystem::remove(config.m_outputPath, removeError);
        return geode::Err(std::move(message));
    };

    if (auto opened = m_muxer.open(config.m_outputPath); opened.isErr())
        return abort(opened.unwrapErr());

    const bool globalHeader =
        m_muxer.context() != nullptr &&
        (m_muxer.context()->oformat->flags & AVFMT_GLOBALHEADER) != 0;

    VideoEncoder::Options video;
    video.m_codecName = config.m_videoCodec;
    video.m_width = config.m_width;
    video.m_height = config.m_height;
    video.m_fps = config.m_fps;
    video.m_bitrate = config.m_bitrate;
    video.m_pixelFormat = config.m_pixelFormat;
    video.m_extraArgs = config.m_extraArgs;
    video.m_globalHeader = globalHeader;

    if (auto opened = m_video.open(video); opened.isErr())
        return abort(opened.unwrapErr());

    auto videoStream = m_muxer.addStream(m_video.context());
    if (videoStream.isErr()) return abort(videoStream.unwrapErr());
    m_videoStream = videoStream.unwrap();

    m_hasAudio = config.m_audio && config.m_sampleRate > 0 &&
                 config.m_channels > 0;
    m_sourceRate = config.m_sampleRate;
    m_sourceChannels = config.m_channels;
    if (m_hasAudio) {
        AudioEncoder::Options audio;
        audio.m_codecName = config.m_audioCodec;
        audio.m_sampleRate = config.m_sampleRate;
        audio.m_channels = config.m_channels;
        audio.m_globalHeader = globalHeader;

        if (auto opened = m_audio.open(audio); opened.isErr())
            return abort(opened.unwrapErr());

        auto audioStream = m_muxer.addStream(m_audio.context());
        if (audioStream.isErr()) return abort(audioStream.unwrapErr());
        m_audioStream = audioStream.unwrap();
    }

    if (auto header = m_muxer.writeHeader(); header.isErr())
        return abort(header.unwrapErr());

    if (auto capture = m_capture.init(m_format,
                                      static_cast<uint32_t>(config.m_width),
                                      static_cast<uint32_t>(config.m_height));
        capture.isErr())
        return abort(capture.unwrapErr());

    m_framesEncoded.store(0, std::memory_order_relaxed);
    m_draining.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    m_encoderThread = std::thread(&PcPipeline::encoderLoop, this);
    return geode::Ok();
}

bool PcPipeline::submitFrame(int64_t pts) {
    if (!m_running.load(std::memory_order_acquire)) return false;

    if (!m_capture.hasCapacity()) return false;

    const GpuCapture::Lease lease = m_capture.capture(pts);
    if (!lease.valid()) return true;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push_back(Pending{lease, lease.m_pts});
    }
    m_queueCv.notify_one();
    return true;
}

geode::Result<> PcPipeline::submitAudio(float const* samples,
                                        size_t sampleCount, int64_t pts) {
    if (!m_hasAudio) return geode::Ok();
    if (!m_running.load(std::memory_order_acquire) &&
        !m_draining.load(std::memory_order_acquire)) {
        return geode::Ok();
    }

    GEODE_UNWRAP(m_audio.submit(samples, sampleCount, m_sourceRate,
                                m_sourceChannels, pts));
    return m_audio.drain(m_muxer, m_audioStream);
}

int PcPipeline::audioFrameSize() const {
    return m_hasAudio ? m_audio.frameSize() : 0;
}

size_t PcPipeline::backlog() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_queue.size();
}

void PcPipeline::encoderLoop() {
    for (;;) {
        Pending pending;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCv.wait(lock, [this] {
                return !m_queue.empty() ||
                       !m_running.load(std::memory_order_acquire);
            });
            if (m_queue.empty()) {
                if (!m_running.load(std::memory_order_acquire)) break;
                continue;
            }
            pending = m_queue.front();
            m_queue.pop_front();
        }

        auto submitted =
            m_video.submit(pending.m_lease.m_data, pending.m_lease.m_size,
                           pending.m_pts, *m_format);
        if (submitted.isOk()) submitted = m_video.drain(m_muxer, m_videoStream);

        m_capture.release(pending.m_lease.m_slot);

        if (submitted.isErr()) {
            recordFailure(submitted.unwrapErr());
            m_running.store(false, std::memory_order_release);
            break;
        }

        m_framesEncoded.fetch_add(1, std::memory_order_relaxed);
    }

    for (;;) {
        Pending pending;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_queue.empty()) break;
            pending = m_queue.front();
            m_queue.pop_front();
        }

        auto submitted =
            m_video.submit(pending.m_lease.m_data, pending.m_lease.m_size,
                           pending.m_pts, *m_format);
        if (submitted.isOk()) submitted = m_video.drain(m_muxer, m_videoStream);
        m_capture.release(pending.m_lease.m_slot);
        if (submitted.isErr()) {
            recordFailure(submitted.unwrapErr());
            break;
        }
        m_framesEncoded.fetch_add(1, std::memory_order_relaxed);
    }

    if (auto flushed = m_video.flush(m_muxer, m_videoStream); flushed.isErr())
        recordFailure(flushed.unwrapErr());

    if (m_hasAudio) {
        if (auto flushed = m_audio.flush(m_muxer, m_audioStream);
            flushed.isErr()) {
            recordFailure(flushed.unwrapErr());
        }
    }

    if (auto finished = m_muxer.finish(); finished.isErr())
        recordFailure(finished.unwrapErr());

    m_draining.store(false, std::memory_order_release);
}

void PcPipeline::requestStop() {
    if (!m_running.load(std::memory_order_acquire)) return;
    m_draining.store(true, std::memory_order_release);
    m_running.store(false, std::memory_order_release);
    m_queueCv.notify_all();
}

void PcPipeline::shutdown() {
    if (m_running.load(std::memory_order_acquire)) {
        std::vector<GpuCapture::Lease> tail = m_capture.drainPending();
        if (!tail.empty()) {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            for (GpuCapture::Lease const& lease : tail)
                m_queue.push_back(Pending{lease, lease.m_pts});
        }
    }

    requestStop();

    if (m_encoderThread.joinable()) m_encoderThread.join();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (Pending const& pending : m_queue)
            m_capture.release(pending.m_lease.m_slot);
        m_queue.clear();
    }

    teardown();
}

void PcPipeline::teardown() {
    m_capture.destroy();
    m_video.close();
    m_audio.close();
    m_muxer.close();
    m_videoStream = -1;
    m_audioStream = -1;
    m_hasAudio = false;
    m_sourceRate = 0;
    m_sourceChannels = 0;
    m_format = nullptr;
    m_draining.store(false, std::memory_order_release);
    m_running.store(false, std::memory_order_release);
}

void PcPipeline::recordFailure(std::string message) {
    std::lock_guard<std::mutex> lock(m_failureMutex);
    if (m_failure.empty()) m_failure = std::move(message);
    geode::log::error("Recorder: {}", m_failure);
}

}
