// ════════════════════════════════════════════════════════════════════
// tab_family_link.cpp
// RasFocus — Family Link Tab (Complete Rewrite)
//
// Architecture:
//   Child PC  → 6-digit PIN enter করে → Firebase pairing_codes check
//   Firebase  → parent_commands/{hwId} document realtime listen করে
//   Parent    → Firebase থেকে commands পাঠায় → child instantly apply করে
//
// Child শুধু PIN connect করে। বাকি সব Parent control করে।
// ════════════════════════════════════════════════════════════════════

#include "tab_family_link.h"
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <atomic>
#include <algorithm>
#include <windows.h>
#include <tlhelp32.h>
#include <powrprof.h>
#pragma comment(lib, "PowrProf.lib")

using namespace Gdiplus;
using namespace std;

// ── Main project থেকে extern ──
extern string g_currentPackage;
extern bool   g_isPremiumUser;
extern string SendFirestoreRequest(const string& method, const string& path, const string& payload);
extern string GetHardwareID();
extern string g_loggedInUserUid;

// ── Firebase project ──
static const string FB_PROJECT = "rasfocus-c746d";
static const string FB_BASE    = "/v1/projects/" + FB_PROJECT + "/databases/(default)/documents/";

// ════════════════════════════════════════════════════════════════════
// GLOBAL PARENT CONTROL STATE  (header এ extern declare আছে)
// ════════════════════════════════════════════════════════════════════
bool   g_isLinkedToParent        = false;
string g_parentUid               = "";

bool   g_parentLockAllTabs       = false;
bool   g_parentForceAdultBlock   = false;
bool   g_parentForceReelsBlock   = false;
bool   g_parentForceShortsBlock  = false;

bool   g_parentAppControlEnabled = false;
string g_parentAppMode           = "BLOCK";   // "ALLOW" or "BLOCK"
string g_parentAllowedAppsCSV    = "";
string g_parentBlockedAppsCSV    = "";

bool   g_parentWebBlockEnabled   = false;
string g_parentBlockedWebsCSV    = "";

bool   g_parentBlockTaskManager  = false;
bool   g_parentBlockSettings     = false;
bool   g_parentBlockFileManager  = false;
string g_parentBlockedFoldersCSV = "";

bool   g_parentInternetFasting   = false;
int    g_parentPowerAction       = 0;  // 0=None, 1=Lock, 2=Sleep, 3=Shutdown

int         g_parentTimeLimitMinutes = 0;
ULONGLONG   g_parentTimeLimitStart   = 0;

// ════════════════════════════════════════════════════════════════════
// INTERNAL STATE
// ════════════════════════════════════════════════════════════════════
static wchar_t  fl_pinCode[7]       = L"";
static bool     fl_isPinFocused     = false;
static bool     fl_hoverConnectBtn  = false;
static int      fl_connectionState  = 0; // 0=Idle, 1=Connecting, 2=Success, 3=Error
static wstring  fl_statusMsg        = L"";
static HWND     fl_hwnd             = NULL;

// Polling timer — Firebase থেকে parent_commands poll করার জন্য
static UINT_PTR FL_POLL_TIMER_ID    = 9901;
static int      fl_pollIntervalSec  = 5;   // প্রতি 5 সেকেন্ডে poll
static int      fl_pollTick         = 0;

// Power action — একবারই execute হবে
static int      fl_lastPowerAction  = 0;

// Internet fasting state
static bool     fl_fastingApplied   = false;

// ════════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ════════════════════════════════════════════════════════════════════

// JSON থেকে string value বের করা
static string ExtractJsonStr(const string& json, const string& key) {
    string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return "";

    size_t sv = json.find("\"stringValue\": \"", pos);
    size_t bv = json.find("\"booleanValue\": ",  pos);
    size_t iv = json.find("\"integerValue\": \"", pos);
    size_t nk = json.find("\"", pos + search.size() + 10); // next key approx

    if (sv != string::npos && (bv == string::npos || sv < bv) && (iv == string::npos || sv < iv)) {
        sv += 16;
        size_t end = json.find("\"", sv);
        if (end != string::npos) return json.substr(sv, end - sv);
    } else if (bv != string::npos && (sv == string::npos || bv < sv)) {
        bv += 16;
        size_t end = json.find_first_of(",\n}", bv);
        if (end != string::npos) return json.substr(bv, end - bv);
    } else if (iv != string::npos) {
        iv += 17;
        size_t end = json.find("\"", iv);
        if (end != string::npos) return json.substr(iv, end - iv);
    }
    return "";
}

// ExtractJsonStr ছাড়া field আছে কিনা check করার জন্য
// absent  → false  (parse skip)
// present → true, out = value (empty string হলেও)
static bool FindJsonField(const string& json, const string& key, string& out) {
    string search = "\"" + key + "\"";
    if (json.find(search) == string::npos) return false;
    out = ExtractJsonStr(json, key);
    return true;
}

// CSV string থেকে vector বানানো
static vector<string> CSVToVector(const string& csv) {
    vector<string> result;
    if (csv.empty()) return result;
    stringstream ss(csv);
    string item;
    while (getline(ss, item, ',')) {
        // trim whitespace
        size_t s = item.find_first_not_of(" \t\r\n");
        size_t e = item.find_last_not_of(" \t\r\n");
        if (s != string::npos) result.push_back(item.substr(s, e - s + 1));
    }
    return result;
}

// Lowercase করা
static string ToLower(string s) {
    transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Rounded rect path helper
static void AddRoundRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

// ════════════════════════════════════════════════════════════════════
// FIREBASE: PARENT COMMANDS POLL
// প্রতি 5 সেকেন্ডে Firebase থেকে parent_commands/{hwId} পড়ে
// সব global variables update করে
// ════════════════════════════════════════════════════════════════════
static void PollParentCommands() {
    if (!g_isLinkedToParent) return;
    string hwId    = GetHardwareID();
    string getPath = FB_BASE + "parent_commands/" + hwId;
    string resp    = SendFirestoreRequest("GET", getPath, "");

    if (resp.empty() || resp.find("\"error\"") != string::npos) return;

    // ── Tab Lock ──
    string v = ExtractJsonStr(resp, "lock_all_tabs");
    g_parentLockAllTabs = (v == "true");

    // ── Adult / Content Block ──
    v = ExtractJsonStr(resp, "force_adult_block");
    g_parentForceAdultBlock = (v == "true");

    v = ExtractJsonStr(resp, "force_reels_block");
    g_parentForceReelsBlock = (v == "true");

    v = ExtractJsonStr(resp, "force_shorts_block");
    g_parentForceShortsBlock = (v == "true");

    // ── App Control ──
    v = ExtractJsonStr(resp, "app_control_enabled");
    g_parentAppControlEnabled = (v == "true");

    v = ExtractJsonStr(resp, "app_mode");
    if (!v.empty()) g_parentAppMode = v;  // "ALLOW" or "BLOCK"

    if (FindJsonField(resp, "allowed_apps_csv",  v)) g_parentAllowedAppsCSV  = v;
    if (FindJsonField(resp, "blocked_apps_csv",  v)) g_parentBlockedAppsCSV  = v;

    // ── Website Block ──
    v = ExtractJsonStr(resp, "web_block_enabled");
    g_parentWebBlockEnabled = (v == "true");

    if (FindJsonField(resp, "blocked_webs_csv",     v)) g_parentBlockedWebsCSV     = v;

    // ── System Lock ──
    v = ExtractJsonStr(resp, "block_task_manager");
    g_parentBlockTaskManager = (v == "true");

    v = ExtractJsonStr(resp, "block_settings");
    g_parentBlockSettings = (v == "true");

    v = ExtractJsonStr(resp, "block_file_manager");
    g_parentBlockFileManager = (v == "true");

    if (FindJsonField(resp, "blocked_folders_csv",  v)) g_parentBlockedFoldersCSV  = v;

    // ── Internet Fasting ──
    v = ExtractJsonStr(resp, "internet_fasting");
    g_parentInternetFasting = (v == "true");

    // ── Power Action ──
    v = ExtractJsonStr(resp, "power_action");
    if (!v.empty()) g_parentPowerAction = atoi(v.c_str());

    // ── Screen Time ──
    v = ExtractJsonStr(resp, "time_limit_minutes");
    if (!v.empty()) {
        int newLimit = atoi(v.c_str());
        if (newLimit != g_parentTimeLimitMinutes) {
            g_parentTimeLimitMinutes = newLimit;
            g_parentTimeLimitStart   = GetTickCount64();
        }
    }

    // UI refresh
    if (fl_hwnd) InvalidateRect(fl_hwnd, NULL, FALSE);
}

// ════════════════════════════════════════════════════════════════════
// FIREBASE: INITIAL PARENT_COMMANDS DOCUMENT SETUP (connect এর সময়)
// ════════════════════════════════════════════════════════════════════
static void InitParentCommandsDocument(const string& hwId, const string& parentUid) {
    string cmdPath = FB_BASE + "parent_commands/" + hwId;

    // আগে check করো আছে কিনা
    string checkResp = SendFirestoreRequest("GET", cmdPath, "");
    if (checkResp.find("NOT_FOUND") == string::npos &&
        checkResp.find("\"error\"")  == string::npos) {
        // Already exists — শুধু parent_uid আপডেট করো
        string patchPath = cmdPath + "?updateMask.fieldPaths=parent_uid"
                                     "&updateMask.fieldPaths=child_device_id";
        string payload = "{\"fields\":{"
            "\"parent_uid\":{\"stringValue\":\"" + parentUid + "\"},"
            "\"child_device_id\":{\"stringValue\":\"" + hwId + "\"}"
            "}}";
        SendFirestoreRequest("PATCH", patchPath, payload);
        return;
    }

    // নতুন document তৈরি — সব fields default value সহ
    string initPayload = "{\"fields\":{"
        "\"parent_uid\":{\"stringValue\":\"" + parentUid + "\"},"
        "\"child_device_id\":{\"stringValue\":\"" + hwId + "\"},"
        "\"lock_all_tabs\":{\"booleanValue\":false},"
        "\"force_adult_block\":{\"booleanValue\":false},"
        "\"force_reels_block\":{\"booleanValue\":false},"
        "\"force_shorts_block\":{\"booleanValue\":false},"
        "\"app_control_enabled\":{\"booleanValue\":false},"
        "\"app_mode\":{\"stringValue\":\"BLOCK\"},"
        "\"allowed_apps_csv\":{\"stringValue\":\"\"},"
        "\"blocked_apps_csv\":{\"stringValue\":\"\"},"
        "\"web_block_enabled\":{\"booleanValue\":false},"
        "\"blocked_webs_csv\":{\"stringValue\":\"\"},"
        "\"block_task_manager\":{\"booleanValue\":false},"
        "\"block_settings\":{\"booleanValue\":false},"
        "\"block_file_manager\":{\"booleanValue\":false},"
        "\"blocked_folders_csv\":{\"stringValue\":\"\"},"
        "\"internet_fasting\":{\"booleanValue\":false},"
        "\"power_action\":{\"integerValue\":\"0\"},"
        "\"time_limit_minutes\":{\"integerValue\":\"0\"}"
        "}}";
    SendFirestoreRequest("PATCH", cmdPath, initPayload);
}

// ════════════════════════════════════════════════════════════════════
// ENFORCEMENT: PARENT COMMANDS APPLY
// প্রতি সেকেন্ডে main timer থেকে call করতে হবে
// ════════════════════════════════════════════════════════════════════
void FamilyLink_EnforceParentCommands(HWND hWnd) {
    if (!g_isLinkedToParent) return;

    // ── 1. Task Manager Block ──
    if (g_parentBlockTaskManager) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe = { sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do {
                string name = pe.szExeFile;
                transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == "taskmgr.exe") {
                    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (ph) { TerminateProcess(ph, 1); CloseHandle(ph); }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    // ── 2. Windows Settings Block ──
    if (g_parentBlockSettings) {
        // ms-settings: URI scheme দিয়ে খোলা SystemSettings.exe kill করো
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe = { sizeof(pe) };
        if (Process32First(hSnap, &pe)) {
            do {
                string name = pe.szExeFile;
                transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == "systemsettings.exe" ||
                    name == "immersivecontrolpanel.exe") {
                    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (ph) { TerminateProcess(ph, 1); CloseHandle(ph); }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);

        // Registry দিয়েও disable করা (পারলে)
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
            0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            DWORD val = 1;
            RegSetValueExA(hKey, "NoControlPanel", 0, REG_DWORD, (BYTE*)&val, sizeof(val));
            RegSetValueExA(hKey, "NoSetFolders",   0, REG_DWORD, (BYTE*)&val, sizeof(val));
            RegCloseKey(hKey);
        }
    } else {
        // Settings block বন্ধ — registry clean করো
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
            0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueA(hKey, "NoControlPanel");
            RegDeleteValueA(hKey, "NoSetFolders");
            RegCloseKey(hKey);
        }
    }

    // ── 3. File Manager (Explorer) Block ──
    if (g_parentBlockFileManager) {
        HWND hFE = NULL;
        // সব explorer windows খুঁজে বের করো
        while ((hFE = FindWindowExA(NULL, hFE, "CabinetWClass", NULL)) != NULL) {
            // Main desktop explorer বাদ দাও
            PostMessage(hFE, WM_CLOSE, 0, 0);
        }
    }

    // ── 4. Specific Folder Block ──
    if (!g_parentBlockedFoldersCSV.empty()) {
        vector<string> blockedFolders = CSVToVector(g_parentBlockedFoldersCSV);
        HWND hFE = NULL;
        while ((hFE = FindWindowExA(NULL, hFE, "CabinetWClass", NULL)) != NULL) {
            char title[512] = {};
            GetWindowTextA(hFE, title, sizeof(title));
            string t = ToLower(string(title));
            for (const auto& folder : blockedFolders) {
                if (!folder.empty() && t.find(ToLower(folder)) != string::npos) {
                    PostMessage(hFE, WM_CLOSE, 0, 0);
                    break;
                }
            }
        }
    }

    // ── 5. App Control (Allow Mode / Block Mode) ──
    if (g_parentAppControlEnabled) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe = { sizeof(pe) };
        DWORD myPid = GetCurrentProcessId();

        // System apps যেগুলো কখনো kill করব না
        static const vector<string> systemSafe = {
            "explorer.exe","svchost.exe","csrss.exe","dwm.exe","lsass.exe",
            "services.exe","smss.exe","wininit.exe","winlogon.exe","spoolsv.exe",
            "fontdrvhost.exe","sihost.exe","taskhostw.exe","ctfmon.exe",
            "audiodg.exe","searchindexer.exe","registry","system",
            "conhost.exe","applicationframehost.exe"
        };

        if (Process32First(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID == myPid) continue;
                string name = ToLower(string(pe.szExeFile));

                bool isSysApp = (find(systemSafe.begin(), systemSafe.end(), name) != systemSafe.end());
                if (isSysApp) continue;

                bool shouldKill = false;

                if (g_parentAppMode == "ALLOW") {
                    // Allow mode: শুধু allowed apps চলবে, বাকি সব kill
                    vector<string> allowed = CSVToVector(g_parentAllowedAppsCSV);
                    bool isAllowed = false;
                    for (const auto& a : allowed) {
                        string la = ToLower(a);
                        if (la.find('.') == string::npos) la += ".exe";
                        if (name == la) { isAllowed = true; break; }
                    }
                    // Browsers সবসময় allow (web filtering আলাদাভাবে হয়)
                    bool isBrowser = (name == "chrome.exe" || name == "msedge.exe" ||
                                      name == "firefox.exe" || name == "brave.exe" ||
                                      name == "opera.exe"   || name == "vivaldi.exe");
                    if (!isAllowed && !isBrowser) shouldKill = true;
                } else {
                    // Block mode: blocked apps গুলো kill
                    vector<string> blocked = CSVToVector(g_parentBlockedAppsCSV);
                    for (const auto& a : blocked) {
                        string la = ToLower(a);
                        if (la.find('.') == string::npos) la += ".exe";
                        if (name == la) { shouldKill = true; break; }
                    }
                }

                if (shouldKill) {
                    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (ph) { TerminateProcess(ph, 1); CloseHandle(ph); }
                }

            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    // ── 6. Website Block (Browser Window Title চেক) ──
    if (g_parentWebBlockEnabled && !g_parentBlockedWebsCSV.empty()) {
        vector<string> blockedWebs = CSVToVector(g_parentBlockedWebsCSV);
        HWND hActive = GetForegroundWindow();
        if (hActive && hActive != hWnd) {
            char title[512] = {};
            if (GetWindowTextA(hActive, title, sizeof(title)) > 0) {
                string t = ToLower(string(title));
                for (const auto& site : blockedWebs) {
                    string s = ToLower(site);
                    // domain এর মূল অংশ match করা
                    // e.g. "youtube.com" → "youtube" match করলেই হবে
                    size_t dotPos = s.find('.');
                    string domain = (dotPos != string::npos) ? s.substr(0, dotPos) : s;
                    if (!domain.empty() && t.find(domain) != string::npos) {
                        // Tab close করো
                        keybd_event(VK_CONTROL, 0, 0, 0);
                        keybd_event('W', 0, 0, 0);
                        keybd_event('W', 0, KEYEVENTF_KEYUP, 0);
                        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
                        Sleep(50);
                        ShowWindow(hActive, SW_MINIMIZE);
                        break;
                    }
                }
            }
        }
    }

    // ── 7. Internet Fasting ──
    if (g_parentInternetFasting && !fl_fastingApplied) {
        fl_fastingApplied = true;
        // Network adapter disable করা (ipconfig /release)
        // Admin privilege লাগে — shell command দিয়ে
        SHELLEXECUTEINFOA sei = { sizeof(sei) };
        sei.lpVerb       = "runas";
        sei.lpFile       = "cmd.exe";
        sei.lpParameters = "/c ipconfig /release";
        sei.nShow        = SW_HIDE;
        ShellExecuteExA(&sei);
    } else if (!g_parentInternetFasting && fl_fastingApplied) {
        fl_fastingApplied = false;
        // Network restore
        SHELLEXECUTEINFOA sei = { sizeof(sei) };
        sei.lpVerb       = "runas";
        sei.lpFile       = "cmd.exe";
        sei.lpParameters = "/c ipconfig /renew";
        sei.nShow        = SW_HIDE;
        ShellExecuteExA(&sei);
    }

    // ── 8. Power Action (একবারই execute হবে) ──
    if (g_parentPowerAction != 0 && g_parentPowerAction != fl_lastPowerAction) {
        fl_lastPowerAction = g_parentPowerAction;

        if (g_parentPowerAction == 1) {
            // Lock PC
            LockWorkStation();
        } else if (g_parentPowerAction == 2) {
            // Sleep
            SetSuspendState(FALSE, FALSE, FALSE);
        } else if (g_parentPowerAction == 3) {
            // Shutdown
            SHELLEXECUTEINFOA sei = { sizeof(sei) };
            sei.lpVerb       = "runas";
            sei.lpFile       = "cmd.exe";
            sei.lpParameters = "/c shutdown /s /t 30";
            sei.nShow        = SW_HIDE;
            ShellExecuteExA(&sei);
        }

        // Firebase এ power_action reset করো (0 করো)
        string hwId = GetHardwareID();
        string patchPath = FB_BASE + "parent_commands/" + hwId +
                           "?updateMask.fieldPaths=power_action";
        string payload = "{\"fields\":{\"power_action\":{\"integerValue\":\"0\"}}}";
        // Background thread এ করো যাতে block না করে
        thread([patchPath, payload]() {
            SendFirestoreRequest("PATCH", patchPath, payload);
        }).detach();
    }

    // ── 9. Screen Time Limit ──
    if (g_parentTimeLimitMinutes > 0 && g_parentTimeLimitStart > 0) {
        ULONGLONG elapsed = (GetTickCount64() - g_parentTimeLimitStart) / 60000ULL;
        if (elapsed >= (ULONGLONG)g_parentTimeLimitMinutes) {
            // Time শেষ — PC lock করো
            LockWorkStation();
            // Limit reset করো পরবর্তীতে আবার trigger না হওয়ার জন্য
            g_parentTimeLimitStart = GetTickCount64(); // reset
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// DRAW — Family Link Tab UI
// ════════════════════════════════════════════════════════════════════
void DrawFamilyLinkTab(Graphics& g, float x, float y, float w, float h) {

    // Background
    SolidBrush bgBrush(Color(255, 245, 248, 250));
    g.FillRectangle(&bgBrush, x, y, w, h);

    FontFamily ff(L"Segoe UI");
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);

    SolidBrush colTeal(Color(255, 0, 150, 160));
    SolidBrush colGray(Color(255, 100, 100, 100));
    SolidBrush colDark(Color(255, 40, 40, 50));
    SolidBrush colRed(Color(255, 220, 50, 50));
    SolidBrush colGreen(Color(255, 0, 160, 90));
    SolidBrush colOrange(Color(255, 230, 120, 0));

    Font fHeader(&ff, 26, FontStyleBold,    UnitPixel);
    Font fTitle (&ff, 16, FontStyleBold,    UnitPixel);
    Font fDesc  (&ff, 13, FontStyleRegular, UnitPixel);
    Font fBold  (&ff, 13, FontStyleBold,    UnitPixel);
    Font fSmall (&ff, 11, FontStyleRegular, UnitPixel);

    float centerY = y + 55.0f;

    // ── Premium check ──
    bool hasAccess = (g_currentPackage == "PREMIUM" ||
                      g_currentPackage == "PARENTAL" ||
                      g_currentPackage == "TRIAL");
    if (!hasAccess) {
        g.DrawString(L"Family Link — Premium Feature", -1, &fHeader,
            RectF(x, y + 150.0f, w, 40.0f), &fmtC, &colDark);
        g.DrawString(L"Upgrade to RasFocus+ Pro or Combo package to use Family Link.",
            -1, &fDesc, RectF(x, y + 200.0f, w, 30.0f), &fmtC, &colGray);
        return;
    }

    // ══════════════════════════════════════════════════════════
    // VIEW A: Connected — Parent Control Dashboard
    // ══════════════════════════════════════════════════════════
    if (g_isLinkedToParent) {
        g.DrawString(L"Family Link — Active", -1, &fHeader,
            RectF(x, centerY, w, 36.0f), &fmtC, &colTeal);

        float cardW = min(w - 40.0f, 560.0f);
        float cardX = x + (w - cardW) / 2.0f;
        float cardY = centerY + 48.0f;
        float rowH  = 38.0f;
        float lx    = cardX + 20.0f;
        float vx    = cardX + 180.0f;

        // ─ Controls grid ─
        // সব controls দুই column এ দেখানো হবে
        struct ControlRow {
            wstring label;
            wstring value;
            ARGB    valueColor;
        };

        // Screen time remaining
        wstring tlStr = L"No limit";
        if (g_parentTimeLimitMinutes > 0) {
            ULONGLONG elapsed   = (GetTickCount64() - g_parentTimeLimitStart) / 60000ULL;
            ULONGLONG remaining = (elapsed < (ULONGLONG)g_parentTimeLimitMinutes)
                                  ? (ULONGLONG)g_parentTimeLimitMinutes - elapsed : 0ULL;
            tlStr = to_wstring(remaining) + L" min left";
        }

        // Short parent UID display
        wchar_t parentW[64] = {};
        string shortP = g_parentUid.size() > 12
            ? g_parentUid.substr(0, 6) + "..." + g_parentUid.substr(g_parentUid.size() - 4)
            : g_parentUid;
        MultiByteToWideChar(CP_UTF8, 0, shortP.c_str(), -1, parentW, 63);

        // App mode wstring
        wstring appModeW = L"Off";
        if (g_parentAppControlEnabled) {
            wchar_t buf[64] = {};
            MultiByteToWideChar(CP_UTF8, 0, g_parentAppMode.c_str(), -1, buf, 63);
            appModeW = wstring(buf) + L" mode";
        }

        ControlRow rows[] = {
            { L"Parent ID",       parentW,                                    Color::MakeARGB(255,40,40,50)   },
            { L"Tab Lock",        g_parentLockAllTabs    ? L"LOCKED" : L"Off", g_parentLockAllTabs    ? Color::MakeARGB(255,220,50,50) : Color::MakeARGB(255,100,100,100) },
            { L"Adult Block",     g_parentForceAdultBlock? L"FORCED ON":L"Off", g_parentForceAdultBlock? Color::MakeARGB(255,220,50,50):Color::MakeARGB(255,100,100,100) },
            { L"Reels Block",     g_parentForceReelsBlock? L"ON" : L"Off",     g_parentForceReelsBlock? Color::MakeARGB(255,220,80,50):Color::MakeARGB(255,100,100,100)  },
            { L"Shorts Block",    g_parentForceShortsBlock?L"ON" : L"Off",     g_parentForceShortsBlock?Color::MakeARGB(255,220,80,50):Color::MakeARGB(255,100,100,100) },
            { L"App Control",     appModeW.c_str(),                            g_parentAppControlEnabled? Color::MakeARGB(255,0,130,180):Color::MakeARGB(255,100,100,100) },
            { L"Web Block",       g_parentWebBlockEnabled? L"ON" : L"Off",     g_parentWebBlockEnabled? Color::MakeARGB(255,220,80,50):Color::MakeARGB(255,100,100,100)  },
            { L"Task Manager",    g_parentBlockTaskManager?L"BLOCKED":L"Off",  g_parentBlockTaskManager?Color::MakeARGB(255,180,50,50):Color::MakeARGB(255,100,100,100)  },
            { L"Settings",        g_parentBlockSettings  ? L"BLOCKED":L"Off",  g_parentBlockSettings  ? Color::MakeARGB(255,180,50,50):Color::MakeARGB(255,100,100,100)  },
            { L"File Manager",    g_parentBlockFileManager?L"BLOCKED":L"Off",  g_parentBlockFileManager?Color::MakeARGB(255,180,50,50):Color::MakeARGB(255,100,100,100)  },
            { L"Net Fasting",     g_parentInternetFasting? L"ACTIVE" :L"Off",  g_parentInternetFasting? Color::MakeARGB(255,220,120,0):Color::MakeARGB(255,100,100,100)  },
            { L"Screen Time",     tlStr.c_str(),                               g_parentTimeLimitMinutes>0?Color::MakeARGB(255,230,120,0):Color::MakeARGB(255,100,100,100)},
        };

        int nRows   = sizeof(rows) / sizeof(rows[0]);
        int halfN   = (nRows + 1) / 2;
        float colW  = (cardW - 40.0f) / 2.0f;
        float startY = cardY + 10.0f;

        for (int i = 0; i < nRows; i++) {
            int  col = i / halfN;
            int  row = i % halfN;
            float rx = cardX + 20.0f + col * colW;
            float ry = startY + row * rowH;

            // Card row background
            if (row % 2 == 0) {
                SolidBrush rowBg(Color(255, 250, 252, 255));
                g.FillRectangle(&rowBg, rx - 8.0f, ry, colW - 4.0f, rowH - 2.0f);
            }

            g.DrawString(rows[i].label.c_str(), -1, &fDesc,
                RectF(rx, ry, 115.0f, rowH), &fmtL, &colGray);

            SolidBrush valColor(rows[i].valueColor);
            g.DrawString(rows[i].value.c_str(), -1, &fBold,
                RectF(rx + 120.0f, ry, colW - 130.0f, rowH), &fmtL, &valColor);
        }

        // Sync info note
        float noteY = startY + halfN * rowH + 12.0f;
        g.DrawString(L"Controls sync from parent in real-time. Child cannot modify these settings.",
            -1, &fSmall, RectF(x, noteY, w, 20.0f), &fmtC, &colGray);

        return;
    }

    // ══════════════════════════════════════════════════════════
    // VIEW B: Not Connected — PIN Entry
    // ══════════════════════════════════════════════════════════
    g.DrawString(L"Family Link Setup", -1, &fHeader,
        RectF(x, centerY, w, 36.0f), &fmtC, &colTeal);

    g.DrawString(L"Enter the 6-digit code from your parent's phone to connect.",
        -1, &fDesc, RectF(x, centerY + 42.0f, w, 26.0f), &fmtC, &colGray);

    // ── PIN input box ──
    float boxW = 240.0f, boxH = 54.0f;
    float boxX = x + (w - boxW) / 2.0f;
    float boxY = centerY + 86.0f;

    GraphicsPath boxPath;
    AddRoundRect(boxPath, boxX, boxY, boxW, boxH, 10.0f);
    SolidBrush boxBg(Color(255, 255, 255, 255));
    g.FillPath(&boxBg, &boxPath);
    Pen boxBorder(fl_isPinFocused ? Color(255, 0, 150, 160) : Color(255, 200, 205, 215), 2.0f);
    g.DrawPath(&boxBorder, &boxPath);

    Font fPin(&ff, 28, FontStyleBold, UnitPixel);
    wstring displayPin(fl_pinCode);
    if (displayPin.empty() && !fl_isPinFocused) {
        SolidBrush ph(Color(255, 185, 185, 185));
        g.DrawString(L"• • • • • •", -1, &fPin,
            RectF(boxX, boxY + 4.0f, boxW, boxH), &fmtC, &ph);
    } else {
        // Spaced digits
        wstring spaced;
        for (size_t i = 0; i < displayPin.length(); i++) {
            spaced += displayPin[i];
            spaced += L"  ";
        }
        SolidBrush pinTxt(Color(255, 40, 40, 50));
        g.DrawString(spaced.c_str(), -1, &fPin,
            RectF(boxX, boxY + 4.0f, boxW, boxH), &fmtC, &pinTxt);
    }

    // ── Connect button ──
    float btnW = 200.0f, btnH = 48.0f;
    float btnX = x + (w - btnW) / 2.0f;
    float btnY = boxY + 68.0f;

    GraphicsPath btnPath;
    AddRoundRect(btnPath, btnX, btnY, btnW, btnH, 10.0f);

    Color btnNormal(255, 0, 150, 160);
    Color btnHover (255, 0, 125, 135);
    Color btnLoading(255, 80, 80, 100);
    SolidBrush btnBrush(fl_connectionState == 1 ? btnLoading
                       : fl_hoverConnectBtn      ? btnHover
                                                 : btnNormal);
    g.FillPath(&btnBrush, &btnPath);

    Font fBtn(&ff, 14, FontStyleBold, UnitPixel);
    SolidBrush btnTxt(Color(255, 255, 255, 255));
    wstring btnLabel = (fl_connectionState == 1) ? L"Connecting..." : L"Connect to Parent";
    g.DrawString(btnLabel.c_str(), -1, &fBtn,
        RectF(btnX, btnY, btnW, btnH), &fmtC, &btnTxt);

    // ── Status message ──
    if (!fl_statusMsg.empty()) {
        ARGB statusARGB = (fl_connectionState == 2) ? Color::MakeARGB(255, 0, 180, 70)
                        : (fl_connectionState == 1) ? Color::MakeARGB(255, 0, 150, 160)
                                                    : Color::MakeARGB(255, 232, 17, 35);
        SolidBrush statusBrush(statusARGB);
        Font fStatus(&ff, 13, FontStyleBold, UnitPixel);
        g.DrawString(fl_statusMsg.c_str(), -1, &fStatus,
            RectF(x, btnY + 60.0f, w, 30.0f), &fmtC, &statusBrush);
    }

    // ── How it works hint ──
    g.DrawString(L"Ask your parent to open RasFocus app → Generate Code → Share the 6-digit code.",
        -1, &fSmall, RectF(x + 20.0f, btnY + 100.0f, w - 40.0f, 24.0f), &fmtC, &colGray);
}

// ════════════════════════════════════════════════════════════════════
// MOUSE MOVE
// ════════════════════════════════════════════════════════════════════
void ProcessFamilyLinkMouseMove(float mx, float my, float cX, float cY) {
    if (g_isLinkedToParent) { fl_hoverConnectBtn = false; return; }

    float contentW = 1024.0f - 170.0f;
    float btnW = 200.0f, btnH = 48.0f;
    float boxY = cY + 55.0f + 86.0f;
    float btnX = cX + (contentW - btnW) / 2.0f;
    float btnY = boxY + 68.0f;

    fl_hoverConnectBtn = (mx >= btnX && mx <= btnX + btnW &&
                          my >= btnY && my <= btnY + btnH);
}

// ════════════════════════════════════════════════════════════════════
// MOUSE CLICK — PIN verify ও Firebase connect
// ════════════════════════════════════════════════════════════════════
void ProcessFamilyLinkMouseClick(float mx, float my, float cX, float cY, HWND hWnd) {
    fl_hwnd = hWnd;
    if (g_isLinkedToParent) return;

    float contentW = 1024.0f - 170.0f;
    float boxW = 240.0f, boxH = 54.0f;
    float boxX = cX + (contentW - boxW) / 2.0f;
    float boxY = cY + 55.0f + 86.0f;

    // PIN box click — focus toggle
    bool clickedBox = (mx >= boxX && mx <= boxX + boxW &&
                       my >= boxY && my <= boxY + boxH);
    fl_isPinFocused = clickedBox;

    if (!fl_hoverConnectBtn) return;

    // ── Connect button clicked ──
    wstring currentPin(fl_pinCode);
    if (currentPin.length() != 6) {
        fl_connectionState = 3;
        fl_statusMsg       = L"Please enter a complete 6-digit PIN.";
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }

    fl_connectionState = 1;
    fl_statusMsg       = L"Verifying PIN with server...";
    InvalidateRect(hWnd, NULL, FALSE);
    UpdateWindow(hWnd);

    // Background thread এ Firebase call করো
    string pinStr(currentPin.begin(), currentPin.end());
    HWND capturedHwnd = hWnd;

    thread([pinStr, capturedHwnd]() {

        // ── Step 1: pairing_codes/{pin} GET ──
        string getPairPath = FB_BASE + "pairing_codes/" + pinStr;
        string pairResp    = SendFirestoreRequest("GET", getPairPath, "");

        if (pairResp.empty() ||
            pairResp.find("\"error\"")   != string::npos ||
            pairResp.find("NOT_FOUND")   != string::npos) {
            fl_connectionState = 3;
            fl_statusMsg       = L"Invalid or expired PIN. Please try again.";
            InvalidateRect(capturedHwnd, NULL, FALSE);
            return;
        }

        string parentUid = ExtractJsonStr(pairResp, "parent_uid");
        if (parentUid.empty()) {
            fl_connectionState = 3;
            fl_statusMsg       = L"Data error: parent_uid not found.";
            InvalidateRect(capturedHwnd, NULL, FALSE);
            return;
        }

        string hwId = GetHardwareID();

        // ── Step 2: devices/{hwId} এ parent_uid save করো ──
        string devPatchPath = FB_BASE + "devices/" + hwId +
                              "?updateMask.fieldPaths=parent_uid"
                              "&updateMask.fieldPaths=linked_at";
        // Current timestamp (epoch seconds string)
        char timeStr[32];
        sprintf_s(timeStr, "%lld", (long long)time(nullptr));
        string devPayload = "{\"fields\":{"
            "\"parent_uid\":{\"stringValue\":\"" + parentUid + "\"},"
            "\"linked_at\":{\"stringValue\":\"" + string(timeStr) + "\"}"
            "}}";
        SendFirestoreRequest("PATCH", devPatchPath, devPayload);

        // ── Step 3: parent_commands document init/update ──
        InitParentCommandsDocument(hwId, parentUid);

        // ── Step 4: pairing_codes/{pin} এ used=true mark করো ──
        string usedPatch = FB_BASE + "pairing_codes/" + pinStr +
                           "?updateMask.fieldPaths=used";
        string usedPayload = "{\"fields\":{\"used\":{\"booleanValue\":true}}}";
        SendFirestoreRequest("PATCH", usedPatch, usedPayload);

        // ── Step 5: State update ──
        g_parentUid       = parentUid;
        g_isLinkedToParent = true;
        fl_connectionState = 2;
        fl_statusMsg       = L"Successfully connected to parent device!";

        // ── Step 6: First poll immediately ──
        PollParentCommands();

        InvalidateRect(capturedHwnd, NULL, FALSE);

    }).detach();
}

// ════════════════════════════════════════════════════════════════════
// KEYBOARD INPUT
// ════════════════════════════════════════════════════════════════════
void ProcessFamilyLinkChar(wchar_t c) {
    if (!fl_isPinFocused) return;
    int len = lstrlenW(fl_pinCode);

    if (c == L'\b') {
        // Backspace
        if (len > 0) fl_pinCode[len - 1] = L'\0';
    } else if (c >= L'0' && c <= L'9') {
        // Digit
        if (len < 6) {
            fl_pinCode[len]     = c;
            fl_pinCode[len + 1] = L'\0';
        }
    }
}

void ProcessFamilyLinkKeyDown(WPARAM wp) {
    if (wp == VK_RETURN && fl_isPinFocused) {
        fl_isPinFocused = false;
    }
}

// ════════════════════════════════════════════════════════════════════
// TIMER — polling ticker
// WM_TIMER case এ call করতে হবে
// ════════════════════════════════════════════════════════════════════
void ProcessFamilyLinkTimer(UINT_PTR timerId, HWND hWnd) {
    // fl_hwnd সবসময় fresh রাখো যাতে PollParentCommands() InvalidateRect করতে পারে
    if (hWnd) fl_hwnd = hWnd;

    if (!g_isLinkedToParent) return;

    fl_pollTick++;
    if (fl_pollTick >= fl_pollIntervalSec) {
        fl_pollTick = 0;
        // Background thread এ poll করো
        thread([]() { PollParentCommands(); }).detach();
    }
}
