#include <Geode/Geode.hpp>
#include <Geode/binding/ForceBlockGameObject.hpp>
#include <Geode/binding/VideoOptionsLayer.hpp>

#include "engine/engine.hpp"
#include "Geode/cocos/platform/win32/CCEGLView.h"
#include "ui/hook.hpp"

using namespace geode::prelude;

#include <Geode/modify/ForceBlockGameObject.hpp>
#include <Geode/modify/VideoOptionsLayer.hpp>

namespace {
void syncImGuiViewport() {
    const auto size = CCDirector::get()->getWinSizeInPixels();
    ImGuiHookCtx::get().handleResize(size.width, size.height);
}
}

struct GrapeVideoOptionsLayer : Modify<GrapeVideoOptionsLayer, VideoOptionsLayer> {
    void onApply(cocos2d::CCObject* sender) {
        VideoOptionsLayer::onApply(sender);

        syncImGuiViewport();
    }
};
