// mini_browser.cpp — RasBrowser | Fast & Secure Theme, Smart Omnibox, Dark Mode
// REFACTORED: Per-Monitor v2 DPI, Chrome bezier tabs, double-buffering,
//             Smart Google Search Omnibox, Dark Mode Toggle, App Branding.
// ADDED: Instant Local NTP (No White Flash), AI Mode Button UI, Taskbar 2px Fix.

#define _CRT_SECURE_NO_WARNINGS
#define WINVER       0x0A00
#define _WIN32_WINNT 0x0A00
#define GDIPVER      0x0110

#include "mini_browser.h"
#include "html_tools.h"
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <wrl.h>
#include <shlobj.h>   // For Desktop Shortcut
#include <shlwapi.h>  // For Desktop Shortcut

#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <sstream>
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
// EXTERNAL GLOBALS  (defined in main translation unit)
// ─────────────────────────────────────────────────────────────────────────────
extern bool  g_isPureViewerMode;
extern float g_scaleFactor;

// ─────────────────────────────────────────────────────────────────────────────
// RESOURCE IDs
// ─────────────────────────────────────────────────────────────────────────────
#define IDI_APP_ICON    101
#define IDC_ADDRESS_BAR 1005

// ─────────────────────────────────────────────────────────────────────────────
// LOCAL NTP (INSTANT GOOGLE HOMEPAGE)
// ─────────────────────────────────────────────────────────────────────────────
const std::wstring LOCAL_NTP_HTML = L"<!DOCTYPE html>"
L"<html><head><meta charset='utf-8'><title>New Tab</title><style>"
L"body { margin:0; padding:0; display:flex; flex-direction:column; justify-content:center; align-items:center; height:100vh; background-color:#202124; font-family:'Segoe UI',Roboto,sans-serif; }"
L"@media (prefers-color-scheme: light) { body { background-color:#ffffff; } }"
L".logo { font-size:85px; font-weight:bold; color:#fff; margin-bottom:30px; letter-spacing:-3px; user-select:none; }"
L"@media (prefers-color-scheme: light) { .logo { color:#202124; } }"
L".logo span:nth-child(1){color:#4285F4;} .logo span:nth-child(2){color:#EA4335;} .logo span:nth-child(3){color:#FBBC05;} .logo span:nth-child(4){color:#4285F4;} .logo span:nth-child(5){color:#34A853;} .logo span:nth-child(6){color:#EA4335;}"
L"form { width: 100%; max-width: 600px; display:flex; justify-content:center; position:relative; }"
L".search-box { width:100%; padding:16px 24px 16px 50px; font-size:16px; border-radius:30px; border:1px solid #5f6368; background:#202124; color:#fff; outline:none; transition:all 0.2s; box-shadow:0 1px 3px rgba(0,0,0,0.2); }"
L".search-box:hover { background:#303134; box-shadow:0 1px 6px rgba(0,0,0,0.3); border-color:#5f6368; }"
L".search-box:focus { background:#303134; box-shadow:0 1px 6px rgba(0,0,0,0.3); border-color:#5f6368; }"
L"@media (prefers-color-scheme: light) { .search-box { background:#fff; border-color:#dfe1e5; color:#000; } .search-box:hover, .search-box:focus { background:#fff; box-shadow:0 1px 6px rgba(32,33,36,0.28); border-color:transparent; } }"
L".search-icon { position:absolute; left:20px; top:50%; transform:translateY(-50%); width:20px; height:20px; fill:#9aa0a6; pointer-events:none; }"
L"</style></head><body>"
L"<div class='logo'><span>G</span><span>o</span><span>o</span><span>g</span><span>l</span><span>e</span></div>"
L"<form action='https://www.google.com/search' method='GET'>"
L"<svg class='search-icon' focusable='false' xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path d='M15.5 14h-.79l-.28-.27A6.471 6.471 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z'></path></svg>"
L"<input type='text' name='q' class='search-box' placeholder='Search Google or type a URL' autocomplete='off' autofocus />"
L"</form></body></html>";

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
// LAYOUT CONSTANTS  (Unified Title/Tab bar)
// ─────────────────────────────────────────────────────────────────────────────
static const int D_TITLEBAR_H  = 42; 
static const int D_TABBAR_H    = 0;  
static const int D_TOOLBAR_H   = 44;
static const int D_NAV_TOTAL_H = D_TITLEBAR_H + D_TABBAR_H + D_TOOLBAR_H;

static const int D_TAB_W_MAX   = 220;
static const int D_TAB_W_MIN   = 80;
static const int D_TAB_PAD     = 10;
static const int D_WIN_BTN_W   = 46;
static const int D_LOGO_W      = 260; 
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
};

// ─────────────────────────────────────────────────────────────────────────────
// PER-WINDOW DATA
// ─────────────────────────────────────────────────────────────────────────────
struct BrowserWindowData {
    std::vector<TabData> tabs;
    int                  activeTab    = 0;
    bool                 isFullScreen = false;
    bool                 isDarkMode   = true; // 🟢 Default Dark Mode
    WINDOWPLACEMENT      wpPrev       = { sizeof(WINDOWPLACEMENT) };
    HWND                 hAddressBar  = NULL;
    HFONT                hAddrFont    = NULL;

    bool hMin = false, hMax = false, hClose = false;
    bool hBack = false, hFwd = false, hRel = false;
    bool hPin = false, hDark = false, hExt = false, hDl = false, hSet = false; 
    int  hoverTabIndex = -1;
    bool hNewTab       = false;

    TabData* active() {
        if (activeTab >= 0 && activeTab < (int)tabs.size()) return &tabs[activeTab];
        return nullptr;
    }
};

static std::map<HWND, BrowserWindowData> g_windows;
static ComPtr<ICoreWebView2Environment>  g_sharedEnv;

// ─────────────────────────────────────────────────────────────────────────────
// URL ENCODER FOR SMART GOOGLE SEARCH
// ─────────────────────────────────────────────────────────────────────────────
static std::string utf8_encode(const std::wstring &wstr) {
    if(wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

static std::wstring UrlEncode(const std::wstring& text) {
    std::string utf8 = utf8_encode(text);
    std::wstringstream escaped;
    for (char c : utf8) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << (wchar_t)c;
        } else if (c == ' ') {
            escaped << L"+";
        } else {
            wchar_t buf[10];
            swprintf(buf, 10, L"%%%02X", (unsigned char)c);
            escaped << buf;
        }
    }
    return escaped.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// AUTO SHORTCUT & DEFAULT BROWSER REGISTRY
// ─────────────────────────────────────────────────────────────────────────────
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

static void RegisterAppForDefaultBrowser() {
    // Hidden implementation
}

// ─────────────────────────────────────────────────────────────────────────────
// ADULT / BLOCKED CONTENT FILTER
// ─────────────────────────────────────────────────────────────────────────────
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
static int NavTotalH(UINT dpi)  { return S(D_NAV_TOTAL_H,  dpi); }
static int TitleBarH(UINT dpi)  { return S(D_TITLEBAR_H,   dpi); }
static int ToolbarH (UINT dpi)  { return S(D_TOOLBAR_H,    dpi); }
static int WinBtnW  (UINT dpi)  { return S(D_WIN_BTN_W,    dpi); }
static int LogoW    (UINT dpi)  { return S(D_LOGO_W,       dpi); } 

static int CalcTabWidth(int windowW, int tabCount, UINT dpi) {
    int winBtnArea = WinBtnW(dpi) * 3;
    int available  = windowW - winBtnArea - LogoW(dpi) - S(D_NEW_TAB_BTN + 8, dpi);
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
    UINT dpi = GetWndDpi(hWnd);
    b.top += NavTotalH(dpi);
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR POSITIONING (UNIQUE UI WITH AI MODE)
// ─────────────────────────────────────────────────────────────────────────────
static void RepositionAddressBar(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (!wd.hAddressBar) return;

    UINT dpi = GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd, &cr);
    int W = cr.right;

    if (wd.isFullScreen) {
        ShowWindow(wd.hAddressBar, SW_HIDE);
        return;
    }

    int navBtnArea    = S(8 + 36*3 + 8, dpi);
    int rightIconArea = S(36*5 + 12,    dpi); 
    int addrH         = S(34,           dpi); // Slightly taller for unique design
    int toolY         = TitleBarH(dpi);
    int addrY         = toolY + (ToolbarH(dpi) - addrH) / 2;
    int addrX         = navBtnArea;
    int addrW         = W - navBtnArea - rightIconArea - S(8, dpi);

    // Make room for 'G' icon on the left (35px) and 'AI Mode' button on the right (100px)
    int leftDecorW  = S(35, dpi);
    int rightDecorW = S(95, dpi);

    ShowWindow(wd.hAddressBar, SW_SHOW);
    SetWindowPos(wd.hAddressBar, NULL,
        addrX + leftDecorW, addrY + S(4,dpi), addrW - leftDecorW - rightDecorW, addrH - S(8,dpi),
        SWP_NOZORDER | SWP_NOACTIVATE);

    if (wd.hAddrFont) DeleteObject(wd.hAddrFont);
    wd.hAddrFont = CreateFontW(
        S(14, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessage(wd.hAddressBar, WM_SETFONT, (WPARAM)wd.hAddrFont, TRUE);
}

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
                (mi.rcMonitor.bottom - mi.rcMonitor.top) - 2, // 🟢 Taskbar 2px Fix
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
            if (vk == VK_ESCAPE && g_windows.count(m_hWnd) && g_windows[m_hWnd].isFullScreen)
            { ToggleFullScreen(m_hWnd); args->put_Handled(TRUE); }
        }
        return S_OK;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR SUBCLASS  (Smart Google Search Chrome Logic)
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

        if (input.empty()) return 0; // Empty input, do nothing
        if (IsBlockedContent(input)) { SetWindowTextW(hWnd, L""); return 0; }

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
    path.AddArc(x+w-r*2,   y+h-r*2,   r*2, r*2,  0, 90);
    path.AddArc(x,         y+h-r*2,   r*2, r*2, 90, 90);
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
// MAIN DRAW FUNCTION  (Dynamic Dark/Light Theme & Unique Omnibox Branding)
// ─────────────────────────────────────────────────────────────────────────────
static void DrawBrowserContent(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (wd.isFullScreen) return;

    UINT dpi = GetWndDpi(hWnd);
    RECT cr;  GetClientRect(hWnd, &cr);
    int W = cr.right;

    int titleH  = TitleBarH(dpi);
    int toolH   = ToolbarH(dpi);
    int navH    = NavTotalH(dpi);
    int winBtnW = WinBtnW(dpi);

    // 🟢 Dynamic Theme Colors
    Color cBgFrame   = wd.isDarkMode ? Color(255, 32, 33, 36)   : Color(255, 222, 225, 230);
    Color cBgTool    = wd.isDarkMode ? Color(255, 53, 54, 58)   : Color(255, 255, 255, 255);
    Color cTxtPrim   = wd.isDarkMode ? Color(255, 240, 240, 240): Color(255, 32, 33, 36);
    Color cTxtDim    = wd.isDarkMode ? Color(255, 154, 156, 160): Color(255, 95, 99, 104);
    Color cTabActive = wd.isDarkMode ? Color(255, 53, 54, 58)   : Color(255, 255, 255, 255);
    Color cTabHover  = wd.isDarkMode ? Color(255, 60, 64, 67)   : Color(255, 235, 236, 240);
    Color cAddrBg    = wd.isDarkMode ? Color(255, 32, 33, 36)   : Color(255, 241, 243, 244);
    Color cAddrBord  = wd.isDarkMode ? Color(255, 90, 94, 97)   : Color(255, 160, 180, 210); // Custom border
    Color cDivLine   = wd.isDarkMode ? Color(255, 60, 64, 67)   : Color(255, 218, 220, 224);

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // ── Background strips ──────────────────────────────────────────────────
    {
        SolidBrush bFrame(cBgFrame);
        SolidBrush bTool (cBgTool);
        g.FillRectangle(&bFrame, 0, 0, W, titleH);
        g.FillRectangle(&bTool,  0, titleH, W, toolH);

        Pen sepPen(cDivLine, 1.0f);
        g.DrawLine(&sepPen, 0, navH - 1, W, navH - 1);
    }

    FontFamily ffSeg(L"Segoe UI");
    FontFamily ffMDL(L"Segoe MDL2 Assets");
    Font fSmall  (&ffSeg, Sf(12.f, dpi), FontStyleRegular, UnitPixel);
    Font fSmallBd(&ffSeg, Sf(12.f, dpi), FontStyleBold,    UnitPixel);
    Font fBrand  (&ffSeg, Sf(15.f, dpi), FontStyleBold,    UnitPixel);
    Font fBrandSm(&ffSeg, Sf(11.f, dpi), FontStyleRegular, UnitPixel);
    Font fIcon   (&ffMDL, Sf(14.f, dpi), FontStyleRegular, UnitPixel);
    Font fIconSm (&ffMDL, Sf(11.f, dpi), FontStyleRegular, UnitPixel);

    StringFormat sfC, sfL;
    sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);
    sfL.SetAlignment(StringAlignmentNear);   sfL.SetLineAlignment(StringAlignmentCenter);

    SolidBrush brPrim(cTxtPrim);
    SolidBrush brDim (cTxtDim);

    // ── Title bar: Branding & App Icon ─────────────────────────────────────
    {
        int iconSz = S(20, dpi);
        int iconX  = S(12, dpi);
        int iconY  = (titleH - iconSz) / 2;
        
        HICON hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, iconSz, iconSz, LR_SHARED);
        if (hIcon) {
            DrawIconEx(hdc, iconX, iconY, hIcon, iconSz, iconSz, 0, NULL, DI_NORMAL);
        }

        g.DrawString(L"RasBrowser", -1, &fBrand, RectF((float)(iconX + iconSz + 8), 2.f, 200.f, (float)(titleH/2 + 5)), &sfL, &brPrim);
        
        SolidBrush brGreen(Color(255, 30, 185, 100)); // Success green text
        g.DrawString(L"Fast & Secure", -1, &fBrandSm, RectF((float)(iconX + iconSz + 8), (float)(titleH/2), 200.f, (float)(titleH/2)), &sfL, &brGreen);
    }

    // ── Window controls ────────────────────────────────────────────────────
    {
        int bx = W - winBtnW * 3;
        auto DrawWinBtn = [&](int x, bool hover, bool isClose, const wchar_t* ico) {
            if (hover) {
                SolidBrush hb(isClose ? Color(255, 232, 17, 35) : (wd.isDarkMode ? Color(50, 255,255,255) : Color(20, 0,0,0)));
                g.FillRectangle(&hb, x, 0, winBtnW, titleH);
            }
            SolidBrush txtClr(isClose && hover ? Color(255,255,255,255) : cTxtPrim);
            g.DrawString(ico, -1, &fIconSm, RectF((float)x, 0.f, (float)winBtnW, (float)titleH), &sfC, &txtClr);
        };

        DrawWinBtn(bx,             wd.hMin,   false, L"\xE921");
        DrawWinBtn(bx + winBtnW,   wd.hMax,   false, IsZoomed(hWnd) ? L"\xE923" : L"\xE922");
        DrawWinBtn(bx + winBtnW*2, wd.hClose, true,  L"\xE8BB");
    }

    // ── Tab strip ──────────────────────────────────────────────────────────
    {
        int tc = (int)wd.tabs.size();
        float cornerR = Sf(8.f, dpi);

        for (int i = 0; i < tc; i++) {
            RECT tr = GetTabRect(W, i, tc, dpi);
            float tx = (float)tr.left, ty = (float)tr.top, tw = (float)(tr.right - tr.left), th = (float)(tr.bottom - tr.top);
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
            SolidBrush fvBrush(isActive ? Color(255,26,115,232) : cTxtDim);
            g.FillEllipse(&fvBrush, iconX, iconY, iconSz, iconSz);

            const auto& tab = wd.tabs[i];
            SolidBrush tBrush(isActive ? cTxtPrim : cTxtDim);
            float titleX = iconX + iconSz + Sf(6.f, dpi);
            float closeW = Sf(24.f, dpi);
            float titleW = tw - (titleX - tx) - closeW;
            if (titleW > 0) {
                std::wstring displayTitle = tab.title;
                if (displayTitle == L"New Tab") displayTitle = L"Google"; // Sync title visually if NTP
                g.DrawString(displayTitle.c_str(), -1, &fSmall, RectF(titleX, ty, titleW, th), &sfL, &tBrush);
            }

            if (isActive || isHover) {
                float cSz = Sf(16.f, dpi);
                float cx = tx + tw - cSz - Sf(6.f, dpi);
                float cy = ty + (th - cSz) * 0.5f;
                if (isHover && !isActive) {
                    SolidBrush hbx(Color(20,0,0,0));
                    g.FillEllipse(&hbx, cx, cy, cSz, cSz);
                }
                g.DrawString(L"\xE8BB", -1, &fIconSm, RectF(cx, cy, cSz, cSz), &sfC, &brDim);
            }
            if (!isActive && i < tc - 1 && i+1 != wd.activeTab) {
                Pen divPen(cDivLine, 1.0f);
                float dx = tx + tw - 1.f;
                g.DrawLine(&divPen, dx, ty + Sf(8.f,dpi), dx, ty + th - Sf(8.f,dpi));
            }
        }

        RECT ntr = GetNewTabBtnRect(W, tc, dpi);
        if (wd.hNewTab) {
            SolidBrush hb(wd.isDarkMode ? Color(50,255,255,255) : Color(20,0,0,0));
            g.FillEllipse(&hb, (float)ntr.left, (float)ntr.top, (float)(ntr.right-ntr.left), (float)(ntr.bottom-ntr.top));
        }
        g.DrawString(L"\xE710", -1, &fIconSm, RectF((float)ntr.left, (float)ntr.top, (float)(ntr.right-ntr.left), (float)(ntr.bottom-ntr.top)), &sfC, &brDim);
    }

    // ── Toolbar ────────────────────────────────────────────────────────────
    {
        int toolY = titleH;
        int curX  = S(8, dpi);
        int btnSz = S(34, dpi);
        int btnStep = S(38, dpi);
        float btnHf = (float)toolH;

        auto DrawNavBtn = [&](bool hover, bool enabled, const wchar_t* ico, int& x) {
            if (hover && enabled) {
                SolidBrush hb(wd.isDarkMode ? Color(50,255,255,255) : Color(20,0,0,0));
                g.FillEllipse(&hb, (float)(x+S(4,dpi)), (float)(toolY+S(4,dpi)), (float)S(28,dpi), (float)S(28,dpi));
            }
            SolidBrush ic(enabled ? cTxtPrim : cDivLine);
            g.DrawString(ico, -1, &fIcon, RectF((float)x, (float)toolY, (float)btnSz, btnHf), &sfC, &ic);
            x += btnStep;
        };

        auto* atab = wd.active();
        bool canBack = atab && atab->canBack;
        bool canFwd  = atab && atab->canFwd;

        DrawNavBtn(wd.hBack, canBack, L"\xE72B", curX);
        DrawNavBtn(wd.hFwd,  canFwd,  L"\xE72A", curX);
        DrawNavBtn(wd.hRel,  true,    L"\xE72C", curX);

        // 🟢 UNIQUE OMNIBOX DESIGN
        {
            int addrX = curX + S(4,dpi);
            int rightIX = W - S(38*5 + 12, dpi); 
            int addrW = rightIX - addrX - S(8,dpi);
            int addrH = S(34, dpi);
            int addrY = toolY + (toolH - addrH) / 2;

            SolidBrush addrBg(cAddrBg);
            Pen addrPen(cAddrBord, 1.5f);
            GraphicsPath pill;
            AddRoundRect(pill, (float)addrX, (float)addrY, (float)addrW, (float)addrH, Sf(17.f, dpi));
            g.FillPath(&addrBg, &pill);
            g.DrawPath(&addrPen, &pill);

            // Left side 'G' icon for Google
            SolidBrush gBrush(wd.isDarkMode ? Color(255, 200, 200, 200) : Color(255, 80, 80, 80));
            g.DrawString(L"G", -1, &fBrand, RectF((float)addrX + Sf(12.f,dpi), (float)addrY, Sf(20.f,dpi), (float)addrH), &sfC, &gBrush);

            // Right side "AI Mode" Blue Button
            float aiW = Sf(85.f, dpi);
            float aiH = addrH - Sf(8.f, dpi);
            float aiX = addrX + addrW - aiW - Sf(4.f, dpi);
            float aiY = addrY + Sf(4.f, dpi);
            
            GraphicsPath aiPill;
            AddRoundRect(aiPill, aiX, aiY, aiW, aiH, Sf(12.f, dpi));
            SolidBrush aiBg(Color(255, 0, 102, 204)); // Deep Blue Theme
            g.FillPath(&aiBg, &aiPill);
            
            SolidBrush aiTxt(Color(255, 255, 255, 255));
            g.DrawString(L"\x2728 AI Mode", -1, &fSmallBd, RectF(aiX, aiY, aiW, aiH), &sfC, &aiTxt);
        }

        // 🟢 Right toolbar icons (Pin, Dark, Ext, Dl, Settings)
        int rx = W - S(38*5 + 8, dpi);
        auto DrawRightBtn = [&](bool hover, const wchar_t* ico, int x) {
            if (hover) {
                SolidBrush hb(wd.isDarkMode ? Color(50,255,255,255) : Color(20,0,0,0));
                g.FillEllipse(&hb, (float)(x+S(4,dpi)), (float)(toolY+S(4,dpi)), (float)S(28,dpi), (float)S(28,dpi));
            }
            g.DrawString(ico, -1, &fIcon, RectF((float)x, (float)toolY, (float)btnSz, btnHf), &sfC, &brPrim);
        };
        DrawRightBtn(wd.hPin,  L"\xE718", rx); rx += btnStep; // Pin Icon (Disabled Msg)
        DrawRightBtn(wd.hDark, wd.isDarkMode ? L"\xE708" : L"\xE706", rx); rx += btnStep; // Moon/Sun
        DrawRightBtn(wd.hExt,  L"\xE9D2", rx); rx += btnStep;
        DrawRightBtn(wd.hDl,   L"\xE896", rx); rx += btnStep;
        DrawRightBtn(wd.hSet,  L"\xE713", rx);
    }
}

void DrawBrowser(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    if (g_windows[hWnd].isFullScreen) return;

    DoubleBufferedPaint(hWnd, hdc, [&](HDC memDC, int W, int H) {
        bool dark = g_windows[hWnd].isDarkMode;
        HBRUSH hbg = CreateSolidBrush(dark ? RGB(32, 33, 36) : RGB(222, 225, 230)); 
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
// TAB MANAGEMENT
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
        // Only show URL if it's not the internal Local NTP
        if (tab.url == L"LOCAL_NTP") SetWindowTextW(wd.hAddressBar, L"");
        else SetWindowTextW(wd.hAddressBar, tab.url.c_str());
    }

    RepositionAddressBar(hWnd);
    InvalidateRect(hWnd, NULL, FALSE);
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
    InvalidateRect(hWnd, NULL, FALSE);
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
    TabControllerHandler(HWND h, int idx, std::wstring url) : m_hWnd(h), m_tabIdx(idx), m_startUrl(std::move(url)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { *ppv = this; return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
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

        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
            COREWEBVIEW2_COLOR bg = wd.isDarkMode ? COREWEBVIEW2_COLOR{255, 32, 33, 36} : COREWEBVIEW2_COLOR{255, 255, 255, 255};
            ctl2->put_DefaultBackgroundColor(bg);
        }

        ICoreWebView2Settings* settings = nullptr;
        tab.webview->get_Settings(&settings);
        ComPtr<ICoreWebView2Settings2> s2;
        if (settings && SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&s2)))) {
            s2->put_UserAgent(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");
        }

        tab.webview->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr; args->get_Uri(&uri);
                if (uri) {
                    if (IsBlockedContent(uri)) {
                        args->put_Cancel(TRUE);
                        if (g_windows.count(m_hWnd)) {
                            auto& w = g_windows[m_hWnd];
                            if (w.hAddressBar) SetWindowTextW(w.hAddressBar, L"");
                        }
                    }
                    CoTaskMemFree(uri);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE);
                LPWSTR uri = nullptr; args->get_Uri(&uri);
                if (uri) { sender->Navigate(uri); CoTaskMemFree(uri); }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_DocumentTitleChanged(Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx >= (int)w.tabs.size()) return S_OK;
                LPWSTR docTitle = nullptr;
                sender->get_DocumentTitle(&docTitle);
                if (docTitle) {
                    w.tabs[m_tabIdx].title = docTitle;
                    CoTaskMemFree(docTitle);
                    UINT dpi = GetWndDpi(m_hWnd);
                    RECT navR = { 0, 0, 32767, NavTotalH(dpi) };
                    InvalidateRect(m_hWnd, &navR, FALSE);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx != w.activeTab) return S_OK;
                LPWSTR src = nullptr; sender->get_Source(&src);
                if (src) {
                    w.tabs[m_tabIdx].url = src;
                    if (w.hAddressBar && w.tabs[m_tabIdx].url != L"LOCAL_NTP" && w.tabs[m_tabIdx].url != L"about:blank") 
                        SetWindowTextW(w.hAddressBar, src);
                    CoTaskMemFree(src);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_HistoryChanged(Callback<ICoreWebView2HistoryChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx >= (int)w.tabs.size()) return S_OK;
                BOOL canB = FALSE, canF = FALSE;
                sender->get_CanGoBack(&canB);
                sender->get_CanGoForward(&canF);
                w.tabs[m_tabIdx].canBack = !!canB;
                w.tabs[m_tabIdx].canFwd  = !!canF;
                UINT dpi = GetWndDpi(m_hWnd);
                RECT r = { 0, 0, 32767, NavTotalH(dpi) };
                InvalidateRect(m_hWnd, &r, FALSE);
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

        // 🟢 LOAD INSTANT GOOGLE PAGE (NO WHITE FLASH)
        if (m_startUrl == L"LOCAL_NTP") {
            tab.webview->NavigateToString(LOCAL_NTP_HTML.c_str());
        } else {
            tab.webview->Navigate(m_startUrl.c_str());
        }
        return S_OK;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// WEBVIEW2 ENVIRONMENT HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    HWND m_hWnd;
    int  m_tabIdx;
    ULONG m_ref = 1;

public:
    EnvCompletedHandler(HWND h, int idx) : m_hWnd(h), m_tabIdx(idx) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { *ppv = this; return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env) override {
        if (FAILED(hr) || !env) return S_OK;
        g_sharedEnv = env;
        if (!g_windows.count(m_hWnd)) return S_OK;
        auto& wd  = g_windows[m_hWnd];
        auto& tab = wd.tabs[m_tabIdx];
        g_sharedEnv->CreateCoreWebView2Controller(m_hWnd, new TabControllerHandler(m_hWnd, m_tabIdx, tab.url));
        return S_OK;
    }
};

static void CreateWebViewForTab(HWND hWnd, int tabIdx) {
    if (!g_windows.count(hWnd)) return;
    auto& wd  = g_windows[hWnd];
    auto& tab = wd.tabs[tabIdx];

    if (g_sharedEnv) {
        g_sharedEnv->CreateCoreWebView2Controller(hWnd, new TabControllerHandler(hWnd, tabIdx, tab.url));
    } else {
        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        options->put_AdditionalBrowserArguments(
            L"--enable-features=msWebView2EnableExtensions "
            L"--enable-gpu-rasterization "
            L"--enable-zero-copy "
            L"--disable-features=Translate");

        const wchar_t* udDir = L"C:\\ProgramData\\RasFocus\\.BrowserData";
        CreateDirectoryW(L"C:\\ProgramData\\RasFocus", NULL);
        CreateDirectoryW(udDir, NULL);
        SetFileAttributesW(udDir, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, udDir, options.Get(), new EnvCompletedHandler(hWnd, tabIdx));

        if (FAILED(hr)) {
            CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr, new EnvCompletedHandler(hWnd, tabIdx));
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
            UINT dpi = GetWndDpi(hWnd);
            int border = S(8, dpi);

            if (!g_windows.count(hWnd) || !g_windows[hWnd].isFullScreen) {
                if (pt.y < border && pt.x < border)                  return HTTOPLEFT;
                if (pt.y < border && pt.x >= cr.right-border)         return HTTOPRIGHT;
                if (pt.y >= cr.bottom-border && pt.x < border)        return HTBOTTOMLEFT;
                if (pt.y >= cr.bottom-border && pt.x >= cr.right-border) return HTBOTTOMRIGHT;
                if (pt.y < border)                return HTTOP;
                if (pt.y >= cr.bottom-border)     return HTBOTTOM;
                if (pt.x < border)                return HTLEFT;
                if (pt.x >= cr.right-border)      return HTRIGHT;

                if (pt.y < TitleBarH(dpi)) {
                    int winBtnX = cr.right - WinBtnW(dpi) * 3;
                    if (pt.x >= winBtnX) return HTCLIENT; 
                    
                    bool onTab = false;
                    auto& wd = g_windows[hWnd];
                    int tc = (int)wd.tabs.size();
                    for(int i=0; i<tc; i++) {
                        RECT tr = GetTabRect(cr.right, i, tc, dpi);
                        if(pt.x >= tr.left && pt.x < tr.right) { onTab = true; break; }
                    }
                    if (onTab || pt.x < LogoW(dpi)) return HTCLIENT; 
                    
                    RECT ntr = GetNewTabBtnRect(cr.right, tc, dpi);
                    if (pt.x >= ntr.left && pt.x <= ntr.right) return HTCLIENT; 

                    return HTCAPTION; 
                }
                if (pt.y < NavTotalH(dpi)) return HTCLIENT;
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
                SetBkColor  (hEdit, RGB(32, 33, 36));
                static HBRUSH hBrDark = CreateSolidBrush(RGB(32, 33, 36));
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
            if (wd.tabs[i].controller)
                if (i == wd.activeTab)
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
        int navH    = NavTotalH(dpi);
        int winBtnW = WinBtnW(dpi);
        int toolY   = titleH;

        {
            int bx = W - winBtnW*3;
            bool nm = (y < titleH && x >= bx           && x < bx + winBtnW);
            bool mx = (y < titleH && x >= bx + winBtnW && x < bx + winBtnW*2);
            bool cl = (y < titleH && x >= bx + winBtnW*2);
            if (wd.hMin!=nm||wd.hMax!=mx||wd.hClose!=cl)
                { wd.hMin=nm; wd.hMax=mx; wd.hClose=cl; dirty=true; }
        }

        {
            int tc = (int)wd.tabs.size();
            int prev = wd.hoverTabIndex; wd.hoverTabIndex = -1;
            for (int i = 0; i < tc; i++) {
                RECT tr = GetTabRect(W, i, tc, dpi);
                if (x >= tr.left && x < tr.right && y >= tr.top  && y < tr.bottom)
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
            int btnStep = S(38, dpi);
            int cx = S(8, dpi);
            bool b  = (y>=toolY&&y<navH&&x>=cx&&x<cx+S(36,dpi)); cx+=btnStep;
            bool f  = (y>=toolY&&y<navH&&x>=cx&&x<cx+S(36,dpi)); cx+=btnStep;
            bool rl = (y>=toolY&&y<navH&&x>=cx&&x<cx+S(36,dpi));
            if (wd.hBack!=b||wd.hFwd!=f||wd.hRel!=rl)
                { wd.hBack=b; wd.hFwd=f; wd.hRel=rl; dirty=true; }

            int rx = W - S(38*5+8, dpi); 
            bool p  = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(36,dpi)); rx+=btnStep; // Pin
            bool dk = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(36,dpi)); rx+=btnStep; // Dark
            bool e  = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(36,dpi)); rx+=btnStep;
            bool dl = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(36,dpi)); rx+=btnStep;
            bool st = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(36,dpi));
            if (wd.hPin!=p||wd.hDark!=dk||wd.hExt!=e||wd.hDl!=dl||wd.hSet!=st)
                { wd.hPin=p; wd.hDark=dk; wd.hExt=e; wd.hDl=dl; wd.hSet=st; dirty=true; }
        }

        if (dirty) {
            RECT r = { 0, 0, W, NavTotalH(dpi) };
            InvalidateRect(hWnd, &r, FALSE);
        }
        break;
    }

    case WM_MOUSELEAVE: {
        if (g_windows.count(hWnd)) {
            auto& wd = g_windows[hWnd];
            wd.hMin=wd.hMax=wd.hClose=false;
            wd.hBack=wd.hFwd=wd.hRel=false;
            wd.hPin=wd.hDark=wd.hExt=wd.hDl=wd.hSet=false;
            wd.hNewTab=false; wd.hoverTabIndex=-1;
            UINT dpi = GetWndDpi(hWnd);
            RECT cr; GetClientRect(hWnd, &cr);
            cr.bottom = NavTotalH(dpi);
            InvalidateRect(hWnd, &cr, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd];
        UINT dpi = GetWndDpi(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd, &cr); int W = cr.right;

        if (wd.hMin)   { ShowWindow(hWnd, SW_MINIMIZE); break; }
        if (wd.hMax)   { ShowWindow(hWnd, IsZoomed(hWnd)?SW_RESTORE:SW_MAXIMIZE); break; }
        if (wd.hClose) { DestroyWindow(hWnd); break; }

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

        // 🟢 Disabled Pin Action (No message)
        if (wd.hPin) { /* do nothing */ }
        
        if (wd.hDark) {
            wd.isDarkMode = !wd.isDarkMode; // Toggle Theme
            if (wd.active() && wd.active()->controller) {
                ComPtr<ICoreWebView2Controller2> ctl2;
                if (SUCCEEDED(wd.active()->controller->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
                    COREWEBVIEW2_COLOR bg = wd.isDarkMode ? COREWEBVIEW2_COLOR{255, 32, 33, 36} : COREWEBVIEW2_COLOR{255, 255, 255, 255};
                    ctl2->put_DefaultBackgroundColor(bg);
                }
            }
            InvalidateRect(hWnd, NULL, TRUE);
            InvalidateRect(wd.hAddressBar, NULL, TRUE);
        }
        if (wd.hExt) MessageBoxW(hWnd, L"Extensions menu will appear here.", L"Extensions", MB_OK|MB_ICONINFORMATION);
        if (wd.hDl)  MessageBoxW(hWnd, L"Downloads panel will appear here.",  L"Downloads",  MB_OK|MB_ICONINFORMATION);
        if (wd.hSet) ShellExecuteW(NULL, L"open", L"ms-settings:defaultapps", NULL, NULL, SW_SHOWNORMAL);
        break;
    }

    case WM_LBUTTONDBLCLK: {
        if (!g_windows.count(hWnd)) break;
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
        mm->ptMinTrackSize.x = S(640, dpi);
        mm->ptMinTrackSize.y = S(480, dpi);

        // 🟢 2px Taskbar Fix for mini_browser
        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfo(hMonitor, &mi)) {
            mm->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mm->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            mm->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mm->ptMaxSize.y = (mi.rcWork.bottom - mi.rcWork.top) - 2; 
        }
        return 0;
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

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ViewerWndProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.lpszClassName = L"RasBrowserWnd";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); // Prevents white flash
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

    // 🟢 Address bar initially EMPTY
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
    if (url.empty() || url == L"RAS_BROWSER") url = L"LOCAL_NTP"; 
    firstTab.url   = url;
    firstTab.title = L"New Tab";
    wd.tabs.push_back(firstTab);
    wd.activeTab = 0;

    ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hWnd);

    RepositionAddressBar(hWnd);
    CreateWebViewForTab(hWnd, 0);
}
