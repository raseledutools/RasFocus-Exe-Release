#pragma once
#include "tab_schedule_blocks.h"
#include <vector>
#include <string>
#include <commdlg.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <thread>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <shlobj.h>
#include <ctime>

using namespace Gdiplus;
using namespace std;

// --- Dynamic Window Size Cache ---
static float s_contentX = 0.0f;
static float s_contentY = 0.0f;
static float s_contentW = 800.0f;
static float s_contentH = 600.0f;

// --- Smooth Scroll States ---
static float tWebScrollY = 0.0f, cWebScrollY = 0.0f;
static float tAppScrollY = 0.0f, cAppScrollY = 0.0f;
static float tStoreScrollY = 0.0f, cStoreScrollY = 0.0f;

// --- Main Navigation States ---
static int currentBlockTab = 0;
static int hoverBlockTab = -1;

// --- Focus Control States ---
static bool isFocusActive = false;
static int controlMode = 0; // 0=Self, 1=Friend
static bool hoverControlDropdown = false;
static bool isControlDropdownOpen = false;
static bool hoverOptSelf = false;
static bool hoverOptFriend = false;
static bool hoverStartFocusBtn = false;
static time_t focusEndTimeBlocks = 0;

// --- Friend Password Storage ---
static wstring friendPassword = L"";
static bool isFriendPasswordSet = false;

// --- Self Control Duration Selection ---
// Mode: 0=Days/Hours/Mins, selectionType
static int selfDurationMonths = 0;
static int selfDurationDays   = 0;
static int selfDurationHours  = 1;
static int selfDurationMins   = 0;
static int selfDurationTab    = 0; // 0=Days/Hours, 1=Months

// --- Live Remaining Time Display ---
static bool showLiveRemainingOverlay = false; // floating window showing remaining time

// --- Quotes States ---
static bool showQuotes = true;
static bool hoverQuotesCheckbox = false;
static int quoteLanguage = 0;
static bool hoverLangDropdown = false;
static bool isLangDropdownOpen = false;
static bool hoverOptBn = false;
static bool hoverOptEn = false;

struct Quote { wstring bn; wstring en; };
vector<Quote> motivationalQuotes = {
    {L"সময়ের মূল্য বোঝো, জীবন তোমার মূল্য বুঝবে।", L"Understand the value of time, life will understand your value."},
    {L"সফলতা আসে ফোকাস থেকে, ডিস্ট্রাকশন থেকে নয়।", L"Success comes from focus, not from distraction."},
    {L"আজকের সময় নষ্ট মানে, কালকের স্বপ্ন নষ্ট।", L"Wasting time today means ruining tomorrow's dreams."},
    {L"যে নিজের মনকে নিয়ন্ত্রণ করতে পারে, সে পৃথিবী জয় করতে পারে।", L"He who can control his mind can conquer the world."},
    {L"বড় কিছু পেতে হলে ছোট আনন্দগুলো ত্যাগ করতে হয়।", L"To achieve something big, you have to sacrifice small pleasures."},
    {L"তোমার আজকের ফোকাস নির্ধারণ করবে তোমার কালকের ভবিষ্যৎ।", L"Your focus today determines your future tomorrow."},
    {L"যা তোমাকে তোমার লক্ষ্য থেকে দূরে সরিয়ে নেয়, তাকে এখনই না বলো।", L"Say no to whatever takes you away from your goals."},
    {L"সাময়িক আনন্দ নয়, স্থায়ী সফলতার দিকে নজর দাও।", L"Focus on permanent success, not temporary pleasure."}
};

// --- Unlock Challenge Text (200 words, no copy-paste) ---
static const wstring UNLOCK_CHALLENGE_TEXT = 
    L"তুমি কি সত্যিই ফোকাস ভাঙতে চাও? মনে রেখো, প্রতিটি মুহূর্ত তোমার ভবিষ্যতের একটি ইট। "
    L"তোমার স্বপ্নগুলো তোমার অপেক্ষা করছে, কিন্তু সেগুলো পূরণ করতে হলে তোমাকে এখনই কাজ করতে হবে। "
    L"সোশ্যাল মিডিয়া, গেমস বা অন্য বিনোদন — এগুলো তোমার মূল্যবান সময় চুরি করছে। "
    L"যে সফল মানুষরা আছেন, তারা কখনো সহজ পথ বেছে নেননি। তারা কঠিন পরিশ্রম করেছেন, "
    L"মনোযোগ ধরে রেখেছেন এবং বিক্ষেপ থেকে দূরে থেকেছেন। তুমিও পারবে। "
    L"তোমার ভেতরে যে সম্ভাবনা আছে, তা অসাধারণ। কিন্তু সম্ভাবনা একা কিছু করে না — "
    L"তোমাকে সেই সম্ভাবনাকে কাজে লাগাতে হবে। এই মুহূর্তে তুমি যদি থামো, "
    L"তাহলে তোমার লক্ষ্য আরও দূরে সরে যাবে। একটু কষ্ট করো, একটু ত্যাগ করো, "
    L"এবং পরে যখন তুমি সফল হবে তখন বুঝতে পারবে এই সংগ্রাম কতটা মূল্যবান ছিল। "
    L"এখন ফিরে যাও এবং তোমার কাজ করো। তোমার স্বপ্ন তোমার অপেক্ষায় আছে!";

// --- Overlays States ---
static bool showTimeOverlay    = false;
static bool showPassOverlay    = false;
static bool showStoreOverlay   = false;
static bool showTitleOverlay   = false;
static bool showUnlockChallenge = false; // NEW: shows 200-word challenge before unlock
static bool showSetPasswordOverlay = false; // NEW: friend sets password first time

// Time overlay spinner hovers
static bool hTimeHM = false, hTimeHP = false;
static bool hTimeMM = false, hTimeMP = false;
static bool hTimeDM = false, hTimeDP = false; // Days
static bool hTimeMonM = false, hTimeMonP = false; // Months
static bool hTimeStart = false, hTimeCancel = false;
static bool hoverSelfTabDays = false, hoverSelfTabMonths = false;

static bool isPassInputActive = true, hPassInput = false;
static bool hPassConfirm = false, hPassCancel = false;
static bool isStoppingFocus = false;
static wstring inputPassText = L"";

// Set Password overlay
static wstring inputNewPass1 = L"";
static wstring inputNewPass2 = L"";
static bool isNewPass1Active = true, isNewPass2Active = false;
static bool hNewPass1 = false, hNewPass2 = false;
static bool hSetPassConfirm = false, hSetPassCancel = false;
static bool newPassMismatch = false;

// Unlock Challenge overlay
static wstring challengeUserType = L"";
static bool hChallengeProceed = false, hChallengeCancel = false;
static bool isChallengeTypingActive = false;

// --- Store Apps Overlay ---
static bool hoverStoreClose = false;
static int hoverStoreAddIdx = -1;
vector<wstring> systemStoreApps = {};

// --- Window Title Overlay ---
static wstring inputTitleText = L"";
static bool isTitleInputActive = true, hTitleInput = false;
static bool hTitleAdd = false, hTitleCancel = false;

// --- Simple Blocks States ---
static int simpleBlockMode = 1; // 0=Allow, 1=Block
static bool hoverModeDropdown = false, isModeDropdownOpen = false;
static bool hoverOptAllow = false, hoverOptBlock = false;

// Websites & Apps
static wstring webInputText = L"";
static bool isWebInputActive = false, hoverWebInput = false, hoverWebAddBtn = false;
static bool hoverWebCombo = false, isWebComboOpen = false;
static int hoverWebOptIdx = -1;
vector<wstring> commonWebsites = { L"facebook.com", L"youtube.com", L"instagram.com", L"tiktok.com", L"reddit.com", L"twitter.com" };

static wstring appInputText = L"";
static bool isAppInputActive = false, hoverAppInput = false, hoverAppAddBtn = false;
static bool hoverAppCombo = false, isAppComboOpen = false;
static int hoverAppOptIdx = -1;

// Apps dropdown: PC live 3rd party apps + blocked system tools
// By default these system tools are in the dropdown and BLOCKED when focus active:
static const vector<wstring> BLOCKED_SYSTEM_TOOLS = {
    L"Taskmgr.exe",       // Task Manager
    L"control.exe",       // Control Panel
    L"SystemSettings.exe",// Settings (Win10/11)
    L"regedit.exe",       // Registry Editor
    L"cmd.exe",           // Command Prompt
    L"powershell.exe",    // PowerShell
    L"WindowsPowerShell.exe",
    L"mmc.exe",           // Management Console
    L"msconfig.exe",      // System Config
    L"RunDialog",         // Win+R Run dialog
    L"explorer.exe"       // partially — block only uninstall/control paths
};

// Common apps for combo dropdown
vector<wstring> commonApps = {
    L"chrome.exe", L"msedge.exe", L"firefox.exe",
    L"telegram.exe", L"whatsapp.exe", L"discord.exe",
    L"vlc.exe", L"spotify.exe", L"netflix.exe",
    L"zoom.exe", L"skype.exe", L"Teams.exe",
    // System tools (shown in dropdown, BLOCKED by default during focus)
    L"Taskmgr.exe [Task Manager]",
    L"control.exe [Control Panel]",
    L"cmd.exe [Command Prompt]",
    L"regedit.exe [Run/Registry]"
};

static bool hoverAddExe = false, hoverAddStoreApp = false, hoverAddWindowTitle = false;

struct BlockItem { wstring name; bool isHoveredCross; bool isSystemLocked; };
static vector<BlockItem> webList;
static vector<BlockItem> appList;

// --- Colors ---
static const Color SClrTeal(255, 12, 168, 176);
static const Color SClrTealHover(255, 30, 185, 195);
static const Color SClrWhite(255, 255, 255, 255);
static const Color SClrDark(255, 50, 50, 50);
static const Color SClrGrayText(255, 120, 120, 120);
static const Color SClrBorder(255, 220, 225, 230);
static const Color SClrBg(255, 248, 250, 252);
static const Color SClrBgHover(255, 235, 248, 250);
static const Color SClrGreen(255, 90, 170, 20);
static const Color SClrGreenHover(255, 100, 190, 25);
static const Color SClrRed(255, 231, 76, 60);
static const Color SClrOrange(255, 230, 120, 20);
static const Color SClrOverlay(180, 0, 0, 0);
static const Color SClrDisabled(255, 200, 200, 200);

// ==========================================
// HIDDEN FOLDER + DATA PERSISTENCE
// ==========================================
wstring GetHiddenAppDataFolder() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        // Hidden folder with dot prefix
        wstring hiddenPath = wstring(path) + L"\\.RasFocusSession";
        CreateDirectoryW(hiddenPath.c_str(), NULL);
        // Set hidden+system attribute
        SetFileAttributesW(hiddenPath.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        return hiddenPath;
    }
    return L"";
}

wstring GetAppDataFolder() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        wstring fullPath = wstring(path) + L"\\RasFocus";
        CreateDirectoryW(fullPath.c_str(), NULL);
        return fullPath;
    }
    return L"";
}

// Add to Windows startup registry so session auto-starts on PC boot
void RegisterAutoStart() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"RasFocus", 0, REG_SZ,
            (BYTE*)exePath, (wcslen(exePath) + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
}

void SaveBlocksData() {
    // Save to both regular folder and hidden folder
    auto saveToPath = [&](wstring path) {
        string narrowPath(path.begin(), path.end());
        ofstream out(narrowPath.c_str(), ios::binary);
        if (!out) return;

        out.write((char*)&simpleBlockMode, sizeof(simpleBlockMode));
        out.write((char*)&controlMode, sizeof(controlMode));
        out.write((char*)&showQuotes, sizeof(showQuotes));
        out.write((char*)&quoteLanguage, sizeof(quoteLanguage));
        out.write((char*)&selfDurationHours, sizeof(selfDurationHours));
        out.write((char*)&selfDurationMins, sizeof(selfDurationMins));
        out.write((char*)&selfDurationDays, sizeof(selfDurationDays));
        out.write((char*)&selfDurationMonths, sizeof(selfDurationMonths));
        out.write((char*)&focusEndTimeBlocks, sizeof(focusEndTimeBlocks));
        out.write((char*)&isFriendPasswordSet, sizeof(isFriendPasswordSet));

        bool isActive = isFocusActive;
        out.write((char*)&isActive, sizeof(isActive));

        // Save friend password
        size_t passLen = friendPassword.length();
        out.write((char*)&passLen, sizeof(passLen));
        if (passLen > 0)
            out.write((char*)friendPassword.data(), passLen * sizeof(wchar_t));

        // Save web list
        size_t wSize = webList.size();
        out.write((char*)&wSize, sizeof(wSize));
        for (auto& w : webList) {
            size_t len = w.name.length();
            out.write((char*)&len, sizeof(len));
            out.write((char*)w.name.data(), len * sizeof(wchar_t));
            out.write((char*)&w.isSystemLocked, sizeof(w.isSystemLocked));
        }

        // Save app list
        size_t aSize = appList.size();
        out.write((char*)&aSize, sizeof(aSize));
        for (auto& a : appList) {
            size_t len = a.name.length();
            out.write((char*)&len, sizeof(len));
            out.write((char*)a.name.data(), len * sizeof(wchar_t));
            out.write((char*)&a.isSystemLocked, sizeof(a.isSystemLocked));
        }
    };

    saveToPath(GetAppDataFolder() + L"\\blocks_data.dat");
    saveToPath(GetHiddenAppDataFolder() + L"\\session.dat");

    // Auto-register on every save if focus is active
    if (isFocusActive) RegisterAutoStart();
}

void LoadBlocksData() {
    // Try hidden folder first, then regular
    wstring paths[] = {
        GetHiddenAppDataFolder() + L"\\session.dat",
        GetAppDataFolder() + L"\\blocks_data.dat"
    };

    for (auto& path : paths) {
        string narrowPath(path.begin(), path.end());
        ifstream in(narrowPath.c_str(), ios::binary);
        if (!in) continue;

        in.read((char*)&simpleBlockMode, sizeof(simpleBlockMode));
        in.read((char*)&controlMode, sizeof(controlMode));
        in.read((char*)&showQuotes, sizeof(showQuotes));
        in.read((char*)&quoteLanguage, sizeof(quoteLanguage));
        in.read((char*)&selfDurationHours, sizeof(selfDurationHours));
        in.read((char*)&selfDurationMins, sizeof(selfDurationMins));
        in.read((char*)&selfDurationDays, sizeof(selfDurationDays));
        in.read((char*)&selfDurationMonths, sizeof(selfDurationMonths));
        in.read((char*)&focusEndTimeBlocks, sizeof(focusEndTimeBlocks));
        in.read((char*)&isFriendPasswordSet, sizeof(isFriendPasswordSet));

        bool savedFocus = false;
        if (in.read((char*)&savedFocus, sizeof(savedFocus))) {
            if (savedFocus) {
                time_t now = std::time(nullptr);
                if (controlMode == 1) {
                    // Friend control: stays locked until password entered (no time limit)
                    isFocusActive = true;
                } else {
                    // Self control: check time
                    if (now < focusEndTimeBlocks) {
                        isFocusActive = true;
                    }
                }
            }
        }

        // Load friend password
        size_t passLen = 0;
        if (in.read((char*)&passLen, sizeof(passLen)) && passLen > 0) {
            friendPassword.resize(passLen);
            in.read((char*)friendPassword.data(), passLen * sizeof(wchar_t));
        }

        // Load web list
        size_t wSize = 0;
        if (in.read((char*)&wSize, sizeof(wSize))) {
            webList.clear();
            for (size_t i = 0; i < wSize; i++) {
                size_t len = 0; in.read((char*)&len, sizeof(len));
                wstring name(len, L'\0');
                in.read((char*)name.data(), len * sizeof(wchar_t));
                bool sysLocked = false;
                in.read((char*)&sysLocked, sizeof(sysLocked));
                webList.push_back({name, false, sysLocked});
            }
        }

        // Load app list
        size_t aSize = 0;
        if (in.read((char*)&aSize, sizeof(aSize))) {
            appList.clear();
            for (size_t i = 0; i < aSize; i++) {
                size_t len = 0; in.read((char*)&len, sizeof(len));
                wstring name(len, L'\0');
                in.read((char*)name.data(), len * sizeof(wchar_t));
                bool sysLocked = false;
                in.read((char*)&sysLocked, sizeof(sysLocked));
                appList.push_back({name, false, sysLocked});
            }
        }
        break; // loaded successfully
    }
}

// ==========================================
// HELPERS
// ==========================================
static GraphicsPath* GetBlockRoundRectPath(RectF rect, int radius) {
    GraphicsPath* path = new GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure();
    return path;
}

static wstring toLowerW_Blocks(wstring str) {
    for (auto& c : str) c = towlower(c);
    return str;
}

// Get remaining time as formatted string
static wstring GetRemainingTimeString() {
    if (!isFocusActive) return L"";
    if (controlMode == 1) return L"Friend Controlled (Password Required)";

    time_t now = std::time(nullptr);
    time_t left = focusEndTimeBlocks - now;
    if (left <= 0) return L"Ending...";

    long long totalSecs = (long long)left;
    int days  = (int)(totalSecs / 86400);
    int hours = (int)((totalSecs % 86400) / 3600);
    int mins  = (int)((totalSecs % 3600) / 60);
    int secs  = (int)(totalSecs % 60);

    wstring result = L"";
    if (days > 0)  result += to_wstring(days)  + L"d ";
    if (hours > 0) result += to_wstring(hours) + L"h ";
    if (mins > 0 || days == 0) result += to_wstring(mins) + L"m ";
    result += to_wstring(secs) + L"s";
    return result;
}

// --- Smart Tab Closer (ONLY TAB CLOSE) ---
void CloseActiveTabOnly(HWND hBrowser) {
    if (GetForegroundWindow() == hBrowser) {
        keybd_event(VK_CONTROL, 0, 0, 0);
        keybd_event('W', 0, 0, 0);
        keybd_event('W', 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    }
}

// --- Smart Tab Closer (TAB CLOSE + MINIMIZE) ---
void CloseActiveTabAndMinimize(HWND hBrowser) {
    if (GetForegroundWindow() == hBrowser) {
        keybd_event(VK_CONTROL, 0, 0, 0);
        keybd_event('W', 0, 0, 0);
        keybd_event('W', 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
        Sleep(50);
    }
    ShowWindow(hBrowser, SW_MINIMIZE);
}

void EnforceSystemApps() {
    for (auto it = appList.begin(); it != appList.end(); ) {
        if (it->isSystemLocked) it = appList.erase(it);
        else ++it;
    }
    if (simpleBlockMode == 0) { // Allow Mode
        appList.insert(appList.begin(), {L"explorer.exe", false, true});
        appList.insert(appList.begin(), {L"svchost.exe", false, true});
        appList.insert(appList.begin(), {L"RasFocus.exe", false, true});
        appList.insert(appList.begin(), {L"Taskmgr.exe", false, true});
        appList.insert(appList.begin(), {L"chrome.exe", false, true});
        appList.insert(appList.begin(), {L"msedge.exe", false, true});
    }
    SaveBlocksData();
}

// ==========================================
// LIVE REMAINING TIME FLOATING WINDOW
// ==========================================
struct LiveTimeWndData { bool visible; };

LRESULT CALLBACK LiveTimeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT rect;
        GetClientRect(hwnd, &rect);

        // Background
        SolidBrush bgBrush(Color(220, 10, 30, 60));
        g.FillRectangle(&bgBrush, 0.0f, 0.0f, (float)rect.right, (float)rect.bottom);
        Pen borderPen(SClrTeal, 2.0f);
        g.DrawRectangle(&borderPen, 1.0f, 1.0f, rect.right - 2.0f, rect.bottom - 2.0f);

        FontFamily ff(L"Segoe UI");
        Font fSmall(&ff, 11, FontStyleRegular, UnitPixel);
        Font fBig(&ff, 18, FontStyleBold, UnitPixel);
        SolidBrush whiteBrush(Color(255, 255, 255, 255));
        SolidBrush tealBrush(SClrTeal);
        StringFormat fmtC;
        fmtC.SetAlignment(StringAlignmentCenter);
        fmtC.SetLineAlignment(StringAlignmentCenter);

        wstring label = L"FOCUS REMAINING";
        wstring timeStr = GetRemainingTimeString();

        g.DrawString(label.c_str(), -1, &fSmall, RectF(0, 5.0f, rect.right, 18.0f), &fmtC, &tealBrush);
        g.DrawString(timeStr.c_str(), -1, &fBig, RectF(0, 22.0f, rect.right, 36.0f), &fmtC, &whiteBrush);

        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_TIMER && wParam == 2) {
        InvalidateRect(hwnd, NULL, TRUE); // repaint every second
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        // Allow dragging the window
        ReleaseCapture();
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND g_hLiveTimeWnd = NULL;

void CreateLiveTimeWindow() {
    if (g_hLiveTimeWnd && IsWindow(g_hLiveTimeWnd)) return;

    WNDCLASS wc = {0};
    wc.lpfnWndProc = LiveTimeWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "RasFocusLiveTime";
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    RegisterClass(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    g_hLiveTimeWnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        "RasFocusLiveTime", "Focus Timer",
        WS_POPUP | WS_VISIBLE,
        screenW - 220, 10, 210, 64,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (g_hLiveTimeWnd) {
        SetLayeredWindowAttributes(g_hLiveTimeWnd, 0, 210, LWA_ALPHA);
        ShowWindow(g_hLiveTimeWnd, SW_SHOW);
        SetTimer(g_hLiveTimeWnd, 2, 1000, NULL); // update every second
    }
}

void DestroyLiveTimeWindow() {
    if (g_hLiveTimeWnd && IsWindow(g_hLiveTimeWnd)) {
        KillTimer(g_hLiveTimeWnd, 2);
        DestroyWindow(g_hLiveTimeWnd);
        g_hLiveTimeWnd = NULL;
    }
}

void UpdateLiveTimeWindow() {
    if (!isFocusActive) {
        DestroyLiveTimeWindow();
        return;
    }
    if (!g_hLiveTimeWnd || !IsWindow(g_hLiveTimeWnd)) {
        CreateLiveTimeWindow();
    } else {
        InvalidateRect(g_hLiveTimeWnd, NULL, TRUE);
    }
}

// ==========================================
// POPUP NOTIFICATION THREAD
// ==========================================
struct BlocksPopupData { wstring quote; };

LRESULT CALLBACK BlocksPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT rect;
        GetClientRect(hwnd, &rect);
        RectF bgRect(0, 0, rect.right, rect.bottom);

        SolidBrush bgBrush(Color(255, 20, 80, 40));
        g.FillRectangle(&bgBrush, bgRect);
        Pen border(SClrTeal, 4.0f);
        g.DrawRectangle(&border, 2.0f, 2.0f, rect.right - 4.0f, rect.bottom - 4.0f);

        FontFamily ff(L"Segoe UI");
        Font fQ(&ff, 28, FontStyleBold, UnitPixel);
        SolidBrush whiteBrush(Color(255, 255, 255, 255));
        StringFormat fmtC;
        fmtC.SetAlignment(StringAlignmentCenter);
        fmtC.SetLineAlignment(StringAlignmentCenter);

        BlocksPopupData* pData = (BlocksPopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (pData) {
            g.DrawString(pData->quote.c_str(), -1, &fQ,
                RectF(20.0f, 20.0f, rect.right - 40.0f, rect.bottom - 40.0f),
                &fmtC, &whiteBrush);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_TIMER && wParam == 1) {
        KillTimer(hwnd, 1);
        DestroyWindow(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_DESTROY) {
        BlocksPopupData* pData = (BlocksPopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (pData) delete pData;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void SafeBlocksPopupThread(wstring quote) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = BlocksPopupWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "RasFocusBlocksPopupClass";
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    RegisterClass(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int w = 1000, h = 250;
    HWND hPopup = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        "RasFocusBlocksPopupClass", "Alert",
        WS_POPUP, (screenW - w) / 2, 80, w, h,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (hPopup) {
        BlocksPopupData* data = new BlocksPopupData{quote};
        SetWindowLongPtr(hPopup, GWLP_USERDATA, (LONG_PTR)data);
        SetLayeredWindowAttributes(hPopup, 0, 240, LWA_ALPHA);
        ShowWindow(hPopup, SW_SHOW);
        SetForegroundWindow(hPopup);
        SetTimer(hPopup, 1, 6000, NULL);

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

void TriggerGlobalBlockAlert(bool isWarning = false, wstring customMsg = L"") {
    wstring finalQuote = L"Focus Active! Access Denied.";
    if (isWarning) {
        finalQuote = customMsg;
    } else if (showQuotes) {
        int rIdx = rand() % motivationalQuotes.size();
        finalQuote = (quoteLanguage == 0) ? motivationalQuotes[rIdx].bn : motivationalQuotes[rIdx].en;
    }
    thread t(SafeBlocksPopupThread, finalQuote);
    t.detach();
}

// ==========================================
// GLOBAL KEYLOGGER
// ==========================================
HHOOK hKeyboardHookBlocks = NULL;
string globalKeyBufferBlocks = "";

LRESULT CALLBACK KeyboardHookProcBlocks(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN && isFocusActive) {
        KBDLLHOOKSTRUCT* kbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = kbdStruct->vkCode;

        if ((vkCode >= 'A' && vkCode <= 'Z') || (vkCode >= '0' && vkCode <= '9') ||
            vkCode == VK_SPACE || vkCode == VK_OEM_PERIOD) {
            char c = MapVirtualKey(vkCode, MAPVK_VK_TO_CHAR);
            if (vkCode == VK_OEM_PERIOD) c = '.';
            globalKeyBufferBlocks += tolower(c);
            if (globalKeyBufferBlocks.length() > 100)
                globalKeyBufferBlocks.erase(0, 1);

            wstring wBuffer(globalKeyBufferBlocks.begin(), globalKeyBufferBlocks.end());
            bool shouldBlock = false;

            for (const auto& web : webList) {
                wstring lowerWeb = toLowerW_Blocks(web.name);
                size_t dotPos = lowerWeb.find(L".");
                wstring coreName = (dotPos != wstring::npos) ? lowerWeb.substr(0, dotPos) : lowerWeb;
                if (coreName.length() > 2 && wBuffer.find(coreName) != wstring::npos) {
                    shouldBlock = true; break;
                }
            }

            if (!shouldBlock && simpleBlockMode == 1) {
                for (const auto& app : appList) {
                    wstring lowerApp = toLowerW_Blocks(app.name);
                    size_t dotPos = lowerApp.find(L".");
                    wstring coreName = (dotPos != wstring::npos) ? lowerApp.substr(0, dotPos) : lowerApp;
                    if (coreName.length() > 2 && wBuffer.find(coreName) != wstring::npos) {
                        shouldBlock = true; break;
                    }
                }
            }

            if (shouldBlock) {
                globalKeyBufferBlocks = "";
                HWND hActive = GetForegroundWindow();
                if (hActive) CloseActiveTabOnly(hActive);
                TriggerGlobalBlockAlert();
            }
        } else if (vkCode == VK_BACK) {
            if (!globalKeyBufferBlocks.empty()) globalKeyBufferBlocks.pop_back();
        }
    }
    return CallNextHookEx(hKeyboardHookBlocks, nCode, wParam, lParam);
}

void StartBlocksKeyloggerThread() {
    hKeyboardHookBlocks = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProcBlocks, NULL, 0);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

// --- Check if a process name is a blocked system tool ---
bool IsBlockedSystemTool(const wstring& processName) {
    wstring lower = toLowerW_Blocks(processName);
    for (const auto& tool : BLOCKED_SYSTEM_TOOLS) {
        wstring lowerTool = toLowerW_Blocks(tool);
        if (lower == lowerTool || lower.find(lowerTool) != wstring::npos)
            return true;
    }
    return false;
}

// --- REAL BLOCKING ENGINE ---
bool ShouldKillProcessBlocks(const wstring& processName, const wstring& windowTitle) {
    if (!isFocusActive) return false;

    wstring lowerTitle   = toLowerW_Blocks(windowTitle);
    wstring lowerProcess = toLowerW_Blocks(processName);

    // Always block these system bypass tools during focus
    if (lowerTitle.find(L"task manager") != wstring::npos ||
        lowerTitle.find(L"taskmgr")      != wstring::npos ||
        lowerProcess == L"taskmgr.exe") {
        TriggerGlobalBlockAlert(true, L"Focus is Active. Task Manager is blocked!");
        return true;
    }
    if (lowerTitle.find(L"control panel") != wstring::npos ||
        lowerProcess == L"control.exe") {
        TriggerGlobalBlockAlert(true, L"Focus is Active. Control Panel is blocked!");
        return true;
    }
    if (lowerProcess == L"cmd.exe" ||
        lowerProcess == L"powershell.exe" ||
        lowerProcess.find(L"windowspowershell") != wstring::npos) {
        TriggerGlobalBlockAlert(true, L"Focus is Active. Command Prompt is blocked!");
        return true;
    }
    if (lowerProcess == L"regedit.exe" || lowerProcess == L"msconfig.exe") {
        TriggerGlobalBlockAlert(true, L"Focus is Active. System tools are blocked!");
        return true;
    }
    if (lowerTitle.find(L"uninstall")            != wstring::npos ||
        lowerTitle.find(L"programs and features") != wstring::npos ||
        lowerTitle.find(L"apps & features")       != wstring::npos) {
        TriggerGlobalBlockAlert(true, L"Focus is Active. Uninstallation is blocked!");
        return true;
    }

    if (simpleBlockMode == 1) {
        for (const auto& app : appList) {
            wstring lowerApp = toLowerW_Blocks(app.name);
            // Strip annotation like "[Task Manager]"
            size_t bracket = lowerApp.find(L" [");
            if (bracket != wstring::npos) lowerApp = lowerApp.substr(0, bracket);

            if (lowerProcess == lowerApp ||
                lowerProcess.find(lowerApp) != wstring::npos ||
                lowerTitle.find(lowerApp)   != wstring::npos) {
                TriggerGlobalBlockAlert();
                return true;
            }
        }
    } else if (simpleBlockMode == 0) {
        bool isAllowed = false;
        for (const auto& app : appList) {
            wstring lowerApp = toLowerW_Blocks(app.name);
            size_t bracket = lowerApp.find(L" [");
            if (bracket != wstring::npos) lowerApp = lowerApp.substr(0, bracket);
            if (lowerProcess == lowerApp || lowerTitle.find(lowerApp) != wstring::npos) {
                isAllowed = true; break;
            }
        }
        if (!isAllowed && windowTitle.length() > 0 && processName != L"explorer.exe") {
            TriggerGlobalBlockAlert(true, L"Focus is Active. Only allowed apps can run.");
            return true;
        }
    }
    return false;
}

BOOL CALLBACK EnumWindowsProcBlocker(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t windowTitle[256];
    GetWindowTextW(hwnd, windowTitle, 256);
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == GetCurrentProcessId()) return TRUE;

    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE, FALSE, processId);
    if (hProcess) {
        wchar_t processName[MAX_PATH];
        HMODULE hMod; DWORD cbNeeded;
        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
            GetModuleBaseNameW(hProcess, hMod, processName, sizeof(processName) / sizeof(wchar_t));
            wstring pName(processName), wTitle(windowTitle);
            if (ShouldKillProcessBlocks(pName, wTitle)) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
                TerminateProcess(hProcess, 0);
            }
        }
        CloseHandle(hProcess);
    }
    return TRUE;
}

void BackgroundBlockerThread() {
    int processKillTimer = 0;
    while (true) {
        if (isFocusActive) {
            // Update live time window
            UpdateLiveTimeWindow();

            // Check self-control time expiry
            if (controlMode == 0) {
                time_t now = std::time(nullptr);
                if (now >= focusEndTimeBlocks) {
                    isFocusActive = false;
                    SaveBlocksData();
                    DestroyLiveTimeWindow();
                }
            }

            HWND hActive = GetForegroundWindow();
            if (hActive) {
                wchar_t windowTitle[512];
                if (GetWindowTextW(hActive, windowTitle, 512) > 0) {
                    wstring lowerTitle = toLowerW_Blocks(windowTitle);
                    bool shouldBlockTab = false;
                    for (const auto& web : webList) {
                        wstring lowerWeb = toLowerW_Blocks(web.name);
                        size_t dotPos = lowerWeb.find(L".");
                        wstring coreName = (dotPos != wstring::npos) ? lowerWeb.substr(0, dotPos) : lowerWeb;
                        if (coreName.length() > 2 && lowerTitle.find(coreName) != wstring::npos) {
                            shouldBlockTab = true; break;
                        }
                    }
                    if (shouldBlockTab) {
                        CloseActiveTabAndMinimize(hActive);
                        TriggerGlobalBlockAlert();
                        Sleep(1500);
                    }
                }
            }

            processKillTimer += 50;
            if (processKillTimer >= 2000) {
                EnumWindows(EnumWindowsProcBlocker, 0);
                processKillTimer = 0;
            }
        } else {
            DestroyLiveTimeWindow();
        }
        Sleep(50);
    }
}

static bool threadStarted = false;
void StartBlockerThread() {
    if (!threadStarted) {
        thread t(BackgroundBlockerThread); t.detach();
        thread kl(StartBlocksKeyloggerThread); kl.detach();
        threadStarted = true;
    }
}

void RefreshRunningApps() {
    systemStoreApps.clear();
    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hProcessSnap, &pe32)) {
        do {
            wstring exeName = pe32.szExeFile;
            if (exeName != L"svchost.exe" && exeName != L"conhost.exe" &&
                exeName != L"System" && exeName.length() > 4) {
                bool exists = false;
                for (const auto& app : systemStoreApps) {
                    if (app == exeName) { exists = true; break; }
                }
                if (!exists && systemStoreApps.size() < 100)
                    systemStoreApps.push_back(exeName);
            }
        } while (Process32NextW(hProcessSnap, &pe32));
    }
    CloseHandle(hProcessSnap);

    // Priority Sorting
    vector<wstring> priorities = {L"chrome", L"msedge", L"firefox", L"telegram",
                                   L"whatsapp", L"discord", L"vlc", L"spotify",
                                   L"netflix", L"zoom", L"skype"};
    auto getPriority = [&](const wstring& name) {
        wstring lower = toLowerW_Blocks(name);
        for (const auto& p : priorities) {
            if (lower.find(p) != wstring::npos) return 0;
        }
        return 1;
    };
    std::sort(systemStoreApps.begin(), systemStoreApps.end(), [&](const wstring& a, const wstring& b) {
        int pA = getPriority(a), pB = getPriority(b);
        if (pA != pB) return pA < pB;
        return a < b;
    });

    // Add system blocked tools to top of list with annotation
    systemStoreApps.insert(systemStoreApps.begin(), L"regedit.exe [Run/Registry]");
    systemStoreApps.insert(systemStoreApps.begin(), L"cmd.exe [Command Prompt]");
    systemStoreApps.insert(systemStoreApps.begin(), L"control.exe [Control Panel]");
    systemStoreApps.insert(systemStoreApps.begin(), L"Taskmgr.exe [Task Manager]");

    if (systemStoreApps.empty())
        systemStoreApps.push_back(L"No active apps found");
}

// ==========================================
// DRAW SPINNER HELPER
// ==========================================
static void DrawBlocksOverlaySpinner(Graphics& g, float x, float y,
    const wstring& valStr, bool hM, bool hP,
    Font* fIcon, Font* fBold) {
    SolidBrush brushBtn(SClrBorder);
    SolidBrush brushBtnHover(SClrGrayText);
    SolidBrush brushWhite(SClrWhite);
    SolidBrush brushDark(SClrDark);
    Pen penBorder(SClrBorder, 1.5f);
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);

    RectF mRect(x, y, 32.0f, 36.0f);
    RectF tRect(x + 32.0f, y, 60.0f, 36.0f);
    RectF pRect(x + 92.0f, y, 32.0f, 36.0f);

    g.FillRectangle(hM ? &brushBtnHover : &brushBtn, mRect);
    g.DrawRectangle(&penBorder, mRect.X, mRect.Y, mRect.Width, mRect.Height);
    g.DrawString(L"\xE738", -1, fIcon, mRect, &fmtC, &brushDark);

    g.FillRectangle(&brushWhite, tRect);
    g.DrawRectangle(&penBorder, tRect.X, tRect.Y, tRect.Width, tRect.Height);
    g.DrawString(valStr.c_str(), -1, fBold, tRect, &fmtC, &brushDark);

    g.FillRectangle(hP ? &brushBtnHover : &brushBtn, pRect);
    g.DrawRectangle(&penBorder, pRect.X, pRect.Y, pRect.Width, pRect.Height);
    g.DrawString(L"\xE710", -1, fIcon, pRect, &fmtC, &brushDark);
}

// ==========================================
// MAIN DRAW FUNCTION
// ==========================================
void DrawBlocksTab(Graphics& g, float contentX, float contentY, float contentW, float contentH) {
    static bool isBlocksDataLoaded = false;
    if (!isBlocksDataLoaded) {
        LoadBlocksData();
        isBlocksDataLoaded = true;
    }

    StartBlockerThread();

    // Smooth scroll
    cWebScrollY += (tWebScrollY - cWebScrollY) * 0.2f;
    if (abs(tWebScrollY - cWebScrollY) < 0.5f) cWebScrollY = tWebScrollY;
    cAppScrollY += (tAppScrollY - cAppScrollY) * 0.2f;
    if (abs(tAppScrollY - cAppScrollY) < 0.5f) cAppScrollY = tAppScrollY;
    cStoreScrollY += (tStoreScrollY - cStoreScrollY) * 0.2f;
    if (abs(tStoreScrollY - cStoreScrollY) < 0.5f) cStoreScrollY = tStoreScrollY;

    s_contentX = contentX; s_contentY = contentY;
    s_contentW = contentW; s_contentH = contentH;

    FontFamily ff(L"Segoe UI");
    Font fTopTab(&ff, 15, FontStyleBold, UnitPixel);
    Font fTitle(&ff, 22, FontStyleBold, UnitPixel);
    Font fNormal(&ff, 15, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 15, FontStyleBold, UnitPixel);
    Font fInfo(&ff, 13, FontStyleItalic, UnitPixel);
    Font fSmall(&ff, 12, FontStyleRegular, UnitPixel);
    Font fSmallerBold(&ff, 12, FontStyleBold, UnitPixel);
    Font fTiny(&ff, 11, FontStyleRegular, UnitPixel);
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fIcon(&ffIcons, 22, FontStyleRegular, UnitPixel);
    Font fSmallIcon(&ffIcons, 14, FontStyleRegular, UnitPixel);

    SolidBrush brushTeal(SClrTeal);
    SolidBrush brushDark(SClrDark);
    SolidBrush brushGray(SClrGrayText);
    SolidBrush brushWhite(SClrWhite);
    SolidBrush brushBg(SClrBg);
    SolidBrush brushRed(SClrRed);
    SolidBrush brushOrange(SClrOrange);
    Pen penBorder(SClrBorder, 1.5f);
    Pen penTeal(SClrTeal, 2.0f);
    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);

    // ==========================================
    // HEADER TABS
    // ==========================================
    g.FillRectangle(&brushWhite, contentX, contentY, contentW, 60.0f);

    float tabW = 200.0f, tabH = 40.0f;
    float tab1X = contentX + 20.0f;
    float tab2X = tab1X + tabW + 10.0f;
    float tab3X = tab2X + tabW + 10.0f;
    float tabY  = contentY + 10.0f;

    SolidBrush bTab1(currentBlockTab == 0 ? Color(255,12,168,176) : (hoverBlockTab==0 ? Color(255,230,230,230) : Color(255,245,245,245)));
    SolidBrush bTab2(currentBlockTab == 1 ? Color(255,12,168,176) : (hoverBlockTab==1 ? Color(255,230,230,230) : Color(255,245,245,245)));
    SolidBrush bTab3(currentBlockTab == 2 ? Color(255,12,168,176) : (hoverBlockTab==2 ? Color(255,230,230,230) : Color(255,245,245,245)));
    SolidBrush bT1(currentBlockTab==0 ? Color(255,255,255,255) : Color(255,100,100,100));
    SolidBrush bT2(currentBlockTab==1 ? Color(255,255,255,255) : Color(255,100,100,100));
    SolidBrush bT3(currentBlockTab==2 ? Color(255,255,255,255) : Color(255,100,100,100));

    g.FillRectangle(&bTab1, tab1X, tabY, tabW, tabH);
    g.DrawString(L"Simple Blocks",   -1, &fTopTab, RectF(tab1X, tabY, tabW, tabH), &fmtC, &bT1);
    g.FillRectangle(&bTab2, tab2X, tabY, tabW, tabH);
    g.DrawString(L"Schedule Blocks", -1, &fTopTab, RectF(tab2X, tabY, tabW, tabH), &fmtC, &bT2);
    g.FillRectangle(&bTab3, tab3X, tabY, tabW, tabH);
    g.DrawString(L"Device Block",    -1, &fTopTab, RectF(tab3X, tabY, tabW, tabH), &fmtC, &bT3);

    float bodyY = contentY + 60.0f;
    g.FillRectangle(&brushBg, contentX, bodyY, contentW, contentH - 60.0f);

    float boxX = contentX + 30.0f;
    float boxW = contentW - 60.0f;
    float boxH = contentH - 60.0f - 40.0f;
    GraphicsPath* boxPath = GetBlockRoundRectPath(RectF(boxX, bodyY + 20.0f, boxW, boxH), 6);
    g.FillPath(&brushWhite, boxPath);
    g.DrawPath(&penBorder, boxPath);
    delete boxPath;

    float ctrlDropX = boxX + 30.0f;
    float ctrlDropY = bodyY + 40.0f;
    float modeDropX = boxX + 150.0f;
    float modeDropY = ctrlDropY + 75.0f;
    float webComboX = boxX + 30.0f + ((boxW - 90.0f) / 2.0f) - 105.0f;
    float webComboY = modeDropY + 95.0f;

    if (currentBlockTab == 0) {
        float rowY = bodyY + 40.0f;

        // --- Control Mode Dropdown ---
        RectF ctrlDrop(ctrlDropX, rowY, 160.0f, 40.0f);
        GraphicsPath* cdp = GetBlockRoundRectPath(ctrlDrop, 4);
        SolidBrush cDropBg((!isFocusActive && hoverControlDropdown) ? SClrBgHover : (isFocusActive ? SClrBg : SClrWhite));
        g.FillPath(&cDropBg, cdp);
        g.DrawPath(&penBorder, cdp);
        delete cdp;
        wstring ctrlTxt = (controlMode == 0) ? L"Self Control" : L"Friend Control";
        g.DrawString(ctrlTxt.c_str(), -1, &fBold,
            RectF(ctrlDrop.X + 15.0f, ctrlDrop.Y, ctrlDrop.Width - 30.0f, ctrlDrop.Height),
            &fmtL, isFocusActive ? &brushGray : &brushDark);
        g.DrawString(L"\xE70D", -1, &fSmallIcon,
            RectF(ctrlDrop.X + ctrlDrop.Width - 30.0f, ctrlDrop.Y, 30.0f, ctrlDrop.Height),
            &fmtC, &brushGray);

        // --- Live Remaining Time Display in header row ---
        if (isFocusActive) {
            wstring remainStr = GetRemainingTimeString();
            SolidBrush remainBrush(controlMode == 1 ? SClrOrange : SClrTeal);
            // Draw timer box
            RectF timerBox(boxX + 355.0f, rowY, 340.0f, 40.0f);
            GraphicsPath* tbp = GetBlockRoundRectPath(timerBox, 4);
            SolidBrush timerBg(controlMode==1 ? Color(20,230,120,20) : Color(20,12,168,176));
            g.FillPath(&timerBg, tbp);
            g.DrawPath(&penBorder, tbp);
            delete tbp;
            // Clock icon
            g.DrawString(L"\xE916", -1, &fSmallIcon,
                RectF(timerBox.X + 8.0f, timerBox.Y, 24.0f, timerBox.Height), &fmtC, &remainBrush);
            // Time string
            g.DrawString(remainStr.c_str(), -1, &fBold,
                RectF(timerBox.X + 32.0f, timerBox.Y, timerBox.Width - 40.0f, timerBox.Height),
                &fmtL, &remainBrush);
        }

        // --- Start/Stop Focus Button ---
        RectF startBtn(boxX + 175.0f, rowY, 170.0f, 40.0f);

        if (isFocusActive && controlMode == 0) {
            time_t now = std::time(nullptr);
            if (now >= focusEndTimeBlocks) {
                isFocusActive = false;
                SaveBlocksData();
                DestroyLiveTimeWindow();
            }
        }

        SolidBrush sbBrush(isFocusActive ?
            (hoverStartFocusBtn ? Color(255,200,50,50) : SClrRed) :
            (hoverStartFocusBtn ? SClrGreenHover : SClrGreen));
        GraphicsPath* sbp = GetBlockRoundRectPath(startBtn, 4);
        g.FillPath(&sbBrush, sbp);
        delete sbp;

        wstring startTextStr = isFocusActive ? L"Stop Focus" : L"Start Focus";
        g.DrawString(startTextStr.c_str(), -1, &fBold, startBtn, &fmtC, &brushWhite);

        // Show FOCUS ACTIVE warning text
        if (isFocusActive) {
            g.DrawString(L"FOCUS ACTIVE: System tools & Task Manager restricted.",
                -1, &fInfo,
                RectF(boxX + 30.0f, rowY + 44.0f, boxW - 60.0f, 18.0f),
                &fmtL, &brushRed);
        }

        rowY += (isFocusActive ? 72.0f : 60.0f);
        g.DrawLine(&penBorder, boxX + 30.0f, rowY, boxX + boxW - 30.0f, rowY);
        rowY += 15.0f;

        SolidBrush activeTextBrush(isFocusActive ? SClrGrayText : SClrDark);
        SolidBrush activeTealBrush(isFocusActive ? SClrGrayText : SClrTeal);
        SolidBrush activeInputBg(isFocusActive ? SClrBg : SClrWhite);

        // --- Mode Dropdown ---
        g.DrawString(L"Select Mode:", -1, &fBold,
            RectF(boxX + 30.0f, rowY, 120.0f, 36.0f), &fmtL, &activeTextBrush);
        RectF dropRect(modeDropX, rowY, 200.0f, 36.0f);
        GraphicsPath* dp = GetBlockRoundRectPath(dropRect, 4);
        SolidBrush dropBg(isFocusActive ? SClrBg : (hoverModeDropdown ? SClrBgHover : SClrWhite));
        g.FillPath(&dropBg, dp);
        g.DrawPath(&penBorder, dp);
        delete dp;
        wstring modeTxt = (simpleBlockMode == 0) ? L"Allow Apps & Web" : L"Block Apps & Web";
        g.DrawString(modeTxt.c_str(), -1, &fNormal,
            RectF(dropRect.X + 15.0f, dropRect.Y, dropRect.Width - 30.0f, dropRect.Height),
            &fmtL, &activeTealBrush);
        g.DrawString(L"\xE70D", -1, &fSmallIcon,
            RectF(dropRect.X + dropRect.Width - 30.0f, dropRect.Y, 30.0f, dropRect.Height),
            &fmtC, &brushGray);

        float colW     = (boxW - 90.0f) / 2.0f;
        float leftColX = boxX + 30.0f;
        float rightColX= boxX + 60.0f + colW;
        float secY     = rowY + 50.0f;

        // ==========================================
        // LEFT COLUMN: WEBSITES
        // ==========================================
        g.DrawString(L"Websites", -1, &fTitle,
            RectF(leftColX, secY, colW, 40.0f), &fmtL, &brushDark);

        RectF webInpRect(leftColX, secY + 45.0f, colW - 110.0f, 36.0f);
        GraphicsPath* wp = GetBlockRoundRectPath(webInpRect, 4);
        g.FillPath(&brushWhite, wp);
        g.DrawPath((isWebInputActive ? &penTeal : &penBorder), wp);
        delete wp;

        if (webInputText.empty() && !isWebInputActive)
            g.DrawString(L"e.g. facebook.com", -1, &fNormal,
                RectF(webInpRect.X + 10.0f, webInpRect.Y, webInpRect.Width, webInpRect.Height),
                &fmtL, &brushGray);
        else {
            g.DrawString(webInputText.c_str(), -1, &fNormal,
                RectF(webInpRect.X + 10.0f, webInpRect.Y, webInpRect.Width, webInpRect.Height),
                &fmtL, &brushDark);
            if (isWebInputActive && (GetTickCount() / 500) % 2 == 0) {
                Graphics gTemp(GetDesktopWindow()); RectF bRect;
                gTemp.MeasureString(webInputText.c_str(), -1, &fNormal, PointF(0,0), &bRect);
                float cursorX = webInputText.empty() ? webInpRect.X + 10.0f : webInpRect.X + 10.0f + bRect.Width;
                g.FillRectangle(&brushDark, cursorX, webInpRect.Y + 8.0f, 1.5f, 20.0f);
            }
        }

        RectF wComboRect(webComboX, secY + 45.0f, 30.0f, 36.0f);
        GraphicsPath* wcp = GetBlockRoundRectPath(wComboRect, 4);
        SolidBrush wComboBrush(isFocusActive ? SClrDisabled : (hoverWebCombo ? SClrBorder : SClrWhite));
        g.FillPath(&wComboBrush, wcp);
        g.DrawPath(&penBorder, wcp);
        delete wcp;
        g.DrawString(L"\xE70D", -1, &fSmallIcon, wComboRect, &fmtC, &brushDark);

        RectF wAddRect(leftColX + colW - 70.0f, secY + 45.0f, 70.0f, 36.0f);
        GraphicsPath* wAddP = GetBlockRoundRectPath(wAddRect, 4);
        SolidBrush wAddBrush(hoverWebAddBtn ? SClrTealHover : SClrTeal);
        g.FillPath(&wAddBrush, wAddP);
        delete wAddP;
        g.DrawString(L"+ Add", -1, &fBold, wAddRect, &fmtC, &brushWhite);

        // Web Table
        RectF webTable(leftColX, secY + 90.0f, colW, 160.0f);
        g.FillRectangle(&brushBg, webTable);
        g.DrawRectangle(&penBorder, webTable.X, webTable.Y, webTable.Width, webTable.Height);

        Region oldClip; g.GetClip(&oldClip);
        g.SetClip(webTable);
        float itemY = webTable.Y + 5.0f - cWebScrollY;
        for (size_t i = 0; i < webList.size(); ++i) {
            if (itemY > webTable.Y - 30.0f && itemY < webTable.Y + webTable.Height) {
                g.DrawString(webList[i].name.c_str(), -1, &fNormal,
                    RectF(leftColX + 10.0f, itemY, colW - 40.0f, 30.0f), &fmtL, &brushDark);
                SolidBrush crossBrush(isFocusActive ? SClrGrayText : (webList[i].isHoveredCross ? SClrRed : SClrGrayText));
                g.DrawString(L"\xE711", -1, &fSmallIcon,
                    RectF(leftColX + colW - 30.0f, itemY, 30.0f, 30.0f), &fmtC, &crossBrush);
                g.DrawLine(&penBorder, leftColX + 5.0f, itemY + 30.0f, leftColX + colW - 5.0f, itemY + 30.0f);
            }
            itemY += 30.0f;
        }
        g.SetClip(&oldClip);

        if (webList.size() * 30.0f > webTable.Height) {
            float maxScroll = webList.size() * 30.0f - webTable.Height + 10.0f;
            float thumbH = max(20.0f, (webTable.Height / (webList.size() * 30.0f)) * webTable.Height);
            float thumbY = webTable.Y + (cWebScrollY / maxScroll) * (webTable.Height - thumbH);
            g.FillRectangle(&brushGray, webTable.X + webTable.Width - 4.0f, thumbY, 4.0f, thumbH);
        }

        // ==========================================
        // RIGHT COLUMN: APPS
        // ==========================================
        float qY = secY;
        SolidBrush cbBrush(showQuotes ? (isFocusActive ? SClrGrayText : SClrTeal) : SClrWhite);
        RectF cbRect(rightColX, qY + 2.0f, 16.0f, 16.0f);
        g.FillRectangle(&cbBrush, cbRect);
        g.DrawRectangle(&penBorder, cbRect.X, cbRect.Y, cbRect.Width, cbRect.Height);
        if (showQuotes)
            g.DrawString(L"\xE73E", -1, &fSmallIcon, cbRect, &fmtC, &brushWhite);
        g.DrawString(L"Motivational Quotes", -1, &fNormal,
            RectF(rightColX + 25.0f, qY, 150.0f, 20.0f), &fmtL, &activeTextBrush);

        RectF langRect(rightColX + 185.0f, qY - 2.0f, 100.0f, 24.0f);
        g.FillRectangle(&activeInputBg, langRect);
        g.DrawRectangle(&penBorder, langRect.X, langRect.Y, langRect.Width, langRect.Height);
        wstring langTxt = (quoteLanguage == 0) ? L"Bangla" : L"English";
        g.DrawString(langTxt.c_str(), -1, &fNormal,
            RectF(langRect.X + 5.0f, langRect.Y, 70.0f, 24.0f), &fmtL, &activeTextBrush);
        g.DrawString(L"\xE70D", -1, &fSmallIcon,
            RectF(langRect.X + 75.0f, langRect.Y, 25.0f, 24.0f), &fmtC, &brushGray);

        g.DrawString(L"Applications", -1, &fTitle,
            RectF(rightColX, secY + 25.0f, colW, 40.0f), &fmtL, &brushDark);

        // Info text about system tools being blocked
        g.DrawString(L"(Task Mgr, Control Panel, CMD, Run are always blocked during focus)",
            -1, &fTiny,
            RectF(rightColX, secY + 53.0f, colW, 16.0f),
            &fmtL, &brushGray);

        RectF appInpRect(rightColX, secY + 70.0f, colW - 110.0f, 36.0f);
        GraphicsPath* ap = GetBlockRoundRectPath(appInpRect, 4);
        g.FillPath(&brushWhite, ap);
        g.DrawPath((isAppInputActive ? &penTeal : &penBorder), ap);
        delete ap;

        if (appInputText.empty() && !isAppInputActive)
            g.DrawString(L"e.g. telegram.exe", -1, &fNormal,
                RectF(appInpRect.X + 10.0f, appInpRect.Y, appInpRect.Width, appInpRect.Height),
                &fmtL, &brushGray);
        else {
            g.DrawString(appInputText.c_str(), -1, &fNormal,
                RectF(appInpRect.X + 10.0f, appInpRect.Y, appInpRect.Width, appInpRect.Height),
                &fmtL, &brushDark);
            if (isAppInputActive && (GetTickCount() / 500) % 2 == 0) {
                Graphics gTemp(GetDesktopWindow()); RectF bRect;
                gTemp.MeasureString(appInputText.c_str(), -1, &fNormal, PointF(0,0), &bRect);
                float cursorX = appInputText.empty() ? appInpRect.X + 10.0f : appInpRect.X + 10.0f + bRect.Width;
                g.FillRectangle(&brushDark, cursorX, appInpRect.Y + 8.0f, 1.5f, 20.0f);
            }
        }

        float aComboX = rightColX + colW - 105.0f;
        RectF aComboRect(aComboX, secY + 70.0f, 30.0f, 36.0f);
        GraphicsPath* acp = GetBlockRoundRectPath(aComboRect, 4);
        SolidBrush aComboBrush(isFocusActive ? SClrDisabled : (hoverAppCombo ? SClrBorder : SClrWhite));
        g.FillPath(&aComboBrush, acp);
        g.DrawPath(&penBorder, acp);
        delete acp;
        g.DrawString(L"\xE70D", -1, &fSmallIcon, aComboRect, &fmtC, &brushDark);

        RectF aAddRect(rightColX + colW - 70.0f, secY + 70.0f, 70.0f, 36.0f);
        GraphicsPath* aAddP = GetBlockRoundRectPath(aAddRect, 4);
        SolidBrush aAddBrush(hoverAppAddBtn ? SClrTealHover : SClrTeal);
        g.FillPath(&aAddBrush, aAddP);
        delete aAddP;
        g.DrawString(L"+ Add", -1, &fBold, aAddRect, &fmtC, &brushWhite);

        // App Table
        RectF appTable(rightColX, secY + 115.0f, colW, 135.0f);
        g.FillRectangle(&brushBg, appTable);
        g.DrawRectangle(&penBorder, appTable.X, appTable.Y, appTable.Width, appTable.Height);

        g.GetClip(&oldClip);
        g.SetClip(appTable);
        float aItemY = appTable.Y + 5.0f - cAppScrollY;
        for (size_t i = 0; i < appList.size(); ++i) {
            if (aItemY > appTable.Y - 30.0f && aItemY < appTable.Y + appTable.Height) {
                g.DrawString(appList[i].name.c_str(), -1, &fNormal,
                    RectF(rightColX + 10.0f, aItemY, colW - 40.0f, 30.0f), &fmtL, &brushDark);
                if (!appList[i].isSystemLocked) {
                    SolidBrush crossBrush(isFocusActive ? SClrGrayText : (appList[i].isHoveredCross ? SClrRed : SClrGrayText));
                    g.DrawString(L"\xE711", -1, &fSmallIcon,
                        RectF(rightColX + colW - 30.0f, aItemY, 30.0f, 30.0f), &fmtC, &crossBrush);
                } else {
                    g.DrawString(L"\xE72E", -1, &fSmallIcon,
                        RectF(rightColX + colW - 30.0f, aItemY, 30.0f, 30.0f), &fmtC, &brushTeal);
                }
                g.DrawLine(&penBorder, rightColX + 5.0f, aItemY + 30.0f, rightColX + colW - 5.0f, aItemY + 30.0f);
            }
            aItemY += 30.0f;
        }
        g.SetClip(&oldClip);

        if (appList.size() * 30.0f > appTable.Height) {
            float maxScroll = appList.size() * 30.0f - appTable.Height + 10.0f;
            float thumbH = max(20.0f, (appTable.Height / (appList.size() * 30.0f)) * appTable.Height);
            float thumbY = appTable.Y + (cAppScrollY / maxScroll) * (appTable.Height - thumbH);
            g.FillRectangle(&brushGray, appTable.X + appTable.Width - 4.0f, thumbY, 4.0f, thumbH);
        }

        // --- Green Buttons ---
        float btnW = (colW - 20.0f) / 3.0f;
        float btnY = secY + 260.0f;
        SolidBrush greenBtn(hoverAddExe ? SClrGreenHover : SClrGreen);
        SolidBrush greenHoverStore(hoverAddStoreApp ? SClrGreenHover : SClrGreen);
        SolidBrush greenHoverTitle(hoverAddWindowTitle ? SClrGreenHover : SClrGreen);

        RectF b1(rightColX, btnY, btnW, 40.0f);
        GraphicsPath* p1 = GetBlockRoundRectPath(b1, 4); g.FillPath(&greenBtn, p1); delete p1;
        g.DrawString(L"Add Exe", -1, &fSmallerBold, b1, &fmtC, &brushWhite);

        RectF b2(rightColX + btnW + 10.0f, btnY, btnW, 40.0f);
        GraphicsPath* p2 = GetBlockRoundRectPath(b2, 4); g.FillPath(&greenHoverStore, p2); delete p2;
        g.DrawString(L"Add Store", -1, &fSmallerBold, b2, &fmtC, &brushWhite);

        RectF b3(rightColX + (btnW * 2) + 20.0f, btnY, btnW, 40.0f);
        GraphicsPath* p3 = GetBlockRoundRectPath(b3, 4); g.FillPath(&greenHoverTitle, p3); delete p3;
        g.DrawString(L"Add Title", -1, &fSmallerBold, b3, &fmtC, &brushWhite);

        // ==========================================
        // DROPDOWNS (drawn on top)
        // ==========================================
        if (isLangDropdownOpen && !isFocusActive) {
            RectF lDrop(langRect.X, langRect.Y + 26.0f, 100.0f, 50.0f);
            g.FillRectangle(&brushWhite, lDrop);
            g.DrawRectangle(&penBorder, lDrop.X, lDrop.Y, lDrop.Width, lDrop.Height);
            SolidBrush bBg(hoverOptBn ? SClrBgHover : SClrWhite);
            g.FillRectangle(&bBg, RectF(lDrop.X+1,lDrop.Y+1,lDrop.Width-2,24));
            g.DrawString(L"Bangla", -1, &fNormal, RectF(lDrop.X+5,lDrop.Y+1,lDrop.Width,24), &fmtL, &brushDark);
            SolidBrush eBg(hoverOptEn ? SClrBgHover : SClrWhite);
            g.FillRectangle(&eBg, RectF(lDrop.X+1,lDrop.Y+25,lDrop.Width-2,24));
            g.DrawString(L"English", -1, &fNormal, RectF(lDrop.X+5,lDrop.Y+25,lDrop.Width,24), &fmtL, &brushDark);
        }

        if (isWebComboOpen && !isFocusActive) {
            float listY = webComboY + 38.0f;
            RectF listRect(webComboX - 120.0f, listY, 150.0f, commonWebsites.size() * 30.0f + 10.0f);
            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);
            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;
            float iY = listY + 5.0f;
            for (size_t i = 0; i < commonWebsites.size(); ++i) {
                SolidBrush optBg(hoverWebOptIdx == (int)i ? SClrBgHover : SClrWhite);
                g.FillRectangle(&optBg, RectF(listRect.X+2,iY,listRect.Width-4,30));
                g.DrawString(commonWebsites[i].c_str(), -1, &fNormal, RectF(listRect.X+10,iY,listRect.Width,30), &fmtL, &brushDark);
                iY += 30.0f;
            }
        }

        if (isAppComboOpen && !isFocusActive) {
            float listY = secY + 108.0f;
            RectF listRect(aComboX - 120.0f, listY, 200.0f, min((int)commonApps.size(), 8) * 28.0f + 10.0f);
            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);
            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;
            float iY = listY + 5.0f;
            for (size_t i = 0; i < commonApps.size(); ++i) {
                bool isSystemEntry = (commonApps[i].find(L"[") != wstring::npos);
                SolidBrush optBg(hoverAppOptIdx == (int)i ? SClrBgHover : SClrWhite);
                g.FillRectangle(&optBg, RectF(listRect.X+2,iY,listRect.Width-4,28));
                SolidBrush optText(isSystemEntry ? SClrOrange : SClrDark);
                // Mark system-blocked entries in orange
                g.DrawString(commonApps[i].c_str(), -1, &fSmall, RectF(listRect.X+10,iY,listRect.Width-20,28), &fmtL, &optText);
                iY += 28.0f;
            }
        }

        if (isModeDropdownOpen && !isFocusActive) {
            float listY = modeDropY + 38.0f;
            RectF listRect(modeDropX, listY, 200.0f, 80.0f);
            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);
            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;
            SolidBrush opt1Bg(hoverOptAllow ? SClrBgHover : SClrWhite);
            g.FillRectangle(&opt1Bg, RectF(listRect.X+2,listY+2,listRect.Width-4,38));
            g.DrawString(L"Allow Apps & Web", -1, &fNormal, RectF(listRect.X+15,listY+2,listRect.Width,38), &fmtL, &brushDark);
            SolidBrush opt2Bg(hoverOptBlock ? SClrBgHover : SClrWhite);
            g.FillRectangle(&opt2Bg, RectF(listRect.X+2,listY+40,listRect.Width-4,38));
            g.DrawString(L"Block Apps & Web", -1, &fNormal, RectF(listRect.X+15,listY+40,listRect.Width,38), &fmtL, &brushDark);
        }

        if (isControlDropdownOpen && !isFocusActive) {
            float listY = ctrlDropY + 42.0f;
            RectF listRect(ctrlDropX, listY, 160.0f, 80.0f);
            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);
            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;
            SolidBrush opt1Bg(hoverOptSelf ? SClrBgHover : SClrWhite);
            g.FillRectangle(&opt1Bg, RectF(listRect.X+2,listY+2,listRect.Width-4,38));
            g.DrawString(L"Self Control", -1, &fBold, RectF(listRect.X+15,listY+2,listRect.Width,38), &fmtL, &brushDark);
            SolidBrush opt2Bg(hoverOptFriend ? SClrBgHover : SClrWhite);
            g.FillRectangle(&opt2Bg, RectF(listRect.X+2,listY+40,listRect.Width-4,38));
            g.DrawString(L"Friend Control", -1, &fBold, RectF(listRect.X+15,listY+40,listRect.Width,38), &fmtL, &brushDark);
        }

    } else if (currentBlockTab == 1) {
        DrawScheduleBlocksTab(g, boxX, bodyY + 20.0f, boxW, boxH);
    }

    // ==========================================
    // FULL SCREEN OVERLAYS — NO OVERLAP
    // ==========================================
    bool anyOverlay = showTimeOverlay || showPassOverlay || showStoreOverlay ||
                      showTitleOverlay || showUnlockChallenge || showSetPasswordOverlay;

    if (anyOverlay) {
        SolidBrush overlayBg(SClrOverlay);
        g.FillRectangle(&overlayBg, contentX, contentY, contentW, contentH);

        // ---- STORE OVERLAY ----
        if (showStoreOverlay) {
            float ovW = 500.0f, ovH = 450.0f;
            float ovX = contentX + (contentW - ovW) / 2.0f;
            float ovY = contentY + (contentH - ovH) / 2.0f;
            GraphicsPath* op = GetBlockRoundRectPath(RectF(ovX, ovY, ovW, ovH), 8);
            g.FillPath(&brushBg, op); g.DrawPath(&penBorder, op); delete op;

            g.DrawString(L"ADD APPS TO BLOCK LIST", -1, &fTitle,
                RectF(ovX, ovY + 18.0f, ovW, 30.0f), &fmtC, &brushDark);
            g.DrawString(L"Orange = always blocked system tools during focus",
                -1, &fTiny, RectF(ovX + 20, ovY + 50.0f, ovW - 40, 16), &fmtL, &brushOrange);
            g.DrawLine(&penBorder, ovX + 20.0f, ovY + 68.0f, ovX + ovW - 20.0f, ovY + 68.0f);

            RectF clipRect(ovX + 10.0f, ovY + 72.0f, ovW - 20.0f, ovH - 130.0f);
            Region oldClip2; g.GetClip(&oldClip2);
            g.SetClip(clipRect);

            float listY = ovY + 76.0f - cStoreScrollY;
            for (size_t i = 0; i < systemStoreApps.size(); ++i) {
                if (listY > ovY + 65.0f && listY < ovY + ovH - 60.0f) {
                    bool isSys = (systemStoreApps[i].find(L"[") != wstring::npos);
                    RectF addBtn(ovX + 28.0f, listY + 4.0f, 60.0f, 30.0f);
                    GraphicsPath* ap2 = GetBlockRoundRectPath(addBtn, 4);
                    SolidBrush aBr(hoverStoreAddIdx == (int)i ? SClrGreenHover : SClrGreen);
                    g.FillPath(&aBr, ap2); delete ap2;
                    g.DrawString(L"Add", -1, &fBold, addBtn, &fmtC, &brushWhite);
                    SolidBrush nameBrush(isSys ? SClrOrange : SClrDark);
                    g.DrawString(systemStoreApps[i].c_str(), -1, &fNormal,
                        RectF(ovX + 100.0f, listY, 340.0f, 40.0f), &fmtL, &nameBrush);
                }
                listY += 42.0f;
            }
            g.SetClip(&oldClip2);

            float totalH2 = systemStoreApps.size() * 42.0f;
            float visibleH2 = ovH - 130.0f;
            if (totalH2 > visibleH2) {
                float maxScroll2 = totalH2 - visibleH2;
                float thumbH2 = max(20.0f, (visibleH2 / totalH2) * visibleH2);
                float thumbY2 = ovY + 76.0f + (cStoreScrollY / maxScroll2) * (visibleH2 - thumbH2);
                g.FillRectangle(&brushGray, ovX + ovW - 10.0f, thumbY2, 4.0f, thumbH2);
            }

            RectF closeBtn(ovX + ovW - 120.0f, ovY + ovH - 50.0f, 90.0f, 35.0f);
            GraphicsPath* cp = GetBlockRoundRectPath(closeBtn, 4);
            SolidBrush cBr(hoverStoreClose ? SClrTealHover : SClrTeal);
            g.FillPath(&cBr, cp); delete cp;
            g.DrawString(L"Close", -1, &fBold, closeBtn, &fmtC, &brushWhite);
        }

        // ---- TITLE OVERLAY ----
        else if (showTitleOverlay) {
            float ovW = 400.0f, ovH = 220.0f;
            float ovX = contentX + (contentW - ovW) / 2.0f;
            float ovY = contentY + (contentH - ovH) / 2.0f;
            GraphicsPath* op = GetBlockRoundRectPath(RectF(ovX, ovY, ovW, ovH), 8);
            g.FillPath(&brushBg, op); g.DrawPath(&penBorder, op); delete op;

            g.DrawString(L"ENTER WINDOW TITLE", -1, &fTitle,
                RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fmtC, &brushDark);
            RectF titleInpRect(ovX + 40.0f, ovY + 80.0f, ovW - 80.0f, 40.0f);
            GraphicsPath* pp = GetBlockRoundRectPath(titleInpRect, 4);
            Pen pTealTitle(SClrTeal, 2.0f);
            g.FillPath(&brushWhite, pp);
            g.DrawPath(isTitleInputActive ? &pTealTitle : &penBorder, pp);
            delete pp;
            if (inputTitleText.empty() && !isTitleInputActive)
                g.DrawString(L"e.g. Google Chrome", -1, &fNormal, titleInpRect, &fmtC, &brushGray);
            else {
                g.DrawString(inputTitleText.c_str(), -1, &fNormal,
                    RectF(ovX + 50.0f, ovY + 85.0f, ovW - 100.0f, 30.0f), &fmtL, &brushDark);
                if (isTitleInputActive && (GetTickCount() / 500) % 2 == 0) {
                    Graphics gTemp(GetDesktopWindow()); RectF bRect;
                    gTemp.MeasureString(inputTitleText.c_str(), -1, &fNormal, PointF(0,0), &bRect);
                    float cursorX = ovX + 52.0f + (inputTitleText.empty() ? 0 : bRect.Width);
                    g.FillRectangle(&brushDark, cursorX, ovY + 90.0f, 1.5f, 20.0f);
                }
            }
            RectF cancelRect(ovX + 40.0f, ovY + 150.0f, 140.0f, 40.0f);
            GraphicsPath* cp2 = GetBlockRoundRectPath(cancelRect, 4);
            SolidBrush cancelBrush(hTitleCancel ? SClrBgHover : SClrWhite);
            g.FillPath(&cancelBrush, cp2); g.DrawPath(&penBorder, cp2); delete cp2;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fmtC, &brushDark);

            RectF confRect(ovX + 200.0f, ovY + 150.0f, 160.0f, 40.0f);
            GraphicsPath* sp = GetBlockRoundRectPath(confRect, 4);
            SolidBrush confBrush(hTitleAdd ? SClrTealHover : SClrTeal);
            g.FillPath(&confBrush, sp); delete sp;
            g.DrawString(L"Add Title", -1, &fBold, confRect, &fmtC, &brushWhite);
        }

        // ---- SELF CONTROL TIME OVERLAY ----
        else if (showTimeOverlay) {
            float ovW = 440.0f, ovH = 260.0f;
            float ovX = contentX + (contentW - ovW) / 2.0f;
            float ovY = contentY + (contentH - ovH) / 2.0f;
            GraphicsPath* op = GetBlockRoundRectPath(RectF(ovX, ovY, ovW, ovH), 8);
            g.FillPath(&brushBg, op); g.DrawPath(&penBorder, op); delete op;

            g.DrawString(L"SET SELF CONTROL DURATION", -1, &fTitle,
                RectF(ovX, ovY + 16.0f, ovW, 30.0f), &fmtC, &brushDark);

            // Tab buttons: Days/Hours | Months
            RectF tabDH(ovX + 30.0f, ovY + 52.0f, 170.0f, 30.0f);
            RectF tabMon(ovX + 210.0f, ovY + 52.0f, 170.0f, 30.0f);
            GraphicsPath* ptDH = GetBlockRoundRectPath(tabDH, 4);
            GraphicsPath* ptMon = GetBlockRoundRectPath(tabMon, 4);
            SolidBrush bDH(selfDurationTab == 0 ? SClrTeal : (hoverSelfTabDays ? SClrBgHover : SClrWhite));
            SolidBrush bMon(selfDurationTab == 1 ? SClrTeal : (hoverSelfTabMonths ? SClrBgHover : SClrWhite));
            g.FillPath(&bDH,  ptDH);  g.DrawPath(&penBorder, ptDH);  delete ptDH;
            g.FillPath(&bMon, ptMon); g.DrawPath(&penBorder, ptMon); delete ptMon;
            SolidBrush tDH(selfDurationTab == 0 ? SClrWhite : SClrDark);
            SolidBrush tMon(selfDurationTab == 1 ? SClrWhite : SClrDark);
            g.DrawString(L"Days / Hours", -1, &fBold, tabDH,  &fmtC, &tDH);
            g.DrawString(L"Months",       -1, &fBold, tabMon, &fmtC, &tMon);

            if (selfDurationTab == 0) {
                // Days spinner
                g.DrawString(L"Days:", -1, &fBold,
                    RectF(ovX + 20.0f, ovY + 102.0f, 60.0f, 36.0f), &fmtL, &brushDark);
                DrawBlocksOverlaySpinner(g, ovX + 80.0f, ovY + 102.0f,
                    to_wstring(selfDurationDays), hTimeDM, hTimeDP, &fIcon, &fBold);
                // Hours spinner
                g.DrawString(L"Hours:", -1, &fBold,
                    RectF(ovX + 20.0f, ovY + 148.0f, 60.0f, 36.0f), &fmtL, &brushDark);
                DrawBlocksOverlaySpinner(g, ovX + 80.0f, ovY + 148.0f,
                    to_wstring(selfDurationHours), hTimeHM, hTimeHP, &fIcon, &fBold);
                // Mins spinner
                g.DrawString(L"Mins:", -1, &fBold,
                    RectF(ovX + 240.0f, ovY + 148.0f, 50.0f, 36.0f), &fmtL, &brushDark);
                DrawBlocksOverlaySpinner(g, ovX + 292.0f, ovY + 148.0f,
                    to_wstring(selfDurationMins), hTimeMM, hTimeMP, &fIcon, &fBold);
            } else {
                // Months spinner
                g.DrawString(L"Months:", -1, &fBold,
                    RectF(ovX + 20.0f, ovY + 120.0f, 80.0f, 36.0f), &fmtL, &brushDark);
                DrawBlocksOverlaySpinner(g, ovX + 105.0f, ovY + 120.0f,
                    to_wstring(selfDurationMonths), hTimeMonM, hTimeMonP, &fIcon, &fBold);
            }

            // Duration summary
            wstring summary = L"Duration: ";
            if (selfDurationTab == 0) {
                if (selfDurationDays > 0) summary += to_wstring(selfDurationDays) + L"d ";
                summary += to_wstring(selfDurationHours) + L"h " + to_wstring(selfDurationMins) + L"m";
            } else {
                summary += to_wstring(selfDurationMonths) + L" month(s)";
            }
            g.DrawString(summary.c_str(), -1, &fSmall,
                RectF(ovX, ovY + 196.0f, ovW, 18.0f), &fmtC, &brushGray);

            RectF cancelRect(ovX + 30.0f, ovY + 218.0f, 140.0f, 34.0f);
            GraphicsPath* cp3 = GetBlockRoundRectPath(cancelRect, 4);
            SolidBrush cancelBrush(hTimeCancel ? SClrBgHover : SClrWhite);
            g.FillPath(&cancelBrush, cp3); g.DrawPath(&penBorder, cp3); delete cp3;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fmtC, &brushDark);

            RectF startRect(ovX + 190.0f, ovY + 218.0f, 210.0f, 34.0f);
            GraphicsPath* sp2 = GetBlockRoundRectPath(startRect, 4);
            SolidBrush startBrush(hTimeStart ? SClrTealHover : SClrTeal);
            g.FillPath(&startBrush, sp2); delete sp2;
            g.DrawString(L"Start Focus (Self)", -1, &fBold, startRect, &fmtC, &brushWhite);
        }

        // ---- SET FRIEND PASSWORD OVERLAY ----
        else if (showSetPasswordOverlay) {
            float ovW = 400.0f, ovH = 280.0f;
            float ovX = contentX + (contentW - ovW) / 2.0f;
            float ovY = contentY + (contentH - ovH) / 2.0f;
            GraphicsPath* op = GetBlockRoundRectPath(RectF(ovX, ovY, ovW, ovH), 8);
            g.FillPath(&brushBg, op); g.DrawPath(&penBorder, op); delete op;

            g.DrawString(L"SET FRIEND PASSWORD", -1, &fTitle,
                RectF(ovX, ovY + 18.0f, ovW, 30.0f), &fmtC, &brushDark);
            g.DrawString(L"Your friend will use this to lock/unlock focus.",
                -1, &fSmall, RectF(ovX + 20, ovY + 50, ovW - 40, 20), &fmtC, &brushGray);

            // Password 1
            RectF p1Rect(ovX + 30.0f, ovY + 80.0f, ovW - 60.0f, 38.0f);
            GraphicsPath* pp1 = GetBlockRoundRectPath(p1Rect, 4);
            Pen pTeal2(SClrTeal, 2.0f);
            g.FillPath(&brushWhite, pp1);
            g.DrawPath(isNewPass1Active ? &pTeal2 : &penBorder, pp1); delete pp1;
            wstring dp1 = wstring(inputNewPass1.length(), L'*');
            if (inputNewPass1.empty() && !isNewPass1Active)
                g.DrawString(L"Enter password", -1, &fNormal, p1Rect, &fmtC, &brushGray);
            else
                g.DrawString(dp1.c_str(), -1, &fBold,
                    RectF(p1Rect.X + 10, p1Rect.Y, p1Rect.Width - 20, p1Rect.Height),
                    &fmtL, &brushDark);

            // Password 2 (confirm)
            RectF p2Rect(ovX + 30.0f, ovY + 128.0f, ovW - 60.0f, 38.0f);
            GraphicsPath* pp2 = GetBlockRoundRectPath(p2Rect, 4);
            g.FillPath(&brushWhite, pp2);
            g.DrawPath(isNewPass2Active ? &pTeal2 : &penBorder, pp2); delete pp2;
            wstring dp2 = wstring(inputNewPass2.length(), L'*');
            if (inputNewPass2.empty() && !isNewPass2Active)
                g.DrawString(L"Confirm password", -1, &fNormal, p2Rect, &fmtC, &brushGray);
            else
                g.DrawString(dp2.c_str(), -1, &fBold,
                    RectF(p2Rect.X + 10, p2Rect.Y, p2Rect.Width - 20, p2Rect.Height),
                    &fmtL, &brushDark);

            if (newPassMismatch)
                g.DrawString(L"Passwords do not match!", -1, &fSmall,
                    RectF(ovX, ovY + 173.0f, ovW, 18.0f), &fmtC, &brushRed);

            RectF cancelR(ovX + 30.0f, ovY + 200.0f, 140.0f, 40.0f);
            GraphicsPath* cpx = GetBlockRoundRectPath(cancelR, 4);
            SolidBrush cancelB(hSetPassCancel ? SClrBgHover : SClrWhite);
            g.FillPath(&cancelB, cpx); g.DrawPath(&penBorder, cpx); delete cpx;
            g.DrawString(L"Cancel", -1, &fBold, cancelR, &fmtC, &brushDark);

            RectF confR(ovX + 200.0f, ovY + 200.0f, 160.0f, 40.0f);
            GraphicsPath* spx = GetBlockRoundRectPath(confR, 4);
            SolidBrush confB(hSetPassConfirm ? SClrGreenHover : SClrGreen);
            g.FillPath(&confB, spx); delete spx;
            g.DrawString(L"Set & Start Focus", -1, &fSmallerBold, confR, &fmtC, &brushWhite);
        }

        // ---- PASSWORD ENTRY OVERLAY ----
        else if (showPassOverlay) {
            float ovW = 400.0f, ovH = 240.0f;
            float ovX = contentX + (contentW - ovW) / 2.0f;
            float ovY = contentY + (contentH - ovH) / 2.0f;
            GraphicsPath* op = GetBlockRoundRectPath(RectF(ovX, ovY, ovW, ovH), 8);
            g.FillPath(&brushBg, op); g.DrawPath(&penBorder, op); delete op;

            wstring titleTxt = isStoppingFocus ? L"ENTER FRIEND'S PASSWORD" : L"FRIEND SETS PASSWORD";
            g.DrawString(titleTxt.c_str(), -1, &fTitle,
                RectF(ovX, ovY + 18.0f, ovW, 30.0f), &fmtC, &brushDark);

            // Sub-message
            wstring subMsg = isStoppingFocus
                ? L"Password needed to stop focus session"
                : L"Friend must enter password to start locking";
            g.DrawString(subMsg.c_str(), -1, &fSmall,
                RectF(ovX + 20, ovY + 52.0f, ovW - 40, 18.0f), &fmtC, &brushGray);

            RectF passInpRect(ovX + 40.0f, ovY + 82.0f, ovW - 80.0f, 40.0f);
            GraphicsPath* pp = GetBlockRoundRectPath(passInpRect, 4);
            Pen pTealPass(SClrTeal, 2.0f);
            g.FillPath(&brushWhite, pp);
            g.DrawPath(isPassInputActive ? &pTealPass : &penBorder, pp);
            delete pp;

            wstring displayPass = wstring(inputPassText.length(), L'*');
            if (inputPassText.empty() && !isPassInputActive)
                g.DrawString(L"Type password here...", -1, &fNormal, passInpRect, &fmtC, &brushGray);
            else {
                g.DrawString(displayPass.c_str(), -1, &fTitle,
                    RectF(ovX + 50.0f, ovY + 87.0f, ovW - 100.0f, 30.0f), &fmtL, &brushDark);
                if (isPassInputActive && (GetTickCount() / 500) % 2 == 0) {
                    Graphics gTemp(GetDesktopWindow()); RectF bRect;
                    gTemp.MeasureString(displayPass.c_str(), -1, &fTitle, PointF(0,0), &bRect);
                    float cursorX = ovX + 52.0f + (displayPass.empty() ? 0 : bRect.Width);
                    g.FillRectangle(&brushDark, cursorX, ovY + 92.0f, 1.5f, 20.0f);
                }
            }

            RectF cancelRect(ovX + 40.0f, ovY + 155.0f, 140.0f, 40.0f);
            GraphicsPath* cp4 = GetBlockRoundRectPath(cancelRect, 4);
            SolidBrush cancelBrush2(hPassCancel ? SClrBgHover : SClrWhite);
            g.FillPath(&cancelBrush2, cp4); g.DrawPath(&penBorder, cp4); delete cp4;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fmtC, &brushDark);

            RectF confRect(ovX + 200.0f, ovY + 155.0f, 160.0f, 40.0f);
            GraphicsPath* sp3 = GetBlockRoundRectPath(confRect, 4);
            SolidBrush confBrush2(hPassConfirm ? SClrTealHover : SClrTeal);
            g.FillPath(&confBrush2, sp3); delete sp3;
            g.DrawString(L"Confirm", -1, &fBold, confRect, &fmtC, &brushWhite);
        }

        // ---- UNLOCK CHALLENGE (200-word text, NO copy-paste) ----
        else if (showUnlockChallenge) {
            float ovW = 560.0f, ovH = 360.0f;
            float ovX = contentX + (contentW - ovW) / 2.0f;
            float ovY = contentY + (contentH - ovH) / 2.0f;
            GraphicsPath* op = GetBlockRoundRectPath(RectF(ovX, ovY, ovW, ovH), 8);
            g.FillPath(&brushBg, op); g.DrawPath(&penBorder, op); delete op;

            // Red warning header
            SolidBrush redHeaderBg(Color(255, 200, 40, 40));
            g.FillRectangle(&redHeaderBg, ovX, ovY, ovW, 48.0f);
            g.DrawString(L"⚠ FOCUS UNLOCK CHALLENGE ⚠", -1, &fBold,
                RectF(ovX, ovY + 10.0f, ovW, 30.0f), &fmtC, &brushWhite);

            // Instruction
            g.DrawString(L"Read the text below carefully. You cannot copy it. Type it to proceed:",
                -1, &fSmall, RectF(ovX + 20, ovY + 56.0f, ovW - 40, 20.0f), &fmtL, &brushGray);

            // Challenge text box (PROTECTED — no copy, selectable for display only)
            RectF textBox(ovX + 20.0f, ovY + 80.0f, ovW - 40.0f, 160.0f);
            SolidBrush textBoxBg(Color(255, 245, 245, 240));
            g.FillRectangle(&textBoxBg, textBox);
            g.DrawRectangle(&penBorder, textBox.X, textBox.Y, textBox.Width, textBox.Height);

            // Draw text with word-wrap simulation (manual)
            StringFormat sfWrap;
            sfWrap.SetAlignment(StringAlignmentNear);
            sfWrap.SetLineAlignment(StringAlignmentNear);
            sfWrap.SetTrimming(StringTrimmingWord);
            g.DrawString(UNLOCK_CHALLENGE_TEXT.c_str(), -1, &fSmall,
                RectF(textBox.X + 8, textBox.Y + 6, textBox.Width - 16, textBox.Height - 12),
                &sfWrap, &brushDark);

            // User type-in box
            g.DrawString(L"Type any 5 words from the text above to unlock:",
                -1, &fSmall, RectF(ovX + 20, ovY + 248.0f, ovW - 40, 18.0f), &fmtL, &brushGray);

            RectF typeRect(ovX + 20.0f, ovY + 268.0f, ovW - 40.0f, 36.0f);
            GraphicsPath* tpp = GetBlockRoundRectPath(typeRect, 4);
            Pen pTealC(SClrTeal, 2.0f);
            g.FillPath(&brushWhite, tpp);
            g.DrawPath(isChallengeTypingActive ? &pTealC : &penBorder, tpp);
            delete tpp;
            if (challengeUserType.empty() && !isChallengeTypingActive)
                g.DrawString(L"Type here...", -1, &fNormal, typeRect, &fmtC, &brushGray);
            else
                g.DrawString(challengeUserType.c_str(), -1, &fNormal,
                    RectF(typeRect.X + 10, typeRect.Y, typeRect.Width - 20, typeRect.Height),
                    &fmtL, &brushDark);

            RectF cancelR2(ovX + 20.0f, ovY + 314.0f, 130.0f, 36.0f);
            GraphicsPath* cpr2 = GetBlockRoundRectPath(cancelR2, 4);
            SolidBrush cancelB2(hChallengeCancel ? SClrBgHover : SClrWhite);
            g.FillPath(&cancelB2, cpr2); g.DrawPath(&penBorder, cpr2); delete cpr2;
            g.DrawString(L"Cancel", -1, &fBold, cancelR2, &fmtC, &brushDark);

            RectF proceedR(ovX + 170.0f, ovY + 314.0f, 350.0f, 36.0f);
            GraphicsPath* spr2 = GetBlockRoundRectPath(proceedR, 4);
            SolidBrush proceedB(hChallengeProceed ? SClrRed : Color(255, 180, 30, 30));
            g.FillPath(&proceedB, spr2); delete spr2;
            g.DrawString(L"I've Read It — Stop Focus", -1, &fBold, proceedR, &fmtC, &brushWhite);
        }
    }
}

// ==========================================
// MOUSE MOVE
// ==========================================
void ProcessBlocksMouseMove(float x, float y) {
    float contentX = s_contentX, contentY = s_contentY;
    float contentW = s_contentW, contentH = s_contentH;

    // Reset all hover states
    hTimeHM=false; hTimeHP=false; hTimeMM=false; hTimeMP=false;
    hTimeDM=false; hTimeDP=false; hTimeMonM=false; hTimeMonP=false;
    hTimeStart=false; hTimeCancel=false;
    hoverSelfTabDays=false; hoverSelfTabMonths=false;
    hPassInput=false; hPassConfirm=false; hPassCancel=false;
    hoverStoreClose=false; hoverStoreAddIdx=-1;
    hTitleInput=false; hTitleAdd=false; hTitleCancel=false;
    hoverControlDropdown=false; hoverModeDropdown=false;
    hoverWebCombo=false; hoverAppCombo=false;
    hoverLangDropdown=false; hoverOptBn=false; hoverOptEn=false;
    hoverQuotesCheckbox=false; hoverWebInput=false; hoverWebAddBtn=false;
    hoverAppInput=false; hoverAppAddBtn=false;
    hoverAddExe=false; hoverAddStoreApp=false; hoverAddWindowTitle=false;
    hoverAppOptIdx=-1; hoverWebOptIdx=-1;
    hoverOptSelf=false; hoverOptFriend=false;
    hoverOptAllow=false; hoverOptBlock=false;
    hoverStartFocusBtn=false;
    hNewPass1=false; hNewPass2=false;
    hSetPassConfirm=false; hSetPassCancel=false;
    hChallengeProceed=false; hChallengeCancel=false;
    for (auto& item : webList) item.isHoveredCross = false;
    for (auto& item : appList) item.isHoveredCross = false;

    bool anyOverlay = showTimeOverlay || showPassOverlay || showStoreOverlay ||
                      showTitleOverlay || showUnlockChallenge || showSetPasswordOverlay;

    if (anyOverlay) {
        if (showStoreOverlay) {
            float ovW=500.0f, ovH=450.0f;
            float ovX=contentX+(contentW-ovW)/2.0f, ovY=contentY+(contentH-ovH)/2.0f;
            float listY = ovY + 76.0f - cStoreScrollY;
            for (size_t i = 0; i < systemStoreApps.size(); ++i) {
                if (listY > ovY + 65.0f && listY < ovY + ovH - 60.0f)
                    if (RectF(ovX+28.0f,listY+4.0f,60.0f,30.0f).Contains(x,y)) hoverStoreAddIdx=(int)i;
                listY += 42.0f;
            }
            if (RectF(ovX+ovW-120.0f,ovY+ovH-50.0f,90.0f,35.0f).Contains(x,y)) hoverStoreClose=true;
        }
        else if (showTitleOverlay) {
            float ovW=400.0f,ovH=220.0f;
            float ovX=contentX+(contentW-ovW)/2.0f, ovY=contentY+(contentH-ovH)/2.0f;
            if (RectF(ovX+40,ovY+80,ovW-80,40).Contains(x,y)) hTitleInput=true;
            if (RectF(ovX+40,ovY+150,140,40).Contains(x,y)) hTitleCancel=true;
            if (RectF(ovX+200,ovY+150,160,40).Contains(x,y)) hTitleAdd=true;
        }
        else if (showTimeOverlay) {
            float ovW=440.0f,ovH=260.0f;
            float ovX=contentX+(contentW-ovW)/2.0f, ovY=contentY+(contentH-ovH)/2.0f;
            if (RectF(ovX+30,ovY+52,170,30).Contains(x,y)) hoverSelfTabDays=true;
            if (RectF(ovX+210,ovY+52,170,30).Contains(x,y)) hoverSelfTabMonths=true;
            if (selfDurationTab==0) {
                if (RectF(ovX+80,ovY+102,32,36).Contains(x,y)) hTimeDM=true;
                if (RectF(ovX+80+92,ovY+102,32,36).Contains(x,y)) hTimeDP=true;
                if (RectF(ovX+80,ovY+148,32,36).Contains(x,y)) hTimeHM=true;
                if (RectF(ovX+80+92,ovY+148,32,36).Contains(x,y)) hTimeHP=true;
                if (RectF(ovX+292,ovY+148,32,36).Contains(x,y)) hTimeMM=true;
                if (RectF(ovX+292+92,ovY+148,32,36).Contains(x,y)) hTimeMP=true;
            } else {
                if (RectF(ovX+105,ovY+120,32,36).Contains(x,y)) hTimeMonM=true;
                if (RectF(ovX+105+92,ovY+120,32,36).Contains(x,y)) hTimeMonP=true;
            }
            if (RectF(ovX+30,ovY+218,140,34).Contains(x,y)) hTimeCancel=true;
            if (RectF(ovX+190,ovY+218,210,34).Contains(x,y)) hTimeStart=true;
        }
        else if (showSetPasswordOverlay) {
            float ovW=400.0f,ovH=280.0f;
            float ovX=contentX+(contentW-ovW)/2.0f, ovY=contentY+(contentH-ovH)/2.0f;
            if (RectF(ovX+30,ovY+80,ovW-60,38).Contains(x,y)) hNewPass1=true;
            if (RectF(ovX+30,ovY+128,ovW-60,38).Contains(x,y)) hNewPass2=true;
            if (RectF(ovX+30,ovY+200,140,40).Contains(x,y)) hSetPassCancel=true;
            if (RectF(ovX+200,ovY+200,160,40).Contains(x,y)) hSetPassConfirm=true;
        }
        else if (showPassOverlay) {
            float ovW=400.0f,ovH=240.0f;
            float ovX=contentX+(contentW-ovW)/2.0f, ovY=contentY+(contentH-ovH)/2.0f;
            if (RectF(ovX+40,ovY+82,ovW-80,40).Contains(x,y)) hPassInput=true;
            if (RectF(ovX+40,ovY+155,140,40).Contains(x,y)) hPassCancel=true;
            if (RectF(ovX+200,ovY+155,160,40).Contains(x,y)) hPassConfirm=true;
        }
        else if (showUnlockChallenge) {
            float ovW=560.0f,ovH=360.0f;
            float ovX=contentX+(contentW-ovW)/2.0f, ovY=contentY+(contentH-ovH)/2.0f;
            if (RectF(ovX+20,ovY+268,ovW-40,36).Contains(x,y)) isChallengeTypingActive=true;
            if (RectF(ovX+20,ovY+314,130,36).Contains(x,y)) hChallengeCancel=true;
            if (RectF(ovX+170,ovY+314,350,36).Contains(x,y)) hChallengeProceed=true;
        }
        return;
    }

    // Tab hover
    hoverBlockTab = -1;
    float tabW=200.0f,tabH=40.0f;
    float tab1X=contentX+20.0f,tab2X=tab1X+tabW+10.0f,tab3X=tab2X+tabW+10.0f,tabY=contentY+10.0f;
    if (y>=contentY && y<=contentY+60.0f) {
        if (RectF(tab1X,tabY,tabW,tabH).Contains(x,y)) hoverBlockTab=0;
        else if (RectF(tab2X,tabY,tabW,tabH).Contains(x,y)) hoverBlockTab=1;
        else if (RectF(tab3X,tabY,tabW,tabH).Contains(x,y)) hoverBlockTab=2;
    }

    if (currentBlockTab == 0) {
        float bodyY=contentY+60.0f;
        float boxX=contentX+30.0f;
        float boxW=contentW-60.0f;
        float ctrlDropX=boxX+30.0f, ctrlDropY=bodyY+40.0f;
        float modeDropX=boxX+150.0f, modeDropY=ctrlDropY+75.0f;
        float webComboX2=boxX+30.0f+((boxW-90.0f)/2.0f)-105.0f;
        float webComboY2=modeDropY+95.0f;
        float colW=(boxW-90.0f)/2.0f;
        float leftColX=boxX+30.0f, rightColX=boxX+60.0f+colW;
        float secY=modeDropY+50.0f;
        float qY=secY;
        float aComboX=rightColX+colW-105.0f;

        // Dropdown hover checking (first, before other checks)
        if (isLangDropdownOpen && !isFocusActive) {
            RectF lDrop(rightColX+185.0f, qY+24.0f, 100.0f, 50.0f);
            if (RectF(lDrop.X,lDrop.Y,lDrop.Width,24).Contains(x,y)) { hoverOptBn=true; return; }
            if (RectF(lDrop.X,lDrop.Y+25,lDrop.Width,24).Contains(x,y)) { hoverOptEn=true; return; }
        }
        if (isControlDropdownOpen && !isFocusActive) {
            float listY=ctrlDropY+42.0f;
            if (RectF(ctrlDropX+2,listY+2,156,38).Contains(x,y)) { hoverOptSelf=true; return; }
            if (RectF(ctrlDropX+2,listY+40,156,38).Contains(x,y)) { hoverOptFriend=true; return; }
        }
        if (isModeDropdownOpen && !isFocusActive) {
            float listY=modeDropY+38.0f;
            if (RectF(modeDropX+2,listY+2,196,38).Contains(x,y)) { hoverOptAllow=true; return; }
            if (RectF(modeDropX+2,listY+40,196,38).Contains(x,y)) { hoverOptBlock=true; return; }
        }
        if (isWebComboOpen && !isFocusActive) {
            float listY=webComboY2+38.0f, iY=listY+5.0f;
            for (size_t i=0;i<commonWebsites.size();++i) {
                if (RectF(webComboX2-120+2,iY,146,30).Contains(x,y)) { hoverWebOptIdx=(int)i; return; }
                iY+=30.0f;
            }
        }
        if (isAppComboOpen && !isFocusActive) {
            float listY=secY+108.0f, iY=listY+5.0f;
            for (size_t i=0;i<commonApps.size();++i) {
                if (RectF(aComboX-120+2,iY,196,28).Contains(x,y)) { hoverAppOptIdx=(int)i; return; }
                iY+=28.0f;
            }
        }

        // Start button
        if (!(isFocusActive && controlMode==0 && std::time(nullptr)<focusEndTimeBlocks)) {
            if (RectF(boxX+175.0f,ctrlDropY,170.0f,40.0f).Contains(x,y)) hoverStartFocusBtn=true;
        }

        if (!isFocusActive) {
            if (RectF(ctrlDropX,ctrlDropY,160,40).Contains(x,y)) hoverControlDropdown=true;
            if (RectF(modeDropX,modeDropY,200,36).Contains(x,y)) hoverModeDropdown=true;
            if (RectF(rightColX,qY+2,150,20).Contains(x,y)) hoverQuotesCheckbox=true;
            if (RectF(rightColX+185,qY-2,100,24).Contains(x,y)) hoverLangDropdown=true;
            if (RectF(webComboX2,secY+45,30,36).Contains(x,y)) hoverWebCombo=true;
            if (RectF(aComboX,secY+70,30,36).Contains(x,y)) hoverAppCombo=true;
        }

        if (RectF(leftColX,secY+45,colW-110,36).Contains(x,y)) hoverWebInput=true;
        if (RectF(leftColX+colW-70,secY+45,70,36).Contains(x,y)) hoverWebAddBtn=true;
        if (RectF(rightColX,secY+70,colW-110,36).Contains(x,y)) hoverAppInput=true;
        if (RectF(rightColX+colW-70,secY+70,70,36).Contains(x,y)) hoverAppAddBtn=true;

        float btnW=(colW-20.0f)/3.0f, btnY=secY+260.0f;
        if (RectF(rightColX,btnY,btnW,40).Contains(x,y)) hoverAddExe=true;
        if (RectF(rightColX+btnW+10,btnY,btnW,40).Contains(x,y)) hoverAddStoreApp=true;
        if (RectF(rightColX+(btnW*2)+20,btnY,btnW,40).Contains(x,y)) hoverAddWindowTitle=true;

        if (!isFocusActive) {
            float iY=secY+90.0f+5.0f-cWebScrollY;
            for (size_t i=0;i<webList.size();++i) {
                if (iY>secY+90.0f-30 && iY<secY+90.0f+160)
                    if (RectF(leftColX+colW-30,iY,30,30).Contains(x,y)) webList[i].isHoveredCross=true;
                iY+=30.0f;
            }
            float aIY=secY+115.0f+5.0f-cAppScrollY;
            for (size_t i=0;i<appList.size();++i) {
                if (!appList[i].isSystemLocked && aIY>secY+115.0f-30 && aIY<secY+115.0f+135)
                    if (RectF(rightColX+colW-30,aIY,30,30).Contains(x,y)) appList[i].isHoveredCross=true;
                aIY+=30.0f;
            }
        }
    } else if (currentBlockTab == 1) {
        ProcessScheduleBlocksMouseMove(x, y);
    }
}

// ==========================================
// MOUSE CLICK
// ==========================================
void ProcessBlocksMouseClick(float x, float y) {
    // ---- STORE ----
    if (showStoreOverlay) {
        if (hoverStoreAddIdx != -1 && hoverStoreAddIdx < (int)systemStoreApps.size()) {
            wstring name = systemStoreApps[hoverStoreAddIdx];
            if (name != L"No active apps found") {
                appList.push_back({name, false, false});
                SaveBlocksData();
            }
        }
        if (hoverStoreClose) showStoreOverlay = false;
        return;
    }

    // ---- TITLE ----
    if (showTitleOverlay) {
        isTitleInputActive = hTitleInput;
        if (hTitleCancel) { showTitleOverlay=false; inputTitleText=L""; }
        if (hTitleAdd && !inputTitleText.empty()) {
            appList.push_back({inputTitleText + L" (Window)", false, false});
            SaveBlocksData();
            showTitleOverlay=false; inputTitleText=L"";
        }
        return;
    }

    // ---- SELF CONTROL TIME ----
    if (showTimeOverlay) {
        // Tab switching
        if (hoverSelfTabDays)   { selfDurationTab=0; return; }
        if (hoverSelfTabMonths) { selfDurationTab=1; return; }

        if (selfDurationTab==0) {
            if (hTimeDM && selfDurationDays>0)  selfDurationDays--;
            if (hTimeDP && selfDurationDays<365) selfDurationDays++;
            if (hTimeHM && selfDurationHours>0) selfDurationHours--;
            if (hTimeHP && selfDurationHours<23) selfDurationHours++;
            if (hTimeMM) { selfDurationMins-=5; if(selfDurationMins<0) selfDurationMins=55; }
            if (hTimeMP) { selfDurationMins=(selfDurationMins+5)%60; }
        } else {
            if (hTimeMonM && selfDurationMonths>0) selfDurationMonths--;
            if (hTimeMonP && selfDurationMonths<24) selfDurationMonths++;
        }

        if (hTimeCancel) showTimeOverlay=false;
        if (hTimeStart) {
            // Calculate total seconds
            time_t totalSecs = 0;
            if (selfDurationTab==0) {
                totalSecs = (time_t)(selfDurationDays * 86400)
                          + (time_t)(selfDurationHours * 3600)
                          + (time_t)(selfDurationMins * 60);
            } else {
                totalSecs = (time_t)(selfDurationMonths * 30 * 86400);
            }
            if (totalSecs > 0) {
                isFocusActive = true;
                focusEndTimeBlocks = std::time(nullptr) + totalSecs;
                showTimeOverlay = false;
                CreateLiveTimeWindow();
                SaveBlocksData();
            }
        }
        return;
    }

    // ---- SET FRIEND PASSWORD ----
    if (showSetPasswordOverlay) {
        if (hNewPass1) { isNewPass1Active=true; isNewPass2Active=false; }
        if (hNewPass2) { isNewPass1Active=false; isNewPass2Active=true; }
        if (hSetPassCancel) {
            showSetPasswordOverlay=false;
            inputNewPass1=L""; inputNewPass2=L"";
            newPassMismatch=false;
        }
        if (hSetPassConfirm) {
            if (inputNewPass1.empty()) return;
            if (inputNewPass1 != inputNewPass2) {
                newPassMismatch = true;
                inputNewPass2 = L"";
                return;
            }
            friendPassword = inputNewPass1;
            isFriendPasswordSet = true;
            isFocusActive = true;
            showSetPasswordOverlay = false;
            inputNewPass1=L""; inputNewPass2=L"";
            newPassMismatch=false;
            SaveBlocksData();
        }
        return;
    }

    // ---- PASSWORD ENTRY ----
    if (showPassOverlay) {
        if (hPassInput) isPassInputActive=true;
        if (hPassCancel) { showPassOverlay=false; inputPassText=L""; }
        if (hPassConfirm && !inputPassText.empty()) {
            if (isStoppingFocus) {
                // Verify password
                if (inputPassText == friendPassword) {
                    // Show unlock challenge first (200-word text)
                    showPassOverlay = false;
                    showUnlockChallenge = true;
                    isChallengeTypingActive = true;
                    challengeUserType = L"";
                    inputPassText = L"";
                } else {
                    TriggerGlobalBlockAlert(true, L"Incorrect password! Focus remains active.");
                    inputPassText = L"";
                }
            } else {
                // Starting focus with friend control — store password and activate
                friendPassword = inputPassText;
                isFriendPasswordSet = true;
                isFocusActive = true;
                showPassOverlay = false;
                inputPassText = L"";
                SaveBlocksData();
            }
        }
        return;
    }

    // ---- UNLOCK CHALLENGE ----
    if (showUnlockChallenge) {
        if (hChallengeCancel) {
            showUnlockChallenge = false;
            challengeUserType = L"";
            isChallengeTypingActive = false;
        }
        if (hChallengeProceed) {
            // Check if user typed at least ~5 words (30 chars) from the text
            if (challengeUserType.length() >= 15) {
                isFocusActive = false;
                showUnlockChallenge = false;
                challengeUserType = L"";
                isChallengeTypingActive = false;
                DestroyLiveTimeWindow();
                SaveBlocksData();
            } else {
                TriggerGlobalBlockAlert(true, L"Please type at least 5 words from the text first!");
            }
        }
        return;
    }

    // ---- TAB SWITCH ----
    if (hoverBlockTab != -1) {
        currentBlockTab = hoverBlockTab;
        isModeDropdownOpen=false; isControlDropdownOpen=false;
        isWebComboOpen=false; isAppComboOpen=false; isLangDropdownOpen=false;
        return;
    }

    if (currentBlockTab == 0) {
        // Start/Stop Focus button
        if (hoverStartFocusBtn) {
            isWebInputActive=false; isAppInputActive=false; hoverStartFocusBtn=false;
            if (isFocusActive) {
                if (controlMode == 1) {
                    // Friend control — need password to stop (show unlock challenge flow)
                    isStoppingFocus=true;
                    showPassOverlay=true;
                    isPassInputActive=true;
                    inputPassText=L"";
                } else {
                    // Self control — show 200-word challenge before stopping
                    showUnlockChallenge=true;
                    isChallengeTypingActive=true;
                    challengeUserType=L"";
                }
            } else {
                if (controlMode == 0) {
                    // Self control: show duration picker
                    showTimeOverlay=true;
                } else {
                    // Friend control: set password first if not set, or ask friend to start
                    isStoppingFocus=false;
                    if (!isFriendPasswordSet) {
                        showSetPasswordOverlay=true;
                        isNewPass1Active=true; isNewPass2Active=false;
                        inputNewPass1=L""; inputNewPass2=L"";
                        newPassMismatch=false;
                    } else {
                        showPassOverlay=true;
                        isPassInputActive=true;
                        inputPassText=L"";
                    }
                }
            }
            return;
        }

        // Close dropdowns on outside click
        bool closedAnyDropdown=false;
        if (isLangDropdownOpen && !hoverLangDropdown && !hoverOptBn && !hoverOptEn) {
            isLangDropdownOpen=false; closedAnyDropdown=true;
        } else if (isLangDropdownOpen) {
            if (hoverOptBn) quoteLanguage=0;
            if (hoverOptEn) quoteLanguage=1;
            isLangDropdownOpen=false; SaveBlocksData(); return;
        }
        if (isControlDropdownOpen && !hoverControlDropdown && !hoverOptSelf && !hoverOptFriend) {
            isControlDropdownOpen=false; closedAnyDropdown=true;
        } else if (isControlDropdownOpen) {
            if (hoverOptSelf)   controlMode=0;
            if (hoverOptFriend) controlMode=1;
            isControlDropdownOpen=false; SaveBlocksData(); return;
        }
        if (isModeDropdownOpen && !hoverModeDropdown && !hoverOptAllow && !hoverOptBlock) {
            isModeDropdownOpen=false; closedAnyDropdown=true;
        } else if (isModeDropdownOpen) {
            if (hoverOptAllow) { simpleBlockMode=0; EnforceSystemApps(); }
            if (hoverOptBlock) { simpleBlockMode=1; EnforceSystemApps(); }
            isModeDropdownOpen=false; SaveBlocksData(); return;
        }
        if (isWebComboOpen && !hoverWebCombo && hoverWebOptIdx==-1) {
            isWebComboOpen=false; closedAnyDropdown=true;
        } else if (isWebComboOpen) {
            if (hoverWebOptIdx!=-1) { webList.push_back({commonWebsites[hoverWebOptIdx],false}); SaveBlocksData(); }
            isWebComboOpen=false; return;
        }
        if (isAppComboOpen && !hoverAppCombo && hoverAppOptIdx==-1) {
            isAppComboOpen=false; closedAnyDropdown=true;
        } else if (isAppComboOpen) {
            if (hoverAppOptIdx!=-1) { appList.push_back({commonApps[hoverAppOptIdx],false,false}); SaveBlocksData(); }
            isAppComboOpen=false; return;
        }
        if (closedAnyDropdown) return;

        if (hoverControlDropdown && !isFocusActive) { isControlDropdownOpen=true; return; }
        if (hoverModeDropdown    && !isFocusActive) { isModeDropdownOpen=true; return; }
        if (hoverWebCombo        && !isFocusActive) { isWebComboOpen=true; return; }
        if (hoverAppCombo        && !isFocusActive) { isAppComboOpen=true; return; }
        if (hoverLangDropdown    && !isFocusActive) { isLangDropdownOpen=true; return; }
        if (hoverQuotesCheckbox  && !isFocusActive) { showQuotes=!showQuotes; SaveBlocksData(); return; }

        isWebInputActive = hoverWebInput;
        isAppInputActive = hoverAppInput;

        if (hoverWebAddBtn && !webInputText.empty()) {
            webList.push_back({webInputText,false}); webInputText=L""; SaveBlocksData();
        }
        if (hoverAppAddBtn && !appInputText.empty()) {
            appList.push_back({appInputText,false,false}); appInputText=L""; SaveBlocksData();
        }

        if (hoverAddExe) {
            OPENFILENAMEW ofn; wchar_t szFile[260]={0};
            ZeroMemory(&ofn,sizeof(ofn)); ofn.lStructSize=sizeof(ofn);
            ofn.hwndOwner=GetActiveWindow(); ofn.lpstrFile=szFile; ofn.nMaxFile=sizeof(szFile);
            ofn.lpstrFilter=L"Executable Files\0*.exe\0All Files\0*.*\0";
            ofn.nFilterIndex=1; ofn.Flags=OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)==TRUE) {
                wstring filePath=ofn.lpstrFile;
                size_t pos=filePath.find_last_of(L"\\/");
                if (pos!=wstring::npos) filePath=filePath.substr(pos+1);
                appList.push_back({filePath,false,false}); SaveBlocksData();
            }
        }
        if (hoverAddStoreApp) {
            RefreshRunningApps(); tStoreScrollY=cStoreScrollY=0; showStoreOverlay=true;
        }
        if (hoverAddWindowTitle) { showTitleOverlay=true; isTitleInputActive=true; }

        if (!isFocusActive) {
            bool changed=false;
            for (auto it=webList.begin();it!=webList.end();) {
                if (it->isHoveredCross) { it=webList.erase(it); changed=true; } else ++it;
            }
            for (auto it=appList.begin();it!=appList.end();) {
                if (it->isHoveredCross && !it->isSystemLocked) { it=appList.erase(it); changed=true; } else ++it;
            }
            if (changed) SaveBlocksData();
        }
    } else if (currentBlockTab == 1) {
        ProcessScheduleBlocksMouseClick(x, y);
    }
}

// ==========================================
// KEY PRESS
// ==========================================
void ProcessBlocksKeyPress(wchar_t c) {
    if (showUnlockChallenge && isChallengeTypingActive) {
        if (c >= 32 && c <= 126 && challengeUserType.length() < 200)
            challengeUserType += c;
        return;
    }
    if (showSetPasswordOverlay) {
        if (isNewPass1Active && c>=32&&c<=126&&inputNewPass1.length()<30) inputNewPass1+=c;
        if (isNewPass2Active && c>=32&&c<=126&&inputNewPass2.length()<30) inputNewPass2+=c;
        return;
    }
    if (showPassOverlay && isPassInputActive) {
        if (c>=32&&c<=126&&inputPassText.length()<30) inputPassText+=c;
        return;
    }
    if (showTitleOverlay && isTitleInputActive) {
        if (c>=32&&c<=126&&inputTitleText.length()<40) inputTitleText+=c;
        return;
    }
    if (!showTimeOverlay&&!showPassOverlay&&!showStoreOverlay&&!showTitleOverlay&&
        !showUnlockChallenge&&!showSetPasswordOverlay) {
        if (currentBlockTab==0) {
            if (isWebInputActive&&c>=32&&c<=126&&webInputText.length()<40) webInputText+=c;
            if (isAppInputActive&&c>=32&&c<=126&&appInputText.length()<40) appInputText+=c;
        } else if (currentBlockTab==1) {
            ProcessScheduleBlocksKeyPress(c);
        }
    }
}

// ==========================================
// KEY DOWN
// ==========================================
void ProcessBlocksKeyDown(WPARAM key) {
    if (key == VK_ESCAPE) {
        showPassOverlay=false; showTitleOverlay=false;
        showTimeOverlay=false; showStoreOverlay=false;
        showUnlockChallenge=false; showSetPasswordOverlay=false;
        inputPassText=L""; inputTitleText=L"";
        inputNewPass1=L""; inputNewPass2=L"";
        challengeUserType=L"";
        newPassMismatch=false;
        return;
    }

    if (showUnlockChallenge && isChallengeTypingActive) {
        if (key==VK_BACK&&!challengeUserType.empty()) challengeUserType.pop_back();
        return;
    }
    if (showSetPasswordOverlay) {
        if (key==VK_BACK) {
            if (isNewPass1Active&&!inputNewPass1.empty()) inputNewPass1.pop_back();
            if (isNewPass2Active&&!inputNewPass2.empty()) inputNewPass2.pop_back();
        }
        if (key==VK_TAB) { isNewPass1Active=!isNewPass1Active; isNewPass2Active=!isNewPass2Active; }
        return;
    }
    if (showPassOverlay && isPassInputActive) {
        if (key==VK_BACK&&!inputPassText.empty()) inputPassText.pop_back();
        if (key==VK_RETURN&&!inputPassText.empty()) ProcessBlocksMouseClick(0,0); // trigger confirm via pass
        return;
    }
    if (showTitleOverlay && isTitleInputActive) {
        if (key==VK_BACK&&!inputTitleText.empty()) inputTitleText.pop_back();
        return;
    }
    if (!showTimeOverlay&&!showPassOverlay&&!showStoreOverlay&&!showTitleOverlay&&
        !showUnlockChallenge&&!showSetPasswordOverlay) {
        if (currentBlockTab==0) {
            if (isWebInputActive) {
                if (key==VK_BACK&&!webInputText.empty()) webInputText.pop_back();
                else if (key==VK_RETURN&&!webInputText.empty()) {
                    webList.push_back({webInputText,false}); webInputText=L""; SaveBlocksData();
                }
            }
            if (isAppInputActive) {
                if (key==VK_BACK&&!appInputText.empty()) appInputText.pop_back();
                else if (key==VK_RETURN&&!appInputText.empty()) {
                    appList.push_back({appInputText,false,false}); appInputText=L""; SaveBlocksData();
                }
            }
        } else if (currentBlockTab==1) {
            ProcessScheduleBlocksKeyDown(key);
        }
    }
}

// ==========================================
// MOUSE WHEEL
// ==========================================
void ProcessBlocksMouseWheel(float x, float y, int delta) {
    int steps = (delta > 0) ? 1 : -1;

    if (showStoreOverlay) {
        tStoreScrollY -= steps * 60.0f;
        float visH = 450.0f - 76.0f - 60.0f;
        float maxS = max(0.0f, systemStoreApps.size() * 42.0f - visH);
        tStoreScrollY = max(0.0f, min(tStoreScrollY, maxS));
        return;
    }

    if (!showTimeOverlay&&!showPassOverlay&&!showTitleOverlay&&
        !showUnlockChallenge&&!showSetPasswordOverlay) {
        if (currentBlockTab==0) {
            float bodyY=s_contentY+60.0f;
            float boxX=s_contentX+30.0f, boxW=s_contentW-60.0f;
            float ctrlDropY=bodyY+40.0f, modeDropY=ctrlDropY+75.0f;
            float colW=(boxW-90.0f)/2.0f;
            float leftColX=boxX+30.0f, rightColX=boxX+60.0f+colW;
            float secY=modeDropY+50.0f;

            RectF webTable(leftColX,secY+90.0f,colW,160.0f);
            if (webTable.Contains(x,y)) {
                tWebScrollY -= steps*40.0f;
                float maxS=max(0.0f,webList.size()*30.0f-160.0f+10.0f);
                tWebScrollY=max(0.0f,min(tWebScrollY,maxS));
            }
            RectF appTable(rightColX,secY+115.0f,colW,135.0f);
            if (appTable.Contains(x,y)) {
                tAppScrollY -= steps*40.0f;
                float maxS=max(0.0f,appList.size()*30.0f-135.0f+10.0f);
                tAppScrollY=max(0.0f,min(tAppScrollY,maxS));
            }
        } else if (currentBlockTab==1) {
            ProcessScheduleBlocksMouseWheel(x, y, delta);
        }
    }
}
