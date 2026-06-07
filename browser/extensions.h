// extensions.h — Stub header for browser extension panel functionality
// Auto-generated: functions declared here are implemented elsewhere or as no-ops

#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <WebView2.h>

// Global state
// Note: 'inline bool' requires C++17; using static to maintain C++14 compatibility.
// Each translation unit that includes this header gets its own copy (stub usage only).
static bool g_extensionPanelOpen = false;

// Draw the extensions panel
inline void DrawExtensionPanel(
    Gdiplus::Graphics& g,
    int W, int H,
    int titleBarH, int toolbarH,
    bool isDarkMode, int dpi,
    int mouseX, int mouseY)
{
    // Stub — no-op until extensions.cpp is implemented
}

// Handle click inside extension panel; returns action string
inline std::wstring HandleExtensionPanelClick(
    int x, int y, int W, int H,
    int titleBarH, int toolbarH, int dpi)
{
    return L""; // Stub
}

// Install a Chrome-format unpacked extension from a folder
inline void InstallExtensionFromFolder(
    ICoreWebView2Environment* env,
    ICoreWebView2Controller*  ctl,
    const wchar_t*            folderPath)
{
    // Stub
}

// Toggle (enable/disable) extension at index
inline void ToggleExtension(ICoreWebView2Controller* ctl, int index)
{
    // Stub
}

// Remove extension at index
inline void UninstallExtension(int index)
{
    // Stub
}

// Scan the extensions folder and refresh the list
inline void ScanExtensionsFolderPublic()
{
    // Stub
}