// mini_browser.cpp — RasBrowser | Premium UI, Smart Omnibox, Dynamic Bookmarks
// FIXED: Build error (ICoreWebView2Settings2), Local NTP on startup, Gemini support,
//        Desktop shortcut creation DISABLED, Chrome-like Profile/Extensions/Settings menus,
//        Gemini Developer Mode, NewWindowRequested properly handled.

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
// FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────────────────────
static void SwitchToTab(HWND, int);
static void AddTab(HWND, std::wstring);
static void CloseTab(HWND, int);
static void CreateWebViewForTab(HWND, int);

// ─────────────────────────────────────────────────────────────────────────────
// 1. DYNAMIC LOCAL NTP
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetLocalNTP_HTML(bool isDark) {
    std::wstring bg        = isDark ? L"#1e1e24" : L"#f8fafc";
    std::wstring text      = isDark ? L"#ffffff" : L"#323232";
    std::wstring subText   = isDark ? L"#a0a0b0" : L"#666666";
    std::wstring boxBg     = isDark ? L"#2b2b36" : L"#ffffff";
    std::wstring boxBorder = isDark ? L"#444444" : L"#dcdfe6";
    std::wstring shadow    = isDark ? L"0 4px 12px rgba(0,0,0,0.3)" : L"0 4px 12px rgba(0,0,0,0.05)";
    std::wstring teal      = L"#0ca8b0";

    return L"<!DOCTYPE html>"
    L"<html><head><meta charset='utf-8'><title>New Tab</title><style>"
    L"body{margin:0;display:flex;flex-direction:column;align-items:center;justify-content:center;height:100vh;background:" + bg + L";font-family:'Segoe UI',sans-serif;color:" + text + L";}"
    L".logo{font-size:64px;font-weight:bold;margin-bottom:5px;letter-spacing:-1.5px;user-select:none;}"
    L".logo span{color:" + teal + L";}"
    L".subtitle{font-size:15px;color:" + subText + L";margin-bottom:35px;font-weight:500;letter-spacing:1px;text-transform:uppercase;}"
    L"form{width:100%;max-width:620px;position:relative;margin-bottom:50px;}"
    L".search-box{width:100%;padding:18px 50px;font-size:16px;border-radius:30px;border:1px solid " + boxBorder + L";background:" + boxBg + L";color:" + text + L";outline:none;box-shadow:" + shadow + L";box-sizing:border-box;transition:all 0.3s ease;}"
    L".search-box:focus{border-color:" + teal + L";box-shadow:0 4px 20px rgba(12,168,176,0.25);}"
    L".icon-search{position:absolute;left:20px;top:50%;transform:translateY(-50%);width:22px;fill:#9aa0a6;}"
    L".quick-links{display:flex;gap:30px;}"
    L".link-item{display:flex;flex-direction:column;align-items:center;text-decoration:none;color:" + text + L";font-size:14px;font-weight:600;transition:transform 0.2s;}"
    L".link-item:hover{transform:translateY(-5px);}"
    L".link-icon{width:56px;height:56px;border-radius:50%;background:" + boxBg + L";display:flex;align-items:center;justify-content:center;box-shadow:" + shadow + L";margin-bottom:12px;font-size:22px;color:" + teal + L";border:1px solid " + boxBorder + L";transition:all 0.3s ease;}"
    L".link-item:hover .link-icon{background:" + teal + L";color:#fff;border-color:" + teal + L";box-shadow:0 8px 15px rgba(12,168,176,0.3);}"
    L"</style></head><body>"
    L"<div class='logo'><span>Ras</span>Browser</div>"
    L"<div class='subtitle'>A Powerful &amp; Safe Browsing Experience</div>"
    L"<form action='https://www.google.com/search' method='GET'>"
    L"<svg class='icon-search' viewBox='0 0 24 24'><path d='M15.5 14h-.79l-.28-.27A6.471 6.471 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z'/></svg>"
    L"<input type='text' name='q' class='search-box' placeholder='Search securely or type a URL' autocomplete='off' autofocus />"
    L"</form>"
    L"<div class='quick-links'>"
    L"<a href='https://www.youtube.com' class='link-item'><div class='link-icon'>&#9654;</div>YouTube</a>"
    L"<a href='https://gemini.google.com' class='link-item'><div class='link-icon'>AI</div>Gemini</a>"
    L"<a href='https://www.facebook.com' class='link-item'><div class='link-icon'>f</div>Facebook</a>"
    L"<a href='https://chatgpt.com' class='link-item'><div class='link-icon'>&#129302;</div>ChatGPT</a>"
    L"<a href='https://github.com' class='link-item'><div class='link-icon'>&lt;/&gt;</div>GitHub</a>"
    L"</div>"
    L"</body></html>";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. DYNAMIC BLOCKED PAGE
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetBlocked_HTML(bool isDark) {
    std::wstring bg     = isDark ? L"#1a1a1f" : L"#f4f6f8";
    std::wstring text   = isDark ? L"#ffffff" : L"#323232";
    std::wstring boxBg  = isDark ? L"#2b2b36" : L"#ffffff";
    std::wstring red    = L"#e74c3c";
    std::wstring border = isDark ? L"#444444" : L"#e2e8f0";

    return L"<!DOCTYPE html>"
    L"<html><head><meta charset='utf-8'><title>Blocked</title><style>"
    L"body{margin:0;display:flex;align-items:center;justify-content:center;height:100vh;background:" + bg + L";font-family:'Segoe UI',sans-serif;color:" + text + L";}"
    L".container{max-width:600px;text-align:center;padding:40px;background:" + boxBg + L";border-radius:16px;box-shadow:0 10px 30px rgba(0,0,0,0.15);border:1px solid " + border + L";}"
    L".icon{font-size:70px;margin-bottom:10px;}"
    L"h1{margin:0 0 10px 0;color:" + red + L";font-size:32px;}"
    L"p{font-size:16px;color:#888;margin-bottom:30px;line-height:1.5;}"
    L".quote-box{background:rgba(12,168,176,0.1);border-left:4px solid #0ca8b0;padding:20px;border-radius:0 8px 8px 0;text-align:left;}"
    L".quote-title{font-size:14px;font-weight:bold;color:#0ca8b0;margin-bottom:10px;text-transform:uppercase;letter-spacing:1px;}"
    L".quote-text{font-size:18px;font-style:italic;line-height:1.6;font-weight:500;}"
    L"</style></head><body>"
    L"<div class='container'>"
    L"<div class='icon'>&#128683;</div>"
    L"<h1>Access Denied</h1>"
    L"<p>This content has been blocked by <b>RasFocus</b> to protect your mind and productivity.</p>"
    L"<div class='quote-box'>"
    L"<div class='quote-title'>&#128161; Motivational Quote</div>"
    L"<div class='quote-text' id='quote'></div>"
    L"</div></div>"
    L"<script>"
    L"const q=['\"Discipline is the bridge between goals and accomplishment.\" - Jim Rohn',"
    L"'\"You have power over your mind, not outside events.\" - Marcus Aurelius',"
    L"'\"The successful warrior is the average man with laser-like focus.\" - Bruce Lee',"
    L"'\"Control yourself or someone else will.\" - John C. Maxwell',"
    L"'\"It does not matter how slowly you go as long as you do not stop.\" - Confucius'];"
    L"document.getElementById('quote').innerText=q[Math.floor(Math.random()*q.length)];"
    L"</script></body></html>";
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. AI INJECT SCRIPT
// ─────────────────────────────────────────────────────────────────────────────
std::wstring GetAiInjectScript(const std::wstring& currentUrl) {
    std::wifstream in(L"rasfocus_ai_data.txt");
    if (!in.is_open()) return L"";

    bool isAiEngineActive=false,cbAiImageBlur=false,cbFemaleDetectWeb=false,cbFemaleDetectVideo=false;
    int aiSensitivityIdx=0;
    bool ytHideHome=false,ytHideShorts=false,ytHideComments=false,ytHideRecVideos=false,
         ytHideThumbnails=false,ytBlurThumbnails=false,ytHideSubs=false,ytHideExplore=false,
         ytHideTopBar=false,ytDisableEndCards=false,ytBlackWhiteMode=false,ytDisableAutoplay=false;
    bool ttHideExplore=false,ttHideLive=false,ttHideComments=false,ttHideSearch=false,ttBlackWhiteMode=false;
    bool igHideStories=false,igHideReels=false,igHideExplore=false,igHideComments=false,
         igHideSuggested=false,igBlackWhiteMode=false;

    in>>isAiEngineActive>>cbAiImageBlur>>cbFemaleDetectWeb>>cbFemaleDetectVideo>>aiSensitivityIdx;
    in>>ytHideHome>>ytHideShorts>>ytHideComments>>ytHideRecVideos>>ytHideThumbnails
      >>ytBlurThumbnails>>ytHideSubs>>ytHideExplore>>ytHideTopBar>>ytDisableEndCards
      >>ytBlackWhiteMode>>ytDisableAutoplay;
    in>>ttHideExplore>>ttHideLive>>ttHideComments>>ttHideSearch>>ttBlackWhiteMode;
    in>>igHideStories>>igHideReels>>igHideExplore>>igHideComments>>igHideSuggested>>igBlackWhiteMode;
    in.close();

    std::wstring css=L"";
    if (currentUrl.find(L"youtube.com")!=std::wstring::npos) {
        if (ytHideHome)        css+=L"ytd-browse[page-subtype='home']{display:none!important;}";
        if (ytHideShorts)      css+=L"ytd-reel-shelf-renderer,ytd-rich-shelf-renderer[is-shorts],a[title='Shorts']{display:none!important;}";
        if (ytHideComments)    css+=L"ytd-comments{display:none!important;}";
        if (ytHideRecVideos)   css+=L"ytd-watch-next-secondary-results-renderer{display:none!important;}";
        if (ytHideThumbnails)  css+=L"ytd-thumbnail{display:none!important;}";
        if (ytBlurThumbnails)  css+=L"ytd-thumbnail img{filter:blur(15px)!important;}";
        if (ytHideSubs)        css+=L"a[title='Subscriptions']{display:none!important;}";
        if (ytHideTopBar)      css+=L"ytd-masthead{display:none!important;}#page-manager{margin-top:0!important;}";
        if (ytDisableEndCards) css+=L".ytp-ce-element{display:none!important;}";
        if (ytBlackWhiteMode)  css+=L"html{filter:grayscale(100%)!important;}";
    } else if (currentUrl.find(L"tiktok.com")!=std::wstring::npos) {
        if (ttHideExplore)  css+=L"[data-e2e='nav-explore']{display:none!important;}";
        if (ttHideLive)     css+=L"[data-e2e='nav-live']{display:none!important;}";
        if (ttHideComments) css+=L".comment-container{display:none!important;}";
        if (ttBlackWhiteMode) css+=L"html{filter:grayscale(100%)!important;}";
    } else if (currentUrl.find(L"instagram.com")!=std::wstring::npos) {
        if (igHideReels)   css+=L"a[href*='/reels/']{display:none!important;}";
        if (igHideExplore) css+=L"a[href*='/explore/']{display:none!important;}";
        if (igBlackWhiteMode) css+=L"html{filter:grayscale(100%)!important;}";
    }

    if (css.empty()) return L"";
    return L"(function(){let s=document.createElement('style');s.innerHTML=\"" + css + L"\";document.head.appendChild(s);})();";
}

// ─────────────────────────────────────────────────────────────────────────────
// DPI HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static inline int S(int px, UINT dpi)         { return MulDiv(px,(int)dpi,96); }
static inline float Sf(float px, UINT dpi)    { return px*(float)dpi/96.0f; }
static UINT GetWndDpi(HWND hWnd) {
    UINT dpi=GetDpiForWindow(hWnd); return dpi?dpi:96;
}

// ─────────────────────────────────────────────────────────────────────────────
// LAYOUT CONSTANTS
// ─────────────────────────────────────────────────────────────────────────────
static const int D_TITLEBAR_H = 42;
static const int D_TOOLBAR_H  = 40;
static const int D_BOOKMARK_H = 32;
static const int D_TAB_W_MAX  = 240;
static const int D_TAB_W_MIN  = 80;
static const int D_TAB_PAD    = 10;
static const int D_WIN_BTN_W  = 46;
static const int D_LOGO_W     = 140;
static const int D_NEW_TAB_BTN= 28;

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
    bool                 isDarkMode   = true;
    WINDOWPLACEMENT      wpPrev       = { sizeof(WINDOWPLACEMENT) };
    HWND                 hAddressBar  = NULL;
    HFONT                hAddrFont    = NULL;

    bool hMin=false, hMax=false, hClose=false;
    bool hPin=false, hDark=false;
    bool isPinned = false;
    bool hBack=false, hFwd=false, hRel=false;
    bool hProfile=false, hExt=false, hMenu=false;
    int  hoverTabIndex = -1;
    bool hNewTab       = false;

    TabData* active() {
        if (activeTab>=0 && activeTab<(int)tabs.size()) return &tabs[activeTab];
        return nullptr;
    }
};

static std::map<HWND, BrowserWindowData> g_windows;
static ComPtr<ICoreWebView2Environment>  g_sharedEnv;

// ─────────────────────────────────────────────────────────────────────────────
// NAV HEIGHT
// ─────────────────────────────────────────────────────────────────────────────
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
// URL ENCODE
// ─────────────────────────────────────────────────────────────────────────────
static std::string utf8_encode(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8,0,wstr.data(),(int)wstr.size(),NULL,0,NULL,NULL);
    std::string s(n,0);
    WideCharToMultiByte(CP_UTF8,0,wstr.data(),(int)wstr.size(),s.data(),n,NULL,NULL);
    return s;
}
static std::wstring UrlEncode(const std::wstring& text) {
    std::string u = utf8_encode(text);
    std::wstringstream esc;
    for (unsigned char c : u) {
        if (isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') esc<<(wchar_t)c;
        else if (c==' ') esc<<L"+";
        else { wchar_t b[8]; swprintf(b,8,L"%%%02X",c); esc<<b; }
    }
    return esc.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// FIX: Desktop shortcut creation — DISABLED as requested
// ─────────────────────────────────────────────────────────────────────────────
static void CreateDesktopShortcut() {
    // DISABLED: Desktop shortcut creation turned off
}
static void RegisterAppForDefaultBrowser() {}

// ─────────────────────────────────────────────────────────────────────────────
// CONTENT BLOCKER
// ─────────────────────────────────────────────────────────────────────────────
bool IsBlockedContent(const std::wstring& text) {
    std::wstring lower = text;
    std::transform(lower.begin(),lower.end(),lower.begin(),::towlower);
    static const std::vector<std::wstring> kBad = {
        L"porn",L"xxx",L"sex",L"nude",L"nsfw",L"sexy",L"hentai",L"rule34",
        L"milf",L"blowjob",L"tits",L"boobs",L"pussy",L"dick",L"cock",
        L"escort",L"bdsm",L"fetish",L"erotica",L"dildo",L"webcam",
        L"camgirls",L"xvideos",L"pornhub",L"xnxx",L"xhamster",L"brazzers",
        L"onlyfans",L"playboy",L"chaturbate",L"stripchat",L"eporner"
    };
    for (const auto& kw : kBad) if (lower.find(kw)!=std::wstring::npos) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// GEOMETRY HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static int CalcTabWidth(int W, int tc, UINT dpi) {
    int avail = W - WinBtnW(dpi)*5 - LogoW(dpi) - S(D_NEW_TAB_BTN+16,dpi);
    int w = tc>0 ? avail/tc : S(D_TAB_W_MAX,dpi);
    return max(S(D_TAB_W_MIN,dpi), min(S(D_TAB_W_MAX,dpi),w));
}
static RECT GetTabRect(int W, int i, int tc, UINT dpi) {
    int tw=CalcTabWidth(W,tc,dpi), x=LogoW(dpi)+i*tw;
    RECT r={x,S(8,dpi),x+tw,TitleBarH(dpi)}; return r;
}
static RECT GetNewTabBtnRect(int W, int tc, UINT dpi) {
    int tw=CalcTabWidth(W,tc,dpi), x=LogoW(dpi)+tc*tw+S(4,dpi);
    int cy=S(8,dpi)+(TitleBarH(dpi)-S(8,dpi))/2, sz=S(22,dpi);
    RECT r={x,cy-sz/2,x+sz,cy+sz/2}; return r;
}
static RECT GetWebViewRect(HWND hWnd) {
    RECT b; GetClientRect(hWnd,&b); b.top+=NavTotalH(hWnd); return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR
// ─────────────────────────────────────────────────────────────────────────────
static void RepositionAddressBar(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (!wd.hAddressBar) return;
    if (g_isPureViewerMode || wd.isFullScreen) { ShowWindow(wd.hAddressBar,SW_HIDE); return; }

    UINT dpi=GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd,&cr); int W=cr.right;
    int navBtnArea=S(8+36*3+8,dpi), rightIconArea=S(38*3+12,dpi);
    int addrH=S(30,dpi), toolY=TitleBarH(dpi);
    int addrX=navBtnArea, addrW=W-navBtnArea-rightIconArea-S(8,dpi);
    int leftDecorW=S(35,dpi), rightDecorW=S(95,dpi);
    int editH=S(18,dpi), editY=toolY+(ToolbarH(dpi)-addrH)/2+(addrH-editH)/2;

    ShowWindow(wd.hAddressBar,SW_SHOW);
    SetWindowPos(wd.hAddressBar,NULL,addrX+leftDecorW,editY,
        addrW-leftDecorW-rightDecorW,editH,SWP_NOZORDER|SWP_NOACTIVATE);

    if (wd.hAddrFont) DeleteObject(wd.hAddrFont);
    wd.hAddrFont=CreateFontW(S(14,dpi),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    SendMessage(wd.hAddressBar,WM_SETFONT,(WPARAM)wd.hAddrFont,TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
// FULLSCREEN TOGGLE
// ─────────────────────────────────────────────────────────────────────────────
void ToggleFullScreen(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd=g_windows[hWnd];
    DWORD style=GetWindowLong(hWnd,GWL_STYLE);
    if (!wd.isFullScreen) {
        MONITORINFO mi={sizeof(mi)};
        if (GetWindowPlacement(hWnd,&wd.wpPrev)&&
            GetMonitorInfo(MonitorFromWindow(hWnd,MONITOR_DEFAULTTOPRIMARY),&mi)) {
            SetWindowLong(hWnd,GWL_STYLE,style&~(WS_CAPTION|WS_THICKFRAME));
            SetWindowPos(hWnd,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,
                mi.rcMonitor.right-mi.rcMonitor.left,
                mi.rcMonitor.bottom-mi.rcMonitor.top-2,
                SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
            wd.isFullScreen=true;
        }
    } else {
        SetWindowLong(hWnd,GWL_STYLE,style|WS_CAPTION|WS_THICKFRAME);
        SetWindowPlacement(hWnd,&wd.wpPrev);
        SetWindowPos(hWnd,NULL,0,0,0,0,
            SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
        wd.isFullScreen=false;
    }
    RepositionAddressBar(hWnd);
    RECT wvr=GetWebViewRect(hWnd);
    if (wd.isFullScreen) wvr.top=0;
    for (auto& t:wd.tabs) if (t.controller) t.controller->put_Bounds(wvr);
    InvalidateRect(hWnd,NULL,TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
// ACCELERATOR HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class AcceleratorHandler : public ICoreWebView2AcceleratorKeyPressedEventHandler {
    HWND m_hWnd; ULONG m_ref=1;
public:
    AcceleratorHandler(HWND h):m_hWnd(h){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid==IID_IUnknown||riid==__uuidof(ICoreWebView2AcceleratorKeyPressedEventHandler))
            {*ppv=this;AddRef();return S_OK;}
        *ppv=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&m_ref);}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r=InterlockedDecrement(&m_ref); if(!r)delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller*,
        ICoreWebView2AcceleratorKeyPressedEventArgs* args) override {
        COREWEBVIEW2_KEY_EVENT_KIND kind; args->get_KeyEventKind(&kind);
        if (kind==COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN||
            kind==COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
            UINT vk; args->get_VirtualKey(&vk);
            if (vk==VK_F11){ToggleFullScreen(m_hWnd);args->put_Handled(TRUE);}
            if (vk==VK_ESCAPE&&g_windows.count(m_hWnd)&&g_windows[m_hWnd].isFullScreen)
                {ToggleFullScreen(m_hWnd);args->put_Handled(TRUE);}
        }
        return S_OK;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ADDRESS BAR SUBCLASS
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK AddrBarProc(HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR){
    if (msg==WM_KEYDOWN&&wParam==VK_RETURN) {
        HWND hParent=GetParent(hWnd);
        if (!g_windows.count(hParent)) return 0;
        auto& wd=g_windows[hParent];
        auto* tab=wd.active();
        if (!tab||!tab->webview) return 0;

        wchar_t buf[2048]; GetWindowTextW(hWnd,buf,2048);
        std::wstring input=buf;
        input.erase(0,input.find_first_not_of(L" \t"));
        if (!input.empty()) input.erase(input.find_last_not_of(L" \t")+1);
        if (input.empty()) return 0;

        if (IsBlockedContent(input)) {
            SetWindowTextW(hWnd,L"blocked by rasfocus");
            tab->url=L"blocked by rasfocus";
            tab->webview->NavigateToString(GetBlocked_HTML(wd.isDarkMode).c_str());
            return 0;
        }

        std::wstring url;
        if      (input.find(L" ")!=std::wstring::npos)                            url=L"https://www.google.com/search?q="+UrlEncode(input);
        else if (input.find(L"http://")==0||input.find(L"https://")==0)            url=input;
        else if (input.find(L".")!=std::wstring::npos)                             url=L"https://"+input;
        else                                                                        url=L"https://www.google.com/search?q="+UrlEncode(input);

        tab->webview->Navigate(url.c_str());
        return 0;
    }
    if (msg==WM_NCPAINT) return 0;
    return DefSubclassProc(hWnd,msg,wParam,lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// DOUBLE-BUFFERED PAINT
// ─────────────────────────────────────────────────────────────────────────────
static void DoubleBufferedPaint(HWND hWnd,HDC hdcReal,std::function<void(HDC,int,int)> fn){
    RECT cr; GetClientRect(hWnd,&cr);
    int W=cr.right,H=cr.bottom;
    if(W<=0||H<=0)return;
    HDC hdcMem=CreateCompatibleDC(hdcReal);
    HBITMAP hBmp=CreateCompatibleBitmap(hdcReal,W,H);
    HBITMAP hOld=(HBITMAP)SelectObject(hdcMem,hBmp);
    fn(hdcMem,W,H);
    BitBlt(hdcReal,0,0,W,H,hdcMem,0,0,SRCCOPY);
    SelectObject(hdcMem,hOld); DeleteObject(hBmp); DeleteDC(hdcMem);
}

// ─────────────────────────────────────────────────────────────────────────────
// GDI+ PATH HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static void AddRoundRect(GraphicsPath& p,float x,float y,float w,float h,float r){
    if(r<=0.f){p.AddRectangle(RectF(x,y,w,h));return;}
    p.AddArc(x,y,r*2,r*2,180,90);
    p.AddArc(x+w-r*2,y,r*2,r*2,270,90);
    p.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90);
    p.AddArc(x,y+h-r*2,r*2,r*2,90,90);
    p.CloseFigure();
}
static void BuildChromeTabPath(GraphicsPath& path,float x,float y,float w,float h,float cr){
    float bl=x,br=x+w,top=y,bot=y+h,nw=cr*1.6f;
    path.StartFigure();
    path.AddLine(bl,bot,bl+nw,bot);
    path.AddBezier(bl+nw,bot,bl+nw*0.5f,bot,bl+cr*0.25f,bot-cr,bl+cr,top+cr);
    path.AddArc(bl+cr,top,cr*2,cr*2,180,90);
    path.AddLine(bl+cr*3,top,br-cr*3,top);
    path.AddArc(br-cr*3,top,cr*2,cr*2,270,90);
    path.AddBezier(br-cr,top+cr,br-cr*0.25f,bot-cr,br-nw*0.5f,bot,br-nw,bot);
    path.AddLine(br-nw,bot,br,bot);
    path.CloseFigure();
}

// ─────────────────────────────────────────────────────────────────────────────
// PROFILE / EXTENSION / SETTINGS / GEMINI MENU HELPERS
// ─────────────────────────────────────────────────────────────────────────────
static void ShowProfileMenu(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    // Show a context menu mimicking Chrome-like profile menu
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING|MF_GRAYED, 0, L"RasBrowser User");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 1001, L"Sync & Google services");
    AppendMenuW(hMenu, MF_STRING, 1002, L"Passwords");
    AppendMenuW(hMenu, MF_STRING, 1003, L"Payment methods");
    AppendMenuW(hMenu, MF_STRING, 1004, L"Addresses and more");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 1005, L"Open Gemini AI");
    AppendMenuW(hMenu, MF_STRING, 1006, L"Open ChatGPT");

    UINT dpi = GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd, &cr);
    int rx = cr.right - S(36*3+8, dpi);
    POINT pt = { rx, TitleBarH(dpi) + ToolbarH(dpi) };
    ClientToScreen(hWnd, &pt);

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD|TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);

    if (!g_windows.count(hWnd)) return;
    auto* tab = wd.active();
    if (!tab || !tab->webview) return;

    if      (cmd==1005) tab->webview->Navigate(L"https://gemini.google.com");
    else if (cmd==1006) tab->webview->Navigate(L"https://chatgpt.com");
}

static void ShowExtensionsMenu(HWND hWnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING|MF_GRAYED, 0, L"Extensions");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING|MF_GRAYED, 0, L"No extensions installed");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 2001, L"Manage Extensions");

    UINT dpi = GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd, &cr);
    int rx = cr.right - S(36*2+8, dpi);
    POINT pt = { rx, TitleBarH(dpi) + ToolbarH(dpi) };
    ClientToScreen(hWnd, &pt);

    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

static void ShowMainMenu(HWND hWnd) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 3001, L"New Tab\tCtrl+T");
    AppendMenuW(hMenu, MF_STRING, 3002, L"New Window");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 3003, L"History");
    AppendMenuW(hMenu, MF_STRING, 3004, L"Downloads\tCtrl+J");
    AppendMenuW(hMenu, MF_STRING, 3005, L"Bookmarks");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    // Gemini Developer Mode toggle
    AppendMenuW(hMenu, MF_STRING, 3010, L"Open Gemini (Developer Mode)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 3006, L"Settings");
    AppendMenuW(hMenu, MF_STRING, 3007, L"Help");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 3008, L"Exit");

    UINT dpi = GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd, &cr);
    int rx = cr.right - S(36+8, dpi);
    POINT pt = { rx, TitleBarH(dpi) + ToolbarH(dpi) };
    ClientToScreen(hWnd, &pt);

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD|TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);

    if (!g_windows.count(hWnd)) return;
    auto* tab = wd.active();

    switch (cmd) {
    case 3001: AddTab(hWnd, L"LOCAL_NTP"); break;
    case 3002: {
        // Open new browser window
        std::wstring newUrl = L"LOCAL_NTP";
        // We call LaunchMiniBrowser via a simple new window approach
        // (just add a tab in this window for now)
        AddTab(hWnd, L"LOCAL_NTP");
        break;
    }
    case 3003: if (tab&&tab->webview) tab->webview->Navigate(L"about:history"); break;
    case 3004: if (tab&&tab->webview) tab->webview->Navigate(L"about:downloads"); break;
    case 3006:
        MessageBoxW(hWnd,
            L"RasBrowser Settings\n\n"
            L"Dark Mode: Toggle with Moon/Sun icon\n"
            L"Always on Top: Toggle with Pin icon\n"
            L"Fullscreen: Press F11\n"
            L"New Tab: Double-click titlebar or Ctrl+T\n"
            L"Close Tab: Click X on tab\n\n"
            L"Gemini Developer Mode: Menu > Open Gemini (Developer Mode)",
            L"RasBrowser Settings", MB_OK|MB_ICONINFORMATION);
        break;
    case 3010:
        // Gemini Developer Mode: open with special user agent for full API access
        if (tab && tab->webview) {
            // Set developer-grade UA then navigate to Gemini
            ComPtr<ICoreWebView2Settings> s;
            if (SUCCEEDED(tab->webview->get_Settings(&s)) && s) {
                ComPtr<ICoreWebView2Settings2> s2;
                if (SUCCEEDED(s->QueryInterface(IID_PPV_ARGS(&s2)))) {
                    s2->put_UserAgent(
                        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                        L"AppleWebKit/537.36 (KHTML, like Gecko) "
                        L"Chrome/124.0.0.0 Safari/537.36");
                }
            }
            tab->webview->Navigate(L"https://gemini.google.com/app");
        }
        break;
    case 3008: DestroyWindow(hWnd); break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN DRAW FUNCTION
// ─────────────────────────────────────────────────────────────────────────────
static void DrawBrowserContent(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    auto& wd = g_windows[hWnd];
    if (wd.isFullScreen) return;

    UINT dpi=GetWndDpi(hWnd);
    RECT cr; GetClientRect(hWnd,&cr); int W=cr.right;
    int titleH=TitleBarH(dpi),toolH=ToolbarH(dpi),navH=NavTotalH(hWnd),winBtnW=WinBtnW(dpi);

    Color cBgFrame  =wd.isDarkMode?Color(255,30,30,30)   :Color(255,230,230,235);
    Color cBgTool   =wd.isDarkMode?Color(255,43,43,43)   :Color(255,255,255,255);
    Color cTxtPrim  =wd.isDarkMode?Color(255,255,255,255):Color(255,32,33,36);
    Color cTxtDim   =wd.isDarkMode?Color(255,154,156,160):Color(255,95,99,104);
    Color cTabActive=wd.isDarkMode?Color(255,43,43,43)   :Color(255,255,255,255);
    Color cTabHover =wd.isDarkMode?Color(255,45,45,45)   :Color(255,235,236,240);
    Color cAddrBg   =wd.isDarkMode?Color(255,26,26,26)   :Color(255,241,243,244);
    Color cAddrBord =wd.isDarkMode?Color(255,68,68,68)   :Color(255,160,180,210);
    Color cDivLine  =wd.isDarkMode?Color(255,45,45,45)   :Color(255,218,220,224);

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    SolidBrush bFrame(cBgFrame);
    g.FillRectangle(&bFrame,0,0,W,titleH);

    if (!g_isPureViewerMode) {
        SolidBrush bTool(cBgTool);
        g.FillRectangle(&bTool,0,titleH,W,toolH);
        Pen sepPen(cDivLine,1.0f);
        if (!(wd.active()&&(wd.active()->url==L"LOCAL_NTP"||wd.active()->url==L"about:blank")))
            g.DrawLine(&sepPen,0,navH-1,W,navH-1);
    }

    FontFamily ffSeg(L"Segoe UI"), ffMDL(L"Segoe MDL2 Assets");
    Font fSmall  (&ffSeg,Sf(12.f,dpi),FontStyleRegular,UnitPixel);
    Font fSmallBd(&ffSeg,Sf(12.f,dpi),FontStyleBold,UnitPixel);
    Font fBrand  (&ffSeg,Sf(16.f,dpi),FontStyleBold,UnitPixel);
    Font fIcon   (&ffMDL,Sf(14.f,dpi),FontStyleRegular,UnitPixel);
    Font fIconSm (&ffMDL,Sf(11.f,dpi),FontStyleRegular,UnitPixel);

    StringFormat sfC,sfL;
    sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);
    sfL.SetAlignment(StringAlignmentNear);   sfL.SetLineAlignment(StringAlignmentCenter);
    sfL.SetFormatFlags(StringFormatFlagsNoWrap);
    sfL.SetTrimming(StringTrimmingEllipsisCharacter);

    SolidBrush brPrim(cTxtPrim), brDim(cTxtDim);

    // Branding
    {
        int iconX=S(15,dpi);
        SolidBrush brTeal(Color(255,12,168,176));
        SolidBrush brWhite(Color(255,255,255,255));
        SolidBrush brDark2(Color(255,32,33,36));
        g.DrawString(L"Ras",-1,&fBrand,RectF((float)iconX,0.f,(float)S(40,dpi),(float)titleH),&sfL,&brTeal);
        g.DrawString(L"Browser",-1,&fBrand,RectF((float)iconX+S(32,dpi),0.f,(float)S(100,dpi),(float)titleH),&sfL,wd.isDarkMode?&brWhite:&brDark2);
    }

    // Window buttons
    {
        int bx=W-winBtnW*5;
        auto DrawWinBtn=[&](int x,bool hover,bool isClose,const wchar_t* ico){
            if (hover){
                SolidBrush hb(isClose?Color(255,232,17,35):(wd.isDarkMode?Color(50,255,255,255):Color(20,0,0,0)));
                g.FillRectangle(&hb,x,0,winBtnW,titleH);
            }
            SolidBrush tc2(isClose&&hover?Color(255,255,255,255):cTxtPrim);
            g.DrawString(ico,-1,&fIconSm,RectF((float)x,0.f,(float)winBtnW,(float)titleH),&sfC,&tc2);
        };
        DrawWinBtn(bx,           wd.hPin,   false, wd.isPinned?L"\xE840":L"\xE718");
        DrawWinBtn(bx+winBtnW,   wd.hDark,  false, wd.isDarkMode?L"\xE708":L"\xE706");
        DrawWinBtn(bx+winBtnW*2, wd.hMin,   false, L"\xE921");
        DrawWinBtn(bx+winBtnW*3, wd.hMax,   false, IsZoomed(hWnd)?L"\xE923":L"\xE922");
        DrawWinBtn(bx+winBtnW*4, wd.hClose, true,  L"\xE8BB");
    }

    if (!g_isPureViewerMode) {
        // Tab strip
        {
            int tc=(int)wd.tabs.size();
            float cornerR=Sf(8.f,dpi);
            for (int i=0;i<tc;i++) {
                RECT tr=GetTabRect(W,i,tc,dpi);
                float tx=(float)tr.left,ty=(float)tr.top,tw=(float)(tr.right-tr.left),th=(float)(tr.bottom-tr.top);
                bool isActive=(i==wd.activeTab), isHover=(i==wd.hoverTabIndex);
                GraphicsPath tp2;
                BuildChromeTabPath(tp2,tx,ty,tw,th,cornerR);
                if (isActive||isHover){
                    SolidBrush bTab(isActive?cTabActive:cTabHover);
                    g.FillPath(&bTab,&tp2);
                }
                float iconSz=Sf(14.f,dpi),iconX2=tx+Sf((float)D_TAB_PAD+4,dpi),iconY2=ty+(th-iconSz)*0.5f;
                SolidBrush fvBrush(isActive?Color(255,12,168,176):cTxtDim);
                g.FillEllipse(&fvBrush,iconX2,iconY2,iconSz,iconSz);
                const auto& tab2=wd.tabs[i];
                SolidBrush tBrush(isActive?cTxtPrim:cTxtDim);
                float titleX=iconX2+iconSz+Sf(6.f,dpi),closeW=Sf(24.f,dpi),titleW=tw-(titleX-tx)-closeW;
                if (titleW>0){
                    std::wstring dt=tab2.title;
                    if (dt.empty()||tab2.url==L"LOCAL_NTP"||tab2.url==L"about:blank") dt=L"New Tab";
                    if (tab2.url.find(L"blocked by rasfocus")!=std::wstring::npos) dt=L"Blocked";
                    g.DrawString(dt.c_str(),-1,&fSmall,RectF(titleX,ty,titleW,th),&sfL,&tBrush);
                }
                if (isActive||isHover){
                    float cSz=Sf(16.f,dpi),cx2=tx+tw-cSz-Sf(6.f,dpi),cy2=ty+(th-cSz)*0.5f;
                    if (isHover&&!isActive){SolidBrush hbx(Color(20,255,255,255));g.FillEllipse(&hbx,cx2,cy2,cSz,cSz);}
                    g.DrawString(L"\xE8BB",-1,&fIconSm,RectF(cx2,cy2,cSz,cSz),&sfC,&brDim);
                }
            }
            RECT ntr=GetNewTabBtnRect(W,tc,dpi);
            if (wd.hNewTab){SolidBrush hb(wd.isDarkMode?Color(50,255,255,255):Color(20,0,0,0));g.FillEllipse(&hb,(float)ntr.left,(float)ntr.top,(float)(ntr.right-ntr.left),(float)(ntr.bottom-ntr.top));}
            g.DrawString(L"\xE710",-1,&fIconSm,RectF((float)ntr.left,(float)ntr.top,(float)(ntr.right-ntr.left),(float)(ntr.bottom-ntr.top)),&sfC,&brDim);
        }

        // Toolbar
        {
            int toolY=titleH,curX=S(8,dpi),btnSz=S(32,dpi),btnStep=S(36,dpi);
            float btnHf=(float)toolH;
            auto DrawNavBtn=[&](bool hover,bool enabled,const wchar_t* ico,int& x){
                if (hover&&enabled){SolidBrush hb(wd.isDarkMode?Color(50,255,255,255):Color(20,0,0,0));g.FillEllipse(&hb,(float)(x+S(2,dpi)),(float)(toolY+S(4,dpi)),(float)S(28,dpi),(float)S(28,dpi));}
                SolidBrush ic2(enabled?cTxtPrim:cDivLine);
                g.DrawString(ico,-1,&fIcon,RectF((float)x,(float)toolY,(float)btnSz,btnHf),&sfC,&ic2);
                x+=btnStep;
            };
            auto* atab=wd.active();
            DrawNavBtn(wd.hBack,atab&&atab->canBack,L"\xE72B",curX);
            DrawNavBtn(wd.hFwd, atab&&atab->canFwd, L"\xE72A",curX);
            DrawNavBtn(wd.hRel, true,                L"\xE72C",curX);

            // Address bar background
            {
                int addrX=curX+S(4,dpi),rightIX=W-S(38*3+12,dpi);
                int addrW2=rightIX-addrX-S(8,dpi),addrH2=S(30,dpi),addrY=toolY+(toolH-addrH2)/2;
                SolidBrush addrBg(cAddrBg); Pen addrPen(cAddrBord,1.0f);
                GraphicsPath pill;
                AddRoundRect(pill,(float)addrX,(float)addrY,(float)addrW2,(float)addrH2,Sf(15.f,dpi));
                g.FillPath(&addrBg,&pill); g.DrawPath(&addrPen,&pill);
                SolidBrush gBrush(wd.isDarkMode?Color(255,200,200,200):Color(255,80,80,80));
                g.DrawString(L"G",-1,&fBrand,RectF((float)addrX+Sf(12.f,dpi),(float)addrY,Sf(20.f,dpi),(float)addrH2),&sfC,&gBrush);
                // AI Mode pill
                float aiW=Sf(85.f,dpi),aiH=(float)addrH2-Sf(6.f,dpi);
                float aiX=(float)addrX+addrW2-aiW-Sf(3.f,dpi),aiY=(float)addrY+Sf(3.f,dpi);
                GraphicsPath aiPill; AddRoundRect(aiPill,aiX,aiY,aiW,aiH,Sf(10.f,dpi));
                LinearGradientBrush aiBg(PointF(aiX,aiY),PointF(aiX+aiW,aiY),Color(255,12,168,176),Color(255,0,92,230));
                g.FillPath(&aiBg,&aiPill);
                SolidBrush aiTxt(Color(255,255,255,255));
                g.DrawString(L"\x2728 AI Mode",-1,&fSmallBd,RectF(aiX,aiY,aiW,aiH),&sfC,&aiTxt);
            }

            // Right buttons: Profile, Extensions, Menu
            int rx=W-S(36*3+8,dpi);
            auto DrawRightBtn=[&](bool hover,const wchar_t* ico,int x){
                if (hover){SolidBrush hb(wd.isDarkMode?Color(50,255,255,255):Color(20,0,0,0));g.FillEllipse(&hb,(float)(x+S(2,dpi)),(float)(toolY+S(4,dpi)),(float)S(28,dpi),(float)S(28,dpi));}
                g.DrawString(ico,-1,&fIcon,RectF((float)x,(float)toolY,(float)btnSz,btnHf),&sfC,&brPrim);
            };
            DrawRightBtn(wd.hProfile,L"\xE77B",rx); rx+=btnStep;
            DrawRightBtn(wd.hExt,    L"\xE9D2",rx); rx+=btnStep;
            DrawRightBtn(wd.hMenu,   L"\xE712",rx);
        }

        // Bookmark bar
        if (wd.active()&&(wd.active()->url==L"LOCAL_NTP"||wd.active()->url==L"about:blank"||
                           wd.active()->url.find(L"blocked by rasfocus")!=std::wstring::npos)) {
            int bmkY=titleH+toolH, bmkH=S(D_BOOKMARK_H,dpi);
            SolidBrush bmkBg(cBgTool); g.FillRectangle(&bmkBg,0,bmkY,W,bmkH);
            Pen sepPen2(cDivLine,1.0f); g.DrawLine(&sepPen2,0,bmkY+bmkH-1,W,bmkY+bmkH-1);
            SolidBrush brTxt(cTxtDim);
            // Bookmark items
            struct BMK { const wchar_t* icon; const wchar_t* label; int x; };
            BMK items[] = {
                {L"\xE8A4", L"Web Store",       15},
                {L"\xE8A4", L"RasFocus Admin", 120},
                {L"\uE7AD", L"Gemini AI",       270}, // Star icon for Gemini
            };
            for (auto& bm : items) {
                g.DrawString(bm.icon,-1,&fIconSm,RectF((float)S(bm.x,dpi),(float)bmkY,(float)S(20,dpi),(float)bmkH),&sfC,&brTxt);
                g.DrawString(bm.label,-1,&fSmall,RectF((float)S(bm.x+20,dpi),(float)bmkY,(float)S(100,dpi),(float)bmkH),&sfL,&brTxt);
            }
            g.DrawString(L"\xE838",-1,&fIconSm,RectF((float)(W-S(130,dpi)),(float)bmkY,(float)S(20,dpi),(float)bmkH),&sfC,&brTxt);
            g.DrawString(L"All Bookmarks",-1,&fSmall,RectF((float)(W-S(110,dpi)),(float)bmkY,(float)S(100,dpi),(float)bmkH),&sfL,&brTxt);
        }
    }
}

void DrawBrowser(HWND hWnd, HDC hdc) {
    if (!g_windows.count(hWnd)) return;
    if (g_windows[hWnd].isFullScreen) return;
    DoubleBufferedPaint(hWnd,hdc,[&](HDC memDC,int W,int H){
        bool dark=g_windows[hWnd].isDarkMode;
        HBRUSH hbg=CreateSolidBrush(dark?RGB(30,30,30):RGB(230,230,235));
        RECT fr={0,0,W,H}; FillRect(memDC,&fr,hbg); DeleteObject(hbg);
        DrawBrowserContent(hWnd,memDC);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB MANAGEMENT
// ─────────────────────────────────────────────────────────────────────────────
static void SwitchToTab(HWND hWnd, int idx) {
    auto& wd=g_windows[hWnd];
    if (idx<0||idx>=(int)wd.tabs.size()) return;
    if (wd.activeTab!=idx&&wd.activeTab<(int)wd.tabs.size())
        if (wd.tabs[wd.activeTab].controller)
            wd.tabs[wd.activeTab].controller->put_IsVisible(FALSE);
    wd.activeTab=idx;
    auto& tab=wd.tabs[idx];
    if (tab.controller) {
        tab.controller->put_IsVisible(TRUE);
        RECT wvr=GetWebViewRect(hWnd); tab.controller->put_Bounds(wvr);
    } else {
        CreateWebViewForTab(hWnd,idx);
    }
    if (wd.hAddressBar) {
        if (tab.url==L"LOCAL_NTP"||tab.url==L"about:blank"||
            tab.url.find(L"blocked by rasfocus")!=std::wstring::npos)
            SetWindowTextW(wd.hAddressBar,L"");
        else SetWindowTextW(wd.hAddressBar,tab.url.c_str());
    }
    RepositionAddressBar(hWnd);
    InvalidateRect(hWnd,NULL,TRUE);
}

static void CloseTab(HWND hWnd, int idx) {
    auto& wd=g_windows[hWnd];
    if (wd.tabs.empty()) return;
    auto& tab=wd.tabs[idx];
    if (tab.controller){tab.controller->put_IsVisible(FALSE);tab.controller->Close();}
    wd.tabs.erase(wd.tabs.begin()+idx);
    if (wd.tabs.empty()){DestroyWindow(hWnd);return;}
    wd.activeTab=min(wd.activeTab,(int)wd.tabs.size()-1);
    SwitchToTab(hWnd,wd.activeTab);
}

static void AddTab(HWND hWnd, std::wstring url) {
    auto& wd=g_windows[hWnd];
    TabData tab; tab.url=url; tab.title=L"New Tab";
    wd.tabs.push_back(tab);
    int newIdx=(int)wd.tabs.size()-1;
    SwitchToTab(hWnd,newIdx);
    CreateWebViewForTab(hWnd,newIdx);
}

// ─────────────────────────────────────────────────────────────────────────────
// WEBVIEW2 CONTROLLER HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class TabControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    HWND         m_hWnd;
    int          m_tabIdx;
    std::wstring m_startUrl;
    ULONG        m_ref=1;
public:
    TabControllerHandler(HWND h,int idx,std::wstring url):m_hWnd(h),m_tabIdx(idx),m_startUrl(std::move(url)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** ppv) override {*ppv=this;return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&m_ref);}
    ULONG STDMETHODCALLTYPE Release() override {ULONG r=InterlockedDecrement(&m_ref);if(!r)delete this;return r;}

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* ctl) override {
        if (FAILED(hr)||!ctl) return S_OK;
        if (!g_windows.count(m_hWnd)) return S_OK;
        auto& wd=g_windows[m_hWnd];
        if (m_tabIdx>=(int)wd.tabs.size()) return S_OK;

        auto& tab=wd.tabs[m_tabIdx];
        tab.controller=ctl;
        ctl->get_CoreWebView2(&tab.webview);

        // Background color
        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
            COREWEBVIEW2_COLOR bg=wd.isDarkMode?COREWEBVIEW2_COLOR{255,30,30,30}:COREWEBVIEW2_COLOR{255,255,255,255};
            ctl2->put_DefaultBackgroundColor(bg);
        }

        // ── Settings ──────────────────────────────────────────────────────────
        ICoreWebView2Settings* settings=nullptr;
        if (SUCCEEDED(tab.webview->get_Settings(&settings))&&settings) {
            settings->put_IsScriptEnabled(TRUE);
            settings->put_AreDefaultScriptDialogsEnabled(TRUE);
            settings->put_IsWebMessageEnabled(TRUE);
            settings->put_AreDefaultContextMenusEnabled(TRUE);
            settings->put_IsStatusBarEnabled(TRUE);

            // FIX: DOM Storage via Settings2 (fixes Gemini & Facebook storage)
            ComPtr<ICoreWebView2Settings2> s2;
            if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&s2)))) {
                // UserAgent: Chrome-compatible for Gemini login
                s2->put_UserAgent(
                    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                    L"AppleWebKit/537.36 (KHTML, like Gecko) "
                    L"Chrome/124.0.0.0 Safari/537.36");
            }

            // FIX BUILD ERROR: IsDomStorageEnabled is on Settings3, not Settings
            ComPtr<ICoreWebView2Settings3> s3;
            if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&s3)))) {
                s3->put_IsDomStorageEnabled(TRUE);
            }
        }

        // Anti-bot script (Gemini / Google login fix)
        tab.webview->AddScriptToExecuteOnDocumentCreated(
            L"Object.defineProperty(navigator,'webdriver',{get:()=>false});"
            L"window.chrome={runtime:{},loadTimes:function(){},csi:function(){},app:{}};"
            L"Object.defineProperty(navigator,'plugins',{get:()=>[1,2,3,4,5]});"
            L"Object.defineProperty(navigator,'languages',{get:()=>['en-US','en']});",
            nullptr);

        // NavigationStarting — block bad content
        tab.webview->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
                LPWSTR uri=nullptr; args->get_Uri(&uri);
                if (uri) {
                    std::wstring urlStr(uri);
                    if (IsBlockedContent(urlStr)) {
                        args->put_Cancel(TRUE);
                        if (g_windows.count(m_hWnd)) {
                            auto& w=g_windows[m_hWnd];
                            if (w.hAddressBar) SetWindowTextW(w.hAddressBar,L"blocked by rasfocus");
                            if (m_tabIdx>=0&&m_tabIdx<(int)w.tabs.size()) {
                                w.tabs[m_tabIdx].url=L"blocked by rasfocus";
                                w.tabs[m_tabIdx].webview->NavigateToString(GetBlocked_HTML(w.isDarkMode).c_str());
                            }
                        }
                    }
                    CoTaskMemFree(uri);
                }
                return S_OK;
            }).Get(),nullptr);

        // NewWindowRequested — open in new tab instead of popup
        tab.webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT{
                LPWSTR uri=nullptr; args->get_Uri(&uri);
                std::wstring newUrl = uri ? std::wstring(uri) : L"LOCAL_NTP";
                if (uri) CoTaskMemFree(uri);
                // Open in new tab
                if (g_windows.count(m_hWnd)) {
                    args->put_Handled(TRUE);
                    AddTab(m_hWnd, newUrl);
                }
                return S_OK;
            }).Get(),nullptr);

        // DocumentTitleChanged
        tab.webview->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2* sender,IUnknown*)->HRESULT{
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w=g_windows[m_hWnd];
                if (m_tabIdx>=(int)w.tabs.size()) return S_OK;
                LPWSTR dt=nullptr; sender->get_DocumentTitle(&dt);
                if (dt){w.tabs[m_tabIdx].title=dt;CoTaskMemFree(dt);InvalidateRect(m_hWnd,NULL,FALSE);}
                return S_OK;
            }).Get(),nullptr);

        // SourceChanged — update address bar & resize
        tab.webview->add_SourceChanged(
            Callback<ICoreWebView2SourceChangedEventHandler>(
            [this](ICoreWebView2* sender,ICoreWebView2SourceChangedEventArgs*)->HRESULT{
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w=g_windows[m_hWnd];
                if (m_tabIdx!=w.activeTab) return S_OK;
                LPWSTR src=nullptr; sender->get_Source(&src);
                if (src) {
                    std::wstring urlStr(src);
                    w.tabs[m_tabIdx].url=urlStr;
                    if (w.hAddressBar) {
                        if (urlStr==L"LOCAL_NTP"||urlStr==L"about:blank"||
                            urlStr.find(L"blocked by rasfocus")!=std::wstring::npos)
                            SetWindowTextW(w.hAddressBar,L"");
                        else SetWindowTextW(w.hAddressBar,src);
                    }
                    CoTaskMemFree(src);
                }
                if (m_tabIdx==w.activeTab) {
                    RECT wvr=GetWebViewRect(m_hWnd);
                    w.tabs[m_tabIdx].controller->put_Bounds(wvr);
                    InvalidateRect(m_hWnd,NULL,TRUE);
                }
                return S_OK;
            }).Get(),nullptr);

        // NavigationCompleted — inject AI filter CSS
        tab.webview->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2* sender,ICoreWebView2NavigationCompletedEventArgs*)->HRESULT{
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w=g_windows[m_hWnd];
                if (m_tabIdx>=(int)w.tabs.size()) return S_OK;
                std::wstring script=GetAiInjectScript(w.tabs[m_tabIdx].url);
                if (!script.empty()) sender->ExecuteScript(script.c_str(),nullptr);
                return S_OK;
            }).Get(),nullptr);

        // HistoryChanged
        tab.webview->add_HistoryChanged(
            Callback<ICoreWebView2HistoryChangedEventHandler>(
            [this](ICoreWebView2* sender,IUnknown*)->HRESULT{
                if (!g_windows.count(m_hWnd)) return S_OK;
                auto& w=g_windows[m_hWnd];
                if (m_tabIdx>=(int)w.tabs.size()) return S_OK;
                BOOL canB=FALSE,canF=FALSE;
                sender->get_CanGoBack(&canB); sender->get_CanGoForward(&canF);
                w.tabs[m_tabIdx].canBack=!!canB; w.tabs[m_tabIdx].canFwd=!!canF;
                InvalidateRect(m_hWnd,NULL,FALSE);
                return S_OK;
            }).Get(),nullptr);

        // F11 accelerator
        ComPtr<ICoreWebView2Controller3> ctl3;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl3)))) {
            EventRegistrationToken tok;
            ctl3->add_AcceleratorKeyPressed(new AcceleratorHandler(m_hWnd),&tok);
        }

        bool isActive=(m_tabIdx==wd.activeTab);
        ctl->put_IsVisible(isActive?TRUE:FALSE);
        RECT wvr=GetWebViewRect(m_hWnd);
        ctl->put_Bounds(wvr);

        // ── NAVIGATE ─────────────────────────────────────────────────────────
        // FIX: Always show Local NTP on startup, never navigate to google.com
        std::wstring nav=m_startUrl;

        // Treat empty/default values as LOCAL_NTP
        if (!g_isPureViewerMode &&
            (nav.empty() || nav==L"LOCAL_NTP" || nav==L"RAS_BROWSER" ||
             nav==L"about:blank" || nav==L"about:newtab"))
        {
            nav=L"LOCAL_NTP";
        }

        if (nav==L"LOCAL_NTP") {
            tab.url=L"LOCAL_NTP";
            tab.webview->NavigateToString(GetLocalNTP_HTML(wd.isDarkMode).c_str());
        } else {
            tab.webview->Navigate(nav.c_str());
        }

        return S_OK;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ENVIRONMENT HANDLER
// ─────────────────────────────────────────────────────────────────────────────
class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    HWND m_hWnd; int m_tabIdx; ULONG m_ref=1;
public:
    EnvCompletedHandler(HWND h,int i):m_hWnd(h),m_tabIdx(i){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** ppv) override{*ppv=this;return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&m_ref);}
    ULONG STDMETHODCALLTYPE Release() override{ULONG r=InterlockedDecrement(&m_ref);if(!r)delete this;return r;}
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,ICoreWebView2Environment* env) override {
        if (FAILED(hr)||!env) return S_OK;
        g_sharedEnv=env;
        if (!g_windows.count(m_hWnd)) return S_OK;
        auto& wd=g_windows[m_hWnd];
        auto& tab=wd.tabs[m_tabIdx];
        g_sharedEnv->CreateCoreWebView2Controller(m_hWnd,new TabControllerHandler(m_hWnd,m_tabIdx,tab.url));
        return S_OK;
    }
};

static void CreateWebViewForTab(HWND hWnd, int tabIdx) {
    if (!g_windows.count(hWnd)) return;
    auto& wd=g_windows[hWnd];
    auto& tab=wd.tabs[tabIdx];

    if (g_sharedEnv) {
        g_sharedEnv->CreateCoreWebView2Controller(hWnd,new TabControllerHandler(hWnd,tabIdx,tab.url));
    } else {
        auto options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        options->put_AdditionalBrowserArguments(
            L"--enable-features=msWebView2EnableExtensions "
            L"--enable-gpu-rasterization "
            L"--enable-zero-copy "
            L"--disable-features=Translate "
            // Gemini Fix: allow all cookies, storage, and disable automation flag
            L"--disable-blink-features=AutomationControlled "
            L"--enable-features=CookiesWithoutSameSiteMustBeSecure "
            L"--disable-web-security=false "
        );

        // User data dir in LocalAppData (required for Gemini login persistence)
        wchar_t appDataPath[MAX_PATH];
        SHGetFolderPathW(NULL,CSIDL_LOCAL_APPDATA,NULL,0,appDataPath);
        std::wstring udDir=std::wstring(appDataPath)+L"\\RasBrowserData";
        CreateDirectoryW(udDir.c_str(),NULL);

        HRESULT hr=CreateCoreWebView2EnvironmentWithOptions(
            nullptr,udDir.c_str(),options.Get(),new EnvCompletedHandler(hWnd,tabIdx));
        if (FAILED(hr))
            CreateCoreWebView2EnvironmentWithOptions(nullptr,nullptr,nullptr,new EnvCompletedHandler(hWnd,tabIdx));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DWM SHADOW
// ─────────────────────────────────────────────────────────────────────────────
static void ApplyDwmShadow(HWND hWnd) {
    MARGINS m={0,0,0,1}; DwmExtendFrameIntoClientArea(hWnd,&m);
    DWORD pref=DWMWCP_ROUND;
    DwmSetWindowAttribute(hWnd,DWMWA_WINDOW_CORNER_PREFERENCE,&pref,sizeof(pref));
}

// ─────────────────────────────────────────────────────────────────────────────
// WINDOW PROCEDURE
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_NCCALCSIZE:
        if (wParam==TRUE) return 0;
        break;

    case WM_NCHITTEST: {
        LRESULT def=DefWindowProcW(hWnd,msg,wParam,lParam);
        if (def==HTNOWHERE||def==HTCLIENT) {
            POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
            ScreenToClient(hWnd,&pt);
            RECT cr; GetClientRect(hWnd,&cr);
            UINT dpi=GetWndDpi(hWnd); int border=S(8,dpi);
            if (!g_windows.count(hWnd)||!g_windows[hWnd].isFullScreen) {
                if (pt.y<border&&pt.x<border)                         return HTTOPLEFT;
                if (pt.y<border&&pt.x>=cr.right-border)               return HTTOPRIGHT;
                if (pt.y>=cr.bottom-border&&pt.x<border)              return HTBOTTOMLEFT;
                if (pt.y>=cr.bottom-border&&pt.x>=cr.right-border)    return HTBOTTOMRIGHT;
                if (pt.y<border)            return HTTOP;
                if (pt.y>=cr.bottom-border) return HTBOTTOM;
                if (pt.x<border)            return HTLEFT;
                if (pt.x>=cr.right-border)  return HTRIGHT;
                if (g_isPureViewerMode) {
                    int wx=cr.right-WinBtnW(dpi)*5;
                    if (pt.y<TitleBarH(dpi)) return pt.x>=wx?HTCLIENT:HTCAPTION;
                    return HTCLIENT;
                }
                if (pt.y<TitleBarH(dpi)) {
                    int wx=cr.right-WinBtnW(dpi)*5;
                    if (pt.x>=wx) return HTCLIENT;
                    auto& wd=g_windows[hWnd]; int tc=(int)wd.tabs.size();
                    for(int i=0;i<tc;i++){RECT tr=GetTabRect(cr.right,i,tc,dpi);if(pt.x>=tr.left&&pt.x<tr.right)return HTCLIENT;}
                    if (pt.x<LogoW(dpi)) return HTCLIENT;
                    RECT ntr=GetNewTabBtnRect(cr.right,tc,dpi);
                    if (pt.x>=ntr.left&&pt.x<=ntr.right) return HTCLIENT;
                    return HTCAPTION;
                }
                if (pt.y<NavTotalH(hWnd)) return HTCLIENT;
            }
            return HTCLIENT;
        }
        return def;
    }

    case WM_NCLBUTTONDBLCLK:
        if (wParam==HTCAPTION){ShowWindow(hWnd,IsZoomed(hWnd)?SW_RESTORE:SW_MAXIMIZE);return 0;}
        break;

    case WM_CREATE: ApplyDwmShadow(hWnd); break;

    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hWnd,&ps);
        DrawBrowser(hWnd,hdc); EndPaint(hWnd,&ps); return 0;
    }

    case WM_ERASEBKGND: return 1;

    case WM_WINDOWPOSCHANGING: {
        auto* wp=(WINDOWPOS*)lParam; wp->flags|=SWP_NOCOPYBITS; break;
    }

    case WM_CTLCOLOREDIT: {
        if (g_windows.count(hWnd)&&(HWND)lParam==g_windows[hWnd].hAddressBar) {
            HDC he=(HDC)wParam; bool isDark=g_windows[hWnd].isDarkMode;
            if (isDark){SetTextColor(he,RGB(255,255,255));SetBkColor(he,RGB(26,26,26));static HBRUSH hBrD=CreateSolidBrush(RGB(26,26,26));return(LRESULT)hBrD;}
            else{SetTextColor(he,RGB(32,33,36));SetBkColor(he,RGB(241,243,244));static HBRUSH hBrL=CreateSolidBrush(RGB(241,243,244));return(LRESULT)hBrL;}
        }
        break;
    }

    case WM_SIZE: {
        if (!g_windows.count(hWnd)) break;
        auto& wd=g_windows[hWnd];
        RepositionAddressBar(hWnd);
        RECT wvr=GetWebViewRect(hWnd);
        for (int i=0;i<(int)wd.tabs.size();i++)
            if (wd.tabs[i].controller&&i==wd.activeTab)
                wd.tabs[i].controller->put_Bounds(wvr);
        InvalidateRect(hWnd,NULL,FALSE); break;
    }

    case WM_DPICHANGED: {
        const RECT* nr=(const RECT*)lParam;
        SetWindowPos(hWnd,NULL,nr->left,nr->top,nr->right-nr->left,nr->bottom-nr->top,SWP_NOZORDER|SWP_NOACTIVATE);
        RepositionAddressBar(hWnd);
        RECT wvr=GetWebViewRect(hWnd);
        if (g_windows.count(hWnd)) for (auto& t:g_windows[hWnd].tabs) if(t.controller)t.controller->put_Bounds(wvr);
        InvalidateRect(hWnd,NULL,TRUE); return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_windows.count(hWnd)||g_windows[hWnd].isFullScreen) break;
        auto& wd=g_windows[hWnd]; UINT dpi=GetWndDpi(hWnd);
        int x=GET_X_LPARAM(lParam),y=GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd,&cr); int W=cr.right; bool dirty=false;
        {TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,hWnd,0};TrackMouseEvent(&tme);}
        int titleH=TitleBarH(dpi),navH=NavTotalH(hWnd),winBtnW2=WinBtnW(dpi),toolY=titleH;
        {
            int bx=W-winBtnW2*5;
            bool p=(y<titleH&&x>=bx&&x<bx+winBtnW2);
            bool dk=(y<titleH&&x>=bx+winBtnW2&&x<bx+winBtnW2*2);
            bool nm=(y<titleH&&x>=bx+winBtnW2*2&&x<bx+winBtnW2*3);
            bool mx=(y<titleH&&x>=bx+winBtnW2*3&&x<bx+winBtnW2*4);
            bool cl=(y<titleH&&x>=bx+winBtnW2*4);
            if (wd.hPin!=p||wd.hDark!=dk||wd.hMin!=nm||wd.hMax!=mx||wd.hClose!=cl)
                {wd.hPin=p;wd.hDark=dk;wd.hMin=nm;wd.hMax=mx;wd.hClose=cl;dirty=true;}
        }
        if (!g_isPureViewerMode) {
            int tc=(int)wd.tabs.size(); int prev=wd.hoverTabIndex; wd.hoverTabIndex=-1;
            for(int i=0;i<tc;i++){RECT tr=GetTabRect(W,i,tc,dpi);if(x>=tr.left&&x<tr.right&&y>=tr.top&&y<tr.bottom){wd.hoverTabIndex=i;break;}}
            if(prev!=wd.hoverTabIndex)dirty=true;
            RECT ntr=GetNewTabBtnRect(W,(int)wd.tabs.size(),dpi);
            bool nt=(x>=ntr.left&&x<ntr.right&&y>=ntr.top&&y<ntr.bottom);
            if(wd.hNewTab!=nt){wd.hNewTab=nt;dirty=true;}
            int btnStep=S(36,dpi),cx=S(8,dpi);
            bool b=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(34,dpi));cx+=btnStep;
            bool f=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(34,dpi));cx+=btnStep;
            bool rl=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=cx&&x<cx+S(34,dpi));
            if(wd.hBack!=b||wd.hFwd!=f||wd.hRel!=rl){wd.hBack=b;wd.hFwd=f;wd.hRel=rl;dirty=true;}
            int rx=W-S(36*3+8,dpi);
            bool pr=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(34,dpi));rx+=btnStep;
            bool e=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(34,dpi));rx+=btnStep;
            bool m=(y>=toolY&&y<toolY+ToolbarH(dpi)&&x>=rx&&x<rx+S(34,dpi));
            if(wd.hProfile!=pr||wd.hExt!=e||wd.hMenu!=m){wd.hProfile=pr;wd.hExt=e;wd.hMenu=m;dirty=true;}
        }
        if(dirty){RECT r={0,0,W,navH};InvalidateRect(hWnd,&r,FALSE);}
        break;
    }

    case WM_MOUSELEAVE: {
        if (g_windows.count(hWnd)) {
            auto& wd=g_windows[hWnd];
            wd.hMin=wd.hMax=wd.hClose=false;
            wd.hBack=wd.hFwd=wd.hRel=false;
            wd.hPin=wd.hDark=wd.hProfile=wd.hExt=wd.hMenu=false;
            wd.hNewTab=false; wd.hoverTabIndex=-1;
            RECT cr; GetClientRect(hWnd,&cr); cr.bottom=NavTotalH(hWnd);
            InvalidateRect(hWnd,&cr,FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (!g_windows.count(hWnd)||g_windows[hWnd].isFullScreen) break;
        auto& wd=g_windows[hWnd]; UINT dpi=GetWndDpi(hWnd);
        int x=GET_X_LPARAM(lParam),y=GET_Y_LPARAM(lParam);
        RECT cr; GetClientRect(hWnd,&cr); int W=cr.right;

        if (wd.hMin)  {ShowWindow(hWnd,SW_MINIMIZE);break;}
        if (wd.hMax)  {ShowWindow(hWnd,IsZoomed(hWnd)?SW_RESTORE:SW_MAXIMIZE);break;}
        if (wd.hClose){DestroyWindow(hWnd);break;}
        if (wd.hPin)  {wd.isPinned=!wd.isPinned;SetWindowPos(hWnd,wd.isPinned?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);InvalidateRect(hWnd,NULL,TRUE);break;}
        if (wd.hDark) {
            wd.isDarkMode=!wd.isDarkMode;
            if (wd.active()&&wd.active()->controller) {
                ComPtr<ICoreWebView2Controller2> c2;
                if (SUCCEEDED(wd.active()->controller->QueryInterface(IID_PPV_ARGS(&c2)))) {
                    COREWEBVIEW2_COLOR bg=wd.isDarkMode?COREWEBVIEW2_COLOR{255,30,30,30}:COREWEBVIEW2_COLOR{255,255,255,255};
                    c2->put_DefaultBackgroundColor(bg);
                }
                std::wstring url2=wd.active()->url;
                if ((url2==L"LOCAL_NTP"||url2==L"about:blank")&&wd.active()->webview)
                    wd.active()->webview->NavigateToString(GetLocalNTP_HTML(wd.isDarkMode).c_str());
                else if (url2.find(L"blocked by rasfocus")!=std::wstring::npos&&wd.active()->webview)
                    wd.active()->webview->NavigateToString(GetBlocked_HTML(wd.isDarkMode).c_str());
            }
            InvalidateRect(hWnd,NULL,TRUE);
            if (wd.hAddressBar) InvalidateRect(wd.hAddressBar,NULL,TRUE);
            break;
        }

        if (!g_isPureViewerMode) {
            int tc=(int)wd.tabs.size();
            for (int i=0;i<tc;i++){
                RECT tr=GetTabRect(W,i,tc,dpi);
                if (x>=tr.left&&x<tr.right&&y>=tr.top&&y<tr.bottom){
                    if (x>=tr.right-S(26,dpi)){CloseTab(hWnd,i);return 0;}
                    SwitchToTab(hWnd,i);return 0;
                }
            }
            if (wd.hNewTab){AddTab(hWnd,L"LOCAL_NTP");break;}
            if (auto* tab=wd.active()) {
                if (wd.hBack&&tab->webview&&tab->canBack) tab->webview->GoBack();
                if (wd.hFwd &&tab->webview&&tab->canFwd)  tab->webview->GoForward();
                if (wd.hRel &&tab->webview)               tab->webview->Reload();
            }
            // Chrome-like menus
            if (wd.hProfile) { ShowProfileMenu(hWnd); break; }
            if (wd.hExt)     { ShowExtensionsMenu(hWnd); break; }
            if (wd.hMenu)    { ShowMainMenu(hWnd); break; }
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        if (!g_windows.count(hWnd)||g_isPureViewerMode) break;
        UINT dpi=GetWndDpi(hWnd);
        int y=GET_Y_LPARAM(lParam),x=GET_X_LPARAM(lParam);
        if (y<TitleBarH(dpi)&&x>LogoW(dpi)) AddTab(hWnd,L"LOCAL_NTP");
        break;
    }

    case WM_GETMINMAXINFO: {
        UINT dpi=GetWndDpi(hWnd); auto* mm=(LPMINMAXINFO)lParam;
        mm->ptMinTrackSize.x=S(640,dpi); mm->ptMinTrackSize.y=S(480,dpi);
        HMONITOR hMon=MonitorFromWindow(hWnd,MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi={sizeof(mi)};
        if (GetMonitorInfo(hMon,&mi)) {
            mm->ptMaxPosition.x=mi.rcWork.left-mi.rcMonitor.left;
            mm->ptMaxPosition.y=mi.rcWork.top-mi.rcMonitor.top;
            mm->ptMaxSize.x=mi.rcWork.right-mi.rcWork.left;
            mm->ptMaxSize.y=mi.rcWork.bottom-mi.rcWork.top-2;
        }
        return 0;
    }

    case WM_CLOSE: DestroyWindow(hWnd); break;

    case WM_DESTROY: {
        if (g_windows.count(hWnd)) {
            for (auto& t:g_windows[hWnd].tabs) if(t.controller)t.controller->Close();
            if (g_windows[hWnd].hAddrFont) DeleteObject(g_windows[hWnd].hAddrFont);
            g_windows.erase(hWnd);
        }
        if (g_isPureViewerMode&&g_windows.empty()) PostQuitMessage(0);
        break;
    }

    default: return DefWindowProcW(hWnd,msg,wParam,lParam);
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// PUBLIC API — LaunchMiniBrowser()
// ─────────────────────────────────────────────────────────────────────────────
void LaunchMiniBrowser(std::wstring url, std::wstring /*title*/) {
    static ULONG_PTR gdiplusToken=0;
    if (!gdiplusToken){GdiplusStartupInput si;GdiplusStartup(&gdiplusToken,&si,nullptr);}

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Desktop shortcut: DISABLED
    // CreateDesktopShortcut();
    RegisterAppForDefaultBrowser();

    static bool registered=false;
    if (!registered){
        WNDCLASSEXW wc={};
        wc.cbSize=sizeof(wc); wc.lpfnWndProc=ViewerWndProc;
        wc.hInstance=GetModuleHandle(NULL); wc.lpszClassName=L"RasBrowserWnd";
        wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.style=CS_DBLCLKS|CS_HREDRAW|CS_VREDRAW;
        wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassExW(&wc); registered=true;
    }

    HWND hWnd=CreateWindowExW(0,L"RasBrowserWnd",L"RasBrowser",
        WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_MAXIMIZEBOX|WS_MINIMIZEBOX|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
        CW_USEDEFAULT,CW_USEDEFAULT,1100,780,NULL,NULL,GetModuleHandle(NULL),NULL);
    if (!hWnd) return;

    SetWindowLongW(hWnd,GWL_STYLE,GetWindowLongW(hWnd,GWL_STYLE)&~WS_CAPTION);
    ApplyDwmShadow(hWnd);
    auto& wd=g_windows[hWnd];

    HWND hEdit=CreateWindowExW(0,L"EDIT",L"",
        WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_LEFT,
        0,0,100,26,hWnd,(HMENU)IDC_ADDRESS_BAR,GetModuleHandle(NULL),NULL);
    SetWindowLongW(hEdit,GWL_STYLE,GetWindowLongW(hEdit,GWL_STYLE)&~WS_BORDER);
    SetWindowTheme(hEdit,L"",L"");
    SetWindowSubclass(hEdit,AddrBarProc,1,0);
    wd.hAddressBar=hEdit;

    HICON hIco=LoadIcon(GetModuleHandle(NULL),MAKEINTRESOURCE(IDI_APP_ICON));
    if (hIco){SendMessage(hWnd,WM_SETICON,ICON_BIG,(LPARAM)hIco);SendMessage(hWnd,WM_SETICON,ICON_SMALL,(LPARAM)hIco);}

    // FIX: Always start with LOCAL_NTP, never google.com
    TabData firstTab;
    if (!g_isPureViewerMode &&
        (url.empty()||url==L"LOCAL_NTP"||url==L"RAS_BROWSER"||
         url==L"about:blank"||url==L"about:newtab"))
    {
        url=L"LOCAL_NTP";
    }
    firstTab.url=url; firstTab.title=L"New Tab";
    wd.tabs.push_back(firstTab); wd.activeTab=0;

    ShowWindow(hWnd,SW_SHOWMAXIMIZED);
    UpdateWindow(hWnd);
    RepositionAddressBar(hWnd);
    CreateWebViewForTab(hWnd,0);
}
// ─────────────────────────────────────────────────────────────────────────────
// END OF FILE
// ─────────────────────────────────────────────────────────────────────────────
