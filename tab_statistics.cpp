#include "tab_statistics.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <iomanip>

#pragma comment(lib, "psapi.lib")

using namespace Gdiplus;
using namespace std;

// ============================================================
//  STATE VARIABLES
// ============================================================
static int   stat_currentTab  = 0; 
static float stat_animProgress = 0.0f;
static bool  stat_firstLoad    = true;
static float stat_scrollY      = 0.0f;       // Current scroll position
static float stat_targetScrollY = 0.0f;      // For smooth scrolling interpolation

// ============================================================
//  DATA STRUCTURES
// ============================================================
struct AppActivityEntry {
    wstring name;
    wstring category;
    wstring activeTime;
    wstring onScreenTime;
    int     launches;
    Color   dotColor;
};

// ============================================================
//  DATA FETCHING LOGIC (Replace with your foreground tracker)
// ============================================================
vector<AppActivityEntry> GetTrackedApps() {
    // এখানে তোমার ডাটাবেস বা ব্যাকগ্রাউন্ড পোলিং (GetForegroundWindow) থেকে ডেটা আসবে।
    // ডেমো পারপাসের জন্য ফোকাসমির মতো কিছু ডেমো ডেটা দেওয়া হলো।
    vector<AppActivityEntry> apps;
    apps.push_back({ L"RasFocus Pro Max", L"Focus System", L"17s", L"1m 16s", 1, Color(255, 16, 185, 129) }); // Emerald
    apps.push_back({ L"Visual Studio Code", L"Code Editor", L"1h 45m", L"2h 10m", 3, Color(255, 139, 92, 246) }); // Purple
    apps.push_back({ L"Chrome", L"Distracting (Web)", L"45m", L"1h 0m", 5, Color(255, 239, 68, 68) }); // Red
    apps.push_back({ L"FocusMe", L"Productivity", L"2m 15s", L"4m 59s", 1, Color(255, 59, 130, 246) }); // Blue
    
    // স্ক্রল টেস্ট করার জন্য আরও কিছু ডেমো অ্যাপ
    for(int i = 1; i <= 20; i++){
        apps.push_back({ L"System Service " + to_wstring(i), L"Background", L"0s", L"10m 5s", 1, Color(255, 156, 163, 175) });
    }
    return apps;
}

// ============================================================
//  PRO UI DRAWING HELPERS
// ============================================================
static void RoundRect(Graphics& g, Brush* br, Pen* pen, float x, float y, float w, float h, int r) {
    GraphicsPath p; float d = r * 2.0f;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
    if (br)  g.FillPath(br, &p);
    if (pen) g.DrawPath(pen, &p);
}

static void FillCircle(Graphics& g, SolidBrush& br, float cx, float cy, float r) {
    g.FillEllipse(&br, cx - r, cy - r, r * 2.0f, r * 2.0f);
}

// ============================================================
//  MAIN DRAW (FocusMe Style Layout + Smooth Scroll)
// ============================================================
void DrawStatisticsTab(Graphics& g, float cx, float cy, float cw, float ch)
{
    // Animation & Smooth Scroll Logic
    if (stat_firstLoad) {
        stat_animProgress += 0.04f; 
        if (stat_animProgress >= 1.0f) { stat_animProgress = 1.0f; stat_firstLoad = false; }
    }
    
    // Smooth Scroll Interpolation (Lerp)
    stat_scrollY += (stat_targetScrollY - stat_scrollY) * 0.2f; 
    if (abs(stat_targetScrollY - stat_scrollY) < 0.5f) stat_scrollY = stat_targetScrollY;

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // --- Typography ---
    FontFamily ff(L"Segoe UI");
    Font fH2(&ff, 18, FontStyleBold, UnitPixel);
    Font fBody(&ff, 14, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 14, FontStyleBold, UnitPixel);
    Font fSm(&ff, 12, FontStyleRegular, UnitPixel);
    Font fLg(&ff, 32, FontStyleRegular, UnitPixel);

    // --- Colors (Pure Black Fonts added) ---
    SolidBrush bBg(Color(255, 248, 250, 252)); // Very light slate
    SolidBrush bCard(Color(255, 255, 255, 255));
    SolidBrush bTextBlack(Color(255, 0, 0, 0)); // Pure Black for better readability
    SolidBrush bTextMain(Color(255, 30, 41, 59)); // Slate 900
    SolidBrush bTextMuted(Color(255, 100, 116, 139)); // Slate 500
    Pen pBrd(Color(255, 226, 232, 240), 1.0f); 

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);    fmtR.SetLineAlignment(StringAlignmentCenter);

    const float PAD = 24.0f;
    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // Layout Calculations
    float leftW = (cw - PAD * 3) * 0.65f;
    float rightW = (cw - PAD * 3) * 0.35f;
    float leftX = cx + PAD;
    float rightX = leftX + leftW + PAD;
    float tableY = cy + PAD;
    float tableH = ch - (PAD * 2);

    vector<AppActivityEntry> apps = GetTrackedApps();
    float rowH = 50.0f;
    float listY = tableY + 45.0f;
    float listH = tableH - 45.0f;

    // Boundary check for scrolling
    float maxScroll = max(0.0f, (apps.size() * rowH) - listH);
    if (stat_targetScrollY > 0.0f) stat_targetScrollY = 0.0f;
    if (stat_targetScrollY < -maxScroll) stat_targetScrollY = -maxScroll;

    // ================================================================
    //  1. LEFT COLUMN: Scrollable Data Table
    // ================================================================
    RoundRect(g, &bCard, &pBrd, leftX, tableY, leftW, tableH, 8);

    // Table Header
    SolidBrush headerBg(Color(255, 241, 245, 249));
    g.FillRectangle(&headerBg, leftX + 1.0f, tableY + 1.0f, leftW - 2.0f, 44.0f);
    g.DrawString(L"Name", -1, &fSm, RectF(leftX + 50.0f, tableY, 200.0f, 44.0f), &fmtL, &bTextMuted);
    g.DrawString(L"Active Time", -1, &fSm, RectF(leftX + leftW - 280.0f, tableY, 100.0f, 44.0f), &fmtC, &bTextMuted);
    g.DrawString(L"On-screen Time", -1, &fSm, RectF(leftX + leftW - 180.0f, tableY, 100.0f, 44.0f), &fmtC, &bTextMuted);
    g.DrawString(L"Launches", -1, &fSm, RectF(leftX + leftW - 80.0f, tableY, 60.0f, 44.0f), &fmtC, &bTextMuted);

    // --- CLIPPING REGION FOR SMOOTH SCROLLING ---
    GraphicsState gState = g.Save();
    Region clipRegion(RectF(leftX, listY, leftW, listH));
    g.SetClip(&clipRegion);

    float currentY = listY + stat_scrollY; // Apply smooth scroll offset

    for (size_t i = 0; i < apps.size(); i++) {
        // Optimization: Draw only visible rows
        if (currentY + rowH > listY && currentY < listY + listH) {
            auto& a = apps[i];
            
            // App Dot Color
            SolidBrush dotBr(a.dotColor);
            FillCircle(g, dotBr, leftX + 26.0f, currentY + rowH / 2.0f, 6.0f);

            // Row Texts (Black Fonts for strict visibility)
            g.DrawString(a.name.c_str(), -1, &fBody, RectF(leftX + 50.0f, currentY, 200.0f, rowH), &fmtL, &bTextBlack);
            g.DrawString(a.activeTime.c_str(), -1, &fBody, RectF(leftX + leftW - 280.0f, currentY, 100.0f, rowH), &fmtC, &bTextBlack);
            g.DrawString(a.onScreenTime.c_str(), -1, &fBody, RectF(leftX + leftW - 180.0f, currentY, 100.0f, rowH), &fmtC, &bTextMain);
            g.DrawString(to_wstring(a.launches).c_str(), -1, &fBody, RectF(leftX + leftW - 80.0f, currentY, 60.0f, rowH), &fmtC, &bTextMain);

            // Subtitle / Category
            g.DrawString(a.category.c_str(), -1, &fSm, RectF(leftX + 50.0f, currentY + 14.0f, 200.0f, rowH), &fmtL, &bTextMuted);

            // Row Separator Line
            g.DrawLine(&pBrd, leftX + 16.0f, currentY + rowH, leftX + leftW - 16.0f, currentY + rowH);
        }
        currentY += rowH;
    }

    g.Restore(gState); // Remove Clipping

    // --- Custom Scrollbar Indicator ---
    if (maxScroll > 0) {
        float scrollPct = abs(stat_scrollY) / maxScroll;
        float trackH = listH - 10.0f;
        float thumbH = max(30.0f, trackH * (listH / (apps.size() * rowH)));
        float thumbY = listY + 5.0f + (trackH - thumbH) * scrollPct;
        
        SolidBrush scrollBr(Color(150, 148, 163, 184)); // Slate transparent
        RoundRect(g, &scrollBr, nullptr, leftX + leftW - 8.0f, thumbY, 4.0f, thumbH, 2);
    }

    // ================================================================
    //  2. RIGHT COLUMN: Donut Chart & Summary
    // ================================================================
    RoundRect(g, &bCard, &pBrd, rightX, tableY, rightW, tableH, 8);

    // Current Date Header
    g.DrawString(L"Today", -1, &fBold, RectF(rightX + 24.0f, tableY + 16.0f, rightW - 48.0f, 20.0f), &fmtL, &bTextBlack);
    g.DrawString(L"Tuesday 12 May 2026", -1, &fSm, RectF(rightX + 24.0f, tableY + 16.0f, rightW - 48.0f, 20.0f), &fmtR, &bTextMuted);
    g.DrawLine(&pBrd, rightX, tableY + 44.0f, rightX + rightW, tableY + 44.0f);

    // Donut Chart
    float dCenX = rightX + rightW / 2.0f;
    float dCenY = tableY + 200.0f;
    float dRad  = min(110.0f, rightW * 0.35f);

    g.DrawString(L"Active Time", -1, &fBody, RectF(rightX, tableY + 60.0f, rightW, 30.0f), &fmtC, &bTextMuted);

    // Background Ring (Blue - FocusMe style)
    Pen pRingBg(Color(255, 59, 130, 246), 45.0f); 
    g.DrawEllipse(&pRingBg, dCenX - dRad, dCenY - dRad, dRad * 2, dRad * 2);
    
    // Foreground Ring (Emerald Green - Active Focus Time)
    Pen segPen(Color(255, 16, 185, 129), 45.0f); 
    float activePct = 0.11f; // Example: 11%
    g.DrawArc(&segPen, dCenX - dRad, dCenY - dRad, dRad * 2, dRad * 2, -90.0f, 360.0f * activePct * stat_animProgress); 

    // Percentage Text inside Donut
    g.DrawString(L"11%", -1, &fLg, RectF(dCenX - dRad, dCenY - 24.0f, dRad * 2, 48.0f), &fmtC, &bCard); 
    g.DrawString(L"89%", -1, &fBody, RectF(dCenX - dRad, dCenY + dRad - 15.0f, dRad * 2, 30.0f), &fmtC, &bCard); // Bottom text

    // Summary Statistics
    float sumY = dCenY + dRad + 50.0f;
    g.DrawString(L"First Activity: 12 May, 11:23:37 PM", -1, &fSm, RectF(rightX, sumY, rightW, 25.0f), &fmtC, &bTextMuted);
    g.DrawString(L"Last Activity: 12 May, 11:40:56 PM", -1, &fSm, RectF(rightX, sumY + 25.0f, rightW, 25.0f), &fmtC, &bTextMuted);
    
    g.DrawString(L"Active: 2m 32s", -1, &fBody, RectF(rightX, sumY + 70.0f, rightW, 25.0f), &fmtC, &bTextBlack);
    g.DrawString(L"On-screen: 11m 14s", -1, &fBody, RectF(rightX, sumY + 95.0f, rightW, 25.0f), &fmtC, &bTextBlack);
}

// ============================================================
//  INPUT HANDLERS
// ============================================================

// Call this from your main window's WM_MOUSEWHEEL message
void ProcessStatisticsMouseWheel(short zDelta) {
    float scrollSpeed = 60.0f; // Scroll 60 pixels per wheel tick
    
    if (zDelta > 0) {
        stat_targetScrollY += scrollSpeed; // Scroll Up
    } else {
        stat_targetScrollY -= scrollSpeed; // Scroll Down
    }
}