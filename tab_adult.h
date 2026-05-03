#pragma once
#include <windows.h>
#include <gdiplus.h>

// main.cpp থেকে কল করার জন্য ফাংশন ডিক্লারেশন
void DrawAdultBlockTab(Gdiplus::Graphics& g, float contentX, float contentY, float contentW, float contentH);
void ProcessAdultMouseMove(float x, float y);
void ProcessAdultMouseClick(float x, float y);