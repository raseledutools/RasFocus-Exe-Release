#pragma once
#include <windows.h>
#include <gdiplus.h>

void DrawFileManagerTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessFileManagerMouseMove(float x, float y);
void ProcessFileManagerMouseClick(float x, float y, HWND hWnd);
void ProcessFileManagerMouseWheel(float x, float y, int delta);
