#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <objbase.h>
#include <propidl.h>

#define _CRT_SECURE_NO_WARNINGS

// CMD স্ক্রিন আসা বন্ধ করার জন্য Linker Command
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:WinMainCRTStartup")

#include <windows.h>
#include <windowsx.h>

HWND hParentWnd = NULL;

#include <shellapi.h>
#include "pdf_reader/tab_pdf_workspace.h"
#include <shlobj.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <process.h>
#include <wininet.h>
#include <chrono>
#include <ctime>

#pragma comment(lib, "wininet.lib")

#include "firebase/app.h"

#include "browser/mini_browser.h"
#include "tab_blocks.h"
#include "tab_adult.h"
#include "tab_settings.h"
#include "tab_device_block.h"
#include "tab_deep_study.h"
#include "tab_utilities.h"
#include "tab_dashboard.h"
#include "tab_special.h"
#include "tab_statistics.h"
#include "tab_family_link.h" // ← Family Link Tab Header যুক্ত করা হয়েছে
#include "prewindow.h"
#include "accounts.h"   // ← My Account tab handler
#include "upgrade.h"    // ← Upgrade popup handler

using namespace Gdiplus;
using namespace std;

#define WM_TRAYICON (WM_USER + 1)
#define IDI_APP_ICON 101
#define IDR_OBSERVER_EXE 102

// --- AUTO UPDATE ---
const string CURRENT_VERSION = "v1.0.6";
const string GITHUB_USER = "raseledutools";
const string GITHUB_REPO = "RasFocus-update";

bool isUpdateReady     = false;
bool isCheckingUpdate  = false;
string newVersionStr   = "";
bool hoverUpdateBtn    = false;

ULONG_PTR gdiplusToken;
float g_scaleFactor = 1.0f;
int windowWidth  = 1024;
int windowHeight = 600;
bool isMaximized = true;

firebase::App* g_firebaseApp = nullptr;

bool g_isPureViewerMode  = false;
wstring currentWorkspacePdf = L"";

// Premium status — accounts.h/cpp must expose this
extern bool g_isPremiumUser;   // set to true after successful premium login
extern string g_loggedInUserUid; // Firebase auth UID from accounts.h
extern wstring g_loggedInName;   // Display name after login
extern wstring g_loggedInEmail;  // Email after login

// ==========================================
// SUBSCRIPTION & PACKAGE STATE
// ==========================================
// NOTE: g_currentPackage is defined in accounts.cpp — extern here to avoid LNK2005
extern string g_currentPackage; // FREE_BASIC, STUDENT, PREMIUM, PARENTAL, TRIAL
int g_daysLeft = 0;
wstring g_packageStatusText = L"Checking Subscription Status...";

NOTIFYICONDATA nid = {};

// ==========================================
// LAYOUT
// ==========================================
extern const int SIDEBAR_WIDTH      = 170;
extern const int TITLEBAR_HEIGHT    = 28;
extern const int SUBHEADER_HEIGHT   = 45;

// UI State
int selectedTab  = 0;
int hoveredTab   = -1;
bool hoverMinimize = false, hoverMaximize = false, hoverClose = false;
bool hoverUpgrade   = false;
bool hoverFeedback  = false;
bool hoverMyAccount = false;

// Feedback popup state
bool showFeedbackBox   = false;
wchar_t feedbackEmail[256]   = {};
wchar_t feedbackMessage[1024] = {};
int feedbackFocusField = 0;
bool hoverFeedbackSubmit = false;
bool hoverFeedbackClose  = false;

// Sidebar tabs (Family Link যোগ করা হয়েছে)
vector<wstring> sidebarTabs = {
    L"Dashboard", L"Blocks", L"Deep Study", L"Special", L"Statistics", L"Settings", L"Family Link"
};
vector<wstring> sidebarIcons = {
    L"\xE80F", L"\xEA18", L"\xE7B3", L"\xE734", L"\xE9D2", L"\xE713", L"\xE8A5"
};

// ==========================================
// COLOR PALETTE
// ==========================================
const Color ColTitleBar(255, 255, 255, 255);
const Color ColTitleBarText(255, 50, 50, 50);
const Color ColSubHeader(255, 0, 150, 160);
const Color ColSidebar(255, 0, 135, 145);
const Color ColSidebarActive(255, 0, 110, 120);
const Color ColSidebarHover(255, 0, 160, 170);
const Color ColWhite(255, 255, 255, 255);
const Color ColBgContent(255, 245, 248, 250);
const Color ColTextDark(255, 50, 50, 50);
const Color ColTextGray(255, 120, 120, 120);
const Color ColUpgradeBtn(255, 243, 156, 18);
const Color ColUpgradeHover(255, 211, 84, 0);
const Color ColTeal(255, 0, 140, 150);

bool isSafeBrowsingActive = false;
bool isStrictActive       = false;

bool RequestParentalAccess(HWND hwnd);

bool g_isAppDisabledByAdmin = false;

// ==========================================
// HARDWARE ID GENERATOR
// ==========================================
string GetHardwareID() {
    DWORD volSerial = 0;
    GetVolumeInformationA("C:\\", NULL, 0, &volSerial, NULL, NULL, NULL, 0);
    char hexStr[32];
    sprintf(hexStr, "%X", volSerial);
    return "device_" + string(hexStr);
}

// ==========================================
// FIRESTORE HTTP HELPER FUNCTION
// ==========================================
std::string SendFirestoreRequest(const std::string& method, const std::string& path, const std::string& payload = "") {
    std::string response = "";
    HINTERNET hInternet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return response;

    HINTERNET hConnect = InternetConnectA(hInternet, "firestore.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (hConnect) {
        HINTERNET hRequest = HttpOpenRequestA(hConnect, method.c_str(), path.c_str(), NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (hRequest) {
            if (method == "PATCH" || method == "POST") {
                std::string headers = "Content-Type: application/json\r\n";
                HttpSendRequestA(hRequest, headers.c_str(), headers.length(), (LPVOID)payload.c_str(), payload.length());
            } else {
                HttpSendRequestA(hRequest, NULL, 0, NULL, 0);
            }

            char buffer[1024];
            DWORD bytesRead = 0;
            while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                response += buffer;
            }
            InternetCloseHandle(hRequest);
        }
        InternetCloseHandle(hConnect);
    }
    InternetCloseHandle(hInternet);
    return response;
}

// ==========================================
// SUBSCRIPTION CHECK THREAD  (Bug Fixed)
// ==========================================
void __cdecl SubscriptionCheckThread(void* p) {
    Sleep(3000);
    std::string pc_id = GetHardwareID();

    while (true) {

        // ── 1. কোন path থেকে data আনব? ──────────────────────────────
        std::string path;
        bool isUser = !g_loggedInUserUid.empty();

        if (isUser)
            path = "/v1/projects/rasfocus-c746d/databases/(default)/documents/users/"
                   + g_loggedInUserUid;
        else
            path = "/v1/projects/rasfocus-c746d/databases/(default)/documents/devices/"
                   + pc_id;

        std::string response = SendFirestoreRequest("GET", path);

        // ── 2. Document পাওয়া যায়নি (প্রথমবার) ──────────────────────
        if (response.find("\"error\"") != std::string::npos &&
            response.find("NOT_FOUND") != std::string::npos)
        {
            if (!isUser) {
                // Device-এ প্রথমবার — TRIAL শুরু
                long long now      = (long long)time(nullptr);
                std::string nowStr = std::to_string(now);
                std::string payload =
                    "{\"fields\":{"
                    "\"current_package\":{\"stringValue\":\"TRIAL\"},"
                    "\"trial_start_date\":{\"integerValue\":\"" + nowStr + "\"}"
                    "}}";
                SendFirestoreRequest("PATCH", path, payload);

                g_currentPackage    = "TRIAL";
                g_isPremiumUser     = false;   // Trial শুরুতে প্রথমে false
                g_daysLeft          = 14;
                g_packageStatusText = L"Trial Active (14 Days Left)";

                // Trial শুরু হলে সাথে সাথে premium দাও
                g_isPremiumUser = true;
            } else {
                g_currentPackage    = "FREE_BASIC";
                g_isPremiumUser     = false;
                g_packageStatusText = L"Free Basic Version";
            }

            if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
            Sleep(60000);
            continue;
        }

        // ── 3. current_package parse ───────────────────────────────────
        {
            size_t pos = response.find("\"current_package\":");
            if (pos != std::string::npos) {
                size_t vs = response.find("stringValue\": \"", pos);
                if (vs != std::string::npos) {
                    vs += 15;
                    size_t ve = response.find("\"", vs);
                    if (ve != std::string::npos)
                        g_currentPackage = response.substr(vs, ve - vs);
                }
            }
        }

        // ── 4. PREMIUM / 1 Month Pro / 6 Month Saver / 1 Year Ultimate / STUDENT / PARENTAL
        //       → g_isPremiumUser = true + expiry check ────────────────
        bool isPaidPackage = (g_currentPackage == "PREMIUM"          ||
                              g_currentPackage == "1 Month Pro"      ||
                              g_currentPackage == "6 Month Saver"    || // <-- Added 6 Month Saver
                              g_currentPackage == "1 Year Ultimate"  ||
                              g_currentPackage == "STUDENT"          ||
                              g_currentPackage == "PARENTAL");

        if (isPaidPackage) {
            // expiryDate parse (Firestore integerValue)
            long long expiryTs = 0;
            size_t ep = response.find("\"expiryDate\":");
            if (ep != std::string::npos) {
                size_t iv = response.find("integerValue\": \"", ep);
                if (iv != std::string::npos) {
                    iv += 16;
                    size_t ive = response.find("\"", iv);
                    if (ive != std::string::npos)
                        expiryTs = std::stoll(response.substr(iv, ive - iv));
                }
            }

            long long now = (long long)time(nullptr);

            if (expiryTs > 0 && now > expiryTs) {
                // ── মেয়াদ শেষ → FREE_BASIC তে নামানো ──────────────
                g_currentPackage    = "FREE_BASIC";
                g_isPremiumUser     = false;   // ← মেইন সুইচ OFF
                g_packageStatusText = L"Subscription Expired — Free Basic";

                std::string upd =
                    "{\"fields\":{"
                    "\"current_package\":{\"stringValue\":\"FREE_BASIC\"}"
                    "}}";
                SendFirestoreRequest("PATCH", path, upd);

            } else {
                // ── মেয়াদ আছে → সব ফিচার আনলক ──────────────────────
                g_isPremiumUser = true;   // ← মেইন সুইচ ON

                if (g_currentPackage == "PREMIUM"          ||
                    g_currentPackage == "1 Month Pro"      ||
                    g_currentPackage == "6 Month Saver"    || // <-- Added 6 Month Saver
                    g_currentPackage == "1 Year Ultimate")
                {
                    long long daysLeft = (expiryTs > 0)
                                        ? (expiryTs - now) / 86400
                                        : -1;
                    if (daysLeft >= 0)
                        g_packageStatusText = L"Premium Active ("
                                              + std::to_wstring(daysLeft)
                                              + L" Days Left)";
                    else
                        g_packageStatusText = L"Premium Access Active";
                }
                else if (g_currentPackage == "STUDENT")
                    g_packageStatusText = L"Student Offer Active";
                else if (g_currentPackage == "PARENTAL")
                    g_packageStatusText = L"Parental Control Active";
            }
        }

        // ── 5. TRIAL → remaining days check ───────────────────────────
        else if (g_currentPackage == "TRIAL") {
            long long trialStart = 0;
            size_t tsPos = response.find("\"trial_start_date\":");
            if (tsPos != std::string::npos) {
                size_t iv = response.find("integerValue\": \"", tsPos);
                if (iv != std::string::npos) {
                    iv += 16;
                    size_t ive = response.find("\"", iv);
                    if (ive != std::string::npos)
                        trialStart = std::stoll(response.substr(iv, ive - iv));
                }
            }

            if (trialStart > 0) {
                long long now     = (long long)time(nullptr);
                long long elapsed = (now - trialStart) / 86400;
                long long left    = 14 - elapsed;

                if (left > 0) {
                    // Trial এখনো চলছে → সব ফিচার আনলক
                    g_isPremiumUser     = true;   // ← মেইন সুইচ ON
                    g_daysLeft          = (int)left;
                    g_packageStatusText = L"Trial Active ("
                                         + std::to_wstring(left)
                                         + L" Days Left)";
                } else {
                    // Trial শেষ → FREE_BASIC তে নামানো
                    g_isPremiumUser     = false;   // ← মেইন সুইচ OFF
                    g_currentPackage    = "FREE_BASIC";
                    g_daysLeft          = 0;
                    g_packageStatusText = L"Trial Expired — Free Basic";

                    std::string upd =
                        "{\"fields\":{"
                        "\"current_package\":{\"stringValue\":\"FREE_BASIC\"},"
                        "\"trial_start_date\":{\"integerValue\":\""
                        + std::to_string(trialStart) + "\"}"
                        "}}";
                    SendFirestoreRequest("PATCH", path, upd);
                }
            } else {
                // trial_start_date নেই — safe fallback, premium দাও
                g_isPremiumUser     = true;   // ← মেইন সুইচ ON
                g_packageStatusText = L"Trial Active";
            }
        }

        // ── 6. FREE_BASIC বা অচেনা package ───────────────────────────
        else {
            g_isPremiumUser     = false;   // ← মেইন সুইচ OFF
            g_currentPackage    = "FREE_BASIC";
            g_packageStatusText = L"Free Basic Version";
        }

        // ── 7. UI রিফ্রেশ ─────────────────────────────────────────────
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
        Sleep(60000);
    }
    _endthread();
}

// ==========================================
// FIREBASE KILL SWITCH
// ==========================================
void __cdecl FirebaseKillThread(void* p) {
    while (true) {
        string url = "https://rasfocus-c746d-default-rtdb.firebaseio.com/app_status.json?t=" + to_string(GetTickCount());
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        string savePath = string(tempPath) + "rf_status.json";
        DeleteUrlCacheEntryA(url.c_str());
        HRESULT hr = URLDownloadToFileA(NULL, url.c_str(), savePath.c_str(), 0, NULL);
        if (hr == S_OK) {
            ifstream inFile(savePath);
            string content((istreambuf_iterator<char>(inFile)), istreambuf_iterator<char>());
            inFile.close();
            remove(savePath.c_str());
            bool isDisabled = (content.find("\"is_active\":false") != string::npos ||
                               content.find("\"is_active\": false") != string::npos);
            if (isDisabled && !g_isAppDisabledByAdmin) {
                g_isAppDisabledByAdmin = true;
                if (hParentWnd) ShowWindow(hParentWnd, SW_HIDE);
                MessageBoxA(NULL, "This application has been disabled by the server administrator.",
                    "RasFocus+ - Access Denied", MB_OK | MB_ICONERROR | MB_TOPMOST);
            } else if (!isDisabled && g_isAppDisabledByAdmin) {
                g_isAppDisabledByAdmin = false;
                if (hParentWnd) {
                    ShowWindow(hParentWnd, SW_SHOWMAXIMIZED);
                    SetForegroundWindow(hParentWnd);
                }
                MessageBoxA(NULL, "Application access has been restored by admin.",
                    "RasFocus+", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            }
        }
        Sleep(5000);
    }
    _endthread();
}

// ==========================================
// HIDE ALL WEBVIEWS
// ==========================================
void HideAllWebViews() {
    if (!hParentWnd) return;
    EnumChildWindows(hParentWnd, [](HWND hwnd, LPARAM lParam) -> BOOL {
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        if (strstr(className, "Chrome_WidgetWin_") != nullptr) {
            ShowWindow(hwnd, SW_HIDE);
            SetWindowPos(hwnd, NULL, -10000, -10000, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
        }
        return TRUE;
    }, 0);
}

// ==========================================
// UTILITY
// ==========================================
string GetSecretDir() {
    static string secretPath;
    if (!secretPath.empty()) return secretPath;
    char appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        secretPath = string(appData) + "\\.rasfocus\\";
    } else {
        char currentDir[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, currentDir);
        secretPath = string(currentDir) + "\\rasfocus_data\\";
    }
    CreateDirectoryA(secretPath.c_str(), NULL);
    SetFileAttributesA(secretPath.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return secretPath;
}

string GetExePath() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    return string(path);
}

void RegisterFileAssociation(const string& ext, const string& progId, const string& desc) {
    string exePath = GetExePath();
    string command = "\"" + exePath + "\" \"%1\"";
    HKEY hKey;
    string extPath = "Software\\Classes\\" + ext;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, extPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "", 0, REG_SZ, (const BYTE*)progId.c_str(), (DWORD)(progId.length() + 1));
        RegCloseKey(hKey);
    }
    string progIdPath = "Software\\Classes\\" + progId;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, progIdPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "", 0, REG_SZ, (const BYTE*)desc.c_str(), (DWORD)(desc.length() + 1));
        RegCloseKey(hKey);
    }
    string iconPath = progIdPath + "\\DefaultIcon";
    if (RegCreateKeyExA(HKEY_CURRENT_USER, iconPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "", 0, REG_SZ, (const BYTE*)exePath.c_str(), (DWORD)(exePath.length() + 1));
        RegCloseKey(hKey);
    }
    string cmdPath = progIdPath + "\\shell\\open\\command";
    if (RegCreateKeyExA(HKEY_CURRENT_USER, cmdPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "", 0, REG_SZ, (const BYTE*)command.c_str(), (DWORD)(command.length() + 1));
        RegCloseKey(hKey);
    }
}

void SetupDefaultViewer() {
    RegisterFileAssociation(".pdf",  "RasFocus.PDF",   "RasFocus+ PDF Document");
    RegisterFileAssociation(".jpg",  "RasFocus.Image", "RasFocus+ Image File");
    RegisterFileAssociation(".png",  "RasFocus.Image", "RasFocus+ Image File");
    RegisterFileAssociation(".jpeg", "RasFocus.Image", "RasFocus+ Image File");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}

// ==========================================
// SILENT UPDATER
// ==========================================
void __cdecl SilentUpdateThread(void* p) {
    if (isCheckingUpdate || isUpdateReady) { _endthread(); return; }
    isCheckingUpdate = true;
    string secretDir = GetSecretDir();
    string apiFile   = secretDir + "api_response.json";
    string apiUrl    = "https://api.github.com/repos/" + GITHUB_USER + "/" + GITHUB_REPO + "/releases/latest";
    DeleteUrlCacheEntryA(apiUrl.c_str());
    HRESULT hrApi = URLDownloadToFileA(NULL, apiUrl.c_str(), apiFile.c_str(), 0, NULL);
    if (hrApi == S_OK) {
        ifstream vf(apiFile);
        string jsonContent((istreambuf_iterator<char>(vf)), istreambuf_iterator<char>());
        vf.close();
        remove(apiFile.c_str());
        string searchKey = "\"tag_name\":";
        size_t tagPos = jsonContent.find(searchKey);
        if (tagPos != string::npos) {
            size_t startQuote = jsonContent.find("\"", tagPos + searchKey.length());
            if (startQuote != string::npos) {
                size_t endQuote = jsonContent.find("\"", startQuote + 1);
                if (endQuote != string::npos) {
                    string latestVer = jsonContent.substr(startQuote + 1, endQuote - startQuote - 1);
                    if (latestVer != CURRENT_VERSION && latestVer.find("v") != string::npos) {
                        newVersionStr = latestVer;
                        string exeUrl = "https://github.com/" + GITHUB_USER + "/" + GITHUB_REPO +
                                        "/releases/download/" + latestVer + "/RasFocus.exe";
                        string updateExePath = secretDir + "RasFocus_New.exe";
                        HRESULT hrExe = URLDownloadToFileA(NULL, exeUrl.c_str(), updateExePath.c_str(), 0, NULL);
                        if (hrExe == S_OK) {
                            isUpdateReady = true;
                            HWND hWnd = FindWindowA("RasFocusCore", "RasFocus+");
                            if (hWnd) InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                }
            }
        }
    }
    isCheckingUpdate = false;
    _endthread();
}

void StartSilentUpdateCheck() { _beginthread(SilentUpdateThread, 0, NULL); }

void ApplySilentUpdate() {
    string secretDir      = GetSecretDir();
    string batPath        = secretDir + "updater.bat";
    string newExePath     = secretDir + "RasFocus_New.exe";
    string currentExePath = GetExePath();
    ofstream batFile(batPath);
    batFile << "@echo off\n"
            << "timeout /t 1 /nobreak >nul\n"
            << "taskkill /F /IM RasObserve.exe /T >nul 2>&1\n"
            << "taskkill /F /IM RasFocus.exe /T >nul 2>&1\n"
            << "timeout /t 1 /nobreak >nul\n"
            << "copy /Y \"" << newExePath     << "\" \"" << currentExePath << "\"\n"
            << "start \"\" \"" << currentExePath << "\"\n"
            << "del \"" << newExePath << "\"\n"
            << "del \"%~f0\"\n";
    batFile.close();
    string cmdExec = "cmd.exe /c \"" + batPath + "\"";
    STARTUPINFOA siBat = { sizeof(STARTUPINFOA) };
    siBat.dwFlags = STARTF_USESHOWWINDOW;
    siBat.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION piBat;
    CreateProcessA(NULL, (LPSTR)cmdExec.c_str(), NULL, NULL, FALSE,
                   CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &siBat, &piBat);
    exit(0);
}

// ==========================================
// SYSTEM
// ==========================================
bool IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

void CreateDesktopShortcut() {
    char desktopPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
        string mainShortcutPath      = string(desktopPath) + "\\RasFocus+.lnk";
        string miniBrowserShortcutPath = string(desktopPath) + "\\RasFocus+ Mini Browser.lnk";
        string exePath = GetExePath();
        CoInitialize(NULL);
        IShellLink* psl;
        if (GetFileAttributesA(mainShortcutPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl))) {
                IPersistFile* ppf;
                psl->SetPath("C:\\Windows\\System32\\schtasks.exe");
                psl->SetArguments("/run /tn \"RasFocusPro_AutoStart\"");
                psl->SetDescription("RasFocus+ - Block Apps & Adult Content");
                psl->SetIconLocation(exePath.c_str(), 0);
                if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf))) {
                    WCHAR wsz[MAX_PATH];
                    MultiByteToWideChar(CP_ACP, 0, mainShortcutPath.c_str(), -1, wsz, MAX_PATH);
                    ppf->Save(wsz, TRUE);
                    ppf->Release();
                }
                psl->Release();
            }
        }
        if (GetFileAttributesA(miniBrowserShortcutPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl))) {
                IPersistFile* ppf;
                psl->SetPath(exePath.c_str());
                psl->SetArguments("-minibrowser");
                psl->SetDescription("RasFocus+ Safe Mini Browser");
                psl->SetIconLocation(exePath.c_str(), 0);
                if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf))) {
                    WCHAR wsz[MAX_PATH];
                    MultiByteToWideChar(CP_ACP, 0, miniBrowserShortcutPath.c_str(), -1, wsz, MAX_PATH);
                    ppf->Save(wsz, TRUE);
                    ppf->Release();
                }
                psl->Release();
            }
        }
        CoUninitialize();
    }
}

void SetupAutoRun() {
    wchar_t szPath[MAX_PATH];
    GetModuleFileNameW(NULL, szPath, MAX_PATH);
    wstring pathStr = szPath;
    if (IsRunAsAdmin()) {
        wstring schCreate =
            L"schtasks.exe /create"
            L" /tn \"RasFocusPro_AutoStart\""
            L" /tr \"\\\"" + pathStr + L"\\\" -silent\""
            L" /sc onlogon"
            L" /rl highest"
            L" /f";
        STARTUPINFOW si1 = { sizeof(STARTUPINFOW) };
        si1.dwFlags = STARTF_USESHOWWINDOW; si1.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi1;
        if (CreateProcessW(NULL, (LPWSTR)schCreate.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si1, &pi1)) {
            WaitForSingleObject(pi1.hProcess, 5000);
            CloseHandle(pi1.hProcess); CloseHandle(pi1.hThread);
        }
        wstring schPowerFix =
            L"powershell.exe -WindowStyle Hidden -Command \""
            L"Set-ScheduledTask -TaskName 'RasFocusPro_AutoStart'"
            L" -Settings (New-ScheduledTaskSettingsSet"
            L" -AllowStartIfOnBatteries"
            L" -DontStopIfGoingOnBatteries"
            L" -ExecutionTimeLimit 0)\"";
        STARTUPINFOW si2 = { sizeof(STARTUPINFOW) };
        si2.dwFlags = STARTF_USESHOWWINDOW; si2.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi2;
        if (CreateProcessW(NULL, (LPWSTR)schPowerFix.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si2, &pi2)) {
            WaitForSingleObject(pi2.hProcess, 5000);
            CloseHandle(pi2.hProcess); CloseHandle(pi2.hThread);
        }
        wstring schStartup =
            L"powershell.exe -WindowStyle Hidden -Command \""
            L"$task = Get-ScheduledTask -TaskName 'RasFocusPro_AutoStart';"
            L"$trigger = New-ScheduledTaskTrigger -AtStartup;"
            L"$task.Triggers += $trigger;"
            L"Set-ScheduledTask -TaskName 'RasFocusPro_AutoStart' -Trigger $task.Triggers\"";
        STARTUPINFOW si3 = { sizeof(STARTUPINFOW) };
        si3.dwFlags = STARTF_USESHOWWINDOW; si3.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi3;
        if (CreateProcessW(NULL, (LPWSTR)schStartup.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si3, &pi3)) {
            WaitForSingleObject(pi3.hProcess, 5000);
            CloseHandle(pi3.hProcess); CloseHandle(pi3.hThread);
        }
    }
    {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            wstring regCmd = L"\"" + pathStr + L"\" -silent";
            RegSetValueExW(hKey, L"RasFocusPro", 0, REG_SZ,
                           (const BYTE*)regCmd.c_str(), (DWORD)((regCmd.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
    }
    if (IsRunAsAdmin()) {
        HKEY hKeyLM;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, KEY_SET_VALUE, &hKeyLM) == ERROR_SUCCESS) {
            wstring regCmd = L"\"" + pathStr + L"\" -silent";
            RegSetValueExW(hKeyLM, L"RasFocusPro", 0, REG_SZ,
                           (const BYTE*)regCmd.c_str(), (DWORD)((regCmd.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKeyLM);
        }
    }
}

void ExtractAndRunObserver() {
    WinExec("taskkill /F /IM RasObserve.exe", SW_HIDE);
    Sleep(50);
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(IDR_OBSERVER_EXE), RT_RCDATA);
    if (!hRes) return;
    HGLOBAL hData = LoadResource(NULL, hRes);
    void* pData = LockResource(hData);
    DWORD size  = SizeofResource(NULL, hRes);
    wstring folderPath = L"C:\\ProgramData\\RasFocus";
    CreateDirectoryW(folderPath.c_str(), NULL);
    wstring destPath = folderPath + L"\\RasObserve.exe";
    HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, pData, size, &written, NULL);
        CloseHandle(hFile);
    }
    wchar_t currentAppPath[MAX_PATH];
    GetModuleFileNameW(NULL, currentAppPath, MAX_PATH);
    wstring wAppPath(currentAppPath);
    wstring wWorkingDir = wAppPath.substr(0, wAppPath.find_last_of(L"\\/"));
    wstring cmdArgs = L"\"" + destPath + L"\" \"" + wAppPath + L"\"";
    wchar_t cmdBuffer[MAX_PATH * 2];
    wcscpy_s(cmdBuffer, cmdArgs.c_str());
    STARTUPINFOW si = { sizeof(STARTUPINFOW) };
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmdBuffer, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, wWorkingDir.c_str(), &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}

void AddTrayIcon(HWND hWnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd   = hWnd;
    nid.uID    = 1001;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon  = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    lstrcpy(nid.szTip, "RasFocus+ is running...");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon() { Shell_NotifyIcon(NIM_DELETE, &nid); }

// ==========================================
// FIREBASE FEEDBACK SUBMIT (Firestore REST)
// ==========================================
// ⚠ CHANGED: Realtime Database থেকে Firestore এ move করা হয়েছে
//            user UID + package + timestamp সহ feedback collection এ save হবে
void SubmitFeedbackToFirebase(const wstring& email, const wstring& message) {
    char emailA[512] = {}, msgA[2048] = {};
    WideCharToMultiByte(CP_UTF8, 0, email.c_str(),   -1, emailA, 511,  NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, msgA,  2047, NULL, NULL);

    // Timestamp
    long long ts  = (long long)time(nullptr);
    string tsStr  = to_string(ts);

    // UID — logged in হলে user UID, না হলে device ID
    string uid = g_loggedInUserUid.empty() ? GetHardwareID() : g_loggedInUserUid;

    // Unique document ID: timestamp_uid
    string docId = tsStr + "_" + uid;

    // Firestore path — feedback collection এ
    string path = "/v1/projects/rasfocus-c746d/databases/(default)/documents/feedback/" + docId;

    // Firestore JSON body (fields format)
    string jsonBody =
        "{\"fields\":{"
        "\"email\":{\"stringValue\":\"" + string(emailA) + "\"},"
        "\"message\":{\"stringValue\":\"" + string(msgA) + "\"},"
        "\"uid\":{\"stringValue\":\"" + uid + "\"},"
        "\"package\":{\"stringValue\":\"" + g_currentPackage + "\"},"
        "\"timestamp\":{\"integerValue\":\"" + tsStr + "\"}"
        "}}";

    string response = SendFirestoreRequest("PATCH", path, jsonBody);

    // Debug log (production এ সরিয়ে দিতে পারেন)
    wstring wResp(response.begin(), response.end());
    OutputDebugStringW((L"[Feedback] " + wResp + L"\n").c_str());
}

// ==========================================
// DRAWING
// ==========================================

// ------------------------------------------
// 1. TITLE BAR
// ------------------------------------------
void DrawTitleBar(Graphics& g, int w) {
    SolidBrush bgWhite(ColTitleBar);
    g.FillRectangle(&bgWhite, 0.0f, 0.0f, (float)w, (float)TITLEBAR_HEIGHT);

    Pen borderPen(Color(255, 220, 225, 230), 1.0f);
    g.DrawLine(&borderPen, 0.0f, (float)TITLEBAR_HEIGHT - 1.0f, (float)w, (float)TITLEBAR_HEIGHT - 1.0f);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");

    const int TB_LOGO_SIZE = 16;
    int tbActualSize = max(16, (int)(TB_LOGO_SIZE * g_scaleFactor));
    HICON hIconSm = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON),
                                     IMAGE_ICON, tbActualSize, tbActualSize, LR_DEFAULTCOLOR);
    if (hIconSm) {
        Bitmap bmp(hIconSm);
        float iconY = (TITLEBAR_HEIGHT - TB_LOGO_SIZE) / 2.0f;
        g.DrawImage(&bmp, 10.0f, iconY, (float)TB_LOGO_SIZE, (float)TB_LOGO_SIZE);
        DestroyIcon(hIconSm);
    }

    Font fTitle(&ff, 11, FontStyleBold, UnitPixel);
    SolidBrush textDark(ColTitleBarText);
    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);

    wstring fullTitleStr = L"RasFocus+ - " + g_packageStatusText;
    g.DrawString(fullTitleStr.c_str(), -1, &fTitle,
                 RectF(34.0f, 0.0f, 500.0f, (float)TITLEBAR_HEIGHT),
                 &fmtL, &textDark);

    float btnW = 42.0f;
    float btnH = (float)TITLEBAR_HEIGHT;
    float startX = (float)w - (btnW * 3);

    if (hoverMinimize) { SolidBrush b(Color(30, 0, 0, 0)); g.FillRectangle(&b, startX, 0.0f, btnW, btnH); }
    if (hoverMaximize) { SolidBrush b(Color(30, 0, 0, 0)); g.FillRectangle(&b, startX + btnW, 0.0f, btnW, btnH); }
    if (hoverClose)    { SolidBrush b(Color(255, 232, 17, 35)); g.FillRectangle(&b, startX + (btnW * 2), 0.0f, btnW, btnH); }

    Font fIcons(&ffIcons, 9, FontStyleRegular, UnitPixel);
    SolidBrush iconColor(Color(255, 80, 80, 80));
    SolidBrush iconWhite(ColWhite);
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);

    g.DrawString(L"\xE921", -1, &fIcons, RectF(startX, 0.0f, btnW, btnH), &fmtC, &iconColor);
    const wchar_t* maxIcon = isMaximized ? L"\xE923" : L"\xE922";
    g.DrawString(maxIcon,   -1, &fIcons, RectF(startX + btnW, 0.0f, btnW, btnH), &fmtC, &iconColor);
    g.DrawString(L"\xE8BB", -1, &fIcons, RectF(startX + (btnW * 2), 0.0f, btnW, btnH),
                 &fmtC, hoverClose ? &iconWhite : &iconColor);

    if (isUpdateReady) {
        float upgW = 150.0f;
        float upgH = (float)TITLEBAR_HEIGHT - 6.0f;
        float upgX = startX - upgW - 10.0f;
        float upgY = 3.0f;
        GraphicsPath upgPath;
        float r = 4.0f, d = r * 2.0f;
        upgPath.AddArc(upgX, upgY, d, d, 180.0f, 90.0f);
        upgPath.AddArc(upgX + upgW - d, upgY, d, d, 270.0f, 90.0f);
        upgPath.AddArc(upgX + upgW - d, upgY + upgH - d, d, d, 0.0f, 90.0f);
        upgPath.AddArc(upgX, upgY + upgH - d, d, d, 90.0f, 90.0f);
        upgPath.CloseFigure();
        SolidBrush upgBg(hoverUpdateBtn ? Color(255, 30, 215, 96) : Color(255, 0, 180, 70));
        g.FillPath(&upgBg, &upgPath);
        Font fUpg(&ff, 9, FontStyleBold, UnitPixel);
        wstring wVer(newVersionStr.begin(), newVersionStr.end());
        wstring btnTxt = L"Update " + wVer + L" Ready";
        SolidBrush white(ColWhite);
        g.DrawString(btnTxt.c_str(), -1, &fUpg, RectF(upgX, upgY, upgW, upgH), &fmtC, &white);
    }
}

// ------------------------------------------
// 2. SUB-HEADER
// ------------------------------------------
void DrawSubHeader(Graphics& g, int w) {
    float subX = 0.0f;
    float subY = (float)TITLEBAR_HEIGHT;
    float subW = (float)w;
    float subH = (float)SUBHEADER_HEIGHT;

    SolidBrush bgDeep(ColSubHeader);
    g.FillRectangle(&bgDeep, subX, subY, subW, subH);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    SolidBrush white(ColWhite);
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtTL;
    fmtTL.SetAlignment(StringAlignmentNear);
    fmtTL.SetLineAlignment(StringAlignmentCenter);

    const int LOGO_SIZE = 26;
    int actualLogoSize = max(26, (int)(LOGO_SIZE * g_scaleFactor));
    HICON hIconLg = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON),
                                     IMAGE_ICON, actualLogoSize, actualLogoSize, LR_DEFAULTCOLOR);
    if (hIconLg) {
        Bitmap bmp(hIconLg);
        float iconX = 16.0f;
        float iconY = subY + (subH - LOGO_SIZE) / 2.0f;
        g.DrawImage(&bmp, iconX, iconY, (float)LOGO_SIZE, (float)LOGO_SIZE);
        DestroyIcon(hIconLg);
    }

    Font fAppName(&ff, 18, FontStyleBold, UnitPixel);
    Font fVersion(&ff, 11, FontStyleRegular, UnitPixel);
    SolidBrush whiteAlpha(Color(200, 255, 255, 255));

    float textX = 16.0f + LOGO_SIZE + 10.0f;
    g.DrawString(L"RasFocus+", -1, &fAppName, RectF(textX, subY, 150.0f, subH), &fmtTL, &white);

    wstring wVer(CURRENT_VERSION.begin(), CURRENT_VERSION.end());
    g.DrawString(wVer.c_str(), -1, &fVersion, RectF(textX + 100.0f, subY + 2.0f, 60.0f, subH), &fmtTL, &whiteAlpha);

    float rightPad = 20.0f;
    float btnH     = 28.0f;
    float btnY     = subY + (subH - btnH) / 2.0f;

    float acBtnW  = 110.0f;
    float acBtnX  = (float)w - rightPad - acBtnW;

    GraphicsPath acPath;
    float r2 = 4.0f, d2 = r2 * 2.0f;
    acPath.AddArc(acBtnX, btnY, d2, d2, 180.0f, 90.0f);
    acPath.AddArc(acBtnX + acBtnW - d2, btnY, d2, d2, 270.0f, 90.0f);
    acPath.AddArc(acBtnX + acBtnW - d2, btnY + btnH - d2, d2, d2, 0.0f, 90.0f);
    acPath.AddArc(acBtnX, btnY + btnH - d2, d2, d2, 90.0f, 90.0f);
    acPath.CloseFigure();

    SolidBrush acBg(hoverMyAccount ? Color(80, 255, 255, 255) : Color(45, 255, 255, 255));
    g.FillPath(&acBg, &acPath);
    Pen acBorder(Color(100, 255, 255, 255), 1.0f);
    g.DrawPath(&acBorder, &acPath);

    Font fBtnIcon(&ffIcons, 13, FontStyleRegular, UnitPixel);
    g.DrawString(L"\xE77B", -1, &fBtnIcon, RectF(acBtnX + 6.0f, btnY, 20.0f, btnH), &fmtC, &white);

    Font fBtnTxt(&ff, 11, FontStyleBold, UnitPixel);
    // Sign in korle name ba email user part dekha, na thakle "My Account"
    wstring sidebarAccLabel = L"My Account";
    if (!g_loggedInName.empty()) {
        sidebarAccLabel = g_loggedInName.length() > 12 ? g_loggedInName.substr(0, 12) : g_loggedInName;
    } else if (!g_loggedInEmail.empty()) {
        size_t atPos = g_loggedInEmail.find(L'@');
        wstring emailUser = (atPos != wstring::npos) ? g_loggedInEmail.substr(0, atPos) : g_loggedInEmail;
        sidebarAccLabel = emailUser.length() > 12 ? emailUser.substr(0, 12) : emailUser;
    }
    g.DrawString(sidebarAccLabel.c_str(), -1, &fBtnTxt, RectF(acBtnX + 28.0f, btnY, acBtnW - 30.0f, btnH), &fmtTL, &white);

    float fbIconW = 60.0f;
    float fbIconX = acBtnX - fbIconW - 10.0f;

    if (hoverFeedback) {
        SolidBrush fbHover(Color(50, 255, 255, 255));
        GraphicsPath fbPath;
        fbPath.AddArc(fbIconX, btnY, d2, d2, 180, 90);
        fbPath.AddArc(fbIconX+fbIconW-d2, btnY, d2, d2, 270, 90);
        fbPath.AddArc(fbIconX+fbIconW-d2, btnY+btnH-d2, d2, d2, 0, 90);
        fbPath.AddArc(fbIconX, btnY+btnH-d2, d2, d2, 90, 90);
        fbPath.CloseFigure();
        g.FillPath(&fbHover, &fbPath);
    }

    Font fFbIcon(&ffIcons, 16, FontStyleRegular, UnitPixel);
    Font fFbTxt(&ff, 9, FontStyleRegular, UnitPixel);

    g.DrawString(L"\xE8C3", -1, &fFbIcon, RectF(fbIconX, btnY + 1.0f, fbIconW, 14.0f), &fmtC, &white);
    g.DrawString(L"Feedback", -1, &fFbTxt, RectF(fbIconX, btnY + 15.0f, fbIconW, 14.0f), &fmtC, &whiteAlpha);
}

// ------------------------------------------
// 3. SIDEBAR (WITH UPGRADE BUTTON)
// ------------------------------------------
void DrawSidebar(Graphics& g, int h) {
    float sideX = 0.0f;
    float sideY = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
    float sideH = (float)(h - sideY);

    SolidBrush bgTeal(ColSidebar);
    g.FillRectangle(&bgTeal, sideX, sideY, (float)SIDEBAR_WIDTH, sideH);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    StringFormat fmtTL; fmtTL.SetAlignment(StringAlignmentNear); fmtTL.SetLineAlignment(StringAlignmentCenter);
    SolidBrush white(ColWhite);

    float tabsStartY = sideY + 20.0f;
    Font fTabTxt(&ff, 15, FontStyleBold, UnitPixel);
    Font fTabIcon(&ffIcons, 18, FontStyleRegular, UnitPixel);
    SolidBrush tealText(ColSubHeader);
    float tabH  = 50.0f;
    float iconW = 42.0f;

    StringFormat fmtIC; fmtIC.SetAlignment(StringAlignmentCenter); fmtIC.SetLineAlignment(StringAlignmentCenter);

    for (size_t i = 0; i < sidebarTabs.size(); ++i) {
        float tabY = tabsStartY + (float)i * tabH;
        RectF tabRect(sideX, tabY, (float)SIDEBAR_WIDTH, tabH);

        int logicalTab = (i == 6) ? 8 : i; // 6th index is Family Link which is selectedTab == 8

        if (selectedTab == logicalTab) {
            SolidBrush activeBg(ColWhite);
            g.FillRectangle(&activeBg, tabRect);
            SolidBrush accentBar(ColSubHeader);
            g.FillRectangle(&accentBar, sideX, tabY, 4.0f, tabH);
            g.DrawString(sidebarIcons[i].c_str(), -1, &fTabIcon, RectF(sideX, tabY, iconW, tabH), &fmtIC, &tealText);
            g.DrawString(sidebarTabs[i].c_str(),  -1, &fTabTxt,  RectF(sideX + iconW, tabY, (float)SIDEBAR_WIDTH - iconW - 8.0f, tabH), &fmtTL, &tealText);
        } else {
            if (hoveredTab == (int)i) {
                SolidBrush hoverBg(ColSidebarHover);
                g.FillRectangle(&hoverBg, tabRect);
            }
            g.DrawString(sidebarIcons[i].c_str(), -1, &fTabIcon, RectF(sideX, tabY, iconW, tabH), &fmtIC, &white);
            g.DrawString(sidebarTabs[i].c_str(),  -1, &fTabTxt,  RectF(sideX + iconW, tabY, (float)SIDEBAR_WIDTH - iconW - 8.0f, tabH), &fmtTL, &white);
        }
    }

    // ── Upgrade Button ──
    if (g_currentPackage == "FREE_BASIC" || g_currentPackage == "TRIAL") {
        float upgH  = 38.0f;
        float upgY  = sideY + sideH - upgH - 16.0f;
        float upgMX = 15.0f;
        float upgW  = (float)SIDEBAR_WIDTH - upgMX * 2.0f;
        GraphicsPath upgPath;
        float r = 7.0f, d = r * 2.0f;
        upgPath.AddArc(upgMX, upgY, d, d, 180.0f, 90.0f);
        upgPath.AddArc(upgMX + upgW - d, upgY, d, d, 270.0f, 90.0f);
        upgPath.AddArc(upgMX + upgW - d, upgY + upgH - d, d, d, 0.0f, 90.0f);
        upgPath.AddArc(upgMX, upgY + upgH - d, d, d, 90.0f, 90.0f);
        upgPath.CloseFigure();

        SolidBrush btnColor(hoverUpgrade ? ColUpgradeHover : ColUpgradeBtn);
        g.FillPath(&btnColor, &upgPath);

        Font fUpg(&ff, 13, FontStyleBold, UnitPixel);
        g.DrawString(L"\u2B06  Upgrade Now", -1, &fUpg, RectF(upgMX, upgY, upgW, upgH), &fmtIC, &white);
    }
}

// ------------------------------------------
// 4. FEEDBACK POPUP
// ------------------------------------------
void DrawFeedbackPopup(Graphics& g, int w, int h) {
    if (!showFeedbackBox) return;
    SolidBrush overlay(Color(140, 0, 0, 0));
    g.FillRectangle(&overlay, 0.0f, 0.0f, (float)w, (float)h);

    float popW = 400.0f, popH = 280.0f;
    float popX = (w - popW) / 2.0f;
    float popY = (h - popH) / 2.0f;

    GraphicsPath cardPath;
    float rc = 10.0f, dc = rc * 2.0f;
    cardPath.AddArc(popX, popY, dc, dc, 180.0f, 90.0f);
    cardPath.AddArc(popX + popW - dc, popY, dc, dc, 270.0f, 90.0f);
    cardPath.AddArc(popX + popW - dc, popY + popH - dc, dc, dc, 0.0f, 90.0f);
    cardPath.AddArc(popX, popY + popH - dc, dc, dc, 90.0f, 90.0f);
    cardPath.CloseFigure();
    SolidBrush cardBg(ColWhite);
    g.FillPath(&cardBg, &cardPath);

    GraphicsPath headerPath;
    headerPath.AddArc(popX, popY, dc, dc, 180.0f, 90.0f);
    headerPath.AddArc(popX + popW - dc, popY, dc, dc, 270.0f, 90.0f);
    headerPath.AddLine(popX + popW, popY + 40.0f, popX, popY + 40.0f);
    headerPath.CloseFigure();
    SolidBrush headerBg(ColSubHeader);
    g.FillPath(&headerBg, &headerPath);

    FontFamily ff(L"Segoe UI");
    SolidBrush white(ColWhite), darkText(Color(255, 60, 60, 60)), grayText(Color(255, 140, 140, 140));
    Font fHeader(&ff, 13, FontStyleBold, UnitPixel), fLabel(&ff, 10, FontStyleRegular, UnitPixel), fInput(&ff, 11, FontStyleRegular, UnitPixel);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"Send Feedback", -1, &fHeader, RectF(popX + 16.0f, popY, popW - 40.0f, 40.0f), &fmtL, &white);

    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fCloseIcon(&ffIcons, 11, FontStyleRegular, UnitPixel);
    SolidBrush closeColor(hoverFeedbackClose ? Color(255, 232, 17, 35) : ColWhite);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"\xE8BB", -1, &fCloseIcon, RectF(popX + popW - 32.0f, popY, 32.0f, 40.0f), &fmtC, &closeColor);

    float fieldX = popX + 20.0f, fieldW = popW - 40.0f;
    g.DrawString(L"Email", -1, &fLabel, RectF(fieldX, popY + 52.0f, fieldW, 16.0f), &fmtL, &grayText);
    Pen fieldBorder(feedbackFocusField == 1 ? Color(255, 0, 140, 150) : Color(255, 200, 205, 210), 1.5f);
    g.DrawRectangle(&fieldBorder, fieldX, popY + 70.0f, fieldW, 28.0f);
    wstring emailStr(feedbackEmail);
    g.DrawString(emailStr.empty() ? L"your@email.com" : emailStr.c_str(), -1, &fInput,
                 RectF(fieldX + 6.0f, popY + 70.0f, fieldW - 12.0f, 28.0f), &fmtL,
                 emailStr.empty() ? &grayText : &darkText);

    g.DrawString(L"Message", -1, &fLabel, RectF(fieldX, popY + 110.0f, fieldW, 16.0f), &fmtL, &grayText);
    Pen msgBorder(feedbackFocusField == 2 ? Color(255, 0, 140, 150) : Color(255, 200, 205, 210), 1.5f);
    g.DrawRectangle(&msgBorder, fieldX, popY + 128.0f, fieldW, 60.0f);
    wstring msgStr(feedbackMessage);
    g.DrawString(msgStr.empty() ? L"Write your message here..." : msgStr.c_str(), -1, &fInput,
                 RectF(fieldX + 6.0f, popY + 132.0f, fieldW - 12.0f, 52.0f), &fmtL,
                 msgStr.empty() ? &grayText : &darkText);

    float sbW = 110.0f, sbH = 32.0f, sbX = popX + popW - 20.0f - sbW, sbY = popY + popH - 16.0f - sbH;
    GraphicsPath sbPath; float rs = 5.0f, ds = rs * 2.0f;
    sbPath.AddArc(sbX, sbY, ds, ds, 180.0f, 90.0f); sbPath.AddArc(sbX + sbW - ds, sbY, ds, ds, 270.0f, 90.0f);
    sbPath.AddArc(sbX + sbW - ds, sbY + sbH - ds, ds, ds, 0.0f, 90.0f); sbPath.AddArc(sbX, sbY + sbH - ds, ds, ds, 90.0f, 90.0f);
    sbPath.CloseFigure();
    SolidBrush sbBg(hoverFeedbackSubmit ? Color(255, 0, 110, 120) : ColSubHeader);
    g.FillPath(&sbBg, &sbPath);
    Font fSbTxt(&ff, 10, FontStyleBold, UnitPixel);
    g.DrawString(L"Submit", -1, &fSbTxt, RectF(sbX, sbY, sbW, sbH), &fmtC, &white);
}

// ------------------------------------------
// MAIN AREA
// ------------------------------------------
void DrawMainArea(Graphics& g, int w, int h) {
    float contentX = (float)SIDEBAR_WIDTH;
    float contentY = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
    float contentW = (float)(w - SIDEBAR_WIDTH);
    float contentH = (float)(h - TITLEBAR_HEIGHT - SUBHEADER_HEIGHT);

    if      (selectedTab == 0) { DrawDashboardTab    (g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 1) { DrawBlocksTab       (g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 2) { DrawDeepStudyTab    (g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 3) { DrawSpecialFeatureTab(g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 4) { DrawStatisticsTab   (g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 5) { DrawSettingsTab     (g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 6) { DrawPdfWorkspaceTab (g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 7) { DrawAccountsTab     (g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 8) { DrawFamilyLinkTab   (g, contentX, contentY, contentW, contentH); } // ← Family Link Tab Draw Call
}

// ------------------------------------------
// ON PAINT
// ------------------------------------------
void OnPaint(HWND hWnd, HDC hdc) {
    RECT r; GetClientRect(hWnd, &r);
    int w = r.right - r.left;
    int h = r.bottom - r.top;

    HDC mdc  = CreateCompatibleDC(hdc);
    HBITMAP mbmp = CreateCompatibleBitmap(hdc, w, h);
    SelectObject(mdc, mbmp);

    Graphics g(mdc);

    // 🔥 IMAGE QUALITY FIX: লোগো বা ছবিগুলো একদম ভেক্টরের মতো ক্রিস্প দেখাবে
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    g.ScaleTransform(g_scaleFactor, g_scaleFactor);
    int scaledW = (int)(w / g_scaleFactor);
    int scaledH = (int)(h / g_scaleFactor);

    DrawMainArea  (g, scaledW, scaledH);
    DrawSidebar   (g, scaledH);
    DrawSubHeader (g, scaledW);
    DrawTitleBar  (g, scaledW);

    DrawFeedbackPopup(g, scaledW, scaledH);
    DrawUpgradePopup(g, scaledW, scaledH); // ← Upgrade Popup Draw Call

    if (showDailyMessage || onboardingStep > 0) {
        DrawPreWindowOverlay(g, scaledW, scaledH, g_scaleFactor);
    }

    BitBlt(hdc, 0, 0, w, h, mdc, 0, 0, SRCCOPY);
    DeleteObject(mbmp);
    DeleteDC(mdc);
}

// ==========================================
// COORDINATE HELPERS
// ==========================================
inline bool HitFeedbackIcon(float x, float y, float w) {
    float subY  = (float)TITLEBAR_HEIGHT;
    float subH  = (float)SUBHEADER_HEIGHT;
    float btnH  = 28.0f;
    float btnY  = subY + (subH - btnH) / 2.0f;
    float acBtnW = 110.0f;
    float acBtnX = w - 20.0f - acBtnW;
    float fbIconW = 60.0f;
    float fbIconX = acBtnX - fbIconW - 10.0f;
    return (x >= fbIconX && x <= fbIconX + fbIconW && y >= btnY && y <= btnY + btnH);
}

inline bool HitMyAccount(float x, float y, float w) {
    float subY  = (float)TITLEBAR_HEIGHT;
    float subH  = (float)SUBHEADER_HEIGHT;
    float btnH  = 28.0f;
    float btnY  = subY + (subH - btnH) / 2.0f;
    float acBtnW = 110.0f;
    float acBtnX = w - 20.0f - acBtnW;
    return (x >= acBtnX && x <= acBtnX + acBtnW && y >= btnY && y <= btnY + btnH);
}

struct PopupRects { float popX, popY, popW, popH; };
PopupRects GetPopupRects(int w, int h) {
    float popW = 400.0f, popH = 280.0f;
    return { (w - popW) / 2.0f, (h - popH) / 2.0f, popW, popH };
}

// ==========================================
// WINDOW PROCEDURE
// ==========================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER: {
        if (wp == 1005) StartSilentUpdateCheck();

        // ── Family Link: 1-second tick — polling + enforcement ──
        if (wp == 1001) {
            ProcessFamilyLinkTimer(wp, hWnd);           // Firebase poll ticker (every 5s)
            FamilyLink_EnforceParentCommands(hWnd); // apply all parent commands
        }
        break;
    }

    case WM_NCCALCSIZE: {
        if (wp == TRUE) return 0;
        break;
    }

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hWnd, &pt);
        int border = 8;
        RECT r; GetClientRect(hWnd, &r);

        if (pt.y < border && pt.x < border)                   return HTTOPLEFT;
        if (pt.y < border && pt.x >= r.right - border)        return HTTOPRIGHT;
        if (pt.y >= r.bottom - border && pt.x < border)       return HTBOTTOMLEFT;
        if (pt.y >= r.bottom - border && pt.x >= r.right - border) return HTBOTTOMRIGHT;
        if (pt.y < border)              return HTTOP;
        if (pt.y >= r.bottom - border)  return HTBOTTOM;
        if (pt.x < border)              return HTLEFT;
        if (pt.x >= r.right - border)   return HTRIGHT;

        if (pt.y < TITLEBAR_HEIGHT * g_scaleFactor) {
            float x = pt.x / g_scaleFactor;
            float scaledW = (r.right - r.left) / g_scaleFactor;
            float btnW = 42.0f;
            float controlsStartX = scaledW - (btnW * 3);
            if (x >= controlsStartX) return HTCLIENT;
            if (isUpdateReady) {
                float upgW = 150.0f;
                float upgX = controlsStartX - upgW - 10.0f;
                if (x >= upgX && x <= upgX + upgW) return HTCLIENT;
            }
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_GETMINMAXINFO: {
        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lp;
        lpMMI->ptMinTrackSize.x = (LONG)(1024 * g_scaleFactor);
        lpMMI->ptMinTrackSize.y = (LONG)(600  * g_scaleFactor);
        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfo(hMonitor, &mi)) {
            lpMMI->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            lpMMI->ptMaxPosition.y = mi.rcWork.top  - mi.rcMonitor.top;
            lpMMI->ptMaxSize.x     = mi.rcWork.right  - mi.rcWork.left;
            lpMMI->ptMaxSize.y     = (mi.rcWork.bottom - mi.rcWork.top) - 2;
        }
        return 0;
    }

    case WM_SIZE: {
        if (wp == SIZE_MAXIMIZED) isMaximized = true;
        else if (wp == SIZE_RESTORED) isMaximized = false;
        RECT r; GetClientRect(hWnd, &r);
        windowWidth  = r.right - r.left;
        windowHeight = r.bottom - r.top;
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }

    case WM_CLOSE:    { ShowWindow(hWnd, SW_HIDE); return 0; }
    case WM_SYSCOMMAND: {
        if ((wp & 0xFFF0) == SC_CLOSE) { ShowWindow(hWnd, SW_HIDE); return 0; }
        return DefWindowProc(hWnd, msg, wp, lp);
    }

    case WM_MOUSEMOVE: {
        float x = GET_X_LPARAM(lp) / g_scaleFactor;
        float y = GET_Y_LPARAM(lp) / g_scaleFactor;
        float scaledW = windowWidth  / g_scaleFactor;
        float scaledH = windowHeight / g_scaleFactor;

        if (showDailyMessage || onboardingStep > 0) {
            if (HandlePreWindowMouseMove(x, y, scaledW, scaledH)) { InvalidateRect(hWnd, NULL, FALSE); break; }
        }

        // ← Upgrade Popup Intercept
        if (g_showUpgradePopup) {
            ProcessUpgradeMouseMove(x, y);
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        bool redraw = false;

        if (showFeedbackBox) {
            auto pr = GetPopupRects((int)scaledW, (int)scaledH);
            float sbW = 110.0f, sbH = 32.0f, sbX = pr.popX + pr.popW - 20.0f - sbW, sbY = pr.popY + pr.popH - 16.0f - sbH;
            bool newFS = (x >= sbX && x <= sbX + sbW && y >= sbY && y <= sbY + sbH);
            bool newFC = (x >= pr.popX + pr.popW - 32.0f && x <= pr.popX + pr.popW && y >= pr.popY && y <= pr.popY + 40.0f);
            if (newFS != hoverFeedbackSubmit || newFC != hoverFeedbackClose) {
                hoverFeedbackSubmit = newFS; hoverFeedbackClose  = newFC; redraw = true;
            }
            if (redraw) InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        if (selectedTab == 7) {
            ProcessAccountsMouseMove(x, y);
            redraw = true;
        }

        float btnW = 42.0f;
        bool oldMin = hoverMinimize, oldMax = hoverMaximize, oldClose = hoverClose;
        hoverMinimize = (y <= TITLEBAR_HEIGHT && x >= scaledW - (btnW*3) && x < scaledW - (btnW*2));
        hoverMaximize = (y <= TITLEBAR_HEIGHT && x >= scaledW - (btnW*2) && x < scaledW - btnW);
        hoverClose    = (y <= TITLEBAR_HEIGHT && x >= scaledW - btnW);
        if (oldMin != hoverMinimize || oldMax != hoverMaximize || oldClose != hoverClose) redraw = true;

        bool oldUpgBtn = hoverUpdateBtn; hoverUpdateBtn = false;
        if (isUpdateReady) {
            float upgW = 150.0f, upgX = scaledW - (btnW*3) - upgW - 10.0f;
            if (x >= upgX && x <= upgX + upgW && y >= 0.0f && y <= (float)TITLEBAR_HEIGHT) hoverUpdateBtn = true;
        }
        if (oldUpgBtn != hoverUpdateBtn) redraw = true;

        bool oldFb = hoverFeedback,  oldAc = hoverMyAccount;
        hoverFeedback  = HitFeedbackIcon(x, y, scaledW);
        hoverMyAccount = HitMyAccount   (x, y, scaledW);
        if (oldFb != hoverFeedback || oldAc != hoverMyAccount) redraw = true;

        int oldTab = hoveredTab; hoveredTab = -1;
        float sideY = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
        float tabsStartY = sideY + 20.0f;
        float tabH  = 50.0f;
        if (x >= 0.0f && x <= SIDEBAR_WIDTH && y >= tabsStartY) {
            int idx = (int)((y - tabsStartY) / tabH);
            if (idx >= 0 && idx < (int)sidebarTabs.size()) hoveredTab = idx;
        }
        if (oldTab != hoveredTab) redraw = true;

        if (g_currentPackage == "FREE_BASIC" || g_currentPackage == "TRIAL") {
            bool oldUpg = hoverUpgrade;
            float upgH  = 38.0f;
            float sideContentY = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
            float sideContentH = scaledH - sideContentY;
            float upgBtnY = sideContentY + sideContentH - upgH - 16.0f;
            float upgMX = 15.0f;
            hoverUpgrade = (x >= upgMX && x <= SIDEBAR_WIDTH - upgMX && y >= upgBtnY && y <= upgBtnY + upgH);
            if (oldUpg != hoverUpgrade) redraw = true;
        }

        if      (selectedTab == 0) { ProcessDashboardMouseMove(x, y);   redraw = true; }
        else if (selectedTab == 1) { ProcessBlocksMouseMove(x, y);      redraw = true; }
        else if (selectedTab == 2) { ProcessDeepStudyMouseMove(x, y);   redraw = true; }
        else if (selectedTab == 3) { ProcessSpecialFeatureMouseMove(x, y);     redraw = true; }
        else if (selectedTab == 5) { ProcessSettingsMouseMove(x, y);    redraw = true; }
        else if (selectedTab == 4) {
            float cX = (float)SIDEBAR_WIDTH, cY = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
            float cW = scaledW - cX;
            ProcessStatisticsMouseMove(x, y, cX, cY, cW);
            redraw = true;
        }
        else if (selectedTab == 8) { // ← Family Link Mouse Move Handled
            float cX = (float)SIDEBAR_WIDTH, cY = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
            ProcessFamilyLinkMouseMove(x, y, cX, cY);
            redraw = true;
        }

        if (redraw) InvalidateRect(hWnd, NULL, FALSE);
        break;
    }

    case WM_LBUTTONDOWN: {
        float x = GET_X_LPARAM(lp) / g_scaleFactor;
        float y = GET_Y_LPARAM(lp) / g_scaleFactor;
        float scaledW = windowWidth  / g_scaleFactor;
        float scaledH = windowHeight / g_scaleFactor;

        if (showDailyMessage || onboardingStep > 0) {
            if (HandlePreWindowClick(x, y, selectedTab)) InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        // ← Upgrade Popup Intercept
        if (g_showUpgradePopup) {
            ProcessUpgradeMouseClick(x, y, hWnd);
            break;
        }

        if (showFeedbackBox) {
            auto pr = GetPopupRects((int)scaledW, (int)scaledH);
            if (x >= pr.popX + pr.popW - 32.0f && x <= pr.popX + pr.popW && y >= pr.popY && y <= pr.popY + 40.0f) {
                showFeedbackBox = false; InvalidateRect(hWnd, NULL, FALSE); break;
            }
            if (x >= pr.popX + 20.0f && x <= pr.popX + pr.popW - 20.0f && y >= pr.popY + 70.0f && y <= pr.popY + 98.0f) {
                feedbackFocusField = 1; InvalidateRect(hWnd, NULL, FALSE); break;
            }
            if (x >= pr.popX + 20.0f && x <= pr.popX + pr.popW - 20.0f && y >= pr.popY + 128.0f && y <= pr.popY + 188.0f) {
                feedbackFocusField = 2; InvalidateRect(hWnd, NULL, FALSE); break;
            }
            float sbW = 110.0f, sbH = 32.0f, sbX = pr.popX + pr.popW - 20.0f - sbW, sbY = pr.popY + pr.popH - 16.0f - sbH;
            if (x >= sbX && x <= sbX + sbW && y >= sbY && y <= sbY + sbH) {
                wstring emailW(feedbackEmail), msgW(feedbackMessage);
                if (!emailW.empty() && !msgW.empty()) {
                    SubmitFeedbackToFirebase(emailW, msgW);
                    showFeedbackBox = false;
                    ZeroMemory(feedbackEmail,   sizeof(feedbackEmail));
                    ZeroMemory(feedbackMessage, sizeof(feedbackMessage));
                    MessageBoxA(hWnd, "Feedback submitted! Thank you.", "RasFocus+", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxA(hWnd, "Please fill in both email and message.", "RasFocus+", MB_OK | MB_ICONWARNING);
                }
                InvalidateRect(hWnd, NULL, FALSE); break;
            }
            break;
        }

        if (isUpdateReady) {
            float btnW = 42.0f, upgW = 150.0f, upgX = scaledW - (btnW*3) - upgW - 10.0f;
            if (x >= upgX && x <= upgX + upgW && y >= 0.0f && y <= (float)TITLEBAR_HEIGHT) {
                ApplySilentUpdate(); return 0;
            }
        }

        if (hoverMinimize) ShowWindow(hWnd, SW_MINIMIZE);
        if (hoverMaximize) { if (isMaximized) ShowWindow(hWnd, SW_RESTORE); else ShowWindow(hWnd, SW_MAXIMIZE); }
        if (hoverClose)    ShowWindow(hWnd, SW_HIDE);

        if (HitFeedbackIcon(x, y, scaledW)) {
            showFeedbackBox = true; feedbackFocusField = 1;
            InvalidateRect(hWnd, NULL, FALSE); break;
        }

        if (HitMyAccount(x, y, scaledW)) {
            selectedTab = 7;
            HideAllWebViews();
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        int prevTab = selectedTab;
        float sideY = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
        float tabsStartY = sideY + 20.0f;
        float tabH  = 50.0f;
        if (x >= 0.0f && x <= SIDEBAR_WIDTH && y >= tabsStartY) {
            int idx = (int)((y - tabsStartY) / tabH);
            if (idx >= 0 && idx < (int)sidebarTabs.size()) {
                int logicalTab = (idx == 6) ? 8 : idx; // ← 7th item (index 6) mapped to selectedTab = 8
                if (selectedTab != logicalTab) { selectedTab = logicalTab; HideAllWebViews(); }
            }
        }

        // ← Upgrade Now বাটনে ক্লিক
        if ((g_currentPackage == "FREE_BASIC" || g_currentPackage == "TRIAL") && hoverUpgrade) {
            g_showUpgradePopup = true;
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        if (prevTab != selectedTab) {
            HideAllWebViews();
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        if      (selectedTab == 0) { ProcessDashboardMouseClick(x, y, selectedTab); }
        else if (selectedTab == 1) { ProcessBlocksMouseClick(x, y); }
        else if (selectedTab == 2) { ProcessDeepStudyMouseClick(x, y); }
        else if (selectedTab == 3) { ProcessSpecialFeatureMouseClick(x, y); }
        else if (selectedTab == 4) {
            float cX = (float)SIDEBAR_WIDTH, cY = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
            float cW = scaledW - cX, cH = scaledH - cY;
            ProcessStatisticsMouseClick(x, y, cX, cY, cW);
        }
        else if (selectedTab == 5) { ProcessSettingsMouseClick(x, y); }
        else if (selectedTab == 7) {
            ProcessAccountsMouseClick(x, y, hWnd);
        }
        else if (selectedTab == 8) { // ← Family Link Mouse Click Handled
            float cX = (float)SIDEBAR_WIDTH, cY = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
            ProcessFamilyLinkMouseClick(x, y, cX, cY, hWnd);
        }
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }

    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hWnd, &pt);
        float x = pt.x / g_scaleFactor;
        float y = pt.y / g_scaleFactor;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (selectedTab == 1) { extern void ProcessBlocksMouseWheel(float,float,int); ProcessBlocksMouseWheel(x,y,delta); InvalidateRect(hWnd,NULL,FALSE); }
        break;
    }

    case WM_TRAYICON: {
        if (lp == WM_LBUTTONUP) {
            if (IsWindowVisible(hWnd) && !IsIconic(hWnd)) { ShowWindow(hWnd, SW_HIDE); }
            else { ShowWindow(hWnd, SW_SHOW); ShowWindow(hWnd, SW_RESTORE); SetForegroundWindow(hWnd); }
        }
        break;
    }

    case WM_CHAR: {
        if (selectedTab == 7) {
            extern void ProcessAccountsChar(wchar_t);
            ProcessAccountsChar((wchar_t)wp);
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }
        if (showFeedbackBox) {
            wchar_t c = (wchar_t)wp;
            if (c == L'\b') {
                if (feedbackFocusField == 1) { int len = (int)wcslen(feedbackEmail); if (len > 0) feedbackEmail[len - 1] = L'\0'; }
                else if (feedbackFocusField == 2) { int len = (int)wcslen(feedbackMessage); if (len > 0) feedbackMessage[len - 1] = L'\0'; }
            } else if (c >= L' ' || c == L'\t') {
                if (feedbackFocusField == 1) { int len = (int)wcslen(feedbackEmail); if (len < 254) { feedbackEmail[len] = c; feedbackEmail[len+1] = L'\0'; } }
                else if (feedbackFocusField == 2) { int len = (int)wcslen(feedbackMessage); if (len < 1022) { feedbackMessage[len] = c; feedbackMessage[len+1] = L'\0'; } }
            }
            InvalidateRect(hWnd, NULL, FALSE); break;
        }
        if (selectedTab == 1) { extern void ProcessBlocksKeyPress(wchar_t); ProcessBlocksKeyPress((wchar_t)wp); InvalidateRect(hWnd,NULL,FALSE); }
        else if (selectedTab == 2) { ProcessDeepStudyKeyPress((wchar_t)wp); InvalidateRect(hWnd,NULL,FALSE); }
        else if (selectedTab == 8) { ProcessFamilyLinkChar((wchar_t)wp); InvalidateRect(hWnd, NULL, FALSE); } // ← Family Link Char Input Handled
        break;
    }

    case WM_KEYDOWN: {
        if (selectedTab == 7) {
            extern void ProcessAccountsKeyDown(WPARAM);
            ProcessAccountsKeyDown(wp);
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }
        if (showFeedbackBox) {
            if (wp == VK_ESCAPE) { showFeedbackBox = false; InvalidateRect(hWnd, NULL, FALSE); }
            else if (wp == VK_TAB) { feedbackFocusField = (feedbackFocusField == 1) ? 2 : 1; InvalidateRect(hWnd, NULL, FALSE); }
            break;
        }
        if (selectedTab == 1) { extern void ProcessBlocksKeyDown(WPARAM); ProcessBlocksKeyDown(wp); InvalidateRect(hWnd,NULL,FALSE); }
        else if (selectedTab == 2) { ProcessDeepStudyKeyDown(wp); InvalidateRect(hWnd,NULL,FALSE); }
        else if (selectedTab == 8) { ProcessFamilyLinkKeyDown(wp); InvalidateRect(hWnd, NULL, FALSE); } // ← Family Link KeyDown Handled
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        OnPaint(hWnd, hdc);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_DESTROY:
        extern void SaveDeepStudySettings();
        SaveDeepStudySettings();
        RemoveTrayIcon();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, msg, wp, lp);
    }
    return 0;
}

// ==========================================
// WINMAIN
// ==========================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    _beginthread(FirebaseKillThread, 0, NULL);
    _beginthread(SubscriptionCheckThread, 0, NULL); // ← NEW SUBSCRIPTION THREAD

    firebase::AppOptions options;
    options.set_project_id   ("rasfocus-c746d");
    options.set_app_id       ("1:868329616276:web:2f1954de893f5d3f231581");
    options.set_api_key      ("AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA");
    options.set_storage_bucket("rasfocus-c746d.firebasestorage.app");

    g_firebaseApp = firebase::App::Create(options);
    if (g_firebaseApp) OutputDebugStringW(L"[RasFocus] Firebase Initialized!\n");
    else               OutputDebugStringW(L"[RasFocus] Firebase Init Failed!\n");

    InitAccountsModule(g_firebaseApp);

    int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_isPureViewerMode = false; wstring viewerUrl = L"", viewerTitle = L"";

    if (argv && argc > 1) {
        for (int i = 1; i < argc; ++i) {
            wstring arg = argv[i], argLower = arg;
            for (auto& k : argLower) k = towlower(k);
            if (argLower == L"-minibrowser") { g_isPureViewerMode = true; viewerUrl = L"https://www.google.com"; viewerTitle = L"RasFocus+ Mini Browser"; break; }
            else if (argLower.length() > 4 && argLower.substr(argLower.length()-4) == L".pdf") { g_isPureViewerMode = true; viewerUrl = arg; viewerTitle = L"RasFocus+ PDF Viewer"; break; }
            else if (argLower.length() > 4 && (argLower.substr(argLower.length()-4) == L".jpg" || argLower.substr(argLower.length()-4) == L".png" || argLower.substr(argLower.length()-5) == L".jpeg")) { g_isPureViewerMode = true; viewerUrl = arg; viewerTitle = L"RasFocus+ Photo Viewer"; break; }
            else if (argLower.find(L"http://") == 0 || argLower.find(L"https://") == 0) { g_isPureViewerMode = true; viewerUrl = arg; viewerTitle = L"RasFocus+ Web Viewer"; break; }
        }
    }
    if (argv) LocalFree(argv);

    HANDLE hMutex = NULL;
    if (!g_isPureViewerMode) {
        hMutex = CreateMutexA(NULL, FALSE, "RasFocusPro_SingleInstance_Mutex");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND hExistingWnd = FindWindowA("RasFocusCore", "RasFocus+");
            if (hExistingWnd) { ShowWindow(hExistingWnd, SW_RESTORE); ShowWindow(hExistingWnd, SW_SHOW); SetForegroundWindow(hExistingWnd); }
            CloseHandle(hMutex);
            return 0;
        }
    }

    extern void CheckFirstRun(); CheckFirstRun();
    CheckDailyMessage();
    SetupAutoRun();
    SetupDefaultViewer();
    CreateDesktopShortcut();
    ExtractAndRunObserver();
    extern void LoadDeepStudySettings(); LoadDeepStudySettings();
    extern void LoadStrictSettings();    LoadStrictSettings();

    SetProcessDPIAware();
    HDC screenDC = GetDC(NULL);
    g_scaleFactor = GetDeviceCaps(screenDC, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, screenDC);

    GdiplusStartupInput gsi;
    GdiplusStartup(&gdiplusToken, &gsi, NULL);

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "RasFocusCore";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    RegisterClass(&wc);

    int sw = (int)(windowWidth  * g_scaleFactor);
    int sh = (int)(windowHeight * g_scaleFactor);

    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    HWND hWnd = CreateWindowEx(
        WS_EX_APPWINDOW, "RasFocusCore", "RasFocus+",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, sw, sh, NULL, NULL, hInst, NULL
    );
    hParentWnd = hWnd;

    HICON hAppIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_APP_ICON));
    if (hAppIcon) {
        SendMessage(hWnd, WM_SETICON, ICON_BIG,   (LPARAM)hAppIcon);
        SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);
    }

    AddTrayIcon(hWnd);

    string cmdLine(lpCmdLine);
    if (g_isPureViewerMode) {
        if (viewerUrl.find(L".pdf") != wstring::npos) {
            selectedTab = 6;
            currentWorkspacePdf = viewerUrl;
            ShowWindow(hWnd, SW_SHOWMAXIMIZED);
            SetForegroundWindow(hWnd);
        } else {
            ShowWindow(hWnd, SW_HIDE);
            LaunchMiniBrowser(viewerUrl, viewerTitle);
        }
    } else if (cmdLine.find("-silent") != string::npos) {
        ShowWindow(hWnd, SW_HIDE);
        int response = MessageBoxA(NULL, "Start your day with high productivity",
            "RasFocus+", MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
        if (response == IDYES) { ShowWindow(hWnd, SW_SHOWMAXIMIZED); SetForegroundWindow(hWnd); }
    } else {
        ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    }

    UpdateWindow(hWnd);
    StartSilentUpdateCheck();
    SetTimer(hWnd, 1005, 300000, NULL);
    SetTimer(hWnd, 1001,   1000, NULL); // Family Link: 1-second enforcement + poll tick

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    if (hMutex) CloseHandle(hMutex);
    return 0;
}
