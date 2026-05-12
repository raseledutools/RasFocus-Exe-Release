#pragma once
#include <windows.h>
#include <gdiplus.h>

// --- Device Block Tab Functions ---
void DrawDeviceBlockTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessDeviceBlockMouseMove(float x, float y);
void ProcessDeviceBlockMouseClick(float x, float y);
