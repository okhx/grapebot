#include "touch_overlay.hpp"
#include "config/config.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "render/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace geode::prelude;

namespace {
constexpr float kButtonsWidth = 160.f;
constexpr float kGripWidth = 22.f;
constexpr float kBoxWidth = kButtonsWidth + kGripWidth;
constexpr float kBoxHeight = 56.f;
constexpr float kCornerRadius = 10.f;
constexpr float kBoxX = 8.f;
constexpr float kBoxY = 8.f;

constexpr float kLeftX = kBoxX + 32.f;
constexpr float kToggleX = kBoxX + kButtonsWidth * 0.5f;
constexpr float kRightX = kBoxX + kButtonsWidth - 32.f;
constexpr float kGripX = kBoxX + kButtonsWidth;
constexpr int kTouchPriority = -510;
constexpr float kCenterY = kBoxY + kBoxHeight * 0.5f;

constexpr cocos2d::ccColor4F kBoxFill = {0.0f, 0.0f, 0.0f, 0.8f};
constexpr cocos2d::ccColor4F kTransparent = {0.0f, 0.0f, 0.0f, 0.0f};
constexpr cocos2d::ccColor4F kGripDot = {1.0f, 1.0f, 1.0f, 0.55f};

constexpr const char* kArrowFrame = "GJ_arrow_02_001.png";
constexpr const char* kPlayFrame = "GJ_playEditorBtn_001.png";
constexpr const char* kStopFrame = "GJ_stopEditorBtn_001.png";

std::vector<cocos2d::CCPoint> roundedRectPoints(float w, float h, float r,
                                                int segments) {
    std::vector<cocos2d::CCPoint> pts;
    const auto addArc = [&](float cx, float cy, float start, float end) {
        for (int i = 0; i <= segments; ++i) {
            const float a = start + (end - start) * i / segments;
            pts.push_back(cocos2d::CCPoint(cx + r * std::cos(a),
                                            cy + r * std::sin(a)));
        }
    };
    constexpr float pi = std::numbers::pi_v<float>;
    addArc(w - r, h - r, 0.f, pi * 0.5f);
    addArc(r, h - r, pi * 0.5f, pi);
    addArc(r, r, pi, pi * 1.5f);
    addArc(w - r, r, pi * 1.5f, pi * 2.f);
    return pts;
}
}

static TouchOverlay* g_instance = nullptr;

TouchOverlay* TouchOverlay::get() {
    if (!g_instance) {
        g_instance = TouchOverlay::create();
        g_instance->retain();
    }
    return g_instance;
}

TouchOverlay* TouchOverlay::create() {
    auto ret = new TouchOverlay();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool TouchOverlay::init() {
    if (!CCLayer::init()) return false;

    m_background = CCDrawNode::create();
    m_background->setAnchorPoint(CCPointZero);
    m_background->setPosition(ccp(kBoxX, kBoxY));
    this->addChild(m_background);
    this->redrawBackground(GrapeSettings::get()->frameStepperMovable);

    m_menu = CCMenu::create();
    m_menu->setPosition(CCPointZero);
    this->addChild(m_menu);

    this->setVisible(false);
    return true;
}

bool TouchOverlay::ensureButtons() {
    if (m_leftBtn) return true;
    auto* frames = CCSpriteFrameCache::sharedSpriteFrameCache();
    if (!frames->spriteFrameByName(kArrowFrame) ||
        !frames->spriteFrameByName(kPlayFrame) ||
        !frames->spriteFrameByName(kStopFrame))
        return false;

    m_leftArrow = CCSprite::createWithSpriteFrameName(kArrowFrame);
    m_leftBtn = CCMenuItemSpriteExtra::create(
        m_leftArrow, this, menu_selector(TouchOverlay::onLeft));

    m_rightArrow = CCSprite::createWithSpriteFrameName(kArrowFrame);
    m_rightArrow->setFlipX(true);
    m_rightBtn = CCMenuItemSpriteExtra::create(
        m_rightArrow, this, menu_selector(TouchOverlay::onRight));

    m_toggleSprite = CCSprite::createWithSpriteFrameName(kPlayFrame);
    m_toggleBtn = CCMenuItemSpriteExtra::create(
        m_toggleSprite, this, menu_selector(TouchOverlay::onToggle));

    for (auto* button : {m_leftBtn, m_toggleBtn, m_rightBtn}) {
        button->setSizeMult(1.5f);
        m_menu->addChild(button);
    }
    m_leftBtn->setPosition(ccp(kLeftX, kCenterY));
    m_toggleBtn->setPosition(ccp(kToggleX, kCenterY));
    m_rightBtn->setPosition(ccp(kRightX, kCenterY));

    m_arrowOpacity = static_cast<float>(std::clamp(
        GrapeSettings::get()->frameStepperArrowOpacity, 0.1, 1.0));
    this->redrawArrows(m_arrowOpacity);
    m_togglePaused = GrapeEngine::get()->timeline().isPaused();
    this->redrawToggle(m_togglePaused);
    return true;
}

void TouchOverlay::redrawArrows(float alpha) {
    const GLubyte opacity =
        static_cast<GLubyte>(std::clamp(alpha, 0.f, 1.f) * 255.f);
    if (m_leftArrow) m_leftArrow->setOpacity(opacity);
    if (m_rightArrow) m_rightArrow->setOpacity(opacity);
}

float TouchOverlay::boxWidth() const {
    return m_movable ? kBoxWidth : kButtonsWidth;
}

void TouchOverlay::redrawBackground(bool movable) {
    m_movable = movable;
    if (!movable) m_dragging = false;
    const float width = boxWidth();
    m_background->clear();
    auto pts = roundedRectPoints(width, kBoxHeight, kCornerRadius, 6);
    m_background->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()),
                              kBoxFill, 0.f, kTransparent);
    m_background->setContentSize({width, kBoxHeight});
    if (!movable) return;
    for (int column = 0; column < 2; ++column)
        for (int row = 0; row < 3; ++row)
            m_background->drawDot(ccp(kButtonsWidth + 7.f + column * 7.f,
                                      kBoxHeight * 0.5f + (row - 1) * 8.f),
                                  1.6f, kGripDot);
}

void TouchOverlay::redrawToggle(bool paused) {
    if (!m_toggleSprite) return;
    if (auto* frame =
            CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(
                paused ? kPlayFrame : kStopFrame))
        m_toggleSprite->setDisplayFrame(frame);
}

void TouchOverlay::update(float dt) {
    auto* settings = GrapeSettings::get();
    if (!m_leftBtn || !settings->frameStepperHold || !this->isVisible()) {
        m_leftHeld = m_rightHeld = m_leftRepeat = m_rightRepeat = 0.f;
        return;
    }

    const float delay = std::max(0.0, settings->frameStepperHoldDelay);
    const float interval =
        1.f / std::max(1.0, settings->frameStepperHoldSpeed);
    const auto repeat = [dt, delay, interval](CCMenuItemSpriteExtra* button,
                                               float& held, float& elapsed,
                                               auto action) {
        if (!button->isSelected()) {
            held = elapsed = 0.f;
            return;
        }
        held += dt;
        if (held < delay) return;
        elapsed += dt;
        while (elapsed >= interval) {
            elapsed -= interval;
            action();
        }
    };

    repeat(m_leftBtn, m_leftHeld, m_leftRepeat,
           [] { GrapeEngine::get()->timeline().backwardsStep(); });
    repeat(m_rightBtn, m_rightHeld, m_rightRepeat,
           [] { GrapeEngine::get()->timeline().stepOnce(); });
}

void TouchOverlay::onLeft(CCObject*) {
    auto& timeline = GrapeEngine::get()->timeline();
    if (timeline.m_backwardsStepping->inner()) timeline.backwardsStep();
}

void TouchOverlay::onToggle(CCObject*) {
    auto& timeline = GrapeEngine::get()->timeline();
    timeline.setPaused(!timeline.isPaused());
    timeline.m_paused->notifyChange();
    const bool paused = timeline.isPaused();
    m_togglePaused = paused;
    this->redrawToggle(paused);
}

void TouchOverlay::onRight(CCObject*) {
    GrapeEngine::get()->timeline().stepOnce();
}

void TouchOverlay::updateVisibility() {
    auto* playLayer = PlayLayer::get();
    auto& timeline = GrapeEngine::get()->timeline();
    if (playLayer && this->getParent() != playLayer) {
        this->removeFromParentAndCleanup(false);
        playLayer->addChild(this, 1000);
    }
    if (!playLayer || !this->ensureButtons()) {
        this->setVisible(false);
        return;
    }
    if (GrapeSettings::get()->frameStepperMovable != m_movable)
        this->redrawBackground(GrapeSettings::get()->frameStepperMovable);
    this->applySavedPosition();
    const float opacity = static_cast<float>(std::clamp(
        GrapeSettings::get()->frameStepperArrowOpacity, 0.1, 1.0));
    if (opacity != m_arrowOpacity) {
        m_arrowOpacity = opacity;
        this->redrawArrows(opacity);
    }

    const bool paused = timeline.isPaused();
    if (paused != m_togglePaused) {
        m_togglePaused = paused;
        this->redrawToggle(paused);
    }

    m_leftBtn->setVisible(timeline.m_backwardsStepping->inner());

    const bool wanted = GrapeSettings::get()->frameStepperOverlay;
#ifdef GEODE_IS_IOS
    this->setVisible(playLayer && wanted && !Renderer::get()->isRecording());
#else
    this->setVisible(playLayer && wanted);
#endif
}

void TouchOverlay::hide() { this->setVisible(false); }

void TouchOverlay::onEnter() {
    CCLayer::onEnter();
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(
        this, kTouchPriority, true);
}

void TouchOverlay::onExit() {
    CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
    m_dragging = false;
    CCLayer::onExit();
}

CCPoint TouchOverlay::clampedOffset(CCPoint offset) const {
    const auto win = CCDirector::sharedDirector()->getWinSize();
    offset.x = std::clamp(offset.x, -kBoxX, win.width - kBoxX - boxWidth());
    offset.y = std::clamp(offset.y, -kBoxY, win.height - kBoxY - kBoxHeight);
    return offset;
}

void TouchOverlay::applySavedPosition() {
    if (m_dragging) return;
    auto* settings = GrapeSettings::get();
    if (settings->frameStepperX < 0.0 || settings->frameStepperY < 0.0) {
        this->setPosition(CCPointZero);
        return;
    }
    const auto win = CCDirector::sharedDirector()->getWinSize();
    this->setPosition(clampedOffset(
        {static_cast<float>(settings->frameStepperX) * win.width,
         static_cast<float>(settings->frameStepperY) * win.height}));
}

bool TouchOverlay::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!this->isVisible() || !m_leftBtn || !m_movable) return false;
    const CCPoint local = this->convertToNodeSpace(touch->getLocation());
    const CCRect grip(kGripX, kBoxY, kGripWidth, kBoxHeight);
    if (!grip.containsPoint(local)) return false;
    m_dragging = true;
    m_dragStart = touch->getLocation();
    m_dragOrigin = this->getPosition();
    return true;
}

void TouchOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_dragging) return;
    this->setPosition(clampedOffset(m_dragOrigin + (touch->getLocation() - m_dragStart)));
}

void TouchOverlay::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (!m_dragging) return;
    this->ccTouchMoved(touch, event);
    m_dragging = false;
    const auto win = CCDirector::sharedDirector()->getWinSize();
    auto* settings = GrapeSettings::get();
    settings->frameStepperX = std::max(0.0, static_cast<double>(this->getPositionX() / win.width));
    settings->frameStepperY = std::max(0.0, static_cast<double>(this->getPositionY() / win.height));
}

void TouchOverlay::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    this->ccTouchEnded(touch, event);
}

void TouchOverlay::resetPosition() {
    auto* settings = GrapeSettings::get();
    settings->frameStepperX = -1.0;
    settings->frameStepperY = -1.0;
    if (g_instance) g_instance->applySavedPosition();
}
