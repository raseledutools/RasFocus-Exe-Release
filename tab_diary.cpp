// tab_diary.cpp 

#include "tab_gemini.h" 
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cstdint>
#include <commdlg.h> 
#include <urlmon.h>
#include <process.h>
#include <shlwapi.h>
#include <algorithm> // For string transformation

// --- WebView2 Headers ---
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"  // <--- শুধু এই নতুন লাইনটি যোগ করুন
#include <wrl.h>
#include <objbase.h>

using namespace Gdiplus;
using namespace std;
using namespace Microsoft::WRL; 

// --- States & Cache ---
static float s_contentX = 0, s_contentY = 0, s_contentW = 800, s_contentH = 600;
extern HWND hParentWnd; 
extern float g_scaleFactor; // DPI Scaling Fix
static bool g_controlsVisible = false;

static bool hoverLaunchBtn = false;
static bool hoverCloseBtn = false;
static bool hoverBackBtn = false;
static bool hoverForwardBtn = false;
static bool hoverRefreshBtn = false;
static bool hoverHomeBtn = false; 
static bool hoverAddBtn = false; // New Tab Button
static bool hoverPopOutBtn = false;
static bool hoverReturnBtn = false;

static bool isGeminiRunning = false;
static bool isDownloading = false; 
static bool isPoppedOut = false;   
static HWND hPopOutWnd = NULL;     

// --- WebView2 Global Pointers ---
static ComPtr<ICoreWebView2Controller> webViewController;
static ComPtr<ICoreWebView2> webView;

// --- Colors (APP MATCHING LIGHT THEME) ---
static const Color GClrWhite(255, 255, 255, 255);    
static const Color GClrAppTeal(255, 12, 168, 176);   
static const Color GClrTealHover(255, 30, 185, 195); 
static const Color GClrTextDark(255, 40, 40, 40);    
static const Color GClrDanger(255, 230, 60, 60);     
static const Color GClrWarning(255, 255, 190, 0);    

static GraphicsPath* GetGeminiRoundRect(RectF rect, int radius) {
    GraphicsPath* path = new GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure(); return path;
}

// =========================================================================
// Full Screen Pop-Out Window Procedure
// =========================================================================
LRESULT CALLBACK PopOutWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            HFONT hFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            
            int sw = GetSystemMetrics(SM_CXSCREEN); // Get Monitor Width

            // Full Screen Windows Buttons
            HWND hBack = CreateWindow("BUTTON", "<", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 5, 5, 30, 25, hWnd, (HMENU)1001, GetModuleHandle(NULL), NULL);
            HWND hFwd = CreateWindow("BUTTON", ">", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 40, 5, 30, 25, hWnd, (HMENU)1002, GetModuleHandle(NULL), NULL);
            HWND hRef = CreateWindow("BUTTON", "R", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 75, 5, 30, 25, hWnd, (HMENU)1003, GetModuleHandle(NULL), NULL);
            HWND hHome = CreateWindow("BUTTON", "Home", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 110, 5, 60, 25, hWnd, (HMENU)1006, GetModuleHandle(NULL), NULL);
            
            // Exit Full Screen Button (Placed at the right edge)
            HWND hRet = CreateWindow("BUTTON", "Exit Full Screen", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, sw - 150, 5, 140, 25, hWnd, (HMENU)1004, GetModuleHandle(NULL), NULL);
            
            SendMessage(hBack, WM_SETFONT, (WPARAM)hFont, TRUE); SendMessage(hFwd, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(hRef, WM_SETFONT, (WPARAM)hFont, TRUE); SendMessage(hHome, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(hRet, WM_SETFONT, (WPARAM)hFont, TRUE);
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == 1001 && webView) webView->GoBack();
            if (LOWORD(wParam) == 1002 && webView) webView->GoForward();
            if (LOWORD(wParam) == 1003 && webView) webView->Reload();
            if (LOWORD(wParam) == 1006 && webView) {
                webView->Navigate(L"https://www.google.com/"); // Go to Google on Home
            }
            if (LOWORD(wParam) == 1004) SendMessage(hWnd, WM_CLOSE, 0, 0); // Exit Full Screen
            break;
        }
        case WM_SIZE:
            if (webViewController != nullptr && isPoppedOut) {
                RECT bounds;
                GetClientRect(hWnd, &bounds);
                bounds.top += 35; // Leave space for the top controls
                webViewController->put_Bounds(bounds);
            }
            break;
        case WM_CLOSE:
            isPoppedOut = false;
            if (webViewController != nullptr) {
                webViewController->put_ParentWindow(hParentWnd); 
                
                // Return to normal App view size
                RECT bounds;
                bounds.left = (LONG)(s_contentX * g_scaleFactor);
                bounds.top = (LONG)((s_contentY + 30) * g_scaleFactor); 
                bounds.right = (LONG)((s_contentX + s_contentW) * g_scaleFactor);
                bounds.bottom = (LONG)((s_contentY + s_contentH) * g_scaleFactor);
                webViewController->put_Bounds(bounds);
                
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            }
            DestroyWindow(hWnd);
            hPopOutWnd = NULL;
            return 0;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// =========================================================================
// IID & COM Handlers 
// =========================================================================
static const IID IID_IUnknown_Local = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const IID IID_ICoreWebView2DownloadStartingEventHandler_Local = { 0xefed3480, 0xb6b0, 0x454c, { 0xbc, 0xad, 0x1d, 0x83, 0x2b, 0x5e, 0x1e, 0x93 } };
static const IID IID_ICoreWebView2StateChangedEventHandler_Local = { 0x81336594, 0x7ede, 0x4ba9, { 0x87, 0x1d, 0x6e, 0xb2, 0x2a, 0x45, 0xd4, 0xa8 } };
static const IID IID_ICoreWebView2_4_Local = { 0x20d02d59, 0x6df2, 0x42dc, { 0xbd, 0x06, 0xf9, 0x8a, 0x69, 0x4b, 0x13, 0x02 } };

// Custom IIDs for Navigation and New Window Events
static const IID IID_ICoreWebView2NewWindowRequestedEventHandler_Local = { 0xd4ce85af, 0x1563, 0x4377, { 0xa5, 0x0f, 0x5c, 0x72, 0xaf, 0xb2, 0x43, 0xb7 } };
static const IID IID_ICoreWebView2NavigationStartingEventHandler_Local = { 0x9adbe429, 0xf36d, 0x432b, { 0x9d, 0xdc, 0xf8, 0x88, 0x1f, 0xbd, 0x76, 0xe3 } };

// --- Adult Blocker Event Handler ---
class NavigationStartingHandler : public ICoreWebView2NavigationStartingEventHandler {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown_Local || riid == IID_ICoreWebView2NavigationStartingEventHandler_Local) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) override {
        LPWSTR uri = nullptr;
        args->get_Uri(&uri);
        if (uri) {
            std::wstring url(uri);
            CoTaskMemFree(uri);
            
            // Convert to lowercase for checking
            std::transform(url.begin(), url.end(), url.begin(), ::towlower);
            
            // Adult keywords list
            std::vector<std::wstring> badWords = { L"porn", L"sex", L"xvideos", L"xnxx", L"redtube", L"brazzers", L"adult" };
            
            bool adultBlockIsActive = true; // TODO: আপনি চাইলে আপনার অ্যাপের গ্লোবাল adult block ভ্যারিয়েবল এখানে বসাতে পারেন।
            
            if (adultBlockIsActive) {
                for (const auto& word : badWords) {
                    if (url.find(word) != std::wstring::npos) {
                        args->put_Cancel(TRUE); // Block the navigation
                        MessageBoxA(hParentWnd, "Adult content is strictly blocked by RasFocus!", "Blocked", MB_ICONWARNING | MB_OK);
                        return S_OK;
                    }
                }
            }
        }
        return S_OK;
    }
};

// --- Chrome-like New Tab Override (Force Same Window) ---
class NewWindowRequestedHandler : public ICoreWebView2NewWindowRequestedEventHandler {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown_Local || riid == IID_ICoreWebView2NewWindowRequestedEventHandler_Local) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) override {
        args->put_Handled(TRUE); // Stop default pop-up behavior
        LPWSTR uri;
        if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
            sender->Navigate(uri); // Open link in the SAME window
            CoTaskMemFree(uri);
        }
        return S_OK;
    }
};

// --- Downloader Handlers ---
class DownloadStateChangedHandler : public ICoreWebView2StateChangedEventHandler {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown_Local || riid == IID_ICoreWebView2StateChangedEventHandler_Local) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2DownloadOperation* sender, IUnknown* args) override {
        COREWEBVIEW2_DOWNLOAD_STATE state;
        sender->get_State(&state);
        if (state != COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS) {
            isDownloading = false;
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        }
        return S_OK;
    }
};

class DownloadStartingHandler : public ICoreWebView2DownloadStartingEventHandler {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown_Local || riid == IID_ICoreWebView2DownloadStartingEventHandler_Local) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2DownloadStartingEventArgs* args) override {
        args->put_Handled(TRUE);
        LPWSTR defaultPath = nullptr;
        args->get_ResultFilePath(&defaultPath);
        wchar_t szFile[MAX_PATH] = {0};
        if (defaultPath) { wcscpy_s(szFile, defaultPath); CoTaskMemFree(defaultPath); } 
        else { wcscpy_s(szFile, L"downloaded_file"); }

        OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = isPoppedOut ? hPopOutWnd : hParentWnd;
        ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = L"All Files\0*.*\0"; ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

        if (GetSaveFileNameW(&ofn) == TRUE) {
            args->put_ResultFilePath(szFile);
            isDownloading = true;
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);

            ICoreWebView2DownloadOperation* downloadOp = nullptr;
            if (SUCCEEDED(args->get_DownloadOperation(&downloadOp)) && downloadOp) {
                EventRegistrationToken tok;
                downloadOp->add_StateChanged(new DownloadStateChangedHandler(), &tok);
                downloadOp->Release();
            }
        } else { args->put_Cancel(TRUE); }
        return S_OK;
    }
};

class ControllerCompletedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        static const IID IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler_Local = { 0x6c4819f3, 0xc9b7, 0x4260, { 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c } };
        if (riid == IID_IUnknown_Local || riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler_Local) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        if (controller != nullptr) {
            webViewController = controller;
            webViewController->get_CoreWebView2(&webView);
            webViewController->put_IsVisible(TRUE);

            ComPtr<ICoreWebView2_4> webView4;
            if (SUCCEEDED(webView->QueryInterface(IID_ICoreWebView2_4_Local, (void**)&webView4))) {
                EventRegistrationToken token;
                webView4->add_DownloadStarting(new DownloadStartingHandler(), &token);
            }
            
            // Adult Filter Event
            EventRegistrationToken navToken;
            webView->add_NavigationStarting(new NavigationStartingHandler(), &navToken);

            // Chrome-like Same Window Tab Event
            EventRegistrationToken windowToken;
            webView->add_NewWindowRequested(new NewWindowRequestedHandler(), &windowToken);

            // Gemini Permissions
            webView->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [](ICoreWebView2* sender, ICoreWebView2PermissionRequestedEventArgs* args) {
                    args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
                    return S_OK;
                }).Get(), nullptr);

            webView->Navigate(L"https://gemini.google.com/?authuser=0");

        } else {
            MessageBoxA(NULL, "Failed to load WebView2 engine.", "Error", MB_ICONERROR | MB_OK);
        }
        return S_OK;
    }
};

class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        static const IID IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler_Local = { 0x4e8a3389, 0xc9d8, 0x4bd2, { 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d } };
        if (riid == IID_IUnknown_Local || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler_Local) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override {
        if (env != nullptr) {
            env->CreateCoreWebView2Controller(hParentWnd, new ControllerCompletedHandler());
        }
        return S_OK;
    }
};

// =========================================================================

void InitGeminiControls(HWND parent) { hParentWnd = parent; }

void ShowGeminiControls(bool show) {
    g_controlsVisible = show;
    if (show && hParentWnd != NULL && !isPoppedOut) { InvalidateRect(hParentWnd, NULL, TRUE); }
    if (webViewController != nullptr && !isPoppedOut) { webViewController->put_IsVisible(show ? TRUE : FALSE); }
}

void ResizeGeminiControls(int cx, int cy, int cw, int ch) {
    s_contentX = (float)cx; s_contentY = (float)cy; s_contentW = (float)cw; s_contentH = (float)ch;
    if (webViewController != nullptr && isGeminiRunning && !isPoppedOut) {
        RECT bounds;
        bounds.left = (LONG)(cx * g_scaleFactor);
        bounds.top = (LONG)((cy + 30) * g_scaleFactor); 
        bounds.right = (LONG)((cx + cw) * g_scaleFactor);
        bounds.bottom = (LONG)((cy + ch) * g_scaleFactor);
        webViewController->put_Bounds(bounds);
    }
}

void DrawGeminiTab(Graphics& g, float cx, float cy, float cw, float ch) {
    s_contentX = cx; s_contentY = cy; s_contentW = cw; s_contentH = ch;

    if (webViewController != nullptr && isGeminiRunning && !isPoppedOut) {
        RECT bounds;
        bounds.left = (LONG)(cx * g_scaleFactor);
        bounds.top = (LONG)((cy + 30) * g_scaleFactor); 
        bounds.right = (LONG)((cx + cw) * g_scaleFactor);
        bounds.bottom = (LONG)((cy + ch) * g_scaleFactor);
        webViewController->put_Bounds(bounds);
    }

    FontFamily ff(L"Segoe UI"); 
    FontFamily ffIcon(L"Segoe MDL2 Assets"); 
    Font fH1(&ff, 28, FontStyleBold, UnitPixel); 
    Font fBold(&ff, 14, FontStyleBold, UnitPixel);
    Font fNormal(&ff, 14, FontStyleRegular, UnitPixel); 
    Font fIcons(&ffIcon, 14, FontStyleRegular, UnitPixel); 
    
    SolidBrush bBg(GClrWhite); 
    SolidBrush bText(GClrTextDark); 
    SolidBrush bWhite(GClrWhite);
    
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    g.FillRectangle(&bBg, cx, cy, cw, ch);

    if (!isGeminiRunning) {
        g.DrawString(L"Rasel Edu Tools Interface", -1, &fH1, RectF(cx, cy + (ch/2) - 100, cw, 40), &fC, &bText);
        g.DrawString(L"Access Gemini AI and Web Apps Instantly.", -1, &fNormal, RectF(cx, cy + (ch/2) - 60, cw, 30), &fC, &bText);

        float btnW = 260.0f; float btnH = 50.0f;
        float btnX = cx + (cw - btnW) / 2.0f; float btnY = cy + (ch / 2.0f);

        RectF btnRect(btnX, btnY, btnW, btnH);
        GraphicsPath* bp = GetGeminiRoundRect(btnRect, 25);
        SolidBrush btnBrush(hoverLaunchBtn ? GClrTealHover : GClrAppTeal);
        g.FillPath(&btnBrush, bp); delete bp;
        g.DrawString(L"Open Web Browser", -1, &fBold, btnRect, &fC, &bWhite);
    } 
    else if (isPoppedOut) {
        g.DrawString(L"Browser is running in Full Screen Mode.", -1, &fBold, RectF(cx, cy + (ch/2) - 50, cw, 40), &fC, &bText);
    }
    else {
        SolidBrush bNavBg(GClrAppTeal);
        g.FillRectangle(&bNavBg, cx, cy, cw, 30.0f); 

        float startX = cx + 5; 
        
        RectF backRect(startX, cy + 2, 30, 26); SolidBrush bBack(hoverBackBtn ? GClrTealHover : GClrAppTeal);
        g.FillRectangle(&bBack, backRect); g.DrawString(L"\xE72B", -1, &fIcons, backRect, &fC, &bWhite); 

        startX += 32; RectF fwdRect(startX, cy + 2, 30, 26); SolidBrush bFwd(hoverForwardBtn ? GClrTealHover : GClrAppTeal);
        g.FillRectangle(&bFwd, fwdRect); g.DrawString(L"\xE72A", -1, &fIcons, fwdRect, &fC, &bWhite); 

        startX += 32; RectF refRect(startX, cy + 2, 30, 26); SolidBrush bRef(hoverRefreshBtn ? GClrTealHover : GClrAppTeal);
        g.FillRectangle(&bRef, refRect); g.DrawString(L"\xE72C", -1, &fIcons, refRect, &fC, &bWhite); 

        startX += 32; RectF homeRect(startX, cy + 2, 30, 26); SolidBrush bHome(hoverHomeBtn ? GClrTealHover : GClrAppTeal);
        g.FillRectangle(&bHome, homeRect); g.DrawString(L"\xE80F", -1, &fIcons, homeRect, &fC, &bWhite); 

        startX += 35; RectF addRect(startX, cy + 2, 30, 26); SolidBrush bAdd(hoverAddBtn ? GClrTealHover : GClrAppTeal);
        g.FillRectangle(&bAdd, addRect); g.DrawString(L"\xE710", -1, &fIcons, addRect, &fC, &bWhite); 

        if (isDownloading) {
            startX += 40; SolidBrush bWarn(GClrWarning);
            g.DrawString(L"Downloading...", -1, &fNormal, RectF(startX, cy + 2, 120, 26), &fC, &bWarn);
        }

        RectF closeRect(cx + cw - 35, cy + 2, 30, 26); SolidBrush bClose(hoverCloseBtn ? GClrDanger : Color(255, 180, 40, 40));
        g.FillRectangle(&bClose, closeRect); g.DrawString(L"\xE8BB", -1, &fIcons, closeRect, &fC, &bWhite); 

        RectF popRect(cx + cw - 70, cy + 2, 30, 26); SolidBrush bPop(hoverPopOutBtn ? GClrTealHover : GClrAppTeal);
        g.FillRectangle(&bPop, popRect); g.DrawString(L"\xE740", -1, &fIcons, popRect, &fC, &bWhite); // Full Screen Icon
    }
}

void ProcessGeminiMouseMove(float x, float y) {
    if (!isGeminiRunning) {
        float btnW = 260.0f; float btnH = 50.0f;
        float btnX = s_contentX + (s_contentW - btnW) / 2.0f; float btnY = s_contentY + (s_contentH / 2.0f);
        bool wasHovering = hoverLaunchBtn; hoverLaunchBtn = RectF(btnX, btnY, btnW, btnH).Contains(x, y);
        if (wasHovering != hoverLaunchBtn && hParentWnd != NULL) { InvalidateRect(hParentWnd, NULL, TRUE); }
    } 
    else if (!isPoppedOut) {
        float startX = s_contentX + 5; float cy = s_contentY;
        bool prevBack = hoverBackBtn; bool prevFwd = hoverForwardBtn; bool prevRef = hoverRefreshBtn;
        bool prevHome = hoverHomeBtn; bool prevAdd = hoverAddBtn; bool prevPop = hoverPopOutBtn; bool prevClose = hoverCloseBtn;

        hoverBackBtn = RectF(startX, cy + 2, 30, 26).Contains(x, y);
        hoverForwardBtn = RectF(startX + 32, cy + 2, 30, 26).Contains(x, y);
        hoverRefreshBtn = RectF(startX + 64, cy + 2, 30, 26).Contains(x, y);
        hoverHomeBtn = RectF(startX + 96, cy + 2, 30, 26).Contains(x, y);
        hoverAddBtn = RectF(startX + 131, cy + 2, 30, 26).Contains(x, y); 
        
        hoverPopOutBtn = RectF(s_contentX + s_contentW - 70, cy + 2, 30, 26).Contains(x, y);
        hoverCloseBtn = RectF(s_contentX + s_contentW - 35, cy + 2, 30, 26).Contains(x, y);

        if (prevBack != hoverBackBtn || prevFwd != hoverForwardBtn || prevRef != hoverRefreshBtn || 
            prevHome != hoverHomeBtn || prevAdd != hoverAddBtn || prevPop != hoverPopOutBtn || prevClose != hoverCloseBtn) {
            if (hParentWnd != NULL) InvalidateRect(hParentWnd, NULL, TRUE);
        }
    }
}

void ProcessGeminiMouseClick(float x, float y) {
    if (!isGeminiRunning) {
        float btnW = 260.0f; float btnH = 50.0f; float btnX = s_contentX + (s_contentW - btnW) / 2.0f; float btnY = s_contentY + (s_contentH / 2.0f);

        if (RectF(btnX, btnY, btnW, btnH).Contains(x, y)) {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            
            std::wstring userDataFolder = L"C:\\Users\\" + std::wstring(_wgetenv(L"USERNAME")) + L"\\AppData\\Local\\RasFocus\\User_Data";
            auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
            options->put_AdditionalBrowserArguments(L"--disable-features=BlockInsecurePrivateNetworkRequests --allow-running-insecure-content --no-sandbox");

            CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), options.Get(), new EnvCompletedHandler());
            
            isGeminiRunning = true;
            if (hParentWnd != NULL) InvalidateRect(hParentWnd, NULL, TRUE);
        }
    } 
    else if (!isPoppedOut) {
        float startX = s_contentX + 5; float cy = s_contentY;

        if (webView != nullptr) {
            if (RectF(startX, cy + 2, 30, 26).Contains(x, y)) { webView->GoBack(); }
            else if (RectF(startX + 32, cy + 2, 30, 26).Contains(x, y)) { webView->GoForward(); }
            else if (RectF(startX + 64, cy + 2, 30, 26).Contains(x, y)) { webView->Reload(); }
            else if (RectF(startX + 96, cy + 2, 30, 26).Contains(x, y)) { 
                webView->Navigate(L"https://gemini.google.com/?authuser=0");
            }
            // 4. Chrome-like Same Window Tab (+)
            else if (RectF(startX + 131, cy + 2, 30, 26).Contains(x, y)) {
                // Clicking '+' navigates the current window to Google
                webView->Navigate(L"https://www.google.com");
            }
            // 5. TRUE FULL SCREEN Button
            else if (RectF(s_contentX + s_contentW - 70, cy + 2, 30, 26).Contains(x, y)) {
                if (!hPopOutWnd) {
                    WNDCLASSEX wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, PopOutWndProc, 0, 0, GetModuleHandle(NULL), NULL, LoadCursor(NULL, IDC_ARROW), CreateSolidBrush(RGB(240,240,240)), NULL, "RasFocusPopOut", NULL };
                    RegisterClassEx(&wcex); 
                    
                    int sw = GetSystemMetrics(SM_CXSCREEN);
                    int sh = GetSystemMetrics(SM_CYSCREEN);
                    
                    // WS_POPUP makes it a borderless full screen window covering the whole monitor
                    hPopOutWnd = CreateWindowEx(WS_EX_TOPMOST, "RasFocusPopOut", "", 
                                                WS_POPUP | WS_CLIPCHILDREN, 0, 0, sw, sh, 
                                                NULL, NULL, GetModuleHandle(NULL), NULL);
                }
                isPoppedOut = true;
                webViewController->put_ParentWindow(hPopOutWnd); 
                ShowWindow(hPopOutWnd, SW_SHOWMAXIMIZED); 
                UpdateWindow(hPopOutWnd);
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            }
        }

        if (RectF(s_contentX + s_contentW - 35, cy + 2, 30, 26).Contains(x, y)) {
            if (webViewController != nullptr) { webViewController->Close(); webViewController = nullptr; webView = nullptr; }
            isGeminiRunning = false;
            if (hParentWnd != NULL) InvalidateRect(hParentWnd, NULL, TRUE);
        }
    }
}

void ProcessGeminiCommand(int id, int code) {}
