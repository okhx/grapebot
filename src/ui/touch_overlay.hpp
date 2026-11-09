#ifndef TOUCH_OVERLAY_HPP
#define TOUCH_OVERLAY_HPP

#include <Geode/Geode.hpp>

class TouchOverlay : public cocos2d::CCLayer {
protected:
    cocos2d::CCSprite* m_leftArrow = nullptr;
    cocos2d::CCSprite* m_rightArrow = nullptr;
    cocos2d::CCSprite* m_toggleSprite = nullptr;

    CCMenuItemSpriteExtra* m_leftBtn = nullptr;
    CCMenuItemSpriteExtra* m_toggleBtn = nullptr;
    CCMenuItemSpriteExtra* m_rightBtn = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCDrawNode* m_background = nullptr;
    bool m_movable = false;

    bool m_togglePaused = false;
    float m_arrowOpacity = -1.f;

    float m_leftHeld = 0.f;
    float m_rightHeld = 0.f;
    float m_leftRepeat = 0.f;
    float m_rightRepeat = 0.f;

    bool m_dragging = false;
    cocos2d::CCPoint m_dragStart;
    cocos2d::CCPoint m_dragOrigin;

    bool init() override;
    void onEnter() override;
    void onExit() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    cocos2d::CCPoint clampedOffset(cocos2d::CCPoint offset) const;
    void applySavedPosition();

    bool ensureButtons();
    void redrawArrows(float alpha);
    void redrawBackground(bool movable);
    float boxWidth() const;
    void redrawToggle(bool paused);

public:
    static TouchOverlay* create();
    static TouchOverlay* get();

    void update(float dt) override;

    void onLeft(cocos2d::CCObject*);
    void onToggle(cocos2d::CCObject*);
    void onRight(cocos2d::CCObject*);

    void updateVisibility();
    void hide();
    static void resetPosition();
};

#endif
