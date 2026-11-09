#include "noclip.hpp"

#include <algorithm>

#include "config/config.hpp"

using namespace geode::prelude;

namespace {

constexpr int FLASH_LAYER_Z = 100000;
constexpr float FLASH_HOLD_SHARE = .35f;
constexpr float SHORTEST_FADE = .01f;

class DeathFlash : public CCLayerColor {
public:
    static DeathFlash* in(CCNode* level) {
        return typeinfo_cast<DeathFlash*>(level->getChildByID(id()));
    }

    static DeathFlash* attach(CCNode* level) {
        auto* flash = new DeathFlash();
        const auto size = CCDirector::get()->getWinSize();
        if (!flash->initWithColor({0, 0, 0, 0}, size.width, size.height)) {
            delete flash;
            return nullptr;
        }
        flash->autorelease();
        flash->setID(id());
        flash->setVisible(false);
        level->addChild(flash, FLASH_LAYER_Z);
        return flash;
    }

    bool playing() const { return m_running; }

    void play(ccColor3B color, float peak, float seconds) {
        m_peak = peak;
        m_hold = seconds * FLASH_HOLD_SHARE;
        m_fade = std::max(SHORTEST_FADE, seconds - m_hold);
        m_elapsed = 0.0f;
        m_running = true;
        setColor(color);
        setOpacity(static_cast<GLubyte>(peak));
        setVisible(true);
        scheduleUpdate();
    }

    void stop() {
        m_running = false;
        unscheduleUpdate();
        setOpacity(0);
        setVisible(false);
    }

    void update(float dt) override {
        m_elapsed += dt;
        const float fading = std::clamp((m_elapsed - m_hold) / m_fade, 0.0f, 1.0f);
        if (fading >= 1.0f) return stop();
        setOpacity(static_cast<GLubyte>(m_peak * (1.0f - fading)));
    }

private:
    static const char* id() { return "noclip-tint"_spr; }

    float m_peak = 0.0f;
    float m_hold = 0.0f;
    float m_fade = 0.0f;
    float m_elapsed = 0.0f;
    bool m_running = false;
};

GLubyte channel(float value) {
    return static_cast<GLubyte>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
}

}

namespace grape::noclip {

bool covers(FrameEngine::NoclipType type, PlayerObject* who,
            PlayerObject* player1, PlayerObject* player2) {
    switch (type) {
        case FrameEngine::NoclipType::Player1: return who != player2;
        case FrameEngine::NoclipType::Player2: return who != player1;
        default: return true;
    }
}

void flash(CCNode* level) {
    const auto& settings = *GrapeSettings::get();
    if (!level || !settings.noclipTintEnabled) return;
    auto* flash = DeathFlash::in(level);
    if (flash && flash->playing()) return;
    if (!flash) flash = DeathFlash::attach(level);
    if (!flash) return;
    const auto& tint = settings.noclipTintColor;
    flash->play({channel(tint[0]), channel(tint[1]), channel(tint[2])},
                static_cast<float>(std::clamp(settings.noclipTintOpacity, 0.0, 1.0)) * 255.0f,
                static_cast<float>(std::clamp(settings.noclipTintTime, 0.0, 10.0)));
}

void stopFlash(CCNode* level) {
    if (!level) return;
    if (auto* flash = DeathFlash::in(level)) flash->stop();
}

}
