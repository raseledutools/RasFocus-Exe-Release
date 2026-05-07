// mini_browser.cpp — RasBrowser | White Theme, Unified Tabs, Fast Maximized
// REFACTORED: Unified Title+TabBar, Desktop Shortcut, Default Browser Registry.

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
#include <shlobj.h> // For Desktop Shortcut
#include <shlwapi.h>

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
// LAYOUT CONSTANTS (Unified Title/Tab bar)
// ─────────────────────────────────────────────────────────────────────────────
static const int D_TITLEBAR_H  = 44; // Tabs and Logo live here now
static const int D_TOOLBAR_H   = 44;
static const int D_NAV_TOTAL_H = D_TITLEBAR_H + D_TOOLBAR_H;

static const int D_TAB_W_MAX   = 240;
static const int D_TAB_W_MIN   = 80;
static const int D_TAB_PAD     = 10;
static const int D_WIN_BTN_W   = 46;
static const int D_BRAND_W     = 140; // Logo + Name width
static const int D_NEW_TAB_BTN = 28;

// ─────────────────────────────────────────────────────────────────────────────
// PER-TAB & PER-WINDOW DATA
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

struct BrowserWindowData {
    std::vector<TabData> tabs;
    int                  activeTab    = 0;
    bool                 isFullScreen = false;
    WINDOWPLACEMENT      wpPrev       = { sizeof(WINDOWPLACEMENT) };
    HWND                 hAddressBar  = NULL;
    HFONT                hAddrFont    = NULL;

    bool hMin = false, hMax = false, hClose = false;
    bool hBack = false, hFwd = false, hRel = false;
    bool hExt  = false, hDl  = false, hSet = false;
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
// CONTENT FILTER
// ─────────────────────────────────────────────────────────────────────────────
bool IsBlockedContent(const std::wstring& text) {
    std::wstring lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    static const std::vector<std::wstring> kBadWords = {
        L"porn", L"xxx", L"sex", L"nude", L"nsfw", L"sexy", L"hentai", L"rule34",
        L"milf", L"blowjob", L"tits", L"boobs", L"pussy", L"dick", L"cock",
        L"xvideos", L"pornhub", L"xnxx", L"xhamster", L"brazzers"
    };
    for (const auto& kw : kBadWords)
        if (lower.find(kw) != std::wstring::npos) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// GEOMETRY HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static int NavTotalH(UINT dpi)  { return S(D_NAV_TOTAL_H, dpi); }
static int TitleBarH(UINT dpi)  { return S(D_TITLEBAR_H,  dpi); }
static int ToolbarH (UINT dpi)  { return S(D_TOOLBAR_H,   dpi); }
static int WinBtnW  (UINT dpi)  { return S(D_WIN_BTN_W,   dpi); }
static int BrandW   (UINT dpi)  { return S(D_BRAND_W,     dpi); }

static int CalcTabWidth(int windowW, int tabCount, UINT dpi) {
    int winBtnArea = WinBtnW(dpi) * 3;
    int available  = windowW - winBtnArea - BrandW(dpi) - S(D_NEW_TAB_BTN + 16, dpi);
    int w = (tabCount > 0) ? available / tabCount : S(D_TAB_W_MAX, dpi);
    return max(S(D_TAB_W_MIN, dpi), min(S(D_TAB_W_MAX, dpi), w));
}

static RECT GetTabRect(int windowW, int index, int tabCount, UINT dpi) {
    int tw = CalcTabWidth(windowW, tabCount, dpi);
    int x  = BrandW(dpi) + index * tw;
    int y  = S(8, dpi); // Tabs drop down slightly from the top edge
    RECT r = { x, y, x + tw, TitleBarH(dpi) };
    return r;
}

static RECT GetNewTabBtnRect(int windowW, int tabCount, UINT dpi) {
    int tw = CalcTabWidth(windowW, tabCount, dpi);
    int x  = BrandW(dpi) + tabCount * tw + S(4, dpi);
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
// AUTO SHORTCUT & DEFAULT BROWSER REGISTRY
// ─────────────────────────────────────────────────────────────────────────────
void CreateDesktopShortcut() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    wchar_t desktopPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
        std::wstring linkPath = std::wstring(desktopPath) + L"\\RasBrowser.lnk";
        if (GetFileAttributesW(linkPath.c_str()) != INVALID_FILE_ATTRIBUTES) return; // Already exists

        CoInitialize(NULL);
        IShellLinkW* psl;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl))) {
            psl->SetPath(exePath);
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

void RegisterAppForDefaultBrowser() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    HKEY hKey;
    std::wstring regPath = L"Software\\Clients\\StartMenuInternet\\RasBrowser";
    RegCreateKeyExW(HKEY_CURRENT_USER, regPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    RegSetValueExW(hKey, L"", 0, REG_SZ, (const BYTE*)L"RasBrowser", 22);
    RegCloseKey(hKey);

    std::wstring cmd = L"\"" + std::wstring(exePath) + L"\"";
    RegCreateKeyExW(HKEY_CURRENT_USER, (regPath + L"\\shell\\open\\command").c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    RegSetValueExW(hKey, L"", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.length() * 2 + 2));
    RegCloseKey(hKey);

    std::wstring capPath = regPath + L"\\Capabilities";
    RegCreateKeyExW(HKEY_CURRENT_USER, capPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    RegSetValueExW(hKey, L"ApplicationName", 0, REG_SZ, (const BYTE*)L"RasBrowser", 22);
    RegSetValueExW(hKey, L"ApplicationDescription", 0, REG_SZ, (const BYTE*)L"RasBrowser Fast Web Browser", 56);
    RegCloseKey(hKey);

    RegCreateKeyExW(HKEY_CURRENT_USER, (capPath + L"\\URLAssociations").c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    RegSetValueExW(hKey, L"http", 0, REG_SZ, (const BYTE*)L"RasBrowser.Url", 30);
    RegSetValueExW(hKey, L"https", 0, REG_SZ, (const BYTE*)L"RasBrowser.Url", 30);
    RegCloseKey(hKey);

    RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\RasBrowser.Url", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    RegSetValueExW(hKey, L"", 0, REG_SZ, (const BYTE*)L"RasBrowser HTML Document", 50);
    RegCloseKey(hKey);

    RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\RasBrowser.Url\\shell\\open\\command", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    std::wstring cmd2 = cmd + L" \"%1\"";
    RegSetValueExW(hKey, L"", 0, REG_SZ, (const BYTE*)cmd2.c_str(), (DWORD)(cmd2.length() * 2 + 2));
    RegCloseKey(hKey);

    RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
    std::wstring capRegPath = L"Software\\Clients\\StartMenuInternet\\RasBrowser\\Capabilities";
    RegSetValueExW(hKey, L"RasBrowser", 0, REG_SZ, (const BYTE*)capRegPath.c_str(), (DWORD)(capRegPath.length() * 2 + 2));
    RegCloseKey(hKey);
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

    int navBtnArea    = S(8 + 36*3 + 8, dpi);
    int rightIconArea = S(36*3 + 12,    dpi);
    int addrH         = S(30,           dpi);
    int toolY         = TitleBarH(dpi);
    int addrY         = toolY + (ToolbarH(dpi) - addrH) / 2;
    int addrX         = navBtnArea;
    int addrW         = W - navBtnArea - rightIconArea - S(8, dpi);

    ShowWindow(wd.hAddressBar, SW_SHOW);
    SetWindowPos(wd.hAddressBar, NULL, addrX, addrY + S(1,dpi), addrW, addrH - S(2,dpi), SWP_NOZORDER | SWP_NOACTIVATE);

    if (wd.hAddrFont) DeleteObject(wd.hAddrFont);
    wd.hAddrFont = CreateFontW(S(15, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessage(wd.hAddressBar, WM_SETFONT, (WPARAM)wd.hAddrFont, TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
// FULLSCREEN TOGGLE (F11 hides taskbar, default maximizes normally)
// ─────────────────────────────────────────────────────────────────────────────
void ToggleFullScreen(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    DWORD style = GetWindowLong(hWnd, GWL_STYLE);

    if (!wd.isFullScreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(hWnd, &wd.wpPrev) &&
            GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hWnd, GWL_STYLE, style & ~(WS_CAPTION | WS_THICKFRAME));
            SetWindowPos(hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right  - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            wd.isFullScreen = true;
        }
    } else {
        SetWindowLong(hWnd, GWL_STYLE, style | WS_CAPTION | WS_THICKFRAME);
        SetWindowPlacement(hWnd, &wd.wpPrev);
        SetWindowPos(hWnd, NULL, 0,0,0,0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
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
            if (vk == VK_ESCAPE && g_windows.count(m_hWnd) && g_windows[m_hWnd].isFullScreen) {
                ToggleFullScreen(m_hWnd); args->put_Handled(TRUE);
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
        if (IsBlockedContent(input)) { SetWindowTextW(hWnd, L""); return 0; }

        std::wstring url;
        if (input.find(L"http://")  == 0 || input.find(L"https://") == 0) { url = input; } 
        else if (input.find(L'.') != std::wstring::npos && input.find(L' ') == std::wstring::npos) { url = L"https://" + input; } 
        else { url = L"https://www.google.com/search?q=" + input; }
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
// GDI+ HELPERS & CHROME PATH
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
// COLOR PALETTE (Chrome Light Theme)
// ─────────────────────────────────────────────────────────────────────────────
namespace Clr {
    static const Color BgFrame    (255, 222, 225, 230); // #DEE1E6 Outer frame / Inactive tabs
    static const Color BgToolbar  (255, 255, 255, 255); // #FFFFFF Toolbar & Active tab
    static const Color TabActive  (255, 255, 255, 255); // #FFFFFF
    static const Color TabHover   (255, 235, 236, 240); // #EBECF0
    static const Color TxtPrim    (255,  32,  33,  36); // #202124 Black text
    static const Color TxtDim     (255,  95,  99, 104); // #5F6368 Gray text
    static const Color AddrBg     (255, 241, 243, 244); // #F1F3F4 Address bar
    static const Color AccentBlue (255,  26, 115, 232); // Google blue
    static const Color DivLine    (255, 218, 220, 224);
    static const Color CloseHov   (255, 232,  17,  35); // Windows red hover
    static const Color TabCloseIc (255,  95,  99, 104);
    static const Color White      (255, 255, 255, 255);
    static const Color WinHov     ( 20,   0,   0,   0); // Subtle dark hover for light theme
    static const Color NavHov     ( 20,   0,   0,   0);
    static const Color TabHovCirc ( 20,   0,   0,   0);
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
    int toolH   = ToolbarH(dpi);
    int navH    = NavTotalH(dpi);
    int winBtnW = WinBtnW(dpi);

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // ── Background strips ──────────────────────────────────────────────────
    {
        SolidBrush bFrame(Clr::BgFrame);
        SolidBrush bTool (Clr::BgToolbar);
        g.FillRectangle(&bFrame, 0, 0, W, titleH);
        g.FillRectangle(&bTool,  0, titleH, W, toolH);
        Pen sepPen(Clr::DivLine, 1.0f);
        g.DrawLine(&sepPen, 0, navH - 1, W, navH - 1);
    }

    // ── Fonts ──────────────────────────────────────────────────────────────
    FontFamily ffSeg(L"Segoe UI");
    FontFamily ffMDL(L"Segoe MDL2 Assets");
    Font fNormal (&ffSeg, Sf(13.f, dpi), FontStyleRegular, UnitPixel);
    Font fSmall  (&ffSeg, Sf(12.f, dpi), FontStyleRegular, UnitPixel);
    Font fBrand  (&ffSeg, Sf(13.f, dpi), FontStyleBold,    UnitPixel);
    Font fIcon   (&ffMDL, Sf(14.f, dpi), FontStyleRegular, UnitPixel);
    Font fIconSm (&ffMDL, Sf(11.f, dpi), FontStyleRegular, UnitPixel);

    StringFormat sfC, sfL;
    sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);
    sfL.SetAlignment(StringAlignmentNear);   sfL.SetLineAlignment(StringAlignmentCenter);
    sfL.SetFormatFlags(StringFormatFlagsNoWrap); sfL.SetTrimming(StringTrimmingEllipsisCharacter);

    SolidBrush brPrim(Clr::TxtPrim), brDim(Clr::TxtDim), brBlue(Clr::AccentBlue), brWhite(Clr::White);

    // ── Title bar: Brand & Icon ─────────────────────────────────────────────
    {
        int circDia = S(16, dpi);
        int circX   = S(12, dpi);
        int circY   = (titleH - circDia) / 2;
        g.FillEllipse(&brBlue, (float)circX, (float)circY, (float)circDia, (float)circDia);
        g.DrawString(L"R", -1, &fIconSm, RectF((float)circX, (float)circY, (float)circDia, (float)circDia), &sfC, &brWhite);
        g.DrawString(L"RasBrowser", -1, &fBrand, RectF((float)(circX + circDia + S(8,dpi)), 0.f, (float)S(100, dpi), (float)titleH), &sfL, &brPrim);
    }

    // ── Window controls ────────────────────────────────────────────────────
    {
        int bx = W - winBtnW * 3;
        auto DrawWinBtn = [&](int x, bool hover, bool isClose, const wchar_t* ico) {
            if (hover) {
                SolidBrush hb(isClose ? Clr::CloseHov : Clr::WinHov);
                g.FillRectangle(&hb, x, 0, winBtnW, titleH);
            }
            SolidBrush txtClr(isClose && hover ? Clr::White : Clr::TxtPrim);
            g.DrawString(ico, -1, &fIconSm, RectF((float)x, 0.f, (float)winBtnW, (float)titleH), &sfC, &txtClr);
        };
        DrawWinBtn(bx,                wd.hMin,   false, L"\xE921");
        DrawWinBtn(bx + winBtnW,      wd.hMax,   false, IsZoomed(hWnd) ? L"\xE923" : L"\xE922");
        DrawWinBtn(bx + winBtnW * 2,  wd.hClose, true,  L"\xE8BB");
    }

    // ── Tabs (Now inside TitleBar) ─────────────────────────────────────────
    {
        int tc = (int)wd.tabs.size();
        float cornerR = Sf(8.f, dpi);
        for (int i = 0; i < tc; i++) {
            RECT tr = GetTabRect(W, i, tc, dpi);
            float tx = (float)tr.left, ty = (float)tr.top, tw = (float)(tr.right - tr.left), th = (float)(tr.bottom - tr.top);
            bool isActive = (i == wd.activeTab), isHover = (i == wd.hoverTabIndex);

            GraphicsPath tabPath;
            BuildChromeTabPath(tabPath, tx, ty, tw, th, cornerR);
            SolidBrush bTab(isActive ? Clr::TabActive : (isHover ? Clr::TabHover : Color(0,0,0,0)));
            if (isActive || isHover) g.FillPath(&bTab, &tabPath);

            float iconSz = Sf(16.f, dpi);
            float iconX  = tx + Sf((float)D_TAB_PAD + 4, dpi);
            float iconY  = ty + (th - iconSz) * 0.5f;
            SolidBrush fvBrush(isActive ? Clr::AccentBlue : Clr::TxtDim);
            g.FillEllipse(&fvBrush, iconX, iconY, iconSz, iconSz);

            SolidBrush tBrush(isActive ? Clr::TxtPrim : Clr::TxtDim);
            float titleX = iconX + iconSz + Sf(8.f, dpi);
            float closeW = Sf(24.f, dpi);
            float titleW = tw - (titleX - tx) - closeW;
            if (titleW > 0) g.DrawString(wd.tabs[i].title.c_str(), -1, &fSmall, RectF(titleX, ty, titleW, th), &sfL, &tBrush);

            if (isActive || isHover) {
                float cSz = Sf(18.f, dpi), cx = tx + tw - cSz - Sf(6.f, dpi), cy = ty + (th - cSz) * 0.5f;
                if (isHover && !isActive) { SolidBrush hbx(Clr::TabHovCirc); g.FillEllipse(&hbx, cx, cy, cSz, cSz); }
                SolidBrush xBrush(Clr::TabCloseIc);
                g.DrawString(L"\xE8BB", -1, &fIconSm, RectF(cx, cy, cSz, cSz), &sfC, &xBrush);
            }

            if (!isActive && i < tc - 1 && i+1 != wd.activeTab) {
                Pen divPen(Clr::DivLine, 1.0f);
                float dx = tx + tw - 1.f;
                g.DrawLine(&divPen, dx, ty + Sf(8.f,dpi), dx, ty + th - Sf(8.f,dpi));
            }
        }

        RECT ntr = GetNewTabBtnRect(W, tc, dpi);
        if (wd.hNewTab) { SolidBrush hb(Clr::WinHov); g.FillEllipse(&hb, (float)ntr.left, (float)ntr.top, (float)(ntr.right-ntr.left), (float)(ntr.bottom-ntr.top)); }
        g.DrawString(L"\xE710", -1, &fIconSm, RectF((float)ntr.left, (float)ntr.top, (float)(ntr.right-ntr.left), (float)(ntr.bottom-ntr.top)), &sfC, &brDim);
    }

    // ── Toolbar ────────────────────────────────────────────────────────────
    {
        int toolY = titleH, curX = S(8, dpi), btnSz = S(36, dpi), btnStep = S(38, dpi);
        auto DrawNavBtn = [&](bool hover, bool enabled, const wchar_t* ico, int& x) {
            if (hover && enabled) { SolidBrush hb(Clr::NavHov); g.FillEllipse(&hb, (float)(x+S(4,dpi)), (float)(toolY+S(4,dpi)), (float)S(28,dpi), (float)S(28,dpi)); }
            SolidBrush ic(enabled ? Clr::TxtPrim : Clr::DivLine);
            g.DrawString(ico, -1, &fIcon, RectF((float)x, (float)toolY, (float)btnSz, (float)toolH), &sfC, &ic);
            x += btnStep;
        };

        auto* atab = wd.active();
        DrawNavBtn(wd.hBack, atab && atab->canBack, L"\xE72B", curX);
        DrawNavBtn(wd.hFwd,  atab && atab->canFwd,  L"\xE72A", curX);
        DrawNavBtn(wd.hRel,  true,                  L"\xE72C", curX);

        {
            int addrX = curX + S(4,dpi), rightIX = W - S(36*3 + 12, dpi), addrW = rightIX - addrX - S(8,dpi), addrH = S(32, dpi);
            int addrY = toolY + (toolH - addrH) / 2;
            SolidBrush addrBg(Clr::AddrBg);
            GraphicsPath pill; AddRoundRect(pill, (float)addrX, (float)addrY, (float)addrW, (float)addrH, Sf(16.f, dpi));
            g.FillPath(&addrBg, &pill);
            g.DrawString(L"\xE72E", -1, &fIconSm, RectF((float)addrX + Sf(8.f,dpi), (float)addrY, Sf(20.f,dpi), (float)addrH), &sfC, &brDim);
        }

        int rx = W - S(38*3 + 8, dpi);
        auto DrawRightBtn = [&](bool hover, const wchar_t* ico, int x) {
            if (hover) { SolidBrush hb(Clr::NavHov); g.FillEllipse(&hb, (float)(x+S(4,dpi)), (float)(toolY+S(4,dpi)), (float)S(28,dpi), (float)S(28,dpi)); }
            g.DrawString(ico, -1, &fIcon, RectF((float)x, (float)toolY, (float)btnSz, (float)toolH), &sfC, &brPrim);
        };
        DrawRightBtn(wd.hExt, L"\xE9D2", rx); rx += btnStep;
        DrawRightBtn(wd.hDl,  L"\xE896", rx); rx += btnStep;
        DrawRightBtn(wd.hSet, L"\xE713", rx);
    }
}

void DrawBrowser(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    if (g_windows[hWnd].isFullScreen) return;
    DoubleBufferedPaint(hWnd, hdc, [&](HDC memDC, int W, int H) {
        HBRUSH hbg = CreateSolidBrush(RGB(222, 225, 230)); // Frame match
        RECT fr = { 0, 0, W, H }; FillRect(memDC, &fr, hbg); DeleteObject(hbg);
        DrawBrowserContent(hWnd, memDC);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// FORWARD DECLARATIONS & TAB MANAGEMENT
// ─────────────────────────────────────────────────────────────────────────────
static void SwitchToTab(HWND, int);
static void AddTab(HWND, std::wstring);
static void CloseTab(HWND, int);
static void CreateWebViewForTab(HWND, int);

static void SwitchToTab(HWND hWnd, int idx) {
    auto& wd = g_windows[hWnd];
    if (idx < 0 || idx >= (int)wd.tabs.size()) return;
    if (wd.activeTab != idx && wd.activeTab < (int)wd.tabs.size())
        if (wd.tabs[wd.activeTab].controller) wd.tabs[wd.activeTab].controller->put_IsVisible(FALSE);
    wd.activeTab = idx;
    auto& tab = wd.tabs[idx];
    if (tab.controller) {
        tab.controller->put_IsVisible(TRUE);
        tab.controller->put_Bounds(GetWebViewRect(hWnd));
    } else { CreateWebViewForTab(hWnd, idx); }
    if (wd.hAddressBar) SetWindowTextW(wd.hAddressBar, tab.url.c_str());
    RepositionAddressBar(hWnd);
    InvalidateRect(hWnd, NULL, FALSE);
}

static void CloseTab(HWND hWnd, int idx) {
    auto& wd = g_windows[hWnd];
    if (wd.tabs.empty()) return;
    if (wd.tabs[idx].controller) { wd.tabs[idx].controller->put_IsVisible(FALSE); wd.tabs[idx].controller->Close(); }
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
        tab.controller = ctl; ctl->get_CoreWebView2(&tab.webview);

        // White background for Webview
        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
            COREWEBVIEW2_COLOR bg = { 255, 255, 255, 255 }; ctl2->put_DefaultBackgroundColor(bg);
        }

        tab.webview->add_DocumentTitleChanged(Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx < (int)w.tabs.size()) {
                    LPWSTR docTitle = nullptr; sender->get_DocumentTitle(&docTitle);
                    if (docTitle) { w.tabs[m_tabIdx].title = docTitle; CoTaskMemFree(docTitle); InvalidateRect(m_hWnd, NULL, FALSE); }
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
            [this](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx == w.activeTab) {
                    LPWSTR src = nullptr; sender->get_Source(&src);
                    if (src) { w.tabs[m_tabIdx].url = src; if (w.hAddressBar) SetWindowTextW(w.hAddressBar, src); CoTaskMemFree(src); }
                }
                return S_OK;
            }).Get(), nullptr);

        tab.webview->add_HistoryChanged(Callback<ICoreWebView2HistoryChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w = g_windows[m_hWnd];
                if (m_tabIdx < (int)w.tabs.size()) {
                    BOOL canB=FALSE, canF=FALSE; sender->get_CanGoBack(&canB); sender->get_CanGoForward(&canF);
                    w.tabs[m_tabIdx].canBack = !!canB; w.tabs[m_tabIdx].canFwd = !!canF;
                    InvalidateRect(m_hWnd, NULL, FALSE);
                }
                return S_OK;
            }).Get(), nullptr);

        ComPtr<ICoreWebView2Controller3> ctl3;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl3)))) {
            EventRegistrationToken tok; ctl3->add_AcceleratorKeyPressed(new AcceleratorHandler(m_hWnd), &tok);
        }

        ctl->put_IsVisible(m_tabIdx == wd.activeTab ? TRUE : FALSE);
        ctl->put_Bounds(GetWebViewRect(m_hWnd));
        tab.webview->Navigate(m_startUrl.c_str());
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
        const wchar_t* udDir = L"C:\\ProgramData\\RasFocus\\.BrowserData";
        CreateDirectoryW(L"C:\\ProgramData\\RasFocus", NULL); CreateDirectoryW(udDir, NULL);
        CreateCoreWebView2EnvironmentWithOptions(nullptr, udDir, options.Get(), new EnvCompletedHandler(hWnd, tabIdx));
    }
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
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }; ScreenToClient(hWnd, &pt);
            RECT cr; GetClientRect(hWnd, &cr); UINT dpi = GetWndDpi(hWnd); int border = S(8, dpi);

            if (!g_windows.count(hWnd) || !g_windows[hWnd].isFullScreen) {
                if (pt.y < border && pt.x < border) return HTTOPLEFT;
                if (pt.y < border && pt.x >= cr.right-border) return HTTOPRIGHT;
                if (pt.y >= cr.bottom-border && pt.x < border) return HTBOTTOMLEFT;
                if (pt.y >= cr.bottom-border && pt.x >= cr.right-border) return HTBOTTOMRIGHT;
                if (pt.y < border) return HTTOP;
                if (pt.y >= cr.bottom-border) return HTBOTTOM;
                if (pt.x < border) return HTLEFT;
                if (pt.x >= cr.right-border) return HTRIGHT;

                if (pt.y < TitleBarH(dpi)) {
                    int winBtnX = cr.right - WinBtnW(dpi) * 3;
                    if (pt.x >= winBtnX) return HTCLIENT; // window buttons
                    
                    // Allow dragging empty space next to tabs
                    bool onTab = false; auto& wd = g_windows[hWnd]; int tc = (int)wd.tabs.size();
                    for(int i=0; i<tc; i++) {
                        RECT tr = GetTabRect(cr.right, i, tc, dpi);
                        if(pt.x >= tr.left && pt.x < tr.right) { onTab = true; break; }
                    }
                    if (onTab || pt.x < BrandW(dpi)) return HTCLIENT; // Inside Tab or Logo area
                    return HTCAPTION; // Empty title bar space is draggable
                }
                if (pt.y < NavTotalH(dpi)) return HTCLIENT;
            }
            return HTCLIENT;
        }
        return def;
    }

    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION) { ShowWindow(hWnd, IsZoomed(hWnd) ? SW_RESTORE : SW_MAXIMIZE); return 0; }
        break;

    case WM_CREATE: {
        MARGINS m = { 0, 0, 0, 1 }; DwmExtendFrameIntoClientArea(hWnd, &m);
        DWORD pref = DWMWCP_ROUND; DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
        BOOL dark = FALSE; DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)); // Light mode shadow
        break;
    }

    case WM_PAINT: { PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps); DrawBrowser(hWnd, hdc); EndPaint(hWnd, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_WINDOWPOSCHANGING: ((WINDOWPOS*)lParam)->flags |= SWP_NOCOPYBITS; break;

    case WM_CTLCOLOREDIT: {
        if (g_windows.count(hWnd) && (HWND)lParam == g_windows[hWnd].hAddressBar) {
            HDC hEdit = (HDC)wParam;
            SetTextColor(hEdit, RGB(32, 33, 36));   // Black Text
            SetBkColor  (hEdit, RGB(241, 243, 244)); // Light Grey BG
            static HBRUSH hBrAddr = CreateSolidBrush(RGB(241, 243, 244));
            return (LRESULT)hBrAddr;
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
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }

    case WM_DPICHANGED: {
        const RECT* r = (const RECT*)lParam;
        SetWindowPos(hWnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        RepositionAddressBar(hWnd);
        RECT wvr = GetWebViewRect(hWnd);
        if (g_windows.count(hWnd)) for (auto& tab : g_windows[hWnd].tabs) if (tab.controller) tab.controller->put_Bounds(wvr);
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd]; UINT dpi = GetWndDpi(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd, &cr); int W = cr.right; bool dirty = false;
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 }; TrackMouseEvent(&tme);

        int titleH = TitleBarH(dpi), toolY = titleH;
        {
            int bx = W - WinBtnW(dpi)*3;
            bool nm = (y < titleH && x >= bx && x < bx + WinBtnW(dpi));
            bool mx = (y < titleH && x >= bx + WinBtnW(dpi) && x < bx + WinBtnW(dpi)*2);
            bool cl = (y < titleH && x >= bx + WinBtnW(dpi)*2);
            if (wd.hMin!=nm||wd.hMax!=mx||wd.hClose!=cl) { wd.hMin=nm; wd.hMax=mx; wd.hClose=cl; dirty=true; }
        }
        {
            int tc = (int)wd.tabs.size(), prev = wd.hoverTabIndex; wd.hoverTabIndex = -1;
            for (int i = 0; i < tc; i++) {
                RECT tr = GetTabRect(W, i, tc, dpi);
                if (x >= tr.left && x < tr.right && y >= tr.top && y < tr.bottom) { wd.hoverTabIndex = i; break; }
            }
            if (prev != wd.hoverTabIndex) dirty = true;
        }
        {
            RECT ntr = GetNewTabBtnRect(W, (int)wd.tabs.size(), dpi);
            bool nt = (x>=ntr.left&&x<ntr.right&&y>=ntr.top&&y<ntr.bottom);
            if (wd.hNewTab != nt) { wd.hNewTab = nt; dirty = true; }
        }
        {
            int btnStep = S(38, dpi), cx = S(8, dpi);
            bool b  = (y>=toolY&&y<NavTotalH(dpi)&&x>=cx&&x<cx+S(36,dpi)); cx+=btnStep;
            bool f  = (y>=toolY&&y<NavTotalH(dpi)&&x>=cx&&x<cx+S(36,dpi)); cx+=btnStep;
            bool rl = (y>=toolY&&y<NavTotalH(dpi)&&x>=cx&&x<cx+S(36,dpi));
            if (wd.hBack!=b||wd.hFwd!=f||wd.hRel!=rl) { wd.hBack=b; wd.hFwd=f; wd.hRel=rl; dirty=true; }

            int rx = W - S(38*3+8, dpi);
            bool e  = (y>=toolY&&y<NavTotalH(dpi)&&x>=rx&&x<rx+S(36,dpi)); rx+=btnStep;
            bool dl = (y>=toolY&&y<NavTotalH(dpi)&&x>=rx&&x<rx+S(36,dpi)); rx+=btnStep;
            bool st = (y>=toolY&&y<NavTotalH(dpi)&&x>=rx&&x<rx+S(36,dpi));
            if (wd.hExt!=e||wd.hDl!=dl||wd.hSet!=st) { wd.hExt=e; wd.hDl=dl; wd.hSet=st; dirty=true; }
        }
        if (dirty) { RECT r = { 0, 0, W, NavTotalH(dpi) }; InvalidateRect(hWnd, &r, FALSE); }
        break;
    }

    case WM_MOUSELEAVE: {
        if (g_windows.count(hWnd)) {
            auto& wd = g_windows[hWnd];
            wd.hMin=wd.hMax=wd.hClose=wd.hBack=wd.hFwd=wd.hRel=wd.hExt=wd.hDl=wd.hSet=wd.hNewTab=false; wd.hoverTabIndex=-1;
            RECT cr; GetClientRect(hWnd, &cr); cr.bottom = NavTotalH(GetWndDpi(hWnd)); InvalidateRect(hWnd, &cr, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd]; UINT dpi = GetWndDpi(hWnd);
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd, &cr); int W = cr.right;

        if (wd.hMin)   { ShowWindow(hWnd, SW_MINIMIZE); break; }
        if (wd.hMax)   { ShowWindow(hWnd, IsZoomed(hWnd)?SW_RESTORE:SW_MAXIMIZE); break; }
        if (wd.hClose) { DestroyWindow(hWnd); break; }

        int tc = (int)wd.tabs.size();
        for (int i = 0; i < tc; i++) {
            RECT tr = GetTabRect(W, i, tc, dpi);
            if (x>=tr.left&&x<tr.right&&y>=tr.top&&y<tr.bottom) {
                if (x >= tr.right - S(26, dpi)) { CloseTab(hWnd, i); return 0; }
                SwitchToTab(hWnd, i); return 0;
            }
        }
        if (wd.hNewTab) { AddTab(hWnd, L"https://www.google.com"); break; }

        if (auto* tab = wd.active()) {
            if (wd.hBack && tab->webview && tab->canBack) tab->webview->GoBack();
            if (wd.hFwd  && tab->webview && tab->canFwd)  tab->webview->GoForward();
            if (wd.hRel  && tab->webview)                 tab->webview->Reload();
        }

        if (wd.hExt) MessageBoxW(hWnd, L"এক্সটেনশন মেনু।", L"Extensions", MB_OK);
        if (wd.hDl)  MessageBoxW(hWnd, L"ডাউনলোড প্যানেল।", L"Downloads", MB_OK);
        if (wd.hSet) { // Settings button opens Windows Default Apps!
            ShellExecuteW(NULL, L"open", L"ms-settings:defaultapps", NULL, NULL, SW_SHOWNORMAL);
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        if (!g_windows.count(hWnd)) break;
        UINT dpi = GetWndDpi(hWnd); int y = GET_Y_LPARAM(lParam);
        if (y < TitleBarH(dpi) && GET_X_LPARAM(lParam) > BrandW(dpi)) AddTab(hWnd, L"https://www.google.com");
        break;
    }

    case WM_GETMINMAXINFO: {
        auto* mm = (LPMINMAXINFO)lParam; mm->ptMinTrackSize.x = S(640, GetWndDpi(hWnd)); mm->ptMinTrackSize.y = S(480, GetWndDpi(hWnd));
        return 0;
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
// PUBLIC API 
// ─────────────────────────────────────────────────────────────────────────────
void LaunchMiniBrowser(std::wstring url, std::wstring /*title*/) {
    static ULONG_PTR gdiplusToken = 0;
    if (!gdiplusToken) { GdiplusStartupInput si; GdiplusStartup(&gdiplusToken, &si, nullptr); }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Auto-create Desktop Shortcut & Register Default App
    CreateDesktopShortcut();
    RegisterAppForDefaultBrowser();

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc  = {}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = ViewerWndProc;
        wc.hInstance = GetModuleHandle(NULL); wc.lpszClassName = L"RasBrowserWnd";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = (HBRUSH)CreateSolidBrush(RGB(255, 255, 255)); // White Background
        RegisterClassExW(&wc); registered = true;
    }

    HWND hWnd = CreateWindowExW(0, L"RasBrowserWnd", L"RasBrowser",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!hWnd) return;

    SetWindowLongW(hWnd, GWL_STYLE, GetWindowLongW(hWnd, GWL_STYLE) & ~WS_CAPTION);
    SendMessage(hWnd, WM_CREATE, 0, 0); // Trigger DWM shadow init explicitly

    auto& wd = g_windows[hWnd];
    HWND hEdit = CreateWindowExW(0, L"EDIT", L"https://www.google.com",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 100, 26,
        hWnd, (HMENU)IDC_ADDRESS_BAR, GetModuleHandle(NULL), NULL);
    SetWindowLongW(hEdit, GWL_STYLE, GetWindowLongW(hEdit, GWL_STYLE) & ~WS_BORDER);
    SetWindowTheme(hEdit, L"", L"");
    SetWindowSubclass(hEdit, AddrBarProc, 1, 0);
    wd.hAddressBar = hEdit;

    HICON hIco = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    if (hIco) { SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIco); SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco); }

    TabData firstTab; firstTab.url = url; firstTab.title = L"New Tab";
    wd.tabs.push_back(firstTab); wd.activeTab = 0;

    // Fast Maximized Show (Taskbar will remain visible)
    ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hWnd);

    RepositionAddressBar(hWnd);
    CreateWebViewForTab(hWnd, 0);
}
