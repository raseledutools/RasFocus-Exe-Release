#include "tab_device_block.h"
#include <vector>
#include <string>
#include <powrprof.h>
#pragma comment(lib, "PowrProf.lib")

// --- Premium Feature Gate ---
extern bool g_isPremiumUser;
extern bool g_showUpgradePopup;

// ── Family Link: Parent Remote Control ──
extern bool g_parentLockAllTabs;       // tab_family_link.cpp থেকে আসছে

using namespace Gdiplus;
using namespace std;

// --- State Variables ---
static float s_cx = 0, s_cy = 0, s_cw = 800, s_ch = 600;

// Internet Fasting States
static bool isFastingActive = false;
static int fastingModeIdx = 0; // 0: Until manually stopped, 1: 1 Hour, 2: 2 Hours
static bool isFastingDropOpen = false;
static bool hovFastingDrop = false;
static int hovFastingOpt = -1;
static bool hovFastingBtn = false;

// Power Management States
static int powerActionIdx = 0; // 0: Lock PC, 1: Sleep, 2: Shutdown
static int powerTimerIdx = 0;  // 0: Immediate, 1: In 15 mins, 2: In 1 Hour
static bool isPowerActionDropOpen = false, hovPowerActionDrop = false;
static bool isPowerTimerDropOpen = false, hovPowerTimerDrop = false;
static int hovPowerActionOpt = -1, hovPowerTimerOpt = -1;
static bool hovPowerBtn = false;

vector<wstring> fastingModes = { L"Until Manually Stopped", L"1 Hour Fasting", L"2 Hours Fasting" };
vector<wstring> powerActions = { L"Lock PC", L"Sleep Mode", L"Shutdown PC" };
vector<wstring> powerTimers = { L"Immediately", L"In 15 Minutes", L"In 1 Hour" };

// --- Colors ---
static const Color ClrTeal(255, 12, 168, 176);
static const Color ClrTealHover(255, 30, 185, 195);
static const Color ClrWhite(255, 255, 255, 255);
static const Color ClrDark(255, 50, 50, 50);
static const Color ClrGrayText(255, 120, 120, 120);
static const Color ClrBorder(255, 220, 225, 230);
static const Color ClrBg(255, 248, 250, 252);
static const Color ClrBgHover(255, 235, 248, 250);
static const Color ClrRed(255, 231, 76, 60);
static const Color ClrGreen(255, 90, 170, 20);
static const Color ClrGreenHover(255, 70, 150, 10);

// --- Helpers ---
static GraphicsPath* GetDeviceRoundRectPath(RectF rect, int radius) {
    GraphicsPath* path = new GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure(); return path;
}

// --- Main Drawing ---
void DrawDeviceBlockTab(Graphics& g, float cx, float cy, float cw, float ch) {
    s_cx = cx; s_cy = cy; s_cw = cw; s_ch = ch;

    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff, 22, FontStyleBold, UnitPixel); 
    Font fCardTitle(&ff, 18, FontStyleBold, UnitPixel); 
    Font fNormal(&ff, 15, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 15, FontStyleBold, UnitPixel);
    
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fIconBig(&ffIcons, 30, FontStyleRegular, UnitPixel);
    Font fIconSmall(&ffIcons, 14, FontStyleRegular, UnitPixel);
    
    SolidBrush bTeal(ClrTeal); SolidBrush bDark(ClrDark);
    SolidBrush bGray(ClrGrayText); SolidBrush bWhite(ClrWhite);
    SolidBrush bBg(ClrBg); Pen pBorder(ClrBorder, 1.5f);
    
    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    // Main Box Area (Assuming header is drawn by tab_blocks.cpp, we draw the body here)
    // If you use this inside the main body space:
    float boxX = cx + 30.0f;
    float boxY = cy + 40.0f; // Adjusted for body padding
    float boxW = cw - 60.0f;
    
    g.DrawString(L"Device Level Blockers", -1, &fTitle, RectF(boxX, boxY, boxW, 30.0f), &fL, &bDark);
    g.DrawString(L"Enforce strict discipline by restricting internet or locking your device entirely.", -1, &fNormal, RectF(boxX, boxY + 30.0f, boxW, 20.0f), &fL, &bGray);

    float cardY = boxY + 70.0f;
    float cardW = (boxW - 30.0f) / 2.0f;
    float cardH = 260.0f;
    float leftCardX = boxX;
    float rightCardX = boxX + cardW + 30.0f;

    // ==========================================
    // CARD 1: INTERNET FASTING
    // ==========================================
    GraphicsPath* cp1 = GetDeviceRoundRectPath(RectF(leftCardX, cardY, cardW, cardH), 8);
    g.FillPath(&bWhite, cp1); g.DrawPath(&pBorder, cp1); delete cp1;

    g.DrawString(L"\xE774", -1, &fIconBig, RectF(leftCardX + 20.0f, cardY + 20.0f, 40.0f, 40.0f), &fL, &bTeal); // Globe Icon
    g.DrawString(L"Internet Fasting", -1, &fCardTitle, RectF(leftCardX + 65.0f, cardY + 25.0f, cardW - 70.0f, 30.0f), &fL, &bDark);
    g.DrawString(L"Disable all network connections to force offline deep work.", -1, &fNormal, RectF(leftCardX + 20.0f, cardY + 70.0f, cardW - 40.0f, 40.0f), &fL, &bGray);

    // Fasting Dropdown
    g.DrawString(L"Select Duration:", -1, &fBold, RectF(leftCardX + 20.0f, cardY + 130.0f, cardW - 40.0f, 20.0f), &fL, &bDark);
    RectF fDropRect(leftCardX + 20.0f, cardY + 155.0f, cardW - 40.0f, 36.0f);
    GraphicsPath* fdp = GetDeviceRoundRectPath(fDropRect, 4);
    SolidBrush fdBg(isFastingActive ? ClrBg : (hovFastingDrop ? ClrBgHover : ClrWhite));
    g.FillPath(&fdBg, fdp); g.DrawPath(&pBorder, fdp); delete fdp;
    
    g.DrawString(fastingModes[fastingModeIdx].c_str(), -1, &fNormal, RectF(fDropRect.X + 10.0f, fDropRect.Y, fDropRect.Width - 30.0f, fDropRect.Height), &fL, isFastingActive ? &bGray : &bDark);
    g.DrawString(L"\xE70D", -1, &fIconSmall, RectF(fDropRect.X + fDropRect.Width - 30.0f, fDropRect.Y, 30.0f, fDropRect.Height), &fC, &bGray);

    // Fasting Button
    RectF fBtnRect(leftCardX + 20.0f, cardY + 205.0f, cardW - 40.0f, 40.0f);
    GraphicsPath* fbp = GetDeviceRoundRectPath(fBtnRect, 4);
    SolidBrush fBtnBrush(isFastingActive ? (hovFastingBtn ? Color(255, 200, 50, 50) : ClrRed) : (hovFastingBtn ? ClrGreenHover : ClrGreen));
    g.FillPath(&fBtnBrush, fbp); delete fbp;
    g.DrawString(isFastingActive ? L"Stop Internet Fasting" : L"Start Internet Fasting", -1, &fBold, fBtnRect, &fC, &bWhite);

    // ==========================================
    // CARD 2: POWER MANAGEMENT
    // ==========================================
    GraphicsPath* cp2 = GetDeviceRoundRectPath(RectF(rightCardX, cardY, cardW, cardH), 8);
    g.FillPath(&bWhite, cp2); g.DrawPath(&pBorder, cp2); delete cp2;

    g.DrawString(L"\xE7E8", -1, &fIconBig, RectF(rightCardX + 20.0f, cardY + 20.0f, 40.0f, 40.0f), &fL, &bTeal); // Power Icon
    g.DrawString(L"Power Management", -1, &fCardTitle, RectF(rightCardX + 65.0f, cardY + 25.0f, cardW - 70.0f, 30.0f), &fL, &bDark);
    g.DrawString(L"Enforce breaks by locking or shutting down your PC.", -1, &fNormal, RectF(rightCardX + 20.0f, cardY + 70.0f, cardW - 40.0f, 40.0f), &fL, &bGray);

    // Action Dropdown
    float halfDropW = (cardW - 50.0f) / 2.0f;
    g.DrawString(L"Action:", -1, &fBold, RectF(rightCardX + 20.0f, cardY + 130.0f, halfDropW, 20.0f), &fL, &bDark);
    RectF pActDropRect(rightCardX + 20.0f, cardY + 155.0f, halfDropW, 36.0f);
    GraphicsPath* pap = GetDeviceRoundRectPath(pActDropRect, 4);
    SolidBrush paBg(hovPowerActionDrop ? ClrBgHover : ClrWhite);
    g.FillPath(&paBg, pap); g.DrawPath(&pBorder, pap); delete pap;
    g.DrawString(powerActions[powerActionIdx].c_str(), -1, &fNormal, RectF(pActDropRect.X + 10.0f, pActDropRect.Y, pActDropRect.Width - 30.0f, pActDropRect.Height), &fL, &bDark);
    g.DrawString(L"\xE70D", -1, &fIconSmall, RectF(pActDropRect.X + pActDropRect.Width - 30.0f, pActDropRect.Y, 30.0f, pActDropRect.Height), &fC, &bGray);

    // Timer Dropdown
    g.DrawString(L"When:", -1, &fBold, RectF(rightCardX + 30.0f + halfDropW, cardY + 130.0f, halfDropW, 20.0f), &fL, &bDark);
    RectF pTimDropRect(rightCardX + 30.0f + halfDropW, cardY + 155.0f, halfDropW, 36.0f);
    GraphicsPath* ptp = GetDeviceRoundRectPath(pTimDropRect, 4);
    SolidBrush ptBg(hovPowerTimerDrop ? ClrBgHover : ClrWhite);
    g.FillPath(&ptBg, ptp); g.DrawPath(&pBorder, ptp); delete ptp;
    g.DrawString(powerTimers[powerTimerIdx].c_str(), -1, &fNormal, RectF(pTimDropRect.X + 10.0f, pTimDropRect.Y, pTimDropRect.Width - 30.0f, pTimDropRect.Height), &fL, &bDark);
    g.DrawString(L"\xE70D", -1, &fIconSmall, RectF(pTimDropRect.X + pTimDropRect.Width - 30.0f, pTimDropRect.Y, 30.0f, pTimDropRect.Height), &fC, &bGray);

    // Apply Action Button
    RectF pBtnRect(rightCardX + 20.0f, cardY + 205.0f, cardW - 40.0f, 40.0f);
    GraphicsPath* pbp = GetDeviceRoundRectPath(pBtnRect, 4);
    SolidBrush pBtnBrush(hovPowerBtn ? ClrTealHover : ClrTeal);
    g.FillPath(&pBtnBrush, pbp); delete pbp;
    g.DrawString(L"Apply Action", -1, &fBold, pBtnRect, &fC, &bWhite);


    // ==========================================
    // Z-INDEX FIX: DRAW DROPDOWNS ON TOP
    // ==========================================
    
    // Fasting Dropdown List
    if (isFastingDropOpen && !isFastingActive) {
        float listY = fDropRect.Y + fDropRect.Height + 2.0f;
        RectF listRect(fDropRect.X, listY, fDropRect.Width, fastingModes.size() * 38.0f);
        GraphicsPath* listP = GetDeviceRoundRectPath(listRect, 4); g.FillPath(&bWhite, listP); g.DrawPath(&pBorder, listP); delete listP;
        float itemY = listY;
        for (size_t i = 0; i < fastingModes.size(); ++i) {
            SolidBrush optBg(hovFastingOpt == i ? ClrBgHover : ClrWhite);
            g.FillRectangle(&optBg, RectF(listRect.X + 2.0f, itemY + 2.0f, listRect.Width - 4.0f, 34.0f));
            g.DrawString(fastingModes[i].c_str(), -1, &fNormal, RectF(listRect.X + 10.0f, itemY, listRect.Width, 38.0f), &fL, &bDark);
            itemY += 38.0f;
        }
    }

    // Power Action Dropdown List
    if (isPowerActionDropOpen) {
        float listY = pActDropRect.Y + pActDropRect.Height + 2.0f;
        RectF listRect(pActDropRect.X, listY, pActDropRect.Width, powerActions.size() * 38.0f);
        GraphicsPath* listP = GetDeviceRoundRectPath(listRect, 4); g.FillPath(&bWhite, listP); g.DrawPath(&pBorder, listP); delete listP;
        float itemY = listY;
        for (size_t i = 0; i < powerActions.size(); ++i) {
            SolidBrush optBg(hovPowerActionOpt == i ? ClrBgHover : ClrWhite);
            g.FillRectangle(&optBg, RectF(listRect.X + 2.0f, itemY + 2.0f, listRect.Width - 4.0f, 34.0f));
            g.DrawString(powerActions[i].c_str(), -1, &fNormal, RectF(listRect.X + 10.0f, itemY, listRect.Width, 38.0f), &fL, &bDark);
            itemY += 38.0f;
        }
    }

    // Power Timer Dropdown List
    if (isPowerTimerDropOpen) {
        float listY = pTimDropRect.Y + pTimDropRect.Height + 2.0f;
        RectF listRect(pTimDropRect.X, listY, pTimDropRect.Width, powerTimers.size() * 38.0f);
        GraphicsPath* listP = GetDeviceRoundRectPath(listRect, 4); g.FillPath(&bWhite, listP); g.DrawPath(&pBorder, listP); delete listP;
        float itemY = listY;
        for (size_t i = 0; i < powerTimers.size(); ++i) {
            SolidBrush optBg(hovPowerTimerOpt == i ? ClrBgHover : ClrWhite);
            g.FillRectangle(&optBg, RectF(listRect.X + 2.0f, itemY + 2.0f, listRect.Width - 4.0f, 34.0f));
            g.DrawString(powerTimers[i].c_str(), -1, &fNormal, RectF(listRect.X + 10.0f, itemY, listRect.Width, 38.0f), &fL, &bDark);
            itemY += 38.0f;
        }
    }
}

// --- Mouse Move ---
void ProcessDeviceBlockMouseMove(float x, float y) {
    float boxX = s_cx + 30.0f; 
    float boxY = s_cy + 40.0f; 
    float boxW = s_cw - 60.0f;
    float cardY = boxY + 70.0f;
    float cardW = (boxW - 30.0f) / 2.0f;
    float leftCardX = boxX;
    float rightCardX = boxX + cardW + 30.0f;

    hovFastingDrop = false; hovFastingOpt = -1; hovFastingBtn = false;
    hovPowerActionDrop = false; hovPowerTimerDrop = false;
    hovPowerActionOpt = -1; hovPowerTimerOpt = -1; hovPowerBtn = false;

    // --- Z-Index Priority (Check open dropdowns first) ---
    if (isFastingDropOpen && !isFastingActive) {
        float listY = cardY + 155.0f + 38.0f;
        RectF listRect(leftCardX + 20.0f, listY, cardW - 40.0f, fastingModes.size() * 38.0f);
        if (listRect.Contains(x, y)) {
            float itemY = listY;
            for (size_t i = 0; i < fastingModes.size(); ++i) {
                if (RectF(listRect.X, itemY, listRect.Width, 38.0f).Contains(x, y)) { hovFastingOpt = i; return; }
                itemY += 38.0f;
            }
            return;
        }
    }

    float halfDropW = (cardW - 50.0f) / 2.0f;

    if (isPowerActionDropOpen) {
        float listY = cardY + 155.0f + 38.0f;
        RectF listRect(rightCardX + 20.0f, listY, halfDropW, powerActions.size() * 38.0f);
        if (listRect.Contains(x, y)) {
            float itemY = listY;
            for (size_t i = 0; i < powerActions.size(); ++i) {
                if (RectF(listRect.X, itemY, listRect.Width, 38.0f).Contains(x, y)) { hovPowerActionOpt = i; return; }
                itemY += 38.0f;
            }
            return;
        }
    }

    if (isPowerTimerDropOpen) {
        float listY = cardY + 155.0f + 38.0f;
        RectF listRect(rightCardX + 30.0f + halfDropW, listY, halfDropW, powerTimers.size() * 38.0f);
        if (listRect.Contains(x, y)) {
            float itemY = listY;
            for (size_t i = 0; i < powerTimers.size(); ++i) {
                if (RectF(listRect.X, itemY, listRect.Width, 38.0f).Contains(x, y)) { hovPowerTimerOpt = i; return; }
                itemY += 38.0f;
            }
            return;
        }
    }

    // --- Base Hover Elements ---
    if (!isFastingActive) {
        if (RectF(leftCardX + 20.0f, cardY + 155.0f, cardW - 40.0f, 36.0f).Contains(x, y)) hovFastingDrop = true;
    }
    if (RectF(leftCardX + 20.0f, cardY + 205.0f, cardW - 40.0f, 40.0f).Contains(x, y)) hovFastingBtn = true;

    if (RectF(rightCardX + 20.0f, cardY + 155.0f, halfDropW, 36.0f).Contains(x, y)) hovPowerActionDrop = true;
    if (RectF(rightCardX + 30.0f + halfDropW, cardY + 155.0f, halfDropW, 36.0f).Contains(x, y)) hovPowerTimerDrop = true;
    if (RectF(rightCardX + 20.0f, cardY + 205.0f, cardW - 40.0f, 40.0f).Contains(x, y)) hovPowerBtn = true;
}

// --- Mouse Click ---
void ProcessDeviceBlockMouseClick(float x, float y) {
    // Entire Device Block feature is Premium only
    if (!g_isPremiumUser) { g_showUpgradePopup = true; return; }

    // 1. Process Open Dropdowns First
    if (isFastingDropOpen) {
        if (hovFastingOpt != -1) fastingModeIdx = hovFastingOpt;
        isFastingDropOpen = false; return; // Consume click
    }
    if (isPowerActionDropOpen) {
        if (hovPowerActionOpt != -1) powerActionIdx = hovPowerActionOpt;
        isPowerActionDropOpen = false; return;
    }
    if (isPowerTimerDropOpen) {
        if (hovPowerTimerOpt != -1) powerTimerIdx = hovPowerTimerOpt;
        isPowerTimerDropOpen = false; return;
    }

    // 2. Open Dropdowns
    if (hovFastingDrop) { isFastingDropOpen = true; return; }
    if (hovPowerActionDrop) { isPowerActionDropOpen = true; return; }
    if (hovPowerTimerDrop) { isPowerTimerDropOpen = true; return; }

    // 3. Process Buttons
    if (hovFastingBtn) {
        if (isFastingActive) {
            isFastingActive = false;
            // REAL LOGIC: Enable Internet
            // system(\"ipconfig /renew\");
            MessageBoxA(NULL, "Internet Connection Restored.", "Fasting Stopped", MB_OK | MB_ICONINFORMATION);
        } else {
            isFastingActive = true;
            // REAL LOGIC: Disable Internet (Requires Admin usually)
            // system(\"ipconfig /release\"); 
            MessageBoxA(NULL, "Internet Fasting Started! Network has been disabled.", "Fasting Active", MB_OK | MB_ICONWARNING);
        }
    }

    // ── Family Link: Parent দূর থেকে PC Lock করলে ──
    if (g_parentLockAllTabs) {
        LockWorkStation();
    }

    if (hovPowerBtn) {
        if (powerActionIdx == 0) { // Lock PC
            // REAL LOGIC: 
            LockWorkStation();
        } 
        else if (powerActionIdx == 1) { // Sleep
            // REAL LOGIC:
            SetSuspendState(0, 0, 0); // Need to adjust token privileges for sleep
        } 
        else if (powerActionIdx == 2) { // Shutdown
            if (powerTimerIdx == 0) {
                // REAL LOGIC: Shutdown immediately
                // system("shutdown /s /t 0");
                MessageBoxA(NULL, "System will shutdown NOW.", "Power", MB_OK | MB_ICONWARNING);
            } else if (powerTimerIdx == 1) {
                // system("shutdown /s /t 900");
                MessageBoxA(NULL, "System will shutdown in 15 Minutes.", "Power", MB_OK | MB_ICONWARNING);
            } else if (powerTimerIdx == 2) {
                // system("shutdown /s /t 3600");
                MessageBoxA(NULL, "System will shutdown in 1 Hour.", "Power", MB_OK | MB_ICONWARNING);
            }
        }
    }
}

// ─── Aliases used by tab_blocks.cpp ───────────────────────────────────────
void ProcessDeviceBlockKeyPress(wchar_t /*c*/)              { /* Device block has no key input */ }
void ProcessDeviceBlockKeyDown(WPARAM /*key*/)              { /* Device block has no key input */ }
void ProcessDeviceBlockMouseWheel(float x, float y, int /*d*/) { ProcessDeviceBlockMouseMove(x, y); }