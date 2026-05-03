#include "tab_settings.h"
#include <vector>
#include <string>

using namespace Gdiplus;
using namespace std;

// --- State Variables for Settings Tabs ---
static int currentSetTab = 0; // Default to General
static int hoverSetTab = -1;

// --- Functional Variables (No Demos) ---
// 1. General
static bool tglStartup = true;
static bool tglRequirePass = false;
static bool tglStartMin = true;
static bool tglDarkTheme = false;
static bool tglDailyBackup = true;
static bool tgl24Hour = false;
static int langIdx = 0; 

// 2. Browsers
static bool tglForceActive = true;
static bool tglStrictTitle = true;
static bool tglCacheURL = true;
static bool tglAggressiveBlock = false;
static bool tglBlockNoExt = false;

// 3. System
static bool tglBlockTaskMgr = false;
static bool tglBlockRegEdit = false;
static bool tglProtectUninstall = true;
static bool tglSafeMode = true;
static bool tglProcessSuspend = false;
static bool tglBlockUninstallers = true;
static bool tglProtectPowerShell = true;

// 4. Advanced
static bool tglRecordUsage = true;
static bool tglProtectClipboard = false;
static bool tglShowDelete = false;
static bool tglShowTimer = true;
static bool tglImportExport = true;
static int maxPlansCount = 0;

// 5. Notification 
static int genPosIdx = 0; 
static int aiPosIdx = 0;
static bool tglAudio = true;
static bool tglMuteAll = false;
static bool tglDND = true;
static int dndStartH = 22; 
static int dndEndH = 6;    
static int threshMin = 10;
static int freqNormMin = 30;
static int freqLowMin = 5;

// 6. Sync
static bool tglCloudSync = false;
static bool tglSyncSchedules = true;

// --- Hover States ---
static int hoverToggleIdx = -1; 
static bool hoverLangBtn = false, hoverChromeBtn = false, hoverFirefoxBtn = false;
static bool hoverMinusBtn = false, hoverPlusBtn = false;

static bool hGenPos = false, hAiPos = false;
static bool hDndStartM = false, hDndStartP = false;
static bool hDndEndM = false, hDndEndP = false;
static bool hThreshM = false, hThreshP = false;
static bool hFreqNormM = false, hFreqNormP = false;
static bool hFreqLowM = false, hFreqLowP = false;

// --- Local Colors (Eyecure Teal Theme) ---
static const Color SClrTeal(255, 12, 168, 176);
static const Color SClrWhite(255, 255, 255, 255);
static const Color SClrDark(255, 50, 50, 50);
static const Color SClrGrayText(255, 120, 120, 120);
static const Color SClrBorder(255, 220, 225, 230);
static const Color SClrBg(255, 248, 250, 252); // Light Gray Background
static const Color SClrCaution(255, 180, 150, 150); 
static const Color SClrBtnLight(255, 235, 235, 235);
static const Color SClrBtnHover(255, 215, 215, 215);

// --- Helper Functions ---
static GraphicsPath* GetSetRoundRectPath(RectF rect, int radius) {
    GraphicsPath* path = new GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure();
    return path;
}

void DrawToggleSwitch(Graphics& g, float x, float y, bool isOn) {
    GraphicsPath* swPath = GetSetRoundRectPath(RectF(x, y, 44.0f, 22.0f), 11);
    SolidBrush swBg(isOn ? SClrTeal : Color(255, 200, 200, 200));
    g.FillPath(&swBg, swPath); delete swPath;
    float circleX = isOn ? x + 24.0f : x + 2.0f;
    SolidBrush circleBrush(SClrWhite);
    g.FillEllipse(&circleBrush, circleX, y + 2.0f, 18.0f, 18.0f);
}

void DrawSetRow(Graphics& g, const wstring& text, float x, float y, float w, float rowH, Font* fNormal, SolidBrush* bText, bool hasCaution = false) {
    g.DrawString(text.c_str(), -1, fNormal, RectF(x, y, w, rowH), NULL, bText);
    if (hasCaution) {
        SolidBrush cautionBrush(SClrCaution);
        FontFamily ff(L"Segoe UI");
        Font fItalic(&ff, 14, FontStyleItalic, UnitPixel);
        RectF bounds;
        g.MeasureString(text.c_str(), -1, fNormal, PointF(0,0), &bounds);
        g.DrawString(L"(Caution!)", -1, &fItalic, RectF(x + bounds.Width + 5.0f, y, 100.0f, rowH), NULL, &cautionBrush);
    }
}

void DrawSpinner(Graphics& g, float x, float y, const wstring& valStr, bool hM, bool hP, Font* fIcon, Font* fBold) {
    SolidBrush brushBtn(SClrBtnLight);
    SolidBrush brushBtnHover(SClrBtnHover);
    SolidBrush brushWhite(SClrWhite);
    SolidBrush brushDark(SClrDark);
    Pen penBorder(SClrBorder, 1.5f);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);

    RectF minusRect(x, y, 32.0f, 32.0f);
    RectF textRect(x + 32.0f, y, 60.0f, 32.0f);
    RectF plusRect(x + 92.0f, y, 32.0f, 32.0f);

    g.FillRectangle(hM ? &brushBtnHover : &brushBtn, minusRect); g.DrawRectangle(&penBorder, minusRect.X, minusRect.Y, minusRect.Width, minusRect.Height);
    g.DrawString(L"\xE738", -1, fIcon, minusRect, &fmtC, &brushDark);

    g.FillRectangle(&brushWhite, textRect); g.DrawRectangle(&penBorder, textRect.X, textRect.Y, textRect.Width, textRect.Height);
    g.DrawString(valStr.c_str(), -1, fBold, textRect, &fmtC, &brushDark);

    g.FillRectangle(hP ? &brushBtnHover : &brushBtn, plusRect); g.DrawRectangle(&penBorder, plusRect.X, plusRect.Y, plusRect.Width, plusRect.Height);
    g.DrawString(L"\xE710", -1, fIcon, plusRect, &fmtC, &brushDark);
}

wstring FmtTime(int h, int m) {
    wstring hs = to_wstring(h); if (hs.length() < 2) hs = L"0" + hs;
    wstring ms = to_wstring(m); if (ms.length() < 2) ms = L"0" + ms;
    return hs + L":" + ms;
}

// --- Main Drawing Function ---
void DrawSettingsTab(Graphics& g, float contentX, float contentY, float contentW, float contentH) {
    FontFamily ff(L"Segoe UI");
    Font fTopTab(&ff, 16, FontStyleBold, UnitPixel);
    Font fNormal(&ff, 15, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 15, FontStyleBold, UnitPixel);
    Font fSmall(&ff, 13, FontStyleRegular, UnitPixel);
    
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fIcon(&ffIcons, 22, FontStyleRegular, UnitPixel);
    
    SolidBrush brushTeal(SClrTeal);
    SolidBrush brushDark(SClrDark);
    SolidBrush brushGray(SClrGrayText);
    SolidBrush brushWhite(SClrWhite);
    SolidBrush brushBg(SClrBg); // Eyecure Background
    Pen penBorder(SClrBorder, 1.5f);
    
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);

    // ==========================================
    // 1. TOP HEADER 
    // ==========================================
    float headerH = 65.0f;
    g.FillRectangle(&brushWhite, contentX, contentY, contentW, headerH); 
    
    g.DrawString(L"\xE713", -1, &fIcon, RectF(contentX + 30.0f, contentY, 30.0f, headerH), &fmtC, &brushDark);

    wstring topTabs[] = { L"General", L"Browsers", L"System", L"Advanced", L"Notification", L"Sync" };
    float tabWidths[] = { 80.0f, 90.0f, 70.0f, 90.0f, 110.0f, 60.0f };
    float currentX = contentX + 80.0f; 

    for (int i = 0; i < 6; ++i) {
        RectF tabRect(currentX, contentY, tabWidths[i], headerH);
        SolidBrush* textBrush = (currentSetTab == i) ? &brushTeal : ((hoverSetTab == i) ? &brushTeal : &brushGray);
        g.DrawString(topTabs[i].c_str(), -1, &fTopTab, tabRect, &fmtC, textBrush);
        if (currentSetTab == i) g.FillRectangle(&brushTeal, currentX + 10.0f, contentY + headerH - 3.0f, tabWidths[i] - 20.0f, 3.0f);
        currentX += tabWidths[i] + 5.0f;
    }

    // ==========================================
    // 2. MAIN CONTENT AREA 
    // ==========================================
    float bodyY = contentY + headerH;
    g.FillRectangle(&brushBg, contentX, bodyY, contentW, contentH - headerH); 

    float boxX = contentX + 30.0f;
    float boxW = contentW - 60.0f;
    float boxH = contentH - headerH - 50.0f;

    GraphicsPath* boxPath = GetSetRoundRectPath(RectF(boxX, bodyY + 25.0f, boxW, boxH), 6);
    g.FillPath(&brushWhite, boxPath); g.DrawPath(&penBorder, boxPath); delete boxPath;

    // ROW CONFIGURATION
    float rowY = bodyY + 35.0f;
    float rowH = (currentSetTab == 4) ? 41.0f : 50.0f; 
    float textX = boxX + 30.0f;
    float swX = boxX + boxW - 80.0f;
    float tOff = (rowH - 22.0f) / 2.0f;

    if (currentSetTab == 0) { // General
        DrawSetRow(g, L"Launch Application at System Logon", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglStartup); rowY += rowH;
        DrawSetRow(g, L"Require Password for Settings Access", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglRequirePass); rowY += rowH;
        DrawSetRow(g, L"Start Application Minimized to Tray", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglStartMin); rowY += rowH;

        wstring langs[] = { L"English", L"Spanish", L"French" };
        DrawSetRow(g, L"Application Language", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        RectF langBox(swX - 100.0f, rowY + 9.0f, 146.0f, 32.0f);
        GraphicsPath* lp = GetSetRoundRectPath(langBox, 4);
        g.FillPath(hoverLangBtn ? &brushBg : &brushWhite, lp); g.DrawPath(&penBorder, lp); delete lp;
        g.DrawString(langs[langIdx].c_str(), -1, &fNormal, langBox, &fmtC, &brushDark); rowY += rowH;

        DrawSetRow(g, L"Enable Dark Theme Aesthetic", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglDarkTheme); rowY += rowH;
        DrawSetRow(g, L"Create Daily Settings Backup", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglDailyBackup); rowY += rowH;
        DrawSetRow(g, L"Use 24-Hour Time Format", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tgl24Hour);
    } 
    else if (currentSetTab == 1) { // Browsers
        DrawSetRow(g, L"Enforce Active Browser Tracking", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglForceActive); rowY += rowH;
        DrawSetRow(g, L"Strict URL Matching for Popular Sites", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglStrictTitle); rowY += rowH;
        DrawSetRow(g, L"Cache Recently Visited Websites", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglCacheURL); rowY += rowH;
        DrawSetRow(g, L"Aggressive Browser Blocking on Failure", textX, rowY, boxW, rowH, &fNormal, &brushDark, true);
        DrawToggleSwitch(g, swX, rowY + tOff, tglAggressiveBlock); rowY += rowH;
        DrawSetRow(g, L"Block Browser if Extension is Missing", textX, rowY, boxW, rowH, &fNormal, &brushDark, true);
        DrawToggleSwitch(g, swX, rowY + tOff, tglBlockNoExt); rowY += rowH;

        g.DrawString(L"Browser Extensions", -1, &fNormal, RectF(textX, rowY, 200.0f, rowH), &fmtL, &brushDark);
        g.DrawString(L"Click to install:", -1, &fSmall, RectF(swX - 250.0f, rowY, 100.0f, rowH), &fmtL, &brushGray);

        RectF chrBtn(swX - 140.0f, rowY + 9.0f, 90.0f, 32.0f);
        GraphicsPath* cp = GetSetRoundRectPath(chrBtn, 4);
        g.FillPath(hoverChromeBtn ? &brushBg : &brushWhite, cp); g.DrawPath(&penBorder, cp); delete cp;
        g.DrawString(L"Chrome", -1, &fNormal, chrBtn, &fmtC, &brushTeal);

        RectF fxBtn(swX - 40.0f, rowY + 9.0f, 86.0f, 32.0f);
        GraphicsPath* fp = GetSetRoundRectPath(fxBtn, 4);
        g.FillPath(hoverFirefoxBtn ? &brushBg : &brushWhite, fp); g.DrawPath(&penBorder, fp); delete fp;
        g.DrawString(L"Firefox", -1, &fNormal, fxBtn, &fmtC, &brushTeal);
    }
    else if (currentSetTab == 2) { // System
        DrawSetRow(g, L"Prevent Task Manager Access", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglBlockTaskMgr); rowY += rowH;
        DrawSetRow(g, L"Block Registry Editor Modifications", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglBlockRegEdit); rowY += rowH;
        DrawSetRow(g, L"Protect Application from Uninstallation", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglProtectUninstall); rowY += rowH;
        DrawSetRow(g, L"Run in Safe Mode", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglSafeMode); rowY += rowH;
        DrawSetRow(g, L"Protect Process Suspending", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglProcessSuspend); rowY += rowH;
        DrawSetRow(g, L"Block 3rd Party Uninstallers", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglBlockUninstallers); rowY += rowH;
        DrawSetRow(g, L"Protect PowerShell History", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglProtectPowerShell);
    }
    else if (currentSetTab == 3) { // Advanced
        DrawSetRow(g, L"Record Website Usage (Locally)", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglRecordUsage); rowY += rowH;
        DrawSetRow(g, L"Protect Clipboard Operations", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglProtectClipboard); rowY += rowH;
        DrawSetRow(g, L"Show 'Delete' Application Block Method", textX, rowY, boxW, rowH, &fNormal, &brushDark, true);
        DrawToggleSwitch(g, swX, rowY + tOff, tglShowDelete); rowY += rowH;
        DrawSetRow(g, L"Selectively Show Timer Windows", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglShowTimer); rowY += rowH;
        DrawSetRow(g, L"Enable Import & Export of Plans", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglImportExport); rowY += rowH;
        
        DrawSetRow(g, L"Maximum Number of Plans", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        wstring plansTxt = (maxPlansCount == 0) ? L"Unlimited" : to_wstring(maxPlansCount);
        DrawSpinner(g, swX - 80.0f, rowY + tOff - 5.0f, plansTxt, hoverMinusBtn, hoverPlusBtn, &fIcon, &fBold);
    }
    else if (currentSetTab == 4) { // Notification 
        wstring positions[] = { L"Top Right", L"Bottom Right", L"Top Left", L"Bottom Left" };
        
        DrawSetRow(g, L"System Alert Screen Position", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        RectF p1Box(swX - 60.0f, rowY + 5.0f, 120.0f, 30.0f);
        GraphicsPath* p1 = GetSetRoundRectPath(p1Box, 4); g.FillPath(hGenPos ? &brushBg : &brushWhite, p1); g.DrawPath(&penBorder, p1); delete p1;
        g.DrawString(positions[genPosIdx].c_str(), -1, &fNormal, p1Box, &fmtC, &brushDark);
        rowY += rowH;

        DrawSetRow(g, L"Assistant Alert Position", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        RectF p2Box(swX - 60.0f, rowY + 5.0f, 120.0f, 30.0f);
        GraphicsPath* p2 = GetSetRoundRectPath(p2Box, 4); g.FillPath(hAiPos ? &brushBg : &brushWhite, p2); g.DrawPath(&penBorder, p2); delete p2;
        g.DrawString(positions[aiPosIdx].c_str(), -1, &fNormal, p2Box, &fmtC, &brushDark);
        rowY += rowH;

        DrawSetRow(g, L"Enable Audio Chimes", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglAudio);
        rowY += rowH;

        DrawSetRow(g, L"Mute All Desktop Alerts", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglMuteAll);
        rowY += rowH;

        DrawSetRow(g, L"Schedule Do Not Disturb (DND)", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglDND);
        rowY += rowH;

        float ctrlX = swX - 80.0f;
        DrawSetRow(g, L"DND Activation Time", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawSpinner(g, ctrlX, rowY + 5.0f, FmtTime(dndStartH, 0), hDndStartM, hDndStartP, &fIcon, &fBold);
        rowY += rowH;

        DrawSetRow(g, L"DND Deactivation Time", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawSpinner(g, ctrlX, rowY + 5.0f, FmtTime(dndEndH, 0), hDndEndM, hDndEndP, &fIcon, &fBold);
        rowY += rowH;

        DrawSetRow(g, L"Alert Threshold Interval (Min)", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawSpinner(g, ctrlX, rowY + 5.0f, FmtTime(0, threshMin), hThreshM, hThreshP, &fIcon, &fBold);
        rowY += rowH;

        DrawSetRow(g, L"Standard Frequency (Min)", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawSpinner(g, ctrlX, rowY + 5.0f, FmtTime(0, freqNormMin), hFreqNormM, hFreqNormP, &fIcon, &fBold);
        rowY += rowH;

        DrawSetRow(g, L"Low Priority Frequency (Min)", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawSpinner(g, ctrlX, rowY + 5.0f, FmtTime(0, freqLowMin), hFreqLowM, hFreqLowP, &fIcon, &fBold);
    }
    else if (currentSetTab == 5) { // Sync
        DrawSetRow(g, L"Enable Cloud Synchronization", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglCloudSync); rowY += rowH;
        DrawSetRow(g, L"Sync Custom Block Schedules", textX, rowY, boxW, rowH, &fNormal, &brushDark);
        DrawToggleSwitch(g, swX, rowY + tOff, tglSyncSchedules);
    }
}

// --- Mouse Move Logic ---
void ProcessSettingsMouseMove(float x, float y) {
    extern const int SIDEBAR_WIDTH;
    extern const int TITLEBAR_HEIGHT;
    extern int windowWidth;
    
    float contentX = (float)SIDEBAR_WIDTH;
    float contentY = (float)TITLEBAR_HEIGHT;
    float contentW = (float)(windowWidth - SIDEBAR_WIDTH);

    hoverSetTab = -1; hoverToggleIdx = -1;
    hoverMinusBtn = false; hoverPlusBtn = false;
    hoverLangBtn = false; hoverChromeBtn = false; hoverFirefoxBtn = false;
    hGenPos = false; hAiPos = false;
    hDndStartM = false; hDndStartP = false;
    hDndEndM = false; hDndEndP = false;
    hThreshM = false; hThreshP = false;
    hFreqNormM = false; hFreqNormP = false;
    hFreqLowM = false; hFreqLowP = false;

    // Top Tabs
    float headerH = 65.0f;
    float tabWidths[] = { 80.0f, 90.0f, 70.0f, 90.0f, 110.0f, 60.0f };
    float currentX = contentX + 80.0f;
    if (y >= contentY && y <= contentY + headerH) {
        for (int i = 0; i < 6; ++i) {
            if (x >= currentX && x <= currentX + tabWidths[i]) { hoverSetTab = i; break; }
            currentX += tabWidths[i] + 5.0f;
        }
    }

    float bodyY = contentY + headerH + 25.0f;
    float boxX = contentX + 30.0f;
    float boxW = contentW - 60.0f;
    float swX = boxX + boxW - 80.0f;
    float rowY = bodyY + 10.0f;
    float rowH = (currentSetTab == 4) ? 41.0f : 50.0f;
    float tOff = (rowH - 22.0f) / 2.0f;
    float ctrlX = swX - 80.0f;

    if (currentSetTab == 0) { // General
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 0; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 1; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 2; rowY += rowH;
        if (RectF(swX - 100.0f, rowY + 9.0f, 146.0f, 32.0f).Contains(x, y)) hoverLangBtn = true; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 3; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 4; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 5;
    }
    else if (currentSetTab == 1) { // Browsers
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 10; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 11; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 12; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 13; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 14; rowY += rowH;
        if (RectF(swX - 140.0f, rowY + 9.0f, 90.0f, 32.0f).Contains(x, y)) hoverChromeBtn = true;
        if (RectF(swX - 40.0f, rowY + 9.0f, 86.0f, 32.0f).Contains(x, y)) hoverFirefoxBtn = true;
    }
    else if (currentSetTab == 2) { // System
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 20; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 21; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 22; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 23; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 24; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 25; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 26;
    }
    else if (currentSetTab == 3) { // Advanced
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 30; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 31; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 32; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 33; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 34; rowY += rowH;
        
        if (RectF(swX - 80.0f, rowY + tOff - 5.0f, 32.0f, 32.0f).Contains(x, y)) hoverMinusBtn = true;
        if (RectF(swX - 80.0f + 92.0f, rowY + tOff - 5.0f, 32.0f, 32.0f).Contains(x, y)) hoverPlusBtn = true;
    }
    else if (currentSetTab == 4) { // Notification
        if (RectF(swX - 60.0f, rowY + 5.0f, 120.0f, 30.0f).Contains(x, y)) hGenPos = true; rowY += rowH;
        if (RectF(swX - 60.0f, rowY + 5.0f, 120.0f, 30.0f).Contains(x, y)) hAiPos = true; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 40; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 41; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 42; rowY += rowH;

        if (RectF(ctrlX, rowY + 5.0f, 32.0f, 32.0f).Contains(x, y)) hDndStartM = true;
        if (RectF(ctrlX + 92.0f, rowY + 5.0f, 32.0f, 32.0f).Contains(x, y)) hDndStartP = true; rowY += rowH;

        if (RectF(ctrlX, rowY + 5.0f, 32.0f, 32.0f).Contains(x, y)) hDndEndM = true;
        if (RectF(ctrlX + 92.0f, rowY + 5.0f, 32.0f, 32.0f).Contains(x, y)) hDndEndP = true; rowY += rowH;

        if (RectF(ctrlX, rowY + 5.0f, 32.0f, 32.0f).Contains(x, y)) hThreshM = true;
        if (RectF(ctrlX + 92.0f, rowY + 5.0f, 32.0f, 32.0f).Contains(x, y)) hThreshP = true; rowY += rowH;

        if (RectF(ctrlX, rowY + 5.0f, 32.0f, 32.0f).Contains(x, y)) hFreqNormM = true;
        if (RectF(ctrlX + 92.0f, rowY + 5.0f, 32.0f, 32.0f).Contains(x, y)) hFreqNormP = true; rowY += rowH;

        if (RectF(ctrlX, rowY + 5.0f, 32.0f, 32.0f).Contains(x, y)) hFreqLowM = true;
        if (RectF(ctrlX + 92.0f, rowY + 5.0f, 32.0f, 32.0f).Contains(x, y)) hFreqLowP = true;
    }
    else if (currentSetTab == 5) { // Sync
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 50; rowY += rowH;
        if (RectF(swX, rowY + tOff, 44.0f, 22.0f).Contains(x, y)) hoverToggleIdx = 51;
    }
}

// --- Mouse Click Logic ---
void ProcessSettingsMouseClick(float x, float y) {
    if (hoverSetTab != -1) { currentSetTab = hoverSetTab; return; }

    // General Logic
    if (hoverToggleIdx == 0) tglStartup = !tglStartup;
    if (hoverToggleIdx == 1) tglRequirePass = !tglRequirePass;
    if (hoverToggleIdx == 2) tglStartMin = !tglStartMin;
    if (hoverToggleIdx == 3) tglDarkTheme = !tglDarkTheme;
    if (hoverToggleIdx == 4) tglDailyBackup = !tglDailyBackup;
    if (hoverToggleIdx == 5) tgl24Hour = !tgl24Hour;
    if (hoverLangBtn) langIdx = (langIdx + 1) % 3;

    // Browsers Logic
    if (hoverToggleIdx == 10) tglForceActive = !tglForceActive;
    if (hoverToggleIdx == 11) tglStrictTitle = !tglStrictTitle;
    if (hoverToggleIdx == 12) tglCacheURL = !tglCacheURL;
    if (hoverToggleIdx == 13) tglAggressiveBlock = !tglAggressiveBlock;
    if (hoverToggleIdx == 14) tglBlockNoExt = !tglBlockNoExt;
    if (hoverChromeBtn) MessageBox(NULL, "Installing Chrome Extension...", "Browser Integration", MB_OK | MB_ICONINFORMATION);
    if (hoverFirefoxBtn) MessageBox(NULL, "Installing Firefox Extension...", "Browser Integration", MB_OK | MB_ICONINFORMATION);

    // System Logic
    if (hoverToggleIdx == 20) tglBlockTaskMgr = !tglBlockTaskMgr;
    if (hoverToggleIdx == 21) tglBlockRegEdit = !tglBlockRegEdit;
    if (hoverToggleIdx == 22) tglProtectUninstall = !tglProtectUninstall;
    if (hoverToggleIdx == 23) tglSafeMode = !tglSafeMode;
    if (hoverToggleIdx == 24) tglProcessSuspend = !tglProcessSuspend;
    if (hoverToggleIdx == 25) tglBlockUninstallers = !tglBlockUninstallers;
    if (hoverToggleIdx == 26) tglProtectPowerShell = !tglProtectPowerShell;

    // Advanced Logic
    if (hoverToggleIdx == 30) tglRecordUsage = !tglRecordUsage;
    if (hoverToggleIdx == 31) tglProtectClipboard = !tglProtectClipboard;
    if (hoverToggleIdx == 32) tglShowDelete = !tglShowDelete;
    if (hoverToggleIdx == 33) tglShowTimer = !tglShowTimer;
    if (hoverToggleIdx == 34) tglImportExport = !tglImportExport;
    if (hoverMinusBtn && maxPlansCount > 0) maxPlansCount--;
    if (hoverPlusBtn) maxPlansCount++;

    // Notification Logic
    if (hGenPos) genPosIdx = (genPosIdx + 1) % 4;
    if (hAiPos) aiPosIdx = (aiPosIdx + 1) % 4;
    if (hoverToggleIdx == 40) tglAudio = !tglAudio;
    if (hoverToggleIdx == 41) tglMuteAll = !tglMuteAll;
    if (hoverToggleIdx == 42) tglDND = !tglDND;
    if (hDndStartM) dndStartH = (dndStartH - 1 + 24) % 24;
    if (hDndStartP) dndStartH = (dndStartH + 1) % 24;
    if (hDndEndM) dndEndH = (dndEndH - 1 + 24) % 24;
    if (hDndEndP) dndEndH = (dndEndH + 1) % 24;
    if (hThreshM && threshMin > 1) threshMin--;
    if (hThreshP && threshMin < 59) threshMin++;
    if (hFreqNormM && freqNormMin > 1) freqNormMin--;
    if (hFreqNormP && freqNormMin < 59) freqNormMin++;
    if (hFreqLowM && freqLowMin > 1) freqLowMin--;
    if (hFreqLowP && freqLowMin < 59) freqLowMin++;

    // Sync Logic
    if (hoverToggleIdx == 50) tglCloudSync = !tglCloudSync;
    if (hoverToggleIdx == 51) tglSyncSchedules = !tglSyncSchedules;
}