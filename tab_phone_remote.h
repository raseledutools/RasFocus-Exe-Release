#pragma once

// ════════════════════════════════════════════════════════════════════
// tab_phone_remote.h  —  RustDesk-style native phone remote
//
// PC is a WebSocket CLIENT that connects to phone's RemoteDesktopService
// (port 9224). Phone streams JPEG frames; PC renders via GDI+.
// No browser. No HTML. Native real-time control.
//
// Inspired by RustDesk open source (MIT License):
//   https://github.com/rustdesk/rustdesk
// ════════════════════════════════════════════════════════════════════

#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Crypt32.lib")

#include <string>
#include <functional>

// ── Connection state ─────────────────────────────────────────────
enum class RdState {
    Disconnected,
    Connecting,
    Connected,
    Error
};

extern RdState      g_rdState;
extern std::string  g_rdPhoneId;      // 9-digit ID shown by phone app
extern std::string  g_rdPhoneIp;      // resolved IP of phone
extern int          g_rdPhonePort;    // phone WS port (default 9224)
extern std::string  g_rdPhoneName;    // phone device name
extern int          g_rdPhoneW;       // phone screen width
extern int          g_rdPhoneH;       // phone screen height
extern std::string  g_rdStatusMsg;    // status shown in UI
extern int          g_rdFps;          // current FPS counter
extern bool         g_rdInputEnabled; // send mouse/key to phone

// ── Public API ────────────────────────────────────────────────────
void RdConnect   (const std::string& ip, int port = 9224);
void RdDisconnect();
void RdTimerTick ();   // call every 100ms from main timer

// ── UI ────────────────────────────────────────────────────────────
void DrawPhoneRemoteTab       (Gdiplus::Graphics& g, float x, float y, float w, float h);
void ProcessPhoneRemoteMouseMove (float mx, float my, float cX, float cY);
void ProcessPhoneRemoteMouseClick(float mx, float my, float cX, float cY, HWND hWnd);
void ProcessPhoneRemoteKey       (WPARAM vk, bool keyDown);

// ── Legacy compat (kept so main.cpp compiles unchanged) ──────────
inline void PhoneRemoteStartServer() {}
inline void PhoneRemoteStopServer () {}
inline void PhoneRemoteTimerTick  () { RdTimerTick(); }

// old globals kept for any code that still references them
extern bool        g_phoneRemoteRunning;
extern int         g_phoneRemotePort;
extern int         g_phoneRemoteUdpPort;
extern int         g_connectedClients;
extern std::string g_phoneRemotePin;
