// tab_rasgram.h
// RasGram Desktop — Telegram-style messaging tab (native C++ / Win32 / GDI+)
// Replaces Student Utilities in tab_special.cpp sub-tab bar (index 2)

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <gdiplus.h>
#include <string>

using namespace std;

// ── Initialization (call once when Special tab first loads) ──
void InitRasGramDesktop();

// ── Called when tab becomes visible / hidden ─────────────────
void ShowRasGramControls(bool show);

// ── Main draw function (called from DrawSpecialFeatureTab) ───
void DrawRasGramTab(Gdiplus::Graphics& g,
                    float cx, float cy, float cw, float ch);

// ── Mouse / keyboard input ────────────────────────────────────
void ProcessRasGramMouseMove (float x, float y);
void ProcessRasGramMouseClick(float x, float y);
void ProcessRasGramMouseWheel(int delta);
void ProcessRasGramChar      (wchar_t c);
void ProcessRasGramKeyDown   (WPARAM vk);
