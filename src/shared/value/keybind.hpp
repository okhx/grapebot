#pragma once

#include <glaze/glaze.hpp>

#include <algorithm>
#include <chrono>
#include <concepts>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class KeybindType { Hold = 0, Toggle, Override };

struct RawKeybind {
    int m_key;
    int m_modifiers;
    std::string m_valueTag;
    KeybindType m_type;
    bool m_active = true;
    std::string m_value;
};

namespace grape::keybinds {

inline constexpr int SHIFT = 0b001;
inline constexpr int CTRL = 0b010;
inline constexpr int ALT = 0b100;
inline constexpr int FRAME_ADVANCE_KEY = 'C';
inline constexpr int ADVANCE_ONE_KEY = 'V';
inline constexpr int MODIFIER_OFFSET = 20;

inline constexpr int SHIFT_KEY_CODE = 0x10;
inline constexpr int CTRL_KEY_CODE = 0x11;
inline constexpr int ALT_KEY_CODE = 0x12;

inline constexpr int modifierMask(bool ctrl, bool shift, bool alt) {
    return (ctrl ? CTRL : 0) | (shift ? SHIFT : 0) | (alt ? ALT : 0);
}

inline constexpr int normalizeModifiers(int key, int modifiers) {
    constexpr std::pair<int, int> selfModifying[] = {
        {CTRL_KEY_CODE, CTRL}, {SHIFT_KEY_CODE, SHIFT}, {ALT_KEY_CODE, ALT}};
    for (const auto& [code, bit] : selfModifying)
        if (key == code) modifiers &= ~bit;
    return modifiers;
}

inline constexpr int chord(int key, int modifiers) { return key | (modifiers << MODIFIER_OFFSET); }

inline void migrateLegacy(RawKeybind& keybind) {
    const bool oldMenuShortcut = keybind.m_key == ALT_KEY_CODE && keybind.m_modifiers == SHIFT &&
                                 keybind.m_valueTag == "ui.visible";
    if (oldMenuShortcut) keybind.m_modifiers = 0;
    
    
    if (keybind.m_key == 18 && keybind.m_valueTag == "ui.visible")
        keybind.m_key = 'F';
    
    
    if (keybind.m_valueTag == "updater.advance_one" ||
        keybind.m_valueTag == "updater.advance_back")
        keybind.m_type = KeybindType::Override;
    keybind.m_modifiers = normalizeModifiers(keybind.m_key, keybind.m_modifiers);
}

}

class KeyPressTracker {
   public:
    using Clock = std::chrono::steady_clock;

    bool resolve(int key, bool pressed, int& chord) {
        if (!pressed) {
            if (auto held = m_down.find(key); held != m_down.end()) {
                chord = held->second.m_chord;
                m_down.erase(held);
            }
            return true;
        }
        const auto now = Clock::now();
        const auto [held, fresh] = m_down.try_emplace(key, Held{chord, now, now});
        if (!fresh) chord = held->second.m_chord;
        return fresh;
    }

    struct RepeatTiming {
        int m_holdDelay;
        int m_repeat;
    };

    template <typename Timing, typename Fire>
    void pump(Timing&& timingFor, Fire&& fire) {
        const auto now = Clock::now();
        for (auto& [key, held] : m_down) {
            const RepeatTiming timing = timingFor(held.m_chord);
            if (timing.m_repeat <= 0) continue;
            if (!held.m_repeating) {
                if (millisecondsBetween(held.m_since, now) < timing.m_holdDelay)
                    continue;
                held.m_repeating = true;
            }
            if (millisecondsBetween(held.m_lastFire, now) < timing.m_repeat)
                continue;
            held.m_lastFire = now;
            fire(held.m_chord);
        }
    }

    template <typename Fire>
    void pump(int holdDelayMs, int repeatMs, Fire&& fire) {
        pump([&](int) { return RepeatTiming{holdDelayMs, repeatMs}; },
             std::forward<Fire>(fire));
    }

   private:
    struct Held {
        int m_chord;
        Clock::time_point m_since;
        Clock::time_point m_lastFire;
        bool m_repeating = false;
    };

    static long long millisecondsBetween(Clock::time_point from, Clock::time_point to) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
    }

    std::unordered_map<int, Held> m_down;
};

template <>
struct glz::meta<RawKeybind> {
    using T = RawKeybind;
    static constexpr auto value = object("key", &T::m_key, "modifiers", &T::m_modifiers, "tag", &T::m_valueTag,
                                         "type", &T::m_type, "value", &T::m_value, "active", &T::m_active);
};

class KeybindControl {
   public:
    using HashT = int;

    virtual ~KeybindControl() = default;

    HashT getHash() const { return grape::keybinds::chord(m_key, m_modifiers); }
    const std::string& getTag() const { return m_valueTag; }

    virtual bool applyValue(bool pressed, void* value, void* previous) = 0;
    virtual void fromString(const std::string& text) = 0;
    virtual std::string toString() const = 0;

   protected:
    KeybindControl(int key, int modifiers, std::string tag, KeybindType type, bool active)
        : m_key(key), m_modifiers(modifiers), m_valueTag(std::move(tag)), m_type(type), m_active(active) {}

    int m_key;
    int m_modifiers;
    std::string m_valueTag;
    KeybindType m_type;
    bool m_enabled = false;
    bool m_active = true;

    friend glz::meta<KeybindControl>;
};

template <typename T>
class GrapeKeybind : public KeybindControl {
   public:
    static constexpr int MODIFIER_SHIFT = grape::keybinds::SHIFT;
    static constexpr int MODIFIER_CTRL = grape::keybinds::CTRL;
    static constexpr int MODIFIER_ALT = grape::keybinds::ALT;

    GrapeKeybind(int key, int modifiers, KeybindType type, T value, std::string tag, bool active = true)
        : KeybindControl(key, modifiers, std::move(tag), type, active), m_value(std::move(value)) {}

    static std::shared_ptr<GrapeKeybind> create(std::string tag, int key, KeybindType type, T value,
                                                int modifiers = 0) {
        return std::make_shared<GrapeKeybind>(key, modifiers, type, std::move(value), std::move(tag));
    }

    static std::shared_ptr<GrapeKeybind> createFromString(std::string tag, int key, KeybindType type,
                                                          std::string text, int modifiers = 0,
                                                          bool active = true) {
        return std::make_shared<GrapeKeybind>(key, modifiers, type, parse(text), std::move(tag), active);
    }

    bool applyValue(bool pressed, void* value, void* previous) override {
        if (!m_active) return false;
        T& current = *static_cast<T*>(value);
        T& before = *static_cast<T*>(previous);
        switch (m_type) {
            case KeybindType::Toggle: return pressed && toggle(current, before);
            case KeybindType::Override: return pressed && force(current, before);
            case KeybindType::Hold: return pressed ? beginHold(current, before) : endHold();
        }
        return false;
    }

    static T parse(const std::string& text) {
        if constexpr (std::same_as<T, std::string>) {
            return text;
        } else {
            T parsed{};
            std::istringstream(text) >> parsed;
            return parsed;
        }
    }

    void fromString(const std::string& text) override { m_value = parse(text); }

    std::string toString() const override {
        if constexpr (std::same_as<T, std::string>) {
            return m_value;
        } else {
            std::ostringstream text;
            text << m_value;
            return text.str();
        }
    }

   private:
    struct HeldValue {
        T m_restore;
        std::vector<GrapeKeybind*> m_holders;
    };
    inline static std::unordered_map<T*, HeldValue> s_held;

    bool toggle(T& current, T& before) {
        if (current == m_value) {
            
            
            if constexpr (std::same_as<T, bool>) {
                current = before == m_value ? !m_value : before;
            } else {
                current = before;
            }
        } else {
            before = current;
            current = m_value;
        }
        return true;
    }

    bool force(T& current, T& before) {
        before = current;
        current = m_value;
        return true;
    }

    bool beginHold(T& current, T& before) {
        if (m_enabled) return false;
        const auto [entry, first] = s_held.try_emplace(&current, HeldValue{current, {}});
        if (first) before = current;
        entry->second.m_holders.push_back(this);
        m_target = &current;
        m_targetBefore = &before;
        m_enabled = true;
        const bool changed = current != m_value;
        current = m_value;
        return changed;
    }

    bool endHold() {
        if (!m_enabled || !m_target) return false;
        T* target = std::exchange(m_target, nullptr);
        T* targetBefore = std::exchange(m_targetBefore, nullptr);
        m_enabled = false;

        const auto entry = s_held.find(target);
        if (entry == s_held.end()) return false;
        auto& holders = entry->second.m_holders;
        const bool onTop = !holders.empty() && holders.back() == this;
        std::erase(holders, this);

        bool changed = false;
        if (onTop) {
            const T& next = holders.empty() ? entry->second.m_restore : holders.back()->m_value;
            changed = *target != next;
            *target = next;
        }
        if (holders.empty()) {
            if (targetBefore) *targetBefore = m_value;
            s_held.erase(entry);
        }
        return changed;
    }

    T m_value;
    T* m_target = nullptr;
    T* m_targetBefore = nullptr;
};

template <>
struct glz::meta<KeybindControl> {
    using T = KeybindControl;
    static constexpr auto value =
        object("key", &T::m_key, "modifiers", &T::m_modifiers, "tag", &T::m_valueTag, "type", &T::m_type,
               "value", custom<&T::fromString, &T::toString>, "active", &T::m_active, "enabled", hide{&T::m_enabled});
};

template <typename T>
using KeybindPtr = std::shared_ptr<GrapeKeybind<T>>;
