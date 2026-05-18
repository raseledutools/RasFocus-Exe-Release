#pragma once

#include <windows.h>
#include <gdiplus.h>

// ── Parent Control State (main.cpp/tab_adult.cpp থেকে extern করে পড়া যাবে) ──
extern bool g_parentLockAllTabs;       // Parent সব tab lock করেছে কিনা
extern bool g_parentForceAdultBlock;   // Parent adult block force করেছে কিনা
extern int  g_parentTimeLimitMinutes;  // Parent কতক্ষণ ব্যবহার allow করেছে (0=unlimited)
extern ULONGLONG g_parentTimeLimitStart; // Time limit শুরুর tick

// ── UI ড্র ──
void DrawFamilyLinkTab(Gdiplus::Graphics& g, float x, float y, float w, float h);

// ── Mouse & Keyboard Events ──
void ProcessFamilyLinkMouseMove(float mx, float my, float cX, float cY);
void ProcessFamilyLinkMouseClick(float mx, float my, float cX, float cY, HWND hWnd);
void ProcessFamilyLinkChar(wchar_t c);
void ProcessFamilyLinkKeyDown(WPARAM wp);

// ── Timer Poll (WM_TIMER থেকে call করতে হবে) ──
void ProcessFamilyLinkTimer(UINT_PTR timerId);
