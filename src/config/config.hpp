#pragma once

#include "util/atomic_file.hpp"
#include "util/storage.hpp"

#include <Geode/Geode.hpp>
#include <glaze/glaze.hpp>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "../shared/value/value.hpp"

namespace grape::settings {
using Rgba = std::array<float, 4>;
using Names = std::vector<std::string>;
}

#define GRAPE_SETTINGS_MENU(SAVE, HIDE, KEEP) \
    SAVE(bool, botEnabled, "global_enabled", true) \
    HIDE(bool, uiVisible, "ui_visible", false) \
    SAVE(bool, glassUi, "glass_ui", true) \
    SAVE(float, animationSpeed, "animation_speed", 1.0f) \
    SAVE(bool, playAnimations, "play_animations", true) \
    SAVE(float, uiScale, "ui_scale", 1.0f) \
    SAVE(float, uiOpacity, "ui_opacity", 0.66f) \
    SAVE(bool, fitMenuToContent, "fit_menu_to_content", false) \
    SAVE(int, theme, "theme", 0) \
    SAVE(bool, rainbowMode, "pride_mode", true) \
    SAVE(Rgba, grapeAccent, "grape_accent", {0.60f, 0.45f, 0.90f, 1.0f}) \
    SAVE(Rgba, skeetAccent, "skeet_accent", {0.576f, 0.773f, 0.224f, 1.0f}) \
    SAVE(Rgba, skeetGradientLeft, "skeet_gradient_left", {0.216f, 0.694f, 0.855f, 1.0f}) \
    SAVE(Rgba, skeetGradientMiddle, "skeet_gradient_middle", {0.788f, 0.329f, 0.753f, 1.0f}) \
    SAVE(Rgba, skeetGradientRight, "skeet_gradient_right", {0.8f, 0.89f, 0.212f, 1.0f}) \
    SAVE(Rgba, mobileMenuColor, "mobile_menu_color", {1.0, 1.0, 1.0, 1.0}) \
    SAVE(bool, showEndMenuButton, "show_end_menu_button", true)

#define GRAPE_SETTINGS_TIMING(SAVE, HIDE, KEEP) \
    HIDE(double, tps, "tps", 240.0) \
    HIDE(double, speed, "speed", 1.0) \
    SAVE(bool, realTime, "real_time", true) \
    SAVE(uint32_t, maxUpr, "max_upr", 10) \
    SAVE(bool, useVisualUpdates, "use_visual_updates", true) \
    SAVE(bool, dynamicUpr, "dynamic_upr", true) \
    SAVE(double, fpsTarget, "target_fps", 60.0) \
    SAVE(bool, lockDelta, "lock_delta", true) \
    KEEP(int, lockDeltaMode, 0) \
    SAVE(bool, physics21, "physics_21", false) \
    SAVE(bool, speedhackAudio, "speedhack_audio", true) \
    SAVE(bool, blockInputs, "block_inputs", true) \
    SAVE(bool, useAlternateHook, "alternate_hook", false) \
    SAVE(bool, scrollSpeedBugFix, "ssb_fix", false)

#define GRAPE_SETTINGS_PREDICTION(SAVE, HIDE, KEEP) \
    KEEP(bool, fullGamePrediction, false) \
    SAVE(float, acceptablePrediction, "acceptable_prediction", 0.45)

#define GRAPE_SETTINGS_MACROS(SAVE, HIDE, KEEP) \
    SAVE(bool, autosaveAtLevelEnd, "autosave_at_level_end", true) \
    SAVE(bool, autosaveAtInterval, "autosave_at_interval", true) \
    SAVE(double, autosaveInterval, "autosave_interval", 180.0) \
    SAVE(bool, autoMacroName, "auto_macro_name", true) \
    SAVE(std::string, macroNameTemplate, "macro_name_template", "%name%")

#define GRAPE_SETTINGS_VIDEO(SAVE, HIDE, KEEP) \
    SAVE(bool, automaticVideoName, "auto_video_name", true) \
    SAVE(bool, previewAudio, "preview_audio", true) \
    SAVE(std::string, videoNameTemplate, "video_name_template", "%name%_%rand%") \
    SAVE(bool, renderLabelsWhileRecording, "render_labels_while_recording", false) \
    SAVE(Names, renderDetectedEncoders, "render_detected_encoders", {}) \
    SAVE(std::string, lastLoadedPreset, "last_loaded_preset", "") \
    SAVE(bool, renderDebug, "render_debug", false)

#define GRAPE_SETTINGS_AUTOCLICKER(SAVE, HIDE, KEEP) \
    HIDE(bool, autoclickerEnabled, "autoclicker_enabled", false) \
    SAVE(int, autoclickerFrequency, "autoclicker_frequency", 1) \
    SAVE(int, autoclickerHoldFrames, "autoclicker_hold_frames", 1) \
    SAVE(int, autoclickerReleaseFrames, "autoclicker_release_frames", 1) \
    SAVE(int, autoclickerP1ClicksPerTick, "autoclicker_p1_clicks_per_tick", 1) \
    SAVE(int, autoclickerP2HoldFrames, "autoclicker_p2_hold_frames", 1) \
    SAVE(int, autoclickerP2ReleaseFrames, "autoclicker_p2_release_frames", 1) \
    SAVE(int, autoclickerP2ClicksPerTick, "autoclicker_p2_clicks_per_tick", 1) \
    SAVE(bool, autoclickerSyncPlayers, "autoclicker_sync_players", true) \
    SAVE(bool, autoclickerSwifts, "autoclicker_swifts", false) \
    SAVE(bool, autoclickerMovingGap, "autoclicker_moving_gap", false) \
    SAVE(bool, autoclickerTestMovingGap, "autoclicker_test_moving_gap", false) \
    SAVE(bool, autoclickerStraightUfo, "autoclicker_straight_ufo", false) \
    SAVE(bool, autoclickerEasyBlackOrbs, "autoclicker_easy_black_orbs", false) \
    SAVE(int, autoclickerMovingGapLookahead, "autoclicker_moving_gap_lookahead", 2) \
    SAVE(int, autoclickerPlayer, "autoclicker_player", 0) \
    SAVE(bool, autoclickerProportional, "autoclicker_proportional", false) \
    SAVE(float, autoclickerHoldPct, "autoclicker_hold_pct", 50.0f) \
    SAVE(float, autoclickerReleasePct, "autoclicker_release_pct", 40.0f) \
    SAVE(float, autoclickerSwiftPct, "autoclicker_swift_pct", 10.0f) \
    SAVE(int, autoclickerCycleFrames, "autoclicker_cycle_frames", 8)

#define GRAPE_SETTINGS_KEYBINDS(SAVE, HIDE, KEEP) \
    SAVE(int, keybindHoldDelay, "keybind_hold_delay", 350) \
    SAVE(int, keybindRepeatFrequency, "keybind_repeat_frequency", 100)

#define GRAPE_SETTINGS_PATHFINDER(SAVE, HIDE, KEEP) \
    HIDE(bool, pathfinderEnabled, "pathfinder_enabled", false) \
    SAVE(int, pathfinderStuckDeaths, "pathfinder_stuck_deaths", 5) \
    SAVE(bool, pathfinderForesight, "pathfinder_foresight", true)

#define GRAPE_SETTINGS_STEPPER(SAVE, HIDE, KEEP) \
    SAVE(bool, backwardsStepping, "backwards_stepping", false) \
    SAVE(uint32_t, stepsToSave, "steps_saved", 120) \
    SAVE(bool, frameStepperOverlay, "frame_stepper_overlay", false) \
    SAVE(bool, frameStepperHold, "frame_stepper_hold", true) \
    SAVE(double, frameStepperHoldDelay, "frame_stepper_hold_delay", 0.35) \
    SAVE(double, frameStepperHoldSpeed, "frame_stepper_hold_speed", 12.0) \
    SAVE(double, frameStepperArrowOpacity, "frame_stepper_arrow_opacity", 0.8) \
    SAVE(double, frameStepperX, "frame_stepper_x", -1.0) \
    SAVE(double, frameStepperY, "frame_stepper_y", -1.0) \
    SAVE(bool, frameStepperMovable, "frame_stepper_movable", false) \
    SAVE(bool, frameStepperInEditorPlayback, "frame_stepper_editor_playback", true)

#define GRAPE_SETTINGS_NOCLIP(SAVE, HIDE, KEEP) \
    SAVE(bool, noclipTintEnabled, "noclip_tint_enabled", true) \
    SAVE(int, noclipPlayer, "noclip_player", 0) \
    SAVE(Rgba, noclipTintColor, "noclip_tint_color", {1.0, 0.0, 0.0, 1.0}) \
    SAVE(double, noclipTintOpacity, "noclip_tint_opacity", 0.35) \
    SAVE(double, noclipTintTime, "noclip_tint_time", 0.2)

#define GRAPE_SETTINGS_LAYOUT(SAVE, HIDE, KEEP) \
    SAVE(bool, useRegularBg, "layout_use_bg", false) \
    SAVE(Rgba, layoutBgColor, "layout_bg", {0.2828, 0.4901, 1.0, 1.0}) \
    SAVE(Rgba, layoutGroundColor, "layout_ground", {0.2828, 0.4901, 1.0, 1.0})

#define GRAPE_SETTINGS(SAVE, HIDE, KEEP) \
    GRAPE_SETTINGS_MENU(SAVE, HIDE, KEEP) \
    GRAPE_SETTINGS_TIMING(SAVE, HIDE, KEEP) \
    GRAPE_SETTINGS_PREDICTION(SAVE, HIDE, KEEP) \
    GRAPE_SETTINGS_MACROS(SAVE, HIDE, KEEP) \
    GRAPE_SETTINGS_VIDEO(SAVE, HIDE, KEEP) \
    GRAPE_SETTINGS_AUTOCLICKER(SAVE, HIDE, KEEP) \
    GRAPE_SETTINGS_KEYBINDS(SAVE, HIDE, KEEP) \
    GRAPE_SETTINGS_PATHFINDER(SAVE, HIDE, KEEP) \
    GRAPE_SETTINGS_STEPPER(SAVE, HIDE, KEEP) \
    GRAPE_SETTINGS_NOCLIP(SAVE, HIDE, KEEP) \
    GRAPE_SETTINGS_LAYOUT(SAVE, HIDE, KEEP)

class GrapeSettings {
   public:
    using Rgba = grape::settings::Rgba;
    using Names = grape::settings::Names;

#define GRAPE_SETTING_FIELD(type, name, key, ...) type name = __VA_ARGS__;
#define GRAPE_SETTING_MEMORY(type, name, ...) type name = __VA_ARGS__;
    GRAPE_SETTINGS(GRAPE_SETTING_FIELD, GRAPE_SETTING_FIELD, GRAPE_SETTING_MEMORY)
#undef GRAPE_SETTING_FIELD
#undef GRAPE_SETTING_MEMORY

    struct TrajectorySettings {
        enum Mode {
            Hold = 0x1,
            Swift = 0x2,
            Release = 0x4,
            Left = 0x8,
            Right = 0x10,
            Player1 = 0x20,
            Player2 = 0x40,
            FollowPlayer = 0x80,
            FollowOpposite = 0x100,
            Platformer = 0x200,
        };

        struct State {
            bool enabled = false;
            Rgba colors = {0.0, 1.0, 0.0, 1.0};
            Rgba player2Colors = {0.0, 1.0, 1.0, 1.0};
        };

        static std::unordered_map<int, State> defaultCategories() {
            std::unordered_map<int, State> shown;
            for (int motion : {Hold, Swift, Release}) {
                shown[motion] = {true};
                for (int side : std::initializer_list<int>{0, Left, Right, FollowPlayer, FollowOpposite})
                    shown[motion | side | Platformer] = {side == FollowPlayer && motion != Swift};
            }
            return shown;
        }

        bool enabled = false;
        double width = 0.5;
        double length = 1.0;
        int maxSteps = 0;
        bool straightEnabled = false;
        Rgba straightColor = {0.54f, 0.81f, 0.94f, 1.0f};
        Rgba straightPlayer2Color = {0.0f, 1.0f, 1.0f, 1.0f};
        bool orbSkipEnabled = true;
        Rgba orbSkipColor = {0.55f, 0.0f, 0.0f, 1.0f};
        Rgba orbSkipPlayer2Color = {0.55f, 0.0f, 0.35f, 1.0f};
        std::unordered_map<int, State> categories = defaultCategories();
    } trajectory;

    struct HitboxSettings {
        enum Type {
            Player,
            PlayerRotated,
            PlayerInner,
            PlayerCircle,
            Solid,
            Hazard,
            Passable,
            Interactable,
            InteractableActive
        };

        struct HBState {
            bool enabled = true;
            double fillOpacity = 0.00;
            Rgba colors{};
            Rgba player2Colors{};
        };

        static std::unordered_map<int, HBState> defaultCategories() {
            const auto both = [](Rgba color) { return HBState{true, 0.0, color, color}; };
            const auto single = [](Rgba color) { return HBState{true, 0.0, color, {}}; };
            return {
                {Player, both({1.0, 0.0, 0.0, 1.0})},
                {PlayerInner, both({0.0, 0.0, 1.0, 1.0})},
                {PlayerRotated, both({0.5, 0.0, 0.0, 1.0})},
                {PlayerCircle, both({1.0, 0.0, 0.0, 1.0})},
                {Solid, single({0.0, 0.0, 1.0, 1.0})},
                {Hazard, single({1.0, 0.0, 0.0, 1.0})},
                {Passable, single({0.0, 1.0, 1.0, 1.0})},
                {Interactable, single({1.0, 1.0, 0.0, 1.0})},
                {InteractableActive, single({0.2, 1.0, 0.0, 1.0})},
            };
        }

        double width = 0.5;
        bool trailEnabled = false;
        bool holdingTrailEnabled = true;
        Rgba holdingTrailColor = {0.0f, 1.0f, 1.0f, 1.0f};
        int trailMaxLength = 1000;
        int trailRebuildInterval = 3;
        bool snapPlacedToTrail = false;
        std::unordered_map<int, HBState> categories = defaultCategories();
    } hitboxes;

    static GrapeSettings* get() {
        static GrapeSettings instance;
        return &instance;
    }
};

template <>
struct glz::meta<GrapeSettings> {
    using T = GrapeSettings;
#define GRAPE_SETTING_SAVED(type, name, key, ...) key, &T::name,
#define GRAPE_SETTING_HIDDEN(type, name, key, ...) key, glz::hide{&T::name},
#define GRAPE_SETTING_UNSAVED(type, name, ...)
    static constexpr auto value =
        glz::object(GRAPE_SETTINGS(GRAPE_SETTING_SAVED, GRAPE_SETTING_HIDDEN, GRAPE_SETTING_UNSAVED)
                        "preset", &T::lastLoadedPreset, "trajectory", &T::trajectory, "hitboxes", &T::hitboxes);
#undef GRAPE_SETTING_SAVED
#undef GRAPE_SETTING_HIDDEN
#undef GRAPE_SETTING_UNSAVED
};

namespace grape::settings {

inline void save() {
    std::string json;
    if (glz::write<glz::opts{.prettify = true}>(*GrapeSettings::get(), json)) {
        geode::log::error("Failed to serialize settings");
    } else {
        std::error_code error;
        if (!grape::files::writeAtomically(grape::paths::file("settings.json"), json, error))
            geode::log::error("Failed to save settings: {}", error.message());
    }
    BindingManager::get()->writeToFile(grape::paths::file("keybinds.json"));
}

}

$on_mod(DataSaved) { grape::settings::save(); }
