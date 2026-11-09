#ifndef LABEL_LABEL_HPP
#define LABEL_LABEL_HPP

#include <Geode/Geode.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "shared/value/value.hpp"

using namespace cocos2d;

constexpr bool labelFontsSupported() {
#if defined(GEODE_IS_IOS) || defined(GEODE_IS_MACOS)
    return true;
#else
    return false;
#endif
}

class GrapeLabel {
   public:
    enum class LabelAnchor { TopLeft, TopRight, BottomLeft, BottomRight };
    enum class LabelFont { BigFont, ChatFont };

    struct LabelConfig {
        bool m_enabled = false;
        LabelAnchor m_anchor = LabelAnchor::TopLeft;
        LabelFont m_font = LabelFont::ChatFont;
        std::string m_customFont;
        float m_opacity = 1.0f;
        float m_scale = 0.7f;
    };

    using Text = std::function<std::string(GrapeLabel&)>;

    LabelConfig m_config;

    GrapeLabel() = default;
    GrapeLabel(std::string id, std::string title, Text text, LabelConfig config)
        : m_config(std::move(config)), m_id(std::move(id)), m_title(std::move(title)),
          m_text(std::move(text)) {}

    const std::string& getId() const { return m_id; }
    const std::string& getFriendlyName() const { return m_title; }

    CCNode* get();
    void setColor(ccColor3B color);
    void update(bool hidden, bool relayout, float& stackHeight);

   private:
    std::string nodeId() const;
    std::string wantedFont() const;
    CCNode* createNode(std::string& font);
    void place(CCNode* node, float& stackHeight);

    std::string m_id;
    std::string m_title;
    Text m_text;
    std::string m_nodeFont;
};

struct CustomLabelDef {
    std::string m_id;
    std::string m_name = "Custom Label";
    std::string m_format = "{ levelName }";
    GrapeLabel::LabelConfig m_config;
};

class LabelManager {
   public:
    std::vector<GrapeLabel> m_labels;
    bool m_requiresRefresh = false;
    bool m_globalEnabled = true;
    ConfigValuePtr<bool> m_globalEnabledValue =
        ConfigValue<bool>::create("labels.global_enabled", &m_globalEnabled);

    LabelManager();

    void addLabel(std::string id, std::string title, GrapeLabel::Text text, GrapeLabel::LabelConfig config = {}) {
        m_labels.emplace_back(std::move(id), std::move(title), std::move(text), std::move(config));
    }

    void update(bool forceDisable = false);
    void readFromConfig();
    void writeToConfig();

    size_t builtinCount() const { return m_builtinCount; }
    bool isCustom(size_t index) const { return index >= m_builtinCount; }
    size_t customCount() const { return m_customLabels.size(); }
    CustomLabelDef* customFor(size_t labelIndex);
    const std::string& customError(size_t labelIndex) const;

    size_t addCustomLabel();
    void removeCustomLabel(size_t labelIndex);
    void setCustomFormat(size_t labelIndex, std::string format);
    void setCustomName(size_t labelIndex, std::string name);
    void readCustomFromConfig();
    void writeCustomToConfig();

   private:
    struct Compiled {
        std::shared_ptr<void> m_script;
        std::string m_error;
    };

    static Compiled compile(const std::string& format);
    std::string freshCustomId();
    void rebuildCustomLabels();
    std::string runCustom(size_t customIndex);

    size_t m_builtinCount = 0;
    uint32_t m_nextCustomId = 0;
    std::vector<CustomLabelDef> m_customLabels;
    std::vector<Compiled> m_compiled;
};

namespace grape::labels {
void registerBuiltins(LabelManager& manager);
}

#endif
