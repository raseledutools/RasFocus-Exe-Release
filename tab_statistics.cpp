#include "tab_statistics.h"
#include <string>
#include <vector>
#include <cmath>
#include <sstream>

using namespace Gdiplus;
using namespace std;

// ============================================================
//  HOVER STATES
// ============================================================
static bool stat_hovExport   = false;
static bool stat_hovRefresh  = false;
static float stat_hovAppIdx  = -1.0f; // which app row is hovered

// ============================================================
//  ANIMATION STATE
// ============================================================
static float stat_animProgress = 0.0f;
static bool  stat_firstLoad    = true;

// ============================================================
//  DATA STRUCTURES
// ============================================================
struct AppEntry {
    wstring name;
    wstring category;
    wstring timeStr;
    float   pct;       // 0-100
    Color   barColor;
    Color   iconBg;
};

struct SiteEntry {
    wstring name;
    wstring initial;
    int     visits;
    int     blockedCount; // 0 = not blocked
    float   barPct;       // 0-100
    Color   iconBg;
    Color   iconFg;
};

struct TimelineEntry {
    wstring appName;
    wstring timeRange;
    wstring duration;
    bool    isBlocked;
    Color   dotColor;
};

// ============================================================
//  HELPER: Rounded Rectangle
// ============================================================
static void RoundRect(Graphics& g, SolidBrush* br, Pen* pen,
                      float x, float y, float w, float h, int r)
{
    GraphicsPath p;
    float d = r * 2.0f;
    p.AddArc(x,         y,         d, d, 180, 90);
    p.AddArc(x + w - d, y,         d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d,   0, 90);
    p.AddArc(x,         y + h - d, d, d,  90, 90);
    p.CloseFigure();
    if (br)  g.FillPath(br,  &p);
    if (pen) g.DrawPath(pen, &p);
}

// ============================================================
//  HELPER: Thin progress bar
// ============================================================
static void DrawProgressBar(Graphics& g, float x, float y, float w, float pct, Color fillColor)
{
    SolidBrush bgBr(Color(255, 235, 238, 242));
    SolidBrush fgBr(fillColor);
    RoundRect(g, &bgBr, nullptr, x, y, w, 5.0f, 2);
    if (pct > 0.0f)
        RoundRect(g, &fgBr, nullptr, x, y, w * (pct / 100.0f), 5.0f, 2);
}

// ============================================================
//  HELPER: Circle / dot
// ============================================================
static void FillCircle(Graphics& g, SolidBrush& br, float cx, float cy, float r)
{
    g.FillEllipse(&br, cx - r, cy - r, r * 2.0f, r * 2.0f);
}

// ============================================================
//  HELPER: Vertical line segment
// ============================================================
static void DrawVLine(Graphics& g, float x, float y1, float y2, Color c)
{
    Pen p(c, 1.0f);
    g.DrawLine(&p, x, y1, x, y2);
}

// ============================================================
//  MAIN DRAW
// ============================================================
void DrawStatisticsTab(Graphics& g, float cx, float cy, float cw, float ch)
{
    // --- Entrance animation ---
    if (stat_firstLoad) {
        stat_animProgress += 0.04f;
        if (stat_animProgress >= 1.0f) { stat_animProgress = 1.0f; stat_firstLoad = false; }
    }

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // ---- Fonts ----
    FontFamily ff(L"Segoe UI");
    Font fH1  (&ff, 22, FontStyleBold,    UnitPixel);
    Font fH2  (&ff, 15, FontStyleBold,    UnitPixel);
    Font fBody(&ff, 13, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 13, FontStyleBold,    UnitPixel);
    Font fSm  (&ff, 11, FontStyleRegular, UnitPixel);
    Font fBig (&ff, 26, FontStyleBold,    UnitPixel);
    Font fMed (&ff, 18, FontStyleBold,    UnitPixel);

    FontFamily ffIc(L"Segoe MDL2 Assets");
    Font fIc    (&ffIc, 18, FontStyleRegular, UnitPixel);
    Font fIcSm  (&ffIc, 14, FontStyleRegular, UnitPixel);

    // ---- Brushes ----
    SolidBrush bWhite (Color(255, 255, 255, 255));
    SolidBrush bBg    (Color(255, 246, 248, 250));
    SolidBrush bCard  (Color(255, 255, 255, 255));
    SolidBrush bDark  (Color(255,  24,  32,  48));
    SolidBrush bGray  (Color(255, 110, 118, 135));
    SolidBrush bLtGray(Color(255, 200, 208, 220));
    SolidBrush bGreen (Color(255,  29, 158, 117));
    SolidBrush bRed   (Color(255, 200,  60,  60));
    SolidBrush bTeal  (Color(255,  12, 168, 176));

    // ---- Pens ----
    Pen pBrd  (Color(255, 225, 230, 238), 1.0f);
    Pen pBrdHv(Color(255, 180, 195, 215), 1.0f);

    // ---- String Formats ----
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);    fmtR.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtLT; fmtLT.SetAlignment(StringAlignmentNear); fmtLT.SetLineAlignment(StringAlignmentNear);

    const float PAD = 24.0f;

    // ================================================================
    //  1.  TOP BAR
    // ================================================================
    float headerH = 60.0f;
    g.FillRectangle(&bWhite, cx, cy, cw, headerH);
    Pen pHdrLine(Color(255, 225, 230, 238), 1.0f);
    g.DrawLine(&pHdrLine, cx, cy + headerH, cx + cw, cy + headerH);

    g.DrawString(L"Productivity Statistics", -1, &fH1,
        RectF(cx + PAD, cy, cw - PAD * 2, headerH), &fmtL, &bDark);

    // Range Tags (Today / This Week / Month)
    struct { wstring lbl; float x; } tabs[] = {
        { L"Today",     cx + cw - 380.0f },
        { L"This Week", cx + cw - 300.0f },
        { L"Month",     cx + cw - 200.0f },
    };
    for (int i = 0; i < 3; i++) {
        bool active = (i == 0);
        SolidBrush tagBg(active ? Color(255, 235, 248, 250) : Color(255, 255, 255, 255));
        Pen tagPen(active ? Color(255, 12, 168, 176) : Color(255, 220, 228, 238), 1.0f);
        RoundRect(g, &tagBg, &tagPen, tabs[i].x, cy + 17.0f, 72.0f, 26.0f, 6);
        SolidBrush tagTxt(active ? Color(255, 12, 130, 140) : Color(255, 90, 100, 115));
        g.DrawString(tabs[i].lbl.c_str(), -1, &fSm,
            RectF(tabs[i].x, cy + 17.0f, 72.0f, 26.0f), &fmtC, &tagTxt);
    }

    // Export button
    float btnX = cx + cw - 120.0f, btnY = cy + 14.0f, btnW = 95.0f, btnH = 32.0f;
    SolidBrush bExp(stat_hovExport ? Color(255, 225, 245, 248) : Color(255, 255, 255, 255));
    RoundRect(g, &bExp, &pBrd, btnX, btnY, btnW, btnH, 6);
    g.DrawString(L"\xE74E  Export", -1, &fBody,
        RectF(btnX, btnY, btnW, btnH), &fmtC, &bTeal);

    // ================================================================
    //  2.  CONTENT BACKGROUND
    // ================================================================
    float cY = cy + headerH;
    g.FillRectangle(&bBg, cx, cY, cw, ch - headerH);

    // ================================================================
    //  3.  METRIC SUMMARY CARDS  (4 across)
    // ================================================================
    float mcY  = cY + PAD;
    float mcH  = 88.0f;
    float mcGap = 12.0f;
    float mcW  = (cw - PAD * 2 - mcGap * 3) / 4.0f;

    struct MetricCard { wstring title; wstring val; wstring trend; bool up; Color accent; };
    MetricCard mc[4] = {
        { L"Screen Time",     L"6h 42m",  L"\xE74A  8% vs yesterday",  true,  Color(255, 12, 168, 176) },
        { L"Focus Sessions",  L"7",        L"\xE74A  3 more than avg",   true,  Color(255, 99, 153, 34)  },
        { L"Sites Blocked",   L"34",       L"\xE74B  5% fewer blocks",   false, Color(255, 186, 117, 23) },
        { L"Daily Goal",      L"75%",      L"\xE74A  3h / 4h done",      true,  Color(255, 29, 158, 117) },
    };

    for (int i = 0; i < 4; i++) {
        float mx = cx + PAD + i * (mcW + mcGap);
        RoundRect(g, &bCard, &pBrd, mx, mcY, mcW, mcH, 10);

        // Accent left bar
        SolidBrush acBr(mc[i].accent);
        RoundRect(g, &acBr, nullptr, mx, mcY + 18.0f, 3.0f, mcH - 36.0f, 2);

        g.DrawString(mc[i].title.c_str(), -1, &fSm,
            RectF(mx + 14.0f, mcY + 14.0f, mcW - 18.0f, 18.0f), &fmtL, &bGray);
        g.DrawString(mc[i].val.c_str(), -1, &fMed,
            RectF(mx + 14.0f, mcY + 30.0f, mcW - 18.0f, 30.0f), &fmtL, &bDark);

        SolidBrush trBr(mc[i].up ? Color(255, 29, 158, 117) : Color(255, 200, 60, 60));
        g.DrawString(mc[i].trend.c_str(), -1, &fSm,
            RectF(mx + 14.0f, mcY + 62.0f, mcW - 18.0f, 18.0f), &fmtL, &trBr);
    }

    // ================================================================
    //  4.  ROW 2:  App Usage (left)  +  Sites Visited (right)
    // ================================================================
    float r2Y  = mcY + mcH + PAD;
    float r2H  = 270.0f;
    float halfW = (cw - PAD * 2 - 12.0f) / 2.0f;
    float leftX  = cx + PAD;
    float rightX = cx + PAD + halfW + 12.0f;

    // ---- App Usage Card ----
    RoundRect(g, &bCard, &pBrd, leftX, r2Y, halfW, r2H, 10);
    g.DrawString(L"App usage today", -1, &fH2,
        RectF(leftX + 14.0f, r2Y + 14.0f, halfW, 22.0f), &fmtL, &bDark);

    vector<AppEntry> apps = {
        { L"Google Chrome",  L"Browser",      L"2h 18m", 85.0f, Color(255, 55, 138, 221), Color(255, 230, 241, 251) },
        { L"VS Code",        L"Development",  L"1h 42m", 60.0f, Color(255, 99, 153, 34),  Color(255, 234, 243, 222) },
        { L"Slack",          L"Communication",L"55m",    38.0f, Color(255, 186, 117, 23), Color(255, 250, 238, 218) },
        { L"Notion",         L"Productivity", L"38m",    28.0f, Color(255, 180, 60, 126), Color(255, 251, 234, 240) },
        { L"YouTube",        L"Entertainment",L"24m",    18.0f, Color(255, 127, 119, 221),Color(255, 238, 237, 254) },
    };

    float appRowH = 40.0f;
    float appY0   = r2Y + 44.0f;
    for (int i = 0; i < (int)apps.size(); i++) {
        float ay = appY0 + i * appRowH;
        auto& a  = apps[i];

        // icon circle
        SolidBrush icBg(a.iconBg);
        FillCircle(g, icBg, leftX + 30.0f, ay + appRowH / 2.0f, 14.0f);
        SolidBrush icFg(a.barColor);
        // first letter as placeholder icon
        wstring initial(1, a.name[0]);
        g.DrawString(initial.c_str(), -1, &fSm,
            RectF(leftX + 16.0f, ay + 4.0f, 28.0f, appRowH - 8.0f), &fmtC, &icFg);

        // name + category
        g.DrawString(a.name.c_str(), -1, &fBold,
            RectF(leftX + 52.0f, ay + 4.0f, halfW - 120.0f, 18.0f), &fmtL, &bDark);
        g.DrawString(a.category.c_str(), -1, &fSm,
            RectF(leftX + 52.0f, ay + 20.0f, halfW - 120.0f, 14.0f), &fmtL, &bGray);

        // time right-aligned
        g.DrawString(a.timeStr.c_str(), -1, &fBold,
            RectF(leftX + halfW - 70.0f, ay + 4.0f, 56.0f, 18.0f), &fmtR, &bDark);

        // progress bar animated
        float animPct = a.pct * stat_animProgress;
        DrawProgressBar(g, leftX + 52.0f, ay + appRowH - 8.0f, halfW - 70.0f, animPct, a.barColor);
    }

    // ---- Sites Visited Card ----
    RoundRect(g, &bCard, &pBrd, rightX, r2Y, halfW, r2H, 10);
    g.DrawString(L"Sites visited today", -1, &fH2,
        RectF(rightX + 14.0f, r2Y + 14.0f, halfW, 22.0f), &fmtL, &bDark);

    vector<SiteEntry> sites = {
        { L"github.com",       L"G", 42, 0,  100.0f, Color(255,230,241,251), Color(255,24, 95,178)  },
        { L"stackoverflow.com",L"S", 28, 0,   67.0f, Color(255,234,243,222), Color(255,59,109,17)  },
        { L"google.com",       L"G", 21, 0,   50.0f, Color(255,250,238,218), Color(255,133,79,11)  },
        { L"notion.so",        L"N", 17, 0,   40.0f, Color(255,238,237,254), Color(255,83, 74,183) },
        { L"facebook.com",     L"F",  9, 6,   21.0f, Color(255,252,235,235), Color(255,163,45, 45) },
        { L"twitter.com",      L"T",  5,12,   12.0f, Color(255,252,235,235), Color(255,163,45, 45) },
        { L"youtube.com",      L"Y",  3,16,    7.0f, Color(255,252,235,235), Color(255,163,45, 45) },
    };

    float siteRowH = 32.0f;
    float siteY0   = r2Y + 44.0f;
    for (int i = 0; i < (int)sites.size(); i++) {
        float sy = siteY0 + i * siteRowH;
        auto& s  = sites[i];

        // favicon square
        SolidBrush favBg(s.iconBg);
        SolidBrush favFg(s.iconFg);
        RoundRect(g, &favBg, nullptr, rightX + 14.0f, sy + 6.0f, 22.0f, 22.0f, 4);
        g.DrawString(s.initial.c_str(), -1, &fSm,
            RectF(rightX + 14.0f, sy + 6.0f, 22.0f, 22.0f), &fmtC, &favFg);

        // site name
        g.DrawString(s.name.c_str(), -1, &fBody,
            RectF(rightX + 44.0f, sy + 6.0f, halfW - 160.0f, 20.0f), &fmtL, &bDark);

        // visits
        wstringstream wss; wss << s.visits << L" visits";
        g.DrawString(wss.str().c_str(), -1, &fSm,
            RectF(rightX + halfW - 148.0f, sy + 6.0f, 60.0f, 20.0f), &fmtR, &bGray);

        if (s.blockedCount > 0) {
            // blocked pill
            SolidBrush pillBg(Color(255, 252, 235, 235));
            Pen pillPen(Color(255, 220, 160, 160), 1.0f);
            float px = rightX + halfW - 82.0f;
            RoundRect(g, &pillBg, &pillPen, px, sy + 7.0f, 72.0f, 18.0f, 9);
            wstringstream blk; blk << L"blocked " << s.blockedCount << L"\xD7";
            SolidBrush pillTxt(Color(255, 163, 45, 45));
            g.DrawString(blk.str().c_str(), -1, &fSm,
                RectF(px, sy + 7.0f, 72.0f, 18.0f), &fmtC, &pillTxt);
        } else {
            // mini bar
            float barX = rightX + halfW - 80.0f;
            DrawProgressBar(g, barX, sy + 14.0f, 66.0f, s.barPct * stat_animProgress, s.iconFg);
        }
    }

    // ================================================================
    //  5.  ROW 3:  Bar Chart (65%)  +  Donut (35%)
    // ================================================================
    float r3Y = r2Y + r2H + PAD;
    float r3H = 230.0f;
    float barChartW = (cw - PAD * 2) * 0.63f;
    float donutW    = (cw - PAD * 2) - barChartW - 12.0f;
    float bcX = cx + PAD;
    float dcX = bcX + barChartW + 12.0f;

    // ---- Bar Chart Card ----
    RoundRect(g, &bCard, &pBrd, bcX, r3Y, barChartW, r3H, 10);
    g.DrawString(L"Focus history \x2014 this week", -1, &fH2,
        RectF(bcX + 14.0f, r3Y + 14.0f, barChartW, 22.0f), &fmtL, &bDark);

    vector<float>   chartData = { 2.5f, 4.0f, 3.2f, 5.8f, 4.2f, 1.5f, 3.8f };
    vector<wstring> days      = { L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun" };
    float maxVal    = 7.0f;
    float barAreaH  = 140.0f;
    float barAreaY0 = r3Y + 45.0f;
    float barAreaX0 = bcX + 30.0f;
    float barAreaW  = barChartW - 60.0f;
    float barSlot   = barAreaW / 7.0f;
    float barW      = 24.0f;

    // Y grid lines
    for (int g2 = 0; g2 <= 3; g2++) {
        float gy = barAreaY0 + barAreaH - (g2 / 3.0f) * barAreaH;
        Pen gp(Color(60, 180, 190, 200), 1.0f);
        g.DrawLine(&gp, barAreaX0, gy, barAreaX0 + barAreaW, gy);
        if (g2 > 0) {
            wstring gl = to_wstring(g2 * 2) + L"h";
            g.DrawString(gl.c_str(), -1, &fSm,
                RectF(bcX + 4.0f, gy - 8.0f, 22.0f, 16.0f), &fmtC, &bGray);
        }
    }

    for (int i = 0; i < 7; i++) {
        float targetH = (chartData[i] / maxVal) * barAreaH;
        float curH    = targetH * stat_animProgress;
        float bx = barAreaX0 + i * barSlot + (barSlot - barW) / 2.0f;
        float by = barAreaY0 + barAreaH - curH;

        bool isToday = (i == 3); // Thursday highlighted
        SolidBrush barBr(isToday
            ? Color(255, 12, 168, 176)
            : Color(255, 192, 221, 151));

        // Rounded-top bar
        if (curH > barW) {
            GraphicsPath bp;
            bp.AddArc(bx, by, barW, barW, 180, 90);
            bp.AddArc(bx, by, barW, barW, 270, 90);
            bp.AddLine(bx + barW, by + barW / 2, bx + barW, by + curH);
            bp.AddLine(bx + barW, by + curH, bx, by + curH);
            bp.AddLine(bx, by + curH, bx, by + barW / 2);
            bp.CloseFigure();
            g.FillPath(&barBr, &bp);
        } else if (curH > 0) {
            g.FillRectangle(&barBr, bx, by, barW, curH);
        }

        // Day label
        g.DrawString(days[i].c_str(), -1, &fSm,
            RectF(bx - 8.0f, barAreaY0 + barAreaH + 6.0f, barW + 16.0f, 16.0f), &fmtC, &bGray);

        // Value label on top when animated
        if (stat_animProgress >= 1.0f) {
            wstring val = to_wstring((int)chartData[i]) + L"h";
            SolidBrush valBr(isToday ? Color(255, 12, 130, 145) : Color(255, 110, 145, 100));
            g.DrawString(val.c_str(), -1, &fSm,
                RectF(bx - 6.0f, by - 16.0f, barW + 12.0f, 14.0f), &fmtC, &valBr);
        }
    }

    // ---- Donut / Category Card ----
    RoundRect(g, &bCard, &pBrd, dcX, r3Y, donutW, r3H, 10);
    g.DrawString(L"Time by category", -1, &fH2,
        RectF(dcX + 14.0f, r3Y + 14.0f, donutW, 22.0f), &fmtL, &bDark);

    float dCenX = dcX + donutW / 2.0f;
    float dCenY = r3Y + 105.0f;
    float dRad  = 55.0f;
    float dThick= 14.0f;

    // Background ring
    Pen pRingBg(Color(255, 232, 236, 242), dThick);
    pRingBg.SetStartCap(LineCapRound); pRingBg.SetEndCap(LineCapRound);
    g.DrawEllipse(&pRingBg, dCenX - dRad, dCenY - dRad, dRad * 2, dRad * 2);

    // Segments: Dev 38%, Web 28%, Chat 16%, Media 10%, Other 8%
    struct Seg { float pct; Color c; wstring lbl; };
    vector<Seg> segs = {
        { 38.0f, Color(255, 99, 153, 34),   L"Dev 38%"   },
        { 28.0f, Color(255, 55, 138, 221),  L"Web 28%"   },
        { 16.0f, Color(255, 186, 117, 23),  L"Chat 16%"  },
        { 10.0f, Color(255, 127, 119, 221), L"Media 10%" },
        {  8.0f, Color(255, 136, 135, 128), L"Other 8%"  },
    };

    float startAngle = -90.0f;
    for (auto& s : segs) {
        float sweep = (s.pct / 100.0f) * 360.0f * stat_animProgress;
        Pen segPen(s.c, dThick);
        segPen.SetStartCap(LineCapFlat); segPen.SetEndCap(LineCapFlat);
        g.DrawArc(&segPen, dCenX - dRad, dCenY - dRad, dRad * 2, dRad * 2, startAngle, sweep - 1.0f);
        startAngle += s.pct / 100.0f * 360.0f;
    }

    // Centre text
    int pct = (int)(75 * stat_animProgress);
    wstring pTxt = to_wstring(pct) + L"%";
    g.DrawString(pTxt.c_str(), -1, &fMed,
        RectF(dCenX - dRad, dCenY - 16.0f, dRad * 2, 32.0f), &fmtC, &bDark);
    g.DrawString(L"goal", -1, &fSm,
        RectF(dCenX - dRad, dCenY + 10.0f, dRad * 2, 16.0f), &fmtC, &bGray);

    // Legend (2 columns)
    float lgX1 = dcX + 10.0f, lgX2 = dcX + donutW / 2.0f + 4.0f;
    float lgY0 = r3Y + 170.0f;
    for (int i = 0; i < (int)segs.size(); i++) {
        float lx = (i % 2 == 0) ? lgX1 : lgX2;
        float ly = lgY0 + (i / 2) * 20.0f;
        SolidBrush dotBr(segs[i].c);
        FillCircle(g, dotBr, lx + 5.0f, ly + 7.0f, 4.0f);
        g.DrawString(segs[i].lbl.c_str(), -1, &fSm,
            RectF(lx + 14.0f, ly, donutW / 2.0f - 16.0f, 16.0f), &fmtL, &bGray);
    }

    // ================================================================
    //  6.  ROW 4:  Activity Timeline
    // ================================================================
    float r4Y = r3Y + r3H + PAD;
    float r4H = 220.0f;
    RoundRect(g, &bCard, &pBrd, cx + PAD, r4Y, cw - PAD * 2, r4H, 10);
    g.DrawString(L"Activity timeline \x2014 today", -1, &fH2,
        RectF(cx + PAD + 14.0f, r4Y + 14.0f, cw, 22.0f), &fmtL, &bDark);

    vector<TimelineEntry> tlLeft = {
        { L"VS Code",                  L"9:00 \x2013 10:30 AM",  L"1h 30m", false, Color(255, 99, 153, 34)   },
        { L"Slack",                    L"10:30 \x2013 10:55 AM", L"25m",    false, Color(255, 186, 117, 23)  },
        { L"Chrome \x2014 github.com", L"11:00 AM \x2013 12:30", L"1h 30m", false, Color(255, 55, 138, 221)  },
        { L"youtube.com [blocked]",    L"12:15 PM (attempt)",    L"\x2014",  true,  Color(255, 200, 60, 60)   },
    };
    vector<TimelineEntry> tlRight = {
        { L"Notion",                      L"1:00 \x2013 1:38 PM",   L"38m",    false, Color(255, 180, 60, 126)  },
        { L"VS Code",                     L"2:00 \x2013 4:00 PM",   L"2h 00m", false, Color(255, 99, 153, 34)   },
        { L"twitter.com [blocked \xD75]", L"3:10 \x2013 4:50 PM",   L"\x2014",  true,  Color(255, 200, 60, 60)   },
        { L"Chrome \x2014 stackoverflow", L"4:00 \x2013 4:45 PM",   L"45m",    false, Color(255, 55, 138, 221)  },
    };

    auto DrawTimeline = [&](vector<TimelineEntry>& tl, float tlX, float tlW) {
        float tyY0 = r4Y + 44.0f;
        float rowH = 40.0f;
        for (int i = 0; i < (int)tl.size(); i++) {
            auto& e = tl[i];
            float ty = tyY0 + i * rowH;

            // Dot
            SolidBrush dotBr(e.dotColor);
            FillCircle(g, dotBr, tlX + 8.0f, ty + 12.0f, 5.0f);

            // Connecting line
            if (i < (int)tl.size() - 1) {
                DrawVLine(g, tlX + 8.0f, ty + 18.0f, ty + rowH, Color(180, 200, 210, 220));
            }

            // App name
            SolidBrush nameBr(e.isBlocked ? Color(255, 163, 45, 45) : Color(255, 24, 32, 48));
            g.DrawString(e.appName.c_str(), -1, &fBold,
                RectF(tlX + 20.0f, ty + 2.0f, tlW - 100.0f, 18.0f), &fmtL, &nameBr);

            // Time range
            g.DrawString(e.timeRange.c_str(), -1, &fSm,
                RectF(tlX + 20.0f, ty + 20.0f, tlW - 100.0f, 14.0f), &fmtL, &bGray);

            // Duration right-aligned
            SolidBrush durBr(e.isBlocked ? Color(255, 163, 45, 45) : Color(255, 24, 32, 48));
            g.DrawString(e.duration.c_str(), -1, &fBody,
                RectF(tlX + tlW - 55.0f, ty + 6.0f, 50.0f, 18.0f), &fmtR, &durBr);
        }
    };

    float tlW = (cw - PAD * 2 - 28.0f) / 2.0f;
    DrawTimeline(tlLeft,  cx + PAD + 14.0f,           tlW);
    DrawTimeline(tlRight, cx + PAD + 14.0f + tlW + 14.0f, tlW);

    // Vertical divider between the two timeline columns
    Pen divPen(Color(100, 210, 218, 228), 1.0f);
    g.DrawLine(&divPen,
        cx + PAD + 14.0f + tlW + 7.0f, r4Y + 44.0f,
        cx + PAD + 14.0f + tlW + 7.0f, r4Y + r4H - 14.0f);
}

// ============================================================
//  MOUSE MOVE
// ============================================================
void ProcessStatisticsMouseMove(float x, float y, float cx, float cw, float cy)
{
    stat_hovExport  = false;
    stat_hovRefresh = false;

    float btnX = cx + cw - 120.0f, btnY = cy + 14.0f, btnW = 95.0f, btnH = 32.0f;
    if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH)
        stat_hovExport = true;
}

// ============================================================
//  MOUSE CLICK
// ============================================================
void ProcessStatisticsMouseClick(float x, float y, float cx, float cw, float cy)
{
    float btnX = cx + cw - 120.0f, btnY = cy + 14.0f, btnW = 95.0f, btnH = 32.0f;

    if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH) {
        MessageBoxW(NULL, L"Data Export Feature Coming Soon!", L"Export",
                    MB_OK | MB_ICONINFORMATION);
    }
}

// ============================================================
//  ANIMATION RESET  (call on tab switch)
// ============================================================
void ResetStatisticsAnimation()
{
    stat_animProgress = 0.0f;
    stat_firstLoad    = true;
}
