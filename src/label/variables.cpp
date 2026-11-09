#include "variables.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameStatsManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/loader/Loader.hpp>

#include <chrono>
#include <cmath>
#include <ctime>

#include "assist/cps.hpp"
#include "config/config.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "vine/session.hpp"

using namespace geode::prelude;
using grape::rift::Array;
using grape::rift::Dict;
using grape::rift::Value;

namespace grape::labels {

namespace {

std::string formatDuration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const auto total = static_cast<long long>(seconds * 1000.0);
    const long long ms = total % 1000;
    const long long allSeconds = total / 1000;
    return fmt::format("{:02}:{:02}:{:02}.{:03}", allSeconds / 3600,
                       (allSeconds / 60) % 60, allSeconds % 60, ms);
}

const char* monthName(int month) {
    static const char* names[] = {"January",   "February", "March",
                                  "April",     "May",      "June",
                                  "July",      "August",   "September",
                                  "October",   "November", "December"};
    return (month >= 1 && month <= 12) ? names[month - 1] : "Unknown";
}

Value regionalIndicators() {
    Dict letters;
    for (char c = 'a'; c <= 'z'; ++c) {
        const unsigned int code = 0x1F1E6u + static_cast<unsigned>(c - 'a');
        std::string utf8;
        utf8 += static_cast<char>(0xF0 | (code >> 18));
        utf8 += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        utf8 += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        utf8 += static_cast<char>(0x80 | (code & 0x3F));
        letters[std::string(1, c)] = Value(utf8);
        letters[std::string(1, static_cast<char>(c - 'a' + 'A'))] = Value(utf8);
    }

    Dict digits;
    for (int i = 0; i <= 9; ++i) {
        std::string utf8;
        utf8 += static_cast<char>('0' + i);
        utf8 += "\xEF\xB8\x8F\xE2\x83\xA3";
        digits[std::to_string(i)] = Value(utf8);
    }
    letters["num"] = Value(digits);
    return Value(letters);
}

const char* platformName() {
#if defined(GEODE_IS_WINDOWS)
    return "Windows";
#elif defined(GEODE_IS_IOS)
    return "iOS";
#elif defined(GEODE_IS_ANDROID)
    return "Android";
#elif defined(GEODE_IS_MACOS)
    return "macOS";
#else
    return "Unknown";
#endif
}

const char* gamemodeName(PlayerObject* player) {
    if (!player) return "none";
    if (player->m_isShip) return "ship";
    if (player->m_isBall) return "ball";
    if (player->m_isBird) return "ufo";
    if (player->m_isDart) return "wave";
    if (player->m_isRobot) return "robot";
    if (player->m_isSpider) return "spider";
    if (player->m_isSwing) return "swing";
    return "cube";
}

int iconFor(IconType type) {
    auto* manager = GameManager::sharedState();
    return manager ? manager->activeIconForType(type) : 0;
}

bool g_contextValid = false;
bool g_staticFilled = false;
grape::rift::Context g_context;

}

void fillStatic(grape::rift::Context& context) {
    auto* mod = Mod::get();
    context.set("modVersion", Value(mod ? mod->getVersion().toVString()
                                        : std::string("unknown")));
    context.set("platform", Value(platformName()));
    context.set("geodeVersion",
                Value(Loader::get()->getVersion().toNonVString()));
    context.set("gameVersion", Value(GEODE_STR(GEODE_GD_VERSION)));

    int total = 0;
    int enabled = 0;
    for (auto* other : Loader::get()->getAllMods()) {
        ++total;
        if (other->isOrWillBeEnabled()) ++enabled;
    }
    context.set("totalMods", Value(total));
    context.set("enabledMods", Value(enabled));

    context.set("ri", regionalIndicators());

    if (auto* manager = GameManager::sharedState()) {
        context.set("username", Value(manager->m_playerName));
        context.set("cubeIcon", Value(iconFor(IconType::Cube)));
        context.set("shipIcon", Value(iconFor(IconType::Ship)));
        context.set("ballIcon", Value(iconFor(IconType::Ball)));
        context.set("ufoIcon", Value(iconFor(IconType::Ufo)));
        context.set("waveIcon", Value(iconFor(IconType::Wave)));
        context.set("robotIcon", Value(iconFor(IconType::Robot)));
        context.set("spiderIcon", Value(iconFor(IconType::Spider)));
        context.set("swingIcon", Value(iconFor(IconType::Swing)));
    }

    context.m_config = [](const std::string& key) -> Value {
        auto* settings = GrapeSettings::get();
        if (key == "global_enabled") return Value(settings->botEnabled);
        if (key == "tps") return Value(settings->tps);
        if (key == "speed") return Value(settings->speed);
        if (key == "noclip") return Value(settings->noclipTintEnabled);
        if (key == "autoclicker_enabled")
            return Value(settings->autoclickerEnabled);
        if (key == "pathfinder_enabled")
            return Value(settings->pathfinderEnabled);
        if (key == "trajectory") return Value(settings->trajectory.enabled);
        return {};
    };
}

void fillLive(grape::rift::Context& context) {
    auto* bot = GrapeEngine::get();
    auto& timeline = bot->timeline();

    const auto now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm local{};
#ifdef GEODE_IS_WINDOWS
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    context.set("hour", Value(local.tm_hour));
    context.set("minute", Value(local.tm_min));
    context.set("second", Value(local.tm_sec));
    context.set("day", Value(local.tm_mday));
    context.set("month", Value(local.tm_mon + 1));
    context.set("monthName", Value(monthName(local.tm_mon + 1)));
    context.set("year", Value(local.tm_year + 1900));
    context.set("clock", Value(fmt::format("{:02}:{:02}:{:02}", local.tm_hour,
                                           local.tm_min, local.tm_sec)));
    const int hour12 = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
    context.set("clock12",
                Value(fmt::format("{:02}:{:02}:{:02} {}", hour12, local.tm_min,
                                  local.tm_sec,
                                  local.tm_hour < 12 ? "AM" : "PM")));

    const float delta = CCDirector::sharedDirector()->getDeltaTime();
    context.set("realFps", Value(delta > 0.0f ? 1.0f / delta : 0.0f));
    context.set("fps", Value(CCDirector::sharedDirector()->getAnimationInterval()
                                 ? 1.0 / CCDirector::sharedDirector()
                                             ->getAnimationInterval()
                                 : 0.0));

    auto* settings = GrapeSettings::get();
    context.set("speedhack", Value(settings->speed != 1.0));
    context.set("speedhackSpeed", Value(settings->speed));
    context.set("framestepper", Value(timeline.isPaused()));
    context.set("noclip", Value(timeline.m_noclip->inner()));
    context.set("cbf", Value(Loader::get()->isModLoaded(
                            "syzzi.click_between_frames")));
    context.set("tps", Value(timeline.getTps()));
    context.set("timeWarp", Value(settings->speed));
    context.set("botState",
                Value(bot->isPlaying()   ? "playing"
                      : bot->isRecording() ? "recording"
                                           : "idle"));

    if (auto* stats = GameStatsManager::sharedState()) {
        Dict table;
        const auto read = [&](const char* key) {
            return Value(static_cast<int64_t>(stats->getStat(key)));
        };
        table["totalJumps"] = read("1");
        table["totalAttempts"] = read("2");
        table["completedLevels"] = read("3");
        table["completedOnlineLevels"] = read("4");
        table["demons"] = read("5");
        table["stars"] = read("6");
        table["completedMapPacks"] = read("7");
        table["goldCoins"] = read("8");
        table["playersDestroyed"] = read("9");
        table["likedLevels"] = read("10");
        table["ratedLevels"] = read("11");
        table["userCoins"] = read("12");
        table["diamonds"] = read("13");
        table["orbs"] = read("14");
        table["dailies"] = read("15");
        table["keys"] = read("16");
        table["totalOrbs"] = read("17");
        table["moons"] = read("28");
        table["diamondShards"] = read("29");
        table["gauntlets"] = read("30");
        table["lists"] = read("31");
        context.set("stats", Value(table));
    }

    auto* play = PlayLayer::get();
    auto* editor = LevelEditorLayer::get();
    auto* layer = play ? static_cast<GJBaseGameLayer*>(play)
                       : static_cast<GJBaseGameLayer*>(editor);
    context.set("editorMode", Value(editor != nullptr));
    if (!layer) {
        context.set("levelName", Value{});
        return;
    }

    if (auto* level = layer->m_level) {
        context.set("levelID", Value(static_cast<int64_t>(
                                   level->m_levelID.value())));
        context.set("levelName", Value(level->m_levelName));
        context.set("author", Value(level->m_creatorName));
        context.set("isRobtopLevel",
                    Value(level->m_levelID.value() > 0 &&
                          level->m_levelID.value() < 100));
        context.set("levelAttempts", Value(level->m_attempts.value()));
        context.set("levelStars", Value(level->m_stars.value()));
        context.set("practicePercent", Value(level->m_practicePercent));
        context.set("bestPercent", Value(level->m_normalPercent.value()));
        context.set("bestTime", Value(level->m_bestTime / 1000.0));
        context.set("isPlatformer", Value(level->isPlatformer()));
        context.set("best", level->isPlatformer()
                                ? Value(formatDuration(level->m_bestTime /
                                                       1000.0))
                                : Value(level->m_normalPercent.value()));
        context.set("levelCoins", Value(level->m_coins));
        context.set("objects", Value(level->m_objectCount.value()));
    }

    context.set("isDualMode", Value(layer->m_gameState.m_isDualMode));
    context.set("isPlatformer", Value(layer->m_isPlatformer));
    context.set("frame", Value(timeline.getFrame()));
    context.set("activeObjects",
                Value(static_cast<int64_t>(layer->m_activeObjectsCount)));

    if (auto* player = layer->m_player1) {
        context.set("playerX", Value(player->m_position.x));
        context.set("playerY", Value(player->m_position.y));
        context.set("isDead", Value(player->m_isDead));
        context.set("gamemode", Value(gamemodeName(player)));
    }
    if (auto* player = layer->m_player2) {
        context.set("player2X", Value(player->m_position.x));
        context.set("player2Y", Value(player->m_position.y));
    }

    if (play) {
        context.set("attempt", Value(play->m_attempts));
        context.set("isTestMode", Value(play->m_isTestMode));
        context.set("isPracticeMode", Value(play->m_isPracticeMode));
        context.set("levelTime", Value(play->m_gameState.m_levelTime));
        context.set("time", Value(formatDuration(play->m_gameState.m_levelTime)));
        const double progress =
            static_cast<double>(play->getCurrentPercent());
        context.set("progress", Value(progress));
        context.set("realProgress", Value(progress));
        context.set("levelLength", Value(play->m_levelLength));

        Array coins;
        for (int i = 0; i < 3; ++i) coins.push_back(Value(0));
        context.set("coins", Value(coins));
    }

    auto& cps = bot->cps();
    const auto clicks = [&](const char* name, int player) {
        context.set(fmt::format("cps{}", name), Value(cps.queryCPS(player)));
        context.set(fmt::format("maxCps{}", name),
                    Value(cps.queryMaxCPS(player)));
    };
    clicks("", 1);
    clicks("P2", 2);
    context.set("cps1", Value(cps.queryCPS(1)));
    context.set("cps1P2", Value(cps.queryCPS(2)));
    context.set("maxCps1", Value(cps.queryMaxCPS(1)));
    context.set("maxCps1P2", Value(cps.queryMaxCPS(2)));
}

grape::rift::Context& sharedContext() {
    if (!g_staticFilled) {
        fillStatic(g_context);
        g_staticFilled = true;
    }
    if (!g_contextValid) {
        fillLive(g_context);
        g_contextValid = true;
    }
    return g_context;
}

void invalidateContext() { g_contextValid = false; }

}
