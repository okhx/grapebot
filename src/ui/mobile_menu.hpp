#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <array>
#include <filesystem>
#include <functional>

namespace grape::vine {
class ReplayDirector;
}

class MobileMenu final : public geode::Popup {
   protected:
    enum class Page { Bot, Converter, Player, Visual, Render, Scripts, Settings };

    inline static Page s_page = Page::Bot;
    inline static bool s_macroChosen = false;

    cocos2d::CCMenu* m_tabMenu = nullptr;
    cocos2d::CCLabelBMFont* m_title = nullptr;
    cocos2d::CCNode* m_body = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCMenu* m_rowMenu = nullptr;
    cocos2d::CCLabelBMFont* m_frameLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCNode* m_recordIcon = nullptr;
    cocos2d::CCNode* m_playIcon = nullptr;
    ButtonSprite* m_renderSprite = nullptr;
    geode::async::TaskHolder<geode::utils::file::PickResult> m_macroPick;
    geode::async::TaskHolder<geode::utils::file::PickResult> m_scriptPick;
    std::string m_status;
    float m_cursor = 0.f;
    bool m_recordingShown = false;
    bool m_playingShown = false;
#ifdef GEODE_IS_MOBILE
    size_t m_renderResolutionIndex = 0;
#endif

    bool init() override;
    void update(float dt) override;

    void buildTabs();
    void rebuildPage();
    void beginRows();
    void endRows();
    float rowWidth() const;
    float takeRow(float height);

    void buildBotPage();
    void buildMacroCard();
    void buildConverterPage();
    void buildPlayerPage();
    void buildVisualPage();
    void buildRenderPage();
    void buildScriptsPage();
    void buildSettingsPage();
    void rebuildLater();

    void sectionLabel(std::string const& text);
    void noteLabel(std::string const& text,
                   cocos2d::ccColor3B color = {190, 190, 190});
    void toggleRow(std::string const& text, bool value,
                   std::function<void(bool)> callback,
                   std::function<void()> openSettings = {});
    void numberRow(std::string const& text, double value,
                   std::function<void(double)> setter, double minimum,
                   double maximum, int decimals = 2);
    void stepperRow(std::string const& text, std::function<double()> getter,
                    std::function<void(double)> setter, double step,
                    double minimum, double maximum,
                    std::function<std::string(double)> formatter);
    void textRow(std::string const& text, std::string value,
                 std::function<void(std::string const&)> setter,
                 float width = 150.f);
    void colorRow(std::string const& text, std::array<float, 4>* target,
                  std::function<void()> changed);
    ButtonSprite* buttonRow(std::string const& text,
                            std::function<void()> callback,
                            float width = 150.f);

    void toggleRecording();
    void togglePlayback();
    void saveMacro();
    void closeMacro();
    void createMacro(std::string const& name);
    void loadMacro(std::filesystem::path const& path);
    void notify(std::string const& text, bool failed = false);

    void onTab(cocos2d::CCObject* sender);

   public:
    static MobileMenu* create();
    static void open();
};
