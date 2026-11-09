#ifndef UI_STATE_HPP
#define UI_STATE_HPP

#include <string>
#include <string_view>
#include <vector>

#include "../config/config.hpp"
#include "../shared/value/value.hpp"
#include <imgui.h>
#include "imgui_helpers.hpp"

struct UIState {
    enum class UITab {
        Record,
        Assist,
        Prediction,
        Edit,
        Render,
        Scripts,
        Settings,
        Theme
    };

    ConfigValuePtr<bool> m_visible =
        ConfigValue<bool>::create("ui.visible", &GrapeSettings::get()->uiVisible);
    ConfigValuePtr<bool> m_useShader =
        ConfigValue<bool>::create("ui.glass_ui", &GrapeSettings::get()->glassUi);
    ConfigValuePtr<float> m_uiScale = ConfigValue<float>::create(
        "ui._______ui_scale",
        &GrapeSettings::get()->uiScale);

    ConfigValuePtr<bool> m_rainbow =
        ConfigValue<bool>::create("ui.rainbow", &GrapeSettings::get()->rainbowMode);
    ConfigValuePtr<bool> m_playAnimations = ConfigValue<bool>::create(
        "ui.play_animations", &GrapeSettings::get()->playAnimations);
    ConfigValuePtr<float> m_animationSpeed = ConfigValue<float>::create(
        "ui.animation_speed", &GrapeSettings::get()->animationSpeed);
    ConfigValuePtr<float> m_opacity =
        ConfigValue<float>::create("ui.opacity", &GrapeSettings::get()->uiOpacity);

    UITab m_currentTab = UITab::Record;
    std::vector<std::string> m_replayNames;
    std::vector<std::string> m_presetNames;
    std::vector<std::string> m_scriptNames;

    bool m_restartGameInfo = false;

    double m_fps = 240.0;
    double m_speed = 1.0;
    double m_tps = 240.0;

    int m_editScrolledTo = -1;

    int m_codec = 0;
    double m_bitrate = 30.0;

    slui::DropdownState m_encoderState = {};
    bool m_encodersDetected = false;

    int m_editIndex = 0;
    bool m_editSelectionInitialized = false;

    std::string m_replayName = "";
    std::string m_lastReplayName = "";

    std::string m_mergeReplayName = "";
    slui::AutocompleteState m_mergeAutocomplete;

    std::string m_presetName = "";
    slui::AutocompleteState m_presetAutocomplete;

    std::string m_scriptName = "";
    slui::AutocompleteState m_scriptAutocomplete;

    using Mode = GrapeSettings::TrajectorySettings::Mode;
    int m_trajectoryMode = Mode::Hold;
    slui::DropdownState m_trajectoryState = {
        .options =
            {
                "Hold",
                "Swift",
                "Release",

                "Hold Stationary",
                "Swift Stationary",
                "Release Stationary",

                "Hold Left",
                "Swift Left",
                "Release Left",

                "Hold Right",
                "Swift Right",
                "Release Right",

                "Hold Follow",
                "Swift Follow",
                "Release Follow",

                "Hold Opposite",
                "Swift Opposite",
                "Release Opposite",
            },
    };
    int m_categories[18] = {
        Mode::Hold,
        Mode::Swift,
        Mode::Release,

        Mode::Hold | Mode::Platformer,
        Mode::Swift | Mode::Platformer,
        Mode::Release | Mode::Platformer,

        Mode::Hold | Mode::Left | Mode::Platformer,
        Mode::Swift | Mode::Left | Mode::Platformer,
        Mode::Release | Mode::Left | Mode::Platformer,

        Mode::Hold | Mode::Right | Mode::Platformer,
        Mode::Swift | Mode::Right | Mode::Platformer,
        Mode::Release | Mode::Right | Mode::Platformer,

        Mode::Hold | Mode::FollowPlayer | Mode::Platformer,
        Mode::Swift | Mode::FollowPlayer | Mode::Platformer,
        Mode::Release | Mode::FollowPlayer | Mode::Platformer,

        Mode::Hold | Mode::FollowOpposite | Mode::Platformer,
        Mode::Swift | Mode::FollowOpposite | Mode::Platformer,
        Mode::Release | Mode::FollowOpposite | Mode::Platformer,
    };

    slui::ColorState m_trajectoryColorState;
    slui::ColorState m_straightTrajectoryColorState;
    bool m_trajectoryPlayer2 = false;

    slui::DropdownState m_lockDeltaState = {
        .options = {"Performance", "Accuracy"},
    };

    slui::DropdownState m_labelState = {};
    slui::DropdownState m_labelPositionsState = {
        .options =
            {
                "Top Left",
                "Top Right",
                "Bottom Left",
                "Bottom Right",
            },
    };

    slui::DropdownState m_labelFontsState = {
        .options =
            {
                "Big",
                "Regular",
            },
    };

    slui::DropdownState m_autoclickerState = {
        .options =
            {
                "Player 1",
                "Player 2",
                "Both",
            },
    };

    slui::DropdownState m_themeState;

    slui::DropdownState m_noclipState = {
        .options =
            {
                "Player 1",
                "Player 2",
                "Both",
            },
    };

    using HitboxType = GrapeSettings::HitboxSettings::Type;
    slui::DropdownState m_hitboxState = {
        .options = {"Player", "Inner Player", "Rotated Player",
                    "Player (Circle)",

                    "Solid", "Hazard", "Passable", "Interactable",
                    "Interactable (Active)"},
    };
    int m_hitboxCategories[9] = {HitboxType::Player,
                                 HitboxType::PlayerInner,
                                 HitboxType::PlayerRotated,
                                 HitboxType::PlayerCircle,

                                 HitboxType::Solid,
                                 HitboxType::Hazard,
                                 HitboxType::Passable,
                                 HitboxType::Interactable,
                                 HitboxType::InteractableActive};

    slui::ColorState m_hitboxColorState;
    bool m_hitboxPlayer2 = false;
    slui::ColorState m_holdingTrailColorState;
    slui::ColorState m_groundColorState;
    slui::ColorState m_bgColorState;
    slui::ColorState m_noclipTintColorState;

    std::vector<std::string> m_customFontNames;
    std::vector<std::string> m_customFontFiles;

    struct KeybindContextState {
        bool  open        = false;
        std::string tag;

        bool  capturing   = false;
        int   capturedKey = 0;
        int   capturedMod = 0;

        KeybindType pendingType = KeybindType::Toggle;
        std::string pendingValue = "1";

        slui::DropdownState typeState = {
            .options = {"Toggle", "Hold", "Override"},
        };
    } m_keybindCtx;

    void toggle() { m_visible->inner() = !m_visible->inner(); }
};

#endif
