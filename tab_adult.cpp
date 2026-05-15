// tab_adult.cpp - Unified Adult Block + Strict Protocols (v2.0)
// Red-marked section removed. AI Filter removed. All in one GDI+ file.
// UPDATED: Professional 2-Column Layout to fix overlap and empty right space.

#include "tab_adult.h"

// --- Premium Feature Gate ---
extern bool g_isPremiumUser;
extern bool g_showUpgradePopup;
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
#include <shlwapi.h>

using namespace Gdiplus;
using namespace std;

// ==========================================
// --- WINDOW SIZE CACHE ---
// ==========================================
static float s_contentX = 0.0f;
static float s_contentY = 0.0f;
static float s_contentW = 800.0f;
static float s_contentH = 600.0f;

// ==========================================
// --- COLOR PALETTE ---
// ==========================================
static const Color AClrTeal(255, 12, 168, 176);
static const Color AClrTealHover(255, 30, 185, 195);
static const Color AClrTealLight(255, 220, 248, 250);
static const Color AClrDark(255, 35, 35, 45);
static const Color AClrGrayText(255, 130, 130, 140);
static const Color AClrBorder(255, 215, 222, 230);
static const Color AClrWhite(255, 255, 255, 255);
static const Color AClrBg(255, 246, 249, 252);
static const Color AClrBgHover(255, 232, 246, 248);
static const Color AClrRed(255, 220, 60, 50);
static const Color AClrGreen(255, 34, 160, 80);
static const Color AClrOrange(255, 240, 140, 30);
static const Color AClrOverlay(190, 10, 12, 20);
static const Color AClrCardBg(255, 252, 254, 255);

// ==========================================
// --- SAFE BROWSING STATE ---
// ==========================================
static bool isAdultFocusActive = false;
static bool hoverAdultFocusBtn = false;
static ULONGLONG focusEndTime = 0;

static int controlMode = 0;
static bool hoverControlDrop = false; static bool isControlDropOpen = false;
static int hoverCtrlIdx = -1;
wstring ctrlModes[] = { L"Self Control", L"Friend Control" };

static bool showTimeOverlay = false;
static int focusHours = 1; static int focusMins = 0;
static bool hTimeHM = false, hTimeHP = false, hTimeMM = false, hTimeMP = false;
static bool hTimeStart = false, hTimeCancel = false;

static bool showPassOverlay = false;
static wstring inputPassText = L"";
static bool isPassInputActive = true;
static bool hPassInput = false, hPassConfirm = false, hPassCancel = false;
static bool isStoppingFocus = false;

static int adultReligion = 0;
static bool hoverRelDrop = false; static bool isRelDropOpen = false;
static int hoverRelIdx = -1;
wstring religions[] = { L"Muslim", L"Hindu", L"Christian", L"Universal" };

static int adultLanguage = 0;
static bool hoverLangDrop = false; static bool isLangDropOpen = false;
static int hoverLangIdx = -1;
wstring languages[] = { L"Bangla", L"English" };

static bool cbAdultWeb = true;  static bool hCbAdultWeb = false;
static bool cbFbReels = true;   static bool hCbFbReels = false;
static bool cbHardcore = true;  static bool hCbHardcore = false;
static bool cbRomantic = true;  static bool hCbRomantic = false;

static bool cbPeriodicPopups = false; static bool hCbPeriodicPopups = false;
static DWORD lastPeriodicPopupTime = 0;

static bool cb24HourLock = false; static bool hCb24HourLock = false;
static ULONGLONG lock24hEndTime = 0;

struct AdultCustomItem { wstring name; bool isHoveredCross; };
static vector<AdultCustomItem> customAdultKeywords;
static wstring customInputText = L"";
static bool isCustomInputActive = false;
static bool hoverCustomInput = false;
static bool hoverCustomAddBtn = false;
static int customScrollOffset = 0;

static bool isPanicActive = false;
static DWORD panicStartTime = 0;
static int totalBlockedAdultCount = 0;

// ==========================================
// --- STRICT PROTOCOLS STATE ---
// ==========================================
static bool cbSilentUrl  = true;
static bool cbDnsFilter  = false;
static bool cbSafeSearch = true;
static bool cbIncognito  = true;
static bool cbStrictMode = false;

static bool hCbSilentUrl  = false;
static bool hCbDnsFilter  = false;
static bool hCbSafeSearch = false;
static bool hCbIncognito  = false;
static bool hCbStrictMode = false;

static bool isStrictFocusActive = false;
static ULONGLONG strictFocusEndTime = 0;
static bool hoverStrictFocusBtn = false;
static bool hoverStrictPanicBtn = false;

static bool showStrictTimeOverlay = false;
static int strictFocusHours = 1; static int strictFocusMins = 0;
static bool hStrictTimeHM = false, hStrictTimeHP = false;
static bool hStrictTimeMM = false, hStrictTimeMP = false;
static bool hStrictTimeStart = false, hStrictTimeCancel = false;

static bool strictSettingsLoaded = false;
static bool adultThreadStarted  = false;

// ==========================================
// --- QUOTES DATABASE ---
// ==========================================
struct Quote { wstring bn; wstring en; };

static vector<Quote> muslimQuotes = {
    {L"\u201c\u09ae\u09c1\u09ae\u09bf\u09a8\u09a6\u09c7\u09b0 \u09ac\u09b2\u09c1\u09a8, \u09a4\u09be\u09b0\u09be \u09af\u09c7\u09a8 \u09a4\u09be\u09a6\u09c7\u09b0 \u09a6\u09c3\u09b7\u09cd\u099f\u09bf \u09a8\u09a4 \u09b0\u09be\u0996\u09c7\u0964\u201d - (\u09b8\u09c2\u09b0\u09be \u0986\u09a8-\u09a8\u09c2\u09b0: \u09e9\u09e6)", L"Tell the believing men to reduce their vision and guard their private parts. (Surah An-Nur: 30)"},
    {L"\u201c\u09b2\u099c\u09cd\u099c\u09be\u09b6\u09c0\u09b2\u09a4\u09be \u0987\u09ae\u09be\u09a8\u09c7\u09b0 \u0985\u0999\u09cd\u0997\u0964\u201d - (\u09b8\u09b9\u09bf\u09b9 \u09ae\u09c1\u09b8\u09b2\u09bf\u09ae)", L"Modesty is a branch of faith. (Sahih Muslim)"},
};
static vector<Quote> hinduQuotes = {
    {L"\u201c\u09af\u09c7 \u09ae\u09a8\u0995\u09c7 \u09a8\u09bf\u09af\u09bc\u09a8\u09cd\u09a4\u09cd\u09b0\u09a3 \u0995\u09b0\u09a4\u09c7 \u09aa\u09be\u09b0\u09c7 \u09a8\u09be, \u09a4\u09be\u09b0 \u09ae\u09a8 \u09a4\u09be\u09b0 \u09b8\u09ac\u099a\u09c7\u09df\u09c7 \u09ac\u09dc \u09b6\u09a4\u09cd\u09b0\u09c1\u0964\u201d - (\u09ad\u0997\u09ac\u09a6\u09cd\u0997\u09c0\u09a4\u09be)", L"For him who has not conquered the mind, the mind is the greatest enemy. (Bhagavad Gita)"},
};
static vector<Quote> christianQuotes = {
    {L"\u201c\u09af\u09c7 \u0995\u09c7\u0989 \u0995\u09cb\u09a8\u09cb \u09b8\u09cd\u09a4\u09cd\u09b0\u09c0\u09b0 \u09a6\u09bf\u0995\u09c7 \u0995\u09be\u09ae\u09a8\u09be\u09b0 \u09a6\u09c3\u09b7\u09cd\u099f\u09bf\u09a4\u09c7 \u09a4\u09be\u0995\u09be\u09df\u2026\u201d - (\u09ae\u09a5\u09bf \u09eb:\u09e8\u09ee)", L"Anyone who looks at a woman lustfully has already committed adultery in his heart. (Matthew 5:28)"},
};
static vector<Quote> universalQuotes = {
    {L"\u201c\u09b8\u09ab\u09b2\u09a4\u09be \u0986\u09b8\u09c7 \u09ab\u09cb\u0995\u09be\u09b8 \u09a5\u09c7\u0995\u09c7, \u09a1\u09bf\u09b8\u09cd\u099f\u09cd\u09b0\u09be\u0995\u09b6\u09a8 \u09a5\u09c5\u0995\u09c7 \u09a8\u09df\u0964\u201d", L"Success comes from focus, not from distraction."},
    {L"\u201c\u09af\u09c7 \u09a8\u09bf\u099c\u09c7\u09b0 \u09ae\u09a8\u0995\u09c7 \u09a8\u09bf\u09af\u09bc\u09a8\u09cd\u09a4\u09cd\u09b0\u09a3 \u0995\u09b0\u09a4\u09c7 \u09aa\u09be\u09b0\u09c7, \u09b8\u09c7 \u09aa\u09c3\u09a5\u09bf\u09ac\u09c0 \u099c\u09df \u0995\u09b0\u09a4\u09c7 \u09aa\u09be\u09b0\u09c7\u0964\u201d", L"He who can control his mind can conquer the world."},
};

// ==========================================
// --- KEYWORD DATABASES ---
// ==========================================
static vector<wstring> hardcoreKeywords = {
    L"porn", L"xxx", L"sex", L"nude", L"nsfw", L"hentai", L"milf", L"blowjob",
    L"xvideos", L"pornhub", L"xnxx", L"xhamster", L"brazzers", L"onlyfans",
    L"chaturbate", L"spankbang", L"redtube", L"youporn",
    L"\u099a\u099f\u09bf", L"\u09aa\u09b0\u09cd\u09a3", L"\u09b8\u09c7\u0995\u09cd\u09b8", L"\u09a8\u0997\u09cd\u09a8",
    L"bhabi", L"chudai", L"bangla choti", L"panu", L"magi", L"choda", L"randi",
};
static vector<wstring> romanticKeywords = {
    L"hot dance", L"seductive", L"item song", L"belly dance",
    L"kissing scene", L"bikini", L"sexy dance", L"cleavage",
    L"semi nude", L"lingerie", L"erotic", L"navel show",
};
static vector<wstring> adultWebsites;

// ==========================================
// --- FILE PATH HELPERS ---
// ==========================================
static wstring GetSaveFilePath() {
    wstring path = L"C:\\ProgramData\\RasFocus";
    CreateDirectoryW(path.c_str(), NULL);
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return path + L"\\rf_sys_data.dat";
}
static wstring GetStrictSaveFilePath() {
    wstring path = L"C:\\ProgramData\\RasFocus";
    CreateDirectoryW(path.c_str(), NULL);
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    return path + L"\\rf_strict_data.dat";
}

// ==========================================
// --- SAVE / LOAD ---
// ==========================================
static void SaveAdultSettings() {
    wstring fp = GetSaveFilePath();
    std::wofstream out(fp.c_str());
    out.imbue(std::locale(out.getloc(), new std::codecvt_utf8<wchar_t>));
    if (out.is_open()) {
        out << cbAdultWeb << L" " << cbFbReels << L" " << cbHardcore << L" " << cbRomantic << L"\n";
        out << controlMode << L" " << adultReligion << L" " << adultLanguage << L" " << totalBlockedAdultCount << L"\n";
        out << cbPeriodicPopups << L" " << cb24HourLock << L" " << lock24hEndTime << L"\n";
        out << isAdultFocusActive << L" " << focusEndTime << L"\n";
        out << customAdultKeywords.size() << L"\n";
        for (auto& k : customAdultKeywords) out << k.name << L"\n";
        out.close();
    }
}
static void SaveStrictSettings() {
    wstring fp = GetStrictSaveFilePath();
    std::wofstream out(fp.c_str());
    out.imbue(std::locale(out.getloc(), new std::codecvt_utf8<wchar_t>));
    if (out.is_open()) {
        out << cbSilentUrl << L" " << cbDnsFilter << L" " << cbSafeSearch << L" " << cbIncognito << L" " << cbStrictMode << L"\n";
        out << isStrictFocusActive << L" " << strictFocusEndTime << L"\n";
        out.close();
    }
}

static vector<wstring> LoadAdultSitesFromResource() {
    vector<wstring> sites;
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(105), RT_RCDATA);
    if (!hRes) return sites;
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return sites;
    DWORD size = SizeofResource(NULL, hRes);
    const char* data = (const char*)LockResource(hData);
    if (data && size > 0) {
        string fc(data, size); stringstream ss(fc); string line;
        while (getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) sites.push_back(wstring(line.begin(), line.end()));
        }
    }
    return sites;
}

static void LoadAdultSettings() {
    if (adultWebsites.empty()) {
        adultWebsites = LoadAdultSitesFromResource();
        if (adultWebsites.empty())
            adultWebsites = { L"pornhub.com", L"xvideos.com", L"xnxx.com", L"xhamster.com", L"redtube.com" };
    }
    wstring fp = GetSaveFilePath();
    std::wifstream in(fp.c_str());
    in.imbue(std::locale(in.getloc(), new std::codecvt_utf8<wchar_t>));
    if (in.is_open()) {
        in >> cbAdultWeb >> cbFbReels >> cbHardcore >> cbRomantic;
        in >> controlMode >> adultReligion >> adultLanguage >> totalBlockedAdultCount;
        in >> cbPeriodicPopups >> cb24HourLock >> lock24hEndTime;
        in >> isAdultFocusActive >> focusEndTime;
        if (cb24HourLock && GetTickCount64() >= lock24hEndTime) { cb24HourLock = false; isAdultFocusActive = false; }
        else if (cb24HourLock) isAdultFocusActive = true;
        if (isAdultFocusActive && controlMode == 0 && GetTickCount64() >= focusEndTime && !cb24HourLock) isAdultFocusActive = false;
        size_t kSize = 0; in >> kSize; in.ignore();
        customAdultKeywords.clear();
        for (size_t i = 0; i < kSize; i++) {
            std::wstring line; std::getline(in, line);
            if (!line.empty()) customAdultKeywords.push_back({ line, false });
        }
        in.close();
    }
}

void LoadStrictSettings() {
    wstring fp = GetStrictSaveFilePath();
    std::wifstream in(fp.c_str());
    in.imbue(std::locale(in.getloc(), new std::codecvt_utf8<wchar_t>));
    if (in.is_open()) {
        in >> cbSilentUrl >> cbDnsFilter >> cbSafeSearch >> cbIncognito >> cbStrictMode;
        in >> isStrictFocusActive >> strictFocusEndTime;
        if (isStrictFocusActive && GetTickCount64() >= strictFocusEndTime) isStrictFocusActive = false;
        in.close();
    }
}

// ==========================================
// --- HELPER: ROUNDED RECT PATH ---
// ==========================================
static GraphicsPath* MakeRoundRect(RectF r, int rad) {
    GraphicsPath* p = new GraphicsPath();
    float d = rad * 2.0f;
    p->AddArc(r.X, r.Y, d, d, 180, 90);
    p->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p->CloseFigure();
    return p;
}

static wstring toLowerW_Logic(wstring s) { for (auto& c : s) c = towlower(c); return s; }

// ==========================================
// --- POPUP LOGIC ---
// ==========================================
struct PopupData { wstring quote; bool isFullScreen; };

static LRESULT CALLBACK AdultPopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT rc; GetClientRect(hwnd, &rc);
        PopupData* pd = (PopupData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (pd && pd->isFullScreen)
            { SolidBrush b(Color(255,20,24,36)); g.FillRectangle(&b, 0.0f, 0.0f, (float)rc.right, (float)rc.bottom); }
        else
            { SolidBrush b(Color(255,15,70,38)); g.FillRectangle(&b, 0.0f,0.0f,(float)rc.right,(float)rc.bottom);
              Pen p(AClrRed,3.5f); g.DrawRectangle(&p,2.0f,2.0f,(float)rc.right-4,(float)rc.bottom-4); }
        FontFamily ff(L"Segoe UI");
        Font fQ(&ff, pd&&pd->isFullScreen?44:34, FontStyleBold, UnitPixel);
        Font fS(&ff, 18, FontStyleRegular, UnitPixel);
        SolidBrush tw(Color(255,255,255,255)); SolidBrush ts(Color(200,255,255,255));
        StringFormat fc; fc.SetAlignment(StringAlignmentCenter); fc.SetLineAlignment(StringAlignmentCenter);
        if (pd) {
            g.DrawString(pd->quote.c_str(),-1,&fQ,RectF(40,40,(float)rc.right-80,(float)rc.bottom-80),&fc,&tw);
            if (pd->isFullScreen) g.DrawString(L"Press ESC to close.",-1,&fS,RectF(0,(float)rc.bottom-45,(float)rc.right,30),&fc,&ts);
        }
        EndPaint(hwnd,&ps); return 0;
    }
    if (msg==WM_TIMER&&wParam==2) { KillTimer(hwnd,2); DestroyWindow(hwnd); PostQuitMessage(0); return 0; }
    if (msg==WM_KEYDOWN&&wParam==VK_ESCAPE) {
        PopupData* pd=(PopupData*)GetWindowLongPtr(hwnd,GWLP_USERDATA);
        if(pd&&pd->isFullScreen){DestroyWindow(hwnd);PostQuitMessage(0);return 0;}
    }
    if (msg==WM_DESTROY){PopupData* pd=(PopupData*)GetWindowLongPtr(hwnd,GWLP_USERDATA);if(pd)delete pd;}
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

static void SafePopupThread(wstring quote, bool fullScreen=false) {
    WNDCLASS wc={0}; wc.lpfnWndProc=AdultPopupWndProc; wc.hInstance=GetModuleHandle(NULL);
    wc.lpszClassName="RasFocusAdultPopupClass"; wc.hbrBackground=(HBRUSH)GetStockObject(NULL_BRUSH);
    RegisterClass(&wc);
    int sw=GetSystemMetrics(SM_CXSCREEN),sh=GetSystemMetrics(SM_CYSCREEN);
    int w=fullScreen?sw:880, h=fullScreen?sh:240;
    int x=fullScreen?0:(sw-w)/2, y=fullScreen?0:48;
    HWND hPopup=CreateWindowEx(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,
        "RasFocusAdultPopupClass","Alert",WS_POPUP,x,y,w,h,NULL,NULL,GetModuleHandle(NULL),NULL);
    if(hPopup){
        PopupData* d=new PopupData{quote,fullScreen};
        SetWindowLongPtr(hPopup,GWLP_USERDATA,(LONG_PTR)d);
        SetLayeredWindowAttributes(hPopup,0,fullScreen?248:235,LWA_ALPHA);
        ShowWindow(hPopup,SW_SHOW); SetForegroundWindow(hPopup);
        if(!fullScreen) SetTimer(hPopup,2,6000,NULL);
        MSG msg; while(GetMessage(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);}
    }
}

static void TriggerAdultPopup(bool isWarning=false, wstring customMsg=L"", bool isFullScreen=false) {
    if(!isFullScreen){ totalBlockedAdultCount++; SaveAdultSettings(); }
    wstring finalQuote=L"";
    if(isWarning){ finalQuote=customMsg; }
    else {
        int idx=0;
        if(adultReligion==0){idx=rand()%muslimQuotes.size();finalQuote=(adultLanguage==0)?muslimQuotes[idx].bn:muslimQuotes[idx].en;}
        else if(adultReligion==1){idx=rand()%hinduQuotes.size();finalQuote=(adultLanguage==0)?hinduQuotes[idx].bn:hinduQuotes[idx].en;}
        else if(adultReligion==2){idx=rand()%christianQuotes.size();finalQuote=(adultLanguage==0)?christianQuotes[idx].bn:christianQuotes[idx].en;}
        else{idx=rand()%universalQuotes.size();finalQuote=(adultLanguage==0)?universalQuotes[idx].bn:universalQuotes[idx].en;}
    }
    thread t(SafePopupThread,finalQuote,isFullScreen); t.detach();
}

static void closeActiveTab() {
    keybd_event(VK_CONTROL,0,0,0); keybd_event('W',0,0,0);
    keybd_event('W',0,KEYEVENTF_KEYUP,0); keybd_event(VK_CONTROL,0,KEYEVENTF_KEYUP,0);
}

// ==========================================
// --- KEYBOARD HOOK ---
// ==========================================
static HHOOK hKeyboardHook = NULL;
static string globalKeyBuffer = "";

static LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if(nCode>=0&&wParam==WM_KEYDOWN&&!isPanicActive){
        KBDLLHOOKSTRUCT* ks=(KBDLLHOOKSTRUCT*)lParam; DWORD vk=ks->vkCode;
        if((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9')||vk==VK_SPACE||vk==VK_OEM_PERIOD){
            char c=(vk==VK_OEM_PERIOD)?'.':tolower(MapVirtualKey(vk,MAPVK_VK_TO_CHAR));
            globalKeyBuffer+=c;
            if(globalKeyBuffer.length()>100) globalKeyBuffer.erase(0,1);
            wstring wb(globalKeyBuffer.begin(),globalKeyBuffer.end()); bool block=false;
            if(cbHardcore&&!block) for(const auto& k:hardcoreKeywords) if(wb.find(toLowerW_Logic(k))!=wstring::npos){block=true;break;}
            if(cbRomantic&&!block) for(const auto& k:romanticKeywords) if(wb.find(toLowerW_Logic(k))!=wstring::npos){block=true;break;}
            if(cbAdultWeb&&!block) for(const auto& w:adultWebsites){size_t dp=w.find(L".");wstring cn=(dp!=wstring::npos)?w.substr(0,dp):w;if(cn.length()>2&&wb.find(cn)!=wstring::npos){block=true;break;}}
            if(!customAdultKeywords.empty()&&!block) for(const auto& it:customAdultKeywords) if(!it.name.empty()&&wb.find(toLowerW_Logic(it.name))!=wstring::npos){block=true;break;}
            if(block){globalKeyBuffer="";closeActiveTab();TriggerAdultPopup();}
        }
        else if(vk==VK_BACK&&!globalKeyBuffer.empty()) globalKeyBuffer.pop_back();
    }
    return CallNextHookEx(hKeyboardHook,nCode,wParam,lParam);
}

static void StartKeyloggerThread() {
    hKeyboardHook=SetWindowsHookEx(WH_KEYBOARD_LL,KeyboardHookProc,NULL,0);
    MSG msg; while(GetMessage(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);}
}

// ==========================================
// --- UIA BROWSER URL DETECTION ---
// ==========================================
static IUIAutomation* pAutomation = NULL;

static wstring GetBrowserURL_Fallback(HWND hBrowser) {
    wstring url=L"";
    if(!pAutomation) return url;
    IUIAutomationElement* pEl=NULL;
    if(SUCCEEDED(pAutomation->ElementFromHandle(hBrowser,&pEl))&&pEl){
        IUIAutomationCondition* pCond=NULL; IUIAutomationElement* pEdit=NULL;
        VARIANT v; v.vt=VT_I4; v.lVal=UIA_EditControlTypeId;
        pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId,v,&pCond);
        if(pCond){pEl->FindFirst(TreeScope_Descendants,pCond,&pEdit);pCond->Release();}
        if(!pEdit){
            VARIANT vn; vn.vt=VT_BSTR; vn.bstrVal=SysAllocString(L"Address and search bar");
            pAutomation->CreatePropertyCondition(UIA_NamePropertyId,vn,&pCond);
            if(pCond){pEl->FindFirst(TreeScope_Descendants,pCond,&pEdit);pCond->Release();}
            SysFreeString(vn.bstrVal);
        }
        if(pEdit){
            VARIANT vv; VariantInit(&vv);
            if(SUCCEEDED(pEdit->GetCurrentPropertyValue(UIA_ValueValuePropertyId,&vv))&&vv.vt==VT_BSTR&&vv.bstrVal)
                url=wstring(vv.bstrVal);
            VariantClear(&vv); pEdit->Release();
        }
        pEl->Release();
    }
    return url;
}

// ==========================================
// --- STRICT: DNS / HOSTS ---
// ==========================================
static void SetFamilyDNS(bool enable) {
    SHELLEXECUTEINFOW sei={sizeof(sei)};
    sei.lpVerb=L"runas"; sei.lpFile=L"cmd.exe"; sei.nShow=SW_HIDE;
    wstring args=enable?L"/c wmic nicconfig where (IPEnabled=TRUE) call SetDNSServerSearchOrder (\"1.1.1.3\", \"1.0.0.3\")"
                       :L"/c wmic nicconfig where (IPEnabled=TRUE) call SetDNSServerSearchOrder ()";
    sei.lpParameters=args.c_str(); ShellExecuteExW(&sei);
    WinExec("ipconfig /flushdns",SW_HIDE);
}

static void EnforceStrictProtocols() {
    string hp="C:\\Windows\\System32\\drivers\\etc\\hosts";
    string tp="C:\\Windows\\System32\\drivers\\etc\\hosts.temp";
    ifstream fi(hp); ofstream fo(tp); string line; bool skip=false;
    if(fi.is_open()&&fo.is_open()){
        while(getline(fi,line)){
            if(line.find("# RasFocus Strict Start")!=string::npos) skip=true;
            if(!skip) fo<<line<<"\n";
            if(line.find("# RasFocus Strict End")!=string::npos) skip=false;
        }
        fi.close();
    }
    if(cbDnsFilter||cbSafeSearch){
        fo<<"\n# RasFocus Strict Start\n";
        if(cbSafeSearch){
            fo<<"216.239.38.120 google.com\n216.239.38.120 www.google.com\n";
            fo<<"204.79.197.220 bing.com\n204.79.197.220 www.bing.com\n";
            fo<<"211.73.64.227 youtube.com\n211.73.64.227 www.youtube.com\n";
        }
        if(cbDnsFilter){
            vector<string> s={"pornhub.com","xvideos.com","xnxx.com","xhamster.com","redtube.com"};
            for(const auto& st:s) fo<<"127.0.0.1 "<<st<<"\n127.0.0.1 www."<<st<<"\n";
        }
        fo<<"# RasFocus Strict End\n";
    }
    fo.close();
    remove(hp.c_str()); rename(tp.c_str(),hp.c_str());
    WinExec("ipconfig /flushdns",SW_HIDE);
}

// ==========================================
// --- BACKGROUND THREAD (Adult + Strict combined) ---
// ==========================================
static void AdultBackgroundThread() {
    CoInitializeEx(NULL,COINIT_MULTITHREADED);
    CoCreateInstance(CLSID_CUIAutomation,NULL,CLSCTX_INPROC_SERVER,IID_IUIAutomation,(void**)&pAutomation);
    lastPeriodicPopupTime=GetTickCount();
    wstring lastTitle=L"";

    while(true){
        // 24h lock
        if(cb24HourLock){
            if(GetTickCount64()>=lock24hEndTime){cb24HourLock=false;isAdultFocusActive=false;SaveAdultSettings();}
            else isAdultFocusActive=true;
        }
        if(isAdultFocusActive&&controlMode==0&&GetTickCount64()>=focusEndTime&&!cb24HourLock){
            isAdultFocusActive=false; SaveAdultSettings();
        }
        // periodic popup
        if(cbPeriodicPopups&&isAdultFocusActive){
            if(GetTickCount()-lastPeriodicPopupTime>=25*60*1000){
                TriggerAdultPopup(false,L"",true); lastPeriodicPopupTime=GetTickCount();
            }
        }
        // strict focus timer
        if(isStrictFocusActive&&GetTickCount64()>=strictFocusEndTime){
            isStrictFocusActive=false; SaveStrictSettings();
        }
        // panic mode
        if(isPanicActive){
            if(GetTickCount()-panicStartTime<15*60*1000){
                HWND ha=GetForegroundWindow();
                if(ha){wchar_t t[256];GetWindowTextW(ha,t,256);wstring tl=toLowerW_Logic(t);
                    if(tl.find(L"chrome")!=wstring::npos||tl.find(L"edge")!=wstring::npos||
                       tl.find(L"firefox")!=wstring::npos||tl.find(L"brave")!=wstring::npos)
                        PostMessage(ha,WM_CLOSE,0,0);}
            } else isPanicActive=false;
        }
        // incognito block
        if(cbIncognito){
            HWND ha=GetForegroundWindow();
            if(ha){wchar_t t[256];GetWindowTextW(ha,t,256);wstring tl=toLowerW_Logic(t);
                if(tl.find(L"incognito")!=wstring::npos||tl.find(L"inprivate")!=wstring::npos){
                    keybd_event(VK_CONTROL,0,0,0);keybd_event('W',0,0,0);
                    keybd_event('W',0,KEYEVENTF_KEYUP,0);keybd_event(VK_CONTROL,0,KEYEVENTF_KEYUP,0);
                }
            }
        }
        // strict/focus: block task manager etc.
        if(cbStrictMode||isStrictFocusActive||isAdultFocusActive){
            HWND ha=GetForegroundWindow();
            if(ha){wchar_t t[256];GetWindowTextW(ha,t,256);wstring tl=toLowerW_Logic(t);
                if(tl.find(L"task manager")!=wstring::npos||tl.find(L"regedit")!=wstring::npos||
                   tl.find(L"uninstall")!=wstring::npos||tl.find(L"control panel")!=wstring::npos)
                    PostMessage(ha,WM_CLOSE,0,0);
            }
        }
        // adult browsing detection
        if(!isPanicActive&&(cbAdultWeb||cbHardcore||cbRomantic||cbFbReels||isAdultFocusActive)){
            HWND ha=GetForegroundWindow();
            if(ha){
                wchar_t t[256]; GetWindowTextW(ha,t,256); wstring title(t);
                if(!title.empty()&&title!=lastTitle){
                    lastTitle=title; wstring lt=toLowerW_Logic(title); bool blk=false;
                    if(cbHardcore&&!blk) for(const auto& k:hardcoreKeywords) if(lt.find(toLowerW_Logic(k))!=wstring::npos){blk=true;break;}
                    if(cbRomantic&&!blk) for(const auto& k:romanticKeywords) if(lt.find(toLowerW_Logic(k))!=wstring::npos){blk=true;break;}
                    if(!customAdultKeywords.empty()&&!blk) for(const auto& it:customAdultKeywords) if(!it.name.empty()&&lt.find(toLowerW_Logic(it.name))!=wstring::npos){blk=true;break;}
                    if(blk){closeActiveTab();TriggerAdultPopup();}
                    else if(lt.find(L"chrome")!=wstring::npos||lt.find(L"edge")!=wstring::npos||lt.find(L"brave")!=wstring::npos){
                        wstring url=GetBrowserURL_Fallback(ha); bool ub=false;
                        if(cbAdultWeb) for(const auto& s:adultWebsites) if(url.find(s)!=wstring::npos||lt.find(s)!=wstring::npos){ub=true;break;}
                        if(!ub&&cbFbReels){wstring lu=toLowerW_Logic(url);
                            if(lu.find(L"facebook.com/reel")!=wstring::npos||lu.find(L"instagram.com/reels")!=wstring::npos||lu.find(L"youtube.com/shorts")!=wstring::npos) ub=true;}
                        if(cbSilentUrl&&!ub){/* silent url monitoring placeholder */}
                        if(ub){closeActiveTab();Sleep(280);TriggerAdultPopup();lastTitle=L"";}
                    }
                }
            }
        }
        Sleep(500);
    }
}

// ==========================================
// --- AUTO START ---
// ==========================================
static void InitAdultSystemOnBoot() {
    if(!adultThreadStarted){
        LoadAdultSettings(); LoadStrictSettings();
        thread t(AdultBackgroundThread); t.detach();
        thread kl(StartKeyloggerThread); kl.detach();
        adultThreadStarted=true;
    }
}

struct AdultAutoStarter {
    AdultAutoStarter(){
        thread t([](){ Sleep(1000); InitAdultSystemOnBoot(); }); t.detach();
    }
} g_adultAutoStarter;

// ==========================================
// --- DRAW HELPERS ---
// ==========================================
static void DrawSpinner(Graphics& g, float x, float y, const wstring& val,
                         bool hM, bool hP, Font* fIcon, Font* fVal) {
    StringFormat fc; fc.SetAlignment(StringAlignmentCenter); fc.SetLineAlignment(StringAlignmentCenter);
    Pen pBorder(AClrBorder,1.5f);
    SolidBrush bHov(AClrBgHover), bNorm(AClrBorder), bW(AClrWhite), bD(AClrDark);

    RectF rm(x,       y, 32.0f, 36.0f);
    RectF rt(x+32.0f, y, 60.0f, 36.0f);
    RectF rp(x+92.0f, y, 32.0f, 36.0f);

    g.FillRectangle(hM?&bHov:&bNorm, rm); g.DrawRectangle(&pBorder,rm.X,rm.Y,rm.Width,rm.Height);
    g.DrawString(L"\xE738",-1,fIcon,rm,&fc,&bD);
    g.FillRectangle(&bW, rt); g.DrawRectangle(&pBorder,rt.X,rt.Y,rt.Width,rt.Height);
    g.DrawString(val.c_str(),-1,fVal,rt,&fc,&bD);
    g.FillRectangle(hP?&bHov:&bNorm, rp); g.DrawRectangle(&pBorder,rp.X,rp.Y,rp.Width,rp.Height);
    g.DrawString(L"\xE710",-1,fIcon,rp,&fc,&bD);
}

// ==========================================
// ==========================================
// --- MAIN DRAW FUNCTION (2-COLUMN PAGE) ---
// ==========================================
// ==========================================
void DrawAdultBlockTab(Graphics& g, float cx, float cy, float cw, float ch) {
    s_contentX=cx; s_contentY=cy; s_contentW=cw; s_contentH=ch;
    InitAdultSystemOnBoot();

    // --- Fonts (Scaled Down Slightly) ---
    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff,16,FontStyleBold,UnitPixel);
    Font fNorm(&ff,12,FontStyleRegular,UnitPixel);
    Font fBold(&ff,13,FontStyleBold,UnitPixel);
    Font fSmall(&ff,11,FontStyleRegular,UnitPixel);
    Font fTiny(&ff,10,FontStyleRegular,UnitPixel);
    FontFamily ffi(L"Segoe MDL2 Assets");
    Font fIcon(&ffi,15,FontStyleRegular,UnitPixel);
    Font fSmIcon(&ffi,12,FontStyleRegular,UnitPixel);
    Font fLgIcon(&ffi,24,FontStyleRegular,UnitPixel);

    // --- Brushes & Pens ---
    SolidBrush bW(AClrWhite), bDk(AClrDark), bGr(AClrGrayText);
    SolidBrush bTeal(AClrTeal), bBg(AClrBg);
    Pen pBorder(AClrBorder,1.5f);
    SolidBrush bRed(AClrRed), bGreen(AClrGreen), bOrange(AClrOrange);
    SolidBrush bSecLbl(AClrTeal);
    StringFormat fL,fC,fR;
    fL.SetAlignment(StringAlignmentNear);   fL.SetLineAlignment(StringAlignmentCenter);
    fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    // Main background
    g.FillRectangle(&bBg,cx,cy,cw,ch);

    // ─────────────────────────────────────────
    // LAYOUT CONSTANTS (2-COLUMN)
    // ─────────────────────────────────────────
    float L_X = cx + 20.0f;
    float R_X = cx + 410.0f;

    // --- ROW 1: General Controls ---
    RectF rFocusA(L_X, cy + 15, 130, 30);
    RectF rMode(L_X + 140, cy + 15, 110, 30);
    RectF rRel(L_X + 260, cy + 15, 100, 30);
    RectF rLang(L_X + 370, cy + 15, 80, 30);
    RectF rFocusS(L_X + 470, cy + 15, 120, 30);
    RectF rPanic(L_X + 600, cy + 15, 110, 30);

    auto drawDrop=[&](RectF r, wstring txt, bool hov){
        GraphicsPath* p=MakeRoundRect(r,4);
        SolidBrush db(hov&&!isAdultFocusActive?AClrBgHover:AClrWhite);
        g.FillPath(&db,p); g.DrawPath(&pBorder,p); delete p;
        g.DrawString(txt.c_str(),-1,&fNorm,RectF(r.X+8,r.Y,r.Width-28,r.Height),&fL,&bDk);
        g.DrawLine(&pBorder,r.X+r.Width-22,r.Y,r.X+r.Width-22,r.Y+r.Height);
        g.DrawString(L"\xE70D",-1,&fSmIcon,RectF(r.X+r.Width-22,r.Y,22,r.Height),&fC,&bGr);
    };

    // Safe Focus
    {
        bool locked=(cb24HourLock||( isAdultFocusActive&&controlMode==0&&!cb24HourLock));
        wstring btnTxt=L"Safe Focus";
        Color btnClr=AClrGreen;
        if(isAdultFocusActive){
            btnClr=AClrRed;
            if(cb24HourLock){
                ULONGLONG left=lock24hEndTime>GetTickCount64()?lock24hEndTime-GetTickCount64():0;
                btnTxt=L"Lock ("+to_wstring(left/3600000)+L"h "+to_wstring((left%3600000)/60000)+L"m)";
            } else if(controlMode==0){
                ULONGLONG left=focusEndTime>GetTickCount64()?focusEndTime-GetTickCount64():0;
                btnTxt=L"Lock ("+to_wstring((left/60000)+1)+L"m)";
            } else btnTxt=L"Stop Focus";
        }
        GraphicsPath* fp=MakeRoundRect(rFocusA,4);
        SolidBrush fb2(btnClr); g.FillPath(&fb2,fp); delete fp;
        if(isAdultFocusActive) g.DrawString(L"\xE72E",-1,&fSmIcon,RectF(rFocusA.X+6,rFocusA.Y,20,rFocusA.Height),&fL,&bW);
        g.DrawString(btnTxt.c_str(),-1,&fBold,RectF(rFocusA.X+(isAdultFocusActive?26:0),rFocusA.Y,rFocusA.Width-(isAdultFocusActive?26:0),rFocusA.Height),&fC,&bW);
    }

    drawDrop(rMode, ctrlModes[controlMode], hoverControlDrop);
    drawDrop(rRel, religions[adultReligion], hoverRelDrop);
    drawDrop(rLang, languages[adultLanguage], hoverLangDrop);

    // Strict Focus
    {
        wstring btnTxt=isStrictFocusActive?L"Stop Strict":L"Strict Focus";
        Color btnClr=isStrictFocusActive?AClrRed:AClrGreen;
        GraphicsPath* fp=MakeRoundRect(rFocusS,4);
        SolidBrush fb2(btnClr); g.FillPath(&fb2,fp); delete fp;
        if(isStrictFocusActive) g.DrawString(L"\xE72E",-1,&fSmIcon,RectF(rFocusS.X+6,rFocusS.Y,20,rFocusS.Height),&fL,&bW);
        if(isStrictFocusActive){
            ULONGLONG left=strictFocusEndTime>GetTickCount64()?strictFocusEndTime-GetTickCount64():0;
            wstring tl=L"Lock ("+to_wstring((left/60000)+1)+L"m)";
            g.DrawString(tl.c_str(),-1,&fBold,RectF(rFocusS.X+26,rFocusS.Y,rFocusS.Width-26,rFocusS.Height),&fC,&bW);
        } else g.DrawString(btnTxt.c_str(),-1,&fBold,rFocusS,&fC,&bW);
    }

    // Panic Button
    {
        GraphicsPath* pp=MakeRoundRect(rPanic,4);
        SolidBrush pb2(isPanicActive?AClrOrange:AClrRed);
        g.FillPath(&pb2,pp); delete pp;
        g.DrawString(L"\xE7BA",-1,&fSmIcon,RectF(rPanic.X+8,rPanic.Y,20,rPanic.Height),&fL,&bW);
        g.DrawString(isPanicActive?L"Panic Active":L"Panic Mode",-1,&fBold,
            RectF(rPanic.X+28,rPanic.Y,rPanic.Width-28,rPanic.Height),&fL,&bW);
    }

    // Divider
    g.DrawLine(&pBorder, L_X, cy + 55.0f, R_X + 360.0f, cy + 55.0f);

    // ─────────────────────────────────────────
    // LEFT COLUMN: Safe Browsing & Custom Keywords
    // ─────────────────────────────────────────
    g.DrawString(L"Safe Browsing Rules",-1,&fBold,RectF(L_X, cy+65, 360, 20),&fL,&bSecLbl);

    float cbStartY = cy + 95;
    RectF rCbAdult(L_X, cbStartY, 360, 22);
    RectF rCbHardcore(L_X, cbStartY + 30, 360, 22);
    RectF rCbRomantic(L_X, cbStartY + 60, 360, 22);
    RectF rCbFbReels(L_X, cbStartY + 90, 360, 22);

    auto drawCb=[&](RectF r, const wchar_t* txt, bool state, bool hover){
        RectF cbR(r.X, r.Y + 2, 16.0f, 16.0f);
        SolidBrush cbFill(state?(isAdultFocusActive?AClrGrayText:AClrTeal):AClrWhite);
        GraphicsPath* cp=MakeRoundRect(cbR,3);
        g.FillPath(&cbFill,cp); g.DrawPath(&pBorder,cp); delete cp;
        if(state) g.DrawString(L"\xE73E",-1,&fSmIcon,cbR,&fC,&bW);
        SolidBrush tBr(hover&&!isAdultFocusActive?AClrTeal:AClrDark);
        g.DrawString(txt,-1,&fNorm,RectF(r.X+22,r.Y,r.Width-22,r.Height),&fL,&tBr);
    };

    drawCb(rCbAdult, L"Block Adult Websites", cbAdultWeb, hCbAdultWeb);
    drawCb(rCbHardcore, L"Block Hardcore Keywords", cbHardcore, hCbHardcore);
    drawCb(rCbRomantic, L"Block Romantic / Softcore", cbRomantic, hCbRomantic);
    drawCb(rCbFbReels, L"Block FB Reels / YT Shorts", cbFbReels, hCbFbReels);

    g.DrawString(L"Custom Keywords",-1,&fBold,RectF(L_X, cy+230, 360, 20),&fL,&bSecLbl);
    
    RectF rCustIn(L_X, cy + 260, 290, 28);
    RectF rCustAdd(L_X + 300, cy + 260, 60, 28);
    RectF rCustTbl(L_X, cy + 295, 360, 105);

    GraphicsPath* cip=MakeRoundRect(rCustIn,4);
    g.FillPath(&bW,cip);
    if(isCustomInputActive){Pen pt(AClrTeal,1.5f);g.DrawPath(&pt,cip);}
    else g.DrawPath(&pBorder,cip);
    delete cip;

    if(customInputText.empty()&&!isCustomInputActive)
        g.DrawString(L"e.g. badword",-1,&fNorm,RectF(rCustIn.X+8,rCustIn.Y,270,28),&fL,&bGr);
    else{
        g.DrawString(customInputText.c_str(),-1,&fNorm,RectF(rCustIn.X+8,rCustIn.Y,270,28),&fL,&bDk);
        if(isCustomInputActive&&(GetTickCount()/500)%2==0){
            Graphics gT(GetDesktopWindow()); RectF br;
            gT.MeasureString(customInputText.c_str(),-1,&fNorm,PointF(0,0),&br);
            g.FillRectangle(&bDk,rCustIn.X+9+br.Width,rCustIn.Y+6,1.5f,16.0f);
        }
    }

    GraphicsPath* cap=MakeRoundRect(rCustAdd,4);
    SolidBrush caB(hoverCustomAddBtn?AClrTealHover:AClrTeal);
    g.FillPath(&caB,cap); delete cap;
    g.DrawString(L"+ Add",-1,&fBold,rCustAdd,&fC,&bW);

    g.FillRectangle(&bW,rCustTbl); g.DrawRectangle(&pBorder,rCustTbl.X,rCustTbl.Y,rCustTbl.Width,rCustTbl.Height);
    int maxD=min(3,(int)customAdultKeywords.size()-customScrollOffset);
    float itmY=rCustTbl.Y+1.0f;
    for(int i=0;i<maxD;i++){
        int di=customScrollOffset+i;
        if(di>=(int)customAdultKeywords.size()) break;
        if(i%2==0){SolidBrush rowBg(Color(255,248,251,253));g.FillRectangle(&rowBg,rCustTbl.X+1,itmY,rCustTbl.Width-2,34.0f);}
        g.DrawString(customAdultKeywords[di].name.c_str(),-1,&fNorm,RectF(L_X+8,itmY,300,34),&fL,&bDk);
        SolidBrush xBr(customAdultKeywords[di].isHoveredCross?AClrRed:AClrGrayText);
        g.DrawString(L"\xE711",-1,&fSmIcon,RectF(L_X+320,itmY,30,34),&fC,&xBr);
        if(i<maxD-1) g.DrawLine(&pBorder,rCustTbl.X+2,itmY+34,rCustTbl.X+rCustTbl.Width-2,itmY+34);
        itmY+=35.0f;
    }
    if(customAdultKeywords.empty()) g.DrawString(L"No custom keywords added yet",-1,&fSmall,rCustTbl,&fC,&bGr);
    if((int)customAdultKeywords.size()>3) g.FillRectangle(&bGr,(INT)(rCustTbl.X+rCustTbl.Width-4),(INT)rCustTbl.Y,4,(INT)rCustTbl.Height);

    // ─────────────────────────────────────────
    // RIGHT COLUMN: Strict Protocols & Advanced Options
    // ─────────────────────────────────────────
    g.DrawString(L"Strict Protocol Settings",-1,&fBold,RectF(R_X, cy+65, 360, 20),&fL,&bSecLbl);

    float cardW2 = 175.0f;
    float cardH2 = 65.0f;
    RectF rCStrict[5] = {
        RectF(R_X, cy + 95, cardW2, cardH2),
        RectF(R_X + 185, cy + 95, cardW2, cardH2),
        RectF(R_X, cy + 170, cardW2, cardH2),
        RectF(R_X + 185, cy + 170, cardW2, cardH2),
        RectF(R_X, cy + 245, 360, cardH2)
    };

    struct StrictCard {
        const wchar_t* icon; const wchar_t* title; const wchar_t* desc;
        bool* state; bool* hover; Color activeClr;
    };
    StrictCard cards[]={
        {L"\xE946", L"Silent Monitor",  L"Log & detect URLs.",       &cbSilentUrl,  &hCbSilentUrl,  AClrTeal},
        {L"\xE70F", L"Family DNS",      L"Cloudflare safe DNS.",     &cbDnsFilter,  &hCbDnsFilter,  AClrTeal},
        {L"\xE721", L"Safe Search",     L"Force web SafeSearch.",    &cbSafeSearch, &hCbSafeSearch, AClrTeal},
        {L"\xE890", L"Block Incognito", L"Close private windows.",   &cbIncognito,  &hCbIncognito,  AClrTeal},
        {L"\xE72E", L"Strict Lock Mode",L"Block TaskMgr & Regedit.", &cbStrictMode, &hCbStrictMode, AClrRed},
    };

    for(int i=0;i<5;i++){
        RectF cardR = rCStrict[i];
        bool st=*cards[i].state; bool hv=*cards[i].hover;
        
        GraphicsPath* cp=MakeRoundRect(cardR,5);
        SolidBrush cbg(hv&&!st?AClrBgHover:(st?AClrTealLight:AClrCardBg));
        g.FillPath(&cbg,cp);
        Pen cp2(st?cards[i].activeClr:AClrBorder,(st?2.0f:1.5f));
        g.DrawPath(&cp2,cp); delete cp;

        SolidBrush iconC(st?cards[i].activeClr:AClrGrayText);
        g.DrawString(cards[i].icon,-1,&fLgIcon,RectF(cardR.X+8,cardR.Y+12,28,40),&fL,&iconC);

        RectF cbx(cardR.X+cardR.Width-20,cardR.Y+8,14,14);
        GraphicsPath* cxp=MakeRoundRect(cbx,3);
        SolidBrush cxf(st?cards[i].activeClr:AClrWhite);
        g.FillPath(&cxf,cxp); g.DrawPath(&pBorder,cxp); delete cxp;
        if(st) g.DrawString(L"\xE73E",-1,&fTiny,cbx,&fC,&bW);

        SolidBrush titleC(st?AClrDark:AClrDark);
        g.DrawString(cards[i].title,-1,&fBold,RectF(cardR.X+38,cardR.Y+8,cardR.Width-62,18),&fL,&titleC);
        g.DrawString(cards[i].desc,-1,&fTiny,RectF(cardR.X+38,cardR.Y+26,cardR.Width-42,30),&fL,&bGr);

        if(st){
            GraphicsPath* bar=MakeRoundRect(RectF(cardR.X,cardR.Y+8,3,cardH2-16),2);
            SolidBrush barB(cards[i].activeClr); g.FillPath(&barB,bar); delete bar;
        }
    }

    g.DrawString(L"Advanced Options",-1,&fBold,RectF(R_X, cy+330, 200, 20),&fL,&bSecLbl);

    RectF rCard24h(R_X, cy + 360, cardW2, 70);
    RectF rCardPop(R_X + 185, cy + 360, cardW2, 70);

    // Card 1: 24h Lockdown
    {
        GraphicsPath* cp=MakeRoundRect(rCard24h,5);
        SolidBrush cbg(hCb24HourLock&&!cb24HourLock?AClrBgHover:AClrCardBg);
        g.FillPath(&cbg,cp);
        Pen cp2(cb24HourLock?AClrTeal:AClrBorder,1.5f);
        g.DrawPath(&cp2,cp); delete cp;

        SolidBrush iconC(cb24HourLock?AClrTeal:AClrOrange);
        g.DrawString(L"\xE72E",-1,&fLgIcon,RectF(rCard24h.X+8,rCard24h.Y+15,28,40),&fL,&iconC);

        RectF cbx(rCard24h.X+rCard24h.Width-20,rCard24h.Y+8,14,14);
        GraphicsPath* cxp=MakeRoundRect(cbx,3);
        SolidBrush cxf(cb24HourLock?AClrTeal:AClrWhite);
        g.FillPath(&cxf,cxp); g.DrawPath(&pBorder,cxp); delete cxp;
        if(cb24HourLock) g.DrawString(L"\xE73E",-1,&fTiny,cbx,&fC,&bW);

        g.DrawString(L"24-Hour Lock",-1,&fBold,RectF(rCard24h.X+38,rCard24h.Y+8,rCard24h.Width-62,18),&fL,&bDk);
        g.DrawString(L"Cannot be undone.",-1,&fTiny,RectF(rCard24h.X+38,rCard24h.Y+26,rCard24h.Width-42,16),&fL,&bGr);
        if(cb24HourLock){
            ULONGLONG left=lock24hEndTime>GetTickCount64()?lock24hEndTime-GetTickCount64():0;
            wstring rem=to_wstring(left/3600000)+L"h "+to_wstring((left%3600000)/60000)+L"m left";
            g.DrawString(rem.c_str(),-1,&fTiny,RectF(rCard24h.X+38,rCard24h.Y+42,rCard24h.Width-42,18),&fL,&bTeal);
        }
    }

    // Card 2: Periodic Popups
    {
        GraphicsPath* cp=MakeRoundRect(rCardPop,5);
        SolidBrush cbg(hCbPeriodicPopups?AClrBgHover:AClrCardBg);
        g.FillPath(&cbg,cp);
        Pen cp2(cbPeriodicPopups?AClrTeal:AClrBorder,1.5f);
        g.DrawPath(&cp2,cp); delete cp;

        SolidBrush iconC(cbPeriodicPopups?AClrTeal:AClrGrayText);
        g.DrawString(L"\xEA8F",-1,&fLgIcon,RectF(rCardPop.X+8,rCardPop.Y+15,28,40),&fL,&iconC);

        RectF cbx(rCardPop.X+rCardPop.Width-20,rCardPop.Y+8,14,14);
        GraphicsPath* cxp=MakeRoundRect(cbx,3);
        SolidBrush cxf(cbPeriodicPopups?AClrTeal:AClrWhite);
        g.FillPath(&cxf,cxp); g.DrawPath(&pBorder,cxp); delete cxp;
        if(cbPeriodicPopups) g.DrawString(L"\xE73E",-1,&fTiny,cbx,&fC,&bW);

        g.DrawString(L"Reminders",-1,&fBold,RectF(rCardPop.X+38,rCardPop.Y+8,rCardPop.Width-62,18),&fL,&bDk);
        g.DrawString(L"Fullscreen quote every 25 mins.",-1,&fTiny,RectF(rCardPop.X+38,rCardPop.Y+26,rCardPop.Width-42,30),&fL,&bGr);
    }

    // ─────────────────────────────────────────
    // BOTTOM: Status Summary Bar
    // ─────────────────────────────────────────
    g.DrawLine(&pBorder, L_X, cy + 460.0f, R_X + 360.0f, cy + 460.0f);
    g.DrawString(L"Active Protections",-1,&fBold,RectF(L_X, cy + 475, 200, 20),&fL,&bSecLbl);

    struct Badge{const wchar_t* icon;const wchar_t* label;bool active;};
    Badge badges[]={
        {L"\xE946",L"URL Monitor", cbSilentUrl},
        {L"\xE70F",L"DNS Filter",  cbDnsFilter},
        {L"\xE721",L"SafeSearch",  cbSafeSearch},
        {L"\xE890",L"No Incognito",cbIncognito},
        {L"\xE72E",L"Strict Lock", cbStrictMode},
        {L"\xE728",L"Focus Active",isStrictFocusActive || isAdultFocusActive},
    };
    float bxx = L_X; float badgeY = cy + 505.0f;
    for(auto& badge:badges){
        Color bc=badge.active?AClrTeal:AClrBorder;
        SolidBrush txtc(badge.active?AClrWhite:AClrGrayText);
        SolidBrush bgc(badge.active?AClrTeal:AClrCardBg);
        wstring bl=wstring(badge.icon)+L" "+badge.label;
        RectF mr; g.MeasureString(bl.c_str(),-1,&fSmall,PointF(0,0),&mr);
        float bw=mr.Width+20;
        RectF br(bxx,badgeY,bw,24);
        GraphicsPath* bp=MakeRoundRect(br,12);
        g.FillPath(&bgc,bp);
        if(!badge.active){Pen bpp(bc,1.0f);g.DrawPath(&bpp,bp);}
        delete bp;
        g.DrawString(bl.c_str(),-1,&fSmall,RectF(bxx+10,badgeY,bw-12,24),&fL,&txtc);
        bxx+=bw+8;
    }

    // ==========================================
    // --- OVERLAYS ---
    // ==========================================
    bool anyOverlay=(showTimeOverlay||showPassOverlay||showStrictTimeOverlay);
    if(anyOverlay){
        SolidBrush ov(AClrOverlay); g.FillRectangle(&ov,cx,cy,cw,ch);
        float ovW=420.0f,ovH=230.0f;
        float ovX=cx+(cw-ovW)/2.0f, ovY=cy+(ch-ovH)/2.0f;
        RectF ovR(ovX,ovY,ovW,ovH);
        GraphicsPath* op=MakeRoundRect(ovR,8);
        SolidBrush ovBg(AClrWhite); g.FillPath(&ovBg,op);
        Pen ovBorder(AClrTeal,2.0f); g.DrawPath(&ovBorder,op); delete op;

        RectF titleBar(ovX,ovY,ovW,46.0f);
        GraphicsPath* tbp=MakeRoundRect(RectF(ovX,ovY,ovW,46.0f),8);
        SolidBrush tbg(AClrTeal); g.FillPath(&tbg,tbp); delete tbp;
        g.FillRectangle(&tbg,ovX,ovY+30.0f,ovW,16.0f);

        if(showTimeOverlay||showStrictTimeOverlay){
            g.DrawString(L"Set Focus Duration",-1,&fTitle,RectF(ovX,ovY,ovW,46.0f),&fC,&bW);
            int& hrs=showTimeOverlay?focusHours:strictFocusHours;
            int& mns=showTimeOverlay?focusMins:strictFocusMins;
            bool hHM=showTimeOverlay?hTimeHM:hStrictTimeHM;
            bool hHP=showTimeOverlay?hTimeHP:hStrictTimeHP;
            bool hMM=showTimeOverlay?hTimeMM:hStrictTimeMM;
            bool hMP=showTimeOverlay?hTimeMP:hStrictTimeMP;
            bool hSt=showTimeOverlay?hTimeStart:hStrictTimeStart;
            bool hCl=showTimeOverlay?hTimeCancel:hStrictTimeCancel;

            g.DrawString(L"Hours:",-1,&fBold,RectF(ovX+50,ovY+72,60,36),&fL,&bDk);
            DrawSpinner(g,ovX+116,ovY+72,to_wstring(hrs),hHM,hHP,&fIcon,&fBold);
            g.DrawString(L"Mins:",-1,&fBold,RectF(ovX+254,ovY+72,50,36),&fL,&bDk);
            DrawSpinner(g,ovX+304,ovY+72,to_wstring(mns),hMM,hMP,&fIcon,&fBold);

            RectF cancelR(ovX+50,ovY+152,140,40);
            GraphicsPath* cxp=MakeRoundRect(cancelR,4);
            SolidBrush cxb(hCl?AClrBgHover:AClrWhite);
            g.FillPath(&cxb,cxp); g.DrawPath(&pBorder,cxp); delete cxp;
            g.DrawString(L"Cancel",-1,&fBold,cancelR,&fC,&bDk);

            RectF startR(ovX+214,ovY+152,156,40);
            GraphicsPath* stp=MakeRoundRect(startR,4);
            SolidBrush stb(hSt?AClrTealHover:AClrTeal);
            g.FillPath(&stb,stp); delete stp;
            g.DrawString(L"Start Focus",-1,&fBold,startR,&fC,&bW);
        }
        else if(showPassOverlay){
            wstring ttl=isStoppingFocus?L"Enter Password to Stop":L"Enter Friend's Password";
            g.DrawString(ttl.c_str(),-1,&fTitle,RectF(ovX,ovY,ovW,46.0f),&fC,&bW);
            RectF piR(ovX+40,ovY+76,ovW-80,40);
            GraphicsPath* pip=MakeRoundRect(piR,4);
            g.FillPath(&bW,pip);
            Pen pip2(AClrTeal,2.0f); g.DrawPath(&pip2,pip); delete pip;
            wstring disp(inputPassText.length(),L'*');
            if(inputPassText.empty()&&!isPassInputActive)
                g.DrawString(L"Type password here...",-1,&fNorm,piR,&fC,&bGr);
            else{
                g.DrawString(disp.c_str(),-1,&fTitle,RectF(ovX+50,ovY+82,ovW-100,28),&fL,&bDk);
                if(isPassInputActive&&(GetTickCount()/500)%2==0){
                    Graphics gT(GetDesktopWindow()); RectF br;
                    gT.MeasureString(disp.c_str(),-1,&fTitle,PointF(0,0),&br);
                    g.FillRectangle(&bDk,ovX+52+br.Width,ovY+88,1.5f,20.0f);
                }
            }
            RectF cancelR(ovX+40,ovY+152,140,40);
            GraphicsPath* cxp=MakeRoundRect(cancelR,4);
            SolidBrush cxb(hPassCancel?AClrBgHover:AClrWhite);
            g.FillPath(&cxb,cxp); g.DrawPath(&pBorder,cxp); delete cxp;
            g.DrawString(L"Cancel",-1,&fBold,cancelR,&fC,&bDk);
            RectF confR(ovX+200,ovY+152,160,40);
            GraphicsPath* cop=MakeRoundRect(confR,4);
            SolidBrush cob(hPassConfirm?AClrTealHover:AClrTeal);
            g.FillPath(&cob,cop); delete cop;
            g.DrawString(L"Confirm",-1,&fBold,confR,&fC,&bW);
        }
    }

    // ==========================================
    // --- DRAW OPEN DROPDOWNS ON TOP ---
    // ==========================================
    if(!anyOverlay){
        if(isControlDropOpen&&!isAdultFocusActive){
            RectF dR(rMode.X,rMode.Y+31,rMode.Width,2*30.0f);
            GraphicsPath* dp=MakeRoundRect(dR,4);
            SolidBrush dBg(AClrWhite); g.FillPath(&dBg,dp); g.DrawPath(&pBorder,dp); delete dp;
            for(int i=0;i<2;i++){
                SolidBrush hb(hoverCtrlIdx==i?AClrBgHover:AClrWhite);
                g.FillRectangle(&hb,dR.X+1,dR.Y+i*30.0f+1,dR.Width-2,28.0f);
                g.DrawString(ctrlModes[i].c_str(),-1,&fNorm,RectF(dR.X+8,dR.Y+i*30,dR.Width-8,30),&fL,&bDk);
            }
        }
        if(isRelDropOpen&&!isAdultFocusActive){
            RectF dR(rRel.X,rRel.Y+31,rRel.Width,4*30.0f);
            GraphicsPath* dp=MakeRoundRect(dR,4);
            SolidBrush dBg(AClrWhite); g.FillPath(&dBg,dp); g.DrawPath(&pBorder,dp); delete dp;
            for(int i=0;i<4;i++){
                SolidBrush hb(hoverRelIdx==i?AClrBgHover:AClrWhite);
                g.FillRectangle(&hb,dR.X+1,dR.Y+i*30.0f+1,dR.Width-2,28.0f);
                g.DrawString(religions[i].c_str(),-1,&fNorm,RectF(dR.X+8,dR.Y+i*30,dR.Width-8,30),&fL,&bDk);
            }
        }
        if(isLangDropOpen&&!isAdultFocusActive){
            RectF dR(rLang.X,rLang.Y+31,rLang.Width,2*30.0f);
            GraphicsPath* dp=MakeRoundRect(dR,4);
            SolidBrush dBg(AClrWhite); g.FillPath(&dBg,dp); g.DrawPath(&pBorder,dp); delete dp;
            for(int i=0;i<2;i++){
                SolidBrush hb(hoverLangIdx==i?AClrBgHover:AClrWhite);
                g.FillRectangle(&hb,dR.X+1,dR.Y+i*30.0f+1,dR.Width-2,28.0f);
                g.DrawString(languages[i].c_str(),-1,&fNorm,RectF(dR.X+8,dR.Y+i*30,dR.Width-8,30),&fL,&bDk);
            }
        }
    }
}

// ==========================================
// --- MOUSE MOVE ---
// ==========================================
void ProcessAdultMouseMove(float x, float y) {
    float cx=s_contentX,cy=s_contentY,cw=s_contentW,ch=s_contentH;
    
    // Reset hovers
    hoverAdultFocusBtn=hoverControlDrop=hoverRelDrop=hoverLangDrop=false;
    hCbAdultWeb=hCbFbReels=hCbHardcore=hCbRomantic=false;
    hoverCustomInput=hoverCustomAddBtn=false;
    hTimeHM=hTimeHP=hTimeMM=hTimeMP=hTimeStart=hTimeCancel=false;
    hPassInput=hPassConfirm=hPassCancel=false;
    hCb24HourLock=hCbPeriodicPopups=false;
    hoverStrictFocusBtn=hoverStrictPanicBtn=false;
    hCbSilentUrl=hCbDnsFilter=hCbSafeSearch=hCbIncognito=hCbStrictMode=false;
    hStrictTimeHM=hStrictTimeHP=hStrictTimeMM=hStrictTimeMP=hStrictTimeStart=hStrictTimeCancel=false;
    for(auto& it:customAdultKeywords) it.isHoveredCross=false;

    bool anyOverlay=(showTimeOverlay||showPassOverlay||showStrictTimeOverlay);
    if(anyOverlay){
        float ovW=420,ovH=230;
        float ovX=cx+(cw-ovW)/2.0f, ovY2=cy+(ch-ovH)/2.0f;
        if(showTimeOverlay){
            if(RectF(ovX+116,ovY2+72,32,36).Contains(x,y)) hTimeHM=true;
            if(RectF(ovX+208,ovY2+72,32,36).Contains(x,y)) hTimeHP=true;
            if(RectF(ovX+304,ovY2+72,32,36).Contains(x,y)) hTimeMM=true;
            if(RectF(ovX+396,ovY2+72,32,36).Contains(x,y)) hTimeMP=true;
            if(RectF(ovX+50, ovY2+152,140,40).Contains(x,y)) hTimeCancel=true;
            if(RectF(ovX+214,ovY2+152,156,40).Contains(x,y)) hTimeStart=true;
        } else if(showStrictTimeOverlay){
            if(RectF(ovX+116,ovY2+72,32,36).Contains(x,y)) hStrictTimeHM=true;
            if(RectF(ovX+208,ovY2+72,32,36).Contains(x,y)) hStrictTimeHP=true;
            if(RectF(ovX+304,ovY2+72,32,36).Contains(x,y)) hStrictTimeMM=true;
            if(RectF(ovX+396,ovY2+72,32,36).Contains(x,y)) hStrictTimeMP=true;
            if(RectF(ovX+50, ovY2+152,140,40).Contains(x,y)) hStrictTimeCancel=true;
            if(RectF(ovX+214,ovY2+152,156,40).Contains(x,y)) hStrictTimeStart=true;
        } else if(showPassOverlay){
            if(RectF(ovX+40, ovY2+76, ovW-80,40).Contains(x,y)) hPassInput=true;
            if(RectF(ovX+40, ovY2+152,140,40).Contains(x,y)) hPassCancel=true;
            if(RectF(ovX+200,ovY2+152,160,40).Contains(x,y)) hPassConfirm=true;
        }
        return;
    }

    float L_X = cx + 20.0f;
    float R_X = cx + 410.0f;

    RectF rFocusA(L_X, cy + 15, 130, 30);
    RectF rMode(L_X + 140, cy + 15, 110, 30);
    RectF rRel(L_X + 260, cy + 15, 100, 30);
    RectF rLang(L_X + 370, cy + 15, 80, 30);
    RectF rFocusS(L_X + 470, cy + 15, 120, 30);
    RectF rPanic(L_X + 600, cy + 15, 110, 30);

    hoverAdultFocusBtn = rFocusA.Contains(x,y) && !cb24HourLock && !(isAdultFocusActive&&controlMode==0);
    hoverStrictFocusBtn= rFocusS.Contains(x,y);
    hoverStrictPanicBtn= rPanic.Contains(x,y);

    if(isControlDropOpen){hoverCtrlIdx=-1;RectF dR(rMode.X,rMode.Y+31,rMode.Width,60);for(int i=0;i<2;i++)if(RectF(dR.X,dR.Y+i*30,dR.Width,30).Contains(x,y))hoverCtrlIdx=i;return;}
    if(isRelDropOpen){hoverRelIdx=-1;RectF dR(rRel.X,rRel.Y+31,rRel.Width,120);for(int i=0;i<4;i++)if(RectF(dR.X,dR.Y+i*30,dR.Width,30).Contains(x,y))hoverRelIdx=i;return;}
    if(isLangDropOpen){hoverLangIdx=-1;RectF dR(rLang.X,rLang.Y+31,rLang.Width,60);for(int i=0;i<2;i++)if(RectF(dR.X,dR.Y+i*30,dR.Width,30).Contains(x,y))hoverLangIdx=i;return;}

    hoverControlDrop=rMode.Contains(x,y);
    hoverRelDrop=rRel.Contains(x,y);
    hoverLangDrop=rLang.Contains(x,y);

    float cbStartY = cy + 95;
    hCbAdultWeb = RectF(L_X, cbStartY, 360, 22).Contains(x,y);
    hCbHardcore = RectF(L_X, cbStartY + 30, 360, 22).Contains(x,y);
    hCbRomantic = RectF(L_X, cbStartY + 60, 360, 22).Contains(x,y);
    hCbFbReels  = RectF(L_X, cbStartY + 90, 360, 22).Contains(x,y);

    hoverCustomInput  = RectF(L_X, cy + 260, 290, 28).Contains(x,y);
    hoverCustomAddBtn = RectF(L_X + 300, cy + 260, 60, 28).Contains(x,y);

    if(!isAdultFocusActive){
        float iy=cy + 296.0f;
        int md=min(3,(int)customAdultKeywords.size()-customScrollOffset);
        for(int i=0;i<md;i++){int di=customScrollOffset+i;if(RectF(L_X+320,iy,30,34).Contains(x,y))customAdultKeywords[di].isHoveredCross=true;iy+=35.0f;}
    }

    float cardW2 = 175.0f;
    float cardH2 = 65.0f;
    RectF rCStrict[5] = {
        RectF(R_X, cy + 95, cardW2, cardH2),
        RectF(R_X + 185, cy + 95, cardW2, cardH2),
        RectF(R_X, cy + 170, cardW2, cardH2),
        RectF(R_X + 185, cy + 170, cardW2, cardH2),
        RectF(R_X, cy + 245, 360, cardH2)
    };
    bool* hov[]={&hCbSilentUrl,&hCbDnsFilter,&hCbSafeSearch,&hCbIncognito,&hCbStrictMode};
    for(int i=0;i<5;i++){
        *hov[i]=rCStrict[i].Contains(x,y);
    }

    hCb24HourLock    = RectF(R_X, cy + 360, cardW2, 70).Contains(x,y);
    hCbPeriodicPopups= RectF(R_X + 185, cy + 360, cardW2, 70).Contains(x,y);
}

// ==========================================
// --- MOUSE CLICK ---
// ==========================================
void ProcessAdultMouseClick(float x, float y) {
    if(showTimeOverlay){
        if(hTimeHM&&focusHours>0)focusHours--;
        if(hTimeHP&&focusHours<23)focusHours++;
        if(hTimeMM){focusMins-=5;if(focusMins<0)focusMins=55;}
        if(hTimeMP)focusMins=(focusMins+5)%60;
        if(hTimeCancel)showTimeOverlay=false;
        if(hTimeStart){isAdultFocusActive=true;focusEndTime=GetTickCount64()+((ULONGLONG)focusHours*3600000)+((ULONGLONG)focusMins*60000);showTimeOverlay=false;SaveAdultSettings();}
        return;
    }
    if(showStrictTimeOverlay){
        if(hStrictTimeHM&&strictFocusHours>0)strictFocusHours--;
        if(hStrictTimeHP&&strictFocusHours<23)strictFocusHours++;
        if(hStrictTimeMM){strictFocusMins-=5;if(strictFocusMins<0)strictFocusMins=55;}
        if(hStrictTimeMP)strictFocusMins=(strictFocusMins+5)%60;
        if(hStrictTimeCancel)showStrictTimeOverlay=false;
        if(hStrictTimeStart){isStrictFocusActive=true;strictFocusEndTime=GetTickCount64()+((ULONGLONG)strictFocusHours*3600000)+((ULONGLONG)strictFocusMins*60000);showStrictTimeOverlay=false;SaveStrictSettings();}
        return;
    }
    if(showPassOverlay){
        isPassInputActive=hPassInput;
        if(hPassCancel){showPassOverlay=false;inputPassText=L"";}
        if(hPassConfirm&&!inputPassText.empty()){
            isAdultFocusActive=!isStoppingFocus;showPassOverlay=false;inputPassText=L"";SaveAdultSettings();
        }
        return;
    }

    if(hoverAdultFocusBtn&&!cb24HourLock){
        if(isAdultFocusActive){
            if(controlMode==1){isStoppingFocus=true;showPassOverlay=true;isPassInputActive=true;}
            else isAdultFocusActive=false;
        } else {
            if(controlMode==0)showTimeOverlay=true;
            else{isStoppingFocus=false;showPassOverlay=true;isPassInputActive=true;}
        }
    }
    if(hoverStrictFocusBtn){
        if(!g_isPremiumUser){ g_showUpgradePopup=true; return; }
        if(isStrictFocusActive)isStrictFocusActive=false;
        else showStrictTimeOverlay=true;
    }
    if(hoverStrictPanicBtn){
        if(!g_isPremiumUser){ g_showUpgradePopup=true; return; }
        isPanicActive=true;panicStartTime=GetTickCount();
    }

    if(isControlDropOpen){if(hoverCtrlIdx!=-1)controlMode=hoverCtrlIdx;isControlDropOpen=false;SaveAdultSettings();return;}
    if(isRelDropOpen){if(hoverRelIdx!=-1)adultReligion=hoverRelIdx;isRelDropOpen=false;SaveAdultSettings();return;}
    if(isLangDropOpen){if(hoverLangIdx!=-1)adultLanguage=hoverLangIdx;isLangDropOpen=false;SaveAdultSettings();return;}
    if(hoverControlDrop&&!isAdultFocusActive)isControlDropOpen=true;
    if(hoverRelDrop&&!isAdultFocusActive)isRelDropOpen=true;
    if(hoverLangDrop&&!isAdultFocusActive)isLangDropOpen=true;

    auto handleCb=[](bool& state,bool hover){if(hover){if(isAdultFocusActive){if(!state)state=true;}else state=!state;}};
    handleCb(cbAdultWeb, hCbAdultWeb);
    handleCb(cbFbReels,  hCbFbReels);
    handleCb(cbHardcore, hCbHardcore);
    handleCb(cbRomantic, hCbRomantic);
    handleCb(cbPeriodicPopups, hCbPeriodicPopups);

    if(hCb24HourLock&&!cb24HourLock){
        if(!g_isPremiumUser){ g_showUpgradePopup=true; return; }
        int r=MessageBox(NULL,"Are you sure? This locks for 24 hours and CANNOT be undone.","24-Hour Lockdown",MB_YESNO|MB_ICONWARNING);
        if(r==IDYES){cb24HourLock=true;isAdultFocusActive=true;lock24hEndTime=GetTickCount64()+86400000ULL;}
    }

    isCustomInputActive=hoverCustomInput;
    if(hoverCustomAddBtn&&!customInputText.empty()){customAdultKeywords.push_back({customInputText,false});customInputText=L"";}
    if(!isAdultFocusActive){for(auto it=customAdultKeywords.begin();it!=customAdultKeywords.end();){if(it->isHoveredCross)it=customAdultKeywords.erase(it);else ++it;}}

    auto toggleStrict=[](bool& state,bool hover,bool doProtocol){ if(hover){state=!state;if(doProtocol){}} };
    if(hCbSilentUrl||hCbDnsFilter||hCbSafeSearch||hCbIncognito||hCbStrictMode){
        if(!g_isPremiumUser){ g_showUpgradePopup=true; return; }
    }
    if(hCbSilentUrl){cbSilentUrl=!cbSilentUrl;}
    if(hCbDnsFilter){cbDnsFilter=!cbDnsFilter;SetFamilyDNS(cbDnsFilter);EnforceStrictProtocols();}
    if(hCbSafeSearch){cbSafeSearch=!cbSafeSearch;EnforceStrictProtocols();}
    if(hCbIncognito){cbIncognito=!cbIncognito;}
    if(hCbStrictMode){cbStrictMode=!cbStrictMode;}

    SaveAdultSettings();
    SaveStrictSettings();
}

// ==========================================
// --- KEY INPUT ---
// ==========================================
void ProcessAdultKeyPress(wchar_t c) {
    if(showPassOverlay&&isPassInputActive){
        if(c>=32&&c<=126&&inputPassText.length()<20) inputPassText+=c;
    } else if(isCustomInputActive&&c>=32&&c<=126&&customInputText.length()<40){
        customInputText+=c;
    }
}

void ProcessAdultKeyDown(WPARAM key) {
    if(showPassOverlay&&isPassInputActive){
        if(key==VK_BACK&&!inputPassText.empty()) inputPassText.pop_back();
    } else if(isCustomInputActive){
        if(key==VK_BACK&&!customInputText.empty()) customInputText.pop_back();
        else if(key==VK_RETURN&&!customInputText.empty()){
            customAdultKeywords.push_back({customInputText,false});
            customInputText=L""; SaveAdultSettings();
        }
    }
}

// ==========================================
// --- MOUSE WHEEL ---
// ==========================================
void ProcessAdultMouseWheel(float x, float y, int delta) {
    int steps=(delta>0)?1:-1;
    if(!showTimeOverlay&&!showPassOverlay&&!showStrictTimeOverlay){
        float L_X = s_contentX + 20.0f;
        RectF tblR(L_X, s_contentY + 295, 360, 105);
        if(tblR.Contains(x,y)){
            if(steps>0&&customScrollOffset>0) customScrollOffset--;
            else if(steps<0&&customScrollOffset<max(0,(int)customAdultKeywords.size()-3)) customScrollOffset++;
        }
    }
}

// ─── Aliases used by tab_blocks.cpp ───────────────────────────────────────
void ProcessAdultBlockMouseMove(float x, float y)          { ProcessAdultMouseMove(x, y); }
void ProcessAdultBlockMouseClick(float x, float y)         { ProcessAdultMouseClick(x, y); }
void ProcessAdultBlockKeyPress(wchar_t c)                  { ProcessAdultKeyPress(c); }
void ProcessAdultBlockKeyDown(WPARAM key)                  { ProcessAdultKeyDown(key); }
void ProcessAdultBlockMouseWheel(float x, float y, int d)  { ProcessAdultMouseWheel(x, y, d); }

// ─── Schedule Integration API ─────────────────────────────────────────────
// tab_schedule_blocks.cpp এর ApplyProfileBlocking() এই function call করে।
// Adult-এর নিজস্ব state, PAC, hosts — সব এক জায়গা থেকেই controlled থাকে।
// Schedule Block নিজে adult logic copy করে না — শুধু এখানে delegate করে।
void AdultBlock_ApplyForSchedule(bool enable) {
    if (enable) {
        // Adult block চালু — state set করি, checkbox গুলো activate করি
        isAdultFocusActive = true;
        cbAdultWeb  = true;
        cbHardcore  = true;
        cbRomantic  = true;
        // focusEndTime 0 রাখি — Schedule Block নিজে session manage করে,
        // Adult এর timer এখানে interfere করবে না।
        focusEndTime = 0;
        // DNS filter ও safe search enforce করি
        EnforceStrictProtocols();
        SaveAdultSettings();
    } else {
        // Adult block বন্ধ — শুধু schedule-triggered focus টা তুলে নিই।
        // User যদি manually adult focus on করে রাখে সেটা touch করব না।
        // (cb24HourLock active থাকলে সেটাও respect করতে হবে)
        if (!cb24HourLock) {
            isAdultFocusActive = false;
            // Strict protocols re-evaluate করি (cbDnsFilter/cbSafeSearch
            // user যদি manually on রাখে, সেগুলো এখনও কাজ করবে)
            EnforceStrictProtocols();
            SaveAdultSettings();
        }
        // cb24HourLock true হলে কিছুই করি না — 24h lock untouchable
    }
}