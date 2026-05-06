// mini_browser.cpp

#define _CRT_SECURE_NO_WARNINGS
#include "mini_browser.h"
#include <vector>
#include "html_tools.h"
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h" // 🟢 FIX: Environment Options এরর সলভ করার জন্য
#include <wrl.h>
#include <map>
#include <gdiplus.h>
#include <string>
#include <algorithm>
#include <windowsx.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

using namespace Microsoft::WRL;
using namespace Gdiplus;

#define IDI_APP_ICON 101
#define IDC_ADDRESS_BAR 1005

extern bool g_isPureViewerMode;
extern float g_scaleFactor;

static ComPtr<ICoreWebView2Environment> g_miniEnv = nullptr;

// --- Data Structure for Each Mini Browser Window ---
struct MiniBrowserData {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::wstring title;
    bool isFullScreen = false;
    WINDOWPLACEMENT wpPrev = { sizeof(WINDOWPLACEMENT) };
    
    bool isBrowserMode = false;
    HWND hAddressBar = NULL;
    
    // UI Hit States
    bool hBack = false, hFwd = false, hRel = false;
    bool hExt = false, hDl = false, hSet = false, hAddTab = false;
    bool hMin = false, hMax = false, hClose = false;
};

static std::map<HWND, MiniBrowserData> g_mbData;

// Chrome-like UI Dimensions
static const int TITLE_HEIGHT = 28;   
static const int TAB_HEIGHT = 34;     
static const int TOOLBAR_HEIGHT = 38; 
static const int NAV_HEIGHT = TITLE_HEIGHT + TAB_HEIGHT + TOOLBAR_HEIGHT; 

// ==========================================
// 🟢 ADULT BLOCK & KEYWORD LOGIC
// ==========================================
bool IsBlockedContent(const std::wstring& text) {
    std::wstring lowerText = text;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::towlower);
    
    // আপনার অ্যাডাল্ট ব্লক সেকশনের বেসিক কিওয়ার্ড লিস্ট (এখানে আরও যোগ করতে পারেন)
    const std::vector<std::wstring> badKeywords = { 
        L"porn", L"xxx", L"sex", L"xvideos", L"pornhub", L"brazzers", L"xhamster", L"nude", L"nsfw",  L"porn", L"xxx", L"sex", L"nude", L"nsfw", L"sexy", L"hentai", L"rule34", L"milf", 
    L"blowjob", L"tits", L"boobs", L"pussy", L"dick", L"cock", L"escort", L"bdsm", 
    L"fetish", L"erotica", L"dildo", L"webcam", L"camgirls", L"xvideos", L"pornhub", 
    L"xnxx", L"xhamster", L"brazzers", L"onlyfans", L"playboy", L"chaturbate", 
    L"stripchat", L"eporner", L"spankbang", L"redtube", L"youporn", L"mia khalifa", 
    L"sunny leone", L"dani daniels", L"johnny sins", L"kendra lust",
    L"চটি", L"পর্ণ", L"সেক্স", L"নগ্ন", L"উলঙ্গ", L"বেশ্যা", L"মাগি", L"খানকি", 
    L"যৌন", L"পর্ণগ্রাফি", L"রেন্ডি", L"চোদাচুতি", L"গরম ভিডিও", L"খারাপ ছবি",
    L"যৌন মিলন", L"যৌনাঙ্গ", L"চুদো", L"নগ্নতা",
    L"bhabi", L"chudai", L"bangla choti", L"panu", L"desi bhabi", L"mms", L"magi", 
    L"choda", L"chodachudi", L"khanki", L"besha", L"randi", L"nengta", L"nangta", 
    L"baal", L"vodai", L"bokachoda", L"kuttar bacha", L"shuarer bacha", L"kharap video", 
    L"hot dance", L"seductive dance", L"item song", L"belly dance", L"hot", 
    L"kissing scene", L"bikini", L"swimsuit", L"sexy dance", L"cleavage", L"hot scene", 
    L"romantic kiss", L"bedroom scene", L"bath scene", L"rain dance", L"bold scene", 
    L"semi nude", L"lingerie", L"erotic", L"hot song", L"romantic video hot", 
    L"navel show", L"deep neck", L"short dress sexy", L"unfaithful scene"
    };

    for (const auto& keyword : badKeywords) {
        if (lowerText.find(keyword) != std::wstring::npos) {
            return true; // খারাপ শব্দ পাওয়া গেছে
        }
    }
    return false;
}

// ==========================================
// FULL SCREEN LOGIC (F11 & ESC)
// ==========================================
void ToggleFullScreen(HWND hWnd) {
    if (g_mbData.find(hWnd) == g_mbData.end()) return;
    auto& data = g_mbData[hWnd];
    DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);

    if (!data.isFullScreen) {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(hWnd, &data.wpPrev) && GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hWnd, GWL_STYLE, dwStyle & ~(WS_CAPTION | WS_THICKFRAME));
            SetWindowPos(hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            data.isFullScreen = true;
            if (data.hAddressBar) ShowWindow(data.hAddressBar, SW_HIDE);
        }
    } else {
        SetWindowLong(hWnd, GWL_STYLE, dwStyle | WS_CAPTION | WS_THICKFRAME);
        SetWindowPlacement(hWnd, &data.wpPrev);
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        data.isFullScreen = false;
        if (data.hAddressBar) ShowWindow(data.hAddressBar, SW_SHOW);
    }
    
    if (data.controller) {
        RECT b; GetClientRect(hWnd, &b);
        if (!data.isFullScreen) b.top += NAV_HEIGHT;
        data.controller->put_Bounds(b);
    }
    InvalidateRect(hWnd, NULL, TRUE);
}

class AcceleratorHandler : public ICoreWebView2AcceleratorKeyPressedEventHandler {
    HWND m_hWnd;
    ULONG m_refCount = 1;
public:
    AcceleratorHandler(HWND hWnd) : m_hWnd(hWnd) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2AcceleratorKeyPressedEventHandler)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r==0) delete this; return r; }
    
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller* sender, ICoreWebView2AcceleratorKeyPressedEventArgs* args) override {
        COREWEBVIEW2_KEY_EVENT_KIND kind;
        args->get_KeyEventKind(&kind);
        if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
            UINT virtualKey; args->get_VirtualKey(&virtualKey);
            if (virtualKey == VK_ESCAPE) {
                if (g_mbData[m_hWnd].isFullScreen) { ToggleFullScreen(m_hWnd); args->put_Handled(TRUE); }
            } else if (virtualKey == VK_F11) {
                ToggleFullScreen(m_hWnd); args->put_Handled(TRUE);
            }
        }
        return S_OK;
    }
};

LRESULT CALLBACK AddressBarSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        HWND hParent = GetParent(hWnd);
        if (g_mbData.count(hParent) && g_mbData[hParent].webview) {
            wchar_t urlBuf[2048];
            GetWindowTextW(hWnd, urlBuf, 2048);
            std::wstring urlStr = urlBuf;
            
            // 🟢 BLOCK LOGIC: খারাপ শব্দ থাকলে ইনপুট ফাঁকা করে দাও
            if (IsBlockedContent(urlStr)) {
                SetWindowTextW(hWnd, L""); // লেখা ক্লিয়ার করে দেওয়া হলো
                return 0; // সার্চ বাতিল
            }

            // 🟢 SAFE SEARCH LOGIC
            if (urlStr.find(L"http://") != 0 && urlStr.find(L"https://") != 0) {
                if (urlStr.find(L".") != std::wstring::npos && urlStr.find(L" ") == std::wstring::npos) {
                    urlStr = L"https://" + urlStr;
                } else {
                    // গুগলে সার্চ করলে safe=active যুক্ত করা হলো
                    urlStr = L"https://www.google.com/search?q=" + urlStr + L"&safe=active";
                }
            } else if (urlStr.find(L"google.com/search") != std::wstring::npos) {
                // লিংকে আগে থেকেই গুগল সার্চ থাকলে সেফ সার্চ যুক্ত করো
                if (urlStr.find(L"&safe=active") == std::wstring::npos) {
                    urlStr += L"&safe=active";
                }
            }

            g_mbData[hParent].webview->Navigate(urlStr.c_str());
        }
        return 0;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ==========================================
// 🎨 CHROME-LIKE UI DRAWING
// ==========================================
void DrawMiniBrowserNav(HWND hWnd, HDC hdc) {
    auto& data = g_mbData[hWnd];
    if (data.isFullScreen) return;

    RECT r; GetClientRect(hWnd, &r);
    int w = r.right - r.left;

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    SolidBrush bgTitle(Color(255, 20, 20, 20));   
    SolidBrush bgToolbar(Color(255, 40, 40, 40)); 
    g.FillRectangle(&bgTitle, 0, 0, w, TITLE_HEIGHT + TAB_HEIGHT);
    g.FillRectangle(&bgToolbar, 0, TITLE_HEIGHT + TAB_HEIGHT, w, TOOLBAR_HEIGHT);
    
    Pen borderPen(Color(255, 60, 60, 60), 1.0f);
    g.DrawLine(&borderPen, 0, NAV_HEIGHT - 1, w, NAV_HEIGHT - 1);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcon(L"Segoe MDL2 Assets");
    Font fTabTitle(&ff, 12, FontStyleRegular, UnitPixel);
    Font fIcon(&ffIcon, 16, FontStyleRegular, UnitPixel);
    Font fIconSml(&ffIcon, 12, FontStyleRegular, UnitPixel);
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);

    SolidBrush textWhite(Color(255, 240, 240, 240));
    SolidBrush textGray(Color(255, 150, 150, 150));
    SolidBrush hoverBg(Color(50, 255, 255, 255));
    SolidBrush closeHoverBg(Color(255, 232, 17, 35)); 

    int btnW = 45;
    int rightControlsX = w - (btnW * 3);
    
    if(data.hMin) g.FillRectangle(&hoverBg, rightControlsX, 0, btnW, TITLE_HEIGHT);
    g.DrawString(L"\xE921", -1, &fIconSml, RectF((float)rightControlsX, 0.0f, (float)btnW, (float)TITLE_HEIGHT), &fmtC, &textGray);
    
    if(data.hMax) g.FillRectangle(&hoverBg, rightControlsX + btnW, 0, btnW, TITLE_HEIGHT);
    const wchar_t* maxIcon = IsZoomed(hWnd) ? L"\xE923" : L"\xE922";
    g.DrawString(maxIcon, -1, &fIconSml, RectF((float)(rightControlsX + btnW), 0.0f, (float)btnW, (float)TITLE_HEIGHT), &fmtC, &textGray);
    
    if(data.hClose) g.FillRectangle(&closeHoverBg, rightControlsX + btnW*2, 0, btnW, TITLE_HEIGHT);
    SolidBrush closeColor(data.hClose ? Color(255, 255, 255, 255) : Color(255, 150, 150, 150));
    g.DrawString(L"\xE8BB", -1, &fIconSml, RectF((float)(rightControlsX + btnW*2), 0.0f, (float)btnW, (float)TITLE_HEIGHT), &fmtC, &closeColor);

    int tabX = 10; int tabW = 240;
    GraphicsPath tabPath;
    tabPath.AddArc((float)tabX, (float)(TITLE_HEIGHT + 6), 10.0f, 10.0f, 180.0f, 90.0f);
    tabPath.AddArc((float)(tabX + tabW - 10), (float)(TITLE_HEIGHT + 6), 10.0f, 10.0f, 270.0f, 90.0f);
    tabPath.AddLine((float)(tabX + tabW), (float)(TITLE_HEIGHT + TAB_HEIGHT), (float)tabX, (float)(TITLE_HEIGHT + TAB_HEIGHT));
    tabPath.CloseFigure();
    
    g.FillPath(&bgToolbar, &tabPath); 
    g.DrawString(L"\xE774", -1, &fIconSml, RectF((float)(tabX + 10), (float)TITLE_HEIGHT, 20.0f, (float)TAB_HEIGHT), &fmtC, &textWhite);
    g.DrawString(data.title.c_str(), -1, &fTabTitle, RectF((float)(tabX + 35), (float)TITLE_HEIGHT, (float)(tabW - 50), (float)TAB_HEIGHT), &fmtL, &textWhite);

    int addTabX = tabX + tabW + 5;
    if(data.hAddTab) {
        SolidBrush addHover(Color(80, 255, 255, 255));
        g.FillEllipse(&addHover, (float)addTabX, (float)(TITLE_HEIGHT + 6), 22.0f, 22.0f);
    }
    g.DrawString(L"\xE710", -1, &fIconSml, RectF((float)addTabX, (float)(TITLE_HEIGHT + 6), 22.0f, 22.0f), &fmtC, &textGray);

    int toolY = TITLE_HEIGHT + TAB_HEIGHT;
    int navBtnW = 36; int curX = 10;

    if(data.hBack) { SolidBrush hb(Color(50,255,255,255)); g.FillEllipse(&hb, curX, toolY+4, 30, 30); }
    g.DrawString(L"\xE72B", -1, &fIcon, RectF(curX, toolY, 30, TOOLBAR_HEIGHT), &fmtC, &textWhite); curX += navBtnW;

    if(data.hFwd) { SolidBrush hb(Color(50,255,255,255)); g.FillEllipse(&hb, curX, toolY+4, 30, 30); }
    g.DrawString(L"\xE72A", -1, &fIcon, RectF(curX, toolY, 30, TOOLBAR_HEIGHT), &fmtC, &textWhite); curX += navBtnW;

    if(data.hRel) { SolidBrush hb(Color(50,255,255,255)); g.FillEllipse(&hb, curX, toolY+4, 30, 30); }
    g.DrawString(L"\xE72C", -1, &fIcon, RectF(curX, toolY, 30, TOOLBAR_HEIGHT), &fmtC, &textWhite); curX += navBtnW;

    int rightIconsX = w - (navBtnW * 3) - 10;
    if(data.hExt) { SolidBrush hb(Color(50,255,255,255)); g.FillEllipse(&hb, rightIconsX, toolY+4, 30, 30); }
    g.DrawString(L"\xE9D2", -1, &fIcon, RectF(rightIconsX, toolY, 30, TOOLBAR_HEIGHT), &fmtC, &textWhite); 
    
    if(data.hDl) { SolidBrush hb(Color(50,255,255,255)); g.FillEllipse(&hb, rightIconsX + navBtnW, toolY+4, 30, 30); }
    g.DrawString(L"\xE896", -1, &fIcon, RectF(rightIconsX + navBtnW, toolY, 30, TOOLBAR_HEIGHT), &fmtC, &textWhite); 
    
    if(data.hSet) { SolidBrush hb(Color(50,255,255,255)); g.FillEllipse(&hb, rightIconsX + navBtnW*2, toolY+4, 30, 30); }
    g.DrawString(L"\xE713", -1, &fIcon, RectF(rightIconsX + navBtnW*2, toolY, 30, TOOLBAR_HEIGHT), &fmtC, &textWhite); 

    int addressX = curX + 10;
    int addressW = rightIconsX - addressX - 20;

    if (data.isBrowserMode && data.hAddressBar) {
        SolidBrush editBg(Color(255, 20, 20, 20)); 
        GraphicsPath editPath;
        float r = 14.0f; float d = r*2;
        editPath.AddArc((float)addressX, (float)(toolY + 5), d, d, 180.0f, 90.0f);
        editPath.AddArc((float)(addressX + addressW - d), (float)(toolY + 5), d, d, 270.0f, 90.0f);
        editPath.AddArc((float)(addressX + addressW - d), (float)(toolY + 33 - d), d, d, 0.0f, 90.0f);
        editPath.AddArc((float)addressX, (float)(toolY + 33 - d), d, d, 90.0f, 90.0f);
        editPath.CloseFigure();
        g.FillPath(&editBg, &editPath);
    }
}

// ==========================================
// WINDOW PROCEDURE FOR MINI BROWSER
// ==========================================
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCCALCSIZE: { if (wParam == TRUE) return 0; break; }
        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            int border = 8;
            RECT r; GetClientRect(hWnd, &r);

            if (pt.y < border && pt.x < border) return HTTOPLEFT;
            if (pt.y < border && pt.x >= r.right - border) return HTTOPRIGHT;
            if (pt.y >= r.bottom - border && pt.x < border) return HTBOTTOMLEFT;
            if (pt.y >= r.bottom - border && pt.x >= r.right - border) return HTBOTTOMRIGHT;
            if (pt.y < border) return HTTOP;
            if (pt.y >= r.bottom - border) return HTBOTTOM;
            if (pt.x < border) return HTLEFT;
            if (pt.x >= r.right - border) return HTRIGHT;

            if (pt.y < TITLE_HEIGHT) {
                int rightControlsX = (r.right - r.left) - (45 * 3);
                if (pt.x >= rightControlsX) return HTCLIENT; 
                return HTCAPTION; 
            }
            if (pt.y < NAV_HEIGHT) return HTCLIENT;
            return HTCLIENT;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
            if (g_mbData.count(hWnd)) DrawMiniBrowserNav(hWnd, hdc);
            EndPaint(hWnd, &ps);
            break;
        }
        case WM_CTLCOLOREDIT: {
            if (g_mbData.count(hWnd) && (HWND)lParam == g_mbData[hWnd].hAddressBar) {
                HDC hdcEdit = (HDC)wParam;
                SetTextColor(hdcEdit, RGB(240, 240, 240));
                SetBkColor(hdcEdit, RGB(20, 20, 20));
                static HBRUSH hBrush = CreateSolidBrush(RGB(20, 20, 20));
                return (LRESULT)hBrush;
            }
            break;
        }
        case WM_SIZE: {
            if (g_mbData.count(hWnd)) {
                auto& data = g_mbData[hWnd];
                RECT b; GetClientRect(hWnd, &b);
                int w = b.right - b.left;
                
                if (data.isBrowserMode && data.hAddressBar) {
                    int navBtnW = 36;
                    int curX = 10 + (navBtnW * 3);
                    int rightIconsX = w - (navBtnW * 3) - 10;
                    int addressX = curX + 10;
                    int addressW = rightIconsX - addressX - 20;
                    
                    if (data.isFullScreen) ShowWindow(data.hAddressBar, SW_HIDE);
                    else {
                        ShowWindow(data.hAddressBar, SW_SHOW);
                        SetWindowPos(data.hAddressBar, NULL, addressX + 15, TITLE_HEIGHT + TAB_HEIGHT + 8, addressW - 30, 20, SWP_NOZORDER);
                    }
                }
                
                if (data.controller) {
                    if (!data.isFullScreen) b.top += NAV_HEIGHT;
                    data.controller->put_Bounds(b);
                }
            }
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }
        case WM_MOUSEMOVE: {
            if (!g_mbData.count(hWnd) || g_mbData[hWnd].isFullScreen) break;
            auto& data = g_mbData[hWnd];
            int x = GET_X_LPARAM(lParam); int y = GET_Y_LPARAM(lParam);
            RECT r; GetClientRect(hWnd, &r); int w = r.right - r.left; 

            bool redraw = false;
            
            int rightControlsX = w - (45 * 3);
            bool oMin = data.hMin, oMax = data.hMax, oClose = data.hClose;
            data.hMin   = (y <= TITLE_HEIGHT && x >= rightControlsX && x < rightControlsX + 45);
            data.hMax   = (y <= TITLE_HEIGHT && x >= rightControlsX + 45 && x < rightControlsX + 90);
            data.hClose = (y <= TITLE_HEIGHT && x >= rightControlsX + 90 && x < w);
            if (oMin != data.hMin || oMax != data.hMax || oClose != data.hClose) redraw = true;

            int addTabX = 10 + 240 + 5;
            bool oAdd = data.hAddTab;
            data.hAddTab = (y >= TITLE_HEIGHT && y < TITLE_HEIGHT + TAB_HEIGHT && x >= addTabX && x <= addTabX + 22);
            if (oAdd != data.hAddTab) redraw = true;

            int toolY = TITLE_HEIGHT + TAB_HEIGHT;
            int navBtnW = 36; int curX = 10;
            bool ob = data.hBack, of = data.hFwd, orl = data.hRel;
            data.hBack = (y >= toolY && y < NAV_HEIGHT && x >= curX && x < curX + 30); curX += navBtnW;
            data.hFwd  = (y >= toolY && y < NAV_HEIGHT && x >= curX && x < curX + 30); curX += navBtnW;
            data.hRel  = (y >= toolY && y < NAV_HEIGHT && x >= curX && x < curX + 30);
            if (ob != data.hBack || of != data.hFwd || orl != data.hRel) redraw = true;

            int rightIconsX = w - (navBtnW * 3) - 10;
            bool oe = data.hExt, odl = data.hDl, oSet = data.hSet;
            data.hExt = (y >= toolY && y < NAV_HEIGHT && x >= rightIconsX && x < rightIconsX + 30);
            data.hDl  = (y >= toolY && y < NAV_HEIGHT && x >= rightIconsX + navBtnW && x < rightIconsX + navBtnW + 30);
            data.hSet = (y >= toolY && y < NAV_HEIGHT && x >= rightIconsX + navBtnW*2 && x < rightIconsX + navBtnW*2 + 30);
            if (oe != data.hExt || odl != data.hDl || oSet != data.hSet) redraw = true;

            if (redraw) {
                RECT navRect = { 0, 0, w, NAV_HEIGHT };
                InvalidateRect(hWnd, &navRect, FALSE);
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (!g_mbData.count(hWnd) || g_mbData[hWnd].isFullScreen) break;
            auto& data = g_mbData[hWnd];
            
            if (data.hBack && data.webview) data.webview->GoBack();
            if (data.hFwd && data.webview) data.webview->GoForward();
            if (data.hRel && data.webview) data.webview->Reload();
            
            if (data.hAddTab) {
                if (data.webview) data.webview->Navigate(L"https://www.google.com");
            }
            
            if (data.hExt) MessageBoxA(hWnd, "Extensions menu will open here.", "Extensions", MB_OK);
            if (data.hDl) MessageBoxA(hWnd, "Downloads panel will open here.", "Downloads", MB_OK);
            if (data.hSet) MessageBoxA(hWnd, "Settings menu will open here.", "Settings", MB_OK);

            if (data.hMin) ShowWindow(hWnd, SW_MINIMIZE);
            if (data.hMax) {
                if (IsZoomed(hWnd)) ShowWindow(hWnd, SW_RESTORE);
                else ShowWindow(hWnd, SW_MAXIMIZE);
            }
            if (data.hClose) DestroyWindow(hWnd);
            
            break;
        }
        case WM_GETMINMAXINFO: {
            LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
            lpMMI->ptMinTrackSize.x = 800; lpMMI->ptMinTrackSize.y = 600;
            return 0;
        }
        case WM_CLOSE: DestroyWindow(hWnd); break;
        case WM_DESTROY: {
            g_mbData.erase(hWnd);
            if (g_isPureViewerMode && g_mbData.empty()) PostQuitMessage(0);
            break;
        }
        default: return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ==========================================
// WEBVIEW2 SETUP HANDLERS
// ==========================================
class ViewerControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    std::wstring m_url;
    HWND m_hWnd;
public:
    ViewerControllerHandler(std::wstring url, HWND hWnd) : m_url(url), m_hWnd(hWnd) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { *ppv = this; return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        if (controller != nullptr) {
            g_mbData[m_hWnd].controller = controller;
            controller->get_CoreWebView2(&g_mbData[m_hWnd].webview);
            controller->put_IsVisible(TRUE);

            ComPtr<ICoreWebView2Controller2> controller2;
            if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller2)))) {
                COREWEBVIEW2_COLOR darkBg = { 255, 30, 30, 30 }; 
                controller2->put_DefaultBackgroundColor(darkBg);
            }

            ICoreWebView2Settings* settings;
            g_mbData[m_hWnd].webview->get_Settings(&settings);
            ComPtr<ICoreWebView2Settings2> settings2;
            if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&settings2)))) {
                settings2->put_UserAgent(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");
            }

            // 🟢 BLOCK CLICKED ADULT LINKS
            g_mbData[m_hWnd].webview->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                    LPWSTR uri;
                    args->get_Uri(&uri);
                    if (uri) {
                        std::wstring currentUrl = uri;
                        CoTaskMemFree(uri);
                        if (IsBlockedContent(currentUrl)) {
                            args->put_Cancel(TRUE); // লিংক বাতিল করে দাও
                            if (g_mbData.count(m_hWnd) && g_mbData[m_hWnd].hAddressBar) {
                                SetWindowTextW(g_mbData[m_hWnd].hAddressBar, L""); // অ্যাড্রেসবার ক্লিয়ার
                            }
                        }
                    }
                    return S_OK;
                }
            ).Get(), nullptr);

            g_mbData[m_hWnd].webview->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [this](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    args->put_Handled(TRUE); 
                    LPWSTR uri; args->get_Uri(&uri);
                    sender->Navigate(uri);   
                    CoTaskMemFree(uri);
                    return S_OK;
                }
            ).Get(), nullptr);

            ComPtr<ICoreWebView2Controller3> controller3;
            if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller3)))) {
                EventRegistrationToken token;
                controller3->add_AcceleratorKeyPressed(new AcceleratorHandler(m_hWnd), &token);
            }

            if (g_mbData[m_hWnd].isBrowserMode) {
                EventRegistrationToken token2;
                g_mbData[m_hWnd].webview->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
                    [this](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* args) -> HRESULT {
                        if (g_mbData.count(m_hWnd)) {
                            if(g_mbData[m_hWnd].hAddressBar) {
                                LPWSTR uri; sender->get_Source(&uri);
                                if (uri) { SetWindowTextW(g_mbData[m_hWnd].hAddressBar, uri); CoTaskMemFree(uri); }
                            }
                            LPWSTR docTitle; sender->get_DocumentTitle(&docTitle);
                            if (docTitle) {
                                g_mbData[m_hWnd].title = docTitle;
                                CoTaskMemFree(docTitle);
                                RECT navRect = { 0, 0, 800, NAV_HEIGHT };
                                InvalidateRect(m_hWnd, &navRect, FALSE);
                            }
                        }
                        return S_OK;
                    }).Get(), &token2);
            }

            RECT b; GetClientRect(m_hWnd, &b);
            b.top += NAV_HEIGHT;
            controller->put_Bounds(b);

            auto wv = g_mbData[m_hWnd].webview;
            if (m_url == L"RAS_BROWSER") wv->Navigate(L"https://www.google.com");
            else if (m_url == L"LOCAL_PDF_SPLIT") wv->NavigateToString(HTML_PDF_SPLIT.c_str());
            else if (m_url == L"LOCAL_PDF_MERGE") wv->NavigateToString(HTML_PDF_MERGE.c_str());
            else if (m_url == L"LOCAL_IMG_TO_PDF") wv->NavigateToString(HTML_IMG_TO_PDF.c_str());
            else if (m_url == L"LOCAL_JOB_PHOTO") wv->NavigateToString(HTML_JOB_PHOTO.c_str());
            else if (m_url == L"LOCAL_JOB_SIGN") wv->NavigateToString(HTML_JOB_SIGN.c_str());
            else if (m_url == L"LOCAL_AGE_CALC") wv->NavigateToString(HTML_AGE_CALC.c_str());
            else if (m_url == L"LOCAL_COMPRESS_PDF") wv->NavigateToString(HTML_COMPRESS_PDF.c_str());
            else if (m_url == L"LOCAL_PHOTO_VIEWER") wv->NavigateToString(HTML_PHOTO_VIEWER.c_str());
            else wv->Navigate(m_url.c_str());
        }
        return S_OK;
    }
};

// ==========================================
// 🚀 LAUNCH FUNCTION 
// ==========================================
void LaunchMiniBrowser(std::wstring url, std::wstring title) {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = ViewerWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"RasMiniBrowserClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc);
        classRegistered = true;
    }

    std::wstring fullTitle = L"New Tab";
    
    HWND hViewerWnd = CreateWindowExW(
        0, L"RasMiniBrowserClass", fullTitle.c_str(),
        WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_CLIPCHILDREN, 
        CW_USEDEFAULT, CW_USEDEFAULT, 1050, 750,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    
    DWORD style = GetWindowLong(hViewerWnd, GWL_STYLE);
    SetWindowLong(hViewerWnd, GWL_STYLE, style & ~WS_CAPTION);

    g_mbData[hViewerWnd].title = fullTitle;

    if (url == L"RAS_BROWSER" || url.find(L"http") == 0) {
        g_mbData[hViewerWnd].isBrowserMode = true;
        HWND hEdit = CreateWindowExW(0, L"EDIT", L"https://www.google.com",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, hViewerWnd, (HMENU)IDC_ADDRESS_BAR, GetModuleHandle(NULL), NULL);
        
        SetWindowSubclass(hEdit, AddressBarSubclassProc, 1, 0);
        SendMessage(hEdit, WM_SETFONT, (WPARAM)CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"), TRUE);
        g_mbData[hViewerWnd].hAddressBar = hEdit;
    }

    HICON hAppIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    if (hAppIcon) {
        SendMessage(hViewerWnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
        SendMessage(hViewerWnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);
    }

    ShowWindow(hViewerWnd, SW_SHOW);
    UpdateWindow(hViewerWnd);

    auto startWebView = [hViewerWnd, url]() {
        g_miniEnv->CreateCoreWebView2Controller(hViewerWnd, new ViewerControllerHandler(url, hViewerWnd));
    };

    if (g_miniEnv) {
        startWebView(); 
    } else {
        // 🟢 FIX: GPU Rasterization, Zero Copy for Ultra Fast Speed
        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        options->put_AdditionalBrowserArguments(L"--enable-features=msWebView2EnableExtensions --enable-gpu-rasterization --enable-zero-copy --disable-features=Translate");

        std::wstring userDataFolder = L"C:\\ProgramData\\RasFocus\\.BrowserData";
        CreateDirectoryW(L"C:\\ProgramData\\RasFocus", NULL);
        CreateDirectoryW(userDataFolder.c_str(), NULL);
        SetFileAttributesW(userDataFolder.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

        CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), options.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [startWebView](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                    if (SUCCEEDED(result)) {
                        g_miniEnv = env;
                        startWebView();
                    }
                    return S_OK;
                }).Get());
    }
}
