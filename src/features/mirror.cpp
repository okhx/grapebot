#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

#include "engine/engine.hpp"
#include "engine/timeline.hpp"

using namespace geode::prelude;

namespace {

bool mirrorBlocked() {
    return GrapeEngine::get()->timeline().m_noMirror->inner();
}

void showMirrorState(GJBaseGameLayer* layer, bool blocked) {
    auto& state = layer->m_gameState;
    state.m_levelFlipping = blocked || !state.m_unkBool10 ? 0.0f : 1.0f;
}

}

struct MirrorGuard : Modify<MirrorGuard, GJBaseGameLayer> {
    void toggleFlipped(bool flipped, bool instant) {
        if (!mirrorBlocked()) return GJBaseGameLayer::toggleFlipped(flipped, instant);
        m_gameState.m_unkBool10 = flipped;
    }
};

$execute {
    GrapeEngine::get()->timeline().m_noMirror->handle([](bool& blocked) {
        if (auto* level = PlayLayer::get()) showMirrorState(level, blocked);
    });
}
