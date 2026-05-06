// mini_browser.cpp — RasBrowser | Chrome-like tabbed browser using WebView2
// Features: Multi-tab support, address bar, back/fwd/reload, fullscreen, adult content blocking

#define _CRT_SECURE_NO_WARNINGS
#include "mini_browser.h"
#include <vector>
#include "html_tools.h"
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include <wrl.h>
#include <map>
#include <gdiplus.h>
#include <string>
#include <algorithm>
#include <windowsx.h>
#include <commctrl.h>
#include <sstream>

#pragma comment(lib, "comctl32.lib")

using namespace Microsoft::WRL;
using namespace Gdiplus;

#define IDI_APP_ICON      101
#define IDC_ADDRESS_BAR   1005
#define IDC_TAB_BAR       1006

extern bool  g_isPureViewerMode;
extern float g_scaleFactor;

// ──────────────────────────────────────────────────────────────────────────────
// CONSTANTS  — Chrome-style chrome heights
// ──────────────────────────────────────────────────────────────────────────────
static const int TITLEBAR_H  = 32;   // Window drag / title / Win-controls
static const int TABBAR_H    = 36;   // Tab strip (tabs + new-tab button)
static const int TOOLBAR_H   = 40;   // Navigation bar (back/fwd/rel + address + icons)
static const int NAV_TOTAL_H = TITLEBAR_H + TABBAR_H + TOOLBAR_H;

static const int TAB_W_MAX   = 220;  // Maximum tab width
static const int TAB_W_MIN   = 80;   // Minimum tab width (squished)
static const int TAB_PAD     = 10;   // Tab horizontal padding
static const int WIN_BTN_W   = 46;   // Width of each Win32 window control button

// ──────────────────────────────────────────────────────────────────────────────
// PER-TAB DATA
// ──────────────────────────────────────────────────────────────────────────────
struct TabData {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2>           webview;
    std::wstring title   = L"New Tab";
    std::wstring url     = L"https://www.google.com";
    bool         loading = false;
};

// ──────────────────────────────────────────────────────────────────────────────
// PER-WINDOW DATA
// ──────────────────────────────────────────────────────────────────────────────
struct BrowserWindowData {
    // Tabs
    std::vector<TabData> tabs;
    int                  activeTab = 0;

    // Window state
    bool            isFullScreen = false;
    WINDOWPLACEMENT wpPrev       = { sizeof(WINDOWPLACEMENT) };

    // Address bar (one shared EDIT control, repositioned)
    HWND hAddressBar = NULL;

    // Hit-test states for title-bar controls
    bool hMin = false, hMax = false, hClose = false;

    // Hit-test states for toolbar buttons
    bool hBack = false, hFwd = false, hRel = false;
    bool hExt  = false, hDl  = false, hSet = false;

    // Hit-test states for tabs
    int  hoverTabIndex  = -1;
    bool hNewTab        = false;

    // Returns the active tab (safe)
    TabData* active() {
        if (activeTab >= 0 && activeTab < (int)tabs.size())
            return &tabs[activeTab];
        return nullptr;
    }
};

static std::map<HWND, BrowserWindowData> g_windows;
static ComPtr<ICoreWebView2Environment>  g_sharedEnv;

// ──────────────────────────────────────────────────────────────────────────────
// ADULT / BLOCKED CONTENT FILTER
// ──────────────────────────────────────────────────────────────────────────────
bool IsBlockedContent(const std::wstring& text) {
    std::wstring lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    static const std::vector<std::wstring> kBadWords = {
        // English
        L"porn", L"xxx", L"sex", L"nude", L"nsfw", L"sexy", L"hentai", L"rule34",
        L"milf", L"blowjob", L"tits", L"boobs", L"pussy", L"dick", L"cock",
        L"escort", L"bdsm", L"fetish", L"erotica", L"dildo", L"webcam",
        L"camgirls", L"xvideos", L"pornhub", L"xnxx", L"xhamster", L"brazzers",
        L"onlyfans", L"playboy", L"chaturbate", L"stripchat", L"eporner",
        L"spankbang", L"redtube", L"youporn", L"mia khalifa", L"sunny leone",
        L"dani daniels", L"johnny sins", L"kendra lust",
        // Bangla / Romanised Bangla
        L"চটি", L"পর্ণ", L"সেক্স", L"নগ্ন", L"উলঙ্গ", L"বেশ্যা", L"মাগি",
        L"খানকি", L"যৌন", L"পর্ণগ্রাফি", L"রেন্ডি", L"চোদাচুতি",
        L"গরম ভিডিও", L"খারাপ ছবি", L"যৌন মিলন", L"যৌনাঙ্গ", L"চুদো", L"নগ্নতা",
        L"bhabi", L"chudai", L"bangla choti", L"panu", L"desi bhabi", L"mms",
        L"magi", L"choda", L"chodachudi", L"khanki", L"besha", L"randi",
        L"nengta", L"nangta", L"baal", L"vodai", L"bokachoda",
        // Indirect / suggestive
        L"hot dance", L"seductive dance", L"bikini", L"swimsuit", L"sexy dance",
        L"cleavage", L"bedroom scene", L"bath scene", L"semi nude", L"lingerie",
        L"erotic", L"navel show", L"deep neck", L"short dress sexy",
    };

    for (const auto& kw : kBadWords)
        if (lower.find(kw) != std::wstring::npos)
            return true;
    return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// GEOMETRY HELPERS
// ──────────────────────────────────────────────────────────────────────────────
static int CalcTabWidth(int windowW, int tabCount) {
    int available = windowW - (WIN_BTN_W * 3) - 40 - 28; // 40=logo area, 28=new-tab btn
    int w = (tabCount > 0) ? available / tabCount : TAB_W_MAX;
    return max(TAB_W_MIN, min(TAB_W_MAX, w));
}

static RECT GetTabRect(int windowW, int index, int tabCount) {
    int tw = CalcTabWidth(windowW, tabCount);
    int x  = 40 + index * tw; // 40px = logo/brand area
    RECT r = { x, TITLEBAR_H, x + tw, TITLEBAR_H + TABBAR_H };
    return r;
}

static RECT GetNewTabBtnRect(int windowW, int tabCount) {
    int tw = CalcTabWidth(windowW, tabCount);
    int x  = 40 + tabCount * tw;
    RECT r = { x, TITLEBAR_H + 6, x + 24, TITLEBAR_H + TABBAR_H - 6 };
    return r;
}

// Returns the content area rect for the active WebView
static RECT GetWebViewRect(HWND hWnd) {
    RECT b; GetClientRect(hWnd, &b);
    b.top += NAV_TOTAL_H;
    return b;
}

// ──────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR POSITIONING
// ──────────────────────────────────────────────────────────────────────────────
static void RepositionAddressBar(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (!wd.hAddressBar) return;

    RECT r; GetClientRect(hWnd, &r);
    int  w = r.right;

    // Nav buttons: back(36) fwd(36) rel(36) = 108; plus 8px left pad
    int navBtnArea = 8 + 36 * 3 + 4;
    // Right icons: ext(36) dl(36) set(36) plus 8px right pad
    int rightIconArea = 36 * 3 + 12;

    int addrX = navBtnArea;
    int addrW = w - navBtnArea - rightIconArea;
    int addrY = TITLEBAR_H + TABBAR_H + (TOOLBAR_H - 26) / 2;

    if (wd.isFullScreen)
        ShowWindow(wd.hAddressBar, SW_HIDE);
    else {
        ShowWindow(wd.hAddressBar, SW_SHOW);
        SetWindowPos(wd.hAddressBar, NULL, addrX + 4, addrY, addrW - 8, 26,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// FULLSCREEN TOGGLE
// ──────────────────────────────────────────────────────────────────────────────
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
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        wd.isFullScreen = false;
    }

    RepositionAddressBar(hWnd);

    // Resize every tab's WebView
    RECT wvr = GetWebViewRect(hWnd);
    if (wd.isFullScreen) { wvr.top = 0; } // covers full screen
    for (auto& tab : wd.tabs)
        if (tab.controller)
            tab.controller->put_Bounds(wvr);

    InvalidateRect(hWnd, NULL, TRUE);
}

// ──────────────────────────────────────────────────────────────────────────────
// ACCELERATOR KEY HANDLER (F11 / ESC for fullscreen)
// ──────────────────────────────────────────────────────────────────────────────
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
    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) override
    {
        COREWEBVIEW2_KEY_EVENT_KIND kind; args->get_KeyEventKind(&kind);
        if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN ||
            kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)
        {
            UINT vk; args->get_VirtualKey(&vk);
            if (vk == VK_F11)  { ToggleFullScreen(m_hWnd); args->put_Handled(TRUE); }
            if (vk == VK_ESCAPE && g_windows.count(m_hWnd) && g_windows[m_hWnd].isFullScreen)
                { ToggleFullScreen(m_hWnd); args->put_Handled(TRUE); }
        }
        return S_OK;
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR SUBCLASS — handles Enter key
// ──────────────────────────────────────────────────────────────────────────────
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

        // Build proper URL
        std::wstring url;
        if (input.find(L"http://") == 0 || input.find(L"https://") == 0) {
            url = input;
            // Append safe search to Google URLs
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
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ──────────────────────────────────────────────────────────────────────────────
// DRAWING — Chrome-inspired dark UI
// ──────────────────────────────────────────────────────────────────────────────

// Color palette (Chrome dark theme)
namespace Clr {
    static const Color BgTitle   (255, 32,  32,  32);   // #202020  title strip
    static const Color BgTabBar  (255, 32,  32,  32);   // #202020  tab strip
    static const Color BgToolbar (255, 41,  41,  41);   // #292929  toolbar
    static const Color TabActive (255, 41,  41,  41);   // #292929  active tab bg
    static const Color TabHover  (255, 55,  55,  55);   // #373737  hovered tab
    static const Color TabNormal (255, 32,  32,  32);   // #202020  idle tab
    static const Color TxtPrim   (255, 232, 232, 232);  // #E8E8E8  primary text
    static const Color TxtDim    (255, 138, 138, 138);  // #8A8A8A  dimmed text
    static const Color AddrBg    (255, 56,  56,  56);   // #383838  address bar bg
    static const Color AddrFocus (255, 48,  48,  48);   // #303030  focused addr
    static const Color HoverBtn  (255,255,255, 255, 30); // semi-transparent
    static const Color BrandBlue (255, 66, 133, 244);   // Google-blue accent
    static const Color DivLine   (255, 60,  60,  60);   // separator
    static const Color CloseHov  (255, 196,  43,  28);  // red close hover
    static const Color TabClose  (255, 160, 160, 160);  // tab X icon
}

// Helper: rounded-rect path (GDI+)
static void AddRoundRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    path.AddArc(x,         y,         r*2, r*2, 180, 90);
    path.AddArc(x+w-r*2,  y,         r*2, r*2, 270, 90);
    path.AddArc(x+w-r*2,  y+h-r*2,   r*2, r*2,   0, 90);
    path.AddArc(x,         y+h-r*2,   r*2, r*2,  90, 90);
    path.CloseFigure();
}

void DrawBrowser(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (wd.isFullScreen) return;

    RECT cr; GetClientRect(hWnd, &cr);
    int W = cr.right, H = NAV_TOTAL_H;

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // ── Background fills ──────────────────────────────────────────────────────
    SolidBrush bTitle(Clr::BgTitle);
    SolidBrush bTabBar(Clr::BgTabBar);
    SolidBrush bToolbar(Clr::BgToolbar);
    g.FillRectangle(&bTitle,   0, 0,              W, TITLEBAR_H);
    g.FillRectangle(&bTabBar,  0, TITLEBAR_H,     W, TABBAR_H);
    g.FillRectangle(&bToolbar, 0, TITLEBAR_H+TABBAR_H, W, TOOLBAR_H);

    // Bottom separator
    Pen sepPen(Clr::DivLine, 1.0f);
    g.DrawLine(&sepPen, 0, NAV_TOTAL_H-1, W, NAV_TOTAL_H-1);

    // ── Fonts ─────────────────────────────────────────────────────────────────
    FontFamily ffSeg(L"Segoe UI");
    FontFamily ffMDL(L"Segoe MDL2 Assets");
    Font fNormal (&ffSeg, 13, FontStyleRegular, UnitPixel);
    Font fSmall  (&ffSeg, 11, FontStyleRegular, UnitPixel);
    Font fIcon   (&ffMDL, 15, FontStyleRegular, UnitPixel);
    Font fIconSm (&ffMDL, 12, FontStyleRegular, UnitPixel);

    StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);
    StringFormat sfL; sfL.SetAlignment(StringAlignmentNear);   sfL.SetLineAlignment(StringAlignmentCenter);
    sfL.SetFormatFlags(StringFormatFlagsNoWrap);
    sfC.SetTrimming(StringTrimmingEllipsisCharacter);
    sfL.SetTrimming(StringTrimmingEllipsisCharacter);

    SolidBrush brPrim(Clr::TxtPrim);
    SolidBrush brDim (Clr::TxtDim);

    // ── Title bar: "RasBrowser" brand ────────────────────────────────────────
    {
        Font fBrand(&ffSeg, 12, FontStyleBold, UnitPixel);
        SolidBrush brBlue(Clr::BrandBlue);
        // Small favicon placeholder circle
        SolidBrush brCirc(Clr::BrandBlue);
        g.FillEllipse(&brCirc, 10, (TITLEBAR_H-14)/2, 14, 14);
        g.DrawString(L"R", -1, &fIconSm, RectF(10, (float)(TITLEBAR_H-14)/2, 14, 14), &sfC, &SolidBrush(Color(255,255,255,255)));
        g.DrawString(L"RasBrowser", -1, &fBrand,
            RectF(30, 0, 160, (float)TITLEBAR_H), &sfL, &brBlue);
    }

    // ── Title bar: Window controls (min / max / close) ────────────────────────
    {
        int bx = W - WIN_BTN_W * 3;
        // Min
        if (wd.hMin) { SolidBrush hb(Color(40,255,255,255)); g.FillRectangle(&hb, bx, 0, WIN_BTN_W, TITLEBAR_H); }
        g.DrawString(L"\xE921", -1, &fIconSm, RectF((float)bx, 0, (float)WIN_BTN_W, (float)TITLEBAR_H), &sfC, &brDim);
        // Max
        if (wd.hMax) { SolidBrush hb(Color(40,255,255,255)); g.FillRectangle(&hb, bx+WIN_BTN_W, 0, WIN_BTN_W, TITLEBAR_H); }
        const wchar_t* maxIco = IsZoomed(hWnd) ? L"\xE923" : L"\xE922";
        g.DrawString(maxIco, -1, &fIconSm, RectF((float)(bx+WIN_BTN_W), 0, (float)WIN_BTN_W, (float)TITLEBAR_H), &sfC, &brDim);
        // Close
        if (wd.hClose) { SolidBrush hb(Clr::CloseHov); g.FillRectangle(&hb, bx+WIN_BTN_W*2, 0, WIN_BTN_W, TITLEBAR_H); }
        SolidBrush closeClr(wd.hClose ? Color(255,255,255,255) : Clr::TxtDim);
        g.DrawString(L"\xE8BB", -1, &fIconSm, RectF((float)(bx+WIN_BTN_W*2), 0, (float)WIN_BTN_W, (float)TITLEBAR_H), &sfC, &closeClr);
    }

    // ── Tab strip ─────────────────────────────────────────────────────────────
    {
        int tabCount = (int)wd.tabs.size();
        int tabW     = CalcTabWidth(W, tabCount);

        for (int i = 0; i < tabCount; i++) {
            RECT tr = GetTabRect(W, i, tabCount);
            float tx = (float)tr.left, ty = (float)tr.top;
            float tw = (float)(tr.right - tr.left), th = (float)(tr.bottom - tr.top);

            bool isActive = (i == wd.activeTab);
            bool isHover  = (i == wd.hoverTabIndex);

            // Tab background (trapezoid via rounded rect at top, straight bottom)
            GraphicsPath tabPath;
            float cornerR = 8.0f;
            tabPath.AddArc(tx+2,      ty+4, cornerR*2, cornerR*2, 180, 90);
            tabPath.AddArc(tx+tw-2-cornerR*2, ty+4, cornerR*2, cornerR*2, 270, 90);
            tabPath.AddLine(tx+tw-2, ty+th, tx+2, ty+th);
            tabPath.CloseFigure();

            Color tabFill = isActive ? Clr::TabActive :
                            (isHover ? Clr::TabHover  : Clr::TabNormal);
            SolidBrush bTab(tabFill);
            g.FillPath(&bTab, &tabPath);

            // Active tab: top accent line (blue stripe)
            if (isActive) {
                SolidBrush accentBrush(Clr::BrandBlue);
                g.FillRectangle(&accentBrush, tx+2+cornerR, ty+4, tw-4-cornerR*2, 2.5f);
            }

            // Favicon placeholder (small colored circle)
            float iconX = tx + TAB_PAD + 2;
            float iconY = ty + (th - 14) / 2;
            SolidBrush fvBrush(isActive ? Clr::BrandBlue : Clr::TxtDim);
            g.FillEllipse(&fvBrush, iconX, iconY, 14.0f, 14.0f);

            // Tab title
            const auto& tab = wd.tabs[i];
            SolidBrush titleBrush(isActive ? Clr::TxtPrim : Clr::TxtDim);
            RectF titleRect(iconX + 18, ty, tw - iconX - 18 - 24, th);
            // Clamp the StringFormat to prevent going into close btn area
            StringFormat sfTab; sfTab.SetAlignment(StringAlignmentNear);
            sfTab.SetLineAlignment(StringAlignmentCenter);
            sfTab.SetTrimming(StringTrimmingEllipsisCharacter);
            sfTab.SetFormatFlags(StringFormatFlagsNoWrap);
            g.DrawString(tab.title.c_str(), -1, &fSmall, titleRect, &sfTab, &titleBrush);

            // Tab close (×) — visible on hover or active
            if (isActive || isHover) {
                float cx = tx + tw - 20, cy = ty + (th - 16) / 2;
                if (isHover && !isActive) { SolidBrush hb(Color(50,255,255,255)); g.FillEllipse(&hb, cx, cy, 16, 16); }
                SolidBrush xBrush(Clr::TabClose);
                g.DrawString(L"\xE8BB", -1, &fIconSm,
                    RectF(cx, cy, 16, 16), &sfC, &xBrush);
            }

            // Divider between non-adjacent tabs
            if (i < tabCount - 1 && i != wd.activeTab && i+1 != wd.activeTab) {
                Pen divPen(Clr::DivLine, 1.0f);
                g.DrawLine(&divPen, tr.right-1, TITLEBAR_H+8, tr.right-1, TITLEBAR_H+TABBAR_H-8);
            }
        }

        // New-tab (+) button
        RECT ntr = GetNewTabBtnRect(W, tabCount);
        if (wd.hNewTab) { SolidBrush hb(Color(40,255,255,255)); g.FillEllipse(&hb, (float)ntr.left, (float)ntr.top, (float)(ntr.right-ntr.left), (float)(ntr.bottom-ntr.top)); }
        g.DrawString(L"\xE710", -1, &fIconSm,
            RectF((float)ntr.left, (float)ntr.top,
                  (float)(ntr.right-ntr.left), (float)(ntr.bottom-ntr.top)),
            &sfC, &brDim);
    }

    // ── Toolbar ───────────────────────────────────────────────────────────────
    {
        int toolY  = TITLEBAR_H + TABBAR_H;
        int curX   = 8;
        float btnH = (float)TOOLBAR_H;

        auto DrawNavBtn = [&](bool hover, const wchar_t* ico, int& x) {
            if (hover) { SolidBrush hb(Color(35,255,255,255)); g.FillEllipse(&hb, (float)x+2, (float)toolY+4, 30.0f, 30.0f); }
            g.DrawString(ico, -1, &fIcon,
                RectF((float)x, (float)toolY, 34.0f, btnH), &sfC, &brPrim);
            x += 36;
        };

        DrawNavBtn(wd.hBack, L"\xE72B", curX);
        DrawNavBtn(wd.hFwd,  L"\xE72A", curX);
        DrawNavBtn(wd.hRel,  L"\xE72C", curX);

        // Address bar background pill (the actual EDIT is overlaid here)
        {
            int addrX = curX + 4;
            int rightIconsX = W - 36 * 3 - 12;
            int addrW = rightIconsX - addrX - 4;
            int addrY = toolY + (TOOLBAR_H - 30) / 2;
            SolidBrush addrBg(Clr::AddrBg);
            GraphicsPath pill;
            AddRoundRect(pill, (float)addrX, (float)addrY, (float)addrW, 30.0f, 15.0f);
            g.FillPath(&addrBg, &pill);
        }

        // Right toolbar icons
        int rx = W - 36 * 3 - 12;
        auto DrawRightBtn = [&](bool hover, const wchar_t* ico, int x) {
            if (hover) { SolidBrush hb(Color(35,255,255,255)); g.FillEllipse(&hb, (float)x+2, (float)toolY+4, 30.0f, 30.0f); }
            g.DrawString(ico, -1, &fIcon,
                RectF((float)x, (float)toolY, 34.0f, btnH), &sfC, &brPrim);
        };
        DrawRightBtn(wd.hExt, L"\xE9D2", rx);       rx += 36;
        DrawRightBtn(wd.hDl,  L"\xE896", rx);       rx += 36;
        DrawRightBtn(wd.hSet, L"\xE713", rx);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// TAB MANAGEMENT
// ──────────────────────────────────────────────────────────────────────────────
static void SwitchToTab(HWND hWnd, int idx);
static void AddTab(HWND hWnd, std::wstring url);
static void CloseTab(HWND hWnd, int idx);

// Forward declaration — defined after ViewerWndProc
static void CreateWebViewForTab(HWND hWnd, int tabIdx);

static void SwitchToTab(HWND hWnd, int idx) {
    auto& wd = g_windows[hWnd];
    if (idx < 0 || idx >= (int)wd.tabs.size()) return;

    // Hide old tab's controller
    if (wd.activeTab != idx && wd.activeTab < (int)wd.tabs.size()) {
        auto& old = wd.tabs[wd.activeTab];
        if (old.controller) old.controller->put_IsVisible(FALSE);
    }

    wd.activeTab = idx;
    auto& tab = wd.tabs[idx];

    if (tab.controller) {
        tab.controller->put_IsVisible(TRUE);
        RECT wvr = GetWebViewRect(hWnd);
        tab.controller->put_Bounds(wvr);
    } else {
        CreateWebViewForTab(hWnd, idx);
    }

    // Update address bar
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

    if (wd.tabs.empty()) {
        DestroyWindow(hWnd);
        return;
    }

    wd.activeTab = min(wd.activeTab, (int)wd.tabs.size() - 1);
    SwitchToTab(hWnd, wd.activeTab);
    InvalidateRect(hWnd, NULL, FALSE);
}

// ──────────────────────────────────────────────────────────────────────────────
// WEBVIEW2 CONTROLLER COMPLETED HANDLER
// ──────────────────────────────────────────────────────────────────────────────
class TabControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    HWND m_hWnd;
    int  m_tabIdx;
    std::wstring m_startUrl;
    ULONG m_ref = 1;
public:
    TabControllerHandler(HWND hWnd, int tabIdx, std::wstring url)
        : m_hWnd(hWnd), m_tabIdx(tabIdx), m_startUrl(url) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        *ppv = this; return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this; return r;
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* ctl) override {
        if (!g_windows.count(m_hWnd)) return S_OK;
        auto& wd = g_windows[m_hWnd];
        if (m_tabIdx >= (int)wd.tabs.size()) return S_OK;
        if (FAILED(hr) || !ctl) return S_OK;

        auto& tab = wd.tabs[m_tabIdx];
        tab.controller = ctl;
        ctl->get_CoreWebView2(&tab.webview);

        // Dark background
        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
            COREWEBVIEW2_COLOR bg = { 255, 30, 30, 30 };
            ctl2->put_DefaultBackgroundColor(bg);
        }

        // User-agent spoofing (Chrome)
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

        // Intercept new-window requests → same tab
        tab.webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    args->put_Handled(TRUE);
                    LPWSTR uri = nullptr; args->get_Uri(&uri);
                    if (uri) { sender->Navigate(uri); CoTaskMemFree(uri); }
                    return S_OK;
                }).Get(), nullptr);

        // Title changed
        tab.webview->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                    if (!g_windows.count(m_hWnd)) return S_OK;
                    auto& w = g_windows[m_hWnd];
                    if (m_tabIdx >= (int)w.tabs.size()) return S_OK;
                    LPWSTR docTitle = nullptr; sender->get_DocumentTitle(&docTitle);
                    if (docTitle) {
                        w.tabs[m_tabIdx].title = docTitle;
                        CoTaskMemFree(docTitle);
                        RECT navR = { 0, 0, 10000, NAV_TOTAL_H };
                        InvalidateRect(m_hWnd, &navR, FALSE);
                    }
                    return S_OK;
                }).Get(), nullptr);

        // URL changed → sync address bar if active
        tab.webview->add_SourceChanged(
            Callback<ICoreWebView2SourceChangedEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                    if (!g_windows.count(m_hWnd)) return S_OK;
                    auto& w = g_windows[m_hWnd];
                    if (m_tabIdx != w.activeTab) return S_OK;
                    LPWSTR src = nullptr; sender->get_Source(&src);
                    if (src && w.hAddressBar) { SetWindowTextW(w.hAddressBar, src); CoTaskMemFree(src); }
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
        if (nav == L"RAS_BROWSER")           nav = L"https://www.google.com";
        else if (nav == L"LOCAL_PDF_SPLIT")  { tab.webview->NavigateToString(HTML_PDF_SPLIT.c_str()); return S_OK; }
        else if (nav == L"LOCAL_PDF_MERGE")  { tab.webview->NavigateToString(HTML_PDF_MERGE.c_str()); return S_OK; }
        else if (nav == L"LOCAL_IMG_TO_PDF") { tab.webview->NavigateToString(HTML_IMG_TO_PDF.c_str()); return S_OK; }
        else if (nav == L"LOCAL_JOB_PHOTO")  { tab.webview->NavigateToString(HTML_JOB_PHOTO.c_str()); return S_OK; }
        else if (nav == L"LOCAL_JOB_SIGN")   { tab.webview->NavigateToString(HTML_JOB_SIGN.c_str()); return S_OK; }
        else if (nav == L"LOCAL_AGE_CALC")   { tab.webview->NavigateToString(HTML_AGE_CALC.c_str()); return S_OK; }
        else if (nav == L"LOCAL_COMPRESS_PDF"){ tab.webview->NavigateToString(HTML_COMPRESS_PDF.c_str()); return S_OK; }
        else if (nav == L"LOCAL_PHOTO_VIEWER"){ tab.webview->NavigateToString(HTML_PHOTO_VIEWER.c_str()); return S_OK; }

        tab.webview->Navigate(nav.c_str());
        return S_OK;
    }
};

static void CreateWebViewForTab(HWND hWnd, int tabIdx) {
    if (!g_windows.count(hWnd)) return;
    auto& wd  = g_windows[hWnd];
    auto& tab = wd.tabs[tabIdx];

    auto doCreate = [hWnd, tabIdx, &tab]() {
        g_sharedEnv->CreateCoreWebView2Controller(
            hWnd, new TabControllerHandler(hWnd, tabIdx, tab.url));
    };

    if (g_sharedEnv) {
        doCreate();
    } else {
        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        options->put_AdditionalBrowserArguments(
            L"--enable-features=msWebView2EnableExtensions "
            L"--enable-gpu-rasterization --enable-zero-copy "
            L"--disable-features=Translate");

        std::wstring udDir = L"C:\\ProgramData\\RasFocus\\.BrowserData";
        CreateDirectoryW(L"C:\\ProgramData\\RasFocus", NULL);
        CreateDirectoryW(udDir.c_str(), NULL);
        SetFileAttributesW(udDir.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

        CreateCoreWebView2EnvironmentWithOptions(
            nullptr, udDir.c_str(), options.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [doCreate](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                    if (SUCCEEDED(hr)) { g_sharedEnv = env; doCreate(); }
                    return S_OK;
                }).Get());
    }
}

static void AddTab(HWND hWnd, std::wstring url) {
    auto& wd = g_windows[hWnd];
    TabData tab;
    tab.url   = url;
    tab.title = L"New Tab";
    wd.tabs.push_back(tab);
    int newIdx = (int)wd.tabs.size() - 1;
    SwitchToTab(hWnd, newIdx);
    CreateWebViewForTab(hWnd, newIdx);
}

// ──────────────────────────────────────────────────────────────────────────────
// WINDOW PROCEDURE
// ──────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    // ── Custom chrome / borderless ────────────────────────────────────────────
    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT r; GetClientRect(hWnd, &r);
        int border = 8;

        if (!g_windows.count(hWnd) || !g_windows[hWnd].isFullScreen) {
            if (pt.y < border && pt.x < border)           return HTTOPLEFT;
            if (pt.y < border && pt.x >= r.right-border)  return HTTOPRIGHT;
            if (pt.y >= r.bottom-border && pt.x < border) return HTBOTTOMLEFT;
            if (pt.y >= r.bottom-border && pt.x >= r.right-border) return HTBOTTOMRIGHT;
            if (pt.y < border)              return HTTOP;
            if (pt.y >= r.bottom-border)    return HTBOTTOM;
            if (pt.x < border)              return HTLEFT;
            if (pt.x >= r.right-border)     return HTRIGHT;

            // Title bar: drag unless over window buttons
            if (pt.y < TITLEBAR_H) {
                int btnX = r.right - WIN_BTN_W * 3;
                if (pt.x >= btnX)  return HTCLIENT; // window buttons
                if (pt.x < 200)    return HTCLIENT; // brand area (click-through)
                return HTCAPTION;
            }
            // Tab strip & toolbar → client
            if (pt.y < NAV_TOTAL_H) return HTCLIENT;
        }
        return HTCLIENT;
    }

    // ── Paint ─────────────────────────────────────────────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        DrawBrowser(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // prevent flicker; we paint everything in WM_PAINT

    // ── Address bar text colour ───────────────────────────────────────────────
    case WM_CTLCOLOREDIT: {
        if (g_windows.count(hWnd) && (HWND)lParam == g_windows[hWnd].hAddressBar) {
            HDC hEdit = (HDC)wParam;
            SetTextColor(hEdit, RGB(232, 232, 232));
            SetBkColor  (hEdit, RGB(56,  56,  56));
            static HBRUSH hb = CreateSolidBrush(RGB(56, 56, 56));
            return (LRESULT)hb;
        }
        break;
    }

    // ── Resize ────────────────────────────────────────────────────────────────
    case WM_SIZE: {
        if (!g_windows.count(hWnd)) break;
        auto& wd = g_windows[hWnd];
        RepositionAddressBar(hWnd);
        RECT wvr = GetWebViewRect(hWnd);
        for (int i = 0; i < (int)wd.tabs.size(); i++) {
            if (wd.tabs[i].controller) {
                if (i == wd.activeTab)
                    wd.tabs[i].controller->put_Bounds(wvr);
            }
        }
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }

    // ── Mouse move (hover hit-testing) ────────────────────────────────────────
    case WM_MOUSEMOVE: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd];
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd, &cr);
        int W = cr.right;
        bool dirty = false;

        // Window buttons
        {
            int bx = W - WIN_BTN_W*3;
            bool nm = (y < TITLEBAR_H && x >= bx        && x < bx+WIN_BTN_W);
            bool mx = (y < TITLEBAR_H && x >= bx+WIN_BTN_W && x < bx+WIN_BTN_W*2);
            bool cl = (y < TITLEBAR_H && x >= bx+WIN_BTN_W*2);
            if (wd.hMin!=nm||wd.hMax!=mx||wd.hClose!=cl) { wd.hMin=nm;wd.hMax=mx;wd.hClose=cl; dirty=true; }
        }

        // Tabs hover
        {
            int tc = (int)wd.tabs.size();
            int prev = wd.hoverTabIndex; wd.hoverTabIndex = -1;
            for (int i = 0; i < tc; i++) {
                RECT tr = GetTabRect(W, i, tc);
                if (x >= tr.left && x < tr.right && y >= tr.top && y < tr.bottom)
                    { wd.hoverTabIndex = i; break; }
            }
            if (prev != wd.hoverTabIndex) dirty = true;
        }

        // New-tab button
        {
            RECT ntr = GetNewTabBtnRect(W, (int)wd.tabs.size());
            bool nt = (x >= ntr.left && x < ntr.right && y >= ntr.top && y < ntr.bottom);
            if (wd.hNewTab != nt) { wd.hNewTab = nt; dirty = true; }
        }

        // Toolbar nav buttons
        {
            int toolY = TITLEBAR_H + TABBAR_H;
            int cx = 8;
            bool b  = (y>=toolY&&y<NAV_TOTAL_H&&x>=cx&&x<cx+34); cx+=36;
            bool f  = (y>=toolY&&y<NAV_TOTAL_H&&x>=cx&&x<cx+34); cx+=36;
            bool rl = (y>=toolY&&y<NAV_TOTAL_H&&x>=cx&&x<cx+34);
            if (wd.hBack!=b||wd.hFwd!=f||wd.hRel!=rl) { wd.hBack=b;wd.hFwd=f;wd.hRel=rl; dirty=true; }

            int rx = W - 36*3 - 12;
            bool e  = (y>=toolY&&y<NAV_TOTAL_H&&x>=rx&&x<rx+34); rx+=36;
            bool dl = (y>=toolY&&y<NAV_TOTAL_H&&x>=rx&&x<rx+34); rx+=36;
            bool st = (y>=toolY&&y<NAV_TOTAL_H&&x>=rx&&x<rx+34);
            if (wd.hExt!=e||wd.hDl!=dl||wd.hSet!=st) { wd.hExt=e;wd.hDl=dl;wd.hSet=st; dirty=true; }
        }

        if (dirty) { RECT r={0,0,W,NAV_TOTAL_H}; InvalidateRect(hWnd,&r,FALSE); }
        break;
    }

    // ── Mouse leave: reset hover ───────────────────────────────────────────────
    case WM_MOUSELEAVE: {
        if (g_windows.count(hWnd)) {
            auto& wd = g_windows[hWnd];
            wd.hMin=wd.hMax=wd.hClose=false;
            wd.hBack=wd.hFwd=wd.hRel=false;
            wd.hExt=wd.hDl=wd.hSet=false;
            wd.hNewTab=false; wd.hoverTabIndex=-1;
            RECT cr; GetClientRect(hWnd,&cr); cr.bottom=NAV_TOTAL_H;
            InvalidateRect(hWnd,&cr,FALSE);
        }
        break;
    }

    // ── Left click ────────────────────────────────────────────────────────────
    case WM_LBUTTONDOWN: {
        if (!g_windows.count(hWnd) || g_windows[hWnd].isFullScreen) break;
        auto& wd = g_windows[hWnd];
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd,&cr); int W=cr.right;

        // Window controls
        if (wd.hMin)   { ShowWindow(hWnd, SW_MINIMIZE); break; }
        if (wd.hMax)   { ShowWindow(hWnd, IsZoomed(hWnd)?SW_RESTORE:SW_MAXIMIZE); break; }
        if (wd.hClose) { DestroyWindow(hWnd); break; }

        // Tab clicks
        {
            int tc = (int)wd.tabs.size();
            for (int i = 0; i < tc; i++) {
                RECT tr = GetTabRect(W, i, tc);
                if (x>=tr.left&&x<tr.right&&y>=tr.top&&y<tr.bottom) {
                    // Check close button (rightmost 20px of tab)
                    if (x >= tr.right-22) { CloseTab(hWnd, i); break; }
                    SwitchToTab(hWnd, i);
                    break;
                }
            }
        }

        // New-tab button
        if (wd.hNewTab) { AddTab(hWnd, L"https://www.google.com"); break; }

        // Toolbar nav
        if (auto* tab = wd.active()) {
            if (wd.hBack && tab->webview) tab->webview->GoBack();
            if (wd.hFwd  && tab->webview) tab->webview->GoForward();
            if (wd.hRel  && tab->webview) tab->webview->Reload();
        }

        // Right icons (stubs)
        if (wd.hExt) MessageBoxW(hWnd, L"এক্সটেনশন মেনু এখানে দেখাবে।", L"Extensions", MB_OK|MB_ICONINFORMATION);
        if (wd.hDl)  MessageBoxW(hWnd, L"ডাউনলোড প্যানেল এখানে দেখাবে।",  L"Downloads",  MB_OK|MB_ICONINFORMATION);
        if (wd.hSet) MessageBoxW(hWnd, L"সেটিংস মেনু এখানে দেখাবে।",       L"Settings",   MB_OK|MB_ICONINFORMATION);
        break;
    }

    // ── Double-click on tab bar: new tab ──────────────────────────────────────
    case WM_LBUTTONDBLCLK: {
        if (!g_windows.count(hWnd)) break;
        int y = GET_Y_LPARAM(lParam);
        if (y >= TITLEBAR_H && y < TITLEBAR_H+TABBAR_H)
            AddTab(hWnd, L"https://www.google.com");
        break;
    }

    // ── Minimum window size ───────────────────────────────────────────────────
    case WM_GETMINMAXINFO: {
        auto* mm = (LPMINMAXINFO)lParam;
        mm->ptMinTrackSize.x = 640;
        mm->ptMinTrackSize.y = 480;
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY: {
        if (g_windows.count(hWnd)) {
            for (auto& tab : g_windows[hWnd].tabs)
                if (tab.controller) tab.controller->Close();
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

// ──────────────────────────────────────────────────────────────────────────────
// PUBLIC API  —  LaunchMiniBrowser()
// ──────────────────────────────────────────────────────────────────────────────
void LaunchMiniBrowser(std::wstring url, std::wstring /*title*/) {
    // Register window class once
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc     = {};
        wc.lpfnWndProc   = ViewerWndProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.lpszClassName = L"RasBrowserWnd";
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.style         = CS_DBLCLKS;            // enable double-click messages
        RegisterClassW(&wc);
        registered = true;
    }

    HWND hWnd = CreateWindowExW(
        0, L"RasBrowserWnd", L"RasBrowser",
        WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU |
        WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 780,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    // Remove OS caption (we draw our own)
    SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) & ~WS_CAPTION);

    // Init window data
    auto& wd = g_windows[hWnd];

    // Address bar (shared EDIT control)
    HWND hEdit = CreateWindowExW(
        0, L"EDIT", L"https://www.google.com",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 100, 26, hWnd,
        (HMENU)IDC_ADDRESS_BAR, GetModuleHandle(NULL), NULL);

    SetWindowSubclass(hEdit, AddrBarProc, 1, 0);
    HFONT hFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    wd.hAddressBar = hEdit;

    // App icon
    HICON hIco = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    if (hIco) {
        SendMessage(hWnd, WM_SETICON, ICON_BIG,   (LPARAM)hIco);
        SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
    }

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    // Add first tab
    TabData firstTab;
    firstTab.url   = url;
    firstTab.title = L"New Tab";
    wd.tabs.push_back(firstTab);
    wd.activeTab = 0;

    RepositionAddressBar(hWnd);
    CreateWebViewForTab(hWnd, 0);
}
