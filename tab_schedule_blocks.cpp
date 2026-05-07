#pragma warning(disable : 4996)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)

#include "tab_schedule_blocks.h"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <shlobj.h>
#include <codecvt>
#include <locale>
#include <algorithm>
#include <ctime>

using namespace Gdiplus;
using namespace std;

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
    int startHour = 0, startMin = 0;
    int endHour = 23, endMin = 59;
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
static float edit_tScroll = 0.0f, edit_cScroll = 0.0f, edit_maxScroll = 0.0f;
static int s_activeSubTab = 0; // 0: Basic & Time, 1: Quick Settings, 2: Custom Lists

// Scrollbar drag state
static bool s_scrollbarDragging = false;
static float s_scrollbarDragStartY = 0.0f;
static float s_scrollbarDragStartScroll = 0.0f;

static vector<wstring> schCommonWebsites = { L"facebook.com", L"youtube.com", L"instagram.com", L"tiktok.com", L"reddit.com", L"twitter.com" };
static vector<wstring> schCommonApps = { L"chrome.exe", L"msedge.exe", L"telegram.exe", L"discord.exe", L"vlc.exe", L"control.exe", L"Taskmgr.exe", L"cmd.exe", L"SystemSettings.exe", L"run.exe" };

// Quick-block buttons for popular content types
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
static int editStH = 0, editStM = 0, editEnH = 23, editEnM = 59;
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
static int s_focusMonths = 0, s_focusDays = 0, s_focusHours = 1, s_focusMins = 0;
static bool s_hTimeMoM=false, s_hTimeMoP=false, s_hTimeDM=false, s_hTimeDP=false;
static bool s_hTimeHM=false, s_hTimeHP=false, s_hTimeMM=false, s_hTimeMP=false; 
static bool s_hTimeStart=false, s_hTimeCancel=false;

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

// Scrollbar hover
static bool s_hScrollbarThumb = false;
static bool s_hScrollbarTrack = false;

// --- Colors ---
static const Color ClrTeal(255, 12, 168, 176);
static const Color ClrTealHover(255, 30, 185, 195);
static const Color ClrDark(255, 50, 50, 50);
static const Color ClrGrayText(255, 120, 120, 120);
static const Color ClrWhite(255, 255, 255, 255);
static const Color ClrBorder(255, 220, 225, 230);
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
    RectF scrollArea;
    RectF saveBtn, cancelBtn;
    
    // Sub-Tabs
    RectF subTabRects[3];
    int hSubTab = -1;

    RectF nameInp, modeDrop;
    RectF days[7];
    RectF stH_Up, stH_Dn, stM_Up, stM_Dn;
    RectF enH_Up, enH_Dn, enM_Up, enM_Dn;
    RectF togInt, togAdt, togUni;
    RectF webInp, webCombo, addWeb;
    RectF appInp, appCombo, addApp;
    RectF keyInp, addKey;
    vector<RectF> webDel, appDel, keyDel;
    RectF modeOpt[3];
    vector<RectF> webOpts, appOpts;
    
    // Scrollbar rects
    RectF scrollbarTrack;
    RectF scrollbarThumb;

    // UI Hovers
    bool hSave=false, hCancel=false;
    int hDay=-1;
    bool hStH_Up=false, hStH_Dn=false, hStM_Up=false, hStM_Dn=false;
    bool hEnH_Up=false, hEnH_Dn=false, hEnM_Up=false, hEnM_Dn=false;
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
    } else if (allKw.empty() && !blockAllInternet) {
        system("reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\" /v AutoConfigURL /f > nul 2>&1");
    }
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
}

// --- Helpers ---
static GraphicsPath* GetSchRoundRectPath(RectF rect, int radius) {
    GraphicsPath* path = new GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure(); return path;
}

static void DrawSchOverlaySpinner(Graphics& g, float x, float y, const wstring& valStr, bool hM, bool hP, Font* fIcon, Font* fBold) {
    SolidBrush brushBtn(ClrBorder); SolidBrush brushBtnHover(ClrGrayText);
    SolidBrush brushWhite(ClrWhite); SolidBrush brushDark(ClrDark);
    Pen penBorder(ClrBorder, 1.5f);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);

    RectF mRect(x, y, 32.0f, 36.0f); RectF tRect(x + 32.0f, y, 50.0f, 36.0f); RectF pRect(x + 82.0f, y, 32.0f, 36.0f);

    g.FillRectangle(hM ? &brushBtnHover : &brushBtn, mRect); g.DrawRectangle(&penBorder, mRect.X, mRect.Y, mRect.Width, mRect.Height);
    g.DrawString(L"\xE738", -1, fIcon, mRect, &fmtC, &brushDark);
    g.FillRectangle(&brushWhite, tRect); g.DrawRectangle(&penBorder, tRect.X, tRect.Y, tRect.Width, tRect.Height);
    g.DrawString(valStr.c_str(), -1, fBold, tRect, &fmtC, &brushDark);
    g.FillRectangle(hP ? &brushBtnHover : &brushBtn, pRect); g.DrawRectangle(&penBorder, pRect.X, pRect.Y, pRect.Width, pRect.Height);
    g.DrawString(L"\xE710", -1, fIcon, pRect, &fmtC, &brushDark);
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

        if (p.isActive && p.lockMode == 0 && std::time(nullptr) >= p.lockEndTime) {
            p.isActive = false;
            // No CMD open command here!
        }

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
// --- DRAWING LOGIC ---
// ==========================================
void DrawScheduleBlocksTab(Graphics& g, float x, float y, float w, float h) {
    if (!isSchDataLoaded) { LoadProfiles(); isSchDataLoaded = true; }
    
    s_cx = x; s_cy = y; s_cw = w; s_ch = h;
    
    // Smooth Scrolling Interpolation
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
    SolidBrush bBgHover(ClrBgHover); SolidBrush bBorder(ClrBorder); SolidBrush bTealHover(ClrTealHover);
    SolidBrush bBg(ClrBg);
    Pen pBorder(ClrBorder, 1.5f); Pen pTeal(ClrTeal, 2.0f);

    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fTL; fTL.SetAlignment(StringAlignmentNear); fTL.SetLineAlignment(StringAlignmentNear);

    // --- MAIN VIEW: PROFILE LIST ---
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
        
        if (g_profiles[i].isActive && g_profiles[i].lockMode == 0 && std::time(nullptr) >= g_profiles[i].lockEndTime) {
            g_profiles[i].isActive = false; 
            SaveProfiles();
            ApplyProfileBlocking(i, false);
            // No CMD open command here!
        }

        RectF cardRect(cX, cY, cardW, cardH);
        GraphicsPath* cP = GetSchRoundRectPath(cardRect, 6);
        g.FillPath(&bWhite, cP); 
        g.DrawPath(g_profiles[i].isActive ? &pTeal : &pBorder, cP);
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
        if (g_profiles[i].isActive && g_profiles[i].lockMode == 0) toggleTxt = L"Locked (Auto)";
        g.DrawString(toggleTxt.c_str(), -1, &fBold, RectF(cX + 75, cY + 115, 100, 26), &fL, g_profiles[i].isActive ? &bTeal : &bDark);

        RectF editRect(cX + cardW - 130, cY + 115, 60, 30);
        GraphicsPath* ep = GetSchRoundRectPath(editRect, 4);
        SolidBrush eBr(g_profiles[i].hEdit ? ClrBorder : ClrBg);
        g.FillPath(&eBr, ep); g.DrawPath(&pBorder, ep); delete ep;
        g.DrawString(L"Edit", -1, &fBold, editRect, &fC, &bDark);

        RectF delRect(cX + cardW - 60, cY + 115, 45, 30);
        GraphicsPath* dp = GetSchRoundRectPath(delRect, 4);
        if (g_profiles[i].isActive) {
            g.FillPath(&bGray, dp); g.DrawPath(&pBorder, dp); delete dp;
            g.DrawString(L"\xE74D", -1, &fIcon, delRect, &fC, &bWhite);
        } else {
            SolidBrush dBr(g_profiles[i].hDel ? ClrRed : ClrWhite);
            g.FillPath(&dBr, dp); g.DrawPath(&pBorder, dp); delete dp;
            g.DrawString(L"\xE74D", -1, &fIcon, delRect, &fC, g_profiles[i].hDel ? &bWhite : &bRed);
        }
    }
    g.SetClip(&oldClip);

    // --- OVERLAY: CREATE / EDIT PROFILE (100% PROFESSIONAL TAB SYSTEM) ---
    if (editingProfileIdx != -1) {
        SolidBrush bgOver(ClrOverlay);
        g.FillRectangle(&bgOver, x, y, w, h);

        float ovW = w - 40.0f;
        float ovH = h - 40.0f;
        float ovX = x + 20.0f;
        float ovY = y + 20.0f;

        // Scrollbar geometry
        float sbW = 16.0f; 
        float sbX = ovX + ovW - sbW - 4.0f;
        float scrollAreaTop = ovY + 115.0f; // Adjusted for Tabs
        float scrollAreaH = ovH - 185.0f; 

        RectF ovRect(ovX, ovY, ovW, ovH);
        GraphicsPath* oP = GetSchRoundRectPath(ovRect, 8);
        g.FillPath(&bBg, oP); g.DrawPath(&pBorder, oP); delete oP;

        // Header (Fixed)
        wstring titleTxt = (editingProfileIdx == -2) ? L"Create New Schedule Profile" : L"Edit Schedule Profile";
        g.DrawString(titleTxt.c_str(), -1, &fTitle, RectF(ovX + 30, ovY + 20, 400, 30), &fL, &bDark);

        // --- DRAW PROFESSIONAL SUB-TABS ---
        wstring tabNames[3] = {L"Basic & Time", L"Quick Settings", L"Custom Lists"};
        float tX = ovX + 30;
        for (int i = 0; i < 3; i++) {
            g_ehb.subTabRects[i] = RectF(tX, ovY + 68, 160, 35);
            GraphicsPath* tabP = GetSchRoundRectPath(g_ehb.subTabRects[i], 17); // Professional Pill Shape
            
            if (s_activeSubTab == i) {
                g.FillPath(&bTeal, tabP);
                g.DrawString(tabNames[i].c_str(), -1, &fBold, g_ehb.subTabRects[i], &fC, &bWhite);
            } else {
                SolidBrush hovBr(g_ehb.hSubTab == i ? ClrBgHover : ClrBg);
                g.FillPath(&hovBr, tabP);
                g.DrawString(tabNames[i].c_str(), -1, &fBold, g_ehb.subTabRects[i], &fC, &bDark);
            }
            delete tabP;
            tX += 170;
        }
        g.DrawLine(&pBorder, ovX, ovY + 115, ovX + ovW, ovY + 115);

        // Footer (Fixed)
        g.DrawLine(&pBorder, ovX, ovY + ovH - 70, ovX + ovW, ovY + ovH - 70);
        g_ehb.saveBtn = RectF(ovX + ovW - 140, ovY + ovH - 50, 110, 35);
        GraphicsPath* svp = GetSchRoundRectPath(g_ehb.saveBtn, 4);
        SolidBrush svBr(g_ehb.hSave ? ClrTealHover : ClrTeal);
        g.FillPath(&svBr, svp); delete svp;
        g.DrawString(L"Save Profile", -1, &fBold, g_ehb.saveBtn, &fC, &bWhite);

        g_ehb.cancelBtn = RectF(ovX + ovW - 260, ovY + ovH - 50, 100, 35);
        GraphicsPath* cvp = GetSchRoundRectPath(g_ehb.cancelBtn, 4);
        SolidBrush cvBr(g_ehb.hCancel ? Color(255, 235, 235, 235) : ClrWhite);
        g.FillPath(&cvBr, cvp); g.DrawPath(&pBorder, cvp); delete cvp;
        g.DrawString(L"Cancel", -1, &fBold, g_ehb.cancelBtn, &fC, &bDark);

        // --- SCROLLABLE CONTENT ---
        edit_cScroll += (edit_tScroll - edit_cScroll) * 0.12f;
        g_ehb.scrollArea = RectF(ovX, scrollAreaTop, ovW - sbW - 6.0f, scrollAreaH);
        g.SetClip(g_ehb.scrollArea);
        
        g_ehb.webDel.clear(); g_ehb.appDel.clear(); g_ehb.keyDel.clear();
        s_quickBlockRects.clear();

        float cY = scrollAreaTop + 10 - edit_cScroll;
        float cardX = ovX + 30;
        float cardW_inner = ovW - 60 - sbW - 6.0f;

        // ================== TAB 0: BASIC & TIME ==================
        if (s_activeSubTab == 0) {
            // General Info Card
            RectF c1Rect(cardX, cY, cardW_inner, 90);
            GraphicsPath* c1P = GetSchRoundRectPath(c1Rect, 6);
            g.FillPath(&bWhite, c1P); g.DrawPath(&pBorder, c1P); delete c1P;
            
            g.DrawString(L"General Information", -1, &fBold, RectF(cardX + 20, cY + 15, 200, 20), &fL, &bDark);
            
            g.DrawString(L"Profile Name:", -1, &fNorm, RectF(cardX + 20, cY + 45, 100, 30), &fL, &bGray);
            g_ehb.nameInp = RectF(cardX + 120, cY + 42, 220, 35);
            GraphicsPath* np = GetSchRoundRectPath(g_ehb.nameInp, 4);
            g.FillPath(&bBg, np); g.DrawPath(activeInput == 1 ? &pTeal : &pBorder, np); delete np;
            if(inpProfileName.empty() && activeInput != 1) g.DrawString(L"e.g. Study Time", -1, &fNorm, g_ehb.nameInp, &fC, &bGray);
            else {
                g.DrawString(inpProfileName.c_str(), -1, &fNorm, RectF(g_ehb.nameInp.X+10, g_ehb.nameInp.Y, g_ehb.nameInp.Width, g_ehb.nameInp.Height), &fL, &bDark);
                if(activeInput == 1 && (GetTickCount()/500)%2==0) {
                    Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(inpProfileName.c_str(), -1, &fNorm, PointF(0,0), &bR);
                    g.FillRectangle(&bDark, g_ehb.nameInp.X+12+(inpProfileName.empty()?0:bR.Width), g_ehb.nameInp.Y+7, 1.5f, 21.0f);
                }
            }

            g.DrawString(L"Lock Mode:", -1, &fNorm, RectF(cardX + 380, cY + 45, 80, 30), &fL, &bGray);
            g_ehb.modeDrop = RectF(cardX + 470, cY + 42, 200, 35);
            GraphicsPath* mdp = GetSchRoundRectPath(g_ehb.modeDrop, 4);
            SolidBrush dropBg(hoverSchModeDropdown ? ClrBgHover : ClrBg);
            g.FillPath(&dropBg, mdp); g.DrawPath(&pBorder, mdp); delete mdp;
            wstring curModeTxt = (tempLockMode == 1) ? L"Parents Control" : ((tempLockMode == 2) ? L"Long Text Unlock" : L"Self Control");
            g.DrawString(curModeTxt.c_str(), -1, &fNorm, RectF(g_ehb.modeDrop.X+10, g_ehb.modeDrop.Y, g_ehb.modeDrop.Width-30, g_ehb.modeDrop.Height), &fL, &bDark);
            g.DrawString(L"\xE70D", -1, &fSmallIcon, RectF(g_ehb.modeDrop.X+g_ehb.modeDrop.Width-30, g_ehb.modeDrop.Y, 30, g_ehb.modeDrop.Height), &fC, &bGray);
            cY += 110;

            // Schedule Settings Card Update
            RectF c2Rect(cardX, cY, cardW_inner, 120);
            GraphicsPath* c2P = GetSchRoundRectPath(c2Rect, 6);
            g.FillPath(&bWhite, c2P); g.DrawPath(&pBorder, c2P); delete c2P;
            
            g.DrawString(L"Schedule Settings", -1, &fBold, RectF(cardX + 20, cY + 15, 200, 20), &fL, &bDark);
            
            g.DrawString(L"Select Active Days:", -1, &fSmallBold, RectF(cardX + 20, cY + 45, 150, 20), &fL, &bGray);
            wstring dLabels[] = {L"S", L"M", L"T", L"W", L"T", L"F", L"S"};
            for(int d=0; d<7; d++) {
                g_ehb.days[d] = RectF(cardX + 20 + (d * 38), cY + 68, 32, 32);
                GraphicsPath* dP = GetSchRoundRectPath(g_ehb.days[d], 16);
                SolidBrush dBr(editDays[d] ? ClrTeal : (g_ehb.hDay == d ? ClrBgHover : ClrBg));
                g.FillPath(&dBr, dP); g.DrawPath(editDays[d] ? &pTeal : &pBorder, dP); delete dP;
                g.DrawString(dLabels[d].c_str(), -1, &fBold, g_ehb.days[d], &fC, editDays[d] ? &bWhite : &bDark);
            }

            g.DrawString(L"Session Time:", -1, &fSmallBold, RectF(cardX + 320, cY + 45, 120, 20), &fL, &bGray);

            auto DrawModernTimeBox = [&](float tx, const wstring& lbl, int h, int m, RectF& hU, RectF& hD, RectF& mU, RectF& mD, bool hhu, bool hhd, bool mmu, bool mmd) {
                g.DrawString(lbl.c_str(), -1, &fSmall, RectF(tx, cY + 68, 40, 32), &fC, &bGray);

                RectF hBox(tx + 45, cY + 68, 35, 32);
                g.FillRectangle(&bBg, hBox); g.DrawRectangle(&pBorder, hBox.X, hBox.Y, hBox.Width, hBox.Height);
                g.DrawString((h < 10 ? L"0" + to_wstring(h) : to_wstring(h)).c_str(), -1, &fBold, hBox, &fC, &bDark);
                hU = RectF(hBox.X, hBox.Y - 15, hBox.Width, 15);
                hD = RectF(hBox.X, hBox.Y + hBox.Height, hBox.Width, 15);
                g.DrawString(L"\xE70E", -1, &fSmallIcon, hU, &fC, hhu ? &bTeal : &bBorder);
                g.DrawString(L"\xE70D", -1, &fSmallIcon, hD, &fC, hhd ? &bTeal : &bBorder);

                g.DrawString(L":", -1, &fBold, RectF(tx + 80, cY + 68, 15, 32), &fC, &bDark);

                RectF mBox(tx + 95, cY + 68, 35, 32);
                g.FillRectangle(&bBg, mBox); g.DrawRectangle(&pBorder, mBox.X, mBox.Y, mBox.Width, mBox.Height);
                g.DrawString((m < 10 ? L"0" + to_wstring(m) : to_wstring(m)).c_str(), -1, &fBold, mBox, &fC, &bDark);
                mU = RectF(mBox.X, mBox.Y - 15, mBox.Width, 15);
                mD = RectF(mBox.X, mBox.Y + mBox.Height, mBox.Width, 15);
                g.DrawString(L"\xE70E", -1, &fSmallIcon, mU, &fC, mmu ? &bTeal : &bBorder);
                g.DrawString(L"\xE70D", -1, &fSmallIcon, mD, &fC, mmd ? &bTeal : &bBorder);
            };

            DrawModernTimeBox(cardX + 320, L"Start", editStH, editStM, g_ehb.stH_Up, g_ehb.stH_Dn, g_ehb.stM_Up, g_ehb.stM_Dn, g_ehb.hStH_Up, g_ehb.hStH_Dn, g_ehb.hStM_Up, g_ehb.hStM_Dn);
            DrawModernTimeBox(cardX + 480, L"End", editEnH, editEnM, g_ehb.enH_Up, g_ehb.enH_Dn, g_ehb.enM_Up, g_ehb.enM_Dn, g_ehb.hEnH_Up, g_ehb.hEnH_Dn, g_ehb.hEnM_Up, g_ehb.hEnM_Dn);
            cY += 140;
        }

        // ================== TAB 1: QUICK SETTINGS ==================
        else if (s_activeSubTab == 1) {
            // Restriction Filters Card
            RectF c3Rect(cardX, cY, cardW_inner, 70);
            GraphicsPath* c3P = GetSchRoundRectPath(c3Rect, 6);
            g.FillPath(&bWhite, c3P); g.DrawPath(&pBorder, c3P); delete c3P;
            
            auto DrawCb = [&](RectF& box, float cx, const wstring& label, bool val, bool hov) {
                box = RectF(cx, cY + 25, 20, 20);
                g.FillRectangle(val ? &bTeal : (hov ? &bBgHover : &bBg), box);
                g.DrawRectangle(&pBorder, box.X, box.Y, box.Width, box.Height);
                if(val) g.DrawString(L"\xE73E", -1, &fSmallIcon, box, &fC, &bWhite);
                g.DrawString(label.c_str(), -1, &fNorm, RectF(cx + 25, cY + 25, 150, 20), &fL, &bDark);
            };
            DrawCb(g_ehb.togInt, cardX + 20, L"Block Internet entirely", editBlockInt, g_ehb.hTogInt);
            DrawCb(g_ehb.togAdt, cardX + 230, L"Block Adult Content", editBlockAdult, g_ehb.hTogAdt);
            DrawCb(g_ehb.togUni, cardX + 440, L"Block Uninstall / Taskmgr", editBlockUninst, g_ehb.hTogUni);
            cY += 90;

            // Quick Block Buttons
            RectF c4Rect(cardX, cY, cardW_inner, 80);
            GraphicsPath* c4P = GetSchRoundRectPath(c4Rect, 6);
            g.FillPath(&bWhite, c4P); g.DrawPath(&pBorder, c4P); delete c4P;

            g.DrawString(L"Quick Block", -1, &fBold, RectF(cardX + 20, cY + 12, 120, 20), &fL, &bDark);
            g.DrawString(L"(Works in Chrome, Edge, Firefox, Brave, Opera)", -1, &fSmall,
                RectF(cardX + 130, cY + 14, 400, 18), &fL, &bGray);

            float qbX = cardX + 20;
            float qbY = cY + 38;
            float qbW = 120.0f;
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
                    g.FillPath(&bTeal, qp); g.DrawPath(&pTeal, qp);
                } else {
                    SolidBrush qbBg(s_quickBlocks[qi].hovered ? ClrBgHover : ClrBg);
                    g.FillPath(&qbBg, qp); g.DrawPath(&pBorder, qp);
                }
                delete qp;

                SolidBrush* txtClr = alreadyAdded ? &bWhite : &bDark;
                g.DrawString(s_quickBlocks[qi].label.c_str(), -1, &fSmallBold, qbRect, &fC, txtClr);
            }
            cY += 100;
        }

        // ================== TAB 2: CUSTOM LISTS ==================
        else if (s_activeSubTab == 2) {
            vector<SchBlockItem>* cWebs = nullptr; vector<SchBlockItem>* cApps = nullptr; vector<SchBlockItem>* cKeys = nullptr;
            if(editingProfileIdx >= 0) {
                cWebs = &g_profiles[editingProfileIdx].blockedWebsites;
                cApps = &g_profiles[editingProfileIdx].blockedApps;
                cKeys = &g_profiles[editingProfileIdx].blockedKeywords;
            }

            auto DrawListCard = [&](const wstring& title, const wstring& ph, wstring& inpStr, int inpIdx, vector<SchBlockItem>* list,
                                    RectF& outInp, RectF* outCombo, RectF& outAdd, bool hovCombo, bool hovAdd, vector<RectF>& outDel) {
                float listH = list ? (list->size() * 35.0f) : 0;
                float cardH = 90.0f + (listH > 0 ? listH + 10.0f : 0);
                
                RectF cRect(cardX, cY, cardW_inner, cardH);
                GraphicsPath* cP = GetSchRoundRectPath(cRect, 6);
                g.FillPath(&bWhite, cP); g.DrawPath(&pBorder, cP); delete cP;

                g.DrawString(title.c_str(), -1, &fBold, RectF(cardX + 20, cY + 15, 200, 20), &fL, &bDark);
                
                outInp = RectF(cardX + 20, cY + 40, outCombo ? 300 : 330, 35);
                GraphicsPath* ip = GetSchRoundRectPath(outInp, 4);
                g.FillPath(&bBg, ip); g.DrawPath(activeInput == inpIdx ? &pTeal : &pBorder, ip); delete ip;
                
                if(inpStr.empty() && activeInput != inpIdx) g.DrawString(ph.c_str(), -1, &fNorm, RectF(outInp.X+10, outInp.Y, outInp.Width, outInp.Height), &fL, &bGray);
                else {
                    g.DrawString(inpStr.c_str(), -1, &fNorm, RectF(outInp.X+10, outInp.Y, outInp.Width, outInp.Height), &fL, &bDark);
                    if(activeInput == inpIdx && (GetTickCount()/500)%2==0) {
                        Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(inpStr.c_str(), -1, &fNorm, PointF(0,0), &bR);
                        g.FillRectangle(&bDark, outInp.X+12+(inpStr.empty()?0:bR.Width), outInp.Y+7, 1.5f, 21.0f);
                    }
                }

                if(outCombo) {
                    *outCombo = RectF(cardX + 325, cY + 40, 35, 35);
                    g.FillRectangle(hovCombo ? &bBgHover : &bBg, *outCombo); g.DrawRectangle(&pBorder, outCombo->X, outCombo->Y, outCombo->Width, outCombo->Height);
                    g.DrawString(L"\xE70D", -1, &fSmallIcon, *outCombo, &fC, &bDark);
                }

                outAdd = RectF(cardX + (outCombo ? 365 : 355), cY + 40, 90, 35);
                GraphicsPath* ap = GetSchRoundRectPath(outAdd, 4);
                SolidBrush aBr(hovAdd ? ClrTealHover : ClrTeal); g.FillPath(&aBr, ap); delete ap;
                g.DrawString(L"+ Add", -1, &fBold, outAdd, &fC, &bWhite);

                if(list && !list->empty()) {
                    float itemY = cY + 85.0f;
                    for(auto& item : *list) {
                        RectF rowR(cardX + 20, itemY, cardW_inner - 40, 30);
                        g.FillRectangle(&bBgHover, rowR);
                        g.DrawString(item.name.c_str(), -1, &fNorm, RectF(rowR.X+10, rowR.Y, rowR.Width-40, rowR.Height), &fL, &bDark);
                        
                        RectF delR(rowR.X + rowR.Width - 30, rowR.Y + 2.5f, 25, 25);
                        outDel.push_back(delR);
                        SolidBrush crBr(item.isHoveredCross ? ClrRed : ClrGrayText);
                        g.DrawString(L"\xE711", -1, &fSmallIcon, delR, &fC, &crBr);
                        itemY += 35.0f;
                    }
                }
                cY += cardH + 20;
            };

            DrawListCard(L"Blocked Websites", L"e.g. facebook.com", inpWeb, 2, cWebs, g_ehb.webInp, &g_ehb.webCombo, g_ehb.addWeb, hoverSchWebCombo, g_ehb.hAddWeb, g_ehb.webDel);
            DrawListCard(L"Blocked Apps", L"e.g. vlc.exe", inpApp, 3, cApps, g_ehb.appInp, &g_ehb.appCombo, g_ehb.addApp, hoverSchAppCombo, g_ehb.hAddApp, g_ehb.appDel);
            DrawListCard(L"Blocked Keywords", L"e.g. games", inpKey, 4, cKeys, g_ehb.keyInp, nullptr, g_ehb.addKey, false, g_ehb.hAddKey, g_ehb.keyDel);
        }

        cY += 20;
        edit_maxScroll = (std::max)(0.0f, cY + edit_cScroll - scrollAreaTop - g_ehb.scrollArea.Height);
        g.SetClip(&oldClip); 

        // ==========================================
        // SCROLLBAR DRAWING
        // ==========================================
        bool hasScroll = edit_maxScroll > 0.0f;
        if (hasScroll) {
            g_ehb.scrollbarTrack = RectF(sbX, scrollAreaTop + 4, sbW, scrollAreaH - 8);
            SolidBrush sbTrackBr(ClrScrollbarTrack);
            GraphicsPath* trP = GetSchRoundRectPath(g_ehb.scrollbarTrack, 8);
            g.FillPath(&sbTrackBr, trP); delete trP;

            float totalContent = scrollAreaH + edit_maxScroll;
            float thumbRatio = (std::min)(1.0f, scrollAreaH / totalContent);
            float thumbH = (std::max)(28.0f, g_ehb.scrollbarTrack.Height * thumbRatio);
            float thumbRange = g_ehb.scrollbarTrack.Height - thumbH;
            float scrollRatio = (edit_maxScroll > 0) ? (edit_cScroll / edit_maxScroll) : 0.0f;
            float thumbY = g_ehb.scrollbarTrack.Y + scrollRatio * thumbRange;

            g_ehb.scrollbarThumb = RectF(sbX + 1, thumbY, sbW - 2, thumbH);
            Color thumbClr = (s_scrollbarDragging || s_hScrollbarThumb) ? ClrScrollbarHover : ClrScrollbar;
            SolidBrush sbThumbBr(thumbClr);
            GraphicsPath* thP = GetSchRoundRectPath(g_ehb.scrollbarThumb, 8);
            g.FillPath(&sbThumbBr, thP); delete thP;

            if (edit_cScroll < edit_maxScroll - 5.0f) {
                RectF hintRect(ovX, ovY + ovH - 140, ovW - sbW - 6, 35);
                LinearGradientBrush fadeBrush(
                    PointF(hintRect.X, hintRect.Y),
                    PointF(hintRect.X, hintRect.Y + hintRect.Height),
                    Color(0, 248, 250, 252),
                    Color(200, 248, 250, 252)
                );
                g.FillRectangle(&fadeBrush, hintRect);

                RectF chevRect(ovX + ovW/2 - 40, ovY + ovH - 112, 80, 22);
                GraphicsPath* chevBg = GetSchRoundRectPath(chevRect, 11);
                SolidBrush chevBgBr(Color(220, 12, 168, 176));
                g.FillPath(&chevBgBr, chevBg); delete chevBg;
                g.DrawString(L"\xE74B  scroll", -1, &fSmall, chevRect, &fC, &bWhite);
            }
        }

        // --- Overlapping Dropdown Menus (Z-Index Top) ---
        if (s_activeSubTab == 0 && isSchModeDropdownOpen) {
            RectF mlR(g_ehb.modeDrop.X, g_ehb.modeDrop.Y + 38, 200, 118);
            GraphicsPath* mlP = GetSchRoundRectPath(mlR, 4);
            g.FillPath(&bWhite, mlP); g.DrawPath(&pBorder, mlP); delete mlP;

            g_ehb.modeOpt[0] = RectF(mlR.X+2, mlR.Y+2, 196, 38);
            g_ehb.modeOpt[1] = RectF(mlR.X+2, mlR.Y+40, 196, 38);
            g_ehb.modeOpt[2] = RectF(mlR.X+2, mlR.Y+78, 196, 38);

            SolidBrush o1Br(g_ehb.hOptSelf ? ClrBgHover : ClrWhite); g.FillRectangle(&o1Br, g_ehb.modeOpt[0]);
            g.DrawString(L"Self Control", -1, &fNorm, RectF(mlR.X+15, mlR.Y+2, 180, 38), &fL, &bDark);

            SolidBrush o2Br(g_ehb.hOptParents ? ClrBgHover : ClrWhite); g.FillRectangle(&o2Br, g_ehb.modeOpt[1]);
            g.DrawString(L"Parents Control", -1, &fNorm, RectF(mlR.X+15, mlR.Y+40, 180, 38), &fL, &bDark);

            SolidBrush o3Br(g_ehb.hOptLongText ? ClrBgHover : ClrWhite); g.FillRectangle(&o3Br, g_ehb.modeOpt[2]);
            g.DrawString(L"Long Text Unlock", -1, &fNorm, RectF(mlR.X+15, mlR.Y+78, 180, 38), &fL, &bDark);
        }
        
        auto DrawDynamicDropdown = [&](RectF btnRect, vector<wstring>& opts, vector<RectF>& outOpts, int hovIdx) {
            RectF lR(btnRect.X - 165, btnRect.Y + 38, 200, opts.size() * 30 + 10);
            GraphicsPath* lP = GetSchRoundRectPath(lR, 4);
            g.FillPath(&bWhite, lP); g.DrawPath(&pBorder, lP); delete lP;
            outOpts.clear();
            float iY = lR.Y + 5;
            for(size_t i=0; i<opts.size(); ++i) {
                RectF optRect(lR.X+2, iY, lR.Width-4, 30);
                outOpts.push_back(optRect);
                SolidBrush oBr(hovIdx == (int)i ? ClrBgHover : ClrWhite);
                g.FillRectangle(&oBr, optRect);
                g.DrawString(opts[i].c_str(), -1, &fNorm, RectF(lR.X+10, iY, lR.Width-10, 30), &fL, &bDark); iY += 30;
            }
        };

        if (s_activeSubTab == 2) {
            if (isSchWebComboOpen) DrawDynamicDropdown(g_ehb.webCombo, schCommonWebsites, g_ehb.webOpts, hoverSchWebOptIdx);
            if (isSchAppComboOpen) DrawDynamicDropdown(g_ehb.appCombo, schCommonApps, g_ehb.appOpts, hoverSchAppOptIdx);
        }
    }

    // ==========================================
    // 3. FULL SCREEN OVERLAYS FOR LOCKING
    // ==========================================
    if (s_showTimeOverlay || s_showPassOverlay || s_showTextUnlockOverlay) {
        SolidBrush overlayBg(ClrOverlay);
        g.FillRectangle(&overlayBg, x, y, w, h);

        float ovW = s_showTextUnlockOverlay ? 600.0f : 500.0f; 
        float ovH = s_showTextUnlockOverlay ? 450.0f : 280.0f;
        float ovX = x + (w - ovW) / 2.0f;
        float ovY = y + (h - ovH) / 2.0f;

        RectF ovRect(ovX, ovY, ovW, ovH);
        GraphicsPath* op = GetSchRoundRectPath(ovRect, 8);
        g.FillPath(&bBg, op); g.DrawPath(&pBorder, op); delete op;

        if (s_showTimeOverlay) {
            g.DrawString(L"SET FOCUS DURATION (SELF CONTROL)", -1, &fTitle, RectF(ovX, ovY + 20, ovW, 30), &fC, &bDark);
            
            g.DrawString(L"Months:", -1, &fBold, RectF(ovX + 40, ovY + 80, 60, 36), &fL, &bDark);
            DrawSchOverlaySpinner(g, ovX + 110, ovY + 80, to_wstring(s_focusMonths), s_hTimeMoM, s_hTimeMoP, &fIcon, &fBold);
            
            g.DrawString(L"Days:", -1, &fBold, RectF(ovX + 250, ovY + 80, 50, 36), &fL, &bDark);
            DrawSchOverlaySpinner(g, ovX + 300, ovY + 80, to_wstring(s_focusDays), s_hTimeDM, s_hTimeDP, &fIcon, &fBold);

            g.DrawString(L"Hours:", -1, &fBold, RectF(ovX + 40, ovY + 140, 60, 36), &fL, &bDark);
            DrawSchOverlaySpinner(g, ovX + 110, ovY + 140, to_wstring(s_focusHours), s_hTimeHM, s_hTimeHP, &fIcon, &fBold);
            
            g.DrawString(L"Mins:", -1, &fBold, RectF(ovX + 250, ovY + 140, 50, 36), &fL, &bDark);
            DrawSchOverlaySpinner(g, ovX + 300, ovY + 140, to_wstring(s_focusMins), s_hTimeMM, s_hTimeMP, &fIcon, &fBold);

            RectF cancelRect(ovX + 60, ovY + 210, 140, 40);
            GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 4);
            SolidBrush cancelBrush(s_hTimeCancel ? ClrBgHover : ClrWhite);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&pBorder, cp); delete cp;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fC, &bDark);

            RectF startRect(ovX + 240, ovY + 210, 140, 40);
            GraphicsPath* sp = GetSchRoundRectPath(startRect, 4);
            SolidBrush startBrush(s_hTimeStart ? ClrTealHover : ClrTeal);
            g.FillPath(&startBrush, sp); delete sp;
            g.DrawString(L"Start Profile", -1, &fBold, startRect, &fC, &bWhite);
        }
        else if (s_showPassOverlay) {
            wstring titleTxt = s_isStoppingFocus ? L"ENTER PARENTS PASSWORD TO STOP" : L"SET PARENTS PASSWORD";
            g.DrawString(titleTxt.c_str(), -1, &fTitle, RectF(ovX, ovY + 20, ovW, 30), &fC, &bDark);
            RectF passInpRect(ovX + 40, ovY + 80, ovW - 80, 40);
            GraphicsPath* pp = GetSchRoundRectPath(passInpRect, 4);
            g.FillPath(&bWhite, pp); g.DrawPath(s_isPassInputActive ? &pTeal : &pBorder, pp); delete pp;
            
            wstring displayPass = wstring(s_inputPassText.length(), L'*');
            if (s_inputPassText.empty() && !s_isPassInputActive) g.DrawString(L"Type password here...", -1, &fNorm, passInpRect, &fC, &bGray);
            else {
                g.DrawString(displayPass.c_str(), -1, &fTitle, RectF(ovX + 50, ovY + 85, ovW - 100, 30), &fL, &bDark);
                if (s_isPassInputActive && (GetTickCount() / 500) % 2 == 0) {
                     Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(displayPass.c_str(), -1, &fTitle, PointF(0,0), &bR);
                     float curX = ovX + 52 + (displayPass.empty()?0:bR.Width);
                     g.FillRectangle(&bDark, curX, ovY + 90, 1.5f, 20.0f);
                }
            }

            RectF cancelRect(ovX + 40, ovY + 150, 140, 40);
            GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 4);
            SolidBrush cancelBrush(s_hPassCancel ? ClrBgHover : ClrWhite);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&pBorder, cp); delete cp;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fC, &bDark);

            RectF confRect(ovX + 220, ovY + 150, 160, 40);
            GraphicsPath* sp = GetSchRoundRectPath(confRect, 4);
            SolidBrush confBrush(s_hPassConfirm ? ClrTealHover : ClrTeal);
            g.FillPath(&confBrush, sp); delete sp;
            g.DrawString(L"Confirm", -1, &fBold, confRect, &fC, &bWhite);
        }
        else if (s_showTextUnlockOverlay) {
            g.DrawString(L"EXACT TEXT UNLOCK MODE", -1, &fTitle, RectF(ovX, ovY + 20, ovW, 30), &fC, &bDark);
            
            RectF targetBox(ovX + 20, ovY + 60, ovW - 40, 110);
            g.FillRectangle(&bWhite, targetBox); g.DrawRectangle(&pBorder, targetBox.X, targetBox.Y, targetBox.Width, targetBox.Height);
            g.DrawString(s_targetUnlockText.c_str(), -1, &fNorm, RectF(targetBox.X+5, targetBox.Y+5, targetBox.Width-10, targetBox.Height-10), &fTL, &bGray);

            RectF typeBox(ovX + 20, ovY + 180, ovW - 40, 160);
            GraphicsPath* tp = GetSchRoundRectPath(typeBox, 4);
            g.FillPath(&bWhite, tp); g.DrawPath(s_isTypingActive ? &pTeal : &pBorder, tp); delete tp;

            g.DrawString(s_currentTypingText.c_str(), -1, &fNorm, RectF(typeBox.X + 5, typeBox.Y + 5, typeBox.Width - 10, typeBox.Height - 10), &fTL, &bDark);

            RectF cancelRect(ovX + 120, ovY + 380, 140, 40);
            GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 4);
            SolidBrush cancelBrush(s_hTextUnlockCancel ? ClrBgHover : ClrWhite);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&pBorder, cp); delete cp;
            g.DrawString(L"Cancel (Esc)", -1, &fBold, cancelRect, &fC, &bDark);

            RectF confRect(ovX + 280, ovY + 380, 160, 40);
            GraphicsPath* sp = GetSchRoundRectPath(confRect, 4);
            SolidBrush confBrush((s_currentTypingText == s_targetUnlockText) ? (s_hTextUnlockConfirm ? ClrTealHover : ClrTeal) : ClrDisabled);
            g.FillPath(&confBrush, sp); delete sp;
            g.DrawString(L"Unlock Profile", -1, &fBold, confRect, &fC, &bWhite);
        }
    }
}

// ==========================================
// --- MOUSE MOVE LOGIC ---
// ==========================================
void ProcessScheduleBlocksMouseMove(float x, float y) {
    hAddProfileBtn = false;
    
    s_hTimeMoM=false; s_hTimeMoP=false; s_hTimeDM=false; s_hTimeDP=false;
    s_hTimeHM=false; s_hTimeHP=false; s_hTimeMM=false; s_hTimeMP=false; s_hTimeStart=false; s_hTimeCancel=false;
    s_hPassInput=false; s_hPassConfirm=false; s_hPassCancel=false;
    s_hTextUnlockConfirm=false; s_hTextUnlockCancel=false;
    s_hScrollbarThumb=false; s_hScrollbarTrack=false;
    g_ehb.hSubTab = -1;

    if (s_showTimeOverlay || s_showPassOverlay || s_showTextUnlockOverlay) {
        float ovW = s_showTextUnlockOverlay ? 600.0f : 500.0f; 
        float ovH = s_showTextUnlockOverlay ? 450.0f : 280.0f;
        float ovX = s_cx + (s_cw - ovW) / 2.0f;
        float ovY = s_cy + (s_ch - ovH) / 2.0f;

        if (s_showTimeOverlay) {
            if (RectF(ovX + 110, ovY + 80, 32, 36).Contains(x, y)) s_hTimeMoM = true;
            if (RectF(ovX + 110 + 92, ovY + 80, 32, 36).Contains(x, y)) s_hTimeMoP = true;
            if (RectF(ovX + 300, ovY + 80, 32, 36).Contains(x, y)) s_hTimeDM = true;
            if (RectF(ovX + 300 + 92, ovY + 80, 32, 36).Contains(x, y)) s_hTimeDP = true;

            if (RectF(ovX + 110, ovY + 140, 32, 36).Contains(x, y)) s_hTimeHM = true;
            if (RectF(ovX + 110 + 92, ovY + 140, 32, 36).Contains(x, y)) s_hTimeHP = true;
            if (RectF(ovX + 300, ovY + 140, 32, 36).Contains(x, y)) s_hTimeMM = true;
            if (RectF(ovX + 300 + 92, ovY + 140, 32, 36).Contains(x, y)) s_hTimeMP = true;

            if (RectF(ovX + 60, ovY + 210, 140, 40).Contains(x, y)) s_hTimeCancel = true;
            if (RectF(ovX + 240, ovY + 210, 140, 40).Contains(x, y)) s_hTimeStart = true;
        }
        else if (s_showPassOverlay) {
            if (RectF(ovX + 40, ovY + 80, ovW - 80, 40).Contains(x, y)) s_hPassInput = true;
            if (RectF(ovX + 40, ovY + 150, 140, 40).Contains(x, y)) s_hPassCancel = true;
            if (RectF(ovX + 220, ovY + 150, 160, 40).Contains(x, y)) s_hPassConfirm = true;
        }
        else if (s_showTextUnlockOverlay) {
            if (RectF(ovX + 120, ovY + 380, 140, 40).Contains(x, y)) s_hTextUnlockCancel = true;
            if (RectF(ovX + 280, ovY + 380, 160, 40).Contains(x, y)) s_hTextUnlockConfirm = true;
        }
        return;
    }

    if (editingProfileIdx != -1) {
        g_ehb.hOptSelf = false; g_ehb.hOptParents = false; g_ehb.hOptLongText = false;
        hoverSchWebOptIdx = -1; hoverSchAppOptIdx = -1;
        g_ehb.hSave = false; g_ehb.hCancel = false;

        for (int i = 0; i < 3; i++) {
            if (g_ehb.subTabRects[i].Contains(x, y)) g_ehb.hSubTab = i;
        }

        if (s_scrollbarDragging) {
            float scrollAreaH = g_ehb.scrollbarTrack.Height;
            float totalContent = scrollAreaH + edit_maxScroll;
            float thumbRatio = (std::min)(1.0f, scrollAreaH / totalContent);
            float thumbH = (std::max)(28.0f, scrollAreaH * thumbRatio);
            float thumbRange = scrollAreaH - thumbH;
            float dy = y - s_scrollbarDragStartY;
            float newScroll = s_scrollbarDragStartScroll + (thumbRange > 0 ? dy / thumbRange * edit_maxScroll : 0);
            edit_tScroll = (std::max)(0.0f, (std::min)(newScroll, edit_maxScroll));
            return;
        }

        if (g_ehb.scrollbarThumb.Contains(x, y)) s_hScrollbarThumb = true;
        if (g_ehb.scrollbarTrack.Contains(x, y)) s_hScrollbarTrack = true;
        
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

        if(g_ehb.saveBtn.Contains(x,y)) g_ehb.hSave = true;
        if(g_ehb.cancelBtn.Contains(x,y)) g_ehb.hCancel = true;

        g_ehb.hDay = -1;
        g_ehb.hStH_Up=false; g_ehb.hStH_Dn=false; g_ehb.hStM_Up=false; g_ehb.hStM_Dn=false;
        g_ehb.hEnH_Up=false; g_ehb.hEnH_Dn=false; g_ehb.hEnM_Up=false; g_ehb.hEnM_Dn=false;
        g_ehb.hTogInt=false; g_ehb.hTogAdt=false; g_ehb.hTogUni=false;
        hoverSchModeDropdown=false; hoverSchWebCombo=false; hoverSchAppCombo=false;
        g_ehb.hAddWeb=false; g_ehb.hAddApp=false; g_ehb.hAddKey=false;
        for (auto& qb : s_quickBlocks) qb.hovered = false;
        
        if (editingProfileIdx >= 0) {
            for(auto& it : g_profiles[editingProfileIdx].blockedWebsites) it.isHoveredCross = false;
            for(auto& it : g_profiles[editingProfileIdx].blockedApps) it.isHoveredCross = false;
            for(auto& it : g_profiles[editingProfileIdx].blockedKeywords) it.isHoveredCross = false;
        }

        if (g_ehb.scrollArea.Contains(x, y)) {
            if (s_activeSubTab == 0) {
                if(g_ehb.modeDrop.Contains(x, y)) hoverSchModeDropdown = true;
                for(int d=0; d<7; d++) { if(g_ehb.days[d].Contains(x,y)) g_ehb.hDay = d; }
                
                if(g_ehb.stH_Up.Contains(x,y)) g_ehb.hStH_Up = true; if(g_ehb.stH_Dn.Contains(x,y)) g_ehb.hStH_Dn = true;
                if(g_ehb.stM_Up.Contains(x,y)) g_ehb.hStM_Up = true; if(g_ehb.stM_Dn.Contains(x,y)) g_ehb.hStM_Dn = true;
                if(g_ehb.enH_Up.Contains(x,y)) g_ehb.hEnH_Up = true; if(g_ehb.enH_Dn.Contains(x,y)) g_ehb.hEnH_Dn = true;
                if(g_ehb.enM_Up.Contains(x,y)) g_ehb.hEnM_Up = true; if(g_ehb.enM_Dn.Contains(x,y)) g_ehb.hEnM_Dn = true;
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

                if(g_ehb.addWeb.Contains(x,y)) g_ehb.hAddWeb = true;
                if(g_ehb.addApp.Contains(x,y)) g_ehb.hAddApp = true;
                if(g_ehb.addKey.Contains(x,y)) g_ehb.hAddKey = true;

                if (editingProfileIdx >= 0) {
                    for(size_t i=0; i<g_ehb.webDel.size(); i++) { if(g_ehb.webDel[i].Contains(x,y)) g_profiles[editingProfileIdx].blockedWebsites[i].isHoveredCross = true; }
                    for(size_t i=0; i<g_ehb.appDel.size(); i++) { if(g_ehb.appDel[i].Contains(x,y)) g_profiles[editingProfileIdx].blockedApps[i].isHoveredCross = true; }
                    for(size_t i=0; i<g_ehb.keyDel.size(); i++) { if(g_ehb.keyDel[i].Contains(x,y)) g_profiles[editingProfileIdx].blockedKeywords[i].isHoveredCross = true; }
                }
            }
        }
        return;
    }

    if (RectF(s_cx + s_cw - 220, s_cy + 20, 200, 40).Contains(x, y)) hAddProfileBtn = true;

    float cardW = (s_cw - 60.0f) / 2.0f; float cardH = 170.0f;
    float startX = s_cx + 20.0f; float startY = s_cy + 100.0f - sch_cScroll;

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        float cX = startX + (i % 2) * (cardW + 20.0f);
        float cY = startY + (i / 2) * (cardH + 20.0f);
        if (cY > s_cy + s_ch || cY + cardH < s_cy + 90.0f) continue;

        g_profiles[i].hToggle = RectF(cX + 15, cY + 115, 60, 26).Contains(x, y) || RectF(cX + 75, cY + 115, 60, 26).Contains(x, y);
        g_profiles[i].hEdit = RectF(cX + cardW - 130, cY + 115, 60, 30).Contains(x, y);
        if(!g_profiles[i].isActive) g_profiles[i].hDel = RectF(cX + cardW - 60, cY + 115, 45, 30).Contains(x, y);
        else g_profiles[i].hDel = false; 
    }
}

// ==========================================
// --- MOUSE BUTTON DOWN ---
// ==========================================
void ProcessScheduleBlocksMouseDown(float x, float y) {
    if (editingProfileIdx == -1) return;
    if (g_ehb.scrollbarThumb.Contains(x, y)) {
        s_scrollbarDragging = true;
        s_scrollbarDragStartY = y;
        s_scrollbarDragStartScroll = edit_cScroll;
    } else if (g_ehb.scrollbarTrack.Contains(x, y)) {
        float scrollAreaH = g_ehb.scrollbarTrack.Height;
        float totalContent = scrollAreaH + edit_maxScroll;
        float thumbRatio = (std::min)(1.0f, scrollAreaH / totalContent);
        float thumbH = (std::max)(28.0f, scrollAreaH * thumbRatio);
        float thumbRange = scrollAreaH - thumbH;
        float relY = y - g_ehb.scrollbarTrack.Y - thumbH / 2.0f;
        float newScroll = (thumbRange > 0) ? (relY / thumbRange * edit_maxScroll) : 0;
        edit_tScroll = (std::max)(0.0f, (std::min)(newScroll, edit_maxScroll));
    }
}

// ==========================================
// --- MOUSE BUTTON UP ---
// ==========================================
void ProcessScheduleBlocksMouseUp(float x, float y) {
    s_scrollbarDragging = false;
}

// ==========================================
// --- MOUSE CLICK LOGIC ---
// ==========================================
void ProcessScheduleBlocksMouseClick(float x, float y) {
    if (s_showTimeOverlay) {
        if (s_hTimeMoM && s_focusMonths > 0) s_focusMonths--;
        if (s_hTimeMoP && s_focusMonths < 12) s_focusMonths++;
        if (s_hTimeDM && s_focusDays > 0) s_focusDays--;
        if (s_hTimeDP && s_focusDays < 30) s_focusDays++;

        if (s_hTimeHM && s_focusHours > 0) s_focusHours--;
        if (s_hTimeHP && s_focusHours < 23) s_focusHours++;
        if (s_hTimeMM) { s_focusMins -= 5; if (s_focusMins < 0) s_focusMins = 55; }
        if (s_hTimeMP) { s_focusMins = (s_focusMins + 5) % 60; }

        if (s_hTimeCancel) s_showTimeOverlay = false;
        if (s_hTimeStart && activeActionProfileIdx >= 0) { 
            g_profiles[activeActionProfileIdx].isActive = true; 
            g_profiles[activeActionProfileIdx].lockEndTime = std::time(nullptr) + (s_focusMonths * 30 * 24 * 3600) + (s_focusDays * 24 * 3600) + (s_focusHours * 3600) + (s_focusMins * 60);
            ApplyProfileBlocking(activeActionProfileIdx, true);
            s_showTimeOverlay = false; 
            LogHistoryToHiddenFolderSch(L"Started Schedule: " + g_profiles[activeActionProfileIdx].profileName);
            SaveProfiles();
        }
        return;
    }
    if (s_showPassOverlay) {
        s_isPassInputActive = s_hPassInput;
        if (s_hPassCancel) { s_showPassOverlay = false; s_inputPassText = L""; }
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
                    // No CMD open command here!
                }
            }
            s_showPassOverlay = false; s_inputPassText = L""; 
            SaveProfiles();
        }
        return;
    }
    if (s_showTextUnlockOverlay) {
        if (s_hTextUnlockCancel) s_showTextUnlockOverlay = false;
        if (s_hTextUnlockConfirm && s_currentTypingText == s_targetUnlockText && activeActionProfileIdx >= 0) {
            g_profiles[activeActionProfileIdx].isActive = false;
            ApplyProfileBlocking(activeActionProfileIdx, false);
            s_showTextUnlockOverlay = false; s_currentTypingText = L"";
            LogHistoryToHiddenFolderSch(L"Unlocked (Long Text) Schedule: " + g_profiles[activeActionProfileIdx].profileName);
            SaveProfiles();
            // No CMD open command here!
        }
        return;
    }

    if (s_scrollbarDragging) return;

    if (editingProfileIdx != -1) {
        // Handle Sub-Tab Switching
        for (int i = 0; i < 3; i++) {
            if (g_ehb.subTabRects[i].Contains(x, y)) {
                s_activeSubTab = i;
                edit_tScroll = 0; edit_cScroll = 0; // Reset scroll on tab switch
                activeInput = (i == 0) ? 1 : 0; // Set profile name input active if tab 0
                isSchModeDropdownOpen = false;
                isSchWebComboOpen = false;
                isSchAppComboOpen = false;
                return;
            }
        }

        bool dropdownClosed = false;

        if (s_activeSubTab == 0) {
            if (isSchModeDropdownOpen && !hoverSchModeDropdown && !g_ehb.hOptSelf && !g_ehb.hOptParents && !g_ehb.hOptLongText) {
                isSchModeDropdownOpen = false; dropdownClosed = true;
            } else if (isSchModeDropdownOpen) {
                if (g_ehb.hOptSelf) tempLockMode = 0;
                if (g_ehb.hOptParents) tempLockMode = 1;
                if (g_ehb.hOptLongText) tempLockMode = 2;
                isSchModeDropdownOpen = false; return;
            }
        }

        if (s_activeSubTab == 2) {
            if (isSchWebComboOpen && !hoverSchWebCombo && hoverSchWebOptIdx == -1) {
                isSchWebComboOpen = false; dropdownClosed = true;
            } else if (isSchWebComboOpen) {
                if (hoverSchWebOptIdx != -1 && editingProfileIdx >= 0) {
                    g_profiles[editingProfileIdx].blockedWebsites.push_back({schCommonWebsites[hoverSchWebOptIdx], false});
                }
                isSchWebComboOpen = false; return;
            }

            if (isSchAppComboOpen && !hoverSchAppCombo && hoverSchAppOptIdx == -1) {
                isSchAppComboOpen = false; dropdownClosed = true;
            } else if (isSchAppComboOpen) {
                if (hoverSchAppOptIdx != -1 && editingProfileIdx >= 0) {
                    g_profiles[editingProfileIdx].blockedApps.push_back({schCommonApps[hoverSchAppOptIdx], false});
                }
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
                g_profiles[editingProfileIdx].lockMode = tempLockMode;
                for(int d=0; d<7; d++) g_profiles[editingProfileIdx].activeDays[d] = editDays[d];
                g_profiles[editingProfileIdx].startHour = editStH; g_profiles[editingProfileIdx].startMin = editStM;
                g_profiles[editingProfileIdx].endHour = editEnH; g_profiles[editingProfileIdx].endMin = editEnM;
                g_profiles[editingProfileIdx].blockInternet = editBlockInt;
                g_profiles[editingProfileIdx].blockAdult = editBlockAdult;
                g_profiles[editingProfileIdx].blockUninstall = editBlockUninst;
            }
            LogHistoryToHiddenFolderSch(L"Saved Profile: " + inpProfileName);
            editingProfileIdx = -1; SaveProfiles(); return;
        }

        if (g_ehb.scrollbarTrack.Contains(x, y)) return;

        if (g_ehb.scrollArea.Contains(x, y)) {
            if (s_activeSubTab == 0) {
                if (hoverSchModeDropdown) { isSchModeDropdownOpen = true; return; }

                if(g_ehb.hDay != -1) editDays[g_ehb.hDay] = !editDays[g_ehb.hDay];

                if(g_ehb.hStH_Up) { editStH = (editStH + 1) % 24; } if(g_ehb.hStH_Dn) { editStH = (editStH - 1 + 24) % 24; }
                if(g_ehb.hStM_Up) { editStM = (editStM + 5) % 60; } if(g_ehb.hStM_Dn) { editStM = (editStM - 5 + 60) % 60; }
                if(g_ehb.hEnH_Up) { editEnH = (editEnH + 1) % 24; } if(g_ehb.hEnH_Dn) { editEnH = (editEnH - 1 + 24) % 24; }
                if(g_ehb.hEnM_Up) { editEnM = (editEnM + 5) % 60; } if(g_ehb.hEnM_Dn) { editEnM = (editEnM - 5 + 60) % 60; }
                
                activeInput = g_ehb.nameInp.Contains(x,y) ? 1 : 0;
            } 
            else if (s_activeSubTab == 1) {
                if(g_ehb.hTogInt) editBlockInt = !editBlockInt;
                if(g_ehb.hTogAdt) editBlockAdult = !editBlockAdult;
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
                        if (!found) {
                            if (!qb.keywords.empty()) {
                                auto& keys = g_profiles[editingProfileIdx].blockedKeywords;
                                for (auto it = keys.begin(); it != keys.end(); ++it) {
                                    if (it->name == qb.keywords[0]) { keys.erase(it); found = true; break; }
                                }
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
                    
                    auto& webs = g_profiles[editingProfileIdx].blockedWebsites;
                    for(auto it = webs.begin(); it != webs.end();) { if(it->isHoveredCross) it = webs.erase(it); else ++it; }
                    
                    auto& apps = g_profiles[editingProfileIdx].blockedApps;
                    for(auto it = apps.begin(); it != apps.end();) { if(it->isHoveredCross) it = apps.erase(it); else ++it; }
                    
                    auto& keys = g_profiles[editingProfileIdx].blockedKeywords;
                    for(auto it = keys.begin(); it != keys.end();) { if(it->isHoveredCross) it = keys.erase(it); else ++it; }
                }
            }
        }
        return;
    }

    if (hAddProfileBtn) {
        editingProfileIdx = -2; 
        s_activeSubTab = 0; // Default open with Basic Tab
        inpProfileName = L""; inpWeb = L""; inpApp = L""; inpKey = L"";
        tempLockMode = 0;
        for(int d=0; d<7; d++) editDays[d] = false;
        editStH = 0; editStM = 0; editEnH = 23; editEnM = 59;
        editBlockInt = false; editBlockAdult = false; editBlockUninst = true;
        edit_tScroll = 0.0f; edit_cScroll = 0.0f; 
        
        FocusProfile np; np.profileName = L""; np.lockMode = 0;
        g_profiles.push_back(np);
        editingProfileIdx = g_profiles.size() - 1;
        
        activeInput = 1; return;
    }

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        if (g_profiles[i].hToggle) { 
            activeActionProfileIdx = i;
            if (!g_profiles[i].isActive) {
                if (g_profiles[i].lockMode == 0) { s_showTimeOverlay = true; }
                else if (g_profiles[i].lockMode == 1) { s_showPassOverlay = true; s_isStoppingFocus = false; s_inputPassText = L""; }
                else if (g_profiles[i].lockMode == 2) { 
                    g_profiles[i].isActive = true;
                    ApplyProfileBlocking(i, true);
                    LogHistoryToHiddenFolderSch(L"Started Schedule: " + g_profiles[i].profileName);
                    SaveProfiles(); 
                }
            } else {
                if (g_profiles[i].lockMode == 0) { /* Block premature stopping */ }
                else if (g_profiles[i].lockMode == 1) { s_showPassOverlay = true; s_isStoppingFocus = true; s_inputPassText = L""; }
                else if (g_profiles[i].lockMode == 2) { s_showTextUnlockOverlay = true; s_currentTypingText = L""; s_isTypingActive = true; }
            }
        }
        if (g_profiles[i].hEdit) {
            editingProfileIdx = i;
            s_activeSubTab = 0; // Reset Tab
            inpProfileName = g_profiles[i].profileName;
            tempLockMode = g_profiles[i].lockMode;
            for(int d=0; d<7; d++) editDays[d] = g_profiles[i].activeDays[d];
            editStH = g_profiles[i].startHour; editStM = g_profiles[i].startMin;
            editEnH = g_profiles[i].endHour; editEnM = g_profiles[i].endMin;
            editBlockInt = g_profiles[i].blockInternet;
            editBlockAdult = g_profiles[i].blockAdult;
            editBlockUninst = g_profiles[i].blockUninstall;
            edit_tScroll = 0.0f; edit_cScroll = 0.0f; 

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
// --- KEYBOARD LOGIC ---
// ==========================================
void ProcessScheduleBlocksKeyPress(wchar_t c) {
    if (s_showPassOverlay && s_isPassInputActive) {
        if (c >= 32 && c <= 126 && s_inputPassText.length() < 20) s_inputPassText += c;
    } else if (s_showTextUnlockOverlay && s_isTypingActive) {
        if (c >= 32 && c <= 126 && s_currentTypingText.length() < 1000) s_currentTypingText += c;
    } else if (editingProfileIdx != -1 && activeInput != 0) {
        if (c >= 32 && c <= 126) {
            if (activeInput == 1 && inpProfileName.length() < 30) inpProfileName += c;
            if (activeInput == 2 && inpWeb.length() < 40) inpWeb += c;
            if (activeInput == 3 && inpApp.length() < 40) inpApp += c;
            if (activeInput == 4 && inpKey.length() < 40) inpKey += c;
        }
    }
}

void ProcessScheduleBlocksKeyDown(WPARAM key) {
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
            if (activeInput == 2 && !inpWeb.empty()) inpWeb.pop_back();
            if (activeInput == 3 && !inpApp.empty()) inpApp.pop_back();
            if (activeInput == 4 && !inpKey.empty()) inpKey.pop_back();
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

// 100% Smooth Scrolling Update with System Integration
void ProcessScheduleBlocksMouseWheel(float x, float y, int delta) {
    UINT scrollLines = 3; // Default
    SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &scrollLines, 0);
    float scrollStep = (float)scrollLines * 25.0f; 
    int steps = (delta > 0) ? 1 : -1;

    if (editingProfileIdx != -1) {
        isSchModeDropdownOpen = false; isSchWebComboOpen = false; isSchAppComboOpen = false;
        edit_tScroll -= steps * scrollStep;
        edit_tScroll = (std::max)(0.0f, (std::min)(edit_tScroll, edit_maxScroll));
        return;
    }
    
    sch_tScroll -= steps * scrollStep;
    float totalRows = ceil((float)g_profiles.size() / 2.0f);
    float maxScroll = (std::max)(0.0f, (totalRows * 190.0f) - (s_ch - 100.0f));
    sch_tScroll = (std::max)(0.0f, (std::min)(sch_tScroll, maxScroll));
}
