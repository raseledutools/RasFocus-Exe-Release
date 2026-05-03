#ifndef TAB_STRICT_H
#define TAB_STRICT_H

#include <windows.h>
#include <gdiplus.h>

void DrawStrictProtocolsTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessStrictProtocolsMouseMove(float x, float y);
void ProcessStrictProtocolsMouseClick(float x, float y);

#endif // TAB_STRICT_H