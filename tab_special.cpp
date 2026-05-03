// tab_special.cpp

#include "tab_special.h"
#include "tab_gemini.h"     // Diary Tab Header
#include "tab_utilities.h"  // Utilities Tab Header
#include <string>
#include <vector>
#include <tlhelp32.h>
#include <shellapi.h>
#include <thread>
#include <time.h>
#include <fstream>

using namespace Gdiplus;
using namespace std;

// --- Sub Tab State ---
static int sf_activeSubTab = 0; // 0 = Tools, 1 = Diary, 2 = Utilities

// --- States ---
static bool sf_isAdblockActive = false; 
static bool sf_hovAdblock = false;
static bool sf_hovEyeCare = false;
static bool sf_hovZenMode = false;
static bool sf_chkEyeCare = false;

// Motivational Popups States
static bool sf_chkMotivation = false;
static bool sf_hovMotivation = false;
static bool sf_hovLangDrop = false;
static bool sf_isLangDropOpen = false;
static int sf_langSel = 0; // 0 = English, 1 = Bangla

// New Features Hover States
static bool sf_hovScratchpad = false;
static bool sf_hovTodo = false;

// Sub-Tab Hover States
static bool sf_hovTabTools = false;
static bool sf_hovTabDiary = false;
static bool sf_hovTabUtils = false;

vector<wstring> sf_languages = { L"English", L"Bangla" };

vector<wstring> quotesEng = {
    L"\"Don't watch the clock; do what it does. Keep going.\" - Sam Levenson",
    L"\"The future depends on what you do today.\" - Mahatma Gandhi",
    L"\"Focus on your goal. Don't look in any direction but ahead.\"",
    L"\"Time is what we want most, but what we use worst.\" - William Penn",
    L"\"Push yourself, because no one else is going to do it for you.\""
};

vector<wstring> quotesBen = {
    L"\"ঘড়ির দিকে তাকিও না; ঘড়ি যা করে তা করো। চলতে থাকো।\"",
    L"\"তোমার ভবিষ্যৎ নির্ভর করে তুমি আজ কী করছো তার ওপর।\"",
    L"\"শুধু লক্ষ্যের দিকে ফোকাস করো। অন্য কোথাও তাকানোর সময় নেই।\"",
    L"\"সময়ই আমাদের সবচেয়ে বেশি দরকার, অথচ এটাকেই আমরা সবচেয়ে বাজেভাবে ব্যবহার করি।\"",
    L"\"নিজেকে নিজে পুশ করো, কারণ অন্য কেউ তোমার হয়ে এটা করে দেবে না।\""
};

// --- Helper: Simple Rectangle (Matching your UI style) ---
static void FillSimpleRectSpecial(Graphics& g, SolidBrush* br, Pen* pen, float x, float y, float rw, float rh) {
    if (br) g.FillRectangle(br, x, y, rw, rh);
    if (pen) g.DrawRectangle(pen, x, y, rw, rh);
}

// NOTE: IsRunAsAdmin() and GetSecretDir() are defined in main.cpp
extern bool IsRunAsAdmin(); 
extern string GetSecretDir();

// External Diary Definitions
extern void ShowGeminiControls(bool show);
extern void DrawGeminiTab(Graphics& g, float cx, float cy, float cw, float ch);
extern void ResizeGeminiControls(int cx, int cy, int cw, int ch);
extern void ProcessDiaryMouseMove(float x, float y);
extern void ProcessGeminiMouseClick(float x, float y);
extern void ProcessGeminiMouseMove(float x, float y); // Added from previous fix context

// External Utilities Definitions
extern void DrawUtilitiesTab(Graphics& g, float cx, float cy, float cw, float ch);
extern void ProcessUtilitiesMouseMove(float x, float y);
extern void ProcessUtilitiesMouseClick(float x, float y);

// UI Refresh Handle (Make sure this is defined globally in main.cpp)
extern HWND hParentWnd; 

// ==============================================================
// --- Silent Extension Installer/Uninstaller ---
// ==============================================================
void ToggleAdBlock(bool enable) {
    if (!IsRunAsAdmin()) {
        wchar_t szPath[MAX_PATH];
        if (GetModuleFileNameW(NULL, szPath, ARRAYSIZE(szPath))) {
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.lpVerb = L"runas";
            sei.lpFile = szPath;
            sei.hwnd = NULL;
            sei.nShow = SW_NORMAL;
            if (!ShellExecuteExW(&sei)) {
                MessageBoxW(NULL, L"Admin permission is required to change Adblocker settings.", L"Permission Denied", MB_OK | MB_ICONERROR);
                sf_isAdblockActive = !enable; 
            } else { exit(0); }
        }
        return;
    }

    HKEY hKey;
    string chromePath = "SOFTWARE\\Policies\\Google\\Chrome\\ExtensionInstallForcelist";
    string adGuardChrome = "bgnkhhnnamicmpeenaelnjfhikgbkllg;https://clients2.google.com/service/update2/crx";
    string edgePath = "SOFTWARE\\Policies\\Microsoft\\Edge\\ExtensionInstallForcelist";
    string adGuardEdge = "pdffkfellgipmhklpdmokmckkkfcopbh;https://edge.microsoft.com/extensionwebstorebase/v1/crx";

    if (enable) {
        if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, chromePath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExA(hKey, "1", 0, REG_SZ, (const BYTE*)adGuardChrome.c_str(), adGuardChrome.length() + 1); RegCloseKey(hKey);
        }
        if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, edgePath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExA(hKey, "1", 0, REG_SZ, (const BYTE*)adGuardEdge.c_str(), adGuardEdge.length() + 1); RegCloseKey(hKey);
        }
        MessageBoxW(NULL, L"Stealth AdBlocker successfully ENABLED for Chrome & Edge!", L"Success", MB_OK | MB_ICONINFORMATION);
    } else {
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, chromePath.c_str(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) { RegDeleteValueA(hKey, "1"); RegCloseKey(hKey); }
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, edgePath.c_str(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) { RegDeleteValueA(hKey, "1"); RegCloseKey(hKey); }
        MessageBoxW(NULL, L"Stealth AdBlocker successfully DISABLED.", L"Removed", MB_OK | MB_ICONINFORMATION);
    }
}

// --- Kill Distracting Apps ---
void ActivateZenMode() {
    vector<wstring> badApps = { L"discord.exe", L"steam.exe", L"epicgames.exe", L"telegram.exe" };
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe; pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                wstring pName = pe.szExeFile; for (auto& c : pName) c = towlower(c);
                for (const auto& bad : badApps) {
                    if (pName == bad) {
                        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                        if (hProc) { TerminateProcess(hProc, 0); CloseHandle(hProc); }
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    MessageBoxW(NULL, L"Zen Mode Activated! All distractions have been eliminated.", L"Zen Mode", MB_OK | MB_ICONINFORMATION);
}

// ==============================================================
// --- Motivational Popup Window Logic ---
// ==============================================================
wstring currentMotiveQuote = L"";

LRESULT CALLBACK MotivationWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT r; GetClientRect(hwnd, &r);
        
        GraphicsPath path; int d = 20;
        path.AddArc(0, 0, d, d, 180, 90); path.AddArc(r.right-d, 0, d, d, 270, 90);
        path.AddArc(r.right-d, r.bottom-d, d, d, 0, 90); path.AddArc(0, r.bottom-d, d, d, 90, 90); path.CloseFigure();
        
        SolidBrush bg(Color(250, 30, 41, 59)); 
        g.FillPath(&bg, &path);
        Pen border(Color(255, 245, 158, 11), 2.0f); 
        g.DrawPath(&border, &path);
        
        FontFamily ff(sf_langSel == 1 ? L"Vrinda" : L"Segoe UI"); 
        Font fQuote(&ff, 22, FontStyleBold, UnitPixel);
        SolidBrush wBr(Color(255, 255, 255, 255));
        StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(currentMotiveQuote.c_str(), -1, &fQuote, RectF(20,20,(float)r.right-40,(float)r.bottom-40), &fmt, &wBr);
        
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_TIMER) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ShowMotivationalPopup() {
    thread([](){
        srand((unsigned)time(0));
        if (sf_langSel == 0) currentMotiveQuote = quotesEng[rand() % quotesEng.size()];
        else currentMotiveQuote = quotesBen[rand() % quotesBen.size()];

        static bool reg = false;
        if (!reg) {
            WNDCLASSW wc = {0}; wc.lpfnWndProc = MotivationWndProc; wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = L"RasMotivClass"; RegisterClassW(&wc); reg = true;
        }
        int w = 550, h = 120; 
        int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, 
            L"RasMotivClass", L"", WS_POPUP, x, 50, w, h, NULL, NULL, NULL, NULL);
        SetLayeredWindowAttributes(hwnd, 0, 245, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE); 
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        
        SetTimer(hwnd, 1, 6000, NULL); 
        
        MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        DestroyWindow(hwnd);
    }).detach();
}

static bool motivationThreadRunning = false;
void MotivationBackgroundThread() {
    while (true) {
        Sleep(1000); 
        if (sf_chkMotivation) {
            static DWORD lastTick = GetTickCount();
            DWORD currentTick = GetTickCount();
            if (currentTick - lastTick >= 900000) { // 15 mins
                lastTick = currentTick; ShowMotivationalPopup();
            }
        }
    }
}

// ==============================================================
// --- Scratchpad & Micro To-Do Logic ---
// ==============================================================
LRESULT CALLBACK ScratchpadProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hEdit;
    string path = GetSecretDir() + "scratchpad.txt";
    switch(msg) {
        case WM_CREATE: {
            hEdit = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL, 0,0,0,0, hwnd, (HMENU)1, NULL, NULL);
            HFONT hFont = CreateFont(18,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH|FF_SWISS, "Segoe UI");
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
            ifstream in(path);
            if(in.is_open()) {
                string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
                SetWindowText(hEdit, content.c_str());
            }
            return 0;
        }
        case WM_SIZE: { MoveWindow(hEdit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE); return 0; }
        case WM_CLOSE: {
            int len = GetWindowTextLength(hEdit);
            char* buf = new char[len + 1];
            GetWindowText(hEdit, buf, len + 1);
            ofstream out(path); out << buf; out.close(); delete[] buf;
            ShowWindow(hwnd, SW_HIDE); return 0;
        }
    } return DefWindowProc(hwnd, msg, wParam, lParam);
}

void OpenScratchpad() {
    static HWND hwnd = NULL;
    if(!hwnd) {
        WNDCLASS wc = {0}; wc.lpfnWndProc = ScratchpadProc; wc.hInstance = GetModuleHandle(NULL); 
        wc.lpszClassName = "RasScratchpad"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); RegisterClass(&wc);
        hwnd = CreateWindowEx(WS_EX_TOOLWINDOW, "RasScratchpad", "Brain Dump (Scratchpad)", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 400, 500, NULL, NULL, GetModuleHandle(NULL), NULL);
    }
    ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd);
}

LRESULT CALLBACK TodoProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hChecks[5], hEdits[5];
    string path = GetSecretDir() + "todo.dat";
    switch(msg) {
        case WM_CREATE: {
            HFONT hFont = CreateFont(18,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH|FF_SWISS, "Segoe UI");
            for(int i=0; i<5; i++) {
                hChecks[i] = CreateWindow("BUTTON", "", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 10, 10 + i*40, 20, 20, hwnd, (HMENU)(INT_PTR)(100+i), NULL, NULL);
                hEdits[i] = CreateWindow("EDIT", "", WS_CHILD|WS_VISIBLE|WS_BORDER, 40, 10 + i*40, 300, 25, hwnd, (HMENU)(INT_PTR)(200+i), NULL, NULL);
                SendMessage(hEdits[i], WM_SETFONT, (WPARAM)hFont, TRUE);
            }
            ifstream in(path);
            if(in.is_open()) {
                string line;
                for(int i=0; i<5 && getline(in, line); i++) {
                    if(line.length() > 0) {
                        SendMessage(hChecks[i], BM_SETCHECK, line[0] == '1' ? BST_CHECKED : BST_UNCHECKED, 0);
                        SetWindowText(hEdits[i], line.substr(1).c_str());
                    }
                }
            }
            return 0;
        }
        case WM_CLOSE: {
            ofstream out(path);
            for(int i=0; i<5; i++) {
                char buf[256]; GetWindowText(hEdits[i], buf, 256);
                bool checked = SendMessage(hChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
                out << (checked ? "1" : "0") << buf << endl;
            } out.close();
            ShowWindow(hwnd, SW_HIDE); return 0;
        }
    } return DefWindowProc(hwnd, msg, wParam, lParam);
}

void OpenTodo() {
    static HWND hwnd = NULL;
    if(!hwnd) {
        WNDCLASS wc = {0}; wc.lpfnWndProc = TodoProc; wc.hInstance = GetModuleHandle(NULL); 
        wc.lpszClassName = "RasTodo"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); RegisterClass(&wc);
        hwnd = CreateWindowEx(WS_EX_TOOLWINDOW, "RasTodo", "Micro Session To-Do", WS_SYSMENU|WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT, 380, 260, NULL, NULL, GetModuleHandle(NULL), NULL);
    }
    ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd);
}

// --- Global offset logic variables ---
static float g_cx = 0, g_cy = 0;

void DrawSpecialFeatureTab(Graphics& g, float cx, float cy, float cw, float ch) {
    g_cx = cx; g_cy = cy;
    
    if (!motivationThreadRunning) { thread t(MotivationBackgroundThread); t.detach(); motivationThreadRunning = true; }

    FontFamily ff(L"Segoe UI");
    Font fH2(&ff, 15, FontStyleBold, UnitPixel); Font fSub(&ff, 13, FontStyleRegular, UnitPixel);
    Font fBtn(&ff, 13, FontStyleBold, UnitPixel); FontFamily ffIc(L"Segoe MDL2 Assets"); Font fIc(&ffIc, 18, FontStyleRegular, UnitPixel);

    SolidBrush bWhite(Color(255, 255, 255, 255)); SolidBrush bBg(Color(255, 248, 250, 252));
    SolidBrush bDark(Color(255, 50, 50, 50)); SolidBrush bGray(Color(255, 120, 120, 120));
    SolidBrush bTeal(Color(255, 12, 168, 176)); SolidBrush bTealHov(Color(255, 30, 185, 195));
    SolidBrush bRed(Color(255, 239, 68, 68)); SolidBrush bRedHov(Color(255, 248, 113, 113));
    Pen pBrd(Color(255, 220, 225, 230), 1.5f);
    
    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    // --- Sub-Tab Navigation Header ---
    g.FillRectangle(&bWhite, cx, cy, cw, 60.0f);
    
    float tabW = 200.0f, tabH = 40.0f;
    float tab1X = cx + 20.0f, tab2X = tab1X + tabW + 10.0f, tab3X = tab2X + tabW + 10.0f, tabY = cy + 10.0f;

    SolidBrush bTab1(sf_activeSubTab == 0 ? Color(255, 12, 168, 176) : (sf_hovTabTools ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    SolidBrush bTab2(sf_activeSubTab == 1 ? Color(255, 35, 137, 215) : (sf_hovTabDiary ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    SolidBrush bTab3(sf_activeSubTab == 2 ? Color(255, 155, 89, 182) : (sf_hovTabUtils ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    
    SolidBrush bT1(sf_activeSubTab == 0 ? Color(255, 255, 255, 255) : Color(255, 100, 100, 100));
    SolidBrush bT2(sf_activeSubTab == 1 ? Color(255, 255, 255, 255) : Color(255, 100, 100, 100));
    SolidBrush bT3(sf_activeSubTab == 2 ? Color(255, 255, 255, 255) : Color(255, 100, 100, 100));

    FillSimpleRectSpecial(g, &bTab1, NULL, tab1X, tabY, tabW, tabH); g.DrawString(L"Tools & Blockers", -1, &fH2, RectF(tab1X, tabY, tabW, tabH), &fC, &bT1);
    FillSimpleRectSpecial(g, &bTab2, NULL, tab2X, tabY, tabW, tabH); g.DrawString(L"Professional Diary", -1, &fH2, RectF(tab2X, tabY, tabW, tabH), &fC, &bT2);
    FillSimpleRectSpecial(g, &bTab3, NULL, tab3X, tabY, tabW, tabH); g.DrawString(L"Student Utilities", -1, &fH2, RectF(tab3X, tabY, tabW, tabH), &fC, &bT3);

    float cY = cy + 60.0f; float cH = ch - 60.0f;
    float cardX = cx + 30.0f; float cardW = cw - 60.0f; float cardH = 65.0f; float gapY = 10.0f;

    if (sf_activeSubTab == 0) {
        ShowGeminiControls(false); // Hide Diary WIN32 controls
        g.FillRectangle(&bBg, cx, cY, cw, cH);

        auto DrawFeatureRow = [&](float y, wstring title, wstring desc, wstring btnTxt, bool isHover, bool isActiveState) {
            g.DrawLine(&pBrd, cardX, y + cardH, cardX + cardW, y + cardH);
            g.DrawString(title.c_str(), -1, &fH2, RectF(cardX, y + 8.0f, cardW - 200.0f, 20.0f), &fL, &bDark);
            g.DrawString(desc.c_str(), -1, &fSub, RectF(cardX, y + 28.0f, cardW - 200.0f, 20.0f), &fL, &bGray);

            float actionW = 130.0f, actionH = 30.0f;
            float actionX = cardX + cardW - actionW; float actionY = y + (cardH - actionH) / 2.0f;

            SolidBrush* currentBg;
            if (isActiveState) currentBg = isHover ? &bRedHov : &bRed; 
            else currentBg = isHover ? &bTealHov : &bTeal; 

            FillSimpleRectSpecial(g, currentBg, NULL, actionX, actionY, actionW, actionH);
            g.DrawString(btnTxt.c_str(), -1, &fBtn, RectF(actionX, actionY, actionW, actionH), &fC, &bWhite);
        };

        // 1. Adblocker
        wstring adBtnTxt = sf_isAdblockActive ? L"Remove Blocker" : L"Install Blocker";
        DrawFeatureRow(cY + 10.0f, L"Stealth Ad & Content Blocker", L"Silent, unremovable adblocker for browsers.", adBtnTxt, sf_hovAdblock, sf_isAdblockActive);

        // 2. Eye Care
        wstring eyeBtnTxt = sf_chkEyeCare ? L"Disable Eye Care" : L"Enable Eye Care";
        DrawFeatureRow(cY + 10.0f + (cardH + gapY) * 1, L"Smart Eye Care (20-20-20 Rule)", L"Reduces eye strain by reminding you to rest your eyes.", eyeBtnTxt, sf_hovEyeCare, sf_chkEyeCare);

        // 3. Zen Mode
        DrawFeatureRow(cY + 10.0f + (cardH + gapY) * 2, L"Instant Zen Mode", L"One click to kill all distracting apps.", L"Activate Zen", sf_hovZenMode, false);

        // 4. Motivational Popups
        float mY = cY + 10.0f + (cardH + gapY) * 3;
        g.DrawLine(&pBrd, cardX, mY + cardH, cardX + cardW, mY + cardH);
        g.DrawString(L"Motivational Popups", -1, &fH2, RectF(cardX, mY + 8.0f, cardW - 350.0f, 20.0f), &fL, &bDark);
        g.DrawString(L"Shows a motivational quote every 15 minutes.", -1, &fSub, RectF(cardX, mY + 28.0f, cardW - 350.0f, 20.0f), &fL, &bGray);

        float dW = 100.0f, dH = 30.0f, dX = cardX + cardW - 130.0f - dW - 15.0f, dY = mY + (cardH - dH) / 2.0f;
        FillSimpleRectSpecial(g, &bWhite, &pBrd, dX, dY, dW, dH);
        g.DrawString(sf_languages[sf_langSel].c_str(), -1, &fSub, RectF(dX + 5.0f, dY, dW - 25.0f, dH), &fL, &bDark);
        g.DrawString(L"\xE70D", -1, &fIc, RectF(dX + dW - 25.0f, dY, 25.0f, dH), &fC, &bDark);
        
        wstring mBtnTxt = sf_chkMotivation ? L"Disable Popups" : L"Enable Popups";
        float actionW = 130.0f, actionH = 30.0f;
        float actionX = cardX + cardW - actionW; float actionY = mY + (cardH - actionH) / 2.0f;
        SolidBrush* mBg; if (sf_chkMotivation) mBg = sf_hovMotivation ? &bRedHov : &bRed; else mBg = sf_hovMotivation ? &bTealHov : &bTeal;
        FillSimpleRectSpecial(g, mBg, NULL, actionX, actionY, actionW, actionH);
        g.DrawString(mBtnTxt.c_str(), -1, &fBtn, RectF(actionX, actionY, actionW, actionH), &fC, &bWhite);

        if (sf_isLangDropOpen) {
            float listY = dY + dH; FillSimpleRectSpecial(g, &bWhite, &pBrd, dX, listY, dW, dH * 2.0f);
            for(int i=0; i<2; i++) {
                SolidBrush hBr(sf_langSel == i ? Color(255, 235, 248, 250) : Color(255, 255, 255, 255));
                FillSimpleRectSpecial(g, &hBr, NULL, dX, listY + (i*dH), dW, dH);
                g.DrawString(sf_languages[i].c_str(), -1, &fSub, RectF(dX + 5.0f, listY + (i*dH), dW, dH), &fL, &bDark);
            }
        }

        // 5. Distraction-Free Scratchpad
        DrawFeatureRow(cY + 10.0f + (cardH + gapY) * 4, L"Distraction-Free Scratchpad", L"Dump distracting thoughts instantly. Auto-saves locally.", L"Open Pad", sf_hovScratchpad, false);

        // 6. Micro To-Do
        DrawFeatureRow(cY + 10.0f + (cardH + gapY) * 5, L"Micro Session To-Do", L"Set 3-5 clear goals before focusing to boost dopamine.", L"Open To-Do", sf_hovTodo, false);
    }
    else if (sf_activeSubTab == 1) {
        DrawGeminiTab(g, cx, cY, cw, cH);
        ResizeGeminiControls((int)cx, (int)cY, (int)cw, (int)cH);
        ShowGeminiControls(true);
    }
    else if (sf_activeSubTab == 2) {
        ShowGeminiControls(false); // Hide Diary WIN32 controls
        DrawUtilitiesTab(g, cx, cY, cw, cH);
    }
}

void ProcessSpecialFeatureMouseMove(float x, float y) {
    bool old_hovTabTools = sf_hovTabTools;
    bool old_hovTabDiary = sf_hovTabDiary;
    bool old_hovTabUtils = sf_hovTabUtils;
    bool old_hovAdblock = sf_hovAdblock;
    bool old_hovEyeCare = sf_hovEyeCare;
    bool old_hovZenMode = sf_hovZenMode;
    bool old_hovMotivation = sf_hovMotivation;
    bool old_hovLangDrop = sf_hovLangDrop;
    bool old_hovScratchpad = sf_hovScratchpad;
    bool old_hovTodo = sf_hovTodo;

    sf_hovTabTools = false; sf_hovTabDiary = false; sf_hovTabUtils = false;
    sf_hovAdblock = false; sf_hovEyeCare = false; sf_hovZenMode = false; sf_hovMotivation = false; sf_hovLangDrop = false;
    sf_hovScratchpad = false; sf_hovTodo = false;

    float tab1X = g_cx + 20.0f, tab2X = tab1X + 210.0f, tab3X = tab2X + 210.0f, tabY = g_cy + 10.0f;
    if(x>=tab1X && x<=tab1X+200.0f && y>=tabY && y<=tabY+40.0f) sf_hovTabTools = true;
    if(x>=tab2X && x<=tab2X+200.0f && y>=tabY && y<=tabY+40.0f) sf_hovTabDiary = true;
    if(x>=tab3X && x<=tab3X+200.0f && y>=tabY && y<=tabY+40.0f) sf_hovTabUtils = true;
    
    if (sf_activeSubTab == 0) {
        float cardX = g_cx + 30.0f; float cardW = 804.0f - 60.0f; 
        float actionW = 130.0f, actionH = 30.0f; float actionX = cardX + cardW - actionW;
        float cY = g_cy + 65.0f; float cardH = 65.0f; float gapY = 10.0f;

        auto CheckHitbox = [&](float rY) { return (x >= actionX && x <= actionX + actionW && y >= rY + (cardH - actionH) / 2.0f && y <= rY + (cardH - actionH) / 2.0f + actionH); };

        if (CheckHitbox(cY + 10.0f)) sf_hovAdblock = true;
        if (CheckHitbox(cY + 10.0f + (cardH + gapY) * 1)) sf_hovEyeCare = true;
        if (CheckHitbox(cY + 10.0f + (cardH + gapY) * 2)) sf_hovZenMode = true;
        if (CheckHitbox(cY + 10.0f + (cardH + gapY) * 3)) sf_hovMotivation = true;
        if (CheckHitbox(cY + 10.0f + (cardH + gapY) * 4)) sf_hovScratchpad = true;
        if (CheckHitbox(cY + 10.0f + (cardH + gapY) * 5)) sf_hovTodo = true;
        
        float mY = cY + 10.0f + (cardH + gapY) * 3;
        float dW = 100.0f, dH = 30.0f, dX = cardX + cardW - 130.0f - dW - 15.0f, dY = mY + (cardH - dH) / 2.0f;
        if (x >= dX && x <= dX + dW && y >= dY && y <= dY + dH) sf_hovLangDrop = true;
    }
    else if (sf_activeSubTab == 1) {
        ProcessGeminiMouseMove(x, y);
    }
    else if (sf_activeSubTab == 2) {
        ProcessUtilitiesMouseMove(x, y);
    }

    bool needsRefresh = (old_hovTabTools != sf_hovTabTools || old_hovTabDiary != sf_hovTabDiary ||
                         old_hovTabUtils != sf_hovTabUtils || old_hovAdblock != sf_hovAdblock ||
                         old_hovEyeCare != sf_hovEyeCare || old_hovZenMode != sf_hovZenMode ||
                         old_hovMotivation != sf_hovMotivation || old_hovLangDrop != sf_hovLangDrop ||
                         old_hovScratchpad != sf_hovScratchpad || old_hovTodo != sf_hovTodo);

    if (needsRefresh && hParentWnd != NULL) {
        InvalidateRect(hParentWnd, NULL, TRUE);
    }
}

void ProcessSpecialFeatureMouseClick(float x, float y) {
    float tab1X = g_cx + 20.0f, tab2X = tab1X + 210.0f, tab3X = tab2X + 210.0f, tabY = g_cy + 10.0f;
    
    if(x>=tab1X && x<=tab1X+200.0f && y>=tabY && y<=tabY+40.0f) { sf_activeSubTab = 0; if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); return; }
    if(x>=tab2X && x<=tab2X+200.0f && y>=tabY && y<=tabY+40.0f) { sf_activeSubTab = 1; if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); return; }
    if(x>=tab3X && x<=tab3X+200.0f && y>=tabY && y<=tabY+40.0f) { sf_activeSubTab = 2; if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); return; }

    if (sf_activeSubTab == 0) {
        float cardX = g_cx + 30.0f; float cardW = 804.0f - 60.0f; 
        float cY = g_cy + 65.0f; float cardH = 65.0f; float gapY = 10.0f;
        float actionW = 130.0f, actionH = 30.0f; float actionX = cardX + cardW - actionW;
        
        auto CheckHitbox = [&](float rY) { return (x >= actionX && x <= actionX + actionW && y >= rY + (cardH - actionH) / 2.0f && y <= rY + (cardH - actionH) / 2.0f + actionH); };

        float mY = cY + 10.0f + (cardH + gapY) * 3;
        float dW = 100.0f, dH = 30.0f, dX = cardX + cardW - 130.0f - dW - 15.0f, dY = mY + (cardH - dH) / 2.0f;

        if (sf_isLangDropOpen) {
            if (x >= dX && x <= dX + dW && y >= dY + dH && y <= dY + dH * 2.0f) sf_langSel = 0; 
            else if (x >= dX && x <= dX + dW && y >= dY + dH * 2.0f && y <= dY + dH * 3.0f) sf_langSel = 1; 
            
            sf_isLangDropOpen = false; 
            if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }

        if (x >= dX && x <= dX + dW && y >= dY && y <= dY + dH) { sf_isLangDropOpen = true; if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); return; }

        if (CheckHitbox(cY + 10.0f)) { sf_isAdblockActive = !sf_isAdblockActive; ToggleAdBlock(sf_isAdblockActive); if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); return; }
        if (CheckHitbox(cY + 10.0f + (cardH + gapY) * 1)) { sf_chkEyeCare = !sf_chkEyeCare; if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE); if (sf_chkEyeCare) MessageBoxW(NULL, L"Eye Care enabled!", L"Eye Care", MB_OK | MB_ICONINFORMATION); return; }
        if (CheckHitbox(cY + 10.0f + (cardH + gapY) * 2)) { ActivateZenMode(); return; }
        if (CheckHitbox(cY + 10.0f + (cardH + gapY) * 3)) { 
            sf_chkMotivation = !sf_chkMotivation; 
            if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            if (sf_chkMotivation) MessageBoxW(NULL, L"Motivational Popups enabled! You'll see quotes every 15 minutes.", L"Motivation", MB_OK | MB_ICONINFORMATION); 
            return; 
        }
        if (CheckHitbox(cY + 10.0f + (cardH + gapY) * 4)) { OpenScratchpad(); return; }
        if (CheckHitbox(cY + 10.0f + (cardH + gapY) * 5)) { OpenTodo(); return; }
    }
    else if (sf_activeSubTab == 1) {
        ProcessGeminiMouseClick(x, y);
    }
    else if (sf_activeSubTab == 2) {
        ProcessUtilitiesMouseClick(x, y);
    }
}