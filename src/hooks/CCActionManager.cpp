#include <Geode/Geode.hpp>
#include <Geode/modify/CCActionManager.hpp>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"

using namespace geode::prelude;

namespace {
bool actionsAreSuspended(FrameEngine& timeline) {
    return GrapeEngine::get()->isEnabled() && timeline.m_onlyRefresh;
}
}

class $modify(GrapeActionManager, CCActionManager) {
    void update(float dt) {
        if (!actionsAreSuspended(GrapeEngine::get()->timeline())) {
            CCActionManager::update(dt);
        }
    }
};
