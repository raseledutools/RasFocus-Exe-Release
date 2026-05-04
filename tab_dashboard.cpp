// tab_dashboard.cpp

#include "tab_dashboard.h"
#include <string>
#include <vector>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd; 
extern float g_scaleFactor;

// --- Overlay & Kill States (From Original Code) ---
static bool showKillPrompt = false;
static wstring killInput = L"";
static int hoverNumBtn = -1; // 0-9 for digits, 10 for Clear, 11 for Enter, 12 for Close
static bool dash_hovKillBtn = false;

static float d_cX = 0.0f, d_cY = 0.0f, d_cW = 0.0f, d_cH = 0.0f;

// --- Dynamic Grid Layout Structures ---
struct DashBtn {
    wstring title;
    RectF bounds;
    bool isHovered;
};

struct DashSec {
    wstring title;
    vector<DashBtn> btns;
};

static vector<DashSec> s_sections;
static bool s_init = false;

// --- Load User Requirements ---
void InitDashboardData() {
    if (s_init) return;

    // Section 1: Quick Blocks
    DashSec sec1 = { L"1. Quick Blocks" };
    vector<wstring> b1 = { L"Rest Button", L"Internet Block", L"Uninstall Block", L"Ads Block", L"Adult Block", L"YT Shorts Block", L"FB Reels Block" };
    for (const auto& t : b1) sec1.btns.push_back({ t, RectF(), false });
    s_sections.push_back(sec1);

    // Section 2: AI & Cloud Workspace
    DashSec sec2 = { L"2. AI & Cloud Workspace" };
    vector<wstring> b2 = { L"Gemini", L"ChatGPT", L"DeepSeek", L"Grok", L"Perplexity", L"MATLAB", L"YouTube", L"Facebook", L"Google Colab", L"OneDrive", L"Gmail", L"Google Docs", L"Google Slides", L"Google Sheets" };
    for (const auto& t : b2) sec2.btns.push_back({ t, RectF(), false });
    s_sections.push_back(sec2);

    // Section 3: Professional Tools & Viewers
    DashSec sec3 = { L"3. Professional Tools & Viewers" };
    vector<wstring> b3 = { L"PDF Reader", L"Photo Viewer", L"Docs Viewer", L"PDF Merge", L"PDF Split", L"Image to PDF", L"PDF to Image", L"Job Photo Maker", L"Job Signature", L"Graphic Calc", L"Scientific Calc" };
    for (const auto& t : b3) sec3.btns.push_back({ t, RectF(), false });
    s_sections.push_back(sec3);

    // Section 4: Personal & Notes
    DashSec sec4 = { L"4. Personal & Notes" };
    vector<wstring> b4 = { L"Personal Diary", L"Instant Note" };
    for (const auto& t : b4) sec4.btns.push_back({ t, RectF(), false });
    s_sections.push_back(sec4);

    // Section 5: Student Corner
    DashSec sec5 = { L"5. Student Corner" };
    vector<wstring> b5 = { L"Study Materials", L"CGPA Calculator", L"Exam Routine" };
    for (const auto& t : b5) sec5.btns.push_back({ t, RectF(), false });
    s_sections.push_back(sec5);

    s_init = true;
}

// --- Helper: Rounded Rectangle Path ---
static void AddRoundedRectPath(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    if (d > w) d = w; if (d > h) d = h;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

// --- Helper: Draw Drop Shadow ---
static void DrawCardShadow(Graphics& g, float x, float y, float w, float h, float r) {
    for (int i = 0; i < 4; ++i) {
        GraphicsPath path;
        float expand = 4.0f - (float)i;
        float offset = (float)i; 
        AddRoundedRectPath(path, x - expand, y - expand + offset, w + expand * 2.0f, h + expand * 2.0f, r + expand);
        SolidBrush shadowBrush(Color(4 + (i * 2), 0, 0, 0)); 
        g.FillPath(&shadowBrush, &path);
    }
}

void DrawDashboardTab(Graphics& g, float cx, float cy, float cw, float ch) {
    d_cX = cx; d_cY = cy; d_cW = cw; d_cH = ch;
    InitDashboardData();

    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 26, FontStyleBold, UnitPixel);
    Font fSec(&ff, 16, FontStyleBold, UnitPixel);
    Font fBtn(&ff, 13, FontStyleBold, UnitPixel);
    FontFamily ffIc(L"Segoe MDL2 Assets");

    SolidBrush bWhite(Color(255, 255, 255, 255));
    SolidBrush bBg(Color(255, 248, 250, 252)); 
    SolidBrush bDark(Color(255, 40, 40, 40));
    SolidBrush bGray(Color(255, 120, 120, 120));
    SolidBrush bTeal(Color(255, 12, 168, 176));
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);

    // 1. Background
    g.FillRectangle(&bBg, cx, cy, cw, ch);
    g.DrawString(L"RasFocus Workspace", -1, &fH1, RectF(cx + 30.0f, cy + 15.0f, cw, 30.0f), &fmtL, &bDark);

    // 2. Draw Grid Sections
    float currentY = cy + 60.0f;
    float marginX = cx + 30.0f;
    float usableWidth = cw - 60.0f;
    
    int columns = 5; 
    float gap = 12.0f;
    float btnW = (usableWidth - (gap * (columns - 1))) / columns;
    float btnH = 40.0f;

    for (auto& sec : s_sections) {
        g.DrawString(sec.title.c_str(), -1, &fSec, RectF(marginX, currentY, usableWidth, 25.0f), &fmtL, &bGray);
        currentY += 30.0f;

        float currentX = marginX;
        int colCount = 0;

        for (auto& btn : sec.btns) {
            if (colCount >= columns) {
                colCount = 0; currentX = marginX; currentY += btnH + gap;
            }

            btn.bounds = RectF(currentX, currentY, btnW, btnH);
            GraphicsPath bPath;
            AddRoundedRectPath(bPath, btn.bounds.X, btn.bounds.Y, btn.bounds.Width, btn.bounds.Height, 6.0f);
            
            SolidBrush btnBg(btn.isHovered ? Color(255, 12, 168, 176) : Color(255, 255, 255, 255));
            SolidBrush btnTxt(btn.isHovered ? Color(255, 255, 255, 255) : Color(255, 70, 80, 90));
            
            g.FillPath(&btnBg, &bPath);
            Pen borderPen(Color(255, 220, 226, 230), 1.0f);
            g.DrawPath(&borderPen, &bPath);
            
            g.DrawString(btn.title.c_str(), -1, &fBtn, btn.bounds, &fmtC, &btnTxt);

            currentX += btnW + gap;
            colCount++;
        }
        currentY += btnH + 20.0f; // Space between sections
    }

    // 3. DEBUG KILL BUTTON (Bottom Right)
    float killW = 120.0f, killH = 35.0f;
    float killX = cx + cw - killW - 20.0f;
    float killY = cy + ch - killH - 20.0f;

    GraphicsPath kPath;
    AddRoundedRectPath(kPath, killX, killY, killW, killH, 6.0f);
    SolidBrush redBtn(dash_hovKillBtn ? Color(255, 255, 60, 60) : Color(255, 230, 50, 50));
    g.FillPath(&redBtn, &kPath);
    
    Font fKill(&ff, 14, FontStyleBold, UnitPixel);
    g.DrawString(L"DEBUG KILL", -1, &fKill, RectF(killX, killY, killW, killH), &fmtC, &bWhite);

    // =======================================================
    // 4. PASSWORD NUMPAD OVERLAY (Security Feature)
    // =======================================================
    if (showKillPrompt) {
        SolidBrush overBg(Color(220, 10, 15, 20));
        g.FillRectangle(&overBg, cx, cy, cw, ch);

        float pW = 320.0f, pH = 420.0f;
        float pX = cx + (cw - pW) / 2.0f;
        float pY = cy + (ch - pH) / 2.0f;

        DrawCardShadow(g, pX, pY, pW, pH, 15.0f);
        GraphicsPath popPath;
        AddRoundedRectPath(popPath, pX, pY, pW, pH, 15.0f);
        g.FillPath(&bWhite, &popPath);

        Font fH2(&ff, 20, FontStyleBold, UnitPixel);
        g.DrawString(L"Enter Debug Password", -1, &fH2, RectF(pX, pY + 20.0f, pW, 30.0f), &fmtC, &bDark);

        // Close Pop-up Button
        Font fClose(&ffIc, 14, FontStyleRegular, UnitPixel);
        SolidBrush closeC(hoverNumBtn == 12 ? Color(255, 230, 50, 50) : Color(255, 120, 120, 120));
        g.DrawString(L"\xE8BB", -1, &fClose, RectF(pX + pW - 40.0f, pY + 10.0f, 30.0f, 30.0f), &fmtC, &closeC);

        // Display Password as Asterisks
        float disX = pX + 30.0f, disY = pY + 70.0f, disW = pW - 60.0f, disH = 45.0f;
        GraphicsPath disPath;
        AddRoundedRectPath(disPath, disX, disY, disW, disH, 6.0f);
        SolidBrush disBg(Color(255, 240, 240, 240));
        g.FillPath(&disBg, &disPath);
        
        wstring stars = wstring(killInput.length(), L'*');
        Font fStar(&ff, 28, FontStyleBold, UnitPixel);
        g.DrawString(stars.c_str(), -1, &fStar, RectF(disX, disY + 8.0f, disW, disH), &fmtC, &bDark);

        // Draw Numpad Grid
        float padX = pX + 40.0f;
        float padY = disY + 65.0f;
        float nW = 70.0f, nH = 50.0f, nGap = 15.0f;

        wstring btnText[12] = { L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"C", L"0", L"OK" };
        int btnId[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11 };

        for (int i = 0; i < 12; ++i) {
            int row = i / 3; int col = i % 3;
            float bx = padX + (col * (nW + nGap));
            float by = padY + (row * (nH + nGap));

            GraphicsPath bPath;
            AddRoundedRectPath(bPath, bx, by, nW, nH, 6.0f);

            bool isHov = (hoverNumBtn == btnId[i]);
            Color cBgColor = Color(255, 245, 245, 245);
            Color cTxtColor = Color(255, 40, 40, 40);

            if (btnId[i] == 11) { // OK Button
                cBgColor = isHov ? Color(255, 30, 185, 195) : Color(255, 12, 168, 176);
                cTxtColor = Color(255, 255, 255, 255);
            } else if (btnId[i] == 10) { // Clear Button
                cBgColor = isHov ? Color(255, 255, 220, 220) : Color(255, 255, 240, 240);
                cTxtColor = Color(255, 230, 50, 50);
            } else if (isHov) { // Regular Digits
                cBgColor = Color(255, 220, 220, 220);
            }

            SolidBrush numBg(cBgColor);
            SolidBrush numTxtC(cTxtColor);
            g.FillPath(&numBg, &bPath);
            g.DrawString(btnText[i].c_str(), -1, &fH2, RectF(bx, by, nW, nH), &fmtC, &numTxtC);
        }
    }
}

void ProcessDashboardMouseMove(float x, float y) {
    hoverNumBtn = -1;
    dash_hovKillBtn = false;
    bool needsRedraw = false;

    // --- Overlay Active Mode (Blocks background hovers) ---
    if (showKillPrompt) {
        float pW = 320.0f, pH = 420.0f;
        float pX = d_cX + (d_cW - pW) / 2.0f;
        float pY = d_cY + (d_cH - pH) / 2.0f;

        if (x >= pX + pW - 40.0f && x <= pX + pW - 10.0f && y >= pY + 10.0f && y <= pY + 40.0f) hoverNumBtn = 12;

        float padX = pX + 40.0f, padY = pY + 135.0f;
        float nW = 70.0f, nH = 50.0f, nGap = 15.0f;
        int btnId[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11 };

        for (int i = 0; i < 12; ++i) {
            int row = i / 3; int col = i % 3;
            float bx = padX + (col * (nW + nGap));
            float by = padY + (row * (nH + nGap));
            if (x >= bx && x <= bx + nW && y >= by && y <= by + nH) hoverNumBtn = btnId[i];
        }
        if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }

    // --- Normal Grid Hovers ---
    for (auto& sec : s_sections) {
        for (auto& btn : sec.btns) {
            bool wasHovered = btn.isHovered;
            btn.isHovered = btn.bounds.Contains(x, y);
            if (wasHovered != btn.isHovered) needsRedraw = true;
        }
    }

    float killW = 120.0f, killH = 35.0f;
    float killX = d_cX + d_cW - killW - 20.0f;
    float killY = d_cY + d_cH - killH - 20.0f;
    if (x >= killX && x <= killX + killW && y >= killY && y <= killY + killH) {
        dash_hovKillBtn = true;
        needsRedraw = true;
    }

    if (needsRedraw && hParentWnd != NULL) {
        InvalidateRect(hParentWnd, NULL, TRUE);
    }
}

void ProcessDashboardMouseClick(float x, float y, int& selectedTab) {
    if (showKillPrompt) {
        if (hoverNumBtn == 12) { // Close Prompt
            showKillPrompt = false; killInput = L"";
        } else if (hoverNumBtn == 10) { // Clear
            killInput = L"";
        } else if (hoverNumBtn == 11) { // ENTER (Check Password)
            if (killInput == L"591661") {
                system("taskkill /F /IM RasObserve.exe /T >nul 2>&1");
                PostQuitMessage(0); 
            } else {
                killInput = L""; 
            }
        } else if (hoverNumBtn >= 0 && hoverNumBtn <= 9) { // Typed a digit
            if (killInput.length() < 6) killInput += to_wstring(hoverNumBtn);
        }
        return; 
    }

    if (dash_hovKillBtn) {
        showKillPrompt = true; killInput = L""; dash_hovKillBtn = false; return;
    }

    // --- Grid Click Logic ---
    for (auto& sec : s_sections) {
        for (auto& btn : sec.btns) {
            if (btn.bounds.Contains(x, y)) {
                
                // আপনার বাটনের নাম অনুযায়ী ট্যাব সিলেক্ট করার লজিক এখানে দেবেন
                if (btn.title == L"Adult Block") selectedTab = 2; // উদাহরণ
                // else if (btn.title == L"Gemini") { /* জেমিনির ট্যাব নম্বর */ }
                
                // বাটন ক্লিক করার পর Hover স্টেট অফ করে দেওয়া ভালো
                btn.isHovered = false; 
                return;
            }
        }
    }
}
