#pragma once

namespace grape::frame {

void markDrawScene();
void afterSwap();
bool drawSceneHooked();
bool holdForCapture();
bool presentDue();
bool beforeSwap();

}
