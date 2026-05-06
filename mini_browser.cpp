// mini_browser.cpp — RasBrowser | Chrome-like tabbed browser using WebView2
// REFACTORED: Per-Monitor v2 DPI, Chrome bezier tabs, double-buffering,
//             styled Omnibox, robust WebView2 init, DWM drop shadow.

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
// DPI HELPERS
// ─────────────────────────────────────────────────────────────────────────────

// Scale a 96-dpi design pixel to physical pixels for a given DPI.
static inline int S(int px, UINT dpi) {
    return MulDiv(px, (int)dpi, 96);
}
static inline float Sf(float px, UINT dpi) {
    return px * (float)dpi / 96.0f;
}

// Per-window DPI cache — updated on WM_DPICHANGED.
static UINT GetWndDpi(HWND hWnd) {
    UINT dpi = GetDpiForWindow(hWnd);
    return dpi ? dpi : 96;
}

// ─────────────────────────────────────────────────────────────────────────────
// LAYOUT CONSTANTS  (96-dpi design values — always scale via S())
// ─────────────────────────────────────────────────────────────────────────────
static const int D_TITLEBAR_H  = 32;
static const int D_TABBAR_H    = 36;
static const int D_TOOLBAR_H   = 40;
static const int D_NAV_TOTAL_H = D_TITLEBAR_H + D_TABBAR_H + D_TOOLBAR_H;

static const int D_TAB_W_MAX   = 220;
static const int D_TAB_W_MIN   = 80;
static const int D_TAB_PAD     = 10;
static const int D_WIN_BTN_W   = 46;
static const int D_LOGO_W      = 40;
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
    WINDOWPLACEMENT      wpPrev       = { sizeof(WINDOWPLACEMENT) };
    HWND                 hAddressBar  = NULL;
    HFONT                hAddrFont    = NULL;

    // Hover states
    bool hMin = false, hMax = false, hClose = false;
    bool hBack = false, hFwd = false, hRel = false;
    bool hExt  = false, hDl  = false, hSet = false;
    int  hoverTabIndex = -1;
    bool hNewTab       = false;

    TabData* active() {
        if (activeTab >= 0 && activeTab < (int)tabs.size())
            return &tabs[activeTab];
        return nullptr;
    }
    const TabData* active() const {
        if (activeTab >= 0 && activeTab < (int)tabs.size())
            return &tabs[activeTab];
        return nullptr;
    }
};

static std::map<HWND, BrowserWindowData> g_windows;
static ComPtr<ICoreWebView2Environment>  g_sharedEnv;

// ─────────────────────────────────────────────────────────────────────────────
// ADULT / BLOCKED CONTENT FILTER  (unchanged from original)
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
        L"\u09AE\u09BE\u0997\u09BF", L"\u0996\u09BE\u09A8\u0995\u09BF",
        L"\u09AF\u09CC\u09A8", L"\u09AA\u09B0\u09CD\u09A3\u0997\u09CD\u09B0\u09BE\u09AB\u09BF",
        L"\u09B0\u09C7\u09A8\u09CD\u09A1\u09BF", L"\u099A\u09CB\u09A6\u09BE\u099A\u09C1\u09A4\u09BF",
        L"\u0997\u09B0\u09AE \u09AD\u09BF\u09A1\u09BF\u0993",
        L"\u09AF\u09CC\u09A8 \u09AE\u09BF\u09B2\u09A8", L"\u09AF\u09CC\u09A8\u09BE\u0999\u09CD\u0997",
        L"\u099A\u09C1\u09A6\u09CB", L"\u09A8\u0997\u09CD\u09A8\u09A4\u09BE",
        L"bhabi", L"chudai", L"bangla choti", L"panu", L"desi bhabi", L"mms",
        L"magi", L"choda", L"chodachudi", L"khanki", L"besha", L"randi",
        L"nengta", L"nangta", L"baal", L"vodai", L"bokachoda",
        L"hot dance", L"seductive dance", L"bikini", L"swimsuit", L"sexy dance",
        L"cleavage", L"bedroom scene", L"bath scene", L"semi nude", L"lingerie",
        L"erotic", L"navel show", L"deep neck", L"short dress sexy",
    };

    for (const auto& kw : kBadWords)
        if (lower.find(kw) != std::wstring::npos)
            return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// GEOMETRY HELPERS  (all scaled)
// ─────────────────────────────────────────────────────────────────────────────
static int NavTotalH(UINT dpi)  { return S(D_NAV_TOTAL_H,  dpi); }
static int TitleBarH(UINT dpi)  { return S(D_TITLEBAR_H,   dpi); }
static int TabBarH  (UINT dpi)  { return S(D_TABBAR_H,     dpi); }
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
    int rightIconArea = S(36*3 + 12,    dpi);
    int addrH         = S(26,           dpi);
    int toolY         = TitleBarH(dpi) + TabBarH(dpi);
    int addrY         = toolY + (ToolbarH(dpi) - addrH) / 2;
    int addrX         = navBtnArea + S(4, dpi);
    int addrW         = W - navBtnArea - rightIconArea - S(8, dpi);

    ShowWindow(wd.hAddressBar, SW_SHOW);
    SetWindowPos(wd.hAddressBar, NULL,
        addrX, addrY, addrW, addrH,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // Update font size for new DPI
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
    }

    RepositionAddressBar(hWnd);

    RECT wvr = GetWebViewRect(hWnd);
    if (wd.isFullScreen) { wvr.top = 0; }
    for (auto& tab : wd.tabs)
        if (tab.controller)
            tab.controller->put_Bounds(wvr);

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
        if (riid == IID_IUnknown ||
            riid == __uuidof(ICoreWebView2AcceleratorKeyPressedEventHandler))
            { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2Controller*,
        ICoreWebView2AcceleratorKeyPressedEventArgs* args) override
    {
        COREWEBVIEW2_KEY_EVENT_KIND kind; args->get_KeyEventKind(&kind);
        if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN ||
            kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)
        {
            UINT vk; args->get_VirtualKey(&vk);
            if (vk == VK_F11) {
                ToggleFullScreen(m_hWnd); args->put_Handled(TRUE);
            }
            if (vk == VK_ESCAPE && g_windows.count(m_hWnd) &&
                g_windows[m_hWnd].isFullScreen)
            {
                ToggleFullScreen(m_hWnd); args->put_Handled(TRUE);
            }
        }
        return S_OK;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR SUBCLASS  (Enter to navigate, strip default border)
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK AddrBarProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                              UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        HWND hParent = GetParent(hWnd);
        if (!g_windows.count(hParent)) return 0;
        auto& wd = g_windows[hParent];
        auto* tab = wd.active();
        if (!tab || !tab->webview) return 0;

        wchar_t buf[2048]; GetWindowTextW(hWnd, buf, 2048);
        std::wstring input = buf;

        if (IsBlockedContent(input)) { SetWindowTextW(hWnd, L""); return 0; }

        std::wstring url;
        if (input.find(L"http://")  == 0 || input.find(L"https://") == 0) {
            url = input;
            if (url.find(L"google.com/search") != std::wstring::npos &&
                url.find(L"&safe=active") == std::wstring::npos)
                url += L"&safe=active";
        } else if (input.find(L'.') != std::wstring::npos &&
                   input.find(L' ') == std::wstring::npos) {
            url = L"https://" + input;
        } else {
            url = L"https://www.google.com/search?q=" + input + L"&safe=active";
        }
        tab->webview->Navigate(url.c_str());
        return 0;
    }
    // Remove the focus rectangle / default EDIT border via WM_NCPAINT
    if (msg == WM_NCPAINT) return 0;
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// DOUBLE-BUFFERED PAINT HELPER
// Allocates a compatible DC + bitmap that matches the client area, calls the
// provided draw callback, then BitBlts to the real HDC.
// ─────────────────────────────────────────────────────────────────────────────
static void DoubleBufferedPaint(HWND hWnd, HDC hdcReal,
                                std::function<void(HDC, int, int)> drawFn) {
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

// Rounded rect path helper
static void AddRoundRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    if (r <= 0.f) { path.AddRectangle(RectF(x,y,w,h)); return; }
    path.AddArc(x,         y,         r*2, r*2, 180, 90);
    path.AddArc(x+w-r*2,  y,         r*2, r*2, 270, 90);
    path.AddArc(x+w-r*2,  y+h-r*2,   r*2, r*2,   0, 90);
    path.AddArc(x,         y+h-r*2,   r*2, r*2,  90, 90);
    path.CloseFigure();
}

// Chrome-style tab shape:
//  - Flat bottom edge
//  - Curved top corners using arcs
//  - Angled (pseudo-trapezoid) sides via bezier curves matching Chrome exactly
static void BuildChromeTabPath(GraphicsPath& path,
                               float x, float y, float w, float h,
                               float cornerR)
{
    // Chrome tab is basically a rounded-top rectangle that connects with
    // a smooth curve at the bottom-left and bottom-right corners.
    // We trace clockwise: bottom-left → top-left → top-right → bottom-right → bottom-left
    float bl = x;
    float br = x + w;
    float top = y + 4.f;  // small inset from the strip top
    float bot = y + h;

    // Bottom-left connector (small outward bezier)
    float notchW = cornerR * 1.6f;
    path.StartFigure();
    // Bottom left
    path.AddLine(bl, bot, bl + notchW, bot);
    // Left curve up
    path.AddBezier(
        bl + notchW, bot,
        bl + notchW * 0.5f, bot,
        bl + cornerR * 0.25f, bot - cornerR,
        bl + cornerR, top + cornerR);
    // Top-left rounded corner
    path.AddArc(bl + cornerR, top, cornerR * 2, cornerR * 2, 180, 90);
    // Top edge
    path.AddLine(bl + cornerR * 3, top, br - cornerR * 3, top);
    // Top-right rounded corner
    path.AddArc(br - cornerR * 3, top, cornerR * 2, cornerR * 2, 270, 90);
    // Right curve down
    path.AddBezier(
        br - cornerR, top + cornerR,
        br - cornerR * 0.25f, bot - cornerR,
        br - notchW * 0.5f, bot,
        br - notchW, bot);
    // Bottom right → close
    path.AddLine(br - notchW, bot, br, bot);
    path.CloseFigure();
}

// ─────────────────────────────────────────────────────────────────────────────
// COLOR PALETTE  (Chrome dark theme — matches real Chrome ~2024)
// ─────────────────────────────────────────────────────────────────────────────
namespace Clr {
    static const Color BgFrame    (255,  30,  30,  30);  // #1E1E1E outer frame
    static const Color BgTabStrip (255,  32,  32,  32);  // #202020 tab strip bg
    static const Color BgToolbar  (255,  41,  41,  41);  // #292929 toolbar
    static const Color TabActive  (255,  41,  41,  41);  // #292929 active tab
    static const Color TabHover   (255,  55,  55,  55);  // #373737
    static const Color TabNormal  (255,  32,  32,  32);  // #202020 idle
    static const Color TxtPrim    (255, 232, 232, 232);
    static const Color TxtDim     (255, 138, 138, 138);
    static const Color AddrBg     (255,  56,  56,  56);
    static const Color AccentBlue (255,  66, 133, 244);  // Google blue
    static const Color DivLine    (255,  60,  60,  60);
    static const Color CloseHov   (255, 196,  43,  28);  // red × hover
    static const Color TabCloseIc (255, 160, 160, 160);
    static const Color White      (255, 255, 255, 255);
    static const Color WinHov     ( 40, 255, 255, 255);  // translucent white
    static const Color NavHov     ( 35, 255, 255, 255);
    static const Color TabHovCirc ( 50, 255, 255, 255);
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN DRAW FUNCTION  (called inside the double-buffer lambda)
// ─────────────────────────────────────────────────────────────────────────────
static void DrawBrowserContent(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (wd.isFullScreen) return;

    UINT dpi = GetWndDpi(hWnd);
    RECT cr;  GetClientRect(hWnd, &cr);
    int W = cr.right;

    // Scaled layout values
    int titleH  = TitleBarH(dpi);
    int tabH    = TabBarH(dpi);
    int toolH   = ToolbarH(dpi);
    int navH    = NavTotalH(dpi);
    int winBtnW = WinBtnW(dpi);
    int logoW   = LogoW(dpi);

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);

    // ── Background strips ──────────────────────────────────────────────────
    {
        SolidBrush bFrame(Clr::BgFrame);
        SolidBrush bStrip(Clr::BgTabStrip);
        SolidBrush bTool (Clr::BgToolbar);

        g.FillRectangle(&bFrame, 0, 0, W, titleH);
        g.FillRectangle(&bStrip, 0, titleH, W, tabH);
        g.FillRectangle(&bTool,  0, titleH + tabH, W, toolH);

        // Thin separator below toolbar
        Pen sepPen(Clr::DivLine, 1.0f);
        g.DrawLine(&sepPen, 0, navH - 1, W, navH - 1);
    }

    // ── Fonts (scaled) ─────────────────────────────────────────────────────
    FontFamily ffSeg(L"Segoe UI");
    FontFamily ffMDL(L"Segoe MDL2 Assets");

    float fSzNormal = Sf(13.f, dpi);
    float fSzSmall  = Sf(11.f, dpi);
    float fSzIcon   = Sf(14.f, dpi);
    float fSzIconSm = Sf(11.f, dpi);
    float fSzBrand  = Sf(12.f, dpi);

    Font fNormal (&ffSeg, fSzNormal, FontStyleRegular,  UnitPixel);
    Font fSmall  (&ffSeg, fSzSmall,  FontStyleRegular,  UnitPixel);
    Font fBrand  (&ffSeg, fSzBrand,  FontStyleBold,     UnitPixel);
    Font fIcon   (&ffMDL, fSzIcon,   FontStyleRegular,  UnitPixel);
    Font fIconSm (&ffMDL, fSzIconSm, FontStyleRegular,  UnitPixel);

    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);
    sfC.SetTrimming(StringTrimmingEllipsisCharacter);

    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);
    sfL.SetFormatFlags(StringFormatFlagsNoWrap);
    sfL.SetTrimming(StringTrimmingEllipsisCharacter);

    SolidBrush brPrim(Clr::TxtPrim);
    SolidBrush brDim (Clr::TxtDim);
    SolidBrush brBlue(Clr::AccentBlue);
    SolidBrush brWhite(Clr::White);

    // ── Title bar: Brand ────────────────────────────────────────────────────
    {
        int circDia  = S(14, dpi);
        int circX    = S(10, dpi);
        int circY    = (titleH - circDia) / 2;
        g.FillEllipse(&brBlue, (float)circX, (float)circY,
                      (float)circDia, (float)circDia);
        g.DrawString(L"R", -1, &fIconSm,
            RectF((float)circX, (float)circY, (float)circDia, (float)circDia),
            &sfC, &brWhite);

        g.DrawString(L"RasBrowser", -1, &fBrand,
            RectF((float)(circX + circDia + S(4,dpi)), 0.f,
                  (float)S(160, dpi), (float)titleH),
            &sfL, &brBlue);
    }

    // ── Window controls: Min / Max / Close ─────────────────────────────────
    {
        int bx = W - winBtnW * 3;

        auto DrawWinBtn = [&](int x, bool hover, bool isClose,
                               const wchar_t* ico)
        {
            if (hover) {
                SolidBrush hb(isClose ? Clr::CloseHov : Clr::WinHov);
                g.FillRectangle(&hb, x, 0, winBtnW, titleH);
            }
            SolidBrush txtClr(
                isClose && hover ? Clr::White :
                hover            ? Clr::TxtPrim :
                                   Clr::TxtDim);
            g.DrawString(ico, -1, &fIconSm,
                RectF((float)x, 0.f, (float)winBtnW, (float)titleH),
                &sfC, &txtClr);
        };

        DrawWinBtn(bx,              wd.hMin,   false, L"\xE921");
        DrawWinBtn(bx + winBtnW,    wd.hMax,   false,
                   IsZoomed(hWnd) ? L"\xE923" : L"\xE922");
        DrawWinBtn(bx + winBtnW*2,  wd.hClose, true,  L"\xE8BB");
    }

    // ── Tab strip ──────────────────────────────────────────────────────────
    {
        int tc   = (int)wd.tabs.size();
        int tabW = CalcTabWidth(W, tc, dpi);
        float cornerR = Sf(6.f, dpi);

        for (int i = 0; i < tc; i++) {
            RECT tr   = GetTabRect(W, i, tc, dpi);
            float tx  = (float)tr.left;
            float ty  = (float)tr.top;
            float tw  = (float)(tr.right  - tr.left);
            float th  = (float)(tr.bottom - tr.top);

            bool isActive = (i == wd.activeTab);
            bool isHover  = (i == wd.hoverTabIndex);

            // Tab background path
            GraphicsPath tabPath;
            BuildChromeTabPath(tabPath, tx, ty, tw, th, cornerR);

            Color fillC = isActive ? Clr::TabActive :
                          isHover  ? Clr::TabHover  :
                                     Clr::TabNormal;
            SolidBrush bTab(fillC);
            g.FillPath(&bTab, &tabPath);

            // Active tab: blue top-accent stripe
            if (isActive) {
                float accentH = Sf(2.5f, dpi);
                float accentX = tx + cornerR * 3;
                float accentW = tw - cornerR * 6;
                if (accentW > 0) {
                    GraphicsPath accPath;
                    AddRoundRect(accPath, accentX, ty + Sf(4.f,dpi),
                                 accentW, accentH, accentH * 0.5f);
                    g.FillPath(&brBlue, &accPath);
                }
            }

            // Favicon circle
            float iconSz = Sf(14.f, dpi);
            float iconX  = tx + Sf((float)D_TAB_PAD + 2, dpi);
            float iconY  = ty + (th - iconSz) * 0.5f;
            SolidBrush fvBrush(isActive ? Clr::AccentBlue : Clr::TxtDim);
            g.FillEllipse(&fvBrush, iconX, iconY, iconSz, iconSz);

            // Tab title
            const auto& tab = wd.tabs[i];
            SolidBrush tBrush(isActive ? Clr::TxtPrim : Clr::TxtDim);
            float titleX = iconX + iconSz + Sf(4.f, dpi);
            float closeW = Sf(24.f, dpi);
            float titleW = tw - (titleX - tx) - closeW;
            if (titleW > 0) {
                g.DrawString(tab.title.c_str(), -1, &fSmall,
                    RectF(titleX, ty, titleW, th), &sfL, &tBrush);
            }

            // Close (×) button — shown on active or hover
            if (isActive || isHover) {
                float cSz = Sf(16.f, dpi);
                float cx  = tx + tw - cSz - Sf(4.f, dpi);
                float cy  = ty + (th - cSz) * 0.5f;
                if (isHover && !isActive) {
                    SolidBrush hbx(Clr::TabHovCirc);
                    g.FillEllipse(&hbx, cx, cy, cSz, cSz);
                }
                SolidBrush xBrush(Clr::TabCloseIc);
                g.DrawString(L"\xE8BB", -1, &fIconSm,
                    RectF(cx, cy, cSz, cSz), &sfC, &xBrush);
            }

            // Divider between non-adjacent tabs
            if (!isActive && i < tc - 1 && i+1 != wd.activeTab) {
                Pen divPen(Clr::DivLine, 1.0f);
                float dx = tx + tw - 1.f;
                g.DrawLine(&divPen, dx, (float)(titleH + S(8,dpi)),
                           dx, (float)(titleH + tabH - S(8,dpi)));
            }
        }

        // New-tab (+) button
        RECT ntr = GetNewTabBtnRect(W, tc, dpi);
        if (wd.hNewTab) {
            SolidBrush hb(Clr::WinHov);
            g.FillEllipse(&hb,
                (float)ntr.left, (float)ntr.top,
                (float)(ntr.right-ntr.left), (float)(ntr.bottom-ntr.top));
        }
        g.DrawString(L"\xE710", -1, &fIconSm,
            RectF((float)ntr.left, (float)ntr.top,
                  (float)(ntr.right-ntr.left), (float)(ntr.bottom-ntr.top)),
            &sfC, &brDim);
    }

    // ── Toolbar ────────────────────────────────────────────────────────────
    {
        int toolY = titleH + tabH;
        int curX  = S(8, dpi);
        int btnSz = S(34, dpi);
        int btnStep = S(36, dpi);
        float btnHf = (float)toolH;

        auto DrawNavBtn = [&](bool hover, bool enabled,
                               const wchar_t* ico, int& x)
        {
            if (hover && enabled) {
                SolidBrush hb(Clr::NavHov);
                g.FillEllipse(&hb,
                    (float)(x+S(2,dpi)), (float)(toolY+S(4,dpi)),
                    (float)S(30,dpi), (float)S(30,dpi));
            }
            SolidBrush ic(enabled ? Clr::TxtPrim : Clr::TxtDim);
            g.DrawString(ico, -1, &fIcon,
                RectF((float)x, (float)toolY, (float)btnSz, btnHf),
                &sfC, &ic);
            x += btnStep;
        };

        auto* atab = wd.active();
        bool canBack = atab && atab->canBack;
        bool canFwd  = atab && atab->canFwd;

        DrawNavBtn(wd.hBack, canBack, L"\xE72B", curX);
        DrawNavBtn(wd.hFwd,  canFwd,  L"\xE72A", curX);
        DrawNavBtn(wd.hRel,  true,    L"\xE72C", curX);

        // Address bar pill background (the EDIT is overlaid here;
        // the pill is purely decorative context behind the EDIT).
        {
            int addrX   = curX + S(4,dpi);
            int rightIX = W - S(36*3 + 12, dpi);
            int addrW   = rightIX - addrX - S(4,dpi);
            int addrH   = S(30, dpi);
            int addrY   = toolY + (toolH - addrH) / 2;
            float r15   = Sf(15.f, dpi);

            // Pill shadow (subtle)
            {
                SolidBrush shadow(Color(20, 0, 0, 0));
                GraphicsPath sPath;
                AddRoundRect(sPath, (float)(addrX+1), (float)(addrY+1),
                             (float)addrW, (float)addrH, r15);
                g.FillPath(&shadow, &sPath);
            }
            SolidBrush addrBg(Clr::AddrBg);
            GraphicsPath pill;
            AddRoundRect(pill, (float)addrX, (float)addrY,
                         (float)addrW, (float)addrH, r15);
            g.FillPath(&addrBg, &pill);

            // Lock / globe icon inside pill (left side)
            SolidBrush lockBrush(Clr::TxtDim);
            g.DrawString(L"\xE72E", -1, &fIconSm,
                RectF((float)addrX + Sf(6.f,dpi),
                      (float)addrY, Sf(20.f,dpi), (float)addrH),
                &sfC, &lockBrush);
        }

        // Right toolbar icons
        int rx = W - S(36*3 + 12, dpi);
        auto DrawRightBtn = [&](bool hover, const wchar_t* ico, int x) {
            if (hover) {
                SolidBrush hb(Clr::NavHov);
                g.FillEllipse(&hb,
                    (float)(x+S(2,dpi)), (float)(toolY+S(4,dpi)),
                    (float)S(30,dpi), (float)S(30,dpi));
            }
            g.DrawString(ico, -1, &fIcon,
                RectF((float)x, (float)toolY, (float)btnSz, btnHf),
                &sfC, &brPrim);
        };
        DrawRightBtn(wd.hExt, L"\xE9D2", rx); rx += btnStep;
        DrawRightBtn(wd.hDl,  L"\xE896", rx); rx += btnStep;
        DrawRightBtn(wd.hSet, L"\xE713", rx);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FULL DrawBrowser (public) — orchestrates double buffering
// ─────────────────────────────────────────────────────────────────────────────
void DrawBrowser(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    if (g_windows[hWnd].isFullScreen) return;

    UINT dpi  = GetWndDpi(hWnd);
    int  navH = NavTotalH(dpi);

    DoubleBufferedPaint(hWnd, hdc, [&](HDC memDC, int W, int H) {
        // Fill full area with frame color first (avoids black artifacts
        // during resize when content is taller than nav chrome)
        HBRUSH hbg = CreateSolidBrush(RGB(30, 30, 30));
        RECT fr = { 0, 0, W, H };
        FillRect(memDC, &fr, hbg);
        DeleteObject(hbg);

        // Now draw navigation chrome into the same mem DC
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

    if (wd.hAddressBar)
        SetWindowTextW(wd.hAddressBar, tab.url.c_str());

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
class TabControllerHandler
    : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler
{
    HWND         m_hWnd;
    int          m_tabIdx;
    std::wstring m_startUrl;
    ULONG        m_ref = 1;

public:
    TabControllerHandler(HWND h, int idx, std::wstring url)
        : m_hWnd(h), m_tabIdx(idx), m_startUrl(std::move(url)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        *ppv = this; return S_OK;
    }
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

        // Dark background colour
        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
            COREWEBVIEW2_COLOR bg = { 255, 30, 30, 30 };
            ctl2->put_DefaultBackgroundColor(bg);
        }

        // Chrome UA spoof
        ICoreWebView2Settings* settings = nullptr;
        tab.webview->get_Settings(&settings);
        ComPtr<ICoreWebView2Settings2> s2;
        if (settings && SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&s2)))) {
            s2->put_UserAgent(
                L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                L"AppleWebKit/537.36 (KHTML, like Gecko) "
                L"Chrome/124.0.0.0 Safari/537.36");
        }

        // Navigation blocking (adult content)
        tab.webview->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT
                {
                    LPWSTR uri = nullptr; args->get_Uri(&uri);
                    if (uri) {
                        if (IsBlockedContent(uri)) {
                            args->put_Cancel(TRUE);
                            if (g_windows.count(m_hWnd)) {
                                auto& w = g_windows[m_hWnd];
                                if (w.hAddressBar)
                                    SetWindowTextW(w.hAddressBar, L"");
                            }
                        }
                        CoTaskMemFree(uri);
                    }
                    return S_OK;
                }).Get(), nullptr);

        // New-window → same tab
        tab.webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2* sender,
                   ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT
                {
                    args->put_Handled(TRUE);
                    LPWSTR uri = nullptr; args->get_Uri(&uri);
                    if (uri) { sender->Navigate(uri); CoTaskMemFree(uri); }
                    return S_OK;
                }).Get(), nullptr);

        // Title changed
        tab.webview->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                [this](ICoreWebView2* sender, IUnknown*) -> HRESULT
                {
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

        // URL sync
        tab.webview->add_SourceChanged(
            Callback<ICoreWebView2SourceChangedEventHandler>(
                [this](ICoreWebView2* sender,
                       ICoreWebView2SourceChangedEventArgs*) -> HRESULT
                {
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

        // Back/forward state refresh
        tab.webview->add_HistoryChanged(
            Callback<ICoreWebView2HistoryChangedEventHandler>(
                [this](ICoreWebView2* sender, IUnknown*) -> HRESULT
                {
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

        // Keyboard shortcuts (F11 / ESC)
        ComPtr<ICoreWebView2Controller3> ctl3;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl3)))) {
            EventRegistrationToken tok;
            ctl3->add_AcceleratorKeyPressed(new AcceleratorHandler(m_hWnd), &tok);
        }

        // Visibility & bounds
        bool isActive = (m_tabIdx == wd.activeTab);
        ctl->put_IsVisible(isActive ? TRUE : FALSE);
        RECT wvr = GetWebViewRect(m_hWnd);
        ctl->put_Bounds(wvr);

        // Navigate
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

// ─────────────────────────────────────────────────────────────────────────────
// WEBVIEW2 ENVIRONMENT HANDLER
// Separates env creation so we can robustly chain → controller creation.
// ─────────────────────────────────────────────────────────────────────────────
class EnvCompletedHandler
    : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
{
    HWND m_hWnd;
    int  m_tabIdx;
    ULONG m_ref = 1;

public:
    EnvCompletedHandler(HWND h, int idx) : m_hWnd(h), m_tabIdx(idx) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        *ppv = this; return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env) override {
        if (FAILED(hr) || !env) return S_OK;
        g_sharedEnv = env;
        // Now create the controller that was waiting for the env
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
        // First launch — create env then controller
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
            nullptr, udDir, options.Get(),
            new EnvCompletedHandler(hWnd, tabIdx));

        if (FAILED(hr)) {
            // Fallback: try default profile
            CreateCoreWebView2EnvironmentWithOptions(
                nullptr, nullptr, nullptr,
                new EnvCompletedHandler(hWnd, tabIdx));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DWM SHADOW HELPER  — extends the invisible 1px DWM frame so we keep the
// native Windows drop shadow on our borderless window.
// ─────────────────────────────────────────────────────────────────────────────
static void ApplyDwmShadow(HWND hWnd) {
    // On Windows 10+ the shadow is controlled via DWM non-client area.
    // Extending by {0,0,0,1} keeps the bottom shadow visible.
    MARGINS m = { 0, 0, 0, 1 };
    DwmExtendFrameIntoClientArea(hWnd, &m);

    // Also opt in to rounded corners (Windows 11)
    DWORD pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

    // Dark title bar (tells DWM we prefer dark chrome)
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

// ─────────────────────────────────────────────────────────────────────────────
// WINDOW PROCEDURE
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    // ── Custom chrome / borderless ──────────────────────────────────────────
    case WM_NCCALCSIZE:
        // Return 0 for TRUE to remove the OS-drawn caption/frame.
        // A 1px bottom reservation is kept for DWM shadow via MARGINS.
        if (wParam == TRUE) return 0;
        break;

    case WM_NCHITTEST: {
        // Let DefWindowProc check for the DWM-shadow resize border first.
        LRESULT def = DefWindowProcW(hWnd, msg, wParam, lParam);
        if (def == HTNOWHERE || def == HTCLIENT) {
            // Our own hit-testing
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            RECT cr; GetClientRect(hWnd, &cr);
            UINT dpi = GetWndDpi(hWnd);
            int border = S(8, dpi);

            if (!g_windows.count(hWnd) || !g_windows[hWnd].isFullScreen) {
                // Resize edges
                if (pt.y < border && pt.x < border)                  return HTTOPLEFT;
                if (pt.y < border && pt.x >= cr.right-border)         return HTTOPRIGHT;
                if (pt.y >= cr.bottom-border && pt.x < border)        return HTBOTTOMLEFT;
                if (pt.y >= cr.bottom-border && pt.x >= cr.right-border) return HTBOTTOMRIGHT;
                if (pt.y < border)                return HTTOP;
                if (pt.y >= cr.bottom-border)     return HTBOTTOM;
                if (pt.x < border)                return HTLEFT;
                if (pt.x >= cr.right-border)      return HTRIGHT;

                // Title bar
                int winBtnX = cr.right - WinBtnW(dpi) * 3;
                if (pt.y < TitleBarH(dpi)) {
                    if (pt.x >= winBtnX) return HTCLIENT;  // window buttons
                    if (pt.x < S(200, dpi)) return HTCLIENT; // brand area
                    return HTCAPTION;
                }
                if (pt.y < NavTotalH(dpi)) return HTCLIENT;
            }
            return HTCLIENT;
        }
        return def;
    }

    // ── Double-click title bar → maximize/restore ──────────────────────────
    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION) {
            ShowWindow(hWnd, IsZoomed(hWnd) ? SW_RESTORE : SW_MAXIMIZE);
            return 0;
        }
        break;

    // ── DWM shadow on creation ─────────────────────────────────────────────
    case WM_CREATE:
        ApplyDwmShadow(hWnd);
        break;

    // ── Paint (double-buffered) ────────────────────────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        DrawBrowser(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;  // suppress; WM_PAINT handles everything

    // ── Prevent black tearing on resize ───────────────────────────────────
    case WM_WINDOWPOSCHANGING: {
        auto* wp = (WINDOWPOS*)lParam;
        wp->flags |= SWP_NOCOPYBITS;
        break;
    }

    // ── Address bar colour ─────────────────────────────────────────────────
    case WM_CTLCOLOREDIT: {
        if (g_windows.count(hWnd) && (HWND)lParam == g_windows[hWnd].hAddressBar) {
            HDC hEdit = (HDC)wParam;
            SetTextColor(hEdit, RGB(232, 232, 232));
            SetBkColor  (hEdit, RGB(56,  56,  56));
            static HBRUSH hBrAddr = CreateSolidBrush(RGB(56, 56, 56));
            return (LRESULT)hBrAddr;
        }
        break;
    }

    // ── Resize ────────────────────────────────────────────────────────────
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

    // ── DPI changed ───────────────────────────────────────────────────────
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
                if (tab.controller)
                    tab.controller->put_Bounds(wvr);
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    // ── Mouse move (hover hit-testing) ────────────────────────────────────
    case WM_MOUSEMOVE: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd];
        UINT dpi = GetWndDpi(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd, &cr); int W = cr.right;
        bool dirty = false;

        // Track WM_MOUSELEAVE
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
        }

        int titleH  = TitleBarH(dpi);
        int navH    = NavTotalH(dpi);
        int winBtnW = WinBtnW(dpi);
        int toolY   = titleH + TabBarH(dpi);

        // Window buttons
        {
            int bx = W - winBtnW*3;
            bool nm = (y < titleH && x >= bx           && x < bx + winBtnW);
            bool mx = (y < titleH && x >= bx + winBtnW && x < bx + winBtnW*2);
            bool cl = (y < titleH && x >= bx + winBtnW*2);
            if (wd.hMin!=nm||wd.hMax!=mx||wd.hClose!=cl)
                { wd.hMin=nm; wd.hMax=mx; wd.hClose=cl; dirty=true; }
        }

        // Tab hover
        {
            int tc = (int)wd.tabs.size();
            int prev = wd.hoverTabIndex; wd.hoverTabIndex = -1;
            for (int i = 0; i < tc; i++) {
                RECT tr = GetTabRect(W, i, tc, dpi);
                if (x >= tr.left && x < tr.right &&
                    y >= tr.top  && y < tr.bottom)
                { wd.hoverTabIndex = i; break; }
            }
            if (prev != wd.hoverTabIndex) dirty = true;
        }

        // New-tab button
        {
            RECT ntr = GetNewTabBtnRect(W, (int)wd.tabs.size(), dpi);
            bool nt = (x>=ntr.left&&x<ntr.right&&y>=ntr.top&&y<ntr.bottom);
            if (wd.hNewTab != nt) { wd.hNewTab = nt; dirty = true; }
        }

        // Toolbar nav buttons
        {
            int btnStep = S(36, dpi);
            int cx = S(8, dpi);
            bool b  = (y>=toolY&&y<navH&&x>=cx&&x<cx+S(34,dpi)); cx+=btnStep;
            bool f  = (y>=toolY&&y<navH&&x>=cx&&x<cx+S(34,dpi)); cx+=btnStep;
            bool rl = (y>=toolY&&y<navH&&x>=cx&&x<cx+S(34,dpi));
            if (wd.hBack!=b||wd.hFwd!=f||wd.hRel!=rl)
                { wd.hBack=b; wd.hFwd=f; wd.hRel=rl; dirty=true; }

            int rx = W - S(36*3+12, dpi);
            bool e  = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(34,dpi)); rx+=btnStep;
            bool dl = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(34,dpi)); rx+=btnStep;
            bool st = (y>=toolY&&y<navH&&x>=rx&&x<rx+S(34,dpi));
            if (wd.hExt!=e||wd.hDl!=dl||wd.hSet!=st)
                { wd.hExt=e; wd.hDl=dl; wd.hSet=st; dirty=true; }
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
            wd.hExt=wd.hDl=wd.hSet=false;
            wd.hNewTab=false; wd.hoverTabIndex=-1;
            UINT dpi = GetWndDpi(hWnd);
            RECT cr; GetClientRect(hWnd, &cr);
            cr.bottom = NavTotalH(dpi);
            InvalidateRect(hWnd, &cr, FALSE);
        }
        break;
    }

    // ── Left click ────────────────────────────────────────────────────────
    case WM_LBUTTONDOWN: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd];
        UINT dpi = GetWndDpi(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd, &cr); int W = cr.right;

        // Window controls
        if (wd.hMin)   { ShowWindow(hWnd, SW_MINIMIZE); break; }
        if (wd.hMax)   { ShowWindow(hWnd, IsZoomed(hWnd)?SW_RESTORE:SW_MAXIMIZE); break; }
        if (wd.hClose) { DestroyWindow(hWnd); break; }

        // Tab clicks
        {
            int tc = (int)wd.tabs.size();
            for (int i = 0; i < tc; i++) {
                RECT tr = GetTabRect(W, i, tc, dpi);
                if (x>=tr.left&&x<tr.right&&y>=tr.top&&y<tr.bottom) {
                    // Close button: rightmost ~22px
                    if (x >= tr.right - S(22, dpi))
                        { CloseTab(hWnd, i); return 0; }
                    SwitchToTab(hWnd, i);
                    return 0;
                }
            }
        }

        if (wd.hNewTab) { AddTab(hWnd, L"https://www.google.com"); break; }

        if (auto* tab = wd.active()) {
            if (wd.hBack && tab->webview && tab->canBack) tab->webview->GoBack();
            if (wd.hFwd  && tab->webview && tab->canFwd)  tab->webview->GoForward();
            if (wd.hRel  && tab->webview)                 tab->webview->Reload();
        }

        if (wd.hExt) MessageBoxW(hWnd, L"এক্সটেনশন মেনু এখানে দেখাবে।", L"Extensions", MB_OK|MB_ICONINFORMATION);
        if (wd.hDl)  MessageBoxW(hWnd, L"ডাউনলোড প্যানেল এখানে দেখাবে।",  L"Downloads",  MB_OK|MB_ICONINFORMATION);
        if (wd.hSet) MessageBoxW(hWnd, L"সেটিংস মেনু এখানে দেখাবে।",       L"Settings",   MB_OK|MB_ICONINFORMATION);
        break;
    }

    // ── Double-click on tab strip → new tab ───────────────────────────────
    case WM_LBUTTONDBLCLK: {
        if (!g_windows.count(hWnd)) break;
        UINT dpi = GetWndDpi(hWnd);
        int y = GET_Y_LPARAM(lParam);
        if (y >= TitleBarH(dpi) && y < TitleBarH(dpi) + TabBarH(dpi))
            AddTab(hWnd, L"https://www.google.com");
        break;
    }

    // ── Minimum window size ───────────────────────────────────────────────
    case WM_GETMINMAXINFO: {
        UINT dpi = GetWndDpi(hWnd);
        auto* mm = (LPMINMAXINFO)lParam;
        mm->ptMinTrackSize.x = S(640, dpi);
        mm->ptMinTrackSize.y = S(480, dpi);
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
    // ── GDI+ (initialised once) ────────────────────────────────────────────
    static ULONG_PTR gdiplusToken = 0;
    if (!gdiplusToken) {
        GdiplusStartupInput si;
        GdiplusStartup(&gdiplusToken, &si, nullptr);
    }

    // ── Per-Monitor v2 DPI awareness ──────────────────────────────────────
    // Call this before creating any windows. Harmless if already set.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // ── Window class (registered once) ────────────────────────────────────
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc  = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ViewerWndProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.lpszClassName = L"RasBrowserWnd";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); // avoids white flash
        RegisterClassExW(&wc);
        registered = true;
    }

    // ── Create window ─────────────────────────────────────────────────────
    HWND hWnd = CreateWindowExW(
        0,
        L"RasBrowserWnd", L"RasBrowser",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU |
        WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 780,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!hWnd) return;

    // Remove OS caption (we draw our own title bar)
    SetWindowLongW(hWnd, GWL_STYLE,
        GetWindowLongW(hWnd, GWL_STYLE) & ~WS_CAPTION);

    // DWM shadow + dark mode + Win11 rounded corners
    ApplyDwmShadow(hWnd);

    // ── Init window data ───────────────────────────────────────────────────
    auto& wd = g_windows[hWnd];

    // ── Address bar ────────────────────────────────────────────────────────
    // Remove WS_BORDER and ES_SUNKEN so no border renders; we draw our own pill.
    HWND hEdit = CreateWindowExW(
        0,                               // no WS_EX_CLIENTEDGE
        L"EDIT", L"https://www.google.com",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
        0, 0, 100, 26,
        hWnd, (HMENU)IDC_ADDRESS_BAR,
        GetModuleHandle(NULL), NULL);

    // Remove the default EDIT border drawn by the control itself
    SetWindowLongW(hEdit, GWL_STYLE,
        GetWindowLongW(hEdit, GWL_STYLE) & ~WS_BORDER);
    // Suppress themed border
    SetWindowTheme(hEdit, L"", L"");

    SetWindowSubclass(hEdit, AddrBarProc, 1, 0);
    wd.hAddressBar = hEdit;

    // ── App icon ──────────────────────────────────────────────────────────
    HICON hIco = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    if (hIco) {
        SendMessage(hWnd, WM_SETICON, ICON_BIG,   (LPARAM)hIco);
        SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
    }

    // ── First tab ─────────────────────────────────────────────────────────
    TabData firstTab;
    firstTab.url   = url;
    firstTab.title = L"New Tab";
    wd.tabs.push_back(firstTab);
    wd.activeTab = 0;

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    RepositionAddressBar(hWnd);
    CreateWebViewForTab(hWnd, 0);
}
