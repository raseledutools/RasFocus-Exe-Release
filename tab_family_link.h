#pragma once

// ── Winsock2 MUST come before windows.h ──────────────────────────────
// _WINSOCKAPI_ blocks the old winsock.h that windows.h would auto-pull in.
// Without this every .cpp that includes this header gets hundreds of
// "sockaddr/WSAData/etc redefinition" errors.
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

// ── Windows core (after winsock2) ────────────────────────────────────
#include <windows.h>

// ── COM / OLE (IStream, PROPID, IUnknown ...) — needed by GDI+ ───────
// objbase.h brings in objidl.h which defines IStream.
// propidl.h defines PROPID / PROPVARIANT used by GDI+ property methods.
#include <objbase.h>
#include <propidl.h>

// ── GDI+ (must come AFTER IStream / PROPID are defined) ──────────────
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <string>

// ════════════════════════════════════════════════════════════════════
// FAMILY LINK — Parent Control State
// এই globals গুলো tab_adult.cpp, tab_device_block.cpp, tab_blocks.cpp
// সহ যেকোনো জায়গা থেকে extern করে read করা যাবে।
// Parent Firebase থেকে command পাঠালে এগুলো instantly update হবে।
// ════════════════════════════════════════════════════════════════════

// ── Connection State ──
extern bool g_isLinkedToParent;         // Parent এর সাথে linked কিনা
extern std::string g_parentUid;         // Linked parent এর UID

// ── Tab / UI Lock ──
extern bool g_parentLockAllTabs;        // সব tab lock — child কিছু দেখতে পাবে না

// ── Adult & Content Block ──
extern bool g_parentForceAdultBlock;    // Adult block force ON করেছে
extern bool g_parentForceReelsBlock;    // FB Reels block force ON
extern bool g_parentForceShortsBlock;   // YT Shorts block force ON

// ── App Control ──
extern bool g_parentAppControlEnabled; // App control চালু কিনা
// g_parentAppMode: "ALLOW" = শুধু allowed apps চলবে, "BLOCK" = blocked apps বন্ধ
extern std::string g_parentAppMode;
// Parent এর set করা app lists (comma separated, e.g. "chrome.exe,vlc.exe")
extern std::string g_parentAllowedAppsCSV;
extern std::string g_parentBlockedAppsCSV;

// ── Website Block ──
extern bool g_parentWebBlockEnabled;   // Website block চালু কিনা
extern std::string g_parentBlockedWebsCSV; // block করা websites (comma separated)

// ── System Lock ──
extern bool g_parentBlockTaskManager;  // Task Manager block
extern bool g_parentBlockSettings;     // Windows Settings block
extern bool g_parentBlockFileManager;  // File Explorer block
extern std::string g_parentBlockedFoldersCSV; // block করা specific folders

// ── Internet & Power ──
extern bool g_parentInternetFasting;   // Internet fasting চালু
extern int  g_parentPowerAction;       // 0=None, 1=Lock PC, 2=Sleep, 3=Shutdown

// ── Screen Time ──
extern int  g_parentTimeLimitMinutes;  // 0 = unlimited
extern ULONGLONG g_parentTimeLimitStart;

// ════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ════════════════════════════════════════════════════════════════════

// ── UI Draw ──
void DrawFamilyLinkTab(Gdiplus::Graphics& g, float x, float y, float w, float h);

// ── Mouse & Keyboard Events ──
void ProcessFamilyLinkMouseMove(float mx, float my, float cX, float cY);
void ProcessFamilyLinkMouseClick(float mx, float my, float cX, float cY, HWND hWnd);
void ProcessFamilyLinkChar(wchar_t c);
void ProcessFamilyLinkKeyDown(WPARAM wp);

// ── Timer (WM_TIMER থেকে call করতে হবে) ──
void ProcessFamilyLinkTimer(UINT_PTR timerId, HWND hWnd);

// ── Enforcement (main timer loop থেকে call করতে হবে) ──
// প্রতি 1 সেকেন্ডে একবার call করো — parent commands enforce করবে
void FamilyLink_EnforceParentCommands(HWND hWnd);