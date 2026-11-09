#ifndef GRAPE_ENGINE_HPP
#define GRAPE_ENGINE_HPP

#include <memory>

#include <config/config.hpp>
#include <shared/value/value.hpp>

#if defined(GRAPE_DEV_MODE) || defined(SL_DEV_MODE)
#define GRAPE_LOG_DEV(...)             \
    {                                  \
        geode::log::info(__VA_ARGS__); \
    }
#else
#define GRAPE_LOG_DEV(...) \
    {                      \
    }
#endif

class FrameEngine;
class GameScheduler;
class TrajectoryManager;
class Autoclicker;
class Pathfinder;
class Hitboxes;
class LabelManager;
class CPSCounter;

#ifndef GEODE_IS_MOBILE
class UIManager;
#endif

namespace grape::vine {
class ReplayDirector;
}

namespace grape::snap {
class PracticeKeeper;
}

class GrapeEngine {
   public:
    static GrapeEngine* get() {
        static GrapeEngine engine;
        return &engine;
    }

    GrapeEngine(const GrapeEngine&) = delete;
    GrapeEngine& operator=(const GrapeEngine&) = delete;

    void initialize();
    bool initialized() { return m_booted; }

    bool isEnabled();
    bool isRecording();
    bool isPlaying();

    FrameEngine& timeline();
    GameScheduler& ticker();
#ifndef GEODE_IS_MOBILE
    UIManager& ui();
#endif
    grape::vine::ReplayDirector& replay();
    grape::snap::PracticeKeeper& keeper();
    TrajectoryManager& trajectory();
    Autoclicker& autoclicker();
    Pathfinder& pathfinder();
    Hitboxes& hitboxes();
    LabelManager& labels();
    CPSCounter& cps();

    ConfigValuePtr<bool> m_enabled = ConfigValue<bool>::create(
        "_________________bot.enabled", &GrapeSettings::get()->botEnabled);

   private:
    GrapeEngine();
    ~GrapeEngine();

    class Parts;
    std::unique_ptr<Parts> m_parts;

    bool m_booted = false;

    void bootstrapWorkspace();
    void readSettings();
    void applyPreset();
    void settleAutoclickerSettings();
    void settleEnabled(bool& enabled);
#ifndef GEODE_IS_MOBILE
    void setHooksActive(bool enabled);
#endif
};

#endif
