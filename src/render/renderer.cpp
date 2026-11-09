#include "renderer.hpp"
#include "util/storage.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/ShaderLayer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>

#include "Geode/utils/random.hpp"
#include "Geode/utils/string.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "dsp.hpp"
#include "hooks/frame_fallback.hpp"
#include "vine/session.hpp"
#ifndef GEODE_IS_MOBILE
#include "ui/manager.hpp"
#endif

using namespace geode::prelude;

namespace fs = std::filesystem;

double Renderer::getEstimatedRemaining() const {
    return getLevelEstimatedRemaining();
}

double Renderer::getLevelEstimatedRemaining() const {
    const auto* playLayer = PlayLayer::get();
    if (!playLayer || !playLayer->m_level)
        return 0.0;

    const double progress = std::clamp(
        static_cast<double>(playLayer->m_gameState.m_currentProgress) / 100.0,
        0.0, 1.0);
    const double renderedLevelTime = m_frameCount * getDt();
    return progress > 0.0 ? renderedLevelTime * (1.0 - progress) / progress : 0.0;
}

void Renderer::saveSettings(fs::path& path) const {
    auto& settings = m_settings;

    (void)glz::write_file_json<glz::opts{.prettify = true}>(
        settings, grape::paths::pathToString(path), std::string{});
}

#ifndef GEODE_IS_IOS
static std::string getDefaultCodec() {
#ifdef GEODE_IS_WINDOWS
    struct Vendor {
        std::array<const char*, 2> m_pciTags;
        const char* m_name;
        const char* m_codec;
    };
    static constexpr Vendor vendors[] = {
        {{"VEN_10DE", nullptr}, "NVIDIA", "h264_nvenc"},
        {{"VEN_1002", "VEN_1022"}, "AMD", "h264_amf"},
        {{"VEN_8086", nullptr}, "Intel", "h264_qsv"},
    };

    HKEY devices = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Enum\\PCI", 0, KEY_READ, &devices) ==
        ERROR_SUCCESS) {
        char device[256];
        for (DWORD index = 0; index < 8192; ++index) {
            DWORD length = sizeof(device);
            if (RegEnumKeyExA(devices, index, device, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            const std::string_view id(device, length);
            for (const Vendor& vendor : vendors) {
                const bool matches = std::ranges::any_of(vendor.m_pciTags, [&](const char* tag) {
                    return tag && id.find(tag) != std::string_view::npos;
                });
                if (!matches) continue;
                RegCloseKey(devices);
                geode::log::info("Found an {} GPU, defaulting to {}", vendor.m_name, vendor.m_codec);
                return vendor.m_codec;
            }
        }
        RegCloseKey(devices);
    }
#endif
    geode::log::info("No hardware encoder vendor found, defaulting to libx264 (CPU)");
    return "libx264";
}
#endif

void Renderer::initializeDefaults() {
#ifdef GEODE_IS_IOS
    m_settings.m_fps = 240;
    m_settings.m_codec = "h264";
    m_settings.m_extension = "mp4";
#else
    m_settings.m_codec = getDefaultCodec();
#endif
}

void Renderer::loadFFmpeg() {
#ifdef GEODE_IS_MOBILE
    m_ffmpegLoaded = false;
#else
    if (auto loaded = m_av.load(); loaded.isErr()) {
        geode::log::warn("[RENDERER] {}", loaded.unwrapErr());
        m_ffmpegLoaded = false;
        return;
    }

    m_ffmpegLoaded = true;
#endif
}

std::vector<std::string> Renderer::detectEncoders() {
#ifdef GEODE_IS_MOBILE
    return {};
#else
    std::vector<std::string> detected;

    if (!m_av.loaded()) {
        geode::log::warn("detectEncoders: FFmpeg not loaded");
        return detected;
    }

    std::vector<std::string> candidates;
    for (const char* hardware : {"nvenc", "amf", "qsv"})
        for (const char* format : {"h264", "hevc", "av1"}) candidates.push_back(fmt::format("{}_{}", format, hardware));
    candidates.insert(candidates.end(), {"libx264", "libx265", "libaom-av1"});

    for (const std::string& name : candidates) {
        const AVCodec* codec = m_av.avcodec_find_encoder_by_name(name.c_str());
        if (!codec) {
            continue;
        }

        AVCodecContext* ctx = m_av.avcodec_alloc_context3(codec);
        if (!ctx) {
            continue;
        }

        AVPixelFormat pixFmt = AV_PIX_FMT_NONE;
        AVPixelFormat* outPixFmts = nullptr;
        int outPixFmtCount = 0;
        if (m_av.avcodec_get_supported_config(
                ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0,
                (const void**)&outPixFmts, &outPixFmtCount) >= 0 &&
            outPixFmts && outPixFmtCount > 0) {
            pixFmt = outPixFmts[0];
        } else {
            pixFmt = AV_PIX_FMT_YUV420P;
        }

        ctx->codec_id = codec->id;
        ctx->pix_fmt = pixFmt;
        ctx->width = 640;
        ctx->height = 480;
        ctx->bit_rate = 4'000'000;
        ctx->time_base = {1, 30};
        ctx->framerate = {30, 1};
        ctx->max_b_frames = 0;

        AVDictionary* opts = nullptr;
        const int ret = m_av.avcodec_open2(ctx, codec, &opts);
        if (opts) {
            m_av.av_dict_free(&opts);
        }

        if (ret >= 0) {
            detected.emplace_back(name);
            geode::log::info("detectEncoders: (+) {} supported", name);
        } else {
            geode::log::info("detectEncoders: (-) {} unsupported", name);
        }

        m_av.avcodec_free_context(&ctx);
    }

    GrapeSettings::get()->renderDetectedEncoders = detected;
    return detected;
#endif
}

#ifndef GEODE_IS_MOBILE
geode::Result<std::string> Renderer::resolveVideoCodec() {
    std::vector<std::string> candidates;
    const auto add = [&candidates](std::string name) {
        if (name.empty()) return;
        if (std::find(candidates.begin(), candidates.end(), name) !=
            candidates.end())
            return;
        candidates.push_back(std::move(name));
    };

    add(m_settings.m_codec);
    for (const auto& name : GrapeSettings::get()->renderDetectedEncoders)
        add(name);
    add(getDefaultCodec());
    add("libx264");

    if (candidates.empty())
        return geode::Err("No video encoder is configured");

    if (!m_av.loaded()) return geode::Ok(candidates.front());

    for (const auto& name : candidates) {
        if (m_av.avcodec_find_encoder_by_name(name.c_str()))
            return geode::Ok(name);
    }

    return geode::Err(
        "None of the configured video encoders are available ({}). Enable "
        "Force Codec in the Render tab to name one manually.",
        fmt::join(candidates, ", "));
}
#endif

#ifdef GEODE_IS_MOBILE
static void silentChangeSize(CCSize size);
static void resizeShaderLayer(CCSize size, CCSize original);
#endif

#if defined(GEODE_IS_MOBILE) && !defined(GEODE_IS_IOS)
namespace {
std::ofstream g_mobileAudioFile;
uint64_t g_mobileAudioSamples = 0;
int g_mobileAudioRate = 0;
int g_mobileAudioChannels = 0;

void wavWriteU32(std::ostream& file, uint32_t value) {
    const uint8_t bytes[4]{static_cast<uint8_t>(value & 0xff),
                           static_cast<uint8_t>((value >> 8) & 0xff),
                           static_cast<uint8_t>((value >> 16) & 0xff),
                           static_cast<uint8_t>((value >> 24) & 0xff)};
    file.write(reinterpret_cast<const char*>(bytes), 4);
}

void wavWriteU16(std::ostream& file, uint16_t value) {
    const uint8_t bytes[2]{static_cast<uint8_t>(value & 0xff),
                           static_cast<uint8_t>((value >> 8) & 0xff)};
    file.write(reinterpret_cast<const char*>(bytes), 2);
}

void closeWavFile() {
    if (g_mobileAudioFile.is_open()) g_mobileAudioFile.close();
    g_mobileAudioFile.clear();
    g_mobileAudioSamples = 0;
    g_mobileAudioRate = 0;
    g_mobileAudioChannels = 0;
}

bool openWavFile(std::filesystem::path const& path, int sampleRate,
                 int channels) {
    closeWavFile();
    if (sampleRate <= 0 || channels <= 0) return false;

    g_mobileAudioFile.open(path, std::ios::binary | std::ios::trunc);
    if (!g_mobileAudioFile) return false;

    g_mobileAudioFile.write("RIFF", 4);
    wavWriteU32(g_mobileAudioFile, 36);
    g_mobileAudioFile.write("WAVE", 4);
    g_mobileAudioFile.write("fmt ", 4);
    wavWriteU32(g_mobileAudioFile, 16);
    wavWriteU16(g_mobileAudioFile, 1);
    wavWriteU16(g_mobileAudioFile, static_cast<uint16_t>(channels));
    wavWriteU32(g_mobileAudioFile, static_cast<uint32_t>(sampleRate));
    wavWriteU32(g_mobileAudioFile, static_cast<uint32_t>(
                                       sampleRate * channels * 2));
    wavWriteU16(g_mobileAudioFile, static_cast<uint16_t>(channels * 2));
    wavWriteU16(g_mobileAudioFile, 16);
    g_mobileAudioFile.write("data", 4);
    wavWriteU32(g_mobileAudioFile, 0);

    if (!g_mobileAudioFile) {
        closeWavFile();
        return false;
    }
    g_mobileAudioRate = sampleRate;
    g_mobileAudioChannels = channels;
    return true;
}

void appendWavSamples(std::vector<float> const& pcm) {
    if (!g_mobileAudioFile.is_open() || pcm.empty()) return;

    std::vector<uint8_t> block(pcm.size() * 2);
    for (size_t i = 0; i < pcm.size(); ++i) {
        const auto sample = static_cast<int16_t>(
            std::lround(std::clamp(pcm[i], -1.0f, 1.0f) * 32767.0f));
        const auto raw = static_cast<uint16_t>(sample);
        block[i * 2] = static_cast<uint8_t>(raw & 0xff);
        block[i * 2 + 1] = static_cast<uint8_t>((raw >> 8) & 0xff);
    }
    g_mobileAudioFile.write(reinterpret_cast<const char*>(block.data()),
                            static_cast<std::streamsize>(block.size()));
    g_mobileAudioSamples += pcm.size();
}

bool finishWavFile() {
    if (!g_mobileAudioFile.is_open()) {
        closeWavFile();
        return false;
    }

    const auto dataBytes = static_cast<uint32_t>(
        std::min<uint64_t>(g_mobileAudioSamples * 2, 0xffffffffull - 36));
    g_mobileAudioFile.seekp(4, std::ios::beg);
    wavWriteU32(g_mobileAudioFile, 36 + dataBytes);
    g_mobileAudioFile.seekp(40, std::ios::beg);
    wavWriteU32(g_mobileAudioFile, dataBytes);
    const bool ok = static_cast<bool>(g_mobileAudioFile) && dataBytes > 0;

    closeWavFile();
    return ok;
}

double wavSecondsWritten() {
    if (g_mobileAudioRate <= 0 || g_mobileAudioChannels <= 0) return 0.0;
    return static_cast<double>(g_mobileAudioSamples) /
           (static_cast<double>(g_mobileAudioRate) * g_mobileAudioChannels);
}
}
#endif

#ifdef GEODE_IS_MOBILE
geode::Result<> Renderer::startMobile() {
    auto* pl = PlayLayer::get();
    if (!pl) return geode::Err("Open a level before starting a render");
    if (pl->m_hasCompletedLevel || pl->m_levelEndAnimationStarted) {
        return geode::Err("Restart the level before starting a render");
    }
#ifndef GEODE_IS_IOS
    if (!Loader::get()->isModLoaded("eclipse.ffmpeg-api")) {
        return geode::Err("Install and enable eclipse.ffmpeg-api to render on mobile");
    }
#endif
    if (m_mobileRecording) return geode::Err("A render is already running");
    if (m_mobileFinalizing)
        return geode::Err("Wait for the previous video to finish saving");

#ifdef GEODE_IS_IOS
    const auto resolutionIndex = iosRenderResolutionIndex(
        m_settings.m_width, m_settings.m_height);
    const auto& resolution = IOS_RENDER_RESOLUTIONS[resolutionIndex];
    m_settings.m_width = resolution.m_width;
    m_settings.m_height = resolution.m_height;
    m_settings.m_fps = 240;
#else
    m_settings.m_width = std::clamp(m_settings.m_width, 64, 3840);
    m_settings.m_height = std::clamp(m_settings.m_height, 64, 2160);
    m_settings.m_width += m_settings.m_width & 1;
    m_settings.m_height += m_settings.m_height & 1;
    m_settings.m_fps = std::clamp(m_settings.m_fps, 1, 240);
#endif
    m_settings.m_bitrate = std::clamp<uint32_t>(
        m_settings.m_bitrate, 1'000'000, 200'000'000);
    m_collectAudio = m_settings.m_renderAudio;
#ifndef GEODE_IS_IOS
    if (m_collectAudio && !mobileCanMixAudio()) {
        geode::log::warn(
            "FFmpeg API cannot mux audio; rendering without a sound track");
        m_collectAudio = false;
    }
#endif

    int sampleRate = 48000;
    int channels = 2;
    FMODAudioEngine::get()->m_system->getSoftwareFormat(
        &sampleRate, nullptr, &channels);

    std::string name = m_settings.m_outputPath.empty()
        ? "grape-mobile-render" : m_settings.m_outputPath;
    if (m_autoVideoName->inner()) {
        name = pl->m_level->m_levelName.empty()
            ? "grape-mobile-render" : pl->m_level->m_levelName;
    }
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*') c = '_';
    }
    std::string extension = m_settings.m_extension.empty()
        ? "mp4" : m_settings.m_extension;
#ifdef GEODE_IS_IOS
    extension = "mp4";
#endif
    if (extension.front() == '.') extension.erase(extension.begin());
    const auto legacyOutput = grape::paths::directory("videos") /
                              (name + "." + extension);
    auto output = legacyOutput;
#ifdef GEODE_IS_IOS
    const auto visibleDirectory = iosVisibleVideosDirectory();
    if (!visibleDirectory.empty()) {
        output = visibleDirectory / (name + "." + extension);
    }
    m_mobileMirrorOutputPath = legacyOutput;
#endif
    m_mobileOutputPath = output;

#ifdef GEODE_IS_IOS
    m_iosWriter = std::make_unique<IOSVideoWriter>();
    auto result = m_iosWriter->open(
        output, m_settings.m_width, m_settings.m_height,
        m_settings.m_fps, m_settings.m_bitrate, sampleRate, channels,
        m_collectAudio);
    if (result.isErr()) {
        m_iosWriter.reset();
        return result;
    }
#else
    m_mobileRecorder = std::make_unique<MobileFFmpegRecorder>();
    if (!m_mobileRecorder->valid()) {
        m_mobileRecorder.reset();
        return geode::Err("FFmpeg API recorder could not be created");
    }

    m_mobileVideoTempPath.clear();
    m_mobileAudioTempPath.clear();
    auto videoTarget = output;
    if (m_collectAudio) {
        m_mobileVideoTempPath =
            output.parent_path() / (name + ".video." + extension);
        m_mobileAudioTempPath = output.parent_path() / (name + ".audio.wav");
        videoTarget = m_mobileVideoTempPath;
    }

    ffmpeg::RenderSettings settings;
    settings.m_pixelFormat = ffmpeg::PixelFormat::RGB24;
    settings.m_codec = m_settings.m_codec;
    settings.m_bitrate = m_settings.m_bitrate;
    settings.m_width = static_cast<uint32_t>(m_settings.m_width);
    settings.m_height = static_cast<uint32_t>(m_settings.m_height);
    settings.m_fps = static_cast<uint16_t>(m_settings.m_fps);
    settings.m_outputFile = videoTarget;
    settings.m_doVerticalFlip = true;
    auto result = m_mobileRecorder->init(settings);
    if (result.isErr()) {
        m_mobileRecorder.reset();
        return result;
    }
#endif

    const auto abortStart = [this] {
#ifndef GEODE_IS_IOS
        const auto scratchVideo = m_mobileVideoTempPath;
        const auto scratchAudio = m_mobileAudioTempPath;
        m_mobileVideoTempPath.clear();
        m_mobileAudioTempPath.clear();
        closeWavFile();
#endif
        stopMobile();
#ifndef GEODE_IS_IOS
        std::error_code error;
        if (!scratchVideo.empty())
            std::filesystem::remove(scratchVideo, error);
        if (!scratchAudio.empty())
            std::filesystem::remove(scratchAudio, error);
#endif
    };

    std::vector<uint8_t> blank(
        static_cast<size_t>(m_settings.m_width) * m_settings.m_height * 4, 0);
    m_mobileTexture = new CCTexture2D();
    if (!m_mobileTexture->initWithData(
            blank.data(), kCCTexture2DPixelFormat_RGBA8888,
            m_settings.m_width, m_settings.m_height,
            {static_cast<float>(m_settings.m_width),
             static_cast<float>(m_settings.m_height)})) {
        m_mobileTexture->release();
        m_mobileTexture = nullptr;
#ifdef GEODE_IS_IOS
        (void)m_iosWriter->finish();
        m_iosWriter.reset();
#else
        abortStart();
#endif
        return geode::Err("Failed to allocate the mobile render texture");
    }

    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    glGenFramebuffers(1, &m_mobileFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_mobileFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_mobileTexture->getName(), 0);
    const auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        abortStart();
        return geode::Err("Mobile render framebuffer is incomplete");
    }

#ifndef GEODE_IS_IOS
    m_mobileFrame.resize(
        static_cast<size_t>(m_settings.m_width) * m_settings.m_height * 3);
#else
    m_mobileFrame.clear();
#endif
    m_mobileRGBAFrame.resize(
        static_cast<size_t>(m_settings.m_width) * m_settings.m_height * 4);

    auto* view = CCEGLView::sharedOpenGLView();
    m_mobileOriginalFrameSize = view->getFrameSize();
    const CCSize renderSize{static_cast<float>(m_settings.m_width),
                            static_cast<float>(m_settings.m_height)};
    if (auto* base = GJBaseGameLayer::get();
        base && base->m_shaderLayer && base->m_shaderLayer->m_renderTexture) {
        silentChangeSize(renderSize);
        resizeShaderLayer(renderSize, renderSize);
        silentChangeSize(m_mobileOriginalFrameSize);
        m_mobileShaderResized = true;
    }

    if (m_collectAudio) {
        auto* audio = AudioRecorder::get();
        if (!audio->init() ||
            !audio->attach(m_settings.m_musicVolume,
                           m_settings.m_sfxVolume)) {
            audio->uninit();
#ifdef GEODE_IS_IOS
            m_iosWriter.reset();
#endif
            abortStart();
            return geode::Err("Failed to initialize renderer audio");
        }
#ifndef GEODE_IS_IOS
        if (!openWavFile(m_mobileAudioTempPath, audio->m_sampleRate,
                         audio->m_channels)) {
            audio->detach();
            audio->uninit();
            abortStart();
            return geode::Err("Failed to create the audio scratch file");
        }
#endif
    }
    m_mobileNextFrameTime = 0.0;
    m_mobileArmFrame = GrapeEngine::get()->timeline().getFrame();
    m_mobileStartFrame = 0;
    m_mobileCaptureStarted = false;
    m_mobileSaveError.clear();
#ifdef GEODE_IS_IOS
    m_mobileCompletionClockStarted = false;
    m_mobileEndMenuReady = false;
#endif
    m_endTime = 0.0f;
    m_time = 0.0;
    m_mobileRecording = true;
    m_recording = true;
    geode::log::info("Mobile render started: {}", output);
    return geode::Ok();
}

void Renderer::stopMobile() {
    std::string saveError;
#ifdef GEODE_IS_IOS
    if (AudioRecorder::get()->m_attached) {
        auto& pending = AudioRecorder::get()->m_buffer;
        if (m_iosWriter && !pending.empty()) {
            auto audioResult = m_iosWriter->appendAudio(pending);
            if (audioResult.isErr()) {
                geode::log::error("iOS final audio write failed: {}",
                                  audioResult.unwrapErr());
            }
            if (audioResult.isOk() && audioResult.unwrap()) pending.clear();
        }
        AudioRecorder::get()->detach();
        AudioRecorder::get()->uninit();
    }
    if (m_iosWriter) {
        auto writer = std::shared_ptr<IOSVideoWriter>(m_iosWriter.release());
        const auto outputPath = m_mobileOutputPath;
        const auto mirrorPath = m_mobileMirrorOutputPath;
        m_mobileFinalizing = true;
        m_mobileSaveError = "Finalizing video...";
        std::thread([writer = std::move(writer), outputPath, mirrorPath] {
            auto result = writer->finish();
            std::string status;
            uintmax_t fileSize = 0;
            if (result.isErr()) {
                status = result.unwrapErr();
            } else {
                std::error_code fileError;
                fileSize = std::filesystem::file_size(outputPath, fileError);
                if (fileError || fileSize == 0) {
                    status = "The iOS video finished but is missing from Grape/videos";
                } else if (mirrorPath != outputPath) {
                    std::error_code directoryError;
                    std::filesystem::create_directories(
                        mirrorPath.parent_path(), directoryError);
                    std::error_code copyError;
                    std::filesystem::copy_file(
                        outputPath, mirrorPath,
                        std::filesystem::copy_options::overwrite_existing,
                        copyError);
                }
            }
            geode::queueInMainThread(
                [status = std::move(status), outputPath, fileSize] {
                    auto* renderer = Renderer::get();
                    renderer->m_mobileFinalizing = false;
                    if (status.empty()) {
                        renderer->m_mobileSaveError =
                            fmt::format("Render saved: {}",
                                        outputPath.filename().string());
                        geode::log::info(
                            "iOS render saved to {} ({} bytes)",
                            outputPath, fileSize);
                    } else {
                        renderer->m_mobileSaveError = status;
                        geode::log::error(
                            "iOS render finalize failed: {}", status);
                    }
                });
        }).detach();
    }
#else
    if (m_mobileRecorder) {
        m_mobileRecorder->stop();
        m_mobileRecorder.reset();
    }

    if (AudioRecorder::get()->m_attached) {
        auto* audio = AudioRecorder::get();
        appendWavSamples(audio->m_buffer);
        audio->m_buffer.clear();
        audio->detach();
        audio->uninit();
    }
    const bool audioUsable = finishWavFile();

    if (!m_mobileVideoTempPath.empty()) {
        const auto videoPath = m_mobileVideoTempPath;
        const auto audioPath = m_mobileAudioTempPath;
        const auto outputPath = m_mobileOutputPath;
        m_mobileVideoTempPath.clear();
        m_mobileAudioTempPath.clear();

        std::error_code existsError;
        if (!std::filesystem::exists(videoPath, existsError)) {
            std::error_code removeError;
            std::filesystem::remove(audioPath, removeError);
            saveError = "The render produced no video file";
        } else if (!audioUsable) {
            std::error_code renameError;
            std::filesystem::rename(videoPath, outputPath, renameError);
            if (renameError) {
                std::error_code copyError;
                std::filesystem::copy_file(
                    videoPath, outputPath,
                    std::filesystem::copy_options::overwrite_existing,
                    copyError);
                if (!copyError) {
                    std::error_code removeError;
                    std::filesystem::remove(videoPath, removeError);
                }
            }
            std::error_code audioRemoveError;
            std::filesystem::remove(audioPath, audioRemoveError);
            saveError = "Render saved without audio (nothing was captured)";
            geode::log::warn("Android render captured no audio samples");
        } else {
            m_mobileFinalizing = true;
            m_mobileSaveError = "Adding audio...";
            std::thread([videoPath, audioPath, outputPath] {
                std::string status;
                bool saved = false;
                auto result =
                    mobileMixVideoAudio(videoPath, audioPath, outputPath);
                if (result.isErr()) status = result.unwrapErr();
                else saved = true;

                std::error_code removeError;
                std::filesystem::remove(audioPath, removeError);
                if (saved) {
                    std::filesystem::remove(videoPath, removeError);
                } else {
                    std::error_code renameError;
                    std::filesystem::rename(videoPath, outputPath,
                                            renameError);
                }

                geode::queueInMainThread(
                    [status = std::move(status), outputPath] {
                        auto* renderer = Renderer::get();
                        renderer->m_mobileFinalizing = false;
                        if (status.empty()) {
                            renderer->m_mobileSaveError =
                                fmt::format("Render saved: {}",
                                            outputPath.filename().string());
                            geode::log::info("Android render saved to {}",
                                             outputPath);
                        } else {
                            renderer->m_mobileSaveError = fmt::format(
                                "Audio mux failed ({}), saved without audio",
                                status);
                            geode::log::error(
                                "Android audio mux failed: {}", status);
                        }
                    });
            }).detach();
        }
    }
#endif
    if (m_mobileFbo) {
        glDeleteFramebuffers(1, &m_mobileFbo);
        m_mobileFbo = 0;
    }
    if (m_mobileTexture) {
        m_mobileTexture->release();
        m_mobileTexture = nullptr;
    }
    m_mobileFrame.clear();
    m_mobileRGBAFrame.clear();
    auto* base = GJBaseGameLayer::get();
    if (m_mobileShaderResized && base && base->m_shaderLayer &&
        base->m_shaderLayer->m_renderTexture &&
        m_mobileOriginalFrameSize.width > 0.f) {
        silentChangeSize(m_mobileOriginalFrameSize);
        resizeShaderLayer(m_mobileOriginalFrameSize,
                          m_mobileOriginalFrameSize);
        m_mobileShaderResized = false;
    }
    m_mobileCaptureStarted = false;
    m_mobileRecording = false;
    m_recording = false;
    geode::log::info("Mobile render stopped");
    if (!saveError.empty()) m_mobileSaveError = std::move(saveError);
}

#ifdef GEODE_IS_IOS
void Renderer::notifyEndLevelMenuReady() {
    if (!m_mobileRecording) return;
    m_mobileEndMenuReady = true;
    m_mobileEndMenuReadyAt = std::chrono::steady_clock::now();
}
#endif

void Renderer::updateMobile(PlayLayer* pl) {
    if (!m_mobileRecording || !pl) return;

    const double tps = std::max(GrapeEngine::get()->timeline().getTps(), 1.0);
    const uint32_t currentFrame = GrapeEngine::get()->timeline().getFrame();
    if (!m_mobileCaptureStarted) {
        if (pl->m_isPaused || !pl->m_started) return;

        if (m_mobileArmFrame > 2 && currentFrame >= m_mobileArmFrame) return;

        m_mobileCaptureStarted = true;
        m_mobileStartFrame = currentFrame;
        m_mobileNextFrameTime = 0.0;
        if (AudioRecorder::get()->m_attached)
            AudioRecorder::get()->m_buffer.clear();
        geode::log::info("Mobile renderer capture started at frame {}",
                         currentFrame);
    }

    const double gameTime =
        static_cast<double>(currentFrame - m_mobileStartFrame) / tps;
    double captureTime = gameTime;
#ifdef GEODE_IS_IOS
    const auto now = std::chrono::steady_clock::now();
    if (pl->m_hasCompletedLevel) {
        if (!m_mobileCompletionClockStarted) {
            m_mobileCompletionClockStarted = true;
            m_mobileCompletionStartedAt = now;
            m_mobileCompletionTimeline =
                std::max(gameTime, m_mobileNextFrameTime);
        }
        const double completionElapsed =
            std::chrono::duration<double>(
                now - m_mobileCompletionStartedAt).count();
        captureTime = std::max(
            gameTime, m_mobileCompletionTimeline + completionElapsed);
    }
#endif
    if (captureTime + 1e-9 < m_mobileNextFrameTime) return;

    GLint previousFbo = 0;
    GLint viewport[4] = {};
    GLint scissorBox[4] = {};
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissorBox);

    auto* director = CCDirector::get();
    auto* view = CCEGLView::sharedOpenGLView();
    const CCSize originalSize = view->getFrameSize();
    const CCSize originalDesign = view->getDesignResolutionSize();
    const auto originalPolicy = view->m_eResolutionPolicy;
    const CCSize renderSize{static_cast<float>(m_settings.m_width),
                            static_cast<float>(m_settings.m_height)};
    view->CCEGLViewProtocol::setFrameSize(renderSize.width, renderSize.height);
    director->updateScreenScale(renderSize);
    view->setDesignResolutionSize(originalDesign.width, originalDesign.height,
                                  kResolutionExactFit);
    director->setViewport();
    director->setProjection(kCCDirectorProjection2D);

    glBindFramebuffer(GL_FRAMEBUFFER, m_mobileFbo);
    glViewport(0, 0, m_settings.m_width, m_settings.m_height);
    glDisable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const bool showCompleteScene =
        m_settings.m_showLevelComplete && pl->m_hasCompletedLevel;
    CCNode* pauseButton = pl->m_uiLayer
        ? pl->m_uiLayer->getChildByIDRecursive("pause-button")
        : nullptr;
    if (!pauseButton)
        pauseButton = pl->getChildByIDRecursive("pause-button");
    const bool pauseWasVisible = pauseButton && pauseButton->isVisible();
    if (pauseButton && !showCompleteScene) pauseButton->setVisible(false);
    if (!m_settings.m_renderOnlyLevel || showCompleteScene) {
        auto* scene = CCDirector::get()->m_pRunningScene;
        if (scene) scene->visit();
        else pl->visit();
    } else {
        pl->visit();
    }
    if (pauseButton && !showCompleteScene)
        pauseButton->setVisible(pauseWasVisible);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_settings.m_width, m_settings.m_height,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_mobileRGBAFrame.data());

#ifndef GEODE_IS_IOS
    const size_t pixels = static_cast<size_t>(m_settings.m_width) *
                          m_settings.m_height;
    for (size_t i = 0; i < pixels; ++i) {
        m_mobileFrame[i * 3]     = m_mobileRGBAFrame[i * 4];
        m_mobileFrame[i * 3 + 1] = m_mobileRGBAFrame[i * 4 + 1];
        m_mobileFrame[i * 3 + 2] = m_mobileRGBAFrame[i * 4 + 2];
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
    view->CCEGLViewProtocol::setFrameSize(originalSize.width,
                                           originalSize.height);
    director->updateScreenScale(originalSize);
    view->setDesignResolutionSize(originalDesign.width, originalDesign.height,
                                  originalPolicy);
    director->setViewport();
    director->setProjection(kCCDirectorProjection2D);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
    if (scissorEnabled) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);

#ifdef GEODE_IS_IOS
    if (m_collectAudio && AudioRecorder::get()->m_attached) {
        AudioRecorder::get()->unpause();
        auto& pcm = AudioRecorder::get()->m_buffer;
        if (!pcm.empty()) {
            auto audioResult = m_iosWriter->appendAudio(pcm);
            if (audioResult.isErr()) {
                geode::log::error("Mobile render audio failed: {}",
                                  audioResult.unwrapErr());
                stopMobile();
                return;
            }
            if (audioResult.unwrap()) pcm.clear();
        }
        AudioRecorder::get()->m_time = AudioRecorder::get()->m_fmodTime;
    }
#endif

    int writtenFrames = 0;
    const double frameDuration = 1.0 / m_settings.m_fps;
#ifdef GEODE_IS_IOS
    while (m_mobileNextFrameTime <= captureTime + 1e-9 &&
           writtenFrames < 32) {
        ++writtenFrames;
        m_mobileNextFrameTime += frameDuration;
    }
    if (writtenFrames > 0) {
        auto result = m_iosWriter->appendRGBA(
            m_mobileRGBAFrame, writtenFrames);
        if (result.isErr()) {
            geode::log::error("Mobile render frame failed: {}",
                              result.unwrapErr());
            stopMobile();
            return;
        }
    }
#else
    while (m_mobileNextFrameTime <= gameTime + 1e-9 &&
           writtenFrames < 32) {
        auto result = m_mobileRecorder->writeFrame(m_mobileFrame);
        if (result.isErr()) {
            geode::log::error("Mobile render frame failed: {}",
                              result.unwrapErr());
            stopMobile();
            return;
        }
        ++writtenFrames;
        m_mobileNextFrameTime += frameDuration;
    }
    if (writtenFrames == 32 && m_mobileNextFrameTime <= gameTime)
        m_mobileNextFrameTime = gameTime + frameDuration;

    if (m_collectAudio && writtenFrames > 0) {
        auto* audio = AudioRecorder::get();
        if (audio->m_attached) {
            for (int i = 0; i < writtenFrames; ++i) audio->unpause();
            const bool produced = !audio->m_buffer.empty();
            appendWavSamples(audio->m_buffer);
            audio->m_buffer.clear();
            audio->m_time = produced ? wavSecondsWritten() : audio->m_fmodTime;
        }
    }
#endif
    m_time = captureTime;

#ifdef GEODE_IS_IOS
    if (pl->m_hasCompletedLevel && m_mobileEndMenuReady) {
        m_endTime = static_cast<float>(
            std::chrono::duration<double>(
                now - m_mobileEndMenuReadyAt).count());
        const float menuHold = std::max(m_settings.m_afterEndTime, 2.0f);
        if (m_endTime >= menuHold) stopMobile();
    }
#else
    const bool endMenuShown = pl->getChildByID("EndLevelLayer") != nullptr;
    if (pl->m_hasCompletedLevel && endMenuShown) {
        m_endTime += static_cast<float>(writtenFrames * frameDuration);
        if (m_endTime >= m_settings.m_afterEndTime) stopMobile();
    }
#endif
}
#endif

fs::path Renderer::liveSettingsPath() {
    return grape::paths::file("render.json");
}

$on_mod(DataSaved) {
    auto path = Renderer::liveSettingsPath();
    Renderer::get()->saveSettings(path);
}

void Renderer::loadSettings(fs::path& path, bool asPreset) {
    if (!fs::exists(path)) {
        return;
    }

    auto settings = m_settings;
    auto ec = glz::read_file_json(
        settings, grape::paths::pathToString(path), std::string{});
    if (ec) {
        geode::log::error("Failed to read renderer settings: {}",
                          ec.custom_error_message);
        return;
    }

    m_settings = std::move(settings);
#ifndef GEODE_IS_MOBILE
    GrapeEngine::get()->ui().m_state.m_bitrate =
        m_settings.m_bitrate / 1000000.0;
#endif
    if (asPreset) GrapeSettings::get()->lastLoadedPreset = path.stem().string();
}

#ifdef GEODE_IS_MOBILE
static void silentChangeSize(CCSize size) {
    auto director = CCDirector::sharedDirector();
    auto view = CCEGLView::sharedOpenGLView();
    view->CCEGLViewProtocol::setFrameSize(size.width, size.height);
    director->updateScreenScale(size);
    director->setViewport();
    director->setProjection(kCCDirectorProjection2D);
    glViewport(0, 0, size.width, size.height);
}

static void resizeShaderLayer(CCSize size, CCSize original) {
    auto* director = CCDirector::get();
    ShaderLayer* shader = GJBaseGameLayer::get()->m_shaderLayer;
    const CCSize window = director->getWinSize();
    const float aspect = size.width / size.height;
    const float targetAspect = original.aspect();
    shader->m_screenSize = size;
    shader->m_scaleFactor = size.height / window.height;
    shader->m_aspectRatio = aspect;

    const float fittedWidth = size.width / aspect * targetAspect;
    const float fittedHeight = size.height / aspect * targetAspect;
    const float contentScale = director->getContentScaleFactor();
    director->setContentScaleFactor(1.0f);
    shader->m_renderTexture->release();
    shader->m_renderTexture = CCRenderTexture::create(fittedWidth, size.height, kCCTexture2DPixelFormat_RGBA8888);
    shader->m_renderTexture->retain();
    director->setContentScaleFactor(contentScale);

    shader->m_sprite->setTexture(shader->m_renderTexture->getSprite()->getTexture());
    shader->m_textureContentSize = shader->m_sprite->getTexture()->getContentSize();
    shader->m_targetTextureSize = size;
    shader->m_targetTextureSizeExtra = CCSize{0.0f, 0.0f};
    if (targetAspect > aspect) shader->m_targetTextureSizeExtra = CCSize{fittedWidth - size.width, 0.0f};
    if (targetAspect < aspect) shader->m_targetTextureSizeExtra = CCSize{0.0f, fittedHeight - size.height};

    const CCSize& extra = shader->m_targetTextureSizeExtra;
    shader->m_sprite->setTextureRect({-1.0f + extra.width, -1.0f + extra.height, size.width + 2.0f, size.height + 2.0f});
    shader->m_state.m_textureScaleX = size.width / window.width;
    shader->m_state.m_textureScaleY = size.height / window.height;
    geode::log::info("Shader layer fitted to {} (extra {})", size, extra);
}
#endif

geode::Result<> Renderer::start() {
    geode::log::info("Starting renderer");

#ifdef GEODE_IS_MOBILE
    return startMobile();
#else
    if (m_recording) return geode::Err("Renderer is already recording");
    if (!grape::frame::drawSceneHooked())
        geode::log::warn("CCDirector::drawScene is not hooked, recording after each buffer swap instead");

    auto* audioRecorder = AudioRecorder::get();
    if (!audioRecorder->refreshFormat()) {
        return geode::Err("FMOD returned an invalid audio format");
    }
    m_sampleRate = audioRecorder->m_sampleRate;
    m_channels = audioRecorder->m_channels;
    m_seenFrames = 0;

    std::string fileName;
    if (m_autoVideoName->inner()) {
        auto pl = PlayLayer::get();
        if (!pl) return geode::Err("No level is loaded");

        GJGameLevel* level = pl->m_level;
        const std::pair<const char*, std::string> fields[] = {
            {"%name%", level->m_levelName},
            {"%id%", std::to_string(level->m_levelID.value())},
            {"%creator%", level->m_creatorName},
            {"%rand%", std::to_string(geode::utils::random::generate(0, 999'999'999))},
        };
        std::string name(m_videoNameTemplate->inner());
        for (const auto& [placeholder, value] : fields) name = geode::utils::string::replace(name, placeholder, value);
        fileName = name + "." + m_settings.m_extension;
    } else {
        fileName = m_settings.m_outputPath + "." + m_settings.m_extension;
    }

    if (!m_av.loaded()) loadFFmpeg();

    GEODE_UNWRAP_INTO(const std::string videoCodec, resolveVideoCodec());
    if (videoCodec != m_settings.m_codec) {
        geode::log::warn("Encoder '{}' is unavailable; rendering with '{}'",
                         m_settings.m_codec, videoCodec);
        if (!m_settings.m_forceCodec) m_settings.m_codec = videoCodec;
    }

    grape::render::PcPipeline::Config config;
    config.m_outputPath =
        grape::paths::directory("videos") / std::filesystem::path(fileName);
    config.m_videoCodec = videoCodec;
    config.m_audioCodec = m_settings.m_audioCodec;
    config.m_extraArgs = m_settings.m_renderArgs;
    config.m_width = m_settings.m_width;
    config.m_height = m_settings.m_height;
    config.m_fps = m_settings.m_fps;
    config.m_bitrate = static_cast<int64_t>(m_settings.m_bitrate);
    config.m_pixelFormat = m_settings.m_pixFmt;
    config.m_audio = m_settings.m_renderAudio;
    config.m_sampleRate = m_sampleRate;
    config.m_channels = m_channels;

    if (!m_pipeline)
        m_pipeline = std::make_unique<grape::render::PcPipeline>();

    GEODE_UNWRAP(m_pipeline->start(config));

    if (m_settings.m_renderAudio) {
        if (!audioRecorder->init() ||
            !audioRecorder->attach(m_settings.m_musicVolume,
                                   m_settings.m_sfxVolume)) {
            audioRecorder->uninit();
            m_pipeline->shutdown();
            return geode::Err("Failed to initialize renderer audio");
        }
    }

    m_frameCount = 0;
    m_audioFrameCount = 0;
    m_audioIndex = 0;
    m_updateIndex = 0;
    m_endTime = 0.0f;
    m_time = 0.0;
    m_halting = false;
    m_collected = false;
    m_realStart = std::chrono::steady_clock::now();
    m_recording = true;
    unlockFrameRate();

    return geode::Ok();
#endif
}

#ifndef GEODE_IS_MOBILE
void Renderer::unlockFrameRate() {
    if (m_savedPacing) return;
    auto* director = CCDirector::get();
#ifdef GEODE_IS_MACOS
    
    
    
    m_savedPacing = SavedPacing{director->getAnimationInterval(), false};
    director->setAnimationInterval(1.0 / 10000.0);
#else
    auto* app = CCApplication::sharedApplication();
    m_savedPacing = SavedPacing{director->getAnimationInterval(), app->getVerticalSyncEnabled()};
    app->toggleVerticalSync(false);
    director->setAnimationInterval(1.0 / 10000.0);
#endif
}

void Renderer::restoreFrameRate() {
    if (!m_savedPacing) return;
    const SavedPacing saved = *std::exchange(m_savedPacing, std::nullopt);
    CCDirector::get()->setAnimationInterval(saved.m_interval);
#ifndef GEODE_IS_MACOS
    CCApplication::sharedApplication()->toggleVerticalSync(saved.m_vsync);
#endif
}
#endif

Renderer::~Renderer() {
#ifndef GEODE_IS_MOBILE
    if (m_pipeline) m_pipeline->shutdown();
#endif
}

geode::Result<> Renderer::writeAudio(const float* frame, uint64_t pts) {
#ifdef GEODE_IS_MOBILE
    (void)frame;
    (void)pts;
    return geode::Ok();
#else
    if (!m_pipeline) return geode::Ok();
    if (m_channels <= 0) return geode::Err("Audio has no channels");
    return m_pipeline->submitAudio(frame, static_cast<size_t>(1024) * m_channels, static_cast<int64_t>(pts));
#endif
}

geode::Result<> Renderer::stop() {
#ifdef GEODE_IS_MOBILE
    return geode::Ok();
#else
    if (!m_recording && !m_pipeline) return geode::Ok();

    m_recording = false;
    restoreFrameRate();
    geode::log::info("Detaching audio and stopping all streams...");
    AudioRecorder::get()->uninit();

    std::string error;
    if (m_pipeline) {
        m_pipeline->shutdown();
        error = m_pipeline->failure();
    }

    m_halting = false;
    m_collected = false;
    geode::log::info("Rendering has stopped. Cleaning up");

    if (!error.empty()) return geode::Err(std::move(error));
    return geode::Ok();
#endif
}

float Renderer::fadeAmount() const {
    const float elapsed =
        static_cast<float>(std::max(m_updateIndex - 1, 0)) * this->getDt();
    return this->fadePlan().amountAt(elapsed, m_endTime);
}

void Renderer::capture() {
#ifndef GEODE_IS_MOBILE
    if (!m_pipeline) return;
    const bool taken = m_pipeline->submitFrame(m_updateIndex - 1);
    m_frameCount = m_pipeline->framesEncoded();
    m_halting = false;

    if (!taken && !m_pipeline->running()) {
        geode::log::error("Recorder stopped early, finishing the file");
        this->signalStop();
    }
#endif
}

void Renderer::update(PlayLayer* pl) {
#ifdef GEODE_IS_MOBILE
    if (m_mobileRecording) {
        updateMobile(pl);
        return;
    }
#endif
    if (!this->isRecording()) return;

    bool started =
        pl->m_started || (m_settings.m_firstAttemptPause && m_seenFrames >= 2);
    m_seenFrames++;
    if (pl->m_isPaused || !started) return;
    if (m_halting || m_collected) return;
    m_halting = true;

    m_time = ++m_updateIndex * this->getDt();

    if (pl->m_hasCompletedLevel &&
        !GrapeEngine::get()->replay().m_head.peek()) {
        if (this->m_endTime >= this->tailLength()) {
            this->signalStop();
            return;
        } else {
            this->m_endTime += this->getDt();
        }
    }

    if (m_collectAudio) {
        AudioRecorder::get()->unpause();
    }

    this->setFade(this->fadeAmount());
    this->capture();
}
