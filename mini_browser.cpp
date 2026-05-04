#define _CRT_SECURE_NO_WARNINGS
#include "mini_browser.h"
#include "html_tools.h"
#include "WebView2.h"
#include <wrl.h>
#include <map>

using namespace Microsoft::WRL;

// আইকন আইডি যদি মেইন ফাইলে থাকে
#define IDI_APP_ICON 101 

struct MiniBrowserData {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
};

static std::map<HWND, MiniBrowserData> g_miniBrowsers;

// উইন্ডো প্রসিডিউর
LRESULT CALLBACK ViewerWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            if (g_miniBrowsers.count(hWnd) && g_miniBrowsers[hWnd].controller) {
                RECT bounds; GetClientRect(hWnd, &bounds);
                g_miniBrowsers[hWnd].controller->put_Bounds(bounds);
            }
            break;
        case WM_CLOSE: DestroyWindow(hWnd); break;
        case WM_DESTROY: g_miniBrowsers.erase(hWnd); break;
        default: return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// WebView2 কন্ট্রোলার হ্যান্ডলার
class ViewerControllerHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    std::wstring m_url; HWND m_hWnd;
public:
    ViewerControllerHandler(std::wstring url, HWND hWnd) : m_url(url), m_hWnd(hWnd) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { *ppv = this; return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        if (controller != nullptr) {
            g_miniBrowsers[m_hWnd].controller = controller;
            controller->get_CoreWebView2(&g_miniBrowsers[m_hWnd].webview);
            controller->put_IsVisible(TRUE);
            RECT b; GetClientRect(m_hWnd, &b); controller->put_Bounds(b);

            auto wv = g_miniBrowsers[m_hWnd].webview;
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

void LaunchMiniBrowser(std::wstring url, std::wstring title) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {0}; wc.lpfnWndProc = ViewerWndProc; wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"RasMiniBrowserClass"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc); registered = true;
    }

    HWND hNewWnd = CreateWindowExW(0, L"RasMiniBrowserClass", title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1050, 750, NULL, NULL, GetModuleHandle(NULL), NULL);

    ShowWindow(hNewWnd, SW_SHOW);
    std::wstring dataPath = L"C:\\ProgramData\\RasFocus\\ViewerData";
    CreateCoreWebView2EnvironmentWithOptions(nullptr, dataPath.c_str(), nullptr, 
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [url, hNewWnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                env->CreateCoreWebView2Controller(hNewWnd, new ViewerControllerHandler(url, hNewWnd));
                return S_OK;
            }).Get());
}
