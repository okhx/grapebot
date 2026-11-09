#include "engine.hpp"
#include "util/storage.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <array>
#include <filesystem>

#include "assist/autoclicker.hpp"
#include "assist/pathfinder.hpp"
#include "assist/cps.hpp"
#include "assist/hitboxes.hpp"
#include "clock.hpp"
#include "label/label.hpp"
#include "render/renderer.hpp"
#include "shared/value/value.hpp"
#include "trajectory/trajectory.hpp"
#include "vine/session.hpp"
#include "checkpoint/practice.hpp"
#ifndef GEODE_IS_MOBILE
#include "ui/manager.hpp"
#endif
#include "timeline.hpp"
#ifdef GEODE_IS_MOBILE
#include "ui/touch_overlay.hpp"
#endif

using namespace geode::prelude;

class GrapeEngine::Parts {
   public:
    FrameEngine m_timeline;
    GameScheduler m_ticker;
    grape::vine::ReplayDirector m_replay;
    grape::snap::PracticeKeeper m_keeper;
    TrajectoryManager m_trajectory;
    Autoclicker m_autoclicker;
    Pathfinder m_pathfinder;
    Hitboxes m_hitboxes;
    LabelManager m_labels;
    CPSCounter m_cps;
#ifndef GEODE_IS_MOBILE
    UIManager m_ui;
#endif
};

GrapeEngine::GrapeEngine() : m_parts(std::make_unique<Parts>()) {}
GrapeEngine::~GrapeEngine() = default;

FrameEngine& GrapeEngine::timeline() { return m_parts->m_timeline; }
GameScheduler& GrapeEngine::ticker() { return m_parts->m_ticker; }
grape::vine::ReplayDirector& GrapeEngine::replay() {
    return m_parts->m_replay;
}
grape::snap::PracticeKeeper& GrapeEngine::keeper() { return m_parts->m_keeper; }
TrajectoryManager& GrapeEngine::trajectory() {
    return m_parts->m_trajectory;
}
Autoclicker& GrapeEngine::autoclicker() { return m_parts->m_autoclicker; }
Pathfinder& GrapeEngine::pathfinder() { return m_parts->m_pathfinder; }
Hitboxes& GrapeEngine::hitboxes() { return m_parts->m_hitboxes; }
LabelManager& GrapeEngine::labels() { return m_parts->m_labels; }
CPSCounter& GrapeEngine::cps() { return m_parts->m_cps; }
#ifndef GEODE_IS_MOBILE
UIManager& GrapeEngine::ui() { return m_parts->m_ui; }
#endif

bool GrapeEngine::isRecording() { return m_parts->m_replay.isRecording(); }
bool GrapeEngine::isPlaying() { return m_parts->m_replay.isPlaying(); }

namespace {

void promoteUnitaryClickCycle(GrapeSettings& settings) {
    if (settings.autoclickerFrequency <= 1) {
        return;
    }
    if (settings.autoclickerHoldFrames != 1 ||
        settings.autoclickerReleaseFrames != 1) {
        return;
    }
    settings.autoclickerHoldFrames = settings.autoclickerFrequency;
    settings.autoclickerReleaseFrames = settings.autoclickerFrequency;
}

void syncClickerFromSettings(Autoclicker& clicker,
                             const GrapeSettings& settings) {
    clicker.m_holdFrames = std::max(1, settings.autoclickerHoldFrames);
    clicker.m_releaseFrames = std::max(1, settings.autoclickerReleaseFrames);
    clicker.m_p1ClicksPerTick =
        std::max(1, settings.autoclickerP1ClicksPerTick);
    clicker.m_p2HoldFrames = std::max(1, settings.autoclickerP2HoldFrames);
    clicker.m_p2ReleaseFrames =
        std::max(1, settings.autoclickerP2ReleaseFrames);
    clicker.m_p2ClicksPerTick =
        std::max(1, settings.autoclickerP2ClicksPerTick);
    clicker.m_syncPlayers = settings.autoclickerSyncPlayers;
    clicker.m_performSwifts = settings.autoclickerSwifts;
    clicker.m_movingGap = settings.autoclickerMovingGap;
    clicker.m_movingGapLookahead =
        std::clamp(settings.autoclickerMovingGapLookahead, 1, 30);
    clicker.m_player = static_cast<Autoclicker::PlayerToggle>(
        std::clamp(settings.autoclickerPlayer, 0, 2));
}

void syncFinderFromSettings(Pathfinder& finder,
                            const GrapeSettings& settings) {
    finder.m_proportional = settings.autoclickerProportional;
    finder.m_holdPct = settings.autoclickerHoldPct;
    finder.m_releasePct = settings.autoclickerReleasePct;
    finder.m_swiftPct = settings.autoclickerSwiftPct;
    finder.m_cycleFrames = std::max(2, settings.autoclickerCycleFrames);
    finder.m_stuckDeaths = std::max(1, settings.pathfinderStuckDeaths);
}

}

void GrapeEngine::settleAutoclickerSettings() {
    auto& settings = *GrapeSettings::get();

    settings.stepsToSave = std::max<uint32_t>(2, settings.stepsToSave);
    settings.autoclickerFrequency = std::max(1, settings.autoclickerFrequency);
    promoteUnitaryClickCycle(settings);
    settings.autoclickerHoldFrames =
        std::max(1, settings.autoclickerHoldFrames);
    settings.autoclickerReleaseFrames =
        std::max(1, settings.autoclickerReleaseFrames);

    syncClickerFromSettings(autoclicker(), settings);
    syncFinderFromSettings(pathfinder(), settings);
}

void GrapeEngine::readSettings() {
    const auto settingsPath = grape::paths::file("settings.json");
    auto& settings = *GrapeSettings::get();
    if (auto error = glz::read_file_json(
            settings, settingsPath.string(), std::string{})) {
        geode::log::error("Failed to read settings: {}",
                          glz::format_error(error, std::string{}));
    }

    settleAutoclickerSettings();

    timeline().m_noclipType = static_cast<FrameEngine::NoclipType>(
        std::clamp(settings.noclipPlayer, 0, 2));
#ifdef GEODE_IS_MOBILE
    settings.useAlternateHook = true;
#endif

    BindingManager::get()->readFromFile(grape::paths::file("keybinds.json"));
}

namespace {
bool applyNamedPreset(const std::string& name, Renderer* renderer) {
    auto presetPath = grape::paths::directory("presets") /
        (name + ".json");
    if (!std::filesystem::exists(presetPath)) {
        geode::log::error("Preset {} does not exist", name);
        return false;
    }

    geode::log::info("Loading preset {}", name);
    renderer->loadSettings(presetPath);
    return true;
}
}

void GrapeEngine::applyPreset() {
    auto* renderer = Renderer::get();
    renderer->initializeDefaults();

    const auto& name = GrapeSettings::get()->lastLoadedPreset;
    if (!name.empty() && applyNamedPreset(name, renderer)) {
#ifndef GEODE_IS_MOBILE
        ui().m_state.m_presetName = name;
#endif
    }

    auto livePath = Renderer::liveSettingsPath();
    renderer->loadSettings(livePath, false);
}

namespace {
void makeWorkspaceDir(const std::filesystem::path& root, const char* name) {
    std::error_code error;
    std::filesystem::create_directories(root / name, error);
    if (error) {
        geode::log::error("Could not create {}/{}: {}", root, name,
                          error.message());
    }
}
}

void GrapeEngine::bootstrapWorkspace() {
    const auto& root = grape::paths::dataRoot();
    static constexpr std::array directories = {
        "backups", "fonts", "libraries", "logs", "presets",
        "replays", "scripts", "videos"};

    for (const auto* name : directories) {
        makeWorkspaceDir(root, name);
    }
}

void GrapeEngine::initialize() {
    bootstrapWorkspace();

    if (geode::Mod* cbf =
            Loader::get()->getInstalledMod("syzzi.click_between_frames")) {
        
        
        cbf->setSettingValue("physics-bypass", false);
    }

    readSettings();
    applyPreset();

    m_enabled->handle([&](bool& enabled) { settleEnabled(enabled); });

    replay().m_autosaveInterval->notifyChange();

    m_booted = true;

    m_enabled->notifyChange();
}

#ifdef GEODE_IS_MOBILE
namespace {
void windDownMobileSystems(GrapeEngine& engine) {
    FrameEngine& updater = engine.timeline();
    PlayLayer* playLayer = PlayLayer::get();

    engine.replay().setMode(grape::vine::RunState::Idle);
    if (Renderer::get()->isRecording()) {
        Renderer::get()->signalStop();
    }

    updater.setPaused(false);
    updater.m_stepOnce->inner() = false;
    updater.m_stepBackwards->inner() = false;
    updater.m_tpsOverflow = 0.0;
    updater.m_tps->inner() = 240.0;
    updater.m_tps->notifyChange();
    updater.m_speedhack->inner() = 1.0;
    updater.m_speedhack->notifyChange();

    engine.autoclicker().reset();
    engine.labels().update(true);
    if (playLayer) {
        if (auto* overlay = TouchOverlay::get()) {
            overlay->hide();
        }
    }

    if (engine.trajectory().exists()) {
        engine.trajectory().uninit();
    }
    engine.hitboxes().clearTrail();
    engine.hitboxes().destroy();
}

void bringUpMobileSystems(GrapeEngine& engine, PlayLayer* playLayer) {
    if (!playLayer) return;

    if (!engine.trajectory().exists()) {
        engine.trajectory().init(playLayer);
    }
    if (!engine.hitboxes().m_initialized) {
        engine.hitboxes().init(playLayer);
    }
    engine.labels().m_requiresRefresh = true;
}
}
#endif

void GrapeEngine::settleEnabled(bool& enabled) {
#ifdef GEODE_IS_MOBILE
    PlayLayer* playLayer = PlayLayer::get();

    if (!enabled) {
        windDownMobileSystems(*this);
    } else {
        bringUpMobileSystems(*this, playLayer);
    }
#else
    if (PlayLayer::get()) {
        enabled = true;
        return;
    }

    if (!enabled) {
        timeline().m_tps->inner() = 240.0;
        timeline().m_tps->notifyChange();
    }

    setHooksActive(enabled);
#endif

    if (enabled) {
        geode::log::info("Successfully enabled Grape.");
    } else {
        geode::log::info("Successfully disabled Grape.");
    }
}

#ifndef GEODE_IS_MOBILE
void GrapeEngine::setHooksActive(const bool enabled) {
    for (Patch* patch : Mod::get()->getPatches()) {
        geode::log::info("{} patch at 0x{:x}",
                         enabled ? "Enabling" : "Disabling",
                         patch->getAddress());
        (void)(enabled ? patch->enable() : patch->disable());
    }

    for (Hook* hook : Mod::get()->getHooks()) {
        if (hook->getDisplayName() == "cocos2d::CCEGLView::swapBuffers") {
            continue;
        }
        (void)(enabled ? hook->enable() : hook->disable());
    }
}
#endif

bool GrapeEngine::isEnabled() {
    return m_enabled->inner();

}
