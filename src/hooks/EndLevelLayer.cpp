#include <Geode/Geode.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

#include "config/config.hpp"
#include "render/renderer.hpp"
#include "ui/mobile_menu.hpp"

using namespace geode::prelude;

#ifdef GEODE_IS_MOBILE
struct GrapeEndLevelLayer : Modify<GrapeEndLevelLayer, EndLevelLayer> {
   private:
    void mountLauncher() {
        auto* sprite = CCSprite::create("grape.png"_spr);
        sprite->setScale(.32f);

        auto* button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(GrapeEndLevelLayer::onGrape));
        auto* menu = CCMenu::create();
        menu->addChild(button);

        const auto size = CCDirector::get()->getWinSize();
        menu->setPosition({size.width - 38.f, 38.f});
        this->addChild(menu, 1000);
    }

   public:
#ifdef GEODE_IS_IOS
    void enterAnimFinished() override {
        EndLevelLayer::enterAnimFinished();
        Renderer::get()->notifyEndLevelMenuReady();
    }
#endif

    void onGrape(CCObject*) { MobileMenu::open(); }

    void customSetup() override {
        EndLevelLayer::customSetup();
        if (!GrapeSettings::get()->showEndMenuButton) return;

        mountLauncher();
    }
};
#endif
