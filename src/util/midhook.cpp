#include "midhook.hpp"

#include <Geode/Geode.hpp>

#include <vector>

namespace util {

void midhook(uintptr_t address, std::string_view label, void (*callback)(safetyhook::Context&)) {
    static std::vector<SafetyHookMid> installed;
    installed.push_back(safetyhook::create_mid(address, callback));
    geode::log::info("Mid-hook {} installed at 0x{:x}", label, address);
}

}
