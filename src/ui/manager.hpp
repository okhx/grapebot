#ifndef UI_MANAGER_HPP
#define UI_MANAGER_HPP

#define IMGUI_DEFINE_MATH_OPERATORS

#include <imgui.h>

#include "imgui_helpers.hpp"
#include "state.hpp"
#include "theme.hpp"

namespace grape::vine {
class ReplayDirector;
}

class UIManager {
   public:
    UIState m_state;

    ImFont* m_font;
    ImFont* m_medium;
    ImFont* m_bold;
    ImFont* m_menuFont;
    ImFont* m_menuMediumFont;
    ImFont* m_menuBoldFont;
    Theme* m_theme;
    slui::AutocompleteState m_replayAutocomplete;

    geode::async::TaskHolder<geode::utils::file::PickResult> m_macroPick;

    GLuint m_texture = 0;

    UIManager();

    void setup();
    void draw();
    void toggle();

    void drawKeybindContextMenu();
    void rescanLabelNames();
    void rescanReplayNames();
    void rescanReplayNames(grape::vine::ReplayDirector& rs);
    void loadReplayByName(grape::vine::ReplayDirector& rs);

    void openKeybindEditor(const std::string& tag) {
        auto& ctx               = m_state.m_keybindCtx;
        ctx.tag                 = tag;
        ctx.open                = true;
        ctx.capturing           = false;
        ctx.capturedKey         = 0;
        ctx.capturedMod         = 0;
        ctx.pendingValue        = "1";
        ctx.pendingType         = KeybindType::Toggle;
        ctx.typeState.selectedIndex = 0;
    }

    void keybindRightClick(const std::string& tag) {
        if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonRight)) {
            openKeybindEditor(tag);
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
};

#endif
