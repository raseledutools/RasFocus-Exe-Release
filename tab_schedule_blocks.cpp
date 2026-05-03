#include "tab_schedule_blocks.h"
#include <vector>
#include <string>
#include <fstream>
#include <shlobj.h>
#include <codecvt>
#include <locale>
#include <algorithm>

using namespace Gdiplus;
using namespace std;

// ==========================================
// --- DATA STRUCTURES (Cold Turkey Style) ---
// ==========================================
struct SchBlockItem { 
    wstring name; 
    bool isHoveredCross = false; 
};

struct FocusProfile {
    wstring profileName;
    vector<SchBlockItem> blockedWebsites;
    vector<SchBlockItem> blockedApps;
    bool isActive = false;
    
    // UI states
    bool hToggle = false;
    bool hEdit = false;
    bool hDel = false;
};

// --- Static Global Variables (Isolated to avoid conflict) ---
static vector<FocusProfile> g_profiles;
static bool isSchDataLoaded = false;
static float sch_tScroll = 0.0f, sch_cScroll = 0.0f;
static float s_cx = 0, s_cy = 0, s_cw = 800, s_ch = 600;

// Overlay Edit States
static int editingProfileIdx = -1; // -1 = Closed, -2 = Creating New, >=0 = Editing
static wstring inpProfileName = L"";
static wstring inpWeb = L"";
static wstring inpApp = L"";
static int activeInput = 0; // 0=None, 1=Name, 2=Web, 3=App

static bool hAddProfileBtn = false;
static bool hSaveBtn = false, hCancelBtn = false;
static bool hAddWebBtn = false, hAddAppBtn = false;

// --- Colors ---
static const Color ClrTeal(255, 12, 168, 176);
static const Color ClrTealHover(255, 30, 185, 195);
static const Color ClrDark(255, 50, 50, 50);
static const Color ClrGrayText(255, 120, 120, 120);
static const Color ClrWhite(255, 255, 255, 255);
static const Color ClrBorder(255, 220, 225, 230);
static const Color ClrBg(255, 248, 250, 252);
static const Color ClrRed(255, 231, 76, 60);
static const Color ClrGreen(255, 90, 170, 20);
static const Color ClrOverlay(180, 0, 0, 0);

// --- Helpers ---
static GraphicsPath* GetSchRoundRectPath(RectF rect, int radius) {
    GraphicsPath* path = new GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure(); return path;
}

// ==========================================
// --- SAVE & LOAD SYSTEM ---
// ==========================================
static wstring GetSchSavePath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        wstring fullPath = wstring(path) + L"\\RasFocus";
        CreateDirectoryW(fullPath.c_str(), NULL);
        return fullPath + L"\\custom_profiles.dat";
    }
    return L"";
}

static void SaveProfiles() {
    wstring path = GetSchSavePath();
    string nPath(path.begin(), path.end());
    wofstream out(nPath);
    out.imbue(locale(out.getloc(), new codecvt_utf8<wchar_t>));
    if (!out) return;

    out << g_profiles.size() << L"\n";
    for (const auto& p : g_profiles) {
        out << p.profileName << L"\n";
        out << p.isActive << L"\n";
        
        out << p.blockedWebsites.size() << L"\n";
        for (const auto& w : p.blockedWebsites) out << w.name << L"\n";
        
        out << p.blockedApps.size() << L"\n";
        for (const auto& a : p.blockedApps) out << a.name << L"\n";
    }
    out.close();
}

static void LoadProfiles() {
    wstring path = GetSchSavePath();
    string nPath(path.begin(), path.end());
    wifstream in(nPath);
    in.imbue(locale(in.getloc(), new codecvt_utf8<wchar_t>));
    if (!in) {
        // Default Profile for first time
        g_profiles.push_back({L"Deep Work Session", {{L"facebook.com"}, {L"youtube.com"}}, {{L"discord.exe"}}, false});
        return;
    }

    size_t pCount = 0; in >> pCount; in.ignore();
    g_profiles.clear();
    for (size_t i = 0; i < pCount; ++i) {
        FocusProfile p;
        getline(in, p.profileName);
        in >> p.isActive; in.ignore();

        size_t wCount = 0; in >> wCount; in.ignore();
        for (size_t j = 0; j < wCount; ++j) {
            wstring w; getline(in, w); p.blockedWebsites.push_back({w, false});
        }

        size_t aCount = 0; in >> aCount; in.ignore();
        for (size_t j = 0; j < aCount; ++j) {
            wstring a; getline(in, a); p.blockedApps.push_back({a, false});
        }
        g_profiles.push_back(p);
    }
    in.close();
}

// ==========================================
// --- DRAWING LOGIC ---
// ==========================================
void DrawScheduleBlocksTab(Graphics& g, float x, float y, float w, float h) {
    if (!isSchDataLoaded) { LoadProfiles(); isSchDataLoaded = true; }
    
    s_cx = x; s_cy = y; s_cw = w; s_ch = h;
    sch_cScroll += (sch_tScroll - sch_cScroll) * 0.2f;

    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff, 24, FontStyleBold, UnitPixel);
    Font fCardTitle(&ff, 18, FontStyleBold, UnitPixel);
    Font fNorm(&ff, 15, FontStyleRegular, UnitPixel);
    Font fBold(&ff, 15, FontStyleBold, UnitPixel);
    Font fSmall(&ff, 13, FontStyleRegular, UnitPixel);
    
    FontFamily ffi(L"Segoe MDL2 Assets");
    Font fIcon(&ffi, 20, FontStyleRegular, UnitPixel);
    Font fSmallIcon(&ffi, 14, FontStyleRegular, UnitPixel);

    SolidBrush bDark(ClrDark); SolidBrush bWhite(ClrWhite); SolidBrush bGray(ClrGrayText);
    SolidBrush bTeal(ClrTeal); SolidBrush bRed(ClrRed); SolidBrush bGreen(ClrGreen);
    Pen pBorder(ClrBorder, 1.5f); Pen pTeal(ClrTeal, 2.0f);

    StringFormat fL; fL.SetAlignment(StringAlignmentNear); fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    // --- MAIN VIEW: PROFILE LIST ---
    g.DrawString(L"Focus Profiles", -1, &fTitle, RectF(x + 20, y + 20, 300, 35), &fL, &bDark);
    g.DrawString(L"Create dedicated profiles to manage your workflow like a pro.", -1, &fNorm, RectF(x + 20, y + 60, 500, 20), &fL, &bGray);

    // Add Profile Button
    RectF addBtnRect(x + w - 220, y + 20, 200, 40);
    GraphicsPath* aP = GetSchRoundRectPath(addBtnRect, 4);
    SolidBrush aBr(hAddProfileBtn ? ClrTealHover : ClrTeal);
    g.FillPath(&aBr, aP); delete aP;
    g.DrawString(L"+ Add Blocking Profile", -1, &fBold, addBtnRect, &fC, &bWhite);

    // Draw Profile Cards
    float cardW = (w - 60.0f) / 2.0f;
    float cardH = 150.0f;
    float startX = x + 20.0f;
    float startY = y + 100.0f - sch_cScroll;

    Region oldClip; g.GetClip(&oldClip);
    g.SetClip(RectF(x, y + 95.0f, w, h - 95.0f));

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        float cX = startX + (i % 2) * (cardW + 20.0f);
        float cY = startY + (i / 2) * (cardH + 20.0f);

        if (cY > y + h || cY + cardH < y + 90.0f) continue; // Out of view

        RectF cardRect(cX, cY, cardW, cardH);
        GraphicsPath* cP = GetSchRoundRectPath(cardRect, 6);
        g.FillPath(&bWhite, cP); 
        g.DrawPath(g_profiles[i].isActive ? &pTeal : &pBorder, cP);
        delete cP;

        g.DrawString(L"\xE82D", -1, &fIcon, RectF(cX + 15, cY + 15, 30, 30), &fL, &bTeal); // Shield icon
        g.DrawString(g_profiles[i].profileName.c_str(), -1, &fCardTitle, RectF(cX + 50, cY + 15, cardW - 60, 30), &fL, &bDark);
        
        wstring statStr = L"Blocked: " + to_wstring(g_profiles[i].blockedWebsites.size()) + L" Websites, " + to_wstring(g_profiles[i].blockedApps.size()) + L" Apps";
        g.DrawString(statStr.c_str(), -1, &fNorm, RectF(cX + 15, cY + 55, cardW - 30, 20), &fL, &bGray);

        // Active Toggle
        RectF togRect(cX + 15, cY + 105, 100, 30);
        GraphicsPath* tp = GetSchRoundRectPath(togRect, 4);
        SolidBrush tBr(g_profiles[i].isActive ? ClrGreen : (g_profiles[i].hToggle ? ClrBorder : Color(255, 220, 220, 220)));
        g.FillPath(&tBr, tp); delete tp;
        g.DrawString(g_profiles[i].isActive ? L"Active" : L"Inactive", -1, &fBold, togRect, &fC, g_profiles[i].isActive ? &bWhite : &bDark);

        // Edit Button
        RectF editRect(cX + cardW - 130, cY + 105, 60, 30);
        GraphicsPath* ep = GetSchRoundRectPath(editRect, 4);
        SolidBrush eBr(g_profiles[i].hEdit ? ClrBorder : ClrBg);
        g.FillPath(&eBr, ep); g.DrawPath(&pBorder, ep); delete ep;
        g.DrawString(L"Edit", -1, &fBold, editRect, &fC, &bDark);

        // Delete Button
        RectF delRect(cX + cardW - 60, cY + 105, 45, 30);
        GraphicsPath* dp = GetSchRoundRectPath(delRect, 4);
        SolidBrush dBr(g_profiles[i].hDel ? ClrRed : ClrWhite);
        g.FillPath(&dBr, dp); g.DrawPath(&pBorder, dp); delete dp;
        g.DrawString(L"\xE74D", -1, &fIcon, delRect, &fC, g_profiles[i].hDel ? &bWhite : &bRed);
    }
    g.SetClip(&oldClip);

    // --- OVERLAY: CREATE / EDIT PROFILE ---
    if (editingProfileIdx != -1) {
        SolidBrush bgOver(ClrOverlay);
        g.FillRectangle(&bgOver, x, y, w, h);

        float ovW = w - 80.0f;
        float ovH = h - 80.0f;
        float ovX = x + 40.0f;
        float ovY = y + 40.0f;

        RectF ovRect(ovX, ovY, ovW, ovH);
        GraphicsPath* oP = GetSchRoundRectPath(ovRect, 8);
        
        // FIX: Creating a SolidBrush from the ClrBg color instead of passing Color directly
        SolidBrush ovBgBrush(ClrBg); 
        g.FillPath(&ovBgBrush, oP); 
        g.DrawPath(&pBorder, oP); 
        delete oP;

        wstring titleTxt = (editingProfileIdx == -2) ? L"Create New Profile" : L"Edit Profile";
        g.DrawString(titleTxt.c_str(), -1, &fTitle, RectF(ovX + 30, ovY + 20, 300, 30), &fL, &bDark);

        // Name Input
        g.DrawString(L"Profile Name:", -1, &fBold, RectF(ovX + 30, ovY + 70, 120, 35), &fL, &bDark);
        RectF nameInp(ovX + 150, ovY + 70, ovW - 180, 35);
        GraphicsPath* np = GetSchRoundRectPath(nameInp, 4);
        g.FillPath(&bWhite, np); g.DrawPath(activeInput == 1 ? &pTeal : &pBorder, np); delete np;
        
        if(inpProfileName.empty() && activeInput != 1) g.DrawString(L"e.g. Hardcore Study Mode", -1, &fNorm, RectF(nameInp.X+10, nameInp.Y, nameInp.Width, nameInp.Height), &fL, &bGray);
        else {
            g.DrawString(inpProfileName.c_str(), -1, &fNorm, RectF(nameInp.X+10, nameInp.Y, nameInp.Width, nameInp.Height), &fL, &bDark);
            if(activeInput == 1 && (GetTickCount()/500)%2==0) {
                Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(inpProfileName.c_str(), -1, &fNorm, PointF(0,0), &bR);
                g.FillRectangle(&bDark, nameInp.X+12+(inpProfileName.empty()?0:bR.Width), nameInp.Y+7, 1.5f, 21.0f);
            }
        }

        // Two Columns: Websites vs Apps
        float colW = (ovW - 90.0f) / 2.0f;
        float lColX = ovX + 30.0f;
        float rColX = ovX + 60.0f + colW;
        float colY = ovY + 130.0f;

        // -> Web Column
        g.DrawString(L"Blocked Websites:", -1, &fBold, RectF(lColX, colY, colW, 25), &fL, &bDark);
        RectF webInp(lColX, colY + 30, colW - 80, 35);
        GraphicsPath* wip = GetSchRoundRectPath(webInp, 4);
        g.FillPath(&bWhite, wip); g.DrawPath(activeInput == 2 ? &pTeal : &pBorder, wip); delete wip;
        
        if(inpWeb.empty() && activeInput != 2) g.DrawString(L"e.g. facebook.com", -1, &fNorm, RectF(webInp.X+10, webInp.Y, webInp.Width, webInp.Height), &fL, &bGray);
        else {
            g.DrawString(inpWeb.c_str(), -1, &fNorm, RectF(webInp.X+10, webInp.Y, webInp.Width, webInp.Height), &fL, &bDark);
            if(activeInput == 2 && (GetTickCount()/500)%2==0) {
                Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(inpWeb.c_str(), -1, &fNorm, PointF(0,0), &bR);
                g.FillRectangle(&bDark, webInp.X+12+(inpWeb.empty()?0:bR.Width), webInp.Y+7, 1.5f, 21.0f);
            }
        }
        
        RectF addWBtn(lColX + colW - 70, colY + 30, 70, 35);
        GraphicsPath* awp = GetSchRoundRectPath(addWBtn, 4);
        SolidBrush awBr(hAddWebBtn ? ClrTealHover : ClrTeal);
        g.FillPath(&awBr, awp); delete awp;
        g.DrawString(L"Add", -1, &fBold, addWBtn, &fC, &bWhite);

        // Temporarily referencing profile data
        vector<SchBlockItem>* currentWebs = nullptr;
        vector<SchBlockItem>* currentApps = nullptr;
        if(editingProfileIdx >= 0) {
            currentWebs = &g_profiles[editingProfileIdx].blockedWebsites;
            currentApps = &g_profiles[editingProfileIdx].blockedApps;
        }

        // Web List Box
        RectF webListR(lColX, colY + 75, colW, ovH - 280);
        g.FillRectangle(&bWhite, webListR); g.DrawRectangle(&pBorder, webListR.X, webListR.Y, webListR.Width, webListR.Height);
        if(currentWebs) {
            g.GetClip(&oldClip); g.SetClip(webListR);
            float itemY = webListR.Y + 5.0f;
            for(auto& wItem : *currentWebs) {
                g.DrawString(wItem.name.c_str(), -1, &fNorm, RectF(webListR.X+10, itemY, webListR.Width-40, 30), &fL, &bDark);
                SolidBrush crBr(wItem.isHoveredCross ? ClrRed : ClrGrayText);
                g.DrawString(L"\xE711", -1, &fSmallIcon, RectF(webListR.X+webListR.Width-30, itemY, 30, 30), &fC, &crBr);
                g.DrawLine(&pBorder, webListR.X+5, itemY+30, webListR.X+webListR.Width-5, itemY+30);
                itemY += 30.0f;
            }
            g.SetClip(&oldClip);
        }

        // -> App Column
        g.DrawString(L"Blocked Apps:", -1, &fBold, RectF(rColX, colY, colW, 25), &fL, &bDark);
        RectF appInp(rColX, colY + 30, colW - 80, 35);
        GraphicsPath* aip = GetSchRoundRectPath(appInp, 4);
        g.FillPath(&bWhite, aip); g.DrawPath(activeInput == 3 ? &pTeal : &pBorder, aip); delete aip;
        
        if(inpApp.empty() && activeInput != 3) g.DrawString(L"e.g. valorant.exe", -1, &fNorm, RectF(appInp.X+10, appInp.Y, appInp.Width, appInp.Height), &fL, &bGray);
        else {
            g.DrawString(inpApp.c_str(), -1, &fNorm, RectF(appInp.X+10, appInp.Y, appInp.Width, appInp.Height), &fL, &bDark);
            if(activeInput == 3 && (GetTickCount()/500)%2==0) {
                Graphics gT(GetDesktopWindow()); RectF bR; gT.MeasureString(inpApp.c_str(), -1, &fNorm, PointF(0,0), &bR);
                g.FillRectangle(&bDark, appInp.X+12+(inpApp.empty()?0:bR.Width), appInp.Y+7, 1.5f, 21.0f);
            }
        }

        RectF addABtn(rColX + colW - 70, colY + 30, 70, 35);
        GraphicsPath* aap = GetSchRoundRectPath(addABtn, 4);
        SolidBrush aaBr(hAddAppBtn ? ClrTealHover : ClrTeal);
        g.FillPath(&aaBr, aap); delete aap;
        g.DrawString(L"Add", -1, &fBold, addABtn, &fC, &bWhite);

        // App List Box
        RectF appListR(rColX, colY + 75, colW, ovH - 280);
        g.FillRectangle(&bWhite, appListR); g.DrawRectangle(&pBorder, appListR.X, appListR.Y, appListR.Width, appListR.Height);
        if(currentApps) {
            g.GetClip(&oldClip); g.SetClip(appListR);
            float itemY = appListR.Y + 5.0f;
            for(auto& aItem : *currentApps) {
                g.DrawString(aItem.name.c_str(), -1, &fNorm, RectF(appListR.X+10, itemY, appListR.Width-40, 30), &fL, &bDark);
                SolidBrush crBr(aItem.isHoveredCross ? ClrRed : ClrGrayText);
                g.DrawString(L"\xE711", -1, &fSmallIcon, RectF(appListR.X+appListR.Width-30, itemY, 30, 30), &fC, &crBr);
                g.DrawLine(&pBorder, appListR.X+5, itemY+30, appListR.X+appListR.Width-5, itemY+30);
                itemY += 30.0f;
            }
            g.SetClip(&oldClip);
        }

        // Action Buttons (Bottom)
        RectF saveBtn(ovX + ovW - 140, ovY + ovH - 60, 110, 40);
        GraphicsPath* svp = GetSchRoundRectPath(saveBtn, 4);
        SolidBrush svBr(hSaveBtn ? ClrTealHover : ClrTeal);
        g.FillPath(&svBr, svp); delete svp;
        g.DrawString(L"Save Profile", -1, &fBold, saveBtn, &fC, &bWhite);

        RectF cancelBtn(ovX + ovW - 260, ovY + ovH - 60, 100, 40);
        GraphicsPath* cvp = GetSchRoundRectPath(cancelBtn, 4);
        SolidBrush cvBr(hCancelBtn ? Color(255, 235, 235, 235) : ClrWhite);
        g.FillPath(&cvBr, cvp); g.DrawPath(&pBorder, cvp); delete cvp;
        g.DrawString(L"Cancel", -1, &fBold, cancelBtn, &fC, &bDark);
    }
}

// ==========================================
// --- MOUSE MOVE LOGIC ---
// ==========================================
void ProcessScheduleBlocksMouseMove(float x, float y) {
    hAddProfileBtn = false;
    hSaveBtn = false; hCancelBtn = false;
    hAddWebBtn = false; hAddAppBtn = false;

    if (editingProfileIdx != -1) {
        float ovW = s_cw - 80.0f, ovH = s_ch - 80.0f;
        float ovX = s_cx + 40.0f, ovY = s_cy + 40.0f;
        float colW = (ovW - 90.0f) / 2.0f;
        float lColX = ovX + 30.0f, rColX = ovX + 60.0f + colW;
        float colY = ovY + 130.0f;

        if (RectF(ovX + ovW - 140, ovY + ovH - 60, 110, 40).Contains(x,y)) hSaveBtn = true;
        if (RectF(ovX + ovW - 260, ovY + ovH - 60, 100, 40).Contains(x,y)) hCancelBtn = true;
        if (RectF(lColX + colW - 70, colY + 30, 70, 35).Contains(x,y)) hAddWebBtn = true;
        if (RectF(rColX + colW - 70, colY + 30, 70, 35).Contains(x,y)) hAddAppBtn = true;

        if (editingProfileIdx >= 0) {
            float iY = colY + 80.0f;
            for (auto& w : g_profiles[editingProfileIdx].blockedWebsites) {
                w.isHoveredCross = RectF(lColX + colW - 30, iY, 30, 30).Contains(x, y); iY += 30.0f;
            }
            iY = colY + 80.0f;
            for (auto& a : g_profiles[editingProfileIdx].blockedApps) {
                a.isHoveredCross = RectF(rColX + colW - 30, iY, 30, 30).Contains(x, y); iY += 30.0f;
            }
        }
        return;
    }

    if (RectF(s_cx + s_cw - 220, s_cy + 20, 200, 40).Contains(x, y)) hAddProfileBtn = true;

    float cardW = (s_cw - 60.0f) / 2.0f;
    float cardH = 150.0f;
    float startX = s_cx + 20.0f;
    float startY = s_cy + 100.0f - sch_cScroll;

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        float cX = startX + (i % 2) * (cardW + 20.0f);
        float cY = startY + (i / 2) * (cardH + 20.0f);
        if (cY > s_cy + s_ch || cY + cardH < s_cy + 90.0f) continue;

        g_profiles[i].hToggle = RectF(cX + 15, cY + 105, 100, 30).Contains(x, y);
        g_profiles[i].hEdit = RectF(cX + cardW - 130, cY + 105, 60, 30).Contains(x, y);
        g_profiles[i].hDel = RectF(cX + cardW - 60, cY + 105, 45, 30).Contains(x, y);
    }
}

// ==========================================
// --- MOUSE CLICK LOGIC ---
// ==========================================
void ProcessScheduleBlocksMouseClick(float x, float y) {
    if (editingProfileIdx != -1) {
        float ovW = s_cw - 80.0f, ovH = s_ch - 80.0f;
        float ovX = s_cx + 40.0f, ovY = s_cy + 40.0f;
        float colW = (ovW - 90.0f) / 2.0f;
        float lColX = ovX + 30.0f, rColX = ovX + 60.0f + colW;
        float colY = ovY + 130.0f;

        // Input Box Focus
        activeInput = 0;
        if (RectF(ovX + 150, ovY + 70, ovW - 180, 35).Contains(x,y)) activeInput = 1;
        if (RectF(lColX, colY + 30, colW - 80, 35).Contains(x,y)) activeInput = 2;
        if (RectF(rColX, colY + 30, colW - 80, 35).Contains(x,y)) activeInput = 3;

        if (hCancelBtn) { editingProfileIdx = -1; return; }
        
        if (hSaveBtn) {
            if (inpProfileName.empty()) inpProfileName = L"Custom Profile";
            if (editingProfileIdx == -2) {
                FocusProfile np; np.profileName = inpProfileName;
                g_profiles.push_back(np);
            } else if (editingProfileIdx >= 0) {
                g_profiles[editingProfileIdx].profileName = inpProfileName;
            }
            editingProfileIdx = -1; SaveProfiles(); return;
        }

        if (editingProfileIdx >= 0) {
            if (hAddWebBtn && !inpWeb.empty()) { g_profiles[editingProfileIdx].blockedWebsites.push_back({inpWeb, false}); inpWeb = L""; }
            if (hAddAppBtn && !inpApp.empty()) { g_profiles[editingProfileIdx].blockedApps.push_back({inpApp, false}); inpApp = L""; }
            
            auto& webs = g_profiles[editingProfileIdx].blockedWebsites;
            for(auto it = webs.begin(); it != webs.end();) { if(it->isHoveredCross) it = webs.erase(it); else ++it; }
            
            auto& apps = g_profiles[editingProfileIdx].blockedApps;
            for(auto it = apps.begin(); it != apps.end();) { if(it->isHoveredCross) it = apps.erase(it); else ++it; }
        }
        return;
    }

    if (hAddProfileBtn) {
        editingProfileIdx = -2; // Create new
        inpProfileName = L""; inpWeb = L""; inpApp = L"";
        
        // Add a temporary empty profile to edit directly
        FocusProfile np; np.profileName = L"";
        g_profiles.push_back(np);
        editingProfileIdx = g_profiles.size() - 1;
        
        activeInput = 1; return;
    }

    for (size_t i = 0; i < g_profiles.size(); ++i) {
        if (g_profiles[i].hToggle) { g_profiles[i].isActive = !g_profiles[i].isActive; SaveProfiles(); }
        if (g_profiles[i].hEdit) {
            editingProfileIdx = i;
            inpProfileName = g_profiles[i].profileName;
            inpWeb = L""; inpApp = L"";
            activeInput = 1;
        }
        if (g_profiles[i].hDel) {
            int r = MessageBoxA(NULL, "Are you sure you want to delete this profile?", "Delete Profile", MB_YESNO | MB_ICONWARNING);
            if (r == IDYES) { g_profiles.erase(g_profiles.begin() + i); SaveProfiles(); break; }
        }
    }
}

// ==========================================
// --- KEYBOARD LOGIC ---
// ==========================================
void ProcessScheduleBlocksKeyPress(wchar_t c) {
    if (editingProfileIdx == -1 || activeInput == 0) return;
    if (c >= 32 && c <= 126) {
        if (activeInput == 1 && inpProfileName.length() < 30) inpProfileName += c;
        if (activeInput == 2 && inpWeb.length() < 40) inpWeb += c;
        if (activeInput == 3 && inpApp.length() < 40) inpApp += c;
    }
}

void ProcessScheduleBlocksKeyDown(WPARAM key) {
    if (editingProfileIdx == -1) return;
    if (key == VK_ESCAPE) { editingProfileIdx = -1; return; }
    
    if (key == VK_BACK) {
        if (activeInput == 1 && !inpProfileName.empty()) inpProfileName.pop_back();
        if (activeInput == 2 && !inpWeb.empty()) inpWeb.pop_back();
        if (activeInput == 3 && !inpApp.empty()) inpApp.pop_back();
    }
    else if (key == VK_RETURN && editingProfileIdx >= 0) {
        if (activeInput == 2 && !inpWeb.empty()) { g_profiles[editingProfileIdx].blockedWebsites.push_back({inpWeb, false}); inpWeb = L""; }
        if (activeInput == 3 && !inpApp.empty()) { g_profiles[editingProfileIdx].blockedApps.push_back({inpApp, false}); inpApp = L""; }
    }
}

void ProcessScheduleBlocksMouseWheel(float x, float y, int delta) {
    if (editingProfileIdx != -1) return; // Add inner scroll later if lists get too long
    
    int steps = (delta > 0) ? 1 : -1;
    sch_tScroll -= steps * 50.0f;
    
    // std::max/min ব্যবহার করে ফিক্স
    float totalRows = ceil((float)g_profiles.size() / 2.0f);
    float maxScroll = (std::max)(0.0f, (totalRows * 170.0f) - (s_ch - 100.0f));
    sch_tScroll = (std::max)(0.0f, (std::min)(sch_tScroll, maxScroll));
}