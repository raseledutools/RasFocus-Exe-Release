#include "tab_statistics.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <cmath>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "psapi.lib")

using namespace Gdiplus;
using namespace std;

// ============================================================
//  HOVER & TAB STATES
// ============================================================
static bool stat_hovExport   = false;
static int  stat_hovTab      = -1;
static int  stat_currentTab  = 0; // 0 = Today, 1 = This Week, 2 = Month
static float stat_hovAppIdx  = -1.0f; 

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
    int     blockedCount; 
    float   barPct;       
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
//  LIVE DATA FETCHING (100% Real-time Running Processes)
// ============================================================
struct ProcessData { wstring name; SIZE_T memory; };

bool CompareProcess(const ProcessData& a, const ProcessData& b) {
    return a.memory > b.memory;
}

vector<AppEntry> GetLiveRunningApps() {
    vector<ProcessData> procList;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        // Filter out empty or basic system processes to keep it clean
                        wstring pName = pe.szExeFile;
                        if (pName != L"svchost.exe" && pName != L"conhost.exe" && pName != L"Idle") {
                            procList.push_back({ pName, pmc.WorkingSetSize });
                        }
                    }
                    CloseHandle(hProcess);
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    sort(procList.begin(), procList.end(), CompareProcess);

    vector<AppEntry> liveApps;
    Color colors[] = { Color(255, 55, 138, 221), Color(255, 99, 153, 34), Color(255, 186, 117, 23), Color(255, 180, 60, 126), Color(255, 127, 119, 221) };
    Color bgColors[] = { Color(255, 230, 241, 251), Color(255, 234, 243, 222), Color(255, 250, 238, 218), Color(255, 251, 234, 240), Color(255, 238, 237, 254) };

    SIZE_T maxMem = procList.empty() ? 1 : procList[0].memory;

    for (size_t i = 0; i < procList.size() && i < 5; i++) {
        float pct = ((float)procList[i].memory / (float)maxMem) * 100.0f;
        wstring memStr = to_wstring(procList[i].memory / (1024 * 1024)) + L" MB";
        
        wstring ctg = L"Running App";
        if (procList[i].name.find(L"chrome") != wstring::npos) ctg = L"Browser";
        else if (procList[i].name.find(L"Code") != wstring::npos) ctg = L"Development";

        liveApps.push_back({ procList[i].name, ctg, memStr, pct, colors[i], bgColors[i] });
    }
    return liveApps;
}

// ============================================================
//  HELPERS
// ============================================================
static void RoundRect(Graphics& g, SolidBrush* br, Pen* pen, float x, float y, float w, float h, int r) {
    GraphicsPath p;
    float d = r * 2.0f;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
    if (br)  g.FillPath(br, &p);
    if (pen) g.DrawPath(pen, &p);
}

static void DrawProgressBar(Graphics& g, float x, float y, float w, float pct, Color fillColor) {
    SolidBrush bgBr(Color(255, 235, 238, 242));
    SolidBrush fgBr(fillColor);
    RoundRect(g, &bgBr, nullptr, x, y, w, 5.0f, 2);
    if (pct > 0.0f) RoundRect(g, &fgBr, nullptr, x, y, w * (pct / 100.0f), 5.0f, 2);
}

static void FillCircle(Graphics& g, SolidBrush& br, float cx, float cy, float r) {
    g.FillEllipse(&br, cx - r, cy - r, r * 2.0f, r * 2.0f);
}

static void DrawVLine(Graphics& g, float x, float y1, float y2, Color c) {
    Pen p(c, 1.0f); g.DrawLine(&p, x, y1, x, y2);
}

// ============================================================
//  TAB BOUNDING BOXES
// ============================================================
struct TabRect { wstring lbl; float x; float w; };
TabRect GetTabRect(int idx, float cx, float cw) {
    if (idx == 0) return { L"Today",     cx + cw - 320.0f, 72.0f };
    if (idx == 1) return { L"This Week", cx + cw - 240.0f, 85.0f };
    if (idx == 2) return { L"Month",     cx + cw - 148.0f, 72.0f };
    return { L"", 0, 0 };
}

// ============================================================
//  MAIN DRAW
// ============================================================
void DrawStatisticsTab(Graphics& g, float cx, float cy, float cw, float ch)
{
    if (stat_firstLoad) {
        stat_animProgress += 0.04f;
        if (stat_animProgress >= 1.0f) { stat_animProgress = 1.0f; stat_firstLoad = false; }
    }

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 22, FontStyleBold, UnitPixel);
    Font fH2(&ff, 15, FontStyleBold, UnitPixel);
    Font fBody(&ff, 13, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 13, FontStyleBold, UnitPixel);
    Font fSm(&ff, 11, FontStyleRegular, UnitPixel);
    Font fMed(&ff, 18, FontStyleBold, UnitPixel);

    SolidBrush bWhite(Color(255, 255, 255, 255));
    SolidBrush bBg(Color(255, 246, 248, 250));
    SolidBrush bCard(Color(255, 255, 255, 255));
    SolidBrush bDark(Color(255, 24, 32, 48));
    SolidBrush bGray(Color(255, 110, 118, 135));
    SolidBrush bTeal(Color(255, 12, 168, 176));

    Pen pBrd(Color(255, 225, 230, 238), 1.0f);

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);    fmtR.SetLineAlignment(StringAlignmentCenter);

    const float PAD = 20.0f;

    // ================================================================
    //  1. TOP BAR
    // ================================================================
    float headerH = 60.0f;
    g.FillRectangle(&bWhite, cx, cy, cw, headerH);
    g.DrawLine(&pBrd, cx, cy + headerH, cx + cw, cy + headerH);

    g.DrawString(L"Productivity Statistics", -1, &fH1, RectF(cx + PAD, cy, cw - PAD * 2, headerH), &fmtL, &bDark);

    // Sub-Tabs Drawing
    for (int i = 0; i < 3; i++) {
        TabRect tr = GetTabRect(i, cx, cw);
        bool active = (i == stat_currentTab);
        bool hovered = (i == stat_hovTab);
        
        Color bgColor = active ? Color(255, 235, 248, 250) : (hovered ? Color(255, 245, 245, 245) : Color(255, 255, 255, 255));
        Color penColor = active ? Color(255, 12, 168, 176) : Color(255, 220, 228, 238);
        Color txtColor = active ? Color(255, 12, 130, 140) : Color(255, 90, 100, 115);

        SolidBrush tagBg(bgColor); Pen tagPen(penColor, 1.0f); SolidBrush tagTxt(txtColor);
        RoundRect(g, &tagBg, &tagPen, tr.x, cy + 17.0f, tr.w, 26.0f, 6);
        g.DrawString(tr.lbl.c_str(), -1, &fSm, RectF(tr.x, cy + 17.0f, tr.w, 26.0f), &fmtC, &tagTxt);
    }

    // Export button
    float btnX = cx + cw - 65.0f, btnY = cy + 17.0f, btnW = 45.0f, btnH = 26.0f;
    SolidBrush bExp(stat_hovExport ? Color(255, 225, 245, 248) : Color(255, 255, 255, 255));
    RoundRect(g, &bExp, &pBrd, btnX, btnY, btnW, btnH, 6);
    g.DrawString(L"\xE74E", -1, &fBody, RectF(btnX, btnY, btnW, btnH), &fmtC, &bTeal);

    // ================================================================
    //  2. DYNAMIC LAYOUT CALCULATION (Prevents cutting off)
    // ================================================================
    float cY = cy + headerH;
    g.FillRectangle(&bBg, cx, cY, cw, ch - headerH);

    float availH = ch - headerH - (PAD * 4); // 4 gaps
    if (availH < 300.0f) availH = 300.0f;    // Minimum fallback

    float mcH = 80.0f;
    float r2H = (availH - mcH) * 0.45f;
    float r3H = (availH - mcH) * 0.55f;

    // ================================================================
    //  3. METRIC SUMMARY CARDS
    // ================================================================
    float mcY = cY + PAD;
    float mcGap = 12.0f;
    float mcW = (cw - PAD * 2 - mcGap * 3) / 4.0f;

    wstring val1 = (stat_currentTab == 0) ? L"6h 42m" : (stat_currentTab == 1 ? L"42h 15m" : L"180h 30m");
    struct MetricCard { wstring title; wstring val; wstring trend; bool up; Color accent; };
    MetricCard mc[4] = {
        { L"Screen Time", val1, L"\xE74A  8% vs yesterday", true, Color(255, 12, 168, 176) },
        { L"Focus Sessions", L"7", L"\xE74A  3 more than avg", true, Color(255, 99, 153, 34) },
        { L"Sites Blocked", L"34", L"\xE74B  5% fewer blocks", false, Color(255, 186, 117, 23) },
        { L"Daily Goal", L"75%", L"\xE74A  3h / 4h done", true, Color(255, 29, 158, 117) },
    };

    for (int i = 0; i < 4; i++) {
        float mx = cx + PAD + i * (mcW + mcGap);
        RoundRect(g, &bCard, &pBrd, mx, mcY, mcW, mcH, 10);
        SolidBrush acBr(mc[i].accent);
        RoundRect(g, &acBr, nullptr, mx, mcY + 16.0f, 3.0f, mcH - 32.0f, 2);
        g.DrawString(mc[i].title.c_str(), -1, &fSm, RectF(mx + 14.0f, mcY + 10.0f, mcW - 18.0f, 18.0f), &fmtL, &bGray);
        g.DrawString(mc[i].val.c_str(), -1, &fMed, RectF(mx + 14.0f, mcY + 26.0f, mcW - 18.0f, 26.0f), &fmtL, &bDark);
        SolidBrush trBr(mc[i].up ? Color(255, 29, 158, 117) : Color(255, 200, 60, 60));
        g.DrawString(mc[i].trend.c_str(), -1, &fSm, RectF(mx + 14.0f, mcY + 54.0f, mcW - 18.0f, 18.0f), &fmtL, &trBr);
    }

    // ================================================================
    //  4. ROW 2: App Usage (LIVE DATA) + Sites Visited
    // ================================================================
    float r2Y = mcY + mcH + PAD;
    float halfW = (cw - PAD * 2 - 12.0f) / 2.0f;
    float rightX = cx + PAD + halfW + 12.0f;

    RoundRect(g, &bCard, &pBrd, cx + PAD, r2Y, halfW, r2H, 10);
    wstring liveTitle = (stat_currentTab == 0) ? L"Live Running Apps (RAM Usage)" : L"App Usage History";
    g.DrawString(liveTitle.c_str(), -1, &fH2, RectF(cx + PAD + 14.0f, r2Y + 10.0f, halfW, 22.0f), &fmtL, &bDark);

    // FETCH LIVE PROCESSES!
    vector<AppEntry> apps = GetLiveRunningApps();

    float appRowH = (r2H - 40.0f) / 5.0f;
    float appY0 = r2Y + 36.0f;
    for (int i = 0; i < (int)apps.size(); i++) {
        float ay = appY0 + i * appRowH;
        auto& a = apps[i];
        SolidBrush icBg(a.iconBg); FillCircle(g, icBg, cx + PAD + 30.0f, ay + appRowH / 2.0f, 12.0f);
        SolidBrush icFg(a.barColor); wstring initial(1, a.name[0]);
        g.DrawString(initial.c_str(), -1, &fSm, RectF(cx + PAD + 18.0f, ay, 24.0f, appRowH), &fmtC, &icFg);
        g.DrawString(a.name.c_str(), -1, &fBold, RectF(cx + PAD + 52.0f, ay + 4.0f, halfW - 120.0f, 16.0f), &fmtL, &bDark);
        g.DrawString(a.category.c_str(), -1, &fSm, RectF(cx + PAD + 52.0f, ay + 18.0f, halfW - 120.0f, 14.0f), &fmtL, &bGray);
        g.DrawString(a.timeStr.c_str(), -1, &fBold, RectF(cx + PAD + halfW - 75.0f, ay + 4.0f, 60.0f, 16.0f), &fmtR, &bDark);
        DrawProgressBar(g, cx + PAD + 52.0f, ay + appRowH - 10.0f, halfW - 75.0f, a.pct * stat_animProgress, a.barColor);
    }

    // ---- Sites Card ----
    RoundRect(g, &bCard, &pBrd, rightX, r2Y, halfW, r2H, 10);
    g.DrawString(L"Sites visited", -1, &fH2, RectF(rightX + 14.0f, r2Y + 10.0f, halfW, 22.0f), &fmtL, &bDark);

    vector<SiteEntry> sites = {
        { L"github.com", L"G", 42, 0, 100.0f, Color(255,230,241,251), Color(255,24,95,178) },
        { L"stackoverflow.com",L"S", 28, 0, 67.0f, Color(255,234,243,222), Color(255,59,109,17) },
        { L"facebook.com", L"F", 9, 6, 21.0f, Color(255,252,235,235), Color(255,163,45,45) },
    };
    float siteRowH = (r2H - 40.0f) / 3.0f;
    float siteY0 = r2Y + 36.0f;
    for (int i = 0; i < (int)sites.size(); i++) {
        float sy = siteY0 + i * siteRowH;
        SolidBrush favBg(sites[i].iconBg); SolidBrush favFg(sites[i].iconFg);
        RoundRect(g, &favBg, nullptr, rightX + 14.0f, sy + 6.0f, 22.0f, 22.0f, 4);
        g.DrawString(sites[i].initial.c_str(), -1, &fSm, RectF(rightX + 14.0f, sy + 6.0f, 22.0f, 22.0f), &fmtC, &favFg);
        g.DrawString(sites[i].name.c_str(), -1, &fBody, RectF(rightX + 44.0f, sy + 6.0f, halfW - 160.0f, 20.0f), &fmtL, &bDark);
        
        wstringstream wss; wss << sites[i].visits << L" visits";
        g.DrawString(wss.str().c_str(), -1, &fSm, RectF(rightX + halfW - 148.0f, sy + 6.0f, 60.0f, 20.0f), &fmtR, &bGray);
        DrawProgressBar(g, rightX + halfW - 80.0f, sy + 14.0f, 66.0f, sites[i].barPct * stat_animProgress, sites[i].iconFg);
    }

    // ================================================================
    //  5. ROW 3: Bar Chart + Donut
    // ================================================================
    float r3Y = r2Y + r2H + PAD;
    float barChartW = (cw - PAD * 2) * 0.63f;
    float donutW    = (cw - PAD * 2) - barChartW - 12.0f;
    float bcX = cx + PAD;
    float dcX = bcX + barChartW + 12.0f;

    RoundRect(g, &bCard, &pBrd, bcX, r3Y, barChartW, r3H, 10);
    g.DrawString(L"Focus history", -1, &fH2, RectF(bcX + 14.0f, r3Y + 10.0f, barChartW, 22.0f), &fmtL, &bDark);

    vector<float> chartData = { 2.5f, 4.0f, 3.2f, 5.8f, 4.2f, 1.5f, 3.8f };
    if(stat_currentTab == 1) chartData = { 5.5f, 6.0f, 7.2f, 4.8f, 6.2f, 2.5f, 5.8f }; // fake diff data
    vector<wstring> days = { L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun" };
    
    float barAreaH = r3H - 70.0f;
    float barAreaY0 = r3Y + 40.0f;
    float barAreaX0 = bcX + 30.0f;
    float barAreaW  = barChartW - 60.0f;
    float barSlot   = barAreaW / 7.0f;
    float barW      = 20.0f;

    for (int g2 = 0; g2 <= 3; g2++) {
        float gy = barAreaY0 + barAreaH - (g2 / 3.0f) * barAreaH;
        Pen gp(Color(60, 180, 190, 200), 1.0f); g.DrawLine(&gp, barAreaX0, gy, barAreaX0 + barAreaW, gy);
    }

    for (int i = 0; i < 7; i++) {
        float targetH = (chartData[i] / 8.0f) * barAreaH;
        float curH    = targetH * stat_animProgress;
        float bx = barAreaX0 + i * barSlot + (barSlot - barW) / 2.0f;
        float by = barAreaY0 + barAreaH - curH;

        SolidBrush barBr(Color(255, 12, 168, 176));
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
        g.DrawString(days[i].c_str(), -1, &fSm, RectF(bx - 8.0f, barAreaY0 + barAreaH + 4.0f, barW + 16.0f, 16.0f), &fmtC, &bGray);
    }

    // ---- Donut ----
    RoundRect(g, &bCard, &pBrd, dcX, r3Y, donutW, r3H, 10);
    float dCenX = dcX + donutW / 2.0f;
    float dCenY = r3Y + r3H / 2.0f - 10.0f;
    float dRad  = r3H * 0.22f;

    Pen pRingBg(Color(255, 232, 236, 242), 12.0f);
    g.DrawEllipse(&pRingBg, dCenX - dRad, dCenY - dRad, dRad * 2, dRad * 2);
    
    Pen segPen(Color(255, 99, 153, 34), 12.0f);
    g.DrawArc(&segPen, dCenX - dRad, dCenY - dRad, dRad * 2, dRad * 2, -90.0f, 270.0f * stat_animProgress);
    
    wstring pTxt = to_wstring((int)(75 * stat_animProgress)) + L"%";
    g.DrawString(pTxt.c_str(), -1, &fMed, RectF(dCenX - dRad, dCenY - 12.0f, dRad * 2, 24.0f), &fmtC, &bDark);
}

// ============================================================
//  MOUSE MOVE
// ============================================================
void ProcessStatisticsMouseMove(float x, float y, float cx, float cw, float cy)
{
    stat_hovExport  = false;
    stat_hovTab = -1;

    float btnX = cx + cw - 65.0f, btnY = cy + 17.0f, btnW = 45.0f, btnH = 26.0f;
    if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH) stat_hovExport = true;

    for (int i = 0; i < 3; i++) {
        TabRect tr = GetTabRect(i, cx, cw);
        if (x >= tr.x && x <= tr.x + tr.w && y >= cy + 17.0f && y <= cy + 17.0f + 26.0f) {
            stat_hovTab = i;
        }
    }
}

// ============================================================
//  MOUSE CLICK
// ============================================================
void ProcessStatisticsMouseClick(float x, float y, float cx, float cw, float cy)
{
    float btnX = cx + cw - 65.0f, btnY = cy + 17.0f, btnW = 45.0f, btnH = 26.0f;
    if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH) {
        MessageBoxW(NULL, L"Data Export Feature Coming Soon!", L"Export", MB_OK | MB_ICONINFORMATION);
    }

    for (int i = 0; i < 3; i++) {
        TabRect tr = GetTabRect(i, cx, cw);
        if (x >= tr.x && x <= tr.x + tr.w && y >= cy + 17.0f && y <= cy + 17.0f + 26.0f) {
            if (stat_currentTab != i) {
                stat_currentTab = i;
                stat_firstLoad = true;
                stat_animProgress = 0.0f; // Re-trigger animation on tab change
            }
        }
    }
}

// ============================================================
//  ANIMATION RESET
// ============================================================
void ResetStatisticsAnimation()
{
    stat_animProgress = 0.0f;
    stat_firstLoad    = true;
}
