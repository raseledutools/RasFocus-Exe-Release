#pragma once
#include <windows.h>
#include <gdiplus.h>

// ড্যাশবোর্ডের জন্য প্রয়োজনীয় ফাংশনগুলোর ডিক্লেয়ারেশন
void DrawDashboardTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessDashboardMouseMove(float x, float y);
void ProcessDashboardMouseClick(float x, float y, int& selectedTab);