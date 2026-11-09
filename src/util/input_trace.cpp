#include "input_trace.hpp"

#include <Geode/Geode.hpp>

#include <deque>

namespace grape::trace {

namespace {
constexpr std::size_t KEEP = 400;
std::deque<std::string> g_lines;
}

void add(std::string line) {
#ifdef GEODE_IS_MOBILE
    geode::log::info("{}", line);
    g_lines.push_back(std::move(line));
    while (g_lines.size() > KEEP) g_lines.pop_front();
#else
    (void)line;
#endif
}

std::vector<std::string> recent() { return {g_lines.begin(), g_lines.end()}; }

void clear() { g_lines.clear(); }

bool enabled() {
#ifdef GEODE_IS_MOBILE
    return true;
#else
    return false;
#endif
}

}
