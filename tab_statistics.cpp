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
//  HOVER & TAB STATES
// ============================================================
static bool stat_hovExport   = false;
static int  stat_hovTab      = -1;
static int  stat_currentTab  = 0; // 0 = Live, 1 = Weekly, 2 = Monthly
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
    wstring memStr;
    float   pct;       
    Color   barColorStart;
    Color   barColorEnd;
    Color   iconBg;
};

// ============================================================
//  100% REAL-TIME LIVE DATA FETCHING (FOCUS APP LOGIC)
// ============================================================
struct ProcessData { wstring name; SIZE_T memory; };

bool CompareProcess(const ProcessData& a, const ProcessData& b) {
    return a.memory > b.memory; // Sort by memory descending
}

vector<AppEntry> GetLiveRunningApps(int& totalProcesses) {
    vector<ProcessData> procList;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    totalProcesses = 0;

    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                totalProcesses++;
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        wstring pName = pe.szExeFile;
                        // Filter out empty or basic system processes
                        if (pName != L"svchost.exe" && pName != L"conhost.exe" && pName != L"Idle" && pName != L"System" && pName != L"Registry") {
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
    
    // Premium Gradients for Apps
    Color cStart[] = { Color(255, 99, 102, 241), Color(255, 16, 185, 129), Color(255, 245, 158, 11), Color(255, 236, 72, 153), Color(255, 14, 165, 233) };
    Color cEnd[]   = { Color(255, 67, 56, 202),  Color(255, 5, 150, 105),  Color(255, 217, 119, 6),  Color(255, 219, 39, 119), Color(255, 2, 132, 199) };
    Color cBg[]    = { Color(255, 238, 242, 255), Color(255, 209, 250, 229), Color(255, 254, 243, 199), Color(255, 252, 231, 243), Color(255, 224, 242, 254) };

    SIZE_T maxMem = procList.empty() ? 1 : procList[0].memory;

    for (size_t i = 0; i < procList.size() && i < 5; i++) {
        float pct = ((float)procList[i].memory / (float)maxMem) * 100.0f;
        float memMB = (float)procList[i].memory / (1024.0f * 1024.0f);
        
        wstringstream ss; 
        ss << fixed << setprecision(1) << memMB << L" MB";
        
        // --- Focus App Categorization Logic ---
        wstring ctg = L"Neutral / System";
        wstring lowerName = procList[i].name;
        transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);

        if (lowerName.find(L"code") != wstring::npos || lowerName.find(L"devenv") != wstring::npos || lowerName.find(L"idea") != wstring::npos) 
            ctg = L"Productive (Dev)";
        else if (lowerName.find(L"chrome") != wstring::npos || lowerName.find(L"edge") != wstring::npos || lowerName.find(L"firefox") != wstring::npos) 
            ctg = L"Distracting (Web)";
        else if (lowerName.find(L"discord") != wstring::npos || lowerName.find(L"slack") != wstring::npos || lowerName.find(L"telegram") != wstring::npos) 
            ctg = L"Communication";
        else if (lowerName.find(L"rasfocus") != wstring::npos) 
            ctg = L"Focus System Core";

        liveApps.push_back({ procList[i].name, ctg, ss.str(), pct, cStart[i % 5], cEnd[i % 5], cBg[i % 5] });
    }
    return liveApps;
}

// Live Uptime
wstring GetSystemUptimeStr() {
    ULONGLONG uptimeMs = GetTickCount64();
    int hours = (uptimeMs / 3600000);
    int mins = (uptimeMs / 60000) % 60;
    wstringstream ss;
    if (hours > 0) ss << hours << L"h ";
    ss << mins << L"m";
    return ss.str();
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

static void DrawGradientProgressBar(Graphics& g, float x, float y, float w, float h, float pct, Color cStart, Color cEnd) {
    SolidBrush bgBr(Color(255, 241, 245, 249)); // Slate 100
    RoundRect(g, &bgBr, nullptr, x, y, w, h, h/2);
    
    if (pct > 0.0f) {
        float fW = w * (pct / 100.0f);
        if(fW < h) fW = h; 
        RectF gradRect(x, y, fW, h);
        LinearGradientBrush gradBr(gradRect, cStart, cEnd, LinearGradientModeHorizontal);
        RoundRect(g, &gradBr, nullptr, x, y, fW, h, h/2);
    }
}

static void FillCircle(Graphics& g, SolidBrush& br, float cx, float cy, float r) {
    g.FillEllipse(&br, cx - r, cy - r, r * 2.0f, r * 2.0f);
}

// ============================================================
//  TAB BOUNDING BOXES
// ============================================================
struct TabRect { wstring lbl; float x; float w; };
TabRect GetTabRect(int idx, float cx, float cw) {
    float startX = cx + cw - 330.0f;
    if (idx == 0) return { L"Today",     startX, 80.0f };
    if (idx == 1) return { L"This Week", startX + 90.0f, 90.0f };
    if (idx == 2) return { L"Monthly",   startX + 190.0f, 80.0f };
    return { L"", 0, 0 };
}

// ============================================================
//  MAIN DRAW (Cold Turkey / Hardcore Layout)
// ============================================================
void DrawStatisticsTab(Graphics& g, float cx, float cy, float cw, float ch)
{
    if (stat_firstLoad) {
        stat_animProgress += 0.04f; 
        if (stat_animProgress >= 1.0f) { stat_animProgress = 1.0f; stat_firstLoad = false; }
    }

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // --- Hardcore Typography ---
    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 26, FontStyleBold, UnitPixel);
    Font fH2(&ff, 17, FontStyleBold, UnitPixel);
    Font fBody(&ff, 13, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 13, FontStyleBold, UnitPixel);
    Font fSm(&ff, 12, FontStyleRegular, UnitPixel);
    Font fXS(&ff, 10, FontStyleBold, UnitPixel);
    Font fMed(&ff, 22, FontStyleBold, UnitPixel);

    // --- Premium Dark & Crimson Colors ---
    SolidBrush bBg(Color(255, 244, 244, 245)); // Very light gray/white
    SolidBrush bCard(Color(255, 255, 255, 255));
    SolidBrush bTextMain(Color(255, 15, 23, 42)); // Slate 900
    SolidBrush bTextMuted(Color(255, 100, 116, 139)); // Slate 500
    SolidBrush bCrimson(Color(255, 220, 38, 38)); // Red 600

    Pen pBrd(Color(255, 226, 232, 240), 1.0f); // Slate 200

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);    fmtR.SetLineAlignment(StringAlignmentCenter);

    const float PAD = 24.0f;

    // ================================================================
    //  1. TOP HEADER BAR
    // ================================================================
    float headerH = 68.0f;
    g.FillRectangle(&bCard, cx, cy, cw, headerH);
    g.DrawLine(&pBrd, cx, cy + headerH, cx + cw, cy + headerH);

    g.DrawString(L"RasFocus Pro Max \x2014 Analytics", -1, &fH1, RectF(cx + PAD, cy, 400.0f, headerH), &fmtL, &bTextMain);

    // "Strict Mode" Badge
    SolidBrush badgeBg(Color(255, 254, 226, 226)); // Light Red
    Pen badgePen(Color(255, 248, 113, 113), 1.0f);
    RoundRect(g, &badgeBg, &badgePen, cx + 420.0f, cy + 22.0f, 130.0f, 24.0f, 4);
    g.DrawString(L"\x2B24  STRICT MODE: ON", -1, &fXS, RectF(cx + 420.0f, cy + 22.0f, 130.0f, 24.0f), &fmtC, &bCrimson);

    // Render Tabs
    for (int i = 0; i < 3; i++) {
        TabRect tr = GetTabRect(i, cx, cw);
        bool active = (i == stat_currentTab);
        bool hovered = (i == stat_hovTab);
        
        Color bgColor = active ? Color(255, 226, 232, 240) : (hovered ? Color(255, 241, 245, 249) : Color(255, 255, 255, 255));
        Color penColor = active ? Color(255, 100, 116, 139) : Color(255, 226, 232, 240);
        Color txtColor = active ? Color(255, 15, 23, 42) : Color(255, 100, 116, 139);

        SolidBrush tagBg(bgColor); Pen tagPen(penColor, 1.0f); SolidBrush tagTxt(txtColor);
        RoundRect(g, &tagBg, &tagPen, tr.x, cy + 20.0f, tr.w, 28.0f, 4);
        g.DrawString(tr.lbl.c_str(), -1, &fSm, RectF(tr.x, cy + 20.0f, tr.w, 28.0f), &fmtC, &tagTxt);
    }

    // Export Icon Button
    float btnX = cx + cw - PAD - 36.0f, btnY = cy + 20.0f, btnS = 28.0f;
    SolidBrush bExp(stat_hovExport ? Color(255, 226, 232, 240) : Color(255, 255, 255, 255));
    RoundRect(g, &bExp, &pBrd, btnX, btnY, btnS, btnS, 4);
    g.DrawString(L"\x2B07", -1, &fBody, RectF(btnX, btnY, btnS, btnS), &fmtC, &bTextMain);

    // ================================================================
    //  2. RESPONSIVE LAYOUT ENGINE
    // ================================================================
    float cY = cy + headerH;
    g.FillRectangle(&bBg, cx, cY, cw, ch - headerH);

    float availH = ch - headerH - (PAD * 4.0f); 
    if (availH < 450.0f) availH = 450.0f; 

    float r1H = min(110.0f, availH * 0.22f); 
    float r2H = (availH - r1H) * 0.48f;      
    float r3H = (availH - r1H) * 0.52f;      

    // ================================================================
    //  3. HARDCORE METRIC CARDS (Row 1)
    // ================================================================
    float mcY = cY + PAD;
    float mcGap = 16.0f;
    float mcW = (cw - PAD * 2 - mcGap * 3) / 4.0f;

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);

    int totalProcs = 0;
    vector<AppEntry> liveApps = GetLiveRunningApps(totalProcs);

    struct MetricCard { wstring title; wstring val; wstring sub; Color startC; Color endC; };
    MetricCard mc[4] = {
        { L"Deep Work Today", L"5h 42m", L"Unbroken Focus", Color(255, 16, 185, 129), Color(255, 5, 150, 105) }, // Deep Green
        { L"Halal Guard Blocks", L"127", L"Malicious Attempts Prevented", Color(255, 239, 68, 68), Color(255, 185, 28, 28) }, // Blood Red
        { L"Time Saved", L"2h 15m", L"Reclaimed from distractions", Color(255, 59, 130, 246), Color(255, 29, 78, 216) }, // Solid Blue
        { L"Longest Streak", L"14 Days", L"Consistent Productivity", Color(255, 245, 158, 11), Color(255, 217, 119, 6) }, // Amber/Gold
    };

    for (int i = 0; i < 4; i++) {
        float mx = cx + PAD + i * (mcW + mcGap);
        RoundRect(g, &bCard, &pBrd, mx, mcY, mcW, r1H, 8); // Sharper corners for stricter look
        
        RectF gradRect(mx, mcY + 16.0f, 5.0f, r1H - 32.0f);
        LinearGradientBrush gradBr(gradRect, mc[i].startC, mc[i].endC, LinearGradientModeVertical);
        RoundRect(g, &gradBr, nullptr, mx, mcY + 16.0f, 5.0f, r1H - 32.0f, 2);
        
        g.DrawString(mc[i].title.c_str(), -1, &fSm, RectF(mx + 18.0f, mcY + 16.0f, mcW - 20.0f, 16.0f), &fmtL, &bTextMuted);
        g.DrawString(mc[i].val.c_str(), -1, &fH1, RectF(mx + 18.0f, mcY + r1H/2.0f - 14.0f, mcW - 20.0f, 28.0f), &fmtL, &bTextMain);
        g.DrawString(mc[i].sub.c_str(), -1, &fSm, RectF(mx + 18.0f, mcY + r1H - 30.0f, mcW - 20.0f, 16.0f), &fmtL, &bTextMuted);
    }

    // ================================================================
    //  4. ROW 2: LIVE APP PROCESSES & INTERCEPTION LOG
    // ================================================================
    float r2Y = mcY + r1H + PAD;
    float halfW = (cw - PAD * 2 - mcGap) / 2.0f;
    float rightX = cx + PAD + halfW + mcGap;

    // --- Left Card: Active RAM & Apps ---
    RoundRect(g, &bCard, &pBrd, cx + PAD, r2Y, halfW, r2H, 8);
    g.DrawString(L"Active Memory Allocation", -1, &fH2, RectF(cx + PAD + 20.0f, r2Y + 16.0f, halfW, 22.0f), &fmtL, &bTextMain);

    StringFormat fmtTrim; 
    fmtTrim.SetAlignment(StringAlignmentNear); fmtTrim.SetLineAlignment(StringAlignmentCenter);
    fmtTrim.SetTrimming(StringTrimmingEllipsisCharacter);

    float appRowH = (r2H - 50.0f) / 5.0f; 
    float appY0 = r2Y + 45.0f;

    for (int i = 0; i < (int)liveApps.size(); i++) {
        float ay = appY0 + i * appRowH;
        auto& a = liveApps[i];
        
        SolidBrush icBg(a.iconBg); FillCircle(g, icBg, cx + PAD + 32.0f, ay + appRowH / 2.0f, 14.0f);
        SolidBrush icFg(a.barColorStart); wstring initial(1, a.name[0]);
        g.DrawString(initial.c_str(), -1, &fBold, RectF(cx + PAD + 18.0f, ay, 28.0f, appRowH), &fmtC, &icFg);
        
        g.DrawString(a.name.c_str(), -1, &fBold, RectF(cx + PAD + 60.0f, ay + appRowH*0.2f, halfW - 160.0f, 18.0f), &fmtTrim, &bTextMain);
        g.DrawString(a.category.c_str(), -1, &fSm, RectF(cx + PAD + 60.0f, ay + appRowH*0.6f, halfW - 160.0f, 14.0f), &fmtTrim, &bTextMuted);
        
        g.DrawString(a.memStr.c_str(), -1, &fBold, RectF(cx + PAD + halfW - 90.0f, ay + appRowH*0.2f, 70.0f, 18.0f), &fmtR, &bTextMain);
        DrawGradientProgressBar(g, cx + PAD + halfW - 90.0f, ay + appRowH*0.6f + 2.0f, 70.0f, 6.0f, a.pct * stat_animProgress, a.barColorStart, a.barColorEnd);
    }

    // --- Right Card: Interception Log (Cold Turkey Style) ---
    RoundRect(g, &bCard, &pBrd, rightX, r2Y, halfW, r2H, 8);
    g.DrawString(L"Interception Log (Blocked)", -1, &fH2, RectF(rightX + 20.0f, r2Y + 16.0f, halfW, 22.0f), &fmtL, &bTextMain);

    struct SiteEntry { wstring name; wstring initial; int blockedCount; float barPct; };
    vector<SiteEntry> sites = {
        { L"facebook.com", L"F", 84, 100.0f },
        { L"youtube.com/shorts",  L"Y", 56, 66.0f },
        { L"instagram.com",  L"I", 31, 36.0f },
    };
    
    float siteRowH = (r2H - 40.0f) / 3.0f;
    float siteY0 = r2Y + 36.0f;
    for (int i = 0; i < (int)sites.size(); i++) {
        float sy = siteY0 + i * siteRowH;
        SolidBrush favBg(Color(255, 254, 226, 226)); SolidBrush favFg(Color(255, 220, 38, 38));
        RoundRect(g, &favBg, nullptr, rightX + 16.0f, sy + 6.0f, 24.0f, 24.0f, 4);
        g.DrawString(sites[i].initial.c_str(), -1, &fBold, RectF(rightX + 16.0f, sy + 6.0f, 24.0f, 24.0f), &fmtC, &favFg);
        g.DrawString(sites[i].name.c_str(), -1, &fBody, RectF(rightX + 50.0f, sy + 6.0f, halfW - 160.0f, 20.0f), &fmtL, &bTextMain);
        
        wstringstream wss; wss << sites[i].blockedCount << L" Blocked";
        g.DrawString(wss.str().c_str(), -1, &fSm, RectF(rightX + halfW - 148.0f, sy + 6.0f, 60.0f, 20.0f), &fmtR, &bTextMuted);
        
        // Solid Red Bar for strictness
        DrawGradientProgressBar(g, rightX + halfW - 80.0f, sy + 14.0f, 66.0f, 6.0f, sites[i].barPct * stat_animProgress, Color(255, 239, 68, 68), Color(255, 185, 28, 28));
    }

    // ================================================================
    //  5. ROW 3: FOCUS HISTORY & HEALTH
    // ================================================================
    float r3Y = r2Y + r2H + PAD;
    float barChartW = (cw - PAD * 2) * 0.65f;
    float donutW    = (cw - PAD * 2) - barChartW - mcGap;
    float bcX = cx + PAD;
    float dcX = bcX + barChartW + mcGap;

    // --- Bar Chart ---
    RoundRect(g, &bCard, &pBrd, bcX, r3Y, barChartW, r3H, 8);
    wstring chartTitle = (stat_currentTab == 1) ? L"Deep Work Analysis (This Week)" : L"Deep Work Analysis (Today)";
    g.DrawString(chartTitle.c_str(), -1, &fH2, RectF(bcX + 20.0f, r3Y + 16.0f, barChartW, 22.0f), &fmtL, &bTextMain);

    vector<float> chartData = { 3.5f, 4.2f, 2.8f, 6.1f, 5.0f, 1.2f, 4.5f }; 
    if(stat_currentTab == 1) chartData = { 6.5f, 5.0f, 7.2f, 4.8f, 6.2f, 2.5f, 5.8f }; 
    if(stat_currentTab == 2) chartData = { 8.5f, 7.0f, 9.2f, 6.8f, 8.2f, 4.5f, 7.8f }; 
    vector<wstring> days = { L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun" };
    
    float barAreaH = r3H - 80.0f;
    float barAreaY0 = r3Y + 50.0f;
    float barAreaX0 = bcX + 40.0f;
    float barAreaW  = barChartW - 80.0f;
    float barSlot   = barAreaW / 7.0f;
    float barW      = min(32.0f, barSlot * 0.55f); 

    for (int g2 = 0; g2 <= 3; g2++) {
        float gy = barAreaY0 + barAreaH - (g2 / 3.0f) * barAreaH;
        Pen gp(Color(255, 241, 245, 249), 1.0f); g.DrawLine(&gp, barAreaX0, gy, barAreaX0 + barAreaW, gy);
    }

    for (int i = 0; i < 7; i++) {
        float targetH = (chartData[i] / 10.0f) * barAreaH;
        float curH    = targetH * stat_animProgress;
        float bx = barAreaX0 + i * barSlot + (barSlot - barW) / 2.0f;
        float by = barAreaY0 + barAreaH - curH;

        if (curH > barW) {
            RectF gradRect(bx, by, barW, curH);
            // Emerald to Teal gradient for deep work
            LinearGradientBrush barBr(gradRect, Color(255, 16, 185, 129), Color(255, 14, 165, 233), LinearGradientModeVertical);
            GraphicsPath bp;
            bp.AddArc(bx, by, barW, barW, 180, 90);
            bp.AddArc(bx, by, barW, barW, 270, 90);
            bp.AddLine(bx + barW, by + barW / 2, bx + barW, by + curH);
            bp.AddLine(bx, by + curH, bx, by + curH);
            bp.AddLine(bx, by + curH, bx, by + barW / 2);
            bp.CloseFigure();
            g.FillPath(&barBr, &bp);
        } else if (curH > 0) {
            SolidBrush barBrSmall(Color(255, 16, 185, 129));
            g.FillRectangle(&barBrSmall, bx, by, barW, curH);
        }
        g.DrawString(days[i].c_str(), -1, &fSm, RectF(bx - 10.0f, barAreaY0 + barAreaH + 8.0f, barW + 20.0f, 16.0f), &fmtC, &bTextMuted);
    }

    // --- Donut Chart (Focus Score) ---
    RoundRect(g, &bCard, &pBrd, dcX, r3Y, donutW, r3H, 8);
    g.DrawString(L"Focus Score", -1, &fH2, RectF(dcX + 20.0f, r3Y + 16.0f, donutW, 22.0f), &fmtL, &bTextMain);

    float dCenX = dcX + donutW / 2.0f;
    float dCenY = r3Y + r3H / 2.0f + 10.0f;
    float dRad  = min(r3H * 0.25f, donutW * 0.25f);

    Pen pRingBg(Color(255, 241, 245, 249), 16.0f);
    g.DrawEllipse(&pRingBg, dCenX - dRad, dCenY - dRad, dRad * 2, dRad * 2);
    
    Pen segPen(Color(255, 16, 185, 129), 16.0f); // Emerald
    segPen.SetStartCap(LineCapRound); segPen.SetEndCap(LineCapRound);
    g.DrawArc(&segPen, dCenX - dRad, dCenY - dRad, dRad * 2, dRad * 2, -90.0f, 335.0f * stat_animProgress); // 93% Score
    
    wstring pTxt = to_wstring((int)(93 * stat_animProgress));
    g.DrawString(pTxt.c_str(), -1, &fMed, RectF(dCenX - dRad, dCenY - 14.0f, dRad * 2, 28.0f), &fmtC, &bTextMain);
}

// ============================================================
//  MOUSE MOVE (For Hover Effects)
// ============================================================
void ProcessStatisticsMouseMove(float x, float y, float cx, float cw, float cy)
{
    stat_hovExport  = false;
    stat_hovTab = -1;

    float btnX = cx + cw - 24.0f - 36.0f, btnY = cy + 20.0f, btnS = 28.0f;
    if (x >= btnX && x <= btnX + btnS && y >= btnY && y <= btnY + btnS) stat_hovExport = true;

    for (int i = 0; i < 3; i++) {
        TabRect tr = GetTabRect(i, cx, cw);
        if (x >= tr.x && x <= tr.x + tr.w && y >= cy + 20.0f && y <= cy + 20.0f + 28.0f) {
            stat_hovTab = i;
        }
    }
}

// ============================================================
//  MOUSE CLICK
// ============================================================
void ProcessStatisticsMouseClick(float x, float y, float cx, float cw, float cy)
{
    float btnX = cx + cw - 24.0f - 36.0f, btnY = cy + 20.0f, btnS = 28.0f;
    if (x >= btnX && x <= btnX + btnS && y >= btnY && y <= btnY + btnS) {
        MessageBoxW(NULL, L"Analytics Data Export Feature Coming Soon!", L"RasFocus Pro Max", MB_OK | MB_ICONINFORMATION);
    }

    for (int i = 0; i < 3; i++) {
        TabRect tr = GetTabRect(i, cx, cw);
        if (x >= tr.x && x <= tr.x + tr.w && y >= cy + 20.0f && y <= cy + 20.0f + 28.0f) {
            if (stat_currentTab != i) {
                stat_currentTab = i;
                stat_firstLoad = true;
                stat_animProgress = 0.0f; 
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
