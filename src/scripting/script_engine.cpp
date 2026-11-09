#include "script_engine.hpp"

#include "vine/session.hpp"

#include <Geode/Geode.hpp>
#ifndef GEODE_IS_MOBILE
#include <imgui.h>
#endif

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glaze/glaze.hpp>
#include <limits>
#include <unordered_map>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <Geode/binding/FMODAudioEngine.hpp>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "trajectory/trajectory.hpp"
#ifndef GEODE_IS_MOBILE
#include "ui/lib/stb_image.h"
#include "ui/manager.hpp"
#endif
#include "ui/notifications.hpp"
#include "util/storage.hpp"

using namespace geode::prelude;

namespace grape::scripting {
struct ScriptEngine::Impl {
    struct Memory {
        size_t used = 0;
        static constexpr size_t limit = 16 * 1024 * 1024;
    };

    struct Script {
        Impl* owner;
        std::filesystem::path path;
        std::string status;
        Memory memory;
        lua_State* state = nullptr;
        bool loaded = false;
        bool drawing = false;
        bool overlay = false;
        bool settingsDirty = false;
        std::chrono::steady_clock::time_point settingsChanged{};
        std::unordered_map<std::string, std::string> settings;
        std::vector<std::pair<std::unique_ptr<bool>, ConfigValuePtr<bool>>>
            controls;

        ~Script() {
            if (state) lua_close(state);
        }
    };

#ifndef GEODE_IS_MOBILE
    struct Image {
        unsigned texture = 0;
        int width = 0;
        int height = 0;
        int frame = 0;
        int frameCount = 1;
        double nextFrame = 0;
        std::vector<unsigned char> pixels;
        std::vector<int> delays;
    };

#endif

    struct Sound {
        FMOD::Sound* sound = nullptr;
        FMOD::Channel* channel = nullptr;
        Script* owner = nullptr;
    };

    std::vector<std::unique_ptr<Script>> scripts;
#ifndef GEODE_IS_MOBILE
    std::unordered_map<std::string, Image> images;
#endif
    std::unordered_map<unsigned, Sound> sounds;
    unsigned nextSound = 0;
    unsigned lastNotification = 0;
    bool dispatchingInput = false;

    static std::filesystem::path settingsPath(const Script& script) {
        std::string name = script.path.stem().string();
        for (char& character : name)
            if (!std::isalnum(static_cast<unsigned char>(character)) &&
                character != '-' && character != '_')
                character = '_';
        return grape::paths::directory("scripts") / "settings" /
               (name.substr(0, 80) + ".json");
    }

    static void readSettings(Script& script) {
        script.settings.clear();
        const auto path = settingsPath(script);
        if (!std::filesystem::exists(path)) return;
        const auto error = glz::read_file_json(
            script.settings, path.string(), std::string{});
        if (error)
            geode::log::warn("Could not read Lua settings for {}",
                             script.path.filename().string());
    }

    static void writeSettings(Script& script) {
        if (!script.settingsDirty) return;
        std::filesystem::create_directories(settingsPath(script).parent_path());
        const auto error = glz::write_file_json<glz::opts{.prettify = true}>(
            script.settings, settingsPath(script).string(), std::string{});
        if (!error) script.settingsDirty = false;
    }

    static void* allocate(void* data, void* pointer, size_t oldSize,
                          size_t newSize) {
        auto& memory = *static_cast<Memory*>(data);
        const size_t accountedOld = pointer ? oldSize : 0;
        if (!newSize) {
            memory.used -= std::min(memory.used, accountedOld);
            std::free(pointer);
            return nullptr;
        }
        if (newSize > accountedOld &&
            memory.used + newSize - accountedOld > Memory::limit)
            return nullptr;
        void* result = std::realloc(pointer, newSize);
        if (result)
            memory.used = memory.used - std::min(memory.used, accountedOld) + newSize;
        return result;
    }

    static Script& script(lua_State* state) {
        return **static_cast<Script**>(lua_getextraspace(state));
    }

    static PlayerObject* player(lua_State* state, int argument = 1) {
        auto* layer = GJBaseGameLayer::get();
        if (!layer) luaL_error(state, "no level is active");
        const int index = static_cast<int>(luaL_checkinteger(state, argument));
        if (index == 1) return layer->m_player1;
        if (index == 2 && layer->m_player2) return layer->m_player2;
        luaL_error(state, "player must be 1 or 2");
        return nullptr;
    }

    static int log(lua_State* state) {
        geode::log::info("[Lua:{}] {}", script(state).path.filename().string(),
                         luaL_checkstring(state, 1));
        return 0;
    }

    static float colorChannel(lua_State* state, int table, int index,
                              float fallback) {
        lua_rawgeti(state, table, index);
        const float value =
            static_cast<float>(luaL_optnumber(state, -1, fallback));
        lua_pop(state, 1);
        if (!std::isfinite(value)) luaL_error(state, "colour must be finite");
        return std::clamp(value, 0.0f, 1.0f);
    }

    static void notifyOptions(lua_State* state, int index, float accent[4],
                              float& life) {
        if (!lua_istable(state, index)) return;
        lua_getfield(state, index, "color");
        if (lua_istable(state, -1)) {
            const int table = lua_gettop(state);
            accent[0] = colorChannel(state, table, 1, 0.0);
            accent[1] = colorChannel(state, table, 2, 0.0);
            accent[2] = colorChannel(state, table, 3, 0.0);
            accent[3] = colorChannel(state, table, 4, 1.0);
        }
        lua_pop(state, 1);
        lua_getfield(state, index, "seconds");
        const double seconds = luaL_optnumber(state, -1, 0.0);
        lua_pop(state, 1);
        if (!std::isfinite(seconds) || seconds < 0.0 || seconds > 600.0)
            luaL_error(state, "seconds must be between 0 and 600");
        life = static_cast<float>(seconds);
    }

    static int notify(lua_State* state) {
        std::string title = luaL_checkstring(state, 1);
        std::string body = luaL_optstring(state, 2, "");
        float accent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float life = 0.0f;
        notifyOptions(state, 3, accent, life);
#ifdef GEODE_IS_MOBILE
        geode::Notification::create(body.empty() ? title : title + ": " + body,
                                    geode::NotificationIcon::None,
                                    life > 0.0f ? life : 4.0f)
            ->show();
#endif
        lua_pushinteger(state, static_cast<lua_Integer>(
            grape::ui::NotificationCenter::get().push(
                std::move(title), std::move(body), accent, life)));
        return 1;
    }

    static grape::ui::Notice* notice(lua_State* state) {
        return grape::ui::NotificationCenter::get().find(
            static_cast<unsigned>(luaL_checkinteger(state, 1)));
    }

    static int notifyUpdate(lua_State* state) {
        auto* target = notice(state);
        if (target) {
            if (!lua_isnoneornil(state, 2))
                target->m_title = luaL_checkstring(state, 2);
            if (!lua_isnoneornil(state, 3))
                target->m_body = luaL_checkstring(state, 3);
        }
        lua_pushboolean(state, target != nullptr);
        return 1;
    }

    static float argChannel(lua_State* state, int index, float fallback) {
        const float value =
            static_cast<float>(luaL_optnumber(state, index, fallback));
        if (!std::isfinite(value)) luaL_error(state, "colour must be finite");
        return std::clamp(value, 0.0f, 1.0f);
    }

    static int notifyColor(lua_State* state) {
        auto* target = notice(state);
        if (target) {
            target->m_accent[0] = argChannel(state, 2, 0.0f);
            target->m_accent[1] = argChannel(state, 3, 0.0f);
            target->m_accent[2] = argChannel(state, 4, 0.0f);
            target->m_accent[3] = argChannel(state, 5, 1.0f);
        }
        lua_pushboolean(state, target != nullptr);
        return 1;
    }

    static int notifyDismiss(lua_State* state) {
        lua_pushboolean(state, grape::ui::NotificationCenter::get().dismiss(
            static_cast<unsigned>(luaL_checkinteger(state, 1))));
        return 1;
    }

    static int notifyCall(lua_State* state) {
        lua_remove(state, 1);
        return notify(state);
    }

    static int notifyClear(lua_State* state) {
        grape::ui::NotificationCenter::get().clear();
        (void)state;
        return 0;
    }

    static int frame(lua_State* state) {
        lua_pushinteger(state, static_cast<lua_Integer>(
            GrapeEngine::get()->timeline().getDisplayFrame()));
        return 1;
    }

    static int inLevel(lua_State* state) {
        lua_pushboolean(state, GJBaseGameLayer::get() &&
            GrapeEngine::get()->trajectory().unsafeInner());
        return 1;
    }

    static int tps(lua_State* state) {
        auto value = GrapeEngine::get()->timeline().m_tps;
        if (lua_gettop(state)) {
            value->inner() = std::clamp(luaL_checknumber(state, 1), 1.0, 1000000.0);
            value->notifyChange();
        }
        lua_pushnumber(state, value->inner());
        return 1;
    }

    static int speed(lua_State* state) {
        auto value = GrapeEngine::get()->timeline().m_speedhack;
        if (lua_gettop(state)) {
            value->inner() = std::clamp(luaL_checknumber(state, 1), 0.01, 100.0);
            value->notifyChange();
        }
        lua_pushnumber(state, value->inner());
        return 1;
    }

    static int input(lua_State* state) {
        const int index = static_cast<int>(luaL_checkinteger(state, 1));
        const int button = static_cast<int>(luaL_checkinteger(state, 2));
        if (index < 1 || index > 2 || button < 1 || button > 3)
            return luaL_error(state, "invalid player or button");
        auto* layer = GJBaseGameLayer::get();
        if (!layer) return luaL_error(state, "no level is active");
        layer->handleButton(lua_toboolean(state, 3), button, index == 1);
        return 0;
    }

    static int playerGet(lua_State* state) {
        auto* p = player(state);
        std::string_view field(luaL_checkstring(state, 2));
        if (field == "x") lua_pushnumber(state, p->getPositionX());
        else if (field == "y") lua_pushnumber(state, p->getPositionY());
        else if (field == "rotation") lua_pushnumber(state, p->getRotation());
        else if (field == "y_velocity") lua_pushnumber(state, p->m_yVelocity);
        else if (field == "x_velocity") lua_pushnumber(state, p->m_platformerXVelocity);
        else if (field == "gravity") lua_pushnumber(state, p->m_gravity);
        else if (field == "scale") lua_pushnumber(state, p->getScale());
        else if (field == "opacity") lua_pushinteger(state, p->getOpacity());
        else if (field == "visible") lua_pushboolean(state, p->isVisible());
        else if (field == "dead") lua_pushboolean(state, p->m_isDead);
        else if (field == "holding") lua_pushboolean(state,
            p->m_holdingButtons[static_cast<int>(PlayerButton::Jump)]);
        else if (field == "upside_down") lua_pushboolean(state, p->m_isUpsideDown);
        else if (field == "going_left") lua_pushboolean(state, p->m_isGoingLeft);
        else if (field == "cube") lua_pushboolean(state, p->isInNormalMode());
        else if (field == "ship") lua_pushboolean(state, p->m_isShip);
        else if (field == "ufo") lua_pushboolean(state, p->m_isBird);
        else if (field == "ball") lua_pushboolean(state, p->m_isBall);
        else if (field == "wave") lua_pushboolean(state, p->m_isDart);
        else if (field == "robot") lua_pushboolean(state, p->m_isRobot);
        else if (field == "spider") lua_pushboolean(state, p->m_isSpider);
        else if (field == "swing") lua_pushboolean(state, p->m_isSwing);
        else if (field == "platformer") lua_pushboolean(state, p->m_isPlatformer);
        else return luaL_error(state, "unknown player field");
        return 1;
    }

    static int playerSet(lua_State* state) {
        auto* p = player(state);
        std::string_view field(luaL_checkstring(state, 2));
        if (field == "visible") {
            p->setVisible(lua_toboolean(state, 3));
            return 0;
        }
        const double raw = luaL_checknumber(state, 3);
        if (!std::isfinite(raw)) return luaL_error(state, "value must be finite");
        if (field == "x") p->setPositionX(std::clamp(raw, -1e9, 1e9));
        else if (field == "y") p->setPositionY(std::clamp(raw, -1e9, 1e9));
        else if (field == "rotation") p->setRotation(static_cast<float>(raw));
        else if (field == "y_velocity") p->m_yVelocity = std::clamp(raw, -1e6, 1e6);
        else if (field == "x_velocity") p->m_platformerXVelocity = std::clamp(raw, -1e6, 1e6);
        else if (field == "gravity") p->m_gravity = std::clamp(raw, -100.0, 100.0);
        else if (field == "scale") p->setScale(static_cast<float>(std::clamp(raw, 0.05, 10.0)));
        else if (field == "opacity") p->setOpacity(static_cast<unsigned char>(std::clamp(raw, 0.0, 255.0)));
        else return luaL_error(state, "unknown or read-only player field");
        return 0;
    }

    static int trajectory(lua_State* state) {
        auto* layer = GJBaseGameLayer::get();
        auto* simulator = GrapeEngine::get()->trajectory().unsafeInner();
        if (!layer || !simulator) return luaL_error(state, "no level is active");

        const int index = static_cast<int>(luaL_checkinteger(state, 1));
        if (index < 1 || index > 2 || (index == 2 && !layer->m_player2))
            return luaL_error(state, "player must be 1 or 2");
        const std::string_view action(luaL_checkstring(state, 2));
        const int delay = static_cast<int>(luaL_checkinteger(state, 3));
        const int frames = static_cast<int>(luaL_checkinteger(state, 4));
        if (delay < 0 || frames < 1 || frames > 600 || delay >= frames)
            return luaL_error(state, "require 0 <= delay < frames <= 600");

        using Mode = Trajectory::TrajectoryMode;
        int mode = index == 1 ? Mode::Player1 : Mode::Player2;
        if (action == "click") mode |= Mode::Release;
        else if (action == "release") mode |= Mode::Hold;
        else return luaL_error(state, "action must be click or release");

        FlipPlan flips;
        flips.push(delay);

        const auto result = simulator->simulate(
            layer, index == 1, mode, false,
            {.m_bypassConfig = true,
             .m_maxLength = frames,
             .m_draw = false,
             .m_flipPlan = flips});
        lua_createtable(state, 0, 7);
        const auto number = [&](const char* key, double value) {
            lua_pushnumber(state, value);
            lua_setfield(state, -2, key);
        };
        number("x", result.position.x);
        number("y", result.position.y);
        number("frames", result.score);
        number("min_y", result.minY);
        number("max_y", result.maxY);
        lua_pushboolean(state, !result.dead);
        lua_setfield(state, -2, "survived");
        return 1;
    }

    static std::shared_ptr<BindingInterface> menuValue(lua_State* state) {
        const std::string tag(luaL_checkstring(state, 1));
        auto value = BindingManager::get()->getValue(tag);
        if (!value) luaL_error(state, "unknown menu control: %s", tag.c_str());
        return value;
    }

    static int menuList(lua_State* state) {
        const auto tags = BindingManager::get()->getValueTags();
        lua_createtable(state, static_cast<int>(tags.size()), 0);
        int index = 1;
        for (const auto& tag : tags) {
            lua_pushlstring(state, tag.data(), tag.size());
            lua_rawseti(state, -2, index++);
        }
        return 1;
    }

    static int menuGet(lua_State* state) {
        auto value = menuValue(state);
        if (auto* item = dynamic_cast<ConfigValue<bool>*>(value.get()))
            lua_pushboolean(state, item->inner());
        else if (auto* item = dynamic_cast<ConfigValue<int>*>(value.get()))
            lua_pushinteger(state, item->inner());
        else if (auto* item = dynamic_cast<ConfigValue<uint32_t>*>(value.get()))
            lua_pushinteger(state, item->inner());
        else if (auto* item = dynamic_cast<ConfigValue<float>*>(value.get()))
            lua_pushnumber(state, item->inner());
        else if (auto* item = dynamic_cast<ConfigValue<double>*>(value.get()))
            lua_pushnumber(state, item->inner());
        else if (auto* item = dynamic_cast<ConfigValue<std::string>*>(value.get()))
            lua_pushlstring(state, item->inner().data(), item->inner().size());
        else
            return luaL_error(state, "unsupported menu control type");
        return 1;
    }

    static int menuSet(lua_State* state) {
        auto value = menuValue(state);
        if (auto* item = dynamic_cast<ConfigValue<bool>*>(value.get()))
            item->inner() = lua_toboolean(state, 2);
        else if (auto* item = dynamic_cast<ConfigValue<int>*>(value.get())) {
            const auto number = luaL_checkinteger(state, 2);
            if (number < std::numeric_limits<int>::min() ||
                number > std::numeric_limits<int>::max())
                return luaL_error(state, "value is outside int range");
            item->inner() = static_cast<int>(number);
        }
        else if (auto* item = dynamic_cast<ConfigValue<uint32_t>*>(value.get())) {
            const auto number = luaL_checkinteger(state, 2);
            if (number < 0 || static_cast<uint64_t>(number) > UINT32_MAX)
                return luaL_error(state, "value is outside uint32 range");
            item->inner() = static_cast<uint32_t>(number);
        } else if (auto* item = dynamic_cast<ConfigValue<float>*>(value.get())) {
            const double number = luaL_checknumber(state, 2);
            if (!std::isfinite(number)) return luaL_error(state, "value must be finite");
            item->inner() = static_cast<float>(number);
        } else if (auto* item = dynamic_cast<ConfigValue<double>*>(value.get())) {
            const double number = luaL_checknumber(state, 2);
            if (!std::isfinite(number)) return luaL_error(state, "value must be finite");
            item->inner() = number;
        } else if (auto* item = dynamic_cast<ConfigValue<std::string>*>(value.get())) {
            size_t size = 0;
            const char* text = luaL_checklstring(state, 2, &size);
            if (size > 4096) return luaL_error(state, "value is too long");
            item->inner().assign(text, size);
        } else
            return luaL_error(state, "unsupported menu control type");
        value->notifyChange();
        return 0;
    }

    static int menuRegister(lua_State* state) {
        auto& owner = script(state);
        std::string name = luaL_checkstring(state, 1);
        if (name.empty() || name.size() > 40)
            return luaL_error(state, "name must be 1-40 characters");
        for (char& character : name)
            if (!std::isalnum(static_cast<unsigned char>(character)) &&
                character != '_')
                character = '_';

        std::string stem = owner.path.stem().string();
        for (char& character : stem)
            if (!std::isalnum(static_cast<unsigned char>(character)) &&
                character != '_')
                character = '_';
        const std::string tag =
            "script." + stem.substr(0, 40) + "." + name;

        for (auto& [value, binding] : owner.controls) {
            if (binding->getId() != tag) continue;
            lua_pushlstring(state, tag.data(), tag.size());
            return 1;
        }

        auto value = std::make_unique<bool>(lua_toboolean(state, 2));
        auto binding = ConfigValue<bool>::create(tag, value.get());
        owner.controls.emplace_back(std::move(value), binding);
        lua_pushlstring(state, tag.data(), tag.size());
        return 1;
    }

    static int menuToggle(lua_State* state) {
        auto value = menuValue(state);
        auto* item = dynamic_cast<ConfigValue<bool>*>(value.get());
        if (!item) return luaL_error(state, "menu.toggle requires a boolean control");
        item->inner() = !item->inner();
        item->notifyChange();
        lua_pushboolean(state, item->inner());
        return 1;
    }

    static int menuPress(lua_State* state) {
        auto value = menuValue(state);
        auto* item = dynamic_cast<ConfigValue<bool>*>(value.get());
        if (!item) return luaL_error(state, "menu.press requires a boolean control");
        item->inner() = true;
        item->notifyChange();
        return 0;
    }

    static int menuMode(lua_State* state) {
        auto* engine = GrapeEngine::get();
        if (lua_gettop(state)) {
            const std::string_view mode(luaL_checkstring(state, 1));
            if (mode == "stopped") engine->replay().setMode(grape::vine::RunState::Idle);
            else if (mode == "recording") engine->replay().setMode(grape::vine::RunState::Recording);
            else if (mode == "playing") engine->replay().setMode(grape::vine::RunState::Playback);
            else return luaL_error(state, "mode must be stopped, recording or playing");
        }
        lua_pushstring(state, engine->replay().isRecording() ? "recording" :
                              engine->replay().isPlaying() ? "playing" : "stopped");
        return 1;
    }

    static grape::vine::Inputs& macroInputs() {
        return GrapeEngine::get()->replay().m_inputs;
    }

    static int macroLength(lua_State* state) {
        lua_pushinteger(state, static_cast<lua_Integer>(macroInputs().m_events.size()));
        return 1;
    }

    static int macroPosition(lua_State* state) {
        lua_pushinteger(state, static_cast<lua_Integer>(
            GrapeEngine::get()->replay().m_head.position()));
        return 1;
    }

    static int macroTps(lua_State* state) {
        if (lua_gettop(state)) {
            const lua_Integer frame = luaL_checkinteger(state, 1);
            if (frame < 0) return luaL_error(state, "frame must not be negative");
            lua_pushnumber(state, macroInputs().tpsAt(static_cast<uint64_t>(frame)));
        } else {
            lua_pushnumber(state, macroInputs().m_tps);
        }
        return 1;
    }

    static int macroPlatformer(lua_State* state) {
        lua_pushboolean(state, macroInputs().m_platformer);
        return 1;
    }

    static int macroSeed(lua_State* state) {
        lua_pushnumber(state, static_cast<lua_Number>(macroInputs().m_seed));
        return 1;
    }

    static int macroName(lua_State* state) {
        lua_pushstring(state, GrapeEngine::get()->replay().m_replayName.c_str());
        return 1;
    }

    static int macroEvent(lua_State* state) {
        const auto& events = macroInputs().m_events;
        const lua_Integer index = luaL_checkinteger(state, 1);
        if (index < 1 || static_cast<size_t>(index) > events.size()) {
            lua_pushnil(state);
            return 1;
        }
        const auto& event = events[static_cast<size_t>(index - 1)];
        lua_newtable(state);
        lua_pushinteger(state, static_cast<lua_Integer>(event.m_frame));
        lua_setfield(state, -2, "frame");
        lua_pushstring(state, macroKindName(event.m_type));
        lua_setfield(state, -2, "type");
        lua_pushboolean(state, event.m_holding);
        lua_setfield(state, -2, "holding");
        lua_pushboolean(state, event.m_player2);
        lua_setfield(state, -2, "player2");
        lua_pushboolean(state, event.m_swift);
        lua_setfield(state, -2, "swift");
        if (event.m_type == grape::vine::EventKind::Tps) {
            lua_pushnumber(state, event.m_tps);
            lua_setfield(state, -2, "tps");
        }
        return 1;
    }

    static const char* macroKindName(grape::vine::EventKind kind) {
        using grape::vine::EventKind;
        switch (kind) {
            case EventKind::Jump: return "jump";
            case EventKind::Left: return "left";
            case EventKind::Right: return "right";
            case EventKind::Restart: return "restart";
            case EventKind::RestartFull: return "restart_full";
            case EventKind::Death: return "death";
            case EventKind::Tps: return "tps";
            case EventKind::Bugpoint: return "bugpoint";
        }
        return "unknown";
    }

    static int storageGet(lua_State* state) {
        auto& owner = script(state);
        const std::string key(luaL_checkstring(state, 1));
        if (key.empty() || key.size() > 64)
            return luaL_error(state, "setting key must contain 1-64 characters");
        const auto found = owner.settings.find(key);
        if (found == owner.settings.end()) {
            luaL_checkany(state, 2);
            lua_pushvalue(state, 2);
            return 1;
        }
        const std::string_view value(found->second);
        if (value == "b:1" || value == "b:0")
            lua_pushboolean(state, value.back() == '1');
        else if (value.starts_with("n:")) {
            if (lua_stringtonumber(state, found->second.c_str() + 2) == 0) {
                lua_pushvalue(state, 2);
                return 1;
            }
            const lua_Number number = lua_tonumber(state, -1);
            lua_pop(state, 1);
            lua_pushnumber(state, number);
        } else if (value.starts_with("s:"))
            lua_pushlstring(state, value.data() + 2, value.size() - 2);
        else
            lua_pushvalue(state, 2);
        return 1;
    }

    static int storageSet(lua_State* state) {
        auto& owner = script(state);
        const std::string key(luaL_checkstring(state, 1));
        if (key.empty() || key.size() > 64)
            return luaL_error(state, "setting key must contain 1-64 characters");
        std::string encoded;
        switch (lua_type(state, 2)) {
            case LUA_TBOOLEAN:
                encoded = lua_toboolean(state, 2) ? "b:1" : "b:0";
                break;
            case LUA_TNUMBER: {
                const double number = lua_tonumber(state, 2);
                if (!std::isfinite(number))
                    return luaL_error(state, "setting number must be finite");
                encoded = fmt::format("n:{}", number);
                break;
            }
            case LUA_TSTRING: {
                size_t length = 0;
                const char* value = lua_tolstring(state, 2, &length);
                if (length > 4096)
                    return luaL_error(state, "setting string is too long");
                encoded = "s:" + std::string(value, length);
                break;
            }
            default:
                return luaL_error(state, "settings support booleans, numbers and strings");
        }
        owner.settings[key] = std::move(encoded);
        owner.settingsDirty = true;
        owner.settingsChanged = std::chrono::steady_clock::now();
        return 0;
    }

    static std::filesystem::path assetPath(
        lua_State* state, const char* folder, const char* name,
        std::initializer_list<const char*> allowed) {
        const std::string_view file(name ? name : "");
        if (file.empty() || file.size() > 120) {
            luaL_error(state, "file name is missing or too long");
            return {};
        }
        if (file.find('/') != std::string_view::npos ||
            file.find('\\') != std::string_view::npos ||
            file.find(':') != std::string_view::npos ||
            file.find("..") != std::string_view::npos) {
            luaL_error(state, "file must be a plain name in the %s folder",
                       folder);
            return {};
        }
        const auto path =
            grape::paths::directory("scripts") / folder / std::string(file);
        const std::string extension = path.extension().string();
        for (const char* candidate : allowed)
            if (extension == candidate) return path;
        luaL_error(state, "unsupported file type for the %s folder", folder);
        return {};
    }

    static constexpr size_t MAX_SOUNDS_PER_SCRIPT = 32;
    static constexpr uintmax_t AUDIO_STREAM_THRESHOLD = 1024 * 1024;
    static constexpr uintmax_t MAX_AUDIO_BYTES = 64ull * 1024 * 1024;

    static FMOD::System* audioSystem(lua_State* state) {
        auto* engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) {
            luaL_error(state, "the audio engine is not running");
            return nullptr;
        }
        return engine->m_system;
    }

    Sound* soundFor(unsigned handle) {
        const auto found = sounds.find(handle);
        return found == sounds.end() ? nullptr : &found->second;
    }

    void reapSounds() {
        for (auto it = sounds.begin(); it != sounds.end();) {
            bool playing = false;
            if (it->second.channel &&
                it->second.channel->isPlaying(&playing) == FMOD_OK && playing) {
                ++it;
                continue;
            }
            if (it->second.sound) it->second.sound->release();
            it = sounds.erase(it);
        }
    }

    void stopSounds(Script& owner) {
        for (auto it = sounds.begin(); it != sounds.end();) {
            if (it->second.owner != &owner) {
                ++it;
                continue;
            }
            if (it->second.channel) it->second.channel->stop();
            if (it->second.sound) it->second.sound->release();
            it = sounds.erase(it);
        }
    }

    static double optionNumber(lua_State* state, int index, const char* name,
                               double fallback, double low, double high) {
        if (!lua_istable(state, index)) return fallback;
        lua_getfield(state, index, name);
        const double value = luaL_optnumber(state, -1, fallback);
        lua_pop(state, 1);
        if (!std::isfinite(value) || value < low || value > high)
            luaL_error(state, "%s must be between %g and %g", name, low, high);
        return value;
    }

    static bool optionFlag(lua_State* state, int index, const char* name,
                           bool fallback) {
        if (!lua_istable(state, index)) return fallback;
        lua_getfield(state, index, name);
        const bool value =
            lua_isnil(state, -1) ? fallback : lua_toboolean(state, -1);
        lua_pop(state, 1);
        return value;
    }

    static int startSound(lua_State* state, const std::filesystem::path& path,
                          int options) {
        auto& owner = script(state);
        auto& impl = *owner.owner;
        impl.reapSounds();

        size_t mine = 0;
        for (const auto& entry : impl.sounds)
            if (entry.second.owner == &owner) ++mine;
        if (mine >= MAX_SOUNDS_PER_SCRIPT)
            return luaL_error(state, "too many sounds are already playing");

        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) {
            lua_pushnil(state);
            return 1;
        }
        if (size > MAX_AUDIO_BYTES) return luaL_error(state, "sound is too large");

        const double volume = optionNumber(state, options, "volume", 1.0, 0.0, 1.0);
        const double pitch = optionNumber(state, options, "pitch", 1.0, 0.01, 10.0);
        const double start = optionNumber(state, options, "start", 0.0, 0.0, 86400.0);
        const bool loop = optionFlag(state, options, "loop", false);

        auto* system = audioSystem(state);
        FMOD::Sound* sound = nullptr;
        const FMOD_MODE mode = FMOD_2D |
            (size > AUDIO_STREAM_THRESHOLD ? FMOD_CREATESTREAM
                                           : FMOD_CREATESAMPLE) |
            (loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
        if (system->createSound(path.string().c_str(), mode, nullptr, &sound) !=
                FMOD_OK ||
            !sound) {
            lua_pushnil(state);
            return 1;
        }

        FMOD::Channel* channel = nullptr;
        if (system->playSound(sound, nullptr, true, &channel) != FMOD_OK ||
            !channel) {
            sound->release();
            lua_pushnil(state);
            return 1;
        }
        channel->setVolume(static_cast<float>(volume));
        channel->setPitch(static_cast<float>(pitch));
        if (start > 0.0)
            channel->setPosition(static_cast<unsigned>(start * 1000.0),
                                 FMOD_TIMEUNIT_MS);
        channel->setPaused(false);

        const unsigned handle = ++impl.nextSound;
        impl.sounds.emplace(handle, Sound{sound, channel, &owner});
        lua_pushinteger(state, static_cast<lua_Integer>(handle));
        return 1;
    }

    static int audioPlay(lua_State* state) {
        const auto path = assetPath(state, "sounds", luaL_checkstring(state, 1),
                                    {".mp3", ".ogg", ".wav", ".flac"});
        return startSound(state, path, 2);
    }

    static int audioPlaySfx(lua_State* state) {
        const std::string_view name(luaL_checkstring(state, 1));
        if (name.empty() || name.size() > 120 ||
            name.find('/') != std::string_view::npos ||
            name.find('\\') != std::string_view::npos ||
            name.find("..") != std::string_view::npos)
            return luaL_error(state, "sound must be a plain file name");
        const auto resolved = CCFileUtils::sharedFileUtils()->fullPathForFilename(
            std::string(name).c_str(), false);
        const std::filesystem::path path{std::string(resolved.c_str())};
        if (path.empty() || !std::filesystem::exists(path)) {
            lua_pushnil(state);
            return 1;
        }
        return startSound(state, path, 2);
    }

    static Sound* audioHandle(lua_State* state) {
        auto& impl = *script(state).owner;
        auto* found = impl.soundFor(
            static_cast<unsigned>(luaL_checkinteger(state, 1)));
        return found && found->owner == &script(state) ? found : nullptr;
    }

    static int audioStop(lua_State* state) {
        auto* entry = audioHandle(state);
        if (entry) {
            auto& impl = *script(state).owner;
            if (entry->channel) entry->channel->stop();
            if (entry->sound) entry->sound->release();
            impl.sounds.erase(static_cast<unsigned>(luaL_checkinteger(state, 1)));
        }
        lua_pushboolean(state, entry != nullptr);
        return 1;
    }

    static int audioStopAll(lua_State* state) {
        auto& owner = script(state);
        owner.owner->stopSounds(owner);
        return 0;
    }

    static int audioVolume(lua_State* state) {
        auto* entry = audioHandle(state);
        if (!entry || !entry->channel) {
            lua_pushnil(state);
            return 1;
        }
        if (!lua_isnoneornil(state, 2)) {
            const double value = luaL_checknumber(state, 2);
            if (!std::isfinite(value) || value < 0.0 || value > 1.0)
                return luaL_error(state, "volume must be between 0 and 1");
            entry->channel->setVolume(static_cast<float>(value));
        }
        float volume = 0.0f;
        entry->channel->getVolume(&volume);
        lua_pushnumber(state, volume);
        return 1;
    }

    static int audioPitch(lua_State* state) {
        auto* entry = audioHandle(state);
        if (!entry || !entry->channel) {
            lua_pushnil(state);
            return 1;
        }
        if (!lua_isnoneornil(state, 2)) {
            const double value = luaL_checknumber(state, 2);
            if (!std::isfinite(value) || value < 0.01 || value > 10.0)
                return luaL_error(state, "pitch must be between 0.01 and 10");
            entry->channel->setPitch(static_cast<float>(value));
        }
        float pitch = 0.0f;
        entry->channel->getPitch(&pitch);
        lua_pushnumber(state, pitch);
        return 1;
    }

    static int audioPosition(lua_State* state) {
        auto* entry = audioHandle(state);
        if (!entry || !entry->channel) {
            lua_pushnil(state);
            return 1;
        }
        if (!lua_isnoneornil(state, 2)) {
            const double value = luaL_checknumber(state, 2);
            if (!std::isfinite(value) || value < 0.0 || value > 86400.0)
                return luaL_error(state, "position must be in seconds");
            entry->channel->setPosition(static_cast<unsigned>(value * 1000.0),
                                        FMOD_TIMEUNIT_MS);
        }
        unsigned position = 0;
        entry->channel->getPosition(&position, FMOD_TIMEUNIT_MS);
        lua_pushnumber(state, position / 1000.0);
        return 1;
    }

    static int audioLength(lua_State* state) {
        auto* entry = audioHandle(state);
        unsigned length = 0;
        if (!entry || !entry->sound ||
            entry->sound->getLength(&length, FMOD_TIMEUNIT_MS) != FMOD_OK) {
            lua_pushnil(state);
            return 1;
        }
        lua_pushnumber(state, length / 1000.0);
        return 1;
    }

    static int audioPaused(lua_State* state) {
        auto* entry = audioHandle(state);
        if (!entry || !entry->channel) {
            lua_pushnil(state);
            return 1;
        }
        if (!lua_isnoneornil(state, 2))
            entry->channel->setPaused(lua_toboolean(state, 2));
        bool paused = false;
        entry->channel->getPaused(&paused);
        lua_pushboolean(state, paused);
        return 1;
    }

    static int audioPlaying(lua_State* state) {
        auto* entry = audioHandle(state);
        bool playing = false;
        if (entry && entry->channel) entry->channel->isPlaying(&playing);
        lua_pushboolean(state, playing);
        return 1;
    }

#ifndef GEODE_IS_MOBILE
    static void requireDraw(lua_State* state) {
        auto& owner = script(state);
        if (!owner.drawing || owner.overlay)
            luaL_error(state, "this ui function requires on_draw");
    }

    static int uiText(lua_State* state) {
        requireDraw(state);
        ImGui::TextWrapped("%s", luaL_checkstring(state, 1));
        return 0;
    }

    static int uiButton(lua_State* state) {
        requireDraw(state);
        lua_pushboolean(state, ImGui::Button(luaL_checkstring(state, 1)));
        return 1;
    }

    static int uiCheckbox(lua_State* state) {
        requireDraw(state);
        bool value = lua_toboolean(state, 2);
        ImGui::Checkbox(luaL_checkstring(state, 1), &value);
        lua_pushboolean(state, value);
        return 1;
    }

    static bool filledSlider(const char* label, double& value, double minimum,
                             double maximum) {
        const float width = ImGui::CalcItemWidth();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + width, min.y + ImGui::GetFrameHeight());
        ImGui::InvisibleButton(label, ImVec2(width, ImGui::GetFrameHeight()));
        const double before = value;
        if (maximum > minimum && ImGui::IsItemActive() &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const double ratio = std::clamp(
                (ImGui::GetIO().MousePos.x - min.x) / width, 0.0f, 1.0f);
            value = minimum + (maximum - minimum) * ratio;
        }
        const float ratio = maximum > minimum
            ? static_cast<float>(std::clamp(
                  (value - minimum) / (maximum - minimum), 0.0, 1.0))
            : 0.0f;
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, max, IM_COL32(24, 21, 24, 255), 4.0f);
        draw->AddRect(min, max, IM_COL32(34, 34, 34, 255), 4.0f);
        if (ratio > 0.0f) {
            const auto& accent = GrapeSettings::get()->grapeAccent;
            draw->AddRectFilled(
                ImVec2(min.x + 2.0f, min.y + 2.0f),
                ImVec2(min.x + 2.0f + (width - 4.0f) * ratio, max.y - 2.0f),
                ImGui::ColorConvertFloat4ToU32(
                    {accent[0], accent[1], accent[2], accent[3]}),
                3.0f);
        }
        char text[64]{};
        std::snprintf(text, sizeof(text), "%.3f", value);
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 textPos(min.x + (width - textSize.x) * .5f,
                             min.y + (max.y - min.y - textSize.y) * .5f);
        draw->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f),
                      IM_COL32(0, 0, 0, 190), text);
        draw->AddText(textPos, IM_COL32_WHITE, text);
        const char* marker = std::strstr(label, "##");
        const char* visibleEnd = marker ? marker : label + std::strlen(label);
        if (visibleEnd != label) {
            draw->AddText(
                ImVec2(max.x + ImGui::GetStyle().ItemInnerSpacing.x,
                       min.y + (max.y - min.y - ImGui::GetFontSize()) * .5f),
                ImGui::GetColorU32(ImGuiCol_Text), label, visibleEnd);
        }
        return value != before;
    }

    static int uiSlider(lua_State* state) {
        requireDraw(state);
        double value = luaL_checknumber(state, 2);
        const double minimum = luaL_checknumber(state, 3);
        const double maximum = luaL_checknumber(state, 4);
        if (slui::Config::get().customMode)
            filledSlider(luaL_checkstring(state, 1), value, minimum, maximum);
        else
            ImGui::SliderScalar(luaL_checkstring(state, 1), ImGuiDataType_Double,
                                &value, &minimum, &maximum, "%.3f",
                                ImGuiSliderFlags_AlwaysClamp);
        lua_pushnumber(state, value);
        return 1;
    }

    static int uiInput(lua_State* state) {
        requireDraw(state);
        const char* label = luaL_checkstring(state, 1);
        double value = luaL_checknumber(state, 2);
        {
            ImGui::InputDouble(label, &value, 0.0, 0.0, "%.3f",
                               ImGuiInputTextFlags_AutoSelectAll);
        }
        lua_pushnumber(state, value);
        return 1;
    }

    static int uiSameLine(lua_State* state) {
        requireDraw(state);
        ImGui::SameLine();
        return 0;
    }

    static int uiSeparator(lua_State* state) {
        requireDraw(state);
        ImGui::Separator();
        return 0;
    }

    static void upload(Image& image) {
        GLint previous = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
        if (!image.texture) glGenTextures(1, &image.texture);
        glBindTexture(GL_TEXTURE_2D, image.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        const auto* frame = image.pixels.data() +
            static_cast<size_t>(image.frame) * image.width * image.height * 4;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, frame);
        glBindTexture(GL_TEXTURE_2D, previous);
    }

    Image* image(Script& owner, const std::string& name) {
        const std::filesystem::path relative(name);
        if (relative.is_absolute()) return nullptr;
        for (const auto& part : relative)
            if (part == "..") return nullptr;
        const auto path = owner.path.parent_path() / relative;
        const auto key = path.lexically_normal().string();
        if (auto found = images.find(key); found != images.end()) return &found->second;
        std::ifstream file(path, std::ios::binary);
        if (!file) return nullptr;
        std::vector<unsigned char> bytes(
            std::istreambuf_iterator<char>(file), {});
        Image result;
        int channels = 0;
        unsigned char* decoded = nullptr;
        if (path.extension() == ".gif") {
            int* delays = nullptr;
            decoded = stbi_load_gif_from_memory(
                bytes.data(), static_cast<int>(bytes.size()), &delays,
                &result.width, &result.height, &result.frameCount, &channels, 4);
            if (delays) {
                result.delays.assign(delays, delays + result.frameCount);
                stbi_image_free(delays);
            }
        } else {
            decoded = stbi_load_from_memory(
                bytes.data(), static_cast<int>(bytes.size()), &result.width,
                &result.height, &channels, 4);
        }
        if (!decoded || result.width <= 0 || result.height <= 0 ||
            result.frameCount <= 0) {
            stbi_image_free(decoded);
            return nullptr;
        }
        const size_t size = static_cast<size_t>(result.width) * result.height *
                            result.frameCount * 4;
        result.pixels.assign(decoded, decoded + size);
        stbi_image_free(decoded);
        upload(result);
        return &images.emplace(key, std::move(result)).first->second;
    }

    static void animate(Image& image) {
        if (image.frameCount <= 1) return;
        const double now = ImGui::GetTime() * 1000.0;
        if (!image.nextFrame)
            image.nextFrame = now + std::max(10, image.delays.empty() ? 100 : image.delays[0]);
        if (now < image.nextFrame) return;
        image.frame = (image.frame + 1) % image.frameCount;
        image.nextFrame = now + std::max(10, image.delays.empty() ? 100 : image.delays[image.frame]);
        upload(image);
    }

    static int uiImage(lua_State* state) {
        requireDraw(state);
        auto& owner = script(state);
        auto* image = owner.owner->image(owner, luaL_checkstring(state, 1));
        if (!image) return luaL_error(state, "image could not be loaded");
        animate(*image);
        const float width = static_cast<float>(luaL_optnumber(state, 2, image->width));
        const float height = static_cast<float>(luaL_optnumber(state, 3, image->height));
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(image->texture)),
                     {width, height});
        return 0;
    }

    static int uiCornerImage(lua_State* state) {
        auto& owner = script(state);
        if (!owner.drawing || !owner.overlay)
            return luaL_error(state, "corner_image requires on_overlay");
        auto* image = owner.owner->image(owner, luaL_checkstring(state, 1));
        if (!image) return luaL_error(state, "image could not be loaded");
        const std::string_view corner(luaL_checkstring(state, 2));
        const float width = static_cast<float>(luaL_optnumber(state, 3, image->width));
        const float height = static_cast<float>(luaL_optnumber(state, 4, image->height));
        const float padding = static_cast<float>(luaL_optnumber(state, 5, 12.0));
        if (!std::isfinite(width) || !std::isfinite(height) ||
            !std::isfinite(padding) || width <= 0 || height <= 0 ||
            width > 4096 || height > 4096 || padding < 0 || padding > 1024)
            return luaL_error(state, "invalid image size or padding");

        const auto* viewport = ImGui::GetMainViewport();
        ImVec2 position(viewport->Pos.x + padding, viewport->Pos.y + padding);
        if (corner == "top_right" || corner == "bottom_right")
            position.x = viewport->Pos.x + viewport->Size.x - width - padding;
        if (corner == "bottom_left" || corner == "bottom_right")
            position.y = viewport->Pos.y + viewport->Size.y - height - padding;
        if (corner != "top_left" && corner != "top_right" &&
            corner != "bottom_left" && corner != "bottom_right")
            return luaL_error(state, "invalid corner");

        animate(*image);
        ImGui::GetForegroundDrawList()->AddImage(
            ImTextureRef(static_cast<ImTextureID>(image->texture)), position,
            ImVec2(position.x + width, position.y + height));
        return 0;
    }

    static GJBaseGameLayer* requireLevelDraw(lua_State* state) {
        auto& owner = script(state);
        if (!owner.drawing || !owner.overlay)
            luaL_error(state, "level drawing requires on_overlay");
        auto* layer = GJBaseGameLayer::get();
        if (!layer || !layer->m_objectLayer)
            luaL_error(state, "no level is active");
        return layer;
    }

    static GJBaseGameLayer* requireLevel(lua_State* state) {
        auto* layer = GJBaseGameLayer::get();
        if (!layer || !layer->m_objectLayer)
            luaL_error(state, "no level is active");
        return layer;
    }

    static ImVec2 worldToScreen(GJBaseGameLayer* layer, float x, float y) {
        const auto world = layer->m_objectLayer->convertToWorldSpace({x, y});
        const auto size = CCDirector::get()->getWinSize();
        const auto* viewport = ImGui::GetMainViewport();
        return {
            viewport->Pos.x + world.x / size.width * viewport->Size.x,
            viewport->Pos.y + (1.0f - world.y / size.height) * viewport->Size.y,
        };
    }

    static float worldSize(GJBaseGameLayer* layer, float x, float y,
                           float size) {
        const auto a = worldToScreen(layer, x, y);
        const auto b = worldToScreen(layer, x + size, y);
        return std::hypot(b.x - a.x, b.y - a.y);
    }

    static float levelNumber(lua_State* state, int argument) {
        const float value = static_cast<float>(luaL_checknumber(state, argument));
        if (!std::isfinite(value)) luaL_error(state, "coordinate must be finite");
        return value;
    }

    static float levelWidth(lua_State* state, int argument) {
        const float value = static_cast<float>(luaL_optnumber(state, argument, 1.0));
        if (!std::isfinite(value) || value <= 0 || value > 10000)
            luaL_error(state, "width must be between 0 and 10000");
        return value;
    }

    static ImU32 levelColor(lua_State* state, int argument) {
        float color[4] = {
            static_cast<float>(luaL_checknumber(state, argument)),
            static_cast<float>(luaL_checknumber(state, argument + 1)),
            static_cast<float>(luaL_checknumber(state, argument + 2)),
            static_cast<float>(luaL_optnumber(state, argument + 3, 1.0)),
        };
        for (float& channel : color) {
            if (!std::isfinite(channel))
                luaL_error(state, "color must be finite");
            channel = std::clamp(channel, 0.0f, 1.0f);
        }
        return ImGui::ColorConvertFloat4ToU32(
            {color[0], color[1], color[2], color[3]});
    }

    static std::vector<ImVec2> levelPoints(lua_State* state,
                                           GJBaseGameLayer* layer) {
        luaL_checktype(state, 1, LUA_TTABLE);
        const size_t count = lua_rawlen(state, 1);
        if (count < 4 || count > 8192 || count % 2)
            luaL_error(state, "points must contain 2-4096 x,y pairs");
        std::vector<ImVec2> points;
        points.reserve(count / 2);
        for (size_t i = 1; i <= count; i += 2) {
            lua_rawgeti(state, 1, i);
            lua_rawgeti(state, 1, i + 1);
            const float x = static_cast<float>(luaL_checknumber(state, -2));
            const float y = static_cast<float>(luaL_checknumber(state, -1));
            lua_pop(state, 2);
            if (!std::isfinite(x) || !std::isfinite(y))
                luaL_error(state, "points must be finite");
            points.push_back(worldToScreen(layer, x, y));
        }
        return points;
    }

    static cocos2d::CCPoint screenToWorld(GJBaseGameLayer* layer, float x,
                                          float y) {
        const auto size = CCDirector::get()->getWinSize();
        const auto* viewport = ImGui::GetMainViewport();
        const float windowX =
            (x - viewport->Pos.x) / viewport->Size.x * size.width;
        const float windowY =
            (1.0f - (y - viewport->Pos.y) / viewport->Size.y) * size.height;
        return layer->m_objectLayer->convertToNodeSpace({windowX, windowY});
    }

    static bool onScreen(ImVec2 point, float slack) {
        const auto* viewport = ImGui::GetMainViewport();
        return point.x >= viewport->Pos.x - slack &&
               point.y >= viewport->Pos.y - slack &&
               point.x <= viewport->Pos.x + viewport->Size.x + slack &&
               point.y <= viewport->Pos.y + viewport->Size.y + slack;
    }

    static int levelWorldToScreen(lua_State* state) {
        auto* layer = requireLevel(state);
        const float x = levelNumber(state, 1);
        const float y = levelNumber(state, 2);
        const auto point = worldToScreen(layer, x, y);
        lua_pushnumber(state, point.x);
        lua_pushnumber(state, point.y);
        lua_pushboolean(state, onScreen(point, 0.0f));
        lua_pushnumber(state, worldSize(layer, x, y, 1.0f));
        return 4;
    }

    static int levelScreenToWorld(lua_State* state) {
        auto* layer = requireLevel(state);
        const auto point = screenToWorld(layer, levelNumber(state, 1),
                                         levelNumber(state, 2));
        lua_pushnumber(state, point.x);
        lua_pushnumber(state, point.y);
        return 2;
    }

    static int levelToScreenMany(lua_State* state) {
        auto* layer = requireLevel(state);
        luaL_checktype(state, 1, LUA_TTABLE);
        const size_t count = lua_rawlen(state, 1);
        if (count % 2 || count > 8192)
            return luaL_error(state, "points must be up to 4096 x,y pairs");
        lua_createtable(state, static_cast<int>(count), 0);
        for (size_t i = 1; i <= count; i += 2) {
            lua_rawgeti(state, 1, static_cast<lua_Integer>(i));
            lua_rawgeti(state, 1, static_cast<lua_Integer>(i + 1));
            const float x = static_cast<float>(luaL_checknumber(state, -2));
            const float y = static_cast<float>(luaL_checknumber(state, -1));
            lua_pop(state, 2);
            if (!std::isfinite(x) || !std::isfinite(y))
                return luaL_error(state, "points must be finite");
            const auto point = worldToScreen(layer, x, y);
            lua_pushnumber(state, point.x);
            lua_rawseti(state, -2, static_cast<lua_Integer>(i));
            lua_pushnumber(state, point.y);
            lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
        }
        return 1;
    }

    static int levelCamera(lua_State* state) {
        auto* layer = requireLevel(state);
        const auto* viewport = ImGui::GetMainViewport();
        const auto center = screenToWorld(
            layer, viewport->Pos.x + viewport->Size.x * .5f,
            viewport->Pos.y + viewport->Size.y * .5f);
        const auto left = screenToWorld(layer, viewport->Pos.x,
                                        viewport->Pos.y + viewport->Size.y * .5f);
        const auto bottom = screenToWorld(
            layer, viewport->Pos.x + viewport->Size.x * .5f,
            viewport->Pos.y + viewport->Size.y);

        lua_createtable(state, 0, 6);
        const auto field = [&](const char* name, double value) {
            lua_pushnumber(state, value);
            lua_setfield(state, -2, name);
        };
        field("x", center.x);
        field("y", center.y);
        field("width", std::hypot(center.x - left.x, center.y - left.y) * 2.0);
        field("height",
              std::hypot(center.x - bottom.x, center.y - bottom.y) * 2.0);
        field("zoom", layer->m_objectLayer->getScale());
        field("rotation", layer->m_objectLayer->getRotation());
        return 1;
    }

    static int levelLine(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const float x1 = levelNumber(state, 1);
        const float y1 = levelNumber(state, 2);
        const float x2 = levelNumber(state, 3);
        const float y2 = levelNumber(state, 4);
        const float width = levelWidth(state, 9);
        ImGui::GetBackgroundDrawList()->AddLine(
            worldToScreen(layer, x1, y1), worldToScreen(layer, x2, y2),
            levelColor(state, 5), std::max(0.5f, worldSize(layer, x1, y1, width)));
        return 0;
    }

    static int levelPolyline(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const auto points = levelPoints(state, layer);
        const float width = levelWidth(state, 7);
        ImGui::GetBackgroundDrawList()->AddPolyline(
            points.data(), static_cast<int>(points.size()), levelColor(state, 2),
            lua_toboolean(state, 6) ? ImDrawFlags_Closed : 0,
            std::max(0.5f, worldSize(layer, 0, 0, width)));
        return 0;
    }

    static int levelPolygon(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const auto points = levelPoints(state, layer);
        if (points.size() < 3) return luaL_error(state, "polygon needs 3 points");
        auto* draw = ImGui::GetBackgroundDrawList();
        const ImU32 color = levelColor(state, 2);
        if (lua_isnoneornil(state, 6) || lua_toboolean(state, 6))
            draw->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), color);
        else
            draw->AddPolyline(
                points.data(), static_cast<int>(points.size()), color,
                ImDrawFlags_Closed,
                std::max(0.5f, worldSize(layer, 0, 0,
                    levelWidth(state, 7))));
        return 0;
    }

    static int levelRect(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const float x1 = levelNumber(state, 1);
        const float y1 = levelNumber(state, 2);
        const float x2 = levelNumber(state, 3);
        const float y2 = levelNumber(state, 4);
        const ImVec2 points[4] = {
            worldToScreen(layer, x1, y1), worldToScreen(layer, x2, y1),
            worldToScreen(layer, x2, y2), worldToScreen(layer, x1, y2),
        };
        auto* draw = ImGui::GetBackgroundDrawList();
        const ImU32 color = levelColor(state, 5);
        if (lua_isnoneornil(state, 9) || lua_toboolean(state, 9))
            draw->AddQuadFilled(points[0], points[1], points[2], points[3], color);
        else
            draw->AddPolyline(points, 4, color, ImDrawFlags_Closed,
                std::max(0.5f, worldSize(layer, x1, y1,
                    levelWidth(state, 10))));
        return 0;
    }

    static int levelCircle(lua_State* state) {
        auto* layer = requireLevelDraw(state);

        const float x = levelNumber(state, 1);
        const float y = levelNumber(state, 2);
        const float radius = levelNumber(state, 3);
        if (radius <= 0 || radius > 10000)
            return luaL_error(state, "invalid circle position or radius");
        const auto center = worldToScreen(layer, x, y);
        const float screenRadius = worldSize(layer, x, y, radius);
        const ImU32 color = levelColor(state, 4);
        const int segments = std::clamp(
            static_cast<int>(luaL_optinteger(state, 10, 0)), 0, 256);
        auto* draw = ImGui::GetBackgroundDrawList();
        if (lua_isnoneornil(state, 8) || lua_toboolean(state, 8))
            draw->AddCircleFilled(center, screenRadius, color, segments);
        else
            draw->AddCircle(center, screenRadius, color, segments,
                std::max(0.5f, worldSize(layer, x, y,
                    levelWidth(state, 9))));
        return 0;
    }

    static int levelText(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const float x = levelNumber(state, 1);
        const float y = levelNumber(state, 2);
        const char* text = luaL_checkstring(state, 3);
        const float size = worldSize(layer, x, y, levelWidth(state, 4));
        ImVec2 position = worldToScreen(layer, x, y);
        if (lua_toboolean(state, 9)) {
            const auto measured = ImGui::GetFont()->CalcTextSizeA(
                size, std::numeric_limits<float>::max(), 0.0f, text);
            position.x -= measured.x * 0.5f;
            position.y -= measured.y * 0.5f;
        }
        ImGui::GetBackgroundDrawList()->AddText(
            ImGui::GetFont(), size, position, levelColor(state, 5), text);
        return 0;
    }

    static int levelImage(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        auto& owner = script(state);
        auto* image = owner.owner->image(owner, luaL_checkstring(state, 1));
        if (!image) return luaL_error(state, "image could not be loaded");
        const float x = levelNumber(state, 2);
        const float y = levelNumber(state, 3);
        const float width = static_cast<float>(luaL_optnumber(state, 4, image->width));
        const float height = static_cast<float>(luaL_optnumber(state, 5, image->height));
        const float rawAlpha = static_cast<float>(luaL_optnumber(state, 6, 1.0));
        if (!std::isfinite(rawAlpha)) return luaL_error(state, "alpha must be finite");
        const float alpha = std::clamp(rawAlpha, 0.0f, 1.0f);
        if (!std::isfinite(x) || !std::isfinite(y) ||
            !std::isfinite(width) || !std::isfinite(height) ||
            width <= 0 || height <= 0 || width > 10000 || height > 10000)
            return luaL_error(state, "invalid image position or size");
        animate(*image);
        ImGui::GetBackgroundDrawList()->AddImageQuad(
            ImTextureRef(static_cast<ImTextureID>(image->texture)),
            worldToScreen(layer, x, y + height),
            worldToScreen(layer, x + width, y + height),
            worldToScreen(layer, x + width, y),
            worldToScreen(layer, x, y), {0, 0}, {1, 0}, {1, 1}, {0, 1},
            IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f)));
        return 0;
    }




#else
    struct Canvas {
        geode::Ref<CCDrawNode> m_draw;
        std::vector<geode::Ref<CCLabelBMFont>> m_labels;
        size_t m_usedLabels = 0;
        float m_fontHeight = 0.0f;
    };
    Canvas canvas;

    static constexpr int CANVAS_Z = 9999;
    static constexpr size_t MAX_LABELS = 256;

    bool beginCanvas() {
        auto* layer = GJBaseGameLayer::get();
        if (!layer || !layer->m_objectLayer) {
            if (canvas.m_draw && canvas.m_draw->getParent())
                canvas.m_draw->removeFromParentAndCleanup(true);
            for (auto& label : canvas.m_labels)
                if (label->getParent()) label->removeFromParentAndCleanup(true);
            canvas.m_labels.clear();
            return false;
        }
        if (!canvas.m_draw || canvas.m_draw->getParent() != layer->m_objectLayer) {
            if (canvas.m_draw && canvas.m_draw->getParent())
                canvas.m_draw->removeFromParentAndCleanup(true);
            for (auto& label : canvas.m_labels)
                if (label->getParent()) label->removeFromParentAndCleanup(true);
            canvas.m_labels.clear();
            canvas.m_draw = CCDrawNode::create();
            layer->m_objectLayer->addChild(canvas.m_draw, CANVAS_Z);
        }
        canvas.m_draw->clear();
        canvas.m_usedLabels = 0;
        return true;
    }

    void endCanvas() {
        for (size_t i = canvas.m_usedLabels; i < canvas.m_labels.size(); ++i)
            canvas.m_labels[i]->setVisible(false);
    }

    static Canvas& canvasOf(lua_State* state) { return script(state).owner->canvas; }

    static GJBaseGameLayer* requireLevelDraw(lua_State* state) {
        auto& owner = script(state);
        if (!owner.drawing || !owner.overlay)
            luaL_error(state, "level drawing requires on_overlay");
        auto* layer = GJBaseGameLayer::get();
        if (!layer || !layer->m_objectLayer || !owner.owner->canvas.m_draw)
            luaL_error(state, "no level is active");
        return layer;
    }

    static GJBaseGameLayer* requireLevel(lua_State* state) {
        auto* layer = GJBaseGameLayer::get();
        if (!layer || !layer->m_objectLayer)
            luaL_error(state, "no level is active");
        return layer;
    }

    static CCPoint worldToScreen(GJBaseGameLayer* layer, float x, float y) {
        const auto world = layer->m_objectLayer->convertToWorldSpace({x, y});
        return {world.x, CCDirector::get()->getWinSize().height - world.y};
    }

    static CCPoint screenToWorld(GJBaseGameLayer* layer, float x, float y) {
        return layer->m_objectLayer->convertToNodeSpace(
            {x, CCDirector::get()->getWinSize().height - y});
    }

    static float worldSize(GJBaseGameLayer* layer, float x, float y, float size) {
        const auto a = worldToScreen(layer, x, y);
        const auto b = worldToScreen(layer, x + size, y);
        return std::hypot(b.x - a.x, b.y - a.y);
    }

    static float levelNumber(lua_State* state, int argument) {
        const float value = static_cast<float>(luaL_checknumber(state, argument));
        if (!std::isfinite(value)) luaL_error(state, "coordinate must be finite");
        return value;
    }

    static float levelWidth(lua_State* state, int argument) {
        const float value = static_cast<float>(luaL_optnumber(state, argument, 1.0));
        if (!std::isfinite(value) || value <= 0 || value > 10000)
            luaL_error(state, "width must be between 0 and 10000");
        return value;
    }

    static ccColor4F levelColor(lua_State* state, int argument) {
        float color[4] = {
            static_cast<float>(luaL_checknumber(state, argument)),
            static_cast<float>(luaL_checknumber(state, argument + 1)),
            static_cast<float>(luaL_checknumber(state, argument + 2)),
            static_cast<float>(luaL_optnumber(state, argument + 3, 1.0)),
        };
        for (float& channel : color) {
            if (!std::isfinite(channel)) luaL_error(state, "color must be finite");
            channel = std::clamp(channel, 0.0f, 1.0f);
        }
        return {color[0], color[1], color[2], color[3]};
    }

    static std::vector<CCPoint> levelPoints(lua_State* state) {
        luaL_checktype(state, 1, LUA_TTABLE);
        const size_t count = lua_rawlen(state, 1);
        if (count < 4 || count > 8192 || count % 2)
            luaL_error(state, "points must contain 2-4096 x,y pairs");
        std::vector<CCPoint> points;
        points.reserve(count / 2);
        for (size_t i = 1; i <= count; i += 2) {
            lua_rawgeti(state, 1, static_cast<lua_Integer>(i));
            lua_rawgeti(state, 1, static_cast<lua_Integer>(i + 1));
            const float x = static_cast<float>(luaL_checknumber(state, -2));
            const float y = static_cast<float>(luaL_checknumber(state, -1));
            lua_pop(state, 2);
            if (!std::isfinite(x) || !std::isfinite(y))
                luaL_error(state, "points must be finite");
            points.emplace_back(x, y);
        }
        return points;
    }

    static void outline(CCDrawNode* draw, const std::vector<CCPoint>& points, bool closed,
                        float width, const ccColor4F& color) {
        const float radius = std::max(0.25f, width * 0.5f);
        for (size_t i = 0; i + 1 < points.size(); ++i)
            draw->drawSegment(points[i], points[i + 1], radius, color);
        if (closed && points.size() > 2)
            draw->drawSegment(points.back(), points.front(), radius, color);
    }

    static void fill(CCDrawNode* draw, std::vector<CCPoint> points, const ccColor4F& color) {
        draw->drawPolygon(points.data(), static_cast<unsigned>(points.size()), color, 0.0f,
                          {0.0f, 0.0f, 0.0f, 0.0f});
    }

    static int levelWorldToScreen(lua_State* state) {
        auto* layer = requireLevel(state);
        const float x = levelNumber(state, 1);
        const float y = levelNumber(state, 2);
        const auto point = worldToScreen(layer, x, y);
        const auto size = CCDirector::get()->getWinSize();
        lua_pushnumber(state, point.x);
        lua_pushnumber(state, point.y);
        lua_pushboolean(state, point.x >= 0 && point.y >= 0 && point.x <= size.width &&
                                   point.y <= size.height);
        lua_pushnumber(state, worldSize(layer, x, y, 1.0f));
        return 4;
    }

    static int levelScreenToWorld(lua_State* state) {
        auto* layer = requireLevel(state);
        const auto point = screenToWorld(layer, levelNumber(state, 1), levelNumber(state, 2));
        lua_pushnumber(state, point.x);
        lua_pushnumber(state, point.y);
        return 2;
    }

    static int levelToScreenMany(lua_State* state) {
        auto* layer = requireLevel(state);
        luaL_checktype(state, 1, LUA_TTABLE);
        const size_t count = lua_rawlen(state, 1);
        if (count % 2 || count > 8192)
            return luaL_error(state, "points must be up to 4096 x,y pairs");
        lua_createtable(state, static_cast<int>(count), 0);
        for (size_t i = 1; i <= count; i += 2) {
            lua_rawgeti(state, 1, static_cast<lua_Integer>(i));
            lua_rawgeti(state, 1, static_cast<lua_Integer>(i + 1));
            const float x = static_cast<float>(luaL_checknumber(state, -2));
            const float y = static_cast<float>(luaL_checknumber(state, -1));
            lua_pop(state, 2);
            if (!std::isfinite(x) || !std::isfinite(y))
                return luaL_error(state, "points must be finite");
            const auto point = worldToScreen(layer, x, y);
            lua_pushnumber(state, point.x);
            lua_rawseti(state, -2, static_cast<lua_Integer>(i));
            lua_pushnumber(state, point.y);
            lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
        }
        return 1;
    }

    static int levelCamera(lua_State* state) {
        auto* layer = requireLevel(state);
        const auto size = CCDirector::get()->getWinSize();
        const auto center = screenToWorld(layer, size.width * .5f, size.height * .5f);
        const auto left = screenToWorld(layer, 0.0f, size.height * .5f);
        const auto bottom = screenToWorld(layer, size.width * .5f, size.height);
        lua_createtable(state, 0, 6);
        const auto field = [&](const char* name, double value) {
            lua_pushnumber(state, value);
            lua_setfield(state, -2, name);
        };
        field("x", center.x);
        field("y", center.y);
        field("width", std::hypot(center.x - left.x, center.y - left.y) * 2.0);
        field("height", std::hypot(center.x - bottom.x, center.y - bottom.y) * 2.0);
        field("zoom", layer->m_objectLayer->getScale());
        field("rotation", layer->m_objectLayer->getRotation());
        return 1;
    }

    static int levelLine(lua_State* state) {
        requireLevelDraw(state);
        const CCPoint from(levelNumber(state, 1), levelNumber(state, 2));
        const CCPoint to(levelNumber(state, 3), levelNumber(state, 4));
        const ccColor4F color = levelColor(state, 5);
        const float width = levelWidth(state, 9);
        canvasOf(state).m_draw->drawSegment(from, to, std::max(0.25f, width * 0.5f), color);
        return 0;
    }

    static int levelPolyline(lua_State* state) {
        requireLevelDraw(state);
        const auto points = levelPoints(state);
        const ccColor4F color = levelColor(state, 2);
        outline(canvasOf(state).m_draw, points, lua_toboolean(state, 6), levelWidth(state, 7),
                color);
        return 0;
    }

    static int levelPolygon(lua_State* state) {
        requireLevelDraw(state);
        const auto points = levelPoints(state);
        if (points.size() < 3) return luaL_error(state, "polygon needs 3 points");
        const ccColor4F color = levelColor(state, 2);
        if (lua_isnoneornil(state, 6) || lua_toboolean(state, 6))
            fill(canvasOf(state).m_draw, points, color);
        else
            outline(canvasOf(state).m_draw, points, true, levelWidth(state, 7), color);
        return 0;
    }

    static int levelRect(lua_State* state) {
        requireLevelDraw(state);
        const float x1 = levelNumber(state, 1);
        const float y1 = levelNumber(state, 2);
        const float x2 = levelNumber(state, 3);
        const float y2 = levelNumber(state, 4);
        const std::vector<CCPoint> points = {{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}};
        const ccColor4F color = levelColor(state, 5);
        if (lua_isnoneornil(state, 9) || lua_toboolean(state, 9))
            fill(canvasOf(state).m_draw, points, color);
        else
            outline(canvasOf(state).m_draw, points, true, levelWidth(state, 10), color);
        return 0;
    }

    static int levelCircle(lua_State* state) {
        requireLevelDraw(state);
        const float x = levelNumber(state, 1);
        const float y = levelNumber(state, 2);
        const float radius = levelNumber(state, 3);
        if (radius <= 0 || radius > 10000)
            return luaL_error(state, "invalid circle position or radius");
        const ccColor4F color = levelColor(state, 4);
        auto* draw = canvasOf(state).m_draw.data();
        if (lua_isnoneornil(state, 8) || lua_toboolean(state, 8)) {
            draw->drawDot({x, y}, radius, color);
            return 0;
        }
        const float width = levelWidth(state, 9);
        int segments = static_cast<int>(luaL_optinteger(state, 10, 0));
        if (segments <= 0) segments = std::clamp(static_cast<int>(radius * 0.5f), 12, 64);
        segments = std::clamp(segments, 3, 256);
        std::vector<CCPoint> points;
        points.reserve(static_cast<size_t>(segments));
        for (int i = 0; i < segments; ++i) {
            const float angle = 6.28318530718f * static_cast<float>(i) / static_cast<float>(segments);
            points.emplace_back(x + radius * std::cos(angle), y + radius * std::sin(angle));
        }
        outline(draw, points, true, width, color);
        return 0;
    }

    static int levelText(lua_State* state) {
        auto* layer = requireLevelDraw(state);
        const float x = levelNumber(state, 1);
        const float y = levelNumber(state, 2);
        const char* text = luaL_checkstring(state, 3);
        const float size = levelWidth(state, 4);
        const ccColor4F color = levelColor(state, 5);
        auto& canvas = canvasOf(state);
        if (canvas.m_usedLabels >= MAX_LABELS) return 0;
        if (canvas.m_usedLabels == canvas.m_labels.size()) {
            auto* label = CCLabelBMFont::create("", "chatFont.fnt");
            layer->m_objectLayer->addChild(label, CANVAS_Z + 1);
            canvas.m_labels.emplace_back(label);
            if (canvas.m_fontHeight <= 0.0f) {
                label->setString("Ag");
                canvas.m_fontHeight = std::max(1.0f, label->getContentSize().height);
            }
        }
        auto* label = canvas.m_labels[canvas.m_usedLabels++].data();
        label->setVisible(true);
        label->setString(text);
        label->setScale(size / canvas.m_fontHeight);
        label->setColor({static_cast<GLubyte>(color.r * 255.0f), static_cast<GLubyte>(color.g * 255.0f),
                         static_cast<GLubyte>(color.b * 255.0f)});
        label->setOpacity(static_cast<GLubyte>(color.a * 255.0f));
        label->setAnchorPoint(lua_toboolean(state, 9) ? CCPoint{0.5f, 0.5f} : CCPoint{0.0f, 1.0f});
        label->setPosition({x, y});
        return 0;
    }

    static int levelImage(lua_State* state) {
        return luaL_error(state, "grape.level.image is not available on mobile yet");
    }

#endif

    
    
    static int unavailableCall(lua_State* state) {
        return luaL_error(state, "%s is not available on this platform",
                          lua_tostring(state, lua_upvalueindex(1)));
    }

    static int unavailableIndex(lua_State* state) {
        const char* key = luaL_checkstring(state, 2);
        lua_getfield(state, lua_upvalueindex(1), "__name");
        const char* table = lua_tostring(state, -1);
        lua_pushfstring(state, "grape.%s.%s", table ? table : "?", key);
        lua_pushcclosure(state, unavailableCall, 1);
        return 1;
    }

    static void unavailable(lua_State* state, const char* name) {
        lua_newtable(state);
        lua_newtable(state);
        lua_pushstring(state, name);
        lua_setfield(state, -2, "__name");
        lua_pushvalue(state, -1);
        lua_pushcclosure(state, unavailableIndex, 1);
        lua_setfield(state, -2, "__index");
        lua_setmetatable(state, -2);
        lua_setfield(state, -2, name);
    }

    static void addFunction(lua_State* state, const char* name,
                            lua_CFunction function) {
        lua_pushcfunction(state, function);
        lua_setfield(state, -2, name);
    }

    static void instructionLimit(lua_State* state, lua_Debug*) {
        luaL_error(state, "instruction limit exceeded");
    }

    void fail(Script& script, const char* fallback) {
        const char* error = lua_tostring(script.state, -1);
        script.status = error ? error : fallback;
        lua_pop(script.state, 1);
        script.loaded = false;
        geode::log::error("Lua {}: {}", script.path.filename().string(),
                          script.status);
    }

    bool run(Script& script, int arguments) {
        lua_sethook(script.state, instructionLimit, LUA_MASKCOUNT, 250000);
        const int result = lua_pcall(script.state, arguments, 0, 0);
        lua_sethook(script.state, nullptr, 0, 0);
        if (result != LUA_OK) {
            fail(script, "script failed");
            return false;
        }
        return true;
    }

    bool callback(Script& script, const char* name,
                  double argument = 0, bool hasArgument = false) {
        lua_getglobal(script.state, name);
        if (!lua_isfunction(script.state, -1)) {
            lua_pop(script.state, 1);
            return true;
        }
        if (hasArgument) lua_pushnumber(script.state, argument);
        return run(script, hasArgument ? 1 : 0);
    }

    bool inputCallback(Script& script, int player, int button, bool pressed) {
        lua_getglobal(script.state, "on_input");
        if (!lua_isfunction(script.state, -1)) {
            lua_pop(script.state, 1);
            return true;
        }
        lua_pushinteger(script.state, player);
        lua_pushinteger(script.state, button);
        lua_pushboolean(script.state, pressed);
        return run(script, 3);
    }

    void createApi(Script& script) {
        auto* state = script.state;
        lua_newtable(state);
        addFunction(state, "log", log);
        lua_newtable(state);
        addFunction(state, "show", notify);
        addFunction(state, "update", notifyUpdate);
        addFunction(state, "color", notifyColor);
        addFunction(state, "dismiss", notifyDismiss);
        addFunction(state, "clear", notifyClear);
        lua_newtable(state);
        addFunction(state, "__call", notifyCall);
        lua_setmetatable(state, -2);
        lua_setfield(state, -2, "notify");
        addFunction(state, "frame", frame);
        addFunction(state, "in_level", inLevel);
        addFunction(state, "tps", tps);
        addFunction(state, "speed", speed);
        addFunction(state, "input", input);
        addFunction(state, "player_get", playerGet);
        addFunction(state, "player_set", playerSet);
        addFunction(state, "trajectory", trajectory);
        lua_newtable(state);
        addFunction(state, "list", menuList);
        addFunction(state, "register", menuRegister);
        addFunction(state, "get", menuGet);
        addFunction(state, "set", menuSet);
        addFunction(state, "toggle", menuToggle);
        addFunction(state, "press", menuPress);
        addFunction(state, "mode", menuMode);
        lua_setfield(state, -2, "menu");
        lua_newtable(state);
        addFunction(state, "get", storageGet);
        addFunction(state, "set", storageSet);
        lua_setfield(state, -2, "storage");
        lua_newtable(state);
        addFunction(state, "length", macroLength);
        addFunction(state, "position", macroPosition);
        addFunction(state, "tps", macroTps);
        addFunction(state, "platformer", macroPlatformer);
        addFunction(state, "seed", macroSeed);
        addFunction(state, "name", macroName);
        addFunction(state, "event", macroEvent);
        lua_setfield(state, -2, "macro");
#ifndef GEODE_IS_MOBILE
        lua_newtable(state);
        addFunction(state, "text", uiText);
        addFunction(state, "button", uiButton);
        addFunction(state, "checkbox", uiCheckbox);
        addFunction(state, "slider", uiSlider);
        addFunction(state, "input", uiInput);
        addFunction(state, "same_line", uiSameLine);
        addFunction(state, "separator", uiSeparator);
        addFunction(state, "image", uiImage);
        addFunction(state, "corner_image", uiCornerImage);
        lua_setfield(state, -2, "ui");
#else
        unavailable(state, "ui");
        unavailable(state, "model");
#endif
        lua_newtable(state);
        addFunction(state, "world_to_screen", levelWorldToScreen);
        addFunction(state, "screen_to_world", levelScreenToWorld);
        addFunction(state, "to_screen", levelToScreenMany);
        addFunction(state, "camera", levelCamera);
        addFunction(state, "line", levelLine);
        addFunction(state, "polyline", levelPolyline);
        addFunction(state, "polygon", levelPolygon);
        addFunction(state, "rect", levelRect);
        addFunction(state, "circle", levelCircle);
        addFunction(state, "text", levelText);
        addFunction(state, "image", levelImage);
        lua_setfield(state, -2, "level");
        lua_newtable(state);
        addFunction(state, "play", audioPlay);
        addFunction(state, "play_sfx", audioPlaySfx);
        addFunction(state, "stop", audioStop);
        addFunction(state, "stop_all", audioStopAll);
        addFunction(state, "volume", audioVolume);
        addFunction(state, "pitch", audioPitch);
        addFunction(state, "position", audioPosition);
        addFunction(state, "length", audioLength);
        addFunction(state, "paused", audioPaused);
        addFunction(state, "playing", audioPlaying);
        lua_setfield(state, -2, "audio");
        unavailable(state, "model");

        lua_setglobal(state, "grape");
    }

    bool notificationCallback(Script& script, const std::string& title,
                              const std::string& body, unsigned id) {
        lua_getglobal(script.state, "on_notification");
        if (!lua_isfunction(script.state, -1)) {
            lua_pop(script.state, 1);
            return true;
        }
        lua_pushstring(script.state, title.c_str());
        lua_pushstring(script.state, body.c_str());
        lua_pushinteger(script.state, static_cast<lua_Integer>(id));
        return run(script, 3);
    }

    bool load(Script& script) {
        writeSettings(script);
        stopSounds(script);
        if (script.state) lua_close(script.state);
        script.memory = {};
        readSettings(script);
        script.state = lua_newstate(allocate, &script.memory);
        if (!script.state) {
            script.status = "could not create Lua state";
            return false;
        }
        *static_cast<Script**>(lua_getextraspace(script.state)) = &script;
        luaL_requiref(script.state, "_G", luaopen_base, 1); lua_pop(script.state, 1);
        luaL_requiref(script.state, LUA_COLIBNAME, luaopen_coroutine, 1); lua_pop(script.state, 1);
        luaL_requiref(script.state, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(script.state, 1);
        luaL_requiref(script.state, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(script.state, 1);
        luaL_requiref(script.state, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(script.state, 1);
        luaL_requiref(script.state, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(script.state, 1);
        for (const char* blocked : {"dofile", "loadfile"}) {
            lua_pushnil(script.state);
            lua_setglobal(script.state, blocked);
        }
        createApi(script);
        if (luaL_loadfile(script.state, script.path.string().c_str()) != LUA_OK) {
            fail(script, "could not compile script");
            return false;
        }
        script.loaded = true;
        script.status = "Loaded";
        if (!run(script, 0) || !callback(script, "on_load")) return false;
        return true;
    }
};

ScriptEngine& ScriptEngine::get() {
    static ScriptEngine engine;
    return engine;
}

ScriptEngine::ScriptEngine() : m_impl(std::make_unique<Impl>()) {}
ScriptEngine::~ScriptEngine() {
    for (auto& script : m_impl->scripts) {
        m_impl->writeSettings(*script);
        m_impl->stopSounds(*script);
    }
}

void ScriptEngine::refresh() {
    const auto directory = grape::paths::directory("scripts");
    std::filesystem::create_directories(directory);
    std::filesystem::create_directories(directory / "resources");
    std::filesystem::create_directories(directory / "settings");
    std::filesystem::create_directories(directory / "sounds");
    std::filesystem::create_directories(directory / "models");
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".lua") continue;
        const auto found = std::find_if(m_impl->scripts.begin(), m_impl->scripts.end(),
            [&](const auto& script) { return script->path == entry.path(); });
        if (found == m_impl->scripts.end()) {
            auto script = std::make_unique<Impl::Script>();
            script->owner = m_impl.get();
            script->path = entry.path();
            script->status = "Not loaded";
            m_impl->scripts.push_back(std::move(script));
        }
    }
    std::erase_if(m_impl->scripts, [](const auto& script) {
        return !script->loaded && !std::filesystem::exists(script->path);
    });
    std::ranges::sort(m_impl->scripts, {}, [](const auto& script) {
        return script->path.filename().string();
    });
}

std::vector<ScriptStatus> ScriptEngine::scripts() const {
    std::vector<ScriptStatus> result;
    for (const auto& script : m_impl->scripts)
        result.push_back({script->path.filename().string(), script->status,
                          script->loaded});
    return result;
}

bool ScriptEngine::load(const std::string& name) {
    const auto found = std::find_if(m_impl->scripts.begin(), m_impl->scripts.end(),
        [&](const auto& script) { return script->path.filename() == name; });
    return found != m_impl->scripts.end() && m_impl->load(**found);
}

void ScriptEngine::unload(const std::string& name) {
    const auto found = std::find_if(m_impl->scripts.begin(), m_impl->scripts.end(),
        [&](const auto& script) { return script->path.filename() == name; });
    if (found == m_impl->scripts.end()) return;
    auto& script = **found;
    if (script.loaded) m_impl->callback(script, "on_unload");
    m_impl->stopSounds(script);
    m_impl->writeSettings(script);
    script.loaded = false;
    script.status = "Not loaded";
    if (script.state) {
        lua_close(script.state);
        script.state = nullptr;
    }
}

void ScriptEngine::update(double dt) {
#ifdef GEODE_IS_MOBILE
    grape::ui::NotificationCenter::get().update(static_cast<float>(dt));
#endif
    const auto now = std::chrono::steady_clock::now();
    m_impl->reapSounds();
    for (auto& script : m_impl->scripts) {
        if (script->loaded) m_impl->callback(*script, "on_update", dt, true);
        if (script->settingsDirty &&
            now - script->settingsChanged >= std::chrono::milliseconds(500))
            m_impl->writeSettings(*script);
    }

    struct Raised {
        std::string title;
        std::string body;
        unsigned id;
    };
    std::vector<Raised> raised;
    for (const auto& notice :
         grape::ui::NotificationCenter::get().notices()) {
        if (notice.m_id <= m_impl->lastNotification) continue;
        raised.push_back({notice.m_title, notice.m_body, notice.m_id});
    }
    for (const auto& notice : raised) {
        m_impl->lastNotification =
            std::max(m_impl->lastNotification, notice.id);
        for (auto& script : m_impl->scripts)
            if (script->loaded)
                m_impl->notificationCallback(*script, notice.title, notice.body,
                                             notice.id);
    }
}

void ScriptEngine::input(int player, int button, bool pressed) {
    if (m_impl->dispatchingInput) return;
    m_impl->dispatchingInput = true;
    for (auto& script : m_impl->scripts)
        if (script->loaded)
            m_impl->inputCallback(*script, player, button, pressed);
    m_impl->dispatchingInput = false;
}

void ScriptEngine::overlay() {
#ifdef GEODE_IS_MOBILE
    if (!m_impl->beginCanvas()) return;
#endif
    for (auto& script : m_impl->scripts) {
        if (!script->loaded) continue;
        script->drawing = true;
        script->overlay = true;
        m_impl->callback(*script, "on_overlay");
        script->overlay = false;
        script->drawing = false;
    }
#ifdef GEODE_IS_MOBILE
    m_impl->endCanvas();
#endif
}

void ScriptEngine::draw() {
    for (auto& script : m_impl->scripts) {
        if (!script->loaded) continue;
        script->drawing = true;
        m_impl->callback(*script, "on_draw");
        script->drawing = false;
    }
}
}
