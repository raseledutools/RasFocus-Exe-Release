#include "tab_blocks.h"
#include "tab_schedule_blocks.h"
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
static int controlMode = 0; // 0=Self, 1=Parent
static bool hoverControlDropdown = false;
static bool isControlDropdownOpen = false;
static bool hoverOptSelf = false;
static bool hoverOptParent = false;
static bool hoverStartFocusBtn = false;
static time_t focusEndTimeBlocks = 0;

// =============================================
// PARENT CONTROL PASSWORD
// =============================================
static wstring parentPassword = L""; // stored password set by parent
static wstring parentPasswordInput = L"";
static bool showSetPasswordOverlay = false;  // first time set password
static wstring setPassInput1 = L"";
static wstring setPassInput2 = L"";
static bool isSetPass1Active = true;
static bool isSetPass2Active = false;
static bool hSetPass1 = false, hSetPass2 = false;
static bool hSetPassConfirm = false, hSetPassCancel = false;
static bool setPassError = false;

// =============================================
// SELF CONTROL DURATION (Month/Days/Hour/Min)
// =============================================
static int selfMonths = 0;
static int selfDays   = 0;
static int selfHours  = 1;
static int selfMins   = 0;

// Spinner hover states for self-control time overlay
static bool hSelfMonthM = false, hSelfMonthP = false;
static bool hSelfDayM   = false, hSelfDayP   = false;
static bool hSelfHourM  = false, hSelfHourP  = false;
static bool hSelfMinM   = false, hSelfMinP   = false;

// =============================================
// LONG TEXT UNLOCK
// =============================================
static bool useLongTextUnlock  = false;     // toggle in UI
static bool hoverLongTextCheck = false;
static wstring longTextPhrase  = L"I accept that focus sessions help me stay on track and achieve my goals. Distractions steal time and energy that could be used for growth. By committing to this session I choose progress over procrastination and success over short-term pleasure.";
// (~200 words demo)
static wstring customLongText  = L"";       // user custom phrase
static bool useCustomLongText  = false;
static bool hoverCustomLTCheck = false;

// Overlay for editing custom long text
static bool showLongTextEditOverlay = false;
static wstring longTextEditBuffer   = L"";
static bool isLTEditActive          = true;
static bool hLTEditConfirm = false, hLTEditCancel = false;

// Overlay shown when user tries to stop with long-text mode
static bool showLongTextUnlockOverlay = false;
static wstring longTextUnlockInput    = L"";
static bool isLTUnlockInputActive     = true;
static bool hLTUnlockConfirm = false, hLTUnlockCancel = false;
static bool ltUnlockError = false;

// =============================================
// QUOTES
// =============================================
static bool showQuotes = true;
static bool hoverQuotesCheckbox = false;
static int quoteLanguage = 0; // 0=Bangla, 1=English
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

// --- Overlays States ---
static bool showTimeOverlay = false;
// (focusHours/focusMins replaced by selfHours/selfMins/selfDays/selfMonths above)
static bool hTimeStart = false, hTimeCancel = false;

static bool showPassOverlay = false;
static wstring inputPassText = L"";
static bool isPassInputActive = true, hPassInput = false;
static bool hPassConfirm = false, hPassCancel = false;
static bool isStoppingFocus = false; 

// --- Store Apps Overlay ---
static bool showStoreOverlay = false;
static bool hoverStoreClose = false;
static int hoverStoreAddIdx = -1;
vector<wstring> systemStoreApps = {};

// --- Window Title Overlay ---
static bool showTitleOverlay = false;
static wstring inputTitleText = L"";
static bool isTitleInputActive = true, hTitleInput = false;
static bool hTitleAdd = false, hTitleCancel = false;

// =============================================
// BLOCKING DROPDOWN - SYSTEM TOOLS
// =============================================
// System tool names shown in block dropdown
static const vector<wstring> systemToolItems = {
    L"Control Panel",
    L"Task Manager",
    L"Run (Win+R)",
    L"CMD / Terminal",
    L"Windows Settings"
};
// Process/window matches for each system tool
static const vector<vector<wstring>> systemToolMatches = {
    {L"control.exe",   L"control panel"},
    {L"taskmgr.exe",   L"task manager"},
    {L"",              L"run"},          // handled specially via hotkey hook
    {L"cmd.exe",       L"powershell.exe", L"windows powershell", L"command prompt"},
    {L"systemsettings.exe", L"ms-settings", L"windows settings", L"settings"}
};
static vector<bool> systemToolBlocked = {false, false, false, false, false};
static bool hoverSysToolDropdown = false;
static bool isSysToolDropdownOpen = false;
static int  hoverSysToolOptIdx = -1;
// hover cross for system tool blocked items
static int  hoverSysToolCrossIdx = -1;

// --- Simple Blocks States ---
static int simpleBlockMode = 1; // 0: Allow, 1: Block
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
vector<wstring> commonApps = { L"chrome.exe", L"msedge.exe", L"firefox.exe", L"telegram.exe", L"whatsapp.exe", L"discord.exe", L"vlc.exe" };

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
static const Color SClrRedHover(255, 200, 50, 50);
static const Color SClrOverlay(180, 0, 0, 0); 
static const Color SClrDisabled(255, 200, 200, 200);
static const Color SClrOrange(255, 220, 120, 20);

// ==========================================
// DATA PERSISTENCE (SAVE / LOAD)
// ==========================================
wstring GetAppDataFolder() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        wstring fullPath = wstring(path) + L"\\RasFocus";
        CreateDirectoryW(fullPath.c_str(), NULL);
        return fullPath;
    }
    return L"";
}

void SaveBlocksData() {
    wstring path = GetAppDataFolder() + L"\\blocks_data.dat";
    string narrowPathOut(path.begin(), path.end());
    ofstream out(narrowPathOut.c_str(), ios::binary);
    if(!out) return;
    
    out.write((char*)&simpleBlockMode, sizeof(simpleBlockMode));
    out.write((char*)&controlMode, sizeof(controlMode));
    out.write((char*)&showQuotes, sizeof(showQuotes));
    out.write((char*)&quoteLanguage, sizeof(quoteLanguage));
    // Self-control duration
    out.write((char*)&selfMonths, sizeof(selfMonths));
    out.write((char*)&selfDays,   sizeof(selfDays));
    out.write((char*)&selfHours,  sizeof(selfHours));
    out.write((char*)&selfMins,   sizeof(selfMins));
    out.write((char*)&focusEndTimeBlocks, sizeof(focusEndTimeBlocks));
    
    bool isActive = isFocusActive;
    out.write((char*)&isActive, sizeof(isActive));

    // Parent password
    size_t ppLen = parentPassword.length();
    out.write((char*)&ppLen, sizeof(ppLen));
    out.write((char*)parentPassword.data(), ppLen * sizeof(wchar_t));

    // Long text unlock
    out.write((char*)&useLongTextUnlock, sizeof(useLongTextUnlock));
    out.write((char*)&useCustomLongText, sizeof(useCustomLongText));
    size_t ltLen = customLongText.length();
    out.write((char*)&ltLen, sizeof(ltLen));
    if(ltLen) out.write((char*)customLongText.data(), ltLen * sizeof(wchar_t));

    // System tool blocked states
    for(int i = 0; i < 5; ++i) out.write((char*)&systemToolBlocked[i], sizeof(bool));

    size_t wSize = webList.size();
    out.write((char*)&wSize, sizeof(wSize));
    for(auto& w : webList) {
        size_t len = w.name.length();
        out.write((char*)&len, sizeof(len));
        out.write((char*)w.name.data(), len * sizeof(wchar_t));
        out.write((char*)&w.isSystemLocked, sizeof(w.isSystemLocked));
    }

    size_t aSize = appList.size();
    out.write((char*)&aSize, sizeof(aSize));
    for(auto& a : appList) {
        size_t len = a.name.length();
        out.write((char*)&len, sizeof(len));
        out.write((char*)a.name.data(), len * sizeof(wchar_t));
        out.write((char*)&a.isSystemLocked, sizeof(a.isSystemLocked));
    }
}

void LoadBlocksData() {
    wstring path = GetAppDataFolder() + L"\\blocks_data.dat";
    string narrowPathIn(path.begin(), path.end());
    ifstream in(narrowPathIn.c_str(), ios::binary);
    if(!in) return;
    
    in.read((char*)&simpleBlockMode, sizeof(simpleBlockMode));
    in.read((char*)&controlMode, sizeof(controlMode));
    in.read((char*)&showQuotes, sizeof(showQuotes));
    in.read((char*)&quoteLanguage, sizeof(quoteLanguage));
    in.read((char*)&selfMonths, sizeof(selfMonths));
    in.read((char*)&selfDays,   sizeof(selfDays));
    in.read((char*)&selfHours,  sizeof(selfHours));
    in.read((char*)&selfMins,   sizeof(selfMins));
    in.read((char*)&focusEndTimeBlocks, sizeof(focusEndTimeBlocks));

    bool savedFocus = false;
    if(in.read((char*)&savedFocus, sizeof(savedFocus))) {
        if (savedFocus) {
            time_t now = std::time(nullptr);
            if (now < focusEndTimeBlocks) isFocusActive = true;
        }
    }

    size_t ppLen = 0;
    if(in.read((char*)&ppLen, sizeof(ppLen)) && ppLen > 0) {
        parentPassword.resize(ppLen);
        in.read((char*)parentPassword.data(), ppLen * sizeof(wchar_t));
    }

    in.read((char*)&useLongTextUnlock, sizeof(useLongTextUnlock));
    in.read((char*)&useCustomLongText, sizeof(useCustomLongText));
    size_t ltLen = 0;
    if(in.read((char*)&ltLen, sizeof(ltLen)) && ltLen > 0) {
        customLongText.resize(ltLen);
        in.read((char*)customLongText.data(), ltLen * sizeof(wchar_t));
    }

    for(int i = 0; i < 5; ++i) {
        bool b = false;
        if(in.read((char*)&b, sizeof(bool))) systemToolBlocked[i] = b;
    }

    size_t wSize = 0;
    if(in.read((char*)&wSize, sizeof(wSize))) {
        webList.clear();
        for(size_t i=0; i<wSize; i++) {
            size_t len=0; in.read((char*)&len, sizeof(len));
            wstring name(len, L'\0');
            in.read((char*)name.data(), len * sizeof(wchar_t));
            bool sysLocked = false;
            in.read((char*)&sysLocked, sizeof(sysLocked));
            webList.push_back({name, false, sysLocked});
        }
    }
    
    size_t aSize = 0;
    if(in.read((char*)&aSize, sizeof(aSize))) {
        appList.clear();
        for(size_t i=0; i<aSize; i++) {
            size_t len=0; in.read((char*)&len, sizeof(len));
            wstring name(len, L'\0');
            in.read((char*)name.data(), len * sizeof(wchar_t));
            bool sysLocked = false;
            in.read((char*)&sysLocked, sizeof(sysLocked));
            appList.push_back({name, false, sysLocked});
        }
    }
}

// --- Helpers ---
static GraphicsPath* GetBlockRoundRectPath(RectF rect, int radius) {
    GraphicsPath* path = new GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure(); return path;
}

static wstring toLowerW_Blocks(wstring str) {
    for (auto& c : str) c = towlower(c); return str;
}

// Check if system tool index is active and blocked
static bool IsSystemToolBlocked(int idx) {
    if(idx < 0 || idx >= 5) return false;
    return systemToolBlocked[idx];
}

// --- Smart Tab Closer ---
void CloseActiveTabOnly(HWND hBrowser) { 
    if (GetForegroundWindow() == hBrowser) {
        keybd_event(VK_CONTROL, 0, 0, 0); 
        keybd_event('W', 0, 0, 0); 
        keybd_event('W', 0, KEYEVENTF_KEYUP, 0); 
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0); 
    }
}

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
// CRASH-FREE SAFE POPUP THREAD LOGIC
// ==========================================
struct BlocksPopupData { wstring quote; };

LRESULT CALLBACK BlocksPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT rect; GetClientRect(hwnd, &rect); RectF bgRect(0, 0, rect.right, rect.bottom);

        SolidBrush bgBrush(Color(255, 20, 80, 40)); 
        g.FillRectangle(&bgBrush, bgRect);
        Pen border(SClrTeal, 4.0f); g.DrawRectangle(&border, 2.0f, 2.0f, rect.right-4.0f, rect.bottom-4.0f);

        FontFamily ff(L"Segoe UI"); Font fQ(&ff, 32, FontStyleBold, UnitPixel);
        SolidBrush whiteBrush(Color(255, 255, 255, 255));
        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);

        BlocksPopupData* pData = (BlocksPopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (pData) {
            g.DrawString(pData->quote.c_str(), -1, &fQ, RectF(20.0f, 20.0f, rect.right - 40.0f, rect.bottom - 40.0f), &fmtC, &whiteBrush);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_TIMER && wParam == 1) { 
        KillTimer(hwnd, 1); DestroyWindow(hwnd); PostQuitMessage(0); return 0; 
    }
    if (msg == WM_DESTROY) {
        BlocksPopupData* pData = (BlocksPopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if(pData) delete pData;
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
    int w = 1000; int h = 250;
    HWND hPopup = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, 
        "RasFocusBlocksPopupClass", "Alert", WS_POPUP, (screenW-w)/2, 80, w, h, NULL, NULL, GetModuleHandle(NULL), NULL);
    
    if (hPopup) {
        BlocksPopupData* data = new BlocksPopupData{ quote };
        SetWindowLongPtr(hPopup, GWLP_USERDATA, (LONG_PTR)data);
        SetLayeredWindowAttributes(hPopup, 0, 240, LWA_ALPHA); 
        ShowWindow(hPopup, SW_SHOW); 
        SetForegroundWindow(hPopup); 
        SetTimer(hPopup, 1, 6000, NULL); 
        
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
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
// GLOBAL KEYLOGGER FOR BLOCKS TAB
// ==========================================
HHOOK hKeyboardHookBlocks = NULL;
string globalKeyBufferBlocks = "";

LRESULT CALLBACK KeyboardHookProcBlocks(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN && isFocusActive) {
        KBDLLHOOKSTRUCT* kbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = kbdStruct->vkCode;

        // Block Win+R (Run dialog) if systemToolBlocked[2]
        if (systemToolBlocked[2]) {
            bool winDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
            if (winDown && vkCode == 'R') {
                TriggerGlobalBlockAlert(true, L"Focus Active. Run dialog is blocked!");
                return 1; // suppress
            }
        }

        if ((vkCode >= 'A' && vkCode <= 'Z') || (vkCode >= '0' && vkCode <= '9') || vkCode == VK_SPACE || vkCode == VK_OEM_PERIOD) {
            char c = MapVirtualKey(vkCode, MAPVK_VK_TO_CHAR);
            if (vkCode == VK_OEM_PERIOD) c = '.'; 
            globalKeyBufferBlocks += tolower(c);
            if (globalKeyBufferBlocks.length() > 100) globalKeyBufferBlocks.erase(0, 1);

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
        } 
        else if (vkCode == VK_BACK) {
            if (!globalKeyBufferBlocks.empty()) globalKeyBufferBlocks.pop_back();
        }
    }
    return CallNextHookEx(hKeyboardHookBlocks, nCode, wParam, lParam);
}

void StartBlocksKeyloggerThread() {
    hKeyboardHookBlocks = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProcBlocks, NULL, 0);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
}

// --- Check if a process/window matches a system tool ---
static bool MatchesSystemTool(int toolIdx, const wstring& processName, const wstring& windowTitle) {
    if(toolIdx < 0 || toolIdx >= 5 || !systemToolBlocked[toolIdx]) return false;
    for(const auto& m : systemToolMatches[toolIdx]) {
        if(m.empty()) continue;
        if(toLowerW_Blocks(processName).find(m) != wstring::npos) return true;
        if(toLowerW_Blocks(windowTitle).find(m) != wstring::npos) return true;
    }
    return false;
}

// --- REAL BLOCKING ENGINE ---
bool ShouldKillProcessBlocks(const wstring& processName, const wstring& windowTitle) {
    if (!isFocusActive) return false;

    wstring lowerTitle  = toLowerW_Blocks(windowTitle);
    wstring lowerProcess = toLowerW_Blocks(processName);

    // Check system tool blocks first
    for(int i = 0; i < 5; ++i) {
        if(MatchesSystemTool(i, lowerProcess, lowerTitle)) {
            wstring msg = L"Focus Active. " + systemToolItems[i] + L" is blocked!";
            TriggerGlobalBlockAlert(true, msg);
            return true;
        }
    }

    // Legacy always-block task manager if systemToolBlocked[1]
    if (!systemToolBlocked[1]) {
        // keep legacy behaviour: block task manager even if not in list
        if (lowerTitle.find(L"task manager") != wstring::npos || lowerTitle.find(L"taskmgr") != wstring::npos) {
            TriggerGlobalBlockAlert(true, L"Focus is Active. Task Manager is blocked!");
            return true;
        }
    }

    if (lowerTitle.find(L"uninstall") != wstring::npos || lowerTitle.find(L"programs and features") != wstring::npos || lowerTitle.find(L"অ্যাপস ও বৈশিষ্ট্য") != wstring::npos) {
        TriggerGlobalBlockAlert(true, L"Focus is Active. Uninstallation is blocked!");
        return true;
    }

    if (simpleBlockMode == 1) { 
        for (const auto& app : appList) {
            wstring lowerApp = toLowerW_Blocks(app.name);
            if (lowerProcess == lowerApp || lowerProcess.find(lowerApp) != wstring::npos || lowerTitle.find(lowerApp) != wstring::npos) {
                TriggerGlobalBlockAlert();
                return true;
            }
        }
    } 
    else if (simpleBlockMode == 0) {
        bool isAllowed = false;
        for (const auto& app : appList) {
            wstring lowerApp = toLowerW_Blocks(app.name);
            if (lowerProcess == lowerApp || lowerTitle.find(lowerApp) != wstring::npos) {
                isAllowed = true; break;
            }
        }
        if (!isAllowed) {
            if (windowTitle.length() > 0 && processName != L"explorer.exe") {
                TriggerGlobalBlockAlert(true, L"Focus is Active. Only allowed apps can run.");
                return true; 
            }
        }
    }
    return false;
}

BOOL CALLBACK EnumWindowsProcBlocker(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    
    wchar_t windowTitle[256]; GetWindowTextW(hwnd, windowTitle, 256);
    DWORD processId; GetWindowThreadProcessId(hwnd, &processId);
    
    if (processId == GetCurrentProcessId()) return TRUE; 

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE, FALSE, processId);
    if (hProcess) {
        wchar_t processName[MAX_PATH]; HMODULE hMod; DWORD cbNeeded;
        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
            GetModuleBaseNameW(hProcess, hMod, processName, sizeof(processName)/sizeof(wchar_t));
            wstring pName(processName); wstring wTitle(windowTitle);
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
        }
        Sleep(50);
    }
}

// =============================================
// REAL-TIME 3RD PARTY APP SCANNER
// =============================================
static vector<wstring> scannedThirdPartyApps;
static bool scannerRunning = false;

void RealTimeAppScannerThread() {
    while(true) {
        vector<wstring> found;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if(hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe32; pe32.dwSize = sizeof(PROCESSENTRY32W);
            if(Process32FirstW(hSnap, &pe32)) {
                do {
                    wstring exe = pe32.szExeFile;
                    // exclude system processes
                    if(exe == L"svchost.exe" || exe == L"conhost.exe" || exe == L"System" ||
                       exe == L"explorer.exe" || exe == L"RasFocus.exe" || exe.length() <= 4) continue;
                    bool exists = false;
                    for(auto& f : found) if(f == exe) { exists = true; break; }
                    if(!exists) found.push_back(exe);
                } while(Process32NextW(hSnap, &pe32));
            }
            CloseHandle(hSnap);
        }
        scannedThirdPartyApps = found;
        Sleep(3000); // scan every 3 seconds
    }
}

static bool threadStarted = false;
void StartBlockerThread() {
    if (!threadStarted) {
        thread t(BackgroundBlockerThread); t.detach();
        thread kl(StartBlocksKeyloggerThread); kl.detach();
        if(!scannerRunning) {
            thread sc(RealTimeAppScannerThread); sc.detach();
            scannerRunning = true;
        }
        threadStarted = true;
    }
}

void RefreshRunningApps() {
    systemStoreApps.clear();
    // Use already-scanned list if available
    if(!scannedThirdPartyApps.empty()) {
        systemStoreApps = scannedThirdPartyApps;
    } else {
        HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hProcessSnap == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32W pe32; pe32.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hProcessSnap, &pe32)) {
            do {
                wstring exeName = pe32.szExeFile;
                if (exeName != L"svchost.exe" && exeName != L"conhost.exe" && exeName != L"System" && exeName.length() > 4) {
                    bool exists = false;
                    for (const auto& app : systemStoreApps) { if (app == exeName) { exists = true; break; } }
                    if (!exists && systemStoreApps.size() < 100) systemStoreApps.push_back(exeName);
                }
            } while (Process32NextW(hProcessSnap, &pe32));
        }
        CloseHandle(hProcessSnap);
    }
    
    vector<wstring> priorities = {L"chrome", L"msedge", L"firefox", L"telegram", L"whatsapp", L"discord", L"vlc", L"spotify", L"netflix", L"zoom", L"skype"};
    auto getPriority = [&](const wstring& name) {
        wstring lower = toLowerW_Blocks(name);
        for (const auto& p : priorities) if (lower.find(p) != wstring::npos) return 0;
        return 1;
    };
    std::sort(systemStoreApps.begin(), systemStoreApps.end(), [&](const wstring& a, const wstring& b) {
        int pA = getPriority(a), pB = getPriority(b);
        if(pA != pB) return pA < pB;
        return a < b;
    });
    if(systemStoreApps.empty()) systemStoreApps.push_back(L"No active apps found");
}

// ==========================================
// SPINNER HELPER (reusable)
// ==========================================
static void DrawBlocksOverlaySpinner(Graphics& g, float x, float y, const wstring& valStr, bool hM, bool hP, Font* fIcon, Font* fBold) {
    SolidBrush brushBtn(SClrBorder); SolidBrush brushBtnHover(SClrGrayText);
    SolidBrush brushWhite(SClrWhite); SolidBrush brushDark(SClrDark);
    Pen penBorder(SClrBorder, 1.5f);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);

    RectF mRect(x, y, 32.0f, 36.0f); RectF tRect(x + 32.0f, y, 60.0f, 36.0f); RectF pRect(x + 92.0f, y, 32.0f, 36.0f);

    g.FillRectangle(hM ? &brushBtnHover : &brushBtn, mRect); g.DrawRectangle(&penBorder, mRect.X, mRect.Y, mRect.Width, mRect.Height);
    g.DrawString(L"\xE738", -1, fIcon, mRect, &fmtC, &brushDark);
    g.FillRectangle(&brushWhite, tRect); g.DrawRectangle(&penBorder, tRect.X, tRect.Y, tRect.Width, tRect.Height);
    g.DrawString(valStr.c_str(), -1, fBold, tRect, &fmtC, &brushDark);
    g.FillRectangle(hP ? &brushBtnHover : &brushBtn, pRect); g.DrawRectangle(&penBorder, pRect.X, pRect.Y, pRect.Width, pRect.Height);
    g.DrawString(L"\xE710", -1, fIcon, pRect, &fmtC, &brushDark);
}

// ==========================================
// COMPUTE TOTAL FOCUS SECONDS
// ==========================================
static time_t ComputeFocusSeconds() {
    return (time_t)(selfMonths * 30 * 24 * 3600)
         + (time_t)(selfDays  * 24 * 3600)
         + (time_t)(selfHours * 3600)
         + (time_t)(selfMins  * 60);
}

// --- Main Drawing Function ---
void DrawBlocksTab(Graphics& g, float contentX, float contentY, float contentW, float contentH) {
    static bool isBlocksDataLoaded = false;
    if (!isBlocksDataLoaded) {
        LoadBlocksData();
        isBlocksDataLoaded = true;
    }

    StartBlockerThread(); 
    
    // Smooth scroll interpolations
    cWebScrollY   += (tWebScrollY   - cWebScrollY)   * 0.2f;  if (abs(tWebScrollY   - cWebScrollY)   < 0.5f) cWebScrollY   = tWebScrollY;
    cAppScrollY   += (tAppScrollY   - cAppScrollY)   * 0.2f;  if (abs(tAppScrollY   - cAppScrollY)   < 0.5f) cAppScrollY   = tAppScrollY;
    cStoreScrollY += (tStoreScrollY - cStoreScrollY) * 0.2f;  if (abs(tStoreScrollY - cStoreScrollY) < 0.5f) cStoreScrollY = tStoreScrollY;

    s_contentX = contentX; s_contentY = contentY; s_contentW = contentW; s_contentH = contentH;

    FontFamily ff(L"Segoe UI");
    Font fTopTab(&ff, 15, FontStyleBold, UnitPixel);
    Font fTitle(&ff, 24, FontStyleBold, UnitPixel); 
    Font fNormal(&ff, 15, FontStyleRegular, UnitPixel); Font fBold(&ff, 15, FontStyleBold, UnitPixel);
    Font fInfo(&ff, 13, FontStyleItalic, UnitPixel); 
    Font fSmallerBold(&ff, 12, FontStyleBold, UnitPixel);
    Font fSmall(&ff, 12, FontStyleRegular, UnitPixel);
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fIcon(&ffIcons, 22, FontStyleRegular, UnitPixel); Font fSmallIcon(&ffIcons, 14, FontStyleRegular, UnitPixel);
    
    SolidBrush brushTeal(SClrTeal); SolidBrush brushDark(SClrDark); SolidBrush brushGray(SClrGrayText); 
    SolidBrush brushWhite(SClrWhite); SolidBrush brushBg(SClrBg); SolidBrush brushRed(SClrRed);
    SolidBrush brushOrange(SClrOrange);
    Pen penBorder(SClrBorder, 1.5f); Pen penTeal(SClrTeal, 2.0f);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtLW; fmtLW.SetAlignment(StringAlignmentNear); fmtLW.SetLineAlignment(StringAlignmentNear);
    fmtLW.SetFormatFlags(StringFormatFlagsLineLimit);
    fmtLW.SetTrimming(StringTrimmingEllipsisWord);

    // ==========================================
    // --- HEADER ---
    // ==========================================
    g.FillRectangle(&brushWhite, contentX, contentY, contentW, 60.0f); 
    
    float tabW = 200.0f, tabH = 40.0f;
    float tab1X = contentX + 20.0f, tab2X = tab1X + tabW + 10.0f, tab3X = tab2X + tabW + 10.0f, tabY = contentY + 10.0f;

    SolidBrush bTab1(currentBlockTab == 0 ? Color(255, 12, 168, 176) : (hoverBlockTab == 0 ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    SolidBrush bTab2(currentBlockTab == 1 ? Color(255, 12, 168, 176) : (hoverBlockTab == 1 ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    SolidBrush bTab3(currentBlockTab == 2 ? Color(255, 12, 168, 176) : (hoverBlockTab == 2 ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    
    SolidBrush bT1(currentBlockTab == 0 ? Color(255,255,255,255) : Color(255,100,100,100));
    SolidBrush bT2(currentBlockTab == 1 ? Color(255,255,255,255) : Color(255,100,100,100));
    SolidBrush bT3(currentBlockTab == 2 ? Color(255,255,255,255) : Color(255,100,100,100));

    g.FillRectangle(&bTab1, tab1X, tabY, tabW, tabH); g.DrawString(L"Simple Blocks",    -1, &fTopTab, RectF(tab1X, tabY, tabW, tabH), &fmtC, &bT1);
    g.FillRectangle(&bTab2, tab2X, tabY, tabW, tabH); g.DrawString(L"Schedule Blocks",  -1, &fTopTab, RectF(tab2X, tabY, tabW, tabH), &fmtC, &bT2);
    g.FillRectangle(&bTab3, tab3X, tabY, tabW, tabH); g.DrawString(L"Device Block",     -1, &fTopTab, RectF(tab3X, tabY, tabW, tabH), &fmtC, &bT3);

    float bodyY = contentY + 60.0f;
    g.FillRectangle(&brushBg, contentX, bodyY, contentW, contentH - 60.0f); 

    float boxX = contentX + 30.0f; float boxW = contentW - 60.0f; float boxH = contentH - 60.0f - 40.0f; 
    GraphicsPath* boxPath = GetBlockRoundRectPath(RectF(boxX, bodyY + 20.0f, boxW, boxH), 6);
    g.FillPath(&brushWhite, boxPath); g.DrawPath(&penBorder, boxPath); delete boxPath;

    float ctrlDropX   = boxX + 30.0f;
    float ctrlDropY   = bodyY + 40.0f;
    float modeDropX   = boxX + 150.0f;
    float modeDropY   = ctrlDropY + 75.0f;
    float colW        = (boxW - 90.0f) / 2.0f;
    float leftColX    = boxX + 30.0f;
    float rightColX   = boxX + 60.0f + colW;
    float webComboX   = leftColX + colW - 105.0f;
    float webComboY   = modeDropY + 95.0f;

    if (currentBlockTab == 0) { 
        float rowY = bodyY + 40.0f;

        // --- CONTROL MODE DROPDOWN ---
        bool ctrlLocked = isFocusActive;
        RectF ctrlDrop(ctrlDropX, rowY, 160.0f, 40.0f);
        GraphicsPath* cdp = GetBlockRoundRectPath(ctrlDrop, 4);
        SolidBrush cDropBg(ctrlLocked ? SClrBg : (hoverControlDropdown ? SClrBgHover : SClrWhite));
        g.FillPath(&cDropBg, cdp); g.DrawPath(&penBorder, cdp); delete cdp;
        
        wstring ctrlTxt = (controlMode == 0) ? L"Self Control" : L"Parent Control";
        SolidBrush ctrlTxtBrush(ctrlLocked ? SClrGrayText : SClrDark);
        g.DrawString(ctrlTxt.c_str(), -1, &fBold, RectF(ctrlDrop.X + 15.0f, ctrlDrop.Y, ctrlDrop.Width - 30.0f, ctrlDrop.Height), &fmtL, &ctrlTxtBrush);
        g.DrawString(L"\xE70D", -1, &fSmallIcon, RectF(ctrlDrop.X + ctrlDrop.Width - 30.0f, ctrlDrop.Y, 30.0f, ctrlDrop.Height), &fmtC, &brushGray);

        // --- START/STOP FOCUS BUTTON ---
        RectF startBtn(boxX + 205.0f, rowY, 150.0f, 40.0f);
        
        if (isFocusActive && controlMode == 0) {
            if (std::time(nullptr) >= focusEndTimeBlocks) { 
                isFocusActive = false;
                SaveBlocksData(); 
            }
        }

        Color btnColor;
        if(isFocusActive) btnColor = hoverStartFocusBtn ? SClrRedHover : SClrRed;
        else              btnColor = hoverStartFocusBtn ? SClrGreenHover : SClrGreen;
        SolidBrush sbBrush(btnColor);
        GraphicsPath* sbp = GetBlockRoundRectPath(startBtn, 4); g.FillPath(&sbBrush, sbp); delete sbp;
        
        wstring startTextStr = L"Start Focus";
        if (isFocusActive) {
            if (controlMode == 0) {
                time_t left = focusEndTimeBlocks - std::time(nullptr);
                if(left < 0) left = 0;
                int dLeft  = (int)(left / 86400);
                int hLeft  = (int)((left % 86400) / 3600);
                int mLeft  = (int)((left % 3600) / 60);
                if(dLeft > 0)       startTextStr = L"Locked (" + to_wstring(dLeft) + L"d " + to_wstring(hLeft) + L"h)";
                else if(hLeft > 0)  startTextStr = L"Locked (" + to_wstring(hLeft) + L"h " + to_wstring(mLeft) + L"m)";
                else                startTextStr = L"Locked (" + to_wstring(mLeft) + L"m)";
            } else {
                startTextStr = L"Stop Focus";
            }
        }
        g.DrawString(startTextStr.c_str(), -1, &fBold, startBtn, &fmtC, &brushWhite);
        
        if (isFocusActive) {
            g.DrawString(L"FOCUS ACTIVE: System tools & blocked apps restricted.", -1, &fInfo, 
                         RectF(boxX + 370.0f, rowY + 5.0f, 500.0f, 30.0f), &fmtL, &brushRed);
        }

        rowY += 60.0f;
        g.DrawLine(&penBorder, boxX + 30.0f, rowY, boxX + boxW - 30.0f, rowY); rowY += 15.0f;

        SolidBrush activeTextBrush(isFocusActive ? SClrGrayText : SClrDark);
        SolidBrush activeTealBrush(isFocusActive ? SClrGrayText : SClrTeal);
        SolidBrush activeInputBg(isFocusActive ? SClrBg : SClrWhite);

        // --- MODE DROPDOWN ---
        g.DrawString(L"Select Mode:", -1, &fBold, RectF(boxX + 30.0f, rowY, 120.0f, 36.0f), &fmtL, &activeTextBrush);
        RectF dropRect(modeDropX, rowY, 200.0f, 36.0f);
        GraphicsPath* dp = GetBlockRoundRectPath(dropRect, 4);
        SolidBrush dropBg(isFocusActive ? SClrBg : (hoverModeDropdown ? SClrBgHover : SClrWhite));
        g.FillPath(&dropBg, dp); g.DrawPath(&penBorder, dp); delete dp;
        wstring modeTxt = (simpleBlockMode == 0) ? L"Allow Apps & Web" : L"Block Apps & Web";
        g.DrawString(modeTxt.c_str(), -1, &fNormal, RectF(dropRect.X + 15.0f, dropRect.Y, dropRect.Width - 30.0f, dropRect.Height), &fmtL, &activeTealBrush);
        g.DrawString(L"\xE70D", -1, &fSmallIcon, RectF(dropRect.X + dropRect.Width - 30.0f, dropRect.Y, 30.0f, dropRect.Height), &fmtC, &brushGray);

        // ============================================================
        // BLOCKING DROPDOWN (with system tools + 3rd party real-time)
        // ============================================================
        float blockDropX = modeDropX + 220.0f;
        float blockDropY = rowY;
        RectF blockDrop(blockDropX, blockDropY, 200.0f, 36.0f);
        GraphicsPath* bdp = GetBlockRoundRectPath(blockDrop, 4);
        SolidBrush bdBg(isFocusActive ? SClrBg : (hoverSysToolDropdown ? SClrBgHover : SClrWhite));
        g.FillPath(&bdBg, bdp); g.DrawPath(&penBorder, bdp); delete bdp;
        g.DrawString(L"Block System Tools \xE70D", -1, &fNormal,
                     RectF(blockDrop.X + 10.0f, blockDrop.Y, blockDrop.Width - 10.0f, blockDrop.Height),
                     &fmtL, &activeTealBrush);

        float secY = rowY + 50.0f;

        // --- LEFT COLUMN: WEBSITES ---
        g.DrawString(L"Websites", -1, &fTitle, RectF(leftColX, secY, colW, 40.0f), &fmtL, &brushDark); 
        
        RectF webInpRect(leftColX, secY + 45.0f, colW - 110.0f, 36.0f); 
        GraphicsPath* wp = GetBlockRoundRectPath(webInpRect, 4);
        g.FillPath(&brushWhite, wp); g.DrawPath((isWebInputActive ? &penTeal : &penBorder), wp); delete wp;
        
        if (webInputText.empty() && !isWebInputActive)
            g.DrawString(L"e.g. facebook.com", -1, &fNormal, RectF(webInpRect.X + 10.0f, webInpRect.Y, webInpRect.Width, webInpRect.Height), &fmtL, &brushGray);
        else {
            g.DrawString(webInputText.c_str(), -1, &fNormal, RectF(webInpRect.X + 10.0f, webInpRect.Y, webInpRect.Width, webInpRect.Height), &fmtL, &brushDark);
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
        g.FillPath(&wComboBrush, wcp); g.DrawPath(&penBorder, wcp); delete wcp;
        g.DrawString(L"\xE70D", -1, &fSmallIcon, wComboRect, &fmtC, &brushDark);

        RectF wAddRect(leftColX + colW - 70.0f, secY + 45.0f, 70.0f, 36.0f);
        GraphicsPath* wAddP = GetBlockRoundRectPath(wAddRect, 4);
        SolidBrush wAddBrush(hoverWebAddBtn ? SClrTealHover : SClrTeal);
        g.FillPath(&wAddBrush, wAddP); delete wAddP;
        g.DrawString(L"+ Add", -1, &fBold, wAddRect, &fmtC, &brushWhite);

        // Web Table
        RectF webTable(leftColX, secY + 90.0f, colW, 160.0f);
        g.FillRectangle(&brushBg, webTable); g.DrawRectangle(&penBorder, webTable.X, webTable.Y, webTable.Width, webTable.Height);
        Region oldClip; g.GetClip(&oldClip);
        g.SetClip(webTable);
        float itemY = webTable.Y + 5.0f - cWebScrollY;
        for (size_t i = 0; i < webList.size(); ++i) {
            if (itemY > webTable.Y - 30.0f && itemY < webTable.Y + webTable.Height) {
                g.DrawString(webList[i].name.c_str(), -1, &fNormal, RectF(leftColX + 10.0f, itemY, colW - 40.0f, 30.0f), &fmtL, &brushDark);
                SolidBrush crossBrush(isFocusActive ? SClrGrayText : (webList[i].isHoveredCross ? SClrRed : SClrGrayText));
                g.DrawString(L"\xE711", -1, &fSmallIcon, RectF(leftColX + colW - 30.0f, itemY, 30.0f, 30.0f), &fmtC, &crossBrush);
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

        // --- RIGHT COLUMN: APPS ---
        float qY = secY; 
        // Quotes checkbox
        SolidBrush cbBrush(showQuotes ? (isFocusActive ? SClrGrayText : SClrTeal) : SClrWhite);
        RectF cbRect(rightColX, qY + 2.0f, 16.0f, 16.0f);
        g.FillRectangle(&cbBrush, cbRect); g.DrawRectangle(&penBorder, cbRect.X, cbRect.Y, cbRect.Width, cbRect.Height);
        if (showQuotes) g.DrawString(L"\xE73E", -1, &fSmallIcon, cbRect, &fmtC, &brushWhite);
        g.DrawString(L"Motivational Quotes", -1, &fNormal, RectF(rightColX + 25.0f, qY, 150.0f, 20.0f), &fmtL, &activeTextBrush);

        RectF langRect(rightColX + 185.0f, qY - 2.0f, 100.0f, 24.0f);
        g.FillRectangle(&activeInputBg, langRect); g.DrawRectangle(&penBorder, langRect.X, langRect.Y, langRect.Width, langRect.Height);
        wstring langTxt = (quoteLanguage == 0) ? L"Bangla" : L"English";
        g.DrawString(langTxt.c_str(), -1, &fNormal, RectF(langRect.X+5.0f, langRect.Y, 70.0f, 24.0f), &fmtL, &activeTextBrush);
        g.DrawString(L"\xE70D", -1, &fSmallIcon, RectF(langRect.X+75.0f, langRect.Y, 25.0f, 24.0f), &fmtC, &brushGray);

        // =============================================
        // LONG TEXT UNLOCK OPTION
        // =============================================
        float ltY = qY + 25.0f;
        SolidBrush ltCheckBrush(useLongTextUnlock ? (isFocusActive ? SClrGrayText : SClrOrange) : SClrWhite);
        RectF ltCbRect(rightColX, ltY + 2.0f, 16.0f, 16.0f);
        g.FillRectangle(&ltCheckBrush, ltCbRect); g.DrawRectangle(&penBorder, ltCbRect.X, ltCbRect.Y, ltCbRect.Width, ltCbRect.Height);
        if(useLongTextUnlock) g.DrawString(L"\xE73E", -1, &fSmallIcon, ltCbRect, &fmtC, &brushWhite);
        g.DrawString(L"Long Text Unlock", -1, &fNormal, RectF(rightColX + 25.0f, ltY, 140.0f, 20.0f), &fmtL, &activeTextBrush);

        if(useLongTextUnlock) {
            // "Custom Word" sub-checkbox
            float ltSub = ltY + 20.0f;
            SolidBrush custCheckBrush(useCustomLongText ? SClrOrange : SClrWhite);
            RectF custCb(rightColX + 20.0f, ltSub + 2.0f, 14.0f, 14.0f);
            g.FillRectangle(&custCheckBrush, custCb); g.DrawRectangle(&penBorder, custCb.X, custCb.Y, custCb.Width, custCb.Height);
            if(useCustomLongText) g.DrawString(L"\xE73E", -1, &fSmallIcon, custCb, &fmtC, &brushWhite);
            g.DrawString(L"Use Custom Text", -1, &fSmall, RectF(rightColX + 40.0f, ltSub, 120.0f, 18.0f), &fmtL, &activeTextBrush);

            // Edit button
            RectF editLT(rightColX + 165.0f, ltSub - 1.0f, 60.0f, 20.0f);
            GraphicsPath* eltp = GetBlockRoundRectPath(editLT, 3);
            SolidBrush editBrush(SClrOrange);
            g.FillPath(&editBrush, eltp); delete eltp;
            g.DrawString(L"Edit", -1, &fSmall, editLT, &fmtC, &brushWhite);

            // Preview text (2 lines)
            wstring previewSrc = useCustomLongText && !customLongText.empty() ? customLongText : longTextPhrase;
            wstring preview = previewSrc.length() > 80 ? previewSrc.substr(0, 80) + L"..." : previewSrc;
            SolidBrush prevBrush(SClrGrayText);
            g.DrawString(preview.c_str(), -1, &fSmall, RectF(rightColX + 5.0f, ltSub + 20.0f, colW - 10.0f, 30.0f), &fmtLW, &prevBrush);
        }

        // Applications section
        float appSectionY = secY + 25.0f + (useLongTextUnlock ? 50.0f : 0.0f);
        // clamp to reasonable position
        if(appSectionY < secY + 25.0f) appSectionY = secY + 25.0f;

        g.DrawString(L"Applications", -1, &fTitle, RectF(rightColX, appSectionY, colW, 40.0f), &fmtL, &brushDark);
        RectF appInpRect(rightColX, appSectionY + 40.0f, colW - 110.0f, 36.0f);
        GraphicsPath* ap = GetBlockRoundRectPath(appInpRect, 4);
        g.FillPath(&brushWhite, ap); g.DrawPath((isAppInputActive ? &penTeal : &penBorder), ap); delete ap;
        
        if (appInputText.empty() && !isAppInputActive)
            g.DrawString(L"e.g. telegram.exe", -1, &fNormal, RectF(appInpRect.X + 10.0f, appInpRect.Y, appInpRect.Width, appInpRect.Height), &fmtL, &brushGray);
        else {
            g.DrawString(appInputText.c_str(), -1, &fNormal, RectF(appInpRect.X + 10.0f, appInpRect.Y, appInpRect.Width, appInpRect.Height), &fmtL, &brushDark);
            if (isAppInputActive && (GetTickCount() / 500) % 2 == 0) {
                Graphics gTemp(GetDesktopWindow()); RectF bRect;
                gTemp.MeasureString(appInputText.c_str(), -1, &fNormal, PointF(0,0), &bRect);
                float cursorX = appInputText.empty() ? appInpRect.X + 10.0f : appInpRect.X + 10.0f + bRect.Width;
                g.FillRectangle(&brushDark, cursorX, appInpRect.Y + 8.0f, 1.5f, 20.0f);
            }
        }

        float aComboX = rightColX + colW - 105.0f;
        RectF aComboRect(aComboX, appSectionY + 40.0f, 30.0f, 36.0f);
        GraphicsPath* acp = GetBlockRoundRectPath(aComboRect, 4);
        SolidBrush aComboBrush(isFocusActive ? SClrDisabled : (hoverAppCombo ? SClrBorder : SClrWhite));
        g.FillPath(&aComboBrush, acp); g.DrawPath(&penBorder, acp); delete acp;
        g.DrawString(L"\xE70D", -1, &fSmallIcon, aComboRect, &fmtC, &brushDark);

        RectF aAddRect(rightColX + colW - 70.0f, appSectionY + 40.0f, 70.0f, 36.0f);
        GraphicsPath* aAddP = GetBlockRoundRectPath(aAddRect, 4);
        SolidBrush aAddBrush(hoverAppAddBtn ? SClrTealHover : SClrTeal);
        g.FillPath(&aAddBrush, aAddP); delete aAddP;
        g.DrawString(L"+ Add", -1, &fBold, aAddRect, &fmtC, &brushWhite);

        // App Table
        RectF appTable(rightColX, appSectionY + 85.0f, colW, 120.0f);
        g.FillRectangle(&brushBg, appTable); g.DrawRectangle(&penBorder, appTable.X, appTable.Y, appTable.Width, appTable.Height);
        g.GetClip(&oldClip);
        g.SetClip(appTable);
        float aItemY = appTable.Y + 5.0f - cAppScrollY;
        for (size_t i = 0; i < appList.size(); ++i) {
            if (aItemY > appTable.Y - 30.0f && aItemY < appTable.Y + appTable.Height) {
                g.DrawString(appList[i].name.c_str(), -1, &fNormal, RectF(rightColX + 10.0f, aItemY, colW - 40.0f, 30.0f), &fmtL, &brushDark);
                if (!appList[i].isSystemLocked) { 
                    SolidBrush crossBrush(isFocusActive ? SClrGrayText : (appList[i].isHoveredCross ? SClrRed : SClrGrayText));
                    g.DrawString(L"\xE711", -1, &fSmallIcon, RectF(rightColX + colW - 30.0f, aItemY, 30.0f, 30.0f), &fmtC, &crossBrush);
                } else {
                    g.DrawString(L"\xE72E", -1, &fSmallIcon, RectF(rightColX + colW - 30.0f, aItemY, 30.0f, 30.0f), &fmtC, &brushTeal); 
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

        // Green Buttons
        float btnW = (colW - 20.0f) / 3.0f;
        float btnY = appSectionY + 215.0f;
        SolidBrush greenBtn(hoverAddExe ? SClrGreenHover : SClrGreen); 
        SolidBrush greenHoverStore(hoverAddStoreApp ? SClrGreenHover : SClrGreen);
        SolidBrush greenHoverTitle(hoverAddWindowTitle ? SClrGreenHover : SClrGreen);

        RectF b1(rightColX, btnY, btnW, 36.0f);
        GraphicsPath* p1 = GetBlockRoundRectPath(b1, 4); g.FillPath(&greenBtn, p1); delete p1;
        g.DrawString(L"Add Exe", -1, &fSmallerBold, b1, &fmtC, &brushWhite); 

        RectF b2(rightColX + btnW + 10.0f, btnY, btnW, 36.0f);
        GraphicsPath* p2 = GetBlockRoundRectPath(b2, 4); g.FillPath(&greenHoverStore, p2); delete p2;
        g.DrawString(L"Add Store", -1, &fSmallerBold, b2, &fmtC, &brushWhite); 

        RectF b3(rightColX + (btnW * 2) + 20.0f, btnY, btnW, 36.0f);
        GraphicsPath* p3 = GetBlockRoundRectPath(b3, 4); g.FillPath(&greenHoverTitle, p3); delete p3;
        g.DrawString(L"Add Title", -1, &fSmallerBold, b3, &fmtC, &brushWhite); 

        // ==========================================================
        // DROPDOWNS (always drawn on top)
        // ==========================================================
        if (isLangDropdownOpen && !isFocusActive) {
            RectF lDrop(langRect.X, langRect.Y + 26.0f, 100.0f, 50.0f);
            g.FillRectangle(&brushWhite, lDrop); g.DrawRectangle(&penBorder, lDrop.X, lDrop.Y, lDrop.Width, lDrop.Height);
            SolidBrush bBg(hoverOptBn ? SClrBgHover : SClrWhite);
            g.FillRectangle(&bBg, RectF(lDrop.X+1.0f, lDrop.Y+1.0f, lDrop.Width-2.0f, 24.0f));
            g.DrawString(L"Bangla", -1, &fNormal, RectF(lDrop.X+5.0f, lDrop.Y+1.0f, lDrop.Width, 24.0f), &fmtL, &brushDark);
            SolidBrush eBg(hoverOptEn ? SClrBgHover : SClrWhite);
            g.FillRectangle(&eBg, RectF(lDrop.X+1.0f, lDrop.Y+25.0f, lDrop.Width-2.0f, 24.0f));
            g.DrawString(L"English", -1, &fNormal, RectF(lDrop.X+5.0f, lDrop.Y+25.0f, lDrop.Width, 24.0f), &fmtL, &brushDark);
        }

        if (isWebComboOpen && !isFocusActive) {
            float listY = secY + 83.0f;
            RectF listRect(webComboX - 120.0f, listY, 150.0f, commonWebsites.size() * 30.0f + 10.0f);
            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);
            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;
            float itmY = listY + 5.0f;
            for (size_t i = 0; i < commonWebsites.size(); ++i) {
                SolidBrush optBg(hoverWebOptIdx == (int)i ? SClrBgHover : SClrWhite);
                g.FillRectangle(&optBg, RectF(listRect.X + 2.0f, itmY, listRect.Width - 4.0f, 30.0f));
                g.DrawString(commonWebsites[i].c_str(), -1, &fNormal, RectF(listRect.X + 10.0f, itmY, listRect.Width, 30.0f), &fmtL, &brushDark);
                itmY += 30.0f;
            }
        }

        if (isAppComboOpen && !isFocusActive) {
            float listY = appSectionY + 78.0f;
            RectF listRect(aComboX - 120.0f, listY, 150.0f, commonApps.size() * 30.0f + 10.0f);
            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);
            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;
            float itmY = listY + 5.0f;
            for (size_t i = 0; i < commonApps.size(); ++i) {
                SolidBrush optBg(hoverAppOptIdx == (int)i ? SClrBgHover : SClrWhite);
                g.FillRectangle(&optBg, RectF(listRect.X + 2.0f, itmY, listRect.Width - 4.0f, 30.0f));
                g.DrawString(commonApps[i].c_str(), -1, &fNormal, RectF(listRect.X + 10.0f, itmY, listRect.Width, 30.0f), &fmtL, &brushDark);
                itmY += 30.0f;
            }
        }

        if (isModeDropdownOpen && !isFocusActive) {
            float listY = rowY + 38.0f;
            RectF listRect(modeDropX, listY, 200.0f, 80.0f);
            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);
            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;
            SolidBrush opt1Bg(hoverOptAllow ? SClrBgHover : SClrWhite);
            g.FillRectangle(&opt1Bg, RectF(listRect.X + 2.0f, listY + 2.0f, listRect.Width - 4.0f, 38.0f));
            g.DrawString(L"Allow Apps & Web", -1, &fNormal, RectF(listRect.X + 15.0f, listY + 2.0f, listRect.Width, 38.0f), &fmtL, &brushDark);
            SolidBrush opt2Bg(hoverOptBlock ? SClrBgHover : SClrWhite);
            g.FillRectangle(&opt2Bg, RectF(listRect.X + 2.0f, listY + 40.0f, listRect.Width - 4.0f, 38.0f));
            g.DrawString(L"Block Apps & Web", -1, &fNormal, RectF(listRect.X + 15.0f, listY + 40.0f, listRect.Width, 38.0f), &fmtL, &brushDark);
        }

        if (isControlDropdownOpen && !isFocusActive) {
            float listY = ctrlDropY + 42.0f;
            RectF listRect(ctrlDropX, listY, 160.0f, 80.0f);
            GraphicsPath* listP = GetBlockRoundRectPath(listRect, 4);
            g.FillPath(&brushWhite, listP); g.DrawPath(&penBorder, listP); delete listP;
            SolidBrush opt1Bg(hoverOptSelf ? SClrBgHover : SClrWhite);
            g.FillRectangle(&opt1Bg, RectF(listRect.X + 2.0f, listY + 2.0f, listRect.Width - 4.0f, 38.0f));
            g.DrawString(L"Self Control",   -1, &fBold, RectF(listRect.X + 15.0f, listY + 2.0f,  listRect.Width, 38.0f), &fmtL, &brushDark);
            SolidBrush opt2Bg(hoverOptParent ? SClrBgHover : SClrWhite);
            g.FillRectangle(&opt2Bg, RectF(listRect.X + 2.0f, listY + 40.0f, listRect.Width - 4.0f, 38.0f));
            g.DrawString(L"Parent Control", -1, &fBold, RectF(listRect.X + 15.0f, listY + 40.0f, listRect.Width, 38.0f), &fmtL, &brushDark);
        }

        // =============================================
        // SYSTEM TOOLS DROPDOWN PANEL
        // =============================================
        if(isSysToolDropdownOpen && !isFocusActive) {
            float stListX = blockDropX;
            float stListY = blockDropY + 38.0f;
            float stW = 280.0f;
            float stItemH = 36.0f;
            // Count 3rd party apps from real-time scan
            int numThird = (int)min(scannedThirdPartyApps.size(), (size_t)8);
            float totalItems = 5.0f + numThird;
            RectF stRect(stListX, stListY, stW, totalItems * stItemH + 10.0f);
            GraphicsPath* stP = GetBlockRoundRectPath(stRect, 4);
            g.FillPath(&brushWhite, stP); g.DrawPath(&penBorder, stP); delete stP;

            // Header: System Tools
            g.DrawString(L"─── System Tools ───", -1, &fSmall,
                         RectF(stListX + 8.0f, stListY + 2.0f, stW - 16.0f, stItemH * 0.5f), &fmtL, &brushGray);

            float itmY = stListY + stItemH * 0.5f;
            for(int i = 0; i < 5; ++i) {
                bool blocked = systemToolBlocked[i];
                SolidBrush rowBg(hoverSysToolOptIdx == i ? SClrBgHover : SClrWhite);
                g.FillRectangle(&rowBg, RectF(stListX + 2.0f, itmY, stW - 4.0f, stItemH));

                // toggle checkbox
                SolidBrush togBrush(blocked ? SClrRed : SClrWhite);
                RectF togRect(stListX + 8.0f, itmY + 10.0f, 16.0f, 16.0f);
                g.FillRectangle(&togBrush, togRect); g.DrawRectangle(&penBorder, togRect.X, togRect.Y, togRect.Width, togRect.Height);
                if(blocked) g.DrawString(L"\xE73E", -1, &fSmallIcon, togRect, &fmtC, &brushWhite);

                SolidBrush nameBr(blocked ? SClrRed : SClrDark);
                g.DrawString(systemToolItems[i].c_str(), -1, &fNormal,
                             RectF(stListX + 32.0f, itmY, stW - 40.0f, stItemH), &fmtL, &nameBr);
                itmY += stItemH;
            }

            if(numThird > 0) {
                g.DrawLine(&penBorder, stListX + 10.0f, itmY, stListX + stW - 10.0f, itmY);
                g.DrawString(L"─── Running 3rd Party Apps ───", -1, &fSmall,
                             RectF(stListX + 8.0f, itmY, stW - 16.0f, stItemH * 0.5f), &fmtL, &brushGray);
                itmY += stItemH * 0.5f;

                for(int i = 0; i < numThird; ++i) {
                    int globalIdx = 5 + i;
                    SolidBrush rowBg2(hoverSysToolOptIdx == globalIdx ? SClrBgHover : SClrWhite);
                    g.FillRectangle(&rowBg2, RectF(stListX + 2.0f, itmY, stW - 4.0f, stItemH));

                    // Check if already in appList
                    bool alreadyBlocked = false;
                    wstring appName = scannedThirdPartyApps[i];
                    for(auto& a : appList) if(toLowerW_Blocks(a.name) == toLowerW_Blocks(appName)) { alreadyBlocked = true; break; }

                    SolidBrush addBr(alreadyBlocked ? SClrGrayText : SClrGreen);
                    g.DrawString(alreadyBlocked ? L"✓" : L"+ Block", -1, &fSmallerBold,
                                 RectF(stListX + 8.0f, itmY, 55.0f, stItemH), &fmtL, &addBr);
                    g.DrawString(appName.c_str(), -1, &fSmall,
                                 RectF(stListX + 68.0f, itmY, stW - 75.0f, stItemH), &fmtL, &brushDark);
                    itmY += stItemH;
                }
            }
        }

    } 
    else if (currentBlockTab == 1) {
        DrawScheduleBlocksTab(g, boxX, bodyY + 20.0f, boxW, boxH);
    }
    
    // ==========================================
    // FULL SCREEN OVERLAYS
    // ==========================================
    bool anyOverlay = showTimeOverlay || showPassOverlay || showStoreOverlay || showTitleOverlay
                   || showSetPasswordOverlay || showLongTextEditOverlay || showLongTextUnlockOverlay;
    if (anyOverlay) {
        SolidBrush overlayBg(SClrOverlay);
        g.FillRectangle(&overlayBg, contentX, contentY, contentW, contentH);

        // Compute overlay size
        float ovW = 400.0f, ovH = 260.0f;
        if(showStoreOverlay)          { ovW = 500.0f; ovH = 450.0f; }
        if(showTimeOverlay)           { ovW = 460.0f; ovH = 280.0f; } // bigger for Month/Day/Hour/Min
        if(showLongTextEditOverlay)   { ovW = 520.0f; ovH = 340.0f; }
        if(showLongTextUnlockOverlay) { ovW = 560.0f; ovH = 340.0f; }
        if(showSetPasswordOverlay)    { ovW = 420.0f; ovH = 280.0f; }

        float ovX = contentX + (contentW - ovW) / 2.0f;
        float ovY = contentY + (contentH - ovH) / 2.0f;
        RectF ovRect(ovX, ovY, ovW, ovH);
        GraphicsPath* op = GetBlockRoundRectPath(ovRect, 8);
        g.FillPath(&brushBg, op); g.DrawPath(&penBorder, op); delete op;

        // ==========================================
        // STORE OVERLAY
        // ==========================================
        if (showStoreOverlay) {
            g.DrawString(L"ADD MICROSOFT STORE APPS", -1, &fTitle, RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fmtC, &brushDark);
            g.DrawLine(&penBorder, ovX + 30.0f, ovY + 60.0f, ovX + ovW - 30.0f, ovY + 60.0f);
            RectF clipRect(ovX + 10.0f, ovY + 65.0f, ovW - 20.0f, ovH - 120.0f);
            Region oldClip2; g.GetClip(&oldClip2);
            g.SetClip(clipRect);
            float listY = ovY + 70.0f - cStoreScrollY;
            for (size_t i = 0; i < systemStoreApps.size(); ++i) {
                if (listY > ovY - 20.0f && listY < ovY + ovH) {
                    RectF addBtn(ovX + 30.0f, listY + 5.0f, 60.0f, 30.0f);
                    GraphicsPath* aap = GetBlockRoundRectPath(addBtn, 4);
                    SolidBrush aBr(hoverStoreAddIdx == (int)i ? SClrGreenHover : SClrGreen);
                    g.FillPath(&aBr, aap); delete aap;
                    g.DrawString(L"Add", -1, &fBold, addBtn, &fmtC, &brushWhite);
                    g.DrawString(systemStoreApps[i].c_str(), -1, &fNormal, RectF(ovX + 110.0f, listY, 300.0f, 40.0f), &fmtL, &brushDark);
                }
                listY += 45.0f;
            }
            g.SetClip(&oldClip2);
            float totalH = systemStoreApps.size() * 45.0f;
            float visibleH = ovH - 120.0f;
            if (totalH > visibleH) {
                float maxScroll = totalH - visibleH;
                float thumbH2 = max(20.0f, (visibleH / totalH) * visibleH);
                float thumbY2 = ovY + 70.0f + (cStoreScrollY / maxScroll) * (visibleH - thumbH2);
                g.FillRectangle(&brushGray, ovX + ovW - 8.0f, thumbY2, 4.0f, thumbH2);
            }
            RectF closeBtn(ovX + ovW - 120.0f, ovY + ovH - 50.0f, 90.0f, 35.0f);
            GraphicsPath* cp = GetBlockRoundRectPath(closeBtn, 4);
            SolidBrush cBr(hoverStoreClose ? SClrGreenHover : SClrGreen);
            g.FillPath(&cBr, cp); delete cp;
            g.DrawString(L"Close", -1, &fBold, closeBtn, &fmtC, &brushWhite);
        }
        // ==========================================
        // TITLE OVERLAY
        // ==========================================
        else if (showTitleOverlay) {
            g.DrawString(L"ENTER WINDOW TITLE", -1, &fTitle, RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fmtC, &brushDark);
            RectF titleInpRect(ovX + 40.0f, ovY + 80.0f, ovW - 80.0f, 40.0f);
            GraphicsPath* pp = GetBlockRoundRectPath(titleInpRect, 4);
            Pen pTealTitle(SClrTeal, 2.0f);
            g.FillPath(&brushWhite, pp); g.DrawPath(isTitleInputActive ? &pTealTitle : &penBorder, pp); delete pp;
            if (inputTitleText.empty() && !isTitleInputActive)
                g.DrawString(L"e.g. Google Chrome", -1, &fNormal, titleInpRect, &fmtC, &brushGray);
            else {
                g.DrawString(inputTitleText.c_str(), -1, &fNormal, RectF(ovX + 50.0f, ovY + 85.0f, ovW - 100.0f, 30.0f), &fmtL, &brushDark);
                if (isTitleInputActive && (GetTickCount() / 500) % 2 == 0) {
                     Graphics gTemp(GetDesktopWindow()); RectF bRect;
                     gTemp.MeasureString(inputTitleText.c_str(), -1, &fNormal, PointF(0,0), &bRect);
                     float cursorX = ovX + 52.0f + (inputTitleText.empty() ? 0.0f : bRect.Width);
                     g.FillRectangle(&brushDark, cursorX, ovY + 90.0f, 1.5f, 20.0f);
                }
            }
            RectF cancelRect(ovX + 40.0f, ovY + 150.0f, 140.0f, 40.0f);
            GraphicsPath* cp = GetBlockRoundRectPath(cancelRect, 4);
            SolidBrush cancelBrush(hTitleCancel ? SClrBgHover : SClrWhite);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&penBorder, cp); delete cp;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fmtC, &brushDark);
            RectF confRect(ovX + 200.0f, ovY + 150.0f, 160.0f, 40.0f);
            GraphicsPath* sp = GetBlockRoundRectPath(confRect, 4);
            SolidBrush confBrush(hTitleAdd ? SClrTealHover : SClrTeal);
            g.FillPath(&confBrush, sp); delete sp;
            g.DrawString(L"Add Title", -1, &fBold, confRect, &fmtC, &brushWhite);
        }
        // ==========================================
        // TIME OVERLAY — Month/Day/Hour/Min spinners
        // ==========================================
        else if (showTimeOverlay) {
            g.DrawString(L"SET FOCUS DURATION", -1, &fTitle, RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fmtC, &brushDark);

            // Row 1: Months + Days
            float spinRow1Y = ovY + 70.0f;
            float spinRow2Y = ovY + 125.0f;
            float col1X = ovX + 20.0f;
            float col2X = ovX + 250.0f;

            g.DrawString(L"Months:", -1, &fBold, RectF(col1X, spinRow1Y, 70.0f, 36.0f), &fmtL, &brushDark);
            DrawBlocksOverlaySpinner(g, col1X + 70.0f, spinRow1Y, to_wstring(selfMonths), hSelfMonthM, hSelfMonthP, &fIcon, &fBold);

            g.DrawString(L"Days:", -1, &fBold, RectF(col2X, spinRow1Y, 55.0f, 36.0f), &fmtL, &brushDark);
            DrawBlocksOverlaySpinner(g, col2X + 55.0f, spinRow1Y, to_wstring(selfDays), hSelfDayM, hSelfDayP, &fIcon, &fBold);

            g.DrawString(L"Hours:", -1, &fBold, RectF(col1X, spinRow2Y, 70.0f, 36.0f), &fmtL, &brushDark);
            DrawBlocksOverlaySpinner(g, col1X + 70.0f, spinRow2Y, to_wstring(selfHours), hSelfHourM, hSelfHourP, &fIcon, &fBold);

            g.DrawString(L"Mins:", -1, &fBold, RectF(col2X, spinRow2Y, 55.0f, 36.0f), &fmtL, &brushDark);
            DrawBlocksOverlaySpinner(g, col2X + 55.0f, spinRow2Y, to_wstring(selfMins), hSelfMinM, hSelfMinP, &fIcon, &fBold);

            // Summary
            time_t total = ComputeFocusSeconds();
            int d = (int)(total / 86400), h = (int)((total % 86400) / 3600), m = (int)((total % 3600) / 60);
            wstring summary = L"Total: ";
            if(d > 0) summary += to_wstring(d) + L"d ";
            if(h > 0) summary += to_wstring(h) + L"h ";
            summary += to_wstring(m) + L"m";
            g.DrawString(summary.c_str(), -1, &fInfo, RectF(ovX + 20.0f, ovY + 175.0f, ovW - 40.0f, 24.0f), &fmtC, &brushTeal);

            RectF cancelRect(ovX + 50.0f, ovY + 210.0f, 140.0f, 40.0f);
            GraphicsPath* cp = GetBlockRoundRectPath(cancelRect, 4);
            SolidBrush cancelBrush(hTimeCancel ? SClrBgHover : SClrWhite);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&penBorder, cp); delete cp;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fmtC, &brushDark);

            RectF startRect(ovX + 270.0f, ovY + 210.0f, 140.0f, 40.0f);
            GraphicsPath* sp = GetBlockRoundRectPath(startRect, 4);
            SolidBrush startBrush(hTimeStart ? SClrTealHover : SClrTeal);
            g.FillPath(&startBrush, sp); delete sp;
            g.DrawString(L"Start Focus", -1, &fBold, startRect, &fmtC, &brushWhite);
        }
        // ==========================================
        // PASSWORD OVERLAY (Parent / Stop)
        // ==========================================
        else if (showPassOverlay) {
            wstring titleTxt = isStoppingFocus ? L"ENTER PARENT PASSWORD TO STOP" : L"SET / ENTER PARENT PASSWORD";
            g.DrawString(titleTxt.c_str(), -1, &fBold, RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fmtC, &brushDark);

            if(!isStoppingFocus) {
                // Info text
                g.DrawString(L"Enter a password to lock focus. Only this password can stop it.", -1, &fInfo,
                             RectF(ovX + 30.0f, ovY + 55.0f, ovW - 60.0f, 30.0f), &fmtL, &brushGray);
            }

            RectF passInpRect(ovX + 40.0f, ovY + 100.0f, ovW - 80.0f, 40.0f);
            GraphicsPath* pp = GetBlockRoundRectPath(passInpRect, 4);
            Pen pTealPass(SClrTeal, 2.0f);
            g.FillPath(&brushWhite, pp); g.DrawPath(isPassInputActive ? &pTealPass : &penBorder, pp); delete pp;
            
            wstring displayPass = wstring(inputPassText.length(), L'*');
            if (inputPassText.empty() && !isPassInputActive)
                g.DrawString(L"Type password here...", -1, &fNormal, passInpRect, &fmtC, &brushGray);
            else {
                g.DrawString(displayPass.c_str(), -1, &fTitle, RectF(ovX + 50.0f, ovY + 105.0f, ovW - 100.0f, 30.0f), &fmtL, &brushDark);
                if (isPassInputActive && (GetTickCount() / 500) % 2 == 0) {
                     Graphics gTemp(GetDesktopWindow()); RectF bRect;
                     gTemp.MeasureString(displayPass.c_str(), -1, &fTitle, PointF(0,0), &bRect);
                     float cursorX = ovX + 52.0f + (displayPass.empty() ? 0.0f : bRect.Width);
                     g.FillRectangle(&brushDark, cursorX, ovY + 108.0f, 1.5f, 20.0f);
                }
            }

            // Error message
            if(setPassError) {
                g.DrawString(L"Incorrect password! Try again.", -1, &fInfo,
                             RectF(ovX + 30.0f, ovY + 148.0f, ovW - 60.0f, 24.0f), &fmtC, &brushRed);
            }

            RectF cancelRect(ovX + 40.0f, ovY + 180.0f, 140.0f, 40.0f);
            GraphicsPath* cp = GetBlockRoundRectPath(cancelRect, 4);
            SolidBrush cancelBrush(hPassCancel ? SClrBgHover : SClrWhite);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&penBorder, cp); delete cp;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fmtC, &brushDark);

            RectF confRect(ovX + 200.0f, ovY + 180.0f, 160.0f, 40.0f);
            GraphicsPath* sp = GetBlockRoundRectPath(confRect, 4);
            SolidBrush confBrush(hPassConfirm ? SClrTealHover : SClrTeal);
            g.FillPath(&confBrush, sp); delete sp;
            g.DrawString(isStoppingFocus ? L"Unlock Focus" : L"Set & Start", -1, &fBold, confRect, &fmtC, &brushWhite);
        }
        // ==========================================
        // LONG TEXT EDIT OVERLAY
        // ==========================================
        else if(showLongTextEditOverlay) {
            g.DrawString(L"EDIT UNLOCK PHRASE", -1, &fTitle, RectF(ovX, ovY + 15.0f, ovW, 30.0f), &fmtC, &brushDark);
            g.DrawString(L"User must type this text EXACTLY to stop focus (case-sensitive).", -1, &fInfo,
                         RectF(ovX + 20.0f, ovY + 50.0f, ovW - 40.0f, 24.0f), &fmtL, &brushGray);

            RectF editRect(ovX + 20.0f, ovY + 80.0f, ovW - 40.0f, 180.0f);
            GraphicsPath* erp = GetBlockRoundRectPath(editRect, 4);
            Pen pTealEdit(SClrTeal, 2.0f);
            g.FillPath(&brushWhite, erp); g.DrawPath(isLTEditActive ? &pTealEdit : &penBorder, erp); delete erp;
            
            StringFormat wrapFmt; 
            wrapFmt.SetAlignment(StringAlignmentNear); 
            wrapFmt.SetLineAlignment(StringAlignmentNear);
            wrapFmt.SetFormatFlags(StringFormatFlagsNoClip);
            wstring editDisplay = longTextEditBuffer.empty() ? L"Type your custom unlock phrase here..." : longTextEditBuffer;
            SolidBrush editTextBrush(longTextEditBuffer.empty() ? SClrGrayText : SClrDark);
            g.DrawString(editDisplay.c_str(), -1, &fSmall, 
                         RectF(editRect.X + 8.0f, editRect.Y + 8.0f, editRect.Width - 16.0f, editRect.Height - 16.0f),
                         &wrapFmt, &editTextBrush);

            // word count
            int wc = 0; bool inWord = false;
            for(auto ch : longTextEditBuffer) { if(ch == L' ' || ch == L'\n') inWord = false; else if(!inWord) { inWord = true; wc++; } }
            g.DrawString((to_wstring(wc) + L" words").c_str(), -1, &fSmall,
                         RectF(ovX + 20.0f, ovY + 265.0f, 100.0f, 24.0f), &fmtL, &brushGray);

            RectF ltCancelRect(ovX + 130.0f, ovY + 290.0f, 130.0f, 36.0f);
            GraphicsPath* lcp = GetBlockRoundRectPath(ltCancelRect, 4);
            SolidBrush ltCancelBrush(hLTEditCancel ? SClrBgHover : SClrWhite);
            g.FillPath(&ltCancelBrush, lcp); g.DrawPath(&penBorder, lcp); delete lcp;
            g.DrawString(L"Cancel", -1, &fBold, ltCancelRect, &fmtC, &brushDark);

            RectF ltConfRect(ovX + 280.0f, ovY + 290.0f, 130.0f, 36.0f);
            GraphicsPath* lsp = GetBlockRoundRectPath(ltConfRect, 4);
            SolidBrush ltConfBrush(hLTEditConfirm ? SClrGreenHover : SClrGreen);
            g.FillPath(&ltConfBrush, lsp); delete lsp;
            g.DrawString(L"Save Phrase", -1, &fBold, ltConfRect, &fmtC, &brushWhite);
        }
        // ==========================================
        // LONG TEXT UNLOCK OVERLAY (must type exact)
        // ==========================================
        else if(showLongTextUnlockOverlay) {
            g.DrawString(L"TYPE THE UNLOCK PHRASE TO STOP", -1, &fBold, RectF(ovX, ovY + 15.0f, ovW, 30.0f), &fmtC, &brushDark);
            
            wstring targetPhrase = (useCustomLongText && !customLongText.empty()) ? customLongText : longTextPhrase;
            
            // Show phrase to type
            RectF phraseRect(ovX + 20.0f, ovY + 55.0f, ovW - 40.0f, 90.0f);
            GraphicsPath* prp = GetBlockRoundRectPath(phraseRect, 4);
            SolidBrush phraseBg(Color(255, 255, 250, 230));
            g.FillPath(&phraseBg, prp); g.DrawPath(&penBorder, prp); delete prp;
            StringFormat wrapFmt2; 
            wrapFmt2.SetAlignment(StringAlignmentNear); wrapFmt2.SetLineAlignment(StringAlignmentNear);
            g.DrawString(targetPhrase.c_str(), -1, &fSmall,
                         RectF(phraseRect.X + 8.0f, phraseRect.Y + 8.0f, phraseRect.Width - 16.0f, phraseRect.Height - 16.0f),
                         &wrapFmt2, &brushDark);

            g.DrawString(L"Type exactly as shown above:", -1, &fInfo,
                         RectF(ovX + 20.0f, ovY + 152.0f, ovW - 40.0f, 22.0f), &fmtL, &brushGray);

            RectF ltInput(ovX + 20.0f, ovY + 175.0f, ovW - 40.0f, 60.0f);
            GraphicsPath* ltip = GetBlockRoundRectPath(ltInput, 4);
            Pen pTealLT(SClrTeal, 2.0f);
            g.FillPath(&brushWhite, ltip); g.DrawPath(isLTUnlockInputActive ? &pTealLT : &penBorder, ltip); delete ltip;
            StringFormat wrapFmt3;
            wrapFmt3.SetAlignment(StringAlignmentNear); wrapFmt3.SetLineAlignment(StringAlignmentNear);
            SolidBrush ltInputBrush(longTextUnlockInput.empty() ? SClrGrayText : SClrDark);
            g.DrawString(longTextUnlockInput.empty() ? L"Start typing here..." : longTextUnlockInput.c_str(), -1, &fSmall,
                         RectF(ltInput.X + 8.0f, ltInput.Y + 8.0f, ltInput.Width - 16.0f, ltInput.Height - 16.0f),
                         &wrapFmt3, &ltInputBrush);

            // Progress indicator
            size_t matched = 0;
            for(size_t i = 0; i < longTextUnlockInput.size() && i < targetPhrase.size(); ++i) {
                if(longTextUnlockInput[i] == targetPhrase[i]) matched++; else break;
            }
            float progress = targetPhrase.empty() ? 0.0f : (float)matched / targetPhrase.size();
            RectF progBg(ovX + 20.0f, ovY + 242.0f, ovW - 40.0f, 8.0f);
            g.FillRectangle(&brushBg, progBg); g.DrawRectangle(&penBorder, progBg.X, progBg.Y, progBg.Width, progBg.Height);
            if(progress > 0.0f) {
                SolidBrush progBrush(progress >= 1.0f ? SClrGreen : SClrTeal);
                g.FillRectangle(&progBrush, RectF(progBg.X, progBg.Y, progBg.Width * progress, progBg.Height));
            }

            if(ltUnlockError) {
                g.DrawString(L"Text does not match. Type exactly!", -1, &fInfo,
                             RectF(ovX + 20.0f, ovY + 255.0f, ovW - 40.0f, 22.0f), &fmtC, &brushRed);
            }

            RectF ltCancelRect(ovX + 60.0f, ovY + 282.0f, 140.0f, 38.0f);
            GraphicsPath* lcp2 = GetBlockRoundRectPath(ltCancelRect, 4);
            SolidBrush ltCB2(hLTUnlockCancel ? SClrBgHover : SClrWhite);
            g.FillPath(&ltCB2, lcp2); g.DrawPath(&penBorder, lcp2); delete lcp2;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, ltCancelRect, &fmtC, &brushDark);

            RectF ltConfRect2(ovX + 230.0f, ovY + 282.0f, 140.0f, 38.0f);
            GraphicsPath* lsp2 = GetBlockRoundRectPath(ltConfRect2, 4);
            SolidBrush ltCF2(hLTUnlockConfirm ? SClrGreenHover : SClrGreen);
            g.FillPath(&ltCF2, lsp2); delete lsp2;
            g.DrawString(L"Confirm Stop", -1, &fBold, ltConfRect2, &fmtC, &brushWhite);
        }
    }
}

// ==========================================
// MOUSE MOVE LOGIC
// ==========================================
void ProcessBlocksMouseMove(float x, float y) {
    float contentX = s_contentX, contentY = s_contentY, contentW = s_contentW, contentH = s_contentH;

    // Reset all hover states
    hTimeStart = false; hTimeCancel = false;
    hSelfMonthM = false; hSelfMonthP = false;
    hSelfDayM   = false; hSelfDayP   = false;
    hSelfHourM  = false; hSelfHourP  = false;
    hSelfMinM   = false; hSelfMinP   = false;
    hPassInput = false; hPassConfirm = false; hPassCancel = false;
    hoverStoreClose = false; hoverStoreAddIdx = -1;
    hTitleInput = false; hTitleAdd = false; hTitleCancel = false;
    hoverControlDropdown = false; hoverModeDropdown = false;
    hoverWebCombo = false; hoverAppCombo = false;
    hoverLangDropdown = false; hoverOptBn = false; hoverOptEn = false;
    hoverOptSelf = false; hoverOptParent = false;
    hoverQuotesCheckbox = false; hoverWebInput = false; hoverWebAddBtn = false;
    hoverAppInput = false; hoverAppAddBtn = false;
    hoverAddExe = false; hoverAddStoreApp = false; hoverAddWindowTitle = false;
    hoverAppOptIdx = -1; hoverWebOptIdx = -1;
    hoverStartFocusBtn = false;
    hoverSysToolDropdown = false; hoverSysToolOptIdx = -1;
    hoverLongTextCheck = false; hoverCustomLTCheck = false;
    hLTEditConfirm = false; hLTEditCancel = false;
    hLTUnlockConfirm = false; hLTUnlockCancel = false;
    hSetPass1 = false; hSetPass2 = false; hSetPassConfirm = false; hSetPassCancel = false;
    for (auto& item : webList) item.isHoveredCross = false;
    for (auto& item : appList) item.isHoveredCross = false;

    bool anyOverlay = showTimeOverlay || showPassOverlay || showStoreOverlay || showTitleOverlay
                   || showSetPasswordOverlay || showLongTextEditOverlay || showLongTextUnlockOverlay;

    if (anyOverlay) {
        float ovW = 400.0f, ovH = 260.0f;
        if(showStoreOverlay)          { ovW = 500.0f; ovH = 450.0f; }
        if(showTimeOverlay)           { ovW = 460.0f; ovH = 280.0f; }
        if(showLongTextEditOverlay)   { ovW = 520.0f; ovH = 340.0f; }
        if(showLongTextUnlockOverlay) { ovW = 560.0f; ovH = 340.0f; }
        if(showSetPasswordOverlay)    { ovW = 420.0f; ovH = 280.0f; }
        float ovX = contentX + (contentW - ovW) / 2.0f;
        float ovY = contentY + (contentH - ovH) / 2.0f;

        if (showStoreOverlay) {
            float listY = ovY + 70.0f - cStoreScrollY;
            for (size_t i = 0; i < systemStoreApps.size(); ++i) {
                if (RectF(ovX + 30.0f, listY + 5.0f, 60.0f, 30.0f).Contains(x, y)) hoverStoreAddIdx = (int)i;
                listY += 45.0f;
            }
            if (RectF(ovX + ovW - 120.0f, ovY + ovH - 50.0f, 90.0f, 35.0f).Contains(x, y)) hoverStoreClose = true;
        }
        else if (showTitleOverlay) {
            if (RectF(ovX + 40.0f, ovY + 80.0f, ovW - 80.0f, 40.0f).Contains(x, y)) hTitleInput = true;
            if (RectF(ovX + 40.0f, ovY + 150.0f, 140.0f, 40.0f).Contains(x, y)) hTitleCancel = true;
            if (RectF(ovX + 200.0f, ovY + 150.0f, 160.0f, 40.0f).Contains(x, y)) hTitleAdd = true;
        }
        else if (showTimeOverlay) {
            float col1X = ovX + 20.0f, col2X = ovX + 250.0f;
            float spinRow1Y = ovY + 70.0f, spinRow2Y = ovY + 125.0f;
            // Months
            if (RectF(col1X + 70.0f, spinRow1Y, 32.0f, 36.0f).Contains(x, y)) hSelfMonthM = true;
            if (RectF(col1X + 70.0f + 92.0f, spinRow1Y, 32.0f, 36.0f).Contains(x, y)) hSelfMonthP = true;
            // Days
            if (RectF(col2X + 55.0f, spinRow1Y, 32.0f, 36.0f).Contains(x, y)) hSelfDayM = true;
            if (RectF(col2X + 55.0f + 92.0f, spinRow1Y, 32.0f, 36.0f).Contains(x, y)) hSelfDayP = true;
            // Hours
            if (RectF(col1X + 70.0f, spinRow2Y, 32.0f, 36.0f).Contains(x, y)) hSelfHourM = true;
            if (RectF(col1X + 70.0f + 92.0f, spinRow2Y, 32.0f, 36.0f).Contains(x, y)) hSelfHourP = true;
            // Mins
            if (RectF(col2X + 55.0f, spinRow2Y, 32.0f, 36.0f).Contains(x, y)) hSelfMinM = true;
            if (RectF(col2X + 55.0f + 92.0f, spinRow2Y, 32.0f, 36.0f).Contains(x, y)) hSelfMinP = true;
            if (RectF(ovX + 50.0f, ovY + 210.0f, 140.0f, 40.0f).Contains(x, y)) hTimeCancel = true;
            if (RectF(ovX + 270.0f, ovY + 210.0f, 140.0f, 40.0f).Contains(x, y)) hTimeStart = true;
        }
        else if (showPassOverlay) {
            if (RectF(ovX + 40.0f, ovY + 100.0f, ovW - 80.0f, 40.0f).Contains(x, y)) hPassInput = true;
            if (RectF(ovX + 40.0f, ovY + 180.0f, 140.0f, 40.0f).Contains(x, y)) hPassCancel = true;
            if (RectF(ovX + 200.0f, ovY + 180.0f, 160.0f, 40.0f).Contains(x, y)) hPassConfirm = true;
        }
        else if (showLongTextEditOverlay) {
            if (RectF(ovX + 20.0f, ovY + 80.0f, ovW - 40.0f, 180.0f).Contains(x, y)) isLTEditActive = true;
            if (RectF(ovX + 130.0f, ovY + 290.0f, 130.0f, 36.0f).Contains(x, y)) hLTEditCancel = true;
            if (RectF(ovX + 280.0f, ovY + 290.0f, 130.0f, 36.0f).Contains(x, y)) hLTEditConfirm = true;
        }
        else if (showLongTextUnlockOverlay) {
            if (RectF(ovX + 60.0f, ovY + 282.0f, 140.0f, 38.0f).Contains(x, y)) hLTUnlockCancel = true;
            if (RectF(ovX + 230.0f, ovY + 282.0f, 140.0f, 38.0f).Contains(x, y)) hLTUnlockConfirm = true;
        }
        return; 
    }

    // Tabs
    hoverBlockTab = -1;
    float tabW = 200.0f, tabH = 40.0f;
    float tab1X = contentX + 20.0f, tab2X = tab1X + tabW + 10.0f, tab3X = tab2X + tabW + 10.0f, tabY = contentY + 10.0f;
    if (y >= contentY && y <= contentY + 60.0f) {
        if (RectF(tab1X, tabY, tabW, tabH).Contains(x, y)) hoverBlockTab = 0;
        else if (RectF(tab2X, tabY, tabW, tabH).Contains(x, y)) hoverBlockTab = 1;
        else if (RectF(tab3X, tabY, tabW, tabH).Contains(x, y)) hoverBlockTab = 2;
    }

    if (currentBlockTab == 0) {
        float bodyY     = contentY + 60.0f;
        float boxX      = contentX + 30.0f;
        float boxW      = contentW - 60.0f;
        float ctrlDropX = boxX + 30.0f;
        float ctrlDropY = bodyY + 40.0f;
        float modeDropX = boxX + 150.0f;
        float rowY      = bodyY + 40.0f;
        float secY      = rowY + 65.0f;  // after divider
        float colW      = (boxW - 90.0f) / 2.0f;
        float leftColX  = boxX + 30.0f;
        float rightColX = boxX + 60.0f + colW;
        float webComboX2 = leftColX + colW - 105.0f;
        float aComboX   = rightColX + colW - 105.0f;
        float qY        = secY;
        float ltY       = qY + 25.0f;
        float appSectionY = secY + 25.0f + (useLongTextUnlock ? 50.0f : 0.0f);
        float blockDropX = modeDropX + 220.0f;
        float blockDropY = rowY;

        // System tools dropdown items hover
        if (isSysToolDropdownOpen && !isFocusActive) {
            float stListY = blockDropY + 38.0f;
            float stItemH = 36.0f;
            for(int i = 0; i < 5; ++i) {
                if(RectF(blockDropX + 2.0f, stListY + stItemH * 0.5f + i * stItemH, 278.0f, stItemH).Contains(x, y))
                    { hoverSysToolOptIdx = i; return; }
            }
            int numThird = (int)min(scannedThirdPartyApps.size(), (size_t)8);
            float thirdStart = stListY + stItemH * 0.5f + 5 * stItemH + stItemH * 0.5f;
            for(int i = 0; i < numThird; ++i) {
                if(RectF(blockDropX + 2.0f, thirdStart + i * stItemH, 278.0f, stItemH).Contains(x, y))
                    { hoverSysToolOptIdx = 5 + i; return; }
            }
        }

        // Dropdown items
        if (isLangDropdownOpen && !isFocusActive) {
            RectF langRect(rightColX + 185.0f, qY - 2.0f, 100.0f, 24.0f);
            RectF lDrop(langRect.X, langRect.Y + 26.0f, 100.0f, 50.0f);
            if (RectF(lDrop.X, lDrop.Y, lDrop.Width, 24.0f).Contains(x, y))    { hoverOptBn = true; return; }
            if (RectF(lDrop.X, lDrop.Y+25.0f, lDrop.Width, 24.0f).Contains(x, y)) { hoverOptEn = true; return; }
        }
        if (isControlDropdownOpen && !isFocusActive) {
            float listY = ctrlDropY + 42.0f;
            if (RectF(ctrlDropX + 2.0f, listY + 2.0f, 156.0f, 38.0f).Contains(x, y))  { hoverOptSelf   = true; return; }
            if (RectF(ctrlDropX + 2.0f, listY + 40.0f, 156.0f, 38.0f).Contains(x, y)) { hoverOptParent = true; return; }
        }
        if (isModeDropdownOpen && !isFocusActive) {
            float listY = rowY + 38.0f;
            if (RectF(modeDropX + 2.0f, listY + 2.0f, 196.0f, 38.0f).Contains(x, y))  { hoverOptAllow = true; return; }
            if (RectF(modeDropX + 2.0f, listY + 40.0f, 196.0f, 38.0f).Contains(x, y)) { hoverOptBlock = true; return; }
        }
        if (isWebComboOpen && !isFocusActive) {
            float listY = secY + 83.0f;
            float itmY = listY + 5.0f;
            for (size_t i = 0; i < commonWebsites.size(); ++i) {
                if (RectF(webComboX2 - 120.0f + 2.0f, itmY, 146.0f, 30.0f).Contains(x, y)) { hoverWebOptIdx = (int)i; return; }
                itmY += 30.0f;
            }
        }
        if (isAppComboOpen && !isFocusActive) {
            float listY = appSectionY + 78.0f;
            float itmY = listY + 5.0f;
            for (size_t i = 0; i < commonApps.size(); ++i) {
                if (RectF(aComboX - 120.0f + 2.0f, itmY, 146.0f, 30.0f).Contains(x, y)) { hoverAppOptIdx = (int)i; return; }
                itmY += 30.0f;
            }
        }

        // Start button
        if (!(isFocusActive && controlMode == 0 && std::time(nullptr) < focusEndTimeBlocks)) {
            if (RectF(boxX + 205.0f, ctrlDropY, 150.0f, 40.0f).Contains(x, y)) hoverStartFocusBtn = true;
        }

        if (!isFocusActive) {
            if (RectF(ctrlDropX, ctrlDropY, 160.0f, 40.0f).Contains(x, y)) hoverControlDropdown = true;
            if (RectF(modeDropX, rowY, 200.0f, 36.0f).Contains(x, y)) hoverModeDropdown = true;
            if (RectF(blockDropX, blockDropY, 200.0f, 36.0f).Contains(x, y)) hoverSysToolDropdown = true;
            if (RectF(rightColX, qY + 2.0f, 170.0f, 20.0f).Contains(x, y)) hoverQuotesCheckbox = true;
            if (RectF(rightColX + 185.0f, qY - 2.0f, 100.0f, 24.0f).Contains(x, y)) hoverLangDropdown = true;
            if (RectF(rightColX, ltY + 2.0f, 160.0f, 20.0f).Contains(x, y)) hoverLongTextCheck = true;
            if (useLongTextUnlock) {
                float ltSub = ltY + 20.0f;
                if (RectF(rightColX + 20.0f, ltSub + 2.0f, 130.0f, 18.0f).Contains(x, y)) hoverCustomLTCheck = true;
            }
            if (RectF(webComboX2, secY + 45.0f, 30.0f, 36.0f).Contains(x, y)) hoverWebCombo = true;
            if (RectF(aComboX, appSectionY + 40.0f, 30.0f, 36.0f).Contains(x, y)) hoverAppCombo = true;
        }

        if (RectF(leftColX, secY + 45.0f, colW - 110.0f, 36.0f).Contains(x, y)) hoverWebInput = true;
        if (RectF(leftColX + colW - 70.0f, secY + 45.0f, 70.0f, 36.0f).Contains(x, y)) hoverWebAddBtn = true;
        if (RectF(rightColX, appSectionY + 40.0f, colW - 110.0f, 36.0f).Contains(x, y)) hoverAppInput = true;
        if (RectF(rightColX + colW - 70.0f, appSectionY + 40.0f, 70.0f, 36.0f).Contains(x, y)) hoverAppAddBtn = true;

        float btnW = (colW - 20.0f) / 3.0f;
        float btnY = appSectionY + 215.0f;
        if (RectF(rightColX, btnY, btnW, 36.0f).Contains(x, y)) hoverAddExe = true;
        if (RectF(rightColX + btnW + 10.0f, btnY, btnW, 36.0f).Contains(x, y)) hoverAddStoreApp = true;
        if (RectF(rightColX + (btnW * 2) + 20.0f, btnY, btnW, 36.0f).Contains(x, y)) hoverAddWindowTitle = true;

        if (!isFocusActive) {
            float itemY2 = secY + 90.0f + 5.0f - cWebScrollY;
            for (size_t i = 0; i < webList.size(); ++i) {
                if (itemY2 > secY + 90.0f - 30.0f && itemY2 < secY + 90.0f + 160.0f) {
                    if (RectF(leftColX + colW - 30.0f, itemY2, 30.0f, 30.0f).Contains(x, y)) webList[i].isHoveredCross = true;
                }
                itemY2 += 30.0f;
            }
            float aItemY2 = appSectionY + 85.0f + 5.0f - cAppScrollY;
            for (size_t i = 0; i < appList.size(); ++i) {
                if (!appList[i].isSystemLocked && aItemY2 > appSectionY + 85.0f - 30.0f && aItemY2 < appSectionY + 85.0f + 120.0f) {
                    if (RectF(rightColX + colW - 30.0f, aItemY2, 30.0f, 30.0f).Contains(x, y)) appList[i].isHoveredCross = true;
                }
                aItemY2 += 30.0f;
            }
        }
    } else if (currentBlockTab == 1) {
        ProcessScheduleBlocksMouseMove(x, y);
    }
}

// ==========================================
// MOUSE CLICK LOGIC
// ==========================================
void ProcessBlocksMouseClick(float x, float y) {
    float contentX = s_contentX, contentY = s_contentY, contentW = s_contentW, contentH = s_contentH;

    // ---- STORE OVERLAY ----
    if (showStoreOverlay) {
        if (hoverStoreAddIdx != -1 && hoverStoreAddIdx < (int)systemStoreApps.size()) { 
            if (systemStoreApps[hoverStoreAddIdx] != L"No active apps found") {
                appList.push_back({systemStoreApps[hoverStoreAddIdx], false, false}); 
                SaveBlocksData();
            }
        }
        if (hoverStoreClose) showStoreOverlay = false;
        return;
    }
    // ---- TITLE OVERLAY ----
    if (showTitleOverlay) {
        isTitleInputActive = hTitleInput;
        if (hTitleCancel) { showTitleOverlay = false; inputTitleText = L""; }
        if (hTitleAdd && !inputTitleText.empty()) {
            appList.push_back({inputTitleText + L" (Window)", false, false}); 
            SaveBlocksData();
            showTitleOverlay = false; inputTitleText = L"";
        }
        return;
    }
    // ---- LONG TEXT EDIT OVERLAY ----
    if(showLongTextEditOverlay) {
        if(hLTEditCancel) { showLongTextEditOverlay = false; longTextEditBuffer = L""; }
        if(hLTEditConfirm && !longTextEditBuffer.empty()) {
            customLongText = longTextEditBuffer;
            useCustomLongText = true;
            showLongTextEditOverlay = false;
            longTextEditBuffer = L"";
            SaveBlocksData();
        }
        return;
    }
    // ---- LONG TEXT UNLOCK OVERLAY ----
    if(showLongTextUnlockOverlay) {
        if(hLTUnlockCancel) { showLongTextUnlockOverlay = false; longTextUnlockInput = L""; ltUnlockError = false; }
        if(hLTUnlockConfirm) {
            wstring target = (useCustomLongText && !customLongText.empty()) ? customLongText : longTextPhrase;
            if(longTextUnlockInput == target) {
                isFocusActive = false;
                showLongTextUnlockOverlay = false;
                longTextUnlockInput = L"";
                ltUnlockError = false;
                SaveBlocksData();
            } else {
                ltUnlockError = true;
            }
        }
        return;
    }
    // ---- TIME OVERLAY ----
    if (showTimeOverlay) {
        if (hSelfMonthM && selfMonths > 0) selfMonths--;
        if (hSelfMonthP && selfMonths < 12) selfMonths++;
        if (hSelfDayM && selfDays > 0) selfDays--;
        if (hSelfDayP && selfDays < 30) selfDays++;
        if (hSelfHourM && selfHours > 0) selfHours--;
        if (hSelfHourP && selfHours < 23) selfHours++;
        if (hSelfMinM) { selfMins -= 5; if (selfMins < 0) selfMins = 55; }
        if (hSelfMinP) { selfMins = (selfMins + 5) % 60; }
        if (hTimeCancel) showTimeOverlay = false;
        if (hTimeStart) { 
            time_t secs = ComputeFocusSeconds();
            if(secs > 0) {
                isFocusActive = true; 
                focusEndTimeBlocks = std::time(nullptr) + secs;
                showTimeOverlay = false; 
                SaveBlocksData();
            }
        }
        return;
    }
    // ---- PASS OVERLAY ----
    if (showPassOverlay) {
        isPassInputActive = hPassInput;
        if (hPassCancel) { showPassOverlay = false; inputPassText = L""; setPassError = false; }
        if (hPassConfirm && !inputPassText.empty()) {
            if (isStoppingFocus) {
                // Verify password
                if(inputPassText == parentPassword) {
                    isFocusActive = false;
                    showPassOverlay = false; inputPassText = L""; setPassError = false;
                    SaveBlocksData();
                } else {
                    setPassError = true;
                }
            } else {
                // Setting password to START focus
                parentPassword = inputPassText;
                isFocusActive = true;
                focusEndTimeBlocks = 0; // no auto-end for parent mode
                showPassOverlay = false; inputPassText = L""; setPassError = false;
                SaveBlocksData();
            }
        }
        return;
    }

    // ---- TABS ----
    if (hoverBlockTab != -1) { 
        currentBlockTab = hoverBlockTab; 
        isModeDropdownOpen = false; isControlDropdownOpen = false;
        isWebComboOpen = false; isAppComboOpen = false;
        isLangDropdownOpen = false; isSysToolDropdownOpen = false;
        return; 
    }

    if (currentBlockTab == 0) {
        float bodyY = contentY + 60.0f;
        float boxX  = contentX + 30.0f;
        float boxW  = contentW - 60.0f;
        float colW  = (boxW - 90.0f) / 2.0f;
        float rightColX = boxX + 60.0f + colW;
        float rowY  = bodyY + 40.0f;
        float secY  = rowY + 65.0f;
        float qY    = secY;
        float ltY   = qY + 25.0f;
        float appSectionY = secY + 25.0f + (useLongTextUnlock ? 50.0f : 0.0f);

        // START FOCUS BUTTON
        if (hoverStartFocusBtn) {
            isWebInputActive = false; isAppInputActive = false;
            if (isFocusActive) {
                // STOPPING
                if(useLongTextUnlock) {
                    // show long text overlay
                    showLongTextUnlockOverlay = true;
                    isLTUnlockInputActive = true;
                    longTextUnlockInput = L"";
                    ltUnlockError = false;
                } else if (controlMode == 1) {
                    // parent password needed
                    isStoppingFocus = true;
                    showPassOverlay = true; isPassInputActive = true;
                    inputPassText = L""; setPassError = false;
                } else { 
                    // Self control — just stop
                    isFocusActive = false; 
                    SaveBlocksData();
                }
            } else {
                // STARTING
                if (controlMode == 0) {
                    showTimeOverlay = true; // show Month/Day/Hour/Min picker
                } else {
                    // Parent control — ask for password
                    isStoppingFocus = false;
                    showPassOverlay = true; isPassInputActive = true;
                    inputPassText = L""; setPassError = false;
                }
            }
            return;
        }

        // Close dropdowns on outside click
        bool closedAny = false;
        if (isSysToolDropdownOpen && !hoverSysToolDropdown && hoverSysToolOptIdx == -1) {
            isSysToolDropdownOpen = false; closedAny = true;
        } else if (isSysToolDropdownOpen) {
            if(hoverSysToolOptIdx >= 0 && hoverSysToolOptIdx < 5) {
                systemToolBlocked[hoverSysToolOptIdx] = !systemToolBlocked[hoverSysToolOptIdx];
                SaveBlocksData();
            } else if(hoverSysToolOptIdx >= 5) {
                int idx = hoverSysToolOptIdx - 5;
                if(idx < (int)scannedThirdPartyApps.size()) {
                    wstring appName = scannedThirdPartyApps[idx];
                    bool exists = false;
                    for(auto& a : appList) if(toLowerW_Blocks(a.name) == toLowerW_Blocks(appName)) { exists = true; break; }
                    if(!exists) { appList.push_back({appName, false, false}); SaveBlocksData(); }
                }
            }
            isSysToolDropdownOpen = false; return;
        }

        if (isLangDropdownOpen && !hoverLangDropdown && !hoverOptBn && !hoverOptEn) {
            isLangDropdownOpen = false; closedAny = true;
        } else if (isLangDropdownOpen) {
            if (hoverOptBn) quoteLanguage = 0;
            if (hoverOptEn) quoteLanguage = 1;
            isLangDropdownOpen = false; SaveBlocksData(); return;
        }
        if (isControlDropdownOpen && !hoverControlDropdown && !hoverOptSelf && !hoverOptParent) {
            isControlDropdownOpen = false; closedAny = true;
        } else if (isControlDropdownOpen) {
            if (hoverOptSelf)   controlMode = 0;
            if (hoverOptParent) controlMode = 1;
            isControlDropdownOpen = false; SaveBlocksData(); return;
        }
        if (isModeDropdownOpen && !hoverModeDropdown && !hoverOptAllow && !hoverOptBlock) {
            isModeDropdownOpen = false; closedAny = true;
        } else if (isModeDropdownOpen) {
            if (hoverOptAllow) { simpleBlockMode = 0; EnforceSystemApps(); }
            if (hoverOptBlock) { simpleBlockMode = 1; EnforceSystemApps(); }
            isModeDropdownOpen = false; SaveBlocksData(); return;
        }
        if (isWebComboOpen && !hoverWebCombo && hoverWebOptIdx == -1) {
            isWebComboOpen = false; closedAny = true;
        } else if (isWebComboOpen) {
            if (hoverWebOptIdx != -1) { webList.push_back({commonWebsites[hoverWebOptIdx], false}); SaveBlocksData(); }
            isWebComboOpen = false; return;
        }
        if (isAppComboOpen && !hoverAppCombo && hoverAppOptIdx == -1) {
            isAppComboOpen = false; closedAny = true;
        } else if (isAppComboOpen) {
            if (hoverAppOptIdx != -1) { appList.push_back({commonApps[hoverAppOptIdx], false, false}); SaveBlocksData(); }
            isAppComboOpen = false; return;
        }
        if(closedAny) return;

        // Open dropdowns
        if (hoverControlDropdown && !isFocusActive) { isControlDropdownOpen = true; return; }
        if (hoverModeDropdown    && !isFocusActive) { isModeDropdownOpen     = true; return; }
        if (hoverSysToolDropdown && !isFocusActive) { isSysToolDropdownOpen  = true; return; }
        if (hoverWebCombo        && !isFocusActive) { isWebComboOpen         = true; return; }
        if (hoverAppCombo        && !isFocusActive) { isAppComboOpen         = true; return; }
        if (hoverLangDropdown    && !isFocusActive) { isLangDropdownOpen     = true; return; }
        
        if (hoverQuotesCheckbox && !isFocusActive) { showQuotes = !showQuotes; SaveBlocksData(); return; }
        
        // Long text unlock toggle
        if(hoverLongTextCheck && !isFocusActive) { useLongTextUnlock = !useLongTextUnlock; SaveBlocksData(); return; }
        if(hoverCustomLTCheck && !isFocusActive && useLongTextUnlock) {
            // Show edit overlay
            showLongTextEditOverlay = true;
            longTextEditBuffer = useCustomLongText ? customLongText : L"";
            isLTEditActive = true;
            return;
        }

        isWebInputActive = hoverWebInput;
        isAppInputActive = hoverAppInput;

        if (hoverWebAddBtn && !webInputText.empty()) { 
            webList.push_back({webInputText, false}); webInputText = L""; SaveBlocksData();
        }
        if (hoverAppAddBtn && !appInputText.empty()) { 
            appList.push_back({appInputText, false, false}); appInputText = L""; SaveBlocksData();
        }

        // Exe file picker — strip .exe and re-add
        if (hoverAddExe) {
            OPENFILENAMEW ofn; wchar_t szFile[260] = { 0 };
            ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = GetActiveWindow(); 
            ofn.lpstrFile = szFile; ofn.nMaxFile = sizeof(szFile);
            ofn.lpstrFilter = L"Executable Files\0*.exe\0All Files\0*.*\0";
            ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR; 
            if (GetOpenFileNameW(&ofn) == TRUE) {
                wstring filePath = ofn.lpstrFile;
                size_t pos = filePath.find_last_of(L"\\/");
                if(pos != wstring::npos) filePath = filePath.substr(pos+1);
                // Ensure .exe extension
                if(filePath.size() < 4 || toLowerW_Blocks(filePath.substr(filePath.size()-4)) != L".exe")
                    filePath += L".exe";
                appList.push_back({filePath, false, false});
                SaveBlocksData();
            }
        }
        if (hoverAddStoreApp) { 
            RefreshRunningApps(); 
            tStoreScrollY = cStoreScrollY = 0;
            showStoreOverlay = true; 
        }
        if (hoverAddWindowTitle) { showTitleOverlay = true; isTitleInputActive = true; }

        if (!isFocusActive) {
            bool listChanged = false;
            for (auto it = webList.begin(); it != webList.end(); ) { 
                if (it->isHoveredCross) { it = webList.erase(it); listChanged = true; } else ++it; 
            }
            for (auto it = appList.begin(); it != appList.end(); ) { 
                if (it->isHoveredCross && !it->isSystemLocked) { it = appList.erase(it); listChanged = true; } else ++it; 
            }
            if (listChanged) SaveBlocksData();
        }
    } else if (currentBlockTab == 1) {
        ProcessScheduleBlocksMouseClick(x, y);
    }
}

// ==========================================
// KEY PRESS
// ==========================================
void ProcessBlocksKeyPress(wchar_t c) {
    if (showLongTextEditOverlay && isLTEditActive) {
        if (c >= 32 && c <= 126) longTextEditBuffer += c;
        else if (c == L' ')      longTextEditBuffer += L' ';
    } else if (showLongTextUnlockOverlay && isLTUnlockInputActive) {
        if (c >= 32 || c == L' ') { longTextUnlockInput += c; ltUnlockError = false; }
    } else if (showPassOverlay && isPassInputActive) {
        if (c >= 32 && c <= 126 && inputPassText.length() < 30) inputPassText += c;
    } else if (showTitleOverlay && isTitleInputActive) {
        if (c >= 32 && c <= 126 && inputTitleText.length() < 40) inputTitleText += c;
    } else if (!showTimeOverlay && !showPassOverlay && !showStoreOverlay && !showTitleOverlay
            && !showLongTextEditOverlay && !showLongTextUnlockOverlay) {
        if (currentBlockTab == 0) {
            if (isWebInputActive && c >= 32 && c <= 126 && webInputText.length() < 40) webInputText += c;
            if (isAppInputActive && c >= 32 && c <= 126 && appInputText.length() < 40) appInputText += c;
        } else if (currentBlockTab == 1) {
            ProcessScheduleBlocksKeyPress(c);
        }
    }
}

// ==========================================
// KEY DOWN
// ==========================================
void ProcessBlocksKeyDown(WPARAM key) {
    if (key == VK_ESCAPE) {
        if(showLongTextEditOverlay)   { showLongTextEditOverlay = false;   longTextEditBuffer = L""; }
        if(showLongTextUnlockOverlay) { showLongTextUnlockOverlay = false; longTextUnlockInput = L""; ltUnlockError = false; }
        if (showPassOverlay)  { showPassOverlay = false;  inputPassText = L""; setPassError = false; }
        if (showTitleOverlay) { showTitleOverlay = false; inputTitleText = L""; }
        if (showTimeOverlay)  { showTimeOverlay = false; }
        if (showStoreOverlay) { showStoreOverlay = false; }
        return;
    }

    if(showLongTextEditOverlay && isLTEditActive) {
        if(key == VK_BACK && !longTextEditBuffer.empty()) longTextEditBuffer.pop_back();
        if(key == VK_RETURN) longTextEditBuffer += L'\n';
    } else if(showLongTextUnlockOverlay && isLTUnlockInputActive) {
        if(key == VK_BACK && !longTextUnlockInput.empty()) { longTextUnlockInput.pop_back(); ltUnlockError = false; }
    } else if (showPassOverlay && isPassInputActive) {
        if (key == VK_BACK && !inputPassText.empty()) inputPassText.pop_back();
        if (key == VK_RETURN && !inputPassText.empty()) ProcessBlocksMouseClick(0, 0); // simulate confirm
    } else if (showTitleOverlay && isTitleInputActive) {
        if (key == VK_BACK && !inputTitleText.empty()) inputTitleText.pop_back();
    } else if (!showTimeOverlay && !showPassOverlay && !showStoreOverlay && !showTitleOverlay
             && !showLongTextEditOverlay && !showLongTextUnlockOverlay) {
        if (currentBlockTab == 0) {
            if (isWebInputActive) {
                if (key == VK_BACK && !webInputText.empty()) webInputText.pop_back();
                else if (key == VK_RETURN && !webInputText.empty()) {
                    webList.push_back({webInputText, false}); webInputText = L""; SaveBlocksData();
                }
            }
            if (isAppInputActive) {
                if (key == VK_BACK && !appInputText.empty()) appInputText.pop_back();
                else if (key == VK_RETURN && !appInputText.empty()) {
                    appList.push_back({appInputText, false, false}); appInputText = L""; SaveBlocksData();
                }
            }
        } else if (currentBlockTab == 1) {
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
        float visibleStoreH = 450.0f - 70.0f - 60.0f; 
        float maxStoreScroll = max(0.0f, (float)systemStoreApps.size() * 45.0f - visibleStoreH);
        tStoreScrollY = max(0.0f, min(tStoreScrollY, maxStoreScroll));
        return;
    }
    
    if (!showTimeOverlay && !showPassOverlay && !showTitleOverlay
     && !showLongTextEditOverlay && !showLongTextUnlockOverlay) {
        if (currentBlockTab == 0) {
            float bodyY    = s_contentY + 60.0f;
            float boxX     = s_contentX + 30.0f;
            float boxW     = s_contentW - 60.0f;
            float colW2    = (boxW - 90.0f) / 2.0f;
            float leftColX = boxX + 30.0f;
            float rightColX = boxX + 60.0f + colW2;
            float rowY     = bodyY + 40.0f;
            float secY     = rowY + 65.0f;
            float appSectionY = secY + 25.0f + (useLongTextUnlock ? 50.0f : 0.0f);
            
            RectF webTable(leftColX, secY + 90.0f, colW2, 160.0f);
            if (webTable.Contains(x, y)) {
                tWebScrollY -= steps * 40.0f; 
                float maxWebScroll = max(0.0f, (float)webList.size() * 30.0f - 160.0f + 10.0f);
                tWebScrollY = max(0.0f, min(tWebScrollY, maxWebScroll));
            }
            RectF appTable(rightColX, appSectionY + 85.0f, colW2, 120.0f);
            if (appTable.Contains(x, y)) {
                tAppScrollY -= steps * 40.0f; 
                float maxAppScroll = max(0.0f, (float)appList.size() * 30.0f - 120.0f + 10.0f);
                tAppScrollY = max(0.0f, min(tAppScrollY, maxAppScroll));
            }
        } else if (currentBlockTab == 1) {
            ProcessScheduleBlocksMouseWheel(x, y, delta);
        }
    }
}
