#pragma once

#include <Geode/Geode.hpp>
#include <glaze/json/read.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "keybind.hpp"
#include "util/atomic_file.hpp"

struct BindingInterface {
    virtual ~BindingInterface() = default;

    virtual const std::string& getId() = 0;
    virtual void* getValue() = 0;
    virtual void* getPrevious() = 0;
    virtual void notifyChange() = 0;
    virtual std::shared_ptr<KeybindControl> createKeybind(RawKeybind& keybind) = 0;
};

class BindingManager {
   public:
    using Keybind = std::shared_ptr<KeybindControl>;
    using Binding = std::shared_ptr<BindingInterface>;

    static BindingManager* get() {
        static BindingManager manager;
        return &manager;
    }

    BindingManager(const BindingManager&) = delete;
    void operator=(const BindingManager&) = delete;

    void registerValue(Binding binding) { m_bindings[binding->getId()] = std::move(binding); }

    bool hasValue(const std::string& tag) const { return m_bindings.contains(tag); }

    Binding getValue(const std::string& tag) const {
        const auto found = m_bindings.find(tag);
        return found == m_bindings.end() ? nullptr : found->second;
    }

    std::vector<std::string> getValueTags() const {
        std::vector<std::string> tags;
        tags.reserve(m_bindings.size());
        for (const auto& entry : m_bindings) tags.push_back(entry.first);
        std::ranges::sort(tags);
        return tags;
    }

    void registerKeybind(Keybind keybind) {
        geode::log::info("Registering new keybind for {}", keybind->getTag());
        m_byChord[keybind->getHash()].push_back(std::move(keybind));
    }

    bool addKeybindForTag(const std::string& tag, RawKeybind& raw) {
        const Binding binding = getValue(tag);
        if (!binding) return false;
        registerKeybind(binding->createKeybind(raw));
        return true;
    }

    std::vector<Keybind> getKeybindsForTag(const std::string& tag) const {
        std::vector<Keybind> matching;
        for (const auto& [chord, keybinds] : m_byChord)
            for (const auto& keybind : keybinds)
                if (keybind->getTag() == tag) matching.push_back(keybind);
        return matching;
    }

    void removeKeybind(const Keybind& keybind) {
        const auto chord = m_byChord.find(keybind->getHash());
        if (chord == m_byChord.end()) return;
        release(keybind);
        std::erase(chord->second, keybind);
        if (chord->second.empty()) m_byChord.erase(chord);
    }

    void removeAllKeybindsForTag(const std::string& tag) {
        const auto ownedByTag = [&](const Keybind& keybind) { return keybind->getTag() == tag; };
        for (auto& [chord, keybinds] : m_byChord) {
            for (const auto& keybind : keybinds)
                if (ownedByTag(keybind)) release(keybind);
            std::erase_if(keybinds, ownedByTag);
        }
        std::erase_if(m_byChord, [](const auto& entry) { return entry.second.empty(); });
    }

    void setTagFilter(std::function<bool(const std::string&)> allow) { m_allowTag = std::move(allow); }
    bool tagAllowed(const std::string& tag) const { return !m_allowTag || m_allowTag(tag); }

    void processKeyEvent(int key, bool pressed, bool ctrl, bool shift, bool alt) {
        const int modifiers = grape::keybinds::normalizeModifiers(key, grape::keybinds::modifierMask(ctrl, shift, alt));
        int chord = grape::keybinds::chord(key, modifiers);
        if (!m_pressed.resolve(key, pressed, chord)) return;
        dispatch(chord, pressed);
    }

    void pump(int holdDelayMs, int repeatMs) {
        
        
        
        const auto timing = [&](int chord) {
            if (const auto found = m_byChord.find(chord); found != m_byChord.end()) {
                for (const auto& keybind : found->second) {
                    const auto& tag = keybind->getTag();
                    if (tag == "updater.advance_one" || tag == "updater.advance_back")
                        return KeyPressTracker::RepeatTiming{100, 25};
                }
            }
            return KeyPressTracker::RepeatTiming{holdDelayMs, repeatMs};
        };
        m_pressed.pump(timing, [&](int chord) { dispatch(chord, true); });
    }

    void writeToFile(const std::filesystem::path& path) {
        std::vector<Keybind> all;
        for (const auto& [chord, keybinds] : m_byChord) all.insert(all.end(), keybinds.begin(), keybinds.end());

        std::string json;
        if (glz::write<glz::opts{.prettify = true}>(all, json)) {
            geode::log::error("Failed to serialize keybinds");
            return;
        }
        std::error_code error;
        if (!grape::files::writeAtomically(path, json, error))
            geode::log::error("Failed to save keybinds: {}", error.message());
    }

    void readFromFile(const std::filesystem::path& path) {
        if (std::exchange(m_loaded, true)) return;

        std::vector<RawKeybind> saved;
        if (auto error = glz::read_file_json(saved, path.string(), std::string{})) {
            geode::log::error("Failed to read keybinds: {}, assuming default keybinds",
                              glz::format_error(error, std::string{}));
            installDefaults();
            return;
        }
        geode::log::info("Read keybinds successfully!");
        if (saved.empty()) {
            installDefaults();
            return;
        }

        for (RawKeybind& keybind : saved) {
            grape::keybinds::migrateLegacy(keybind);
            const Binding binding = getValue(keybind.m_valueTag);
            if (!binding) {
                geode::log::error("Failed to register keybind for tag {}. Does it exist?", keybind.m_valueTag);
                continue;
            }
            registerKeybind(binding->createKeybind(keybind));
        }

        
        
        
        bool hasMenuKey = false;
        for (const auto& keybind : getKeybindsForTag("ui.visible"))
            if ((keybind->getHash() & 0xFFFFF) == 'F') hasMenuKey = true;
        if (!hasMenuKey) {
            RawKeybind fallback{'F', 0, "ui.visible", KeybindType::Toggle, true, "1"};
            addKeybindForTag("ui.visible", fallback);
        }
    }

    void wantNewKey() { m_capture = {.m_waiting = true}; }
    void cancelNewKey() { m_capture = {}; }

    void setNewKey(cocos2d::enumKeyCodes key, bool ctrl, bool shift, bool alt) {
        const int code = static_cast<int>(key);
        m_capture = {
            .m_received = true,
            .m_key = key,
            .m_modifiers = grape::keybinds::normalizeModifiers(code, grape::keybinds::modifierMask(ctrl, shift, alt)),
        };
    }

    cocos2d::enumKeyCodes getNewKey() {
        if (!std::exchange(m_capture.m_received, false)) return cocos2d::enumKeyCodes::KEY_None;
        return m_capture.m_key;
    }

    int getNewModifiers() const { return m_capture.m_modifiers; }
    bool isWaitingForKey() const { return m_capture.m_waiting; }
    bool hasNewKey() const { return m_capture.m_received; }

   private:
    struct KeyCapture {
        bool m_waiting = false;
        bool m_received = false;
        cocos2d::enumKeyCodes m_key = cocos2d::enumKeyCodes::KEY_None;
        int m_modifiers = 0;
    };

    BindingManager() = default;

    void release(const Keybind& keybind) {
        const Binding binding = getValue(keybind->getTag());
        if (binding && keybind->applyValue(false, binding->getValue(), binding->getPrevious()))
            binding->notifyChange();
    }

    void dispatch(int chord, bool pressed) {
        const auto keybinds = m_byChord.find(chord);
        if (keybinds == m_byChord.end()) return;
        for (const auto& keybind : keybinds->second) {
            if (!tagAllowed(keybind->getTag())) continue;
            const auto binding = m_bindings.find(keybind->getTag());
            if (binding == m_bindings.end()) continue;
            if (keybind->applyValue(pressed, binding->second->getValue(), binding->second->getPrevious()))
                binding->second->notifyChange();
        }
    }

    void installDefaults() {
        struct Default {
            int m_key;
            const char* m_tag;
        };
        static constexpr Default defaults[] = {
            {'F', "ui.visible"},
            {grape::keybinds::FRAME_ADVANCE_KEY, "updater.frame_advance"},
            {'B', "updater.advance_back"},
            {grape::keybinds::ADVANCE_ONE_KEY, "updater.advance_one"},
            {84, "trajectory.enabled"},
        };
        for (const Default& entry : defaults) {
            const bool oneShot = std::string_view(entry.m_tag).starts_with("updater.advance_");
            RawKeybind raw{entry.m_key, 0, entry.m_tag,
                           oneShot ? KeybindType::Override : KeybindType::Toggle,
                           true, "1"};
            if (!addKeybindForTag(entry.m_tag, raw))
                geode::log::error("Unknown default keybind tag {}", entry.m_tag);
        }
    }

    std::unordered_map<std::string, Binding> m_bindings;
    std::unordered_map<KeybindControl::HashT, std::vector<Keybind>> m_byChord;
    std::function<bool(const std::string&)> m_allowTag;
    KeyPressTracker m_pressed;
    KeyCapture m_capture;
    bool m_loaded = false;
};

template <typename T>
struct ConfigValuePtr;

template <typename T>
class ConfigValue : public BindingInterface {
   public:
    ConfigValue(std::string tag, T* storage) : m_tag(std::move(tag)), m_storage(storage), m_before(*storage) {}

    explicit operator bool() const = delete;

    static ConfigValuePtr<T> create(std::string tag, T* storage) {
        ConfigValuePtr<T> handle;
        handle.m_shared = std::make_shared<ConfigValue>(std::move(tag), storage);
        BindingManager::get()->registerValue(handle.m_shared);
        return handle;
    }

    T& inner() { return *m_storage; }
    const T& inner() const { return *m_storage; }

    void handle(std::function<void(T&)> onChange) { m_onChange = std::move(onChange); }

    void notifyChange() override {
        if (m_onChange) m_onChange(*m_storage);
    }

    const std::string& getId() override { return m_tag; }
    void* getValue() override { return m_storage; }
    void* getPrevious() override { return &m_before; }

    std::shared_ptr<KeybindControl> createKeybind(RawKeybind& keybind) override {
        return GrapeKeybind<T>::createFromString(keybind.m_valueTag, keybind.m_key, keybind.m_type,
                                                 keybind.m_value, keybind.m_modifiers, keybind.m_active);
    }

   private:
    std::string m_tag;
    T* m_storage;
    T m_before;
    std::function<void(T&)> m_onChange;
};

template <typename T>
struct ConfigValuePtr {
    explicit operator bool() const = delete;

    ConfigValue<T>* operator->() { return m_shared.get(); }
    const ConfigValue<T>* operator->() const { return m_shared.get(); }

   private:
    ConfigValuePtr() = default;
    std::shared_ptr<ConfigValue<T>> m_shared;

    friend class ConfigValue<T>;
};
