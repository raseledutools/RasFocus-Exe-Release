// mini_browser.cpp

#define _CRT_SECURE_NO_WARNINGS
#include "mini_browser.h"
#include "html_tools.h"
#include "WebView2.h"
#include <wrl.h>
#include <map>
#include <gdiplus.h>
#include <string>
#include <windowsx.h>

using namespace Microsoft::WRL;
using namespace Gdiplus;

#define IDI_APP_ICON 101

extern bool g_isPureViewerMode; // main.cpp থেকে আসবে

// --- Data Structure for Each Mini Browser Window ---
struct MiniBrowserData {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::wstring title;
    bool isFullScreen = false;
    WINDOWPLACEMENT wpPrev = { sizeof(WINDOWPLACEMENT) };
    
    // Hover states for Navigation Icons
    bool hBack = false, hFwd = false, hRel = false, hFS = false;
};

static std::map<HWND, MiniBrowserData> g_mbData;
static const int NAV_HEIGHT = 45;

// ==========================================
// 🚀 FULL SCREEN LOGIC (F11 & ESC)
// ==========================================
void ToggleFullScreen(HWND hWnd) {
    if (g_mbData.find(hWnd) == g_mbData.end()) return;
    auto& data = g_mbData[hWnd];
    DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);

    if (!data.isFullScreen) {
        // ফুল স্ক্রিনে যাওয়া
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(hWnd, &data.wpPrev) && GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hWnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            data.isFullScreen = true;
        }
    } else {
        // ফুল স্ক্রিন থেকে সাধারণ উইন্ডোতে ফেরা
        SetWindowLong(hWnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hWnd, &data.wpPrev);
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        data.isFullScreen = false;
    }
    
    // WebView2 এর সাইজ অ্যাডজাস্ট করা
    if (data.controller) {
        RECT b; GetClientRect(hWnd, &b);
        if (!data.isFullScreen) b.top += NAV_HEIGHT; // ন্যাভিগেশন বারের জন্য জায়গা রাখা
        data.controller->put_Bounds(b);
    }
    InvalidateRect(hWnd, NULL, TRUE);
}

// --- কীবোর্ড থেকে ESC এবং F11 ধরার জন্য Handler ---
class AcceleratorHandler : public ICoreWebView2AcceleratorKeyPressedEventHandler {
    HWND m_hWnd;
    ULONG m_refCount = 1;
public:
    AcceleratorHandler(HWND hWnd) : m_hWnd(hWnd) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(ICoreWebView2AcceleratorKeyPressedEventHandler)) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r==0) delete this; return r; }
    
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller* sender, ICoreWebView2AcceleratorKeyPressedEventArgs* args) override {
        COREWEBVIEW2_KEY_EVENT_KIND kind;
        args->get_KeyEventKind(&kind);
        if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
            UINT virtualKey;
            args->get_VirtualKey(&virtualKey);
            
            // ESC চাপলে ফুলস্ক্রিন থেকে ব্যাক করবে
            if (virtualKey == VK_ESCAPE) {
                if (g_mbData[m_hWnd].isFullScreen) {
                    ToggleFullScreen(m_hWnd);
                    args->put_Handled(TRUE);
                }
            } 
            // F11 চাপলে ফুলস্ক্রিন টগল হবে
            else if (virtualKey == VK_F11) {
                ToggleFullScreen(m_hWnd);
                args->put_Handled(TRUE);
            }
        }
        return S_OK;
    }
};

// ==========================================
// 🎨 CUSTOM NAVIGATION BAR DRAWING
// ==========================================
void DrawMiniBrowserNav(HWND hWnd, HDC hdc) {
    auto& data = g_mbData[hWnd];
    if (data.isFullScreen) return; // ফুল স্ক্রিনে ন্যাভ বার হাইড থাকবে

    RECT r; GetClientRect(hWnd, &r);
    int w = r.right - r.left;

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // ডার্ক প্রফেশনাল ব্যাকগ্রাউন্ড (আপনার থিমের সাথে মিলিয়ে)
    SolidBrush bg(Color(255, 12, 26, 37)); 
    g.FillRectangle(&bg, 0, 0, w, NAV_HEIGHT);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcon(L"Segoe MDL2 Assets");
    Font fTitle(&ff, 16, FontStyleBold, UnitPixel);
    Font fIcon(&ffIcon, 18, FontStyleRegular, UnitPixel);
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);

    SolidBrush textWhite(Color(255, 255, 255, 255));
    SolidBrush textTeal(Color(255, 12, 168, 176)); // RasFocus Teal Color
    SolidBrush hoverBg(Color(50, 255, 255, 255));  // হালকা হোভার ইফেক্ট

    // 1. ডাইনামিক উইন্ডো টাইটেল ড্র করা
    g.DrawString(data.title.c_str(), -1, &fTitle, RectF(15.0f, 0.0f, (float)w - 200.0f, (float)NAV_HEIGHT), &fmtL, &textTeal);

    // 2. ন্যাভিগেশন আইকনগুলো (Back, Forward, Reload, FullScreen)
    int btnW = 45;
    int startX = w - (btnW * 4) - 10;

    // Back Button
    if(data.hBack) g.FillRectangle(&hoverBg, startX, 0, btnW, NAV_HEIGHT);
    g.DrawString(L"\xE72B", -1, &fIcon, RectF((float)startX, 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);
    
    // Forward Button
    if(data.hFwd) g.FillRectangle(&hoverBg, startX + btnW, 0, btnW, NAV_HEIGHT);
    g.DrawString(L"\xE72A", -1, &fIcon, RectF((float)(startX + btnW), 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);

    // Reload Button
    if(data.hRel) g.FillRectangle(&hoverBg, startX + btnW*2, 0, btnW, NAV_HEIGHT);
    g.DrawString(L"\xE72C", -1, &fIcon, RectF((float)(startX + btnW*2), 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);

    // Full Screen Button
    if(data.hFS) g.FillRectangle(&hoverBg, startX + btnW*3, 0, btnW, NAV_HEIGHT);
    const wchar_t* fsIcon = data.isFullScreen ? L"\xE73F" : L"\xE740"; // Toggle icon based on state
    g.DrawString(fsIcon, -1, &fIcon, RectF((float)(startX + btnW*3), 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);
}

// ==========================================
// 🖱️ WINDOW PROCEDURE FOR MINI BROWSER
// ==========================================
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            if (g_mbData.count(hWnd)) DrawMiniBrowserNav(hWnd, hdc);
            EndPaint(hWnd, &ps);
            break;
        }
        case WM_SIZE: {
            if (g_mbData.count(hWnd) && g_mbData[hWnd].controller) {
                RECT b; GetClientRect(hWnd, &b);
                if (!g_mbData[hWnd].isFullScreen) b.top += NAV_HEIGHT;
                g_mbData[hWnd].controller->put_Bounds(b);
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (!g_mbData.count(hWnd) || g_mbData[hWnd].isFullScreen) break;
            auto& data = g_mbData[hWnd];
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            RECT r; GetClientRect(hWnd, &r);
            int w = r.right - r.left;
            int btnW = 45;
            int startX = w - (btnW * 4) - 10;

            bool oldB = data.hBack, oldF = data.hFwd, oldR = data.hRel, oldFS = data.hFS;
            
            data.hBack = (y <= NAV_HEIGHT && x >= startX && x < startX + btnW);
            data.hFwd  = (y <= NAV_HEIGHT && x >= startX + btnW && x < startX + btnW*2);
            data.hRel  = (y <= NAV_HEIGHT && x >= startX + btnW*2 && x < startX + btnW*3);
            data.hFS   = (y <= NAV_HEIGHT && x >= startX + btnW*3 && x < startX + btnW*4);

            if (oldB != data.hBack || oldF != data.hFwd || oldR != data.hRel || oldFS != data.hFS) {
                RECT navRect = { startX, 0, w, NAV_HEIGHT };
                InvalidateRect(hWnd, &navRect, FALSE); // শুধু ন্যাভ বার রিড্র হবে
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (!g_mbData.count(hWnd) || g_mbData[hWnd].isFullScreen) break;
            auto& data = g_mbData[hWnd];
            if (data.hBack && data.webview) data.webview->GoBack();
            if (data.hFwd && data.webview) data.webview->GoForward();
            if (data.hRel && data.webview) data.webview->Reload();
            if (data.hFS) ToggleFullScreen(hWnd);
            break;
        }
        case WM_GETMINMAXINFO: {
            LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
            lpMMI->ptMinTrackSize.x = 800;
            lpMMI->ptMinTrackSize.y = 600;
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
// 🌐 WEBVIEW2 SETUP HANDLERS
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

            // Esc এবং F11 কীবোর্ড ইভেন্ট লিসেনার অ্যাড করা
            ComPtr<ICoreWebView2Controller3> controller3;
            if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller3)))) {
                EventRegistrationToken token;
                controller3->add_AcceleratorKeyPressed(new AcceleratorHandler(m_hWnd), &token);
            }

            RECT b; GetClientRect(m_hWnd, &b);
            b.top += NAV_HEIGHT; // ন্যাভিগেশন বারের জন্য জায়গা ছাড়া
            controller->put_Bounds(b);

            auto wv = g_mbData[m_hWnd].webview;
            
            // Local HTML Mapping
            if (m_url == L"LOCAL_PDF_SPLIT") wv->NavigateToString(HTML_PDF_SPLIT.c_str());
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

class ViewerEnvHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    std::wstring m_url;
    HWND m_hWnd;
public:
    ViewerEnvHandler(std::wstring url, HWND hWnd) : m_url(url), m_hWnd(hWnd) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { *ppv = this; return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override {
        if (env != nullptr) env->CreateCoreWebView2Controller(m_hWnd, new ViewerControllerHandler(m_url, m_hWnd));
        return S_OK;
    }
};

// ==========================================
// 🚀 LAUNCH FUNCTION (CALLED FROM MAIN/DASHBOARD)
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

    // উইন্ডোজের রিয়েল টাইটেল বারেও টাইটেল সেট করা হচ্ছে
    std::wstring fullTitle = L"RasFocus - " + title;
    
    HWND hViewerWnd = CreateWindowExW(
        0, L"RasMiniBrowserClass", fullTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1050, 750,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    // ডেটা স্ট্রাকচারে টাইটেল সেভ করা (আমাদের কাস্টম ড্রয়িংয়ের জন্য)
    g_mbData[hViewerWnd].title = fullTitle;

    HICON hAppIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    if (hAppIcon) {
        SendMessage(hViewerWnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
        SendMessage(hViewerWnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);
    }

    ShowWindow(hViewerWnd, SW_SHOW);
    UpdateWindow(hViewerWnd);

    std::wstring userDataFolder = L"C:\\ProgramData\\RasFocus\\ViewerData";
    CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), nullptr, new ViewerEnvHandler(url, hViewerWnd));
}
