#ifndef TAB_BLOCKS_H
#define TAB_BLOCKS_H

#include <windows.h>
#include <gdiplus.h>

void DrawBlocksTab(Gdiplus::Graphics& g, float contentX, float contentY, float contentW, float contentH);
void ProcessBlocksMouseMove(float x, float y);
void ProcessBlocksMouseClick(float x, float y);

#endif // TAB_BLOCKS_H