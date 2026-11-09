#pragma once

#include <string>
#include <vector>

namespace grape::trace {

void add(std::string line);
std::vector<std::string> recent();
void clear();
bool enabled();

}
