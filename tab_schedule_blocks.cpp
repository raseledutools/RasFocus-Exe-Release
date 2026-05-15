#pragma warning(disable : 4996)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)

#include "tab_schedule_blocks.h"
#include "tab_adult.h"   // ← AdultBlock_ApplyForSchedule() এর জন্য
#include <tlhelp32.h>    // ← CreateToolhelp32Snapshot, KillBlockedApps এর জন্য
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <shlobj.h>
#include <codecvt>
#include <locale>
#include <algorithm>
#include <ctime>
#include <thread>        // 🟢 NEW: For Background Observer Thread
#include <mutex>         // 🟢 NEW: For Thread Safety
#include <wininet.h>     // 🟢 NEW: For InternetSetOption

#pragma comment(lib, "wininet.lib")

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;  // 🟢 NEW: Access main window handle for redrawing from thread
static std::mutex g_schMutex; // 🟢 NEW: Mutex to prevent crashes during continuous blocking
static bool isSchThreadRunning = false; // 🟢 NEW: Thread running flag

// --- MISSING HELPERS & COLORS ---
static void AddRoundedRectPath(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static void AddRoundedRectPath(GraphicsPath& path, RectF rect, float r) {
    AddRoundedRectPath(path, rect.X, rect.Y, rect.Width, rect.Height, r);
}

// Returns a heap-allocated GraphicsPath for a rounded rectangle (caller must delete)
static GraphicsPath* GetSchRoundRectPath(RectF rect, float r) {
    GraphicsPath* path = new GraphicsPath();
    AddRoundedRectPath(*path, rect, r);
    return path;
}

static GraphicsPath* GetSchRoundRectPath(float x, float y, float w, float h, float r) {
    return GetSchRoundRectPath(RectF(x, y, w, h), r);
}

// ==========================================
// DrawSchOverlaySpinner — +/- বাটন সহ value দেখানোর helper
// cx/cy = left edge of the spinner group
// Draws: [−] [ value ] [+]
// Returns nothing; hover state (hMinus/hPlus) এবং stored RectF বাইরে manage হয়
// ==========================================
static void DrawSchOverlaySpinner(
    Graphics& g,
    float cx, float cy,
    const wstring& val,
    bool hMinus, bool hPlus,
    Font* fIcon, Font* fBold)
{
    // Colors
    Color clrBg(255, 240, 242, 245);
    Color clrHov(255, 220, 240, 242);
    Color clrBorder(255, 200, 210, 220);
    Color clrTeal(255, 12, 168, 176);
    Color clrDark(255, 50, 50, 50);
    Color clrWhite(255, 255, 255, 255);

    Pen pBorder(clrBorder, 1.5f);
    Pen pTeal(clrTeal, 1.5f);

    StringFormat fC;
    fC.SetAlignment(StringAlignmentCenter);
    fC.SetLineAlignment(StringAlignmentCenter);

    SolidBrush bDark(clrDark);
    SolidBrush bWhite(clrWhite);

    float btnW = 32.0f, btnH = 36.0f;
    float valW = 52.0f;

    // [−] button
    RectF minusRect(cx, cy, btnW, btnH);
    {
        GraphicsPath* mp = GetSchRoundRectPath(minusRect, 5);
        SolidBrush mb(hMinus ? clrHov : clrBg);
        g.FillPath(&mb, mp);
        g.DrawPath(hMinus ? &pTeal : &pBorder, mp);
        delete mp;
        SolidBrush mb2(hMinus ? clrTeal : clrDark);
        g.DrawString(L"\uE108", -1, fIcon, minusRect, &fC, &mb2); // Segoe MDL2: Remove/Subtract
    }

    // [ value ]
    RectF valRect(cx + btnW + 4, cy, valW, btnH);
    {
        GraphicsPath* vp = GetSchRoundRectPath(valRect, 5);
        SolidBrush vb(clrWhite);
        g.FillPath(&vb, vp);
        g.DrawPath(&pBorder, vp);
        delete vp;
        g.DrawString(val.c_str(), -1, fBold, valRect, &fC, &bDark);
    }

    // [+] button
    RectF plusRect(cx + btnW + 4 + valW + 4, cy, btnW, btnH);
    {
        GraphicsPath* pp = GetSchRoundRectPath(plusRect, 5);
        SolidBrush pb(hPlus ? clrHov : clrBg);
        g.FillPath(&pb, pp);
        g.DrawPath(hPlus ? &pTeal : &pBorder, pp);
        delete pp;
        SolidBrush pb2(hPlus ? clrTeal : clrDark);
        g.DrawString(L"\uE109", -1, fIcon, plusRect, &fC, &pb2); // Segoe MDL2: Add
    }
}

// Helper: spinner'র [−] rect ও [+] rect কোথায় — hover/click detection-এ ব্যবহার
static RectF SchSpinnerMinusRect(float cx, float cy) { return RectF(cx, cy, 32.0f, 36.0f); }
static RectF SchSpinnerPlusRect(float cx, float cy)  { return RectF(cx + 32.0f + 4.0f + 52.0f + 4.0f, cy, 32.0f, 36.0f); }

// Scrollbar Colors
#define s_hScrollbarThumb Color(255, 180, 180, 180)
#define s_hScrollbarTrack Color(255, 240, 240, 240)

// ==========================================
// --- DATA STRUCTURES & GLOBALS ---
// ==========================================
struct SchBlockItem { 
    wstring name; 
    bool isHoveredCross = false; 
};

struct FocusProfile {
    wstring profileName;
    vector<SchBlockItem> blockedWebsites;
    vector<SchBlockItem> blockedApps;
    vector<SchBlockItem> blockedKeywords; 
    bool isActive = false;
    
    int lockMode = 0; 
    time_t lockEndTime = 0; 
    wstring parentsPassword = L""; 
    
    bool activeDays[7] = {false};
    int startHour = 9, startMin = 0;
    int endHour = 17, endMin = 0;
    bool blockInternet = false;
    bool blockAdult = false;
    bool blockUninstall = true;
    
    bool hToggle = false;
    bool hEdit = false;
    bool hDel = false;
};

static vector<FocusProfile> g_profiles;
static bool isSchDataLoaded = false;
static float sch_tScroll = 0.0f, sch_cScroll = 0.0f;
static float s_cx = 0, s_cy = 0, s_cw = 800, s_ch = 600;

// Edit Overlay & Sub-Tab System
static int s_activeSubTab = 0; 
static float s_listScrollT[3] = {0, 0, 0}; 
static float s_listScrollC[3] = {0, 0, 0};
static float s_listScrollMax[3] = {0, 0, 0};

// Scrollbar drag state (Main View Only)
static bool s_scrollbarDragging = false;
static float s_scrollbarDragStartY = 0.0f;
static float s_scrollbarDragStartScroll = 0.0f;

static vector<wstring> schCommonWebsites = { L"facebook.com", L"youtube.com", L"instagram.com", L"tiktok.com", L"reddit.com", L"twitter.com" };
static vector<wstring> schCommonApps = { L"chrome.exe", L"msedge.exe", L"telegram.exe", L"discord.exe", L"vlc.exe", L"control.exe", L"Taskmgr.exe", L"cmd.exe", L"SystemSettings.exe", L"run.exe" };

struct QuickBlockBtn {
    wstring label;
    wstring icon;
    vector<wstring> websites;
    vector<wstring> keywords;
    bool hovered = false;
};

static vector<QuickBlockBtn> s_quickBlocks = {
    { L"YT Shorts", L"\xE714", { L"youtube.com/shorts" }, { L"youtube.com/shorts", L"/shorts/" } },
    { L"FB Reels", L"\xE93E", { L"facebook.com/reels", L"fb.watch" }, { L"facebook.com/reels", L"/reels/", L"fb.watch" } },
    { L"YT Ads", L"\xE8D4", {}, { L"googlevideo.com", L"doubleclick.net", L"googleadservices.com", L"youtube.com/pagead" } },
    { L"IG Reels", L"\xE93E", { L"instagram.com/reels" }, { L"instagram.com/reels", L"/reels/" } },
};
static vector<RectF> s_quickBlockRects;

static int editingProfileIdx = -1;
static wstring inpProfileName = L"";
static wstring inpWeb = L"";
static wstring inpApp = L"";
static wstring inpKey = L"";
static int activeInput = 0; 

static bool editDays[7] = {false};
static int editStH = 9, editStM = 0, editEnH = 17, editEnM = 0;
static bool editBlockInt = false, editBlockAdult = false, editBlockUninst = true;

static bool hAddProfileBtn = false;
static bool hoverSchWebCombo = false;
static int hoverSchWebOptIdx = -1;
static bool hoverSchAppCombo = false;
static int hoverSchAppOptIdx = -1;
static bool hoverSchModeDropdown = false;
static int tempLockMode = 0;

static bool isSchWebComboOpen = false;
static bool isSchAppComboOpen = false;
static bool isSchModeDropdownOpen = false;

// --- Full Screen Overlays (Locking System) ---
static int activeActionProfileIdx = -1;
static bool s_showTimeOverlay = false;

// 🟢 Time Overlay state — Months/Days/Hours/Mins + Full Day toggle
static int s_focusMonths = 0, s_focusDays = 0, s_focusHours = 1, s_focusMins = 0;
static bool s_focusFullDay = false; // 🟢 NEW: Full Day checkbox

// Spinner hover booleans
static bool s_hTimeMoM=false, s_hTimeMoP=false;
static bool s_hTimeDM=false,  s_hTimeDP=false;
static bool s_hTimeHM=false,  s_hTimeHP=false;
static bool s_hTimeMM=false,  s_hTimeMP=false;
static bool s_hTimeFullDay=false; // 🟢 NEW
static bool s_hTimeStart=false, s_hTimeCancel=false;

// Stored spinner rects for hit-testing (computed during draw, used in hover/click)
// Layout per row: label | [−][val][+]
// cx offsets (ovX-relative):
//   Months  label@40  spinner@110
//   Days    label@250 spinner@300   (same row as Months)
//   Hours   label@40  spinner@110
//   Mins    label@250 spinner@300   (same row as Hours, only shown if !fullDay)
// Full Day checkbox row between the two spinner rows
static const float kSpinLabelCol1 = 40.0f;
static const float kSpinnerCol1   = 110.0f;
static const float kSpinLabelCol2 = 265.0f;
static const float kSpinnerCol2   = 305.0f;
static const float kSpinRowY1     = 80.0f;   // Months / Days row
static const float kFullDayRowY   = 130.0f;  // Full Day checkbox row
static const float kSpinRowY2     = 175.0f;  // Hours / Mins row (hidden if fullDay)
static const float kBtnRowY_FD    = 225.0f;  // button row when fullDay=true  (no Hours/Mins row)
static const float kBtnRowY_NFD   = 225.0f;  // button row same — overlay height adjusts

// Overlay sizing
static const float kTimeOvW = 500.0f;
// Height: fullDay → shorter (no Hours/Mins row)
// We always draw same height and just skip Hours/Mins to keep layout stable

static bool s_showPassOverlay = false;
static wstring s_inputPassText = L"";
static bool s_isPassInputActive = true, s_hPassInput = false;
static bool s_hPassConfirm = false, s_hPassCancel = false;
static bool s_isStoppingFocus = false; 

static bool s_showTextUnlockOverlay = false;
static wstring s_targetUnlockText = L"To unlock this PC, you must realize that focus is the key to success. Avoid distractions, work hard, and never give up. True discipline comes from within. Success is not an accident, it is hard work, perseverance, learning, studying, sacrifice and most of all, love of what you are doing or learning to do. Type this exact text carefully to regain access and prove your self-control.";
static wstring s_currentTypingText = L"";
static bool s_isTypingActive = true;
static bool s_hTextUnlockConfirm = false, s_hTextUnlockCancel = false;

// --- Colors ---
static const Color ClrTeal(255, 12, 168, 176);
static const Color ClrTealHover(255, 30, 185, 195);
static const Color ClrDark(255, 50, 50, 50);
static const Color ClrGrayText(255, 120, 120, 120);
static const Color ClrWhite(255, 255, 255, 255);
static const Color ClrBg(255, 248, 250, 252);
static const Color ClrBgHover(255, 235, 248, 250);
static const Color ClrRed(255, 231, 76, 60);
static const Color ClrGreen(255, 90, 170, 20);
static const Color ClrOverlay(180, 0, 0, 0);
static const Color ClrDisabled(255, 200, 200, 200);
static const Color ClrScrollbar(255, 200, 210, 220);
static const Color ClrScrollbarHover(255, 12, 168, 176);
static const Color ClrScrollbarTrack(255, 240, 242, 245);

// --- DYNAMIC HITBOX SYSTEM FOR EDIT OVERLAY ---
struct EditHitboxes {
    RectF saveBtn, cancelBtn, nextBtn, backBtn;
    RectF subTabRects[3];
    int hSubTab = -1;

    RectF nameInp, modeDrop;
    RectF days[7];
    RectF stH_Box, stM_Box, stAmPm;
    RectF enH_Box, enM_Box, enAmPm;
    RectF togInt, togAdt, togUni;
    RectF webInp, webCombo, addWeb;
    RectF appInp, appCombo, addApp;
    RectF keyInp, addKey;
    
    vector<pair<RectF, int>> webDel, appDel, keyDel;
    RectF listAreas[3]; 

    RectF modeOpt[3];
    vector<RectF> webOpts, appOpts;

    bool hSave=false, hCancel=false, hNext=false, hBack=false;
    int hDay=-1;
    bool hStH=false, hStM=false, hStAmPm=false;
    bool hEnH=false, hEnM=false, hEnAmPm=false;
    bool hTogInt=false, hTogAdt=false, hTogUni=false;
    bool hAddWeb=false, hAddApp=false, hAddKey=false;
    bool hOptSelf=false, hOptParents=false, hOptLongText=false;
} g_ehb;


// ==========================================
// --- BROWSER-ACCURATE BLOCKING LOGIC ---
// ==========================================
static vector<wstring> GetAllBlockPatterns(const wstring& entry) {
    vector<wstring> patterns;
    wstring e = entry;
    if (e.substr(0, 8) == L"https://") e = e.substr(8);
    if (e.substr(0, 7) == L"http://") e = e.substr(7);
    if (e.substr(0, 4) == L"www.") e = e.substr(4);

    if (e == L"youtube.com/shorts" || e == L"/shorts/") {
        patterns.push_back(L"kw:youtube.com/shorts");
        patterns.push_back(L"kw:/shorts/");
        patterns.push_back(L"kw:youtubei.googleapis.com/youtubei/v1/reel");
        patterns.push_back(L"kw:www.youtube.com/shorts");
        return patterns;
    }
    if (e == L"facebook.com/reels" || e == L"/reels/" || e == L"instagram.com/reels") {
        patterns.push_back(L"kw:" + e);
        patterns.push_back(L"kw:/reels/");
        patterns.push_back(L"kw:graph.facebook.com/reels");
        patterns.push_back(L"kw:graph.instagram.com/reels");
        return patterns;
    }
    if (e == L"fb.watch") {
        patterns.push_back(L"fb.watch");
        patterns.push_back(L"www.fb.watch");
        return patterns;
    }
    if (e == L"googlevideo.com") {
        patterns.push_back(L"kw:googlevideo.com");         
        patterns.push_back(L"kw:r?.---sn-*.googlevideo.com"); 
        return patterns;
    }
    if (e == L"doubleclick.net") {
        patterns.push_back(L"doubleclick.net");
        patterns.push_back(L"www.doubleclick.net");
        patterns.push_back(L"ad.doubleclick.net");
        patterns.push_back(L"cm.doubleclick.net");
        patterns.push_back(L"stats.g.doubleclick.net");
        return patterns;
    }
    if (e == L"googleadservices.com") {
        patterns.push_back(L"googleadservices.com");
        patterns.push_back(L"www.googleadservices.com");
        patterns.push_back(L"pagead2.googlesyndication.com");
        patterns.push_back(L"adservice.google.com");
        patterns.push_back(L"kw:youtube.com/pagead");
        patterns.push_back(L"kw:youtube.com/api/stats/ads");
        return patterns;
    }
    if (e == L"youtube.com/pagead") {
        patterns.push_back(L"kw:youtube.com/pagead");
        patterns.push_back(L"kw:youtube.com/api/stats/ads");
        patterns.push_back(L"kw:youtube.com/youtubei/v1/log_event");
        return patterns;
    }

    patterns.push_back(e);
    patterns.push_back(L"www." + e);

    if (e == L"youtube.com") {
        patterns.push_back(L"m.youtube.com");
        patterns.push_back(L"music.youtube.com");
        patterns.push_back(L"youtu.be");
        patterns.push_back(L"yt3.ggpht.com");
        patterns.push_back(L"i.ytimg.com");
        patterns.push_back(L"s.ytimg.com");
        patterns.push_back(L"youtubei.googleapis.com");
    }
    else if (e == L"facebook.com") {
        patterns.push_back(L"m.facebook.com");
        patterns.push_back(L"l.facebook.com");
        patterns.push_back(L"static.xx.fbcdn.net");
        patterns.push_back(L"graph.facebook.com");
        patterns.push_back(L"connect.facebook.net");
        patterns.push_back(L"edge-chat.facebook.com");
    }
    else if (e == L"instagram.com") {
        patterns.push_back(L"i.instagram.com");
        patterns.push_back(L"graph.instagram.com");
        patterns.push_back(L"cdninstagram.com");
        patterns.push_back(L"scontent.cdninstagram.com");
    }
    else if (e == L"twitter.com" || e == L"x.com") {
        patterns.push_back(L"x.com");
        patterns.push_back(L"www.x.com");
        patterns.push_back(L"twitter.com");
        patterns.push_back(L"www.twitter.com");
        patterns.push_back(L"t.co");
        patterns.push_back(L"api.twitter.com");
        patterns.push_back(L"abs.twimg.com");
        patterns.push_back(L"pbs.twimg.com");
    }
    else if (e == L"tiktok.com") {
        patterns.push_back(L"vm.tiktok.com");
        patterns.push_back(L"m.tiktok.com");
        patterns.push_back(L"api.tiktokv.com");
        patterns.push_back(L"api16-normal-c-useast1a.tiktokv.com");
        patterns.push_back(L"lf16-cdn-tos.tiktokcdn.com");
        patterns.push_back(L"sf16-website-login.neutral.ttwstatic.com");
        patterns.push_back(L"mon.tiktok.com");
    }
    else if (e == L"reddit.com") {
        patterns.push_back(L"old.reddit.com");
        patterns.push_back(L"new.reddit.com");
        patterns.push_back(L"api.reddit.com");
        patterns.push_back(L"oauth.reddit.com");
        patterns.push_back(L"i.redd.it");
        patterns.push_back(L"v.redd.it");
        patterns.push_back(L"www.redditstatic.com");
    }

    return patterns;
}

static void ApplyHostsFileBlocking(const vector<wstring>& patterns, bool block) {
    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    wstring hostsPath = wstring(sysDir) + L"\\drivers\\etc\\hosts";
    
    wifstream fin(hostsPath);
    fin.imbue(locale(fin.getloc(), new codecvt_utf8<wchar_t>));
    wstring content;
    if (fin) {
        wstring ln;
        while (getline(fin, ln)) content += ln + L"\n";
        fin.close();
    }

    for (const auto& pat : patterns) {
        if (pat.substr(0, 3) == L"kw:") continue; 

        wstring blockLine = L"0.0.0.0 " + pat;
        wstring fullLine = blockLine + L" # RasFocus";

        if (block) {
            if (content.find(blockLine) == wstring::npos)
                content += fullLine + L"\n";
        } else {
            wstring newContent;
            size_t pos = 0;
            while (pos <= content.size()) {
                size_t nl = content.find(L'\n', pos);
                wstring line = (nl == wstring::npos) ? content.substr(pos) : content.substr(pos, nl - pos);
                if (line.find(blockLine) == wstring::npos) newContent += line + L"\n";
                if (nl == wstring::npos) break;
                pos = nl + 1;
            }
            content = newContent;
        }
    }

    wofstream fout(hostsPath);
    fout.imbue(locale(fout.getloc(), new codecvt_utf8<wchar_t>));
    if (fout) { fout << content; fout.close(); }

    system("ipconfig /flushdns > nul 2>&1");
}

static void ApplyPACFileBlocking(const vector<wstring>& keywords, bool block) {
    wchar_t appData[MAX_PATH];
    if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) return;
    wstring pacPath = wstring(appData) + L"\\RasFocus\\block.pac";
    wstring pacDir = wstring(appData) + L"\\RasFocus";
    CreateDirectoryW(pacDir.c_str(), NULL);

    wifstream fin(pacPath);
    fin.imbue(locale(fin.getloc(), new codecvt_utf8<wchar_t>));
    wstring content;
    if (fin) {
        wstring ln;
        while (getline(fin, ln)) content += ln + L"\n";
        fin.close();
    }

    vector<wstring> allKw;
    bool blockAllInternet = false;

    for (const auto& p : g_profiles) {
        if (!p.isActive) continue;
        if (p.blockInternet) blockAllInternet = true; 
        
        for (const auto& w : p.blockedWebsites) {
            auto pats = GetAllBlockPatterns(w.name);
            for (const auto& pt : pats) {
                if (pt.substr(0, 3) == L"kw:") allKw.push_back(pt.substr(3));
            }
        }
        for (const auto& k : p.blockedKeywords) {
            allKw.push_back(k.name);
        }
    }

    wstring pac = L"function FindProxyForURL(url, host) {\n";
    if (blockAllInternet) {
        pac += L"  return \"PROXY 127.0.0.1:1\";\n";
    } else {
        for (const auto& kw : allKw) {
            pac += L"  if (url.indexOf(\"" + kw + L"\") !== -1) return \"PROXY 127.0.0.1:1\";\n";
        }
    }
    pac += L"  return \"DIRECT\";\n}\n";

    wofstream fout(pacPath);
    fout.imbue(locale(fout.getloc(), new codecvt_utf8<wchar_t>));
    if (fout) { fout << pac; fout.close(); }

    if (block && (!allKw.empty() || blockAllInternet)) {
        string pacUrl = "file://" + string(pacPath.begin(), pacPath.end());
        string cmd = "reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\" /v AutoConfigURL /t REG_SZ /d \"" + pacUrl + "\" /f > nul 2>&1";
        system(cmd.c_str());
        system("reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\" /v ProxyEnable /t REG_DWORD /d 0 /f > nul 2>&1");
        
        InternetSetOptionA(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0);
        InternetSetOptionA(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
    } else if (allKw.empty() && !blockAllInternet) {
        system("reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\" /v AutoConfigURL /f > nul 2>&1");
        
        InternetSetOptionA(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0);
        InternetSetOptionA(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
    }
}

// ─── App Killing Helper ───────────────────────────────────────────────────
static void KillBlockedApps(const vector<SchBlockItem>& apps) {
    if (apps.empty()) return;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            wstring procName = pe.szExeFile;
            wstring procLower = procName;
            transform(procLower.begin(), procLower.end(), procLower.begin(), ::towlower);

            for (const auto& app : apps) {
                wstring appLower = app.name;
                transform(appLower.begin(), appLower.end(), appLower.begin(), ::towlower);

                if (procLower == appLower) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        TerminateProcess(hProc, 1);
                        CloseHandle(hProc);
                    }
                    break;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

void ApplyProfileBlocking(int profileIdx, bool enable) {
    if (profileIdx < 0 || profileIdx >= (int)g_profiles.size()) return;
    const auto& p = g_profiles[profileIdx];

    vector<wstring> allPatterns;
    for (const auto& w : p.blockedWebsites) {
        auto pats = GetAllBlockPatterns(w.name);
        for (const auto& pt : pats) allPatterns.push_back(pt);
    }
    for (const auto& k : p.blockedKeywords) {
        allPatterns.push_back(L"kw:" + k.name);
    }

    ApplyHostsFileBlocking(allPatterns, enable);
    ApplyPACFileBlocking(allPatterns, enable);

    if (enable && !p.blockedApps.empty()) {
        KillBlockedApps(p.blockedApps);
    }

    if (p.blockAdult) {
        AdultBlock_ApplyForSchedule(enable);
    }
}

// ==========================================
// --- HISTORY & SAVE/LOAD SYSTEM ---
// ==========================================
void LogHistoryToHiddenFolderSch(wstring action) {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        wstring historyDir = wstring(path) + L"\\RasFocus\\History";
        CreateDirectoryW(historyDir.c_str(), NULL);
        SetFileAttributesW(historyDir.c_str(), FILE_ATTRIBUTE_HIDDEN);

        wstring logFile = historyDir + L"\\schedule_activity_log.txt";
        string narrowPathOut(logFile.begin(), logFile.end());
        ofstream out(narrowPathOut.c_str(), ios::app);
        
        time_t now = std::time(0);
        string dt = std::ctime(&now);
        dt.pop_back(); 

        string narrowAction(action.begin(), action.end());
        if(out) out << "[" << dt << "] " << narrowAction << "\n";
    }
}

static wstring GetSchSavePath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        wstring fullPath = wstring(path) + L"\\RasFocus";
        CreateDirectoryW(fullPath.c_str(), NULL);
        return fullPath + L"\\custom_profiles_v2.dat";
    }
    return L"";
}

static void SaveProfiles() {
    wstring path = GetSchSavePath();
    string nPath(path.begin(), path.end());
    wofstream out(nPath);
    out.imbue(locale(out.getloc(), new codecvt_utf8<wchar_t>));
    if (!out) return;

    out << g_profiles.size() << L"\n";
    for (const auto& p : g_profiles) {
        out << p.profileName << L"\n";
        out << p.isActive << L"\n";
        out << p.lockMode << L"\n";
        out << p.lockEndTime << L"\n";
        out << p.parentsPassword << L"\n";
        
        for(int i=0; i<7; i++) out << p.activeDays[i] << L" ";
        out << L"\n" << p.startHour << L" " << p.startMin << L" " << p.endHour << L" " << p.endMin << L"\n";
        out << p.blockInternet << L" " << p.blockAdult << L" " << p.blockUninstall << L"\n";
        
        out << p.blockedWebsites.size() << L"\n";
        for (const auto& w : p.blockedWebsites) out << w.name << L"\n";
        
        out << p.blockedApps.size() << L"\n";
        for (const auto& a : p.blockedApps) out << a.name << L"\n";

        out << p.blockedKeywords.size() << L"\n";
        for (const auto& k : p.blockedKeywords) out << k.name << L"\n";
    }
    out.close();
}

static void LoadProfiles() {
    wstring path = GetSchSavePath();
    string nPath(path.begin(), path.end());
    wifstream in(nPath);
    in.imbue(locale(in.getloc(), new codecvt_utf8<wchar_t>));
    if (!in) {
        FocusProfile defProfile;
        defProfile.profileName = L"Deep Work Session";
        defProfile.blockedWebsites.push_back({L"facebook.com", false});
        defProfile.blockedWebsites.push_back({L"youtube.com", false});
        defProfile.blockedApps.push_back({L"discord.exe", false});
        defProfile.isActive = false;
        defProfile.lockMode = 0;
        defProfile.lockEndTime = 0;
        for(int i=1; i<=5; i++) defProfile.activeDays[i] = true;
        defProfile.startHour = 9; defProfile.startMin = 0;
        defProfile.endHour = 17; defProfile.endMin = 0;
        defProfile.blockInternet = false;
        defProfile.blockAdult = true;
        defProfile.blockUninstall = true;
        g_profiles.push_back(defProfile);
        return;
    }

    size_t pCount = 0; in >> pCount; in.ignore();
    g_profiles.clear();
    for (size_t i = 0; i < pCount; ++i) {
        FocusProfile p;
        getline(in, p.profileName);
        in >> p.isActive >> p.lockMode >> p.lockEndTime; in.ignore();
        getline(in, p.parentsPassword);

        for(int d=0; d<7; d++) in >> p.activeDays[d];
        in >> p.startHour >> p.startMin >> p.endHour >> p.endMin;
        in >> p.blockInternet >> p.blockAdult >> p.blockUninstall;
        in.ignore();

        size_t wCount = 0; in >> wCount; in.ignore();
        for (size_t j = 0; j < wCount; ++j) { wstring w; getline(in, w); p.blockedWebsites.push_back({w, false}); }

        size_t aCount = 0; in >> aCount; in.ignore();
        for (size_t j = 0; j < aCount; ++j) { wstring a; getline(in, a); p.blockedApps.push_back({a, false}); }

        size_t kCount = 0; 
        if (in >> kCount) {
            in.ignore();
            for (size_t j = 0; j < kCount; ++j) { wstring k; getline(in, k); p.blockedKeywords.push_back({k, false}); }
        }
        g_profiles.push_back(p);
    }
    in.close();
}


// ==========================================
// 🟢 BACKGROUND OBSERVER THREAD
// ==========================================
void ScheduleObserverThread() {
    while (true) {
        Sleep(1500);
        
        if (!isSchDataLoaded) continue;

        bool profilesChanged = false;

        g_schMutex.lock();

        time_t t = std::time(nullptr);
        tm* now = std::localtime(&t);
        int currentDay = now->tm_wday; 
        int currentTotalMins = now->tm_hour * 60 + now->tm_min;

        for (size_t i = 0; i < g_profiles.size(); ++i) {
            auto& p = g_profiles[i];

            // 1. Timer Expiration Check
            if (p.isActive && p.lockMode == 0 && p.lockEndTime > 0 && t >= p.lockEndTime) {
                p.isActive = false;
                p.lockEndTime = 0;
                ApplyProfileBlocking(i, false);
                profilesChanged = true;
            }

            // 2. Schedule Auto Start/Stop Check
            bool hasSchedule = false;
            for (int d = 0; d < 7; d++) { if (p.activeDays[d]) hasSchedule = true; }

            if (hasSchedule && p.lockMode == 0 && p.lockEndTime == 0) {
                int startTotalMins = p.startHour * 60 + p.startMin;
                int endTotalMins = p.endHour * 60 + p.endMin;

                bool shouldBeActive = false;
                if (startTotalMins <= endTotalMins) {
                    if (p.activeDays[currentDay]) {
                        shouldBeActive = (currentTotalMins >= startTotalMins && currentTotalMins < endTotalMins);
                    }
                } else {
                    int prevDay = (currentDay + 6) % 7;
                    if (p.activeDays[currentDay] && currentTotalMins >= startTotalMins)
                        shouldBeActive = true;
                    if (p.activeDays[prevDay] && currentTotalMins < endTotalMins)
                        shouldBeActive = true;
                }

                if (shouldBeActive && !p.isActive) {
                    p.isActive = true;
                    ApplyProfileBlocking(i, true);
                    profilesChanged = true;
                } else if (!shouldBeActive && p.isActive) {
                    p.isActive = false;
                    ApplyProfileBlocking(i, false);
                    profilesChanged = true;
                }
            }

            // 3. Continuous App Killing
            if (p.isActive && !p.blockedApps.empty()) {
                KillBlockedApps(p.blockedApps);
            }
        }

        if (profilesChanged) {
            SaveProfiles();
            if (hParentWnd) {
                InvalidateRect(hParentWnd, NULL, FALSE);
            }
        }

        g_schMutex.unlock();
    }
}


// ==========================================
// --- DRAWING LOGIC ---
// ==========================================
void DrawScheduleBlocksTab(Graphics& g, float x, float y, float w, float h) {
    if (!isSchDataLoaded) { 
        LoadProfiles(); 
        isSchDataLoaded = true; 
        
        if (!isSchThreadRunning) {
            std::thread(ScheduleObserverThread).detach();
            isSchThreadRunning = true;
        }
    }
    
    std::lock_guard<std::mutex> lock(g_schMutex);
    
    s_cx = x; s_cy = y; s_cw = w; s_ch = h;
    
    sch_cScroll += (sch_tScroll - sch_cScroll) * 0.12f;

    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff, 24, FontStyleBold, UnitPixel);
    Font fCardTitle(&ff, 18, FontStyleBold, UnitPixel);
    Font fNorm(&ff, 15, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 15, FontStyleBold, UnitPixel);
    Font fSmall(&ff, 13, FontStyleRegular, UnitPixel);
    Font fSmallBold(&ff, 12, FontStyleBold, UnitPixel);
    
    FontFamily ffi(L"Segoe MDL2 Assets");
    Font fIcon(&ffi, 20, FontStyleRegular, UnitPixel);
    Font fSmallIcon(&ffi, 14, FontStyleRegular, UnitPixel);

    SolidBrush bDark(ClrDark); SolidBrush bWhite(ClrWhite); SolidBrush bGray(ClrGrayText);
    SolidBrush bTeal(ClrTeal); SolidBrush bRed(ClrRed); SolidBrush bGreen(ClrGreen);
    SolidBrush bBgHover(ClrBgHover); SolidBrush bTealHover(ClrTealHover);
    SolidBrush bBg(ClrBg);
    
    Pen pThin(Color(255, 230, 235, 240), 1.5f);
    Pen pTeal(ClrTeal, 2.0f);

    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fTL; fTL.SetAlignment(StringAlignmentNear); fTL.SetLineAlignment(StringAlignmentNear);

    // --- MAIN VIEW ---
    g.DrawString(L"Focus Profiles", -1, &fTitle, RectF(x + 20, y + 20, 300, 35), &fL, &bDark);
    g.DrawString(L"Create dedicated schedules with advanced app, web & internet lock.", -1, &fNorm, RectF(x + 20, y + 60, 600, 20), &fL, &bGray);

    RectF addBtnRect(x + w - 220, y + 20, 200, 40);
    GraphicsPath* aP = GetSchRoundRectPath(addBtnRect, 4);
    SolidBrush aBr(hAddProfileBtn ? ClrTealHover : ClrTeal);
    g.FillPath(&aBr, aP); delete aP;
    g.DrawString(L"+ Add Blocking Profile", -1, &fBold, addBtnRect, &fC, &bWhite);

    float cardW = (w - 60.0f) / 2.0f;
    float cardH = 170.0f;
    float startX = x + 20.0f;
    float startY = y + 100.0f - sch_cScroll;

    Region oldClip; g.GetClip(&oldClip);
    g.SetClip(RectF(x, y + 95.0f, w, h - 95.0f));

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        float cX = startX + (i % 2) * (cardW + 20.0f);
        float cY = startY + (i / 2) * (cardH + 20.0f);

        if (cY > y + h || cY + cardH < y + 90.0f) continue; 
        
        RectF cardRect(cX, cY, cardW, cardH);
        GraphicsPath* cP = GetSchRoundRectPath(cardRect, 6);
        g.FillPath(&bWhite, cP); 
        g.DrawPath(g_profiles[i].isActive ? &pTeal : &pThin, cP);
        delete cP;

        g.DrawString(L"\xE82D", -1, &fIcon, RectF(cX + 15, cY + 15, 30, 30), &fL, g_profiles[i].isActive ? &bTeal : &bGray); 
        g.DrawString(g_profiles[i].profileName.c_str(), -1, &fCardTitle, RectF(cX + 50, cY + 15, cardW - 60, 30), &fL, &bDark);
        
        wstring statStr = L"Blocked: " + to_wstring(g_profiles[i].blockedWebsites.size()) + L" Web, " + to_wstring(g_profiles[i].blockedApps.size()) + L" App, " + to_wstring(g_profiles[i].blockedKeywords.size()) + L" Key";
        g.DrawString(statStr.c_str(), -1, &fNorm, RectF(cX + 15, cY + 50, cardW - 30, 20), &fL, &bGray);

        wstring modeName = L"Mode: Self Control";
        if (g_profiles[i].lockMode == 1) modeName = L"Mode: Parents Control";
        if (g_profiles[i].lockMode == 2) modeName = L"Mode: Long Text Unlock";
        g.DrawString(modeName.c_str(), -1, &fSmall, RectF(cX + 15, cY + 75, cardW - 30, 20), &fL, &bTeal);

        RectF togRect(cX + 15, cY + 115, 50, 26);
        GraphicsPath* tp = GetSchRoundRectPath(togRect, 13);
        SolidBrush tBg(g_profiles[i].isActive ? ClrGreen : ClrGrayText);
        g.FillPath(&tBg, tp); delete tp;
        
        float knobX = g_profiles[i].isActive ? (cX + 15.0f + 50.0f - 24.0f) : (cX + 15.0f + 2.0f);
        g.FillEllipse(&bWhite, knobX, cY + 115.0f + 2.0f, 22.0f, 22.0f);

        wstring toggleTxt = g_profiles[i].isActive ? L"Active" : L"Inactive";
        if (g_profiles[i].isActive && g_profiles[i].lockMode == 0 && g_profiles[i].lockEndTime > 0) toggleTxt = L"Locked (Timer)";
        else if (g_profiles[i].isActive && g_profiles[i].lockMode == 0 && g_profiles[i].lockEndTime == 0) toggleTxt = L"Locked (Auto)";
        g.DrawString(toggleTxt.c_str(), -1, &fBold, RectF(cX + 75, cY + 115, 100, 26), &fL, g_profiles[i].isActive ? &bTeal : &bDark);

        RectF editRect(cX + cardW - 130, cY + 115, 60, 30);
        GraphicsPath* ep = GetSchRoundRectPath(editRect, 4);
        if (g_profiles[i].isActive) {
            g.FillPath(&bGray, ep); g.DrawPath(&pThin, ep); delete ep;
            g.DrawString(L"Edit", -1, &fBold, editRect, &fC, &bWhite);
        } else {
            SolidBrush eBr(g_profiles[i].hEdit ? ClrBgHover : ClrBg);
            g.FillPath(&eBr, ep); g.DrawPath(&pThin, ep); delete ep;
            g.DrawString(L"Edit", -1, &fBold, editRect, &fC, &bDark);
        }

        RectF delRect(cX + cardW - 60, cY + 115, 45, 30);
        GraphicsPath* dp = GetSchRoundRectPath(delRect, 4);
        if (g_profiles[i].isActive) {
            g.FillPath(&bGray, dp); g.DrawPath(&pThin, dp); delete dp;
            g.DrawString(L"\xE74D", -1, &fIcon, delRect, &fC, &bWhite);
        } else {
            SolidBrush dBr(g_profiles[i].hDel ? ClrRed : ClrWhite);
            g.FillPath(&dBr, dp); g.DrawPath(&pThin, dp); delete dp;
            g.DrawString(L"\xE74D", -1, &fIcon, delRect, &fC, g_profiles[i].hDel ? &bWhite : &bRed);
        }
    }
    g.SetClip(&oldClip);

    // --- EDIT OVERLAY ---
    if (editingProfileIdx != -1) {
        SolidBrush bgOver(ClrOverlay);
        g.FillRectangle(&bgOver, x, y, w, h);

        float ovW = w - 40.0f;
        float ovH = h - 40.0f;
        float ovX = x + 20.0f;
        float ovY = y + 20.0f;

        RectF ovRect(ovX, ovY, ovW, ovH);
        GraphicsPath* oP = GetSchRoundRectPath(ovRect, 8);
        g.FillPath(&bBg, oP); g.DrawPath(&pThin, oP); delete oP;

        wstring titleTxt = (editingProfileIdx == -2) ? L"Create New Schedule Profile" : L"Edit Schedule Profile";
        g.DrawString(titleTxt.c_str(), -1, &fCardTitle, RectF(ovX + 25, ovY + 15, 300, 25), &fL, &bDark);

        // Sub Tabs
        wstring tabNames[3] = {L"Basic & Time", L"Quick Settings", L"Custom Lists"};
        float tX = ovX + 25;
        float tY = ovY + 45;
        for (int i = 0; i < 3; i++) {
            g_ehb.subTabRects[i] = RectF(tX, tY, 130, 28);
            GraphicsPath* tabP = GetSchRoundRectPath(g_ehb.subTabRects[i], 14);
            if (s_activeSubTab == i) {
                g.FillPath(&bTeal, tabP);
                g.DrawString(tabNames[i].c_str(), -1, &fSmallBold, g_ehb.subTabRects[i], &fC, &bWhite);
            } else {
                SolidBrush hovBr(g_ehb.hSubTab == i ? ClrBgHover : Color(255, 235, 238, 242));
                g.FillPath(&hovBr, tabP);
                g.DrawString(tabNames[i].c_str(), -1, &fSmallBold, g_ehb.subTabRects[i], &fC, &bDark);
            }
            delete tabP;
            tX += 140;
        }

        float contentY = ovY + 95.0f;
        g.DrawLine(&pThin, ovX, contentY - 15.0f, ovX + ovW, contentY - 15.0f);

        // Nav buttons
        g.DrawLine(&pThin, ovX, ovY + ovH - 65, ovX + ovW, ovY + ovH - 65);
        if (s_activeSubTab > 0) {
            g_ehb.backBtn = RectF(ovX + 25, ovY + ovH - 50, 90, 35);
            GraphicsPath* bbp = GetSchRoundRectPath(g_ehb.backBtn, 4);
            SolidBrush bbBr(g_ehb.hBack ? ClrBgHover : ClrWhite);
            g.FillPath(&bbBr, bbp); g.DrawPath(&pThin, bbp); delete bbp;
            g.DrawString(L"< Back", -1, &fBold, g_ehb.backBtn, &fC, &bDark);
        } else { g_ehb.backBtn = RectF(); }

        if (s_activeSubTab < 2) {
            g_ehb.nextBtn = RectF(ovX + 125, ovY + ovH - 50, 90, 35);
            GraphicsPath* nbp = GetSchRoundRectPath(g_ehb.nextBtn, 4);
            SolidBrush nbBr(g_ehb.hNext ? ClrBgHover : ClrWhite);
            g.FillPath(&nbBr, nbp); g.DrawPath(&pThin, nbp); delete nbp;
            g.DrawString(L"Next >", -1, &fBold, g_ehb.nextBtn, &fC, &bDark);
        } else { g_ehb.nextBtn = RectF(); }

        g_ehb.saveBtn = RectF(ovX + ovW - 140, ovY + ovH - 50, 110, 35);
        GraphicsPath* svp = GetSchRoundRectPath(g_ehb.saveBtn, 4);
        SolidBrush svBr(g_ehb.hSave ? ClrTealHover : ClrTeal);
        g.FillPath(&svBr, svp); delete svp;
        g.DrawString(L"Save Profile", -1, &fBold, g_ehb.saveBtn, &fC, &bWhite);

        g_ehb.cancelBtn = RectF(ovX + ovW - 250, ovY + ovH - 50, 100, 35);
        GraphicsPath* cvp = GetSchRoundRectPath(g_ehb.cancelBtn, 4);
        SolidBrush cvBr(g_ehb.hCancel ? ClrBgHover : ClrWhite);
        g.FillPath(&cvBr, cvp); g.DrawPath(&pThin, cvp); delete cvp;
        g.DrawString(L"Cancel", -1, &fBold, g_ehb.cancelBtn, &fC, &bDark);

        float cardX = ovX + 25;
        float cardW_inner = ovW - 50; 

        // ================== TAB 0: BASIC & TIME ==================
        if (s_activeSubTab == 0) {
            RectF c1Rect(cardX, contentY, cardW_inner, 75);
            GraphicsPath* c1P = GetSchRoundRectPath(c1Rect, 6);
            g.FillPath(&bWhite, c1P); g.DrawPath(&pThin, c1P); delete c1P;
            
            g.DrawString(L"General Information", -1, &fBold, RectF(cardX + 15, contentY + 10, 200, 20), &fL, &bDark);
            g.DrawString(L"Profile Name:", -1, &fNorm, RectF(cardX + 15, contentY + 35, 100, 30), &fL, &bGray);
            
            g_ehb.nameInp = RectF(cardX + 115, contentY + 32, 200, 32);
            GraphicsPath* np = GetSchRoundRectPath(g_ehb.nameInp, 4);
            g.FillPath(activeInput == 1 ? &bWhite : &bBg, np); 
            g.DrawPath(activeInput == 1 ? &pTeal : &pThin, np); delete np;
            
            if(inpProfileName.empty() && activeInput != 1) g.DrawString(L"e.g. Study Time", -1, &fNorm, g_ehb.nameInp, &fC, &bGray);
            else {
                g.DrawString(inpProfileName.c_str(), -1, &fNorm, RectF(g_ehb.nameInp.X+8, g_ehb.nameInp.Y, g_ehb.nameInp.Width, g_ehb.nameInp.Height), &fL, &bDark);
                if(activeInput == 1 && (GetTickCount()/500)%2==0) {
                    Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(inpProfileName.c_str(), -1, &fNorm, PointF(0,0), &bR);
                    g.FillRectangle(&bDark, g_ehb.nameInp.X+10+(inpProfileName.empty()?0:bR.Width), g_ehb.nameInp.Y+6, 1.5f, 20.0f);
                }
            }

            g.DrawString(L"Lock Mode:", -1, &fNorm, RectF(cardX + 340, contentY + 35, 80, 30), &fL, &bGray);
            g_ehb.modeDrop = RectF(cardX + 420, contentY + 32, 180, 32);
            GraphicsPath* mdp = GetSchRoundRectPath(g_ehb.modeDrop, 4);
            SolidBrush dropBg(hoverSchModeDropdown ? ClrWhite : ClrBg);
            g.FillPath(&dropBg, mdp); g.DrawPath(&pThin, mdp); delete mdp;
            wstring curModeTxt = (tempLockMode == 1) ? L"Parents Control" : ((tempLockMode == 2) ? L"Long Text Unlock" : L"Self Control");
            g.DrawString(curModeTxt.c_str(), -1, &fNorm, RectF(g_ehb.modeDrop.X+8, g_ehb.modeDrop.Y, g_ehb.modeDrop.Width-25, g_ehb.modeDrop.Height), &fL, &bDark);
            g.DrawString(L"\xE70D", -1, &fSmallIcon, RectF(g_ehb.modeDrop.X+g_ehb.modeDrop.Width-25, g_ehb.modeDrop.Y, 25, g_ehb.modeDrop.Height), &fC, &bGray);
            
            contentY += 90;

            // Schedule Settings Card
            RectF c2Rect(cardX, contentY, cardW_inner, 100);
            GraphicsPath* c2P = GetSchRoundRectPath(c2Rect, 6);
            g.FillPath(&bWhite, c2P); g.DrawPath(&pThin, c2P); delete c2P;
            
            g.DrawString(L"Schedule Settings", -1, &fBold, RectF(cardX + 15, contentY + 10, 200, 20), &fL, &bDark);
            g.DrawString(L"Active Days:", -1, &fSmallBold, RectF(cardX + 15, contentY + 35, 150, 20), &fL, &bGray);
            wstring dLabels[] = {L"S", L"M", L"T", L"W", L"T", L"F", L"S"};
            for(int d=0; d<7; d++) {
                g_ehb.days[d] = RectF(cardX + 15 + (d * 34), contentY + 55, 30, 30);
                GraphicsPath* dP = GetSchRoundRectPath(g_ehb.days[d], 15);
                SolidBrush dBr(editDays[d] ? ClrTeal : (g_ehb.hDay == d ? ClrWhite : ClrBg));
                g.FillPath(&dBr, dP); g.DrawPath(editDays[d] ? &pTeal : &pThin, dP); delete dP;
                g.DrawString(dLabels[d].c_str(), -1, &fBold, g_ehb.days[d], &fC, editDays[d] ? &bWhite : &bDark);
            }

            g.DrawString(L"Session Time:", -1, &fSmallBold, RectF(cardX + 280, contentY + 35, 120, 20), &fL, &bGray);

            auto DrawModernTimeBox = [&](float tx, float ty, const wstring& lbl, int h, int m, RectF& hBox, RectF& mBox, RectF& ampmBtn, bool hH, bool hM, bool hAmPm) {
                g.DrawString(lbl.c_str(), -1, &fSmall, RectF(tx, ty, 40, 30), &fC, &bGray);

                int dispH = h % 12; if (dispH == 0) dispH = 12;
                wstring ampmStr = (h >= 12) ? L"PM" : L"AM";

                hBox = RectF(tx + 45, ty, 35, 30);
                GraphicsPath hp; AddRoundedRectPath(hp, hBox.X, hBox.Y, hBox.Width, hBox.Height, 6);
                SolidBrush hbBr(hH ? ClrBgHover : ClrBg);
                g.FillPath(&hbBr, &hp); g.DrawPath(&pThin, &hp);
                g.DrawString((dispH < 10 ? L"0" + to_wstring(dispH) : to_wstring(dispH)).c_str(), -1, &fBold, hBox, &fC, &bDark);

                g.DrawString(L":", -1, &fBold, RectF(tx + 80, ty, 10, 30), &fC, &bDark);

                mBox = RectF(tx + 90, ty, 35, 30);
                GraphicsPath mp; AddRoundedRectPath(mp, mBox.X, mBox.Y, mBox.Width, mBox.Height, 6);
                SolidBrush mbBr(hM ? ClrBgHover : ClrBg);
                g.FillPath(&mbBr, &mp); g.DrawPath(&pThin, &mp);
                g.DrawString((m < 10 ? L"0" + to_wstring(m) : to_wstring(m)).c_str(), -1, &fBold, mBox, &fC, &bDark);

                ampmBtn = RectF(tx + 130, ty, 38, 30);
                GraphicsPath ap; AddRoundedRectPath(ap, ampmBtn.X, ampmBtn.Y, ampmBtn.Width, ampmBtn.Height, 6);
                SolidBrush aBr(hAmPm ? ClrTealHover : ClrBgHover);
                g.FillPath(&aBr, &ap); g.DrawPath(hAmPm ? &pTeal : &pThin, &ap);
                g.DrawString(ampmStr.c_str(), -1, &fBold, ampmBtn, &fC, (h >= 12) ? &bTeal : &bDark);
            };

            DrawModernTimeBox(cardX + 280, contentY + 55, L"Start", editStH, editStM, g_ehb.stH_Box, g_ehb.stM_Box, g_ehb.stAmPm, g_ehb.hStH, g_ehb.hStM, g_ehb.hStAmPm);
            DrawModernTimeBox(cardX + 460, contentY + 55, L"End", editEnH, editEnM, g_ehb.enH_Box, g_ehb.enM_Box, g_ehb.enAmPm, g_ehb.hEnH, g_ehb.hEnM, g_ehb.hEnAmPm);
        }

        // ================== TAB 1: QUICK SETTINGS ==================
        else if (s_activeSubTab == 1) {
            RectF c3Rect(cardX, contentY, cardW_inner, 60);
            GraphicsPath* c3P = GetSchRoundRectPath(c3Rect, 6);
            g.FillPath(&bWhite, c3P); g.DrawPath(&pThin, c3P); delete c3P;
            
            auto DrawCb = [&](RectF& box, float cx, const wstring& label, bool val, bool hov) {
                box = RectF(cx, contentY + 20, 20, 20);
                GraphicsPath* bp = GetSchRoundRectPath(box, 4);
                g.FillPath(val ? &bTeal : (hov ? &bWhite : &bBg), bp); 
                g.DrawPath(&pThin, bp); delete bp;
                if(val) g.DrawString(L"\xE73E", -1, &fSmallIcon, box, &fC, &bWhite);
                g.DrawString(label.c_str(), -1, &fNorm, RectF(cx + 25, contentY + 20, 150, 20), &fL, &bDark);
            };
            DrawCb(g_ehb.togInt, cardX + 15, L"Block Internet entirely", editBlockInt, g_ehb.hTogInt);
            DrawCb(g_ehb.togAdt, cardX + 220, L"Block Adult Content", editBlockAdult, g_ehb.hTogAdt);
            DrawCb(g_ehb.togUni, cardX + 420, L"Block Uninstall / Taskmgr", editBlockUninst, g_ehb.hTogUni);
            
            contentY += 75;

            RectF c4Rect(cardX, contentY, cardW_inner, 75);
            GraphicsPath* c4P = GetSchRoundRectPath(c4Rect, 6);
            g.FillPath(&bWhite, c4P); g.DrawPath(&pThin, c4P); delete c4P;

            g.DrawString(L"Quick Block", -1, &fBold, RectF(cardX + 15, contentY + 10, 100, 20), &fL, &bDark);
            g.DrawString(L"(Works in Chrome, Edge, Firefox, Brave, Opera)", -1, &fSmall, RectF(cardX + 110, contentY + 12, 400, 18), &fL, &bGray);

            float qbX = cardX + 15;
            float qbY = contentY + 35;
            float qbW = 110.0f;
            float qbH = 28.0f;
            float qbGap = 10.0f;

            s_quickBlockRects.resize(s_quickBlocks.size());
            for (size_t qi = 0; qi < s_quickBlocks.size(); ++qi) {
                RectF qbRect(qbX + qi * (qbW + qbGap), qbY, qbW, qbH);
                s_quickBlockRects[qi] = qbRect;

                bool alreadyAdded = false;
                if (editingProfileIdx >= 0) {
                    for (const auto& w : g_profiles[editingProfileIdx].blockedWebsites) {
                        if (!s_quickBlocks[qi].websites.empty() && w.name == s_quickBlocks[qi].websites[0]) {
                            alreadyAdded = true; break;
                        }
                    }
                    if (!alreadyAdded) {
                        for (const auto& k : g_profiles[editingProfileIdx].blockedKeywords) {
                            if (!s_quickBlocks[qi].keywords.empty() && k.name == s_quickBlocks[qi].keywords[0]) {
                                alreadyAdded = true; break;
                            }
                        }
                    }
                }

                GraphicsPath* qp = GetSchRoundRectPath(qbRect, 4);
                if (alreadyAdded) {
                    g.FillPath(&bTeal, qp);
                } else {
                    SolidBrush qbBg(s_quickBlocks[qi].hovered ? ClrWhite : ClrBg);
                    g.FillPath(&qbBg, qp); g.DrawPath(&pThin, qp);
                }
                delete qp;

                SolidBrush* txtClr = alreadyAdded ? &bWhite : &bDark;
                g.DrawString(s_quickBlocks[qi].label.c_str(), -1, &fSmallBold, qbRect, &fC, txtClr);
            }
        }

        // ================== TAB 2: CUSTOM LISTS ==================
        else if (s_activeSubTab == 2) {
            vector<SchBlockItem>* cWebs = nullptr; vector<SchBlockItem>* cApps = nullptr; vector<SchBlockItem>* cKeys = nullptr;
            if(editingProfileIdx >= 0) {
                cWebs = &g_profiles[editingProfileIdx].blockedWebsites;
                cApps = &g_profiles[editingProfileIdx].blockedApps;
                cKeys = &g_profiles[editingProfileIdx].blockedKeywords;
            }

            float listAreaH = (ovY + ovH - 75.0f) - contentY;
            
            RectF cRect(cardX, contentY, cardW_inner, listAreaH);
            GraphicsPath* cP = GetSchRoundRectPath(cRect, 6);
            g.FillPath(&bWhite, cP); g.DrawPath(&pThin, cP); delete cP;

            float colW = (cardW_inner - 30.0f) / 3.0f;

            auto DrawListCol = [&](int colIdx, float colX, const wstring& title, const wstring& ph, wstring& inpStr, int inpIdx, vector<SchBlockItem>* list,
                                   RectF& outInp, RectF* outCombo, RectF& outAdd, bool hovCombo, bool hovAdd, vector<pair<RectF,int>>& outDel) {
                
                g.DrawString(title.c_str(), -1, &fBold, RectF(colX + 10, contentY + 10, colW, 20), &fL, &bDark);
                
                float inpW = colW - (outCombo ? 25 : 0) - 50 - 10;
                outInp = RectF(colX + 10, contentY + 35, inpW, 30);
                GraphicsPath* ip = GetSchRoundRectPath(outInp, 4);
                g.FillPath(activeInput == inpIdx ? &bWhite : &bBg, ip);
                g.DrawPath(activeInput == inpIdx ? &pTeal : &pThin, ip); delete ip;
                
                if(inpStr.empty() && activeInput != inpIdx) g.DrawString(ph.c_str(), -1, &fSmall, RectF(outInp.X+5, outInp.Y, outInp.Width-10, outInp.Height), &fL, &bGray);
                else {
                    g.DrawString(inpStr.c_str(), -1, &fNorm, RectF(outInp.X+5, outInp.Y, outInp.Width-10, outInp.Height), &fL, &bDark);
                    if(activeInput == inpIdx && (GetTickCount()/500)%2==0) {
                        Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(inpStr.c_str(), -1, &fNorm, PointF(0,0), &bR);
                        g.FillRectangle(&bDark, outInp.X+6+(inpStr.empty()?0:bR.Width), outInp.Y+5, 1.5f, 20.0f);
                    }
                }

                float nextX = outInp.X + outInp.Width + 5;
                if(outCombo) {
                    *outCombo = RectF(nextX, contentY + 35, 25, 30);
                    GraphicsPath* cbp = GetSchRoundRectPath(*outCombo, 4);
                    SolidBrush cbBr(hovCombo ? ClrWhite : ClrBg);
                    g.FillPath(&cbBr, cbp); g.DrawPath(&pThin, cbp); delete cbp;
                    g.DrawString(L"\xE70D", -1, &fSmallIcon, *outCombo, &fC, &bDark);
                    nextX += 30;
                }

                outAdd = RectF(nextX, contentY + 35, 45, 30);
                GraphicsPath* ap = GetSchRoundRectPath(outAdd, 4);
                SolidBrush aBr(hovAdd ? ClrTealHover : ClrTeal); g.FillPath(&aBr, ap); delete ap;
                g.DrawString(L"+", -1, &fBold, outAdd, &fC, &bWhite);

                outDel.clear();
                float listStartY = contentY + 75.0f;
                float boxH = listAreaH - 85.0f;
                g_ehb.listAreas[colIdx] = RectF(colX, listStartY, colW, boxH);
                
                GraphicsPath bP; AddRoundedRectPath(bP, colX+5, listStartY, colW-10, boxH, 4);
                g.FillPath(&bBg, &bP); g.DrawPath(&pThin, &bP);

                if(list && !list->empty()) {
                    s_listScrollMax[colIdx] = (std::max)(0.0f, (list->size() * 35.0f) - boxH + 10.0f);
                    s_listScrollC[colIdx] += (s_listScrollT[colIdx] - s_listScrollC[colIdx]) * 0.15f;
                    
                    g.SetClip(g_ehb.listAreas[colIdx]);
                    float itemY = listStartY + 5.0f - s_listScrollC[colIdx];
                    
                    for(size_t i = 0; i < list->size(); ++i) {
                        auto& item = (*list)[i];
                        if (itemY + 30 > listStartY && itemY < listStartY + boxH) {
                            RectF rowR(colX + 10, itemY, colW - 20, 30);
                            g.FillRectangle(&bBgHover, rowR);
                            g.DrawString(item.name.c_str(), -1, &fSmall, RectF(rowR.X+5, rowR.Y, rowR.Width-30, rowR.Height), &fL, &bDark);
                            
                            RectF delR(rowR.X + rowR.Width - 25, rowR.Y + 2.5f, 25, 25);
                            outDel.push_back({delR, (int)i});
                            SolidBrush crBr(item.isHoveredCross ? ClrRed : ClrGrayText);
                            g.DrawString(L"\xE711", -1, &fSmallIcon, delR, &fC, &crBr);
                        }
                        itemY += 35.0f;
                    }
                    g.SetClip(&oldClip);

                    if (s_listScrollMax[colIdx] > 0) {
                        float thumbH = (std::max)(20.0f, boxH * (boxH / (boxH + s_listScrollMax[colIdx])));
                        float thumbY = listStartY + (s_listScrollC[colIdx] / s_listScrollMax[colIdx]) * (boxH - thumbH);
                        RectF thumbR(colX + colW - 9, thumbY, 4, thumbH);
                        GraphicsPath thP; AddRoundedRectPath(thP, thumbR.X, thumbR.Y, thumbR.Width, thumbR.Height, 2);
                        SolidBrush thB(Color(100, 150, 150, 150));
                        g.FillPath(&thB, &thP);
                    }
                } else {
                    s_listScrollMax[colIdx] = 0.0f; s_listScrollT[colIdx] = 0.0f;
                }
            };

            DrawListCol(0, cardX, L"Websites", L"fb.com", inpWeb, 2, cWebs, g_ehb.webInp, &g_ehb.webCombo, g_ehb.addWeb, hoverSchWebCombo, g_ehb.hAddWeb, g_ehb.webDel);
            SolidBrush bSep(Color(255, 235, 238, 242));
            g.FillRectangle(&bSep, cardX + colW + 4, contentY + 10, 2.0f, listAreaH - 20);
            
            DrawListCol(1, cardX + colW + 10, L"Apps", L"vlc.exe", inpApp, 3, cApps, g_ehb.appInp, &g_ehb.appCombo, g_ehb.addApp, hoverSchAppCombo, g_ehb.hAddApp, g_ehb.appDel);
            g.FillRectangle(&bSep, cardX + colW * 2 + 14, contentY + 10, 2.0f, listAreaH - 20);
            
            DrawListCol(2, cardX + colW * 2 + 20, L"Keywords", L"games", inpKey, 4, cKeys, g_ehb.keyInp, nullptr, g_ehb.addKey, false, g_ehb.hAddKey, g_ehb.keyDel);
        }

        if (s_activeSubTab == 0 && isSchModeDropdownOpen) {
            RectF mlR(g_ehb.modeDrop.X, g_ehb.modeDrop.Y + 34, 180, 118);
            GraphicsPath* mlP = GetSchRoundRectPath(mlR, 4);
            g.FillPath(&bWhite, mlP); g.DrawPath(&pThin, mlP); delete mlP;

            g_ehb.modeOpt[0] = RectF(mlR.X+2, mlR.Y+2, 176, 38);
            g_ehb.modeOpt[1] = RectF(mlR.X+2, mlR.Y+40, 176, 38);
            g_ehb.modeOpt[2] = RectF(mlR.X+2, mlR.Y+78, 176, 38);

            SolidBrush o1Br(g_ehb.hOptSelf ? ClrBgHover : ClrWhite); g.FillRectangle(&o1Br, g_ehb.modeOpt[0]);
            g.DrawString(L"Self Control", -1, &fNorm, RectF(mlR.X+10, mlR.Y+2, 160, 38), &fL, &bDark);

            SolidBrush o2Br(g_ehb.hOptParents ? ClrBgHover : ClrWhite); g.FillRectangle(&o2Br, g_ehb.modeOpt[1]);
            g.DrawString(L"Parents Control", -1, &fNorm, RectF(mlR.X+10, mlR.Y+40, 160, 38), &fL, &bDark);

            SolidBrush o3Br(g_ehb.hOptLongText ? ClrBgHover : ClrWhite); g.FillRectangle(&o3Br, g_ehb.modeOpt[2]);
            g.DrawString(L"Long Text Unlock", -1, &fNorm, RectF(mlR.X+10, mlR.Y+78, 160, 38), &fL, &bDark);
        }
        
        auto DrawDynamicDropdown = [&](RectF btnRect, vector<wstring>& opts, vector<RectF>& outOpts, int hovIdx) {
            RectF lR(btnRect.X - 120, btnRect.Y + 32, 145, opts.size() * 30 + 10);
            GraphicsPath* lP = GetSchRoundRectPath(lR, 4);
            g.FillPath(&bWhite, lP); g.DrawPath(&pThin, lP); delete lP;
            outOpts.clear();
            float iY = lR.Y + 5;
            for(size_t i=0; i<opts.size(); ++i) {
                RectF optRect(lR.X+2, iY, lR.Width-4, 30);
                outOpts.push_back(optRect);
                SolidBrush oBr(hovIdx == (int)i ? ClrBgHover : ClrWhite);
                g.FillRectangle(&oBr, optRect);
                g.DrawString(opts[i].c_str(), -1, &fSmall, RectF(lR.X+5, iY, lR.Width-10, 30), &fL, &bDark); iY += 30;
            }
        };

        if (s_activeSubTab == 2) {
            if (isSchWebComboOpen) DrawDynamicDropdown(g_ehb.webCombo, schCommonWebsites, g_ehb.webOpts, hoverSchWebOptIdx);
            if (isSchAppComboOpen) DrawDynamicDropdown(g_ehb.appCombo, schCommonApps, g_ehb.appOpts, hoverSchAppOptIdx);
        }
    }

    // ==========================================
    // FULL SCREEN OVERLAYS
    // ==========================================
    if (s_showTimeOverlay || s_showPassOverlay || s_showTextUnlockOverlay) {
        SolidBrush overlayBg(ClrOverlay);
        g.FillRectangle(&overlayBg, x, y, w, h);

        // ── TIME OVERLAY ──────────────────────────────────────────────────────
        if (s_showTimeOverlay) {
            // Height: always show all 3 rows so layout stays stable
            // Row 1: Months / Days     @ kSpinRowY1   = 80
            // Row 2: Full Day toggle   @ kFullDayRowY = 130
            // Row 3: Hours / Mins      @ kSpinRowY2   = 175  (greyed if fullDay)
            // Row 4: Buttons           @ 235
            float ovW = kTimeOvW;
            float ovH = 295.0f; // fixed height covers all rows comfortably
            float ovX = x + (w - ovW) / 2.0f;
            float ovY = y + (h - ovH) / 2.0f;

            RectF ovRect(ovX, ovY, ovW, ovH);
            GraphicsPath* op = GetSchRoundRectPath(ovRect, 8);
            g.FillPath(&bBg, op); g.DrawPath(&pThin, op); delete op;

            // Title
            g.DrawString(L"SET FOCUS DURATION", -1, &fBold,
                RectF(ovX, ovY + 16, ovW, 26), &fC, &bDark);
            g.DrawString(L"(Self Control Mode)", -1, &fSmall,
                RectF(ovX, ovY + 42, ovW, 20), &fC, &bGray);

            // ── Row 1: Months & Days ────────────────────────────────────────
            float row1Y = ovY + kSpinRowY1;

            g.DrawString(L"Months", -1, &fSmallBold,
                RectF(ovX + kSpinLabelCol1, row1Y, 55, 36), &fL, &bGray);
            DrawSchOverlaySpinner(g,
                ovX + kSpinnerCol1, row1Y,
                to_wstring(s_focusMonths),
                s_hTimeMoM, s_hTimeMoP,
                &fIcon, &fBold);

            g.DrawString(L"Days", -1, &fSmallBold,
                RectF(ovX + kSpinLabelCol2, row1Y, 40, 36), &fL, &bGray);
            DrawSchOverlaySpinner(g,
                ovX + kSpinnerCol2, row1Y,
                to_wstring(s_focusDays),
                s_hTimeDM, s_hTimeDP,
                &fIcon, &fBold);

            // ── Row 2: Full Day checkbox ────────────────────────────────────
            float row2Y = ovY + kFullDayRowY;

            // Separator line above checkbox row
            g.DrawLine(&pThin, ovX + 20, row2Y - 8, ovX + ovW - 20, row2Y - 8);

            // Checkbox
            RectF cbRect(ovX + kSpinLabelCol1, row2Y + 4, 20, 20);
            {
                GraphicsPath* cbP = GetSchRoundRectPath(cbRect, 4);
                SolidBrush cbBg(s_focusFullDay ? ClrTeal : (s_hTimeFullDay ? ClrBgHover : ClrBg));
                g.FillPath(&cbBg, cbP);
                g.DrawPath(s_focusFullDay ? &pTeal : &pThin, cbP);
                delete cbP;
                if (s_focusFullDay)
                    g.DrawString(L"\xE73E", -1, &fSmallIcon, cbRect, &fC, &bWhite);
            }
            g.DrawString(L"Full Day  (ignores Hours & Mins — runs for whole day(s))",
                -1, &fNorm,
                RectF(cbRect.X + 28, row2Y, ovW - cbRect.X - 48, 28),
                &fL, &bDark);

            // ── Row 3: Hours & Mins (greyed/disabled when fullDay) ──────────
            float row3Y = ovY + kSpinRowY2;

            // Determine visual disabled state
            Color clrLabel = s_focusFullDay
                ? Color(255, 200, 200, 200)   // greyed
                : Color(255, 120, 120, 120);  // normal gray

            SolidBrush bLabelH(clrLabel), bLabelM(clrLabel);

            g.DrawString(L"Hours", -1, &fSmallBold,
                RectF(ovX + kSpinLabelCol1, row3Y, 50, 36), &fL, &bLabelH);
            g.DrawString(L"Mins", -1, &fSmallBold,
                RectF(ovX + kSpinLabelCol2, row3Y, 40, 36), &fL, &bLabelM);

            if (!s_focusFullDay) {
                // Draw fully interactive spinners
                DrawSchOverlaySpinner(g,
                    ovX + kSpinnerCol1, row3Y,
                    to_wstring(s_focusHours),
                    s_hTimeHM, s_hTimeHP,
                    &fIcon, &fBold);
                DrawSchOverlaySpinner(g,
                    ovX + kSpinnerCol2, row3Y,
                    to_wstring(s_focusMins),
                    s_hTimeMM, s_hTimeMP,
                    &fIcon, &fBold);
            } else {
                // Draw visually-dimmed static boxes (not interactive)
                auto DrawDimmedSpinner = [&](float cx, float cy, const wstring& val) {
                    float btnW = 32.0f, btnH = 36.0f, valW = 52.0f;
                    Color clrDim(255, 215, 215, 215);
                    Pen pDim(clrDim, 1.5f);

                    RectF minusR(cx, cy, btnW, btnH);
                    RectF valR(cx + btnW + 4, cy, valW, btnH);
                    RectF plusR(cx + btnW + 4 + valW + 4, cy, btnW, btnH);

                    for (auto& r : {minusR, valR, plusR}) {
                        GraphicsPath* rp = GetSchRoundRectPath(r, 5);
                        SolidBrush rb(clrDim);
                        g.FillPath(&rb, rp); g.DrawPath(&pDim, rp); delete rp;
                    }
                    SolidBrush bDimTxt(Color(255, 190, 190, 190));
                    g.DrawString(val.c_str(), -1, &fBold, valR, &fC, &bDimTxt);
                };
                DrawDimmedSpinner(ovX + kSpinnerCol1, row3Y, to_wstring(s_focusHours));
                DrawDimmedSpinner(ovX + kSpinnerCol2, row3Y, to_wstring(s_focusMins));
            }

            // ── Buttons ─────────────────────────────────────────────────────
            float btnY = ovY + 240.0f;
            g.DrawLine(&pThin, ovX + 20, btnY - 8, ovX + ovW - 20, btnY - 8);

            RectF cancelRect(ovX + 60, btnY, 140, 40);
            {
                GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 4);
                SolidBrush cancelBrush(s_hTimeCancel ? ClrBgHover : ClrWhite);
                g.FillPath(&cancelBrush, cp); g.DrawPath(&pThin, cp); delete cp;
                g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fC, &bDark);
            }

            RectF startRect(ovX + 240, btnY, 170, 40);
            {
                GraphicsPath* sp = GetSchRoundRectPath(startRect, 4);
                SolidBrush startBrush(s_hTimeStart ? ClrTealHover : ClrTeal);
                g.FillPath(&startBrush, sp); delete sp;
                g.DrawString(L"Start Profile", -1, &fBold, startRect, &fC, &bWhite);
            }
        }

        // ── PASS OVERLAY ─────────────────────────────────────────────────────
        else if (s_showPassOverlay) {
            float ovW = 500.0f, ovH = 280.0f;
            float ovX = x + (w - ovW) / 2.0f;
            float ovY = y + (h - ovH) / 2.0f;

            RectF ovRect(ovX, ovY, ovW, ovH);
            GraphicsPath* op = GetSchRoundRectPath(ovRect, 8);
            g.FillPath(&bBg, op); g.DrawPath(&pThin, op); delete op;

            wstring titleTxt = s_isStoppingFocus ? L"ENTER PARENTS PASSWORD TO STOP" : L"SET PARENTS PASSWORD";
            g.DrawString(titleTxt.c_str(), -1, &fTitle, RectF(ovX, ovY + 20, ovW, 30), &fC, &bDark);

            RectF passInpRect(ovX + 40, ovY + 80, ovW - 80, 40);
            GraphicsPath* pp = GetSchRoundRectPath(passInpRect, 4);
            g.FillPath(s_isPassInputActive ? &bWhite : &bBg, pp); 
            g.DrawPath(s_isPassInputActive ? &pTeal : &pThin, pp); delete pp;
            
            wstring displayPass = wstring(s_inputPassText.length(), L'*');
            if (s_inputPassText.empty() && !s_isPassInputActive)
                g.DrawString(L"Type password here...", -1, &fNorm, passInpRect, &fC, &bGray);
            else {
                g.DrawString(displayPass.c_str(), -1, &fTitle, RectF(ovX + 50, ovY + 85, ovW - 100, 30), &fL, &bDark);
                if (s_isPassInputActive && (GetTickCount() / 500) % 2 == 0) {
                     Graphics gT(GetDesktopWindow()); RectF bR;
                     gT.MeasureString(displayPass.c_str(), -1, &fTitle, PointF(0,0), &bR);
                     float curX = ovX + 52 + (displayPass.empty()?0:bR.Width);
                     g.FillRectangle(&bDark, curX, ovY + 90, 1.5f, 20.0f);
                }
            }

            RectF cancelRect(ovX + 40, ovY + 150, 140, 40);
            GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 4);
            SolidBrush cancelBrush(s_hPassCancel ? ClrBgHover : ClrWhite);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&pThin, cp); delete cp;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fC, &bDark);

            RectF confRect(ovX + 220, ovY + 150, 160, 40);
            GraphicsPath* sp = GetSchRoundRectPath(confRect, 4);
            SolidBrush confBrush(s_hPassConfirm ? ClrTealHover : ClrTeal);
            g.FillPath(&confBrush, sp); delete sp;
            g.DrawString(L"Confirm", -1, &fBold, confRect, &fC, &bWhite);
        }

        // ── TEXT UNLOCK OVERLAY ───────────────────────────────────────────────
        else if (s_showTextUnlockOverlay) {
            float ovW = 600.0f, ovH = 450.0f;
            float ovX = x + (w - ovW) / 2.0f;
            float ovY = y + (h - ovH) / 2.0f;

            RectF ovRect(ovX, ovY, ovW, ovH);
            GraphicsPath* op = GetSchRoundRectPath(ovRect, 8);
            g.FillPath(&bBg, op); g.DrawPath(&pThin, op); delete op;

            g.DrawString(L"EXACT TEXT UNLOCK MODE", -1, &fTitle, RectF(ovX, ovY + 20, ovW, 30), &fC, &bDark);
            
            RectF targetBox(ovX + 20, ovY + 60, ovW - 40, 110);
            GraphicsPath* tbp = GetSchRoundRectPath(targetBox, 4);
            g.FillPath(&bWhite, tbp); g.DrawPath(&pThin, tbp); delete tbp;
            g.DrawString(s_targetUnlockText.c_str(), -1, &fNorm,
                RectF(targetBox.X+8, targetBox.Y+8, targetBox.Width-16, targetBox.Height-16), &fTL, &bGray);

            RectF typeBox(ovX + 20, ovY + 180, ovW - 40, 160);
            GraphicsPath* tp = GetSchRoundRectPath(typeBox, 4);
            g.FillPath(s_isTypingActive ? &bWhite : &bBg, tp); 
            g.DrawPath(s_isTypingActive ? &pTeal : &pThin, tp); delete tp;
            g.DrawString(s_currentTypingText.c_str(), -1, &fNorm,
                RectF(typeBox.X + 8, typeBox.Y + 8, typeBox.Width - 16, typeBox.Height - 16), &fTL, &bDark);

            RectF cancelRect(ovX + 120, ovY + 380, 140, 40);
            GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 4);
            SolidBrush cancelBrush(s_hTextUnlockCancel ? ClrBgHover : ClrWhite);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&pThin, cp); delete cp;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fC, &bDark);

            RectF confRect(ovX + 280, ovY + 380, 160, 40);
            GraphicsPath* sp = GetSchRoundRectPath(confRect, 4);
            SolidBrush confBrush((s_currentTypingText == s_targetUnlockText)
                ? (s_hTextUnlockConfirm ? ClrTealHover : ClrTeal)
                : ClrDisabled);
            g.FillPath(&confBrush, sp); delete sp;
            g.DrawString(L"Unlock Profile", -1, &fBold, confRect, &fC, &bWhite);
        }
    }
}

// ==========================================
// --- MOUSE MOVE LOGIC ---
// ==========================================
void ProcessScheduleBlocksMouseMove(float x, float y) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    
    hAddProfileBtn = false;
    
    s_hTimeMoM=false; s_hTimeMoP=false; s_hTimeDM=false; s_hTimeDP=false;
    s_hTimeHM=false; s_hTimeHP=false; s_hTimeMM=false; s_hTimeMP=false;
    s_hTimeFullDay=false; // 🟢
    s_hTimeStart=false; s_hTimeCancel=false;
    s_hPassInput=false; s_hPassConfirm=false; s_hPassCancel=false;
    s_hTextUnlockConfirm=false; s_hTextUnlockCancel=false;
    g_ehb.hSubTab = -1;

    if (s_showTimeOverlay) {
        float ovW = kTimeOvW, ovH = 295.0f;
        float ovX = s_cx + (s_cw - ovW) / 2.0f;
        float ovY = s_cy + (s_ch - ovH) / 2.0f;

        float row1Y = ovY + kSpinRowY1;
        float row2Y = ovY + kFullDayRowY;
        float row3Y = ovY + kSpinRowY2;
        float btnY  = ovY + 240.0f;

        // Row 1: Months spinner
        if (SchSpinnerMinusRect(ovX + kSpinnerCol1, row1Y).Contains(x, y)) s_hTimeMoM = true;
        if (SchSpinnerPlusRect (ovX + kSpinnerCol1, row1Y).Contains(x, y)) s_hTimeMoP = true;
        // Row 1: Days spinner
        if (SchSpinnerMinusRect(ovX + kSpinnerCol2, row1Y).Contains(x, y)) s_hTimeDM = true;
        if (SchSpinnerPlusRect (ovX + kSpinnerCol2, row1Y).Contains(x, y)) s_hTimeDP = true;

        // Row 2: Full Day checkbox
        RectF cbRect(ovX + kSpinLabelCol1, row2Y + 4, 20, 20);
        if (cbRect.Contains(x, y)) s_hTimeFullDay = true;

        // Row 3: Hours/Mins (only interactive when not fullDay)
        if (!s_focusFullDay) {
            if (SchSpinnerMinusRect(ovX + kSpinnerCol1, row3Y).Contains(x, y)) s_hTimeHM = true;
            if (SchSpinnerPlusRect (ovX + kSpinnerCol1, row3Y).Contains(x, y)) s_hTimeHP = true;
            if (SchSpinnerMinusRect(ovX + kSpinnerCol2, row3Y).Contains(x, y)) s_hTimeMM = true;
            if (SchSpinnerPlusRect (ovX + kSpinnerCol2, row3Y).Contains(x, y)) s_hTimeMP = true;
        }

        // Buttons
        if (RectF(ovX + 60,  btnY, 140, 40).Contains(x, y)) s_hTimeCancel = true;
        if (RectF(ovX + 240, btnY, 170, 40).Contains(x, y)) s_hTimeStart  = true;
        return;
    }

    if (s_showPassOverlay) {
        float ovW = 500.0f, ovH = 280.0f;
        float ovX = s_cx + (s_cw - ovW) / 2.0f;
        float ovY = s_cy + (s_ch - ovH) / 2.0f;
        if (RectF(ovX + 40, ovY + 80,  ovW - 80, 40).Contains(x, y)) s_hPassInput   = true;
        if (RectF(ovX + 40, ovY + 150, 140, 40).Contains(x, y))       s_hPassCancel  = true;
        if (RectF(ovX + 220,ovY + 150, 160, 40).Contains(x, y))       s_hPassConfirm = true;
        return;
    }

    if (s_showTextUnlockOverlay) {
        float ovW = 600.0f, ovH = 450.0f;
        float ovX = s_cx + (s_cw - ovW) / 2.0f;
        float ovY = s_cy + (s_ch - ovH) / 2.0f;
        if (RectF(ovX + 120, ovY + 380, 140, 40).Contains(x, y)) s_hTextUnlockCancel  = true;
        if (RectF(ovX + 280, ovY + 380, 160, 40).Contains(x, y)) s_hTextUnlockConfirm = true;
        return;
    }

    if (editingProfileIdx != -1) {
        g_ehb.hOptSelf = false; g_ehb.hOptParents = false; g_ehb.hOptLongText = false;
        hoverSchWebOptIdx = -1; hoverSchAppOptIdx = -1;
        g_ehb.hSave = false; g_ehb.hCancel = false; g_ehb.hNext = false; g_ehb.hBack = false;

        for (int i = 0; i < 3; i++) {
            if (g_ehb.subTabRects[i].Contains(x, y)) g_ehb.hSubTab = i;
        }

        if (s_activeSubTab == 0 && isSchModeDropdownOpen) {
            if (g_ehb.modeOpt[0].Contains(x, y)) g_ehb.hOptSelf = true;
            if (g_ehb.modeOpt[1].Contains(x, y)) g_ehb.hOptParents = true;
            if (g_ehb.modeOpt[2].Contains(x, y)) g_ehb.hOptLongText = true;
            return;
        }
        if (s_activeSubTab == 2) {
            if (isSchWebComboOpen) {
                for(size_t i=0; i<g_ehb.webOpts.size(); ++i) { if(g_ehb.webOpts[i].Contains(x, y)) { hoverSchWebOptIdx = i; return; } }
            }
            if (isSchAppComboOpen) {
                for(size_t i=0; i<g_ehb.appOpts.size(); ++i) { if(g_ehb.appOpts[i].Contains(x, y)) { hoverSchAppOptIdx = i; return; } }
            }
        }

        if(g_ehb.saveBtn.Contains(x,y))   g_ehb.hSave   = true;
        if(g_ehb.cancelBtn.Contains(x,y)) g_ehb.hCancel = true;
        if(g_ehb.nextBtn.Contains(x,y))   g_ehb.hNext   = true;
        if(g_ehb.backBtn.Contains(x,y))   g_ehb.hBack   = true;

        g_ehb.hDay = -1;
        g_ehb.hStH=false; g_ehb.hStM=false; g_ehb.hStAmPm=false;
        g_ehb.hEnH=false; g_ehb.hEnM=false; g_ehb.hEnAmPm=false;
        g_ehb.hTogInt=false; g_ehb.hTogAdt=false; g_ehb.hTogUni=false;
        hoverSchModeDropdown=false; hoverSchWebCombo=false; hoverSchAppCombo=false;
        g_ehb.hAddWeb=false; g_ehb.hAddApp=false; g_ehb.hAddKey=false;
        for (auto& qb : s_quickBlocks) qb.hovered = false;
        
        if (editingProfileIdx >= 0) {
            for(auto& it : g_profiles[editingProfileIdx].blockedWebsites)  it.isHoveredCross = false;
            for(auto& it : g_profiles[editingProfileIdx].blockedApps)       it.isHoveredCross = false;
            for(auto& it : g_profiles[editingProfileIdx].blockedKeywords)   it.isHoveredCross = false;
        }

        if (s_activeSubTab == 0) {
            if(g_ehb.modeDrop.Contains(x, y)) hoverSchModeDropdown = true;
            for(int d=0; d<7; d++) { if(g_ehb.days[d].Contains(x,y)) g_ehb.hDay = d; }
            if(g_ehb.stH_Box.Contains(x,y)) g_ehb.hStH    = true;
            if(g_ehb.stM_Box.Contains(x,y)) g_ehb.hStM    = true;
            if(g_ehb.stAmPm.Contains(x,y))  g_ehb.hStAmPm = true;
            if(g_ehb.enH_Box.Contains(x,y)) g_ehb.hEnH    = true;
            if(g_ehb.enM_Box.Contains(x,y)) g_ehb.hEnM    = true;
            if(g_ehb.enAmPm.Contains(x,y))  g_ehb.hEnAmPm = true;
        } 
        else if (s_activeSubTab == 1) {
            if(g_ehb.togInt.Contains(x,y)) g_ehb.hTogInt = true;
            if(g_ehb.togAdt.Contains(x,y)) g_ehb.hTogAdt = true;
            if(g_ehb.togUni.Contains(x,y)) g_ehb.hTogUni = true;
            for (size_t qi = 0; qi < s_quickBlocks.size() && qi < s_quickBlockRects.size(); ++qi) {
                if (s_quickBlockRects[qi].Contains(x, y)) s_quickBlocks[qi].hovered = true;
            }
        } 
        else if (s_activeSubTab == 2) {
            if(g_ehb.webCombo.Contains(x,y)) hoverSchWebCombo = true;
            if(g_ehb.appCombo.Contains(x,y)) hoverSchAppCombo = true;
            if(g_ehb.addWeb.Contains(x,y))   g_ehb.hAddWeb    = true;
            if(g_ehb.addApp.Contains(x,y))   g_ehb.hAddApp    = true;
            if(g_ehb.addKey.Contains(x,y))   g_ehb.hAddKey    = true;

            if (editingProfileIdx >= 0) {
                if (g_ehb.listAreas[0].Contains(x, y))
                    for(auto& p : g_ehb.webDel) if(p.first.Contains(x,y)) g_profiles[editingProfileIdx].blockedWebsites[p.second].isHoveredCross = true;
                if (g_ehb.listAreas[1].Contains(x, y))
                    for(auto& p : g_ehb.appDel) if(p.first.Contains(x,y)) g_profiles[editingProfileIdx].blockedApps[p.second].isHoveredCross = true;
                if (g_ehb.listAreas[2].Contains(x, y))
                    for(auto& p : g_ehb.keyDel) if(p.first.Contains(x,y)) g_profiles[editingProfileIdx].blockedKeywords[p.second].isHoveredCross = true;
            }
        }
        return;
    }

    // Main view
    if (RectF(s_cx + s_cw - 220, s_cy + 20, 200, 40).Contains(x, y)) hAddProfileBtn = true;

    float cardW2 = (s_cw - 60.0f) / 2.0f; float cardH = 170.0f;
    float startX = s_cx + 20.0f; float startY = s_cy + 100.0f - sch_cScroll;

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        float cX = startX + (i % 2) * (cardW2 + 20.0f);
        float cY = startY + (i / 2) * (cardH + 20.0f);
        if (cY > s_cy + s_ch || cY + cardH < s_cy + 90.0f) continue;

        g_profiles[i].hToggle = RectF(cX + 15, cY + 115, 60, 26).Contains(x, y)
                              || RectF(cX + 75, cY + 115, 60, 26).Contains(x, y);
        if (!g_profiles[i].isActive) {
            g_profiles[i].hEdit = RectF(cX + cardW2 - 130, cY + 115, 60, 30).Contains(x, y);
            g_profiles[i].hDel  = RectF(cX + cardW2 - 60,  cY + 115, 45, 30).Contains(x, y);
        } else {
            g_profiles[i].hEdit = false;
            g_profiles[i].hDel  = false;
        }
    }
}

// ==========================================
// --- MOUSE BUTTON DOWN ---
// ==========================================
void ProcessScheduleBlocksMouseDown(float x, float y) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    // main view scrollbar drag start — extend here if needed
}

// ==========================================
// --- MOUSE BUTTON UP ---
// ==========================================
void ProcessScheduleBlocksMouseUp(float x, float y) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    s_scrollbarDragging = false;
}

// ==========================================
// --- MOUSE CLICK ---
// ==========================================
void ProcessScheduleBlocksMouseClick(float x, float y) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    
    // ── TIME OVERLAY CLICKS ───────────────────────────────────────────────
    if (s_showTimeOverlay) {
        // Row 1: Months
        if (s_hTimeMoM && s_focusMonths > 0)  s_focusMonths--;
        if (s_hTimeMoP && s_focusMonths < 12) s_focusMonths++;
        // Row 1: Days
        if (s_hTimeDM && s_focusDays > 0)  s_focusDays--;
        if (s_hTimeDP && s_focusDays < 30) s_focusDays++;

        // Row 2: Full Day toggle
        if (s_hTimeFullDay) {
            s_focusFullDay = !s_focusFullDay;
            // Reset hours/mins when toggled off so user sees sane defaults
            if (!s_focusFullDay && s_focusHours == 0 && s_focusMins == 0)
                s_focusHours = 1;
        }

        // Row 3: Hours/Mins (only when not fullDay)
        if (!s_focusFullDay) {
            if (s_hTimeHM && s_focusHours > 0)  s_focusHours--;
            if (s_hTimeHP && s_focusHours < 23) s_focusHours++;
            if (s_hTimeMM) { s_focusMins -= 5; if (s_focusMins < 0) s_focusMins = 55; }
            if (s_hTimeMP) { s_focusMins = (s_focusMins + 5) % 60; }
        }

        // Buttons
        if (s_hTimeCancel) {
            s_showTimeOverlay = false;
            return;
        }
        if (s_hTimeStart && activeActionProfileIdx >= 0) {
            // Compute lock duration
            time_t duration = 0;
            duration += (time_t)s_focusMonths * 30 * 24 * 3600;
            duration += (time_t)s_focusDays   * 24 * 3600;
            if (s_focusFullDay) {
                // Full day means today/tonight's midnight-to-midnight; treat as 24h per day selected
                // If user set 0 months and 0 days with fullDay, treat as 1 day minimum
                if (s_focusMonths == 0 && s_focusDays == 0)
                    duration = 24 * 3600;
            } else {
                duration += (time_t)s_focusHours * 3600;
                duration += (time_t)s_focusMins  * 60;
            }
            if (duration <= 0) duration = 3600; // safety: at least 1 hour

            g_profiles[activeActionProfileIdx].isActive     = true;
            g_profiles[activeActionProfileIdx].lockEndTime  = std::time(nullptr) + duration;
            ApplyProfileBlocking(activeActionProfileIdx, true);
            s_showTimeOverlay = false;
            LogHistoryToHiddenFolderSch(L"Started Schedule: " + g_profiles[activeActionProfileIdx].profileName);
            SaveProfiles();
        }
        return;
    }

    // ── PASS OVERLAY CLICKS ───────────────────────────────────────────────
    if (s_showPassOverlay) {
        s_isPassInputActive = s_hPassInput || s_isPassInputActive; // keep active unless clicking outside
        if (s_hPassInput) s_isPassInputActive = true;
        if (s_hPassCancel) { s_showPassOverlay = false; s_inputPassText = L""; return; }
        if (s_hPassConfirm && !s_inputPassText.empty() && activeActionProfileIdx >= 0) {
            if (!s_isStoppingFocus) {
                g_profiles[activeActionProfileIdx].parentsPassword = s_inputPassText;
                g_profiles[activeActionProfileIdx].isActive = true;
                ApplyProfileBlocking(activeActionProfileIdx, true);
                LogHistoryToHiddenFolderSch(L"Locked (Parents) Schedule: " + g_profiles[activeActionProfileIdx].profileName);
            } else {
                if (g_profiles[activeActionProfileIdx].parentsPassword == s_inputPassText) {
                    g_profiles[activeActionProfileIdx].isActive = false;
                    ApplyProfileBlocking(activeActionProfileIdx, false);
                    LogHistoryToHiddenFolderSch(L"Unlocked (Parents) Schedule: " + g_profiles[activeActionProfileIdx].profileName);
                }
            }
            s_showPassOverlay = false; s_inputPassText = L"";
            SaveProfiles();
        }
        return;
    }

    // ── TEXT UNLOCK CLICKS ────────────────────────────────────────────────
    if (s_showTextUnlockOverlay) {
        if (s_hTextUnlockCancel) { s_showTextUnlockOverlay = false; return; }
        if (s_hTextUnlockConfirm && s_currentTypingText == s_targetUnlockText && activeActionProfileIdx >= 0) {
            g_profiles[activeActionProfileIdx].isActive = false;
            ApplyProfileBlocking(activeActionProfileIdx, false);
            s_showTextUnlockOverlay = false; s_currentTypingText = L"";
            LogHistoryToHiddenFolderSch(L"Unlocked (Long Text) Schedule: " + g_profiles[activeActionProfileIdx].profileName);
            SaveProfiles();
        }
        return;
    }

    // ── EDIT OVERLAY CLICKS ───────────────────────────────────────────────
    if (editingProfileIdx != -1) {
        for (int i = 0; i < 3; i++) {
            if (g_ehb.subTabRects[i].Contains(x, y)) {
                s_activeSubTab = i;
                activeInput = (i == 0) ? 1 : 0;
                isSchModeDropdownOpen = false; isSchWebComboOpen = false; isSchAppComboOpen = false;
                return;
            }
        }

        if (g_ehb.hNext && s_activeSubTab < 2) { s_activeSubTab++; activeInput = (s_activeSubTab == 0) ? 1 : 0; return; }
        if (g_ehb.hBack && s_activeSubTab > 0) { s_activeSubTab--; activeInput = (s_activeSubTab == 0) ? 1 : 0; return; }

        bool dropdownClosed = false;

        if (s_activeSubTab == 0) {
            if (isSchModeDropdownOpen && !hoverSchModeDropdown && !g_ehb.hOptSelf && !g_ehb.hOptParents && !g_ehb.hOptLongText) {
                isSchModeDropdownOpen = false; dropdownClosed = true;
            } else if (isSchModeDropdownOpen) {
                if (g_ehb.hOptSelf)     tempLockMode = 0;
                if (g_ehb.hOptParents)  tempLockMode = 1;
                if (g_ehb.hOptLongText) tempLockMode = 2;
                isSchModeDropdownOpen = false; return;
            }
        }

        if (s_activeSubTab == 2) {
            if (isSchWebComboOpen && !hoverSchWebCombo && hoverSchWebOptIdx == -1) {
                isSchWebComboOpen = false; dropdownClosed = true;
            } else if (isSchWebComboOpen) {
                if (hoverSchWebOptIdx != -1 && editingProfileIdx >= 0)
                    g_profiles[editingProfileIdx].blockedWebsites.push_back({schCommonWebsites[hoverSchWebOptIdx], false});
                isSchWebComboOpen = false; return;
            }

            if (isSchAppComboOpen && !hoverSchAppCombo && hoverSchAppOptIdx == -1) {
                isSchAppComboOpen = false; dropdownClosed = true;
            } else if (isSchAppComboOpen) {
                if (hoverSchAppOptIdx != -1 && editingProfileIdx >= 0)
                    g_profiles[editingProfileIdx].blockedApps.push_back({schCommonApps[hoverSchAppOptIdx], false});
                isSchAppComboOpen = false; return;
            }
        }

        if (dropdownClosed) return;

        if (g_ehb.hCancel) { editingProfileIdx = -1; return; }
        
        if (g_ehb.hSave) {
            if (inpProfileName.empty()) inpProfileName = L"Custom Profile";
            if (editingProfileIdx == -2) {
                g_profiles.back().profileName = inpProfileName;
            } else if (editingProfileIdx >= 0) {
                g_profiles[editingProfileIdx].profileName = inpProfileName;
            }
            if(editingProfileIdx >= 0) {
                g_profiles[editingProfileIdx].lockMode       = tempLockMode;
                for(int d=0; d<7; d++) g_profiles[editingProfileIdx].activeDays[d] = editDays[d];
                g_profiles[editingProfileIdx].startHour      = editStH;
                g_profiles[editingProfileIdx].startMin       = editStM;
                g_profiles[editingProfileIdx].endHour        = editEnH;
                g_profiles[editingProfileIdx].endMin         = editEnM;
                g_profiles[editingProfileIdx].blockInternet  = editBlockInt;
                g_profiles[editingProfileIdx].blockAdult     = editBlockAdult;
                g_profiles[editingProfileIdx].blockUninstall = editBlockUninst;
            }
            LogHistoryToHiddenFolderSch(L"Saved Profile: " + inpProfileName);
            editingProfileIdx = -1; SaveProfiles(); return;
        }

        if (s_activeSubTab == 0) {
            if (hoverSchModeDropdown) { isSchModeDropdownOpen = true; return; }
            if(g_ehb.hDay != -1) editDays[g_ehb.hDay] = !editDays[g_ehb.hDay];
            if(g_ehb.hStH)    { editStH = (editStH + 1) % 24; }
            if(g_ehb.hStM)    { editStM = (editStM + 5) % 60; }
            if(g_ehb.hStAmPm) { editStH = (editStH + 12) % 24; }
            if(g_ehb.hEnH)    { editEnH = (editEnH + 1) % 24; }
            if(g_ehb.hEnM)    { editEnM = (editEnM + 5) % 60; }
            if(g_ehb.hEnAmPm) { editEnH = (editEnH + 12) % 24; }
            activeInput = g_ehb.nameInp.Contains(x,y) ? 1 : 0;
        } 
        else if (s_activeSubTab == 1) {
            if(g_ehb.hTogInt) editBlockInt    = !editBlockInt;
            if(g_ehb.hTogAdt) editBlockAdult  = !editBlockAdult;
            if(g_ehb.hTogUni) editBlockUninst = !editBlockUninst;

            if (editingProfileIdx >= 0) {
                for (size_t qi = 0; qi < s_quickBlocks.size() && qi < s_quickBlockRects.size(); ++qi) {
                    if (!s_quickBlocks[qi].hovered) continue;
                    auto& qb = s_quickBlocks[qi];

                    bool found = false;
                    if (!qb.websites.empty()) {
                        auto& webs = g_profiles[editingProfileIdx].blockedWebsites;
                        for (auto it = webs.begin(); it != webs.end(); ++it) {
                            if (it->name == qb.websites[0]) { webs.erase(it); found = true; break; }
                        }
                    }
                    if (!found && !qb.keywords.empty()) {
                        auto& keys = g_profiles[editingProfileIdx].blockedKeywords;
                        for (auto it = keys.begin(); it != keys.end(); ++it) {
                            if (it->name == qb.keywords[0]) { keys.erase(it); found = true; break; }
                        }
                    }
                    if (!found) {
                        for (const auto& ws : qb.websites)
                            g_profiles[editingProfileIdx].blockedWebsites.push_back({ws, false});
                        for (const auto& kw : qb.keywords)
                            g_profiles[editingProfileIdx].blockedKeywords.push_back({kw, false});
                    }
                }
            }
        }
        else if (s_activeSubTab == 2) {
            if (hoverSchWebCombo) { isSchWebComboOpen = true; return; }
            if (hoverSchAppCombo) { isSchAppComboOpen = true; return; }

            activeInput = 0;
            if (g_ehb.webInp.Contains(x,y)) activeInput = 2;
            if (g_ehb.appInp.Contains(x,y)) activeInput = 3;
            if (g_ehb.keyInp.Contains(x,y)) activeInput = 4;

            if (editingProfileIdx >= 0) {
                if (g_ehb.hAddWeb && !inpWeb.empty()) { g_profiles[editingProfileIdx].blockedWebsites.push_back({inpWeb, false}); inpWeb = L""; }
                if (g_ehb.hAddApp && !inpApp.empty()) {
                    if (inpApp.length() < 4 || inpApp.substr(inpApp.length() - 4) != L".exe") inpApp += L".exe";
                    g_profiles[editingProfileIdx].blockedApps.push_back({inpApp, false}); inpApp = L"";
                }
                if (g_ehb.hAddKey && !inpKey.empty()) { g_profiles[editingProfileIdx].blockedKeywords.push_back({inpKey, false}); inpKey = L""; }

                if (g_ehb.listAreas[0].Contains(x, y)) {
                    auto& webs = g_profiles[editingProfileIdx].blockedWebsites;
                    for(auto& p : g_ehb.webDel) if(p.first.Contains(x,y)) webs.erase(webs.begin() + p.second);
                }
                if (g_ehb.listAreas[1].Contains(x, y)) {
                    auto& apps = g_profiles[editingProfileIdx].blockedApps;
                    for(auto& p : g_ehb.appDel) if(p.first.Contains(x,y)) apps.erase(apps.begin() + p.second);
                }
                if (g_ehb.listAreas[2].Contains(x, y)) {
                    auto& keys = g_profiles[editingProfileIdx].blockedKeywords;
                    for(auto& p : g_ehb.keyDel) if(p.first.Contains(x,y)) keys.erase(keys.begin() + p.second);
                }
            }
        }
        return;
    }

    // ── MAIN VIEW CLICKS ──────────────────────────────────────────────────
    if (hAddProfileBtn) {
        editingProfileIdx = -2; 
        s_activeSubTab = 0; 
        inpProfileName = L""; inpWeb = L""; inpApp = L""; inpKey = L"";
        tempLockMode = 0;
        for(int d=0; d<7; d++) editDays[d] = false;
        editStH = 9; editStM = 0; editEnH = 17; editEnM = 0;
        editBlockInt = false; editBlockAdult = false; editBlockUninst = true;
        s_listScrollT[0] = s_listScrollT[1] = s_listScrollT[2] = 0;
        s_listScrollC[0] = s_listScrollC[1] = s_listScrollC[2] = 0;
        
        FocusProfile np; np.profileName = L""; np.lockMode = 0;
        g_profiles.push_back(np);
        editingProfileIdx = (int)g_profiles.size() - 1;
        activeInput = 1; return;
    }

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        if (g_profiles[i].hToggle) { 
            activeActionProfileIdx = (int)i;
            if (!g_profiles[i].isActive) {
                if (g_profiles[i].lockMode == 0) {
                    // Reset time overlay state each time it opens
                    s_focusMonths = 0; s_focusDays = 0; s_focusHours = 1; s_focusMins = 0;
                    s_focusFullDay = false;
                    s_showTimeOverlay = true;
                }
                else if (g_profiles[i].lockMode == 1) { s_showPassOverlay = true; s_isStoppingFocus = false; s_inputPassText = L""; }
                else if (g_profiles[i].lockMode == 2) {
                    g_profiles[i].isActive = true;
                    ApplyProfileBlocking(i, true);
                    LogHistoryToHiddenFolderSch(L"Started Schedule: " + g_profiles[i].profileName);
                    SaveProfiles();
                }
            } else {
                if (g_profiles[i].lockMode == 0 && g_profiles[i].lockEndTime == 0) {
                    g_profiles[i].isActive = false;
                    ApplyProfileBlocking(i, false);
                    SaveProfiles();
                } else if (g_profiles[i].lockMode == 0 && g_profiles[i].lockEndTime > 0) {
                    // Timer lock: cannot stop manually
                }
                else if (g_profiles[i].lockMode == 1) { s_showPassOverlay = true; s_isStoppingFocus = true; s_inputPassText = L""; }
                else if (g_profiles[i].lockMode == 2) { s_showTextUnlockOverlay = true; s_currentTypingText = L""; s_isTypingActive = true; }
            }
        }
        if (g_profiles[i].hEdit) {
            if (g_profiles[i].isActive) {
                MessageBoxA(NULL, "Cannot edit an active blocking profile.\nPlease deactivate it first.", "Profile Active", MB_OK | MB_ICONINFORMATION);
                return;
            }
            editingProfileIdx = (int)i;
            s_activeSubTab = 0;
            inpProfileName = g_profiles[i].profileName;
            tempLockMode   = g_profiles[i].lockMode;
            for(int d=0; d<7; d++) editDays[d] = g_profiles[i].activeDays[d];
            editStH = g_profiles[i].startHour; editStM = g_profiles[i].startMin;
            editEnH = g_profiles[i].endHour;   editEnM = g_profiles[i].endMin;
            editBlockInt   = g_profiles[i].blockInternet;
            editBlockAdult = g_profiles[i].blockAdult;
            editBlockUninst= g_profiles[i].blockUninstall;
            s_listScrollT[0] = s_listScrollT[1] = s_listScrollT[2] = 0;
            s_listScrollC[0] = s_listScrollC[1] = s_listScrollC[2] = 0;
            inpWeb = L""; inpApp = L""; inpKey = L"";
            activeInput = 1;
        }
        if (g_profiles[i].hDel && !g_profiles[i].isActive) {
            int r = MessageBoxA(NULL, "Are you sure you want to delete this profile?", "Delete Profile", MB_YESNO | MB_ICONWARNING);
            if (r == IDYES) { 
                LogHistoryToHiddenFolderSch(L"Deleted Profile: " + g_profiles[i].profileName);
                g_profiles.erase(g_profiles.begin() + i); SaveProfiles(); break; 
            }
        }
    }
}

// ==========================================
// --- KEYBOARD ---
// ==========================================
void ProcessScheduleBlocksKeyPress(wchar_t c) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    
    if (s_showPassOverlay && s_isPassInputActive) {
        if (c >= 32 && c <= 126 && s_inputPassText.length() < 20) s_inputPassText += c;
    } else if (s_showTextUnlockOverlay && s_isTypingActive) {
        if (c >= 32 && c <= 126 && s_currentTypingText.length() < 1000) s_currentTypingText += (wchar_t)c;
    } else if (editingProfileIdx != -1 && activeInput != 0) {
        if (c >= 32 && c <= 126) {
            if (activeInput == 1 && inpProfileName.length() < 30) inpProfileName += c;
            if (activeInput == 2 && inpWeb.length()        < 40) inpWeb         += c;
            if (activeInput == 3 && inpApp.length()        < 40) inpApp         += c;
            if (activeInput == 4 && inpKey.length()        < 40) inpKey         += c;
        }
    }
}

void ProcessScheduleBlocksKeyDown(WPARAM key) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    
    if (key == VK_ESCAPE) { 
        if (s_showTimeOverlay || s_showPassOverlay || s_showTextUnlockOverlay) {
            s_showTimeOverlay = false; s_showPassOverlay = false; s_showTextUnlockOverlay = false;
            return;
        }
        if (editingProfileIdx != -1) { editingProfileIdx = -1; return; }
    }
    
    if (s_showPassOverlay && s_isPassInputActive) {
        if (key == VK_BACK && !s_inputPassText.empty()) s_inputPassText.pop_back();
    } else if (s_showTextUnlockOverlay && s_isTypingActive) {
        if (key == VK_BACK && !s_currentTypingText.empty()) s_currentTypingText.pop_back();
    } else if (editingProfileIdx != -1) {
        if (key == VK_BACK) {
            if (activeInput == 1 && !inpProfileName.empty()) inpProfileName.pop_back();
            if (activeInput == 2 && !inpWeb.empty())         inpWeb.pop_back();
            if (activeInput == 3 && !inpApp.empty())         inpApp.pop_back();
            if (activeInput == 4 && !inpKey.empty())         inpKey.pop_back();
        }
        else if (key == VK_RETURN && editingProfileIdx >= 0) {
            if (activeInput == 2 && !inpWeb.empty()) { g_profiles[editingProfileIdx].blockedWebsites.push_back({inpWeb, false}); inpWeb = L""; }
            if (activeInput == 3 && !inpApp.empty()) {
                if (inpApp.length() < 4 || inpApp.substr(inpApp.length() - 4) != L".exe") inpApp += L".exe";
                g_profiles[editingProfileIdx].blockedApps.push_back({inpApp, false}); inpApp = L"";
            }
            if (activeInput == 4 && !inpKey.empty()) { g_profiles[editingProfileIdx].blockedKeywords.push_back({inpKey, false}); inpKey = L""; }
        }
    }
}

void ProcessScheduleBlocksMouseWheel(float x, float y, int delta) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    
    UINT scrollLines = 3;
    SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &scrollLines, 0);
    float scrollStep = (float)scrollLines * 15.0f; 
    int steps = (delta > 0) ? 1 : -1;

    if (editingProfileIdx != -1) {
        if (s_activeSubTab == 2) {
            for (int i = 0; i < 3; i++) {
                if (g_ehb.listAreas[i].Contains(x, y)) {
                    s_listScrollT[i] -= steps * scrollStep;
                    s_listScrollT[i] = (std::max)(0.0f, (std::min)(s_listScrollT[i], s_listScrollMax[i]));
                }
            }
        }
        isSchModeDropdownOpen = false; isSchWebComboOpen = false; isSchAppComboOpen = false;
        return;
    }
    
    sch_tScroll -= steps * scrollStep;
    float totalRows = ceil((float)g_profiles.size() / 2.0f);
    float maxScroll = (std::max)(0.0f, (totalRows * 190.0f) - (s_ch - 100.0f));
    sch_tScroll = (std::max)(0.0f, (std::min)(sch_tScroll, maxScroll));
}
