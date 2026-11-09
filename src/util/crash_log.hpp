#pragma once

#include <string_view>

namespace crash_log {
    void install();
    void breadcrumb(std::string_view message);
}
