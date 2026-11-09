#pragma once

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cfloat>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "obfuscate.h"


namespace slui {

void bringCurrentWindowToFront();

struct WidgetState {
    bool pressed = false;
    bool hovered = false;
    bool held = false;
    bool changed = false;
};

struct DropdownState {
    std::vector<std::string> options;
    int selectedIndex = -1;
};

struct AutocompleteState {
    std::vector<std::string> suggestions;
};

struct ColorState {
    std::array<float, 4> colors{};
    std::string hex;
};

using Font = ImFont*;

struct Config {
    float uiScale = 1.0f;
    float widgetWidth = 180.0f;
    float animationSpeed = 1.0f;
    bool playAnimations = true;
    bool customMode = false;
    ImFont* customFont = nullptr;
    ImFont* mediumFont = nullptr;
    ImFont* customMediumFont = nullptr;
    ImFont* boldFont = nullptr;
    ImFont* customBoldFont = nullptr;
    const char* username = "";
    bool skeetMode = false;
    ImFont* skeetHeaderFont = nullptr;
    bool skeetGridStarted = false;
    bool customSectionStarted = false;
    bool customSectionWantsTitle = false;
    int customSectionIndex = 0;
    float customSectionWidth = 0.0f;
    int customSectionColumn = 0;
    ImVec2 customSectionOrigin{};
    std::array<float, 2> customSectionColumnY{};
    ImVec2 customSectionMin{};
    ImVec2 customSectionClipMin{};
    ImVec2 customSectionClipMax{};
    float customSectionHeight = 0.0f;
    std::string customSectionTitle;
    bool fitWindowToContent = false;
    float fittedWindowHeight = 460.0f;
    int revealTab = -1;
    double revealStart = 0.0;
    std::function<void(ImVec2, ImVec2)> titleAccessory;
    float windowOpacity = 1.0f;
    float contentOpacity = 1.0f;
    float windowOffset = 0.0f;
    bool windowClosing = false;
    ImVec2 windowPos{};
    ImVec2 windowSize{};

    static Config& get() {
        static Config config;
        return config;
    }
};

class ScopedFont {
public:
    explicit ScopedFont(ImFont* font) {
        const auto& config = Config::get();
        if (config.customMode && config.customFont) {
            font = font == config.boldFont ? config.customBoldFont
                 : font == config.mediumFont ? config.customMediumFont
                 : config.customFont;
        }
        m_pushed = font != nullptr;
        if (m_pushed) ImGui::PushFont(font);
    }
    ~ScopedFont() {
        if (m_pushed) ImGui::PopFont();
    }
private:
    bool m_pushed = false;
};

inline WidgetState state(bool changed) {
    return {changed, ImGui::IsItemHovered(), ImGui::IsItemActive(), changed};
}

inline std::string_view visible_label(std::string_view label) {
    const auto marker = label.find(AY_OBFUSCATE("##"));
    return label.substr(0, marker);
}

inline void align_custom_value(std::string_view label) {
    const auto shown = visible_label(label);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float available = ImGui::GetContentRegionAvail().x;
    const ImVec2 textSize = ImGui::CalcTextSize(
        shown.data(), shown.data() + shown.size());
    const float width = std::min(
        ImGui::CalcItemWidth(),
        std::max(1.0f, available - (shown.empty() ? 0.0f
            : textSize.x + ImGui::GetStyle().ItemInnerSpacing.x)));
    if (!shown.empty())
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(start.x, start.y +
                (ImGui::GetFrameHeight() - textSize.y) * .5f),
            ImGui::GetColorU32(ImGuiCol_Text),
            shown.data(), shown.data() + shown.size());
    ImGui::SetCursorScreenPos(ImVec2(start.x + available - width, start.y));
    ImGui::SetNextItemWidth(width);
}

inline float animate_last_item(float target) {
    auto& config = Config::get();
    float* value = ImGui::GetStateStorage()->GetFloatRef(
        ImGui::GetItemID(), target);
    const float blend = config.playAnimations
        ? 1.0f - std::exp(-12.0f * config.animationSpeed *
                          ImGui::GetIO().DeltaTime)
        : 1.0f;
    *value += (target - *value) * std::clamp(blend, 0.0f, 1.0f);
    return *value;
}

inline bool custom_button(std::string_view label, ImVec2 requested) {
    const auto shown = visible_label(label);
    const ImVec2 textSize = ImGui::CalcTextSize(
        shown.data(), shown.data() + shown.size());
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    ImVec2 size = requested;
    if (size.x == 0.0f) size.x = textSize.x + padding.x * 2.0f;
    else if (size.x < 0.0f)
        size.x = std::max(1.0f, ImGui::GetContentRegionAvail().x + size.x);
    if (size.y <= 0.0f) size.y = ImGui::GetFrameHeight();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(
        std::string(label).c_str(), size);
    const float hover = animate_last_item(ImGui::IsItemHovered() ? 1.0f : 0.0f);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    draw->AddRectFilled(min, max, IM_COL32(24, 21, 24, 255), 4.0f);
    if (hover > .001f)
        draw->AddRectFilled(min, max,
            IM_COL32(255, 255, 255, static_cast<int>(20.0f * hover)), 4.0f);
    draw->AddRect(min, max, IM_COL32(34, 34, 34, 255), 4.0f);
    draw->AddText(ImVec2(min.x + (size.x - textSize.x) * .5f,
                         min.y + (size.y - textSize.y) * .5f),
                  IM_COL32_WHITE, shown.data(), shown.data() + shown.size());
    return pressed;
}

inline bool raw_button(std::string_view label, ImVec2 requested = ImVec2()) {
    if (Config::get().customMode) return custom_button(label, requested);
    return ImGui::Button(std::string(label).c_str(), requested);
}

inline constexpr ImU32 PASTEL_RED = IM_COL32(255, 112, 112, 255);

inline bool tinted_button(std::string_view label, ImVec2 requested, bool tinted,
                          ImU32 fill = PASTEL_RED) {
    const bool pressed = raw_button(label, requested);
    if (!tinted) return pressed;
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    float rounding = ImGui::GetStyle().FrameRounding;
    auto* draw = ImGui::GetWindowDrawList();
    const float shade = ImGui::IsItemActive() ? .8f : ImGui::IsItemHovered() ? 1.f : .9f;
    ImVec4 color = ImGui::ColorConvertU32ToFloat4(fill);
    color.x *= shade;
    color.y *= shade;
    color.z *= shade;
    draw->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f),
                        ImVec2(max.x - 1.0f, max.y - 1.0f),
                        ImGui::ColorConvertFloat4ToU32(color), rounding);
    const auto shown = visible_label(label);
    const ImVec2 text = ImGui::CalcTextSize(shown.data(), shown.data() + shown.size());
    draw->AddText(ImVec2(min.x + (max.x - min.x - text.x) * .5f,
                         min.y + (max.y - min.y - text.y) * .5f),
                  IM_COL32(40, 12, 12, 255), shown.data(), shown.data() + shown.size());
    return pressed;
}

inline WidgetState button(std::string_view label) {
    return state(raw_button(label));
}

inline WidgetState button_selector(std::string_view label, bool selected) {
    if (Config::get().skeetMode) {
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(
                ImGuiCol_CheckMark));
        const bool value =
            ImGui::Selectable(std::string(label).c_str(), selected);
        if (selected) ImGui::PopStyleColor();
        return state(value);
    }
    const bool value = ImGui::Selectable(std::string(label).c_str(), selected);
    return state(value);
}

inline WidgetState checkbox(std::string_view label, bool& value) {
    if (Config::get().customMode) {
        const auto shown = visible_label(label);
        const float side = ImGui::GetFrameHeight();
        const ImVec2 textSize = ImGui::CalcTextSize(
            shown.data(), shown.data() + shown.size());
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 size(side + (shown.empty() ? 0.0f
            : ImGui::GetStyle().ItemInnerSpacing.x + textSize.x), side);
        const bool pressed = ImGui::InvisibleButton(
            std::string(label).c_str(), size);
        if (pressed) value = !value;
        const float checked = animate_last_item(value ? 1.0f : 0.0f);
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 boxMax(min.x + side, min.y + side);
        draw->AddRectFilled(min, boxMax,
                            IM_COL32(24, 21, 24, 255), 4.0f);
        draw->AddRect(min, boxMax, IM_COL32(34, 34, 34, 255), 4.0f);
        if (checked > .001f) {
            const ImU32 tick = IM_COL32(
                255, 255, 255, static_cast<int>(255.0f * checked));
            draw->AddLine(ImVec2(min.x + side * .24f, min.y + side * .52f),
                          ImVec2(min.x + side * .43f, min.y + side * .71f),
                          tick, 2.0f);
            draw->AddLine(ImVec2(min.x + side * .43f, min.y + side * .71f),
                          ImVec2(min.x + side * .78f, min.y + side * .30f),
                          tick, 2.0f);
        }
        if (!shown.empty())
            draw->AddText(
                ImVec2(min.x + side + ImGui::GetStyle().ItemInnerSpacing.x,
                       min.y + (side - textSize.y) * .5f),
                IM_COL32_WHITE, shown.data(), shown.data() + shown.size());
        return {pressed, ImGui::IsItemHovered(), ImGui::IsItemActive(), pressed};
    }
    return state(ImGui::Checkbox(std::string(label).c_str(), &value));
}

template <class T, class U>
WidgetState radio(T& value, U option, std::string_view label) {
    const bool selected = value == static_cast<T>(option);
    if (Config::get().skeetMode || Config::get().customMode) {
        bool checked = selected;
        const bool pressed = checkbox(label, checked).pressed;
        if (pressed) value = static_cast<T>(option);
        return state(pressed);
    }
    const bool pressed = ImGui::RadioButton(std::string(label).c_str(), selected);
    if (pressed) value = static_cast<T>(option);
    return state(pressed);
}

template <class T>
WidgetState drag(std::string_view label, T& value, T min = T{}, T max = T{100},
                 float speed = 1.0f, std::string_view = {}) {
    ImGuiDataType type;
    if constexpr (std::is_same_v<T, float>) type = ImGuiDataType_Float;
    else if constexpr (std::is_same_v<T, double>) type = ImGuiDataType_Double;
    else if constexpr (std::is_same_v<T, int>) type = ImGuiDataType_S32;
    else if constexpr (std::is_same_v<T, unsigned int>) type = ImGuiDataType_U32;
    else if constexpr (std::is_same_v<T, uint64_t>) type = ImGuiDataType_U64;
    else if constexpr (std::is_same_v<T, int64_t>) type = ImGuiDataType_S64;
    else static_assert(sizeof(T) == 0, "Unsupported ImGui drag type");
    if (Config::get().customMode) {
        const char* format;
        if constexpr (std::is_floating_point_v<T>) format = "%.3f";
        else if constexpr (std::is_signed_v<T>)
            format = sizeof(T) > sizeof(int) ? "%lld" : "%d";
        else format = sizeof(T) > sizeof(unsigned int) ? "%llu" : "%u";
        align_custom_value(label);
        const std::string id = std::string(AY_OBFUSCATE("##value-")) + std::string(label);
        const bool changed = ImGui::InputScalar(
            id.c_str(), type, &value, nullptr, nullptr, format,
            ImGuiInputTextFlags_AutoSelectAll);
        if (changed) value = std::clamp(value, min, max);
        return state(changed);
    }
    return state(ImGui::DragScalar(std::string(label).c_str(), type, &value,
                                   speed, &min, &max));
}

inline WidgetState triple_slider(std::string_view label, float& a, float& b,
                                 float& c) {
    float sum = a + b + c;
    if (!(sum > 0.0f)) {
        a = b = c = 100.0f / 3.0f;
        sum = 100.0f;
    } else if (std::fabs(sum - 100.0f) > 0.001f) {
        a = a / sum * 100.0f;
        b = b / sum * 100.0f;
        c = c / sum * 100.0f;
    }

    const std::string_view shown = visible_label(label);
    if (!shown.empty())
        ImGui::TextUnformatted(shown.data(), shown.data() + shown.size());

    const float height = ImGui::GetFrameHeight();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + height);
    const std::string id = std::string(AY_OBFUSCATE("##tri-")) + std::string(label);
    ImGui::InvisibleButton(id.c_str(), ImVec2(width, height));

    float d1 = a * 0.01f;
    float d2 = (a + b) * 0.01f;

    static ImGuiID s_activeId = 0;
    static int s_activeDivider = 0;

    bool changed = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float ratio = std::clamp(
            (ImGui::GetIO().MousePos.x - p0.x) / width, 0.0f, 1.0f);
        if (ImGui::IsItemActivated()) {
            s_activeId = ImGui::GetItemID();
            s_activeDivider =
                std::fabs(ratio - d1) <= std::fabs(ratio - d2) ? 1 : 2;
        }
        if (s_activeId == ImGui::GetItemID()) {
            if (s_activeDivider == 1) d1 = std::clamp(ratio, 0.0f, d2);
            else d2 = std::clamp(ratio, d1, 1.0f);
            a = d1 * 100.0f;
            b = (d2 - d1) * 100.0f;
            c = (1.0f - d2) * 100.0f;
            changed = true;
        }
    }

    auto* draw = ImGui::GetWindowDrawList();
    const float mid1 = p0.x + width * d1;
    const float mid2 = p0.x + width * d2;
    const ImU32 colA = IM_COL32(90, 200, 120, 255);
    const ImU32 colB = IM_COL32(90, 150, 235, 255);
    const ImU32 colC = IM_COL32(235, 165, 80, 255);
    draw->AddRectFilled(p0, ImVec2(mid1, p1.y), colA);
    draw->AddRectFilled(ImVec2(mid1, p0.y), ImVec2(mid2, p1.y), colB);
    draw->AddRectFilled(ImVec2(mid2, p0.y), p1, colC);
    draw->AddLine(ImVec2(mid1, p0.y), ImVec2(mid1, p1.y),
                  IM_COL32(15, 15, 15, 255), 2.0f);
    draw->AddLine(ImVec2(mid2, p0.y), ImVec2(mid2, p1.y),
                  IM_COL32(15, 15, 15, 255), 2.0f);
    draw->AddRect(p0, p1, Config::get().customMode
        ? IM_COL32(34, 34, 34, 255) : IM_COL32(10, 10, 10, 255));

    const auto segLabel = [&](float loX, float hiX, float pct) {
        char buf[16]{};
        std::snprintf(buf, sizeof(buf), "%.0f%%", pct);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        if (hiX - loX < ts.x + 2.0f) return;
        const ImVec2 tp((loX + hiX - ts.x) * 0.5f,
                        p0.y + (height - ts.y) * 0.5f);
        draw->AddText(ImVec2(tp.x + 1, tp.y + 1), IM_COL32(0, 0, 0, 200), buf);
        draw->AddText(tp, IM_COL32(245, 245, 245, 255), buf);
    };
    segLabel(p0.x, mid1, a);
    segLabel(mid1, mid2, b);
    segLabel(mid2, p1.x, c);

    return {changed, ImGui::IsItemHovered(), ImGui::IsItemActive(), changed};
}

inline WidgetState input_text(std::string_view label, std::string_view hint,
                              std::string& value) {
    return state(ImGui::InputTextWithHint(std::string(label).c_str(),
                                         std::string(hint).c_str(), &value));
}

inline void next_input_full_width() {
}

inline constexpr float TAB_REVEAL_SECONDS = .22f;
inline constexpr float TAB_REVEAL_RISE = 12.0f;
inline constexpr float WINDOW_FOLLOW_RATE = 18.0f;
inline constexpr float DROPDOWN_OPEN_SECONDS = .18f;
inline constexpr float DROPDOWN_OPEN_DROP = 8.0f;

inline float ease_out_cubic(float t) {
    const float inverse = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - inverse * inverse * inverse;
}

inline void fade_vertices(ImDrawList* list, int from, float alpha) {
    for (int i = from; i < list->VtxBuffer.Size; ++i) {
        ImU32& color = list->VtxBuffer[i].col;
        const float opacity =
            static_cast<float>((color & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT);
        color = (color & ~IM_COL32_A_MASK) |
                (static_cast<ImU32>(opacity * alpha + .5f) << IM_COL32_A_SHIFT);
    }
}

int child_window_count();
void fade_child_windows(int from, float alpha);
void shift_child_windows(int from, float offsetY);
void present_current_window(float alpha, float offsetY);
void smooth_window_moves();
bool begin_reveal(const char* id, bool open);
void end_reveal();
float popup_max_height();
void smooth_popup_scroll();

inline float smooth_step(float t) {
    const float x = std::clamp(t, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

inline float reveal_time(double start, float seconds) {
    const auto& config = Config::get();
    if (!config.playAnimations) return 1.0f;
    const float span = seconds / std::max(config.animationSpeed, .05f);
    return std::clamp(static_cast<float>((ImGui::GetTime() - start) / span),
                      0.0f, 1.0f);
}

inline bool custom_dropdown_option(std::string_view label, bool selected);

inline WidgetState input_text_autocomplete(
    std::string_view label, std::string_view hint, std::string& value,
    AutocompleteState& autocomplete, std::function<void()> = {}) {
    bool changed = input_text(label, hint, value).changed;
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    float popupWidth = ImGui::GetItemRectSize().x;
    for (const auto& suggestion : autocomplete.suggestions)
        popupWidth = std::max(
            popupWidth, ImGui::CalcTextSize(suggestion.c_str()).x + 16.0f);
    if ((ImGui::IsItemActivated() || ImGui::IsItemClicked() || changed) &&
        !autocomplete.suggestions.empty())
        ImGui::OpenPopup((std::string(label) + std::string(AY_OBFUSCATE("##suggestions"))).c_str());
    const std::string popup = std::string(label) + std::string(AY_OBFUSCATE("##suggestions"));
    if (!autocomplete.suggestions.empty()) {
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(popupWidth, 0.0f), ImVec2(popupWidth, popup_max_height()));
        if (ImGui::BeginPopup(
                popup.c_str(), ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoScrollWithMouse)) {
            bringCurrentWindowToFront();
            const ImGuiID openedKey = ImGui::GetID("##suggestions-opened");
            if (ImGui::IsWindowAppearing())
                ImGui::GetStateStorage()->SetFloat(
                    openedKey, static_cast<float>(ImGui::GetTime()));
            const float openedAt =
                ImGui::GetStateStorage()->GetFloat(openedKey, -100.0f);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsWindowHovered() && !hovered)
                ImGui::CloseCurrentPopup();
            for (int i = 0;
                 i < static_cast<int>(autocomplete.suggestions.size()); ++i) {
                const auto& suggestion = autocomplete.suggestions[i];
                ImGui::PushID(i);
                const bool pressed = Config::get().customMode
                    ? custom_dropdown_option(suggestion, false)
                    : ImGui::Selectable(suggestion.c_str());
                ImGui::PopID();
                if (pressed) {
                    value = suggestion;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            smooth_popup_scroll();
            const float shown = reveal_time(openedAt, DROPDOWN_OPEN_SECONDS);
            if (shown < 1.0f)
                present_current_window(smooth_step(shown),
                                       -(1.0f - ease_out_cubic(shown)) *
                                           DROPDOWN_OPEN_DROP * Config::get().uiScale);
            ImGui::EndPopup();
        }
    }
    return {changed, hovered, held, changed};
}

inline bool custom_dropdown_option(std::string_view label, bool selected) {
    constexpr float padding = 4.0f;
    const ImVec2 textSize = ImGui::CalcTextSize(
        label.data(), label.data() + label.size());
    const float inset = ImGui::GetStyle().WindowPadding.x;
    const ImVec2 min(ImGui::GetWindowPos().x + inset, ImGui::GetCursorScreenPos().y);
    const float right =
        ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorScreenPos(min);
    const std::string id = std::string(AY_OBFUSCATE("##dropdown-option-")) + std::string(label);
    const bool pressed = ImGui::InvisibleButton(
        id.c_str(), ImVec2(std::max(1.0f, right - min.x),
                           textSize.y + padding * 2.0f));
    const bool hovered = ImGui::IsItemHovered();
    float* animation = ImGui::GetStateStorage()->GetFloatRef(
        ImGui::GetItemID(), 0.0f);
    const float target = hovered ? 1.0f : 0.0f;
    const auto& config = Config::get();
    const float blend = config.playAnimations
        ? 1.0f - std::exp(-12.0f * config.animationSpeed *
                          ImGui::GetIO().DeltaTime)
        : 1.0f;
    *animation += (target - *animation) * std::clamp(blend, 0.0f, 1.0f);

    auto* draw = ImGui::GetWindowDrawList();
    if (*animation > .001f) {
        draw->AddRectFilled(
            min, ImGui::GetItemRectMax(),
            IM_COL32(255, 255, 255,
                     static_cast<int>(255.0f * .20f * *animation)),
            4.0f);
    }
    draw->AddText(
        ImVec2(min.x + padding, min.y + padding),
        selected ? IM_COL32_WHITE : IM_COL32(204, 204, 204, 255),
        label.data(), label.data() + label.size());
    if (pressed) ImGui::CloseCurrentPopup();
    return pressed;
}

inline WidgetState dropdown(std::string_view label, DropdownState& stateData,
                            int& selected, std::function<void()> = {},
                            bool = true) {
    const char* preview = selected >= 0 &&
            selected < static_cast<int>(stateData.options.size())
        ? stateData.options[selected].c_str() : "Select";
    bool changed = false;
    std::string id(label);
    bool opened = false;
    bool hovered = false;
    bool held = false;
    {
        if (Config::get().customMode) {
            auto* draw = ImGui::GetWindowDrawList();
            const auto padding = ImGui::GetStyle().FramePadding;
            align_custom_value(label);
            id = std::string(AY_OBFUSCATE("##combo-")) + std::string(label);
            const float popupWidth = ImGui::CalcItemWidth();
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(popupWidth, 0.0f), ImVec2(popupWidth, popup_max_height()));
            ImGui::PushStyleVar(
                ImGuiStyleVar_FramePadding,
                ImVec2(padding.x + 14.0f, padding.y));
            opened = ImGui::BeginCombo(
                id.c_str(), preview, ImGuiComboFlags_NoArrowButton);
            ImGui::PopStyleVar();

            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const ImVec2 center(min.x + 9.0f, (min.y + max.y) * .5f);
            const float turn = animate_last_item(opened ? 1.0f : 0.0f) * 3.14159265f;
            const float cosine = std::cos(turn);
            const float sine = std::sin(turn);
            const auto spin = [&](float x, float y) {
                return ImVec2(center.x + x * cosine - y * sine,
                              center.y + x * sine + y * cosine);
            };
            draw->AddTriangleFilled(spin(-4.0f, -2.0f), spin(4.0f, -2.0f),
                                    spin(0.0f, 3.0f), IM_COL32(128, 128, 128, 255));
            hovered = ImGui::IsItemHovered();
            held = ImGui::IsItemActive();
        } else {
            opened = ImGui::BeginCombo(id.c_str(), preview);
        }
    }
    if (opened) {
        const ImGuiID openedKey = ImGui::GetID("##dropdown-opened");
        if (ImGui::IsWindowAppearing())
            ImGui::GetStateStorage()->SetFloat(openedKey,
                                               static_cast<float>(ImGui::GetTime()));
        const float openedAt = ImGui::GetStateStorage()->GetFloat(openedKey, -100.0f);
        for (int i = 0; i < static_cast<int>(stateData.options.size()); ++i) {
            ImGui::PushID(i);
            const bool pressed = Config::get().customMode
                ? custom_dropdown_option(stateData.options[i], selected == i)
                : ImGui::Selectable(stateData.options[i].c_str(), selected == i);
            ImGui::PopID();
            if (pressed) {
                selected = i;
                stateData.selectedIndex = i;
                changed = true;
            }
        }
        smooth_popup_scroll();
        const float shown = reveal_time(openedAt, DROPDOWN_OPEN_SECONDS);
        if (shown < 1.0f)
            present_current_window(smooth_step(shown),
                                   -(1.0f - ease_out_cubic(shown)) *
                                       DROPDOWN_OPEN_DROP * Config::get().uiScale);
        ImGui::EndCombo();
    }
    if (Config::get().skeetMode || Config::get().customMode)
        return {changed, hovered, held, changed};
    return state(changed);
}

inline constexpr float COLOR_POPUP_SECONDS = .2f;
inline constexpr float COLOR_POPUP_RISE = 8.0f;
inline constexpr float COLOR_HANDLE_FOLLOW = 22.0f;

inline float ease_toward(float current, float target) {
    const auto& config = Config::get();
    if (!config.playAnimations) return target;
    const float blend = 1.0f - std::exp(-COLOR_HANDLE_FOLLOW *
                                        std::max(config.animationSpeed, .05f) *
                                        ImGui::GetIO().DeltaTime);
    return current + (target - current) * blend;
}

inline void draw_bar_marker(ImDrawList* draw, float left, float right, float y) {
    draw->AddRect(ImVec2(left - 2.0f, y - 3.0f), ImVec2(right + 2.0f, y + 3.0f),
                  IM_COL32(0, 0, 0, 200), 2.0f, 0, 2.0f);
    draw->AddRect(ImVec2(left - 1.0f, y - 2.0f), ImVec2(right + 1.0f, y + 2.0f),
                  IM_COL32(255, 255, 255, 255), 2.0f, 0, 1.5f);
}

inline ImU32 mix_color(ImU32 from, ImU32 to, float t) {
    const auto channel = [&](int shift) {
        const float a = static_cast<float>((from >> shift) & 0xFF);
        const float b = static_cast<float>((to >> shift) & 0xFF);
        return static_cast<ImU32>(a + (b - a) * std::clamp(t, 0.0f, 1.0f) + .5f)
               << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) |
           channel(IM_COL32_B_SHIFT) | channel(IM_COL32_A_SHIFT);
}

template <class Shade>
inline void shaded_rect(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding,
                        ImDrawFlags corners, Shade shade) {
    const int first = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, max, IM_COL32_WHITE, rounding, corners);
    for (int i = first; i < draw->VtxBuffer.Size; ++i) {
        ImDrawVert& vertex = draw->VtxBuffer[i];
        const ImU32 color = shade(ImVec2(std::clamp(vertex.pos.x, min.x, max.x),
                                         std::clamp(vertex.pos.y, min.y, max.y)));
        const float fringe =
            static_cast<float>((vertex.col & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT) / 255.0f;
        const float alpha =
            static_cast<float>((color & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT) * fringe;
        vertex.col = (color & ~IM_COL32_A_MASK) |
                     (static_cast<ImU32>(alpha + .5f) << IM_COL32_A_SHIFT);
    }
}

inline void rounded_gradient(ImDrawList* draw, ImVec2 min, ImVec2 max,
                             ImU32 from, ImU32 to, bool vertical, float rounding,
                             ImDrawFlags corners = ImDrawFlags_RoundCornersAll) {
    const float span = vertical ? max.y - min.y : max.x - min.x;
    shaded_rect(draw, min, max, rounding, corners, [&](ImVec2 pos) {
        const float along = vertical ? pos.y - min.y : pos.x - min.x;
        return mix_color(from, to, span > 0.0f ? along / span : 0.0f);
    });
}

inline float color_field_width() {
    const float bar = std::round(ImGui::GetFrameHeight());
    return std::round(bar * 7.5f) +
           2.0f * (std::round(ImGui::GetStyle().ItemInnerSpacing.x) + bar);
}

inline bool color_field(std::array<float, 4>& rgba) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float bar = std::round(ImGui::GetFrameHeight());
    const float gap = std::round(style.ItemInnerSpacing.x);
    const float square = std::round(bar * 7.5f);

    float h = storage->GetFloat(ImGui::GetID("##hue"), 0.0f);
    float s = storage->GetFloat(ImGui::GetID("##sat"), 0.0f);
    float v = storage->GetFloat(ImGui::GetID("##val"), 0.0f);
    float r = 0.0f, g = 0.0f, b = 0.0f;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    if (std::fabs(r - rgba[0]) > .002f || std::fabs(g - rgba[1]) > .002f ||
        std::fabs(b - rgba[2]) > .002f) {
        float nh = 0.0f, ns = 0.0f, nv = 0.0f;
        ImGui::ColorConvertRGBtoHSV(rgba[0], rgba[1], rgba[2], nh, ns, nv);
        if (ns > .0001f && nv > .0001f) h = nh;
        s = ns;
        v = nv;
    }
    float a = rgba[3];

    const ImGuiID shownKey = ImGui::GetID("##shown");
    const bool fresh = !storage->GetBool(shownKey, false);
    storage->SetBool(shownKey, true);
    const auto shown = [&](const char* key, float target) {
        const ImGuiID id = ImGui::GetID(key);
        const float value =
            fresh ? target : ease_toward(storage->GetFloat(id, target), target);
        storage->SetFloat(id, value);
        return value;
    };

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 origin(std::floor(cursor.x), std::floor(cursor.y));
    const float hueX = origin.x + square + gap;
    const float alphaX = hueX + bar + gap;
    bool changed = false;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const auto unit = [](float value) { return std::clamp(value, 0.0f, 1.0f); };

    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##sv", ImVec2(square, square));
    if (ImGui::IsItemActive()) {
        s = unit((mouse.x - origin.x) / square);
        v = 1.0f - unit((mouse.y - origin.y) / square);
        changed = true;
    }
    const bool svActive = ImGui::IsItemActive();
    ImGui::SetCursorScreenPos(ImVec2(hueX, origin.y));
    ImGui::InvisibleButton("##hue-bar", ImVec2(bar, square));
    if (ImGui::IsItemActive()) {
        h = unit((mouse.y - origin.y) / square);
        changed = true;
    }
    ImGui::SetCursorScreenPos(ImVec2(alphaX, origin.y));
    ImGui::InvisibleButton("##alpha-bar", ImVec2(bar, square));
    if (ImGui::IsItemActive()) {
        a = 1.0f - unit((mouse.y - origin.y) / square);
        changed = true;
    }

    const float shownH = shown("##shown-hue", h);
    const float shownS = shown("##shown-sat", s);
    const float shownV = shown("##shown-val", v);
    const float shownA = shown("##shown-alpha", a);
    const float grow = shown("##shown-grow", svActive ? 1.0f : 0.0f);

    float hr = 0.0f, hg = 0.0f, hb = 0.0f;
    ImGui::ColorConvertHSVtoRGB(h, 1.0f, 1.0f, hr, hg, hb);
    const ImU32 hue = ImGui::GetColorU32(ImVec4(hr, hg, hb, 1.0f));
    const ImVec2 squareMax(origin.x + square, origin.y + square);
    const float rounding = style.WindowRounding;
    const float barRounding = std::min(rounding, bar * .5f);
    const auto sv = [&](ImVec2 pos) {
        return mix_color(mix_color(IM_COL32_WHITE, hue, (pos.x - origin.x) / square),
                         IM_COL32_BLACK, (pos.y - origin.y) / square);
    };
    const int strips =
        std::max(1, static_cast<int>(square / std::max(4.0f, rounding + 1.0f)));
    for (int i = 0; i < strips; ++i) {
        ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
        if (i == 0) corners |= ImDrawFlags_RoundCornersTop;
        if (i == strips - 1) corners |= ImDrawFlags_RoundCornersBottom;
        shaded_rect(draw,
                    ImVec2(origin.x, origin.y + std::round(square * i / strips)),
                    ImVec2(squareMax.x, origin.y + std::round(square * (i + 1) / strips)),
                    corners == ImDrawFlags_RoundCornersNone ? 0.0f : rounding, corners, sv);
    }

    static constexpr ImU32 hues[7] = {
        IM_COL32(255, 0, 0, 255),   IM_COL32(255, 255, 0, 255),
        IM_COL32(0, 255, 0, 255),   IM_COL32(0, 255, 255, 255),
        IM_COL32(0, 0, 255, 255),   IM_COL32(255, 0, 255, 255),
        IM_COL32(255, 0, 0, 255)};
    for (int i = 0; i < 6; ++i) {
        const float top = origin.y + std::round(square * i / 6.0f);
        const float bottom = origin.y + std::round(square * (i + 1) / 6.0f);
        const ImDrawFlags corners = i == 0 ? ImDrawFlags_RoundCornersTop
                                  : i == 5 ? ImDrawFlags_RoundCornersBottom
                                           : ImDrawFlags_RoundCornersNone;
        rounded_gradient(draw, ImVec2(hueX, top), ImVec2(hueX + bar, bottom),
                         hues[i], hues[i + 1], true,
                         corners == ImDrawFlags_RoundCornersNone ? 0.0f : barRounding,
                         corners);
    }

    const float cell = std::round(bar * .5f);
    const int rows = std::max(1, static_cast<int>(std::round(square / cell)));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < 2; ++column) {
            const ImVec2 min(alphaX + column * cell,
                             origin.y + std::round(square * row / rows));
            const ImVec2 max(column ? alphaX + bar : min.x + cell,
                             origin.y + std::round(square * (row + 1) / rows));
            ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
            if (row == 0)
                corners = column ? ImDrawFlags_RoundCornersTopRight
                                 : ImDrawFlags_RoundCornersTopLeft;
            if (row == rows - 1)
                corners |= column ? ImDrawFlags_RoundCornersBottomRight
                                  : ImDrawFlags_RoundCornersBottomLeft;
            draw->AddRectFilled(min, max,
                                (row + column) % 2 ? IM_COL32(204, 204, 204, 255)
                                                   : IM_COL32(128, 128, 128, 255),
                                corners == ImDrawFlags_RoundCornersNone ? 0.0f
                                                                        : barRounding,
                                corners);
        }
    }
    float cr = 0.0f, cg = 0.0f, cb = 0.0f;
    ImGui::ColorConvertHSVtoRGB(h, s, v, cr, cg, cb);
    const ImU32 opaque = ImGui::GetColorU32(ImVec4(cr, cg, cb, 1.0f));
    const ImU32 clear = ImGui::GetColorU32(ImVec4(cr, cg, cb, 0.0f));
    rounded_gradient(draw, ImVec2(alphaX, origin.y),
                     ImVec2(alphaX + bar, squareMax.y), opaque, clear, true,
                     barRounding);

    const ImVec2 handle(origin.x + shownS * square,
                        origin.y + (1.0f - shownV) * square);
    const float radius = bar * (.24f + .08f * grow);
    draw->AddCircleFilled(handle, radius, opaque, 20);
    draw->AddCircle(handle, radius + 1.0f, IM_COL32(128, 128, 128, 255), 20, 1.5f);
    draw->AddCircle(handle, radius, IM_COL32_WHITE, 20, 1.5f);
    draw_bar_marker(draw, hueX, hueX + bar, origin.y + shownH * square);
    draw_bar_marker(draw, alphaX, alphaX + bar,
                    origin.y + (1.0f - shownA) * square);

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(alphaX + bar - origin.x, square));

    storage->SetFloat(ImGui::GetID("##hue"), h);
    storage->SetFloat(ImGui::GetID("##sat"), s);
    storage->SetFloat(ImGui::GetID("##val"), v);
    if (changed) {
        ImGui::ColorConvertHSVtoRGB(h, s, v, rgba[0], rgba[1], rgba[2]);
        rgba[3] = a;
    }
    return changed;
}

inline WidgetState color(std::string_view label, ColorState& value,
                         std::function<void()> = {}) {
    const std::string id(label);
    const auto syncHex = [&] {
        char hex[10];
        std::snprintf(
            hex, sizeof(hex), "#%02X%02X%02X%02X",
            static_cast<int>(std::clamp(value.colors[0], 0.f, 1.f) * 255.f),
            static_cast<int>(std::clamp(value.colors[1], 0.f, 1.f) * 255.f),
            static_cast<int>(std::clamp(value.colors[2], 0.f, 1.f) * 255.f),
            static_cast<int>(std::clamp(value.colors[3], 0.f, 1.f) * 255.f));
        value.hex = hex;
    };
    const auto applyHex = [&] {
        std::string_view hex = value.hex;
        if (hex.starts_with('#')) hex.remove_prefix(1);
        uint32_t color = 0;
        const auto parsed = std::from_chars(
            hex.data(), hex.data() + hex.size(), color, 16);
        if ((hex.size() != 6 && hex.size() != 8) ||
            parsed.ec != std::errc{} || parsed.ptr != hex.data() + hex.size())
            return false;
        if (hex.size() == 6) color = color << 8 | 0xff;
        value.colors = {
            ((color >> 24) & 0xff) / 255.f,
            ((color >> 16) & 0xff) / 255.f,
            ((color >> 8) & 0xff) / 255.f,
            (color & 0xff) / 255.f,
        };
        return true;
    };

    const ImVec4 preview{value.colors[0], value.colors[1], value.colors[2],
                         value.colors[3]};
    bool pressed = false;
    bool hovered = false;
    bool held = false;
    if (Config::get().customMode) {
        const float side = ImGui::GetFrameHeight();
        const auto shown = visible_label(label);
        const bool hideLabel = shown == "Color";
        align_custom_value(hideLabel ? std::string_view{} : shown);
        const ImVec2 right = ImGui::GetCursorScreenPos();
        const ImVec2 min(right.x + ImGui::CalcItemWidth() - side, right.y);
        ImGui::SetCursorScreenPos(min);
        pressed = ImGui::InvisibleButton(
            ("##color_button" + id).c_str(), ImVec2(side, side));
        hovered = ImGui::IsItemHovered();
        held = ImGui::IsItemActive();
        const float hover = animate_last_item(hovered ? 1.0f : 0.0f);
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 max(min.x + side, min.y + side);
        draw->AddRectFilled(min, max, ImGui::GetColorU32(preview), 4.0f);
        if (hover > .001f)
            draw->AddRectFilled(
                min, max,
                IM_COL32(255, 255, 255, static_cast<int>(30.0f * hover)),
                4.0f);
        draw->AddRect(min, max, IM_COL32(34, 34, 34, 255), 4.0f);
    } else
    {
        pressed = ImGui::ColorButton(
            ("##color_button" + id).c_str(), preview,
            ImGuiColorEditFlags_AlphaPreviewHalf);
        ImGui::SameLine();
        const auto suffix = id.find("##");
        ImGui::TextUnformatted(id.data(), id.data() +
            (suffix == std::string::npos ? id.size() : suffix));
        hovered = ImGui::IsItemHovered();
        held = ImGui::IsItemActive();
    }
    const ImGuiID openedKey = ImGui::GetID(("##color_opened" + id).c_str());
    if (pressed) {
        syncHex();
        ImGui::GetStateStorage()->SetFloat(openedKey,
                                           static_cast<float>(ImGui::GetTime()));
        ImGui::OpenPopup(("##color_popup" + id).c_str());
    }
    const float openedAt = ImGui::GetStateStorage()->GetFloat(openedKey, -100.0f);
    bool changed = false;
    const bool styledPopup = Config::get().skeetMode;
    if (styledPopup) {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.067f, 0.067f, 0.067f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.27f, 0.27f, 0.27f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    }
    if (ImGui::BeginPopup(("##color_popup" + id).c_str())) {
        if (ImGui::IsWindowAppearing())
            ImGui::GetStateStorage()->SetBool(ImGui::GetID("##shown"), false);
        if (color_field(value.colors)) {
            changed = true;
            syncHex();
        }

        const ImGuiStyle& popupStyle = ImGui::GetStyle();
        const float copyWidth =
            ImGui::CalcTextSize("Copy").x + popupStyle.FramePadding.x * 2.0f;
        const float pasteWidth =
            ImGui::CalcTextSize("Paste").x + popupStyle.FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(std::max(
            1.0f, color_field_width() - copyWidth - pasteWidth -
                      popupStyle.ItemSpacing.x * 2.0f));
        if (ImGui::InputTextWithHint(("##color_hex" + id).c_str(), "#RRGGBBAA",
                                     &value.hex))
            changed |= applyHex();
        ImGui::SameLine();
        if (ImGui::Button(("Copy##color_" + id).c_str(),
                          ImVec2(copyWidth, ImGui::GetFrameHeight()))) {
            syncHex();
            ImGui::SetClipboardText(value.hex.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button(("Paste##color_" + id).c_str(),
                          ImVec2(pasteWidth, ImGui::GetFrameHeight()))) {
            value.hex =
                ImGui::GetClipboardText() ? ImGui::GetClipboardText() : "";
            changed |= applyHex();
        }
        const float opened = reveal_time(openedAt, COLOR_POPUP_SECONDS);
        if (opened < 1.0f)
            present_current_window(smooth_step(opened),
                                   (1.0f - ease_out_cubic(opened)) *
                                       COLOR_POPUP_RISE * Config::get().uiScale);
        ImGui::EndPopup();
    }
    if (styledPopup) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }
    return {pressed || changed, hovered, held, changed};
}

inline void text(std::string_view value, ImFont* font = nullptr) {
    auto& config = Config::get();
    if (config.customMode && config.customSectionWantsTitle) {
        config.customSectionWantsTitle = false;
        config.customSectionTitle.assign(value);
        return;
    }
    ScopedFont scoped(
        config.skeetMode && font
            ? config.skeetHeaderFont
            : config.skeetMode ? nullptr : font);
    ImGui::TextUnformatted(value.data(), value.data() + value.size());
}
inline void same_line() { ImGui::SameLine(); }
inline void spacer(double size = 0.0) { ImGui::Dummy(ImVec2(0.0f, size)); }
inline void begin_custom_section() {
    auto& config = Config::get();
    const int index = config.customSectionIndex++;
    if (index == 0) {
        const ImVec2 parentPos = ImGui::GetWindowPos();
        const ImVec2 parentSize = ImGui::GetWindowSize();
        config.customSectionClipMin = parentPos;
        config.customSectionClipMax = ImVec2(
            parentPos.x + parentSize.x, parentPos.y + parentSize.y);
        config.customSectionOrigin = ImGui::GetCursorScreenPos();
        config.customSectionOrigin.y += ImGui::GetFontSize() * .5f + 4.0f;
        config.customSectionWidth =
            (ImGui::GetContentRegionAvail().x -
             ImGui::GetStyle().ItemSpacing.x) * .5f;
        config.customSectionColumnY.fill(config.customSectionOrigin.y);
    }
    config.customSectionColumn = index & 1;
    config.customSectionMin = ImVec2(
        config.customSectionOrigin.x + config.customSectionColumn *
            (config.customSectionWidth + ImGui::GetStyle().ItemSpacing.x),
        config.customSectionColumnY[config.customSectionColumn]);
    ImGui::SetCursorScreenPos(config.customSectionMin);
    config.customSectionTitle.clear();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(28, 28, 28, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(34, 34, 34, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 10.0f));
    const std::string id = std::string(AY_OBFUSCATE("##default-section-")) + std::to_string(index);
    const bool fixedHeight = config.customSectionHeight > 0.0f;
    ImGui::BeginChild(
        id.c_str(), ImVec2(config.customSectionWidth,
                           fixedHeight ? config.customSectionHeight : 0.0f),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding |
            (fixedHeight ? 0 : ImGuiChildFlags_AutoResizeY |
                ImGuiChildFlags_AlwaysAutoResize),
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings);
    config.customSectionStarted = true;
    config.customSectionWantsTitle = true;
}
inline void end_custom_section() {
    auto& config = Config::get();
    if (!config.customSectionStarted) return;
    if (!config.customSectionTitle.empty()) {
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(config.customSectionClipMin,
                           config.customSectionClipMax, false);
        const ImVec2 size = ImGui::CalcTextSize(config.customSectionTitle.c_str());
        const ImVec2 textPos(config.customSectionMin.x + 10.0f,
                             config.customSectionMin.y - size.y * .5f);
        draw->AddRectFilled(
            ImVec2(textPos.x - 4.0f, textPos.y),
            ImVec2(textPos.x + size.x + 4.0f, textPos.y + size.y),
            IM_COL32(28, 28, 28, 255));
        draw->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text),
                      config.customSectionTitle.c_str());
        draw->PopClipRect();
    }
    ImGui::EndChild();
    config.customSectionColumnY[config.customSectionColumn] =
        ImGui::GetItemRectMax().y + ImGui::GetFontSize() * .5f + 8.0f;
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    config.customSectionStarted = false;
    config.customSectionWantsTitle = false;
    config.customSectionTitle.clear();
}
inline void divider(bool visible = true) {
    (void)visible;
    auto& config = Config::get();
    if (config.customMode) {
        end_custom_section();
        begin_custom_section();
        return;
    }
    if (!config.skeetMode) {
        ImGui::Separator();
        return;
    }
}
inline void hide_section_box() {
}
inline void off_the_screen() { ImGui::SetCursorPos(ImVec2(-10000.0f, -10000.0f)); }

template <class F> void group(F&& fn, float width = 0.0f) {
    ImGui::BeginGroup();
    if (width > 0.0f) ImGui::PushItemWidth(width);
    fn();
    if (width > 0.0f) ImGui::PopItemWidth();
    ImGui::EndGroup();
}
template <class F> void fraction(double count, F&& fn, double = 0.0) {
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x / count);
    fn();
    ImGui::PopItemWidth();
}
inline void follow_drag(bool dragging) {
    const auto& config = Config::get();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID xKey = ImGui::GetID("##follow-x");
    const ImGuiID yKey = ImGui::GetID("##follow-y");
    const ImGuiID liveKey = ImGui::GetID("##follow-live");
    const ImVec2 position = ImGui::GetWindowPos();
    bool live = storage->GetBool(liveKey, false);
    ImVec2 target = live ? ImVec2(storage->GetFloat(xKey, position.x),
                                  storage->GetFloat(yKey, position.y))
                         : position;
    if (dragging) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        target = ImVec2(target.x + delta.x, target.y + delta.y);
        live = true;
    }
    if (!live) return;
    ImVec2 next = target;
    if (config.playAnimations) {
        const float follow = 1.0f - std::exp(-WINDOW_FOLLOW_RATE *
                                             std::max(config.animationSpeed, .05f) *
                                             ImGui::GetIO().DeltaTime);
        next = ImVec2(position.x + (target.x - position.x) * follow,
                      position.y + (target.y - position.y) * follow);
    }
    if (!dragging && std::fabs(target.x - next.x) < .5f &&
        std::fabs(target.y - next.y) < .5f) {
        next = target;
        live = false;
    }
    ImGui::SetWindowPos(next);
    storage->SetFloat(xKey, target.x);
    storage->SetFloat(yKey, target.y);
    storage->SetBool(liveKey, live);
}

template <class T, class F> void tab(T current, T expected, F&& fn) {
    if (current != expected) return;
    auto& config = Config::get();
    if (config.revealTab != static_cast<int>(expected)) {
        config.revealTab = static_cast<int>(expected);
        config.revealStart = ImGui::GetTime();
    }
    const float elapsed = reveal_time(config.revealStart, TAB_REVEAL_SECONDS);
    const float rise = 1.0f - ease_out_cubic(elapsed);
    const float opacity = smooth_step(elapsed);
    ImDrawList* host = ImGui::GetWindowDrawList();
    const int firstVertex = host->VtxBuffer.Size;
    const int firstChild = child_window_count();
    if (config.skeetMode) config.skeetGridStarted = false;
    if (config.customMode) {
        config.customSectionStarted = false;
        config.customSectionWantsTitle = false;
        config.customSectionIndex = 0;
        config.customSectionWidth = 0.0f;
        config.customSectionColumn = 0;
        config.customSectionOrigin = {};
        config.customSectionColumnY = {};
        config.customSectionHeight = 0.0f;
        config.customSectionTitle.clear();
    }
    ImGui::PushID(static_cast<int>(expected));
    fn();
    if (config.customMode) {
        end_custom_section();
        ImGui::SetCursorScreenPos(ImVec2(
            config.customSectionOrigin.x,
            std::max(config.customSectionColumnY[0],
                     config.customSectionColumnY[1])));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    ImGui::PopID();
    if (config.skeetMode && config.skeetGridStarted) {
        config.skeetGridStarted = false;
    }
    if (rise > 0.0f) {
        const float drop = rise * TAB_REVEAL_RISE * config.uiScale;
        for (int i = firstVertex; i < host->VtxBuffer.Size; ++i)
            host->VtxBuffer[i].pos.y += drop;
        shift_child_windows(firstChild, drop);
    }
    if (opacity < 1.0f) {
        fade_vertices(host, firstVertex, opacity);
        fade_child_windows(firstChild, opacity);
    }
}
template <class F>
void window(ImTextureID logoTex, ImVec2 logoSize, ImVec2 logoUv, F&& fn) {
    const float scale = Config::get().uiScale;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    
    
    
    
    static bool s_wasClosing = false;
    const bool justOpened = s_wasClosing && !Config::get().windowClosing;
    s_wasClosing = Config::get().windowClosing;
    static bool s_placed = false;
    static ImVec2 s_lastWork(-1.0f, -1.0f);
    const ImVec2 work = viewport->WorkSize;
    
    
    
    const bool workChanged = std::fabs(work.x - s_lastWork.x) > 2.0f ||
                             std::fabs(work.y - s_lastWork.y) > 2.0f;
    if (workChanged) s_lastWork = work;

    
    
    ImVec2 size(std::min(620.0f * scale, work.x * .42f),
                std::min(460.0f * scale, work.y * .60f));
    if (Config::get().fitWindowToContent) {
        size = ImVec2(std::min(620.0f * scale, work.x * .92f),
                      std::min(Config::get().fittedWindowHeight * scale,
                               work.y * .90f));
    }
    
    
    const bool interacting = ImGui::IsAnyItemActive() ||
                             ImGui::GetIO().WantTextInput ||
                             ImGui::GetIO().MouseDown[0] ||
                             ImGui::GetIO().MouseDown[1];
    const bool recenter = justOpened || !s_placed || (workChanged && !interacting);
    s_placed = true;
    
    
    if (recenter) {
        const ImVec2 topLeft(viewport->Pos.x + (work.x - size.x) * .5f,
                             viewport->Pos.y + (work.y - size.y) * .5f);
        ImGui::SetNextWindowPos(topLeft, ImGuiCond_Always);
    }
    ImGui::SetNextWindowSize(size, recenter || Config::get().fitWindowToContent
                                       ? ImGuiCond_Always
                                       : ImGuiCond_FirstUseEver);
    if (!Config::get().fitWindowToContent) {
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(460.0f, work.x * .30f), std::min(320.0f, work.y * .40f)),
            work);
    }
    if (ImGui::Begin(AY_OBFUSCATE("Main UI"), nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                     (Config::get().windowClosing ? ImGuiWindowFlags_NoInputs
                                                  : ImGuiWindowFlags_None))) {
        const int firstContentVertex = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        const int firstContentChild = child_window_count();
        const float titleHeight = 28.0f * scale;
        const ImVec2 titlePos = ImGui::GetCursorScreenPos();
        const bool accessory =
            Config::get().customMode && Config::get().titleAccessory;
        const float accessoryWidth = accessory ? titleHeight + 8.0f * scale : 0.0f;
        ImGui::InvisibleButton(
            AY_OBFUSCATE("##titlebar"),
            ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x - accessoryWidth),
                   titleHeight));
        follow_drag(ImGui::IsItemActive() &&
                    ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f));
        Config::get().windowPos = ImGui::GetWindowPos();
        Config::get().windowSize = ImGui::GetWindowSize();

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 titleMin = Config::get().customMode
            ? windowPos : titlePos;
        const ImVec2 titleMax(
            Config::get().customMode ? windowPos.x + windowSize.x
                                     : titlePos.x +
                                           ImGui::GetContentRegionAvail().x,
            titlePos.y + titleHeight);
        draw->AddRectFilled(titleMin, titleMax,
                            ImGui::GetColorU32(ImGuiCol_TitleBgActive),
                            ImGui::GetStyle().WindowRounding,
                            ImDrawFlags_RoundCornersTop);
        if (Config::get().customMode) {
            const char* username = Config::get().username;
            const ImVec2 usernameSize = ImGui::CalcTextSize(username);
            const float textY = titlePos.y +
                (titleHeight - ImGui::GetFontSize()) * .5f - 2.0f * scale;
            static constexpr std::array quotes{
                "~ 'one frame at a time'", "~ 'stay precise'",
                "~ 'trust the replay'", "~ 'keep moving forward'",
                "~ 'practice makes progress'"};
            static const size_t quoteIndex = static_cast<size_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()) %
                quotes.size();
            draw->AddText(ImVec2(titleMin.x + 10.0f, textY),
                          IM_COL32(204, 204, 204, 145), quotes[quoteIndex]);
            if (accessory) {
                const float side = titleHeight - 8.0f * scale;
                const ImVec2 min(titleMax.x - side - 8.0f * scale,
                                 (titleMin.y + titleMax.y - side) * .5f);
                const ImVec2 resume = ImGui::GetCursorScreenPos();
                Config::get().titleAccessory(min, ImVec2(min.x + side, min.y + side));
                ImGui::SetCursorScreenPos(resume);
            } else {
                draw->AddText(ImVec2(titleMax.x - usernameSize.x - 10.0f, textY),
                              IM_COL32(204, 204, 204, 255), username);
            }
            draw->AddLine(
                ImVec2(titleMin.x, titleMax.y), titleMax,
                IM_COL32(34, 34, 34, 255), 1.0f);
        } else if (logoTex) {
            float drawHeight = titleHeight * 0.75f;
            float drawWidth = logoSize.x * (drawHeight / logoSize.y);
            ImVec2 pMin = ImVec2(titlePos.x + (titleMax.x - titlePos.x - drawWidth) * 0.5f,
                                 titlePos.y + (titleHeight - drawHeight) * 0.5f);
            ImVec2 pMax = ImVec2(pMin.x + drawWidth, pMin.y + drawHeight);

            ImVec2 shadowOffset(2.0f, 2.0f);
            draw->AddImage(logoTex, ImVec2(pMin.x + shadowOffset.x, pMin.y + shadowOffset.y),
                           ImVec2(pMax.x + shadowOffset.x, pMax.y + shadowOffset.y),
                           ImVec2(0,0), logoUv, IM_COL32(0, 0, 0, 180));

            draw->AddImage(logoTex, pMin, pMax, ImVec2(0,0), logoUv, IM_COL32(255, 255, 255, 255));
        }
        fn();
        if (Config::get().contentOpacity < 1.0f) {
            fade_vertices(ImGui::GetWindowDrawList(), firstContentVertex,
                          Config::get().contentOpacity);
            fade_child_windows(firstContentChild, Config::get().contentOpacity);
        }
        if (Config::get().windowOpacity < 1.0f)
            present_current_window(Config::get().windowOpacity,
                                   Config::get().windowOffset);
    }
    ImGui::End();
}
template <class F> void section(std::string_view id, F&& fn,
                                float width = 0.0f, bool = false) {
    if (width > 0.0f) {
        ImGui::BeginChild(std::string(id).c_str(), ImVec2(width, 0.0f), true);
        fn();
        ImGui::EndChild();
        ImGui::SameLine();
    } else {
        ImGui::BeginChild(std::string(id).c_str(), ImVec2(0.0f, 0.0f), true);
        fn();
        ImGui::EndChild();
    }
}

}
