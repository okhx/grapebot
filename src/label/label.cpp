#include "label.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include <glaze/glaze.hpp>

#include "engine/engine.hpp"
#include "label/variables.hpp"
#include "util/storage.hpp"

using namespace geode::prelude;

template <>
struct glz::meta<GrapeLabel::LabelConfig> {
    using T = GrapeLabel::LabelConfig;
    static constexpr auto value = object("enabled", &T::m_enabled, "anchor", &T::m_anchor, "font", &T::m_font,
                                         "custom_font", &T::m_customFont, "opacity", &T::m_opacity,
                                         "scale", &T::m_scale);
};

template <>
struct glz::meta<CustomLabelDef> {
    using T = CustomLabelDef;
    static constexpr auto value =
        object("id", &T::m_id, "name", &T::m_name, "format", &T::m_format, "config", &T::m_config);
};

namespace {

constexpr int LABEL_LAYER = 100000;
constexpr float EDGE_GAP = 8.0f;
constexpr float BOTTOM_LEFT_GAP = 10.0f;
constexpr float LINE_GAP = 4.0f;
constexpr float TTF_SIZE = 20.0f;
constexpr const char* PLACEHOLDER = "Loading...";

struct Corner {
    bool m_right;
    bool m_top;
    float m_sideGap;
};

constexpr std::array<Corner, 4> CORNERS{{
    {false, true, EDGE_GAP},
    {true, true, EDGE_GAP},
    {false, false, BOTTOM_LEFT_GAP},
    {true, false, EDGE_GAP},
}};

const char* builtinFont(GrapeLabel::LabelFont font) {
    return font == GrapeLabel::LabelFont::BigFont ? "bigFont.fnt" : "chatFont.fnt";
}

std::filesystem::path configFile(const char* name) {
    return grape::paths::toStd(Mod::get()->getConfigDir() / name);
}

template <class T>
bool loadJson(T& into, const char* name, const char* what) {
    const auto path = configFile(name);
    if (!std::filesystem::exists(path)) return true;
    T loaded{};
    if (auto error = glz::read_file_json(loaded, path.string(), std::string{})) {
        log::error("Failed to read {}: {}", what, error.custom_error_message);
        return false;
    }
    into = std::move(loaded);
    return true;
}

template <class T>
void saveJson(const T& value, const char* name, const char* what) {
    auto error = glz::write_file_json<glz::opts{.prettify = true}>(value, configFile(name).string(), std::string{});
    if (error) log::error("Failed to write {}: {}", what, error.custom_error_message);
}

template <class Each>
void eachText(CCNode* node, Each&& each) {
    if (auto* bitmap = typeinfo_cast<CCLabelBMFont*>(node)) each(bitmap);
    if (auto* truetype = typeinfo_cast<CCLabelTTF*>(node)) each(truetype);
}

const std::string NO_ERROR_TEXT;

}

std::string GrapeLabel::nodeId() const { return fmt::format("{}/label.{}", Mod::get()->getID(), m_id); }

std::string GrapeLabel::wantedFont() const {
    return m_config.m_customFont.empty() ? builtinFont(m_config.m_font) : m_config.m_customFont;
}

CCNode* GrapeLabel::createNode(std::string& font) {
    if (m_config.m_customFont.empty()) return CCLabelBMFont::create(PLACEHOLDER, font.c_str());

    const auto file = grape::paths::directory("fonts") / font;
    std::error_code error;
    if (labelFontsSupported() && std::filesystem::is_regular_file(file, error))
        return CCLabelTTF::create(PLACEHOLDER, file.string().c_str(), TTF_SIZE);

    m_config.m_customFont.clear();
    font = builtinFont(m_config.m_font);
    return CCLabelBMFont::create(PLACEHOLDER, font.c_str());
}

CCNode* GrapeLabel::get() {
    auto* level = PlayLayer::get();
    if (!level) return nullptr;

    const std::string id = nodeId();
    std::string font = wantedFont();
    if (auto* current = level->getChildByID(id)) {
        if (font == m_nodeFont) return current;
        current->removeFromParentAndCleanup(true);
    }

    CCNode* node = createNode(font);
    if (!node) return nullptr;
    m_nodeFont = font;
    node->setID(id);
    node->setScale(m_config.m_scale);
    level->addChild(node, LABEL_LAYER);
    return node;
}

void GrapeLabel::setColor(ccColor3B color) {
    eachText(get(), [&](auto* text) { text->setColor(color); });
}

void GrapeLabel::place(CCNode* node, float& stackHeight) {
    const auto size = PlayLayer::get()->getContentSize();
    const Corner& corner = CORNERS[static_cast<int>(m_config.m_anchor)];
    const float x = corner.m_right ? size.width - corner.m_sideGap : corner.m_sideGap;
    const float y = corner.m_top ? size.height - EDGE_GAP - stackHeight : stackHeight + EDGE_GAP;
    stackHeight += node->getScaledContentSize().height + LINE_GAP * m_config.m_scale;
    node->setPosition({x, y});
    node->setAnchorPoint({corner.m_right ? 1.0f : 0.0f, corner.m_top ? 1.0f : 0.0f});
}

void GrapeLabel::update(bool hidden, bool relayout, float& stackHeight) {
    if (!PlayLayer::get()) return;
    CCNode* node = get();
    if (!node) return;

    if (relayout && m_config.m_enabled) {
        const auto alpha = static_cast<GLubyte>(m_config.m_opacity * 255.0f);
        eachText(node, [&](auto* text) { text->setOpacity(alpha); });
        node->setScale(m_config.m_scale);
        place(node, stackHeight);
    }
    node->setVisible(m_config.m_enabled && !hidden);
    const std::string text = m_text(*this);
    if (auto* writable = dynamic_cast<CCLabelProtocol*>(node)) writable->setString(text.c_str());
}

LabelManager::LabelManager() {
    grape::labels::registerBuiltins(*this);
    m_builtinCount = m_labels.size();
}

void LabelManager::update(bool forceDisable) {
    grape::labels::invalidateContext();
    std::array<float, 4> stacks{};
    const bool hidden = forceDisable || !m_globalEnabled;
    for (GrapeLabel& label : m_labels)
        label.update(hidden, m_requiresRefresh, stacks[static_cast<int>(label.m_config.m_anchor)]);
    m_requiresRefresh = false;
}

void LabelManager::readFromConfig() {
    std::unordered_map<std::string, GrapeLabel::LabelConfig> saved;
    if (!loadJson(saved, "labels.json", "label config")) return;
    for (GrapeLabel& label : m_labels) label.m_config = saved[label.getId()];
    m_requiresRefresh = true;
}

void LabelManager::writeToConfig() {
    std::unordered_map<std::string, GrapeLabel::LabelConfig> saved;
    for (const GrapeLabel& label : m_labels) saved[label.getId()] = label.m_config;
    saveJson(saved, "labels.json", "label config");
}

CustomLabelDef* LabelManager::customFor(size_t labelIndex) {
    if (!isCustom(labelIndex)) return nullptr;
    const size_t index = labelIndex - m_builtinCount;
    return index < m_customLabels.size() ? &m_customLabels[index] : nullptr;
}

const std::string& LabelManager::customError(size_t labelIndex) const {
    if (!isCustom(labelIndex)) return NO_ERROR_TEXT;
    const size_t index = labelIndex - m_builtinCount;
    return index < m_compiled.size() ? m_compiled[index].m_error : NO_ERROR_TEXT;
}

LabelManager::Compiled LabelManager::compile(const std::string& format) {
    auto script = std::make_shared<grape::rift::Script>(grape::rift::Script::compile(format));
    std::string error = script->valid() ? std::string{} : script->error();
    return {std::move(script), std::move(error)};
}

std::string LabelManager::freshCustomId() { return fmt::format("custom.{}", m_nextCustomId++); }

std::string LabelManager::runCustom(size_t customIndex) {
    if (customIndex >= m_compiled.size()) return {};
    auto script = std::static_pointer_cast<grape::rift::Script>(m_compiled[customIndex].m_script);
    return script ? script->run(grape::labels::sharedContext()) : std::string{};
}

void LabelManager::rebuildCustomLabels() {
    m_labels.resize(m_builtinCount);
    m_compiled.clear();
    m_compiled.reserve(m_customLabels.size());
    for (size_t i = 0; i < m_customLabels.size(); ++i) {
        CustomLabelDef& def = m_customLabels[i];
        m_compiled.push_back(compile(def.m_format));
        if (def.m_id.empty()) def.m_id = freshCustomId();
        addLabel(def.m_id, def.m_name, [this, i](GrapeLabel&) { return runCustom(i); }, def.m_config);
    }
    m_requiresRefresh = true;
}

size_t LabelManager::addCustomLabel() {
    CustomLabelDef def;
    def.m_id = freshCustomId();
    def.m_name = fmt::format("Custom Label {}", m_customLabels.size() + 1);
    def.m_config.m_enabled = true;
    m_customLabels.push_back(std::move(def));
    rebuildCustomLabels();
    return m_builtinCount + m_customLabels.size() - 1;
}

void LabelManager::removeCustomLabel(size_t labelIndex) {
    if (!customFor(labelIndex)) return;
    if (CCNode* node = m_labels[labelIndex].get()) node->removeFromParent();
    m_customLabels.erase(m_customLabels.begin() + static_cast<std::ptrdiff_t>(labelIndex - m_builtinCount));
    rebuildCustomLabels();
}

void LabelManager::setCustomFormat(size_t labelIndex, std::string format) {
    CustomLabelDef* def = customFor(labelIndex);
    if (!def || def->m_format == format) return;
    def->m_format = std::move(format);
    m_compiled[labelIndex - m_builtinCount] = compile(def->m_format);
    m_requiresRefresh = true;
}

void LabelManager::setCustomName(size_t labelIndex, std::string name) {
    CustomLabelDef* def = customFor(labelIndex);
    if (!def || def->m_name == name) return;
    def->m_name = std::move(name);
    rebuildCustomLabels();
}

void LabelManager::readCustomFromConfig() {
    loadJson(m_customLabels, "custom_labels.json", "custom labels");
    std::unordered_set<std::string> taken;
    for (CustomLabelDef& def : m_customLabels) {
        if (def.m_id.empty() || taken.contains(def.m_id)) def.m_id = freshCustomId();
        taken.insert(def.m_id);
    }
    rebuildCustomLabels();
}

void LabelManager::writeCustomToConfig() {
    for (size_t i = 0; i < m_customLabels.size(); ++i) {
        const size_t labelIndex = m_builtinCount + i;
        if (labelIndex < m_labels.size()) m_customLabels[i].m_config = m_labels[labelIndex].m_config;
    }
    saveJson(m_customLabels, "custom_labels.json", "custom labels");
}

$on_mod(Loaded) {
    auto& labels = GrapeEngine::get()->labels();
    labels.readCustomFromConfig();
    labels.readFromConfig();
}


$on_mod(DataSaved) {
    auto& labels = GrapeEngine::get()->labels();
    labels.writeToConfig();
    labels.writeCustomToConfig();
}
