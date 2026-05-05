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
    
    // 🟢 Custom Titlebar & Control States
    bool hBack = false, hFwd = false, hRel = false, hFS = false;
    bool hPin = false, hAdd = false, hMin = false, hMax = false, hClose = false;
    bool isPinned = false; // Always on Top State
};

static std::map<HWND, MiniBrowserData> g_mbData;
static const int NAV_HEIGHT = 45; // Custom Titlebar Height

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
            if (urlStr.find(L"http://") != 0 && urlStr.find(L"https://") != 0) {
                if (urlStr.find(L".") != std::wstring::npos) urlStr = L"https://" + urlStr;
                else urlStr = L"https://www.google.com/search?q=" + urlStr;
            }
            g_mbData[hParent].webview->Navigate(urlStr.c_str());
        }
        return 0;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ==========================================
// 🎨 CUSTOM NAVIGATION & TITLE BAR DRAWING
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

    // 🟢 Custom Borderless Titlebar Background (Teal)
    SolidBrush bg(Color(255, 12, 168, 176)); 
    g.FillRectangle(&bg, 0, 0, w, NAV_HEIGHT);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcon(L"Segoe MDL2 Assets");
    Font fTitle(&ff, 16, FontStyleBold, UnitPixel);
    Font fIcon(&ffIcon, 16, FontStyleRegular, UnitPixel);
    Font fIconSml(&ffIcon, 14, FontStyleRegular, UnitPixel);
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);

    SolidBrush textWhite(Color(255, 255, 255, 255));
    SolidBrush hoverBg(Color(50, 255, 255, 255));
    SolidBrush closeHoverBg(Color(255, 232, 17, 35)); // Red for Close
    SolidBrush pinActiveColor(Color(255, 255, 215, 0)); // Yellow for Pinned

    int btnW = 45;
    int rightControlsX = w - (btnW * 3);

    // --- 1. Right Side Windows Controls (Min, Max, Close) ---
    if(data.hMin) g.FillRectangle(&hoverBg, rightControlsX, 0, btnW, NAV_HEIGHT);
    g.DrawString(L"\xE921", -1, &fIcon, RectF((float)rightControlsX, 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);
    
    if(data.hMax) g.FillRectangle(&hoverBg, rightControlsX + btnW, 0, btnW, NAV_HEIGHT);
    const wchar_t* maxIcon = IsZoomed(hWnd) ? L"\xE923" : L"\xE922";
    g.DrawString(maxIcon, -1, &fIcon, RectF((float)(rightControlsX + btnW), 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);
    
    if(data.hClose) g.FillRectangle(&closeHoverBg, rightControlsX + btnW*2, 0, btnW, NAV_HEIGHT);
    g.DrawString(L"\xE8BB", -1, &fIcon, RectF((float)(rightControlsX + btnW*2), 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);

    // --- 2. Left Side Special Controls (Pin & Add) ---
    int leftControlsX = rightControlsX - (btnW * 2) - 10;
    
    // Add (+) Button for new Tab/Window
    if(data.hAdd) g.FillRectangle(&hoverBg, leftControlsX, 0, btnW, NAV_HEIGHT);
    g.DrawString(L"\xE710", -1, &fIconSml, RectF((float)leftControlsX, 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, &textWhite);
    
    // Pin Button (Always on top)
    if(data.hPin) g.FillRectangle(&hoverBg, leftControlsX + btnW, 0, btnW, NAV_HEIGHT);
    const wchar_t* pinIcon = data.isPinned ? L"\xE840" : L"\xE718";
    SolidBrush* currentPinColor = data.isPinned ? &pinActiveColor : &textWhite;
    g.DrawString(pinIcon, -1, &fIconSml, RectF((float)(leftControlsX + btnW), 0.0f, (float)btnW, (float)NAV_HEIGHT), &fmtC, currentPinColor);

    // --- 3. Browser Navigation Controls & Address Bar ---
    int navStartX = 10;
    int navBtnW = 35;

    if(data.hBack) g.FillRectangle(&hoverBg, navStartX, 0, navBtnW, NAV_HEIGHT);
    g.DrawString(L"\xE72B", -1, &fIcon, RectF((float)navStartX, 0.0f, (float)navBtnW, (float)NAV_HEIGHT), &fmtC, &textWhite);
    
    if(data.hFwd) g.FillRectangle(&hoverBg, navStartX + navBtnW, 0, navBtnW, NAV_HEIGHT);
    g.DrawString(L"\xE72A", -1, &fIcon, RectF((float)(navStartX + navBtnW), 0.0f, (float)navBtnW, (float)NAV_HEIGHT), &fmtC, &textWhite);

    if(data.hRel) g.FillRectangle(&hoverBg, navStartX + navBtnW*2, 0, navBtnW, NAV_HEIGHT);
    g.DrawString(L"\xE72C", -1, &fIcon, RectF((float)(navStartX + navBtnW*2), 0.0f, (float)navBtnW, (float)NAV_HEIGHT), &fmtC, &textWhite);

    // Title or Address Bar
    int titleX = navStartX + navBtnW*3 + 10;
    int titleW = leftControlsX - titleX - 10;

    if (data.isBrowserMode) {
        int editY = 8;
        int editH = 28;
        GraphicsPath editPath;
        AddRoundedRectPath(editPath, (float)titleX, (float)editY, (float)titleW, (float)editH, 14.0f);
        SolidBrush editBg(Color(255, 255, 255, 255));
        g.FillPath(&editBg, &editPath);
    } else {
        g.DrawString(data.title.c_str(), -1, &fTitle, RectF((float)titleX, 0.0f, (float)titleW, (float)NAV_HEIGHT), &fmtL, &textWhite);
    }
}

// ==========================================
// WINDOW PROCEDURE FOR MINI BROWSER
// ==========================================
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        // 🟢 FIX: Borderless Dragging & Resizing Logic
        case WM_NCCALCSIZE: {
            if (wParam == TRUE) return 0;
            break;
        }
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

            // Custom Titlebar Drag Logic
            if (pt.y < NAV_HEIGHT) {
                int w = r.right - r.left;
                int rightControlsX = w - (45 * 3);
                int leftControlsX = rightControlsX - (45 * 2) - 10;
                
                // Allow clicking on buttons
                if (pt.x >= leftControlsX) return HTCLIENT; 
                if (pt.x <= 10 + (35 * 3)) return HTCLIENT;
                if (g_mbData[hWnd].isBrowserMode && pt.y >= 8 && pt.y <= 36) return HTCLIENT; // Address bar
                
                // Everywhere else on Titlebar = Drag
                return HTCAPTION; 
            }
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
                    int btnW = 45; int navBtnW = 35;
                    int rightControlsX = w - (btnW * 3);
                    int leftControlsX = rightControlsX - (btnW * 2) - 10;
                    int titleX = 10 + navBtnW*3 + 10;
                    int titleW = leftControlsX - titleX - 10;
                    
                    if (data.isFullScreen) ShowWindow(data.hAddressBar, SW_HIDE);
                    else {
                        ShowWindow(data.hAddressBar, SW_SHOW);
                        SetWindowPos(data.hAddressBar, NULL, titleX + 10, 12, titleW - 20, 20, SWP_NOZORDER);
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
            RECT r; GetClientRect(hWnd, &r);
            int w = r.right - r.left; 
            
            int btnW = 45; int navBtnW = 35;
            int rightControlsX = w - (btnW * 3);
            int leftControlsX = rightControlsX - (btnW * 2) - 10;

            bool redraw = false;

            // Nav Controls
            bool ob = data.hBack, of = data.hFwd, orl = data.hRel;
            data.hBack = (y <= NAV_HEIGHT && x >= 10 && x < 10 + navBtnW);
            data.hFwd  = (y <= NAV_HEIGHT && x >= 10 + navBtnW && x < 10 + navBtnW*2);
            data.hRel  = (y <= NAV_HEIGHT && x >= 10 + navBtnW*2 && x < 10 + navBtnW*3);
            if (ob != data.hBack || of != data.hFwd || orl != data.hRel) redraw = true;

            // Special Controls (Add, Pin)
            bool oA = data.hAdd, oP = data.hPin;
            data.hAdd = (y <= NAV_HEIGHT && x >= leftControlsX && x < leftControlsX + btnW);
            data.hPin = (y <= NAV_HEIGHT && x >= leftControlsX + btnW && x < leftControlsX + btnW*2);
            if (oA != data.hAdd || oP != data.hPin) redraw = true;

            // Windows Controls (Min, Max, Close)
            bool oMin = data.hMin, oMax = data.hMax, oClose = data.hClose;
            data.hMin   = (y <= NAV_HEIGHT && x >= rightControlsX && x < rightControlsX + btnW);
            data.hMax   = (y <= NAV_HEIGHT && x >= rightControlsX + btnW && x < rightControlsX + btnW*2);
            data.hClose = (y <= NAV_HEIGHT && x >= rightControlsX + btnW*2 && x < w);
            if (oMin != data.hMin || oMax != data.hMax || oClose != data.hClose) redraw = true;

            if (redraw) {
                RECT navRect = { 0, 0, w, NAV_HEIGHT };
                InvalidateRect(hWnd, &navRect, FALSE);
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (!g_mbData.count(hWnd) || g_mbData[hWnd].isFullScreen) break;
            auto& data = g_mbData[hWnd];
            
            // Nav Clicks
            if (data.hBack && data.webview) data.webview->GoBack();
            if (data.hFwd && data.webview) data.webview->GoForward();
            if (data.hRel && data.webview) data.webview->Reload();
            
            // 🟢 Action: Add New Window
            if (data.hAdd) {
                extern void LaunchMiniBrowser(std::wstring url, std::wstring title);
                if (data.isBrowserMode) LaunchMiniBrowser(L"RAS_BROWSER", L"RasBrowser");
                else LaunchMiniBrowser(L"https://www.google.com", L"New Tab");
            }
            
            // 🟢 Action: Pin Window (Always on Top)
            if (data.hPin) {
                data.isPinned = !data.isPinned;
                if (data.isPinned) SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                else SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                InvalidateRect(hWnd, NULL, FALSE);
            }

            // Window Controls Clicks
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

    std::wstring fullTitle = L"RasFocus - " + title;
    
    // 🟢 FIX: Borderless Window for Custom Titlebar
    HWND hViewerWnd = CreateWindowExW(
        0, L"RasMiniBrowserClass", fullTitle.c_str(),
        WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_CLIPCHILDREN, 
        CW_USEDEFAULT, CW_USEDEFAULT, 1050, 750,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    
    // Remove Default Windows Titlebar (keep shadow and resizing)
    DWORD style = GetWindowLong(hViewerWnd, GWL_STYLE);
    SetWindowLong(hViewerWnd, GWL_STYLE, style & ~WS_CAPTION);

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
