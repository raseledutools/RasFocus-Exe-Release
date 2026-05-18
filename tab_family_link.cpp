#include "tab_family_link.h"
#include <string>
#include <thread>
#include <atomic>
#include <windows.h>

using namespace Gdiplus;
using namespace std;

// মেইন ফাইল থেকে ফাংশন ও ভেরিয়েবল ইমপোর্ট করা হচ্ছে (Real Connections)
extern string g_currentPackage; 
extern bool g_isPremiumUser;
extern string SendFirestoreRequest(const string& method, const string& path, const string& payload);
extern string GetHardwareID();
extern string g_loggedInUserUid;

// পেজের স্টেট ভেরিয়েবল
wchar_t fl_pinCode[7] = L""; 
bool fl_isPinFocused = false;
bool fl_hoverConnectBtn = false;
int fl_connectionState = 0; // 0=Idle, 1=Connecting, 2=Success, 3=Error
wstring fl_statusMsg = L"";

// ───── Parent Control State ─────
static string g_parentUid = "";         // Link হওয়া parent এর UID
static bool g_isLinkedToParent = false; // Parent এর সাথে linked কিনা
static HWND g_familyHwnd = NULL;        // Invalidate এর জন্য

// Parent command থেকে apply হওয়া settings
// এগুলো main.cpp/tabs থেকে extern করে পড়া যাবে
bool g_parentLockAllTabs = false;       // সব tab lock
bool g_parentForceAdultBlock = false;   // Adult block force on
int  g_parentTimeLimitMinutes = 0;      // 0 = no limit
ULONGLONG g_parentTimeLimitStart = 0;

// ── Helper: JSON থেকে value বের করা ──
static string ExtractJsonStr(const string& json, const string& key) {
    string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return "";
    // stringValue format
    size_t sv = json.find("\"stringValue\": \"", pos);
    size_t bv = json.find("\"booleanValue\": ", pos);
    size_t iv = json.find("\"integerValue\": \"", pos);
    
    // পরের key এর আগে শেষ করে
    size_t nextKey = json.find("\",\n", pos + search.size());
    
    if (sv != string::npos && (bv == string::npos || sv < bv) && (iv == string::npos || sv < iv)) {
        sv += 16;
        size_t end = json.find("\"", sv);
        if (end != string::npos) return json.substr(sv, end - sv);
    } else if (bv != string::npos) {
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

// ── Parent Command Poll (Background Thread) ──
static atomic<bool> g_pollRunning(false);

static void PollParentCommands() {
    if (!g_isLinkedToParent || g_parentUid.empty()) return;
    
    string hwId = GetHardwareID();
    string path = "/v1/projects/rasfocus-c746d/databases/(default)/documents/parent_commands/" + hwId;
    string response = SendFirestoreRequest("GET", path, "");
    
    if (response.find("\"error\"") != string::npos || response.find("NOT_FOUND") != string::npos) {
        return; // কোনো command নেই
    }
    
    // Command fields পড়া
    string lockAll    = ExtractJsonStr(response, "lock_all_tabs");
    string forceAdult = ExtractJsonStr(response, "force_adult_block");
    string timeLimit  = ExtractJsonStr(response, "time_limit_minutes");
    
    // Apply commands
    g_parentLockAllTabs    = (lockAll == "true");
    g_parentForceAdultBlock = (forceAdult == "true");
    
    if (!timeLimit.empty()) {
        int mins = atoi(timeLimit.c_str());
        if (mins != g_parentTimeLimitMinutes) {
            g_parentTimeLimitMinutes = mins;
            g_parentTimeLimitStart = GetTickCount64();
        }
    }
    
    // UI refresh
    if (g_familyHwnd) InvalidateRect(g_familyHwnd, NULL, FALSE);
}

// ── রাউন্ডেড রেক্ট্যাঙ্গেল হেল্পার ──
void AddRoundRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

// ── Family Link Tab Draw ──
void DrawFamilyLinkTab(Graphics& g, float x, float y, float w, float h) {
    g_familyHwnd = NULL; // updated via HWND later if needed
    
    SolidBrush bgContent(Color(255, 245, 248, 250));
    g.FillRectangle(&bgContent, x, y, w, h);

    FontFamily ff(L"Segoe UI");
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    // ── ১. প্যাকেজ চেকিং ──
    bool hasAccess = (g_currentPackage == "PREMIUM" || g_currentPackage == "PARENTAL" || g_currentPackage == "TRIAL");
    if (!hasAccess) {
        SolidBrush textDark(Color(255, 50, 50, 50));
        Font fTitle(&ff, 24, FontStyleBold, UnitPixel);
        Font fSub(&ff, 14, FontStyleRegular, UnitPixel);
        g.DrawString(L"Premium Feature", -1, &fTitle, RectF(x, y + 150.0f, w, 40.0f), &fmtC, &textDark);
        g.DrawString(L"Upgrade to RasFocus+ Pro or Combo package to link parent device.", -1, &fSub, RectF(x, y + 190.0f, w, 30.0f), &fmtC, &textDark);
        return; 
    }

    SolidBrush textTeal(Color(255, 0, 150, 160));
    SolidBrush textGray(Color(255, 100, 100, 100));
    SolidBrush textDark(Color(255, 40, 40, 50));
    Font fHeader(&ff, 26, FontStyleBold, UnitPixel);
    Font fDesc(&ff, 13, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 13, FontStyleBold, UnitPixel);

    float centerY = y + 60.0f;

    // ── ২. Linked হয়ে গেলে Status Dashboard দেখাও ──
    if (g_isLinkedToParent) {
        g.DrawString(L"Family Link - Active", -1, &fHeader, RectF(x, centerY, w, 36.0f), &fmtC, &textTeal);

        // Status card
        float cardW = min(w - 60.0f, 480.0f);
        float cardX = x + (w - cardW) / 2.0f;
        float cardY = centerY + 50.0f;
        float cardH = 240.0f;
        GraphicsPath cardPath;
        AddRoundRect(cardPath, cardX, cardY, cardW, cardH, 10.0f);
        SolidBrush cardBg(Color(255, 255, 255, 255));
        g.FillPath(&cardBg, &cardPath);
        Pen cardBorder(Color(255, 220, 230, 240), 1.5f);
        g.DrawPath(&cardBorder, &cardPath);

        float rowY = cardY + 18.0f;
        float rowH = 36.0f;
        float lx = cardX + 20.0f;

        // Parent UID (short)
        wchar_t parentW[64] = {};
        string shortP = g_parentUid.size() > 12 ? g_parentUid.substr(0, 8) + "..." + g_parentUid.substr(g_parentUid.size()-4) : g_parentUid;
        MultiByteToWideChar(CP_UTF8, 0, shortP.c_str(), -1, parentW, 63);
        g.DrawString(L"Parent ID:", -1, &fDesc, RectF(lx, rowY, 120.0f, rowH), &fmtL, &textGray);
        g.DrawString(parentW, -1, &fBold, RectF(lx + 120.0f, rowY, cardW - 140.0f, rowH), &fmtL, &textDark);
        rowY += rowH;

        // Tab Lock status
        g.DrawString(L"Tab Lock:", -1, &fDesc, RectF(lx, rowY, 120.0f, rowH), &fmtL, &textGray);
        SolidBrush lockClr(g_parentLockAllTabs ? Color(255, 220, 50, 50) : Color(255, 0, 160, 90));
        g.DrawString(g_parentLockAllTabs ? L"LOCKED by Parent" : L"Unlocked", -1, &fBold, RectF(lx + 120.0f, rowY, cardW - 140.0f, rowH), &fmtL, &lockClr);
        rowY += rowH;

        // Adult Block status
        g.DrawString(L"Adult Block:", -1, &fDesc, RectF(lx, rowY, 120.0f, rowH), &fmtL, &textGray);
        SolidBrush abClr(g_parentForceAdultBlock ? Color(255, 220, 50, 50) : Color(255, 100, 100, 100));
        g.DrawString(g_parentForceAdultBlock ? L"FORCED ON by Parent" : L"Normal", -1, &fBold, RectF(lx + 120.0f, rowY, cardW - 140.0f, rowH), &fmtL, &abClr);
        rowY += rowH;

        // Time limit
        g.DrawString(L"Time Limit:", -1, &fDesc, RectF(lx, rowY, 120.0f, rowH), &fmtL, &textGray);
        wstring tlStr = L"No limit";
        if (g_parentTimeLimitMinutes > 0) {
            ULONGLONG elapsed = (GetTickCount64() - g_parentTimeLimitStart) / 60000ULL;
            ULONGLONG remaining = (elapsed < (ULONGLONG)g_parentTimeLimitMinutes) ? 
                                  (ULONGLONG)g_parentTimeLimitMinutes - elapsed : 0ULL;
            tlStr = to_wstring(remaining) + L" min remaining";
        }
        SolidBrush tlClr(g_parentTimeLimitMinutes > 0 ? Color(255, 230, 120, 0) : Color(255, 100, 100, 100));
        g.DrawString(tlStr.c_str(), -1, &fBold, RectF(lx + 120.0f, rowY, cardW - 140.0f, rowH), &fmtL, &tlClr);
        rowY += rowH + 10.0f;

        // Info note
        Font fSmall(&ff, 11, FontStyleRegular, UnitPixel);
        g.DrawString(L"Parent controls update automatically every 5 minutes.", -1, &fSmall, RectF(lx, rowY, cardW - 40.0f, 20.0f), &fmtL, &textGray);

        return;
    }

    // ── ৩. PIN Entry View (Not yet linked) ──
    g.DrawString(L"Family Link Setup", -1, &fHeader, RectF(x, centerY, w, 36.0f), &fmtC, &textTeal);
    g.DrawString(L"Enter the 6-digit connection code from your parent's phone.", -1, &fDesc, RectF(x, centerY + 42.0f, w, 26.0f), &fmtC, &textGray);

    // PIN Input Box
    float boxW = 220.0f, boxH = 50.0f;
    float boxX = x + (w - boxW) / 2.0f;
    float boxY = centerY + 90.0f;

    GraphicsPath boxPath;
    AddRoundRect(boxPath, boxX, boxY, boxW, boxH, 8.0f);
    SolidBrush boxBg(Color(255, 255, 255, 255));
    g.FillPath(&boxBg, &boxPath);
    Pen boxBorder(fl_isPinFocused ? Color(255, 0, 150, 160) : Color(255, 200, 200, 200), 2.0f);
    g.DrawPath(&boxBorder, &boxPath);

    Font fPin(&ff, 26, FontStyleBold, UnitPixel);
    SolidBrush pinText(Color(255, 50, 50, 50));
    wstring displayPin(fl_pinCode);
    if (displayPin.empty() && !fl_isPinFocused) {
        SolidBrush placeholderColor(Color(255, 180, 180, 180));
        g.DrawString(L"000000", -1, &fPin, RectF(boxX, boxY + 4.0f, boxW, boxH), &fmtC, &placeholderColor);
    } else {
        wstring spacedPin = L"";
        for (size_t i = 0; i < displayPin.length(); i++) { spacedPin += displayPin[i]; spacedPin += L" "; }
        g.DrawString(spacedPin.c_str(), -1, &fPin, RectF(boxX, boxY + 4.0f, boxW, boxH), &fmtC, &pinText);
    }

    // Connect Button
    float btnW = 180.0f, btnH = 45.0f;
    float btnX = x + (w - btnW) / 2.0f;
    float btnY = boxY + 70.0f;

    GraphicsPath btnPath;
    AddRoundRect(btnPath, btnX, btnY, btnW, btnH, 8.0f);
    Color btnNormal(255, 0, 150, 160), btnHover(255, 0, 120, 130), btnLoading(255, 80, 80, 100);
    SolidBrush btnBg(fl_connectionState == 1 ? btnLoading : (fl_hoverConnectBtn ? btnHover : btnNormal));
    g.FillPath(&btnBg, &btnPath);
    Font fBtn(&ff, 14, FontStyleBold, UnitPixel);
    SolidBrush btnWhite(Color(255, 255, 255, 255));
    wstring btnLabel = (fl_connectionState == 1) ? L"Connecting..." : L"Connect Device";
    g.DrawString(btnLabel.c_str(), -1, &fBtn, RectF(btnX, btnY, btnW, btnH), &fmtC, &btnWhite);

    // Status Message
    if (!fl_statusMsg.empty()) {
        Color statusColor = (fl_connectionState == 2) ? Color(255, 0, 180, 70) :
                            (fl_connectionState == 1) ? Color(255, 0, 150, 160) :
                                                        Color(255, 232, 17, 35);
        SolidBrush statusCol(statusColor);
        Font fStatus(&ff, 13, FontStyleBold, UnitPixel);
        g.DrawString(fl_statusMsg.c_str(), -1, &fStatus, RectF(x, btnY + 58.0f, w, 30.0f), &fmtC, &statusCol);
    }
}

// ── Mouse Move ──
void ProcessFamilyLinkMouseMove(float mx, float my, float cX, float cY) {
    if (g_isLinkedToParent) { fl_hoverConnectBtn = false; return; }
    float contentW = 1024.0f - 170.0f; 
    float btnW = 180.0f, btnH = 45.0f;
    float btnX = cX + (contentW - btnW) / 2.0f;
    float btnY = cY + 60.0f + 90.0f + 70.0f; 
    fl_hoverConnectBtn = (mx >= btnX && mx <= btnX + btnW && my >= btnY && my <= btnY + btnH);
}

// ── Mouse Click ──
void ProcessFamilyLinkMouseClick(float mx, float my, float cX, float cY, HWND hWnd) {
    g_familyHwnd = hWnd;
    if (g_isLinkedToParent) return;

    float contentW = 1024.0f - 170.0f; 
    float boxW = 220.0f, boxH = 50.0f;
    float boxX = cX + (contentW - boxW) / 2.0f;
    float boxY = cY + 60.0f + 90.0f;

    fl_isPinFocused = (mx >= boxX && mx <= boxX + boxW && my >= boxY && my <= boxY + boxH);

    if (fl_hoverConnectBtn) {
        wstring currentPin(fl_pinCode);
        if (currentPin.length() == 6) {
            
            fl_connectionState = 1;
            fl_statusMsg = L"Verifying PIN with Server...";
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);

            string pinStr(currentPin.begin(), currentPin.end());
            
            // ── ধাপ ১: Firebase থেকে PIN চেক করা ──
            string getPath = "/v1/projects/rasfocus-c746d/databases/(default)/documents/pairing_codes/" + pinStr;
            string response = SendFirestoreRequest("GET", getPath, "");

            if (response.find("\"error\"") == string::npos && response.find("NOT_FOUND") == string::npos) {
                
                string parentUid = ExtractJsonStr(response, "parent_uid");

                if (!parentUid.empty()) {
                    // ── ধাপ ২: PC তে parent_uid লিংক করা ──
                    string hwId = GetHardwareID();
                    string patchPath = "/v1/projects/rasfocus-c746d/databases/(default)/documents/devices/" + hwId + "?updateMask.fieldPaths=parent_uid";
                    string payload = "{\"fields\":{\"parent_uid\":{\"stringValue\":\"" + parentUid + "\"}}}";
                    SendFirestoreRequest("PATCH", patchPath, payload);

                    // ── ধাপ ৩: Parent command document তৈরি (যদি না থাকে) ──
                    string cmdPath = "/v1/projects/rasfocus-c746d/databases/(default)/documents/parent_commands/" + hwId;
                    string initPayload = "{\"fields\":{"
                        "\"lock_all_tabs\":{\"booleanValue\":false},"
                        "\"force_adult_block\":{\"booleanValue\":false},"
                        "\"time_limit_minutes\":{\"integerValue\":\"0\"},"
                        "\"child_device_id\":{\"stringValue\":\"" + hwId + "\"},"
                        "\"parent_uid\":{\"stringValue\":\"" + parentUid + "\"}"
                        "}}";
                    // PATCH করা হবে শুধু যদি document না থাকে
                    string checkCmd = SendFirestoreRequest("GET", cmdPath, "");
                    if (checkCmd.find("NOT_FOUND") != string::npos) {
                        SendFirestoreRequest("PATCH", cmdPath, initPayload);
                    }

                    g_parentUid = parentUid;
                    g_isLinkedToParent = true;
                    fl_connectionState = 2;
                    fl_statusMsg = L"Successfully Linked to Parent Device!";

                    // ── ধাপ ৪: Timer দিয়ে parent commands poll শুরু করা ──
                    SetTimer(hWnd, 2001, 5 * 60 * 1000, NULL); // 5 মিনিট পরপর poll

                    // প্রথমবার সাথেসাথে poll করা
                    thread([]() { PollParentCommands(); }).detach();
                    
                } else {
                    fl_connectionState = 3;
                    fl_statusMsg = L"Data Error. Parent UID not found.";
                }
            } else {
                fl_connectionState = 3;
                fl_statusMsg = L"Invalid or Expired PIN. Try again.";
            }
        } else {
            fl_connectionState = 3;
            fl_statusMsg = L"Please enter a valid 6-digit PIN.";
        }
    }
}

// ── Family Link Timer Poll (main.cpp এর WM_TIMER থেকে call করতে হবে) ──
void ProcessFamilyLinkTimer(UINT_PTR timerId) {
    if (timerId == 2001 && g_isLinkedToParent) {
        thread([]() { PollParentCommands(); }).detach();
    }
}

// ── Keyboard Input ──
void ProcessFamilyLinkChar(wchar_t c) {
    if (!fl_isPinFocused) return;
    int len = lstrlenW(fl_pinCode);
    if (c == L'\b') { 
        if (len > 0) fl_pinCode[len - 1] = L'\0';
    } else if (c >= L'0' && c <= L'9') { 
        if (len < 6) { fl_pinCode[len] = c; fl_pinCode[len + 1] = L'\0'; }
    }
}

void ProcessFamilyLinkKeyDown(WPARAM wp) {
    if (fl_isPinFocused && wp == VK_RETURN) {
        fl_isPinFocused = false;
    }
}
