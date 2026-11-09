#include "mobile_menu.hpp"

#include <Geode/UI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cctype>
#include <tuple>
#include <cmath>
#include <filesystem>
#include <fmt/format.h>
#include <limits>

#include "assist/autoclicker.hpp"
#include "assist/hitboxes.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "checkpoint/practice.hpp"
#include "vine/session.hpp"
#include "render/renderer.hpp"
#include "config/config.hpp"
#include "trajectory/trajectory.hpp"
#include "util/crash_log.hpp"
#include "util/input_trace.hpp"
#include "ui/touch_overlay.hpp"
#if defined(GEODE_IS_MOBILE)
#include "scripting/script_engine.hpp"
#endif
#include "util/storage.hpp"

using namespace geode::prelude;

namespace {
ButtonSprite* makeButtonSprite(std::string const& text, float width) {
    return ButtonSprite::create(text.c_str(), static_cast<int>(width), true,
                                "bigFont.fnt", "GJ_button_01.png", 24.f,
                                .55f);
}

bool parseNumber(std::string const& text, double& value) {
    if (text.empty() || text == "-" || text == "." || text == "-.") {
        return false;
    }
    try {
        size_t used = 0;
        value = std::stod(text, &used);
        return used == text.size() && std::isfinite(value);
    } catch (...) {
        return false;
    }
}

ccColor4B toColor(std::array<float, 4> const& value) {
    return {static_cast<GLubyte>(std::clamp(value[0], 0.f, 1.f) * 255.f),
            static_cast<GLubyte>(std::clamp(value[1], 0.f, 1.f) * 255.f),
            static_cast<GLubyte>(std::clamp(value[2], 0.f, 1.f) * 255.f),
            static_cast<GLubyte>(std::clamp(value[3], 0.f, 1.f) * 255.f)};
}

void applyMenuColor(cocos2d::CCNodeRGBA* background) {
    if (!background) return;
    const auto color = toColor(GrapeSettings::get()->mobileMenuColor);
    background->setColor({color.r, color.g, color.b});
    background->setOpacity(std::max<GLubyte>(color.a, 60));
}

class MobileColorPopup final : public geode::ColorPickPopup {
    bool init(std::array<float, 4>* target,
              std::function<void(ccColor3B)> picked) {
        if (!ColorPickPopup::init(toColor(*target), true)) return false;
        this->setCallback([target, picked = std::move(picked)](
                              ccColor4B const& color) {
            (*target)[0] = color.r / 255.f;
            (*target)[1] = color.g / 255.f;
            (*target)[2] = color.b / 255.f;
            (*target)[3] = color.a / 255.f;
            if (picked) picked({color.r, color.g, color.b});
        });

        const auto addClipboardButton =
            [this](const char* text, float x, std::function<void()> callback) {
                auto* sprite = makeButtonSprite(text, 70.f);
                auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(
                    sprite,
                    [callback = std::move(callback)](CCMenuItemSpriteExtra*) {
                        callback();
                    });
                m_buttonMenu->addChildAtPosition(
                    item, geode::Anchor::Bottom, {x, 20.f});
            };
        addClipboardButton("Copy", -95.f, [this] {
            auto* input = typeinfo_cast<geode::TextInput*>(
                getChildByIDRecursive("hex-input"));
            if (input)
                geode::utils::clipboard::write(
                    "#" + std::string(input->getString()));
        });
        addClipboardButton("Paste", 95.f, [this] {
            auto* input = typeinfo_cast<geode::TextInput*>(
                getChildByIDRecursive("hex-input"));
            if (!input) return;
            std::string hex = geode::utils::clipboard::read();
            if (hex.starts_with('#')) hex.erase(0, 1);
            if (hex.size() == 8) hex.resize(6);
            if (geode::cocos::cc3bFromHexString(hex, false))
                input->setString(hex, true);
        });
        return true;
    }

   public:
    static MobileColorPopup* create(std::array<float, 4>* target,
                                    std::function<void(ccColor3B)> picked) {
        auto* popup = new MobileColorPopup();
        if (popup->init(target, std::move(picked))) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

void openColorPicker(std::array<float, 4>* target,
                     std::function<void(ccColor3B)> picked = {}) {
    if (auto* popup = MobileColorPopup::create(target, std::move(picked)))
        popup->show();
}

class MobileFeaturePopup final : public geode::Popup {
   public:
    enum class Feature {
        Hitboxes,
        Trajectory,
        Autoclicker,
        Layout,
        Noclip,
        Stepper,
        Rendering,
    };

   private:
    Feature m_feature;
    ScrollLayer* m_scroll = nullptr;
    CCNode* m_content = nullptr;
    CCMenu* m_menu = nullptr;
    int m_category = 0;
    bool m_player2 = false;

    float m_top = 215.f;
    static constexpr float kHeaderLift = 28.f;
    static constexpr float kHeaderRoom = 50.f;

    float rowY(int row) const { return m_top - row * 29.f; }

    void label(std::string const& text, float x, float y, float scale = .34f,
               ccColor3B color = ccWHITE) {
        auto* node = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        node->setScale(scale);
        node->setColor(color);
        node->setPosition({x, y});
        node->limitLabelWidth(270.f, scale, .14f);
        m_content->addChild(node);
    }

    void settingLabel(std::string const& text, float y,
                      float scale = .34f) {
        auto* node = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        node->setAnchorPoint({0.f, .5f});
        node->setScale(scale);
        node->setPosition({28.f, y});
        node->limitLabelWidth(185.f, scale, .14f);
        m_content->addChild(node);
    }

    ButtonSprite* button(std::string const& text, float x, float y,
                         std::function<void()> callback, float width = 85.f) {
        auto* sprite = makeButtonSprite(text, width);
        auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(
            sprite, [callback = std::move(callback)](CCMenuItemSpriteExtra*) {
                callback();
            });
        item->setPosition({x, y});
        m_menu->addChild(item);
        return sprite;
    }

    void toggle(std::string const& text, int row, bool value,
                std::function<void(bool)> callback, float x = 275.f) {
        const float y = rowY(row);
        settingLabel(text, y);
        auto* item =
            geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(
                .42f,
                [callback = std::move(callback)](CCMenuItemToggler* sender) {
                    callback(!sender->isToggled());
                });
        item->toggle(value);
        item->setPosition({x + 75.f, y});
        m_menu->addChild(item);
    }

    void colorSquare(float x, float y, std::array<float, 4>* target) {
        auto* square = ColorChannelSprite::create();
        const auto color = toColor(*target);
        square->setColor({color.r, color.g, color.b});
        square->setScale(.62f);
        geode::Ref<ColorChannelSprite> keep = square;
        auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(
            square, [target, keep](CCMenuItemSpriteExtra*) {
                openColorPicker(target, [keep](ccColor3B picked) {
                    keep->setColor(picked);
                });
            });
        item->setPosition({x, y});
        m_menu->addChild(item);
    }

    TextInput* number(std::string const& text, int row, double value,
                      std::function<void(double)> setter, double minimum,
                      double maximum, int decimals = 2, float x = 260.f) {
        const float y = rowY(row);
        settingLabel(text, y);
        auto* input = TextInput::create(92.f, "Value");
        input->setScaleY(.84f);
        input->setCommonFilter(CommonFilter::Float);
        input->setString(fmt::format("{:.{}f}", value, decimals));
        input->setCallback([setter = std::move(setter), minimum,
                            maximum](std::string const& text) {
            double parsed = 0.0;
            if (parseNumber(text, parsed)) {
                setter(std::clamp(parsed, minimum, maximum));
            }
        });
        input->setPosition({x + 50.f, y});
        m_content->addChild(input, 2);
        return input;
    }

    TextInput* text(std::string const& labelText, int row, std::string value,
                    std::function<void(std::string const&)> setter) {
        const float y = rowY(row);
        settingLabel(labelText, y);
        auto* input = TextInput::create(145.f, "Value");
        input->setScaleY(.84f);
        input->setCommonFilter(CommonFilter::ID);
        input->setString(value);
        input->setCallback(std::move(setter));
        input->setPosition({283.f, y});
        m_content->addChild(input, 2);
        return input;
    }

    void color(std::string const& text, int row,
               std::array<float, 4>* target) {
        settingLabel(text, rowY(row));
        colorSquare(340.f, rowY(row), target);
    }

    void rebuild() {
        m_content->removeAllChildren();
        m_menu = CCMenu::create();
        m_menu->setPosition({0.f, 0.f});
        m_menu->setAnchorPoint({0.f, 0.f});
        m_menu->setContentSize(m_content->getContentSize());
        m_content->addChild(m_menu, 3);

        switch (m_feature) {
            case Feature::Hitboxes: buildHitboxes(); break;
            case Feature::Trajectory: buildTrajectory(); break;
            case Feature::Autoclicker: buildAutoclicker(); break;
            case Feature::Layout: buildLayout(); break;
            case Feature::Noclip: buildNoclip(); break;
            case Feature::Stepper: buildStepper(); break;
            case Feature::Rendering: buildRendering(); break;
        }
        m_scroll->scrollToTop();
    }

    void buildHitboxes() {
        auto* settings = GrapeSettings::get();
        auto* hitboxes = &GrapeEngine::get()->hitboxes();
        static constexpr const char* names[] = {
            "Player", "Inner Player", "Rotated Player", "Player Circle",
            "Solid",  "Hazard",       "Passable",       "Interactable",
            "Interactable Active"};
        static constexpr int categories[] = {
            GrapeSettings::HitboxSettings::Player,
            GrapeSettings::HitboxSettings::PlayerInner,
            GrapeSettings::HitboxSettings::PlayerRotated,
            GrapeSettings::HitboxSettings::PlayerCircle,
            GrapeSettings::HitboxSettings::Solid,
            GrapeSettings::HitboxSettings::Hazard,
            GrapeSettings::HitboxSettings::Passable,
            GrapeSettings::HitboxSettings::Interactable,
            GrapeSettings::HitboxSettings::InteractableActive};
        m_category = std::clamp(m_category, 0, 8);
        auto* state = &settings->hitboxes.categories[categories[m_category]];
        button(m_player2 ? "Player 2" : "Player 1", 185.f, m_top + kHeaderLift, [this] {
            m_player2 = !m_player2;
            rebuild();
        }, 120.f);

        number("Line width", 0, hitboxes->m_width->inner(),
               [hitboxes](double value) { hitboxes->m_width->inner() = value; },
               .01, 5.0);
        toggle("Isolate death", 1, hitboxes->m_deathOnly->inner(),
               [hitboxes](bool value) {
                   hitboxes->m_deathOnly->inner() = value;
               });
        toggle("Trail", 2, hitboxes->m_trailEnabled->inner(),
               [hitboxes](bool value) {
                   hitboxes->m_trailEnabled->inner() = value;
               }, 155.f);
        button("Snap", 318.f, rowY(2), [hitboxes] {
            hitboxes->snapToTrail();
        }, 62.f);
        toggle("Holding trail", 3, settings->hitboxes.holdingTrailEnabled,
               [settings](bool value) {
                   settings->hitboxes.holdingTrailEnabled = value;
               }, 155.f);
        colorSquare(340.f, rowY(3), &settings->hitboxes.holdingTrailColor);
        number("Trail frames", 4, settings->hitboxes.trailMaxLength,
               [settings](double value) {
                   settings->hitboxes.trailMaxLength =
                       std::max(1, static_cast<int>(value));
               },
               1, 100000, 0);
        label(names[m_category], 185.f, rowY(5), .35f, {255, 220, 90});
        button("<", 55.f, rowY(5), [this] {
            m_category = (m_category + 8) % 9;
            rebuild();
        }, 32.f);
        button(">", 315.f, rowY(5), [this] {
            m_category = (m_category + 1) % 9;
            rebuild();
        }, 32.f);
        toggle("Category visible", 6, state->enabled,
               [state](bool value) { state->enabled = value; });
        number("Fill opacity", 7, state->fillOpacity,
               [state](double value) { state->fillOpacity = value; }, 0, 1,
               2, 155.f);
        if (categories[m_category] <=
            GrapeSettings::HitboxSettings::PlayerCircle) {
            auto* colors = m_player2 ? &state->player2Colors : &state->colors;
            colorSquare(340.f, rowY(7), colors);
        } else {
            colorSquare(340.f, rowY(7), &state->colors);
        }
    }

    void buildTrajectory() {
        auto* settings = GrapeSettings::get();
        auto* manager = &GrapeEngine::get()->trajectory();
        static constexpr const char* names[] = {
            "Hold",          "Swift",          "Release",
            "Hold Still",    "Swift Still",    "Release Still",
            "Hold Left",     "Swift Left",     "Release Left",
            "Hold Right",    "Swift Right",    "Release Right",
            "Hold Follow",   "Swift Follow",   "Release Follow",
            "Hold Opposite", "Swift Opposite", "Release Opposite"};
        using M = GrapeSettings::TrajectorySettings::Mode;
        static constexpr int categories[] = {
            M::Hold, M::Swift, M::Release,
            M::Hold | M::Platformer, M::Swift | M::Platformer,
            M::Release | M::Platformer,
            M::Hold | M::Left | M::Platformer,
            M::Swift | M::Left | M::Platformer,
            M::Release | M::Left | M::Platformer,
            M::Hold | M::Right | M::Platformer,
            M::Swift | M::Right | M::Platformer,
            M::Release | M::Right | M::Platformer,
            M::Hold | M::FollowPlayer | M::Platformer,
            M::Swift | M::FollowPlayer | M::Platformer,
            M::Release | M::FollowPlayer | M::Platformer,
            M::Hold | M::FollowOpposite | M::Platformer,
            M::Swift | M::FollowOpposite | M::Platformer,
            M::Release | M::FollowOpposite | M::Platformer};
        m_category = std::clamp(m_category, 0, 17);
        auto* state = &settings->trajectory.categories[categories[m_category]];
        button(m_player2 ? "Player 2" : "Player 1", 185.f, m_top + kHeaderLift, [this] {
            m_player2 = !m_player2;
            rebuild();
        }, 120.f);

        number("Line width", 0, manager->m_state.m_width->inner(),
               [manager](double value) {
                   manager->m_state.m_width->inner() = value;
               },
               .01, 5.0);
        number("Path seconds", 1, manager->m_state.m_length->inner(),
               [manager](double value) {
                   manager->m_state.m_length->inner() = value;
               },
               .05, 30.0);
        number("Max steps (0=auto)", 2, settings->trajectory.maxSteps,
               [settings](double value) {
                   settings->trajectory.maxSteps =
                       std::max(0, static_cast<int>(value));
               },
               0, 1000000, 0);
        label(names[m_category], 185.f, rowY(3), .35f, {255, 220, 90});
        button("<", 55.f, rowY(3), [this] {
            m_category = (m_category + 17) % 18;
            rebuild();
        }, 32.f);
        button(">", 315.f, rowY(3), [this] {
            m_category = (m_category + 1) % 18;
            rebuild();
        }, 32.f);
        toggle("Show category", 4, state->enabled,
               [state](bool value) { state->enabled = value; });
        color("Category color", 5,
              m_player2 ? &state->player2Colors : &state->colors);
        toggle("Straight wave", 6, settings->trajectory.straightEnabled,
               [settings](bool value) {
                   settings->trajectory.straightEnabled = value;
               });
        color("Straight color", 7,
              m_player2 ? &settings->trajectory.straightPlayer2Color
                        : &settings->trajectory.straightColor);
        toggle("Orb skip path", 8, settings->trajectory.orbSkipEnabled,
               [settings](bool value) {
                   settings->trajectory.orbSkipEnabled = value;
               });
        color("Orb skip color", 9,
              m_player2 ? &settings->trajectory.orbSkipPlayer2Color
                        : &settings->trajectory.orbSkipColor);
    }

    void buildAutoclicker() {
        auto* autoclicker = &GrapeEngine::get()->autoclicker();
        static constexpr const char* players[] = {"Player 1", "Player 2",
                                                   "Both"};
        int player = std::clamp(static_cast<int>(autoclicker->m_player), 0, 2);
        settingLabel("Player", rowY(0));
        button(players[player], 288.f, rowY(0), [this, autoclicker] {
            int next = (static_cast<int>(autoclicker->m_player) + 1) % 3;
            autoclicker->m_player =
                static_cast<Autoclicker::PlayerToggle>(next);
            autoclicker->saveSettings();
            rebuild();
        }, 120.f);
        int row = 1;
        toggle("Sync players", row++, autoclicker->m_syncPlayers,
               [this, autoclicker](bool value) {
                   autoclicker->m_syncPlayers = value;
                   autoclicker->saveSettings();
                   rebuild();
               });
        number("Hold frames", row++, autoclicker->m_holdFrames,
               [autoclicker](double value) {
                   autoclicker->m_holdFrames =
                       std::max(1, static_cast<int>(value));
                   autoclicker->saveSettings();
               },
               1, 100000, 0);
        number("Release frames", row++, autoclicker->m_releaseFrames,
               [autoclicker](double value) {
                   autoclicker->m_releaseFrames =
                       std::max(1, static_cast<int>(value));
                   autoclicker->saveSettings();
               },
               1, 100000, 0);
        number("Clicks per tick", row++, autoclicker->m_p1ClicksPerTick,
               [autoclicker](double value) {
                   autoclicker->m_p1ClicksPerTick =
                       std::max(1, static_cast<int>(value));
                   autoclicker->saveSettings();
               },
               1, 32, 0);
        if (!autoclicker->m_syncPlayers) {
            number("P2 hold frames", row++, autoclicker->m_p2HoldFrames,
                   [autoclicker](double value) {
                       autoclicker->m_p2HoldFrames =
                           std::max(1, static_cast<int>(value));
                       autoclicker->saveSettings();
                   },
                   1, 100000, 0);
            number("P2 release frames", row++, autoclicker->m_p2ReleaseFrames,
                   [autoclicker](double value) {
                       autoclicker->m_p2ReleaseFrames =
                           std::max(1, static_cast<int>(value));
                       autoclicker->saveSettings();
                   },
                   1, 100000, 0);
            number("P2 clicks per tick", row++, autoclicker->m_p2ClicksPerTick,
                   [autoclicker](double value) {
                       autoclicker->m_p2ClicksPerTick =
                           std::max(1, static_cast<int>(value));
                       autoclicker->saveSettings();
                   },
                   1, 32, 0);
        }
        toggle("Swift clicks", row++, autoclicker->m_performSwifts,
               [autoclicker](bool value) {
                   autoclicker->m_performSwifts = value;
                   autoclicker->saveSettings();
               });
        toggle("Moving gap assist", row++, autoclicker->m_movingGap,
               [autoclicker](bool value) {
                   autoclicker->m_movingGap = value;
                   autoclicker->saveSettings();
               });
        number("Safety frames", row++, autoclicker->m_movingGapLookahead,
               [autoclicker](double value) {
                   autoclicker->m_movingGapLookahead =
                       std::clamp(static_cast<int>(value), 1, 30);
                   autoclicker->saveSettings();
               },
               1, 30, 0);
    }

    void buildLayout() {
        auto* settings = GrapeSettings::get();
        auto* updater = &GrapeEngine::get()->timeline();
        toggle("Use regular background", 0,
               updater->m_useRegularBg->inner(), [updater](bool value) {
                   updater->m_useRegularBg->inner() = value;
               });
        color("Background color", 2, &settings->layoutBgColor);
        color("Ground color", 3, &settings->layoutGroundColor);
    }

    void buildNoclip() {
        auto* settings = GrapeSettings::get();
        auto* updater = &GrapeEngine::get()->timeline();
        static constexpr const char* players[] = {"Player 1", "Player 2",
                                                   "Both"};
        int player = std::clamp(static_cast<int>(updater->m_noclipType), 0, 2);
        settingLabel("Affected player", rowY(0));
        button(players[player], 288.f, rowY(0), [this, updater] {
            int next = (static_cast<int>(updater->m_noclipType) + 1) % 3;
            updater->m_noclipType =
                static_cast<FrameEngine::NoclipType>(next);
            GrapeSettings::get()->noclipPlayer = next;
            rebuild();
        }, 120.f);
        toggle("Death tint", 1, settings->noclipTintEnabled,
               [settings](bool value) { settings->noclipTintEnabled = value; });
        color("Tint color", 2, &settings->noclipTintColor);
        number("Tint opacity", 3, settings->noclipTintOpacity,
               [settings](double value) {
                   settings->noclipTintOpacity = value;
               },
               0, 1);
        number("Tint time", 4, settings->noclipTintTime,
               [settings](double value) { settings->noclipTintTime = value; },
               0, 10);
    }

    void buildStepper() {
        auto* settings = GrapeSettings::get();
        auto* updater = &GrapeEngine::get()->timeline();
        toggle("Backwards stepping", 0,
               updater->m_backwardsStepping->inner(), [updater](bool value) {
                   updater->m_backwardsStepping->inner() = value;
               });
        toggle("Hold arrows", 1, settings->frameStepperHold,
               [settings](bool value) { settings->frameStepperHold = value; });
        number("Hold delay", 2, settings->frameStepperHoldDelay,
               [settings](double value) {
                   settings->frameStepperHoldDelay = value;
               },
               0, 5);
        number("Steps / second", 3, settings->frameStepperHoldSpeed,
               [settings](double value) {
                   settings->frameStepperHoldSpeed = value;
               },
               1, 240);
        number("Saved frames", 4, settings->stepsToSave,
               [settings](double value) {
                   settings->stepsToSave =
                       std::max(2, static_cast<int>(value));
               },
               2, 10000, 0);
        number("Arrow opacity", 5, settings->frameStepperArrowOpacity,
               [settings](double value) {
                   settings->frameStepperArrowOpacity = value;
               },
               0.1, 1.0);
        toggle("Move stepper", 6, settings->frameStepperMovable,
               [settings](bool value) { settings->frameStepperMovable = value; });
        label("Drag the dots on the right of the stepper to move it", 185.f,
              rowY(7), .26f, {200, 200, 200});
        button("Reset position", 185.f, rowY(8), [] { TouchOverlay::resetPosition(); },
               130.f);
    }

    void buildRendering() {
        auto* renderer = Renderer::get();
        number("Width", 0, renderer->m_settings.m_width,
               [renderer](double value) {
                   renderer->m_settings.m_width = static_cast<int>(value);
               },
               16, 16384, 0);
        number("Height", 1, renderer->m_settings.m_height,
               [renderer](double value) {
                   renderer->m_settings.m_height = static_cast<int>(value);
               },
               16, 16384, 0);
        number("FPS", 2, renderer->m_settings.m_fps,
               [renderer](double value) {
                   renderer->m_settings.m_fps = static_cast<int>(value);
               },
               1, 1000, 0);
        number("Bitrate Mbps", 3,
               renderer->m_settings.m_bitrate / 1000000.0,
               [renderer](double value) {
                   renderer->m_settings.m_bitrate =
                       static_cast<uint32_t>(value * 1000000.0);
               },
               1, 1000, 1);
        text("Video codec", 4, renderer->m_settings.m_codec,
             [renderer](std::string const& value) {
                 renderer->m_settings.m_codec = value;
             });
        text("Container", 5, renderer->m_settings.m_extension,
             [renderer](std::string const& value) {
                 renderer->m_settings.m_extension = value;
             });
        button("Save PC preset", 185.f, rowY(6), [renderer] {
            auto path = grape::paths::directory("presets") /
                        "mobile-render.json";
            renderer->saveSettings(path);
            GrapeSettings::get()->lastLoadedPreset = "mobile-render";
        }, 145.f);
    }

    bool init(Feature feature) {
        m_feature = feature;
        if (!Popup::init(400.f, 260.f, "GJ_square01.png")) return false;
        applyMenuColor(m_bgSprite);
        static constexpr const char* titles[] = {
            "Hitbox Settings", "Trajectory Settings", "Autoclicker Settings",
            "Layout Settings", "Noclip Settings", "Frame Stepper Settings",
            "Render Settings (PC Preset)"};
        setTitle(titles[static_cast<int>(feature)], "goldFont.fnt", .72f,
                 18.f);
        m_scroll = ScrollLayer::create({370.f, 205.f});
        if (!m_scroll) return false;
        m_scroll->setPosition({15.f, 20.f});
        if (feature == Feature::Trajectory) m_top += 58.f;
        if (feature == Feature::Stepper) m_top += 29.f;
        m_scroll->m_contentLayer->setContentSize({370.f, m_top + kHeaderRoom});
        m_content = m_scroll->m_contentLayer;
        m_mainLayer->addChild(m_scroll);
        rebuild();
        return true;
    }

   public:
    static void open(Feature feature) {
        auto* popup = new MobileFeaturePopup();
        if (popup && popup->init(feature)) {
            popup->autorelease();
            popup->show();
        } else {
            CC_SAFE_DELETE(popup);
        }
    }
};

class MobileMacroEditorPopup final : public geode::Popup {
    CCNode* m_content = nullptr;
    CCMenu* m_menu = nullptr;
    int m_selected = 0;
    CCLabelBMFont* m_inputLabel = nullptr;

    Inputs& atom() {
        return GrapeEngine::get()->replay().m_inputs;
    }

    int currentInputIndex() {
        auto* bot = GrapeEngine::get();
        const auto size = atom().m_events.size();
        if (size == 0) return -1;
        if (bot->isRecording()) return static_cast<int>(size - 1);

        const size_t next = bot->replay().m_head.position();
        return next == 0
            ? -1
            : static_cast<int>(std::min(next - 1, size - 1));
    }

    void normalize() {
        auto& replay = GrapeEngine::get()->replay();
        auto& inputs = atom().m_events;
        std::stable_sort(inputs.begin(), inputs.end(), [](const auto& a,
                                                          const auto& b) {
            return a.m_frame < b.m_frame;
        });
        replay.rebindHead();
        if (GrapeEngine::get()->isPlaying()) {
            replay.m_head.seekAfter(
                GrapeEngine::get()->timeline().getFrame());
        } else if (GrapeEngine::get()->isRecording()) {
            replay.m_head.setPosition(inputs.size());
        } else {
            replay.m_head.setPosition(0);
        }
        if (inputs.empty()) {
            m_selected = 0;
        } else {
            m_selected = std::clamp(
                m_selected, 0, static_cast<int>(inputs.size()) - 1);
        }
    }

    CCLabelBMFont* label(std::string const& text, float x, float y,
                         float scale = .34f, ccColor3B color = ccWHITE) {
        auto* node = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        node->setAnchorPoint({0.f, .5f});
        node->setScale(scale);
        node->setColor(color);
        node->setPosition({x, y});
        node->limitLabelWidth(250.f, scale, .14f);
        m_content->addChild(node);
        return node;
    }

    void button(std::string const& text, float x, float y,
                std::function<void()> callback, float width = 82.f) {
        auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(
            makeButtonSprite(text, width),
            [callback = std::move(callback)](CCMenuItemSpriteExtra*) {
                callback();
            });
        item->setPosition({x, y});
        m_menu->addChild(item);
    }

    void toggle(std::string const& text, float x, float y, bool value,
                std::function<void(bool)> callback) {
        label(text, x, y);
        auto* item =
            geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(
                .4f, [callback = std::move(callback)](CCMenuItemToggler* item) {
                    callback(!item->isToggled());
                });
        item->toggle(value);
        item->setPosition({x + 145.f, y});
        m_menu->addChild(item);
    }

    void rebuild() {
        m_inputLabel = nullptr;
        m_content->removeAllChildren();
        m_menu = CCMenu::create();
        m_menu->setPosition({0.f, 0.f});
        m_menu->setAnchorPoint({0.f, 0.f});
        m_menu->setContentSize(m_content->getContentSize());
        m_content->addChild(m_menu, 3);

        auto& inputs = atom().m_events;
        if (inputs.empty()) {
            label("No macro inputs", 24.f, 170.f, .4f, {255, 220, 90});
            button("Add input", 190.f, 105.f, [this] {
                atom().m_events.emplace_back(
                    0, EventKind::Jump, true, false);
                normalize();
                rebuild();
            }, 120.f);
            return;
        }

        m_selected = std::clamp(
            m_selected, 0, static_cast<int>(inputs.size()) - 1);
        auto* input = &inputs[m_selected];
        auto* bot = GrapeEngine::get();
        const int currentInput = currentInputIndex();
        const bool isCurrent = (bot->isRecording() || bot->isPlaying()) &&
                               m_selected == currentInput;
        m_inputLabel = label(
            fmt::format("Input {} / {}", m_selected + 1, inputs.size()),
            24.f, 223.f, .38f,
            isCurrent ? ccColor3B{255, 125, 125}
                      : ccColor3B{255, 220, 90});
        button("<", 285.f, 223.f, [this] {
            const int size = static_cast<int>(atom().m_events.size());
            m_selected = (m_selected + size - 1) % size;
            rebuild();
        }, 34.f);
        button(">", 335.f, 223.f, [this] {
            m_selected = (m_selected + 1) % atom().m_events.size();
            rebuild();
        }, 34.f);

        label("Frame", 24.f, 188.f);
        auto* frame = TextInput::create(105.f, "Frame");
        frame->setScaleY(.84f);
        frame->setCommonFilter(CommonFilter::Int);
        frame->setString(std::to_string(input->m_frame));
        frame->setCallback([this](std::string const& value) {
            double parsed = 0.0;
            if (!parseNumber(value, parsed) || atom().m_events.empty()) return;
            const uint64_t target = static_cast<uint64_t>(std::clamp(
                parsed, 0.0,
                static_cast<double>(std::numeric_limits<uint32_t>::max())));
            atom().m_events[m_selected].m_frame = target;
        });
        frame->setPosition({190.f, 188.f});
        m_content->addChild(frame, 2);

        toggle("Holding / click", 24.f, 153.f, input->m_holding,
               [this](bool value) {
                   if (!atom().m_events.empty())
                       atom().m_events[m_selected].m_holding = value;
               });
        toggle("Player 2", 210.f, 153.f, input->m_player2,
               [this](bool value) {
                   if (!atom().m_events.empty())
                       atom().m_events[m_selected].m_player2 = value;
               });

        using ActionType = EventKind;
        toggle("Left", 24.f, 118.f, input->m_type == ActionType::Left,
               [this](bool value) {
                   if (atom().m_events.empty()) return;
                   auto& selected = atom().m_events[m_selected];
                   selected.m_type = value
                       ? ActionType::Left : ActionType::Jump;
                   rebuild();
               });
        toggle("Right", 210.f, 118.f,
               input->m_type == ActionType::Right, [this](bool value) {
                   if (atom().m_events.empty()) return;
                   auto& selected = atom().m_events[m_selected];
                   selected.m_type = value
                       ? ActionType::Right : ActionType::Jump;
                   rebuild();
               });

        button("Add", 105.f, 88.f, [this] {
            auto& inputs = atom().m_events;
            const auto& selected = inputs[m_selected];
            const auto type = selected.isPlayer()
                ? selected.m_type : EventKind::Jump;
            inputs.insert(inputs.begin() + m_selected + 1,
                grape::vine::Event(selected.m_frame + 1, type,
                      !selected.m_holding, selected.m_player2));
            ++m_selected;
            normalize();
            rebuild();
        }, 72.f);
        button("Delete", 265.f, 88.f, [this] {
            auto& inputs = atom().m_events;
            inputs.erase(inputs.begin() + m_selected);
            normalize();
            rebuild();
        }, 82.f);
        button("Remove P1", 105.f, 55.f, [this] {
            std::erase_if(atom().m_events,
                          [](const auto& action) { return !action.m_player2; });
            normalize();
            rebuild();
        }, 96.f);
        button("Remove P2", 265.f, 55.f, [this] {
            std::erase_if(atom().m_events,
                          [](const auto& action) { return action.m_player2; });
            normalize();
            rebuild();
        }, 96.f);

        button("Flip hold", 105.f, 22.f, [this] {
            for (auto& action : atom().m_events)
                action.m_holding = !action.m_holding;
            rebuild();
        }, 115.f);
        button("Flip players", 265.f, 22.f, [this] {
            for (auto& action : atom().m_events)
                action.m_player2 = !action.m_player2;
            rebuild();
        }, 125.f);
    }

    bool init() override {
        if (!Popup::init(420.f, 305.f, "GJ_square01.png")) return false;
        applyMenuColor(m_bgSprite);
        setTitle("Inputs Editor", "goldFont.fnt", .72f, 18.f);
        m_content = CCNode::create();
        m_content->setPosition({15.f, 20.f});
        m_mainLayer->addChild(m_content);

        const auto& inputs = atom().m_events;
        if (!inputs.empty()) {
            m_selected = std::max(0, currentInputIndex());
        }
        this->scheduleUpdate();
        rebuild();
        return true;
    }

    void update(float) override {
        auto* bot = GrapeEngine::get();
        if ((!bot->isRecording() && !bot->isPlaying()) ||
            atom().m_events.empty()) {
            if (m_inputLabel) m_inputLabel->setColor({255, 220, 90});
            return;
        }

        const int active = currentInputIndex();
        if (m_inputLabel) {
            m_inputLabel->setColor(
                m_selected == active ? ccColor3B{255, 125, 125}
                                     : ccColor3B{255, 220, 90});
        }
    }

    void onClose(CCObject* sender) override {
        normalize();
        Popup::onClose(sender);
    }

   public:
    static void open() {
        auto* popup = new MobileMacroEditorPopup();
        if (popup && popup->init()) {
            popup->autorelease();
            popup->show();
        } else {
            CC_SAFE_DELETE(popup);
        }
    }
};
}

namespace {
constexpr float kSidebarWidth = 96.f;
constexpr float kRowHeight = 26.f;
constexpr float kMargin = 12.f;
constexpr float kControlGap = 6.f;
constexpr float kIconSize = 30.f;
constexpr ccColor4F kIconWhite{1.f, 1.f, 1.f, 1.f};
constexpr ccColor4F kIconRed{1.f, 0.f, 0.f, 1.f};
constexpr ccColor4F kIconOutline{0.f, 0.f, 0.f, 1.f};
constexpr ccColor3B kTabIdle{255, 255, 255};
constexpr ccColor3B kTabActive{120, 175, 255};
constexpr ccColor3B kCounterColor{255, 220, 90};

CCDrawNode* iconCanvas() {
    auto* node = CCDrawNode::create();
    node->setContentSize({kIconSize, kIconSize});
    node->setAnchorPoint({.5f, .5f});
    return node;
}

void drawTray(CCDrawNode* node) {
    node->drawSegment({6.f, 11.f}, {6.f, 5.f}, 2.f, kIconWhite);
    node->drawSegment({6.f, 5.f}, {24.f, 5.f}, 2.f, kIconWhite);
    node->drawSegment({24.f, 5.f}, {24.f, 11.f}, 2.f, kIconWhite);
}

void drawUpload(CCDrawNode* node) {
    node->drawSegment({15.f, 10.f}, {15.f, 25.f}, 2.2f, kIconWhite);
    node->drawSegment({15.f, 25.f}, {9.5f, 19.5f}, 2.2f, kIconWhite);
    node->drawSegment({15.f, 25.f}, {20.5f, 19.5f}, 2.2f, kIconWhite);
    drawTray(node);
}

void drawSave(CCDrawNode* node) {
    node->drawSegment({15.f, 25.f}, {15.f, 11.f}, 2.2f, kIconWhite);
    node->drawSegment({15.f, 11.f}, {9.5f, 16.5f}, 2.2f, kIconWhite);
    node->drawSegment({15.f, 11.f}, {20.5f, 16.5f}, 2.2f, kIconWhite);
    drawTray(node);
}

void drawClose(CCDrawNode* node) {
    node->drawSegment({8.f, 8.f}, {22.f, 22.f}, 2.6f, kIconWhite);
    node->drawSegment({8.f, 22.f}, {22.f, 8.f}, 2.6f, kIconWhite);
}

void drawStop(CCDrawNode* node) {
    CCPoint square[] = {{8.f, 8.f}, {22.f, 8.f}, {22.f, 22.f}, {8.f, 22.f}};
    node->drawPolygon(square, 4, kIconWhite, 1.5f, kIconOutline);
}

void drawRecord(CCDrawNode* node, bool active) {
    node->clear();
    if (active) return drawStop(node);
    node->drawDot({15.f, 15.f}, 10.f, kIconOutline);
    node->drawDot({15.f, 15.f}, 8.f, kIconRed);
}

void drawPlay(CCDrawNode* node, bool active) {
    node->clear();
    if (active) return drawStop(node);
    CCPoint triangle[] = {{10.f, 6.f}, {24.f, 15.f}, {10.f, 24.f}};
    node->drawPolygon(triangle, 3, kIconWhite, 1.5f, kIconOutline);
}

void drawPlus(CCDrawNode* node) {
    node->drawSegment({15.f, 7.f}, {15.f, 23.f}, 2.6f, kIconWhite);
    node->drawSegment({7.f, 15.f}, {23.f, 15.f}, 2.6f, kIconWhite);
}

CCNode* drawnIcon(void (*draw)(CCDrawNode*)) {
    auto* node = iconCanvas();
    draw(node);
    return node;
}

CCMenuItemSpriteExtra* circleButton(CCNode* icon, CircleBaseColor color,
                                    std::function<void()> callback) {
    auto* sprite =
        CircleButtonSprite::create(icon, color, CircleBaseSize::Medium);
    sprite->setScale(.78f);
    return geode::cocos::CCMenuItemExt::createSpriteExtra(
        sprite, [callback = std::move(callback)](CCMenuItemSpriteExtra*) {
            callback();
        });
}

float circleButtonWidth() {
    static const float width =
        CircleButtonSprite::create(CCNode::create(), CircleBaseColor::Green,
                                   CircleBaseSize::Medium)
            ->getContentSize()
            .width *
        .78f;
    return width;
}

CCMenuItemSpriteExtra* gameButton(const char* frame,
                                  std::function<CCNode*()> fallbackIcon,
                                  CircleBaseColor fallbackColor,
                                  std::function<void()> callback) {
    if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frame))
        return circleButton(fallbackIcon(), fallbackColor, std::move(callback));
    auto* sprite = CCSprite::createWithSpriteFrameName(frame);
    const float width = sprite->getContentSize().width;
    if (width > 0.f) sprite->setScale(circleButtonWidth() / width);
    return geode::cocos::CCMenuItemExt::createSpriteExtra(
        sprite, [callback = std::move(callback)](CCMenuItemSpriteExtra*) {
            callback();
        });
}

CCMenuItemSpriteExtra* textButton(std::string const& text, float width,
                                  std::function<void()> callback,
                                  const char* texture = "GJ_button_01.png") {
    auto* sprite = ButtonSprite::create(text.c_str(), static_cast<int>(width),
                                        true, "bigFont.fnt", texture, 24.f,
                                        .55f);
    return geode::cocos::CCMenuItemExt::createSpriteExtra(
        sprite, [callback = std::move(callback)](CCMenuItemSpriteExtra*) {
            callback();
        });
}

CCScale9Sprite* darkPanel(CCSize size, GLubyte opacity = 90) {
    auto* panel = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
    panel->setContentSize(size);
    panel->setColor({0, 0, 0});
    panel->setOpacity(opacity);
    return panel;
}

std::string defaultMacroName() {
    auto* playLayer = PlayLayer::get();
    if (!playLayer || !playLayer->m_level) return "macro";
    return geode::utils::string::replace(
        GrapeSettings::get()->macroNameTemplate, "%name%",
        std::string(playLayer->m_level->m_levelName));
}

class InputLogPopup final : public geode::Popup {
    bool init() override {
        if (!Popup::init(400.f, 250.f, "GJ_square01.png")) return false;
        applyMenuColor(m_bgSprite);
        setTitle("Input Log", "bigFont.fnt", .6f, 22.f);

        const auto lines = grape::trace::recent();
        const CCSize view{370.f, 170.f};
        m_mainLayer->addChildAtPosition(
            darkPanel({view.width + 6.f, view.height + 6.f}), Anchor::Center,
            {0.f, -4.f});
        auto* scroll = ScrollLayer::create(view);
        scroll->setPosition({(m_size.width - view.width) / 2.f,
                             (m_size.height - view.height) / 2.f - 4.f});
        m_mainLayer->addChild(scroll);

        constexpr float lineHeight = 11.f;
        const float total = std::max(
            view.height, lineHeight * static_cast<float>(lines.size()) + 6.f);
        scroll->m_contentLayer->setContentSize({view.width, total});
        for (size_t i = 0; i < lines.size(); ++i) {
            auto* label =
                CCLabelBMFont::create(lines[i].c_str(), "chatFont.fnt");
            label->setAnchorPoint({0.f, .5f});
            label->setScale(.5f);
            label->limitLabelWidth(view.width - 8.f, .5f, .2f);
            label->setPosition(
                {4.f, total - 3.f - lineHeight * (static_cast<float>(i) + .5f)});
            scroll->m_contentLayer->addChild(label);
        }
        scroll->scrollToTop();
        if (lines.empty()) {
            auto* empty = CCLabelBMFont::create(
                "Record or replay something first", "bigFont.fnt");
            empty->setScale(.35f);
            empty->setColor({190, 190, 190});
            m_mainLayer->addChildAtPosition(empty, Anchor::Center, {0.f, -4.f});
        }

        auto* copy = textButton("Copy", 60.f, [] {
            std::string text;
            for (auto const& line : grape::trace::recent()) text += line + "\n";
            geode::utils::clipboard::write(text);
            Notification::create("Input log copied", NotificationIcon::Success)
                ->show();
        });
        m_buttonMenu->addChildAtPosition(copy, Anchor::Bottom, {-45.f, 18.f});
        auto* clear = textButton("Clear", 60.f, [this] {
            grape::trace::clear();
            this->onClose(nullptr);
        }, "GJ_button_06.png");
        m_buttonMenu->addChildAtPosition(clear, Anchor::Bottom, {45.f, 18.f});
        return true;
    }

   public:
    static void open() {
        auto* popup = new InputLogPopup();
        if (popup->init()) {
            popup->autorelease();
            popup->show();
        } else {
            delete popup;
        }
    }
};

class MacroNamePopup final : public geode::Popup {
    TextInput* m_input = nullptr;
    std::function<void(std::string const&)> m_done;

    bool init(std::string const& initial,
              std::function<void(std::string const&)> done) {
        if (!Popup::init(300.f, 160.f, "GJ_square01.png")) return false;
        applyMenuColor(m_bgSprite);
        m_done = std::move(done);
        setTitle("Macro Name", "bigFont.fnt", .7f, 26.f);

        m_input = TextInput::create(230.f, "Macro name");
        m_input->setCommonFilter(CommonFilter::Name);
        m_input->setMaxCharCount(64);
        m_input->setString(initial);
        m_mainLayer->addChildAtPosition(m_input, Anchor::Center, {0.f, 4.f});

        auto* ok = textButton("OK", 60.f, [this] { this->confirm(); });
        m_buttonMenu->addChildAtPosition(ok, Anchor::Bottom, {0.f, 28.f});
        return true;
    }

    void confirm() {
        std::string name = m_input->getString();
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!name.empty() && name.front() == ' ') name.erase(0, 1);
        if (name.empty()) {
            Notification::create("Enter a macro name", NotificationIcon::Error)
                ->show();
            return;
        }
        auto done = m_done;
        this->onClose(nullptr);
        done(name);
    }

   public:
    static void open(std::string const& initial,
                     std::function<void(std::string const&)> done) {
        auto* popup = new MacroNamePopup();
        if (popup->init(initial, std::move(done))) {
            popup->autorelease();
            popup->show();
        } else {
            delete popup;
        }
    }
};

class MacroListPopup final : public geode::Popup {
    std::function<void(std::filesystem::path const&)> m_pick;
    ScrollLayer* m_scroll = nullptr;

    static std::vector<std::filesystem::path> macroFiles() {
        std::vector<std::pair<std::filesystem::file_time_type,
                              std::filesystem::path>> found;
        std::error_code error;
        const auto dir = grape::paths::directory("replays");
        for (auto const& entry :
             std::filesystem::directory_iterator(dir, error)) {
            if (!entry.is_regular_file(error)) continue;
            auto extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(),
                           extension.begin(), [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (extension != ".vine" && extension != ".grape" &&
                extension != ".slc" && extension != ".gdr" &&
                extension != ".json")
                continue;
            found.emplace_back(entry.last_write_time(error), entry.path());
        }
        std::sort(found.begin(), found.end(),
                  [](auto const& a, auto const& b) { return a.first > b.first; });
        std::vector<std::filesystem::path> paths;
        for (auto& item : found) paths.push_back(std::move(item.second));
        return paths;
    }

    static constexpr float kViewWidth = 290.f;
    static constexpr float kViewHeight = 180.f;

    void confirmDelete(std::filesystem::path const& path) {
        geode::Ref<MacroListPopup> self = this;
        geode::createQuickPopup(
            "Delete Macro",
            fmt::format("Delete <cr>{}</c>? This can't be undone.",
                        path.filename().string()),
            "Cancel", "Delete", [self, path](FLAlertLayer*, bool confirmed) {
                if (!confirmed) return;
                std::error_code error;
                if (!std::filesystem::remove(path, error) || error) {
                    geode::Notification::create("Couldn't delete the macro",
                                                geode::NotificationIcon::Error)
                        ->show();
                    return;
                }
                if (self->getParent()) self->fillList();
            });
    }

    CCMenuItemSpriteExtra* deleteButton(std::filesystem::path const& path) {
        const auto callback = [this, path] { confirmDelete(path); };
        if (CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(
                "GJ_trashBtn_001.png")) {
            auto* sprite = CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png");
            sprite->setScale(22.f / sprite->getContentSize().height);
            return geode::cocos::CCMenuItemExt::createSpriteExtra(
                sprite, [callback](CCMenuItemSpriteExtra*) { callback(); });
        }
        auto* item = textButton("Del", 34.f, callback, "GJ_button_06.png");
        item->setScale(.7f);
        item->m_baseScale = .7f;
        return item;
    }

    void fillList() {
        auto* content = m_scroll->m_contentLayer;
        content->removeAllChildren();
        const CCSize view{kViewWidth, kViewHeight};
        const auto files = macroFiles();
        constexpr float entryHeight = 30.f;
        const float total =
            std::max(view.height, entryHeight * static_cast<float>(files.size()));
        content->setContentSize({view.width, total});

        if (files.empty()) {
            auto* empty = CCLabelBMFont::create(
                "No macros in Grape/replays yet", "bigFont.fnt");
            empty->setScale(.4f);
            empty->setColor({190, 190, 190});
            empty->setPosition({view.width / 2.f, total / 2.f});
            content->addChild(empty);
        }

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setAnchorPoint({0.f, 0.f});
        menu->setContentSize({view.width, total});
        content->addChild(menu, 2);
        for (size_t i = 0; i < files.size(); ++i) {
            const auto path = files[i];
            const float y =
                total - entryHeight * (static_cast<float>(i) + .5f);
            const CCSize rowSize{view.width - 8.f, entryHeight - 4.f};

            auto* background = darkPanel(rowSize, 70);
            background->setPosition({view.width / 2.f, y});
            content->addChild(background);

            auto* load = textButton("Load", 44.f, [this, path] {
                auto pick = m_pick;
                this->onClose(nullptr);
                pick(path);
            });
            load->setScale(.7f);
            load->m_baseScale = .7f;
            const float loadHalf = load->getScaledContentSize().width / 2.f;
            load->setPosition({view.width - 8.f - loadHalf, y});
            menu->addChild(load);

            auto* bin = deleteButton(path);
            const float binHalf = bin->getScaledContentSize().width / 2.f;
            bin->setPosition({view.width - 12.f - loadHalf * 2.f - binHalf, y});
            menu->addChild(bin);
            const float buttons = loadHalf * 2.f + binHalf * 2.f + 4.f;

            auto* name = CCLabelBMFont::create(
                path.stem().string().c_str(), "bigFont.fnt");
            name->setAnchorPoint({0.f, .5f});
            name->setScale(.4f);
            name->limitLabelWidth(view.width - buttons - 70.f, .4f, .15f);
            name->setPosition({12.f, y});
            content->addChild(name);

            auto* extension = CCLabelBMFont::create(
                path.extension().string().c_str(), "bigFont.fnt");
            extension->setAnchorPoint({1.f, .5f});
            extension->setScale(.28f);
            extension->setColor({170, 170, 170});
            extension->setPosition({view.width - 16.f - buttons, y});
            content->addChild(extension);
        }
        m_scroll->scrollToTop();
    }

    bool init(std::function<void(std::filesystem::path const&)> pick) {
        if (!Popup::init(330.f, 250.f, "GJ_square01.png")) return false;
        applyMenuColor(m_bgSprite);
        m_pick = std::move(pick);
        setTitle("Load Macro", "bigFont.fnt", .7f, 24.f);

        const CCSize view{kViewWidth, kViewHeight};
        m_mainLayer->addChildAtPosition(
            darkPanel({view.width + 8.f, view.height + 8.f}), Anchor::Center,
            {0.f, -14.f});
        m_scroll = ScrollLayer::create(view);
        m_scroll->setPosition(
            {(m_size.width - view.width) / 2.f,
             (m_size.height - view.height) / 2.f - 14.f});
        m_mainLayer->addChild(m_scroll);
        m_scroll->setStealingTouches(true);
        fillList();
        return true;
    }

   public:
    static void open(std::function<void(std::filesystem::path const&)> pick) {
        auto* popup = new MacroListPopup();
        if (popup->init(std::move(pick))) {
            popup->autorelease();
            popup->show();
        } else {
            delete popup;
        }
    }
};
}

bool MobileMenu::init() {
    const auto winSize = CCDirector::get()->getWinSize();
    const float width = std::min(440.f, winSize.width - 40.f);
    const float height = std::min(262.f, winSize.height - 44.f);
    if (!Popup::init(width, height, "GJ_square01.png")) return false;
    applyMenuColor(m_bgSprite);
    GrapeEngine::get()->timeline().m_ssbFix->inner() = true;

    auto* sidebar = darkPanel({kSidebarWidth, height - kMargin * 2.f});
    sidebar->setAnchorPoint({0.f, 0.f});
    sidebar->setPosition({kMargin, kMargin});
    m_mainLayer->addChild(sidebar);

    m_tabMenu = CCMenu::create();
    m_tabMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_tabMenu, 2);

    const float bodyX = kMargin + kSidebarWidth + 8.f;
    m_title = CCLabelBMFont::create("", "bigFont.fnt");
    m_title->setPosition({bodyX + (width - bodyX - kMargin) / 2.f, height - 26.f});
    m_mainLayer->addChild(m_title);

    m_body = CCNode::create();
    m_body->setPosition({bodyX, kMargin});
    m_body->setContentSize({width - bodyX - kMargin, height - kMargin * 2.f});
    m_mainLayer->addChild(m_body);

    buildTabs();
    this->scheduleUpdate();
    rebuildPage();
    return true;
}

void MobileMenu::buildTabs() {
    static constexpr std::pair<Page, const char*> tabs[] = {
        {Page::Bot, "Bot"},       {Page::Converter, "Converter"},
        {Page::Player, "Player"}, {Page::Visual, "Visual"},
        {Page::Render, "Render"}, {Page::Scripts, "Scripts"}};
    m_tabMenu->removeAllChildren();
    const float top = m_size.height - 34.f;
    for (size_t i = 0; i < std::size(tabs); ++i) {
        auto* label = CCLabelBMFont::create(tabs[i].second, "bigFont.fnt");
        label->setScale(.46f);
        label->limitLabelWidth(kSidebarWidth - 16.f, .46f, .2f);
        label->setColor(tabs[i].first == s_page ? kTabActive : kTabIdle);
        auto* item = CCMenuItemSpriteExtra::create(
            label, this, menu_selector(MobileMenu::onTab));
        item->setTag(static_cast<int>(tabs[i].first));
        item->setAnchorPoint({0.f, .5f});
        item->setPosition({kMargin + 8.f, top - static_cast<float>(i) * 29.f});
        m_tabMenu->addChild(item);
    }

    auto* label = CCLabelBMFont::create("Settings", "bigFont.fnt");
    label->setScale(.46f);
    label->limitLabelWidth(kSidebarWidth - 16.f, .46f, .2f);
    label->setColor(s_page == Page::Settings ? kTabActive : kTabIdle);
    auto* settings = CCMenuItemSpriteExtra::create(
        label, this, menu_selector(MobileMenu::onTab));
    settings->setTag(static_cast<int>(Page::Settings));
    settings->setAnchorPoint({0.f, .5f});
    settings->setPosition({kMargin + 8.f, kMargin + 18.f});
    m_tabMenu->addChild(settings);
}

void MobileMenu::update(float) {
    auto* bot = GrapeEngine::get();
    if (m_frameLabel) {
        m_frameLabel->setString(
            fmt::format("Frame {}  |  {} inputs",
                        bot->timeline().getDisplayFrame(),
                        bot->replay().m_inputs.length())
                .c_str());
    }
    if (m_recordIcon && bot->isRecording() != m_recordingShown) {
        m_recordingShown = bot->isRecording();
        drawRecord(static_cast<CCDrawNode*>(m_recordIcon), m_recordingShown);
    }
    if (m_playIcon && bot->isPlaying() != m_playingShown) {
        m_playingShown = bot->isPlaying();
        drawPlay(static_cast<CCDrawNode*>(m_playIcon), m_playingShown);
    }
    if (m_renderSprite) {
        auto* renderer = Renderer::get();
        m_renderSprite->setString(renderer->isRecording() ? "Stop render"
                                                          : "Start render");
        const auto& saveStatus = renderer->getMobileSaveError();
        const auto isProgress = [](std::string const& text) {
            return text == "Finalizing video..." || text == "Adding audio...";
        };
        if (!saveStatus.empty() &&
            (isProgress(saveStatus) || isProgress(m_status) ||
             saveStatus.starts_with("Render saved") ||
             saveStatus.starts_with("Audio mux failed"))) {
            m_status = saveStatus;
        }
    }
    if (m_statusLabel) m_statusLabel->setString(m_status.c_str());
}

float MobileMenu::rowWidth() const { return m_body->getContentSize().width; }

void MobileMenu::beginRows() {
    const float top = s_page == Page::Bot ? 46.f : 28.f;
    const CCSize view{rowWidth(), m_body->getContentSize().height - top};
    m_scroll = ScrollLayer::create(view);
    m_scroll->setPosition({0.f, 0.f});
    m_body->addChild(m_scroll);
    m_rowMenu = CCMenu::create();
    m_rowMenu->setPosition({0.f, 0.f});
    m_scroll->m_contentLayer->addChild(m_rowMenu, 3);
    m_cursor = 2.f;
}

void MobileMenu::endRows() {
    auto* content = m_scroll->m_contentLayer;
    const float view = m_scroll->getContentSize().height;
    const float used = m_cursor + 2.f;
    const float total = std::max(view, used);
    content->setContentSize({rowWidth(), total});
    m_rowMenu->setAnchorPoint({0.f, 0.f});
    m_rowMenu->setContentSize({rowWidth(), total});
    m_scroll->setTouchEnabled(used > view + .5f);
    for (auto* child : CCArrayExt<CCNode*>(content->getChildren())) {
        if (child == m_rowMenu) continue;
        child->setPositionY(child->getPositionY() + total);
    }
    for (auto* child : CCArrayExt<CCNode*>(m_rowMenu->getChildren()))
        child->setPositionY(child->getPositionY() + total);
    m_scroll->scrollToTop();
}

float MobileMenu::takeRow(float height) {
    const float center = -(m_cursor + height / 2.f);
    m_cursor += height;
    return center;
}

void MobileMenu::sectionLabel(std::string const& text) {
    const float y = takeRow(24.f) - 2.f;
    auto* label = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
    label->setAnchorPoint({0.f, .5f});
    label->setScale(.55f);
    label->setPosition({4.f, y});
    m_scroll->m_contentLayer->addChild(label);
}

void MobileMenu::noteLabel(std::string const& text, ccColor3B color) {
    const float y = takeRow(20.f);
    auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(.3f);
    label->setColor(color);
    label->limitLabelWidth(rowWidth() - 12.f, .3f, .12f);
    label->setPosition({rowWidth() / 2.f, y});
    m_scroll->m_contentLayer->addChild(label);
}

static CCLabelBMFont* rowLabel(std::string const& text, float y,
                               float maxWidth) {
    auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setAnchorPoint({0.f, .5f});
    label->setScale(.4f);
    label->limitLabelWidth(maxWidth, .4f, .15f);
    label->setPosition({8.f, y});
    return label;
}

void MobileMenu::toggleRow(std::string const& text, bool value,
                           std::function<void(bool)> callback,
                           std::function<void()> openSettings) {
    const float y = takeRow(kRowHeight);
    float cursor = rowWidth() - 16.f;

    auto* toggle = geode::cocos::CCMenuItemExt::createTogglerWithStandardSprites(
        .6f, [callback = std::move(callback)](CCMenuItemToggler* item) {
            callback(!item->isToggled());
        });
    toggle->toggle(value);
    toggle->setPosition({cursor, y});
    m_rowMenu->addChild(toggle);
    cursor -= 30.f;

    if (openSettings) {
        auto* more = textButton("...", 22.f, std::move(openSettings),
                                "GJ_button_04.png");
        more->setScale(.8f);
        more->m_baseScale = .8f;
        more->setPosition({cursor, y});
        m_rowMenu->addChild(more);
        cursor -= 30.f;
    }

    m_scroll->m_contentLayer->addChild(rowLabel(text, y, cursor - 14.f));
}

void MobileMenu::numberRow(std::string const& text, double value,
                           std::function<void(double)> setter,
                           double minimum, double maximum, int decimals) {
    const float y = takeRow(kRowHeight);
    constexpr float inputWidth = 78.f;
    auto* input = TextInput::create(inputWidth, "Value");
    input->setScale(.85f);
    input->setCommonFilter(CommonFilter::Float);
    input->setString(fmt::format("{:.{}f}", value, decimals));
    input->setCallback([setter = std::move(setter), minimum,
                        maximum](std::string const& text) {
        double parsed = 0.0;
        if (parseNumber(text, parsed))
            setter(std::clamp(parsed, minimum, maximum));
    });
    input->setPosition({rowWidth() - 8.f - inputWidth * .85f / 2.f, y});
    m_scroll->m_contentLayer->addChild(input, 2);
    m_scroll->m_contentLayer->addChild(
        rowLabel(text, y, rowWidth() - inputWidth - 28.f));
}

void MobileMenu::stepperRow(std::string const& text,
                            std::function<double()> getter,
                            std::function<void(double)> setter, double step,
                            double minimum, double maximum,
                            std::function<std::string(double)> formatter) {
    const float y = takeRow(kRowHeight);
    constexpr float inputWidth = 58.f;
    constexpr float inputScale = .85f;

    auto* input = TextInput::create(inputWidth, "Value");
    input->setScale(inputScale);
    input->setCommonFilter(CommonFilter::Float);
    input->setString(formatter(getter()));
    m_scroll->m_contentLayer->addChild(input, 2);

    const auto apply = [setter, formatter, input, minimum,
                        maximum](double value) {
        value = std::clamp(value, minimum, maximum);
        setter(value);
        input->setString(formatter(value), false);
    };
    input->setCallback([apply](std::string const& text) {
        double value = 0.0;
        if (parseNumber(text, value)) apply(value);
    });

    const auto makeButton = [&](const char* label, double sign) {
        auto* button = textButton(label, 14.f, [getter, apply, step, sign] {
            apply(getter() + step * sign);
        }, "GJ_button_04.png");
        button->setScale(.75f);
        button->m_baseScale = .75f;
        m_rowMenu->addChild(button);
        return button;
    };
    auto* minus = makeButton("-", -1.0);
    auto* plus = makeButton("+", 1.0);
    const float buttonHalf = plus->getScaledContentSize().width / 2.f;
    const float inputHalf = inputWidth * inputScale / 2.f;

    const float plusX = rowWidth() - 8.f - buttonHalf;
    const float inputX = plusX - buttonHalf - kControlGap - inputHalf;
    const float minusX = inputX - inputHalf - kControlGap - buttonHalf;
    plus->setPosition({plusX, y});
    input->setPosition({inputX, y});
    minus->setPosition({minusX, y});
    m_scroll->m_contentLayer->addChild(
        rowLabel(text, y, minusX - buttonHalf - 16.f));
}

void MobileMenu::colorRow(std::string const& text, std::array<float, 4>* target,
                          std::function<void()> changed) {
    const float y = takeRow(kRowHeight);
    float cursor = rowWidth() - 16.f;

    auto* reset = textButton("Reset", 44.f, [this, target, changed] {
        *target = {1.f, 1.f, 1.f, 1.f};
        changed();
        rebuildLater();
    }, "GJ_button_04.png");
    reset->setScale(.7f);
    reset->m_baseScale = .7f;
    const float resetHalf = reset->getScaledContentSize().width / 2.f;
    reset->setPosition({cursor - resetHalf + 6.f, y});
    m_rowMenu->addChild(reset);
    cursor -= resetHalf * 2.f + 4.f;

    auto* square = ColorChannelSprite::create();
    const auto color = toColor(*target);
    square->setColor({color.r, color.g, color.b});
    square->setScale(.55f);
    geode::Ref<ColorChannelSprite> keep = square;
    auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(
        square, [target, keep, changed](CCMenuItemSpriteExtra*) {
            openColorPicker(target, [keep, changed](ccColor3B picked) {
                keep->setColor(picked);
                changed();
            });
        });
    item->setPosition({cursor - 6.f, y});
    m_rowMenu->addChild(item);
    cursor -= 30.f;

    m_scroll->m_contentLayer->addChild(rowLabel(text, y, cursor - 14.f));
}

void MobileMenu::textRow(std::string const& text, std::string value,
                         std::function<void(std::string const&)> setter,
                         float width) {
    const float y = takeRow(kRowHeight);
    auto* input = TextInput::create(width, text);
    input->setScale(.85f);
    input->setCommonFilter(CommonFilter::Name);
    input->setMaxCharCount(64);
    input->setString(value);
    input->setCallback(std::move(setter));
    input->setPosition({rowWidth() - 8.f - width * .85f / 2.f, y});
    m_scroll->m_contentLayer->addChild(input, 2);
    m_scroll->m_contentLayer->addChild(
        rowLabel(text, y, rowWidth() - width * .85f - 28.f));
}

ButtonSprite* MobileMenu::buttonRow(std::string const& text,
                                    std::function<void()> callback,
                                    float width) {
    const float y = takeRow(32.f);
    auto* item = textButton(text, width, std::move(callback));
    item->setPosition({rowWidth() / 2.f, y});
    m_rowMenu->addChild(item);
    return static_cast<ButtonSprite*>(item->getNormalImage());
}

void MobileMenu::rebuildPage() {
    m_frameLabel = nullptr;
    m_statusLabel = nullptr;
    m_recordIcon = nullptr;
    m_playIcon = nullptr;
    m_renderSprite = nullptr;
    m_body->removeAllChildren();
    buildTabs();

    static constexpr const char* titles[] = {"Bot",    "Converter", "Player",
                                             "Visual", "Render",    "Scripts",
                                             "Settings"};
    m_title->setString(titles[static_cast<int>(s_page)]);
    m_title->limitLabelWidth(rowWidth() - 20.f, .8f, .3f);

    if (s_page == Page::Bot) {
        m_frameLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_frameLabel->setScale(.34f);
        m_frameLabel->setColor(kCounterColor);
        m_frameLabel->setPosition(
            {rowWidth() / 2.f, m_body->getContentSize().height - 32.f});
        m_body->addChild(m_frameLabel);
    }

    beginRows();
    switch (s_page) {
        case Page::Bot: buildBotPage(); break;
        case Page::Converter: buildConverterPage(); break;
        case Page::Player: buildPlayerPage(); break;
        case Page::Visual: buildVisualPage(); break;
        case Page::Render: buildRenderPage(); break;
        case Page::Scripts: buildScriptsPage(); break;
        case Page::Settings: buildSettingsPage(); break;
    }
    endRows();
    this->update(0.f);
}

void MobileMenu::notify(std::string const& text, bool failed) {
    Notification::create(text, failed ? NotificationIcon::Error
                                      : NotificationIcon::Success)
        ->show();
}

void MobileMenu::toggleRecording() {
    auto* bot = GrapeEngine::get();
    auto& replay = bot->replay();
    if (bot->isRecording()) {
        replay.setMode(grape::vine::RunState::Idle);
        return;
    }
    if (!replay.m_inputs.m_events.empty()) replay.createBackup();
    replay.setMode(grape::vine::RunState::Recording);
    replay.m_head.setPosition(0);
    if (auto* playLayer = PlayLayer::get()) {
        playLayer->resetLevel();
    } else {
        replay.m_inputs.clipFrom(0);
        replay.rebindHead();
        bot->timeline().resetFrame();
    }
}

void MobileMenu::togglePlayback() {
    auto* bot = GrapeEngine::get();
    auto& replay = bot->replay();
    if (bot->isPlaying()) {
        replay.setMode(grape::vine::RunState::Idle);
        return;
    }
    auto* playLayer = PlayLayer::get();
    if (playLayer && (playLayer->m_hasCompletedLevel ||
                      playLayer->m_levelEndAnimationStarted)) {
        notify("Close the completion screen before playback", true);
        return;
    }
    if (replay.m_inputs.m_events.empty()) {
        notify("This macro has no inputs yet", true);
        return;
    }
    replay.setMode(grape::vine::RunState::Playback);
    replay.m_head.setPosition(0);
    if (playLayer) playLayer->resetLevel();
}

void MobileMenu::saveMacro() {
    auto& replay = GrapeEngine::get()->replay();
    if (replay.m_replayName.empty()) replay.m_replayName = defaultMacroName();
    auto path = replay.getCurrentPath();
    replay.backupExisting(path);
    replay.save(path);
    if (replay.m_lastOperationSucceeded)
        notify(fmt::format("Saved {} ({} inputs)", replay.m_replayName,
                           replay.m_inputs.length()));
    else
        notify("Could not save the macro", true);
}

void MobileMenu::closeMacro() {
    auto& replay = GrapeEngine::get()->replay();
    replay.setMode(grape::vine::RunState::Idle);
    if (!replay.m_inputs.m_events.empty()) replay.createBackup();
    replay.m_inputs = Inputs{};
    replay.rebindHead();
    replay.m_head.setPosition(0);
    s_macroChosen = false;
    rebuildPage();
}

void MobileMenu::createMacro(std::string const& name) {
    auto& replay = GrapeEngine::get()->replay();
    replay.setMode(grape::vine::RunState::Idle);
    if (!replay.m_inputs.m_events.empty()) replay.createBackup();
    replay.m_inputs = Inputs{};
    replay.rebindHead();
    replay.m_head.setPosition(0);
    replay.m_replayName = name;
    s_macroChosen = true;
    notify(fmt::format("Created {}", name));
    rebuildPage();
}

void MobileMenu::loadMacro(std::filesystem::path const& path) {
    auto& replay = GrapeEngine::get()->replay();
    auto* playLayer = PlayLayer::get();
    const bool completionOpen =
        playLayer && (playLayer->m_hasCompletedLevel ||
                      playLayer->m_levelEndAnimationStarted);
    replay.load(path);
    if (!replay.m_lastOperationSucceeded) {
        notify("Could not load that macro", true);
        return;
    }
    s_macroChosen = true;
    if (completionOpen) {
        replay.m_head.setPosition(0);
    } else if (playLayer) {
        playLayer->resetLevel();
    }
    notify(fmt::format("Loaded {} ({} inputs)", replay.m_replayName,
                       replay.m_inputs.length()));
    rebuildPage();
}

void MobileMenu::buildMacroCard() {
    auto* bot = GrapeEngine::get();
    auto& replay = bot->replay();
    const float mid = rowWidth() / 2.f;

    if (!s_macroChosen && replay.m_inputs.m_events.empty()) {
        const float y = takeRow(58.f);
        auto* create = gameButton(
            "GJ_plusBtn_001.png", [] { return drawnIcon(drawPlus); },
            CircleBaseColor::Green, [this] {
                MacroNamePopup::open(defaultMacroName(),
                                     [this](std::string const& name) {
                                         this->createMacro(name);
                                     });
            });
        create->setPosition({mid - 32.f, y});
        m_rowMenu->addChild(create);

        auto* load = gameButton(
            "GJ_shareBtn_001.png", [] { return drawnIcon(drawUpload); },
            CircleBaseColor::Gray, [this] {
                MacroListPopup::open([this](std::filesystem::path const& path) {
                    this->loadMacro(path);
                });
            });
        load->setPosition({mid + 32.f, y});
        m_rowMenu->addChild(load);

        const float labelY = takeRow(24.f);
        auto* none = CCLabelBMFont::create("No Macro Selected", "bigFont.fnt");
        none->setScale(.45f);
        none->setPosition({mid, labelY});
        m_scroll->m_contentLayer->addChild(none);
        return;
    }

    if (replay.m_replayName.empty()) replay.m_replayName = defaultMacroName();
    textRow("Name", replay.m_replayName,
            [&replay](std::string const& value) { replay.m_replayName = value; },
            170.f);

    const float y = takeRow(52.f);
    auto* record = iconCanvas();
    m_recordingShown = bot->isRecording();
    drawRecord(record, m_recordingShown);
    m_recordIcon = record;
    auto* play = iconCanvas();
    m_playingShown = bot->isPlaying();
    drawPlay(play, m_playingShown);
    m_playIcon = play;

    const std::array<CCMenuItemSpriteExtra*, 4> buttons{
        circleButton(record, CircleBaseColor::Green,
                     [this] { this->toggleRecording(); }),
        circleButton(play, CircleBaseColor::Green,
                     [this] { this->togglePlayback(); }),
        gameButton("GJ_myServerBtn_001.png",
                   [] { return drawnIcon(drawSave); }, CircleBaseColor::Blue,
                   [this] { this->saveMacro(); }),
        gameButton("GJ_closeBtn_001.png",
                   [] { return drawnIcon(drawClose); }, CircleBaseColor::Pink,
                   [this] { this->closeMacro(); }),
    };
    for (size_t i = 0; i < buttons.size(); ++i) {
        buttons[i]->setPosition(
            {mid + (static_cast<float>(i) - 1.5f) * 50.f, y});
        m_rowMenu->addChild(buttons[i]);
    }
    const float editorY = takeRow(kRowHeight);
    auto* editor = textButton("...", 22.f, [] { MobileMacroEditorPopup::open(); },
                              "GJ_button_04.png");
    editor->setScale(.8f);
    editor->m_baseScale = .8f;
    editor->setPosition({rowWidth() - 18.f, editorY});
    m_rowMenu->addChild(editor);
    m_scroll->m_contentLayer->addChild(
        rowLabel("Inputs editor", editorY, rowWidth() - 60.f));
}

void MobileMenu::buildBotPage() {
    auto* bot = GrapeEngine::get();
    auto& updater = bot->timeline();
    auto& replay = bot->replay();

    buildMacroCard();

    sectionLabel("Playback");
    numberRow("TPS", updater.m_tps->inner(), [&updater](double value) {
        updater.m_tps->inner() = value;
        updater.m_tps->notifyChange();
    }, 1.0, 100000.0, 0);
    stepperRow("Speed", [&updater] { return updater.m_speedhack->inner(); },
               [&updater](double value) {
                   updater.m_speedhack->inner() = value;
                   updater.m_speedhack->notifyChange();
               },
               .05, .05, 100.0,
               [](double value) { return fmt::format("{:.2f}", value); });
    toggleRow("Audio speedhack", updater.m_speedhackAudio->inner(),
              [&updater](bool value) {
                  updater.m_speedhackAudio->inner() = value;
                  updater.m_speedhackAudio->notifyChange();
              });
    toggleRow("Ignore inputs while playing", replay.m_ignoreInputs->inner(),
              [&replay](bool value) { replay.m_ignoreInputs->inner() = value; });
    buttonRow("Input log", [] { InputLogPopup::open(); }, 120.f);
}

void MobileMenu::buildSettingsPage() {
    auto* bot = GrapeEngine::get();
    auto& updater = bot->timeline();
    auto& replay = bot->replay();

    sectionLabel("Engine");
    toggleRow("Bot enabled", bot->m_enabled->inner(), [bot](bool value) {
        bot->m_enabled->inner() = value;
        bot->m_enabled->notifyChange();
    });
    toggleRow("Real time", updater.m_realTime->inner(),
              [&updater](bool value) { updater.m_realTime->inner() = value; });
    numberRow("Max updates per frame", updater.m_maxUPR->inner(),
              [&updater](double value) {
                  updater.m_maxUPR->inner() = static_cast<uint32_t>(value);
              },
              1.0, 100000.0, 0);

    sectionLabel("Saving");
    toggleRow("Autosave at level end", replay.m_autosaveAtLevelEnd->inner(),
              [&replay](bool value) {
                  replay.m_autosaveAtLevelEnd->inner() = value;
              });
    toggleRow("Periodic backup", replay.m_autosaveAtInterval->inner(),
              [&replay](bool value) {
                  replay.m_autosaveAtInterval->inner() = value;
                  replay.m_autosaveAtInterval->notifyChange();
              });
    numberRow("Backup every (seconds)", replay.m_autosaveInterval->inner(),
              [&replay](double value) {
                  replay.m_autosaveInterval->inner() = value;
                  replay.m_autosaveInterval->notifyChange();
              },
              15.0, 3600.0, 0);
    toggleRow("End screen menu button", GrapeSettings::get()->showEndMenuButton,
              [](bool value) { GrapeSettings::get()->showEndMenuButton = value; });

    sectionLabel("Interface");
    colorRow("Menu background", &GrapeSettings::get()->mobileMenuColor,
             [this] { applyMenuColor(m_bgSprite); });
}

void MobileMenu::buildConverterPage() {
    noteLabel("Turn a macro from another bot into a Grape macro.",
              {230, 230, 230});
    noteLabel("It is saved to Grape/replays and starts playing.");
    noteLabel("GDR  yBot  xdBot  TCBot  zBot  GucciBot  SKK3", kCounterColor);
    noteLabel("Legacy  plain text / CSV", kCounterColor);
    takeRow(8.f);
    buttonRow("Choose macro file", [this] {
        m_status = "Waiting for a file...";
        m_macroPick.spawn(
            geode::utils::file::pick(
                geode::utils::file::PickMode::OpenFile,
                grape::vine::ReplayDirector::converterFileOptions()),
            [this](geode::utils::file::PickResult result) {
                if (result.isErr()) {
                    m_status = "File picker failed";
                    return;
                }
                auto path = std::move(result).unwrap();
                if (!path) {
                    m_status.clear();
                    return;
                }
                auto converted = GrapeEngine::get()->replay().convertAndPlay(
                    grape::paths::toStd(*path));
                if (converted.isOk()) {
                    s_macroChosen = true;
                    m_status = fmt::format(
                        "Converted {} inputs from {}", converted.unwrap(),
                        GrapeEngine::get()->replay().m_lastImportFormat);
                } else {
                    m_status = converted.unwrapErr();
                }
            });
    }, 190.f);

    const float y = takeRow(22.f);
    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(.32f);
    m_statusLabel->setColor({130, 255, 170});
    m_statusLabel->setPosition({rowWidth() / 2.f, y});
    m_scroll->m_contentLayer->addChild(m_statusLabel);
}

void MobileMenu::rebuildLater() {
    geode::Ref<MobileMenu> self = this;
    geode::Loader::get()->queueInMainThread([self] {
        if (self->getParent()) self->rebuildPage();
    });
}

void MobileMenu::buildScriptsPage() {
#if !defined(GEODE_IS_MOBILE)
    noteLabel("Scripts are not available on this platform");
#else
    auto& scripts = grape::scripting::ScriptEngine::get();
    scripts.refresh();
    noteLabel("Lua scripts live in Grape/scripts.", {230, 230, 230});
    noteLabel("Import a .lua file to add it, then switch it on.");
    noteLabel("Script-tab UI, images and 3D models are PC only.", kCounterColor);
    takeRow(6.f);
    buttonRow("Import script", [this] {
        m_scriptPick.spawn(
            geode::utils::file::pick(
                geode::utils::file::PickMode::OpenFile,
                {.defaultPath = std::nullopt,
                 .filters = {{.description = "Lua scripts", .files = {"*.lua"}}}}),
            [this](geode::utils::file::PickResult result) {
                if (result.isErr()) {
                    notify("File picker failed", true);
                    return;
                }
                auto path = std::move(result).unwrap();
                if (!path) return;
                
                if (path->extension() != "lua") {
                    notify("Pick a .lua file", true);
                    return;
                }
                std::error_code error;
                const auto target = grape::paths::directory("scripts") / path->filename().string();
                std::filesystem::copy_file(
                    grape::paths::toStd(*path), target,
                    std::filesystem::copy_options::overwrite_existing, error);
                if (error) {
                    notify("Could not copy the script", true);
                    return;
                }
                notify(fmt::format("Imported {}", target.filename().string()));
                rebuildLater();
            });
    }, 150.f);

    sectionLabel("Scripts");
    const auto list = scripts.scripts();
    if (list.empty()) noteLabel("No scripts yet");
    for (const auto& entry : list) {
        toggleRow(entry.name, entry.loaded, [this, name = entry.name](bool on) {
            auto& engine = grape::scripting::ScriptEngine::get();
            if (on) engine.load(name);
            else engine.unload(name);
            rebuildLater();
        });
        const bool idle = entry.status == "Not loaded";
        noteLabel(entry.status, entry.loaded ? ccColor3B{130, 255, 170}
                                : idle      ? ccColor3B{170, 170, 170}
                                            : ccColor3B{255, 120, 120});
    }
#endif
}

void MobileMenu::buildPlayerPage() {
    auto* bot = GrapeEngine::get();
    auto& updater = bot->timeline();
    auto& replay = bot->replay();
    auto* settings = GrapeSettings::get();
    auto* autoclicker = &bot->autoclicker();

    toggleRow("Noclip", updater.m_noclip->inner(),
              [&updater](bool value) { updater.m_noclip->inner() = value; },
              [] { MobileFeaturePopup::open(MobileFeaturePopup::Feature::Noclip); });
    toggleRow("Autoclicker", autoclicker->m_enabled->inner(),
              [autoclicker](bool value) {
                  autoclicker->m_enabled->inner() = value;
                  crash_log::breadcrumb(fmt::format(
                      "Autoclicker {} (hold={}, release={}, player={}, swifts={})",
                      value ? "enabled" : "disabled", autoclicker->m_holdFrames,
                      autoclicker->m_releaseFrames,
                      static_cast<int>(autoclicker->m_player),
                      autoclicker->m_performSwifts));
              },
              [] {
                  MobileFeaturePopup::open(
                      MobileFeaturePopup::Feature::Autoclicker);
              });
    toggleRow("Frame stepper", settings->frameStepperOverlay,
              [&updater, settings](bool value) {
                  settings->frameStepperOverlay = value;
                  updater.m_backwardsStepping->inner() = value;
                  if (!value && updater.isPaused()) {
                      updater.setPaused(false);
                      updater.m_paused->notifyChange();
                  }
              },
              [] { MobileFeaturePopup::open(MobileFeaturePopup::Feature::Stepper); });
    toggleRow("Intentional death", updater.m_canDie->inner(),
              [&updater](bool value) { updater.m_canDie->inner() = value; });
    toggleRow("No mirror", updater.m_noMirror->inner(),
              [&updater](bool value) { updater.m_noMirror->inner() = value; });
    toggleRow("Mirror inputs", replay.m_mirrorInputs,
              [&replay](bool value) { replay.m_mirrorInputs = value; });
    toggleRow("Mirror inverted", replay.m_mirrorInverted,
              [&replay](bool value) { replay.m_mirrorInverted = value; });
    toggleRow("Maintain gravity", replay.m_maintainGravity,
              [&replay](bool value) { replay.m_maintainGravity = value; });
}

void MobileMenu::buildVisualPage() {
    auto* bot = GrapeEngine::get();
    auto& updater = bot->timeline();
    auto& hitboxes = bot->hitboxes();
    auto& trajectory = bot->trajectory();

    toggleRow("Show hitboxes", hitboxes.m_enabled->inner(),
              [&hitboxes](bool value) { hitboxes.m_enabled->inner() = value; },
              [] { MobileFeaturePopup::open(MobileFeaturePopup::Feature::Hitboxes); });
    toggleRow("Hitbox trail", hitboxes.m_trailEnabled->inner(),
              [&hitboxes](bool value) {
                  hitboxes.m_trailEnabled->inner() = value;
              });
    toggleRow("Snap to Hitbox Trail",
              GrapeSettings::get()->hitboxes.snapPlacedToTrail,
              [](bool value) {
                  GrapeSettings::get()->hitboxes.snapPlacedToTrail = value;
              });
    toggleRow("Trajectory", trajectory.m_state.m_enabled->inner(),
              [&trajectory](bool value) { trajectory.setEnabled(value); },
              [] {
                  MobileFeaturePopup::open(MobileFeaturePopup::Feature::Trajectory);
              });
    toggleRow("Layout mode", updater.m_layoutMode->inner(),
              [&updater](bool value) {
                  updater.m_layoutMode->inner() = value;
                  updater.m_layoutMode->notifyChange();
              },
              [] { MobileFeaturePopup::open(MobileFeaturePopup::Feature::Layout); });
    toggleRow("Extrapolation", updater.m_extrapolateFrames->inner(),
              [&updater](bool value) {
                  updater.m_extrapolateFrames->inner() = value;
              });
    stepperRow("Hitbox width", [&hitboxes] { return hitboxes.m_width->inner(); },
               [&hitboxes](double value) { hitboxes.m_width->inner() = value; },
               .05, .05, 1.0,
               [](double value) { return fmt::format("{:.2f}", value); });
    stepperRow("Path length",
               [&trajectory] { return trajectory.m_state.m_length->inner(); },
               [&trajectory](double value) {
                   trajectory.m_state.m_length->inner() = value;
               },
               .1, .1, 5.0,
               [](double value) { return fmt::format("{:.1f}", value); });
}

void MobileMenu::buildRenderPage() {
    auto* renderer = Renderer::get();
    auto& settings = renderer->m_settings;
    if (!renderer->isRecording() && !renderer->getMobileSaveError().empty())
        m_status = renderer->getMobileSaveError();

#ifdef GEODE_IS_MOBILE
    {
        const float y = takeRow(kRowHeight);
        m_renderResolutionIndex =
            iosRenderResolutionIndex(settings.m_width, settings.m_height);
        const auto resolutionText = [this] {
            const auto& resolution =
                IOS_RENDER_RESOLUTIONS[m_renderResolutionIndex];
            return fmt::format("{}  {}x{}", resolution.m_name,
                               resolution.m_width, resolution.m_height);
        };
        auto* value = CCLabelBMFont::create(resolutionText().c_str(),
                                            "bigFont.fnt");
        value->setScale(.34f);
        value->limitLabelWidth(120.f, .34f, .14f);
        const float valueX = rowWidth() - 84.f;
        value->setPosition({valueX, y});
        m_scroll->m_contentLayer->addChild(value);
        const auto shift = [this, renderer, value, resolutionText](int by) {
            const size_t count = IOS_RENDER_RESOLUTIONS.size();
            m_renderResolutionIndex =
                (m_renderResolutionIndex + count + by) % count;
            const auto& resolution =
                IOS_RENDER_RESOLUTIONS[m_renderResolutionIndex];
            renderer->m_settings.m_width = resolution.m_width;
            renderer->m_settings.m_height = resolution.m_height;
            value->setString(resolutionText().c_str());
        };
        shift(0);
        for (auto [label, x, by] :
             {std::tuple{"<", valueX - 72.f, -1}, std::tuple{">", valueX + 72.f, 1}}) {
            auto* button = textButton(label, 14.f, [shift, by] { shift(by); },
                                      "GJ_button_04.png");
            button->setScale(.75f);
            button->m_baseScale = .75f;
            button->setPosition({x, y});
            m_rowMenu->addChild(button);
        }
        m_scroll->m_contentLayer->addChild(rowLabel("Resolution", y, 90.f));
    }
#else
    numberRow("Width", settings.m_width, [&settings](double value) {
        settings.m_width = std::clamp(static_cast<int>(value), 64, 3840);
    }, 64, 3840, 0);
    numberRow("Height", settings.m_height, [&settings](double value) {
        settings.m_height = std::clamp(static_cast<int>(value), 64, 2160);
    }, 64, 2160, 0);
#endif

#ifdef GEODE_IS_IOS
    settings.m_fps = 240;
#else
    numberRow("FPS", settings.m_fps, [&settings](double value) {
        settings.m_fps = std::clamp(static_cast<int>(value), 1, 240);
    }, 1, 240, 0);
    textRow("Codec", settings.m_codec,
            [&settings](std::string const& value) { settings.m_codec = value; },
            110.f);
    textRow("Container", settings.m_extension,
            [&settings](std::string const& value) {
                settings.m_extension = value;
            },
            110.f);
#endif
    numberRow("Bitrate (Mbps)", settings.m_bitrate / 1'000'000.0,
              [&settings](double value) {
                  settings.m_bitrate = static_cast<uint32_t>(
                      std::clamp(value, 1.0, 200.0) * 1'000'000.0);
              },
              1.0, 200.0, 0);
    numberRow("Seconds after end", settings.m_afterEndTime,
              [&settings](double value) {
                  settings.m_afterEndTime =
                      static_cast<float>(std::clamp(value, 0.0, 30.0));
              },
              0.0, 30.0, 1);
    toggleRow("Show level complete", settings.m_showLevelComplete,
              [&settings](bool value) { settings.m_showLevelComplete = value; });
    toggleRow("Render level only", settings.m_renderOnlyLevel,
              [&settings](bool value) { settings.m_renderOnlyLevel = value; });
    toggleRow("Record audio", settings.m_renderAudio,
              [&settings](bool value) { settings.m_renderAudio = value; });

    m_renderSprite = buttonRow(
        renderer->isRecording() ? "Stop render" : "Start render",
        [this, renderer] {
            if (renderer->isRecording()) {
                renderer->signalStop();
                m_status = renderer->getMobileSaveError().empty()
                               ? "Render saved to Grape/videos"
                               : renderer->getMobileSaveError();
                return;
            }
            auto result = renderer->start();
            m_status = result.isOk()
                           ? "Renderer ready - start or restart the level"
                           : result.unwrapErr();
        },
        170.f);

    const float y = takeRow(16.f);
    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(.28f);
    m_statusLabel->setColor({130, 255, 170});
    m_statusLabel->setPosition({rowWidth() / 2.f, y});
    m_scroll->m_contentLayer->addChild(m_statusLabel);
}

void MobileMenu::onTab(CCObject* sender) {
    const auto page = static_cast<Page>(sender->getTag());
    if (page == s_page) return;
    s_page = page;
    rebuildPage();
}

MobileMenu* MobileMenu::create() {
    auto* ret = new MobileMenu();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

void MobileMenu::open() {
    if (auto* popup = create()) popup->show();
}
