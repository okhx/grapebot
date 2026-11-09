#pragma once

#include <cstdint>

class PlayLayer;

namespace grape::intentional {

bool armed();

bool respawnsFromStart();

void noteDeath();

bool takeOverFullReset(PlayLayer* layer);

bool keepsCheckpoints();

void writeResetEvent(uint64_t frame);

void settle();

}
