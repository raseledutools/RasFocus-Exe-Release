// main.cpp

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>

HWND hParentWnd = NULL; // গ্লোবাল উইন্ডো হ্যান্ডেল

#include <shellapi.h> 
#include "tab_pdf_workspace.h"
#include <shlobj.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <process.h> 
#include <wininet.h>

#pragma comment(lib, "wininet.lib") // Firebase REST API কল করার জন্য

// 🟢 1. ফায়ারবেস হেডার ফাইল যুক্ত করা হলো
#include "firebase/app.h"

// --- Custom Includes ---
#include "browser/mini_browser.h"
#include "tab_blocks.h"
#include "tab_adult.h" 
#include "tab_settings.h" 
#include "tab_deep_study.h"
#include "tab_utilities.h" 
#include "tab_dashboard.h" 
#include "tab_special.h"    
#include "tab_statistics.h" 
#include "prewindow.h"

// 🟢 2. Accounts ফাইলের লিংক যুক্ত করা হলো
#include "accounts.h"

using namespace Gdiplus;
using namespace std;

// --- Custom Messages & Resource ID ---
#define WM_TRAYICON (WM_USER + 1) 
#define IDI_APP_ICON 101 
#define IDR_OBSERVER_EXE 102 

// --- AUTO UPDATE CONFIGURATION ---
const string CURRENT_VERSION = "v1.0.6"; 
const string GITHUB_USER = "raseledutools";    
const string GITHUB_REPO = "RasFocus-update";    

bool isUpdateReady = false;      
bool isCheckingUpdate = false;   
string newVersionStr = "";
bool hoverUpdateBtn = false; 

// --- Global Variables & DPI Scaling ---
ULONG_PTR gdiplusToken;
float g_scaleFactor = 1.0f;
int windowWidth = 1024;  
int windowHeight = 600;  
bool isMaximized = true; 

// 🟢 Firebase Global App Object
firebase::App* g_firebaseApp = nullptr;

bool g_isPureViewerMode = false; 
wstring currentWorkspacePdf = L""; 

NOTIFYICONDATA nid = {}; 

// --- Layout Dimensions ---
extern const int SIDEBAR_WIDTH = 100;   // ✅ CCleaner style: narrower sidebar (icon+text stacked)
extern const int TITLEBAR_HEIGHT = 45;  
extern const int SUBHEADER_HEIGHT = 65; 

// --- UI State ---
int selectedTab = 0; 
int hoveredTab = -1;
bool hoverMinimize = false, hoverMaximize = false, hoverClose = false;
bool hoverUpgrade = false;

vector<wstring> sidebarTabs = {
    L"Dashboard", L"Blocks", L"Adult Block", L"Deep Study", L"Special Feature", L"Statistics", L"Settings", L"Accounts"
};
vector<wstring> sidebarIcons = {
    L"\xE80F", L"\xEA18", L"\xE72E", L"\xE7B3", L"\xE734", L"\xE9D2", L"\xE713", L"\xE77B"
};

// Colors
const Color ColTeal(255, 12, 168, 176);          
const Color ColTealHover(255, 30, 185, 195);    
const Color ColTealDark(255, 8, 140, 148);       // ✅ Active tab darker teal accent
const Color ColDarkBar(255, 43, 43, 43);         // ✅ CCleaner dark titlebar color
const Color ColDarkBarHover(255, 60, 60, 60);    // ✅ Titlebar button hover
const Color ColWhite(255, 255, 255, 255);
const Color ColBgContent(255, 248, 250, 252);   
const Color ColTextDark(255, 50, 50, 50);
const Color ColTextGray(255, 120, 120, 120);
const Color ColUpgradeBtn(255, 243, 156, 18);
const Color ColUpgradeHover(255, 211, 84, 0);

// ==========================================
// 🔴 FIX #1: Global variables that tab_ai.cpp needs
// ==========================================
bool isSafeBrowsingActive = false;
bool isStrictActive = false;

// ==========================================
// 🔴 FIX #2: Forward declaration for RequestParentalAccess
// ==========================================
bool RequestParentalAccess(HWND hwnd);

// ==========================================
// 🔴 SMART FIREBASE REAL-TIME KILL SWITCH (REVIVABLE)
// ==========================================
bool g_isAppDisabledByAdmin = false;

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

            bool isDisabled = (content.find("\"is_active\":false") != string::npos || content.find("\"is_active\": false") != string::npos);
            
            if (isDisabled && !g_isAppDisabledByAdmin) {
                g_isAppDisabledByAdmin = true;
                if (hParentWnd) ShowWindow(hParentWnd, SW_HIDE);
                MessageBoxA(NULL, "This application has been disabled by the server administrator.", "RasFocus Pro - Access Denied", MB_OK | MB_ICONERROR | MB_TOPMOST);
            } 
            else if (!isDisabled && g_isAppDisabledByAdmin) {
                g_isAppDisabledByAdmin = false;
                if (hParentWnd) {
                    ShowWindow(hParentWnd, SW_SHOWMAXIMIZED);
                    SetForegroundWindow(hParentWnd);
                }
                MessageBoxA(NULL, "Application access has been restored by admin.", "RasFocus Pro", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
            }
        }
        Sleep(5000); 
    }
    _endthread();
}

// ==========================================
// 🔴 MAGIC FIX: TAB CHANGE LOGIC FOR WEBVIEW2
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
// UTILITY FUNCTIONS
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

// ==========================================
// 🟢 DEFAULT VIEWER REGISTRY SETUP 
// ==========================================
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
    RegisterFileAssociation(".pdf", "RasFocus.PDF", "RasFocus PDF Document");
    RegisterFileAssociation(".jpg", "RasFocus.Image", "RasFocus Image File");
    RegisterFileAssociation(".png", "RasFocus.Image", "RasFocus Image File");
    RegisterFileAssociation(".jpeg", "RasFocus.Image", "RasFocus Image File");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL); 
}

// ==========================================
// 100% SILENT BACKGROUND UPDATER
// ==========================================
void __cdecl SilentUpdateThread(void* p) {
    if (isCheckingUpdate || isUpdateReady) {
        _endthread();
        return;
    }
    isCheckingUpdate = true;

    string secretDir = GetSecretDir(); 
    string apiFile = secretDir + "api_response.json";
    
    string apiUrl = "https://api.github.com/repos/" + GITHUB_USER + "/" + GITHUB_REPO + "/releases/latest";
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
                        string exeUrl = "https://github.com/" + GITHUB_USER + "/" + GITHUB_REPO + "/releases/download/" + latestVer + "/RasFocus.exe";
                        string updateExePath = secretDir + "RasFocus_New.exe";
                        
                        HRESULT hrExe = URLDownloadToFileA(NULL, exeUrl.c_str(), updateExePath.c_str(), 0, NULL);
                        if (hrExe == S_OK) {
                            isUpdateReady = true; 
                            HWND hWnd = FindWindowA("RasFocusCore", "RasFocus Pro");
                            if(hWnd) {
                                InvalidateRect(hWnd, NULL, FALSE);
                            }
                        }
                    }
                }
            }
        }
    }
    isCheckingUpdate = false;
    _endthread();
}

void StartSilentUpdateCheck() {
    _beginthread(SilentUpdateThread, 0, NULL);
}

void ApplySilentUpdate() {
    string secretDir = GetSecretDir();
    string batPath = secretDir + "updater.bat";
    string newExePath = secretDir + "RasFocus_New.exe";
    string currentExePath = GetExePath(); 

    ofstream batFile(batPath);
    batFile << "@echo off\n"
            << "timeout /t 1 /nobreak >nul\n"
            << "taskkill /F /IM RasObserve.exe /T >nul 2>&1\n" 
            << "taskkill /F /IM RasFocus.exe /T >nul 2>&1\n"
            << "timeout /t 1 /nobreak >nul\n"
            << "copy /Y \"" << newExePath << "\" \"" << currentExePath << "\"\n" 
            << "start \"\" \"" << currentExePath << "\"\n" 
            << "del \"" << newExePath << "\"\n"            
            << "del \"%~f0\"\n";                            
    batFile.close();

    string cmdExec = "cmd.exe /c \"" + batPath + "\"";
    STARTUPINFOA siBat = { sizeof(STARTUPINFOA) };
    siBat.dwFlags = STARTF_USESHOWWINDOW;
    siBat.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION piBat;
    
    CreateProcessA(NULL, (LPSTR)cmdExec.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &siBat, &piBat);
    exit(0);
}

// ==========================================
// SYSTEM LEVEL LOGIC
// ==========================================
bool IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

// 🟢 UAC BYPASS SHORTCUT CREATION & MINI BROWSER SHORTCUT
void CreateDesktopShortcut() {
    char desktopPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
        string mainShortcutPath = string(desktopPath) + "\\RasFocus Pro.lnk";
        string miniBrowserShortcutPath = string(desktopPath) + "\\RasFocus Mini Browser.lnk";
        string exePath = GetExePath();

        CoInitialize(NULL);
        IShellLink* psl;

        // 1. Main App Shortcut (UAC BYPASS VIA TASK SCHEDULER)
        if (GetFileAttributesA(mainShortcutPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl))) {
                IPersistFile* ppf;
                psl->SetPath("C:\\Windows\\System32\\schtasks.exe");
                psl->SetArguments("/run /tn \"RasFocusPro_AutoStart\"");
                psl->SetDescription("RasFocus Pro - Block Apps & Adult Content");
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

        // 2. Mini Browser Shortcut
        if (GetFileAttributesA(miniBrowserShortcutPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl))) {
                IPersistFile* ppf;
                psl->SetPath(exePath.c_str());
                psl->SetArguments("-minibrowser");
                psl->SetDescription("RasFocus Safe Mini Browser");
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

// ==========================================
// 🟢 ROBUST AUTOSTART
// ==========================================
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
        si1.dwFlags = STARTF_USESHOWWINDOW;
        si1.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi1;
        if (CreateProcessW(NULL, (LPWSTR)schCreate.c_str(), NULL, NULL, FALSE,
                           CREATE_NO_WINDOW, NULL, NULL, &si1, &pi1)) {
            WaitForSingleObject(pi1.hProcess, 5000);
            CloseHandle(pi1.hProcess);
            CloseHandle(pi1.hThread);
        }

        wstring schPowerFix =
            L"powershell.exe -WindowStyle Hidden -Command \""
            L"Set-ScheduledTask -TaskName 'RasFocusPro_AutoStart'"
            L" -Settings (New-ScheduledTaskSettingsSet"
            L" -AllowStartIfOnBatteries"
            L" -DontStopIfGoingOnBatteries"
            L" -ExecutionTimeLimit 0)\"";

        STARTUPINFOW si2 = { sizeof(STARTUPINFOW) };
        si2.dwFlags = STARTF_USESHOWWINDOW;
        si2.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi2;
        if (CreateProcessW(NULL, (LPWSTR)schPowerFix.c_str(), NULL, NULL, FALSE,
                           CREATE_NO_WINDOW, NULL, NULL, &si2, &pi2)) {
            WaitForSingleObject(pi2.hProcess, 5000);
            CloseHandle(pi2.hProcess);
            CloseHandle(pi2.hThread);
        }

        wstring schStartup =
            L"powershell.exe -WindowStyle Hidden -Command \""
            L"$task = Get-ScheduledTask -TaskName 'RasFocusPro_AutoStart';"
            L"$trigger = New-ScheduledTaskTrigger -AtStartup;"
            L"$task.Triggers += $trigger;"
            L"Set-ScheduledTask -TaskName 'RasFocusPro_AutoStart' -Trigger $task.Triggers\"";

        STARTUPINFOW si3 = { sizeof(STARTUPINFOW) };
        si3.dwFlags = STARTF_USESHOWWINDOW;
        si3.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi3;
        if (CreateProcessW(NULL, (LPWSTR)schStartup.c_str(), NULL, NULL, FALSE,
                           CREATE_NO_WINDOW, NULL, NULL, &si3, &pi3)) {
            WaitForSingleObject(pi3.hProcess, 5000);
            CloseHandle(pi3.hProcess);
            CloseHandle(pi3.hThread);
        }
    }

    {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            wstring regCmd = L"\"" + pathStr + L"\" -silent";
            RegSetValueExW(hKey, L"RasFocusPro", 0, REG_SZ,
                           (const BYTE*)regCmd.c_str(),
                           (DWORD)((regCmd.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
        }
    }

    if (IsRunAsAdmin()) {
        HKEY hKeyLM;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, KEY_SET_VALUE, &hKeyLM) == ERROR_SUCCESS) {
            wstring regCmd = L"\"" + pathStr + L"\" -silent";
            RegSetValueExW(hKeyLM, L"RasFocusPro", 0, REG_SZ,
                           (const BYTE*)regCmd.c_str(),
                           (DWORD)((regCmd.size() + 1) * sizeof(wchar_t)));
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
    DWORD size = SizeofResource(NULL, hRes);
    
    wstring folderPath = L"C:\\ProgramData\\RasFocus";
    CreateDirectoryW(folderPath.c_str(), NULL);
    wstring destPath = folderPath + L"\\RasObserve.exe";
    
    HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL);
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
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; 
    PROCESS_INFORMATION pi;

    if (CreateProcessW(NULL, cmdBuffer, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, wWorkingDir.c_str(), &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void AddTrayIcon(HWND hWnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1001;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON)); 
    lstrcpy(nid.szTip, "RasFocus Pro is running...");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// ==========================================
// ✅ DRAWING FUNCTIONS — CCleaner Style
// ==========================================

// ✅ DrawTitleBar — Dark charcoal titlebar like CCleaner
void DrawTitleBar(Graphics& g, int w) {
    // Dark background for entire titlebar
    SolidBrush bgDark(ColDarkBar);
    g.FillRectangle(&bgDark, 0.0f, 0.0f, (float)w, (float)TITLEBAR_HEIGHT);

    // App icon
    HICON hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 24, 24, LR_SHARED);
    if (hIcon) {
        float iconSize = 24.0f;
        float iconY = ((float)TITLEBAR_HEIGHT - iconSize) / 2.0f;
        HDC hdcG = g.GetHDC();
        DrawIconEx(hdcG, 12, (int)iconY, hIcon, 24, 24, 0, NULL, DI_NORMAL);
        g.ReleaseHDC(hdcG);
    }

    // App title text — white on dark
    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff, 13, FontStyleRegular, UnitPixel);
    SolidBrush textWhite(ColWhite);
    SolidBrush textMuted(Color(255, 180, 180, 180));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);

    g.DrawString(L"RasFocus Pro", -1, &fTitle, RectF(44.0f, 0.0f, 300.0f, (float)TITLEBAR_HEIGHT), &fmt, &textWhite);

    // Window control buttons (Minimize / Maximize / Close)
    float btnW = 46.0f;
    float btnH = (float)TITLEBAR_HEIGHT;
    float startX = (float)w - (btnW * 3);

    StringFormat fmtIcon;
    fmtIcon.SetAlignment(StringAlignmentCenter);
    fmtIcon.SetLineAlignment(StringAlignmentCenter);

    // ✅ Update button — shown when update is ready (teal color, matches brand)
    if (isUpdateReady) {
        float upgW = 170.0f;
        float upgH = (float)TITLEBAR_HEIGHT - 10.0f;
        float upgX = startX - upgW - 12.0f;
        float upgY = 5.0f;

        GraphicsPath upgPath;
        float r = 4.0f; float d = r * 2.0f;
        upgPath.AddArc(upgX, upgY, d, d, 180.0f, 90.0f);
        upgPath.AddArc(upgX + upgW - d, upgY, d, d, 270.0f, 90.0f);
        upgPath.AddArc(upgX + upgW - d, upgY + upgH - d, d, d, 0.0f, 90.0f);
        upgPath.AddArc(upgX, upgY + upgH - d, d, d, 90.0f, 90.0f);
        upgPath.CloseFigure();

        // Teal fill for update button — brand consistent
        SolidBrush upgBg(hoverUpdateBtn ? ColTealHover : ColTeal);
        g.FillPath(&upgBg, &upgPath);

        Font fUpg(&ff, 12, FontStyleBold, UnitPixel);
        wstring wVer(newVersionStr.begin(), newVersionStr.end());
        wstring finalBtnText = L"Update " + wVer + L" Ready";
        g.DrawString(finalBtnText.c_str(), -1, &fUpg, RectF(upgX, upgY, upgW, upgH), &fmtIcon, &textWhite);
    }

    // Hover backgrounds for window controls
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fIcons(&ffIcons, 11, FontStyleRegular, UnitPixel);

    if (hoverMinimize) {
        SolidBrush b(ColDarkBarHover);
        g.FillRectangle(&b, startX, 0.0f, btnW, btnH);
    }
    if (hoverMaximize) {
        SolidBrush b(ColDarkBarHover);
        g.FillRectangle(&b, startX + btnW, 0.0f, btnW, btnH);
    }
    if (hoverClose) {
        // Close button red on hover — standard convention
        SolidBrush b(Color(255, 196, 43, 28));
        g.FillRectangle(&b, startX + (btnW * 2), 0.0f, btnW, btnH);
    }

    // Draw window control icons
    g.DrawString(L"\xE921", -1, &fIcons, RectF(startX, 0.0f, btnW, btnH), &fmtIcon, &textWhite);
    const wchar_t* maxIcon = isMaximized ? L"\xE923" : L"\xE922";
    g.DrawString(maxIcon, -1, &fIcons, RectF(startX + btnW, 0.0f, btnW, btnH), &fmtIcon, &textWhite);
    g.DrawString(L"\xE8BB", -1, &fIcons, RectF(startX + (btnW * 2), 0.0f, btnW, btnH), &fmtIcon, &textWhite);
}

// ✅ DrawSidebar — Teal sidebar, CCleaner style (icon on top, text below, stacked layout)
void DrawSidebar(Graphics& g, int h) {
    // Full teal sidebar background
    SolidBrush bgTeal(ColTeal);
    g.FillRectangle(&bgTeal, 0.0f, (float)TITLEBAR_HEIGHT, (float)SIDEBAR_WIDTH, (float)(h - TITLEBAR_HEIGHT));

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");

    // ✅ Brand logo area at top of sidebar — compact, icon + "RasFocus" text
    float brandAreaH = 70.0f;
    float brandY = (float)TITLEBAR_HEIGHT;

    // Slightly darker teal strip for brand area
    SolidBrush brandBg(ColTealDark);
    g.FillRectangle(&brandBg, 0.0f, brandY, (float)SIDEBAR_WIDTH, brandAreaH);

    // App icon centered in brand area
    HICON hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_SHARED);
    if (hIcon) {
        int iconX = (SIDEBAR_WIDTH - 32) / 2;
        HDC hdcG = g.GetHDC();
        DrawIconEx(hdcG, iconX, (int)brandY + 8, hIcon, 32, 32, 0, NULL, DI_NORMAL);
        g.ReleaseHDC(hdcG);
    }

    // App name below icon
    Font fBrandName(&ff, 10, FontStyleBold, UnitPixel);
    SolidBrush textWhite(ColWhite);
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"RasFocus", -1, &fBrandName, RectF(0.0f, brandY + 42.0f, (float)SIDEBAR_WIDTH, 18.0f), &fmtC, &textWhite);

    // ✅ Tab items — CCleaner style: icon top, label below, stacked in column
    float tabStartY = brandY + brandAreaH + 4.0f;  // Start just after brand area
    float tabH = 62.0f;   // Height per tab item (icon + text)
    float tabW = (float)SIDEBAR_WIDTH;

    Font fTabIcon(&ffIcons, 22, FontStyleRegular, UnitPixel);  // Large icon
    Font fTabLabel(&ff, 9, FontStyleRegular, UnitPixel);        // Small label below

    SolidBrush textLight(Color(255, 220, 245, 248));   // Soft white for inactive
    SolidBrush textTeal(ColTeal);                       // Teal text for active item
    SolidBrush textActiveWhite(ColWhite);               // Bright white for active

    for (size_t i = 0; i < sidebarTabs.size(); ++i) {
        float itemY = tabStartY + (i * tabH);
        RectF tabRect(0.0f, itemY, tabW, tabH);

        if (selectedTab == (int)i) {
            // ✅ Active tab: white background, teal text — exactly like CCleaner's active blue tab
            SolidBrush activeBg(ColWhite);
            g.FillRectangle(&activeBg, tabRect);

            // Left accent line — teal stripe on left edge of active tab
            SolidBrush accentLine(ColTealDark);
            g.FillRectangle(&accentLine, 0.0f, itemY, 3.0f, tabH);

            // Icon — teal color on white bg
            RectF iconRect(0.0f, itemY + 8.0f, tabW, 28.0f);
            g.DrawString(sidebarIcons[i].c_str(), -1, &fTabIcon, iconRect, &fmtC, &textTeal);

            // Label — teal color on white bg
            RectF labelRect(0.0f, itemY + 36.0f, tabW, 20.0f);
            g.DrawString(sidebarTabs[i].c_str(), -1, &fTabLabel, labelRect, &fmtC, &textTeal);
        }
        else {
            // ✅ Hover: slightly lighter teal
            if (hoveredTab == (int)i) {
                SolidBrush hoverBg(ColTealHover);
                g.FillRectangle(&hoverBg, tabRect);
            }

            // Icon — soft white
            RectF iconRect(0.0f, itemY + 8.0f, tabW, 28.0f);
            g.DrawString(sidebarIcons[i].c_str(), -1, &fTabIcon, iconRect, &fmtC, &textLight);

            // Label — soft white
            RectF labelRect(0.0f, itemY + 36.0f, tabW, 20.0f);
            g.DrawString(sidebarTabs[i].c_str(), -1, &fTabLabel, labelRect, &fmtC, &textLight);
        }
    }

    // ✅ Upgrade to Pro button at bottom — outlined style like CCleaner "Learn More" button
    float upgY = (float)h - 58.0f;
    float upgX = 8.0f;
    float upgW = (float)SIDEBAR_WIDTH - 16.0f;
    float upgH = 42.0f;

    RectF upgRect(upgX, upgY, upgW, upgH);
    GraphicsPath upgPath;
    float r = 6.0f; float d = r * 2.0f;
    upgPath.AddArc(upgX, upgY, d, d, 180.0f, 90.0f);
    upgPath.AddArc(upgX + upgW - d, upgY, d, d, 270.0f, 90.0f);
    upgPath.AddArc(upgX + upgW - d, upgY + upgH - d, d, d, 0.0f, 90.0f);
    upgPath.AddArc(upgX, upgY + upgH - d, d, d, 90.0f, 90.0f);
    upgPath.CloseFigure();

    if (hoverUpgrade) {
        // Filled on hover
        SolidBrush upgFill(Color(255, 243, 156, 18));
        g.FillPath(&upgFill, &upgPath);
    } else {
        // Outlined when not hovered — white border on teal bg
        Pen upgBorder(ColWhite, 1.5f);
        g.DrawPath(&upgBorder, &upgPath);
    }

    Font fUpgLabel(&ff, 9, FontStyleBold, UnitPixel);
    g.DrawString(L"Upgrade to Pro", -1, &fUpgLabel, upgRect, &fmtC, &textWhite);
}

void DrawMainArea(Graphics& g, int w, int h) {
    float contentX = (float)SIDEBAR_WIDTH;
    float contentY = (float)TITLEBAR_HEIGHT;
    float contentW = (float)(w - SIDEBAR_WIDTH);
    float contentH = (float)(h - TITLEBAR_HEIGHT);

    if (selectedTab == 0) { DrawDashboardTab(g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 1) { DrawBlocksTab(g, contentX, contentY, contentW, contentH); } 
    else if (selectedTab == 2) { DrawAdultBlockTab(g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 3) { DrawDeepStudyTab(g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 4) { DrawSpecialFeatureTab(g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 5) { DrawStatisticsTab(g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 6) { DrawSettingsTab(g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 7) { DrawAccountsTab(g, contentX, contentY, contentW, contentH); }
    else if (selectedTab == 8) { DrawPdfWorkspaceTab(g, contentX, contentY, contentW, contentH); }
}

void OnPaint(HWND hWnd, HDC hdc) {
    RECT r; GetClientRect(hWnd, &r);
    int w = r.right - r.left;
    int h = r.bottom - r.top;

    HDC mdc = CreateCompatibleDC(hdc);
    HBITMAP mbmp = CreateCompatibleBitmap(hdc, w, h);
    SelectObject(mdc, mbmp);

    Graphics g(mdc);
    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    g.ScaleTransform(g_scaleFactor, g_scaleFactor);
    int scaledW = (int)(w / g_scaleFactor);
    int scaledH = (int)(h / g_scaleFactor);

    DrawMainArea(g, scaledW, scaledH);
    DrawSidebar(g, scaledH);
    DrawTitleBar(g, scaledW);

    if (showDailyMessage || onboardingStep > 0) {
        DrawPreWindowOverlay(g, scaledW, scaledH, g_scaleFactor);
    }

    BitBlt(hdc, 0, 0, w, h, mdc, 0, 0, SRCCOPY);
    DeleteObject(mbmp);
    DeleteDC(mdc);
}

// --- Window Procedure ---
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER: {
        if (wp == 1005) { StartSilentUpdateCheck(); }
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

        if (pt.y < border && pt.x < border) return HTTOPLEFT;
        if (pt.y < border && pt.x >= r.right - border) return HTTOPRIGHT;
        if (pt.y >= r.bottom - border && pt.x < border) return HTBOTTOMLEFT;
        if (pt.y >= r.bottom - border && pt.x >= r.right - border) return HTBOTTOMRIGHT;
        if (pt.y < border) return HTTOP;
        if (pt.y >= r.bottom - border) return HTBOTTOM;
        if (pt.x < border) return HTLEFT;
        if (pt.x >= r.right - border) return HTRIGHT;
        
        if (pt.y < TITLEBAR_HEIGHT * g_scaleFactor) {
            float x = pt.x / g_scaleFactor;
            float scaledW = (r.right - r.left) / g_scaleFactor;
            float btnW = 46.0f;
            float upgW = 170.0f;
            float controlsStartX = scaledW - (btnW * 3);
            
            if (x >= controlsStartX) return HTCLIENT; 
            
            if (isUpdateReady) {
                float upgX = controlsStartX - upgW - 12.0f;
                if (x >= upgX && x <= upgX + upgW) return HTCLIENT;
            }
            
            return HTCAPTION; 
        }
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        LPMINMAXINFO lpMMI = (LPMINMAXINFO)lp;
        lpMMI->ptMinTrackSize.x = (LONG)(1024 * g_scaleFactor); 
        lpMMI->ptMinTrackSize.y = (LONG)(600 * g_scaleFactor);  
        
        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfo(hMonitor, &mi)) {
            lpMMI->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            lpMMI->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            lpMMI->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            // 🟢 3. Taskbar Fix
            lpMMI->ptMaxSize.y = (mi.rcWork.bottom - mi.rcWork.top) - 2; 
        }
        return 0;
    }
    case WM_SIZE: {
        if (wp == SIZE_MAXIMIZED) isMaximized = true;
        else if (wp == SIZE_RESTORED) isMaximized = false;
        
        RECT r; GetClientRect(hWnd, &r);
        windowWidth = r.right - r.left;
        windowHeight = r.bottom - r.top;
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }
    
    case WM_CLOSE: { ShowWindow(hWnd, SW_HIDE); return 0; }
    case WM_SYSCOMMAND: {
        if ((wp & 0xFFF0) == SC_CLOSE) { ShowWindow(hWnd, SW_HIDE); return 0; }
        return DefWindowProc(hWnd, msg, wp, lp);
    }

    case WM_MOUSEMOVE: {
        float x = GET_X_LPARAM(lp) / g_scaleFactor;
        float y = GET_Y_LPARAM(lp) / g_scaleFactor;
        float scaledW = windowWidth / g_scaleFactor; 
        float scaledH = windowHeight / g_scaleFactor; 
        
        if (showDailyMessage || onboardingStep > 0) {
            if (HandlePreWindowMouseMove(x, y, scaledW, scaledH)) {
                InvalidateRect(hWnd, NULL, FALSE);
                break;
            }
        }
        
        bool redraw = false;

        float btnW = 46.0f;
        bool oldMin = hoverMinimize, oldMax = hoverMaximize, oldClose = hoverClose;
        hoverMinimize = (y <= TITLEBAR_HEIGHT && x >= scaledW - (btnW * 3) && x < scaledW - (btnW * 2));
        hoverMaximize = (y <= TITLEBAR_HEIGHT && x >= scaledW - (btnW * 2) && x < scaledW - btnW);
        hoverClose    = (y <= TITLEBAR_HEIGHT && x >= scaledW - btnW);
        if (oldMin != hoverMinimize || oldMax != hoverMaximize || oldClose != hoverClose) redraw = true;

        bool oldUpgBtn = hoverUpdateBtn;
        hoverUpdateBtn = false;
        if (isUpdateReady) {
            float upgW = 170.0f;
            float upgX = scaledW - (btnW * 3) - upgW - 12.0f;
            if (x >= upgX && x <= upgX + upgW && y >= 0.0f && y <= (float)TITLEBAR_HEIGHT) {
                hoverUpdateBtn = true;
            }
        }
        if (oldUpgBtn != hoverUpdateBtn) redraw = true;

        // ✅ Sidebar hover — updated for new CCleaner tab layout (62px per tab, starts after 70px brand area)
        int oldTab = hoveredTab;
        hoveredTab = -1;
        if (x >= 0.0f && x <= SIDEBAR_WIDTH) {
            float tabStartY = (float)TITLEBAR_HEIGHT + 70.0f + 4.0f;
            float tabH = 62.0f;
            for (size_t i = 0; i < sidebarTabs.size(); ++i) {
                float itemY = tabStartY + (i * tabH);
                if (y >= itemY && y <= itemY + tabH) { hoveredTab = (int)i; break; }
            }
        }
        if (oldTab != hoveredTab) redraw = true;

        // ✅ Upgrade button hover — updated Y position
        bool oldUpg = hoverUpgrade;
        hoverUpgrade = (x >= 8.0f && x <= SIDEBAR_WIDTH - 8.0f && y >= scaledH - 58.0f && y <= scaledH - 16.0f);
        if (oldUpg != hoverUpgrade) redraw = true;

        // 🔴 FIX #3: ProcessStatisticsMouseMove
        if (selectedTab == 0) { ProcessDashboardMouseMove(x, y); redraw = true; }
        else if (selectedTab == 1) { ProcessBlocksMouseMove(x, y); redraw = true; }
        else if (selectedTab == 2) { ProcessAdultMouseMove(x, y); redraw = true; }
        else if (selectedTab == 3) { ProcessDeepStudyMouseMove(x, y); redraw = true; } 
        else if (selectedTab == 4) { ProcessSpecialFeatureMouseMove(x, y); redraw = true; } 
        else if (selectedTab == 5) {
            float contentX = (float)SIDEBAR_WIDTH;
            float contentY = (float)TITLEBAR_HEIGHT;
            float contentW = (float)(windowWidth / g_scaleFactor) - SIDEBAR_WIDTH;
            float contentH = (float)(windowHeight / g_scaleFactor) - TITLEBAR_HEIGHT;
            ProcessStatisticsMouseMove(x, y, contentX, contentY, contentW);
            redraw = true;
        }
        else if (selectedTab == 6) { ProcessSettingsMouseMove(x, y); redraw = true; }
        else if (selectedTab == 7) { ProcessAccountsMouseMove(x, y); redraw = true; }
        else if (selectedTab == 8) { ProcessPdfWorkspaceMouseMove(x, y); redraw = true; }

        if (redraw) InvalidateRect(hWnd, NULL, FALSE);
        break;
    }
    case WM_LBUTTONDOWN: {
        float x = GET_X_LPARAM(lp) / g_scaleFactor;
        float y = GET_Y_LPARAM(lp) / g_scaleFactor;
        float scaledW = windowWidth / g_scaleFactor; 
        float scaledH = windowHeight / g_scaleFactor;

        if (showDailyMessage || onboardingStep > 0) {
            if (HandlePreWindowClick(x, y, selectedTab)) {
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break; 
        }

        if (isUpdateReady) {
            float btnW = 46.0f;
            float upgW = 170.0f;
            float upgX = scaledW - (btnW * 3) - upgW - 12.0f;
            if (x >= upgX && x <= upgX + upgW && y >= 0.0f && y <= (float)TITLEBAR_HEIGHT) {
                ApplySilentUpdate();
                return 0;
            }
        }

        if (hoverMinimize) { ShowWindow(hWnd, SW_MINIMIZE); }
        if (hoverMaximize) {
            if (isMaximized) ShowWindow(hWnd, SW_RESTORE);
            else ShowWindow(hWnd, SW_MAXIMIZE);
        }
        if (hoverClose) { ShowWindow(hWnd, SW_HIDE); } 

        // ✅ Tab click — updated for CCleaner layout
        if (x >= 0.0f && x <= SIDEBAR_WIDTH) {
            float tabStartY = (float)TITLEBAR_HEIGHT + 70.0f + 4.0f;
            float tabH = 62.0f;
            for (size_t i = 0; i < sidebarTabs.size(); ++i) {
                float itemY = tabStartY + (i * tabH);
                if (y >= itemY && y <= itemY + tabH) {
                    if (selectedTab != (int)i) {
                        selectedTab = (int)i;
                        HideAllWebViews();
                    }
                    InvalidateRect(hWnd, NULL, FALSE);
                    break;
                }
            }
        }
        
        if (hoverUpgrade) {
            MessageBox(hWnd, "Upgrade to Pro dialog will open here.", "Activate Pro", MB_OK | MB_ICONINFORMATION);
        }

        int prevTab = selectedTab;

        // 🔴 FIX #4: ProcessStatisticsMouseClick
        if (selectedTab == 0) { ProcessDashboardMouseClick(x, y, selectedTab); }
        else if (selectedTab == 1) { ProcessBlocksMouseClick(x, y); }
        else if (selectedTab == 2) { ProcessAdultMouseClick(x, y); }
        else if (selectedTab == 3) { ProcessDeepStudyMouseClick(x, y); } 
        else if (selectedTab == 4) { ProcessSpecialFeatureMouseClick(x, y); } 
        else if (selectedTab == 5) {
            float contentX = (float)SIDEBAR_WIDTH;
            float contentY = (float)TITLEBAR_HEIGHT;
            float contentW = (float)(windowWidth / g_scaleFactor) - SIDEBAR_WIDTH;
            float contentH = (float)(windowHeight / g_scaleFactor) - TITLEBAR_HEIGHT;
            ProcessStatisticsMouseClick(x, y, contentX, contentY, contentW);
        }
        else if (selectedTab == 6) { ProcessSettingsMouseClick(x, y); }
        else if (selectedTab == 7) { ProcessAccountsMouseClick(x, y); }
        else if (selectedTab == 8) { ProcessPdfWorkspaceMouseClick(x, y); }

        if (prevTab != selectedTab) {
            HideAllWebViews();
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

        if (selectedTab == 1) {
            extern void ProcessBlocksMouseWheel(float x, float y, int delta);
            ProcessBlocksMouseWheel(x, y, delta);
            InvalidateRect(hWnd, NULL, FALSE);
        } else if (selectedTab == 2) {
            extern void ProcessAdultMouseWheel(float x, float y, int delta);
            ProcessAdultMouseWheel(x, y, delta);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_TRAYICON: {
        if (lp == WM_LBUTTONUP) {
            if (IsWindowVisible(hWnd) && !IsIconic(hWnd)) {
                ShowWindow(hWnd, SW_HIDE);
            } else {
                ShowWindow(hWnd, SW_SHOW);
                ShowWindow(hWnd, SW_RESTORE);
                SetForegroundWindow(hWnd);
            }
        }
        break;
    }

    case WM_CHAR: {
        if (selectedTab == 1) {
            extern void ProcessBlocksKeyPress(wchar_t c); 
            ProcessBlocksKeyPress((wchar_t)wp);
            InvalidateRect(hWnd, NULL, FALSE);
        } else if (selectedTab == 2) {
            extern void ProcessAdultKeyPress(wchar_t c); 
            ProcessAdultKeyPress((wchar_t)wp);
            InvalidateRect(hWnd, NULL, FALSE);
        } else if (selectedTab == 3) { 
            ProcessDeepStudyKeyPress((wchar_t)wp);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }
    case WM_KEYDOWN: {
        if (selectedTab == 1) {
            extern void ProcessBlocksKeyDown(WPARAM key);
            ProcessBlocksKeyDown(wp);
            InvalidateRect(hWnd, NULL, FALSE);
        } else if (selectedTab == 2) {
            extern void ProcessAdultKeyDown(WPARAM key);
            ProcessAdultKeyDown(wp);
            InvalidateRect(hWnd, NULL, FALSE);
        } else if (selectedTab == 3) { 
            ProcessDeepStudyKeyDown(wp);
            InvalidateRect(hWnd, NULL, FALSE);
        }
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

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    // 🟢 1. ব্যাকগ্রাউন্ডে রিয়েলটাইম কিল-সুইচ থ্রেড চালু করা হলো
    _beginthread(FirebaseKillThread, 0, NULL);

    firebase::AppOptions options;
    options.set_project_id("rasfocus-c746d");
    options.set_app_id("1:868329616276:web:2f1954de893f5d3f231581");
    options.set_api_key("AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA");
    options.set_storage_bucket("rasfocus-c746d.firebasestorage.app");

    g_firebaseApp = firebase::App::Create(options);
    if (g_firebaseApp) {
        OutputDebugStringW(L"[RasFocus] Firebase Initialized Successfully!\n");
    } else {
        OutputDebugStringW(L"[RasFocus] Failed to Initialize Firebase!\n");
    }

    // =======================================================
    // 🔍 ARGUMENT PARSING (File, Link & MiniBrowser Detection)
    // =======================================================
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_isPureViewerMode = false;
    std::wstring viewerUrl = L"";
    std::wstring viewerTitle = L"";

    if (argv && argc > 1) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            std::wstring argLower = arg;
            for (size_t k = 0; k < argLower.length(); ++k) argLower[k] = towlower(argLower[k]);

            if (argLower == L"-minibrowser") {
                g_isPureViewerMode = true; 
                viewerUrl = L"https://www.google.com";
                viewerTitle = L"RasFocus Mini Browser"; 
                break;
            } else if (argLower.length() > 4 && argLower.substr(argLower.length() - 4) == L".pdf") {
                g_isPureViewerMode = true; viewerUrl = arg; viewerTitle = L"RasFocus PDF Viewer"; break;
            } else if (argLower.length() > 4 && (argLower.substr(argLower.length() - 4) == L".jpg" || argLower.substr(argLower.length() - 4) == L".png" || argLower.substr(argLower.length() - 5) == L".jpeg")) {
                g_isPureViewerMode = true; viewerUrl = arg; viewerTitle = L"RasFocus Photo Viewer"; break;
            } else if (argLower.find(L"http://") == 0 || argLower.find(L"https://") == 0) {
                g_isPureViewerMode = true; viewerUrl = arg; viewerTitle = L"RasFocus Web Viewer"; break;
            }
        }
    }
    if (argv) LocalFree(argv);

    // =======================================================
    // 🚀 MUTEX BYPASS FOR MULTIPLE VIEWER INSTANCES
    // =======================================================
    HANDLE hMutex = NULL;
    if (!g_isPureViewerMode) {
        hMutex = CreateMutexA(NULL, FALSE, "RasFocusPro_SingleInstance_Mutex");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND hExistingWnd = FindWindowA("RasFocusCore", "RasFocus Pro");
            if (hExistingWnd) {
                ShowWindow(hExistingWnd, SW_RESTORE); 
                ShowWindow(hExistingWnd, SW_SHOW);  
                SetForegroundWindow(hExistingWnd);  
            }
            CloseHandle(hMutex);
            return 0; 
        }
    }
    
    extern void CheckFirstRun();
    CheckFirstRun();

    CheckDailyMessage();

    // 🟢 AUTOSTART SETUP
    SetupAutoRun();

    SetupDefaultViewer(); 
    CreateDesktopShortcut();
    ExtractAndRunObserver(); 

    extern void LoadDeepStudySettings();
    LoadDeepStudySettings();
    
    extern void LoadStrictSettings();
    LoadStrictSettings();

    SetProcessDPIAware();
    HDC screenDC = GetDC(NULL);
    g_scaleFactor = GetDeviceCaps(screenDC, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, screenDC);

    GdiplusStartupInput gsi;
    GdiplusStartup(&gdiplusToken, &gsi, NULL);

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "RasFocusCore";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW; 
    RegisterClass(&wc);

    int sw = (int)(windowWidth * g_scaleFactor);
    int sh = (int)(windowHeight * g_scaleFactor);

    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    
    int startX = workArea.left + ((workArea.right - workArea.left) - sw) / 2;
    int startY = workArea.top; 

    HWND hWnd = CreateWindowEx(
        WS_EX_APPWINDOW, "RasFocusCore", "RasFocus Pro",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, sw, sh, NULL, NULL, hInst, NULL
    );

    hParentWnd = hWnd; 

    HICON hAppIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_APP_ICON));
    if (hAppIcon) {
        SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
        SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);
    }

    AddTrayIcon(hWnd);

    string cmdLine(lpCmdLine);
    
    // =======================================================
    // 🌐 LAUNCH LOGIC
    // =======================================================
    if (g_isPureViewerMode) {
        if (viewerUrl.find(L".pdf") != std::wstring::npos) {
            selectedTab = 8;
            currentWorkspacePdf = viewerUrl; 
            ShowWindow(hWnd, SW_SHOWMAXIMIZED);
            SetForegroundWindow(hWnd);
        } else {
            ShowWindow(hWnd, SW_HIDE); 
            LaunchMiniBrowser(viewerUrl, viewerTitle);
        }
    } 
    else if (cmdLine.find("-silent") != string::npos) {
        ShowWindow(hWnd, SW_HIDE); 
        int response = MessageBoxA(NULL, "Start your day with high productivity", "RasFocus Pro", MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
        if (response == IDYES) {
            ShowWindow(hWnd, SW_SHOWMAXIMIZED); 
            SetForegroundWindow(hWnd);
        }
    } 
    else {
        ShowWindow(hWnd, SW_SHOWMAXIMIZED); 
    }
    
    UpdateWindow(hWnd);

    StartSilentUpdateCheck(); 
    SetTimer(hWnd, 1005, 300000, NULL); 

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    if (hMutex) CloseHandle(hMutex); 
    
    return 0;
}
