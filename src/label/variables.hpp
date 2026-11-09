#pragma once

#include "rift/rift.hpp"

namespace grape::labels {

void fillStatic(grape::rift::Context& context);

void fillLive(grape::rift::Context& context);

grape::rift::Context& sharedContext();

void invalidateContext();

}
