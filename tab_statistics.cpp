// ============================================================
//  Statistics Tab - GDI+ Graphics & Analytics Visualization
//  Sub-tabs: Today | This Week | Monthly | Alarm
//  AlarmClock-inspired dark theme for Alarm sub-tab
// ============================================================
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <ctime>
#include <cmath>

using namespace Gdiplus;
using namespace std;

// ============================================================
//  Global: Animation progress (defined here, extern'd elsewhere)
// ============================================================
float stat_animProgress = 1.0f;

// ============================================================
//  Alarm sub-tab state
// ============================================================
struct AlarmEntry {
    int  hour;
    int  minute;
    bool enabled;
    wstring label;
    wstring days;    // e.g. L"Mon Tue Wed"
};

static vector<AlarmEntry> s_alarms = {
    { 6, 30, true,  L"Morning Prayer",   L"Mon Tue Wed Thu Fri Sat Sun" },
    { 8,  0, true,  L"Study Session",    L"Mon Tue Wed Thu Fri"         },
    { 13, 0, false, L"Lunch Break",      L"Mon Tue Wed Thu Fri"         },
    { 22, 0, true,  L"Sleep Reminder",   L"Mon Tue Wed Thu Fri Sat Sun" },
};

// Hover state for alarm toggle buttons
static int  s_alarmHoverIdx  = -1;
static int  s_alarmToggleHovIdx = -1;

// ============================================================
//  Helper: RoundRect (GDI+ version — avoids WinGDI name clash)
// ============================================================
static void RoundRect(Graphics& g, Brush* brush, Pen* pen,
                      float x, float y, float w, float h, int radius)
{
    GraphicsPath path;
    float r = (float)radius;
    float d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180, 90);
    path.AddArc(x + w - d, y,         d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d,   0, 90);
    path.AddArc(x,         y + h - d, d, d,  90, 90);
    path.CloseFigure();
    if (brush) g.FillPath(brush, &path);
    if (pen)   g.DrawPath(pen,   &path);
}

// ============================================================
//  Helper: FillCircle (center + radius)
// ============================================================
static void FillCircle(Graphics& g, Brush& brush, float cx, float cy, float r)
{
    g.FillEllipse(&brush, cx - r, cy - r, r * 2.0f, r * 2.0f);
}

// ============================================================
//  Helper: Draw a toggle switch (AlarmClock style)
//  Returns the rect used (for hit-testing)
// ============================================================
static RectF DrawToggle(Graphics& g, float tx, float ty, bool on, bool hovered)
{
    float tw = 44.0f, th = 24.0f;
    // Track
    Color trackOff(255, 55, 65, 81);     // #374151 dark gray
    Color trackOn (255, 58, 235, 23);    // #3AEB17 AlarmClock green
    Color trackHov(255, 80, 200, 50);    // lighter green on hover

    Color trackColor = on ? (hovered ? trackHov : trackOn) : trackOff;
    SolidBrush trackBr(trackColor);
    RoundRect(g, &trackBr, nullptr, tx, ty, tw, th, (int)(th / 2));

    // Thumb
    float thumbR = th / 2.0f - 3.0f;
    float thumbX = on ? tx + tw - thumbR * 2.0f - 2.0f : tx + 2.0f;
    SolidBrush thumbBr(Color(255, 255, 255, 255));
    FillCircle(g, thumbBr, thumbX + thumbR, ty + th / 2.0f, thumbR);

    return RectF(tx, ty, tw, th);
}

// ============================================================
//  DrawAlarmSubTab — AlarmClock-inspired dark panel
// ============================================================
static void DrawAlarmSubTab(Graphics& g, float cx, float cy, float cw, float ch)
{
    float PAD = 20.0f;

    // ── Background panel (dark, like AlarmClock window) ──────────────────
    Color colBg    (255, 45,  45,  48);   // #2D2D30 AlarmClock bg
    Color colCard  (255, 60,  60,  65);   // slightly lighter card
    Color colBorder(255, 80,  80,  88);   // border
    Color colGreen (255, 58, 235, 23);    // #3AEB17 AlarmClock accent
    Color colGreenDim(255, 35, 140, 14);  // dimmer green for muted
    Color colWhite (255, 240, 240, 240);
    Color colGray  (255, 160, 160, 170);
    Color colRed   (255, 239, 68,  68);
    Color colOff   (255, 100, 100, 110);

    SolidBrush bBg   (colBg);
    SolidBrush bCard (colCard);
    SolidBrush bGreen(colGreen);
    SolidBrush bGreenDim(colGreenDim);
    SolidBrush bWhite(colWhite);
    SolidBrush bGray (colGray);
    SolidBrush bOff  (colOff);
    Pen pBorder(colBorder, 1.0f);
    Pen pGreen (colGreen,  1.5f);

    // Main dark bg
    RoundRect(g, &bBg, nullptr, cx + PAD, cy, cw - PAD * 2, ch - PAD, 12);

    // ── Digital Clock Display (top center) ───────────────────────────────
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t timeBuf[16], dateBuf[32];
    swprintf_s(timeBuf, L"%02d:%02d", st.wHour, st.wMinute);
    const wchar_t* dayNames[] = { L"SUN", L"MON", L"TUE", L"WED", L"THU", L"FRI", L"SAT" };
    swprintf_s(dateBuf, L"%s  %02d/%02d/%04d", dayNames[st.wDayOfWeek], st.wDay, st.wMonth, st.wYear);

    Font fDigital(L"Courier New", 36.0f, FontStyleBold, UnitPixel);
    Font fDate   (L"Segoe UI",    12.0f, FontStyleRegular, UnitPixel);
    Font fSec    (L"Courier New", 18.0f, FontStyleBold, UnitPixel);
    Font fLabel  (L"Segoe UI",    10.0f, FontStyleRegular, UnitPixel);
    Font fAlarmT (L"Courier New", 15.0f, FontStyleBold, UnitPixel);
    Font fBold   (L"Segoe UI",    11.0f, FontStyleBold, UnitPixel);
    Font fSm     (L"Segoe UI",     9.5f, FontStyleRegular, UnitPixel);
    Font fH2     (L"Segoe UI",    13.0f, FontStyleBold, UnitPixel);

    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);    fmtR.SetLineAlignment(StringAlignmentCenter);

    // Clock card
    float clockCardH = 110.0f;
    float clockCardX = cx + PAD + 16.0f;
    float clockCardW = cw - PAD * 2 - 32.0f;
    RoundRect(g, &bCard, &pBorder, clockCardX, cy + 16.0f, clockCardW, clockCardH, 10);

    // Glowing circle behind clock
    float glowCx = clockCardX + clockCardW / 2.0f;
    float glowCy = cy + 16.0f + clockCardH / 2.0f;
    for (int i = 3; i >= 0; i--) {
        float alpha = 18.0f + i * 10.0f;
        float radius = 52.0f + i * 10.0f;
        SolidBrush glowBr(Color((BYTE)alpha, 58, 235, 23));
        FillCircle(g, glowBr, glowCx, glowCy, radius);
    }

    // HH:MM in green digital font
    g.DrawString(timeBuf, -1, &fDigital,
        RectF(clockCardX, cy + 16.0f + 10.0f, clockCardW, 55.0f), &fmtC, &bGreen);

    // :SS in smaller green
    wchar_t secBuf[8];
    swprintf_s(secBuf, L":%02d", st.wSecond);
    g.DrawString(secBuf, -1, &fSec,
        RectF(clockCardX + clockCardW * 0.5f + 72.0f, cy + 16.0f + 28.0f, 60.0f, 30.0f),
        &fmtL, &bGreenDim);

    // Date row
    g.DrawString(dateBuf, -1, &fDate,
        RectF(clockCardX, cy + 16.0f + 70.0f, clockCardW, 24.0f), &fmtC, &bGray);

    // ── "ALARM CLOCK" header label (AlarmClock style) ────────────────────
    Font fTitle(L"Segoe UI", 10.0f, FontStyleBold, UnitPixel);
    SolidBrush bGreenLabel(colGreen);
    g.DrawString(L"ALARM CLOCK", -1, &fTitle,
        RectF(clockCardX, cy + 16.0f + 94.0f, clockCardW, 14.0f), &fmtC, &bGreenLabel);

    // ── Alarm list ────────────────────────────────────────────────────────
    float listY   = cy + 16.0f + clockCardH + 16.0f;
    float listX   = clockCardX;
    float listW   = clockCardW;
    float rowH    = 62.0f;
    float rowGap  =  8.0f;

    // "Alarms" section header
    g.DrawString(L"Alarms", -1, &fH2,
        RectF(listX, listY, 100.0f, 20.0f), &fmtL, &bWhite);

    // Next alarm hint
    // Find next enabled alarm
    wstring nextAlarmStr = L"No upcoming alarms";
    int nextMinutes = 9999;
    int nowMinutes  = st.wHour * 60 + st.wMinute;
    for (auto& a : s_alarms) {
        if (!a.enabled) continue;
        int am = a.hour * 60 + a.minute;
        int diff = am - nowMinutes;
        if (diff < 0) diff += 24 * 60;
        if (diff < nextMinutes) {
            nextMinutes = diff;
            wchar_t buf[64];
            int h = nextMinutes / 60, m = nextMinutes % 60;
            if (h > 0)
                swprintf_s(buf, L"Next: %s in %dh %02dm", a.label.c_str(), h, m);
            else
                swprintf_s(buf, L"Next: %s in %dm", a.label.c_str(), m);
            nextAlarmStr = buf;
        }
    }
    SolidBrush bNextHint(colGreenDim);
    g.DrawString(nextAlarmStr.c_str(), -1, &fSm,
        RectF(listX + 70.0f, listY + 3.0f, listW - 70.0f, 16.0f), &fmtR, &bNextHint);

    listY += 26.0f;

    for (int i = 0; i < (int)s_alarms.size(); i++) {
        auto& a = s_alarms[i];
        float ry = listY + i * (rowH + rowGap);
        bool  hov = (s_alarmHoverIdx == i);

        // Row card
        Color rowBg = hov ? Color(255, 72, 72, 78) : colCard;
        SolidBrush rowBr(rowBg);
        RoundRect(g, &rowBr, &pBorder, listX, ry, listW, rowH, 8);

        // Enabled: left green accent bar
        if (a.enabled) {
            SolidBrush accentBr(colGreen);
            g.FillRectangle(&accentBr, RectF(listX, ry + 8.0f, 3.0f, rowH - 16.0f));
        }

        // Time (HH:MM) — digital font, green if enabled, gray if off
        wchar_t atBuf[8];
        swprintf_s(atBuf, L"%02d:%02d", a.hour, a.minute);
        SolidBrush timeBr(a.enabled ? colGreen : colOff);
        g.DrawString(atBuf, -1, &fAlarmT,
            RectF(listX + 16.0f, ry + 8.0f, 90.0f, 28.0f), &fmtL, &timeBr);

        // AM/PM label
        wchar_t ampm[4];
        swprintf_s(ampm, a.hour < 12 ? L"AM" : L"PM");
        SolidBrush ampmBr(a.enabled ? colGreenDim : colOff);
        g.DrawString(ampm, -1, &fLabel,
            RectF(listX + 16.0f, ry + 36.0f, 40.0f, 16.0f), &fmtL, &ampmBr);

        // Divider
        Pen pDiv(Color(255, 80, 80, 88), 1.0f);
        g.DrawLine(&pDiv, listX + 110.0f, ry + 10.0f, listX + 111.0f, ry + rowH - 10.0f);

        // Label & days
        g.DrawString(a.label.c_str(), -1, &fBold,
            RectF(listX + 120.0f, ry + 10.0f, listW - 180.0f, 20.0f), &fmtL,
            a.enabled ? &bWhite : &bGray);
        g.DrawString(a.days.c_str(), -1, &fSm,
            RectF(listX + 120.0f, ry + 32.0f, listW - 180.0f, 18.0f), &fmtL, &bGray);

        // Toggle switch (right side)
        float toggleX = listX + listW - 60.0f;
        float toggleY = ry + (rowH - 24.0f) / 2.0f;
        bool togHov = (s_alarmToggleHovIdx == i);
        DrawToggle(g, toggleX, toggleY, a.enabled, togHov);
    }

    // ── Bottom tip ───────────────────────────────────────────────────────
    float tipY = listY + s_alarms.size() * (rowH + rowGap) + 6.0f;
    SolidBrush bTip(Color(255, 90, 90, 100));
    g.DrawString(L"Click toggle to enable / disable an alarm",
        -1, &fSm, RectF(listX, tipY, listW, 16.0f), &fmtC, &bTip);
}

// ============================================================
//  Alarm sub-tab hit-test (for mouse interaction)
// ============================================================
static void AlarmSubTabHover(float mx, float my, float cx, float cy, float cw, float ch)
{
    float PAD = 20.0f;
    float clockCardH = 110.0f;
    float listY = cy + 16.0f + clockCardH + 16.0f + 26.0f;
    float listX = cx + PAD + 16.0f;
    float listW = cw - PAD * 2 - 32.0f;
    float rowH  = 62.0f, rowGap = 8.0f;

    s_alarmHoverIdx     = -1;
    s_alarmToggleHovIdx = -1;

    for (int i = 0; i < (int)s_alarms.size(); i++) {
        float ry = listY + i * (rowH + rowGap);
        if (mx >= listX && mx <= listX + listW && my >= ry && my <= ry + rowH) {
            s_alarmHoverIdx = i;
            float toggleX = listX + listW - 60.0f;
            float toggleY = ry + (rowH - 24.0f) / 2.0f;
            if (mx >= toggleX && mx <= toggleX + 44.0f && my >= toggleY && my <= toggleY + 24.0f)
                s_alarmToggleHovIdx = i;
        }
    }
    (void)ch;
}

static void AlarmSubTabClick(float mx, float my, float cx, float cy, float cw, float ch)
{
    float PAD = 20.0f;
    float clockCardH = 110.0f;
    float listY = cy + 16.0f + clockCardH + 16.0f + 26.0f;
    float listX = cx + PAD + 16.0f;
    float listW = cw - PAD * 2 - 32.0f;
    float rowH  = 62.0f, rowGap = 8.0f;

    for (int i = 0; i < (int)s_alarms.size(); i++) {
        float ry = listY + i * (rowH + rowGap);
        float toggleX = listX + listW - 60.0f;
        float toggleY = ry + (rowH - 24.0f) / 2.0f;
        if (mx >= toggleX && mx <= toggleX + 44.0f && my >= toggleY && my <= toggleY + 24.0f) {
            s_alarms[i].enabled = !s_alarms[i].enabled;
            return;
        }
    }
    (void)ch;
}

// ============================================================
//  ២ This Week Tab - Weekly Trends, Top Time Wasters & Gamification
// ============================================================
void DrawWeeklyAnalytics(Graphics& g, float cx, float cY, float cw, float availH, float r1H, float r2H, float r3H, 
                        Font& fH1, Font& fH2, Font& fBody, Font& fBold, Font& fSm, Font& fMed, 
                        SolidBrush& bCard, SolidBrush& bTextMain, SolidBrush& bTextMuted, Pen& pBrd)
{
    float PAD = 24.0f;
    float r2Y = cY + PAD + r1H + PAD;
    float mcGap = 16.0f;
    float thirdW = (cw - PAD * 2 - mcGap * 2) / 3.0f;

    StringFormat fmtTrim; fmtTrim.SetAlignment(StringAlignmentNear); fmtTrim.SetTrimming(StringTrimmingEllipsisCharacter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);

    // --- Column 1: Top Time-Wasters (This Week) ---
    RoundRect(g, &bCard, &pBrd, cx + PAD, r2Y, thirdW, r2H, 8);
    g.DrawString(L"Top Time-Wasters", -1, &fH2, RectF(cx + PAD + 20.0f, r2Y + 16.0f, thirdW, 22.0f), nullptr, &bTextMain);

    struct Waster { wstring name; wstring timeStr; float pct; Color c; };
    vector<Waster> wasters = {
        { L"YouTube",   L"8h 45m", 85.0f, Color(255, 239, 68, 68) },
        { L"Facebook",  L"5h 20m", 55.0f, Color(255, 59, 130, 246) },
        { L"Instagram", L"3h 15m", 35.0f, Color(255, 217, 70, 239) },
        { L"Netflix",   L"2h 10m", 25.0f, Color(255, 0, 0, 0) }
    };

    float wRowH = (r2H - 50.0f) / 4.0f;
    float wY0 = r2Y + 50.0f;

    for (int i = 0; i < (int)wasters.size(); i++) {
        float wy = wY0 + i * wRowH;
        g.DrawString(wasters[i].name.c_str(), -1, &fBold, RectF(cx + PAD + 20.0f, wy + 8.0f, thirdW - 100.0f, 18.0f), &fmtTrim, &bTextMain);
        g.DrawString(wasters[i].timeStr.c_str(), -1, &fSm, RectF(cx + PAD + thirdW - 80.0f, wy + 8.0f, 60.0f, 18.0f), &fmtR, &bTextMuted);
        
        SolidBrush bgBr(Color(255, 241, 245, 249));
        RoundRect(g, &bgBr, nullptr, cx + PAD + 20.0f, wy + 32.0f, thirdW - 40.0f, 6.0f, 3);
        SolidBrush fillBr(wasters[i].c);
        float fillW = (thirdW - 40.0f) * (wasters[i].pct / 100.0f) * stat_animProgress;
        if(fillW > 6.0f) RoundRect(g, &fillBr, nullptr, cx + PAD + 20.0f, wy + 32.0f, fillW, 6.0f, 3);
    }

    // --- Column 2: Weekly Focus Rhythm ---
    float midX = cx + PAD + thirdW + mcGap;
    RoundRect(g, &bCard, &pBrd, midX, r2Y, thirdW, r2H, 8);
    g.DrawString(L"Focus Rhythm", -1, &fH2, RectF(midX + 20.0f, r2Y + 16.0f, thirdW, 22.0f), nullptr, &bTextMain);

    vector<wstring> days = { L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun" };
    vector<float> focusHrs = { 4.5f, 6.2f, 5.0f, 7.5f, 6.8f, 2.0f, 3.5f };
    float maxHrs = 8.0f; 
    
    float fRowH = (r2H - 50.0f) / 7.0f;
    float fY0 = r2Y + 45.0f;

    for (int i = 0; i < 7; i++) {
        float fy = fY0 + i * fRowH;
        g.DrawString(days[i].c_str(), -1, &fSm, RectF(midX + 20.0f, fy + 4.0f, 40.0f, 18.0f), nullptr, &bTextMuted);
        
        float barMaxW = thirdW - 90.0f;
        float barW = (focusHrs[i] / maxHrs) * barMaxW * stat_animProgress;
        
        SolidBrush barBg(Color(255, 241, 245, 249));
        RoundRect(g, &barBg, nullptr, midX + 60.0f, fy + 8.0f, barMaxW, 10.0f, 5);
        
        if(barW > 10.0f) {
            RectF gradRect(midX + 60.0f, fy + 8.0f, barW, 10.0f);
            LinearGradientBrush gradBr(gradRect, Color(255, 16, 185, 129), Color(255, 5, 150, 105), LinearGradientModeHorizontal);
            RoundRect(g, &gradBr, nullptr, midX + 60.0f, fy + 8.0f, barW, 10.0f, 5);
        }
    }

    // --- Column 3: Gamification & Achievements ---
    float rightX = midX + thirdW + mcGap;
    RoundRect(g, &bCard, &pBrd, rightX, r2Y, thirdW, r2H, 8);
    g.DrawString(L"Recent Achievements", -1, &fH2, RectF(rightX + 20.0f, r2Y + 16.0f, thirdW, 22.0f), nullptr, &bTextMain);

    struct Badge { wstring icon; wstring title; wstring desc; Color bgC; Color textC; };
    vector<Badge> badges = {
        { L"[Trophy]", L"Iron Will",      L"7-Day Strict Streak",  Color(255, 254, 243, 199), Color(255, 180, 83, 9)   },
        { L"[Shield]", L"Halal Warrior",  L"Blocked 100+ Ads",     Color(255, 224, 242, 254), Color(255, 3, 105, 161)  },
        { L"[Fire]",   L"Deep Work Guru", L"4h Unbroken Focus",    Color(255, 254, 226, 226), Color(255, 185, 28, 28)  }
    };

    float bRowH = (r2H - 50.0f) / 3.0f;
    float bY0 = r2Y + 45.0f;

    for (int i = 0; i < (int)badges.size(); i++) {
        float by = bY0 + i * bRowH;
        SolidBrush badgeBg(badges[i].bgC);
        FillCircle(g, badgeBg, rightX + 40.0f, by + bRowH/2.0f, 20.0f);
        SolidBrush iconBr(badges[i].textC);
        g.DrawString(badges[i].icon.c_str(), -1, &fMed, RectF(rightX + 20.0f, by + bRowH/2.0f - 14.0f, 40.0f, 28.0f), &fmtC, &iconBr);
        g.DrawString(badges[i].title.c_str(), -1, &fBold, RectF(rightX + 70.0f, by + bRowH*0.2f, thirdW - 90.0f, 20.0f), &fmtTrim, &bTextMain);
        g.DrawString(badges[i].desc.c_str(), -1, &fSm, RectF(rightX + 70.0f, by + bRowH*0.6f, thirdW - 90.0f, 16.0f), &fmtTrim, &bTextMuted);
    }
}

// ============================================================
//  ៣ Monthly Tab
// ============================================================
void DrawMonthlyAnalytics(Graphics& g, float cx, float cY, float cw, float availH, float r1H, float r2H, float r3H, 
                        Font& fH1, Font& fH2, Font& fBody, Font& fBold, Font& fSm, Font& fMed, 
                        SolidBrush& bCard, SolidBrush& bTextMain, SolidBrush& bTextMuted, Pen& pBrd)
{
    float PAD = 24.0f;
    float r2Y = cY + PAD + r1H + PAD;
    float mcGap = 16.0f;
    float halfW = (cw - PAD * 2 - mcGap) / 2.0f;
    float rightX = cx + PAD + halfW + mcGap;

    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtTrim; fmtTrim.SetAlignment(StringAlignmentNear); fmtTrim.SetTrimming(StringTrimmingEllipsisCharacter);

    // --- Left Card: Productivity Heatmap ---
    RoundRect(g, &bCard, &pBrd, cx + PAD, r2Y, halfW, r2H, 8);
    g.DrawString(L"Deep Work Heatmap (Last 30 Days)", -1, &fH2, RectF(cx + PAD + 20.0f, r2Y + 16.0f, halfW, 22.0f), nullptr, &bTextMain);

    int cols = 6, rows = 7;
    float boxSize = 18.0f, boxGap = 4.0f;
    float gridW = cols * (boxSize + boxGap);
    float startX = cx + PAD + (halfW - gridW) / 2.0f;
    float startY = r2Y + 60.0f;

    Color hColors[] = {
        Color(255, 241, 245, 249), Color(255, 167, 243, 208), Color(255, 52, 211, 153),
        Color(255, 16, 185, 129),  Color(255, 4, 120, 87)
    };
    int simData[42] = {0,1,2,3,4,0,0, 1,2,4,4,3,1,0, 2,3,3,4,4,2,0, 0,1,2,3,4,0,0, 4,4,4,3,2,0,0, 1,2,3,0,0,0,0};

    for(int c = 0; c < cols; c++) {
        for(int r = 0; r < rows; r++) {
            int idx = c * rows + r;
            if (idx >= 30) continue;
            int level = simData[idx];
            SolidBrush boxBr(hColors[level]);
            float bx = startX + c * (boxSize + boxGap);
            float by = startY + r * (boxSize + boxGap);
            float popSize = boxSize * stat_animProgress;
            float offset = (boxSize - popSize) / 2.0f;
            if(popSize > 2.0f) RoundRect(g, &boxBr, nullptr, bx + offset, by + offset, popSize, popSize, 4);
        }
    }

    // --- Right Card: Chronotype & Digital Wellbeing ---
    RoundRect(g, &bCard, &pBrd, rightX, r2Y, halfW, r2H, 8);
    g.DrawString(L"Peak Productivity & Wellbeing", -1, &fH2, RectF(rightX + 20.0f, r2Y + 16.0f, halfW, 22.0f), nullptr, &bTextMain);

    float chronoY = r2Y + 50.0f;
    SolidBrush highlightBg(Color(255, 238, 242, 255));
    SolidBrush highlightText(Color(255, 67, 56, 202));
    RoundRect(g, &highlightBg, nullptr, rightX + 20.0f, chronoY, halfW - 40.0f, 60.0f, 6);
    g.DrawString(L"[Clock] Peak Focus Time: 9:00 AM - 12:00 PM", -1, &fBold, RectF(rightX + 30.0f, chronoY + 10.0f, halfW - 60.0f, 20.0f), &fmtTrim, &highlightText);
    g.DrawString(L"Schedule your hardest tasks during this window.", -1, &fSm, RectF(rightX + 30.0f, chronoY + 32.0f, halfW - 60.0f, 18.0f), &fmtTrim, &bTextMain);

    float ergoY = chronoY + 75.0f;
    g.DrawString(L"Ergonomics Check (This Month)", -1, &fBold, RectF(rightX + 20.0f, ergoY, halfW - 40.0f, 20.0f), nullptr, &bTextMain);

    struct ErgoStat { wstring lbl; wstring val; float pct; Color c; };
    vector<ErgoStat> ergos = {
        { L"20-20-20 Rule Followed", L"68%", 68.0f, Color(255, 16, 185, 129) },
        { L"Posture Breaks Taken",   L"42%", 42.0f, Color(255, 245, 158, 11) }
    };

    float eRowH = 35.0f;
    for (int i = 0; i < (int)ergos.size(); i++) {
        float ey = ergoY + 25.0f + i * eRowH;
        g.DrawString(ergos[i].lbl.c_str(), -1, &fSm, RectF(rightX + 20.0f, ey, halfW - 100.0f, 18.0f), nullptr, &bTextMuted);
        g.DrawString(ergos[i].val.c_str(), -1, &fBold, RectF(rightX + halfW - 70.0f, ey, 50.0f, 18.0f), &fmtR, &bTextMain);
        SolidBrush bgBr(Color(255, 241, 245, 249));
        RoundRect(g, &bgBr, nullptr, rightX + 20.0f, ey + 20.0f, halfW - 40.0f, 4.0f, 2);
        SolidBrush fillBr(ergos[i].c);
        float fillW = (halfW - 40.0f) * (ergos[i].pct / 100.0f) * stat_animProgress;
        if(fillW > 4.0f) RoundRect(g, &fillBr, nullptr, rightX + 20.0f, ey + 20.0f, fillW, 4.0f, 2);
    }
}

// ============================================================
//  Tab state — 4 sub-tabs now (0=Today 1=ThisWeek 2=Monthly 3=Alarm)
// ============================================================
static int   stat_activeSubTab = 0;
static float stat_animTimer    = 0.0f;

// ============================================================
//  DrawStatisticsTab — main entry point called from main.cpp
// ============================================================
void DrawStatisticsTab(Graphics& g, float cx, float cy, float cw, float ch)
{
    stat_animProgress = min(stat_animProgress + 0.04f, 1.0f);

    float PAD    = 24.0f;
    float availH = ch - PAD * 2;
    float r1H    = 80.0f;
    float r3H    = 100.0f;
    float r2H    = availH - r1H - r3H - PAD * 2;

    // Shared light-theme brushes/fonts/pen (used by Today/Week/Monthly)
    SolidBrush bCard    (Color(255, 255, 255, 255));
    SolidBrush bTextMain(Color(255,  30,  41,  59));
    SolidBrush bTextMuted(Color(255, 100, 116, 139));
    Pen        pBrd     (Color(255, 226, 232, 240), 1.0f);

    Font fH1  (L"Segoe UI", 18.0f, FontStyleBold,    UnitPixel);
    Font fH2  (L"Segoe UI", 13.0f, FontStyleBold,    UnitPixel);
    Font fBody(L"Segoe UI", 11.0f, FontStyleRegular, UnitPixel);
    Font fBold(L"Segoe UI", 11.0f, FontStyleBold,    UnitPixel);
    Font fSm  (L"Segoe UI",  9.0f, FontStyleRegular, UnitPixel);
    Font fMed (L"Segoe UI", 10.0f, FontStyleBold,    UnitPixel);

    // ── Sub-tab selector row (4 tabs now) ────────────────────────────────
    const wchar_t* tabs[]  = { L"Today", L"This Week", L"Monthly", L"Alarm" };
    const int      nTabs   = 4;
    float tabW = 100.0f, tabH = 32.0f, tabGap = 8.0f;
    float tabsStartX = cx + PAD;

    for (int i = 0; i < nTabs; i++) {
        float tx = tabsStartX + i * (tabW + tabGap);
        bool  active = (i == stat_activeSubTab);

        // Alarm tab gets dark-theme styling even in the tab bar
        Color activeBg = (i == 3) ? Color(255, 45, 45, 48) : Color(255, 99, 102, 241);
        Color activeAccent = (i == 3) ? Color(255, 58, 235, 23) : Color(255, 255, 255, 255);

        SolidBrush tabBg(active ? activeBg : Color(255, 241, 245, 249));
        RoundRect(g, &tabBg, nullptr, tx, cy + PAD, tabW, tabH, 6);

        // Green underline for active Alarm tab
        if (active && i == 3) {
            SolidBrush greenLine(Color(255, 58, 235, 23));
            g.FillRectangle(&greenLine, RectF(tx + 8.0f, cy + PAD + tabH - 3.0f, tabW - 16.0f, 3.0f));
        }

        SolidBrush tabTxt(active ? activeAccent : Color(255, 100, 116, 139));
        StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(tabs[i], -1, &fBold, RectF(tx, cy + PAD, tabW, tabH), &fmt, &tabTxt);
    }

    // ── Dispatch ─────────────────────────────────────────────────────────
    float contentY = cy + PAD + tabH + PAD;
    float contentH = ch - (contentY - cy) - PAD;

    if (stat_activeSubTab == 1)
        DrawWeeklyAnalytics(g, cx, contentY, cw, contentH, r1H, r2H, r3H,
                            fH1, fH2, fBody, fBold, fSm, fMed,
                            bCard, bTextMain, bTextMuted, pBrd);
    else if (stat_activeSubTab == 2)
        DrawMonthlyAnalytics(g, cx, contentY, cw, contentH, r1H, r2H, r3H,
                             fH1, fH2, fBody, fBold, fSm, fMed,
                             bCard, bTextMain, bTextMuted, pBrd);
    else if (stat_activeSubTab == 3)
        DrawAlarmSubTab(g, cx, contentY, cw, contentH);
    else {
        // Today placeholder
        SolidBrush bg(Color(255, 248, 250, 252));
        g.FillRectangle(&bg, RectF(cx + PAD, contentY, cw - PAD * 2, contentH));
        StringFormat fc; fc.SetAlignment(StringAlignmentCenter); fc.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"Today's statistics coming soon", -1, &fBody,
                     RectF(cx + PAD, contentY, cw - PAD * 2, contentH), &fc, &bTextMuted);
    }
}

// ============================================================
//  Mouse interaction
// ============================================================
void ProcessStatisticsMouseMove(float mx, float my, float cx, float cy, float cw)
{
    if (stat_activeSubTab == 3) {
        float PAD = 24.0f;
        float tabH = 32.0f;
        float contentY = cy + PAD + tabH + PAD;
        float ch = 9999.0f; // not used for hover rect calc
        AlarmSubTabHover(mx, my, cx, contentY, cw, ch);
    }
    (void)mx; (void)my; (void)cx; (void)cy; (void)cw;
}

void ProcessStatisticsMouseClick(float mx, float my, float cx, float cy, float cw)
{
    float PAD         = 24.0f;
    float tabW        = 100.0f, tabH = 32.0f, tabGap = 8.0f;
    float tabsStartX  = cx + PAD;
    const int nTabs   = 4;

    // Sub-tab click
    for (int i = 0; i < nTabs; i++) {
        float tx = tabsStartX + i * (tabW + tabGap);
        float ty = cy + PAD;
        if (mx >= tx && mx <= tx + tabW && my >= ty && my <= ty + tabH) {
            stat_activeSubTab = i;
            stat_animProgress = 0.0f;
            return;
        }
    }

    // Alarm sub-tab internal clicks
    if (stat_activeSubTab == 3) {
        float contentY = cy + PAD + tabH + PAD;
        float ch = 9999.0f;
        AlarmSubTabClick(mx, my, cx, contentY, cw, ch);
    }
    (void)cw;
}
