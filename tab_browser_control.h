#pragma once
// tab_browser_control.h — Browser Control Panel (YouTube/Facebook/Instagram toggles)
// Tab 9 এর content area তে draw হয়; toggle করলে rasfocus_ai_data.txt update + browser refresh।

#include <windows.h>
#include <gdiplus.h>
using namespace Gdiplus;

// Draw the browser control panel into the content area
void DrawBrowserControlTab(Graphics& g, float cX, float cY, float cW, float cH);

// Handle mouse click on the browser control panel
void ProcessBrowserControlMouseClick(float x, float y, float cX, float cY, float cW, float cH);

// Handle mouse move (hover) — call this and InvalidateRect if it returns true
bool ProcessBrowserControlMouseMove(float x, float y, float cX, float cY, float cW, float cH);

// Load current state from rasfocus_ai_data.txt (call once at startup)
void BrowserControlLoadState();

// Refresh all open RasBrowser windows after a toggle change
void BrowserControlRefreshBrowserWindows();
