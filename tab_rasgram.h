// tab_rasgram.h
// RasGram Desktop — Telegram-style messaging tab (native C++ / Win32 / GDI+)
// Replaces Student Utilities in tab_special.cpp sub-tab bar (index 2)

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <objbase.h>
#include <propidl.h>
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

// ── WndProc message hook (call from main WndProc for these WM_USER ids)
// Returns true if the message was handled.
bool RgHandleParentWndMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

// ── Shutdown helpers (call from WM_DESTROY) ──────────────────
void RgNotify_Destroy();
void RgNet_StopIncomingCallPolling();

// WM_USER message IDs shared between tab_rasgram and main WndProc
#define WM_RG_INCOMING_CALL (WM_USER + 70)
#define WM_RG_CALL_ENDED    (WM_USER + 71)
#define WM_RG_VIDEO_FRAME   (WM_USER + 72)
#define WM_RG_NEW_MESSAGE   (WM_USER + 73)
// Login completion — posted from background thread so UI thread runs RgExecJS
#define WM_RG_LOGIN_OK      (WM_USER + 74)
#define WM_RG_LOGIN_ERR     (WM_USER + 75)

