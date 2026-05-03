#include "tab_adult.h"
#include "tab_ai.h"
#include "tab_strict.h" 
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <iostream>
#include <sstream>
#include <fstream>
#include <codecvt> 
#include <locale>  
#include <psapi.h>
#include <tlhelp32.h> 
#include <uiautomation.h>
#include <ctime>

using namespace Gdiplus;
using namespace std;

// --- Dynamic Window Size Cache ---
static float s_contentX = 0.0f;
static float s_contentY = 0.0f;
static float s_contentW = 800.0f; 
static float s_contentH = 600.0f;

// --- Sub Tab States for Adult Block ---
static int ad_activeSubTab = 0; // 0 = Safe Browsing, 1 = AI Filter, 2 = Strict Protocols
static bool ad_hovTab1 = false;
static bool ad_hovTab2 = false;
static bool ad_hovTab3 = false;

// --- Colors ---
static const Color AClrTeal(255, 12, 168, 176);
static const Color AClrTealHover(255, 30, 185, 195);   
static const Color AClrDark(255, 50, 50, 50);
static const Color AClrGrayText(255, 120, 120, 120);
static const Color AClrBorder(255, 220, 225, 230);
static const Color AClrWhite(255, 255, 255, 255);
static const Color AClrBg(255, 248, 250, 252);
static const Color AClrBgHover(255, 235, 248, 250);    
static const Color AClrRed(255, 231, 76, 60);
static const Color AClrGreen(255, 90, 170, 20);
static const Color AClrOverlay(180, 0, 0, 0);          
static const Color AClrCardBg(255, 250, 252, 255); 
static const Color AClrLink(255, 0, 102, 204); 

// --- State Variables ---
static bool isAdultFocusActive = false;
static bool hoverAdultFocusBtn = false;
static ULONGLONG focusEndTime = 0; 

// Mode Control
static int controlMode = 0; // 0 = Self, 1 = Friend
static bool hoverControlDrop = false; static bool isControlDropOpen = false;
static int hoverCtrlIdx = -1;
wstring ctrlModes[] = { L"Self Control", L"Friend Control" };

// Overlays
static bool showTimeOverlay = false;
static int focusHours = 1; static int focusMins = 0;
static bool hTimeHM = false, hTimeHP = false, hTimeMM = false, hTimeMP = false; 
static bool hTimeStart = false, hTimeCancel = false;

static bool showPassOverlay = false;
static wstring inputPassText = L"";
static bool isPassInputActive = true, hPassInput = false;
static bool hPassConfirm = false, hPassCancel = false;
static bool isStoppingFocus = false; 

// Religion & Language
static int adultReligion = 0; 
static bool hoverRelDrop = false; static bool isRelDropOpen = false;
static int hoverRelIdx = -1;
wstring religions[] = { L"Muslim", L"Hindu", L"Christian", L"Universal" };

static int adultLanguage = 0; 
static bool hoverLangDrop = false; static bool isLangDropOpen = false;
static int hoverLangIdx = -1;
wstring languages[] = { L"Bangla", L"English" };

// Basic Features (Safe Browsing Tab)
static bool cbAdultWeb = true; static bool hCbAdultWeb = false;
static bool cbFbReels = true;  static bool hCbFbReels = false;
static bool cbYtShorts = true; static bool hCbYtShorts = false;
static bool cbHardcore = true; static bool hCbHardcore = false;
static bool cbRomantic = true; static bool hCbRomantic = false;

// --- NEW ADVANCED FEATURES ---
static bool cbPeriodicPopups = false; static bool hCbPeriodicPopups = false;
static DWORD lastPeriodicPopupTime = 0; 

static bool cb24HourLock = false; static bool hCb24HourLock = false;
static ULONGLONG lock24hEndTime = 0;

static int cleanStreakDays = 12; 

// --- Quick Links Hover States ---
static bool hLinkAiFilter = false;
static bool hLinkStrict = false;

// Custom Keywords (Table System)
struct AdultCustomItem { wstring name; bool isHoveredCross; };
static vector<AdultCustomItem> customAdultKeywords;
static wstring customInputText = L"";
static bool isCustomInputActive = false;
static bool hoverCustomInput = false;
static bool hoverCustomAddBtn = false;
static int customScrollOffset = 0;

static bool isPanicActive = false;
static bool hoverPanicBtn = false;
static DWORD panicStartTime = 0;
static int totalBlockedCount = 0; 

// --- DATE HELPER ---
wstring GetTodayDateString() {
    time_t now = time(0);
    tm* ltm = localtime(&now);
    wchar_t buf[100];
    wcsftime(buf, 100, L"%B %d, %Y", ltm);
    return wstring(buf);
}

// --- SAVE & LOAD SETTINGS DATA (Hidden File in C Drive) ---
wstring GetSaveFilePath() {
    wstring path = L"C:\\ProgramData\\RasFocus";
    CreateDirectoryW(path.c_str(), NULL);
    // সুপার হিডেন ফোল্ডার
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return path + L"\\rf_sys_data.dat";
}

// ==========================================
// LOAD ADULT SITES FROM RESOURCE (104)
// ==========================================
vector<wstring> adultWebsites; // initially empty

vector<wstring> LoadAdultSitesFromResource() {
    vector<wstring> sites;
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(105), RT_RCDATA);
    if (!hRes) return sites; 

    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return sites;

    DWORD size = SizeofResource(NULL, hRes);
    const char* data = (const char*)LockResource(hData);

    if (data && size > 0) {
        string fileContent(data, size);
        stringstream ss(fileContent);
        string line;
        
        while (getline(ss, line)) {
            if (!line.empty() && line[line.length() - 1] == '\r') {
                line.erase(line.length() - 1);
            }
            if (!line.empty()) {
                sites.push_back(wstring(line.begin(), line.end()));
            }
        }
    }
    return sites;
}

void SaveAdultSettings() {
    wstring filePath = GetSaveFilePath();
    std::wofstream out(filePath.c_str());
    out.imbue(std::locale(out.getloc(), new std::codecvt_utf8<wchar_t>));
    if (out.is_open()) {
        out << cbAdultWeb << L" " << cbFbReels << L" " << cbYtShorts << L" " << cbHardcore << L" " << cbRomantic << L"\n";
        out << controlMode << L" " << adultReligion << L" " << adultLanguage << L" " << totalBlockedCount << L"\n";
        out << cbPeriodicPopups << L" " << cb24HourLock << L" " << lock24hEndTime << L" " << cleanStreakDays << L"\n";
        
        out << isAdultFocusActive << L" " << focusEndTime << L"\n";

        out << customAdultKeywords.size() << L"\n";
        for (auto& k : customAdultKeywords) {
            out << k.name << L"\n";
        }
        out.close();
    }
}

void LoadAdultSettings() {
    // রিসোর্স থেকে ওয়েবসাইট লোড (যদি আগে থেকে লোড না হয়ে থাকে)
    if (adultWebsites.empty()) {
        adultWebsites = LoadAdultSitesFromResource();
        if (adultWebsites.empty()) {
            // ফেইলসেফ ডিফল্ট
            adultWebsites = { L"pornhub.com", L"xvideos.com", L"xnxx.com", L"xhamster.com", L"redtube.com" };
        }
    }

    wstring filePath = GetSaveFilePath();
    std::wifstream in(filePath.c_str());
    in.imbue(std::locale(in.getloc(), new std::codecvt_utf8<wchar_t>));
    if (in.is_open()) {
        in >> cbAdultWeb >> cbFbReels >> cbYtShorts >> cbHardcore >> cbRomantic;
        in >> controlMode >> adultReligion >> adultLanguage >> totalBlockedCount;
        in >> cbPeriodicPopups >> cb24HourLock >> lock24hEndTime >> cleanStreakDays;
        
        in >> isAdultFocusActive >> focusEndTime;

        if (cb24HourLock && GetTickCount64() >= lock24hEndTime) {
            cb24HourLock = false;
            isAdultFocusActive = false; 
        } else if (cb24HourLock) {
            isAdultFocusActive = true; 
        }

        if (isAdultFocusActive && controlMode == 0 && GetTickCount64() >= focusEndTime && !cb24HourLock) {
            isAdultFocusActive = false;
        }

        size_t kSize = 0;
        in >> kSize;
        in.ignore(); 
        customAdultKeywords.clear();
        for (size_t i = 0; i < kSize; i++) {
            std::wstring line;
            std::getline(in, line);
            if (!line.empty()) customAdultKeywords.push_back({ line, false });
        }
        in.close();
    }
}

// --- Quotes & Keywords Databases ---
struct Quote { wstring bn; wstring en; };

vector<Quote> muslimQuotes = {
    {L"“মুমিনদের বলুন, তারা যেন তাদের দৃষ্টি নত রাখে এবং যৌনাঙ্গের হেফাজত করে।” - (সূরা আন-নূর: ৩০)", L"Tell the believing men to reduce [some] of their vision and guard their private parts. (Surah An-Nur: 30)"},
    {L"“অশ্লীলতার কাছেও না যাওয়া: \"বলুন, আমার পালনকর্তা কেবল অশ্লীল বিষয়সমূহ হারাম করেছেন—যা প্রকাশ্য এবং যা গোপন।\" (সূরা আল-আরাফ, আয়াত: ৩৩)", L"Say, 'My Lord has only forbidden immoralities—what is apparent of them and what is concealed...' (Surah Al-A'raf, Ayat: 33)"},
    {L"“লজ্জাশীলতা ঈমানের অঙ্গ।” - (সহিহ মুসলিম)", L"Modesty is a branch of faith. (Sahih Muslim)"},
    {L"“যে ব্যক্তি মন্দ কাজ থেকে বিরত থাকে, সে যেন ভালো কাজই করল।”", L"Whoever abstains from evil deeds is as if he performed good deeds."},
    {L"“আল্লাহ তার বান্দার তওবা কবুল করেন যতক্ষণ না তার মৃত্যুযন্ত্রণা শুরু হয়।”", L"Allah accepts the repentance of His servant so long as his death rattle has not begun."}
};

vector<Quote> hinduQuotes = {
    {L"“যে মনকে নিয়ন্ত্রণ করতে পারে না, তার মন তার সবচেয়ে বড় শত্রু।” - (ভগবদ্গীতা)", L"For him who has conquered the mind, the mind is the best of friends; but for one who has failed to do so, his mind will remain the greatest enemy. (Bhagavad Gita)"},
    {L"“কাম, ক্রোধ এবং লোভ—এই তিনটি নরকের দ্বার।” - (ভগবদ্গীতা)", L"Lust, anger, and greed are the three doors to hell. (Bhagavad Gita)"},
    {L"“ইন্দ্রিয়ের তৃপ্তি ক্ষণস্থায়ী, কিন্তু আত্মার শান্তি চিরস্থায়ী।”", L"Pleasure from the senses is temporary, but the peace of the soul is eternal."},
    {L"“সৎ কর্ম কখনোই বৃথা যায়লগ্ন।”", L"A good deed is never lost."}
};

vector<Quote> christianQuotes = {
    {L"“যে কেউ কোনো স্ত্রীর দিকে কামনার দৃষ্টিতে তাকায়, সে তার মনে আগেই ব্যভিচার করেছে।” - (মথি ৫:২৮)", L"But I tell you that anyone who looks at a woman lustfully has already committed adultery with her in his heart. (Matthew 5:28)"},
    {L"“খারাপ সাহচর্য ভালো চরিত্র নষ্ট করে।” - (১ করিন্থীয় ১৫:৩৩)", L"Bad company ruins good morals. (1 Corinthians 15:33)"},
    {L"“যে নিজেকে সংযত রাখতে পারে, সে শহর জয়কারীর চেয়েও শক্তিশালী।” - (হিতোপদেশ ১৬:৩২)", L"He who rules his spirit is better than he who takes a city. (Proverbs 16:32)"},
    {L"“অহংকার পতনের মূল।” - (হিতোপদেশ ১৬:১৮)", L"Pride goes before destruction. (Proverbs 16:18)"}
};

vector<Quote> universalQuotes = {
    {L"“সফলতা আসে ফোকাস থেকে, ডিস্ট্রাকশন থেকে নয়।”", L"Success comes from focus, not from distraction."},
    {L"“আজকের সময় নষ্ট মানে, কালকের স্বপ্ন নষ্ট।”", L"Wasting time today means ruining tomorrow's dreams."},
    {L"“বড় কিছু পেতে হলে ছোট আনন্দগুলো ত্যাগ করতে হয়।”", L"To achieve something big, you have to sacrifice small pleasures."},
    {L"“যে নিজের মনকে নিয়ন্ত্রণ করতে পারে, সে পৃথিবী জয় করতে পারে।”", L"He who can control his mind can conquer the world."},
    {L"“যেখানে মনোযোগ যায়, সেখানেই শক্তি প্রবাহিত হয়।”", L"Where focus goes, energy flows."}
};

vector<wstring> hardcoreKeywords = {
    L"porn", L"xxx", L"sex", L"nude", L"nsfw", L"sexy", L"hentai", L"rule34", L"milf", 
    L"blowjob", L"tits", L"boobs", L"pussy", L"dick", L"cock", L"escort", L"bdsm", 
    L"fetish", L"erotica", L"dildo", L"webcam", L"camgirls", L"xvideos", L"pornhub", 
    L"xnxx", L"xhamster", L"brazzers", L"onlyfans", L"playboy", L"chaturbate", 
    L"stripchat", L"eporner", L"spankbang", L"redtube", L"youporn", L"mia khalifa", 
    L"sunny leone", L"dani daniels", L"johnny sins", L"kendra lust",
    L"চটি", L"পর্ণ", L"সেক্স", L"নগ্ন", L"উলঙ্গ", L"বেশ্যা", L"মাগি", L"খানকি", 
    L"যৌন", L"পর্ণগ্রাফি", L"রেন্ডি", L"চোদাচুতি", L"গরম ভিডিও", L"খারাপ ছবি",
    L"যৌন মিলন", L"যৌনাঙ্গ", L"চুদো", L"নগ্নতা",
    L"bhabi", L"chudai", L"bangla choti", L"panu", L"desi bhabi", L"mms", L"magi", 
    L"choda", L"chodachudi", L"khanki", L"besha", L"randi", L"nengta", L"nangta", 
    L"baal", L"vodai", L"bokachoda", L"kuttar bacha", L"shuarer bacha", L"kharap video"
};

vector<wstring> romanticKeywords = {
    L"hot dance", L"seductive dance", L"item song", L"belly dance", L"hot", 
    L"kissing scene", L"bikini", L"swimsuit", L"sexy dance", L"cleavage", L"hot scene", 
    L"romantic kiss", L"bedroom scene", L"bath scene", L"rain dance", L"bold scene", 
    L"semi nude", L"lingerie", L"erotic", L"hot song", L"romantic video hot", 
    L"navel show", L"deep neck", L"short dress sexy", L"unfaithful scene"
};

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

wstring toLowerW_Logic(wstring str) {
    for (auto& c : str) c = towlower(c); return str;
}


// --- CRASH-FREE POPUP LOGIC (SAFE THREAD) ---
struct PopupData { wstring quote; bool isFullScreen; };

LRESULT CALLBACK AdultPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT rect; GetClientRect(hwnd, &rect); RectF bgRect(0, 0, rect.right, rect.bottom);
        
        PopupData* pData = (PopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        
        if (pData && pData->isFullScreen) {
            SolidBrush bgBrush(Color(255, 30, 30, 40)); 
            g.FillRectangle(&bgBrush, bgRect);
        } else {
            SolidBrush bgBrush(Color(255, 20, 80, 40)); 
            g.FillRectangle(&bgBrush, bgRect);
            Pen border(AClrRed, 4.0f); g.DrawRectangle(&border, 2.0f, 2.0f, rect.right-4.0f, rect.bottom-4.0f);
        }

        FontFamily ff(L"Segoe UI"); 
        Font fQ(&ff, pData && pData->isFullScreen ? 48 : 38, FontStyleBold, UnitPixel); 
        Font fS(&ff, 20, FontStyleRegular, UnitPixel); 

        SolidBrush textBrush(Color(255, 255, 255, 255)); 
        SolidBrush subBrush(Color(200, 255, 255, 255)); 
        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
        
        if (pData) {
            g.DrawString(pData->quote.c_str(), -1, &fQ, RectF(40.0f, 40.0f, rect.right - 80.0f, rect.bottom - 80.0f), &fmtC, &textBrush);
            if (pData->isFullScreen) {
                g.DrawString(L"Press ESC to close this message.", -1, &fS, RectF(0.0f, rect.bottom - 50.0f, rect.right, 30.0f), &fmtC, &subBrush);
            }
        }
        
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_TIMER && wParam == 2) { 
        KillTimer(hwnd, 2); DestroyWindow(hwnd); PostQuitMessage(0); return 0; 
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        PopupData* pData = (PopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (pData && pData->isFullScreen) { DestroyWindow(hwnd); PostQuitMessage(0); return 0; }
    }

    if (msg == WM_DESTROY) {
        PopupData* pData = (PopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if(pData) delete pData;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void SafePopupThread(wstring quote, bool fullScreen = false) {
    WNDCLASS wc = {0}; 
    wc.lpfnWndProc = AdultPopupWndProc; 
    wc.hInstance = GetModuleHandle(NULL); 
    wc.lpszClassName = "RasFocusAdultPopupClass"; 
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH); 
    RegisterClass(&wc); 

    int screenW = GetSystemMetrics(SM_CXSCREEN); int screenH = GetSystemMetrics(SM_CYSCREEN);
    int w = fullScreen ? screenW : 900; 
    int h = fullScreen ? screenH : 250;
    int x = fullScreen ? 0 : (screenW-w)/2;
    int y = fullScreen ? 0 : 50;
    
    HWND hPopup = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, 
        "RasFocusAdultPopupClass", "Alert", WS_POPUP, x, y, w, h, NULL, NULL, GetModuleHandle(NULL), NULL);
    
    if (hPopup) {
        PopupData* data = new PopupData{ quote, fullScreen };
        SetWindowLongPtr(hPopup, GWLP_USERDATA, (LONG_PTR)data);
        SetLayeredWindowAttributes(hPopup, 0, fullScreen ? 250 : 240, LWA_ALPHA); 
        ShowWindow(hPopup, SW_SHOW); 
        SetForegroundWindow(hPopup); 
        
        if (!fullScreen) SetTimer(hPopup, 2, 6000, NULL); 
        
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
    }
}

void TriggerAdultPopup(bool isWarning = false, wstring customMsg = L"", bool isFullScreen = false) {
    if(!isFullScreen) {
        totalBlockedCount++; 
        cleanStreakDays = 0; // Reset streak on violation
        SaveAdultSettings(); 
    }
    
    wstring finalQuote = L"";

    if (isWarning) {
        finalQuote = customMsg;
    } else {
        int rIdx = 0;
        if (adultReligion == 0) { rIdx = rand() % muslimQuotes.size(); finalQuote = (adultLanguage == 0) ? muslimQuotes[rIdx].bn : muslimQuotes[rIdx].en; }
        else if (adultReligion == 1) { rIdx = rand() % hinduQuotes.size(); finalQuote = (adultLanguage == 0) ? hinduQuotes[rIdx].bn : hinduQuotes[rIdx].en; }
        else if (adultReligion == 2) { rIdx = rand() % christianQuotes.size(); finalQuote = (adultLanguage == 0) ? christianQuotes[rIdx].bn : christianQuotes[rIdx].en; }
        else { rIdx = rand() % universalQuotes.size(); finalQuote = (adultLanguage == 0) ? universalQuotes[rIdx].bn : universalQuotes[rIdx].en; }
    }
    thread t(SafePopupThread, finalQuote, isFullScreen);
    t.detach();
}

void closeActiveTab() {
    keybd_event(VK_CONTROL, 0, 0, 0); keybd_event('W', 0, 0, 0);
    keybd_event('W', 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
}

// --- GLOBAL KEYLOGGER (TYPING DETECTION) ---
HHOOK hKeyboardHook = NULL;
string globalKeyBuffer = "";

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN && !isPanicActive) {
        KBDLLHOOKSTRUCT* kbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = kbdStruct->vkCode;

        if ((vkCode >= 'A' && vkCode <= 'Z') || (vkCode >= '0' && vkCode <= '9') || vkCode == VK_SPACE || vkCode == VK_OEM_PERIOD) {
            char c = MapVirtualKey(vkCode, MAPVK_VK_TO_CHAR);
            if (vkCode == VK_OEM_PERIOD) c = '.';
            globalKeyBuffer += tolower(c);
            if (globalKeyBuffer.length() > 100) globalKeyBuffer.erase(0, 1);

            wstring wBuffer(globalKeyBuffer.begin(), globalKeyBuffer.end());
            bool shouldBlock = false;

            if (cbHardcore && !shouldBlock) { for (const auto& k : hardcoreKeywords) if (wBuffer.find(toLowerW_Logic(k)) != wstring::npos) shouldBlock = true; }
            if (cbRomantic && !shouldBlock) { for (const auto& k : romanticKeywords) if (wBuffer.find(toLowerW_Logic(k)) != wstring::npos) shouldBlock = true; }
            if (cbAdultWeb && !shouldBlock) { 
                for (const auto& w : adultWebsites) {
                    size_t dotPos = w.find(L".");
                    wstring coreName = (dotPos != wstring::npos) ? w.substr(0, dotPos) : w;
                    if (coreName.length() > 2 && wBuffer.find(coreName) != wstring::npos) { shouldBlock = true; break; }
                }
            }
            if (!customAdultKeywords.empty() && !shouldBlock) {
                for (const auto& item : customAdultKeywords) {
                    if (!item.name.empty() && wBuffer.find(toLowerW_Logic(item.name)) != wstring::npos) { shouldBlock = true; break; }
                }
            }

            if (shouldBlock) {
                globalKeyBuffer = ""; 
                closeActiveTab(); 
                TriggerAdultPopup(); 
            }
        } 
        else if (vkCode == VK_BACK) {
            if (!globalKeyBuffer.empty()) globalKeyBuffer.pop_back();
        }
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

void StartKeyloggerThread() {
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, NULL, 0);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
}

// ==========================================
// SMART URL EXTRACTOR & DETECTION (UIA)
// ==========================================
IUIAutomation* pAutomation = NULL;

wstring GetBrowserURL_Fallback(HWND hBrowser) {
    wstring url = L"";
    if (!pAutomation) return url;

    IUIAutomationElement* pElement = NULL;
    HRESULT hr = pAutomation->ElementFromHandle(hBrowser, &pElement);
    
    if (SUCCEEDED(hr) && pElement) {
        IUIAutomationCondition* pCondition = NULL;
        IUIAutomationElement* pEdit = NULL;

        // Fallback 1: Control Type
        VARIANT varProp;
        varProp.vt = VT_I4;
        varProp.lVal = UIA_EditControlTypeId;
        pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId, varProp, &pCondition);
        
        if (pCondition) {
            pElement->FindFirst(TreeScope_Descendants, pCondition, &pEdit);
            pCondition->Release();
        }

        // Fallback 2: Name Property (English)
        if (!pEdit) {
            VARIANT varName;
            varName.vt = VT_BSTR;
            varName.bstrVal = SysAllocString(L"Address and search bar");
            pAutomation->CreatePropertyCondition(UIA_NamePropertyId, varName, &pCondition);
            if (pCondition) { pElement->FindFirst(TreeScope_Descendants, pCondition, &pEdit); pCondition->Release(); }
            SysFreeString(varName.bstrVal);
        }

        // Fallback 3: Name Property (Bangla)
        if (!pEdit) {
            VARIANT varNameBn;
            varNameBn.vt = VT_BSTR;
            varNameBn.bstrVal = SysAllocString(L"ঠিকানা এবং অনুসন্ধান বার");
            pAutomation->CreatePropertyCondition(UIA_NamePropertyId, varNameBn, &pCondition);
            if (pCondition) { pElement->FindFirst(TreeScope_Descendants, pCondition, &pEdit); pCondition->Release(); }
            SysFreeString(varNameBn.bstrVal);
        }

        if (pEdit) {
            VARIANT varValue;
            VariantInit(&varValue);
            hr = pEdit->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &varValue);
            if (SUCCEEDED(hr) && varValue.vt == VT_BSTR && varValue.bstrVal != NULL) {
                url = wstring(varValue.bstrVal);
            }
            VariantClear(&varValue);
            pEdit->Release();
        }
        pElement->Release();
    }
    return url;
}

bool IsFacebookReelsOrVideo(const wstring& url, bool checkReels, bool checkAllVideo) {
    wstring lowerUrl = toLowerW_Logic(url);
    if (checkReels) {
        if (lowerUrl.find(L"facebook.com/reel") != wstring::npos || 
            lowerUrl.find(L"facebook.com/reels") != wstring::npos ||
            lowerUrl.find(L"instagram.com/reels") != wstring::npos || 
            lowerUrl.find(L"youtube.com/shorts") != wstring::npos) {
            return true;
        }
    }
    if (checkAllVideo) {
        if (lowerUrl.find(L"facebook.com/watch") != wstring::npos || 
            lowerUrl.find(L"facebook.com/video") != wstring::npos ||
            lowerUrl.find(L"facebook.com/gaming") != wstring::npos) {
            return true;
        }
    }
    return false;
}

// --- BACKGROUND THREAD ---
void AdultBackgroundThread() {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, IID_IUIAutomation, (void**)&pAutomation);
    wstring lastTitle = L"";

    lastPeriodicPopupTime = GetTickCount();

    while (true) {
        
        // 24 Hour Lock Check
        if (cb24HourLock) {
            if (GetTickCount64() >= lock24hEndTime) {
                cb24HourLock = false; 
                isAdultFocusActive = false;
                SaveAdultSettings();
            } else {
                isAdultFocusActive = true; 
            }
        }

        // Standard Focus Check
        if (isAdultFocusActive && controlMode == 0 && GetTickCount64() >= focusEndTime && !cb24HourLock) {
            isAdultFocusActive = false;
            SaveAdultSettings();
        }

        // Periodic Full Screen Popups (Every 25 mins)
        if (cbPeriodicPopups && isAdultFocusActive) {
            if (GetTickCount() - lastPeriodicPopupTime >= 25 * 60 * 1000) {
                TriggerAdultPopup(false, L"", true); 
                lastPeriodicPopupTime = GetTickCount();
            }
        }

        if (!isPanicActive && (cbAdultWeb || cbHardcore || cbRomantic || cbFbReels || cbYtShorts || isAdultFocusActive)) {
            HWND hActive = GetForegroundWindow();
            if (hActive) {
                wchar_t t[256]; GetWindowTextW(hActive, t, 256); wstring title(t);
                
                if (!title.empty() && title != lastTitle) {
                    lastTitle = title; wstring lowerTitle = toLowerW_Logic(title);
                    bool shouldBlockTab = false;

                    if (isAdultFocusActive) {
                        if (lowerTitle.find(L"task manager") != wstring::npos || lowerTitle.find(L"taskmgr") != wstring::npos) {
                            PostMessage(hActive, WM_CLOSE, 0, 0);
                            TriggerAdultPopup(true, L"Security Alert: Task Manager is blocked!");
                            continue;
                        }
                        if (lowerTitle.find(L"uninstall") != wstring::npos || lowerTitle.find(L"programs and features") != wstring::npos || lowerTitle.find(L"অ্যাপস ও বৈশিষ্ট্য") != wstring::npos || lowerTitle.find(L"control panel") != wstring::npos) {
                            PostMessage(hActive, WM_CLOSE, 0, 0);
                            TriggerAdultPopup(true, L"Security Alert: Uninstallation is blocked!");
                            continue;
                        }
                    }

                    if (cbHardcore && !shouldBlockTab) { for (const auto& k : hardcoreKeywords) if (lowerTitle.find(toLowerW_Logic(k)) != wstring::npos) shouldBlockTab = true; }
                    if (cbRomantic && !shouldBlockTab) { for (const auto& k : romanticKeywords) if (lowerTitle.find(toLowerW_Logic(k)) != wstring::npos) shouldBlockTab = true; }
                    
                    if (!customAdultKeywords.empty() && !shouldBlockTab) {
                        for (const auto& item : customAdultKeywords) {
                            if (!item.name.empty() && lowerTitle.find(toLowerW_Logic(item.name)) != wstring::npos) { shouldBlockTab = true; break; }
                        }
                    }

                    if (shouldBlockTab) {
                        closeActiveTab(); 
                        TriggerAdultPopup(); 
                    } 
                    else if (lowerTitle.find(L"google chrome") != wstring::npos || lowerTitle.find(L"edge") != wstring::npos || lowerTitle.find(L"brave") != wstring::npos) {
                        
                        // --- Smart URL Fallback Detection ---
                        wstring url = GetBrowserURL_Fallback(hActive);
                        bool isUrlBlocked = false;
                        
                        if (cbAdultWeb) {
                            for (const auto& site : adultWebsites) {
                                if (url.find(site) != wstring::npos || lowerTitle.find(site) != wstring::npos) { isUrlBlocked = true; break; }
                            }
                        }

                        // Check Reels and Shorts using advanced logic
                        if (!isUrlBlocked && (cbFbReels || cbYtShorts)) {
                            if(IsFacebookReelsOrVideo(url, true, false)) isUrlBlocked = true;
                        }

                        if (isUrlBlocked) {
                            closeActiveTab(); Sleep(300); 
                            TriggerAdultPopup(); 
                            lastTitle = L""; 
                        }
                    }
                }
            }
        }
        Sleep(500); 
    }
}

// ==========================================
// 100% AUTO START BACKGROUND TRICK (NO MAIN.CPP CHANGE)
// ==========================================
static bool adultThreadStarted = false;

void InitAdultSystemOnBoot() {
    if (!adultThreadStarted) { 
        LoadAdultSettings(); 
        thread t(AdultBackgroundThread); t.detach(); 
        thread kl(StartKeyloggerThread); kl.detach(); 
        adultThreadStarted = true; 
    }
}

// এই স্ট্রাকচারটি অ্যাপ রান হওয়ার সাথে সাথেই ব্যাকগ্রাউন্ড থ্রেডগুলো অটো-স্টার্ট করে দেবে
struct AdultAutoStarter {
    AdultAutoStarter() {
        thread t([]() {
            Sleep(1000); // Wait 1 second for app to fully boot
            InitAdultSystemOnBoot();
        });
        t.detach();
    }
} g_adultAutoStarter;


static void DrawAdultOverlaySpinner(Graphics& g, float x, float y, const wstring& valStr, bool hM, bool hP, Font* fIcon, Font* fBold) {
    SolidBrush brushBtn(AClrBorder); SolidBrush brushBtnHover(AClrGrayText);
    SolidBrush brushWhite(AClrWhite); SolidBrush brushDark(AClrDark);
    Pen penBorder(AClrBorder, 1.5f);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);

    RectF mRect(x, y, 32.0f, 36.0f); RectF tRect(x + 32.0f, y, 60.0f, 36.0f); RectF pRect(x + 92.0f, y, 32.0f, 36.0f);

    g.FillRectangle(hM ? &brushBtnHover : &brushBtn, mRect); g.DrawRectangle(&penBorder, mRect.X, mRect.Y, mRect.Width, mRect.Height);
    g.DrawString(L"\xE738", -1, fIcon, mRect, &fmtC, &brushDark);
    g.FillRectangle(&brushWhite, tRect); g.DrawRectangle(&penBorder, tRect.X, tRect.Y, tRect.Width, tRect.Height);
    g.DrawString(valStr.c_str(), -1, fBold, tRect, &fmtC, &brushDark);
    g.FillRectangle(hP ? &brushBtnHover : &brushBtn, pRect); g.DrawRectangle(&penBorder, pRect.X, pRect.Y, pRect.Width, pRect.Height);
    g.DrawString(L"\xE710", -1, fIcon, pRect, &fmtC, &brushDark);
}


// --- DRAWING ---
void DrawAdultBlockTab(Graphics& g, float cx, float cy, float cw, float ch) {
    s_contentX = cx; s_contentY = cy; s_contentW = cw; s_contentH = ch;

    // Safety fallback initialization if autostarter missed somehow
    InitAdultSystemOnBoot();

    FontFamily ff(L"Segoe UI");
    Font fTabBtn(&ff, 15, FontStyleBold, UnitPixel); 
    Font fTitle(&ff, 24, FontStyleBold, UnitPixel); 
    Font fNorm(&ff, 15, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 16, FontStyleBold, UnitPixel);
    Font fSmall(&ff, 13, FontStyleRegular, UnitPixel);
    Font fTiny(&ff, 11, FontStyleRegular, UnitPixel);
    Font fBigTitle(&ff, 42, FontStyleBold, UnitPixel);
    FontFamily ffi(L"Segoe MDL2 Assets"); 
    Font fIcon(&ffi, 18, FontStyleRegular, UnitPixel);
    Font fSmallIcon(&ffi, 14, FontStyleRegular, UnitPixel);
    Font fLargeIcon(&ffi, 36, FontStyleRegular, UnitPixel);
    
    SolidBrush bWhite(AClrWhite); SolidBrush bDark(AClrDark); SolidBrush bGray(AClrGrayText);
    SolidBrush bTeal(AClrTeal); SolidBrush bBg(AClrBg); Pen pBorder(AClrBorder, 1.5f);
    
    SolidBrush bRed(Color(255, 230, 50, 50)); 
    SolidBrush bGreen(Color(255, 50, 200, 50));
    SolidBrush bLink(hLinkAiFilter || hLinkStrict ? AClrTealHover : AClrLink); 

    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    // ==========================================
    // --- HEADER: Sub-Tab Navigation ---
    // ==========================================
    g.FillRectangle(&bWhite, cx, cy, cw, 60.0f);
    
    float tabW = 200.0f, tabH = 40.0f;
    float tab1X = cx + 20.0f, tab2X = tab1X + tabW + 10.0f, tab3X = tab2X + tabW + 10.0f, tabY = cy + 10.0f;

    SolidBrush bTab1(ad_activeSubTab == 0 ? Color(255, 12, 168, 176) : (ad_hovTab1 ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    SolidBrush bTab2(ad_activeSubTab == 1 ? Color(255, 12, 168, 176) : (ad_hovTab2 ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    SolidBrush bTab3(ad_activeSubTab == 2 ? Color(255, 12, 168, 176) : (ad_hovTab3 ? Color(255, 230, 230, 230) : Color(255, 245, 245, 245)));
    
    SolidBrush bT1(ad_activeSubTab == 0 ? Color(255, 255, 255, 255) : Color(255, 100, 100, 100));
    SolidBrush bT2(ad_activeSubTab == 1 ? Color(255, 255, 255, 255) : Color(255, 100, 100, 100));
    SolidBrush bT3(ad_activeSubTab == 2 ? Color(255, 255, 255, 255) : Color(255, 100, 100, 100));

    g.FillRectangle(&bTab1, tab1X, tabY, tabW, tabH); g.DrawString(L"Safe Browsing", -1, &fTabBtn, RectF(tab1X, tabY, tabW, tabH), &fC, &bT1);
    g.FillRectangle(&bTab2, tab2X, tabY, tabW, tabH); g.DrawString(L"AI Filter", -1, &fTabBtn, RectF(tab2X, tabY, tabW, tabH), &fC, &bT2);
    g.FillRectangle(&bTab3, tab3X, tabY, tabW, tabH); g.DrawString(L"Strict Protocols", -1, &fTabBtn, RectF(tab3X, tabY, tabW, tabH), &fC, &bT3);

    // Main Background
    float bY = cy + 60.0f;
    g.FillRectangle(&bBg, cx, bY, cw, ch - 60.0f);

    float bX = cx + 40.0f; 
    
    // ==========================================
    // --- TAB 0: SAFE BROWSING ---
    // ==========================================
    if (ad_activeSubTab == 0) {
        // --- ROW 1: Controls ---
        float row1Y = bY + 20.0f; 
        
        RectF startBtn(bX, row1Y, 150.0f, 35.0f);
        SolidBrush sb(isAdultFocusActive ? Color(255, 230, 50, 50) : AClrGreen);
        GraphicsPath* sbp = GetBlockRoundRectPath(startBtn, 4); g.FillPath(&sb, sbp); delete sbp;
        
        wstring startTxt = L"Start Focus";
        if (isAdultFocusActive) {
            if (cb24HourLock) {
                ULONGLONG left = lock24hEndTime > GetTickCount64() ? lock24hEndTime - GetTickCount64() : 0;
                int hLeft = left / 3600000;
                int mLeft = (left % 3600000) / 60000;
                startTxt = L"Locked (" + to_wstring(hLeft) + L"h " + to_wstring(mLeft) + L"m)";
            }
            else if (controlMode == 0) {
                ULONGLONG left = focusEndTime > GetTickCount64() ? focusEndTime - GetTickCount64() : 0;
                int mLeft = (left / 60000) + 1;
                startTxt = L"Locked (" + to_wstring(mLeft) + L"m)";
            } else {
                startTxt = L"Stop Focus";
            }
        }
        g.DrawString(startTxt.c_str(), -1, &fBold, startBtn, &fC, &bWhite);

        SolidBrush activeTextBrush(isAdultFocusActive ? AClrGrayText : AClrDark);

        auto drawBeautifulDropdown = [&](float x, float y, float w, float h, wstring text, bool hover) {
            RectF r(x, y, w, h); GraphicsPath* p = GetBlockRoundRectPath(r, 4);
            SolidBrush dBg(hover && !isAdultFocusActive ? AClrBgHover : AClrWhite);
            g.FillPath(&dBg, p); g.DrawPath(&pBorder, p); delete p;
            g.DrawString(text.c_str(), -1, &fNorm, RectF(x+10, y, w-35, h), &fL, &activeTextBrush);
            g.DrawLine(&pBorder, x+w-30, y, x+w-30, y+h);
            g.DrawString(L"\xE70D", -1, &fSmallIcon, RectF(x+w-30, y, 30, h), &fC, &bGray);
        };

        g.DrawString(L"Mode:", -1, &fBold, RectF(bX + 170.0f, row1Y, 50.0f, 35.0f), &fL, &activeTextBrush);
        drawBeautifulDropdown(bX + 225.0f, row1Y, 130.0f, 35.0f, ctrlModes[controlMode], hoverControlDrop);

        g.DrawString(L"Religion:", -1, &fBold, RectF(bX + 375.0f, row1Y, 70.0f, 35.0f), &fL, &activeTextBrush);
        drawBeautifulDropdown(bX + 445.0f, row1Y, 120.0f, 35.0f, religions[adultReligion], hoverRelDrop);

        g.DrawString(L"Lang:", -1, &fBold, RectF(bX + 585.0f, row1Y, 50.0f, 35.0f), &fL, &activeTextBrush);
        drawBeautifulDropdown(bX + 635.0f, row1Y, 90.0f, 35.0f, languages[adultLanguage], hoverLangDrop);

        float div1Y = row1Y + 50.0f;
        g.DrawLine(&pBorder, bX, div1Y, cx + cw - 40.0f, div1Y); 
        
        // --- ROW 2: TWO COLUMNS (Checkboxes | Custom Table) ---
        float row2Y = div1Y + 15.0f; 
        auto drawCb = [&](float x, float y, const wchar_t* txt, bool state) {
            SolidBrush cbb(state ? (isAdultFocusActive ? AClrGrayText : AClrTeal) : AClrWhite); 
            RectF r(x, y, 18.0f, 18.0f);
            g.FillRectangle(&cbb, r); g.DrawRectangle(&pBorder, r.X, r.Y, r.Width, r.Height);
            if (state) g.DrawString(L"\xE73E", -1, &fIcon, r, &fC, &bWhite);
            g.DrawString(txt, -1, &fNorm, RectF(x + 25.0f, y-2, 250.0f, 22.0f), &fL, &activeTextBrush);
        };

        float currY = row2Y;
        drawCb(bX, currY, L"Block Adult Websites", cbAdultWeb); currY += 35.0f;
        drawCb(bX, currY, L"Block Hardcore Keywords", cbHardcore); currY += 35.0f;
        drawCb(bX, currY, L"Block Romantic/Softcore", cbRomantic); currY += 35.0f;
        drawCb(bX, currY, L"Block Facebook/YT Shorts & Reels", cbFbReels); currY += 35.0f; // Label updated

        // Right Column: Custom Keywords
        float rX = bX + 310.0f;
        float rY = row2Y;
        g.DrawString(L"Custom Keywords:", -1, &fBold, RectF(rX, rY, 200.0f, 25.0f), &fL, &bDark); rY += 30.0f;
        
        RectF customInpRect(rX, rY, 250.0f, 32.0f);
        GraphicsPath* cip = GetBlockRoundRectPath(customInpRect, 4);
        g.FillPath(&bWhite, cip); 
        if (isCustomInputActive) { Pen pTeal(AClrTeal, 2.0f); g.DrawPath(&pTeal, cip); } 
        else { g.DrawPath(&pBorder, cip); }
        delete cip;
        
        if(customInputText.empty() && !isCustomInputActive) g.DrawString(L"e.g. badword", -1, &fNorm, RectF(customInpRect.X+10, customInpRect.Y, 230, 32), &fL, &bGray);
        else {
            g.DrawString(customInputText.c_str(), -1, &fNorm, RectF(customInpRect.X+10, customInpRect.Y, 230, 32), &fL, &bDark);
            if(isCustomInputActive && (GetTickCount()/500)%2==0) {
                Graphics gTemp(GetDesktopWindow()); RectF bR; gTemp.MeasureString(customInputText.c_str(), -1, &fNorm, PointF(0,0), &bR);
                float cursorX = customInputText.empty() ? customInpRect.X+10 : customInpRect.X+12+bR.Width;
                g.FillRectangle(&bDark, cursorX, customInpRect.Y+6, 1.5f, 20.0f);
            }
        }

        RectF cAddRect(rX + 260.0f, rY, 70.0f, 32.0f);
        GraphicsPath* cAddP = GetBlockRoundRectPath(cAddRect, 4);
        SolidBrush cAddBrush(hoverCustomAddBtn ? AClrTealHover : AClrTeal);
        g.FillPath(&cAddBrush, cAddP); delete cAddP;
        g.DrawString(L"+ Add", -1, &fBold, cAddRect, &fC, &bWhite);

        rY += 40.0f;
        RectF cTableRect(rX, rY, 330.0f, 100.0f);
        g.FillRectangle(&bWhite, cTableRect); g.DrawRectangle(&pBorder, cTableRect.X, cTableRect.Y, cTableRect.Width, cTableRect.Height);
        
        int maxCustomDraw = min(3, (int)customAdultKeywords.size() - customScrollOffset);
        float itemY = cTableRect.Y + 5.0f;
        for (int i = 0; i < maxCustomDraw; ++i) {
            int dataIdx = customScrollOffset + i;
            if (dataIdx >= customAdultKeywords.size()) break;
            g.DrawString(customAdultKeywords[dataIdx].name.c_str(), -1, &fNorm, RectF(rX + 10.0f, itemY, 280.0f, 30.0f), &fL, &bDark);
            SolidBrush crossBrush(isAdultFocusActive ? AClrGrayText : (customAdultKeywords[dataIdx].isHoveredCross ? AClrRed : AClrGrayText));
            g.DrawString(L"\xE711", -1, &fSmallIcon, RectF(rX + 290.0f, itemY, 30.0f, 30.0f), &fC, &crossBrush);
            g.DrawLine(&pBorder, rX + 5.0f, itemY + 30.0f, rX + 325.0f, itemY + 30.0f);
            itemY += 30.0f;
        }
        if (customAdultKeywords.size() > 3) g.FillRectangle(&bGray, cTableRect.X + cTableRect.Width - 4.0f, cTableRect.Y, 4.0f, cTableRect.Height);

        // Divider 2
        float row3Y = row2Y + 190.0f; 
        g.DrawLine(&pBorder, bX, row3Y - 15.0f, cx + cw - 40.0f, row3Y - 15.0f); 

        // ==========================================
        // --- ROW 3: STREAK & SPECIAL OPTIONS ---
        // ==========================================
        // Daily Streak Card
        RectF streakCard(bX, row3Y, 340.0f, 110.0f);
        GraphicsPath* sPath = GetBlockRoundRectPath(streakCard, 8);
        SolidBrush cardBg(AClrCardBg);
        g.FillPath(&cardBg, sPath); g.DrawPath(&pBorder, sPath); delete sPath;

        g.DrawString(L"\xE735", -1, &fLargeIcon, RectF(bX + 15.0f, row3Y + 20.0f, 50.0f, 70.0f), &fL, &bTeal); // Star Icon
        g.DrawString(L"Clean Streak", -1, &fBold, RectF(bX + 70.0f, row3Y + 15.0f, 200.0f, 25.0f), &fL, &bDark);
        wstring todayStr = GetTodayDateString();
        g.DrawString(todayStr.c_str(), -1, &fSmall, RectF(bX + 70.0f, row3Y + 35.0f, 200.0f, 20.0f), &fL, &bGray);
        
        wstring streakTxt = to_wstring(cleanStreakDays) + L" Days";
        g.DrawString(streakTxt.c_str(), -1, &fBigTitle, RectF(bX + 68.0f, row3Y + 55.0f, 200.0f, 40.0f), &fL, &bTeal);
        
        if(cleanStreakDays == 0) g.DrawString(L"Stay focused to build your streak!", -1, &fSmall, RectF(bX + 70.0f, row3Y + 90.0f, 250.0f, 20.0f), &fL, &bRed);
        else g.DrawString(L"Keep up the great work!", -1, &fSmall, RectF(bX + 70.0f, row3Y + 90.0f, 250.0f, 20.0f), &fL, &bGreen);

        // Special Options 
        float spX = bX + 360.0f;
        float spY = row3Y + 10.0f;

        auto drawSpecialCb = [&](float x, float y, const wchar_t* title, const wchar_t* sub, bool state, bool isHovered) {
            SolidBrush cbb(state ? (isAdultFocusActive && cb24HourLock ? AClrGrayText : AClrTeal) : AClrWhite); 
            RectF r(x, y, 18.0f, 18.0f);
            g.FillRectangle(&cbb, r); g.DrawRectangle(&pBorder, r.X, r.Y, r.Width, r.Height);
            if (state) g.DrawString(L"\xE73E", -1, &fIcon, r, &fC, &bWhite);
            
            g.DrawString(title, -1, &fBold, RectF(x + 25.0f, y - 2.0f, 300.0f, 22.0f), &fL, &activeTextBrush);
            
            if (isHovered) {
                g.DrawString(sub, -1, &fTiny, RectF(x + 25.0f, y + 20.0f, 320.0f, 30.0f), &fL, &bGray);
            }
        };

        drawSpecialCb(spX, spY, L"24-Hour Lockdown Mode", L"Force Focus for 24h. Cannot be undone.", cb24HourLock, hCb24HourLock);
        spY += 45.0f;
        drawSpecialCb(spX, spY, L"Periodic Religious Popups", L"Show fullscreen religious quotes every 25 mins.", cbPeriodicPopups, hCbPeriodicPopups);

        // ==========================================
        // --- QUICK LINKS ---
        // ==========================================
        float linkY = row3Y + 125.0f;
        SolidBrush bLinkAi(hLinkAiFilter ? AClrTealHover : AClrLink);
        SolidBrush bLinkStr(hLinkStrict ? AClrTealHover : AClrLink);
        
        g.DrawString(L"Go to AI Filter Settings", -1, &fBold, RectF(bX, linkY, 200.0f, 20.0f), &fL, &bLinkAi);
        g.DrawString(L"\xE76C", -1, &fSmallIcon, RectF(bX + 175.0f, linkY, 20.0f, 20.0f), &fL, &bLinkAi);

        g.DrawString(L"Go to Strict Protocols", -1, &fBold, RectF(bX + 250.0f, linkY, 200.0f, 20.0f), &fL, &bLinkStr);
        g.DrawString(L"\xE76C", -1, &fSmallIcon, RectF(bX + 415.0f, linkY, 20.0f, 20.0f), &fL, &bLinkStr);
    }
    else if (ad_activeSubTab == 1) {
        DrawAiFilterTab(g, cx, cy + 60.0f, cw, ch - 60.0f);
    }
    else if (ad_activeSubTab == 2) {
        DrawStrictProtocolsTab(g, cx, cy + 60.0f, cw, ch - 60.0f);
    }

    // --- OVERLAYS ---
    if (showTimeOverlay || showPassOverlay) {
        SolidBrush overlayBg(AClrOverlay); 
        g.FillRectangle(&overlayBg, cx, cy, cw, ch);

        float ovW = 400.0f; float ovH = 220.0f;
        float ovX = cx + (cw - ovW) / 2.0f; float ovY = cy + (ch - ovH) / 2.0f;

        RectF ovRect(ovX, ovY, ovW, ovH);
        GraphicsPath* op = GetBlockRoundRectPath(ovRect, 8);
        g.FillPath(&bBg, op); g.DrawPath(&pBorder, op); delete op;

        if (showTimeOverlay) {
            g.DrawString(L"SET FOCUS DURATION", -1, &fTitle, RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fC, &bDark);
            g.DrawString(L"Hours:", -1, &fBold, RectF(ovX + 50.0f, ovY + 80.0f, 60.0f, 36.0f), &fL, &bDark);
            DrawAdultOverlaySpinner(g, ovX + 110.0f, ovY + 80.0f, to_wstring(focusHours), hTimeHM, hTimeHP, &fIcon, &fBold);
            g.DrawString(L"Mins:", -1, &fBold, RectF(ovX + 250.0f, ovY + 80.0f, 50.0f, 36.0f), &fL, &bDark);
            DrawAdultOverlaySpinner(g, ovX + 300.0f, ovY + 80.0f, to_wstring(focusMins), hTimeMM, hTimeMP, &fIcon, &fBold);

            RectF cancelRect(ovX + 50.0f, ovY + 150.0f, 140.0f, 40.0f);
            SolidBrush cancelBrush(hTimeCancel ? AClrBgHover : AClrWhite); 
            GraphicsPath* cp = GetBlockRoundRectPath(cancelRect, 4);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&pBorder, cp); delete cp;
            g.DrawString(L"Cancel", -1, &fBold, cancelRect, &fC, &bDark);

            RectF startORect(ovX + 210.0f, ovY + 150.0f, 140.0f, 40.0f);
            SolidBrush startBrush(hTimeStart ? AClrTealHover : AClrTeal); 
            GraphicsPath* sp = GetBlockRoundRectPath(startORect, 4);
            g.FillPath(&startBrush, sp); delete sp;
            g.DrawString(L"Start Focus", -1, &fBold, startORect, &fC, &bWhite);
        }
        else if (showPassOverlay) {
            wstring titleTxt = isStoppingFocus ? L"ENTER PASSWORD TO STOP" : L"ENTER FRIEND'S PASSWORD";
            g.DrawString(titleTxt.c_str(), -1, &fTitle, RectF(ovX, ovY + 20.0f, ovW, 30.0f), &fC, &bDark);
            RectF passInpRect(ovX + 40.0f, ovY + 80.0f, ovW - 80.0f, 40.0f);
            GraphicsPath* pp = GetBlockRoundRectPath(passInpRect, 4);
            Pen pTealPass(AClrTeal, 2.0f); 
            g.FillPath(&bWhite, pp); g.DrawPath(isPassInputActive ? &pTealPass : &pBorder, pp); delete pp;
            
            wstring displayPass = wstring(inputPassText.length(), L'*');
            if (inputPassText.empty() && !isPassInputActive) g.DrawString(L"Type password here...", -1, &fNorm, passInpRect, &fC, &bGray);
            else {
                g.DrawString(displayPass.c_str(), -1, &fTitle, RectF(ovX + 50.0f, ovY + 85.0f, ovW - 100.0f, 30.0f), &fL, &bDark);
                if (isPassInputActive && (GetTickCount() / 500) % 2 == 0) {
                     Graphics gTemp(GetDesktopWindow()); RectF bRect;
                     gTemp.MeasureString(displayPass.c_str(), -1, &fTitle, PointF(0,0), &bRect);
                     float cxp = ovX + 52.0f + bRect.Width;
                     if(displayPass.empty()) cxp = ovX + 52.0f;
                     g.FillRectangle(&bDark, cxp, ovY + 90.0f, 1.5f, 20.0f);
                }
            }

            RectF cancelRect(ovX + 40.0f, ovY + 150.0f, 140.0f, 40.0f);
            SolidBrush cancelBrush(hPassCancel ? AClrBgHover : AClrWhite); 
            GraphicsPath* cp = GetBlockRoundRectPath(cancelRect, 4);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&pBorder, cp); delete cp;
            g.DrawString(L"Cancel", -1, &fBold, cancelRect, &fC, &bDark);

            RectF confRect(ovX + 200.0f, ovY + 150.0f, 160.0f, 40.0f);
            SolidBrush confBrush(hPassConfirm ? AClrTealHover : AClrTeal); 
            GraphicsPath* sp = GetBlockRoundRectPath(confRect, 4);
            g.FillPath(&confBrush, sp); delete sp;
            g.DrawString(L"Confirm", -1, &fBold, confRect, &fC, &bWhite);
        }
    }

    // DRAW OPEN DROPDOWNS ON TOP
    if (ad_activeSubTab == 0 && !showTimeOverlay && !showPassOverlay) {
        float row1Y = cy + 80.0f;
        if (isControlDropOpen && !isAdultFocusActive) {
            RectF dR(bX + 225.0f, row1Y + 36.0f, 130.0f, 2 * 35.0f);
            GraphicsPath* dp = GetBlockRoundRectPath(dR, 4);
            g.FillPath(&bWhite, dp); g.DrawPath(&pBorder, dp); delete dp;
            for(int i=0; i<2; i++) {
                SolidBrush hB(hoverCtrlIdx == i ? AClrBgHover : AClrWhite);
                g.FillRectangle(&hB, dR.X+1, dR.Y + (i*35.0f)+1, dR.Width-2, 33.0f);
                g.DrawString(ctrlModes[i].c_str(), -1, &fNorm, RectF(dR.X+10, dR.Y+(i*35), dR.Width-10, 35), &fL, &bDark);
            }
        }
        if (isRelDropOpen && !isAdultFocusActive) {
            RectF dR(bX + 445.0f, row1Y + 36.0f, 120.0f, 4 * 35.0f);
            GraphicsPath* dp = GetBlockRoundRectPath(dR, 4);
            g.FillPath(&bWhite, dp); g.DrawPath(&pBorder, dp); delete dp;
            for(int i=0; i<4; i++) {
                SolidBrush hB(hoverRelIdx == i ? AClrBgHover : AClrWhite);
                g.FillRectangle(&hB, dR.X+1, dR.Y + (i*35.0f)+1, dR.Width-2, 33.0f);
                g.DrawString(religions[i].c_str(), -1, &fNorm, RectF(dR.X+10, dR.Y+(i*35), dR.Width-10, 35), &fL, &bDark);
            }
        }
        if (isLangDropOpen && !isAdultFocusActive) {
            RectF dL(bX + 635.0f, row1Y + 36.0f, 90.0f, 2 * 35.0f);
            GraphicsPath* dp = GetBlockRoundRectPath(dL, 4);
            g.FillPath(&bWhite, dp); g.DrawPath(&pBorder, dp); delete dp;
            for(int i=0; i<2; i++) {
                SolidBrush hB(hoverLangIdx == i ? AClrBgHover : AClrWhite);
                g.FillRectangle(&hB, dL.X+1, dL.Y + (i*35.0f)+1, dL.Width-2, 33.0f);
                g.DrawString(languages[i].c_str(), -1, &fNorm, RectF(dL.X+10, dL.Y+(i*35), dL.Width-10, 35), &fL, &bDark);
            }
        }
    }
}

void ProcessAdultMouseMove(float x, float y) {
    float cx = s_contentX; float cy = s_contentY; float cw = s_contentW; float ch = s_contentH;
    float bX = cx + 40.0f; 

    float row1Y = cy + 80.0f;
    float row2Y = row1Y + 65.0f;
    float row3Y = row2Y + 190.0f;
    float linkY = row3Y + 125.0f;

    ad_hovTab1 = false; ad_hovTab2 = false; ad_hovTab3 = false;
    hoverAdultFocusBtn = false; hoverControlDrop = false; hoverRelDrop = false; hoverLangDrop = false;
    hCbAdultWeb = false; hCbFbReels = false; hCbHardcore = false; hCbYtShorts = false; hCbRomantic = false;
    hoverCustomInput = false; hoverCustomAddBtn = false; 
    
    hTimeHM = false; hTimeHP = false; hTimeMM = false; hTimeMP = false; hTimeStart = false; hTimeCancel = false;
    hPassInput = false; hPassConfirm = false; hPassCancel = false;
    
    hCb24HourLock = false; hCbPeriodicPopups = false;
    hLinkAiFilter = false; hLinkStrict = false;

    for (auto& item : customAdultKeywords) item.isHoveredCross = false;

    if (showTimeOverlay || showPassOverlay) {
        float ovW = 400.0f; float ovH = 220.0f;
        float ovX = cx + (cw - ovW) / 2.0f; float ovY = cy + (ch - ovH) / 2.0f;

        if (showTimeOverlay) {
            if (RectF(ovX + 110.0f, ovY + 80.0f, 32.0f, 36.0f).Contains(x, y)) hTimeHM = true;
            if (RectF(ovX + 110.0f + 92.0f, ovY + 80.0f, 32.0f, 36.0f).Contains(x, y)) hTimeHP = true;
            if (RectF(ovX + 300.0f, ovY + 80.0f, 32.0f, 36.0f).Contains(x, y)) hTimeMM = true;
            if (RectF(ovX + 300.0f + 92.0f, ovY + 80.0f, 32.0f, 36.0f).Contains(x, y)) hTimeMP = true;
            if (RectF(ovX + 50.0f, ovY + 150.0f, 140.0f, 40.0f).Contains(x, y)) hTimeCancel = true;
            if (RectF(ovX + 210.0f, ovY + 150.0f, 140.0f, 40.0f).Contains(x, y)) hTimeStart = true;
        }
        else if (showPassOverlay) {
            if (RectF(ovX + 40.0f, ovY + 80.0f, ovW - 80.0f, 40.0f).Contains(x, y)) hPassInput = true;
            if (RectF(ovX + 40.0f, ovY + 150.0f, 140.0f, 40.0f).Contains(x, y)) hPassCancel = true;
            if (RectF(ovX + 200.0f, ovY + 150.0f, 160.0f, 40.0f).Contains(x, y)) hPassConfirm = true;
        }
        return; 
    }

    float tabW = 200.0f, tabH = 40.0f;
    float tab1X = cx + 20.0f, tab2X = tab1X + tabW + 10.0f, tab3X = tab2X + tabW + 10.0f, tabY = cy + 10.0f;
    if(x>=tab1X && x<=tab1X+tabW && y>=tabY && y<=tabY+tabH) ad_hovTab1 = true;
    if(x>=tab2X && x<=tab2X+tabW && y>=tabY && y<=tabY+tabH) ad_hovTab2 = true;
    if(x>=tab3X && x<=tab3X+tabW && y>=tabY && y<=tabY+tabH) ad_hovTab3 = true;

    if (ad_activeSubTab == 0) {
        if (isAdultFocusActive && controlMode == 0 && !cb24HourLock) { 
            hoverAdultFocusBtn = false; 
        } else if (cb24HourLock) {
            hoverAdultFocusBtn = false; 
        } else {
            hoverAdultFocusBtn = RectF(bX, row1Y, 150.0f, 35.0f).Contains(x,y);
        }
        
        if (isControlDropOpen) {
            hoverCtrlIdx = -1; RectF dR(bX + 225.0f, row1Y + 36.0f, 130.0f, 2 * 35.0f);
            for(int i=0; i<2; i++) if(RectF(dR.X, dR.Y + (i*35.0f), dR.Width, 35.0f).Contains(x,y)) hoverCtrlIdx = i; return;
        }
        if (isRelDropOpen) {
            hoverRelIdx = -1; RectF dR(bX + 445.0f, row1Y + 36.0f, 120.0f, 4 * 35.0f);
            for(int i=0; i<4; i++) if(RectF(dR.X, dR.Y + (i*35.0f), dR.Width, 35.0f).Contains(x,y)) hoverRelIdx = i; return;
        }
        if (isLangDropOpen) {
            hoverLangIdx = -1; RectF dL(bX + 635.0f, row1Y + 36.0f, 90.0f, 2 * 35.0f);
            for(int i=0; i<2; i++) if(RectF(dL.X, dL.Y + (i*35.0f), dL.Width, 35.0f).Contains(x,y)) hoverLangIdx = i; return;
        }

        hoverControlDrop = RectF(bX + 225.0f, row1Y, 130.0f, 35.0f).Contains(x,y);
        hoverRelDrop = RectF(bX + 445.0f, row1Y, 120.0f, 35.0f).Contains(x,y);
        hoverLangDrop = RectF(bX + 635.0f, row1Y, 90.0f, 35.0f).Contains(x,y);
        
        float currY = row2Y;
        hCbAdultWeb = RectF(bX, currY, 250.0f, 20.0f).Contains(x,y); currY += 35.0f;
        hCbHardcore = RectF(bX, currY, 250.0f, 20.0f).Contains(x,y); currY += 35.0f;
        hCbRomantic = RectF(bX, currY, 250.0f, 20.0f).Contains(x,y); currY += 35.0f;
        hCbFbReels = RectF(bX, currY, 250.0f, 20.0f).Contains(x,y); currY += 35.0f;
        hCbYtShorts = RectF(bX, currY, 250.0f, 20.0f).Contains(x,y);

        float rX = bX + 310.0f;
        float rY = row2Y; 
        hoverCustomInput = RectF(rX, rY + 30.0f, 250.0f, 32.0f).Contains(x,y);
        hoverCustomAddBtn = RectF(rX + 260.0f, rY + 30.0f, 70.0f, 32.0f).Contains(x,y);

        if (!isAdultFocusActive) {
            float itemY = rY + 70.0f + 5.0f;
            int maxDraw = min(3, (int)customAdultKeywords.size() - customScrollOffset);
            for (int i = 0; i < maxDraw; ++i) {
                int dataIdx = customScrollOffset + i;
                if (RectF(rX + 290.0f, itemY, 30.0f, 30.0f).Contains(x, y)) customAdultKeywords[dataIdx].isHoveredCross = true;
                itemY += 30.0f;
            }
        }

        float spX = bX + 360.0f;
        float spY = row3Y + 10.0f; 
        hCb24HourLock = RectF(spX, spY, 350.0f, 40.0f).Contains(x, y); spY += 45.0f;
        hCbPeriodicPopups = RectF(spX, spY, 350.0f, 40.0f).Contains(x, y);

        hLinkAiFilter = RectF(bX, linkY, 200.0f, 25.0f).Contains(x,y);
        hLinkStrict = RectF(bX + 250.0f, linkY, 200.0f, 25.0f).Contains(x,y);
    }
    else if (ad_activeSubTab == 1) {
        ProcessAiFilterMouseMove(x, y);
    }
    else if (ad_activeSubTab == 2) {
        ProcessStrictProtocolsMouseMove(x, y);
    }
}

void ProcessAdultMouseClick(float x, float y) {
    if (showTimeOverlay) {
        if (hTimeHM && focusHours > 0) focusHours--;
        if (hTimeHP && focusHours < 23) focusHours++;
        if (hTimeMM) { focusMins -= 5; if (focusMins < 0) focusMins = 55; }
        if (hTimeMP) { focusMins = (focusMins + 5) % 60; }
        if (hTimeCancel) showTimeOverlay = false;
        if (hTimeStart) { 
            isAdultFocusActive = true; 
            focusEndTime = GetTickCount64() + ((ULONGLONG)focusHours * 3600000) + ((ULONGLONG)focusMins * 60000);
            showTimeOverlay = false; 
            SaveAdultSettings(); 
        }
        return;
    }
    if (showPassOverlay) {
        isPassInputActive = hPassInput;
        if (hPassCancel) { showPassOverlay = false; inputPassText = L""; }
        if (hPassConfirm && !inputPassText.empty()) {
            isAdultFocusActive = !isStoppingFocus; 
            showPassOverlay = false; inputPassText = L""; 
            SaveAdultSettings(); 
        }
        return;
    }

    if(ad_hovTab1) { ad_activeSubTab = 0; return; }
    if(ad_hovTab2 || hLinkAiFilter) { ad_activeSubTab = 1; return; }
    if(ad_hovTab3 || hLinkStrict) { ad_activeSubTab = 2; return; }

    auto handleCb = [](bool& state, bool hover) {
        if (hover) {
            if (isAdultFocusActive) { if(!state) state = true; } 
            else { state = !state; }
        }
    };

    if (ad_activeSubTab == 0) {
        if (hoverAdultFocusBtn && !cb24HourLock) {
            if (isAdultFocusActive) { 
                if (controlMode == 1) { isStoppingFocus = true; showPassOverlay = true; isPassInputActive = true; }
                else { isAdultFocusActive = false; }
            } else { 
                if (controlMode == 0) { showTimeOverlay = true; }
                else { isStoppingFocus = false; showPassOverlay = true; isPassInputActive = true; }
            }
        }
        
        if (isControlDropOpen) { if(hoverCtrlIdx != -1) controlMode = hoverCtrlIdx; isControlDropOpen = false; SaveAdultSettings(); return; }
        if (isRelDropOpen) { if(hoverRelIdx != -1) adultReligion = hoverRelIdx; isRelDropOpen = false; SaveAdultSettings(); return; }
        if (isLangDropOpen) { if(hoverLangIdx != -1) adultLanguage = hoverLangIdx; isLangDropOpen = false; SaveAdultSettings(); return; }
        
        if (hoverControlDrop && !isAdultFocusActive) isControlDropOpen = true;
        if (hoverRelDrop && !isAdultFocusActive) isRelDropOpen = true;
        if (hoverLangDrop && !isAdultFocusActive) isLangDropOpen = true;

        handleCb(cbAdultWeb, hCbAdultWeb);
        handleCb(cbFbReels, hCbFbReels);
        handleCb(cbHardcore, hCbHardcore);
        handleCb(cbYtShorts, hCbYtShorts);
        handleCb(cbRomantic, hCbRomantic);

        handleCb(cbPeriodicPopups, hCbPeriodicPopups);
        
        if (hCb24HourLock) {
            if (!cb24HourLock) { 
                int r = MessageBox(NULL, "Are you sure? This will lock your browser safety for 24 hours and CANNOT be undone.", "24-Hour Lockdown", MB_YESNO | MB_ICONWARNING);
                if (r == IDYES) {
                    cb24HourLock = true;
                    isAdultFocusActive = true;
                    lock24hEndTime = GetTickCount64() + 86400000ULL; 
                }
            }
        }

        isCustomInputActive = hoverCustomInput;

        if (hoverCustomAddBtn && !customInputText.empty()) {
            customAdultKeywords.push_back({customInputText, false});
            customInputText = L"";
        }

        if (!isAdultFocusActive) {
            for (auto it = customAdultKeywords.begin(); it != customAdultKeywords.end();) {
                if (it->isHoveredCross) { it = customAdultKeywords.erase(it); } else ++it;
            }
        }
    }
    else if (ad_activeSubTab == 1) {
        ProcessAiFilterMouseClick(x, y);
    }
    else if (ad_activeSubTab == 2) {
        ProcessStrictProtocolsMouseClick(x, y);
    }
    
    SaveAdultSettings();
}

void ProcessAdultKeyPress(wchar_t c) {
    if (showPassOverlay && isPassInputActive) {
        if (c >= 32 && c <= 126 && inputPassText.length() < 20) inputPassText += c;
    }
    else if (ad_activeSubTab == 0 && isCustomInputActive && c >= 32 && c <= 126 && customInputText.length() < 40) {
        customInputText += c;
    }
}

void ProcessAdultKeyDown(WPARAM key) {
    if (showPassOverlay && isPassInputActive) {
        if (key == VK_BACK && !inputPassText.empty()) inputPassText.pop_back();
    }
    else if (ad_activeSubTab == 0 && isCustomInputActive) {
        if (key == VK_BACK && !customInputText.empty()) customInputText.pop_back();
        else if (key == VK_RETURN && !customInputText.empty()) {
            customAdultKeywords.push_back({customInputText, false});
            customInputText = L"";
            SaveAdultSettings(); 
        }
    }
}

void ProcessAdultMouseWheel(float x, float y, int delta) {
    int steps = (delta > 0) ? 1 : -1;
    if (ad_activeSubTab == 0 && !showTimeOverlay && !showPassOverlay) {
        float cx = s_contentX; float cy = s_contentY; float cw = s_contentW; float ch = s_contentH;
        float bX = cx + 40.0f;
        float row1Y = cy + 80.0f;
        float row2Y = row1Y + 65.0f;
        float rX = bX + 310.0f; 
        
        RectF cTableRect(rX, row2Y + 70.0f, 330.0f, 100.0f);
        
        if (cTableRect.Contains(x, y)) {
            if (steps > 0 && customScrollOffset > 0) customScrollOffset--;
            else if (steps < 0 && customScrollOffset < max(0, (int)customAdultKeywords.size() - 3)) customScrollOffset++;
        }
    } else if (ad_activeSubTab == 1) {
        ProcessAiFilterMouseWheel(x, y, delta);
    }
}