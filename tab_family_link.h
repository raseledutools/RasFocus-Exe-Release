#pragma once

#include <windows.h>
#include <gdiplus.h>

// UI ড্র করার ফাংশন
void DrawFamilyLinkTab(Gdiplus::Graphics& g, float x, float y, float w, float h);

// মাউস এবং কীবোর্ড ইভেন্ট হ্যান্ডলার
void ProcessFamilyLinkMouseMove(float mx, float my, float cX, float cY);
void ProcessFamilyLinkMouseClick(float mx, float my, float cX, float cY, HWND hWnd);
void ProcessFamilyLinkChar(wchar_t c);
void ProcessFamilyLinkKeyDown(WPARAM wp);
