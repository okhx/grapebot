#ifndef RECORDER_DSP_HPP
#define RECORDER_DSP_HPP

#include <Geode/Geode.hpp>

#include <atomic>
#include <memory>
#include <vector>

#include "config/config.hpp"

class AudioRecorder {
   public:
    static AudioRecorder* get() {
        static AudioRecorder recorder;
        return &recorder;
    }

    ~AudioRecorder();

    bool init();
    bool refreshFormat();
    bool attach(double musicVolume, double sfxVolume);
    void detach();
    void uninit();
    void unpause();

    ConfigValuePtr<bool> m_audioPreview =
        ConfigValue<bool>::create("audio.preview", &GrapeSettings::get()->previewAudio);
    bool m_attached = false;
    std::atomic_bool m_shouldUpdateFmod = false;

    double m_fmodTime = 0.0;
    double m_time = 0.0;
    uint32_t m_index = 0;
    int m_sampleRate = 0;
    int m_channels = 0;
    std::vector<float> m_buffer;

   private:
    class Preview;

    AudioRecorder();

    static FMOD_RESULT F_CALLBACK capture(FMOD_DSP_STATE*, float* in, float* out, unsigned int frames,
                                          int inChannels, int* outChannels);
    void resetClock();
    void startPreview();
    void stopPreview();

    FMOD::ChannelGroup* m_master = nullptr;
    FMOD::DSP* m_dsp = nullptr;
    float m_musicVolumeBefore = 0.0f;
    float m_sfxVolumeBefore = 0.0f;
    std::unique_ptr<Preview> m_preview;
};

#endif
