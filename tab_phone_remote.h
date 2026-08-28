#pragma once

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ws2_32.lib")

#include <string>

// ════════════════════════════════════════════════════════════════
// PHONE REMOTE — Phone থেকে PC Control (WiFi/Hotspot)
// Features:
//   1. CMD Shell  — phone থেকে command দাও, output দেখো
//   2. File Access — PC ফাইল phone এ দেখো/নামাও
//   3. Screen View — PC screen phone এ live দেখো
//   4. Full Control — mouse + keyboard phone থেকে চালাও
// ════════════════════════════════════════════════════════════════

// Server state
extern bool   g_phoneRemoteRunning;
extern int    g_phoneRemotePort;       // default 9222
extern int    g_connectedClients;

// Public API
void DrawPhoneRemoteTab(Gdiplus::Graphics& g, float x, float y, float w, float h);
void ProcessPhoneRemoteMouseMove (float mx, float my, float cX, float cY);
void ProcessPhoneRemoteMouseClick(float mx, float my, float cX, float cY, HWND hWnd);
void PhoneRemoteStartServer();
void PhoneRemoteStopServer();
void PhoneRemoteTimerTick();          // call every ~500 ms from WM_TIMER
