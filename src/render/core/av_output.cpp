#include "av_output.hpp"

#include <cstring>
#include <string_view>

namespace grape::render {

namespace {

std::pair<const char*, const char*> fastEncoderSetting(std::string const& codec) {
    const auto endsWith = [&](std::string_view suffix) {
        return codec.size() >= suffix.size() && codec.compare(codec.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (codec == "libx264" || codec == "libx265") return {"preset", "veryfast"};
    if (endsWith("_nvenc")) return {"preset", "p3"};
    if (endsWith("_qsv")) return {"preset", "faster"};
    if (endsWith("_amf")) return {"quality", "speed"};
    return {nullptr, nullptr};
}

std::vector<std::pair<std::string, std::string>> parseArgs(
    std::string const& text) {
    std::vector<std::pair<std::string, std::string>> args;
    size_t i = 0;
    auto skipSpace = [&] {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(
                                      text[i])))
            ++i;
    };
    auto token = [&] {
        const size_t start = i;
        while (i < text.size() &&
               !std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        return text.substr(start, i - start);
    };

    skipSpace();
    while (i < text.size()) {
        std::string key = token();
        if (key.empty()) break;
        if (key.front() == '-') key.erase(key.begin());
        skipSpace();

        std::string value;
        if (i < text.size() && text[i] != '-') {
            value = token();
            skipSpace();
        }
        if (!key.empty()) args.emplace_back(std::move(key), std::move(value));
    }
    return args;
}

}

geode::Result<> Muxer::open(std::filesystem::path const& path) {
    close();
    if (!m_api || !m_api->loaded()) return geode::Err("FFmpeg is not loaded");

    const std::string target = path.string();
    const int ret = m_api->avformat_alloc_output_context2(
        &m_formatCtx, nullptr, nullptr, target.c_str());
    if (ret < 0 || !m_formatCtx)
        return geode::Err("Could not infer an output format from '{}'", target);

    if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
        const int opened =
            m_api->avio_open(&m_formatCtx->pb, target.c_str(), AVIO_FLAG_WRITE);
        if (opened < 0) {
            m_api->avformat_free_context(m_formatCtx);
            m_formatCtx = nullptr;
            return geode::Err("Could not open '{}' for writing", target);
        }
    }

    return geode::Ok();
}

geode::Result<int> Muxer::addStream(AVCodecContext* codec) {
    if (!m_formatCtx) return geode::Err("Muxer is not open");
    if (!codec) return geode::Err("No codec context supplied");

    AVStream* stream = m_api->avformat_new_stream(m_formatCtx, nullptr);
    if (!stream) return geode::Err("Failed to allocate output stream");

    if (m_api->avcodec_parameters_from_context(stream->codecpar, codec) < 0)
        return geode::Err("Failed to copy codec parameters to stream");

    stream->time_base = codec->time_base;
    m_streams.push_back(stream);
    return geode::Ok(stream->index);
}

geode::Result<> Muxer::writeHeader() {
    if (!m_formatCtx) return geode::Err("Muxer is not open");
    if (m_headerWritten) return geode::Ok();

    if (m_api->avformat_write_header(m_formatCtx, nullptr) < 0)
        return geode::Err("Failed to write container header");

    m_headerWritten = true;
    return geode::Ok();
}

geode::Result<> Muxer::write(AVPacket* packet, int streamIndex,
                             AVRational sourceTimeBase) {
    if (!m_formatCtx || !packet) return geode::Err("Muxer is not open");

    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (!m_headerWritten || m_trailerWritten) return geode::Ok();

    if (streamIndex < 0 ||
        streamIndex >= static_cast<int>(m_formatCtx->nb_streams))
        return geode::Err("Stream index {} out of range", streamIndex);

    AVStream* stream = m_formatCtx->streams[streamIndex];
    m_api->av_packet_rescale_ts(packet, sourceTimeBase, stream->time_base);
    packet->stream_index = streamIndex;

    if (m_api->av_interleaved_write_frame(m_formatCtx, packet) < 0)
        return geode::Err("Failed to write packet to container");

    return geode::Ok();
}

geode::Result<> Muxer::finish() {
    if (!m_formatCtx) return geode::Ok();

    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (!m_headerWritten || m_trailerWritten) return geode::Ok();

    m_trailerWritten = true;
    if (m_api->av_write_trailer(m_formatCtx) < 0)
        return geode::Err("Failed to write container trailer");

    return geode::Ok();
}

void Muxer::close() {
    if (!m_formatCtx) return;

    if (m_formatCtx->pb && !(m_formatCtx->oformat->flags & AVFMT_NOFILE))
        m_api->avio_close(m_formatCtx->pb);

    m_api->avformat_free_context(m_formatCtx);
    m_formatCtx = nullptr;
    m_streams.clear();
    m_headerWritten = false;
    m_trailerWritten = false;
}

geode::Result<> VideoEncoder::open(Options const& options) {
    close();
    if (!m_api || !m_api->loaded()) return geode::Err("FFmpeg is not loaded");

    AVCodec const* codec =
        m_api->avcodec_find_encoder_by_name(options.m_codecName.c_str());
    if (!codec)
        return geode::Err("Unknown video encoder '{}'", options.m_codecName);

    const auto openWith = [&](bool fastDefault) {
        if (m_codecCtx) m_api->avcodec_free_context(&m_codecCtx);
        m_codecCtx = m_api->avcodec_alloc_context3(codec);
        if (!m_codecCtx) return AVERROR(ENOMEM);

        m_codecCtx->width = options.m_width;
        m_codecCtx->height = options.m_height;
        m_codecCtx->time_base = AVRational{1, options.m_fps};
        m_codecCtx->framerate = AVRational{options.m_fps, 1};
        m_codecCtx->pix_fmt = options.m_pixelFormat;
        m_codecCtx->bit_rate = options.m_bitrate;
        m_codecCtx->max_b_frames = 0;
        m_codecCtx->color_primaries = AVCOL_PRI_BT709;
        m_codecCtx->color_trc = AVCOL_TRC_BT709;
        m_codecCtx->colorspace = AVCOL_SPC_BT709;
        m_codecCtx->color_range = AVCOL_RANGE_MPEG;
        if (options.m_globalHeader) m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        AVDictionary* opts = nullptr;
        for (auto const& [key, value] : parseArgs(options.m_extraArgs))
            m_api->av_dict_set(&opts, key.c_str(), value.c_str(), 0);
        if (fastDefault) {
            const auto [key, value] = fastEncoderSetting(options.m_codecName);
            if (key) m_api->av_dict_set(&opts, key, value, AV_DICT_DONT_OVERWRITE);
        }
        const int opened = m_api->avcodec_open2(m_codecCtx, codec, &opts);
        m_api->av_dict_free(&opts);
        return opened;
    };

    if (openWith(true) < 0 && openWith(false) < 0) {
        close();
        return geode::Err("Failed to open video encoder '{}'",
                          options.m_codecName);
    }

    m_frame = m_api->av_frame_alloc();
    m_packet = m_api->av_packet_alloc();
    if (!m_frame || !m_packet) {
        close();
        return geode::Err("Failed to allocate video frame or packet");
    }

    m_frame->format = m_codecCtx->pix_fmt;
    m_frame->width = m_codecCtx->width;
    m_frame->height = m_codecCtx->height;
    return geode::Ok();
}

geode::Result<> VideoEncoder::submit(uint8_t* data, size_t size, int64_t pts,
                                     FormatSpec const& format) {
    if (!m_codecCtx || !m_frame) return geode::Err("Video encoder is closed");
    if (!data || size == 0) return geode::Err("No frame data supplied");

    const uint32_t width = static_cast<uint32_t>(m_codecCtx->width);
    const uint32_t height = static_cast<uint32_t>(m_codecCtx->height);

    m_api->av_image_fill_linesizes(m_frame->linesize, m_codecCtx->pix_fmt,
                                   m_codecCtx->width);
    for (uint32_t i = 0; i < format.m_planeCount; ++i)
        m_frame->data[i] = data + format.planeOffset(i, width, height);

    m_frame->pts = pts;

    const int ret = m_api->avcodec_send_frame(m_codecCtx, m_frame);
    if (ret < 0) return geode::Err("Failed to submit video frame: {}", ret);
    return geode::Ok();
}

geode::Result<> VideoEncoder::drain(Muxer& muxer, int streamIndex) {
    if (!m_codecCtx || !m_packet) return geode::Err("Video encoder is closed");

    while (true) {
        const int ret = m_api->avcodec_receive_packet(m_codecCtx, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return geode::Err("Failed to receive video packet");

        auto written = muxer.write(m_packet, streamIndex, m_codecCtx->time_base);
        m_api->av_packet_unref(m_packet);
        if (written.isErr()) return written;
    }
    return geode::Ok();
}

geode::Result<> VideoEncoder::flush(Muxer& muxer, int streamIndex) {
    if (!m_codecCtx) return geode::Ok();
    m_api->avcodec_send_frame(m_codecCtx, nullptr);
    return drain(muxer, streamIndex);
}

void VideoEncoder::close() {
    if (m_frame) m_api->av_frame_free(&m_frame);
    if (m_packet) m_api->av_packet_free(&m_packet);
    if (m_codecCtx) m_api->avcodec_free_context(&m_codecCtx);
    m_frame = nullptr;
    m_packet = nullptr;
    m_codecCtx = nullptr;
}

geode::Result<> AudioEncoder::open(Options const& options) {
    close();
    if (!m_api || !m_api->loaded()) return geode::Err("FFmpeg is not loaded");

    AVCodec const* codec =
        m_api->avcodec_find_encoder_by_name(options.m_codecName.c_str());
    if (!codec)
        return geode::Err("Unknown audio encoder '{}'", options.m_codecName);

    m_codecCtx = m_api->avcodec_alloc_context3(codec);
    if (!m_codecCtx) return geode::Err("Failed to allocate audio encoder");

    m_codecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    m_codecCtx->sample_rate = options.m_sampleRate;
    m_codecCtx->bit_rate = options.m_bitrate;
    m_codecCtx->time_base = AVRational{1, options.m_sampleRate};
    m_api->av_channel_layout_default(&m_codecCtx->ch_layout,
                                     options.m_channels);
    if (options.m_globalHeader)
        m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (m_api->avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        close();
        return geode::Err("Failed to open audio encoder '{}'",
                          options.m_codecName);
    }

    m_frame = m_api->av_frame_alloc();
    m_packet = m_api->av_packet_alloc();
    if (!m_frame || !m_packet) {
        close();
        return geode::Err("Failed to allocate audio frame or packet");
    }

    m_frame->format = m_codecCtx->sample_fmt;
    m_frame->sample_rate = m_codecCtx->sample_rate;
    m_frame->ch_layout = m_codecCtx->ch_layout;
    m_frame->nb_samples = m_codecCtx->frame_size;

    if (m_api->av_frame_get_buffer(m_frame, 0) < 0) {
        close();
        return geode::Err("Failed to allocate audio frame buffer");
    }
    return geode::Ok();
}

int AudioEncoder::frameSize() const {
    return m_codecCtx ? m_codecCtx->frame_size : 0;
}

geode::Result<> AudioEncoder::ensureResampler(int sourceRate,
                                              int sourceChannels) {
    if (m_swr && m_swrRate == sourceRate && m_swrChannels == sourceChannels)
        return geode::Ok();

    if (m_swr) m_api->swr_free(&m_swr);
    m_swr = nullptr;

    AVChannelLayout inLayout;
    m_api->av_channel_layout_default(&inLayout, sourceChannels);

    if (m_api->swr_alloc_set_opts2(&m_swr, &m_codecCtx->ch_layout,
                                   m_codecCtx->sample_fmt,
                                   m_codecCtx->sample_rate, &inLayout,
                                   AV_SAMPLE_FMT_FLT, sourceRate, 0,
                                   nullptr) < 0 ||
        !m_swr)
        return geode::Err("Failed to configure audio resampler");

    if (m_api->swr_init(m_swr) < 0) {
        m_api->swr_free(&m_swr);
        m_swr = nullptr;
        return geode::Err("Failed to initialise audio resampler");
    }

    m_swrRate = sourceRate;
    m_swrChannels = sourceChannels;
    return geode::Ok();
}

geode::Result<> AudioEncoder::submit(float const* samples, size_t sampleCount,
                                     int sourceRate, int sourceChannels,
                                     int64_t pts) {
    if (!m_codecCtx || !m_frame) return geode::Err("Audio encoder is closed");
    if (!samples || sampleCount == 0)
        return geode::Err("No audio samples supplied");
    if (sourceRate <= 0 || sourceChannels <= 0)
        return geode::Err("Invalid source audio format");

    GEODE_UNWRAP(ensureResampler(sourceRate, sourceChannels));

    if (m_api->av_frame_make_writable(m_frame) < 0)
        return geode::Err("Audio frame is not writable");

    auto const* input = reinterpret_cast<uint8_t const*>(samples);
    const int converted = m_api->swr_convert(
        m_swr, m_frame->data, m_codecCtx->frame_size, &input,
        static_cast<int>(sampleCount / sourceChannels));
    if (converted < 0) return geode::Err("Failed to resample audio");

    m_frame->pts = pts;
    m_frame->nb_samples = m_codecCtx->frame_size;

    const int ret = m_api->avcodec_send_frame(m_codecCtx, m_frame);
    if (ret < 0) return geode::Err("Failed to submit audio frame: {}", ret);
    return geode::Ok();
}

geode::Result<> AudioEncoder::drain(Muxer& muxer, int streamIndex) {
    if (!m_codecCtx || !m_packet) return geode::Err("Audio encoder is closed");

    while (true) {
        const int ret = m_api->avcodec_receive_packet(m_codecCtx, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return geode::Err("Failed to receive audio packet");

        auto written = muxer.write(m_packet, streamIndex, m_codecCtx->time_base);
        m_api->av_packet_unref(m_packet);
        if (written.isErr()) return written;
    }
    return geode::Ok();
}

geode::Result<> AudioEncoder::flush(Muxer& muxer, int streamIndex) {
    if (!m_codecCtx) return geode::Ok();
    m_api->avcodec_send_frame(m_codecCtx, nullptr);
    return drain(muxer, streamIndex);
}

void AudioEncoder::close() {
    if (m_swr) m_api->swr_free(&m_swr);
    if (m_frame) m_api->av_frame_free(&m_frame);
    if (m_packet) m_api->av_packet_free(&m_packet);
    if (m_codecCtx) m_api->avcodec_free_context(&m_codecCtx);
    m_swr = nullptr;
    m_frame = nullptr;
    m_packet = nullptr;
    m_codecCtx = nullptr;
}

}
