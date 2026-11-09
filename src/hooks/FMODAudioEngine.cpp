#include <Geode/Geode.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>

#include <algorithm>

#include "../render/dsp.hpp"
#include "../render/renderer.hpp"

using namespace geode::prelude;

namespace {

constexpr int AUDIO_FRAME = 1024;

bool fmodMayRun() {
    auto* audio = AudioRecorder::get();
    return !audio->m_attached || audio->m_shouldUpdateFmod;
}

FMOD_RESULT abortRender(const char* reason, FMOD_RESULT code) {
    geode::log::error("{}; stopping render", reason);
    Renderer::get()->signalStop();
    return code;
}

FMOD_RESULT flushFrames(AudioRecorder* audio) {
#ifdef GEODE_IS_MOBILE
    (void)audio;
    return FMOD_OK;
#else
    auto& pending = audio->m_buffer;
    const size_t frameSamples = static_cast<size_t>(AUDIO_FRAME) * audio->m_channels;
    const double frameSeconds = static_cast<double>(AUDIO_FRAME) / static_cast<double>(audio->m_sampleRate);
    size_t used = 0;
    bool failed = false;
    for (; pending.size() - used >= frameSamples; used += frameSamples) {
        const auto pts = static_cast<uint64_t>(audio->m_index * AUDIO_FRAME);
        if (Renderer::get()->writeAudio(pending.data() + used, pts).isErr()) {
            failed = true;
            break;
        }
        audio->m_time = static_cast<double>(audio->m_index++) * frameSeconds;
    }
    pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(used));
    return failed ? abortRender("Failed to write audio", FMOD_ERR_INTERNAL) : FMOD_OK;
#endif
}

FMOD_RESULT updateSystem(FMOD::System* system) {
    if (!fmodMayRun()) return FMOD_OK;

    auto* audio = AudioRecorder::get();
    if (!audio->m_attached) return system->update();
    if (audio->m_sampleRate <= 0 || audio->m_channels <= 0)
        return abortRender("Invalid renderer audio format", FMOD_ERR_INVALID_PARAM);

    unsigned int block = 0;
    int blocks = 0;
    if (system->getDSPBufferSize(&block, &blocks) != FMOD_OK || block == 0)
        return abortRender("Invalid FMOD DSP buffer", FMOD_ERR_INVALID_PARAM);

    const double behind = std::max(audio->m_fmodTime - audio->m_time, 0.0);
    const int needed = static_cast<int>(behind * audio->m_sampleRate);
    for (int mixed = 0; mixed < needed; mixed += block) {
        if (const FMOD_RESULT result = system->update(); result != FMOD_OK) {
            Renderer::get()->signalStop();
            return result;
        }
    }
    return flushFrames(audio);
}

FMOD_RESULT systemUpdateHook(FMOD::System* self) { return updateSystem(self); }

}

struct GrapeAudioEngine : Modify<GrapeAudioEngine, FMODAudioEngine> {
    void update(float dt) {
        if (!fmodMayRun()) return;
        AudioRecorder::get()->m_fmodTime += dt;
        FMODAudioEngine::update(dt);
    }
};

$execute {
    auto hooked = Mod::get()->hook(reinterpret_cast<void*>(geode::addresser::getNonVirtual(&FMOD::System::update)),
                                   &systemUpdateHook, "FMOD::System::update", tulip::hook::TulipConvention::Stdcall);
    if (!hooked) geode::log::error("Could not hook FMOD::System::update, rendered audio will be missing: {}",
                                   hooked.unwrapErr());
}
