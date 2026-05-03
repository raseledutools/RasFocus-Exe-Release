#include "tab_statistics.h"
#include <string>
#include <vector>

using namespace Gdiplus;
using namespace std;

// --- Hover States ---
static bool stat_hovExport = false;
static bool stat_hovRefresh = false;

// --- Animation State ---
static float stat_animProgress = 0.0f;
static bool stat_firstLoad = true;

// Helper: Rounded Rectangle
static void FillRoundRectStat(Graphics& g, SolidBrush* br, Pen* pen, float x, float y, float rw, float rh, int rad) {
    GraphicsPath p; float d = rad * 2.0f;
    p.AddArc(x, y, d, d, 180, 90); p.AddArc(x + rw - d, y, d, d, 270, 90);
    p.AddArc(x + rw - d, y + rh - d, d, d, 0, 90); p.AddArc(x, y + rh - d, d, d, 90, 90); p.CloseFigure();
    if (br) g.FillPath(br, &p);
    if (pen) g.DrawPath(pen, &p);
}

void DrawStatisticsTab(Graphics& g, float cx, float cy, float cw, float ch) {
    // --- Simple Entrance Animation Logic ---
    if (stat_firstLoad) { stat_animProgress += 0.05f; if (stat_animProgress >= 1.0f) { stat_animProgress = 1.0f; stat_firstLoad = false; } }

    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 26, FontStyleBold, UnitPixel); Font fH2(&ff, 18, FontStyleBold, UnitPixel);
    Font fSub(&ff, 14, FontStyleRegular, UnitPixel); Font fBold(&ff, 14, FontStyleBold, UnitPixel);
    Font fStatVal(&ff, 32, FontStyleBold, UnitPixel); Font fStatLbl(&ff, 13, FontStyleBold, UnitPixel);
    Font fBtn(&ff, 14, FontStyleBold, UnitPixel);
    
    FontFamily ffIc(L"Segoe MDL2 Assets");
    Font fIc(&ffIc, 22, FontStyleRegular, UnitPixel); Font fIcSmall(&ffIc, 16, FontStyleRegular, UnitPixel);

    SolidBrush bWhite(Color(255, 255, 255, 255)); SolidBrush bBg(Color(255, 248, 250, 252));
    SolidBrush bDark(Color(255, 30, 41, 59)); SolidBrush bGray(Color(255, 120, 120, 120));
    SolidBrush bTeal(Color(255, 12, 168, 176)); SolidBrush bEmerald(Color(255, 16, 185, 129));
    SolidBrush bGold(Color(255, 245, 158, 11)); SolidBrush bRose(Color(255, 244, 63, 94));
    
    Pen pBrd(Color(255, 230, 235, 240), 1.5f);
    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    // 1. Header Area
    g.FillRectangle(&bWhite, cx, cy, cw, 65.0f);
    g.DrawString(L"Productivity Statistics", -1, &fH1, RectF(cx + 40.0f, cy, cw, 65.0f), &fL, &bDark);

    // Export Button
    float btnW = 120.0f, btnH = 35.0f, btnX = cx + cw - btnW - 40.0f, btnY = cy + 15.0f;
    SolidBrush bExp(stat_hovExport ? Color(255, 235, 248, 250) : Color(255, 255, 255, 255));
    FillRoundRectStat(g, &bExp, &pBrd, btnX, btnY, btnW, btnH, 6);
    g.DrawString(L"\xE74E  Export", -1, &fBtn, RectF(btnX, btnY, btnW, btnH), &fC, &bTeal);

    // Refresh Button
    float rBtnX = btnX - 45.0f;
    SolidBrush bRef(stat_hovRefresh ? Color(255, 240, 240, 240) : Color(255, 255, 255, 255));
    FillRoundRectStat(g, &bRef, &pBrd, rBtnX, btnY, 35.0f, btnH, 6);
    g.DrawString(L"\xE72C", -1, &fIcSmall, RectF(rBtnX, btnY, 35.0f, btnH), &fC, &bDark);

    // 2. Main Background
    float cY = cy + 65.0f;
    g.FillRectangle(&bBg, cx, cY, cw, ch - 65.0f);

    // --- 3. PREMIUM SUMMARY CARDS ---
    float cardY = cY + 30.0f, cardW = (cw - 120.0f) / 3.0f, cardH = 120.0f;

    auto DrawStatCard = [&](float x, wstring ic, wstring val, wstring title, wstring trend, Color c, bool up) {
        FillRoundRectStat(g, &bWhite, &pBrd, x, cardY, cardW, cardH, 12);
        SolidBrush icBg(Color(30, c.GetR(), c.GetG(), c.GetB())); SolidBrush icFg(c);
        FillRoundRectStat(g, &icBg, NULL, x + 20.0f, cardY + 25.0f, 45.0f, 45.0f, 22);
        g.DrawString(ic.c_str(), -1, &fIc, RectF(x + 20.0f, cardY + 25.0f, 45.0f, 45.0f), &fC, &icFg);
        
        g.DrawString(title.c_str(), -1, &fStatLbl, RectF(x + 80.0f, cardY + 30.0f, cardW - 90.0f, 20.0f), &fL, &bGray);
        g.DrawString(val.c_str(), -1, &fStatVal, RectF(x + 80.0f, cardY + 50.0f, cardW - 90.0f, 35.0f), &fL, &bDark);
        
        SolidBrush trBr(up ? Color(255, 16, 185, 129) : Color(255, 244, 63, 94));
        wstring arr = up ? L"\xE74A " : L"\xE74B ";
        g.DrawString((arr + trend).c_str(), -1, &fSub, RectF(x + 20.0f, cardY + 85.0f, cardW, 20.0f), &fL, &trBr);
    };

    DrawStatCard(cx + 40.0f, L"\xE823", L"18h 45m", L"Total Focus Time", L"12% vs last week", Color(255, 12, 168, 176), true);
    DrawStatCard(cx + 40.0f + cardW + 20.0f, L"\xE734", L"24", L"Sessions Completed", L"4% vs last week", Color(255, 245, 158, 11), true);
    DrawStatCard(cx + 40.0f + (cardW * 2) + 40.0f, L"\xEA18", L"112", L"Distractions Blocked", L"2% vs last week", Color(255, 244, 63, 94), false);

    // --- 4. WEEKLY FOCUS CHART (Dynamic Bar Chart) ---
    float chY = cardY + cardH + 30.0f;
    float chW = (cw - 100.0f) * 0.65f; // 65% width
    float chH = 260.0f;
    
    FillRoundRectStat(g, &bWhite, &pBrd, cx + 40.0f, chY, chW, chH, 12);
    g.DrawString(L"Focus History (This Week)", -1, &fH2, RectF(cx + 60.0f, chY + 20.0f, chW, 25.0f), &fL, &bDark);

    // Bar Chart Data (Dummy Data for visual)
    vector<float> chartData = { 2.5f, 4.0f, 3.2f, 5.8f, 4.2f, 1.5f, 3.8f }; 
    vector<wstring> days = { L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun" };
    float maxVal = 6.0f;
    float barMaxH = 140.0f;
    float barSpacing = chW / 7.0f;
    float startX = cx + 60.0f + (barSpacing / 4.0f);

    for (int i = 0; i < 7; i++) {
        float targetH = (chartData[i] / maxVal) * barMaxH;
        float currentH = targetH * stat_animProgress; // Entrance Animation
        
        float bX = startX + (i * barSpacing);
        float bY = chY + 210.0f - currentH;
        float bW = 28.0f;

        // Draw Bar
        GraphicsPath bp; 
        bp.AddArc(bX, bY, bW, bW, 180, 90); bp.AddArc(bX, bY, bW, bW, 270, 90); // Top rounded
        bp.AddLine(bX + bW, bY + bW/2, bX + bW, bY + currentH); // Right edge
        bp.AddLine(bX + bW, bY + currentH, bX, bY + currentH); // Bottom edge
        bp.AddLine(bX, bY + currentH, bX, bY + bW/2); // Left edge
        bp.CloseFigure();

        SolidBrush barBr(i == 3 ? Color(255, 12, 168, 176) : Color(255, 210, 230, 235)); // Highlight Thursday
        g.FillPath(&barBr, &bp);

        // Day Label
        g.DrawString(days[i].c_str(), -1, &fSub, RectF(bX - 10.0f, chY + 220.0f, bW + 20.0f, 20.0f), &fC, &bGray);
    }

    // --- 5. DONUT CHART (Daily Goal) ---
    float dnX = cx + 40.0f + chW + 20.0f;
    float dnW = cw - chW - 100.0f;
    FillRoundRectStat(g, &bWhite, &pBrd, dnX, chY, dnW, chH, 12);
    g.DrawString(L"Daily Goal", -1, &fH2, RectF(dnX + 20.0f, chY + 20.0f, dnW, 25.0f), &fL, &bDark);

    float dCenX = dnX + dnW / 2.0f;
    float dCenY = chY + 140.0f;
    float dRad = 65.0f;

    // Background Ring
    Pen pRingBg(Color(255, 235, 240, 245), 14.0f);
    pRingBg.SetStartCap(LineCapRound); pRingBg.SetEndCap(LineCapRound);
    g.DrawEllipse(&pRingBg, dCenX - dRad, dCenY - dRad, dRad * 2.0f, dRad * 2.0f);

    // Foreground Animated Ring
    Pen pRingFg(Color(255, 16, 185, 129), 14.0f); // Emerald Green
    pRingFg.SetStartCap(LineCapRound); pRingFg.SetEndCap(LineCapRound);
    float targetAngle = 270.0f; // 75% complete
    float currentAngle = targetAngle * stat_animProgress;
    g.DrawArc(&pRingFg, dCenX - dRad, dCenY - dRad, dRad * 2.0f, dRad * 2.0f, -90.0f, currentAngle);

    // Text inside Donut
    Font fPer(&ff, 28, FontStyleBold, UnitPixel);
    int percent = (int)(75 * stat_animProgress);
    wstring pTxt = to_wstring(percent) + L"%";
    g.DrawString(pTxt.c_str(), -1, &fPer, RectF(dCenX - dRad, dCenY - 20.0f, dRad * 2.0f, 40.0f), &fC, &bDark);
    
    g.DrawString(L"3h / 4h Goal", -1, &fSub, RectF(dnX, dCenY + dRad + 20.0f, dnW, 20.0f), &fC, &bGray);
}

void ProcessStatisticsMouseMove(float x, float y) {
    stat_hovExport = false;
    stat_hovRefresh = false;

    // Get dynamic coordinates based on screen size (Assuming standard 800 width)
    // We'll use absolute bounding boxes for simplicity
    if (x >= 800.0f - 160.0f && x <= 800.0f - 40.0f && y >= 15.0f && y <= 50.0f) stat_hovExport = true;
    if (x >= 800.0f - 205.0f && x <= 800.0f - 170.0f && y >= 15.0f && y <= 50.0f) stat_hovRefresh = true;
}

void ProcessStatisticsMouseClick(float x, float y) {
    if (stat_hovRefresh) {
        // Re-trigger animation
        stat_animProgress = 0.0f;
        stat_firstLoad = true;
    }
    if (stat_hovExport) {
        // Future logic for exporting data
        MessageBoxW(NULL, L"Data Export Feature Coming Soon!", L"Export", MB_OK | MB_ICONINFORMATION);
    }
}