#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GJEffectManager.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/ShaderLayer.hpp>

#include <algorithm>
#include <array>

#include "config/config.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"

using namespace geode::prelude;

namespace {

constexpr int PLAIN_BACKGROUND = 13;
constexpr int PLAIN_GROUND = 18;
constexpr int PLAIN_GROUND_LINE = 2;
constexpr ccColor3B MIDDLEGROUND_TINT = {40, 125, 255};

constexpr auto TRIGGERS_TO_HIDE = std::to_array<int>({
    29,   30,   32,   33,   104,  105,  221,  717,  718,  743,  744,
    899,  915,  1006, 1007, 2903, 2904, 2905, 2907, 2909, 2910, 2911,
    2912, 2913, 2914, 2915, 2916, 2917, 2919, 2920, 2921, 2922, 2923,
    2924, 3029, 3030, 3031, 3606, 3612,
});

constexpr auto SCENERY_IDS = std::to_array<int>({
    902,  943,  944,  945,  946,  947,  948,  949,  950,  951,  980,
    981,  982,  983,  984,  985,  986,  987,  988,  1023, 1024, 1025,
    1026, 1027, 1028, 1029, 1030, 1031, 1032, 1063, 1064, 1065, 1066,
    1067, 1068, 1069, 1070, 1071,
});

constexpr int FIRST_SCENERY_BLOCK = 506;
constexpr int LAST_SCENERY_BLOCK = 640;

template <std::size_t N>
constexpr bool listed(const std::array<int, N>& sorted, int id) {
    return std::binary_search(sorted.begin(), sorted.end(), id);
}

static_assert(std::is_sorted(TRIGGERS_TO_HIDE.begin(), TRIGGERS_TO_HIDE.end()));
static_assert(std::is_sorted(SCENERY_IDS.begin(), SCENERY_IDS.end()));
static_assert(listed(TRIGGERS_TO_HIDE, 899) && !listed(TRIGGERS_TO_HIDE, 900));

bool layoutWanted() {
    return GrapeEngine::get()->timeline().m_layoutMode->inner();
}

bool layoutInLevel() {
    return GrapeEngine::get()->isEnabled() && layoutWanted() &&
           !LevelEditorLayer::get();
}

bool plainBackdrop() {
    return layoutInLevel() &&
           !GrapeEngine::get()->timeline().m_useRegularBg->inner();
}

bool isScenery(GameObject* object) {
    const int id = object->m_objectID;
    return object->m_objectType == GameObjectType::Decoration ||
           object->m_isNoTouch ||
           (id >= FIRST_SCENERY_BLOCK && id <= LAST_SCENERY_BLOCK) ||
           listed(SCENERY_IDS, id);
}

ccColor3B toBytes(const std::array<float, 4>& color) {
    const auto byte = [](float channel) {
        return static_cast<GLubyte>(std::clamp(channel, 0.0f, 1.0f) * 255.0f);
    };
    return {byte(color[0]), byte(color[1]), byte(color[2])};
}

void paintChannel(GJEffectManager* effects, int channel, ccColor3B color) {
    if (auto* action = effects->getColorAction(channel)) action->m_color = color;
}

void flattenForLayout(GameObject* object) {
    object->setOpacity(255);
    object->m_isHide = false;
    object->m_hasNoGlow = true;
    object->m_activeMainColorID = -1;
    object->m_activeDetailColorID = -1;
    object->m_baseUsesHSV = false;
    object->m_detailUsesHSV = false;
    object->m_isDontFade = true;
    object->m_isDontEnter = true;
}

}

struct LayoutPlayLayer : Modify<LayoutPlayLayer, PlayLayer> {
    void addObject(GameObject* object) {
        if (layoutInLevel()) {
            if (listed(TRIGGERS_TO_HIDE, object->m_objectID))
                object->m_isHide = true;
            else
                flattenForLayout(object);
        }
        PlayLayer::addObject(object);
    }

    void updateVisibility(float dt) {
        PlayLayer::updateVisibility(dt);
        if (!layoutWanted() || !m_effectManager) return;
        const auto& settings = *GrapeSettings::get();
        const ccColor3B ground = toBytes(settings.layoutGroundColor);
        paintChannel(m_effectManager, 1000, toBytes(settings.layoutBgColor));
        paintChannel(m_effectManager, 1001, ground);
        paintChannel(m_effectManager, 1009, ground);
        paintChannel(m_effectManager, 1013, MIDDLEGROUND_TINT);
        paintChannel(m_effectManager, 1014, MIDDLEGROUND_TINT);
    }
};

struct LayoutGameLayer : Modify<LayoutGameLayer, GJBaseGameLayer> {
    void createBackground(int background) {
        GJBaseGameLayer::createBackground(plainBackdrop() ? PLAIN_BACKGROUND
                                                          : background);
    }

    void createMiddleground(int middleground) {
        if (!plainBackdrop()) return GJBaseGameLayer::createMiddleground(middleground);
        if (m_middleground) {
            m_middleground->removeFromParent();
            m_middleground = nullptr;
        }
    }

    void createGroundLayer(int ground, int line) {
        if (plainBackdrop()) {
            ground = PLAIN_GROUND;
            line = PLAIN_GROUND_LINE;
        }
        GJBaseGameLayer::createGroundLayer(ground, line);
    }

    void updateColor(ccColor3B& color, float fadeTime, int colorID, bool blending,
                     float opacity, ccHSVValue& copyHSV, int colorIDToCopy,
                     bool copyOpacity, EffectGameObject* callerObject, int unk1,
                     int unk2) {
        if (layoutInLevel()) return;
        GJBaseGameLayer::updateColor(color, fadeTime, colorID, blending, opacity,
                                     copyHSV, colorIDToCopy, copyOpacity,
                                     callerObject, unk1, unk2);
    }

    void triggerGradientCommand(GradientTriggerObject* trigger) {
        if (layoutInLevel()) return;
        GJBaseGameLayer::triggerGradientCommand(trigger);
    }
};

struct LayoutGameObject : Modify<LayoutGameObject, GameObject> {
    void addGlow(std::string frame) {
        GameObject::addGlow(std::move(frame));
        if (LevelEditorLayer::get() || !layoutWanted()) return;
        if (isScenery(this)) {
            m_isHide = true;
            return;
        }
        m_isHide = false;
        m_isDontFade = true;
        m_isDontEnter = true;
        if (m_isPassable) {
            m_opacityMod = .5f;
            m_opacityMod2 = .5f;
        }
    }
};

struct LayoutEffects : Modify<LayoutEffects, GJEffectManager> {
    void updateEffects(float dt) {
        if (layoutInLevel()) {
            m_pulseEffectMap.clear();
            m_pulseEffectVector.clear();
            m_opacityEffectMap.clear();
        }
        GJEffectManager::updateEffects(dt);
    }
};

struct LayoutShaders : Modify<LayoutShaders, ShaderLayer> {
    void performCalculations() {
        if (layoutInLevel()) {
            m_state.m_usesShaders = false;
            return;
        }
        ShaderLayer::performCalculations();
    }
};
