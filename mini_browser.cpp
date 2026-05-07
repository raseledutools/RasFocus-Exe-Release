// mini_browser.cpp — RasBrowser | Chrome-like tabbed browser using WebView2
// REFACTORED: Per-Monitor v2 DPI, Chrome bezier tabs, double-buffering,
//             styled Omnibox, robust WebView2 init, DWM drop shadow,
//             Pin on Top, Dark/Light Mode, Adult Blocker with Popup & SafeSearch.

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
// DPI HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static inline int S(int px, UINT dpi) { return MulDiv(px, (int)dpi, 96); }
static inline float Sf(float px, UINT dpi) { return px * (float)dpi / 96.0f; }
static UINT GetWndDpi(HWND hWnd) { UINT dpi = GetDpiForWindow(hWnd); return dpi ? dpi : 96; }

// ─────────────────────────────────────────────────────────────────────────────
// LAYOUT CONSTANTS (Increased for Chrome-like scale)
// ─────────────────────────────────────────────────────────────────────────────
static const int D_TITLEBAR_H  = 36;
static const int D_TABBAR_H    = 38;
static const int D_TOOLBAR_H   = 44;
static const int D_NAV_TOTAL_H = D_TITLEBAR_H + D_TABBAR_H + D_TOOLBAR_H;

static const int D_TAB_W_MAX   = 240;
static const int D_TAB_W_MIN   = 80;
static const int D_TAB_PAD     = 10;
static const int D_WIN_BTN_W   = 46;
static const int D_LOGO_W      = 160; // Space for Logo + Brand Name
static const int D_NEW_TAB_BTN = 28;

// ─────────────────────────────────────────────────────────────────────────────
// PER-TAB DATA
// ─────────────────────────────────────────────────────────────────────────────
struct TabData {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2>           webview;
    std::wstring title   = L"New Tab";
    std::wstring url     = L"https://www.google.com";
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
    bool                 isPinned     = false;
    bool                 isNightMode  = true; // True by default
    WINDOWPLACEMENT      wpPrev       = { sizeof(WINDOWPLACEMENT) };
    HWND                 hAddressBar  = NULL;
    HFONT                hAddrFont    = NULL;

    // Hover states
    bool hPin = false, hMin = false, hMax = false, hClose = false;
    bool hBack = false, hFwd = false, hRel = false;
    bool hExt  = false, hDl  = false, hNight = false, hSet = false;
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
// ADULT / BLOCKED CONTENT FILTER & POPUP
// ─────────────────────────────────────────────────────────────────────────────
bool IsBlockedContent(const std::wstring& text) {
    std::wstring lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    static const std::vector<std::wstring> kBadWords = {
        L"porn", L"xxx", L"sex", L"nude", L"nsfw", L"sexy", L"hentai", L"rule34",
        L"milf", L"blowjob", L"tits", L"boobs", L"pussy", L"dick", L"cock",
        L"escort", L"bdsm", L"fetish", L"erotica", L"dildo", L"webcam",
        L"camgirls", L"xvideos", L"pornhub", L"xnxx", L"xhamster", L"brazzers",
        L"onlyfans", L"playboy", L"chaturbate", L"stripchat", L"eporner",
        L"spankbang", L"redtube", L"youporn", L"mia khalifa", L"sunny leone",
        L"dani daniels", L"johnny sins", L"kendra lust",
        L"\u091A\u091F\u093F", L"\u092A\u0930\u094D\u0923", L"\u09B8\u09C7\u0995\u09CD\u09B8",
        L"\u09A8\u0997\u09CD\u09A8", L"\u0989\u09B2\u0999\u09CD\u0997", L"\u09AC\u09C7\u09B6\u09CD\u09AF\u09BE",
        L"\u09AE\u09BE\u0997\u09BF", L"\u0996\u09BE\u09A8\u0995\u09BF", L"magi", L"khanki"
    };

    for (const auto& kw : kBadWords)
        if (lower.find(kw) != std::wstring::npos)
            return true;
    return false;
}

void ShowAdultWarning(HWND hWnd) {
    MessageBoxW(hWnd, L"Don't waste your time, stay with Rasfocus.", L"Access Denied", MB_OK | MB_ICONWARNING);
}

// ─────────────────────────────────────────────────────────────────────────────
// GEOMETRY HELPERS 
// ─────────────────────────────────────────────────────────────────────────────
static int NavTotalH(UINT dpi)  { return S(D_NAV_TOTAL_H,  dpi); }
static int TitleBarH(UINT dpi)  { return S(D_TITLEBAR_H,   dpi); }
static int TabBarH  (UINT dpi)  { return S(D_TABBAR_H,     dpi); }
static int ToolbarH (UINT dpi)  { return S(D_TOOLBAR_H,    dpi); }
static int WinBtnW  (UINT dpi)  { return S(D_WIN_BTN_W,    dpi); }
static int LogoW    (UINT dpi)  { return S(D_LOGO_W,       dpi); }

static int CalcTabWidth(int windowW, int tabCount, UINT dpi) {
    int winBtnArea = WinBtnW(dpi) * 4; // 4 buttons now (Pin, Min, Max, Close)
    int available  = windowW - winBtnArea - LogoW(dpi) - S(D_NEW_TAB_BTN + 8, dpi);
    int w = (tabCount > 0) ? available / tabCount : S(D_TAB_W_MAX, dpi);
    return max(S(D_TAB_W_MIN, dpi), min(S(D_TAB_W_MAX, dpi), w));
}

static RECT GetTabRect(int windowW, int index, int tabCount, UINT dpi) {
    int tw = CalcTabWidth(windowW, tabCount, dpi);
    int x  = LogoW(dpi) + index * tw;
    RECT r = { x, TitleBarH(dpi), x + tw, TitleBarH(dpi) + TabBarH(dpi) };
    return r;
}

static RECT GetNewTabBtnRect(int windowW, int tabCount, UINT dpi) {
    int tw = CalcTabWidth(windowW, tabCount, dpi);
    int x  = LogoW(dpi) + tabCount * tw;
    int cy = TitleBarH(dpi) + TabBarH(dpi) / 2;
    int sz = S(20, dpi);
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
// ADDRESS BAR POSITIONING
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

    int navBtnArea    = S(8 + 36*3 + 4, dpi);
    int rightIconArea = S(36*4 + 12,    dpi); // 4 icons now (Ext, DL, Night, Set)
    int addrH         = S(30,           dpi); // Slightly taller pill shape
    int toolY         = TitleBarH(dpi) + TabBarH(dpi);
    int addrY         = toolY + (ToolbarH(dpi) - addrH) / 2;
    int addrX         = navBtnArea + S(4, dpi);
    int addrW         = W - navBtnArea - rightIconArea - S(8, dpi);

    ShowWindow(wd.hAddressBar, SW_SHOW);
    SetWindowPos(wd.hAddressBar, NULL,
        addrX, addrY, addrW, addrH,
        SWP_NOZORDER | SWP_NOACTIVATE);

    if (wd.hAddrFont) DeleteObject(wd.hAddrFont);
    wd.hAddrFont = CreateFontW(
        S(14, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessage(wd.hAddressBar, WM_SETFONT, (WPARAM)wd.hAddrFont, TRUE);
    
    // Update Text/Bg color dynamically based on Night mode
    InvalidateRect(wd.hAddressBar, NULL, TRUE); 
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
                mi.rcMonitor.bottom - mi.rcMonitor.top,
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
        if (wd.isPinned) SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
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

        // ADULT CHECK
        if (IsBlockedContent(input)) { 
            SetWindowTextW(hWnd, L""); 
            ShowAdultWarning(hParent);
            return 0; 
        }

        std::wstring url;
        if (input.find(L"http://")  == 0 || input.find(L"https://") == 0) {
            url = input;
            // Always append safe search for google
            if (url.find(L"google.") != std::wstring::npos && url.find(L"&safe=active") == std::wstring::npos) {
                if (url.find(L"?") != std::wstring::npos) url += L"&safe=active";
                else url += L"?safe=active";
            }
        } else if (input.find(L'.') != std::wstring::npos && input.find(L' ') == std::wstring::npos) {
            url = L"https://" + input;
        } else {
            // Force SafeSearch on Google queries
            url = L"https://www.google.com/search?q=" + input + L"&safe=active";
        }
        tab->webview->Navigate(url.c_str());
        return 0;
    }
    if (msg == WM_NCPAINT) return 0;
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

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

static void AddRoundRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    if (r <= 0.f) { path.AddRectangle(RectF(x,y,w,h)); return; }
    path.AddArc(x,         y,         r*2, r*2, 180, 90);
    path.AddArc(x+w-r*2,  y,         r*2, r*2, 270, 90);
    path.AddArc(x+w-r*2,  y+h-r*2,   r*2, r*2,  0, 90);
    path.AddArc(x,         y+h-r*2,   r*2, r*2,  90, 90);
    path.CloseFigure();
}

static void BuildChromeTabPath(GraphicsPath& path, float x, float y, float w, float h, float cornerR) {
    float bl = x, br = x + w, top = y + 4.f, bot = y + h;
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
// DYNAMIC COLOR PALETTE
// ─────────────────────────────────────────────────────────────────────────────
struct ThemeColors {
    Color BgFrame, BgTabStrip, BgToolbar, TabActive, TabHover, TabNormal;
    Color TxtPrim, TxtDim, AddrBg, BrandTeal, DivLine, CloseHov, TabCloseIc;
    Color White, WinHov, NavHov, TabHovCirc;
};

ThemeColors GetTheme(bool isNight) {
    ThemeColors c;
    c.BrandTeal = Color(255, 12, 168, 176); // Ras Teal
    c.White     = Color(255, 255, 255, 255);
    c.CloseHov  = Color(255, 231, 76, 60);

    if (isNight) {
        c.BgFrame    = Color(255,  30,  30,  30); 
        c.BgTabStrip = Color(255,  32,  32,  32); 
        c.BgToolbar  = Color(255,  41,  41,  41); 
        c.TabActive  = Color(255,  41,  41,  41); 
        c.TabHover   = Color(255,  55,  55,  55); 
        c.TabNormal  = Color(255,  32,  32,  32); 
        c.TxtPrim    = Color(255, 232, 232, 232);
        c.TxtDim     = Color(255, 138, 138, 138);
        c.AddrBg     = Color(255,  56,  56,  56);
        c.DivLine    = Color(255,  60,  60,  60);
        c.TabCloseIc = Color(255, 160, 160, 160);
        c.WinHov     = Color( 40, 255, 255, 255); 
        c.NavHov     = Color( 35, 255, 255, 255);
        c.TabHovCirc = Color( 50, 255, 255, 255);
    } else {
        c.BgFrame    = Color(255, 235, 242, 250); // Light blueish
        c.BgTabStrip = Color(255, 240, 244, 248);
        c.BgToolbar  = Color(255, 255, 255, 255); 
        c.TabActive  = Color(255, 255, 255, 255); 
        c.TabHover   = Color(255, 245, 248, 252); 
        c.TabNormal  = Color(255, 235, 242, 250); 
        c.TxtPrim    = Color(255,  40,  40,  40);
        c.TxtDim     = Color(255, 100, 100, 100);
        c.AddrBg     = Color(255, 240, 244, 248);
        c.DivLine    = Color(255, 220, 220, 220);
        c.TabCloseIc = Color(255, 100, 100, 100);
        c.WinHov     = Color( 40,   0,   0,   0); // Dark hover over light bg
        c.NavHov     = Color( 20,   0,   0,   0);
        c.TabHovCirc = Color( 20,   0,   0,   0);
    }
    return c;
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN DRAW FUNCTION 
// ─────────────────────────────────────────────────────────────────────────────
static void DrawBrowserContent(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (wd.isFullScreen) return;

    UINT dpi = GetWndDpi(hWnd);
    RECT cr;  GetClientRect(hWnd, &cr);
    int W = cr.right;

    int titleH  = TitleBarH(dpi);
    int tabH    = TabBarH(dpi);
    int toolH   = ToolbarH(dpi);
    int navH    = NavTotalH(dpi);
    int winBtnW = WinBtnW(dpi);

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    ThemeColors clr = GetTheme(wd.isNightMode);

    // Background strips
    {
        SolidBrush bFrame(clr.BgFrame), bStrip(clr.BgTabStrip), bTool(clr.BgToolbar);
        g.FillRectangle(&bFrame, 0, 0, W, titleH);
        g.FillRectangle(&bStrip, 0, titleH, W, tabH);
        g.FillRectangle(&bTool,  0, titleH + tabH, W, toolH);

        Pen sepPen(clr.DivLine, 1.0f);
        g.DrawLine(&sepPen, 0, navH - 1, W, navH - 1);
    }

    FontFamily ffSeg(L"Segoe UI"); FontFamily ffMDL(L"Segoe MDL2 Assets");
    Font fNormal(&ffSeg, Sf(13.f, dpi), FontStyleRegular, UnitPixel);
    Font fSmall (&ffSeg, Sf(12.f, dpi), FontStyleRegular, UnitPixel);
    Font fBrand (&ffSeg, Sf(14.f, dpi), FontStyleBold,    UnitPixel);
    Font fIcon  (&ffMDL, Sf(15.f, dpi), FontStyleRegular, UnitPixel);
    Font fIconSm(&ffMDL, Sf(12.f, dpi), FontStyleRegular, UnitPixel);

    StringFormat sfC, sfL;
    sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);
    sfL.SetAlignment(StringAlignmentNear);   sfL.SetLineAlignment(StringAlignmentCenter); sfL.SetFormatFlags(StringFormatFlagsNoWrap);

    SolidBrush brPrim(clr.TxtPrim), brDim(clr.TxtDim), brTeal(clr.BrandTeal), brWhite(clr.White);

    // Title bar: Brand & Logo
    {
        g.DrawString(L"\xE774", -1, &fIcon, RectF(Sf(15.f, dpi), 0.f, Sf(30.f, dpi), (float)titleH), &sfC, &brTeal);
        g.DrawString(L"RasBrowser", -1, &fBrand, RectF(Sf(45.f, dpi), 0.f, Sf(100.f, dpi), (float)titleH), &sfL, &brPrim);
    }

    // Window controls: Pin / Min / Max / Close
    {
        int bx = W - winBtnW * 4; // 4 buttons

        auto DrawWinBtn = [&](int x, bool hover, bool isClose, const wchar_t* ico, bool isActiveState = false) {
            if (hover) {
                SolidBrush hb(isClose ? clr.CloseHov : clr.WinHov);
                g.FillRectangle(&hb, x, 0, winBtnW, titleH);
            }
            SolidBrush txtClr(
                isClose && hover ? clr.White :
                isActiveState    ? clr.BrandTeal : 
                hover            ? clr.TxtPrim : clr.TxtDim);
            g.DrawString(ico, -1, &fIconSm, RectF((float)x, 0.f, (float)winBtnW, (float)titleH), &sfC, &txtClr);
        };

        DrawWinBtn(bx,                wd.hPin,   false, L"\xE718", wd.isPinned); 
        DrawWinBtn(bx + winBtnW,      wd.hMin,   false, L"\xE921");
        DrawWinBtn(bx + winBtnW*2,    wd.hMax,   false, IsZoomed(hWnd) ? L"\xE923" : L"\xE922");
        DrawWinBtn(bx + winBtnW*3,    wd.hClose, true,  L"\xE8BB");
    }

    // Tab strip
    {
        int tc   = (int)wd.tabs.size();
        float cornerR = Sf(6.f, dpi);

        for (int i = 0; i < tc; i++) {
            RECT tr   = GetTabRect(W, i, tc, dpi);
            float tx  = (float)tr.left, ty = (float)tr.top;
            float tw  = (float)(tr.right - tr.left), th = (float)(tr.bottom - tr.top);

            bool isActive = (i == wd.activeTab);
            bool isHover  = (i == wd.hoverTabIndex);

            GraphicsPath tabPath; BuildChromeTabPath(tabPath, tx, ty, tw, th, cornerR);
            SolidBrush bTab(isActive ? clr.TabActive : isHover ? clr.TabHover : clr.TabNormal);
            g.FillPath(&bTab, &tabPath);

            // Active accent
            if (isActive) {
                GraphicsPath accPath;
                AddRoundRect(accPath, tx + cornerR*3, ty + Sf(4.f,dpi), tw - cornerR*6, Sf(2.5f,dpi), Sf(1.25f,dpi));
                g.FillPath(&brTeal, &accPath);
            }

            float iconSz = Sf(14.f, dpi), iconX = tx + Sf((float)D_TAB_PAD + 2, dpi), iconY = ty + (th - iconSz) * 0.5f;
            SolidBrush fvBrush(isActive ? clr.BrandTeal : clr.TxtDim);
            g.FillEllipse(&fvBrush, iconX, iconY, iconSz, iconSz);

            const auto& tab = wd.tabs[i];
            SolidBrush tBrush(isActive ? clr.TxtPrim : clr.TxtDim);
            float titleX = iconX + iconSz + Sf(4.f, dpi), titleW = tw - (titleX - tx) - Sf(24.f, dpi);
            if (titleW > 0) g.DrawString(tab.title.c_str(), -1, &fSmall, RectF(titleX, ty, titleW, th), &sfL, &tBrush);

            if (isActive || isHover) {
                float cSz = Sf(16.f, dpi), cx = tx + tw - cSz - Sf(4.f, dpi), cy = ty + (th - cSz) * 0.5f;
                if (isHover && !isActive) {
                    SolidBrush hbx(clr.TabHovCirc); g.FillEllipse(&hbx, cx, cy, cSz, cSz);
                }
                SolidBrush xBrush(clr.TabCloseIc);
                g.DrawString(L"\xE8BB", -1, &fIconSm, RectF(cx, cy, cSz, cSz), &sfC, &xBrush);
            }

            if (!isActive && i < tc - 1 && i+1 != wd.activeTab) {
                Pen divPen(clr.DivLine, 1.0f);
                g.DrawLine(&divPen, tx+tw-1.f, (float)(titleH+S(8,dpi)), tx+tw-1.f, (float)(titleH+tabH-S(8,dpi)));
            }
        }

        RECT ntr = GetNewTabBtnRect(W, tc, dpi);
        if (wd.hNewTab) {
            SolidBrush hb(clr.WinHov);
            g.FillEllipse(&hb, (float)ntr.left, (float)ntr.top, (float)(ntr.right-ntr.left), (float)(ntr.bottom-ntr.top));
        }
        g.DrawString(L"\xE710", -1, &fIconSm, RectF((float)ntr.left, (float)ntr.top, (float)(ntr.right-ntr.left), (float)(ntr.bottom-ntr.top)), &sfC, &brDim);
    }

    // Toolbar
    {
        int toolY = titleH + tabH, curX = S(8, dpi), btnSz = S(34, dpi), btnStep = S(36, dpi);

        auto DrawNavBtn = [&](bool hover, bool enabled, const wchar_t* ico, int& x) {
            if (hover && enabled) {
                SolidBrush hb(clr.NavHov);
                g.FillEllipse(&hb, (float)(x+S(2,dpi)), (float)(toolY+S(5,dpi)), (float)S(30,dpi), (float)S(30,dpi));
            }
            SolidBrush ic(enabled ? clr.TxtPrim : clr.TxtDim);
            g.DrawString(ico, -1, &fIcon, RectF((float)x, (float)toolY, (float)btnSz, (float)toolH), &sfC, &ic);
            x += btnStep;
        };

        auto* atab = wd.active();
        DrawNavBtn(wd.hBack, atab && atab->canBack, L"\xE72B", curX);
        DrawNavBtn(wd.hFwd,  atab && atab->canFwd,  L"\xE72A", curX);
        DrawNavBtn(wd.hRel,  true,                  L"\xE72C", curX);

        // Address bar
        {
            int addrX = curX + S(4,dpi), rightIX = W - S(36*4 + 12, dpi), addrW = rightIX - addrX - S(4,dpi);
            int addrH = S(30, dpi), addrY = toolY + (toolH - addrH) / 2;
            
            SolidBrush addrBg(clr.AddrBg);
            GraphicsPath pill; AddRoundRect(pill, (float)addrX, (float)addrY, (float)addrW, (float)addrH, Sf(15.f, dpi));
            g.FillPath(&addrBg, &pill);

            SolidBrush lockBrush(clr.TxtDim);
            g.DrawString(L"\xE72E", -1, &fIconSm, RectF((float)addrX + Sf(6.f,dpi), (float)addrY, Sf(20.f,dpi), (float)addrH), &sfC, &lockBrush);
        }

        // Right toolbar icons
        int rx = W - S(36*4 + 12, dpi); // Ext, DL, Night, Set
        auto DrawRightBtn = [&](bool hover, const wchar_t* ico, int x, bool isActiveState = false) {
            if (hover) {
                SolidBrush hb(clr.NavHov);
                g.FillEllipse(&hb, (float)(x+S(2,dpi)), (float)(toolY+S(5,dpi)), (float)S(30,dpi), (float)S(30,dpi));
            }
            SolidBrush tCol(isActiveState ? clr.BrandTeal : clr.TxtPrim);
            g.DrawString(ico, -1, &fIcon, RectF((float)x, (float)toolY, (float)btnSz, (float)toolH), &sfC, &tCol);
        };
        
        DrawRightBtn(wd.hExt,   L"\xE9D2", rx); rx += btnStep;
        DrawRightBtn(wd.hDl,    L"\xE896", rx); rx += btnStep;
        DrawRightBtn(wd.hNight, wd.isNightMode ? L"\xE708" : L"\xE706", rx, !wd.isNightMode); rx += btnStep;
        DrawRightBtn(wd.hSet,   L"\xE713", rx);
    }
}

void DrawBrowser(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) return;
    DoubleBufferedPaint(hWnd, hdc, [&](HDC memDC, int W, int H) {
        ThemeColors clr = GetTheme(g_windows[hWnd].isNightMode);
        HBRUSH hbg = CreateSolidBrush(clr.BgFrame.ToCOLORREF());
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
    if (wd.activeTab != idx && wd.activeTab < (int)wd.tabs.size() && wd.tabs[wd.activeTab].controller)
        wd.tabs[wd.activeTab].controller->put_IsVisible(FALSE);

    wd.activeTab = idx;
    auto& tab = wd.tabs[idx];

    if (tab.controller) {
        tab.controller->put_IsVisible(TRUE);
        tab.controller->put_Bounds(GetWebViewRect(hWnd));
    } else {
        CreateWebViewForTab(hWnd, idx);
    }
    if (wd.hAddressBar) SetWindowTextW(wd.hAddressBar, tab.url.c_str());
    RepositionAddressBar(hWnd);
    InvalidateRect(hWnd, NULL, FALSE);
}

static void CloseTab(HWND hWnd, int idx) {
    auto& wd = g_windows[hWnd];
    if (wd.tabs.empty()) return;
    if (wd.tabs[idx].controller) {
        wd.tabs[idx].controller->put_IsVisible(FALSE);
        wd.tabs[idx].controller->Close();
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
}

// ─────────────────────────────────────────────────────────────────────────────
// WEBVIEW2 CONTROLLER COMPLETION HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class TabControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    HWND m_hWnd; int m_tabIdx; std::wstring m_startUrl; ULONG m_ref = 1;
public:
    TabControllerHandler(HWND h, int idx, std::wstring url) : m_hWnd(h), m_tabIdx(idx), m_startUrl(std::move(url)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { *ppv = this; return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return r; }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* ctl) override {
        if (FAILED(hr) || !ctl || !g_windows.count(m_hWnd)) return S_OK;
        auto& wd = g_windows[m_hWnd];
        if (m_tabIdx >= (int)wd.tabs.size()) return S_OK;

        auto& tab = wd.tabs[m_tabIdx];
        tab.controller = ctl;
        ctl->get_CoreWebView2(&tab.webview);

        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
            COREWEBVIEW2_COLOR bg = { 255, 30, 30, 30 }; // Dark default
            ctl2->put_DefaultBackgroundColor(bg);
        }

        ICoreWebView2Settings* settings = nullptr; tab.webview->get_Settings(&settings);
        ComPtr<ICoreWebView2Settings2> s2;
        if (settings && SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&s2)))) {
            s2->put_UserAgent(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/124.0.0.0 Safari/537.36");
        }

        tab.webview->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr; args->get_Uri(&uri);
                if (uri) {
                    if (IsBlockedContent(uri)) {
                        args->put_Cancel(TRUE);
                        if (g_windows.count(m_hWnd)) {
                            if (g_windows[m_hWnd].hAddressBar) SetWindowTextW(g_windows[m_hWnd].hAddressBar, L"");
                        }
                        ShowAdultWarning(m_hWnd);
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
                LPWSTR docTitle = nullptr; sender->get_DocumentTitle(&docTitle);
                if (docTitle) {
                    w.tabs[m_tabIdx].title = docTitle; CoTaskMemFree(docTitle);
                    RECT navR = { 0, 0, 32767, NavTotalH(GetWndDpi(m_hWnd)) };
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
                    if (w.hAddressBar) SetWindowTextW(w.hAddressBar, src);
                    CoTaskMemFree(src);
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_HistoryChanged(Callback<ICoreWebView2HistoryChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx >= (int)w.tabs.size()) return S_OK;
                BOOL canB, canF; sender->get_CanGoBack(&canB); sender->get_CanGoForward(&canF);
                w.tabs[m_tabIdx].canBack = !!canB; w.tabs[m_tabIdx].canFwd = !!canF;
                RECT r = { 0, 0, 32767, NavTotalH(GetWndDpi(m_hWnd)) }; InvalidateRect(m_hWnd, &r, FALSE);
                return S_OK;
            }).Get(), nullptr);

        ComPtr<ICoreWebView2Controller3> ctl3;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl3)))) {
            EventRegistrationToken tok;
            ctl3->add_AcceleratorKeyPressed(new AcceleratorHandler(m_hWnd), &tok);
        }

        ctl->put_IsVisible((m_tabIdx == wd.activeTab) ? TRUE : FALSE);
        ctl->put_Bounds(GetWebViewRect(m_hWnd));

        std::wstring nav = m_startUrl;
        if      (nav == L"RAS_BROWSER")            nav = L"https://www.google.com";
        else if (nav == L"LOCAL_PDF_SPLIT")        { tab.webview->NavigateToString(HTML_PDF_SPLIT.c_str());    return S_OK; }
        else if (nav == L"LOCAL_PDF_MERGE")        { tab.webview->NavigateToString(HTML_PDF_MERGE.c_str());    return S_OK; }
        else if (nav == L"LOCAL_IMG_TO_PDF")       { tab.webview->NavigateToString(HTML_IMG_TO_PDF.c_str());   return S_OK; }
        else if (nav == L"LOCAL_JOB_PHOTO")        { tab.webview->NavigateToString(HTML_JOB_PHOTO.c_str());    return S_OK; }
        else if (nav == L"LOCAL_JOB_SIGN")         { tab.webview->NavigateToString(HTML_JOB_SIGN.c_str());     return S_OK; }
        else if (nav == L"LOCAL_AGE_CALC")         { tab.webview->NavigateToString(HTML_AGE_CALC.c_str());     return S_OK; }
        else if (nav == L"LOCAL_COMPRESS_PDF")     { tab.webview->NavigateToString(HTML_COMPRESS_PDF.c_str()); return S_OK; }
        else if (nav == L"LOCAL_PHOTO_VIEWER")     { tab.webview->NavigateToString(HTML_PHOTO_VIEWER.c_str()); return S_OK; }

        tab.webview->Navigate(nav.c_str());
        return S_OK;
    }
};

class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    HWND m_hWnd; int m_tabIdx; ULONG m_ref = 1;
public:
    EnvCompletedHandler(HWND h, int idx) : m_hWnd(h), m_tabIdx(idx) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { *ppv = this; return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env) override {
        if (FAILED(hr) || !env || !g_windows.count(m_hWnd)) return S_OK;
        g_sharedEnv = env;
        g_sharedEnv->CreateCoreWebView2Controller(m_hWnd, new TabControllerHandler(m_hWnd, m_tabIdx, g_windows[m_hWnd].tabs[m_tabIdx].url));
        return S_OK;
    }
};

static void CreateWebViewForTab(HWND hWnd, int tabIdx) {
    if (!g_windows.count(hWnd)) return;
    if (g_sharedEnv) {
        g_sharedEnv->CreateCoreWebView2Controller(hWnd, new TabControllerHandler(hWnd, tabIdx, g_windows[hWnd].tabs[tabIdx].url));
    } else {
        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        options->put_AdditionalBrowserArguments(L"--enable-features=msWebView2EnableExtensions --enable-gpu-rasterization --enable-zero-copy --disable-features=Translate");
        const wchar_t* udDir = L"C:\\ProgramData\\RasFocus\\.BrowserData";
        CreateDirectoryW(L"C:\\ProgramData\\RasFocus", NULL); CreateDirectoryW(udDir, NULL);
        SetFileAttributesW(udDir, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

        if (FAILED(CreateCoreWebView2EnvironmentWithOptions(nullptr, udDir, options.Get(), new EnvCompletedHandler(hWnd, tabIdx)))) {
            CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr, new EnvCompletedHandler(hWnd, tabIdx));
        }
    }
}

static void ApplyDwmShadow(HWND hWnd) {
    MARGINS m = { 0, 0, 0, 1 }; DwmExtendFrameIntoClientArea(hWnd, &m);
    DWORD pref = DWMWCP_ROUND; DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    BOOL dark = TRUE; DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

// ─────────────────────────────────────────────────────────────────────────────
// WINDOW PROCEDURE
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0; break;

    case WM_NCHITTEST: {
        LRESULT def = DefWindowProcW(hWnd, msg, wParam, lParam);
        if (def == HTNOWHERE || def == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }; ScreenToClient(hWnd, &pt);
            RECT cr; GetClientRect(hWnd, &cr); UINT dpi = GetWndDpi(hWnd); int border = S(8, dpi);

            if (!g_windows.count(hWnd) || !g_windows[hWnd].isFullScreen) {
                if (pt.y < border && pt.x < border)                  return HTTOPLEFT;
                if (pt.y < border && pt.x >= cr.right-border)         return HTTOPRIGHT;
                if (pt.y >= cr.bottom-border && pt.x < border)        return HTBOTTOMLEFT;
                if (pt.y >= cr.bottom-border && pt.x >= cr.right-border) return HTBOTTOMRIGHT;
                if (pt.y < border)                return HTTOP;
                if (pt.y >= cr.bottom-border)     return HTBOTTOM;
                if (pt.x < border)                return HTLEFT;
                if (pt.x >= cr.right-border)      return HTRIGHT;

                int winBtnX = cr.right - WinBtnW(dpi) * 4; // Min/Max/Close + Pin
                if (pt.y < TitleBarH(dpi)) {
                    if (pt.x >= winBtnX) return HTCLIENT; 
                    if (pt.x < S(200, dpi)) return HTCLIENT;
                    return HTCAPTION;
                }
                if (pt.y < NavTotalH(dpi)) return HTCLIENT;
            }
            return HTCLIENT;
        }
        return def;
    }
    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION) { ShowWindow(hWnd, IsZoomed(hWnd) ? SW_RESTORE : SW_MAXIMIZE); return 0; } break;

    case WM_CREATE:
        ApplyDwmShadow(hWnd); break;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        DrawBrowser(hWnd, hdc);
        EndPaint(hWnd, &ps); return 0;
    }
    case WM_ERASEBKGND: return 1;

    case WM_WINDOWPOSCHANGING:
        ((WINDOWPOS*)lParam)->flags |= SWP_NOCOPYBITS; break;

    case WM_CTLCOLOREDIT: {
        if (g_windows.count(hWnd) && (HWND)lParam == g_windows[hWnd].hAddressBar) {
            bool isNight = g_windows[hWnd].isNightMode;
            HDC hEdit = (HDC)wParam;
            SetTextColor(hEdit, isNight ? RGB(232, 232, 232) : RGB(40, 40, 40));
            SetBkColor  (hEdit, isNight ? RGB(56, 56, 56) : RGB(240, 244, 248));
            static HBRUSH hBrDark = CreateSolidBrush(RGB(56, 56, 56));
            static HBRUSH hBrLight = CreateSolidBrush(RGB(240, 244, 248));
            return (LRESULT)(isNight ? hBrDark : hBrLight);
        }
        break;
    }

    case WM_SIZE: {
        if (!g_windows.count(hWnd)) break;
        RepositionAddressBar(hWnd);
        RECT wvr = GetWebViewRect(hWnd);
        for (int i = 0; i < (int)g_windows[hWnd].tabs.size(); i++)
            if (g_windows[hWnd].tabs[i].controller && i == g_windows[hWnd].activeTab)
                g_windows[hWnd].tabs[i].controller->put_Bounds(wvr);
        InvalidateRect(hWnd, NULL, FALSE); break;
    }

    case WM_DPICHANGED: {
        const RECT* newRect = (const RECT*)lParam;
        SetWindowPos(hWnd, NULL, newRect->left, newRect->top, newRect->right - newRect->left, newRect->bottom - newRect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        RepositionAddressBar(hWnd);
        RECT wvr = GetWebViewRect(hWnd);
        if (g_windows.count(hWnd))
            for (auto& tab : g_windows[hWnd].tabs)
                if (tab.controller) tab.controller->put_Bounds(wvr);
        InvalidateRect(hWnd, NULL, TRUE); return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd]; UINT dpi = GetWndDpi(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd, &cr); int W = cr.right; bool dirty = false;

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 }; TrackMouseEvent(&tme);

        int titleH = TitleBarH(dpi), navH = NavTotalH(dpi), winBtnW = WinBtnW(dpi), toolY = titleH + TabBarH(dpi);

        // Window buttons (Pin, Min, Max, Close)
        int bx = W - winBtnW*4;
        bool p  = (y < titleH && x >= bx && x < bx + winBtnW); bx += winBtnW;
        bool nm = (y < titleH && x >= bx && x < bx + winBtnW); bx += winBtnW;
        bool mx = (y < titleH && x >= bx && x < bx + winBtnW); bx += winBtnW;
        bool cl = (y < titleH && x >= bx);
        if (wd.hPin!=p||wd.hMin!=nm||wd.hMax!=mx||wd.hClose!=cl) { wd.hPin=p; wd.hMin=nm; wd.hMax=mx; wd.hClose=cl; dirty=true; }

        // Tabs
        int tc = (int)wd.tabs.size(), prev = wd.hoverTabIndex; wd.hoverTabIndex = -1;
        for (int i = 0; i < tc; i++) {
            RECT tr = GetTabRect(W, i, tc, dpi);
            if (x >= tr.left && x < tr.right && y >= tr.top && y < tr.bottom) { wd.hoverTabIndex = i; break; }
        }
        if (prev != wd.hoverTabIndex) dirty = true;

        RECT ntr = GetNewTabBtnRect(W, tc, dpi); bool nt = (x>=ntr.left&&x<ntr.right&&y>=ntr.top&&y<ntr.bottom);
        if (wd.hNewTab != nt) { wd.hNewTab = nt; dirty = true; }

        // Toolbar Nav
        int btnStep = S(36, dpi), cx = S(8, dpi);
        bool b  = (y>=toolY&&y<navH&&x>=cx&&x<cx+S(34,dpi)); cx+=btnStep;
        bool f  = (y>=toolY&&y<navH&&x>=cx&&x<cx+S(34,dpi)); cx+=btnStep;
        bool rl = (y>=toolY&&y<navH&&x>=cx&&x<cx+S(34,dpi));
        if (wd.hBack!=b||wd.hFwd!=f||wd.hRel!=rl) { wd.hBack=b; wd.hFwd=f; wd.hRel=rl; dirty=true; }

        // Toolbar Right
        int rx = W - S(36*4+12, dpi); // Ext, DL, Night, Set
        bool e  = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(34,dpi)); rx+=btnStep;
        bool dl = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(34,dpi)); rx+=btnStep;
        bool night = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(34,dpi)); rx+=btnStep;
        bool st = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(34,dpi));
        if (wd.hExt!=e||wd.hDl!=dl||wd.hNight!=night||wd.hSet!=st) { wd.hExt=e; wd.hDl=dl; wd.hNight=night; wd.hSet=st; dirty=true; }

        if (dirty) { RECT r = { 0, 0, W, NavTotalH(dpi) }; InvalidateRect(hWnd, &r, FALSE); } break;
    }

    case WM_MOUSELEAVE: {
        if (g_windows.count(hWnd)) {
            auto& wd = g_windows[hWnd];
            wd.hPin=wd.hMin=wd.hMax=wd.hClose=false;
            wd.hBack=wd.hFwd=wd.hRel=false;
            wd.hExt=wd.hDl=wd.hNight=wd.hSet=false;
            wd.hNewTab=false; wd.hoverTabIndex=-1;
            RECT cr; GetClientRect(hWnd, &cr); cr.bottom = NavTotalH(GetWndDpi(hWnd)); InvalidateRect(hWnd, &cr, FALSE);
        } break;
    }

    case WM_LBUTTONDOWN: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd]; UINT dpi = GetWndDpi(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam); RECT cr; GetClientRect(hWnd, &cr); int W = cr.right;

        if (wd.hPin) { 
            wd.isPinned = !wd.isPinned; 
            SetWindowPos(hWnd, wd.isPinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            break; 
        }
        if (wd.hMin)   { ShowWindow(hWnd, SW_MINIMIZE); break; }
        if (wd.hMax)   { ShowWindow(hWnd, IsZoomed(hWnd)?SW_RESTORE:SW_MAXIMIZE); break; }
        if (wd.hClose) { DestroyWindow(hWnd); break; }

        int tc = (int)wd.tabs.size();
        for (int i = 0; i < tc; i++) {
            RECT tr = GetTabRect(W, i, tc, dpi);
            if (x>=tr.left&&x<tr.right&&y>=tr.top&&y<tr.bottom) {
                if (x >= tr.right - S(22, dpi)) { CloseTab(hWnd, i); return 0; }
                SwitchToTab(hWnd, i); return 0;
            }
        }
        if (wd.hNewTab) { AddTab(hWnd, L"https://www.google.com"); break; }

        if (auto* tab = wd.active()) {
            if (wd.hBack && tab->webview && tab->canBack) tab->webview->GoBack();
            if (wd.hFwd  && tab->webview && tab->canFwd)  tab->webview->GoForward();
            if (wd.hRel  && tab->webview)                 tab->webview->Reload();
        }

        if (wd.hNight) { 
            wd.isNightMode = !wd.isNightMode; 
            BOOL dark = wd.isNightMode ? TRUE : FALSE;
            DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            RepositionAddressBar(hWnd); 
            InvalidateRect(hWnd, NULL, TRUE); 
            break; 
        }

        if (wd.hExt) MessageBoxW(hWnd, L"Extensions menu goes here.", L"Extensions", MB_OK|MB_ICONINFORMATION);
        if (wd.hDl)  MessageBoxW(hWnd, L"Downloads panel goes here.", L"Downloads", MB_OK|MB_ICONINFORMATION);
        if (wd.hSet) MessageBoxW(hWnd, L"Settings menu goes here.", L"Settings", MB_OK|MB_ICONINFORMATION);
        break;
    }

    case WM_LBUTTONDBLCLK: {
        if (!g_windows.count(hWnd)) break; UINT dpi = GetWndDpi(hWnd); int y = GET_Y_LPARAM(lParam);
        if (y >= TitleBarH(dpi) && y < TitleBarH(dpi) + TabBarH(dpi)) AddTab(hWnd, L"https://www.google.com");
        break;
    }

    case WM_GETMINMAXINFO: {
        auto* mm = (LPMINMAXINFO)lParam; mm->ptMinTrackSize.x = S(640, GetWndDpi(hWnd)); mm->ptMinTrackSize.y = S(480, GetWndDpi(hWnd)); return 0;
    }
    case WM_CLOSE: DestroyWindow(hWnd); break;
    case WM_DESTROY: {
        if (g_windows.count(hWnd)) {
            for (auto& tab : g_windows[hWnd].tabs) if (tab.controller) tab.controller->Close();
            if (g_windows[hWnd].hAddrFont) DeleteObject(g_windows[hWnd].hAddrFont);
            g_windows.erase(hWnd);
        }
        if (g_isPureViewerMode && g_windows.empty()) PostQuitMessage(0);
        break;
    }
    default: return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// PUBLIC API — LaunchMiniBrowser()
// ─────────────────────────────────────────────────────────────────────────────
void LaunchMiniBrowser(std::wstring url, std::wstring /*title*/) {
    static ULONG_PTR gdiplusToken = 0;
    if (!gdiplusToken) { GdiplusStartupInput si; GdiplusStartup(&gdiplusToken, &si, nullptr); }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc  = { sizeof(wc), CS_DBLCLKS|CS_HREDRAW|CS_VREDRAW, ViewerWndProc, 0, 0, GetModuleHandle(NULL), NULL, LoadCursor(NULL,IDC_ARROW), (HBRUSH)GetStockObject(BLACK_BRUSH), NULL, L"RasBrowserWnd", NULL };
        RegisterClassExW(&wc); registered = true;
    }

    HWND hWnd = CreateWindowExW(0, L"RasBrowserWnd", L"RasBrowser", WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_MAXIMIZEBOX|WS_MINIMIZEBOX|WS_CLIPCHILDREN|WS_CLIPSIBLINGS, CW_USEDEFAULT, CW_USEDEFAULT, 1100, 780, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!hWnd) return;

    SetWindowLongW(hWnd, GWL_STYLE, GetWindowLongW(hWnd, GWL_STYLE) & ~WS_CAPTION);
    ApplyDwmShadow(hWnd);

    auto& wd = g_windows[hWnd];
    HWND hEdit = CreateWindowExW(0, L"EDIT", L"https://www.google.com", WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_LEFT, 0, 0, 100, 26, hWnd, (HMENU)IDC_ADDRESS_BAR, GetModuleHandle(NULL), NULL);
    SetWindowLongW(hEdit, GWL_STYLE, GetWindowLongW(hEdit, GWL_STYLE) & ~WS_BORDER);
    SetWindowTheme(hEdit, L"", L"");
    SetWindowSubclass(hEdit, AddrBarProc, 1, 0);
    wd.hAddressBar = hEdit;

    HICON hIco = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    if (hIco) { SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIco); SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco); }

    TabData firstTab; firstTab.url = url; firstTab.title = L"New Tab";
    wd.tabs.push_back(firstTab); wd.activeTab = 0;

    // Show window immediately for "Super fast open" feeling
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    RepositionAddressBar(hWnd);
    CreateWebViewForTab(hWnd, 0);
}
