#pragma warning(disable : 4996)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)

#include "tab_schedule_blocks.h"
#include "tab_adult.h"
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <shlobj.h>
#include <codecvt>
#include <locale>
#include <algorithm>
#include <ctime>
#include <thread>
#include <mutex>
#include <wininet.h>
#include <shellapi.h>
#include <commdlg.h>

#pragma comment(lib, "wininet.lib")

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;
static std::mutex g_schMutex;
static bool isSchThreadRunning = false;

// ==========================================
// BACKGROUND GLOBAL HELPERS
// ==========================================
static void CloseActiveTabOnly(HWND hBrowser) {
    if (GetForegroundWindow() == hBrowser) {
        keybd_event(VK_CONTROL, 0, 0, 0);
        keybd_event('W', 0, 0, 0);
        keybd_event('W', 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    }
}

static void SetInternetStateSch(bool block) {
    string cmd = block ? "ipconfig /release" : "ipconfig /renew";
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
}

static vector<wstring> hardcoreKeywords = {
    L"porn", L"xxx", L"sex", L"nude", L"nsfw", L"hentai", L"milf",
    L"blowjob", L"xvideos", L"pornhub", L"xnxx", L"xhamster", L"brazzers",
    L"onlyfans", L"chaturbate", L"spankbang", L"redtube", L"youporn",
    L"\u099A\u099F\u09BF", L"\u09AA\u09B0\u09CD\u09A3", L"\u09B8\u09C7\u0995\u09CD\u09B8",
    L"\u09A8\u0997\u09CD\u09A8", L"bhabi", L"chudai", L"bangla choti",
    L"panu", L"magi", L"choda", L"randi"
};
static vector<wstring> romanticKeywords = {
    L"hot dance", L"seductive", L"item song", L"belly dance", L"kissing scene",
    L"bikini", L"sexy dance", L"cleavage", L"semi nude", L"lingerie",
    L"erotic", L"navel show"
};
static vector<wstring> g_adultResourceSites;

static void LoadAdultSitesFromResourceOnce() {
    if (!g_adultResourceSites.empty()) return;
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(105), RT_RCDATA);
    if (hRes) {
        HGLOBAL hLoad = LoadResource(NULL, hRes);
        char* pData = (char*)LockResource(hLoad);
        DWORD size = SizeofResource(NULL, hRes);
        if (pData && size > 0) {
            string s(pData, size); stringstream ss(s); string line;
            while (getline(ss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) g_adultResourceSites.push_back(wstring(line.begin(), line.end()));
            }
        }
    }
    if (g_adultResourceSites.empty()) {
        g_adultResourceSites = { L"pornhub.com", L"xvideos.com", L"xnxx.com", L"xhamster.com", L"redtube.com" };
    }
}

// ==========================================
// HELPERS & COLORS
// ==========================================
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

static GraphicsPath* GetSchRoundRectPath(RectF rect, float radius) {
    GraphicsPath* path = new GraphicsPath();
    float x = rect.X, y = rect.Y, w = rect.Width, h = rect.Height, d = radius * 2.0f;
    if (w < d) d = w; if (h < d) d = h; // FIX: prevent negative arc
    path->AddArc(x, y, d, d, 180.0f, 90.0f);
    path->AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path->AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path->AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path->CloseFigure();
    return path;
}

// FIX: Safe cursor blink helper - no more GetDesktopWindow() Graphics
static float MeasureStringWidth(const wstring& text, Font* font, Graphics& g) {
    if (text.empty()) return 0.0f;
    RectF bR;
    StringFormat sf;
    g.MeasureString(text.c_str(), -1, font, PointF(0, 0), &sf, &bR);
    return bR.Width;
}

static void DrawSchOverlaySpinner(Graphics& g, float x, float y, const std::wstring& value,
    bool hMinus, bool hPlus, Font* fIcon, Font* fBold) {
    SolidBrush bDark(Color(255, 50, 50, 50)), bWhite(Color(255, 255, 255, 255));
    Color colTeal(255, 12, 168, 176), colTealHover(255, 30, 185, 195);
    Pen pThin(Color(255, 200, 210, 220), 1.5f);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    RectF minusRect(x, y, 36.0f, 36.0f);
    GraphicsPath* mp = GetSchRoundRectPath(minusRect, 6);
    SolidBrush mBr(hMinus ? colTealHover : colTeal);
    g.FillPath(&mBr, mp); delete mp;
    g.DrawString(L"-", -1, fBold, minusRect, &fC, &bWhite);

    RectF valRect(x + 40.0f, y, 50.0f, 36.0f);
    GraphicsPath* vp = GetSchRoundRectPath(valRect, 4);
    SolidBrush vBr(Color(255, 248, 250, 252));
    g.FillPath(&vBr, vp); g.DrawPath(&pThin, vp); delete vp;
    g.DrawString(value.c_str(), -1, fBold, valRect, &fC, &bDark);

    RectF plusRect(x + 94.0f, y, 36.0f, 36.0f);
    GraphicsPath* pp = GetSchRoundRectPath(plusRect, 6);
    SolidBrush pBr(hPlus ? colTealHover : colTeal);
    g.FillPath(&pBr, pp); delete pp;
    g.DrawString(L"+", -1, fBold, plusRect, &fC, &bWhite);
}

// ==========================================
// DATA STRUCTURES & GLOBALS
// ==========================================
struct SchBlockItem { wstring name; bool isHoveredCross = false; };

struct FocusProfile {
    wstring profileName;
    vector<SchBlockItem> blockedWebsites;
    vector<SchBlockItem> blockedApps;
    vector<SchBlockItem> adultCustomKeywords;

    bool isActive = false;
    int lockMode = 0;
    time_t lockEndTime = 0;
    wstring parentsPassword = L"";

    bool activeDays[7] = { false };
    int startHour = 9, startMin = 0, endHour = 17, endMin = 0;
    bool blockInternet = false, blockUninstall = true;

    bool qbYTShorts = false, qbFBReels = false, qbYTAds = false, qbIGReels = false;
    bool schAdultWeb = false, schHardcore = false, schRomantic = false, schStrictDns = false;

    bool hToggle = false, hEdit = false, hDel = false;
};

static vector<FocusProfile> g_profiles;
static bool isSchDataLoaded = false;
static float sch_tScroll = 0.0f, sch_cScroll = 0.0f;
static float s_cx = 0, s_cy = 0, s_cw = 800, s_ch = 600;

static int s_activeSubTab = 0;
// FIX: Array size 4 (was 3, caused out-of-bounds when index 3 accessed)
static float s_listScrollT[4] = { 0, 0, 0, 0 };
static float s_listScrollC[4] = { 0, 0, 0, 0 };
static float s_listScrollMax[4] = { 0, 0, 0, 0 };
static bool s_scrollbarDragging = false;
static float s_scrollbarDragStartY = 0.0f, s_scrollbarDragStartScroll = 0.0f;

static vector<wstring> schCommonWebsites = {
    L"facebook.com", L"youtube.com", L"instagram.com",
    L"tiktok.com", L"reddit.com", L"twitter.com"
};
static vector<wstring> schCommonApps = {
    L"chrome.exe", L"msedge.exe", L"telegram.exe",
    L"discord.exe", L"vlc.exe", L"Taskmgr.exe", L"cmd.exe"
};
static vector<wstring> schCommonStoreApps = {
    L"Netflix", L"WhatsApp", L"Spotify", L"Instagram", L"TikTok", L"Facebook"
};

struct QuickBlockBtn { wstring label; bool hovered = false; };
static vector<QuickBlockBtn> s_quickBlocks = {
    { L"YT Shorts" }, { L"FB Reels" }, { L"YT Ads" }, { L"IG Reels" }
};
static vector<RectF> s_quickBlockRects;

static int editingProfileIdx = -1;
static wstring inpProfileName = L"", inpWeb = L"", inpApp = L"", inpKey = L"";
static int activeInput = 0;
static bool editDays[7] = { false };
static int editStH = 9, editStM = 0, editEnH = 17, editEnM = 0;
static bool editBlockInt = false, editBlockUninst = true;

static bool hAddProfileBtn = false, hoverSchWebCombo = false, hoverSchAppCombo = false;
static bool hoverSchStoreCombo = false, hoverSchModeDropdown = false;
static int hoverSchWebOptIdx = -1, hoverSchAppOptIdx = -1, hoverSchStoreOptIdx = -1;
static int tempLockMode = 0;
static bool isSchWebComboOpen = false, isSchAppComboOpen = false;
static bool isSchStoreComboOpen = false, isSchModeDropdownOpen = false;

static int activeActionProfileIdx = -1;
static bool s_showTimeOverlay = false, s_showPassOverlay = false, s_showTextUnlockOverlay = false;
static int s_focusMonths = 0, s_focusDays = 0, s_focusHours = 1, s_focusMins = 0;
static bool s_hTimeMoM = false, s_hTimeMoP = false, s_hTimeDM = false, s_hTimeDP = false;
static bool s_hTimeHM = false, s_hTimeHP = false, s_hTimeMM = false, s_hTimeMP = false;
static bool s_hTimeStart = false, s_hTimeCancel = false;
static wstring s_inputPassText = L"", s_currentTypingText = L"";
static bool s_isPassInputActive = true, s_hPassInput = false, s_hPassConfirm = false;
static bool s_hPassCancel = false, s_isStoppingFocus = false, s_isTypingActive = true;
static bool s_hTextUnlockConfirm = false, s_hTextUnlockCancel = false;
static wstring s_targetUnlockText = L"To unlock this PC, you must realize that focus is the key to success. Avoid distractions, work hard, and never give up. True discipline comes from within. Success is not an accident, it is hard work, perseverance, learning, studying, sacrifice and most of all, love of what you are doing or learning to do. Type this exact text carefully to regain access and prove your self-control.";

static const Color
    ClrTeal(255, 12, 168, 176),
    ClrTealHover(255, 30, 185, 195),
    ClrTealLight(255, 220, 248, 250),
    ClrDark(255, 32, 38, 46),
    ClrGrayText(255, 120, 130, 145),
    ClrWhite(255, 255, 255, 255),
    ClrBg(255, 245, 248, 251),
    ClrCardBg(255, 255, 255, 255),
    ClrBgHover(255, 232, 246, 248),
    ClrRed(255, 220, 60, 50),
    ClrGreen(255, 40, 180, 100),
    ClrOverlay(200, 10, 15, 25),
    ClrDisabled(255, 200, 205, 210),
    ClrBorder(255, 220, 228, 236),
    ClrActiveBorder(255, 12, 168, 176);

struct EditHitboxes {
    RectF saveBtn, cancelBtn, nextBtn, backBtn;
    RectF subTabRects[4];
    int hSubTab = -1;

    RectF nameInp, modeDrop, days[7], stH_Box, stM_Box, stAmPm, enH_Box, enM_Box, enAmPm, togInt, togUni;
    RectF webInp, webCombo, addWeb, appInp, appCombo, addApp, keyInp, addKey;
    RectF btnAddExe, btnAddStore, btnAddTitle;
    bool hBtnAddExe = false, hBtnAddStore = false, hBtnAddTitle = false;

    RectF cbAdultWeb, cbHardcore, cbRomantic, cbStrictDns;
    bool hCbAdultWeb = false, hCbHardcore = false, hCbRomantic = false, hCbStrictDns = false;

    vector<pair<RectF, int>> webDel, appDel, keyDel;
    RectF listAreas[3];
    RectF modeOpt[3];
    vector<RectF> webOpts, appOpts, storeOpts;

    bool hSave = false, hCancel = false, hNext = false, hBack = false;
    bool hStH = false, hStM = false, hStAmPm = false, hEnH = false, hEnM = false, hEnAmPm = false;
    bool hTogInt = false, hTogUni = false, hAddWeb = false, hAddApp = false, hAddKey = false;
    bool hOptSelf = false, hOptParents = false, hOptLongText = false;
    int hDay = -1;
} g_ehb;

// ==========================================
// BLOCKING LOGIC
// ==========================================
static vector<wstring> GetAllBlockPatterns(const wstring& entry) {
    vector<wstring> patterns; wstring e = entry;
    if (e.size() >= 8 && e.substr(0, 8) == L"https://") e = e.substr(8);
    else if (e.size() >= 7 && e.substr(0, 7) == L"http://") e = e.substr(7);
    if (e.size() >= 4 && e.substr(0, 4) == L"www.") e = e.substr(4);
    patterns.push_back(e);
    patterns.push_back(L"www." + e);
    if (e == L"youtube.com") {
        patterns.push_back(L"m.youtube.com");
        patterns.push_back(L"youtu.be");
        patterns.push_back(L"yt3.ggpht.com");
    }
    else if (e == L"facebook.com") {
        patterns.push_back(L"m.facebook.com");
        patterns.push_back(L"l.facebook.com");
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
    if (fin) { wstring ln; while (getline(fin, ln)) content += ln + L"\n"; fin.close(); }

    for (const auto& pat : patterns) {
        if (pat.size() >= 3 && pat.substr(0, 3) == L"kw:") continue;
        wstring blockLine = L"0.0.0.0 " + pat;
        wstring fullLine = blockLine + L" # RasFocus";
        if (block) {
            if (content.find(blockLine) == wstring::npos) content += fullLine + L"\n";
        }
        else {
            wstring newContent; size_t pos = 0;
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
    ShellExecuteA(NULL, "open", "cmd.exe", "/c ipconfig /flushdns", NULL, SW_HIDE);
}

static void ApplyPACFileBlocking(const vector<wstring>& patterns, bool block) {
    wchar_t appData[MAX_PATH];
    if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) return;
    wstring pacPath = wstring(appData) + L"\\RasFocus\\block.pac";
    wstring pacDir = wstring(appData) + L"\\RasFocus";
    CreateDirectoryW(pacDir.c_str(), NULL);

    vector<wstring> allKw; bool blockAllInternet = false;
    for (const auto& p : g_profiles) {
        if (!p.isActive) continue;
        if (p.blockInternet) blockAllInternet = true;
        for (const auto& w : p.blockedWebsites) {
            auto pats = GetAllBlockPatterns(w.name);
            for (const auto& pt : pats) {
                if (pt.size() >= 3 && pt.substr(0, 3) == L"kw:") allKw.push_back(pt.substr(3));
            }
        }
        for (const auto& kw : p.adultCustomKeywords) allKw.push_back(kw.name);
    }

    wstring pac = L"function FindProxyForURL(url, host) {\n";
    if (blockAllInternet) { pac += L"  return \"PROXY 127.0.0.1:1\";\n"; }
    else { for (const auto& kw : allKw) pac += L"  if (url.indexOf(\"" + kw + L"\") !== -1) return \"PROXY 127.0.0.1:1\";\n"; }
    pac += L"  return \"DIRECT\";\n}\n";

    wofstream fout(pacPath);
    fout.imbue(locale(fout.getloc(), new codecvt_utf8<wchar_t>));
    if (fout) { fout << pac; fout.close(); }

    if (block && (!allKw.empty() || blockAllInternet)) {
        string pacUrl = "file://" + string(pacPath.begin(), pacPath.end());
        string cmd1 = "/c reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\" /v AutoConfigURL /t REG_SZ /d \"" + pacUrl + "\" /f";
        ShellExecuteA(NULL, "open", "cmd.exe", cmd1.c_str(), NULL, SW_HIDE);
        ShellExecuteA(NULL, "open", "cmd.exe", "/c reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\" /v ProxyEnable /t REG_DWORD /d 0 /f", NULL, SW_HIDE);
        InternetSetOptionA(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0);
        InternetSetOptionA(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
    }
    else if (allKw.empty() && !blockAllInternet) {
        ShellExecuteA(NULL, "open", "cmd.exe", "/c reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\" /v AutoConfigURL /f", NULL, SW_HIDE);
        InternetSetOptionA(NULL, INTERNET_OPTION_SETTINGS_CHANGED, NULL, 0);
        InternetSetOptionA(NULL, INTERNET_OPTION_REFRESH, NULL, 0);
    }
}

static void KillBlockedApps(const vector<SchBlockItem>& apps) {
    if (apps.empty()) return;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            wstring procLower = pe.szExeFile;
            transform(procLower.begin(), procLower.end(), procLower.begin(), ::towlower);
            for (const auto& app : apps) {
                wstring appLower = app.name;
                transform(appLower.begin(), appLower.end(), appLower.begin(), ::towlower);
                if (procLower == appLower) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) { TerminateProcess(hProc, 1); CloseHandle(hProc); }
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
    if (p.qbYTAds) {
        allPatterns.push_back(L"googlevideo.com");
        allPatterns.push_back(L"doubleclick.net");
        allPatterns.push_back(L"kw:youtube.com/pagead");
    }
    if (p.blockInternet) {
        allPatterns.push_back(L"www.msftconnecttest.com");
        allPatterns.push_back(L"ipv6.msftconnecttest.com");
        SetInternetStateSch(enable);
    }
    if (p.schStrictDns && enable) { AdultBlock_ApplyForSchedule(enable); }

    ApplyHostsFileBlocking(allPatterns, enable);
    ApplyPACFileBlocking(allPatterns, enable);
    if (enable && !p.blockedApps.empty()) KillBlockedApps(p.blockedApps);
}

// ==========================================
// HISTORY & SAVE/LOAD SYSTEM
// ==========================================
void LogHistoryToHiddenFolderSch(wstring action) {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        wstring historyDir = wstring(path) + L"\\RasFocus\\History";
        CreateDirectoryW(historyDir.c_str(), NULL);
        SetFileAttributesW(historyDir.c_str(), FILE_ATTRIBUTE_HIDDEN);
        wstring logFile = historyDir + L"\\schedule_activity_log.txt";
        ofstream out(string(logFile.begin(), logFile.end()).c_str(), ios::app);
        time_t now = std::time(0); string dt = std::ctime(&now);
        if (!dt.empty()) dt.pop_back();
        if (out) out << "[" << dt << "] " << string(action.begin(), action.end()) << "\n";
    }
}

static wstring GetSchSavePath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        wstring fullPath = wstring(path) + L"\\RasFocus";
        CreateDirectoryW(fullPath.c_str(), NULL);
        return fullPath + L"\\custom_profiles_v3.dat";
    }
    return L"";
}

static void SaveProfiles() {
    wstring savePath = GetSchSavePath();
    if (savePath.empty()) return;
    wofstream out(string(savePath.begin(), savePath.end()));
    out.imbue(locale(out.getloc(), new codecvt_utf8<wchar_t>));
    if (!out) return;
    out << g_profiles.size() << L"\n";
    for (const auto& p : g_profiles) {
        out << p.profileName << L"\n" << p.isActive << L"\n" << p.lockMode << L"\n"
            << p.lockEndTime << L"\n" << p.parentsPassword << L"\n";
        for (int i = 0; i < 7; i++) out << p.activeDays[i] << L" ";
        out << L"\n" << p.startHour << L" " << p.startMin << L" "
            << p.endHour << L" " << p.endMin << L"\n";
        out << p.blockInternet << L" " << p.blockUninstall << L"\n";
        out << p.qbYTShorts << L" " << p.qbFBReels << L" " << p.qbYTAds << L" " << p.qbIGReels << L"\n";
        out << p.schAdultWeb << L" " << p.schHardcore << L" " << p.schRomantic << L" " << p.schStrictDns << L"\n";
        out << p.blockedWebsites.size() << L"\n";
        for (const auto& w : p.blockedWebsites) out << w.name << L"\n";
        out << p.blockedApps.size() << L"\n";
        for (const auto& a : p.blockedApps) out << a.name << L"\n";
        out << p.adultCustomKeywords.size() << L"\n";
        for (const auto& k : p.adultCustomKeywords) out << k.name << L"\n";
    }
    out.close();
}

static void LoadProfiles() {
    wstring savePath = GetSchSavePath();
    if (savePath.empty()) return;
    wifstream in(string(savePath.begin(), savePath.end()));
    in.imbue(locale(in.getloc(), new codecvt_utf8<wchar_t>));
    if (!in) {
        FocusProfile defProfile;
        defProfile.profileName = L"Deep Work Session";
        defProfile.blockedWebsites.push_back({ L"facebook.com", false });
        defProfile.blockedApps.push_back({ L"discord.exe", false });
        defProfile.schAdultWeb = true; defProfile.schHardcore = true;
        for (int i = 1; i <= 5; i++) defProfile.activeDays[i] = true;
        g_profiles.push_back(defProfile);
        return;
    }
    size_t pCount = 0;
    in >> pCount; in.ignore(); g_profiles.clear();
    for (size_t i = 0; i < pCount; ++i) {
        FocusProfile p;
        getline(in, p.profileName);
        in >> p.isActive >> p.lockMode >> p.lockEndTime;
        in.ignore(); getline(in, p.parentsPassword);
        for (int d = 0; d < 7; d++) in >> p.activeDays[d];
        in >> p.startHour >> p.startMin >> p.endHour >> p.endMin
            >> p.blockInternet >> p.blockUninstall;
        if (in >> p.qbYTShorts >> p.qbFBReels >> p.qbYTAds >> p.qbIGReels
            >> p.schAdultWeb >> p.schHardcore >> p.schRomantic >> p.schStrictDns) {
            in.ignore();
        }
        else { in.clear(); in.ignore(); }
        size_t wCount = 0;
        if (in >> wCount) {
            in.ignore();
            for (size_t j = 0; j < wCount; ++j) {
                wstring w; getline(in, w);
                if (!w.empty()) p.blockedWebsites.push_back({ w, false });
            }
        }
        size_t aCount = 0;
        if (in >> aCount) {
            in.ignore();
            for (size_t j = 0; j < aCount; ++j) {
                wstring a; getline(in, a);
                if (!a.empty()) p.blockedApps.push_back({ a, false });
            }
        }
        size_t kCount = 0;
        if (in >> kCount) {
            in.ignore();
            for (size_t j = 0; j < kCount; ++j) {
                wstring k; getline(in, k);
                if (!k.empty()) p.adultCustomKeywords.push_back({ k, false });
            }
        }
        g_profiles.push_back(p);
    }
    in.close();
}

// ==========================================
// BACKGROUND OBSERVER THREAD
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
            if (p.isActive && p.lockMode == 0 && p.lockEndTime > 0 && t >= p.lockEndTime) {
                p.isActive = false; p.lockEndTime = 0;
                ApplyProfileBlocking((int)i, false);
                profilesChanged = true;
            }
            bool hasSchedule = false;
            for (int d = 0; d < 7; d++) { if (p.activeDays[d]) hasSchedule = true; }
            if (hasSchedule && p.lockMode == 0 && p.lockEndTime == 0) {
                int startTotalMins = p.startHour * 60 + p.startMin;
                int endTotalMins = p.endHour * 60 + p.endMin;
                bool shouldBeActive = false;
                if (p.activeDays[currentDay]) {
                    shouldBeActive = (startTotalMins <= endTotalMins)
                        ? (currentTotalMins >= startTotalMins && currentTotalMins < endTotalMins)
                        : (currentTotalMins >= startTotalMins || currentTotalMins < endTotalMins);
                }
                if (shouldBeActive && !p.isActive) {
                    p.isActive = true; ApplyProfileBlocking((int)i, true); profilesChanged = true;
                }
                else if (!shouldBeActive && p.isActive) {
                    p.isActive = false; ApplyProfileBlocking((int)i, false); profilesChanged = true;
                }
            }
        }
        if (profilesChanged) {
            SaveProfiles();
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
        }
        vector<FocusProfile> safeProfiles = g_profiles;
        g_schMutex.unlock();

        // Heavy work outside lock
        for (const auto& p : safeProfiles) {
            if (p.isActive && !p.blockedApps.empty()) KillBlockedApps(p.blockedApps);
            if (p.isActive) {
                HWND hActive = GetForegroundWindow();
                if (hActive) {
                    wchar_t windowTitle[512] = { 0 };
                    if (GetWindowTextW(hActive, windowTitle, 512) > 0) {
                        wstring lowerTitle = windowTitle;
                        for (auto& c : lowerTitle) c = towlower(c);
                        bool triggerBlock = false;
                        if (p.qbYTShorts && lowerTitle.find(L"youtube") != wstring::npos && lowerTitle.find(L"shorts") != wstring::npos) triggerBlock = true;
                        if (p.qbFBReels && lowerTitle.find(L"facebook") != wstring::npos && lowerTitle.find(L"reels") != wstring::npos) triggerBlock = true;
                        if (p.qbIGReels && lowerTitle.find(L"instagram") != wstring::npos && lowerTitle.find(L"reels") != wstring::npos) triggerBlock = true;
                        if (p.schHardcore && !triggerBlock) {
                            for (const auto& kw : hardcoreKeywords) {
                                if (lowerTitle.find(kw) != wstring::npos) { triggerBlock = true; break; }
                            }
                        }
                        if (p.schRomantic && !triggerBlock) {
                            for (const auto& kw : romanticKeywords) {
                                if (lowerTitle.find(kw) != wstring::npos) { triggerBlock = true; break; }
                            }
                        }
                        if (p.schAdultWeb && !triggerBlock) {
                            LoadAdultSitesFromResourceOnce();
                            for (const auto& site : g_adultResourceSites) {
                                size_t dot = site.find(L".");
                                wstring core = (dot != wstring::npos) ? site.substr(0, dot) : site;
                                if (core.length() > 2 && lowerTitle.find(core) != wstring::npos) {
                                    triggerBlock = true; break;
                                }
                            }
                        }
                        if (!triggerBlock) {
                            for (const auto& kw : p.adultCustomKeywords) {
                                wstring lowerKw = kw.name;
                                for (auto& c : lowerKw) c = towlower(c);
                                if (lowerTitle.find(lowerKw) != wstring::npos) { triggerBlock = true; break; }
                            }
                        }
                        if (triggerBlock) CloseActiveTabOnly(hActive);
                    }
                }
            }
        }
    }
}

// ==========================================
// DRAWING LOGIC
// ==========================================
void DrawScheduleBlocksTab(Graphics& g, float x, float y, float w, float h) {
    if (!isSchDataLoaded) {
        LoadProfiles(); isSchDataLoaded = true;
        if (!isSchThreadRunning) {
            std::thread(ScheduleObserverThread).detach();
            isSchThreadRunning = true;
        }
    }

    std::lock_guard<std::mutex> lock(g_schMutex);
    s_cx = x; s_cy = y; s_cw = w; s_ch = h;
    sch_cScroll += (sch_tScroll - sch_cScroll) * 0.12f;

    // Fonts
    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff, 22, FontStyleBold, UnitPixel);
    Font fCardTitle(&ff, 16, FontStyleBold, UnitPixel);
    Font fNorm(&ff, 14, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 14, FontStyleBold, UnitPixel);
    Font fSmall(&ff, 12, FontStyleRegular, UnitPixel);
    Font fSmallBold(&ff, 12, FontStyleBold, UnitPixel);
    FontFamily ffi(L"Segoe MDL2 Assets");
    Font fIcon(&ffi, 18, FontStyleRegular, UnitPixel);
    Font fSmallIcon(&ffi, 13, FontStyleRegular, UnitPixel);

    // Brushes & Pens
    SolidBrush bDark(ClrDark), bWhite(ClrWhite), bGray(ClrGrayText);
    SolidBrush bTeal(ClrTeal), bRed(ClrRed), bGreen(ClrGreen);
    SolidBrush bBgHover(ClrBgHover), bBg(ClrBg), bCardBg(ClrCardBg);
    Pen pThin(ClrBorder, 1.0f);
    Pen pTeal(ClrTeal, 1.5f);
    Pen pFocus(ClrActiveBorder, 2.0f);

    StringFormat fL, fC, fTL;
    fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);
    fTL.SetAlignment(StringAlignmentNear); fTL.SetLineAlignment(StringAlignmentNear);
    fL.SetFormatFlags(StringFormatFlagsNoWrap);
    fL.SetTrimming(StringTrimmingEllipsisCharacter);

    // Background
    g.FillRectangle(&bBg, x, y, w, h);

    // ==========================================
    // MAIN VIEW: PROFILE LIST
    // ==========================================
    if (editingProfileIdx == -1 && !s_showTimeOverlay && !s_showPassOverlay && !s_showTextUnlockOverlay) {

        // Header bar
        SolidBrush bHeaderBg(ClrCardBg);
        g.FillRectangle(&bHeaderBg, x, y, w, 80.0f);
        Pen pHeaderBorder(ClrBorder, 1.0f);
        g.DrawLine(&pHeaderBorder, x, y + 79.0f, x + w, y + 79.0f);

        // Title
        g.DrawString(L"Focus Profiles", -1, &fTitle,
            RectF(x + 24, y + 18, 300, 30), &fL, &bDark);
        g.DrawString(L"Schedule-based blocking with app, web & internet control", -1, &fSmall,
            RectF(x + 24, y + 52, 500, 18), &fL, &bGray);

        // Add button - premium style
        RectF addBtnRect(x + w - 210, y + 22, 185, 38);
        GraphicsPath* aP = GetSchRoundRectPath(addBtnRect, 6);
        SolidBrush aBr(hAddProfileBtn ? ClrTealHover : ClrTeal);
        g.FillPath(&aBr, aP); delete aP;
        g.DrawString(L"+ New Profile", -1, &fBold, addBtnRect, &fC, &bWhite);

        // Profile cards
        float cardW2 = (w - 56.0f) / 2.0f;
        float cardH = 180.0f;
        float startX = x + 20.0f;
        float startY = y + 96.0f - sch_cScroll;

        Region oldClip; g.GetClip(&oldClip);
        g.SetClip(RectF(x, y + 90.0f, w, h - 90.0f));

        for (size_t i = 0; i < g_profiles.size(); ++i) {
            float cX = startX + (i % 2) * (cardW2 + 16.0f);
            float cY = startY + (i / 2) * (cardH + 16.0f);
            if (cY > y + h || cY + cardH < y + 85.0f) continue;

            const auto& prof = g_profiles[i];
            bool isActive = prof.isActive;

            // Card shadow effect
            SolidBrush shadowBr(Color(18, 0, 0, 0));
            for (int si = 3; si >= 1; si--) {
                GraphicsPath* sP = GetSchRoundRectPath(RectF(cX + si, cY + si, cardW2, cardH), 8);
                g.FillPath(&shadowBr, sP); delete sP;
            }

            // Card body
            GraphicsPath* cP = GetSchRoundRectPath(RectF(cX, cY, cardW2, cardH), 8);
            g.FillPath(&bCardBg, cP);
            Pen pCard(isActive ? ClrActiveBorder : ClrBorder, isActive ? 2.0f : 1.0f);
            g.DrawPath(&pCard, cP); delete cP;

            // Active indicator strip
            if (isActive) {
                GraphicsPath stripPath;
                AddRoundedRectPath(stripPath, cX, cY, 4.0f, cardH, 2.0f);
                g.FillPath(&bTeal, &stripPath);
            }

            // Icon
            SolidBrush* iClr = isActive ? &bTeal : &bGray;
            g.DrawString(L"\xE82D", -1, &fIcon, RectF(cX + 18, cY + 16, 26, 26), &fC, iClr);

            // Profile name
            g.DrawString(prof.profileName.c_str(), -1, &fCardTitle,
                RectF(cX + 50, cY + 16, cardW2 - 65, 26), &fL, &bDark);

            // Stats line
            wstring statStr = to_wstring(prof.blockedWebsites.size()) + L" Sites  •  "
                + to_wstring(prof.blockedApps.size()) + L" Apps";
            g.DrawString(statStr.c_str(), -1, &fSmall,
                RectF(cX + 18, cY + 52, cardW2 - 30, 18), &fL, &bGray);

            // Mode badge
            wstring modeName = (prof.lockMode == 1) ? L"Parents Control"
                : (prof.lockMode == 2) ? L"Long Text Unlock" : L"Self Control";
            float badgeW = 120.0f; float badgeH = 22.0f;
            RectF badgeR(cX + 18, cY + 76, badgeW, badgeH);
            GraphicsPath* bp = GetSchRoundRectPath(badgeR, 11);
            SolidBrush badgeBg(ClrTealLight); g.FillPath(&badgeBg, bp); delete bp;
            g.DrawString(modeName.c_str(), -1, &fSmallBold, badgeR, &fC, &bTeal);

            // Divider
            g.DrawLine(&pThin, cX + 14, cY + 110.0f, cX + cardW2 - 14, cY + 110.0f);

            // Toggle switch
            RectF togRect(cX + 16, cY + 124, 46, 24);
            GraphicsPath* tp = GetSchRoundRectPath(togRect, 12);
            SolidBrush tBg(isActive ? ClrGreen : Color(255, 190, 198, 210));
            g.FillPath(&tBg, tp); delete tp;
            float knobX = isActive ? (cX + 16.0f + 46.0f - 22.0f) : (cX + 16.0f + 2.0f);
            g.FillEllipse(&bWhite, knobX, cY + 126.0f, 20.0f, 20.0f);

            wstring toggleTxt = isActive ? L"Active" : L"Inactive";
            if (isActive && prof.lockMode == 0 && prof.lockEndTime > 0) toggleTxt = L"Locked (Timer)";
            else if (isActive && prof.lockMode == 0 && prof.lockEndTime == 0) toggleTxt = L"Auto-Schedule";
            g.DrawString(toggleTxt.c_str(), -1, &fSmallBold,
                RectF(cX + 68, cY + 124, 90, 24), &fL, isActive ? &bTeal : &bGray);

            // Edit button
            RectF editRect(cX + cardW2 - 125, cY + 122, 55, 28);
            GraphicsPath* ep = GetSchRoundRectPath(editRect, 5);
            SolidBrush eBr(prof.hEdit ? ClrBgHover : ClrBg);
            g.FillPath(&eBr, ep); g.DrawPath(&pThin, ep); delete ep;
            g.DrawString(L"Edit", -1, &fSmallBold, editRect, &fC, &bDark);

            // Delete button
            RectF delRect(cX + cardW2 - 62, cY + 122, 46, 28);
            GraphicsPath* dp = GetSchRoundRectPath(delRect, 5);
            if (isActive) {
                g.FillPath(&bBg, dp); g.DrawPath(&pThin, dp); delete dp;
                SolidBrush disabledClr(ClrDisabled);
                g.DrawString(L"\xE74D", -1, &fSmallIcon, delRect, &fC, &disabledClr);
            }
            else {
                SolidBrush dBr(prof.hDel ? ClrRed : ClrBg);
                g.FillPath(&dBr, dp); g.DrawPath(&pThin, dp); delete dp;
                SolidBrush delIconClr(prof.hDel ? ClrWhite : ClrRed);
                g.DrawString(L"\xE74D", -1, &fSmallIcon, delRect, &fC, &delIconClr);
            }
        }

        // Empty state
        if (g_profiles.empty()) {
            g.DrawString(L"\xE82D", -1, &fTitle, RectF(x, y + h / 2 - 60, w, 40), &fC, &bGray);
            g.DrawString(L"No profiles yet. Click \"+ New Profile\" to get started.", -1, &fNorm,
                RectF(x, y + h / 2 - 14, w, 28), &fC, &bGray);
        }

        g.SetClip(&oldClip);
        return; // done with main view
    }

    // =========================================================================
    // OVERLAY: FULL-WINDOW EDIT PROFILE
    // =========================================================================
    if (editingProfileIdx != -1) {
        float ovX = x, ovY = y, ovW = w, ovH = h;

        // Background
        g.FillRectangle(&bBg, ovX, ovY, ovW, ovH);

        // Top header
        g.FillRectangle(&bCardBg, ovX, ovY, ovW, 58.0f);
        Pen pHdrLine(ClrBorder, 1.0f);
        g.DrawLine(&pHdrLine, ovX, ovY + 57.0f, ovX + ovW, ovY + 57.0f);

        // Sub-tabs
        wstring tabNames[4] = { L"Basic & Time", L"Quick Settings", L"Custom Lists", L"Adult Block" };
        float tX = ovX + 20.0f; float tY = ovY + 14.0f;
        for (int i = 0; i < 4; i++) {
            g_ehb.subTabRects[i] = RectF(tX, tY, 125.0f, 30.0f);
            GraphicsPath* tabP = GetSchRoundRectPath(g_ehb.subTabRects[i], 6);
            if (s_activeSubTab == i) {
                g.FillPath(&bTeal, tabP);
                g.DrawString(tabNames[i].c_str(), -1, &fSmallBold, g_ehb.subTabRects[i], &fC, &bWhite);
            }
            else {
                SolidBrush hovBr(g_ehb.hSubTab == i ? ClrBgHover : Color(255, 238, 241, 245));
                g.FillPath(&hovBr, tabP);
                g.DrawString(tabNames[i].c_str(), -1, &fSmallBold, g_ehb.subTabRects[i], &fC, &bGray);
            }
            delete tabP;
            tX += 132.0f;
        }

        // Footer bar
        float footerY = ovY + ovH - 60.0f;
        g.FillRectangle(&bCardBg, ovX, footerY, ovW, 60.0f);
        g.DrawLine(&pHdrLine, ovX, footerY, ovX + ovW, footerY);

        // Back / Next
        if (s_activeSubTab > 0) {
            g_ehb.backBtn = RectF(ovX + 20, footerY + 12, 85, 34);
            GraphicsPath* bbp = GetSchRoundRectPath(g_ehb.backBtn, 5);
            SolidBrush bbBr(g_ehb.hBack ? ClrBgHover : ClrBg);
            g.FillPath(&bbBr, bbp); g.DrawPath(&pThin, bbp); delete bbp;
            g.DrawString(L"< Back", -1, &fBold, g_ehb.backBtn, &fC, &bDark);
        }
        else { g_ehb.backBtn = RectF(); }

        if (s_activeSubTab < 3) {
            g_ehb.nextBtn = RectF(ovX + 115, footerY + 12, 85, 34);
            GraphicsPath* nbp = GetSchRoundRectPath(g_ehb.nextBtn, 5);
            SolidBrush nbBr(g_ehb.hNext ? ClrBgHover : ClrBg);
            g.FillPath(&nbBr, nbp); g.DrawPath(&pThin, nbp); delete nbp;
            g.DrawString(L"Next >", -1, &fBold, g_ehb.nextBtn, &fC, &bDark);
        }
        else { g_ehb.nextBtn = RectF(); }

        // Save / Cancel
        g_ehb.saveBtn = RectF(ovX + ovW - 130, footerY + 12, 108, 34);
        GraphicsPath* svp = GetSchRoundRectPath(g_ehb.saveBtn, 5);
        SolidBrush svBr(g_ehb.hSave ? ClrTealHover : ClrTeal);
        g.FillPath(&svBr, svp); delete svp;
        g.DrawString(L"Save Profile", -1, &fBold, g_ehb.saveBtn, &fC, &bWhite);

        g_ehb.cancelBtn = RectF(ovX + ovW - 248, footerY + 12, 108, 34);
        GraphicsPath* cvp = GetSchRoundRectPath(g_ehb.cancelBtn, 5);
        SolidBrush cvBr(g_ehb.hCancel ? ClrBgHover : ClrBg);
        g.FillPath(&cvBr, cvp); g.DrawPath(&pThin, cvp); delete cvp;
        g.DrawString(L"Cancel", -1, &fBold, g_ehb.cancelBtn, &fC, &bDark);

        float contentY = ovY + 68.0f;
        float cardX = ovX + 20.0f;
        float cardW_inner = ovW - 40.0f;
        float contentH = footerY - contentY - 10.0f;

        // ================== TAB 0: BASIC & TIME ==================
        if (s_activeSubTab == 0) {
            // Card 1: General
            RectF c1Rect(cardX, contentY, cardW_inner, 82);
            GraphicsPath* c1P = GetSchRoundRectPath(c1Rect, 8);
            g.FillPath(&bCardBg, c1P); g.DrawPath(&pThin, c1P); delete c1P;

            g.DrawString(L"General", -1, &fBold, RectF(cardX + 16, contentY + 8, 120, 20), &fL, &bTeal);

            g.DrawString(L"Profile Name", -1, &fSmall, RectF(cardX + 16, contentY + 34, 95, 20), &fL, &bGray);

            g_ehb.nameInp = RectF(cardX + 112, contentY + 28, 195, 34);
            GraphicsPath* np = GetSchRoundRectPath(g_ehb.nameInp, 5);
            g.FillPath(activeInput == 1 ? &bCardBg : &bBg, np);
            Pen* pInp1 = activeInput == 1 ? &pFocus : &pThin;
            g.DrawPath(pInp1, np); delete np;
            if (inpProfileName.empty() && activeInput != 1) {
                g.DrawString(L"e.g. Study Time", -1, &fNorm,
                    RectF(g_ehb.nameInp.X + 8, g_ehb.nameInp.Y, g_ehb.nameInp.Width - 16, g_ehb.nameInp.Height),
                    &fL, &bGray);
            }
            else {
                g.DrawString(inpProfileName.c_str(), -1, &fNorm,
                    RectF(g_ehb.nameInp.X + 8, g_ehb.nameInp.Y, g_ehb.nameInp.Width - 16, g_ehb.nameInp.Height),
                    &fL, &bDark);
                if (activeInput == 1 && (GetTickCount() / 500) % 2 == 0) {
                    float tw = MeasureStringWidth(inpProfileName, &fNorm, g);
                    g.FillRectangle(&bDark, g_ehb.nameInp.X + 8 + tw, g_ehb.nameInp.Y + 7, 1.5f, 20.0f);
                }
            }

            g.DrawString(L"Lock Mode", -1, &fSmall, RectF(cardX + 330, contentY + 34, 80, 20), &fL, &bGray);

            g_ehb.modeDrop = RectF(cardX + 420, contentY + 28, 200, 34);
            GraphicsPath* mdp = GetSchRoundRectPath(g_ehb.modeDrop, 5);
            SolidBrush dropBg(hoverSchModeDropdown ? ClrBgHover : ClrBg);
            g.FillPath(&dropBg, mdp); g.DrawPath(&pThin, mdp); delete mdp;
            wstring curModeTxt = (tempLockMode == 1) ? L"Parents Control"
                : (tempLockMode == 2) ? L"Long Text Unlock" : L"Self Control";
            g.DrawString(curModeTxt.c_str(), -1, &fNorm,
                RectF(g_ehb.modeDrop.X + 10, g_ehb.modeDrop.Y, g_ehb.modeDrop.Width - 30, g_ehb.modeDrop.Height),
                &fL, &bDark);
            g.DrawString(L"\xE70D", -1, &fSmallIcon,
                RectF(g_ehb.modeDrop.X + g_ehb.modeDrop.Width - 28, g_ehb.modeDrop.Y,
                    28, g_ehb.modeDrop.Height), &fC, &bGray);

            contentY += 94;

            // Card 2: Schedule
            RectF c2Rect(cardX, contentY, cardW_inner, 112);
            GraphicsPath* c2P = GetSchRoundRectPath(c2Rect, 8);
            g.FillPath(&bCardBg, c2P); g.DrawPath(&pThin, c2P); delete c2P;

            g.DrawString(L"Schedule", -1, &fBold, RectF(cardX + 16, contentY + 8, 120, 20), &fL, &bTeal);
            g.DrawString(L"Active Days", -1, &fSmall, RectF(cardX + 16, contentY + 36, 90, 18), &fL, &bGray);

            wstring dLabels[] = { L"S", L"M", L"T", L"W", L"T", L"F", L"S" };
            for (int d = 0; d < 7; d++) {
                g_ehb.days[d] = RectF(cardX + 108 + (d * 38.0f), contentY + 28, 32, 32);
                GraphicsPath* dP = GetSchRoundRectPath(g_ehb.days[d], 16);
                SolidBrush dBr(editDays[d] ? ClrTeal
                    : (g_ehb.hDay == d ? ClrBgHover : ClrBg));
                g.FillPath(&dBr, dP);
                Pen pDay(editDays[d] ? ClrTeal : ClrBorder, 1.0f);
                g.DrawPath(&pDay, dP); delete dP;
                g.DrawString(dLabels[d].c_str(), -1, &fSmallBold, g_ehb.days[d],
                    &fC, editDays[d] ? &bWhite : &bDark);
            }

            g.DrawString(L"Session Time", -1, &fSmall, RectF(cardX + 16, contentY + 74, 90, 18), &fL, &bGray);

            auto DrawModernTimeBox = [&](float tx, float ty, const wstring& lbl,
                int hh, int mm, RectF& hBox, RectF& mBox, RectF& ampmBtn,
                bool hH, bool hM, bool hAmPm) {
                    g.DrawString(lbl.c_str(), -1, &fSmall, RectF(tx, ty, 36, 30), &fC, &bGray);
                    int dispH = hh % 12; if (dispH == 0) dispH = 12;
                    wstring ampmStr = (hh >= 12) ? L"PM" : L"AM";

                    hBox = RectF(tx + 40, ty, 36, 30);
                    GraphicsPath hp; AddRoundedRectPath(hp, hBox, 5);
                    SolidBrush hbBr(hH ? ClrBgHover : ClrBg);
                    g.FillPath(&hbBr, &hp); g.DrawPath(&pThin, &hp);
                    wstring hStr = (dispH < 10) ? L"0" + to_wstring(dispH) : to_wstring(dispH);
                    g.DrawString(hStr.c_str(), -1, &fBold, hBox, &fC, &bDark);

                    g.DrawString(L":", -1, &fBold, RectF(tx + 76, ty, 10, 30), &fC, &bDark);

                    mBox = RectF(tx + 86, ty, 36, 30);
                    GraphicsPath mp; AddRoundedRectPath(mp, mBox, 5);
                    SolidBrush mbBr(hM ? ClrBgHover : ClrBg);
                    g.FillPath(&mbBr, &mp); g.DrawPath(&pThin, &mp);
                    wstring mStr = (mm < 10) ? L"0" + to_wstring(mm) : to_wstring(mm);
                    g.DrawString(mStr.c_str(), -1, &fBold, mBox, &fC, &bDark);

                    ampmBtn = RectF(tx + 126, ty, 40, 30);
                    GraphicsPath ap; AddRoundedRectPath(ap, ampmBtn, 5);
                    SolidBrush aBr2(hAmPm ? ClrTealHover : ClrBgHover);
                    g.FillPath(&aBr2, &ap);
                    Pen pAp(hAmPm ? ClrTeal : ClrBorder, 1.0f); g.DrawPath(&pAp, &ap);
                    SolidBrush amClr(hh >= 12 ? ClrTeal : ClrDark);
                    g.DrawString(ampmStr.c_str(), -1, &fBold, ampmBtn, &fC, &amClr);
            };

            DrawModernTimeBox(cardX + 108, contentY + 68, L"Start",
                editStH, editStM, g_ehb.stH_Box, g_ehb.stM_Box, g_ehb.stAmPm,
                g_ehb.hStH, g_ehb.hStM, g_ehb.hStAmPm);
            DrawModernTimeBox(cardX + 295, contentY + 68, L"End",
                editEnH, editEnM, g_ehb.enH_Box, g_ehb.enM_Box, g_ehb.enAmPm,
                g_ehb.hEnH, g_ehb.hEnM, g_ehb.hEnAmPm);
        }

        // ================== TAB 1: QUICK SETTINGS ==================
        else if (s_activeSubTab == 1) {
            // Card: Restrictions
            RectF c3Rect(cardX, contentY, cardW_inner, 82);
            GraphicsPath* c3P = GetSchRoundRectPath(c3Rect, 8);
            g.FillPath(&bCardBg, c3P); g.DrawPath(&pThin, c3P); delete c3P;
            g.DrawString(L"Restrictions", -1, &fBold, RectF(cardX + 16, contentY + 8, 150, 20), &fL, &bTeal);

            float cbWidth = (cardW_inner - 32.0f) / 2.0f;

            auto DrawCb = [&](RectF& outHitbox, float cx, float cy, const wstring& label, bool val, bool hov) {
                outHitbox = RectF(cx, cy, cbWidth - 10, 38);
                if (hov) {
                    GraphicsPath* hp = GetSchRoundRectPath(outHitbox, 5);
                    g.FillPath(&bBgHover, hp); delete hp;
                }
                RectF cbBox(cx + 10, cy + 9, 20, 20);
                GraphicsPath* bp = GetSchRoundRectPath(cbBox, 4);
                g.FillPath(val ? &bTeal : &bCardBg, bp);
                Pen pCb(val ? ClrTeal : ClrBorder, 1.5f); g.DrawPath(&pCb, bp); delete bp;
                if (val) g.DrawString(L"\xE73E", -1, &fSmallIcon, cbBox, &fC, &bWhite);
                g.DrawString(label.c_str(), -1, &fNorm,
                    RectF(cx + 38, cy + 9, cbWidth - 50, 20), &fL, &bDark);
            };

            float cbY = contentY + 32.0f;
            DrawCb(g_ehb.togInt, cardX + 16, cbY, L"Block Internet entirely", editBlockInt, g_ehb.hTogInt);
            DrawCb(g_ehb.togUni, cardX + 16 + cbWidth, cbY, L"Block Uninstall & Taskmgr", editBlockUninst, g_ehb.hTogUni);

            contentY += 96;

            // Card: Quick Block
            RectF c4Rect(cardX, contentY, cardW_inner, 90);
            GraphicsPath* c4P = GetSchRoundRectPath(c4Rect, 8);
            g.FillPath(&bCardBg, c4P); g.DrawPath(&pThin, c4P); delete c4P;
            g.DrawString(L"Quick Block", -1, &fBold, RectF(cardX + 16, contentY + 8, 110, 20), &fL, &bTeal);
            g.DrawString(L"Chrome · Edge · Firefox · Brave · Opera", -1, &fSmall,
                RectF(cardX + 122, contentY + 10, 400, 18), &fL, &bGray);

            float qbX = cardX + 16; float qbY = contentY + 44;
            float qbW = 120.0f; float qbH = 32.0f; float qbGap = 10.0f;
            s_quickBlockRects.resize(s_quickBlocks.size());
            for (size_t qi = 0; qi < s_quickBlocks.size(); ++qi) {
                RectF qbRect(qbX + qi * (qbW + qbGap), qbY, qbW, qbH);
                s_quickBlockRects[qi] = qbRect;
                bool alreadyAdded = false;
                if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                    const auto& ep = g_profiles[editingProfileIdx];
                    if (qi == 0) alreadyAdded = ep.qbYTShorts;
                    else if (qi == 1) alreadyAdded = ep.qbFBReels;
                    else if (qi == 2) alreadyAdded = ep.qbYTAds;
                    else if (qi == 3) alreadyAdded = ep.qbIGReels;
                }
                GraphicsPath* qp = GetSchRoundRectPath(qbRect, 6);
                if (alreadyAdded) { g.FillPath(&bTeal, qp); }
                else {
                    SolidBrush qbBg(s_quickBlocks[qi].hovered ? ClrBgHover : ClrBg);
                    g.FillPath(&qbBg, qp); g.DrawPath(&pThin, qp);
                }
                delete qp;
                SolidBrush* txtClr2 = alreadyAdded ? &bWhite : &bDark;
                g.DrawString(s_quickBlocks[qi].label.c_str(), -1, &fSmallBold, qbRect, &fC, txtClr2);
            }
        }

        // ================== TAB 2: CUSTOM LISTS ==================
        else if (s_activeSubTab == 2) {
            vector<SchBlockItem>* cWebs = nullptr;
            vector<SchBlockItem>* cApps = nullptr;
            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                cWebs = &g_profiles[editingProfileIdx].blockedWebsites;
                cApps = &g_profiles[editingProfileIdx].blockedApps;
            }

            float listAreaH = contentH;
            RectF cRect(cardX, contentY, cardW_inner, listAreaH);
            GraphicsPath* cP = GetSchRoundRectPath(cRect, 8);
            g.FillPath(&bCardBg, cP); g.DrawPath(&pThin, cP); delete cP;

            float colGap = 18.0f;
            float colW = (cardW_inner - colGap - 30.0f) / 2.0f;

            auto DrawListCol = [&](int colIdx, float colX, const wstring& title, const wstring& ph,
                wstring& inpStr, int inpIdx, vector<SchBlockItem>* list,
                RectF& outInp, RectF* outCombo, RectF& outAdd, bool hovCombo, bool hovAdd,
                vector<pair<RectF, int>>& outDel) {

                g.DrawString(title.c_str(), -1, &fBold,
                    RectF(colX, contentY + 10, colW, 24), &fL, &bDark);

                float inpY = contentY + 42.0f;
                float addW = 62.0f; float gap = 5.0f;
                float comboW = outCombo ? 30.0f : 0.0f;
                float inpW = colW - comboW - addW - (outCombo ? gap * 2 : gap);

                outInp = RectF(colX, inpY, inpW, 34);
                GraphicsPath* ip = GetSchRoundRectPath(outInp, 5);
                g.FillPath(activeInput == inpIdx ? &bCardBg : &bBg, ip);
                Pen* pInpX = activeInput == inpIdx ? &pFocus : &pThin;
                g.DrawPath(pInpX, ip); delete ip;

                if (inpStr.empty() && activeInput != inpIdx) {
                    g.DrawString(ph.c_str(), -1, &fNorm,
                        RectF(outInp.X + 8, outInp.Y, outInp.Width - 16, outInp.Height), &fL, &bGray);
                }
                else {
                    g.DrawString(inpStr.c_str(), -1, &fNorm,
                        RectF(outInp.X + 8, outInp.Y, outInp.Width - 16, outInp.Height), &fL, &bDark);
                    if (activeInput == inpIdx && (GetTickCount() / 500) % 2 == 0) {
                        float tw = MeasureStringWidth(inpStr, &fNorm, g);
                        g.FillRectangle(&bDark, outInp.X + 8 + tw, outInp.Y + 7, 1.5f, 20.0f);
                    }
                }

                float nextX = outInp.X + outInp.Width + gap;
                if (outCombo) {
                    *outCombo = RectF(nextX, inpY, comboW, 34);
                    GraphicsPath* cbp = GetSchRoundRectPath(*outCombo, 5);
                    SolidBrush cbBr(hovCombo ? ClrBgHover : ClrBg);
                    g.FillPath(&cbBr, cbp); g.DrawPath(&pThin, cbp); delete cbp;
                    g.DrawString(L"\xE70D", -1, &fSmallIcon, *outCombo, &fC, &bDark);
                    nextX += comboW + gap;
                }
                outAdd = RectF(nextX, inpY, addW, 34);
                GraphicsPath* ap = GetSchRoundRectPath(outAdd, 5);
                SolidBrush aBrX(hovAdd ? ClrTealHover : ClrTeal);
                g.FillPath(&aBrX, ap); delete ap;
                g.DrawString(L"+ Add", -1, &fBold, outAdd, &fC, &bWhite);

                outDel.clear();
                float listStartY = inpY + 42.0f;
                float boxH = listAreaH - 100.0f;
                if (colIdx == 1) boxH -= 46.0f;

                g_ehb.listAreas[colIdx] = RectF(colX, listStartY, colW, boxH);
                SolidBrush listBg(ClrBg); g.FillRectangle(&listBg, colX, listStartY, colW, boxH);
                g.DrawRectangle(&pThin, colX, listStartY, colW, boxH);

                Region oldClipX; g.GetClip(&oldClipX);
                if (list && !list->empty()) {
                    s_listScrollMax[colIdx] = (std::max)(0.0f, (list->size() * 34.0f) - boxH + 8.0f);
                    s_listScrollC[colIdx] += (s_listScrollT[colIdx] - s_listScrollC[colIdx]) * 0.15f;
                    g.SetClip(g_ehb.listAreas[colIdx]);
                    float itemY = listStartY + 4.0f - s_listScrollC[colIdx];
                    for (size_t ii = 0; ii < list->size(); ++ii) {
                        auto& item = (*list)[ii];
                        if (itemY + 30 > listStartY && itemY < listStartY + boxH) {
                            RectF rowR(colX + 4, itemY, colW - 8, 30);
                            if (item.isHoveredCross) {
                                SolidBrush rowHov(Color(20, 220, 60, 50));
                                g.FillRectangle(&rowHov, rowR);
                            }
                            g.DrawString(item.name.c_str(), -1, &fNorm,
                                RectF(rowR.X + 6, rowR.Y, rowR.Width - 36, rowR.Height), &fL, &bDark);
                            RectF delR(rowR.X + rowR.Width - 28, rowR.Y + 5, 22, 20);
                            outDel.push_back({ delR, (int)ii });
                            SolidBrush crBr(item.isHoveredCross ? ClrRed : ClrGrayText);
                            g.DrawString(L"\xE711", -1, &fSmallIcon, delR, &fC, &crBr);
                            Pen pRow(ClrBorder, 0.5f);
                            g.DrawLine(&pRow, rowR.X, rowR.Y + 30.0f, rowR.X + rowR.Width, rowR.Y + 30.0f);
                        }
                        itemY += 34.0f;
                    }
                    g.SetClip(&oldClipX);
                    if (s_listScrollMax[colIdx] > 0) {
                        float thumbH = (std::max)(20.0f, boxH * (boxH / (boxH + s_listScrollMax[colIdx])));
                        float thumbY = listStartY + (s_listScrollC[colIdx] / s_listScrollMax[colIdx]) * (boxH - thumbH);
                        GraphicsPath thP; AddRoundedRectPath(thP, colX + colW - 5, thumbY, 4, thumbH, 2);
                        SolidBrush thB(Color(120, 12, 168, 176)); g.FillPath(&thB, &thP);
                    }
                }
                else {
                    g.SetClip(&oldClipX);
                    s_listScrollMax[colIdx] = 0.0f; s_listScrollT[colIdx] = 0.0f;
                    g.DrawString(L"Nothing added yet", -1, &fSmall,
                        RectF(colX, listStartY, colW, boxH), &fC, &bGray);
                }
            };

            float startColX = cardX + 15.0f;
            DrawListCol(0, startColX, L"Websites", L"e.g. facebook.com",
                inpWeb, 2, cWebs, g_ehb.webInp, &g_ehb.webCombo, g_ehb.addWeb,
                hoverSchWebCombo, g_ehb.hAddWeb, g_ehb.webDel);
            DrawListCol(1, startColX + colW + colGap, L"Applications", L"e.g. vlc.exe",
                inpApp, 3, cApps, g_ehb.appInp, &g_ehb.appCombo, g_ehb.addApp,
                hoverSchAppCombo, g_ehb.hAddApp, g_ehb.appDel);

            // Bottom buttons for Apps column
            float appColX = startColX + colW + colGap;
            float btnW3 = (colW - 12.0f) / 3.0f;
            float btnY = contentY + listAreaH - 44.0f;

            g_ehb.btnAddExe = RectF(appColX, btnY, btnW3, 34.0f);
            GraphicsPath* p1 = GetSchRoundRectPath(g_ehb.btnAddExe, 5);
            SolidBrush b1(g_ehb.hBtnAddExe ? ClrGreen : Color(255, 40, 180, 100));
            g.FillPath(&b1, p1); delete p1;
            g.DrawString(L"Add .exe", -1, &fSmallBold, g_ehb.btnAddExe, &fC, &bWhite);

            g_ehb.btnAddStore = RectF(appColX + btnW3 + 6.0f, btnY, btnW3, 34.0f);
            GraphicsPath* p2 = GetSchRoundRectPath(g_ehb.btnAddStore, 5);
            SolidBrush b2(g_ehb.hBtnAddStore ? ClrGreen : Color(255, 40, 180, 100));
            g.FillPath(&b2, p2); delete p2;
            g.DrawString(L"Add Store", -1, &fSmallBold, g_ehb.btnAddStore, &fC, &bWhite);

            g_ehb.btnAddTitle = RectF(appColX + (btnW3 * 2) + 12.0f, btnY, btnW3, 34.0f);
            GraphicsPath* p3 = GetSchRoundRectPath(g_ehb.btnAddTitle, 5);
            SolidBrush b3(g_ehb.hBtnAddTitle ? ClrGreen : Color(255, 40, 180, 100));
            g.FillPath(&b3, p3); delete p3;
            g.DrawString(L"Add Title", -1, &fSmallBold, g_ehb.btnAddTitle, &fC, &bWhite);
        }

        // ================== TAB 3: ADULT BLOCK ==================
        else if (s_activeSubTab == 3) {
            float listAreaH = contentH;
            RectF cRect(cardX, contentY, cardW_inner, listAreaH);
            GraphicsPath* cP = GetSchRoundRectPath(cRect, 8);
            g.FillPath(&bCardBg, cP); g.DrawPath(&pThin, cP); delete cP;

            float colGap = 18.0f;
            float colW = (cardW_inner - colGap - 30.0f) / 2.0f;
            float startColX = cardX + 15.0f;

            // Left: Safe Browsing Rules
            g.DrawString(L"Safe Browsing Rules", -1, &fBold,
                RectF(startColX, contentY + 10, colW, 24), &fL, &bDark);

            bool cA = false, cH = false, cR = false, cD = false;
            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                const auto& ep = g_profiles[editingProfileIdx];
                cA = ep.schAdultWeb; cH = ep.schHardcore; cR = ep.schRomantic; cD = ep.schStrictDns;
            }

            struct RuleItem { const wchar_t* label; const wchar_t* desc; };
            RuleItem rules[] = {
                { L"Block Adult Websites", L"Uses resource list of known adult domains" },
                { L"Block Hardcore Keywords", L"Porn, NSFW, and related terms" },
                { L"Block Romantic / Softcore", L"Item songs, seductive content, etc." },
                { L"Enforce Strict DNS & SafeSearch", L"Forces safe search on Google, Bing" }
            };
            RectF* cbPtrs[] = { &g_ehb.cbAdultWeb, &g_ehb.cbHardcore, &g_ehb.cbRomantic, &g_ehb.cbStrictDns };
            bool cbVals[] = { cA, cH, cR, cD };
            bool* cbHovPtrs[] = { &g_ehb.hCbAdultWeb, &g_ehb.hCbHardcore, &g_ehb.hCbRomantic, &g_ehb.hCbStrictDns };

            for (int ri = 0; ri < 4; ri++) {
                float ry = contentY + 44.0f + ri * 50.0f;
                *cbPtrs[ri] = RectF(startColX, ry, colW - 10, 44);
                if (*cbHovPtrs[ri]) {
                    GraphicsPath* hp = GetSchRoundRectPath(*cbPtrs[ri], 5);
                    g.FillPath(&bBgHover, hp); delete hp;
                }
                RectF cbBox(startColX + 8, ry + 12, 20, 20);
                GraphicsPath* bp = GetSchRoundRectPath(cbBox, 4);
                g.FillPath(cbVals[ri] ? &bTeal : &bCardBg, bp);
                Pen pCbR(cbVals[ri] ? ClrTeal : ClrBorder, 1.5f);
                g.DrawPath(&pCbR, bp); delete bp;
                if (cbVals[ri]) g.DrawString(L"\xE73E", -1, &fSmallIcon, cbBox, &fC, &bWhite);
                g.DrawString(rules[ri].label, -1, &fNorm,
                    RectF(startColX + 36, ry + 4, colW - 48, 18), &fL, &bDark);
                g.DrawString(rules[ri].desc, -1, &fSmall,
                    RectF(startColX + 36, ry + 24, colW - 48, 16), &fL, &bGray);
            }

            // Right: Custom Bad Words
            float rightColX = startColX + colW + colGap;
            g.DrawString(L"Custom Bad Keywords", -1, &fBold,
                RectF(rightColX, contentY + 10, colW, 24), &fL, &bDark);

            float inpY = contentY + 42.0f;
            float addW = 62.0f; float gap = 5.0f;
            float inpW = colW - addW - gap;

            g_ehb.keyInp = RectF(rightColX, inpY, inpW, 34);
            GraphicsPath* ip = GetSchRoundRectPath(g_ehb.keyInp, 5);
            g.FillPath(activeInput == 4 ? &bCardBg : &bBg, ip);
            Pen* pInpK = activeInput == 4 ? &pFocus : &pThin;
            g.DrawPath(pInpK, ip); delete ip;
            if (inpKey.empty() && activeInput != 4) {
                g.DrawString(L"e.g. badword", -1, &fNorm,
                    RectF(g_ehb.keyInp.X + 8, g_ehb.keyInp.Y, g_ehb.keyInp.Width - 16, g_ehb.keyInp.Height),
                    &fL, &bGray);
            }
            else {
                g.DrawString(inpKey.c_str(), -1, &fNorm,
                    RectF(g_ehb.keyInp.X + 8, g_ehb.keyInp.Y, g_ehb.keyInp.Width - 16, g_ehb.keyInp.Height),
                    &fL, &bDark);
                if (activeInput == 4 && (GetTickCount() / 500) % 2 == 0) {
                    float tw = MeasureStringWidth(inpKey, &fNorm, g);
                    g.FillRectangle(&bDark, g_ehb.keyInp.X + 8 + tw, g_ehb.keyInp.Y + 7, 1.5f, 20.0f);
                }
            }

            g_ehb.addKey = RectF(g_ehb.keyInp.X + g_ehb.keyInp.Width + gap, inpY, addW, 34);
            GraphicsPath* ap = GetSchRoundRectPath(g_ehb.addKey, 5);
            SolidBrush aBrK(g_ehb.hAddKey ? ClrTealHover : ClrTeal);
            g.FillPath(&aBrK, ap); delete ap;
            g.DrawString(L"+ Add", -1, &fBold, g_ehb.addKey, &fC, &bWhite);

            // Keyword list
            g_ehb.keyDel.clear();
            float listStartY = inpY + 42.0f;
            float boxH = listAreaH - 100.0f;
            g_ehb.listAreas[2] = RectF(rightColX, listStartY, colW, boxH);
            SolidBrush listBg2(ClrBg); g.FillRectangle(&listBg2, rightColX, listStartY, colW, boxH);
            g.DrawRectangle(&pThin, rightColX, listStartY, colW, boxH);

            vector<SchBlockItem>* aList = nullptr;
            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size())
                aList = &g_profiles[editingProfileIdx].adultCustomKeywords;

            Region oldClipK; g.GetClip(&oldClipK);
            if (aList && !aList->empty()) {
                s_listScrollMax[2] = (std::max)(0.0f, (aList->size() * 34.0f) - boxH + 8.0f);
                s_listScrollC[2] += (s_listScrollT[2] - s_listScrollC[2]) * 0.15f;
                g.SetClip(g_ehb.listAreas[2]);
                float itemY = listStartY + 4.0f - s_listScrollC[2];
                for (size_t ii = 0; ii < aList->size(); ++ii) {
                    auto& item = (*aList)[ii];
                    if (itemY + 30 > listStartY && itemY < listStartY + boxH) {
                        RectF rowR(rightColX + 4, itemY, colW - 8, 30);
                        if (item.isHoveredCross) {
                            SolidBrush rowHov(Color(20, 220, 60, 50));
                            g.FillRectangle(&rowHov, rowR);
                        }
                        g.DrawString(item.name.c_str(), -1, &fNorm,
                            RectF(rowR.X + 6, rowR.Y, rowR.Width - 36, rowR.Height), &fL, &bDark);
                        RectF delR(rowR.X + rowR.Width - 28, rowR.Y + 5, 22, 20);
                        g_ehb.keyDel.push_back({ delR, (int)ii });
                        SolidBrush crBr(item.isHoveredCross ? ClrRed : ClrGrayText);
                        g.DrawString(L"\xE711", -1, &fSmallIcon, delR, &fC, &crBr);
                        Pen pRow(ClrBorder, 0.5f);
                        g.DrawLine(&pRow, rowR.X, rowR.Y + 30.0f, rowR.X + rowR.Width, rowR.Y + 30.0f);
                    }
                    itemY += 34.0f;
                }
                g.SetClip(&oldClipK);
                if (s_listScrollMax[2] > 0) {
                    float thumbH = (std::max)(20.0f, boxH * (boxH / (boxH + s_listScrollMax[2])));
                    float thumbY = listStartY + (s_listScrollC[2] / s_listScrollMax[2]) * (boxH - thumbH);
                    GraphicsPath thP; AddRoundedRectPath(thP, rightColX + colW - 5, thumbY, 4, thumbH, 2);
                    SolidBrush thB(Color(120, 12, 168, 176)); g.FillPath(&thB, &thP);
                }
            }
            else {
                g.SetClip(&oldClipK);
                s_listScrollMax[2] = 0.0f; s_listScrollT[2] = 0.0f;
                g.DrawString(L"No keywords added", -1, &fSmall,
                    RectF(rightColX, listStartY, colW, boxH), &fC, &bGray);
            }
        }

        // ================== DROPDOWN OVERLAYS ==================
        if (s_activeSubTab == 0 && isSchModeDropdownOpen) {
            RectF mlR(g_ehb.modeDrop.X, g_ehb.modeDrop.Y + 36, 200, 122);
            GraphicsPath* mlP = GetSchRoundRectPath(mlR, 6);
            SolidBrush shadowD(Color(30, 0, 0, 0));
            RectF shadowR(mlR.X + 2, mlR.Y + 2, mlR.Width, mlR.Height);
            GraphicsPath* smlP = GetSchRoundRectPath(shadowR, 6); g.FillPath(&shadowD, smlP); delete smlP;
            g.FillPath(&bCardBg, mlP); g.DrawPath(&pThin, mlP); delete mlP;

            wstring modeLabels[3] = { L"Self Control", L"Parents Control", L"Long Text Unlock" };
            bool* modeHovs[3] = { &g_ehb.hOptSelf, &g_ehb.hOptParents, &g_ehb.hOptLongText };
            for (int mi = 0; mi < 3; mi++) {
                g_ehb.modeOpt[mi] = RectF(mlR.X + 2, mlR.Y + 2 + mi * 39, mlR.Width - 4, 38);
                SolidBrush oBr(*modeHovs[mi] ? ClrBgHover : ClrCardBg);
                g.FillRectangle(&oBr, g_ehb.modeOpt[mi]);
                g.DrawString(modeLabels[mi].c_str(), -1, &fNorm,
                    RectF(mlR.X + 12, mlR.Y + 2 + mi * 39, mlR.Width - 20, 38), &fL, &bDark);
            }
        }

        auto DrawDynamicDropdown = [&](RectF btnRect, vector<wstring>& opts,
            vector<RectF>& outOpts, int hovIdx) {
                float dropH = opts.size() * 30.0f + 10.0f;
                RectF lR(btnRect.X - 120, btnRect.Y + 36, 150, dropH);
                // Keep in bounds
                if (lR.X < s_cx + 2) lR.X = s_cx + 2;
                if (lR.X + lR.Width > s_cx + s_cw - 2) lR.X = s_cx + s_cw - lR.Width - 2;
                GraphicsPath* lP = GetSchRoundRectPath(lR, 6);
                SolidBrush shadowD2(Color(25, 0, 0, 0));
                RectF shadowR2(lR.X + 2, lR.Y + 2, lR.Width, lR.Height);
                GraphicsPath* slP = GetSchRoundRectPath(shadowR2, 6); g.FillPath(&shadowD2, slP); delete slP;
                g.FillPath(&bCardBg, lP); g.DrawPath(&pThin, lP); delete lP;
                outOpts.clear(); float iY = lR.Y + 5;
                for (size_t oi = 0; oi < opts.size(); ++oi) {
                    RectF optRect(lR.X + 2, iY, lR.Width - 4, 28);
                    outOpts.push_back(optRect);
                    SolidBrush oBr(hovIdx == (int)oi ? ClrBgHover : ClrCardBg);
                    g.FillRectangle(&oBr, optRect);
                    g.DrawString(opts[oi].c_str(), -1, &fSmall,
                        RectF(lR.X + 6, iY, lR.Width - 12, 28), &fL, &bDark);
                    iY += 30;
                }
        };

        if (s_activeSubTab == 2) {
            if (isSchWebComboOpen) DrawDynamicDropdown(g_ehb.webCombo, schCommonWebsites, g_ehb.webOpts, hoverSchWebOptIdx);
            if (isSchAppComboOpen) DrawDynamicDropdown(g_ehb.appCombo, schCommonApps, g_ehb.appOpts, hoverSchAppOptIdx);
            if (isSchStoreComboOpen) {
                float dropH = schCommonStoreApps.size() * 30.0f + 10.0f;
                RectF lR(g_ehb.btnAddStore.X, g_ehb.btnAddStore.Y - dropH - 4, 150, dropH);
                GraphicsPath* lP = GetSchRoundRectPath(lR, 6);
                g.FillPath(&bCardBg, lP); g.DrawPath(&pThin, lP); delete lP;
                g_ehb.storeOpts.clear(); float iY = lR.Y + 5;
                for (size_t si = 0; si < schCommonStoreApps.size(); ++si) {
                    RectF optRect(lR.X + 2, iY, lR.Width - 4, 28);
                    g_ehb.storeOpts.push_back(optRect);
                    SolidBrush oBr(hoverSchStoreOptIdx == (int)si ? ClrBgHover : ClrCardBg);
                    g.FillRectangle(&oBr, optRect);
                    g.DrawString(schCommonStoreApps[si].c_str(), -1, &fSmall,
                        RectF(lR.X + 6, iY, lR.Width - 12, 28), &fL, &bDark);
                    iY += 30;
                }
            }
        }
    } // end editingProfileIdx != -1

    // ==========================================
    // OVERLAYS: Time / Password / Text Unlock
    // ==========================================
    if (s_showTimeOverlay || s_showPassOverlay || s_showTextUnlockOverlay) {
        SolidBrush overlayBg(ClrOverlay);
        g.FillRectangle(&overlayBg, x, y, w, h);

        float ovW = s_showTextUnlockOverlay ? 620.0f : 500.0f;
        float ovH = s_showTextUnlockOverlay ? 460.0f : 290.0f;
        float ovX = x + (w - ovW) / 2.0f;
        float ovY2 = y + (h - ovH) / 2.0f;

        // Dialog shadow
        SolidBrush shadowBrO(Color(40, 0, 0, 0));
        for (int si = 4; si >= 1; si--) {
            GraphicsPath* sP = GetSchRoundRectPath(RectF(ovX + si, ovY2 + si, ovW, ovH), 10);
            g.FillPath(&shadowBrO, sP); delete sP;
        }

        RectF ovRect(ovX, ovY2, ovW, ovH);
        GraphicsPath* op = GetSchRoundRectPath(ovRect, 10);
        g.FillPath(&bCardBg, op); g.DrawPath(&pThin, op); delete op;

        FontFamily ffTitle(L"Segoe UI");
        Font fOvTitle(&ffTitle, 13, FontStyleBold, UnitPixel);

        if (s_showTimeOverlay) {
            // Header strip
            SolidBrush tealHdr(ClrTeal);
            GraphicsPath* hdrP = GetSchRoundRectPath(RectF(ovX, ovY2, ovW, 50), 10);
            g.FillPath(&tealHdr, hdrP); delete hdrP;
            // Fix bottom corners of header
            g.FillRectangle(&tealHdr, ovX, ovY2 + 30, ovW, 20);

            g.DrawString(L"SET FOCUS DURATION", -1, &fBold,
                RectF(ovX, ovY2 + 12, ovW, 26), &fC, &bWhite);

            FontFamily ffSeg(L"Segoe UI");
            Font fSpinLbl(&ffSeg, 13, FontStyleRegular, UnitPixel);

            struct SpinRow { const wchar_t* lbl; int val; bool& hM; bool& hP; float ox; float oy; };
            SpinRow rows[] = {
                {L"Months", s_focusMonths, s_hTimeMoM, s_hTimeMoP, ovX + 30, ovY2 + 72},
                {L"Days",   s_focusDays,  s_hTimeDM,  s_hTimeDP,  ovX + 256, ovY2 + 72},
                {L"Hours",  s_focusHours, s_hTimeHM,  s_hTimeHP,  ovX + 30, ovY2 + 132},
                {L"Mins",   s_focusMins,  s_hTimeMM,  s_hTimeMP,  ovX + 256, ovY2 + 132},
            };
            FontFamily ffi2(L"Segoe MDL2 Assets");
            Font fSpinIcon(&ffi2, 18, FontStyleRegular, UnitPixel);
            FontFamily ffi3(L"Segoe UI");
            Font fSpinVal(&ffi3, 14, FontStyleBold, UnitPixel);

            for (auto& sr : rows) {
                g.DrawString(sr.lbl, -1, &fSpinLbl, RectF(sr.ox, sr.oy + 8, 60, 20), &fL, &bGray);
                DrawSchOverlaySpinner(g, sr.ox + 65, sr.oy, to_wstring(sr.val), sr.hM, sr.hP, &fSpinIcon, &fSpinVal);
            }

            RectF cancelRect(ovX + 50, ovY2 + 220, 150, 40);
            GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 6);
            SolidBrush cancelBrush(s_hTimeCancel ? ClrBgHover : ClrBg);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&pThin, cp); delete cp;
            g.DrawString(L"Cancel", -1, &fBold, cancelRect, &fC, &bDark);

            RectF startRect(ovX + 240, ovY2 + 220, 160, 40);
            GraphicsPath* sp = GetSchRoundRectPath(startRect, 6);
            SolidBrush startBrush(s_hTimeStart ? ClrTealHover : ClrTeal);
            g.FillPath(&startBrush, sp); delete sp;
            g.DrawString(L"Start Focus", -1, &fBold, startRect, &fC, &bWhite);
        }
        else if (s_showPassOverlay) {
            SolidBrush tealHdr(ClrTeal);
            GraphicsPath* hdrP = GetSchRoundRectPath(RectF(ovX, ovY2, ovW, 50), 10);
            g.FillPath(&tealHdr, hdrP); delete hdrP;
            g.FillRectangle(&tealHdr, ovX, ovY2 + 30, ovW, 20);

            wstring titleTxt = s_isStoppingFocus ? L"ENTER PARENTS PASSWORD" : L"SET PARENTS PASSWORD";
            g.DrawString(titleTxt.c_str(), -1, &fBold, RectF(ovX, ovY2 + 12, ovW, 26), &fC, &bWhite);

            RectF passInpRect(ovX + 40, ovY2 + 80, ovW - 80, 44);
            GraphicsPath* pp = GetSchRoundRectPath(passInpRect, 6);
            g.FillPath(s_isPassInputActive ? &bCardBg : &bBg, pp);
            Pen* pPassPen = s_isPassInputActive ? &pFocus : &pThin;
            g.DrawPath(pPassPen, pp); delete pp;

            wstring displayPass = wstring(s_inputPassText.length(), L'*');
            FontFamily ffP(L"Segoe UI");
            Font fPassFont(&ffP, 18, FontStyleBold, UnitPixel);
            if (s_inputPassText.empty() && !s_isPassInputActive) {
                g.DrawString(L"Type password here...", -1, &fNorm, passInpRect, &fC, &bGray);
            }
            else {
                g.DrawString(displayPass.c_str(), -1, &fPassFont,
                    RectF(ovX + 50, ovY2 + 84, ovW - 100, 36), &fL, &bDark);
                if (s_isPassInputActive && (GetTickCount() / 500) % 2 == 0) {
                    float tw = MeasureStringWidth(displayPass, &fPassFont, g);
                    g.FillRectangle(&bDark, ovX + 52 + tw, ovY2 + 88, 1.5f, 20.0f);
                }
            }

            RectF cancelRect(ovX + 40, ovY2 + 158, 150, 40);
            GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 6);
            SolidBrush cancelBrush(s_hPassCancel ? ClrBgHover : ClrBg);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&pThin, cp); delete cp;
            g.DrawString(L"Cancel", -1, &fBold, cancelRect, &fC, &bDark);

            RectF confRect(ovX + 230, ovY2 + 158, 165, 40);
            GraphicsPath* spp = GetSchRoundRectPath(confRect, 6);
            SolidBrush confBrush(s_hPassConfirm ? ClrTealHover : ClrTeal);
            g.FillPath(&confBrush, spp); delete spp;
            g.DrawString(L"Confirm", -1, &fBold, confRect, &fC, &bWhite);
        }
        else if (s_showTextUnlockOverlay) {
            SolidBrush tealHdr(ClrTeal);
            GraphicsPath* hdrP = GetSchRoundRectPath(RectF(ovX, ovY2, ovW, 50), 10);
            g.FillPath(&tealHdr, hdrP); delete hdrP;
            g.FillRectangle(&tealHdr, ovX, ovY2 + 30, ovW, 20);
            g.DrawString(L"EXACT TEXT UNLOCK MODE", -1, &fBold, RectF(ovX, ovY2 + 12, ovW, 26), &fC, &bWhite);

            RectF targetBox(ovX + 18, ovY2 + 58, ovW - 36, 120);
            GraphicsPath* tbp = GetSchRoundRectPath(targetBox, 6);
            SolidBrush targetBg(Color(255, 248, 250, 252)); g.FillPath(&targetBg, tbp); g.DrawPath(&pThin, tbp); delete tbp;
            StringFormat fTLClip; fTLClip.SetAlignment(StringAlignmentNear); fTLClip.SetLineAlignment(StringAlignmentNear);
            g.DrawString(s_targetUnlockText.c_str(), -1, &fSmall,
                RectF(targetBox.X + 8, targetBox.Y + 8, targetBox.Width - 16, targetBox.Height - 16),
                &fTLClip, &bGray);

            RectF typeBox(ovX + 18, ovY2 + 188, ovW - 36, 170);
            GraphicsPath* tp = GetSchRoundRectPath(typeBox, 6);
            g.FillPath(s_isTypingActive ? &bCardBg : &bBg, tp);
            Pen* pTypePen = s_isTypingActive ? &pFocus : &pThin;
            g.DrawPath(pTypePen, tp); delete tp;
            g.DrawString(s_currentTypingText.c_str(), -1, &fSmall,
                RectF(typeBox.X + 8, typeBox.Y + 8, typeBox.Width - 16, typeBox.Height - 16),
                &fTLClip, &bDark);

            bool textMatches = (s_currentTypingText == s_targetUnlockText);

            RectF cancelRect(ovX + 100, ovY2 + 376, 140, 40);
            GraphicsPath* cp = GetSchRoundRectPath(cancelRect, 6);
            SolidBrush cancelBrush(s_hTextUnlockCancel ? ClrBgHover : ClrBg);
            g.FillPath(&cancelBrush, cp); g.DrawPath(&pThin, cp); delete cp;
            g.DrawString(L"Cancel", -1, &fBold, cancelRect, &fC, &bDark);

            RectF confRect(ovX + 268, ovY2 + 376, 165, 40);
            GraphicsPath* spp = GetSchRoundRectPath(confRect, 6);
            SolidBrush confBrush(textMatches ? (s_hTextUnlockConfirm ? ClrTealHover : ClrTeal) : ClrDisabled);
            g.FillPath(&confBrush, spp); delete spp;
            g.DrawString(L"Unlock Profile", -1, &fBold, confRect, &fC, &bWhite);
        }
    }
}

// ==========================================
// MOUSE MOVE LOGIC
// ==========================================
void ProcessScheduleBlocksMouseMove(float x, float y) {
    std::lock_guard<std::mutex> lock(g_schMutex);

    hAddProfileBtn = false;
    s_hTimeMoM = s_hTimeMoP = s_hTimeDM = s_hTimeDP = false;
    s_hTimeHM = s_hTimeHP = s_hTimeMM = s_hTimeMP = false;
    s_hTimeStart = s_hTimeCancel = false;
    s_hPassInput = s_hPassConfirm = s_hPassCancel = false;
    s_hTextUnlockConfirm = s_hTextUnlockCancel = false;
    g_ehb.hSubTab = -1;

    if (s_showTimeOverlay || s_showPassOverlay || s_showTextUnlockOverlay) {
        float ovW = s_showTextUnlockOverlay ? 620.0f : 500.0f;
        float ovH = s_showTextUnlockOverlay ? 460.0f : 290.0f;
        float ovX = s_cx + (s_cw - ovW) / 2.0f;
        float ovY = s_cy + (s_ch - ovH) / 2.0f;

        if (s_showTimeOverlay) {
            if (RectF(ovX + 95, ovY + 72, 36, 36).Contains(x, y)) s_hTimeMoM = true;
            if (RectF(ovX + 187, ovY + 72, 36, 36).Contains(x, y)) s_hTimeMoP = true;
            if (RectF(ovX + 321, ovY + 72, 36, 36).Contains(x, y)) s_hTimeDM = true;
            if (RectF(ovX + 413, ovY + 72, 36, 36).Contains(x, y)) s_hTimeDP = true;
            if (RectF(ovX + 95, ovY + 132, 36, 36).Contains(x, y)) s_hTimeHM = true;
            if (RectF(ovX + 187, ovY + 132, 36, 36).Contains(x, y)) s_hTimeHP = true;
            if (RectF(ovX + 321, ovY + 132, 36, 36).Contains(x, y)) s_hTimeMM = true;
            if (RectF(ovX + 413, ovY + 132, 36, 36).Contains(x, y)) s_hTimeMP = true;
            if (RectF(ovX + 50, ovY + 220, 150, 40).Contains(x, y)) s_hTimeCancel = true;
            if (RectF(ovX + 240, ovY + 220, 160, 40).Contains(x, y)) s_hTimeStart = true;
        }
        else if (s_showPassOverlay) {
            if (RectF(ovX + 40, ovY + 80, ovW - 80, 44).Contains(x, y)) s_hPassInput = true;
            if (RectF(ovX + 40, ovY + 158, 150, 40).Contains(x, y)) s_hPassCancel = true;
            if (RectF(ovX + 230, ovY + 158, 165, 40).Contains(x, y)) s_hPassConfirm = true;
        }
        else if (s_showTextUnlockOverlay) {
            if (RectF(ovX + 100, ovY + 376, 140, 40).Contains(x, y)) s_hTextUnlockCancel = true;
            if (RectF(ovX + 268, ovY + 376, 165, 40).Contains(x, y)) s_hTextUnlockConfirm = true;
        }
        return;
    }

    if (editingProfileIdx != -1) {
        g_ehb.hOptSelf = g_ehb.hOptParents = g_ehb.hOptLongText = false;
        hoverSchWebOptIdx = hoverSchAppOptIdx = hoverSchStoreOptIdx = -1;
        g_ehb.hSave = g_ehb.hCancel = g_ehb.hNext = g_ehb.hBack = false;
        g_ehb.hCbAdultWeb = g_ehb.hCbHardcore = g_ehb.hCbRomantic = g_ehb.hCbStrictDns = false;
        g_ehb.hBtnAddExe = g_ehb.hBtnAddStore = g_ehb.hBtnAddTitle = false;
        g_ehb.hAddWeb = g_ehb.hAddApp = g_ehb.hAddKey = false;
        hoverSchModeDropdown = hoverSchWebCombo = hoverSchAppCombo = hoverSchStoreCombo = false;
        g_ehb.hDay = -1;
        g_ehb.hStH = g_ehb.hStM = g_ehb.hStAmPm = false;
        g_ehb.hEnH = g_ehb.hEnM = g_ehb.hEnAmPm = false;
        g_ehb.hTogInt = g_ehb.hTogUni = false;
        for (auto& qb : s_quickBlocks) qb.hovered = false;

        if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
            for (auto& it : g_profiles[editingProfileIdx].blockedWebsites) it.isHoveredCross = false;
            for (auto& it : g_profiles[editingProfileIdx].blockedApps) it.isHoveredCross = false;
            for (auto& it : g_profiles[editingProfileIdx].adultCustomKeywords) it.isHoveredCross = false;
        }

        for (int i = 0; i < 4; i++) { if (g_ehb.subTabRects[i].Contains(x, y)) g_ehb.hSubTab = i; }

        // Dropdown hover priority
        if (s_activeSubTab == 0 && isSchModeDropdownOpen) {
            if (g_ehb.modeOpt[0].Contains(x, y)) g_ehb.hOptSelf = true;
            if (g_ehb.modeOpt[1].Contains(x, y)) g_ehb.hOptParents = true;
            if (g_ehb.modeOpt[2].Contains(x, y)) g_ehb.hOptLongText = true;
            return;
        }
        if (s_activeSubTab == 2) {
            if (isSchWebComboOpen) {
                for (size_t i = 0; i < g_ehb.webOpts.size(); ++i) {
                    if (g_ehb.webOpts[i].Contains(x, y)) { hoverSchWebOptIdx = (int)i; return; }
                }
            }
            if (isSchAppComboOpen) {
                for (size_t i = 0; i < g_ehb.appOpts.size(); ++i) {
                    if (g_ehb.appOpts[i].Contains(x, y)) { hoverSchAppOptIdx = (int)i; return; }
                }
            }
            if (isSchStoreComboOpen) {
                for (size_t i = 0; i < g_ehb.storeOpts.size(); ++i) {
                    if (g_ehb.storeOpts[i].Contains(x, y)) { hoverSchStoreOptIdx = (int)i; return; }
                }
            }
        }

        if (g_ehb.saveBtn.Contains(x, y)) g_ehb.hSave = true;
        if (g_ehb.cancelBtn.Contains(x, y)) g_ehb.hCancel = true;
        if (g_ehb.nextBtn.Contains(x, y)) g_ehb.hNext = true;
        if (g_ehb.backBtn.Contains(x, y)) g_ehb.hBack = true;

        if (s_activeSubTab == 0) {
            if (g_ehb.modeDrop.Contains(x, y)) hoverSchModeDropdown = true;
            for (int d = 0; d < 7; d++) { if (g_ehb.days[d].Contains(x, y)) g_ehb.hDay = d; }
            if (g_ehb.stH_Box.Contains(x, y)) g_ehb.hStH = true;
            if (g_ehb.stM_Box.Contains(x, y)) g_ehb.hStM = true;
            if (g_ehb.stAmPm.Contains(x, y)) g_ehb.hStAmPm = true;
            if (g_ehb.enH_Box.Contains(x, y)) g_ehb.hEnH = true;
            if (g_ehb.enM_Box.Contains(x, y)) g_ehb.hEnM = true;
            if (g_ehb.enAmPm.Contains(x, y)) g_ehb.hEnAmPm = true;
        }
        else if (s_activeSubTab == 1) {
            if (g_ehb.togInt.Contains(x, y)) g_ehb.hTogInt = true;
            if (g_ehb.togUni.Contains(x, y)) g_ehb.hTogUni = true;
            for (size_t qi = 0; qi < s_quickBlocks.size() && qi < s_quickBlockRects.size(); ++qi) {
                if (s_quickBlockRects[qi].Contains(x, y)) s_quickBlocks[qi].hovered = true;
            }
        }
        else if (s_activeSubTab == 2) {
            if (g_ehb.webCombo.Contains(x, y)) hoverSchWebCombo = true;
            if (g_ehb.appCombo.Contains(x, y)) hoverSchAppCombo = true;
            if (g_ehb.addWeb.Contains(x, y)) g_ehb.hAddWeb = true;
            if (g_ehb.addApp.Contains(x, y)) g_ehb.hAddApp = true;
            if (g_ehb.btnAddExe.Contains(x, y)) g_ehb.hBtnAddExe = true;
            if (g_ehb.btnAddStore.Contains(x, y)) g_ehb.hBtnAddStore = true;
            if (g_ehb.btnAddTitle.Contains(x, y)) g_ehb.hBtnAddTitle = true;
            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                if (g_ehb.listAreas[0].Contains(x, y)) {
                    for (auto& p : g_ehb.webDel) {
                        if (p.first.Contains(x, y) && p.second < (int)g_profiles[editingProfileIdx].blockedWebsites.size())
                            g_profiles[editingProfileIdx].blockedWebsites[p.second].isHoveredCross = true;
                    }
                }
                if (g_ehb.listAreas[1].Contains(x, y)) {
                    for (auto& p : g_ehb.appDel) {
                        if (p.first.Contains(x, y) && p.second < (int)g_profiles[editingProfileIdx].blockedApps.size())
                            g_profiles[editingProfileIdx].blockedApps[p.second].isHoveredCross = true;
                    }
                }
            }
        }
        else if (s_activeSubTab == 3) {
            if (g_ehb.cbAdultWeb.Contains(x, y)) g_ehb.hCbAdultWeb = true;
            if (g_ehb.cbHardcore.Contains(x, y)) g_ehb.hCbHardcore = true;
            if (g_ehb.cbRomantic.Contains(x, y)) g_ehb.hCbRomantic = true;
            if (g_ehb.cbStrictDns.Contains(x, y)) g_ehb.hCbStrictDns = true;
            if (g_ehb.addKey.Contains(x, y)) g_ehb.hAddKey = true;
            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                if (g_ehb.listAreas[2].Contains(x, y)) {
                    for (auto& p : g_ehb.keyDel) {
                        if (p.first.Contains(x, y) && p.second < (int)g_profiles[editingProfileIdx].adultCustomKeywords.size())
                            g_profiles[editingProfileIdx].adultCustomKeywords[p.second].isHoveredCross = true;
                    }
                }
            }
        }
        return;
    }

    // Main list hover
    if (RectF(s_cx + s_cw - 210, s_cy + 22, 185, 38).Contains(x, y)) hAddProfileBtn = true;

    if (s_scrollbarDragging) {
        float maxScroll = (std::max)(0.0f, (ceil((float)g_profiles.size() / 2.0f) * 196.0f) - (s_ch - 96.0f));
        float thumbH = (std::max)(28.0f, (s_ch - 96.0f) * ((s_ch - 96.0f) / ((s_ch - 96.0f) + maxScroll)));
        float thumbRange = (s_ch - 96.0f) - thumbH;
        float dy = y - s_scrollbarDragStartY;
        float newScroll = s_scrollbarDragStartScroll + (thumbRange > 0 ? dy / thumbRange * maxScroll : 0);
        sch_tScroll = (std::max)(0.0f, (std::min)(newScroll, maxScroll));
        return;
    }

    float cardW2 = (s_cw - 56.0f) / 2.0f;
    float cardH = 180.0f;
    float startX = s_cx + 20.0f;
    float startY = s_cy + 96.0f - sch_cScroll;

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        g_profiles[i].hToggle = g_profiles[i].hEdit = g_profiles[i].hDel = false;
        float cX = startX + (i % 2) * (cardW2 + 16.0f);
        float cY = startY + (i / 2) * (cardH + 16.0f);
        if (cY > s_cy + s_ch || cY + cardH < s_cy + 85.0f) continue;

        g_profiles[i].hToggle = RectF(cX + 16, cY + 124, 140, 26).Contains(x, y);
        g_profiles[i].hEdit = RectF(cX + cardW2 - 125, cY + 122, 55, 28).Contains(x, y);
        if (!g_profiles[i].isActive)
            g_profiles[i].hDel = RectF(cX + cardW2 - 62, cY + 122, 46, 28).Contains(x, y);
    }
}

// ==========================================
// MOUSE BUTTON DOWN & UP
// ==========================================
void ProcessScheduleBlocksMouseDown(float x, float y) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    // Scrollbar drag initiation could go here if needed
}

void ProcessScheduleBlocksMouseUp(float x, float y) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    s_scrollbarDragging = false;
}

// ==========================================
// MOUSE CLICK LOGIC
// ==========================================
void ProcessScheduleBlocksMouseClick(float x, float y) {
    std::lock_guard<std::mutex> lock(g_schMutex);

    // --- Overlay clicks ---
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
        if (s_hTimeStart && activeActionProfileIdx >= 0 && activeActionProfileIdx < (int)g_profiles.size()) {
            g_profiles[activeActionProfileIdx].isActive = true;
            g_profiles[activeActionProfileIdx].lockEndTime = std::time(nullptr)
                + (s_focusMonths * 30 * 24 * 3600)
                + (s_focusDays * 24 * 3600)
                + (s_focusHours * 3600)
                + (s_focusMins * 60);
            ApplyProfileBlocking(activeActionProfileIdx, true);
            s_showTimeOverlay = false;
            LogHistoryToHiddenFolderSch(L"Started Schedule: " + g_profiles[activeActionProfileIdx].profileName);
            SaveProfiles();
        }
        return;
    }
    if (s_showPassOverlay) {
        if (s_hPassInput) s_isPassInputActive = true;
        if (s_hPassCancel) { s_showPassOverlay = false; s_inputPassText = L""; }
        if (s_hPassConfirm && !s_inputPassText.empty()
            && activeActionProfileIdx >= 0 && activeActionProfileIdx < (int)g_profiles.size()) {
            if (!s_isStoppingFocus) {
                g_profiles[activeActionProfileIdx].parentsPassword = s_inputPassText;
                g_profiles[activeActionProfileIdx].isActive = true;
                ApplyProfileBlocking(activeActionProfileIdx, true);
            }
            else {
                if (g_profiles[activeActionProfileIdx].parentsPassword == s_inputPassText) {
                    g_profiles[activeActionProfileIdx].isActive = false;
                    ApplyProfileBlocking(activeActionProfileIdx, false);
                }
            }
            s_showPassOverlay = false; s_inputPassText = L""; SaveProfiles();
        }
        return;
    }
    if (s_showTextUnlockOverlay) {
        if (s_hTextUnlockCancel) s_showTextUnlockOverlay = false;
        if (s_hTextUnlockConfirm && s_currentTypingText == s_targetUnlockText
            && activeActionProfileIdx >= 0 && activeActionProfileIdx < (int)g_profiles.size()) {
            g_profiles[activeActionProfileIdx].isActive = false;
            ApplyProfileBlocking(activeActionProfileIdx, false);
            s_showTextUnlockOverlay = false; s_currentTypingText = L""; SaveProfiles();
        }
        return;
    }

    // --- Edit overlay clicks ---
    if (editingProfileIdx != -1) {
        // Sub-tab switching
        for (int i = 0; i < 4; i++) {
            if (g_ehb.subTabRects[i].Contains(x, y)) {
                s_activeSubTab = i;
                activeInput = (i == 0) ? 1 : 0;
                isSchModeDropdownOpen = isSchWebComboOpen = isSchAppComboOpen = isSchStoreComboOpen = false;
                return;
            }
        }
        if (g_ehb.hNext && s_activeSubTab < 3) { s_activeSubTab++; activeInput = (s_activeSubTab == 0) ? 1 : 0; return; }
        if (g_ehb.hBack && s_activeSubTab > 0) { s_activeSubTab--; activeInput = (s_activeSubTab == 0) ? 1 : 0; return; }

        bool dropdownClosed = false;

        if (s_activeSubTab == 0) {
            if (isSchModeDropdownOpen) {
                if (g_ehb.hOptSelf) tempLockMode = 0;
                else if (g_ehb.hOptParents) tempLockMode = 1;
                else if (g_ehb.hOptLongText) tempLockMode = 2;
                isSchModeDropdownOpen = false;
                if (!g_ehb.hOptSelf && !g_ehb.hOptParents && !g_ehb.hOptLongText && !hoverSchModeDropdown)
                    dropdownClosed = true;
                else return;
            }
        }

        if (s_activeSubTab == 2) {
            if (isSchStoreComboOpen) {
                if (hoverSchStoreOptIdx != -1 && editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                    g_profiles[editingProfileIdx].blockedApps.push_back({ schCommonStoreApps[hoverSchStoreOptIdx], false });
                }
                isSchStoreComboOpen = false; return;
            }
            if (isSchWebComboOpen) {
                if (hoverSchWebOptIdx != -1 && editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                    g_profiles[editingProfileIdx].blockedWebsites.push_back({ schCommonWebsites[hoverSchWebOptIdx], false });
                }
                isSchWebComboOpen = false; return;
            }
            if (isSchAppComboOpen) {
                if (hoverSchAppOptIdx != -1 && editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                    g_profiles[editingProfileIdx].blockedApps.push_back({ schCommonApps[hoverSchAppOptIdx], false });
                }
                isSchAppComboOpen = false; return;
            }

            if (g_ehb.hBtnAddExe && editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                OPENFILENAMEW ofn; wchar_t szFile[260] = { 0 };
                ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hParentWnd; ofn.lpstrFile = szFile;
                ofn.nMaxFile = 260;
                ofn.lpstrFilter = L"Executables\0*.exe\0All\0*.*\0";
                ofn.nFilterIndex = 1; ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                if (GetOpenFileNameW(&ofn) == TRUE) {
                    wstring fullPath = ofn.lpstrFile;
                    size_t pos = fullPath.find_last_of(L"\\/");
                    wstring exeName = (pos != wstring::npos) ? fullPath.substr(pos + 1) : fullPath;
                    g_profiles[editingProfileIdx].blockedApps.push_back({ exeName, false });
                }
                return;
            }
            if (g_ehb.hBtnAddStore) { isSchStoreComboOpen = !isSchStoreComboOpen; return; }
        }

        if (dropdownClosed) return;

        if (g_ehb.hCancel) {
            // FIX: If adding new profile (editingProfileIdx == g_profiles.size()-1 and was just pushed),
            // remove it on cancel
            if (!g_profiles.empty() && g_profiles.back().profileName.empty()
                && editingProfileIdx == (int)g_profiles.size() - 1) {
                g_profiles.pop_back();
            }
            editingProfileIdx = -1; return;
        }
        if (g_ehb.hSave && editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
            if (inpProfileName.empty()) inpProfileName = L"Custom Profile";
            g_profiles[editingProfileIdx].profileName = inpProfileName;
            g_profiles[editingProfileIdx].lockMode = tempLockMode;
            for (int d = 0; d < 7; d++) g_profiles[editingProfileIdx].activeDays[d] = editDays[d];
            g_profiles[editingProfileIdx].startHour = editStH; g_profiles[editingProfileIdx].startMin = editStM;
            g_profiles[editingProfileIdx].endHour = editEnH; g_profiles[editingProfileIdx].endMin = editEnM;
            g_profiles[editingProfileIdx].blockInternet = editBlockInt;
            g_profiles[editingProfileIdx].blockUninstall = editBlockUninst;
            LogHistoryToHiddenFolderSch(L"Saved Profile: " + inpProfileName);
            editingProfileIdx = -1; SaveProfiles(); return;
        }

        if (s_activeSubTab == 0) {
            if (hoverSchModeDropdown) { isSchModeDropdownOpen = true; return; }
            if (g_ehb.hDay != -1) editDays[g_ehb.hDay] = !editDays[g_ehb.hDay];
            if (g_ehb.hStH) editStH = (editStH + 1) % 24;
            if (g_ehb.hStM) editStM = (editStM + 5) % 60;
            if (g_ehb.hStAmPm) editStH = (editStH + 12) % 24;
            if (g_ehb.hEnH) editEnH = (editEnH + 1) % 24;
            if (g_ehb.hEnM) editEnM = (editEnM + 5) % 60;
            if (g_ehb.hEnAmPm) editEnH = (editEnH + 12) % 24;
            if (g_ehb.nameInp.Contains(x, y)) activeInput = 1; else if (activeInput == 1) {} // keep
        }
        else if (s_activeSubTab == 1) {
            if (g_ehb.hTogInt) editBlockInt = !editBlockInt;
            if (g_ehb.hTogUni) editBlockUninst = !editBlockUninst;
            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                for (size_t qi = 0; qi < s_quickBlocks.size() && qi < s_quickBlockRects.size(); ++qi) {
                    if (s_quickBlockRects[qi].Contains(x, y)) {
                        auto& ep = g_profiles[editingProfileIdx];
                        if (qi == 0) ep.qbYTShorts = !ep.qbYTShorts;
                        else if (qi == 1) ep.qbFBReels = !ep.qbFBReels;
                        else if (qi == 2) ep.qbYTAds = !ep.qbYTAds;
                        else if (qi == 3) ep.qbIGReels = !ep.qbIGReels;
                    }
                }
            }
        }
        else if (s_activeSubTab == 2) {
            if (hoverSchWebCombo) { isSchWebComboOpen = true; return; }
            if (hoverSchAppCombo) { isSchAppComboOpen = true; return; }
            if (g_ehb.webInp.Contains(x, y)) activeInput = 2;
            else if (g_ehb.appInp.Contains(x, y)) activeInput = 3;
            else activeInput = 0;
            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                auto& ep = g_profiles[editingProfileIdx];
                // FIX: Add on button click only, not on hover
                if (g_ehb.addWeb.Contains(x, y) && !inpWeb.empty()) {
                    ep.blockedWebsites.push_back({ inpWeb, false }); inpWeb = L"";
                }
                if (g_ehb.addApp.Contains(x, y) && !inpApp.empty()) {
                    if (inpApp.size() < 4 || inpApp.substr(inpApp.size() - 4) != L".exe") inpApp += L".exe";
                    ep.blockedApps.push_back({ inpApp, false }); inpApp = L"";
                }
                // FIX: Check bounds before erase
                if (g_ehb.listAreas[0].Contains(x, y)) {
                    for (auto it = g_ehb.webDel.rbegin(); it != g_ehb.webDel.rend(); ++it) {
                        if (it->first.Contains(x, y) && it->second < (int)ep.blockedWebsites.size()) {
                            ep.blockedWebsites.erase(ep.blockedWebsites.begin() + it->second); break;
                        }
                    }
                }
                if (g_ehb.listAreas[1].Contains(x, y)) {
                    for (auto it = g_ehb.appDel.rbegin(); it != g_ehb.appDel.rend(); ++it) {
                        if (it->first.Contains(x, y) && it->second < (int)ep.blockedApps.size()) {
                            ep.blockedApps.erase(ep.blockedApps.begin() + it->second); break;
                        }
                    }
                }
            }
        }
        else if (s_activeSubTab == 3) {
            if (editingProfileIdx >= 0 && editingProfileIdx < (int)g_profiles.size()) {
                auto& ep = g_profiles[editingProfileIdx];
                if (g_ehb.cbAdultWeb.Contains(x, y)) ep.schAdultWeb = !ep.schAdultWeb;
                if (g_ehb.cbHardcore.Contains(x, y)) ep.schHardcore = !ep.schHardcore;
                if (g_ehb.cbRomantic.Contains(x, y)) ep.schRomantic = !ep.schRomantic;
                if (g_ehb.cbStrictDns.Contains(x, y)) ep.schStrictDns = !ep.schStrictDns;
                if (g_ehb.keyInp.Contains(x, y)) activeInput = 4; else activeInput = 0;
                if (g_ehb.addKey.Contains(x, y) && !inpKey.empty()) {
                    ep.adultCustomKeywords.push_back({ inpKey, false }); inpKey = L"";
                }
                if (g_ehb.listAreas[2].Contains(x, y)) {
                    for (auto it = g_ehb.keyDel.rbegin(); it != g_ehb.keyDel.rend(); ++it) {
                        if (it->first.Contains(x, y) && it->second < (int)ep.adultCustomKeywords.size()) {
                            ep.adultCustomKeywords.erase(ep.adultCustomKeywords.begin() + it->second); break;
                        }
                    }
                }
            }
        }
        return;
    }

    // --- Main list clicks ---
    if (hAddProfileBtn) {
        FocusProfile np; np.profileName = L""; np.lockMode = 0;
        g_profiles.push_back(np);
        editingProfileIdx = (int)g_profiles.size() - 1;
        s_activeSubTab = 0;
        inpProfileName = L""; inpWeb = L""; inpApp = L""; inpKey = L"";
        tempLockMode = 0;
        for (int d = 0; d < 7; d++) editDays[d] = false;
        editStH = 9; editStM = 0; editEnH = 17; editEnM = 0;
        editBlockInt = false; editBlockUninst = true;
        for (int i = 0; i < 4; i++) { s_listScrollT[i] = 0; s_listScrollC[i] = 0; }
        activeInput = 1;
        return;
    }

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        if (g_profiles[i].hToggle) {
            activeActionProfileIdx = (int)i;
            if (!g_profiles[i].isActive) {
                if (g_profiles[i].lockMode == 0) { s_showTimeOverlay = true; }
                else if (g_profiles[i].lockMode == 1) { s_showPassOverlay = true; s_isStoppingFocus = false; s_inputPassText = L""; }
                else if (g_profiles[i].lockMode == 2) { g_profiles[i].isActive = true; ApplyProfileBlocking((int)i, true); SaveProfiles(); }
            }
            else {
                if (g_profiles[i].lockMode == 0 && g_profiles[i].lockEndTime == 0) {
                    g_profiles[i].isActive = false; ApplyProfileBlocking((int)i, false); SaveProfiles();
                }
                else if (g_profiles[i].lockMode == 1) { s_showPassOverlay = true; s_isStoppingFocus = true; s_inputPassText = L""; }
                else if (g_profiles[i].lockMode == 2) { s_showTextUnlockOverlay = true; s_currentTypingText = L""; s_isTypingActive = true; }
            }
        }
        if (g_profiles[i].hEdit) {
            editingProfileIdx = (int)i;
            s_activeSubTab = 0;
            inpProfileName = g_profiles[i].profileName;
            tempLockMode = g_profiles[i].lockMode;
            for (int d = 0; d < 7; d++) editDays[d] = g_profiles[i].activeDays[d];
            editStH = g_profiles[i].startHour; editStM = g_profiles[i].startMin;
            editEnH = g_profiles[i].endHour; editEnM = g_profiles[i].endMin;
            editBlockInt = g_profiles[i].blockInternet; editBlockUninst = g_profiles[i].blockUninstall;
            for (int ii = 0; ii < 4; ii++) { s_listScrollT[ii] = 0; s_listScrollC[ii] = 0; }
            inpWeb = L""; inpApp = L""; inpKey = L""; activeInput = 1;
        }
        if (g_profiles[i].hDel && !g_profiles[i].isActive) {
            if (MessageBoxW(hParentWnd, L"Delete this profile?", L"Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
                g_profiles.erase(g_profiles.begin() + i); SaveProfiles(); break;
            }
        }
    }
}

// ==========================================
// KEYBOARD LOGIC
// ==========================================
void ProcessScheduleBlocksKeyPress(wchar_t c) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    if (s_showPassOverlay && s_isPassInputActive) {
        if (c >= 32 && c <= 126 && s_inputPassText.length() < 20) s_inputPassText += c;
    }
    else if (s_showTextUnlockOverlay && s_isTypingActive) {
        if (c >= 32 && (int)s_currentTypingText.length() < (int)s_targetUnlockText.length() + 10)
            s_currentTypingText += c;
    }
    else if (editingProfileIdx != -1 && activeInput != 0) {
        if (c >= 32 && c <= 126) {
            if (activeInput == 1 && inpProfileName.length() < 40) inpProfileName += c;
            if (activeInput == 2 && inpWeb.length() < 60) inpWeb += c;
            if (activeInput == 3 && inpApp.length() < 60) inpApp += c;
            if (activeInput == 4 && inpKey.length() < 60) inpKey += c;
        }
    }
}

void ProcessScheduleBlocksKeyDown(WPARAM key) {
    std::lock_guard<std::mutex> lock(g_schMutex);
    if (key == VK_ESCAPE) {
        if (s_showTimeOverlay) { s_showTimeOverlay = false; return; }
        if (s_showPassOverlay) { s_showPassOverlay = false; s_inputPassText = L""; return; }
        if (s_showTextUnlockOverlay) { s_showTextUnlockOverlay = false; return; }
        if (editingProfileIdx != -1) {
            // Cancel: remove empty new profile
            if (!g_profiles.empty() && g_profiles.back().profileName.empty()
                && editingProfileIdx == (int)g_profiles.size() - 1) {
                g_profiles.pop_back();
            }
            editingProfileIdx = -1; return;
        }
    }
    if (s_showPassOverlay && s_isPassInputActive) {
        if (key == VK_BACK && !s_inputPassText.empty()) s_inputPassText.pop_back();
    }
    else if (s_showTextUnlockOverlay && s_isTypingActive) {
        if (key == VK_BACK && !s_currentTypingText.empty()) s_currentTypingText.pop_back();
    }
    else if (editingProfileIdx != -1 && editingProfileIdx < (int)g_profiles.size()) {
        if (key == VK_BACK) {
            if (activeInput == 1 && !inpProfileName.empty()) inpProfileName.pop_back();
            if (activeInput == 2 && !inpWeb.empty()) inpWeb.pop_back();
            if (activeInput == 3 && !inpApp.empty()) inpApp.pop_back();
            if (activeInput == 4 && !inpKey.empty()) inpKey.pop_back();
        }
        else if (key == VK_RETURN) {
            auto& ep = g_profiles[editingProfileIdx];
            if (activeInput == 2 && !inpWeb.empty()) { ep.blockedWebsites.push_back({ inpWeb, false }); inpWeb = L""; }
            if (activeInput == 3 && !inpApp.empty()) {
                if (inpApp.size() < 4 || inpApp.substr(inpApp.size() - 4) != L".exe") inpApp += L".exe";
                ep.blockedApps.push_back({ inpApp, false }); inpApp = L"";
            }
            if (activeInput == 4 && !inpKey.empty()) { ep.adultCustomKeywords.push_back({ inpKey, false }); inpKey = L""; }
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
            for (int i = 0; i < 2; i++) {
                if (g_ehb.listAreas[i].Contains(x, y)) {
                    s_listScrollT[i] -= steps * scrollStep;
                    s_listScrollT[i] = (std::max)(0.0f, (std::min)(s_listScrollT[i], s_listScrollMax[i]));
                }
            }
        }
        else if (s_activeSubTab == 3) {
            if (g_ehb.listAreas[2].Contains(x, y)) {
                s_listScrollT[2] -= steps * scrollStep;
                s_listScrollT[2] = (std::max)(0.0f, (std::min)(s_listScrollT[2], s_listScrollMax[2]));
            }
        }
        isSchModeDropdownOpen = isSchWebComboOpen = isSchAppComboOpen = isSchStoreComboOpen = false;
        return;
    }

    sch_tScroll -= steps * scrollStep;
    float totalRows = ceil((float)g_profiles.size() / 2.0f);
    float maxScroll = (std::max)(0.0f, (totalRows * 196.0f) - (s_ch - 96.0f));
    sch_tScroll = (std::max)(0.0f, (std::min)(sch_tScroll, maxScroll));
}
