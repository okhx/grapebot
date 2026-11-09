#include "dsp.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/fmod/fmod.hpp>

#include <algorithm>
#include <cstring>

#include "engine/engine.hpp"
#include "renderer.hpp"

namespace {

FMOD::System* gameSystem() {
    auto* engine = FMODAudioEngine::get();
    return engine ? engine->m_system : nullptr;
}

class SampleRing {
   public:
    explicit SampleRing(size_t capacity) : m_samples(std::max<size_t>(capacity, 1)) {}

    void push(const float* from, size_t count) {
        const size_t written = m_written.load(std::memory_order_relaxed);
        const size_t size = m_samples.size();
        count = std::min(count, size - (written - m_read.load(std::memory_order_acquire)));
        const size_t at = written % size;
        const size_t untilWrap = std::min(count, size - at);
        std::copy_n(from, untilWrap, m_samples.begin() + at);
        std::copy_n(from + untilWrap, count - untilWrap, m_samples.begin());
        m_written.store(written + count, std::memory_order_release);
    }

    size_t pop(float* to, size_t count, float gain) {
        const size_t read = m_read.load(std::memory_order_relaxed);
        const size_t size = m_samples.size();
        count = std::min(count, m_written.load(std::memory_order_acquire) - read);
        const size_t at = read % size;
        const size_t untilWrap = std::min(count, size - at);
        const auto scaled = [gain](float sample) { return sample * gain; };
        std::transform(m_samples.begin() + at, m_samples.begin() + at + untilWrap, to, scaled);
        std::transform(m_samples.begin(), m_samples.begin() + (count - untilWrap), to + untilWrap, scaled);
        m_read.store(read + count, std::memory_order_release);
        return count;
    }

   private:
    std::vector<float> m_samples;
    std::atomic<size_t> m_written{0};
    std::atomic<size_t> m_read{0};
};

}

class AudioRecorder::Preview {
   public:
    Preview(int sampleRate, int channels, float gain)
        : m_ring(static_cast<size_t>(sampleRate) * channels / 2), m_gain(gain) {}

    ~Preview() {
        s_live.store(nullptr, std::memory_order_release);
        if (m_channel) m_channel->stop();
        if (m_sound) m_sound->release();
        if (m_system) {
            m_system->close();
            m_system->release();
        }
    }

    bool open(int sampleRate, int channels) {
        if (FMOD::System_Create(&m_system) != FMOD_OK || !m_system) {
            m_system = nullptr;
            return fail("create an FMOD system");
        }
        m_system->setSoftwareFormat(sampleRate, FMOD_SPEAKERMODE_DEFAULT, 0);
        if (m_system->init(32, FMOD_INIT_NORMAL, nullptr) != FMOD_OK) return fail("start the FMOD system");

        FMOD_CREATESOUNDEXINFO stream{};
        stream.cbsize = sizeof(stream);
        stream.numchannels = channels;
        stream.defaultfrequency = sampleRate;
        stream.format = FMOD_SOUND_FORMAT_PCMFLOAT;
        stream.decodebuffersize = 1024;
        stream.length = static_cast<unsigned int>(sampleRate) * channels * sizeof(float);
        stream.pcmreadcallback = &Preview::fill;
        s_live.store(this, std::memory_order_release);
        if (m_system->createSound(nullptr, FMOD_OPENUSER | FMOD_CREATESTREAM | FMOD_LOOP_NORMAL, &stream, &m_sound) !=
            FMOD_OK)
            return fail("open the preview stream");

        m_system->playSound(m_sound, nullptr, false, &m_channel);
        geode::log::info("Audio preview running at {} Hz, {} channels", sampleRate, channels);
        return true;
    }

    void feed(const float* samples, size_t count) { m_ring.push(samples, count); }
    void update() { m_system->update(); }

   private:
    static FMOD_RESULT F_CALLBACK fill(FMOD_SOUND*, void* data, unsigned int bytes) {
        auto* out = static_cast<float*>(data);
        const size_t wanted = bytes / sizeof(float);
        Preview* live = s_live.load(std::memory_order_acquire);
        const size_t got = live ? live->m_ring.pop(out, wanted, live->m_gain) : 0;
        std::fill(out + got, out + wanted, 0.0f);
        return FMOD_OK;
    }

    static bool fail(const char* what) {
        geode::log::warn("Audio preview could not {}", what);
        return false;
    }

    inline static std::atomic<Preview*> s_live = nullptr;

    FMOD::System* m_system = nullptr;
    FMOD::Sound* m_sound = nullptr;
    FMOD::Channel* m_channel = nullptr;
    SampleRing m_ring;
    float m_gain;
};

AudioRecorder::AudioRecorder() = default;
AudioRecorder::~AudioRecorder() = default;

FMOD_RESULT F_CALLBACK AudioRecorder::capture(FMOD_DSP_STATE*, float* in, float* out, unsigned int frames,
                                              int inChannels, int* outChannels) {
    auto* recorder = AudioRecorder::get();
    if (!recorder->m_shouldUpdateFmod) return FMOD_OK;

    const size_t samples = static_cast<size_t>(frames) * inChannels;
    if (in && samples) {
        auto& pending = recorder->m_buffer;
        const size_t at = pending.size();
        pending.resize(at + samples);
        std::transform(in, in + samples, pending.begin() + at,
                       [](float sample) { return std::clamp(sample, -1.0f, 1.0f); });
        if (recorder->m_preview && recorder->m_audioPreview->inner()) recorder->m_preview->feed(in, samples);
    }

    const int outputChannels = outChannels ? *outChannels : inChannels;
    if (out && outputChannels > 0) std::fill_n(out, static_cast<size_t>(frames) * outputChannels, 0.0f);
    return FMOD_OK;
}

bool AudioRecorder::init() {
    uninit();

    FMOD::System* system = gameSystem();
    if (!system || system->getMasterChannelGroup(&m_master) != FMOD_OK || !m_master) {
        geode::log::error("Cannot initialize renderer audio: no FMOD master");
        m_master = nullptr;
        return false;
    }

    FMOD_DSP_DESCRIPTION tap{};
    std::strncpy(tap.name, "Grape capture", sizeof(tap.name) - 1);
    tap.version = 0x00020000;
    tap.numinputbuffers = 1;
    tap.numoutputbuffers = 1;
    tap.read = &AudioRecorder::capture;
    if (system->createDSP(&tap, &m_dsp) != FMOD_OK || !m_dsp) {
        geode::log::error("Cannot initialize renderer audio DSP");
        uninit();
        return false;
    }
    if (system->setDSPBufferSize(1024, 2) != FMOD_OK) geode::log::warn("Could not set renderer audio DSP buffer size");

    resetClock();
    return true;
}

void AudioRecorder::resetClock() {
    m_time = m_fmodTime = 0.0;
    m_index = 0;
    m_buffer.clear();
}

void AudioRecorder::uninit() {
    detach();
    if (m_dsp) m_dsp->release();
    m_dsp = nullptr;
    m_master = nullptr;
}

bool AudioRecorder::refreshFormat() {
    FMOD::System* system = gameSystem();
    if (!system) return false;

    int sampleRate = 0;
    int channels = 0;
    FMOD_SPEAKERMODE mode = FMOD_SPEAKERMODE_DEFAULT;
    if (system->getSoftwareFormat(&sampleRate, &mode, &channels) != FMOD_OK) return false;
    if (mode != FMOD_SPEAKERMODE_RAW && system->getSpeakerModeChannels(mode, &channels) != FMOD_OK) return false;
    if (sampleRate <= 0 || channels <= 0) return false;

    m_sampleRate = sampleRate;
    m_channels = channels;
    return true;
}

bool AudioRecorder::attach(double musicVolume, double sfxVolume) {
    if (!m_master || !m_dsp) {
        geode::log::error("Cannot attach renderer audio: DSP is unavailable");
        return false;
    }
    if (!refreshFormat()) {
        geode::log::error("Cannot attach renderer audio: invalid FMOD format");
        return false;
    }

    auto* engine = FMODAudioEngine::get();
    if (!engine || !engine->m_system) return false;

    int tail = 0;
    if (m_master->getNumDSPs(&tail) != FMOD_OK || m_master->addDSP(tail, m_dsp) != FMOD_OK) {
        geode::log::error("Cannot attach renderer audio DSP");
        return false;
    }
    m_attached = true;
    m_dsp->setMeteringEnabled(true, false);

    m_musicVolumeBefore = engine->getBackgroundMusicVolume();
    
    m_sfxVolumeBefore = engine->m_sfxVolume;
    m_master->setPaused(false);
    engine->setEffectsVolume(sfxVolume);
    engine->setBackgroundMusicVolume(musicVolume);

    if (engine->m_system->setOutput(FMOD_OUTPUTTYPE_NOSOUND_NRT) != FMOD_OK) {
        geode::log::error("Cannot attach renderer audio: NRT output failed");
        detach();
        return false;
    }

    if (m_audioPreview->inner()) startPreview();
    return true;
}

void AudioRecorder::detach() {
    stopPreview();
    if (!m_attached) return;

    if (m_master) {
        if (m_dsp) m_master->removeDSP(m_dsp);
        m_master->setPaused(false);
    }
    if (auto* engine = FMODAudioEngine::get()) {
        engine->setEffectsVolume(m_sfxVolumeBefore);
        engine->setBackgroundMusicVolume(m_musicVolumeBefore);
        if (engine->m_system) engine->m_system->setOutput(FMOD_OUTPUTTYPE_AUTODETECT);
    }
    m_attached = false;
}

void AudioRecorder::unpause() {
    auto* engine = FMODAudioEngine::get();
    if (!m_attached || !engine) return;

    m_shouldUpdateFmod = true;
    engine->update(Renderer::get()->getDt());
    m_shouldUpdateFmod = false;

    if (m_preview) m_preview->update();
}

void AudioRecorder::startPreview() {
    if (m_preview) return;
    auto preview = std::make_unique<Preview>(m_sampleRate, m_channels, m_musicVolumeBefore);
    if (preview->open(m_sampleRate, m_channels)) m_preview = std::move(preview);
}

void AudioRecorder::stopPreview() { m_preview.reset(); }
