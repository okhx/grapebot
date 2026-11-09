#ifndef RENDERER_HPP
#define RENDERER_HPP

#include "../config/config.hpp"
#include "../shared/value/value.hpp"
#include "ffmpeg.hpp"
#ifdef GEODE_IS_IOS
#include "ios_video_writer.hpp"
#elif defined(GEODE_IS_MOBILE)
#include "mobile_ffmpeg_api.hpp"
#else
#include "pc_pipeline.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <optional>
#include <array>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <glaze/glaze.hpp>
#include <memory>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include "dsp.hpp"
#include "fade.hpp"

#ifdef GEODE_IS_MOBILE
struct IOSRenderResolution {
    const char* m_name;
    int m_width;
    int m_height;
};

inline constexpr std::array<IOSRenderResolution, 4> IOS_RENDER_RESOLUTIONS{{
    {"720p", 1480, 720},
    {"1080p", 2320, 1080},
    {"1440p", 3060, 1440},
    {"2160p", 3840, 1760},
}};

inline size_t iosRenderResolutionIndex(int width, int height) {
    for (size_t index = 0; index < IOS_RENDER_RESOLUTIONS.size(); ++index) {
        const auto& resolution = IOS_RENDER_RESOLUTIONS[index];
        if (width == resolution.m_width && height == resolution.m_height)
            return index;
    }
    return 0;
}
#endif

struct RendererSettings {
#ifdef GEODE_IS_IOS
    int m_width = 1480;
    int m_height = 720;
#else
    int m_width = 3840;
    int m_height = 2160;
#endif
    uint32_t m_bitrate = 68'000'000;
    std::string m_codec = "";
    AVPixelFormat m_pixFmt = AV_PIX_FMT_NV12;

#ifdef GEODE_IS_IOS
    int m_fps = 240;
#else
    int m_fps = 60;
#endif

    float m_afterEndTime = 5.0f;
    float m_fadeIn = 0.0f;
    float m_fadeOut = 0.0f;
    bool m_colorFix = true;

    std::string m_outputPath = "output";
    std::string m_extension = "mp4";
    std::string m_audioCodec = "aac";

    std::string m_renderArgs = "";

    double m_musicVolume = 1.0;
    double m_sfxVolume = 1.0;

    bool m_firstAttemptPause = false;
    bool m_renderOnlyLevel = true;
    bool m_showLevelComplete = true;
    bool m_renderAudio = true;

    bool m_forceCodec = false;
};

template <>
struct glz::meta<RendererSettings> {
    using T = RendererSettings;
    static constexpr auto value = glz::object(
        "width", &T::m_width, "height", &T::m_height, "bitrate", &T::m_bitrate,
        "codec", &T::m_codec, "pix_fmt", &T::m_pixFmt, "fps", &T::m_fps,
        "after_end_time", &T::m_afterEndTime,
        "fade_in", &T::m_fadeIn, "fade_out", &T::m_fadeOut,
        "color_fix", &T::m_colorFix,
        "extension", &T::m_extension,
        "audio_codec", &T::m_audioCodec,
        "render_args", &T::m_renderArgs, "output_path", hide{&T::m_outputPath},
        "music_volume", &T::m_musicVolume, "sfx_volume", &T::m_sfxVolume,
        "record_paused", &T::m_firstAttemptPause,
        "render_only_level", &T::m_renderOnlyLevel,
        "show_level_complete", &T::m_showLevelComplete,
        "render_audio", &T::m_renderAudio,
        "force_codec", &T::m_forceCodec);
};

class Renderer {
   public:
    ~Renderer();

    void queueStart() { m_shouldStart = true; }
    void startIfQueued() {
        if (m_shouldStart) {
            m_shouldStart = false;
            auto ret = start();
            if (ret.isErr()) {
                geode::log::error("Failed to start recording: {}",
                                  ret.unwrapErr());
                geode::Notification::create(ret.unwrapErr(), geode::NotificationIcon::Error, 6.0f)->show();
            }
        }
    }
    geode::Result<> start();
    geode::Result<> writeAudio(const float* frame, uint64_t pts);
    geode::Result<> stop();

    void signalStop() {
#ifdef GEODE_IS_MOBILE
        if (m_mobileRecording) {
            stopMobile();
            return;
        }
#endif
        m_recording = false;
#ifndef GEODE_IS_MOBILE
        auto ret = stop();
        if (ret.isErr()) {
            geode::log::error("Failed to stop recording: {}", ret.unwrapErr());
        }
#endif
    }

    void capture();
    void update(PlayLayer* pl);

    void displayPreview() {
#ifndef GEODE_IS_MOBILE
        if (m_pipeline) m_pipeline->displayPreview();
#endif
    }

    bool canCapture() const {
#ifdef GEODE_IS_MOBILE
        return true;
#else
        return m_pipeline ? m_pipeline->canCapture() : true;
#endif
    }

    void setFade(float amount) {
#ifndef GEODE_IS_MOBILE
        if (m_pipeline) m_pipeline->setFade(amount);
#else
        (void)amount;
#endif
    }

    float fadeAmount() const;

    grape::render::FadePlan fadePlan() const {
        return {m_settings.m_fadeIn, m_settings.m_fadeOut,
                m_settings.m_afterEndTime};
    }

    float tailLength() const { return this->fadePlan().tail(); }

    int sceneFramebuffer() const {
#ifdef GEODE_IS_MOBILE
        return 0;
#else
        return m_pipeline ? m_pipeline->sceneFramebuffer() : 0;
#endif
    }

    RendererSettings m_settings;

    static Renderer* get() {
        static Renderer instance;

        return &instance;
    }

    float getDt() const { return 1. / m_settings.m_fps; }
    bool isRecording() const { return m_recording; }
    double getTime() const {
        return m_recording ? std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_realStart).count() : m_time;
    }
    int getFrameCount() const { return m_frameCount; }
    float getFramesPerMinute() const {
        const auto elapsed = getTime();
        return elapsed > 0.0 ? static_cast<float>(m_frameCount * 60.0 / elapsed) : 0.0f;
    }
    double getEstimatedRemaining() const;
    double getLevelEstimatedRemaining() const;
    inline bool isFFmpegLoaded() const { return m_ffmpegLoaded; }
    const std::string& getMobileSaveError() const {
        return m_mobileSaveError;
    }

    void loadSettings(std::filesystem::path& path, bool asPreset = true);
    static std::filesystem::path liveSettingsPath();
    void saveSettings(std::filesystem::path& path) const;
    void initializeDefaults();

    std::vector<std::string> detectEncoders();

#ifndef GEODE_IS_MOBILE
    geode::Result<std::string> resolveVideoCodec();
#endif
    void loadFFmpeg();

    bool m_shouldStart = false;
    bool m_collectAudio = true;

#ifdef GEODE_IS_MOBILE
    geode::Result<> startMobile();
    void stopMobile();
    void updateMobile(PlayLayer* pl);
#ifdef GEODE_IS_IOS
    void notifyEndLevelMenuReady();
#endif
#endif

    std::atomic<bool> m_halting = false;
    std::atomic<bool> m_collected = false;

    ConfigValuePtr<bool> m_autoVideoName = ConfigValue<bool>::create(
        "renderer.auto_video_name", &GrapeSettings::get()->automaticVideoName);
    ConfigValuePtr<std::string> m_videoNameTemplate = ConfigValue<std::string>::create(
        "renderer.video_name_template", &GrapeSettings::get()->videoNameTemplate);
    ConfigValuePtr<bool> m_firstAttemptPause = ConfigValue<bool>::create(
        "render.first_attempt_pause", &m_settings.m_firstAttemptPause);
    ConfigValuePtr<bool> m_renderLabels = ConfigValue<bool>::create(
        "render.labels_while_recording",
        &GrapeSettings::get()->renderLabelsWhileRecording);
    ConfigValuePtr<bool> m_renderDebug = ConfigValue<bool>::create(
        "render.debug", &GrapeSettings::get()->renderDebug);

    double m_time = 0;

   private:
#ifndef GEODE_IS_MOBILE
    struct SavedPacing {
        double m_interval;
        bool m_vsync;
    };

    void unlockFrameRate();
    void restoreFrameRate();

    std::optional<SavedPacing> m_savedPacing;
    std::unique_ptr<grape::render::PcPipeline> m_pipeline;
    grape::render::AvApi m_av;
#endif
    std::chrono::steady_clock::time_point m_realStart{};
    std::atomic<bool> m_recording = false;

    int m_frameCount = 0;
    int m_audioFrameCount = 0;
    int m_audioIndex = 0;

    int m_updateIndex = 0;
    int m_seenFrames = 0;
    float m_endTime = 0;

    int m_channels;
    int m_sampleRate;

    bool m_ffmpegLoaded = false;

    std::mutex m_lock;
    std::string m_mobileSaveError;

    friend class AudioRecorder;

#ifdef GEODE_IS_MOBILE
#ifdef GEODE_IS_IOS
    std::unique_ptr<IOSVideoWriter> m_iosWriter;
#else
    std::unique_ptr<MobileFFmpegRecorder> m_mobileRecorder;
#endif
    cocos2d::CCTexture2D* m_mobileTexture = nullptr;
    GLuint m_mobileFbo = 0;
    std::vector<uint8_t> m_mobileFrame;
    std::vector<uint8_t> m_mobileRGBAFrame;
    double m_mobileNextFrameTime = 0.0;
#ifdef GEODE_IS_IOS
    std::chrono::steady_clock::time_point m_mobileCompletionStartedAt{};
    std::chrono::steady_clock::time_point m_mobileEndMenuReadyAt{};
    double m_mobileCompletionTimeline = 0.0;
    bool m_mobileCompletionClockStarted = false;
    bool m_mobileEndMenuReady = false;
#endif
    bool m_mobileFinalizing = false;
    uint32_t m_mobileArmFrame = 0;
    uint32_t m_mobileStartFrame = 0;
    cocos2d::CCSize m_mobileOriginalFrameSize{};
    std::filesystem::path m_mobileOutputPath;
#ifdef GEODE_IS_IOS
    std::filesystem::path m_mobileMirrorOutputPath;
#else
    std::filesystem::path m_mobileVideoTempPath;
    std::filesystem::path m_mobileAudioTempPath;
#endif
    bool m_mobileCaptureStarted = false;
    bool m_mobileShaderResized = false;
    bool m_mobileRecording = false;
#endif
};

#endif
