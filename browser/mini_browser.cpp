// mini_browser.cpp — RasBrowser | Premium UI, Smart Omnibox, Dynamic Bookmarks
// REFACTORED: Per-Monitor v2 DPI, Chrome bezier tabs, double-buffering,
//             Smart Google Search, Dark Mode Toggle, App Branding.
// ADDED: Google Login Bypass, Pure Popup Mode, AI Filter Integration, Chrome 3-Dot Menu.
// FIXED: mY undeclared identifier, C4267 size_t conversions, C4244 type conversions,
//        C4996 deprecated functions, wchar_t->char string construction warnings.

#define _CRT_SECURE_NO_WARNINGS
#define WINVER       0x0A00
#define _WIN32_WINNT 0x0A00
#define GDIPVER      0x0110

#include "mini_browser.h"
#include "html_tools.h"
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include "bookmarks.h"
#include "settings.h"
#include "find_in_page.h"
#include "context_menu.h"
#include "history_panel.h"
#include "downloads_panel.h"
#include "extensions.h"
#include "advanced_feature.h"   // SetupAdvancedFeatures, SaveToHistory, g_downloads
#include "feature_browser.h"    // DrawFeatureBrowser

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <wrl.h>
#include <shlobj.h>   
#include <shlwapi.h>  

#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <functional>
#include <cassert>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace Microsoft::WRL;
using namespace Gdiplus;

// ─────────────────────────────────────────────────────────────────────────────
// EXTERNAL GLOBALS
// ─────────────────────────────────────────────────────────────────────────────
extern bool  g_isPureViewerMode;
extern float g_scaleFactor;

// ─────────────────────────────────────────────────────────────────────────────
// RESOURCE IDs
// ─────────────────────────────────────────────────────────────────────────────
#define IDI_APP_ICON    101
#define IDC_ADDRESS_BAR 1005

// ─────────────────────────────────────────────────────────────────────────────
// 1. DYNAMIC LOCAL NTP (APP THEMED NEW TAB PAGE)
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetLocalNTP_HTML(bool isDark) {
    std::wstring bg      = isDark ? L"#1e1e24" : L"#f8fafc";
    std::wstring text    = isDark ? L"#ffffff" : L"#323232";
    std::wstring subText = isDark ? L"#a0a0b0" : L"#666666";
    std::wstring boxBg   = isDark ? L"#2b2b36" : L"#ffffff";
    std::wstring boxBorder = isDark ? L"#444444" : L"#dcdfe6";
    std::wstring shadow  = isDark ? L"0 4px 12px rgba(0,0,0,0.3)" : L"0 4px 12px rgba(0,0,0,0.05)";
    std::wstring teal    = L"#0ca8b0"; 

    return L"<!DOCTYPE html>"
    L"<html><head><meta charset='utf-8'><title>New Tab</title><style>"
    L"body { margin:0; display:flex; flex-direction:column; align-items:center; justify-content:center; height:100vh; background:" + bg + L"; font-family:'Segoe UI',sans-serif; color:" + text + L"; }"
    L".logo { font-size:64px; font-weight:bold; margin-bottom:5px; letter-spacing:-1.5px; user-select:none; }"
    L".logo span { color:" + teal + L"; }"
    L".subtitle { font-size:15px; color:" + subText + L"; margin-bottom:35px; font-weight:500; letter-spacing:1px; text-transform:uppercase; }"
    L"form { width:100%; max-width:620px; position:relative; margin-bottom:50px; }"
    L".search-box { width:100%; padding:18px 50px; font-size:16px; border-radius:30px; border:1px solid " + boxBorder + L"; background:" + boxBg + L"; color:" + text + L"; outline:none; box-shadow:" + shadow + L"; box-sizing:border-box; transition:all 0.3s ease; }"
    L".search-box:focus { border-color:" + teal + L"; box-shadow:0 4px 20px rgba(12,168,176,0.25); }"
    L".icon-search { position:absolute; left:20px; top:50%; transform:translateY(-50%); width:22px; fill:#9aa0a6; }"
    L".quick-links { display:flex; gap:30px; }"
    L".link-item { display:flex; flex-direction:column; align-items:center; text-decoration:none; color:" + text + L"; font-size:14px; font-weight:600; transition:transform 0.2s; }"
    L".link-item:hover { transform:translateY(-5px); }"
    L".link-icon { width:56px; height:56px; border-radius:50%; background:" + boxBg + L"; display:flex; align-items:center; justify-content:center; box-shadow:" + shadow + L"; margin-bottom:12px; font-size:22px; color:" + teal + L"; border:1px solid " + boxBorder + L"; transition:all 0.3s ease; }"
    L".link-item:hover .link-icon { background:" + teal + L"; color:#fff; border-color:" + teal + L"; box-shadow:0 8px 15px rgba(12,168,176,0.3); }"
    L"</style></head><body>"
    L"<div class='logo'><span>Ras</span>Browser</div>"
    L"<div class='subtitle'>A Powerful &amp; Safe Browsing Experience</div>"
    L"<form action='https://www.google.com/search' method='GET'>"
    L"<svg class='icon-search' viewBox='0 0 24 24'><path d='M15.5 14h-.79l-.28-.27A6.471 6.471 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z'></path></svg>"
    L"<input type='text' name='q' class='search-box' placeholder='Search securely or type a URL' autocomplete='off' autofocus />"
    L"</form>"
    L"<div class='quick-links'>"
    L"<a href='https://www.youtube.com' class='link-item'><div class='link-icon'>&#9654;</div>YouTube</a>"
    L"<a href='https://www.facebook.com' class='link-item'><div class='link-icon'>f</div>Facebook</a>"
    L"<a href='https://chatgpt.com' class='link-item'><div class='link-icon'>AI</div>ChatGPT</a>"
    L"<a href='https://github.com' class='link-item'><div class='link-icon'>&lt;/&gt;</div>GitHub</a>"
    L"<a href='https://www.wikipedia.org' class='link-item'><div class='link-icon'>W</div>Wikipedia</a>"
    L"</div>"
    L"</body></html>";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. DYNAMIC BLOCKED PAGE (MOTIVATIONAL QUOTES)
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetBlocked_HTML(bool isDark) {
    std::wstring bg     = isDark ? L"#1a1a1f" : L"#f4f6f8";
    std::wstring text   = isDark ? L"#ffffff" : L"#323232";
    std::wstring boxBg  = isDark ? L"#2b2b36" : L"#ffffff";
    std::wstring red    = L"#e74c3c";
    std::wstring border = isDark ? L"#444444" : L"#e2e8f0";

    return L"<!DOCTYPE html>"
    L"<html><head><meta charset='utf-8'><title>Blocked by RasFocus</title><style>"
    L"body { margin:0; display:flex; align-items:center; justify-content:center; height:100vh; background:" + bg + L"; font-family:'Segoe UI',sans-serif; color:" + text + L"; }"
    L".container { max-width:600px; text-align:center; padding:40px; background:" + boxBg + L"; border-radius:16px; box-shadow:0 10px 30px rgba(0,0,0,0.15); border:1px solid " + border + L"; }"
    L".icon { font-size:70px; margin-bottom:10px; color:" + red + L"; }"
    L"h1 { margin:0 0 10px 0; color:" + red + L"; font-size:32px; }"
    L"p { font-size:16px; color:#888; margin-bottom:30px; line-height:1.5; }"
    L".quote-box { background:rgba(12,168,176,0.1); border-left:4px solid #0ca8b0; padding:20px; border-radius:0 8px 8px 0; text-align:left; }"
    L".quote-title { font-size:14px; font-weight:bold; color:#0ca8b0; margin-bottom:10px; text-transform:uppercase; letter-spacing:1px; }"
    L".quote-text { font-size:18px; font-style:italic; line-height:1.6; font-weight:500; }"
    L"</style></head><body>"
    L"<div class='container'>"
    L"<div class='icon'>&#x1F6AB;</div>"
    L"<h1>Access Denied</h1>"
    L"<p>This content has been blocked by <b>RasFocus</b> to protect your mind and productivity. Stay strong and keep your focus on what truly matters.</p>"
    L"<div class='quote-box'>"
    L"<div class='quote-title'>&#x1F4A1; \u09AE\u09A8\u09C0\u09B7\u09C0\u09A6\u09C7\u09B0 \u09C1\u0995\u09CD\u09A4\u09BF</div>"
    L"<div class='quote-text' id='quote'>\"\u09AF\u09C7 \u09AC\u09CD\u09AF\u0995\u09CD\u09A4\u09BF \u09A8\u09BF\u099C\u09C7\u09B0 \u09AA\u09CD\u09B0\u09AC\u09C3\u09A4\u09CD\u09A4\u09BF \u0993 \u0987\u099A\u09CD\u099B\u09BE\u0995\u09C7 \u09A8\u09BF\u09AF\u09BC\u09A8\u09CD\u09A4\u09CD\u09B0\u09A3 \u0995\u09B0\u09A4\u09C7 \u09AA\u09BE\u09B0\u09C7, \u09B8\u09C7\u0987 \u09B9\u09B2\u09CB \u09B8\u09A4\u09CD\u09AF\u09BF\u0995\u09BE\u09B0\u09C7\u09B0 \u09AC\u09BF\u099C\u09AF\u09BC\u09C0\u0964\"</div>"
    L"</div></div>"
    L"<script>"
    L"const quotes = ["
    L"  '\"\\u099A\u09B0\u09BF\u09A4\u09CD\u09B0\u09B9\u09C0\u09A8\u09A4\u09BE\u09B0 \u099A\u09C7\u09AF\u09BC\u09C7 \u09AC\u09DC \u0995\u09CB\u09A8\u09CB \u09A6\u09BE\u09B0\u09BF\u09A6\u09CD\u09B0\u09CD\u09AF \u09A8\u09C7\u0987\u0964\" - \u09B9\u09AF\u09B0\u09A4 \u0986\u09B2\u09C0 (\u09B0\u09BE\u0983)',"
    L"  '\"\\u09AF\u09C7 \u09A8\u09BF\u099C\u09C7\u0995\u09C7 \u09A8\u09BF\u09AF\u09BC\u09A8\u09CD\u09A4\u09CD\u09B0\u09A3 \u0995\u09B0\u09A4\u09C7 \u09AA\u09BE\u09B0\u09C7 \u09A8\u09BE, \u09B8\u09C7 \u0985\u09A8\u09CD\u09AF\u0995\u09C7\u0993 \u09A8\u09BF\u09AF\u09BC\u09A8\u09CD\u09A4\u09CD\u09B0\u09A3 \u0995\u09B0\u09A4\u09C7 \u09AA\u09BE\u09B0\u09C7 \u09A8\u09BE\u0964\" - \u09B8\u09C7\u09A8\u09C7\u0995\u09BE',"
    L"  '\"\\u0995\u09CD\u09B7\u09A3\u09BF\u0995\u09C7\u09B0 \u0986\u09A8\u09A8\u09CD\u09A6\u09C7\u09B0 \u099C\u09A8\u09CD\u09AF \u09A8\u09BF\u099C\u09C7\u09B0 \u09AD\u09AC\u09BF\u09B7\u09CD\u09AF\u09CE \u09A8\u09B7\u09CD\u099F \u0995\u09B0\u09CB\u09A8\u09BE\u0964\" - \u09B8\u0982\u0997\u09C3\u09B9\u09C0\u09A4',"
    L"  '\"\\u09AE\u09A8\u0995\u09C7 \u09AF\u09A6\u09BF \u09A4\u09C1\u09AE\u09BF \u09A8\u09BF\u09AF\u09BC\u09A8\u09CD\u09A4\u09CD\u09B0\u09A3 \u09A8\u09BE \u0995\u09B0\u09CB, \u09A4\u09AC\u09C7 \u09AE\u09A8 \u09A4\u09CB\u09AE\u09BE\u0995\u09C7 \u09A8\u09BF\u09AF\u09BC\u09A8\u09CD\u09A4\u09CD\u09B0\u09A3 \u0995\u09B0\u09AC\u09C7\u0964\" - \u09B9\u09CB\u09B0\u09C7\u09B8',"
    L"  '\"\\u09B6\u09C1\u09A6\u09CD\u09A7 \u09AE\u09A8 \u098F\u09AC\u0982 \u09B8\u09C1\u09A8\u09CD\u09A6\u09B0 \u099A\u09B0\u09BF\u09A4\u09CD\u09B0\u0987 \u09B9\u09B2\u09CB \u09AE\u09BE\u09A8\u09C1\u09B7\u09C7\u09B0 \u0986\u09B8\u09B2 \u09B8\u09CC\u09A8\u09CD\u09A6\u09B0\u09CD\u09AF\u0964\" - \u09B8\u09CD\u09AC\u09BE\u09AE\u09C0 \u09AC\u09BF\u09AC\u09C7\u0995\u09BE\u09A8\u09A8\u09CD\u09A6'"
    L"];"
    L"document.getElementById('quote').innerText = quotes[Math.floor(Math.random() * quotes.length)];"
    L"</script></body></html>";
}

// ─────────────────────────────────────────────────────────────────────────────
// 🟢 AI IN-APP BLOCKING INJECTION SCRIPT GENERATOR
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetAiInjectScript(const std::wstring& currentUrl) {
    std::wifstream in(L"rasfocus_ai_data.txt");
    if (!in.is_open()) return L"";

    bool isAiEngineActive = false, cbAiImageBlur = false, cbFemaleDetectWeb = false, cbFemaleDetectVideo = false;
    int aiSensitivityIdx = 0;
    bool ytHideHome = false, ytHideShorts = false, ytHideComments = false, ytHideRecVideos = false;
    bool ytHideThumbnails = false, ytBlurThumbnails = false, ytHideSubs = false, ytHideExplore = false;
    bool ytHideTopBar = false, ytDisableEndCards = false, ytBlackWhiteMode = false, ytDisableAutoplay = false;
    bool ttHideExplore = false, ttHideLive = false, ttHideComments = false, ttHideSearch = false, ttBlackWhiteMode = false;
    bool igHideStories = false, igHideReels = false, igHideExplore = false, igHideComments = false;
    bool igHideSuggested = false, igBlackWhiteMode = false;

    in >> isAiEngineActive >> cbAiImageBlur >> cbFemaleDetectWeb >> cbFemaleDetectVideo >> aiSensitivityIdx;
    in >> ytHideHome >> ytHideShorts >> ytHideComments >> ytHideRecVideos >> ytHideThumbnails
       >> ytBlurThumbnails >> ytHideSubs >> ytHideExplore >> ytHideTopBar >> ytDisableEndCards
       >> ytBlackWhiteMode >> ytDisableAutoplay;
    in >> ttHideExplore >> ttHideLive >> ttHideComments >> ttHideSearch >> ttBlackWhiteMode;
    in >> igHideStories >> igHideReels >> igHideExplore >> igHideComments >> igHideSuggested >> igBlackWhiteMode;
    in.close();

    std::wstring css = L"";

    if (currentUrl.find(L"youtube.com") != std::wstring::npos) {
        if (ytHideHome)        css += L"ytd-browse[page-subtype='home'] { display: none !important; } ";
        if (ytHideShorts)      css += L"ytd-reel-shelf-renderer, ytd-rich-shelf-renderer[is-shorts], a[title='Shorts'], ytd-mini-guide-entry-renderer[aria-label='Shorts'] { display: none !important; } ";
        if (ytHideComments)    css += L"ytd-comments { display: none !important; } ";
        if (ytHideRecVideos)   css += L"ytd-watch-next-secondary-results-renderer { display: none !important; } ";
        if (ytHideThumbnails)  css += L"ytd-thumbnail { display: none !important; } ";
        if (ytBlurThumbnails)  css += L"ytd-thumbnail img { filter: blur(15px) !important; } ";
        if (ytHideSubs)        css += L"a[title='Subscriptions'], ytd-mini-guide-entry-renderer[aria-label='Subscriptions'] { display: none !important; } ";
        if (ytHideExplore)     css += L"ytd-guide-section-renderer:nth-child(3) { display: none !important; } ";
        if (ytHideTopBar)      css += L"ytd-masthead { display: none !important; } #page-manager { margin-top: 0 !important; } ";
        if (ytDisableEndCards) css += L".ytp-ce-element { display: none !important; } ";
        if (ytBlackWhiteMode)  css += L"html { filter: grayscale(100%) !important; } ";
    } 
    else if (currentUrl.find(L"tiktok.com") != std::wstring::npos) {
        if (ttHideExplore)  css += L"[data-e2e='nav-explore'] { display: none !important; } ";
        if (ttHideLive)     css += L"[data-e2e='nav-live'] { display: none !important; } ";
        if (ttHideComments) css += L".comment-container, [data-e2e='comment-icon'] { display: none !important; } ";
        if (ttBlackWhiteMode) css += L"html { filter: grayscale(100%) !important; } ";
    } 
    else if (currentUrl.find(L"instagram.com") != std::wstring::npos) {
        if (igHideReels)    css += L"a[href*='/reels/'] { display: none !important; } ";
        if (igHideExplore)  css += L"a[href*='/explore/'] { display: none !important; } ";
        if (igBlackWhiteMode) css += L"html { filter: grayscale(100%) !important; } ";
    }

    if (css.empty()) return L"";

    std::wstring js = L"let style = document.createElement('style'); style.innerHTML = \"" + css + L"\"; document.head.appendChild(style);";
    return js;
}

// ─────────────────────────────────────────────────────────────────────────────
// DPI HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static inline int S(int px, UINT dpi) { return MulDiv(px, (int)dpi, 96); }
static inline float Sf(float px, UINT dpi) { return px * (float)dpi / 96.0f; }

static UINT GetWndDpi(HWND hWnd) {
    UINT dpi = GetDpiForWindow(hWnd);
    return dpi ? dpi : 96;
}

// ─────────────────────────────────────────────────────────────────────────────
// LAYOUT CONSTANTS
// ─────────────────────────────────────────────────────────────────────────────
static const int D_TITLEBAR_H  = 42; 
static const int D_TOOLBAR_H   = 40;
static const int D_BOOKMARK_H  = 32; 

static const int D_TAB_W_MAX   = 240;
static const int D_TAB_W_MIN   = 80;
static const int D_TAB_PAD     = 10;
static const int D_WIN_BTN_W   = 46;
static const int D_LOGO_W      = 140; 
static const int D_NEW_TAB_BTN = 28;

// ─────────────────────────────────────────────────────────────────────────────
// PER-TAB DATA
// ─────────────────────────────────────────────────────────────────────────────
struct TabData {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2>           webview;
    std::wstring title   = L"New Tab";
    std::wstring url     = L"LOCAL_NTP"; 
    bool         loading = false;
    bool         canBack = false;
    bool         canFwd  = false;
    // Favicon: GDI+ Bitmap (fetched via JavaScript inject)
    std::shared_ptr<Bitmap> favicon;
    // Loading spinner frame counter (0-7, incremented via timer)
    int          loadingFrame = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// PER-WINDOW DATA
// ─────────────────────────────────────────────────────────────────────────────
struct BrowserWindowData {
    std::vector<TabData> tabs;
    int                  activeTab    = 0;
    bool                 isFullScreen = false;
    bool                 isDarkMode   = true; 
    WINDOWPLACEMENT      wpPrev       = { sizeof(WINDOWPLACEMENT) };
    HWND                 hAddressBar  = NULL;
    HFONT                hAddrFont    = NULL;

    bool hMin = false, hMax = false, hClose = false;
    bool hPin = false, hDark = false, hFocus = false;
    bool isPinned = false;
    bool isFocusMode = false; // header+tab লুকানো "native app" mode
    bool hBack = false, hFwd = false, hRel = false;
    
    // Right Icons
    bool hProfile = false, hExt = false, hMenu = false; 
    
    // 🟢 Menu State Tracking
    bool isMenuOpen    = false;
    int  hoverMenuIdx  = -1;

    int  hoverTabIndex = -1;
    bool hNewTab       = false;

    TabData* active() {
        if (activeTab >= 0 && activeTab < (int)tabs.size()) return &tabs[activeTab];
        return nullptr;
    }
};

static std::map<HWND, BrowserWindowData> g_windows;
ComPtr<ICoreWebView2Environment>  g_sharedEnv;

// ─────────────────────────────────────────────────────────────────────────────
// FIX: Helper to compute menu Y position consistently across all handlers
// This was the root cause of the 'mY undeclared identifier' errors at
// mini_browser.cpp(1461) and mini_browser.cpp(1477).
// ─────────────────────────────────────────────────────────────────────────────
static float GetMenuY(HWND hWnd, UINT dpi) {
    return (float)(S(D_TITLEBAR_H, dpi) + S(D_TOOLBAR_H, dpi) - S(5, dpi));
}

// Dynamic Total Header Height Calculation
static int NavTotalH(HWND hWnd) {
    UINT dpi = GetWndDpi(hWnd);
    if (g_isPureViewerMode) return S(D_TITLEBAR_H, dpi); 

    int h = S(D_TITLEBAR_H + D_TOOLBAR_H, dpi);
    if (g_windows.count(hWnd)) {
        auto* tab = g_windows[hWnd].active();
        if (tab && (tab->url == L"LOCAL_NTP" ||
                    tab->url.find(L"blocked by rasfocus") != std::wstring::npos ||
                    tab->url == L"about:blank")) 
            h += S(D_BOOKMARK_H, dpi);
    }
    return h;
}

static int TitleBarH(UINT dpi) { return S(D_TITLEBAR_H, dpi); }
static int ToolbarH (UINT dpi) { return S(D_TOOLBAR_H,  dpi); }
static int WinBtnW  (UINT dpi) { return S(D_WIN_BTN_W,  dpi); }
static int LogoW    (UINT dpi) { return S(D_LOGO_W,     dpi); } 

// ─────────────────────────────────────────────────────────────────────────────
// URL ENCODER
// ─────────────────────────────────────────────────────────────────────────────
static std::string utf8_encode(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

static std::wstring UrlEncode(const std::wstring& text) {
    std::string utf8 = utf8_encode(text);
    std::wstringstream escaped;
    for (unsigned char c : utf8) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << (wchar_t)c;
        } else if (c == ' ') {
            escaped << L"+";
        } else {
            wchar_t buf[10];
            swprintf(buf, 10, L"%%%02X", c);
            escaped << buf;
        }
    }
    return escaped.str();
}

static void CreateDesktopShortcut() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    wchar_t desktopPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
        std::wstring linkPath = std::wstring(desktopPath) + L"\\RasBrowser.lnk";
        if (GetFileAttributesW(linkPath.c_str()) != INVALID_FILE_ATTRIBUTES) return;

        CoInitialize(NULL);
        IShellLinkW* psl;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl))) {
            psl->SetPath(exePath);
            psl->SetArguments(L"-minibrowser");
            psl->SetIconLocation(exePath, 0); 
            IPersistFile* ppf;
            if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf))) {
                ppf->Save(linkPath.c_str(), TRUE);
                ppf->Release();
            }
            psl->Release();
        }
        CoUninitialize();
    }
}

static void RegisterAppForDefaultBrowser() {}

bool IsBlockedContent(const std::wstring& text) {
    std::wstring lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    static const std::vector<std::wstring> kBadWords = {
        L"porn", L"xxx", L"sex", L"nude", L"nsfw", L"sexy", L"hentai", L"rule34",
        L"milf", L"blowjob", L"tits", L"boobs", L"pussy", L"dick", L"cock",
        L"escort", L"bdsm", L"fetish", L"erotica", L"dildo", L"webcam",
        L"camgirls", L"xvideos", L"pornhub", L"xnxx", L"xhamster", L"brazzers",
        L"onlyfans", L"playboy", L"chaturbate", L"stripchat", L"eporner"
    };
    for (const auto& kw : kBadWords)
        if (lower.find(kw) != std::wstring::npos) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// GEOMETRY HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static int CalcTabWidth(int windowW, int tabCount, UINT dpi) {
    int winBtnArea = WinBtnW(dpi) * 6; 
    int available  = windowW - winBtnArea - LogoW(dpi) - S(D_NEW_TAB_BTN + 16, dpi);
    int w = (tabCount > 0) ? available / tabCount : S(D_TAB_W_MAX, dpi);
    return max(S(D_TAB_W_MIN, dpi), min(S(D_TAB_W_MAX, dpi), w));
}

static RECT GetTabRect(int windowW, int index, int tabCount, UINT dpi) {
    int tw = CalcTabWidth(windowW, tabCount, dpi);
    int x  = LogoW(dpi) + index * tw;
    RECT r = { x, S(8, dpi), x + tw, TitleBarH(dpi) };
    return r;
}

static RECT GetNewTabBtnRect(int windowW, int tabCount, UINT dpi) {
    int tw = CalcTabWidth(windowW, tabCount, dpi);
    int x  = LogoW(dpi) + tabCount * tw + S(4, dpi);
    int cy = S(8, dpi) + (TitleBarH(dpi) - S(8, dpi)) / 2;
    int sz = S(22, dpi);
    RECT r = { x, cy - sz/2, x + sz, cy + sz/2 };
    return r;
}

static RECT GetWebViewRect(HWND hWnd) {
    RECT b; GetClientRect(hWnd, &b);
    // Focus mode: WebView সম্পূর্ণ window নেয়, কোনো header নেই
    if (g_windows.count(hWnd) && g_windows[hWnd].isFocusMode) {
        return b; // top = 0, full window
    }
    b.top += NavTotalH(hWnd);
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR POSITIONING & CURSOR FIX
// ─────────────────────────────────────────────────────────────────────────────
static void RepositionAddressBar(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (!wd.hAddressBar) return;

    if (g_isPureViewerMode || wd.isFullScreen || wd.isFocusMode) {
        ShowWindow(wd.hAddressBar, SW_HIDE);
        return;
    }

    UINT dpi = GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd, &cr);
    int W = cr.right;

    int navBtnArea    = S(8 + 36*3 + 8, dpi);
    int rightIconArea = S(38*3 + 12,    dpi); 
    int addrH         = S(30,           dpi); 
    int toolY         = TitleBarH(dpi);
    int addrY         = toolY + (ToolbarH(dpi) - addrH) / 2;
    int addrX         = navBtnArea;
    int addrW         = W - navBtnArea - rightIconArea - S(8, dpi);

    int leftDecorW  = S(35, dpi);
    int rightDecorW = S(95, dpi);

    int editH = S(18, dpi); 
    int editY = addrY + (addrH - editH) / 2;

    ShowWindow(wd.hAddressBar, SW_SHOW);
    SetWindowPos(wd.hAddressBar, NULL,
        addrX + leftDecorW, editY, addrW - leftDecorW - rightDecorW, editH,
        SWP_NOZORDER | SWP_NOACTIVATE);

    if (wd.hAddrFont) DeleteObject(wd.hAddrFont);
    wd.hAddrFont = CreateFontW(
        S(14, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessage(wd.hAddressBar, WM_SETFONT, (WPARAM)wd.hAddrFont, TRUE);
}

// Forward declarations
static void ToggleFocusMode(HWND hWnd);

// ─────────────────────────────────────────────────────────────────────────────
// FULLSCREEN TOGGLE
// ─────────────────────────────────────────────────────────────────────────────
void ToggleFullScreen(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    DWORD style = GetWindowLong(hWnd, GWL_STYLE);

    if (!wd.isFullScreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(hWnd, &wd.wpPrev) &&
            GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi))
        {
            SetWindowLong(hWnd, GWL_STYLE, style & ~(WS_CAPTION | WS_THICKFRAME));
            SetWindowPos(hWnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right  - mi.rcMonitor.left,
                (mi.rcMonitor.bottom - mi.rcMonitor.top) - 2, 
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            wd.isFullScreen = true;
        }
    } else {
        SetWindowLong(hWnd, GWL_STYLE, style | WS_CAPTION | WS_THICKFRAME);
        SetWindowPlacement(hWnd, &wd.wpPrev);
        SetWindowPos(hWnd, NULL, 0,0,0,0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        wd.isFullScreen = false;
    }

    RepositionAddressBar(hWnd);
    RECT wvr = GetWebViewRect(hWnd);
    if (wd.isFullScreen) { wvr.top = 0; }
    for (auto& tab : wd.tabs)
        if (tab.controller) tab.controller->put_Bounds(wvr);
    InvalidateRect(hWnd, NULL, TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
// ACCELERATOR KEY HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class AcceleratorHandler : public ICoreWebView2AcceleratorKeyPressedEventHandler {
    HWND  m_hWnd;
    ULONG m_ref = 1;
public:
    AcceleratorHandler(HWND h) : m_hWnd(h) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2AcceleratorKeyPressedEventHandler))
            { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) override {
        COREWEBVIEW2_KEY_EVENT_KIND kind; args->get_KeyEventKind(&kind);
        if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
            UINT vk; args->get_VirtualKey(&vk);
            if (vk == VK_F11) { ToggleFullScreen(m_hWnd); args->put_Handled(TRUE); }
            if (vk == VK_ESCAPE && g_windows.count(m_hWnd)) {
                auto& wdEsc = g_windows[m_hWnd];
                if (wdEsc.isFocusMode) { ToggleFocusMode(m_hWnd); args->put_Handled(TRUE); }
                else if (wdEsc.isFullScreen) { ToggleFullScreen(m_hWnd); args->put_Handled(TRUE); }
            }
        }
        return S_OK;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR SUBCLASS 
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK AddrBarProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        HWND hParent = GetParent(hWnd);
        if (!g_windows.count(hParent)) return 0;
        auto& wd = g_windows[hParent];
        auto* tab = wd.active();
        if (!tab || !tab->webview) return 0;

        wchar_t buf[2048]; GetWindowTextW(hWnd, buf, 2048);
        std::wstring input = buf;
        
        input.erase(0, input.find_first_not_of(L" \t"));
        input.erase(input.find_last_not_of(L" \t") + 1);

        if (input.empty()) return 0; 
        
        if (IsBlockedContent(input)) { 
            SetWindowTextW(hWnd, L"blocked by rasfocus"); 
            tab->url = L"blocked by rasfocus";
            tab->webview->NavigateToString(GetBlocked_HTML(wd.isDarkMode).c_str());
            return 0; 
        }

        std::wstring url;
        if (input.find(L" ") != std::wstring::npos) {
            url = L"https://www.google.com/search?q=" + UrlEncode(input);
        } else if (input.find(L"http://") == 0 || input.find(L"https://") == 0) {
            url = input;
        } else if (input.find(L".") != std::wstring::npos) {
            url = L"https://" + input;
        } else {
            url = L"https://www.google.com/search?q=" + UrlEncode(input);
        }

        tab->webview->Navigate(url.c_str());
        return 0;
    }
    if (msg == WM_NCPAINT) return 0;
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// DOUBLE-BUFFERED PAINT HELPER
// ─────────────────────────────────────────────────────────────────────────────
static void DoubleBufferedPaint(HWND hWnd, HDC hdcReal, std::function<void(HDC, int, int)> drawFn) {
    RECT cr; GetClientRect(hWnd, &cr);
    int W = cr.right, H = cr.bottom;
    if (W <= 0 || H <= 0) return;

    HDC     hdcMem  = CreateCompatibleDC(hdcReal);
    HBITMAP hBmp    = CreateCompatibleBitmap(hdcReal, W, H);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    drawFn(hdcMem, W, H);
    BitBlt(hdcReal, 0, 0, W, H, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
}

// ─────────────────────────────────────────────────────────────────────────────
// GDI+ HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static void AddRoundRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    if (r <= 0.f) { path.AddRectangle(RectF(x,y,w,h)); return; }
    path.AddArc(x,         y,         r*2, r*2, 180, 90);
    path.AddArc(x+w-r*2,   y,         r*2, r*2, 270, 90);
    path.AddArc(x+w-r*2,   y+h-r*2,   r*2, r*2,   0, 90);
    path.AddArc(x,         y+h-r*2,   r*2, r*2,  90, 90);
    path.CloseFigure();
}

static void BuildChromeTabPath(GraphicsPath& path, float x, float y, float w, float h, float cornerR) {
    float bl = x, br = x + w, top = y, bot = y + h;
    float notchW = cornerR * 1.6f;
    path.StartFigure();
    path.AddLine(bl, bot, bl + notchW, bot);
    path.AddBezier(bl + notchW, bot, bl + notchW * 0.5f, bot, bl + cornerR * 0.25f, bot - cornerR, bl + cornerR, top + cornerR);
    path.AddArc(bl + cornerR, top, cornerR * 2, cornerR * 2, 180, 90);
    path.AddLine(bl + cornerR * 3, top, br - cornerR * 3, top);
    path.AddArc(br - cornerR * 3, top, cornerR * 2, cornerR * 2, 270, 90);
    path.AddBezier(br - cornerR, top + cornerR, br - cornerR * 0.25f, bot - cornerR, br - notchW * 0.5f, bot, br - notchW, bot);
    path.AddLine(br - notchW, bot, br, bot);
    path.CloseFigure();
}

// ─────────────────────────────────────────────────────────────────────────────
// MENU ITEM TYPES SHARED TABLE
// Used consistently in DrawBrowserContent, WM_MOUSEMOVE, WM_LBUTTONDOWN
// ─────────────────────────────────────────────────────────────────────────────
static const int kMenuTypes[] = { 2, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0 }; // 13 items total
static const int kMenuTypeCount = 13;

// ─────────────────────────────────────────────────────────────────────────────
// MAIN DRAW FUNCTION 
// ─────────────────────────────────────────────────────────────────────────────
static void DrawBrowserContent(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (wd.isFullScreen) return;
    // Focus mode: header ও tab bar draw করার দরকার নেই
    if (wd.isFocusMode) return;

    UINT dpi = GetWndDpi(hWnd);
    RECT cr;  GetClientRect(hWnd, &cr);
    int W = cr.right;

    int titleH  = TitleBarH(dpi);
    int toolH   = ToolbarH(dpi);
    int navH    = NavTotalH(hWnd);
    int winBtnW = WinBtnW(dpi);

    Color cBgFrame   = wd.isDarkMode ? Color(255, 30, 30, 30)    : Color(255, 230, 230, 235);
    Color cBgTool    = wd.isDarkMode ? Color(255, 43, 43, 43)    : Color(255, 255, 255, 255);
    Color cTxtPrim   = wd.isDarkMode ? Color(255, 255, 255, 255) : Color(255, 32, 33, 36);
    Color cTxtDim    = wd.isDarkMode ? Color(255, 154, 156, 160) : Color(255, 95, 99, 104);
    Color cTabActive = wd.isDarkMode ? Color(255, 43, 43, 43)    : Color(255, 255, 255, 255);
    Color cTabHover  = wd.isDarkMode ? Color(255, 45, 45, 45)    : Color(255, 235, 236, 240);
    Color cAddrBg    = wd.isDarkMode ? Color(255, 26, 26, 26)    : Color(255, 241, 243, 244);
    Color cAddrBord  = wd.isDarkMode ? Color(255, 68, 68, 68)    : Color(255, 160, 180, 210);
    Color cDivLine   = wd.isDarkMode ? Color(255, 45, 45, 45)    : Color(255, 218, 220, 224);

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    SolidBrush bFrame(cBgFrame);
    g.FillRectangle(&bFrame, 0, 0, W, titleH);
    
    if (!g_isPureViewerMode) {
        SolidBrush bTool(cBgTool);
        g.FillRectangle(&bTool, 0, titleH, W, toolH);

        Pen sepPen(cDivLine, 1.0f);
        if (!(wd.active() && (wd.active()->url == L"LOCAL_NTP" || wd.active()->url == L"about:blank"))) {
            g.DrawLine(&sepPen, 0, navH - 1, W, navH - 1);
        }

        // ── Chrome-style Loading Bar ──
        if (wd.active() && wd.active()->loading) {
            static ULONGLONG loadStart = 0;
            ULONGLONG now = GetTickCount64();
            if (loadStart == 0 || (now - loadStart) > 4000) loadStart = now;

            // elapsed 0→4000ms → progress 0→95%
            float elapsed = (float)(now - loadStart);
            float progress = elapsed / 4000.0f;
            if (progress > 0.95f) progress = 0.95f;

            // teal loading bar (2px height, just below nav bar)
            int barY = navH - 2;
            int barW = (int)(W * progress);

            // glow effect: dark gradient
            LinearGradientBrush loadBrush(
                PointF(0.f, (float)barY),
                PointF((float)barW, (float)barY),
                Color(255, 12, 168, 176),
                Color(255, 0, 92, 230)
            );
            g.FillRectangle(&loadBrush, 0, barY, barW, 2);

            // shimmer: bright leading edge
            if (barW > 8) {
                SolidBrush shimmer(Color(200, 255, 255, 255));
                g.FillRectangle(&shimmer, barW - 8, barY, 8, 2);
            }

            // Repaint চালিয়ে যাও animation এর জন্য
            // spinner frame advance করো সব loading tab এর জন্য
            for (auto& t : wd.tabs) {
                if (t.loading) t.loadingFrame = (t.loadingFrame + 1) % 8;
            }
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }

    FontFamily ffSeg(L"Segoe UI");
    FontFamily ffMDL(L"Segoe MDL2 Assets");
    Font fSmall  (&ffSeg, Sf(12.f, dpi), FontStyleRegular, UnitPixel);
    Font fSmallBd(&ffSeg, Sf(12.f, dpi), FontStyleBold,    UnitPixel);
    Font fBrand  (&ffSeg, Sf(16.f, dpi), FontStyleBold,    UnitPixel);
    Font fIcon   (&ffMDL, Sf(14.f, dpi), FontStyleRegular, UnitPixel);
    Font fIconSm (&ffMDL, Sf(11.f, dpi), FontStyleRegular, UnitPixel);

    StringFormat sfC, sfL, sfR;
    sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);
    sfL.SetAlignment(StringAlignmentNear);   sfL.SetLineAlignment(StringAlignmentCenter);
    sfR.SetAlignment(StringAlignmentFar);    sfR.SetLineAlignment(StringAlignmentCenter);

    SolidBrush brPrim(cTxtPrim);
    SolidBrush brDim (cTxtDim);

    // Title bar: Branding
    {
        int iconX = S(15, dpi);
        SolidBrush brTeal (Color(255, 12, 168, 176)); 
        SolidBrush brWhite(Color(255, 255, 255, 255));
        SolidBrush brDark (Color(255, 32, 33, 36));

        RectF rRas((float)iconX, 0.f, (float)S(40, dpi), (float)titleH); 
        g.DrawString(L"Ras", -1, &fBrand, rRas, &sfL, &brTeal);

        RectF rBrowser((float)(iconX + S(32, dpi)), 0.f, (float)S(100, dpi), (float)titleH);
        g.DrawString(L"Browser", -1, &fBrand, rBrowser, &sfL, wd.isDarkMode ? &brWhite : &brDark);
    }

    // Window controls
    {
        int bx = W - winBtnW * 6; 
        auto DrawWinBtn = [&](int x, bool hover, bool isClose, const wchar_t* ico) {
            if (hover) {
                SolidBrush hb(isClose ? Color(255, 232, 17, 35) : (wd.isDarkMode ? Color(50, 255,255,255) : Color(20, 0,0,0)));
                g.FillRectangle(&hb, x, 0, winBtnW, titleH);
            }
            SolidBrush txtClr(isClose && hover ? Color(255,255,255,255) : cTxtPrim);
            g.DrawString(ico, -1, &fIconSm, RectF((float)x, 0.f, (float)winBtnW, (float)titleH), &sfC, &txtClr);
        };

        DrawWinBtn(bx,               wd.hFocus, false, wd.isFocusMode ? L"\xE7B8" : L"\xE7C8"); // Focus/Native App mode
        DrawWinBtn(bx + winBtnW,     wd.hPin,   false, wd.isPinned ? L"\xE840" : L"\xE718"); 
        DrawWinBtn(bx + winBtnW * 2, wd.hDark,  false, wd.isDarkMode ? L"\xE708" : L"\xE706"); 
        DrawWinBtn(bx + winBtnW * 3, wd.hMin,   false, L"\xE921");
        DrawWinBtn(bx + winBtnW * 4, wd.hMax,   false, IsZoomed(hWnd) ? L"\xE923" : L"\xE922");
        DrawWinBtn(bx + winBtnW * 5, wd.hClose, true,  L"\xE8BB");
    }

    if (!g_isPureViewerMode) {
        // Tab strip
        {
            int tc = (int)wd.tabs.size();
            float cornerR = Sf(8.f, dpi);

            for (int i = 0; i < tc; i++) {
                RECT tr = GetTabRect(W, i, tc, dpi);
                float tx = (float)tr.left, ty = (float)tr.top;
                float tw = (float)(tr.right - tr.left), th = (float)(tr.bottom - tr.top);
                bool isActive = (i == wd.activeTab);
                bool isHover  = (i == wd.hoverTabIndex);

                GraphicsPath tabPath;
                BuildChromeTabPath(tabPath, tx, ty, tw, th, cornerR);

                if (isActive || isHover) {
                    SolidBrush bTab(isActive ? cTabActive : cTabHover);
                    g.FillPath(&bTab, &tabPath);
                }

                float iconSz = Sf(14.f, dpi);
                float iconX  = tx + Sf((float)D_TAB_PAD + 4, dpi);
                float iconY  = ty + (th - iconSz) * 0.5f;

                const auto& tab = wd.tabs[i];

                if (tab.loading) {
                    // ── Spinning dots loading animation ──
                    int frame = tab.loadingFrame % 8;
                    float cx = iconX + iconSz * 0.5f;
                    float cy = iconY + iconSz * 0.5f;
                    float r  = iconSz * 0.42f;
                    for (int d = 0; d < 8; d++) {
                        float angle = (float)d * 3.14159f / 4.0f;
                        float dx = cx + r * cosf(angle) - 1.5f;
                        float dy = cy + r * sinf(angle) - 1.5f;
                        // dot brightness: leading dot = full, trailing = faded
                        int dist = (d - frame + 8) % 8;
                        int alpha = 255 - dist * 28;
                        if (alpha < 40) alpha = 40;
                        SolidBrush dotBrush(Color(alpha, 12, 168, 176));
                        g.FillEllipse(&dotBrush, dx, dy, 2.5f, 2.5f);
                    }
                } else if (tab.favicon && tab.url != L"LOCAL_NTP" && tab.url != L"about:blank" 
                           && tab.url.find(L"blocked by rasfocus") == std::wstring::npos) {
                    // ── Real favicon image ──
                    g.DrawImage(tab.favicon.get(),
                        RectF(iconX, iconY, iconSz, iconSz),
                        0.f, 0.f,
                        (float)tab.favicon->GetWidth(),
                        (float)tab.favicon->GetHeight(),
                        UnitPixel);
                } else {
                    // ── Fallback: teal dot ──
                    SolidBrush fvBrush(isActive ? Color(255,12,168,176) : Color(180,12,168,176));
                    g.FillEllipse(&fvBrush, iconX, iconY, iconSz, iconSz);
                }
                SolidBrush tBrush(isActive ? cTxtPrim : cTxtDim);
                float titleX = iconX + iconSz + Sf(6.f, dpi);
                float closeW = Sf(24.f, dpi);
                float titleW = tw - (titleX - tx) - closeW;
                
                if (titleW > 0) {
                    std::wstring displayTitle = tab.title;
                    if (displayTitle.empty() || tab.url == L"LOCAL_NTP" || tab.url == L"about:blank") 
                        displayTitle = L"New Tab"; 
                    if (tab.url.find(L"blocked by rasfocus") != std::wstring::npos) 
                        displayTitle = L"Blocked by RasFocus";
                    
                    g.DrawString(displayTitle.c_str(), -1, &fSmall, RectF(titleX, ty, titleW, th), &sfL, &tBrush);
                }

                if (isActive || isHover) {
                    float cSz = Sf(16.f, dpi);
                    float cx = tx + tw - cSz - Sf(6.f, dpi);
                    float cy = ty + (th - cSz) * 0.5f;
                    if (isHover && !isActive) {
                        SolidBrush hbx(Color(20,255,255,255));
                        g.FillEllipse(&hbx, cx, cy, cSz, cSz);
                    }
                    g.DrawString(L"\xE8BB", -1, &fIconSm, RectF(cx, cy, cSz, cSz), &sfC, &brDim);
                }
            }

            RECT ntr = GetNewTabBtnRect(W, tc, dpi);
            if (wd.hNewTab) {
                SolidBrush hb(wd.isDarkMode ? Color(50,255,255,255) : Color(20,0,0,0));
                g.FillEllipse(&hb, (float)ntr.left, (float)ntr.top,
                    (float)(ntr.right - ntr.left), (float)(ntr.bottom - ntr.top));
            }
            g.DrawString(L"\xE710", -1, &fIconSm,
                RectF((float)ntr.left, (float)ntr.top,
                      (float)(ntr.right-ntr.left), (float)(ntr.bottom-ntr.top)), &sfC, &brDim);
        }

        // Toolbar
        {
            int toolY   = titleH;
            int curX    = S(8, dpi);
            int btnSz   = S(32, dpi);
            int btnStep = S(36, dpi);
            float btnHf = (float)toolH;

            auto DrawNavBtn = [&](bool hover, bool enabled, const wchar_t* ico, int& x) {
                if (hover && enabled) {
                    SolidBrush hb(wd.isDarkMode ? Color(50,255,255,255) : Color(20,0,0,0));
                    g.FillEllipse(&hb, (float)(x+S(2,dpi)), (float)(toolY+S(4,dpi)),
                                  (float)S(28,dpi), (float)S(28,dpi));
                }
                SolidBrush ic(enabled ? cTxtPrim : cDivLine);
                g.DrawString(ico, -1, &fIcon,
                    RectF((float)x, (float)toolY, (float)btnSz, btnHf), &sfC, &ic);
                x += btnStep;
            };

            auto* atab = wd.active();
            bool canBack = atab && atab->canBack;
            bool canFwd  = atab && atab->canFwd;

            DrawNavBtn(wd.hBack, canBack, L"\xE72B", curX);
            DrawNavBtn(wd.hFwd,  canFwd,  L"\xE72A", curX);
            DrawNavBtn(wd.hRel,  true,    L"\xE72C", curX);

            {
                int addrX  = curX + S(4, dpi);
                int rightIX = W - S(38*3 + 12, dpi); 
                int addrW  = rightIX - addrX - S(8, dpi);
                int addrH  = S(30, dpi);
                int addrY  = toolY + (toolH - addrH) / 2;

                SolidBrush addrBg(cAddrBg);
                Pen addrPen(cAddrBord, 1.0f);
                GraphicsPath pill;
                AddRoundRect(pill, (float)addrX, (float)addrY, (float)addrW, (float)addrH, Sf(15.f, dpi));
                g.FillPath(&addrBg, &pill);
                g.DrawPath(&addrPen, &pill);

                SolidBrush gBrush(wd.isDarkMode ? Color(255,200,200,200) : Color(255,80,80,80));
                g.DrawString(L"G", -1, &fBrand,
                    RectF((float)addrX + Sf(12.f,dpi), (float)addrY, Sf(20.f,dpi), (float)addrH),
                    &sfC, &gBrush);

                float aiW = Sf(85.f, dpi);
                float aiH = (float)addrH - Sf(6.f, dpi);
                float aiX = (float)addrX + (float)addrW - aiW - Sf(3.f, dpi);
                float aiY = (float)addrY + Sf(3.f, dpi);
                
                GraphicsPath aiPill;
                AddRoundRect(aiPill, aiX, aiY, aiW, aiH, Sf(10.f, dpi));
                
                LinearGradientBrush aiBg(
                    PointF(aiX, aiY), PointF(aiX + aiW, aiY),
                    Color(255, 12, 168, 176), Color(255, 0, 92, 230));
                g.FillPath(&aiBg, &aiPill);
                
                SolidBrush aiTxt(Color(255, 255, 255, 255));
                g.DrawString(L"\x2728 AI Mode", -1, &fSmallBd,
                    RectF(aiX, aiY, aiW, aiH), &sfC, &aiTxt);
            }

            // Right Toolbar Icons
            int rx = W - S(36*3 + 8, dpi);
            auto DrawRightBtn = [&](bool hover, const wchar_t* ico, int x) {
                if (hover) {
                    SolidBrush hb(wd.isDarkMode ? Color(50,255,255,255) : Color(20,0,0,0));
                    g.FillEllipse(&hb, (float)(x+S(2,dpi)), (float)(toolY+S(4,dpi)),
                                  (float)S(28,dpi), (float)S(28,dpi));
                }
                g.DrawString(ico, -1, &fIcon,
                    RectF((float)x, (float)toolY, (float)btnSz, btnHf), &sfC, &brPrim);
            };
            DrawRightBtn(wd.hProfile, L"\xE77B", rx); rx += btnStep; 
            DrawRightBtn(wd.hExt,     L"\xE9D2", rx); rx += btnStep; 
            DrawRightBtn(wd.hMenu,    L"\xE712", rx); 
        }

        // Bookmark Bar
        if (wd.active() && (wd.active()->url == L"LOCAL_NTP" ||
            wd.active()->url == L"about:blank" ||
            wd.active()->url.find(L"blocked by rasfocus") != std::wstring::npos))
        {
            int bmkY = titleH + toolH;
            int bmkH = S(D_BOOKMARK_H, dpi);
            
            SolidBrush bmkBg(cBgTool); 
            g.FillRectangle(&bmkBg, 0, bmkY, W, bmkH);

            Pen sepPen(cDivLine, 1.0f);
            g.DrawLine(&sepPen, 0, bmkY + bmkH - 1, W, bmkY + bmkH - 1);

            SolidBrush brTxt(cTxtDim);
            
            g.DrawString(L"\xE8A4", -1, &fIconSm,
                RectF((float)S(15,dpi), (float)bmkY, (float)S(20,dpi), (float)bmkH), &sfC, &brTxt);
            g.DrawString(L"Web Store", -1, &fSmall,
                RectF((float)S(35,dpi), (float)bmkY, (float)S(100,dpi), (float)bmkH), &sfL, &brTxt);
            
            g.DrawString(L"\xE8A4", -1, &fIconSm,
                RectF((float)S(120,dpi), (float)bmkY, (float)S(20,dpi), (float)bmkH), &sfC, &brTxt);
            g.DrawString(L"RasFocus Admin", -1, &fSmall,
                RectF((float)S(140,dpi), (float)bmkY, (float)S(120,dpi), (float)bmkH), &sfL, &brTxt);
            
            g.DrawString(L"\xE838", -1, &fIconSm,
                RectF((float)(W - S(130,dpi)), (float)bmkY, (float)S(20,dpi), (float)bmkH), &sfC, &brTxt);
            g.DrawString(L"All Bookmarks", -1, &fSmall,
                RectF((float)(W - S(110,dpi)), (float)bmkY, (float)S(100,dpi), (float)bmkH), &sfL, &brTxt);
        }
    } // End of !g_isPureViewerMode

    // ── 🟢 3-DOT MENU OVERLAY (CHROME STYLE) ──────────────────
    if (wd.isMenuOpen && !g_isPureViewerMode) {
        float menuW = (float)S(320, dpi);
        int   itemH = S(34, dpi);
        float mX    = (float)W - menuW - (float)S(10, dpi);
        float mY    = GetMenuY(hWnd, dpi);   // ← FIX: use shared helper

        struct MenuItem { int type; const wchar_t* icon; const wchar_t* text; const wchar_t* shortcut; };
        std::vector<MenuItem> menuItems = {
            { 2, L"\xE77B", L"Rasel Mia",            L"Signed in"  },
            { 1, L"",       L"",                      L""           },
            { 0, L"\xE710", L"New tab",               L"Ctrl+T"     },
            { 0, L"\xE727", L"New window",            L"Ctrl+N"     },
            { 1, L"",       L"",                      L""           },
            { 0, L"\xE81C", L"History",               L"Ctrl+H"     },
            { 0, L"\xE896", L"Downloads",             L"Ctrl+J"     },
            { 0, L"\xE8A4", L"Bookmarks and lists",   L""           },
            { 0, L"\xE9D2", L"Extensions",            L""           },
            { 1, L"",       L"",                      L""           },
            { 0, L"\x2728", L"Open Gemini AI",        L""           },
            { 0, L"\xE713", L"Settings",              L""           },
            { 0, L"\xE7E8", L"Exit",                  L"Alt+F4"     }
        };

        float totalH = (float)S(10, dpi); 
        for (const auto& mi : menuItems) {
            if      (mi.type == 2) totalH += (float)S(54, dpi);
            else if (mi.type == 1) totalH += (float)S(11, dpi);
            else                   totalH += (float)itemH;
        }
        totalH += (float)S(10, dpi);

        GraphicsPath mPath;
        AddRoundRect(mPath, mX, mY, menuW, totalH, Sf(10.f, dpi));
        
        SolidBrush shadowBr(Color(60, 0, 0, 0));
        g.FillRectangle(&shadowBr, mX + 4, mY + 4, menuW, totalH);

        SolidBrush mBg(wd.isDarkMode ? Color(255, 41, 42, 45) : Color(255, 255, 255, 255));
        g.FillPath(&mBg, &mPath);
        Pen mBorder(wd.isDarkMode ? Color(255, 80, 80, 80) : Color(255, 200, 200, 200), 1.0f);
        g.DrawPath(&mBorder, &mPath);

        SolidBrush hHover(wd.isDarkMode ? Color(255, 65, 66, 70) : Color(255, 240, 240, 240));
        SolidBrush txtBr (wd.isDarkMode ? Color(255, 230, 230, 230) : Color(255, 40, 40, 40));
        SolidBrush dimBr (wd.isDarkMode ? Color(255, 150, 150, 150) : Color(255, 120, 120, 120));
        SolidBrush accentBr(Color(255, 0, 102, 204));
        
        float currY  = mY + (float)S(10, dpi);
        int itemIndex = 0;

        for (const auto& mi : menuItems) {
            if (mi.type == 2) {
                if (itemIndex == wd.hoverMenuIdx)
                    g.FillRectangle(&hHover, mX + 2, currY, menuW - 4, (float)S(54, dpi));
                g.FillEllipse(&accentBr, mX + (float)S(15, dpi), currY + (float)S(10, dpi),
                              (float)S(34, dpi), (float)S(34, dpi));
                SolidBrush wBr(Color(255,255,255,255));
                g.DrawString(mi.icon, -1, &fIcon,
                    RectF(mX + (float)S(15,dpi), currY + (float)S(10,dpi),
                          (float)S(34,dpi), (float)S(34,dpi)), &sfC, &wBr);
                g.DrawString(mi.text, -1, &fSmallBd,
                    RectF(mX + (float)S(65,dpi), currY, menuW - (float)S(75,dpi), (float)S(30,dpi)),
                    &sfL, &txtBr);
                g.DrawString(mi.shortcut, -1, &fSmall,
                    RectF(mX + (float)S(65,dpi), currY + (float)S(22,dpi),
                          menuW - (float)S(75,dpi), (float)S(20,dpi)), &sfL, &dimBr);
                currY += (float)S(54, dpi);
                itemIndex++;
            } 
            else if (mi.type == 1) {
                currY += (float)S(5, dpi);
                g.DrawLine(&mBorder, mX, currY, mX + menuW, currY);
                currY += (float)S(6, dpi);
            } 
            else {
                if (itemIndex == wd.hoverMenuIdx)
                    g.FillRectangle(&hHover, mX + 2, currY, menuW - 4, (float)itemH);
                g.DrawString(mi.icon, -1, &fIconSm,
                    RectF(mX + (float)S(15,dpi), currY, (float)S(25,dpi), (float)itemH), &sfL, &txtBr);
                g.DrawString(mi.text, -1, &fSmall,
                    RectF(mX + (float)S(55,dpi), currY, menuW - (float)S(100,dpi), (float)itemH),
                    &sfL, &txtBr);
                g.DrawString(mi.shortcut, -1, &fSmall,
                    RectF(mX, currY, menuW - (float)S(20,dpi), (float)itemH), &sfR, &dimBr);
                currY += (float)itemH;
                itemIndex++;
            }
        }
    }

    // ── Panel Overlays (menu এর পরে আঁকো যাতে সব কিছুর উপরে থাকে) ──
    RECT cr2; GetClientRect(hWnd, &cr2);
    int W2 = cr2.right, H2 = cr2.bottom;
    POINT mousePt; GetCursorPos(&mousePt); ScreenToClient(hWnd, &mousePt);

    if (g_bookmarkPanelOpen)
        DrawBookmarkPanel(g, W2, H2, TitleBarH(dpi), ToolbarH(dpi),
                          wd.isDarkMode, (int)dpi, mousePt.x, mousePt.y,
                          g_bookmarkHoverIdx);

    if (g_historyPanelOpen)
        DrawHistoryPanel(g, W2, H2, TitleBarH(dpi), ToolbarH(dpi),
                         wd.isDarkMode, (int)dpi, mousePt.x, mousePt.y);

    if (g_downloadsPanelOpen)
        DrawDownloadsPanel(g, W2, H2, TitleBarH(dpi), ToolbarH(dpi),
                           wd.isDarkMode, (int)dpi, mousePt.x, mousePt.y);

    if (g_extensionPanelOpen)
        DrawExtensionPanel(g, W2, H2, TitleBarH(dpi), ToolbarH(dpi),
                           wd.isDarkMode, (int)dpi, mousePt.x, mousePt.y);

    if (g_findBarOpen)
        DrawFindBar(g, W2, H2, wd.isDarkMode, (int)dpi);

    if (g_contextMenuOpen)
        DrawContextMenu(g, W2, H2, wd.isDarkMode, (int)dpi, mousePt.x, mousePt.y);
}

void DrawBrowser(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    if (g_windows[hWnd].isFullScreen) return;

    DoubleBufferedPaint(hWnd, hdc, [&](HDC memDC, int W, int H) {
        bool dark = g_windows[hWnd].isDarkMode;
        HBRUSH hbg = CreateSolidBrush(dark ? RGB(30,30,30) : RGB(230,230,235)); 
        RECT fr = { 0, 0, W, H };
        FillRect(memDC, &fr, hbg);
        DeleteObject(hbg);
        DrawBrowserContent(hWnd, memDC);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────────────────────
static void SwitchToTab(HWND, int);
static void AddTab(HWND, std::wstring);
static void CloseTab(HWND, int);
static void CreateWebViewForTab(HWND, int);

// ─────────────────────────────────────────────────────────────────────────────
// FOCUS MODE TOGGLE  (header + tab bar লুকায়, native app feel)
// ─────────────────────────────────────────────────────────────────────────────
static void ToggleFocusMode(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    wd.isFocusMode = !wd.isFocusMode;

    // Address bar hide/show
    ShowWindow(wd.hAddressBar, wd.isFocusMode ? SW_HIDE : SW_SHOW);

    // WebView কে সব জায়গা দাও যদি focus mode চালু থাকে
    RECT wvr = GetWebViewRect(hWnd);
    for (auto& tab : wd.tabs)
        if (tab.controller) tab.controller->put_Bounds(wvr);

    InvalidateRect(hWnd, NULL, TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
static void SwitchToTab(HWND hWnd, int idx) {
    auto& wd = g_windows[hWnd];
    if (idx < 0 || idx >= (int)wd.tabs.size()) return;

    if (wd.activeTab != idx && wd.activeTab < (int)wd.tabs.size())
        if (wd.tabs[wd.activeTab].controller)
            wd.tabs[wd.activeTab].controller->put_IsVisible(FALSE);

    wd.activeTab = idx;
    auto& tab = wd.tabs[idx];

    if (tab.controller) {
        tab.controller->put_IsVisible(TRUE);
        RECT wvr = GetWebViewRect(hWnd);
        tab.controller->put_Bounds(wvr);
    } else {
        CreateWebViewForTab(hWnd, idx);
    }

    if (wd.hAddressBar) {
        if (tab.url == L"LOCAL_NTP" || tab.url == L"about:blank" ||
            tab.url.find(L"blocked by rasfocus") != std::wstring::npos) 
            SetWindowTextW(wd.hAddressBar, L"");
        else 
            SetWindowTextW(wd.hAddressBar, tab.url.c_str());
    }

    RepositionAddressBar(hWnd);
    InvalidateRect(hWnd, NULL, TRUE); 
}

static void CloseTab(HWND hWnd, int idx) {
    auto& wd = g_windows[hWnd];
    if (wd.tabs.empty()) return;

    auto& tab = wd.tabs[idx];
    if (tab.controller) {
        tab.controller->put_IsVisible(FALSE);
        tab.controller->Close();
    }
    wd.tabs.erase(wd.tabs.begin() + idx);

    if (wd.tabs.empty()) { DestroyWindow(hWnd); return; }

    wd.activeTab = min(wd.activeTab, (int)wd.tabs.size() - 1);
    SwitchToTab(hWnd, wd.activeTab);
}

static void AddTab(HWND hWnd, std::wstring url) {
    auto& wd = g_windows[hWnd];
    TabData tab; tab.url = url; tab.title = L"New Tab";
    wd.tabs.push_back(tab);
    int newIdx = (int)wd.tabs.size() - 1;
    SwitchToTab(hWnd, newIdx);
    CreateWebViewForTab(hWnd, newIdx);
}

// ─────────────────────────────────────────────────────────────────────────────
// WEBVIEW2 CONTROLLER COMPLETION HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class TabControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    HWND         m_hWnd;
    int          m_tabIdx;
    std::wstring m_startUrl;
    ULONG        m_ref = 1;

public:
    TabControllerHandler(HWND h, int idx, std::wstring url)
        : m_hWnd(h), m_tabIdx(idx), m_startUrl(std::move(url)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { *ppv = this; return S_OK; }
    ULONG   STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG   STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this; return r;
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* ctl) override {
        if (FAILED(hr) || !ctl) return S_OK;
        if (!g_windows.count(m_hWnd)) return S_OK;
        auto& wd = g_windows[m_hWnd];
        if (m_tabIdx >= (int)wd.tabs.size()) return S_OK;

        auto& tab = wd.tabs[m_tabIdx];
        tab.controller = ctl;
        ctl->get_CoreWebView2(&tab.webview);

        // ── Advanced Features: Download Manager, Custom Context Menu, Privacy Shield ──
        SetupAdvancedFeatures(m_hWnd, tab.webview);

        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
            COREWEBVIEW2_COLOR bg = wd.isDarkMode
                ? COREWEBVIEW2_COLOR{255, 30, 30, 30}
                : COREWEBVIEW2_COLOR{255, 255, 255, 255};
            ctl2->put_DefaultBackgroundColor(bg);
        }

        ICoreWebView2Settings* settings = nullptr;
        if (SUCCEEDED(tab.webview->get_Settings(&settings)) && settings) {
            settings->put_IsScriptEnabled(TRUE);
            settings->put_AreDefaultScriptDialogsEnabled(TRUE);
            settings->put_IsWebMessageEnabled(TRUE);
            settings->put_AreDefaultContextMenusEnabled(TRUE);
            settings->put_IsStatusBarEnabled(TRUE);

            ComPtr<ICoreWebView2Settings2> s2;
            if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&s2)))) {
                // Latest Chrome UA — ChatGPT/OpenAI older UA কে suspicious মনে করে
                s2->put_UserAgent(
                    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                    L"AppleWebKit/537.36 (KHTML, like Gecko) "
                    L"Chrome/136.0.0.0 Safari/537.36");
            }
        }

        // ── Full Anti-Bot Detection Bypass (Google + ChatGPT + OpenAI compatible) ──
        tab.webview->AddScriptToExecuteOnDocumentCreated(
            // ── 1. webdriver flag — most important ──
            L"(() => {"
            L"  try { Object.defineProperty(navigator, 'webdriver', { get: () => undefined, configurable: true }); } catch(e){}"

            // ── 2. chrome object — ChatGPT checks window.chrome deeply ──
            L"  if (!window.chrome) {"
            L"    window.chrome = {"
            L"      app: { isInstalled: false },"
            L"      csi: function(){ return {}; },"
            L"      loadTimes: function(){ return {}; },"
            L"      runtime: {},"
            L"      webstore: {}"
            L"    };"
            L"  }"

            // ── 3. permissions API — ChatGPT uses this for microphone/notifications ──
            L"  const origQuery = window.navigator.permissions && window.navigator.permissions.query ? window.navigator.permissions.query.bind(window.navigator.permissions) : null;"
            L"  if (origQuery) {"
            L"    Object.defineProperty(window.navigator.permissions, 'query', {"
            L"      value: (p) => p.name === 'notifications' ? Promise.resolve({state: Notification.permission}) : origQuery(p),"
            L"      configurable: true"
            L"    });"
            L"  }"

            // ── 4. plugins — empty plugins = automation flag ──
            L"  try { Object.defineProperty(navigator, 'plugins', { get: () => { const a = [1,2,3,4,5]; a.item = i => a[i]; a.namedItem = () => null; a.refresh = () => {}; return a; }, configurable: true }); } catch(e){}"

            // ── 5. languages ──
            L"  try { Object.defineProperty(navigator, 'languages', { get: () => ['en-US', 'en'], configurable: true }); } catch(e){}"

            // ── 6. platform ──
            L"  try { Object.defineProperty(navigator, 'platform', { get: () => 'Win32', configurable: true }); } catch(e){}"

            // ── 7. hardwareConcurrency — 0 = bot flag ──
            L"  try { Object.defineProperty(navigator, 'hardwareConcurrency', { get: () => 8, configurable: true }); } catch(e){}"

            // ── 8. deviceMemory — ChatGPT checks this ──
            L"  try { Object.defineProperty(navigator, 'deviceMemory', { get: () => 8, configurable: true }); } catch(e){}"

            // ── 9. connection — WebView2 lacks this, ChatGPT checks it ──
            L"  try { if (!navigator.connection) { Object.defineProperty(navigator, 'connection', { get: () => ({ effectiveType: '4g', rtt: 50, downlink: 10, saveData: false }), configurable: true }); } } catch(e){}"

            // ── 10. vendor — should be 'Google Inc.' for Chrome ──
            L"  try { Object.defineProperty(navigator, 'vendor', { get: () => 'Google Inc.', configurable: true }); } catch(e){}"

            // ── 11. maxTouchPoints — 0 on desktop is OK but some checks look for it ──
            L"  try { Object.defineProperty(navigator, 'maxTouchPoints', { get: () => 1, configurable: true }); } catch(e){}"

            // ── 12. WebGL fingerprint — some anti-bot checks this ──
            L"  try {"
            L"    const origGetParam = WebGLRenderingContext.prototype.getParameter;"
            L"    WebGLRenderingContext.prototype.getParameter = function(p) {"
            L"      if (p === 37445) return 'Intel Inc.';"
            L"      if (p === 37446) return 'Intel Iris OpenGL Engine';"
            L"      return origGetParam.call(this, p);"
            L"    };"
            L"  } catch(e){}"

            // ── 13. Remove WebView2-specific properties from window ──
            L"  try { delete window.chrome.webview; } catch(e){}"

            // ── 14. Notification permission — ChatGPT checks this on load ──
            L"  try { if (Notification && Notification.permission === 'default') {} } catch(e){}"

            // ── 15. window.open — Google OAuth uses this for popup flow ──
            L"  const _origOpen = window.open;"
            L"  window.open = function(url, target, features) {"
            L"    if (url && (url.includes('accounts.google.com') || url.includes('google.com/o/oauth'))) {"
            L"      return _origOpen.call(this, url, '_blank', features);"
            L"    }"
            L"    return _origOpen.call(this, url, target, features);"
            L"  };"

            // ── 16. localStorage / sessionStorage — কিছু auth flow এ দরকার ──
            L"  try { window.localStorage.setItem('__test__', '1'); window.localStorage.removeItem('__test__'); } catch(e){}"

            // ── 17. Credential Management API — Google One-tap এ দরকার ──
            L"  if (!navigator.credentials) {"
            L"    Object.defineProperty(navigator, 'credentials', {"
            L"      get: () => ({ get: () => Promise.resolve(null), store: () => Promise.resolve(), create: () => Promise.resolve(null) }),"
            L"      configurable: true"
            L"    });"
            L"  }"

            L"})();",
            nullptr);

        tab.webview->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr; args->get_Uri(&uri);
                if (uri) {
                    std::wstring urlStr(uri);

                    // ── ChatGPT/OpenAI এর জন্য extra headers inject করো ──
                    bool isChatGPT = (urlStr.find(L"chatgpt.com") != std::wstring::npos ||
                                      urlStr.find(L"openai.com")  != std::wstring::npos);
                    if (isChatGPT) {
                        // Sec-Fetch headers যোগ করো — real browser এর মতো
                        ComPtr<ICoreWebView2HttpRequestHeaders> headers;
                        if (SUCCEEDED(args->get_RequestHeaders(&headers)) && headers) {
                            headers->SetHeader(L"Sec-Fetch-Mode",    L"navigate");
                            headers->SetHeader(L"Sec-Fetch-Site",    L"none");
                            headers->SetHeader(L"Sec-Fetch-User",    L"?1");
                            headers->SetHeader(L"Sec-Fetch-Dest",    L"document");
                            headers->SetHeader(L"Accept-Language",   L"en-US,en;q=0.9");
                            headers->SetHeader(L"Upgrade-Insecure-Requests", L"1");
                        }
                    }

                    if (IsBlockedContent(urlStr)) {
                        args->put_Cancel(TRUE);
                        if (g_windows.count(m_hWnd)) {
                            auto& w = g_windows[m_hWnd];
                            if (w.hAddressBar) SetWindowTextW(w.hAddressBar, L"blocked by rasfocus");
                            if (m_tabIdx >= 0 && m_tabIdx < (int)w.tabs.size()) {
                                w.tabs[m_tabIdx].url = L"blocked by rasfocus";
                                w.tabs[m_tabIdx].loading = false;
                                w.tabs[m_tabIdx].webview->NavigateToString(
                                    GetBlocked_HTML(w.isDarkMode).c_str());
                            }
                        }
                    } else {
                        // ── Loading শুরু হলে loading = true ──
                        if (g_windows.count(m_hWnd)) {
                            auto& w = g_windows[m_hWnd];
                            if (m_tabIdx >= 0 && m_tabIdx < (int)w.tabs.size()) {
                                w.tabs[m_tabIdx].loading = true;
                                w.tabs[m_tabIdx].favicon = nullptr; // পুরনো favicon সরিয়ে দাও
                                w.tabs[m_tabIdx].loadingFrame = 0;
                                InvalidateRect(m_hWnd, NULL, FALSE);
                            }
                        }
                    }
                    CoTaskMemFree(uri);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                // Google OAuth, "Sign in with Google" এবং যেকোনো popup window handle করো
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                std::wstring popupUrl = uri ? uri : L"";
                if (uri) CoTaskMemFree(uri);

                BOOL isUserInitiated = FALSE;
                args->get_IsUserInitiated(&isUserInitiated);

                // Google OAuth / SSO popup — accounts.google.com বা auth endpoint
                bool isGoogleAuth = (
                    popupUrl.find(L"accounts.google.com") != std::wstring::npos ||
                    popupUrl.find(L"google.com/o/oauth")  != std::wstring::npos ||
                    popupUrl.find(L"auth.openai.com")     != std::wstring::npos ||
                    popupUrl.find(L"login.microsoftonline")!= std::wstring::npos ||
                    popupUrl.find(L"appleid.apple.com")   != std::wstring::npos ||
                    popupUrl.find(L"github.com/login")    != std::wstring::npos ||
                    popupUrl.find(L"facebook.com/login")  != std::wstring::npos
                );

                if (isGoogleAuth || isUserInitiated) {
                    // নতুন tab এ খোলো
                    AddTab(m_hWnd, popupUrl);
                    args->put_Handled(TRUE);
                } else if (!popupUrl.empty()) {
                    // অন্য popup — নতুন tab এ খোলো
                    AddTab(m_hWnd, popupUrl);
                    args->put_Handled(TRUE);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx >= (int)w.tabs.size()) return S_OK;
                LPWSTR docTitle = nullptr;
                sender->get_DocumentTitle(&docTitle);
                if (docTitle) {
                    w.tabs[m_tabIdx].title = docTitle;
                    CoTaskMemFree(docTitle);
                    InvalidateRect(m_hWnd, NULL, FALSE);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_SourceChanged(
            Callback<ICoreWebView2SourceChangedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx != w.activeTab) return S_OK;
                LPWSTR src = nullptr; sender->get_Source(&src);
                if (src) {
                    std::wstring urlStr(src);
                    w.tabs[m_tabIdx].url = urlStr;
                    
                    // ── History Auto-save ──
                    SaveToHistory(urlStr, w.tabs[m_tabIdx].title);

                    if (w.hAddressBar) {
                        if (urlStr == L"LOCAL_NTP" || urlStr == L"about:blank" ||
                            urlStr.find(L"blocked by rasfocus") != std::wstring::npos) {
                            SetWindowTextW(w.hAddressBar, L"");
                        } else {
                            SetWindowTextW(w.hAddressBar, src);
                        }
                    }
                    CoTaskMemFree(src);
                }
                
                if (m_tabIdx == w.activeTab) {
                    RECT wvr = GetWebViewRect(m_hWnd);
                    w.tabs[m_tabIdx].controller->put_Bounds(wvr);
                    InvalidateRect(m_hWnd, NULL, TRUE);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx >= (int)w.tabs.size()) return S_OK;

                // ── Loading শেষ ──
                w.tabs[m_tabIdx].loading = false;

                // ── Favicon fetch via JS ──
                // favicon কে base64 data URL হিসেবে নিয়ে আসি
                {
                    int captureIdx = m_tabIdx;
                    HWND captureWnd = m_hWnd;
                    sender->ExecuteScript(
                        L"(() => {"
                        L"  const link = document.querySelector(\"link[rel*='icon']\");"
                        L"  const href = link ? link.href : (location.origin + '/favicon.ico');"
                        L"  return new Promise(resolve => {"
                        L"    const img = new Image();"
                        L"    img.crossOrigin = 'anonymous';"
                        L"    img.onload = () => {"
                        L"      const c = document.createElement('canvas');"
                        L"      c.width = c.height = 32;"
                        L"      const ctx = c.getContext('2d');"
                        L"      ctx.drawImage(img, 0, 0, 32, 32);"
                        L"      resolve(c.toDataURL('image/png'));"
                        L"    };"
                        L"    img.onerror = () => resolve('');"
                        L"    img.src = href;"
                        L"  });"
                        L"})()",
                        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                        [captureIdx, captureWnd](HRESULT, LPCWSTR resultJson) -> HRESULT {
                            if (!resultJson || !g_windows.count(captureWnd)) return S_OK;
                            auto& w2 = g_windows[captureWnd];
                            if (captureIdx >= (int)w2.tabs.size()) return S_OK;

                            // resultJson = "\"data:image/png;base64,AAAA...\""
                            std::wstring s(resultJson);
                            // strip leading/trailing quotes
                            if (s.size() >= 2 && s.front() == L'"') {
                                s = s.substr(1, s.size() - 2);
                                // unescape \\/ etc.
                                std::wstring clean;
                                for (size_t k = 0; k < s.size(); k++) {
                                    if (s[k] == L'\\' && k + 1 < s.size()) { clean += s[k+1]; k++; }
                                    else clean += s[k];
                                }
                                s = clean;
                            }
                            // data:image/png;base64, prefix কাটো
                            const std::wstring prefix = L"data:image/png;base64,";
                            if (s.find(prefix) != 0) return S_OK;
                            std::wstring b64w = s.substr(prefix.size());

                            // wstring → narrow string
                            std::string b64(b64w.begin(), b64w.end());

                            // Base64 decode
                            auto decodeB64 = [](const std::string& in) -> std::vector<BYTE> {
                                static const std::string tbl =
                                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                                std::vector<BYTE> out;
                                int val = 0, valb = -8;
                                for (unsigned char c : in) {
                                    if (c == '=') break;
                                    int pos = (int)tbl.find(c);
                                    if (pos == (int)std::string::npos) continue;
                                    val = (val << 6) + pos;
                                    valb += 6;
                                    if (valb >= 0) {
                                        out.push_back((BYTE)((val >> valb) & 0xFF));
                                        valb -= 8;
                                    }
                                }
                                return out;
                            };
                            std::vector<BYTE> pngBytes = decodeB64(b64);
                            if (pngBytes.empty()) return S_OK;

                            // PNG bytes → GDI+ Bitmap (IStream দিয়ে)
                            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, pngBytes.size());
                            if (!hMem) return S_OK;
                            void* pMem = GlobalLock(hMem);
                            if (!pMem) { GlobalFree(hMem); return S_OK; }
                            memcpy(pMem, pngBytes.data(), pngBytes.size());
                            GlobalUnlock(hMem);

                            IStream* pStream = nullptr;
                            if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &pStream)) && pStream) {
                                auto bmp = std::make_shared<Bitmap>(pStream);
                                pStream->Release();
                                if (bmp && bmp->GetLastStatus() == Ok) {
                                    w2.tabs[captureIdx].favicon = bmp;
                                    InvalidateRect(captureWnd, NULL, FALSE);
                                }
                            }
                            return S_OK;
                        }).Get()
                    );
                }

                InvalidateRect(m_hWnd, NULL, FALSE);

                // ── ChatGPT / OpenAI post-load bot-check bypass ──
                {
                    const std::wstring& curUrl = w.tabs[m_tabIdx].url;
                    bool isChatGPT = (curUrl.find(L"chatgpt.com") != std::wstring::npos ||
                                      curUrl.find(L"openai.com")  != std::wstring::npos);
                    if (isChatGPT) {
                        sender->ExecuteScript(
                            L"(() => {"
                            // webdriver আবার remove করো (some SPAs re-check on route change)
                            L"  try { Object.defineProperty(navigator, 'webdriver', { get: () => undefined, configurable: true }); } catch(e){}"
                            // chrome object reinforce করো
                            L"  if (!window.chrome || !window.chrome.runtime) {"
                            L"    window.chrome = { app:{isInstalled:false}, csi:()=>({}), loadTimes:()=>({}), runtime:{}, webstore:{} };"
                            L"  }"
                            // vendor
                            L"  try { Object.defineProperty(navigator, 'vendor', { get: () => 'Google Inc.', configurable: true }); } catch(e){}"
                            // hardwareConcurrency
                            L"  try { Object.defineProperty(navigator, 'hardwareConcurrency', { get: () => 8, configurable: true }); } catch(e){}"
                            // deviceMemory
                            L"  try { Object.defineProperty(navigator, 'deviceMemory', { get: () => 8, configurable: true }); } catch(e){}"
                            // connection
                            L"  try { if (!navigator.connection) Object.defineProperty(navigator, 'connection', { get: () => ({effectiveType:'4g',rtt:50,downlink:10,saveData:false}), configurable: true }); } catch(e){}"
                            // WebView2 specific flag remove
                            L"  try { delete window.__WebView2__; } catch(e){}"
                            L"  try { delete window.chrome.webview; } catch(e){}"
                            L"})();",
                            nullptr);
                    }
                }

                // ── AI Inject Script (platform-specific UI hiding) ──
                std::wstring injectScript = GetAiInjectScript(w.tabs[m_tabIdx].url);
                if (!injectScript.empty()) {
                    sender->ExecuteScript(injectScript.c_str(), nullptr);
                }

                // ── 100% Ad Blocker Script ──
                // YouTube, website banners, popups, tracking scripts সব block করে
                static const wchar_t* kAdBlockScript = LR"JS(
(function() {
  // ── 1. YouTube Ads (video ads + banner ads + overlay) ──
  function blockYouTubeAds() {
    // video ad container গুলো hide করো
    const adSelectors = [
      '.ytp-ad-module', '.ytp-ad-overlay-container',
      '.ytp-ad-text-overlay', '.ytp-ad-skip-button-container',
      '.ytp-ad-progress-bar', '.ytp-ad-player-overlay',
      '#masthead-ad', '.ytd-banner-promo-renderer',
      'ytd-ad-slot-renderer', 'ytd-in-feed-ad-layout-renderer',
      'ytd-promoted-sparkles-web-renderer', '.ytd-promoted-video-renderer',
      '#player-ads', '.ad-showing .video-ads',
      'ytd-display-ad-renderer', 'ytd-action-companion-ad-renderer',
      '.ytd-rich-item-renderer:has(ytd-ad-slot-renderer)',
      'ytd-promoted-sparkles-text-search-renderer',
      '.GoogleActiveViewElement', '#feedModuleAdSlot'
    ];
    adSelectors.forEach(sel => {
      document.querySelectorAll(sel).forEach(el => {
        el.style.setProperty('display', 'none', 'important');
        el.style.setProperty('visibility', 'hidden', 'important');
      });
    });

    // video ad auto-skip
    const skipBtn = document.querySelector('.ytp-skip-ad-button, .ytp-ad-skip-button');
    if (skipBtn) skipBtn.click();

    // video playing হলে ad duration check করো
    const video = document.querySelector('video.html5-main-video');
    if (video) {
      const adBadge = document.querySelector('.ytp-ad-badge, .ad-badge');
      if (adBadge) {
        // mute করো ad এর সময়
        const wasMuted = video.muted;
        video.muted = true;
        video.playbackRate = 16; // fast forward
        const restoreCheck = setInterval(() => {
          const skipNow = document.querySelector('.ytp-skip-ad-button, .ytp-ad-skip-button');
          if (skipNow) { skipNow.click(); }
          const stillAd = document.querySelector('.ytp-ad-badge, .ad-badge');
          if (!stillAd) {
            video.playbackRate = 1;
            video.muted = wasMuted;
            clearInterval(restoreCheck);
          }
        }, 200);
      }
    }
  }

  // ── 2. General Website Ad Selectors ──
  function blockGeneralAds() {
    const generalAdSelectors = [
      // Common ad class names
      '[class*="google-ad"]', '[class*="adsense"]', '[id*="adsense"]',
      '[class*="ad-banner"]', '[class*="ad-container"]', '[class*="ad-wrapper"]',
      '[class*="ad-slot"]', '[class*="ad-unit"]', '[class*="ad-placement"]',
      '[id*="ad-banner"]', '[id*="ad-container"]', '[id*="ad-slot"]',
      '[id*="google_ads"]', '[id*="gads"]', '[id*="div-gpt-ad"]',
      '[class*="sponsored"]', '[class*="promo-ad"]', '[class*="advertisement"]',
      '[data-ad-unit]', '[data-ad-slot]', '[data-ad-client]',
      'ins.adsbygoogle', 'div[id^="google_ads_iframe"]',
      'iframe[src*="googlesyndication"]', 'iframe[src*="doubleclick"]',
      'iframe[src*="adservice"]', 'iframe[src*="ads."]',
      // Facebook ads
      '[data-pagelet*="FeedUnit_Sponsor"]', '._7jyg._7jyi',
      // Generic popups/overlays
      '[class*="popup-ad"]', '[class*="modal-ad"]', '[id*="popup-ad"]',
      '[class*="overlay-ad"]', '[class*="interstitial"]',
      // Sticky banners
      '[class*="sticky-ad"]', '[class*="floating-ad"]', '[id*="sticky-ad"]'
    ];
    generalAdSelectors.forEach(sel => {
      try {
        document.querySelectorAll(sel).forEach(el => {
          el.style.setProperty('display', 'none', 'important');
        });
      } catch(e) {}
    });
  }

  // ── 3. Popup/Overlay/Cookie Banner Blocker ──
  function blockPopups() {
    // Cookie consent overlays
    const cookieSelectors = [
      '[class*="cookie-banner"]', '[class*="cookie-consent"]', '[class*="cookie-notice"]',
      '[id*="cookie-banner"]', '[id*="cookie-consent"]', '[id*="cookiebar"]',
      '[class*="gdpr"]', '[id*="gdpr"]', '[class*="consent-banner"]',
      '.cc-window', '#onetrust-banner-sdk', '.evidon-banner',
      '#cookie-law-info-bar', '.cookiealert', '#CybotCookiebotDialog'
    ];
    cookieSelectors.forEach(sel => {
      try {
        document.querySelectorAll(sel).forEach(el => {
          el.style.setProperty('display', 'none', 'important');
        });
      } catch(e) {}
    });

    // Body scroll lock থেকে মুক্তি দাও (popup এর কারণে scroll বন্ধ হলে)
    document.body.style.removeProperty('overflow');
    document.documentElement.style.removeProperty('overflow');
  }

  // ── 4. Run immediately ──
  blockYouTubeAds();
  blockGeneralAds();
  blockPopups();

  // ── 5. DOM changes observe করো (dynamically loaded ads এর জন্য) ──
  const observer = new MutationObserver(() => {
    blockYouTubeAds();
    blockGeneralAds();
    blockPopups();
  });
  observer.observe(document.documentElement, {
    childList: true, subtree: true
  });

  // ── 6. interval দিয়েও চালাও (YouTube SPA navigation এর জন্য) ──
  setInterval(() => {
    blockYouTubeAds();
    blockPopups();
  }, 1000);

})();
)JS";
                sender->ExecuteScript(kAdBlockScript, nullptr);

                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_HistoryChanged(
            Callback<ICoreWebView2HistoryChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx >= (int)w.tabs.size()) return S_OK;
                BOOL canB = FALSE, canF = FALSE;
                sender->get_CanGoBack(&canB);
                sender->get_CanGoForward(&canF);
                w.tabs[m_tabIdx].canBack = !!canB;
                w.tabs[m_tabIdx].canFwd  = !!canF;
                InvalidateRect(m_hWnd, NULL, FALSE);
                return S_OK;
            }).Get(), nullptr);

        ComPtr<ICoreWebView2Controller3> ctl3;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl3)))) {
            EventRegistrationToken tok;
            ctl3->add_AcceleratorKeyPressed(new AcceleratorHandler(m_hWnd), &tok);
        }

        bool isActive = (m_tabIdx == wd.activeTab);
        ctl->put_IsVisible(isActive ? TRUE : FALSE);
        RECT wvr = GetWebViewRect(m_hWnd);
        ctl->put_Bounds(wvr);

        std::wstring nav = m_startUrl;
        if (!g_isPureViewerMode && (nav == L"RAS_BROWSER" || nav.empty() || nav == L"about:blank")) {
            nav = L"LOCAL_NTP"; 
        }
        
        if (nav == L"LOCAL_NTP") {
            tab.webview->NavigateToString(GetLocalNTP_HTML(wd.isDarkMode).c_str());
        } else {
            tab.webview->Navigate(nav.c_str());
        }
        return S_OK;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// WEBVIEW2 ENVIRONMENT HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    HWND  m_hWnd;
    int   m_tabIdx;
    ULONG m_ref = 1;

public:
    EnvCompletedHandler(HWND h, int idx) : m_hWnd(h), m_tabIdx(idx) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { *ppv = this; return S_OK; }
    ULONG   STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG   STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env) override {
        if (FAILED(hr) || !env) return S_OK;
        g_sharedEnv = env;
        if (!g_windows.count(m_hWnd)) return S_OK;
        auto& wd  = g_windows[m_hWnd];
        auto& tab = wd.tabs[m_tabIdx];
        g_sharedEnv->CreateCoreWebView2Controller(
            m_hWnd, new TabControllerHandler(m_hWnd, m_tabIdx, tab.url));
        return S_OK;
    }
};

static void CreateWebViewForTab(HWND hWnd, int tabIdx) {
    if (!g_windows.count(hWnd)) return;
    auto& wd  = g_windows[hWnd];
    auto& tab = wd.tabs[tabIdx];

    if (g_sharedEnv) {
        g_sharedEnv->CreateCoreWebView2Controller(
            hWnd, new TabControllerHandler(hWnd, tabIdx, tab.url));
    } else {
        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        options->put_AdditionalBrowserArguments(
            // Extension support
            L"--enable-features=msWebView2EnableExtensions "
            // GPU
            L"--enable-gpu-rasterization "
            L"--enable-zero-copy "
            // Bot detection bypass — সবচেয়ে গুরুত্বপূর্ণ
            L"--disable-blink-features=AutomationControlled "
            // Google Sign-in এর জন্য — third-party cookie দরকার
            L"--disable-features=SameSiteByDefaultCookies,CookiesWithoutSameSiteMustBeSecure "
            // Google OAuth popup এর জন্য
            L"--disable-features=BlockInsecurePrivateNetworkRequests "
            // General compatibility
            L"--disable-features=Translate "
            L"--no-first-run "
            L"--no-default-browser-check "
            // Web Audio API — কিছু site sign-in verify তে ব্যবহার করে
            L"--autoplay-policy=no-user-gesture-required"
        );

        wchar_t appDataPath[MAX_PATH];
        SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath);
        std::wstring udDir = std::wstring(appDataPath) + L"\\RasBrowserData";
        CreateDirectoryW(udDir.c_str(), NULL);

        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, udDir.c_str(), options.Get(), new EnvCompletedHandler(hWnd, tabIdx));

        if (FAILED(hr)) {
            CreateCoreWebView2EnvironmentWithOptions(
                nullptr, nullptr, nullptr, new EnvCompletedHandler(hWnd, tabIdx));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DWM SHADOW HELPER
// ─────────────────────────────────────────────────────────────────────────────
static void ApplyDwmShadow(HWND hWnd) {
    MARGINS m = { 0, 0, 0, 1 };
    DwmExtendFrameIntoClientArea(hWnd, &m);
    DWORD pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

// ─────────────────────────────────────────────────────────────────────────────
// WINDOW PROCEDURE
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;

    case WM_NCHITTEST: {
        LRESULT def = DefWindowProcW(hWnd, msg, wParam, lParam);
        if (def == HTNOWHERE || def == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            RECT cr; GetClientRect(hWnd, &cr);
            UINT dpi    = GetWndDpi(hWnd);
            int  border = S(8, dpi);

            if (!g_windows.count(hWnd) || !g_windows[hWnd].isFullScreen) {
                if (pt.y < border && pt.x < border)                          return HTTOPLEFT;
                if (pt.y < border && pt.x >= cr.right - border)              return HTTOPRIGHT;
                if (pt.y >= cr.bottom - border && pt.x < border)             return HTBOTTOMLEFT;
                if (pt.y >= cr.bottom - border && pt.x >= cr.right - border) return HTBOTTOMRIGHT;
                if (pt.y < border)              return HTTOP;
                if (pt.y >= cr.bottom - border) return HTBOTTOM;
                if (pt.x < border)              return HTLEFT;
                if (pt.x >= cr.right - border)  return HTRIGHT;

                if (g_isPureViewerMode) {
                    int winBtnX = cr.right - WinBtnW(dpi) * 6; 
                    if (pt.y < TitleBarH(dpi)) {
                        if (pt.x >= winBtnX) return HTCLIENT; 
                        return HTCAPTION; 
                    }
                    return HTCLIENT;
                }

                if (pt.y < TitleBarH(dpi)) {
                    int winBtnX = cr.right - WinBtnW(dpi) * 6; 
                    if (pt.x >= winBtnX) return HTCLIENT; 
                    
                    bool onTab = false;
                    auto& wd = g_windows[hWnd];
                    int tc = (int)wd.tabs.size();
                    for (int i = 0; i < tc; i++) {
                        RECT tr = GetTabRect(cr.right, i, tc, dpi);
                        if (pt.x >= tr.left && pt.x < tr.right) { onTab = true; break; }
                    }
                    if (onTab || pt.x < LogoW(dpi)) return HTCLIENT; 
                    
                    RECT ntr = GetNewTabBtnRect(cr.right, tc, dpi);
                    if (pt.x >= ntr.left && pt.x <= ntr.right) return HTCLIENT; 

                    return HTCAPTION; 
                }
                if (pt.y < NavTotalH(hWnd)) return HTCLIENT;
            }
            return HTCLIENT;
        }
        return def;
    }

    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION) {
            ShowWindow(hWnd, IsZoomed(hWnd) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        break;

    case WM_CREATE:
        ApplyDwmShadow(hWnd);
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        DrawBrowser(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND: return 1;

    case WM_WINDOWPOSCHANGING: {
        auto* wp = (WINDOWPOS*)lParam;
        wp->flags |= SWP_NOCOPYBITS;
        break;
    }

    case WM_CTLCOLOREDIT: {
        if (g_windows.count(hWnd) && (HWND)lParam == g_windows[hWnd].hAddressBar) {
            HDC hEdit = (HDC)wParam;
            bool isDark = g_windows[hWnd].isDarkMode;
            if (isDark) {
                SetTextColor(hEdit, RGB(255, 255, 255));
                SetBkColor  (hEdit, RGB(26, 26, 26)); 
                static HBRUSH hBrDark = CreateSolidBrush(RGB(26, 26, 26));
                return (LRESULT)hBrDark;
            } else {
                SetTextColor(hEdit, RGB(32, 33, 36));
                SetBkColor  (hEdit, RGB(241, 243, 244));
                static HBRUSH hBrLight = CreateSolidBrush(RGB(241, 243, 244));
                return (LRESULT)hBrLight;
            }
        }
        break;
    }

    case WM_SIZE: {
        if (!g_windows.count(hWnd)) break;
        auto& wd = g_windows[hWnd];
        RepositionAddressBar(hWnd);
        RECT wvr = GetWebViewRect(hWnd);
        for (int i = 0; i < (int)wd.tabs.size(); i++)
            if (wd.tabs[i].controller && i == wd.activeTab)
                wd.tabs[i].controller->put_Bounds(wvr);
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }

    case WM_DPICHANGED: {
        const RECT* newRect = (const RECT*)lParam;
        SetWindowPos(hWnd, NULL,
            newRect->left, newRect->top,
            newRect->right  - newRect->left,
            newRect->bottom - newRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        RepositionAddressBar(hWnd);
        RECT wvr = GetWebViewRect(hWnd);
        if (g_windows.count(hWnd))
            for (auto& tab : g_windows[hWnd].tabs)
                if (tab.controller) tab.controller->put_Bounds(wvr);
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd];
        UINT dpi = GetWndDpi(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd, &cr); int W = cr.right;
        bool dirty = false;

        { TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 }; TrackMouseEvent(&tme); }

        int titleH  = TitleBarH(dpi);
        int navH    = NavTotalH(hWnd);
        int winBtnW = WinBtnW(dpi);
        int toolY   = titleH;

        {
            int bx = W - winBtnW * 6; 
            bool fc = (y < titleH && x >= bx             && x < bx + winBtnW);
            bool p  = (y < titleH && x >= bx + winBtnW   && x < bx + winBtnW*2);
            bool dk = (y < titleH && x >= bx + winBtnW*2 && x < bx + winBtnW*3);
            bool nm = (y < titleH && x >= bx + winBtnW*3 && x < bx + winBtnW*4);
            bool mx = (y < titleH && x >= bx + winBtnW*4 && x < bx + winBtnW*5);
            bool cl = (y < titleH && x >= bx + winBtnW*5);
            if (wd.hFocus!=fc||wd.hPin!=p||wd.hDark!=dk||wd.hMin!=nm||wd.hMax!=mx||wd.hClose!=cl)
                { wd.hFocus=fc; wd.hPin=p; wd.hDark=dk; wd.hMin=nm; wd.hMax=mx; wd.hClose=cl; dirty=true; }
        }

        if (!g_isPureViewerMode) {
            {
                int tc = (int)wd.tabs.size();
                int prev = wd.hoverTabIndex; wd.hoverTabIndex = -1;
                for (int i = 0; i < tc; i++) {
                    RECT tr = GetTabRect(W, i, tc, dpi);
                    if (x >= tr.left && x < tr.right && y >= tr.top && y < tr.bottom)
                    { wd.hoverTabIndex = i; break; }
                }
                if (prev != wd.hoverTabIndex) dirty = true;
            }

            {
                RECT ntr = GetNewTabBtnRect(W, (int)wd.tabs.size(), dpi);
                bool nt = (x>=ntr.left&&x<ntr.right&&y>=ntr.top&&y<ntr.bottom);
                if (wd.hNewTab != nt) { wd.hNewTab = nt; dirty = true; }
            }

            {
                int btnStep = S(36, dpi);
                int cx = S(8, dpi);
                bool b  = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(34,dpi)); cx+=btnStep;
                bool f  = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(34,dpi)); cx+=btnStep;
                bool rl = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(34,dpi));
                if (wd.hBack!=b||wd.hFwd!=f||wd.hRel!=rl)
                    { wd.hBack=b; wd.hFwd=f; wd.hRel=rl; dirty=true; }

                int rx = W - S(36*3+8, dpi); 
                bool pr = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(34,dpi)); rx+=btnStep;
                bool e  = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(34,dpi)); rx+=btnStep;
                bool m  = (y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(34,dpi));
                if (wd.hProfile!=pr||wd.hExt!=e||wd.hMenu!=m)
                    { wd.hProfile=pr; wd.hExt=e; wd.hMenu=m; dirty=true; }
            }
            
            // 🟢 Menu Overlay Hover Logic — FIX: mY now computed via GetMenuY()
            if (wd.isMenuOpen) {
                float menuW  = (float)S(320, dpi);
                float mX     = (float)W - menuW - (float)S(10, dpi);
                float mY     = GetMenuY(hWnd, dpi);   // ← WAS UNDECLARED
                int   itemH  = S(34, dpi);
                float currY  = mY + (float)S(10, dpi);
                int   hoverIdx   = -1;
                int   itemIndex  = 0;
                
                for (int i = 0; i < kMenuTypeCount; i++) {
                    int t = kMenuTypes[i];
                    float h = (t == 2) ? (float)S(54,dpi) : (t == 1) ? (float)S(11,dpi) : (float)itemH;
                    if (t != 1) {
                        if ((float)x >= mX && (float)x <= mX + menuW &&
                            (float)y >= currY && (float)y <= currY + h)
                            hoverIdx = itemIndex;
                        itemIndex++;
                    }
                    currY += h;
                }
                // Dismiss hover if outside menu
                if ((float)x < mX || (float)x > mX + menuW || (float)y < mY || (float)y > currY)
                    hoverIdx = -1;

                if (wd.hoverMenuIdx != hoverIdx) {
                    wd.hoverMenuIdx = hoverIdx;
                    dirty = true;
                }
            }
        }

        if (dirty) {
            RECT r = { 0, 0, W, navH };
            InvalidateRect(hWnd, &r, FALSE);
            if (wd.isMenuOpen) InvalidateRect(hWnd, NULL, FALSE);
        }

        // ── Panel hover updates (panels need full redraw on mouse move) ──
        if (g_bookmarkPanelOpen || g_historyPanelOpen || g_downloadsPanelOpen ||
            g_findBarOpen || g_contextMenuOpen || g_extensionPanelOpen) {
            // Update hover indices for panels that use them
            if (g_historyPanelOpen) {
                // history_panel tracks hover internally via mouseX/mouseY passed in Draw
                InvalidateRect(hWnd, NULL, FALSE);
            }
            if (g_bookmarkPanelOpen || g_downloadsPanelOpen ||
                g_contextMenuOpen   || g_extensionPanelOpen) {
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        break;
    }

    case WM_MOUSELEAVE: {
        if (g_windows.count(hWnd)) {
            auto& wd = g_windows[hWnd];
            wd.hMin=wd.hMax=wd.hClose=false;
            wd.hBack=wd.hFwd=wd.hRel=false;
            wd.hPin=wd.hDark=wd.hFocus=wd.hProfile=wd.hExt=wd.hMenu=false;
            wd.hNewTab=false; wd.hoverTabIndex=-1;
            RECT cr; GetClientRect(hWnd, &cr);
            cr.bottom = NavTotalH(hWnd);
            InvalidateRect(hWnd, &cr, FALSE);
            if (wd.isMenuOpen) InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd];
        UINT dpi = GetWndDpi(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd, &cr); int W = cr.right;
        RECT crFull; GetClientRect(hWnd, &crFull); int H = crFull.bottom;

        if (wd.hMin)   { ShowWindow(hWnd, SW_MINIMIZE); break; }
        if (wd.hMax)   { ShowWindow(hWnd, IsZoomed(hWnd) ? SW_RESTORE : SW_MAXIMIZE); break; }
        if (wd.hClose) { DestroyWindow(hWnd); break; }

        // ── Context Menu Click ──
        if (g_contextMenuOpen) {
            std::wstring action = HandleContextMenuClick(x, y, (int)dpi);
            CloseContextMenu();
            if (!action.empty() && action != L"" && wd.active()) {
                if      (action == L"back"    && wd.active()->webview && wd.active()->canBack) wd.active()->webview->GoBack();
                else if (action == L"forward" && wd.active()->webview && wd.active()->canFwd)  wd.active()->webview->GoForward();
                else if (action == L"reload"  && wd.active()->webview) wd.active()->webview->Reload();
                else if (action == L"open_new_tab" && wd.active()) AddTab(hWnd, wd.active()->url);
            }
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // ── Find Bar Click ──
        if (g_findBarOpen) {
            bool closed = HandleFindBarClick(x, y, W, H, (int)dpi);
            if (closed) { CloseFindBar(); InvalidateRect(hWnd, NULL, FALSE); return 0; }
        }

        // ── Bookmark Panel Click ──
        if (g_bookmarkPanelOpen) {
            std::wstring navUrl;
            int removeIdx = -1;
            bool hit = HandleBookmarkPanelClick(x, y, W, H, TitleBarH(dpi), ToolbarH(dpi), (int)dpi, navUrl, removeIdx);
            if (removeIdx >= 0) {
                RemoveBookmark(removeIdx);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            if (!navUrl.empty()) {
                g_bookmarkPanelOpen = false;
                if (wd.active() && wd.active()->webview) wd.active()->webview->Navigate(navUrl.c_str());
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
            if (hit) return 0; // click was inside panel but no action
        }

        // ── History Panel Click ──
        if (g_historyPanelOpen) {
            std::wstring navUrl = HandleHistoryPanelClick(x, y, W, H, TitleBarH(dpi), ToolbarH(dpi), (int)dpi);
            if (!navUrl.empty()) {
                g_historyPanelOpen = false;
                if (wd.active() && wd.active()->webview) wd.active()->webview->Navigate(navUrl.c_str());
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }

        // ── Downloads Panel Click ──
        if (g_downloadsPanelOpen) {
            std::wstring action = HandleDownloadsPanelClick(x, y, W, H, TitleBarH(dpi), ToolbarH(dpi), (int)dpi);
            if (action == L"clear_all") {
                ClearCompletedDownloads();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            } else if (action.substr(0, 10) == L"open_file:") {
                std::wstring path = action.substr(10);
                ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                return 0;
            } else if (action.substr(0, 12) == L"open_folder:") {
                std::wstring path = action.substr(12);
                ShellExecuteW(NULL, L"explore", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                return 0;
            }
        }

        // ── Extension Panel Click ──
        if (g_extensionPanelOpen) {
            std::wstring action = HandleExtensionPanelClick(
                x, y, W, H, TitleBarH(dpi), ToolbarH(dpi), (int)dpi);
            if (action == L"install") {
                // Folder browse dialog
                BROWSEINFOW bi = {};
                bi.hwndOwner  = hWnd;
                bi.lpszTitle  = L"Unpacked Extension folder select করুন (manifest.json থাকতে হবে)";
                bi.ulFlags    = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
                if (pidl) {
                    wchar_t folderPath[MAX_PATH];
                    if (SHGetPathFromIDListW(pidl, folderPath)) {
                        // g_sharedEnv use করো
                        InstallExtensionFromFolder(g_sharedEnv.Get(), nullptr, folderPath);
                    }
                    CoTaskMemFree(pidl);
                }
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            } else if (action.substr(0, 7) == L"toggle:") {
                int idx = std::stoi(action.substr(7));
                ToggleExtension(nullptr, idx);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            } else if (action.substr(0, 7) == L"remove:") {
                int idx = std::stoi(action.substr(7));
                UninstallExtension(idx);
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }

        if (!g_isPureViewerMode) {
            
            // 🟢 Handle Menu Open/Click interactions
            if (wd.isMenuOpen) {
                float menuW = (float)S(320, dpi);
                float mX    = (float)W - menuW - (float)S(10, dpi);
                float mY    = GetMenuY(hWnd, dpi);   // ← WAS UNDECLARED
                
                int   itemH     = S(34, dpi);
                float currY     = mY + (float)S(10, dpi);
                int   clickIdx  = -1;
                int   itemIndex = 0;
                
                for (int i = 0; i < kMenuTypeCount; i++) {
                    int t = kMenuTypes[i];
                    float h = (t == 2) ? (float)S(54,dpi) : (t == 1) ? (float)S(11,dpi) : (float)itemH;
                    if (t != 1) {
                        if ((float)x >= mX && (float)x <= mX + menuW &&
                            (float)y >= currY && (float)y <= currY + h)
                            clickIdx = itemIndex;
                        itemIndex++;
                    }
                    currY += h;
                }

                wd.isMenuOpen   = false;
                wd.hoverMenuIdx = -1;
                InvalidateRect(hWnd, NULL, TRUE);

                if (clickIdx != -1) {
                    if      (clickIdx == 1) AddTab(hWnd, L"LOCAL_NTP");
                    else if (clickIdx == 2) LaunchMiniBrowser(L"LOCAL_NTP", L"New Window");
                    else if (clickIdx == 3) {
                        // History
                        g_historyPanelOpen   = !g_historyPanelOpen;
                        g_bookmarkPanelOpen  = false;
                        g_downloadsPanelOpen = false;
                        if (g_historyPanelOpen) LoadHistory();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    else if (clickIdx == 4) {
                        // Downloads
                        g_downloadsPanelOpen = !g_downloadsPanelOpen;
                        g_historyPanelOpen   = false;
                        g_bookmarkPanelOpen  = false;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    else if (clickIdx == 5) {
                        // Bookmarks
                        g_bookmarkPanelOpen  = !g_bookmarkPanelOpen;
                        g_historyPanelOpen   = false;
                        g_downloadsPanelOpen = false;
                        if (g_bookmarkPanelOpen) LoadBookmarks();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    else if (clickIdx == 6) {
                        // Extensions Panel
                        g_extensionPanelOpen = !g_extensionPanelOpen;
                        g_historyPanelOpen   = false;
                        g_bookmarkPanelOpen  = false;
                        g_downloadsPanelOpen = false;
                        if (g_extensionPanelOpen) ScanExtensionsFolderPublic();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    else if (clickIdx == 7) AddTab(hWnd, L"https://gemini.google.com/app");
                    else if (clickIdx == 8) {
                        // Settings — WebView2 এ settings page খোলো
                        if (auto* tab = wd.active()) {
                            if (tab->webview) {
                                tab->webview->NavigateToString(
                                    GetSettingsPageHTML(wd.isDarkMode).c_str());
                            }
                        }
                    }
                    else if (clickIdx == 9) DestroyWindow(hWnd);
                    
                    return 0;
                }
            }

            {
                int tc = (int)wd.tabs.size();
                for (int i = 0; i < tc; i++) {
                    RECT tr = GetTabRect(W, i, tc, dpi);
                    if (x>=tr.left&&x<tr.right&&y>=tr.top&&y<tr.bottom) {
                        if (x >= tr.right - S(26, dpi)) { CloseTab(hWnd, i); return 0; }
                        SwitchToTab(hWnd, i);
                        return 0;
                    }
                }
            }

            if (wd.hNewTab) { AddTab(hWnd, L"LOCAL_NTP"); break; }

            if (auto* tab = wd.active()) {
                if (wd.hBack && tab->webview && tab->canBack) tab->webview->GoBack();
                if (wd.hFwd  && tab->webview && tab->canFwd)  tab->webview->GoForward();
                if (wd.hRel  && tab->webview)                 tab->webview->Reload();
            }

            if (wd.hProfile) MessageBoxW(hWnd, L"Profile menu will appear here.", L"Profile", MB_OK|MB_ICONINFORMATION);
            if (wd.hExt) {
                g_extensionPanelOpen = !g_extensionPanelOpen;
                g_historyPanelOpen   = false;
                g_bookmarkPanelOpen  = false;
                g_downloadsPanelOpen = false;
                if (g_extensionPanelOpen) ScanExtensionsFolderPublic();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            
            if (wd.hMenu) { 
                wd.isMenuOpen = !wd.isMenuOpen;
                InvalidateRect(hWnd, NULL, TRUE);
                return 0;
            }
        }

        if (wd.hFocus) {
            ToggleFocusMode(hWnd);
        }

        if (wd.hPin) { 
            wd.isPinned = !wd.isPinned;
            SetWindowPos(hWnd, wd.isPinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        
        if (wd.hDark) {
            wd.isDarkMode = !wd.isDarkMode; 
            if (wd.active() && wd.active()->controller) {
                ComPtr<ICoreWebView2Controller2> ctl2;
                if (SUCCEEDED(wd.active()->controller->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
                    COREWEBVIEW2_COLOR bg = wd.isDarkMode
                        ? COREWEBVIEW2_COLOR{255, 30, 30, 30}
                        : COREWEBVIEW2_COLOR{255, 255, 255, 255};
                    ctl2->put_DefaultBackgroundColor(bg);
                }
                
                std::wstring url = wd.active()->url;
                if ((url == L"LOCAL_NTP" || url == L"about:blank") && wd.active()->webview) {
                    wd.active()->webview->NavigateToString(GetLocalNTP_HTML(wd.isDarkMode).c_str());
                } else if (url.find(L"blocked by rasfocus") != std::wstring::npos && wd.active()->webview) {
                    wd.active()->webview->NavigateToString(GetBlocked_HTML(wd.isDarkMode).c_str());
                }
            }
            InvalidateRect(hWnd, NULL, TRUE);
            InvalidateRect(wd.hAddressBar, NULL, TRUE);
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        if (!g_windows.count(hWnd)) break;
        if (g_isPureViewerMode) break; 
        UINT dpi = GetWndDpi(hWnd);
        int y = GET_Y_LPARAM(lParam);
        int x = GET_X_LPARAM(lParam);
        if (y < TitleBarH(dpi) && x > LogoW(dpi)) {
            AddTab(hWnd, L"LOCAL_NTP");
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        UINT dpi = GetWndDpi(hWnd);
        auto* mm = (LPMINMAXINFO)lParam;
        mm->ptMinTrackSize.x = S(380, dpi);
        mm->ptMinTrackSize.y = S(260, dpi);

        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfo(hMonitor, &mi)) {
            mm->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mm->ptMaxPosition.y = mi.rcWork.top  - mi.rcMonitor.top;
            mm->ptMaxSize.x     = mi.rcWork.right  - mi.rcWork.left;
            mm->ptMaxSize.y     = (mi.rcWork.bottom - mi.rcWork.top) - 2; 
        }
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // KEYBOARD SHORTCUTS
    // ─────────────────────────────────────────────────────────────────────────
    case WM_KEYDOWN: {
        if (!g_windows.count(hWnd)) break;
        auto& wd = g_windows[hWnd];
        bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;

        // Find bar open থাকলে input handle করো
        if (g_findBarOpen) {
            if (wParam == VK_ESCAPE) { CloseFindBar(); InvalidateRect(hWnd, NULL, FALSE); return 0; }
            if (wParam == VK_BACK)   { FindBarBackspace(); if (auto* t = wd.active()) if (t->webview) ExecuteFind(t->webview.Get()); InvalidateRect(hWnd, NULL, FALSE); return 0; }
            if (wParam == VK_RETURN) { if (auto* t = wd.active()) if (t->webview) ExecuteFind(t->webview.Get(), !shift); return 0; }
        }

        if (wParam == VK_ESCAPE) {
            // Focus mode বন্ধ করো সবার আগে
            if (g_windows.count(hWnd) && g_windows[hWnd].isFocusMode) {
                ToggleFocusMode(hWnd);
                return 0;
            }
            // সব panel বন্ধ করো
            bool any = g_bookmarkPanelOpen || g_historyPanelOpen ||
                       g_downloadsPanelOpen || g_findBarOpen ||
                       g_contextMenuOpen   || g_extensionPanelOpen;
            g_bookmarkPanelOpen  = g_historyPanelOpen = g_downloadsPanelOpen = false;
            g_extensionPanelOpen = false;
            g_findBarOpen        = false;
            g_contextMenuOpen    = false;
            if (any) { InvalidateRect(hWnd, NULL, FALSE); return 0; }
        }

        if (ctrl) {
            switch (wParam) {
            case 'T':  // Ctrl+T — New Tab
                AddTab(hWnd, L"LOCAL_NTP"); return 0;

            case 'W':  // Ctrl+W — Close Tab
                if (wd.tabs.size() > 1) CloseTab(hWnd, wd.activeTab);
                else DestroyWindow(hWnd);
                return 0;

            case 'N':  // Ctrl+N — New Window
                LaunchMiniBrowser(L"LOCAL_NTP", L"New Window"); return 0;

            case 'H':  // Ctrl+H — History
                g_historyPanelOpen   = !g_historyPanelOpen;
                g_bookmarkPanelOpen  = false;
                g_downloadsPanelOpen = false;
                if (g_historyPanelOpen) LoadHistory();
                InvalidateRect(hWnd, NULL, FALSE); return 0;

            case 'J':  // Ctrl+J — Downloads
                g_downloadsPanelOpen = !g_downloadsPanelOpen;
                g_historyPanelOpen   = false;
                g_bookmarkPanelOpen  = false;
                InvalidateRect(hWnd, NULL, FALSE); return 0;

            case 'D':  // Ctrl+D — Bookmark current page
                if (auto* tab = wd.active()) {
                    ToggleBookmark(tab->url, tab->title);
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;

            case 'B':  // Ctrl+B — Bookmarks Panel
                g_bookmarkPanelOpen  = !g_bookmarkPanelOpen;
                g_historyPanelOpen   = false;
                g_downloadsPanelOpen = false;
                g_extensionPanelOpen = false;
                if (g_bookmarkPanelOpen) LoadBookmarks();
                InvalidateRect(hWnd, NULL, FALSE); return 0;

            case 'E':  // Ctrl+E — Extensions Panel
                g_extensionPanelOpen = !g_extensionPanelOpen;
                g_historyPanelOpen   = false;
                g_bookmarkPanelOpen  = false;
                g_downloadsPanelOpen = false;
                if (g_extensionPanelOpen) ScanExtensionsFolderPublic();
                InvalidateRect(hWnd, NULL, FALSE); return 0;

            case 'F':  // Ctrl+F — Find in Page
                if (g_findBarOpen) CloseFindBar();
                else OpenFindBar();
                InvalidateRect(hWnd, NULL, FALSE); return 0;

            case 'R':  // Ctrl+R — Reload
                if (auto* tab = wd.active())
                    if (tab->webview) tab->webview->Reload();
                return 0;

            case VK_TAB:  // Ctrl+Tab — Next Tab
                if (!wd.tabs.empty()) {
                    int next = (wd.activeTab + (shift ? -1 : 1) + (int)wd.tabs.size()) % (int)wd.tabs.size();
                    SwitchToTab(hWnd, next);
                }
                return 0;

            default:
                // Ctrl+1~9 — Switch Tab
                if (wParam >= '1' && wParam <= '9') {
                    int idx = (int)(wParam - '1');
                    if (idx < (int)wd.tabs.size()) SwitchToTab(hWnd, idx);
                    return 0;
                }
                break;
            }
        }

        if (wParam == VK_F5) {  // F5 — Reload
            if (auto* tab = wd.active())
                if (tab->webview) tab->webview->Reload();
            return 0;
        }
        break;
    }

    case WM_CHAR: {
        // Find bar text input
        if (g_findBarOpen && !g_windows.empty()) {
            wchar_t ch = (wchar_t)wParam;
            if (ch >= L' ' && ch != VK_BACK) {
                FindBarAddChar(ch);
                if (g_windows.count(hWnd)) {
                    auto& wd2 = g_windows[hWnd];
                    if (auto* tab = wd2.active())
                        if (tab->webview) ExecuteFind(tab->webview.Get());
                }
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        break;
    }

    case WM_RBUTTONDOWN: {
        if (!g_windows.count(hWnd)) break;
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        // WebView এর উপরে right-click হলে context menu খোলো
        if (my > NavTotalH(hWnd)) {
            OpenContextMenu(mx, my, false, false, false, L"");
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -3 : 3;
        if (g_historyPanelOpen) {
            HandleHistoryScroll(delta);
            InvalidateRect(hWnd, NULL, FALSE);
        } else if (g_bookmarkPanelOpen || g_downloadsPanelOpen) {
            // panels handle their own scroll via redraw
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY: {
        if (g_windows.count(hWnd)) {
            for (auto& tab : g_windows[hWnd].tabs)
                if (tab.controller) tab.controller->Close();
            if (g_windows[hWnd].hAddrFont)
                DeleteObject(g_windows[hWnd].hAddrFont);
            g_windows.erase(hWnd);
        }
        if (g_isPureViewerMode && g_windows.empty())
            PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// PUBLIC API — LaunchMiniBrowser()
// ─────────────────────────────────────────────────────────────────────────────
void LaunchMiniBrowser(std::wstring url, std::wstring /*title*/) {
    static ULONG_PTR gdiplusToken = 0;
    if (!gdiplusToken) {
        GdiplusStartupInput si;
        GdiplusStartup(&gdiplusToken, &si, nullptr);
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CreateDesktopShortcut();
    RegisterAppForDefaultBrowser();

    // ── Browser Data Init (প্রথমবার call এ) ──
    static bool dataLoaded = false;
    if (!dataLoaded) {
        LoadBookmarks();
        LoadSettings();
        dataLoaded = true;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ViewerWndProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.lpszClassName = L"RasBrowserWnd";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassExW(&wc);
        registered = true;
    }

    HWND hWnd = CreateWindowExW(
        0, L"RasBrowserWnd", L"RasBrowser",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU |
        WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 780,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!hWnd) return;

    SetWindowLongW(hWnd, GWL_STYLE, GetWindowLongW(hWnd, GWL_STYLE) & ~WS_CAPTION);
    ApplyDwmShadow(hWnd);

    auto& wd = g_windows[hWnd];

    HWND hEdit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
        0, 0, 100, 26, hWnd, (HMENU)IDC_ADDRESS_BAR, GetModuleHandle(NULL), NULL);

    SetWindowLongW(hEdit, GWL_STYLE, GetWindowLongW(hEdit, GWL_STYLE) & ~WS_BORDER);
    SetWindowTheme(hEdit, L"", L"");
    SetWindowSubclass(hEdit, AddrBarProc, 1, 0);
    wd.hAddressBar = hEdit;

    HICON hIco = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    if (hIco) {
        SendMessage(hWnd, WM_SETICON, ICON_BIG,   (LPARAM)hIco);
        SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
    }

    TabData firstTab;
    if (!g_isPureViewerMode && (url.empty() || url == L"RAS_BROWSER" || url == L"about:blank")) {
        url = L"LOCAL_NTP"; 
    }
    
    firstTab.url   = url;
    firstTab.title = L"New Tab";
    wd.tabs.push_back(firstTab);
    wd.activeTab = 0;

    ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hWnd);

    RepositionAddressBar(hWnd);
    CreateWebViewForTab(hWnd, 0);
}
// COMPLETE FILE END ───────────────────────────────────────────────────────────