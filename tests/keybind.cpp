#include "shared/value/keybind.hpp"

#include <string>

#define CHECK(condition) \
    do { if (!(condition)) return __LINE__; } while (false)

int main() {
    using BoolKeybind = GrapeKeybind<bool>;

    CHECK(grape::keybinds::FRAME_ADVANCE_KEY == 'C');
    CHECK(grape::keybinds::ADVANCE_ONE_KEY == 'V');
    CHECK(grape::keybinds::modifierMask(true, false, true) ==
           (grape::keybinds::CTRL | grape::keybinds::ALT));
    CHECK(grape::keybinds::normalizeModifiers(
               0x12, grape::keybinds::SHIFT | grape::keybinds::ALT) ==
           grape::keybinds::SHIFT);

    RawKeybind legacyAlt{0x12, grape::keybinds::SHIFT, "ui.visible",
                         KeybindType::Toggle, true, "true"};
    grape::keybinds::migrateLegacy(legacyAlt);
    CHECK(legacyAlt.m_modifiers == 0);

    KeyPressTracker presses;
    int pressedHash = 'K' | (grape::keybinds::CTRL << 20);
    CHECK(presses.resolve('K', true, pressedHash));
    int repeatedHash = 'K';
    CHECK(!presses.resolve('K', true, repeatedHash));
    int releasedHash = 'K';
    CHECK(presses.resolve('K', false, releasedHash));
    CHECK(releasedHash == pressedHash);
    const auto shift = BoolKeybind::create(
        "test", 65, KeybindType::Override, true,
        BoolKeybind::MODIFIER_SHIFT);
    const auto control = BoolKeybind::create(
        "test", 65, KeybindType::Override, true,
        BoolKeybind::MODIFIER_CTRL);
    const auto alt = BoolKeybind::create(
        "test", 65, KeybindType::Override, true,
        BoolKeybind::MODIFIER_ALT);
    CHECK(shift->getHash() != control->getHash());
    CHECK(control->getHash() != alt->getHash());

    bool value = false;
    bool previous = false;
    CHECK(!shift->applyValue(false, &value, &previous));
    CHECK(!value);
    CHECK(shift->applyValue(true, &value, &previous));
    CHECK(value);
    CHECK(!shift->applyValue(false, &value, &previous));

    value = false;
    previous = false;
    const auto hold = BoolKeybind::create(
        "test", 66, KeybindType::Hold, true,
        BoolKeybind::MODIFIER_CTRL);
    CHECK(hold->applyValue(true, &value, &previous));
    CHECK(value);
    CHECK(!hold->applyValue(true, &value, &previous));
    CHECK(hold->applyValue(false, &value, &previous));
    CHECK(!value);
    CHECK(!hold->applyValue(false, &value, &previous));

    const auto firstHold = BoolKeybind::create(
        "test", 67, KeybindType::Hold, true);
    const auto secondHold = BoolKeybind::create(
        "test", 68, KeybindType::Hold, true);
    CHECK(firstHold->applyValue(true, &value, &previous));
    CHECK(!secondHold->applyValue(true, &value, &previous));
    CHECK(!firstHold->applyValue(false, &value, &previous));
    CHECK(value);
    CHECK(secondHold->applyValue(false, &value, &previous));
    CHECK(!value);

    int number = 0;
    int previousNumber = 0;
    const auto holdOne = GrapeKeybind<int>::create(
        "number", 69, KeybindType::Hold, 1);
    const auto holdTwo = GrapeKeybind<int>::create(
        "number", 70, KeybindType::Hold, 2);
    CHECK(holdOne->applyValue(true, &number, &previousNumber));
    CHECK(holdTwo->applyValue(true, &number, &previousNumber));
    CHECK(number == 2);
    CHECK(holdTwo->applyValue(false, &number, &previousNumber));
    CHECK(number == 1);
    CHECK(holdOne->applyValue(false, &number, &previousNumber));
    CHECK(number == 0);

    auto text = GrapeKeybind<std::string>::createFromString(
        "text", 66, KeybindType::Override, "hello world");
    CHECK(text->toString() == "hello world");

    value = false;
    const auto inactive = BoolKeybind::createFromString(
        "test", 67, KeybindType::Override, "1", 0, false);
    CHECK(!inactive->applyValue(true, &value, &previous));
    CHECK(!value);
}
