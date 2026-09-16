// ============================================================
//  Statistics Tab  —  GDI+ rendering + Alarm management
//  Sub-tabs: Today | This Week | Monthly | Alarm
//
//  Alarm sub-tab features:
//   • Live digital clock (GetLocalTime, redraws every second via timer)
//   • Alarm list with toggle (enable/disable)
//   • + Add button  →  inline form to create a new alarm
//   • Click any alarm row → inline edit form opens
//   • Delete button (×) per row
//   • Background alarm check: rings Beep + Windows Toast notification
//   • Alarms saved/loaded to %APPDATA%\.rasfocus\alarms.txt
// ============================================================
#include <windows.h>
#include <gdiplus.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "winmm.lib")

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;

// ============================================================
//  Global: Animation progress
// ============================================================
float stat_animProgress = 1.0f;

// ============================================================
//  AlarmEntry
// ============================================================
struct AlarmEntry {
    int     hour    = 0;
    int     minute  = 0;
    bool    enabled = true;
    wstring label   = L"Alarm";
    bool    fired   = false;
};

// ============================================================
//  Alarm persistence  — %APPDATA%\.rasfocus\alarms.txt
// ============================================================
static string GetAlarmFilePath() {
    char appData[MAX_PATH] = {};
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData);
    string dir = string(appData) + "\\.rasfocus\\";
    CreateDirectoryA(dir.c_str(), NULL);
    return dir + "alarms.txt";
}

static void SaveAlarms(const vector<AlarmEntry>& alarms) {
    ofstream f(GetAlarmFilePath(), ios::out | ios::trunc);
    if (!f) return;
    for (auto& a : alarms) {
        int sz = WideCharToMultiByte(CP_UTF8, 0, a.label.c_str(), -1, NULL, 0, NULL, NULL);
        string lab(sz, '\0');
        WideCharToMultiByte(CP_UTF8, 0, a.label.c_str(), -1, lab.data(), sz, NULL, NULL);
        if (!lab.empty() && lab.back() == '\0') lab.pop_back();
        f << a.hour << " " << a.minute << " " << (a.enabled ? 1 : 0) << " " << lab << "\n";
    }
}

static void LoadAlarms(vector<AlarmEntry>& alarms) {
    alarms.clear();
    ifstream f(GetAlarmFilePath());
    if (!f) {
        alarms.push_back({ 6, 30, true,  L"Morning Prayer", false });
        alarms.push_back({ 8,  0, true,  L"Study Session",  false });
        alarms.push_back({ 13, 0, false, L"Lunch Break",    false });
        alarms.push_back({ 22, 0, true,  L"Sleep Reminder", false });
        SaveAlarms(alarms);
        return;
    }
    string line;
    while (getline(f, line)) {
        if (line.empty()) continue;
        istringstream ss(line);
        AlarmEntry a;
        int en = 1;
        string labUtf8;
        ss >> a.hour >> a.minute >> en;
        getline(ss, labUtf8);
        if (!labUtf8.empty() && labUtf8.front() == ' ') labUtf8.erase(labUtf8.begin());
        a.enabled = (en != 0);
        if (!labUtf8.empty()) {
            int wsz = MultiByteToWideChar(CP_UTF8, 0, labUtf8.c_str(), -1, NULL, 0);
            a.label.resize(wsz);
            MultiByteToWideChar(CP_UTF8, 0, labUtf8.c_str(), -1, a.label.data(), wsz);
            while (!a.label.empty() && a.label.back() == L'\0') a.label.pop_back();
        }
        alarms.push_back(a);
    }
}

// ============================================================
//  PC Notification (Windows Toast + Beep)
// ============================================================
static void FireAlarmNotification(const wstring& labelW) {
    // Beep in detached thread
    HANDLE hThread = CreateThread(NULL, 0, [](LPVOID) -> DWORD {
        for (int i = 0; i < 3; i++) { Beep(1000, 400); Sleep(200); }
        return 0;
    }, NULL, 0, NULL);
    if (hThread) CloseHandle(hThread);

    // Windows Toast via PowerShell
    int sz = WideCharToMultiByte(CP_UTF8, 0, labelW.c_str(), -1, NULL, 0, NULL, NULL);
    string labelA(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, labelW.c_str(), -1, labelA.data(), sz, NULL, NULL);
    if (!labelA.empty() && labelA.back() == '\0') labelA.pop_back();

    string ps =
        "try{"
        "$aumid='RasFocus+';"
        "$rp='HKCU:\\Software\\Classes\\AppUserModelId\\'+$aumid;"
        "if(-not(Test-Path $rp)){New-Item -Path $rp -Force|Out-Null};"
        "Set-ItemProperty -Path $rp -Name DisplayName -Value 'RasFocus+' -Force -ErrorAction SilentlyContinue;"
        "[Windows.UI.Notifications.ToastNotificationManager,Windows.UI.Notifications,ContentType=WindowsRuntime]|Out-Null;"
        "[Windows.Data.Xml.Dom.XmlDocument,Windows.Data.Xml.Dom,ContentType=WindowsRuntime]|Out-Null;"
        "$xml=[Windows.Data.Xml.Dom.XmlDocument]::new();"
        "$xml.LoadXml('<toast><visual><binding template=\"ToastGeneric\">"
            "<text>Alarm - RasFocus</text>"
            "<text>" + labelA + "</text>"
            "</binding></visual>"
            "<audio src=\"ms-winsoundevent:Notification.Reminder\"/>"
        "</toast>');"
        "$toast=[Windows.UI.Notifications.ToastNotification]::new($xml);"
        "$toast.Tag='RasFocusAlarm';"
        "[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($aumid).Show($toast);"
        "}catch{}";

    int wlen = MultiByteToWideChar(CP_UTF8, 0, ps.c_str(), -1, NULL, 0);
    vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_UTF8, 0, ps.c_str(), -1, wbuf.data(), wlen);
    const BYTE* raw = reinterpret_cast<const BYTE*>(wbuf.data());
    DWORD rawLen = (DWORD)((wlen - 1) * sizeof(wchar_t));
    DWORD b64Len = 0;
    CryptBinaryToStringA(raw, rawLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &b64Len);
    vector<char> b64(b64Len);
    CryptBinaryToStringA(raw, rawLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &b64Len);
    string encoded(b64.data(), b64Len - 1);

    string cmd = "powershell.exe -WindowStyle Hidden -NonInteractive -EncodedCommand " + encoded;
    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread)  CloseHandle(pi.hThread);
}

// ============================================================
//  Alarm state
// ============================================================
static vector<AlarmEntry> s_alarms;
static bool s_alarmsLoaded = false;

// Hover state
static int  s_alarmHoverIdx     = -1;
static int  s_alarmToggleHovIdx = -1;
static int  s_alarmDelHovIdx    = -1;

// Edit/Add form state
// editIdx == -1  → form closed
// editIdx == -2  → Add new alarm
// editIdx >= 0   → editing existing alarm at that index
static int   s_editIdx      = -1;
static int   s_formHour     = 7;
static int   s_formMinute   = 0;
static bool  s_formEnabled  = true;
static wchar_t s_formLabel[64] = L"Alarm";
static int   s_formFocus    = -1;   // 0=hour, 1=minute, 2=label
static bool  s_formSaveHov  = false;
static bool  s_formCancelHov= false;
static int   s_formArrowHov = -1;   // 0=hourUp,1=hourDn,2=minUp,3=minDn
static bool  s_addBtnHov    = false;

// ============================================================
//  Check alarms — call from WM_TIMER every second
// ============================================================
void CheckAlarmsFire() {
    if (!s_alarmsLoaded) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    int nowH = st.wHour, nowM = st.wMinute;
    for (auto& a : s_alarms) {
        if (!a.enabled) { a.fired = false; continue; }
        if (a.hour == nowH && a.minute == nowM) {
            if (!a.fired) {
                a.fired = true;
                FireAlarmNotification(a.label);
            }
        } else {
            a.fired = false;
        }
    }
}

// ============================================================
//  Helpers
// ============================================================
static void RoundRect(Graphics& g, Brush* brush, Pen* pen,
                      float x, float y, float w, float h, int radius)
{
    GraphicsPath path;
    float r = (float)radius, d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180, 90);
    path.AddArc(x + w - d, y,         d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d,   0, 90);
    path.AddArc(x,         y + h - d, d, d,  90, 90);
    path.CloseFigure();
    if (brush) g.FillPath(brush, &path);
    if (pen)   g.DrawPath(pen,   &path);
}

static void FillCircle(Graphics& g, Brush& brush, float cx, float cy, float r) {
    g.FillEllipse(&brush, cx - r, cy - r, r * 2.0f, r * 2.0f);
}

static RectF DrawToggle(Graphics& g, float tx, float ty, bool on, bool hov) {
    float tw = 44.0f, th = 24.0f;
    SolidBrush trackBr(on ? (hov ? Color(255, 80, 200, 50) : Color(255, 58, 235, 23))
                          : Color(255, 55, 65, 81));
    RoundRect(g, &trackBr, nullptr, tx, ty, tw, th, (int)(th / 2));
    float thumbR = th / 2.0f - 3.0f;
    float thumbX = on ? tx + tw - thumbR * 2.0f - 2.0f : tx + 2.0f;
    SolidBrush thumbBr(Color(255, 255, 255, 255));
    FillCircle(g, thumbBr, thumbX + thumbR, ty + th / 2.0f, thumbR);
    return RectF(tx, ty, tw, th);
}

static bool HitRect(float rx, float ry, float rw, float rh, float mx, float my) {
    return mx >= rx && mx <= rx + rw && my >= ry && my <= ry + rh;
}

// ============================================================
//  Layout
// ============================================================
struct AlarmLayout {
    float PAD;
    float clockCardX, clockCardY, clockCardW, clockCardH;
    float headerY;
    float listX, listY, listW;
    float rowH, rowGap;
    float addBtnX, addBtnY, addBtnW, addBtnH;
};

static AlarmLayout GetAlarmLayout(float cx, float cy, float cw) {
    AlarmLayout L;
    L.PAD        = 20.0f;
    L.clockCardX = cx + L.PAD + 16.0f;
    L.clockCardW = cw - L.PAD * 2 - 32.0f;
    L.clockCardH = 110.0f;
    L.clockCardY = cy + 16.0f;
    L.headerY    = L.clockCardY + L.clockCardH + 16.0f;
    L.listX      = L.clockCardX;
    L.listW      = L.clockCardW;
    L.listY      = L.headerY + 26.0f;
    L.rowH       = 64.0f;
    L.rowGap     =  8.0f;
    L.addBtnW    = 80.0f;
    L.addBtnH    = 26.0f;
    L.addBtnX    = L.listX + L.listW - L.addBtnW;
    L.addBtnY    = L.headerY - 2.0f;
    return L;
}

// ============================================================
//  Form hit rects
// ============================================================
struct FormHitRects {
    float hourBoxX, hourBoxY, hourBoxW, hourBoxH;
    float minBoxX,  minBoxY,  minBoxW,  minBoxH;
    float labelBoxX,labelBoxY,labelBoxW,labelBoxH;
    float hourUpX,  hourUpY,  hourUpW,  hourUpH;
    float hourDnX,  hourDnY,  hourDnW,  hourDnH;
    float minUpX,   minUpY,   minUpW,   minUpH;
    float minDnX,   minDnY,   minDnW,   minDnH;
    float togX,     togY,     togW,     togH;
    float savX,     savY,     savW,     savH;
    float canX,     canY,     canW,     canH;
};

static FormHitRects GetFormHitRects(const AlarmLayout& L, float formY) {
    FormHitRects R = {};
    float fieldY  = formY + 38.0f;
    float fieldH  = 32.0f;
    float hourX   = L.listX + 16.0f;
    float hourW   = 60.0f;
    float arrowW  = 20.0f;
    float minX    = hourX + hourW + arrowW + 8.0f;
    float minW    = 60.0f;
    float labelX  = minX + minW + arrowW + 8.0f;
    float labelW  = L.listW - (labelX - L.listX) - 16.0f;
    float arX     = hourX + hourW + 2.0f;
    float marX    = minX  + minW  + 2.0f;
    float fH      = 160.0f;
    float btnW    = 72.0f, btnH = 28.0f;

    R.hourBoxX  = hourX;   R.hourBoxY  = fieldY;               R.hourBoxW  = hourW;   R.hourBoxH  = fieldH;
    R.minBoxX   = minX;    R.minBoxY   = fieldY;               R.minBoxW   = minW;    R.minBoxH   = fieldH;
    R.labelBoxX = labelX;  R.labelBoxY = fieldY;               R.labelBoxW = labelW;  R.labelBoxH = fieldH;
    R.hourUpX   = arX;     R.hourUpY   = fieldY;               R.hourUpW   = arrowW;  R.hourUpH   = fieldH / 2.0f;
    R.hourDnX   = arX;     R.hourDnY   = fieldY + fieldH/2.0f; R.hourDnW   = arrowW;  R.hourDnH   = fieldH / 2.0f;
    R.minUpX    = marX;    R.minUpY    = fieldY;               R.minUpW    = arrowW;  R.minUpH    = fieldH / 2.0f;
    R.minDnX    = marX;    R.minDnY    = fieldY + fieldH/2.0f; R.minDnW    = arrowW;  R.minDnH    = fieldH / 2.0f;
    R.togX      = L.listX + 80.0f; R.togY = formY + 104.0f;   R.togW      = 44.0f;   R.togH      = 24.0f;
    R.savX      = L.listX + L.listW - btnW * 2 - 24.0f;
    R.savY      = formY + fH - btnH - 12.0f;
    R.savW      = btnW;    R.savH = btnH;
    R.canX      = L.listX + L.listW - btnW - 8.0f;
    R.canY      = R.savY;  R.canW = btnW; R.canH = btnH;
    return R;
}

static float AlarmFormY(const AlarmLayout& L) {
    if (s_editIdx == -2)
        return L.listY + (float)s_alarms.size() * (L.rowH + L.rowGap) + 8.0f;
    if (s_editIdx >= 0)
        return L.listY + s_editIdx * (L.rowH + L.rowGap) + L.rowH + 2.0f;
    return 0.0f;
}

// ============================================================
//  Draw the Add/Edit form
// ============================================================
static void DrawAlarmForm(Graphics& g, const AlarmLayout& L, float formY,
    const Color& colBorder, const Color& colGreen, const Color& colWhite, const Color& colGray)
{
    Font fBold   (L"Segoe UI",    11.0f, FontStyleBold,    UnitPixel);
    Font fSm     (L"Segoe UI",     9.5f, FontStyleRegular, UnitPixel);
    Font fAlarmT (L"Courier New", 15.0f, FontStyleBold,    UnitPixel);

    float fieldY  = formY + 38.0f;
    float fieldH  = 32.0f;
    float hourX   = L.listX + 16.0f;
    float hourW   = 60.0f;
    float arrowW  = 20.0f;
    float minX    = hourX + hourW + arrowW + 8.0f;
    float minW    = 60.0f;
    float labelX  = minX + minW + arrowW + 8.0f;
    float labelW  = L.listW - (labelX - L.listX) - 16.0f;
    float fH      = 160.0f;
    float btnW    = 72.0f, btnH = 28.0f;

    SolidBrush bWhite(colWhite);
    SolidBrush bGray (colGray);
    SolidBrush bGreen(colGreen);
    SolidBrush bFld  (Color(255, 38, 38, 42));
    Pen pBrd(colBorder, 1.0f);

    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    // Form background
    SolidBrush bForm(Color(255, 52, 52, 57));
    RoundRect(g, &bForm, &pBrd, L.listX, formY, L.listW, fH, 10);

    // Title
    bool isAdd = (s_editIdx == -2);
    g.DrawString(isAdd ? L"Add Alarm" : L"Edit Alarm", -1, &fBold,
        RectF(L.listX + 16.0f, formY + 12.0f, L.listW, 18.0f), &fmtL, &bWhite);

    // Hour box
    bool hFoc = (s_formFocus == 0);
    Pen pHour(hFoc ? colGreen : colBorder, 1.5f);
    RoundRect(g, &bFld, &pHour, hourX, fieldY, hourW, fieldH, 6);
    wchar_t hBuf[8]; swprintf_s(hBuf, L"%02d", s_formHour);
    SolidBrush bHourTxt(hFoc ? colGreen : colWhite);
    g.DrawString(hBuf, -1, &fAlarmT, RectF(hourX, fieldY, hourW, fieldH), &fmtC, &bHourTxt);

    // Hour arrows
    float arX = hourX + hourW + 2.0f;
    SolidBrush bArHU(s_formArrowHov == 0 ? colGreen : colGray);
    SolidBrush bArHD(s_formArrowHov == 1 ? colGreen : colGray);
    g.DrawString(L"▲", -1, &fSm, RectF(arX, fieldY,               arrowW, fieldH/2.0f), &fmtC, &bArHU);
    g.DrawString(L"▼", -1, &fSm, RectF(arX, fieldY + fieldH/2.0f, arrowW, fieldH/2.0f), &fmtC, &bArHD);

    // Colon
    g.DrawString(L":", -1, &fBold,
        RectF(hourX + hourW + arrowW + 4.0f, fieldY, 8.0f, fieldH), &fmtC, &bWhite);

    // Minute box
    bool mFoc = (s_formFocus == 1);
    Pen pMin(mFoc ? colGreen : colBorder, 1.5f);
    RoundRect(g, &bFld, &pMin, minX, fieldY, minW, fieldH, 6);
    wchar_t mBuf[8]; swprintf_s(mBuf, L"%02d", s_formMinute);
    SolidBrush bMinTxt(mFoc ? colGreen : colWhite);
    g.DrawString(mBuf, -1, &fAlarmT, RectF(minX, fieldY, minW, fieldH), &fmtC, &bMinTxt);

    // Minute arrows
    float marX = minX + minW + 2.0f;
    SolidBrush bArMU(s_formArrowHov == 2 ? colGreen : colGray);
    SolidBrush bArMD(s_formArrowHov == 3 ? colGreen : colGray);
    g.DrawString(L"▲", -1, &fSm, RectF(marX, fieldY,               arrowW, fieldH/2.0f), &fmtC, &bArMU);
    g.DrawString(L"▼", -1, &fSm, RectF(marX, fieldY + fieldH/2.0f, arrowW, fieldH/2.0f), &fmtC, &bArMD);

    // Label box
    bool lFoc = (s_formFocus == 2);
    Pen pLbl(lFoc ? colGreen : colBorder, 1.5f);
    RoundRect(g, &bFld, &pLbl, labelX, fieldY, labelW, fieldH, 6);
    wstring labelStr(s_formLabel);
    if (lFoc) labelStr += L"|";
    Font fLbl(L"Segoe UI", 11.0f, FontStyleRegular, UnitPixel);
    SolidBrush bLblTxt(lFoc ? colGreen : colWhite);
    StringFormat fmtLbl; fmtLbl.SetAlignment(StringAlignmentNear); fmtLbl.SetLineAlignment(StringAlignmentCenter);
    fmtLbl.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(labelStr.c_str(), -1, &fLbl,
        RectF(labelX + 8.0f, fieldY, labelW - 16.0f, fieldH), &fmtLbl, &bLblTxt);

    // Field labels
    float lblY = fieldY + fieldH + 4.0f;
    g.DrawString(L"Hour",   -1, &fSm, RectF(hourX,  lblY, hourW,  14.0f), &fmtC, &bGray);
    g.DrawString(L"Minute", -1, &fSm, RectF(minX,   lblY, minW,   14.0f), &fmtC, &bGray);
    g.DrawString(L"Label",  -1, &fSm, RectF(labelX, lblY, labelW, 14.0f), &fmtL, &bGray);

    // Enabled toggle
    float togY = formY + 104.0f;
    g.DrawString(L"Enabled", -1, &fSm,
        RectF(L.listX + 16.0f, togY + 4.0f, 60.0f, 18.0f), &fmtL, &bGray);
    DrawToggle(g, L.listX + 80.0f, togY, s_formEnabled, false);

    // Save / Cancel buttons
    float savX = L.listX + L.listW - btnW * 2 - 24.0f;
    float canX = L.listX + L.listW - btnW - 8.0f;
    float btnY = formY + fH - btnH - 12.0f;

    SolidBrush bSav(s_formSaveHov   ? Color(255, 80, 210, 50) : colGreen);
    SolidBrush bCan(s_formCancelHov ? Color(255, 90, 90, 100) : Color(255, 60, 60, 65));
    RoundRect(g, &bSav, nullptr, savX, btnY, btnW, btnH, 6);
    RoundRect(g, &bCan, &pBrd,   canX, btnY, btnW, btnH, 6);

    SolidBrush bSavTxt(Color(255, 15, 15, 20));
    g.DrawString(L"Save",   -1, &fBold, RectF(savX, btnY, btnW, btnH), &fmtC, &bSavTxt);
    SolidBrush bCanTxt(colWhite);
    g.DrawString(L"Cancel", -1, &fBold, RectF(canX, btnY, btnW, btnH), &fmtC, &bCanTxt);
}

// ============================================================
//  DrawAlarmSubTab
// ============================================================
static void DrawAlarmSubTab(Graphics& g, float cx, float cy, float cw, float ch)
{
    if (!s_alarmsLoaded) { LoadAlarms(s_alarms); s_alarmsLoaded = true; }

    AlarmLayout L = GetAlarmLayout(cx, cy, cw);

    // Colors
    Color colBg    (255, 45,  45,  48);
    Color colCard  (255, 60,  60,  65);
    Color colBorder(255, 80,  80,  88);
    Color colGreen (255, 58, 235, 23);
    Color colGreenDim(255, 35, 140, 14);
    Color colWhite (255, 240, 240, 240);
    Color colGray  (255, 160, 160, 170);
    Color colOff   (255, 100, 100, 110);
    Color colRed   (255, 220,  50,  50);

    SolidBrush bBg (colBg);
    SolidBrush bCard(colCard);
    SolidBrush bGreen(colGreen);
    SolidBrush bGreenDim(colGreenDim);
    SolidBrush bWhite(colWhite);
    SolidBrush bGray(colGray);
    SolidBrush bOff(colOff);
    Pen pBorder(colBorder, 1.0f);

    // Dark bg panel
    RoundRect(g, &bBg, nullptr, cx + L.PAD, cy, cw - L.PAD * 2, ch - L.PAD, 12);

    // Fonts
    Font fDigital(L"Courier New", 36.0f, FontStyleBold,    UnitPixel);
    Font fSec    (L"Courier New", 18.0f, FontStyleBold,    UnitPixel);
    Font fAlarmT (L"Courier New", 15.0f, FontStyleBold,    UnitPixel);
    Font fDate   (L"Segoe UI",    12.0f, FontStyleRegular, UnitPixel);
    Font fH2     (L"Segoe UI",    13.0f, FontStyleBold,    UnitPixel);
    Font fBold   (L"Segoe UI",    11.0f, FontStyleBold,    UnitPixel);
    Font fSm     (L"Segoe UI",     9.5f, FontStyleRegular, UnitPixel);
    Font fLabel  (L"Segoe UI",    10.0f, FontStyleRegular, UnitPixel);
    Font fTitle  (L"Segoe UI",    10.0f, FontStyleBold,    UnitPixel);

    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);    fmtR.SetLineAlignment(StringAlignmentCenter);

    // Clock card
    RoundRect(g, &bCard, &pBorder,
              L.clockCardX, L.clockCardY, L.clockCardW, L.clockCardH, 10);

    SYSTEMTIME st; GetLocalTime(&st);

    // Glow
    float gcx = L.clockCardX + L.clockCardW / 2.0f;
    float gcy = L.clockCardY + L.clockCardH / 2.0f;
    for (int i = 3; i >= 0; i--) {
        SolidBrush gb(Color((BYTE)(18 + i * 10), 58, 235, 23));
        FillCircle(g, gb, gcx, gcy, 52.0f + i * 10.0f);
    }

    // HH:MM
    wchar_t timeBuf[8]; swprintf_s(timeBuf, L"%02d:%02d", st.wHour, st.wMinute);
    g.DrawString(timeBuf, -1, &fDigital,
        RectF(L.clockCardX, L.clockCardY + 10.0f, L.clockCardW, 55.0f), &fmtC, &bGreen);

    // :SS
    wchar_t secBuf[6]; swprintf_s(secBuf, L":%02d", st.wSecond);
    g.DrawString(secBuf, -1, &fSec,
        RectF(L.clockCardX + L.clockCardW * 0.5f + 72.0f, L.clockCardY + 28.0f, 60.0f, 30.0f),
        &fmtL, &bGreenDim);

    // Date
    const wchar_t* dayNames[] = { L"SUN",L"MON",L"TUE",L"WED",L"THU",L"FRI",L"SAT" };
    wchar_t dateBuf[40];
    swprintf_s(dateBuf, L"%s  %02d/%02d/%04d", dayNames[st.wDayOfWeek],
               st.wDay, st.wMonth, st.wYear);
    g.DrawString(dateBuf, -1, &fDate,
        RectF(L.clockCardX, L.clockCardY + 70.0f, L.clockCardW, 24.0f), &fmtC, &bGray);

    g.DrawString(L"ALARM CLOCK", -1, &fTitle,
        RectF(L.clockCardX, L.clockCardY + 94.0f, L.clockCardW, 14.0f), &fmtC, &bGreen);

    // Header row
    g.DrawString(L"Alarms", -1, &fH2,
        RectF(L.listX, L.headerY, 80.0f, 22.0f), &fmtL, &bWhite);

    // Next alarm hint
    wstring nextStr = L"No upcoming alarms";
    int nowMin = st.wHour * 60 + st.wMinute;
    int bestDiff = 99999;
    for (auto& a : s_alarms) {
        if (!a.enabled) continue;
        int am = a.hour * 60 + a.minute;
        int diff = am - nowMin; if (diff <= 0) diff += 24 * 60;
        if (diff < bestDiff) {
            bestDiff = diff;
            wchar_t buf[80];
            int h = bestDiff / 60, m = bestDiff % 60;
            if (h > 0) swprintf_s(buf, L"Next: %s in %dh %02dm", a.label.c_str(), h, m);
            else       swprintf_s(buf, L"Next: %s in %dm",        a.label.c_str(), m);
            nextStr = buf;
        }
    }
    g.DrawString(nextStr.c_str(), -1, &fSm,
        RectF(L.listX + 82.0f, L.headerY + 4.0f,
              L.listW - 82.0f - L.addBtnW - 8.0f, 16.0f), &fmtL, &bGreenDim);

    // + Add button (hidden when form is open)
    bool formOpen = (s_editIdx == -2 || s_editIdx >= 0);
    if (!formOpen) {
        SolidBrush bAddBg(s_addBtnHov ? Color(255, 80, 210, 50) : colGreen);
        RoundRect(g, &bAddBg, nullptr, L.addBtnX, L.addBtnY, L.addBtnW, L.addBtnH, 6);
        SolidBrush bAddTxt(Color(255, 15, 15, 20));
        g.DrawString(L"+ Add", -1, &fBold,
            RectF(L.addBtnX, L.addBtnY, L.addBtnW, L.addBtnH), &fmtC, &bAddTxt);
    }

    // Alarm rows
    for (int i = 0; i < (int)s_alarms.size(); i++) {
        auto& a = s_alarms[i];
        float ry = L.listY + i * (L.rowH + L.rowGap);
        bool  hov = (s_alarmHoverIdx == i) && (s_editIdx != i);

        SolidBrush rowBr(hov ? Color(255, 72, 72, 78) : colCard);
        RoundRect(g, &rowBr, &pBorder, L.listX, ry, L.listW, L.rowH, 8);

        // Green accent bar
        if (a.enabled) {
            SolidBrush accentBr(colGreen);
            g.FillRectangle(&accentBr, RectF(L.listX, ry + 8.0f, 3.0f, L.rowH - 16.0f));
        }

        // Time
        wchar_t atBuf[8]; swprintf_s(atBuf, L"%02d:%02d", a.hour, a.minute);
        SolidBrush timeBr(a.enabled ? colGreen : colOff);
        g.DrawString(atBuf, -1, &fAlarmT,
            RectF(L.listX + 16.0f, ry + 8.0f, 90.0f, 28.0f), &fmtL, &timeBr);

        // AM/PM
        SolidBrush ampmBr(a.enabled ? colGreenDim : colOff);
        g.DrawString(a.hour < 12 ? L"AM" : L"PM", -1, &fLabel,
            RectF(L.listX + 16.0f, ry + 36.0f, 40.0f, 16.0f), &fmtL, &ampmBr);

        // Divider
        Pen pDiv(Color(255, 80, 80, 88), 1.0f);
        g.DrawLine(&pDiv, L.listX + 110.0f, ry + 10.0f, L.listX + 110.0f, ry + L.rowH - 10.0f);

        // Label
        SolidBrush lbr(a.enabled ? colWhite : colGray);
        StringFormat fmtTrim; fmtTrim.SetAlignment(StringAlignmentNear);
        fmtTrim.SetTrimming(StringTrimmingEllipsisCharacter);
        g.DrawString(a.label.c_str(), -1, &fBold,
            RectF(L.listX + 120.0f, ry + 10.0f, L.listW - 240.0f, 20.0f), &fmtTrim, &lbr);

        if (hov) {
            SolidBrush bHint(Color(255, 100, 100, 120));
            g.DrawString(L"click to edit", -1, &fSm,
                RectF(L.listX + 120.0f, ry + 34.0f, L.listW - 240.0f, 16.0f), &fmtL, &bHint);
        } else {
            g.DrawString(L"Every day", -1, &fSm,
                RectF(L.listX + 120.0f, ry + 34.0f, L.listW - 240.0f, 16.0f), &fmtL, &bGray);
        }

        // Toggle
        float togX = L.listX + L.listW - 60.0f;
        float togY = ry + (L.rowH - 24.0f) / 2.0f;
        DrawToggle(g, togX, togY, a.enabled, (s_alarmToggleHovIdx == i));

        // × Delete button
        float delX = togX - 28.0f;
        float delY = ry + (L.rowH - 20.0f) / 2.0f;
        SolidBrush bDel(s_alarmDelHovIdx == i ? colRed : colOff);
        g.DrawString(L"×", -1, &fBold, RectF(delX, delY, 20.0f, 20.0f), &fmtC, &bDel);
    }

    // Inline form
    if (formOpen) {
        DrawAlarmForm(g, L, AlarmFormY(L), colBorder, colGreen, colWhite, colGray);
    }

    // Bottom tip
    float tipY = L.listY + s_alarms.size() * (L.rowH + L.rowGap)
               + (formOpen ? 170.0f : 0.0f) + 6.0f;
    SolidBrush bTip(Color(255, 80, 80, 96));
    g.DrawString(L"Toggle to enable/disable  •  Click row to edit  •  × to delete",
        -1, &fSm, RectF(L.listX, tipY, L.listW, 16.0f), &fmtC, &bTip);

    (void)ch;
}

// ============================================================
//  Mouse Hover
// ============================================================
static void AlarmSubTabHover(float mx, float my, float cx, float cy, float cw)
{
    if (!s_alarmsLoaded) return;
    AlarmLayout L = GetAlarmLayout(cx, cy, cw);

    s_alarmHoverIdx     = -1;
    s_alarmToggleHovIdx = -1;
    s_alarmDelHovIdx    = -1;
    s_addBtnHov         = false;
    s_formSaveHov       = false;
    s_formCancelHov     = false;
    s_formArrowHov      = -1;

    bool formOpen = (s_editIdx >= 0 || s_editIdx == -2);

    if (!formOpen)
        s_addBtnHov = HitRect(L.addBtnX, L.addBtnY, L.addBtnW, L.addBtnH, mx, my);

    for (int i = 0; i < (int)s_alarms.size(); i++) {
        float ry = L.listY + i * (L.rowH + L.rowGap);
        if (HitRect(L.listX, ry, L.listW, L.rowH, mx, my)) {
            s_alarmHoverIdx = i;
            float togX = L.listX + L.listW - 60.0f;
            float togY = ry + (L.rowH - 24.0f) / 2.0f;
            if (HitRect(togX, togY, 44.0f, 24.0f, mx, my)) s_alarmToggleHovIdx = i;
            float delX = togX - 28.0f;
            float delY = ry + (L.rowH - 20.0f) / 2.0f;
            if (HitRect(delX, delY, 20.0f, 20.0f, mx, my)) s_alarmDelHovIdx = i;
        }
    }

    if (formOpen) {
        float fy = AlarmFormY(L);
        FormHitRects FR = GetFormHitRects(L, fy);
        s_formSaveHov   = HitRect(FR.savX, FR.savY, FR.savW, FR.savH, mx, my);
        s_formCancelHov = HitRect(FR.canX, FR.canY, FR.canW, FR.canH, mx, my);
        if      (HitRect(FR.hourUpX, FR.hourUpY, FR.hourUpW, FR.hourUpH, mx, my)) s_formArrowHov = 0;
        else if (HitRect(FR.hourDnX, FR.hourDnY, FR.hourDnW, FR.hourDnH, mx, my)) s_formArrowHov = 1;
        else if (HitRect(FR.minUpX,  FR.minUpY,  FR.minUpW,  FR.minUpH,  mx, my)) s_formArrowHov = 2;
        else if (HitRect(FR.minDnX,  FR.minDnY,  FR.minDnW,  FR.minDnH,  mx, my)) s_formArrowHov = 3;
    }
}

// ============================================================
//  Mouse Click
// ============================================================
static void AlarmSubTabClick(float mx, float my, float cx, float cy, float cw)
{
    if (!s_alarmsLoaded) return;
    AlarmLayout L = GetAlarmLayout(cx, cy, cw);

    bool formOpen = (s_editIdx >= 0 || s_editIdx == -2);
    if (formOpen) {
        float fy = AlarmFormY(L);
        FormHitRects FR = GetFormHitRects(L, fy);

        if      (HitRect(FR.hourUpX, FR.hourUpY, FR.hourUpW, FR.hourUpH, mx, my)) { s_formHour   = (s_formHour   + 1)  % 24; return; }
        else if (HitRect(FR.hourDnX, FR.hourDnY, FR.hourDnW, FR.hourDnH, mx, my)) { s_formHour   = (s_formHour   + 23) % 24; return; }
        else if (HitRect(FR.minUpX,  FR.minUpY,  FR.minUpW,  FR.minUpH,  mx, my)) { s_formMinute = (s_formMinute + 1)  % 60; return; }
        else if (HitRect(FR.minDnX,  FR.minDnY,  FR.minDnW,  FR.minDnH,  mx, my)) { s_formMinute = (s_formMinute + 59) % 60; return; }

        if (HitRect(FR.hourBoxX, FR.hourBoxY, FR.hourBoxW, FR.hourBoxH, mx, my)) { s_formFocus = 0; return; }
        if (HitRect(FR.minBoxX,  FR.minBoxY,  FR.minBoxW,  FR.minBoxH,  mx, my)) { s_formFocus = 1; return; }
        if (HitRect(FR.labelBoxX,FR.labelBoxY,FR.labelBoxW,FR.labelBoxH,mx, my)) { s_formFocus = 2; return; }
        if (HitRect(FR.togX,     FR.togY,     FR.togW,     FR.togH,     mx, my)) { s_formEnabled = !s_formEnabled; return; }

        if (HitRect(FR.savX, FR.savY, FR.savW, FR.savH, mx, my)) {
            if (s_editIdx == -2) {
                AlarmEntry ne;
                ne.hour    = s_formHour;
                ne.minute  = s_formMinute;
                ne.enabled = s_formEnabled;
                ne.label   = wstring(s_formLabel);
                s_alarms.push_back(ne);
            } else {
                s_alarms[s_editIdx].hour    = s_formHour;
                s_alarms[s_editIdx].minute  = s_formMinute;
                s_alarms[s_editIdx].enabled = s_formEnabled;
                s_alarms[s_editIdx].label   = wstring(s_formLabel);
                s_alarms[s_editIdx].fired   = false;
            }
            SaveAlarms(s_alarms);
            s_editIdx   = -1;
            s_formFocus = -1;
            return;
        }
        if (HitRect(FR.canX, FR.canY, FR.canW, FR.canH, mx, my)) {
            s_editIdx   = -1;
            s_formFocus = -1;
            return;
        }
        return; // swallow
    }

    // + Add button
    if (HitRect(L.addBtnX, L.addBtnY, L.addBtnW, L.addBtnH, mx, my)) {
        s_editIdx     = -2;
        s_formHour    = 7;
        s_formMinute  = 0;
        s_formEnabled = true;
        s_formFocus   = -1;
        wcscpy_s(s_formLabel, L"Alarm");
        return;
    }

    // Alarm rows
    for (int i = 0; i < (int)s_alarms.size(); i++) {
        float ry = L.listY + i * (L.rowH + L.rowGap);
        if (!HitRect(L.listX, ry, L.listW, L.rowH, mx, my)) continue;

        // Toggle
        float togX = L.listX + L.listW - 60.0f;
        float togY = ry + (L.rowH - 24.0f) / 2.0f;
        if (HitRect(togX, togY, 44.0f, 24.0f, mx, my)) {
            s_alarms[i].enabled = !s_alarms[i].enabled;
            s_alarms[i].fired   = false;
            SaveAlarms(s_alarms);
            return;
        }

        // × Delete
        float delX = togX - 28.0f;
        float delY = ry + (L.rowH - 20.0f) / 2.0f;
        if (HitRect(delX, delY, 20.0f, 20.0f, mx, my)) {
            s_alarms.erase(s_alarms.begin() + i);
            SaveAlarms(s_alarms);
            return;
        }

        // Click row → open edit form
        s_editIdx     = i;
        s_formHour    = s_alarms[i].hour;
        s_formMinute  = s_alarms[i].minute;
        s_formEnabled = s_alarms[i].enabled;
        s_formFocus   = -1;
        wcsncpy_s(s_formLabel, s_alarms[i].label.c_str(), 63);
        return;
    }
}

// ============================================================
//  Keyboard input for label field
// ============================================================
void ProcessAlarmKeyChar(wchar_t ch) {
    if (s_editIdx == -1 || s_formFocus != 2) return;
    int len = (int)wcslen(s_formLabel);
    if (ch == L'\b') {
        if (len > 0) s_formLabel[len - 1] = L'\0';
    } else if (ch >= 32 && len < 62) {
        s_formLabel[len]     = ch;
        s_formLabel[len + 1] = L'\0';
    }
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
}

// ============================================================
//  Weekly Analytics
// ============================================================
void DrawWeeklyAnalytics(Graphics& g, float cx, float cY, float cw,
    float /*availH*/, float /*r1H*/, float r2H, float /*r3H*/,
    Font& fH1, Font& fH2, Font& fBody, Font& fBold, Font& fSm, Font& fMed,
    SolidBrush& bCard, SolidBrush& bTextMain, SolidBrush& bTextMuted, Pen& pBrd)
{
    float PAD   = 24.0f;
    float r2Y   = cY + PAD;
    float mcGap = 16.0f;
    float thirdW = (cw - PAD * 2 - mcGap * 2) / 3.0f;

    StringFormat fmtTrim; fmtTrim.SetAlignment(StringAlignmentNear); fmtTrim.SetTrimming(StringTrimmingEllipsisCharacter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);

    RoundRect(g, &bCard, &pBrd, cx + PAD, r2Y, thirdW, r2H, 8);
    g.DrawString(L"Top Time-Wasters", -1, &fH2, RectF(cx+PAD+20,r2Y+16,thirdW,22), nullptr, &bTextMain);

    struct Waster { wstring name; wstring timeStr; float pct; Color c; };
    vector<Waster> wasters = {
        { L"YouTube",   L"8h 45m", 85, Color(255,239,68,68)  },
        { L"Facebook",  L"5h 20m", 55, Color(255,59,130,246) },
        { L"Instagram", L"3h 15m", 35, Color(255,217,70,239) },
        { L"Netflix",   L"2h 10m", 25, Color(255,100,100,100)}
    };
    float wRowH = (r2H-50)/4; float wY0 = r2Y+50;
    for (int i=0;i<(int)wasters.size();i++) {
        float wy=wY0+i*wRowH;
        g.DrawString(wasters[i].name.c_str(),-1,&fBold,RectF(cx+PAD+20,wy+8,thirdW-100,18),&fmtTrim,&bTextMain);
        g.DrawString(wasters[i].timeStr.c_str(),-1,&fSm,RectF(cx+PAD+thirdW-80,wy+8,60,18),&fmtR,&bTextMuted);
        SolidBrush bg(Color(255,241,245,249)); RoundRect(g,&bg,nullptr,cx+PAD+20,wy+32,thirdW-40,6,3);
        SolidBrush fl(wasters[i].c);
        float fw=(thirdW-40)*(wasters[i].pct/100)*stat_animProgress;
        if(fw>6) RoundRect(g,&fl,nullptr,cx+PAD+20,wy+32,fw,6,3);
    }
    float midX=cx+PAD+thirdW+mcGap;
    RoundRect(g,&bCard,&pBrd,midX,r2Y,thirdW,r2H,8);
    g.DrawString(L"Focus Rhythm",-1,&fH2,RectF(midX+20,r2Y+16,thirdW,22),nullptr,&bTextMain);
    vector<wstring> days={L"Mon",L"Tue",L"Wed",L"Thu",L"Fri",L"Sat",L"Sun"};
    vector<float> fh={4.5f,6.2f,5,7.5f,6.8f,2,3.5f}; float maxH=8;
    float fRowH=(r2H-50)/7; float fY0=r2Y+45;
    for(int i=0;i<7;i++){
        float fy=fY0+i*fRowH;
        g.DrawString(days[i].c_str(),-1,&fSm,RectF(midX+20,fy+4,40,18),nullptr,&bTextMuted);
        float bMaxW=thirdW-90; float bW=(fh[i]/maxH)*bMaxW*stat_animProgress;
        SolidBrush barBg(Color(255,241,245,249)); RoundRect(g,&barBg,nullptr,midX+60,fy+8,bMaxW,10,5);
        if(bW>10){RectF gr(midX+60,fy+8,bW,10);LinearGradientBrush gb(gr,Color(255,16,185,129),Color(255,5,150,105),LinearGradientModeHorizontal);RoundRect(g,&gb,nullptr,midX+60,fy+8,bW,10,5);}
    }
    float rightX=midX+thirdW+mcGap;
    RoundRect(g,&bCard,&pBrd,rightX,r2Y,thirdW,r2H,8);
    g.DrawString(L"Recent Achievements",-1,&fH2,RectF(rightX+20,r2Y+16,thirdW,22),nullptr,&bTextMain);
    struct Badge{wstring ic,ti,de;Color bg,tx;};
    vector<Badge> badges={
        {L"[T]",L"Iron Will",L"7-Day Streak",Color(255,254,243,199),Color(255,180,83,9)},
        {L"[S]",L"Halal Warrior",L"Blocked 100+",Color(255,224,242,254),Color(255,3,105,161)},
        {L"[F]",L"Deep Work Guru",L"4h Focus",Color(255,254,226,226),Color(255,185,28,28)}
    };
    float bRowH=(r2H-50)/3; float bY0=r2Y+45;
    for(int i=0;i<(int)badges.size();i++){
        float by=bY0+i*bRowH;
        SolidBrush bbg(badges[i].bg); FillCircle(g,bbg,rightX+40,by+bRowH/2,20);
        SolidBrush bic(badges[i].tx);
        g.DrawString(badges[i].ic.c_str(),-1,&fMed,RectF(rightX+20,by+bRowH/2-14,40,28),&fmtC,&bic);
        g.DrawString(badges[i].ti.c_str(),-1,&fBold,RectF(rightX+70,by+bRowH*0.2f,thirdW-90,20),&fmtTrim,&bTextMain);
        g.DrawString(badges[i].de.c_str(),-1,&fSm,RectF(rightX+70,by+bRowH*0.6f,thirdW-90,16),&fmtTrim,&bTextMuted);
    }
    (void)fH1; (void)fBody;
}

// ============================================================
//  Monthly Analytics
// ============================================================
void DrawMonthlyAnalytics(Graphics& g, float cx, float cY, float cw,
    float /*availH*/, float /*r1H*/, float r2H, float /*r3H*/,
    Font& fH1, Font& fH2, Font& fBody, Font& fBold, Font& fSm, Font& fMed,
    SolidBrush& bCard, SolidBrush& bTextMain, SolidBrush& bTextMuted, Pen& pBrd)
{
    float PAD=24; float r2Y=cY+PAD; float mcGap=16;
    float halfW=(cw-PAD*2-mcGap)/2; float rightX=cx+PAD+halfW+mcGap;
    StringFormat fmtR;fmtR.SetAlignment(StringAlignmentFar);
    StringFormat fmtC;fmtC.SetAlignment(StringAlignmentCenter);fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtTrim;fmtTrim.SetAlignment(StringAlignmentNear);fmtTrim.SetTrimming(StringTrimmingEllipsisCharacter);

    RoundRect(g,&bCard,&pBrd,cx+PAD,r2Y,halfW,r2H,8);
    g.DrawString(L"Deep Work Heatmap (Last 30 Days)",-1,&fH2,RectF(cx+PAD+20,r2Y+16,halfW,22),nullptr,&bTextMain);
    int cols=6,rows=7; float boxSize=18,boxGap=4;
    float gridW=cols*(boxSize+boxGap);
    float sX=cx+PAD+(halfW-gridW)/2; float sY=r2Y+60;
    Color hC[]={Color(255,241,245,249),Color(255,167,243,208),Color(255,52,211,153),Color(255,16,185,129),Color(255,4,120,87)};
    int sd[42]={0,1,2,3,4,0,0,1,2,4,4,3,1,0,2,3,3,4,4,2,0,0,1,2,3,4,0,0,4,4,4,3,2,0,0,1,2,3,0,0,0,0};
    for(int c=0;c<cols;c++)for(int r=0;r<rows;r++){int idx=c*rows+r;if(idx>=30)continue;int lv=sd[idx];SolidBrush bb(hC[lv]);float bx=sX+c*(boxSize+boxGap);float by=sY+r*(boxSize+boxGap);float ps=boxSize*stat_animProgress;float of=(boxSize-ps)/2;if(ps>2)RoundRect(g,&bb,nullptr,bx+of,by+of,ps,ps,4);}
    RoundRect(g,&bCard,&pBrd,rightX,r2Y,halfW,r2H,8);
    g.DrawString(L"Peak Productivity & Wellbeing",-1,&fH2,RectF(rightX+20,r2Y+16,halfW,22),nullptr,&bTextMain);
    float chronoY=r2Y+50;
    SolidBrush hBg(Color(255,238,242,255));SolidBrush hTx(Color(255,67,56,202));
    RoundRect(g,&hBg,nullptr,rightX+20,chronoY,halfW-40,60,6);
    g.DrawString(L"Peak Focus: 9:00 AM - 12:00 PM",-1,&fBold,RectF(rightX+30,chronoY+10,halfW-60,20),&fmtTrim,&hTx);
    g.DrawString(L"Schedule your hardest tasks during this window.",-1,&fSm,RectF(rightX+30,chronoY+32,halfW-60,18),&fmtTrim,&bTextMain);
    float ergoY=chronoY+75;
    g.DrawString(L"Ergonomics Check (This Month)",-1,&fBold,RectF(rightX+20,ergoY,halfW-40,20),nullptr,&bTextMain);
    struct ES{wstring l,v;float p;Color c;};
    vector<ES> es={{L"20-20-20 Rule",L"68%",68,Color(255,16,185,129)},{L"Posture Breaks",L"42%",42,Color(255,245,158,11)}};
    for(int i=0;i<(int)es.size();i++){float ey=ergoY+25+i*35;
        g.DrawString(es[i].l.c_str(),-1,&fSm,RectF(rightX+20,ey,halfW-100,18),nullptr,&bTextMuted);
        g.DrawString(es[i].v.c_str(),-1,&fBold,RectF(rightX+halfW-70,ey,50,18),&fmtR,&bTextMain);
        SolidBrush bgb(Color(255,241,245,249));RoundRect(g,&bgb,nullptr,rightX+20,ey+20,halfW-40,4,2);
        SolidBrush fb(es[i].c);float fw=(halfW-40)*(es[i].p/100)*stat_animProgress;if(fw>4)RoundRect(g,&fb,nullptr,rightX+20,ey+20,fw,4,2);}
    (void)fH1;(void)fBody;(void)fMed;
}

// ============================================================
//  Tab state
// ============================================================
static int   stat_activeSubTab = 0;
static float stat_animTimer    = 0.0f;

// ============================================================
//  DrawStatisticsTab
// ============================================================
void DrawStatisticsTab(Graphics& g, float cx, float cy, float cw, float ch)
{
    stat_animProgress = min(stat_animProgress + 0.04f, 1.0f);

    float PAD = 24.0f;
    float r2H = ch - PAD * 4 - 80.0f;

    SolidBrush bCard    (Color(255,255,255,255));
    SolidBrush bTextMain(Color(255, 30, 41, 59));
    SolidBrush bTextMuted(Color(255,100,116,139));
    Pen        pBrd     (Color(255,226,232,240), 1.0f);

    Font fH1  (L"Segoe UI",18,FontStyleBold,   UnitPixel);
    Font fH2  (L"Segoe UI",13,FontStyleBold,   UnitPixel);
    Font fBody(L"Segoe UI",11,FontStyleRegular,UnitPixel);
    Font fBold(L"Segoe UI",11,FontStyleBold,   UnitPixel);
    Font fSm  (L"Segoe UI", 9,FontStyleRegular,UnitPixel);
    Font fMed (L"Segoe UI",10,FontStyleBold,   UnitPixel);

    // Sub-tab bar
    const wchar_t* tabs[] = { L"Today", L"This Week", L"Monthly", L"Alarm" };
    const int nTabs = 4;
    float tabW=100, tabH=32, tabGap=8;
    float tabsX = cx + PAD;

    for (int i=0;i<nTabs;i++) {
        float tx = tabsX + i*(tabW+tabGap);
        bool  act = (i == stat_activeSubTab);
        Color actBg  = (i==3) ? Color(255,45,45,48)   : Color(255,99,102,241);
        Color actTxt = (i==3) ? Color(255,58,235,23)   : Color(255,255,255,255);
        SolidBrush tbg(act ? actBg : Color(255,241,245,249));
        RoundRect(g, &tbg, nullptr, tx, cy+PAD, tabW, tabH, 6);
        if (act && i==3) {
            SolidBrush gl(Color(255,58,235,23));
            g.FillRectangle(&gl, RectF(tx+8,cy+PAD+tabH-3,tabW-16,3));
        }
        SolidBrush ttxt(act ? actTxt : Color(255,100,116,139));
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(tabs[i],-1,&fBold,RectF(tx,cy+PAD,tabW,tabH),&sf,&ttxt);
    }

    float contentY = cy + PAD + tabH + PAD;
    float contentH = ch - (contentY - cy) - PAD;

    if      (stat_activeSubTab == 1)
        DrawWeeklyAnalytics(g,cx,contentY,cw,contentH,80,r2H,100,fH1,fH2,fBody,fBold,fSm,fMed,bCard,bTextMain,bTextMuted,pBrd);
    else if (stat_activeSubTab == 2)
        DrawMonthlyAnalytics(g,cx,contentY,cw,contentH,80,r2H,100,fH1,fH2,fBody,fBold,fSm,fMed,bCard,bTextMain,bTextMuted,pBrd);
    else if (stat_activeSubTab == 3)
        DrawAlarmSubTab(g, cx, contentY, cw, contentH);
    else {
        SolidBrush bg(Color(255,248,250,252));
        g.FillRectangle(&bg, RectF(cx+PAD,contentY,cw-PAD*2,contentH));
        StringFormat fc; fc.SetAlignment(StringAlignmentCenter); fc.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"Today's statistics coming soon",-1,&fBody,RectF(cx+PAD,contentY,cw-PAD*2,contentH),&fc,&bTextMuted);
    }
}

// ============================================================
//  Public mouse interface
// ============================================================
void ProcessStatisticsMouseMove(float mx, float my, float cx, float cy, float cw)
{
    if (stat_activeSubTab == 3) {
        float PAD=24, tabH=32;
        float contentY = cy + PAD + tabH + PAD;
        AlarmSubTabHover(mx, my, cx, contentY, cw);
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
    }
}

void ProcessStatisticsMouseClick(float mx, float my, float cx, float cy, float cw)
{
    float PAD=24, tabW=100, tabH=32, tabGap=8;
    float tabsX = cx + PAD;
    for (int i=0;i<4;i++) {
        float tx=tabsX+i*(tabW+tabGap);
        if (mx>=tx && mx<=tx+tabW && my>=cy+PAD && my<=cy+PAD+tabH) {
            stat_activeSubTab = i;
            stat_animProgress = 0.0f;
            return;
        }
    }
    if (stat_activeSubTab == 3) {
        float contentY = cy + PAD + tabH + PAD;
        AlarmSubTabClick(mx, my, cx, contentY, cw);
    }
    (void)cw;
}
CPPEOF
