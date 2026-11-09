#pragma once

#include <safetyhook.hpp>

#include <cstdint>
#include <string_view>

namespace util {

void midhook(uintptr_t address, std::string_view label, void (*callback)(safetyhook::Context&));

}
