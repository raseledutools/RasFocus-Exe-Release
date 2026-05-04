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
#include <commctrl.h> // অ্যাড্রেস বারের জন্য

#pragma comment(lib, "comctl32.lib")

using namespace Microsoft::WRL;
using namespace Gdiplus;

#define IDI_APP_ICON 101
#define IDC_ADDRESS_BAR 1005

extern bool g_isPureViewerMode; // main.cpp থেকে আসবে

// --- 🚀 Super Fast Loading Environment ---
static ComPtr<ICoreWebView2Environment> g_miniEnv = nullptr;

// --- Data Structure for Each Mini Browser Window ---
struct MiniBrowserData {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::wstring title;
    bool isFullScreen = false;
    WINDOWPLACEMENT wpPrev = { sizeof(WINDOWPLACEMENT) };
    
    // Browser Mode Extras
    bool isBrowserMode = false;
    HWND hAddressBar = NULL;
    
    // Hover states for Navigation Icons
    bool hBack = false, hFwd = false, hRel = false, hFS = false;
};

static std::map<HWND, MiniBrowserData> g_mbData;
static const int NAV_HEIGHT = 45;

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
            SetWindowLong(hWnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            data.isFullScreen = true;
            if (data.hAddressBar) ShowWindow(data.hAddressBar, SW_HIDE);
        }
    } else {
        SetWindowLong(hWnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
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

// --- কীবোর্ড থেকে ESC এবং F11 ধরার জন্য Handler ---
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

// --- Address Bar 'Enter' Key Handler ---
LRESULT CALLBACK AddressBarSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        HWND hParent = GetParent(hWnd);
        if (g_mbData.count(hParent) && g_mbData[hParent].webview) {
            wchar_t urlBuf[2048];
            GetWindowTextW(hWnd, urlBuf, 2048);
            std::wstring urlStr = urlBuf;
            if (urlStr.find(L"http://") != 0 && urlStr.find(L"https://") != 0) {
                if (urlStr.find(L".") != std::wstring::npos) urlStr = L"https://" + urlStr;
                else urlStr = L"https://www.google.com/search?q=" + urlStr;
            }
            g_mbData[hParent].webview->Navigate(urlStr.c_str());
        }
        return 0; // Handled
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ==========================================
// CUSTOM NAVIGATION BAR DRAWING
// ==========================================
void AddRoundedRectPath(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void DrawMiniBrowserNav(HWND hWnd, HDC hdc) {
    auto& data = g_mbData[hWnd];
    if (data.isFullScreen) return;

    RECT r; GetClientRect(hWnd, &r);
    int w = r.right - r.left;

    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    SolidBrush bg(Color(255, 12, 26, 37)); 
    g.FillRectangle(&bg, 0, 0, w, NAV_HEIGHT);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcon(L"Segoe MDL2 Assets");
    Font fTitle(&ff, 16, FontStyleBold, UnitPixel);
    Font fIcon(&ffIcon, 18, FontStyleRegular, UnitPixel);
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);

    SolidBrush textWhite(Color(255, 255, 255, 255));
    SolidBrush textTeal(Color(255, 12, 168, 176));
    SolidBrush hoverBg(Color(50, 255, 255, 255));

    int btnW = 45;
    int startX = w - (btnW * 4) - 10;

    // Address Bar Background (If RasBrowser Mode)
    if (data.isBrowserMode) {
        int editX = 15;
        int editY = 8;
        int editW = startX - editX - 20;
        int editH = 28;
        GraphicsPath editPath;
        AddRoundedRectPath(editPath, (float)editX, (float)editY, (float)editW, (float)editH, 14.0f);
        SolidBrush editBg(Color(255, 255, 255, 255));
        g.FillPath(&editBg, &editPath);
    } else {
        g.DrawString(data.title.c_str(), -1, &fTitle, RectF(15.0f, 0.0f, (float)w - 200.0f, (float)NAV_HEIGHT), &fmtL, &textTeal);
    }

    if(data.hBack) g.FillRectangle(&hoverBg, startX, 0, btnW, NAV_HEIGHT);
    g.DrawString(L"\xE72B", -1, &fIcon, RectF((float)startX, 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);
    
    if(data.hFwd) g.FillRectangle(&hoverBg, startX + btnW, 0, btnW, NAV_HEIGHT);
    g.DrawString(L"\xE72A", -1, &fIcon, RectF((float)(startX + btnW), 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);

    if(data.hRel) g.FillRectangle(&hoverBg, startX + btnW*2, 0, btnW, NAV_HEIGHT);
    g.DrawString(L"\xE72C", -1, &fIcon, RectF((float)(startX + btnW*2), 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);

    if(data.hFS) g.FillRectangle(&hoverBg, startX + btnW*3, 0, btnW, NAV_HEIGHT);
    const wchar_t* fsIcon = data.isFullScreen ? L"\xE73F" : L"\xE740";
    g.DrawString(fsIcon, -1, &fIcon, RectF((float)(startX + btnW*3), 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);
}

// ==========================================
// WINDOW PROCEDURE FOR MINI BROWSER
// ==========================================
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
            if (g_mbData.count(hWnd)) DrawMiniBrowserNav(hWnd, hdc);
            EndPaint(hWnd, &ps);
            break;
        }
        case WM_CTLCOLOREDIT: {
            if (g_mbData.count(hWnd) && (HWND)lParam == g_mbData[hWnd].hAddressBar) {
                HDC hdcEdit = (HDC)wParam;
                SetTextColor(hdcEdit, RGB(30, 40, 50));
                SetBkColor(hdcEdit, RGB(255, 255, 255));
                static HBRUSH hBrush = CreateSolidBrush(RGB(255, 255, 255));
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
                    int btnW = 45; int startX = w - (btnW * 4) - 10;
                    int editX = 25; int editY = 12; int editW = startX - editX - 30; int editH = 20;
                    if (data.isFullScreen) ShowWindow(data.hAddressBar, SW_HIDE);
                    else {
                        ShowWindow(data.hAddressBar, SW_SHOW);
                        SetWindowPos(data.hAddressBar, NULL, editX, editY, editW, editH, SWP_NOZORDER);
                    }
                }
                
                if (data.controller) {
                    if (!data.isFullScreen) b.top += NAV_HEIGHT;
                    data.controller->put_Bounds(b);
                }
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (!g_mbData.count(hWnd) || g_mbData[hWnd].isFullScreen) break;
            auto& data = g_mbData[hWnd];
            int x = GET_X_LPARAM(lParam); int y = GET_Y_LPARAM(lParam);
            RECT r; GetClientRect(hWnd, &r);
            int w = r.right - r.left; int btnW = 45; int startX = w - (btnW * 4) - 10;

            bool oldB = data.hBack, oldF = data.hFwd, oldR = data.hRel, oldFS = data.hFS;
            data.hBack = (y <= NAV_HEIGHT && x >= startX && x < startX + btnW);
            data.hFwd  = (y <= NAV_HEIGHT && x >= startX + btnW && x < startX + btnW*2);
            data.hRel  = (y <= NAV_HEIGHT && x >= startX + btnW*2 && x < startX + btnW*3);
            data.hFS   = (y <= NAV_HEIGHT && x >= startX + btnW*3 && x < startX + btnW*4);

            if (oldB != data.hBack || oldF != data.hFwd || oldR != data.hRel || oldFS != data.hFS) {
                RECT navRect = { startX, 0, w, NAV_HEIGHT };
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
            if (data.hFS) ToggleFullScreen(hWnd);
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

            // Esc & F11 Key Handler
            ComPtr<ICoreWebView2Controller3> controller3;
            if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller3)))) {
                EventRegistrationToken token;
                controller3->add_AcceleratorKeyPressed(new AcceleratorHandler(m_hWnd), &token);
            }

            // Update Address Bar on URL change
            if (g_mbData[m_hWnd].isBrowserMode) {
                EventRegistrationToken token2;
                g_mbData[m_hWnd].webview->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
                    [this](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* args) -> HRESULT {
                        if (g_mbData.count(m_hWnd) && g_mbData[m_hWnd].hAddressBar) {
                            LPWSTR uri; sender->get_Source(&uri);
                            if (uri) { SetWindowTextW(g_mbData[m_hWnd].hAddressBar, uri); CoTaskMemFree(uri); }
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

    std::wstring fullTitle = L"RasFocus - " + title;
    
    HWND hViewerWnd = CreateWindowExW(
        0, L"RasMiniBrowserClass", fullTitle.c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1050, 750,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    g_mbData[hViewerWnd].title = fullTitle;

    // 🟢 Address Bar Setup if URL is "RAS_BROWSER"
    if (url == L"RAS_BROWSER") {
        g_mbData[hViewerWnd].isBrowserMode = true;
        HWND hEdit = CreateWindowExW(0, L"EDIT", L"https://www.google.com",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, hViewerWnd, (HMENU)IDC_ADDRESS_BAR, GetModuleHandle(NULL), NULL);
        
        SetWindowSubclass(hEdit, AddressBarSubclassProc, 1, 0);
        SendMessage(hEdit, WM_SETFONT, (WPARAM)CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"), TRUE);
        g_mbData[hViewerWnd].hAddressBar = hEdit;
    }

    HICON hAppIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    if (hAppIcon) {
        SendMessage(hViewerWnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
        SendMessage(hViewerWnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);
    }

    ShowWindow(hViewerWnd, SW_SHOW);
    UpdateWindow(hViewerWnd);

    // 🚀 Super Fast Loader Trigger
    auto startWebView = [hViewerWnd, url]() {
        g_miniEnv->CreateCoreWebView2Controller(hViewerWnd, new ViewerControllerHandler(url, hViewerWnd));
    };

    if (g_miniEnv) {
        startWebView(); // Already loaded! Instant open.
    } else {
        std::wstring userDataFolder = L"C:\\ProgramData\\RasFocus\\ViewerData";
        CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [startWebView](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                    g_miniEnv = env;
                    startWebView();
                    return S_OK;
                }).Get());
    }
}
