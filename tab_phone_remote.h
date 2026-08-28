#pragma once

#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ws2_32.lib")

#include <string>

// ════════════════════════════════════════════════════════════════
// PHONE REMOTE — RustDesk style PIN system
//
// PC generates a 6-digit PIN.
// Phone enters PIN once → auto-connect next time.
//
// PIN encodes: IP + port (local network only, no relay server)
// Discovery: PC broadcasts UDP beacon on port 9223
//            Phone scans and matches PIN → gets IP:port
// ════════════════════════════════════════════════════════════════

extern bool        g_phoneRemoteRunning;
extern int         g_phoneRemotePort;     // HTTP+WS port, default 9222
extern int         g_phoneRemoteUdpPort;  // UDP beacon port, default 9223
extern int         g_connectedClients;
extern std::string g_phoneRemotePin;      // 6-digit PIN shown on PC

void DrawPhoneRemoteTab   (Gdiplus::Graphics& g, float x, float y, float w, float h);
void ProcessPhoneRemoteMouseMove (float mx, float my, float cX, float cY);
void ProcessPhoneRemoteMouseClick(float mx, float my, float cX, float cY, HWND hWnd);
void PhoneRemoteStartServer();
void PhoneRemoteStopServer();
void PhoneRemoteTimerTick();
