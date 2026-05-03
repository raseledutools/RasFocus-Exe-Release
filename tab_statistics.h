#ifndef TAB_STATISTICS_H
#define TAB_STATISTICS_H

#include <windows.h>
#include <gdiplus.h>

void DrawStatisticsTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessStatisticsMouseMove(float x, float y);
void ProcessStatisticsMouseClick(float x, float y);

#endif // TAB_STATISTICS_H