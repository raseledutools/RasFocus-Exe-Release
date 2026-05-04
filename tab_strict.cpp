// tab_strict.cpp

#include "tab_strict.h"
#include <vector>
#include <string>
#include <fstream>
#include <codecvt>
#include <locale>
#include <psapi.h>
#include <tlhelp32.h>
#include <uiautomation.h>
#include <thread>
#include <cstdlib> 
#include <shlwapi.h>

// --- WebView2 Headers ---
#include "WebView2.h"
#include <wrl.h>
#include <objbase.h>

using namespace std;
using namespace Microsoft::WRL;

// --- Global States & WebView2 ---
extern HWND hParentWnd;
extern float g_scaleFactor;

static ComPtr<ICoreWebView2Controller> strictWebViewController;
static ComPtr<ICoreWebView2> strictWebView;
static bool isStrictWebViewRunning = false;
static bool strictSettingsLoaded = false;

// --- Strict Protocols Internal States ---
static bool cbSilentUrl = true;   
static bool cbDnsFilter = false;  
static bool cbSafeSearch = true;  
static bool cbIncognito = true;   
static bool cbStrictMode = false; 

static int strictControlMode = 0; 
static int strictReligion = 0; 
static int strictLanguage = 0; 
static int totalBlockedCount = 0; 

static bool isStrictFocusActive = false;
static ULONGLONG strictFocusEndTime = 0;
static bool isPanicActive = false;
static DWORD panicStartTime = 0;

// --- Helper Functions ---
wstring toLowerW_Strict(wstring str) {
    for (auto& c : str) c = towlower(c); return str;
}

wstring GetStrictSaveFilePath() {
    wstring path = L"C:\\ProgramData\\RasFocus";
    CreateDirectoryW(path.c_str(), NULL);
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return path + L"\\rf_strict_data.dat";
}

void SaveStrictSettings() {
    wstring filePath = GetStrictSaveFilePath();
    std::wofstream out(filePath.c_str());
    out.imbue(std::locale(out.getloc(), new std::codecvt_utf8<wchar_t>));
    if (out.is_open()) {
        out << cbSilentUrl << L" " << cbDnsFilter << L" " << cbSafeSearch << L" " << cbIncognito << L" " << cbStrictMode << L"\n";
        out << strictControlMode << L" " << strictReligion << L" " << strictLanguage << L" " << totalBlockedCount << L"\n";
        out << isStrictFocusActive << L" " << strictFocusEndTime << L"\n";
        out.close();
    }
}

void LoadStrictSettings() {
    wstring filePath = GetStrictSaveFilePath();
    std::wifstream in(filePath.c_str());
    in.imbue(std::locale(in.getloc(), new std::codecvt_utf8<wchar_t>));
    if (in.is_open()) {
        in >> cbSilentUrl >> cbDnsFilter >> cbSafeSearch >> cbIncognito >> cbStrictMode;
        in >> strictControlMode >> strictReligion >> strictLanguage >> totalBlockedCount;
        in >> isStrictFocusActive >> strictFocusEndTime;
        
        if (isStrictFocusActive && strictControlMode == 0 && GetTickCount64() >= strictFocusEndTime) {
            isStrictFocusActive = false;
        }
        in.close();
    }
}

void SendStateToHtml() {
    if (!strictWebView) return;
    
    string json = "{\"type\":\"STATE_UPDATE\", \"cbSilentUrl\":" + to_string(cbSilentUrl) + 
                  ", \"cbDnsFilter\":" + to_string(cbDnsFilter) + 
                  ", \"cbSafeSearch\":" + to_string(cbSafeSearch) + 
                  ", \"cbIncognito\":" + to_string(cbIncognito) + 
                  ", \"cbStrictMode\":" + to_string(cbStrictMode) + 
                  ", \"mode\":" + to_string(strictControlMode) + 
                  ", \"rel\":" + to_string(strictReligion) + 
                  ", \"lang\":" + to_string(strictLanguage) + 
                  ", \"blockedCount\":" + to_string(totalBlockedCount) + 
                  ", \"isFocus\":" + (isStrictFocusActive ? "true" : "false") + 
                  ", \"isPanic\":" + (isPanicActive ? "true" : "false");

    if (isStrictFocusActive && strictControlMode == 0) {
        long long mLeft = max(0LL, (long long)((strictFocusEndTime - GetTickCount64()) / 60000));
        json += ", \"timeLeft\":" + to_string(mLeft);
    } else {
        json += ", \"timeLeft\":0";
    }
    json += "}";

    strictWebView->PostWebMessageAsJson(wstring(json.begin(), json.end()).c_str());
}

// --- Strict Logics ---
void SetFamilyDNS(bool enable) {
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas"; sei.lpFile = L"cmd.exe"; sei.nShow = SW_HIDE;
    wstring args = enable ? L"/c wmic nicconfig where (IPEnabled=TRUE) call SetDNSServerSearchOrder (\"1.1.1.3\", \"1.0.0.3\")" 
                          : L"/c wmic nicconfig where (IPEnabled=TRUE) call SetDNSServerSearchOrder ()";
    sei.lpParameters = args.c_str();
    ShellExecuteExW(&sei);
    WinExec("ipconfig /flushdns", SW_HIDE);
}

void EnforceStrictProtocols() {
    string hostsPath = "C:\\Windows\\System32\\drivers\\etc\\hosts";
    string tempPath = "C:\\Windows\\System32\\drivers\\etc\\hosts.temp";
    ifstream fileIn(hostsPath); ofstream fileOut(tempPath);
    string line; bool skip = false;

    if (fileIn.is_open() && fileOut.is_open()) {
        while (getline(fileIn, line)) {
            if (line.find("# RasFocus Strict Protocols Start") != string::npos) skip = true;
            if (!skip) fileOut << line << "\n";
            if (line.find("# RasFocus Strict Protocols End") != string::npos) skip = false;
        }
        fileIn.close();
    }
    
    if (cbDnsFilter || cbSafeSearch) {
        fileOut << "\n# RasFocus Strict Protocols Start\n";
        if (cbSafeSearch) {
            fileOut << "216.239.38.120 google.com\n216.239.38.120 www.google.com\n";
            fileOut << "204.79.197.220 bing.com\n204.79.197.220 www.bing.com\n";
            fileOut << "107.20.240.232 duckduckgo.com\n107.20.240.232 safe.duckduckgo.com\n";
            fileOut << "211.73.64.227 youtube.com\n211.73.64.227 www.youtube.com\n211.73.64.227 m.youtube.com\n";
        }
        if (cbDnsFilter) {
            vector<string> sites = { "pornhub.com", "xvideos.com", "xnxx.com", "xhamster.com", "redtube.com", "youporn.com" };
            for(const auto& site : sites) fileOut << "127.0.0.1 " << site << "\n127.0.0.1 www." << site << "\n";
        }
        fileOut << "# RasFocus Strict Protocols End\n";
    }
    fileOut.close();
    remove(hostsPath.c_str()); rename(tempPath.c_str(), hostsPath.c_str());
    WinExec("ipconfig /flushdns", SW_HIDE);
}

void StrictBackgroundThread() {
    while (true) {
        if (isPanicActive) {
            if (GetTickCount() - panicStartTime < 15 * 60 * 1000) { 
                HWND hActive = GetForegroundWindow();
                if (hActive) {
                    wchar_t t[256]; GetWindowTextW(hActive, t, 256);
                    wstring title = toLowerW_Strict(t);
                    if (title.find(L"chrome") != wstring::npos || title.find(L"edge") != wstring::npos || 
                        title.find(L"firefox") != wstring::npos || title.find(L"brave") != wstring::npos ||
                        title.find(L"opera") != wstring::npos) {
                        PostMessage(hActive, WM_CLOSE, 0, 0); 
                    }
                }
            } else { isPanicActive = false; SendStateToHtml(); }
        }

        if (cbStrictMode || isStrictFocusActive) {
            HWND hActive = GetForegroundWindow();
            if (hActive) {
                wchar_t t[256]; GetWindowTextW(hActive, t, 256); wstring title = toLowerW_Strict(t);
                if (title.find(L"task manager") != wstring::npos || title.find(L"taskmgr") != wstring::npos ||
                    title.find(L"registry editor") != wstring::npos || title.find(L"regedit") != wstring::npos ||
                    title.find(L"uninstall") != wstring::npos || title.find(L"programs and features") != wstring::npos ||
                    title.find(L"control panel") != wstring::npos) {
                    PostMessage(hActive, WM_CLOSE, 0, 0);
                }
            }
        }

        if (cbIncognito) {
            HWND hActive = GetForegroundWindow();
            if (hActive) {
                wchar_t t[256]; GetWindowTextW(hActive, t, 256); wstring title = toLowerW_Strict(t);
                if (title.find(L"incognito") != wstring::npos || title.find(L"inprivate") != wstring::npos || 
                    title.find(L"private browsing") != wstring::npos) {
                    keybd_event(VK_CONTROL, 0, 0, 0); keybd_event('W', 0, 0, 0);
                    keybd_event('W', 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
                }
            }
        }
        
        if (isStrictFocusActive && strictControlMode == 0 && GetTickCount64() >= strictFocusEndTime) {
            isStrictFocusActive = false;
            SaveStrictSettings();
            SendStateToHtml();
        }

        Sleep(500);
    }
}

// ==========================================
// WebView2 COM Handlers
// ==========================================
static const IID IID_IUnknown_Local = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const IID IID_ICoreWebView2WebMessageReceivedEventHandler_Local = { 0x57213F19, 0x00E6, 0x49FA, { 0x8E, 0x07, 0x89, 0x8E, 0xA0, 0x1E, 0xCB, 0xD2 } };
static const IID IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler_Local = { 0x6c4819f3, 0xc9b7, 0x4260, { 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c } };
static const IID IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler_Local = { 0x4e8a3389, 0xc9d8, 0x4bd2, { 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d } };

class StrictMessageReceivedHandler : public ICoreWebView2WebMessageReceivedEventHandler {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown_Local || riid == IID_ICoreWebView2WebMessageReceivedEventHandler_Local) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        LPWSTR message;
        if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message) {
            wstring msg(message);
            CoTaskMemFree(message);
            
            if (msg == L"REQUEST_STATE") { SendStateToHtml(); }
            else if (msg.find(L"TOGGLE:") == 0) {
                wstring toggle = msg.substr(7);
                if (toggle == L"cbSilent_1") cbSilentUrl = true; else if (toggle == L"cbSilent_0") cbSilentUrl = false;
                else if (toggle == L"cbDns_1") { cbDnsFilter = true; SetFamilyDNS(true); EnforceStrictProtocols(); } else if (toggle == L"cbDns_0") { cbDnsFilter = false; SetFamilyDNS(false); EnforceStrictProtocols(); }
                else if (toggle == L"cbSafe_1") { cbSafeSearch = true; EnforceStrictProtocols(); } else if (toggle == L"cbSafe_0") { cbSafeSearch = false; EnforceStrictProtocols(); }
                else if (toggle == L"cbIncog_1") cbIncognito = true; else if (toggle == L"cbIncog_0") cbIncognito = false;
                else if (toggle == L"cbStrict_1") cbStrictMode = true; else if (toggle == L"cbStrict_0") cbStrictMode = false;
                SaveStrictSettings(); SendStateToHtml();
            }
            else if (msg.find(L"SET_MODE:") == 0) { strictControlMode = stoi(msg.substr(9)); SaveStrictSettings(); }
            else if (msg.find(L"SET_REL:") == 0) { strictReligion = stoi(msg.substr(8)); SaveStrictSettings(); }
            else if (msg.find(L"SET_LANG:") == 0) { strictLanguage = stoi(msg.substr(9)); SaveStrictSettings(); }
            else if (msg.find(L"START_FOCUS:") == 0) {
                int mins = stoi(msg.substr(12));
                isStrictFocusActive = true;
                if (strictControlMode == 0) strictFocusEndTime = GetTickCount64() + ((ULONGLONG)mins * 60000);
                SaveStrictSettings(); SendStateToHtml();
            }
            else if (msg.find(L"STOP_FOCUS:") == 0) {
                isStrictFocusActive = false;
                SaveStrictSettings(); SendStateToHtml();
            }
            else if (msg.find(L"START_PANIC:") == 0) {
                isPanicActive = true; panicStartTime = GetTickCount();
                SendStateToHtml();
            }
        }
        return S_OK;
    }
};

class StrictControllerCompletedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown_Local || riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler_Local) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        if (controller != nullptr) {
            strictWebViewController = controller;
            strictWebViewController->get_CoreWebView2(&strictWebView);
            strictWebViewController->put_IsVisible(TRUE);

            EventRegistrationToken token;
            strictWebView->add_WebMessageReceived(new StrictMessageReceivedHandler(), &token);

            // --- ম্যাজিক: NavigateToString এর মাধ্যমে tab_strict.h থেকে HTML লোড হচ্ছে ---
            strictWebView->NavigateToString(HTML_STRICT_TAB.c_str());
        }
        return S_OK;
    }
};

class StrictEnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown_Local || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler_Local) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override {
        if (env != nullptr) {
            env->CreateCoreWebView2Controller(hParentWnd, new StrictControllerCompletedHandler());
        }
        return S_OK;
    }
};

// ==========================================
// --- MAIN DRAWING FUNCTION ---
// ==========================================
void DrawStrictProtocolsTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch) {
    if (!strictSettingsLoaded) {
        LoadStrictSettings();
        thread t(StrictBackgroundThread); t.detach();
        strictSettingsLoaded = true;
    }

    if (!isStrictWebViewRunning && hParentWnd != NULL) {
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        CreateCoreWebView2EnvironmentWithOptions(nullptr, L"RasFocus_AppData", nullptr, new StrictEnvCompletedHandler());
        isStrictWebViewRunning = true;
    }

    if (strictWebViewController != nullptr) {
        RECT bounds;
        bounds.left = (LONG)(cx * g_scaleFactor);
        bounds.top = (LONG)(cy * g_scaleFactor);
        bounds.right = (LONG)((cx + cw) * g_scaleFactor);
        bounds.bottom = (LONG)((cy + ch) * g_scaleFactor);
        strictWebViewController->put_Bounds(bounds);
        strictWebViewController->put_IsVisible(TRUE); 
    }

    Gdiplus::SolidBrush bBg(Gdiplus::Color(255, 255, 255, 255));
    g.FillRectangle(&bBg, cx, cy, cw, ch);
}

void HideStrictProtocolsTab() {
    if (strictWebViewController != nullptr) {
        strictWebViewController->put_IsVisible(FALSE);
    }
}

void ProcessStrictProtocolsMouseMove(float x, float y) {}
void ProcessStrictProtocolsMouseClick(float x, float y) {}
