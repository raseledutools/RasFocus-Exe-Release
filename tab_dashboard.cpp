#include "tab_dashboard.h"
#include <string>
#include <vector>

using namespace Gdiplus;
using namespace std;

// --- Dashboard Hover States ---
static bool dash_hovBlocks = false;
static bool dash_hovAdult = false;
static bool ds_hovDeepStudy = false;
static bool dash_hovStartEasy = false;
static bool dash_hovKillBtn = false;

// --- Password Overlay States ---
static bool showKillPrompt = false;
static wstring killInput = L"";
static int hoverNumBtn = -1; // 0-9 for digits, 10 for Clear, 11 for Enter, 12 for Close

// --- Dashboard Layout Variables ---
static float d_cX = 0.0f, d_cY = 0.0f, d_cW = 0.0f, d_cH = 0.0f;

// --- Helper: Rounded Rectangle Path ---
static void AddRoundedRectPath(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    if (d > w) d = w;
    if (d > h) d = h;
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
        float offset = (float)i; // Shadow goes slightly downwards
        float sx = x - expand;
        float sy = y - expand + offset; 
        float sw = w + expand * 2.0f;
        float sh = h + expand * 2.0f;
        
        AddRoundedRectPath(path, sx, sy, sw, sh, r + expand);
        SolidBrush shadowBrush(Color(4 + (i * 2), 0, 0, 0)); 
        g.FillPath(&shadowBrush, &path);
    }
}

void DrawDashboardTab(Graphics& g, float cx, float cy, float cw, float ch) {
    d_cX = cx; d_cY = cy; d_cW = cw; d_cH = ch;

    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 32, FontStyleBold, UnitPixel);
    Font fH2(&ff, 20, FontStyleBold, UnitPixel);
    Font fSub(&ff, 16, FontStyleRegular, UnitPixel);
    Font fBtn(&ff, 16, FontStyleBold, UnitPixel);
    
    FontFamily ffIc(L"Segoe MDL2 Assets");
    Font fIcBig(&ffIc, 28, FontStyleRegular, UnitPixel);

    SolidBrush bWhite(Color(255, 255, 255, 255));
    SolidBrush bBg(Color(255, 248, 250, 252)); // Light Off-White Background
    SolidBrush bDark(Color(255, 40, 40, 40));
    SolidBrush bGray(Color(255, 120, 120, 120));
    SolidBrush bTeal(Color(255, 12, 168, 176));
    
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);

    // 1. Background Content Area
    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // 2. Main Title (Breathing space updated)
    g.DrawString(L"Dashboard", -1, &fH1, RectF(cx + 40.0f, cy + 30.0f, cw - 80.0f, 40.0f), &fmtL, &bDark);
    g.DrawString(L"Welcome back! Your productivity control center is ready.", -1, &fSub, RectF(cx + 42.0f, cy + 70.0f, cw - 80.0f, 25.0f), &fmtL, &bGray);

    // --- 3. HERO BANNER (With Drop Shadow) ---
    float banY = cy + 130.0f;
    float banH = 140.0f;
    float banW = cw - 80.0f;
    float rad = 12.0f;
    
    DrawCardShadow(g, cx + 40.0f, banY, banW, banH, rad);
    
    GraphicsPath heroPath;
    AddRoundedRectPath(heroPath, cx + 40.0f, banY, banW, banH, rad);
    g.FillPath(&bTeal, &heroPath);
    
    Font fBTitle(&ff, 26, FontStyleBold, UnitPixel);
    g.DrawString(L"Ready for Deep Work?", -1, &fBTitle, RectF(cx + 70.0f, banY + 40.0f, banW - 250.0f, 35.0f), &fmtL, &bWhite);
    g.DrawString(L"Start an instant blocking session to eliminate all distractions.", -1, &fSub, RectF(cx + 70.0f, banY + 75.0f, banW - 250.0f, 25.0f), &fmtL, &bWhite);

    // Start Button inside banner
    float btnW = 180.0f; float btnH = 45.0f;
    float btnX = cx + 40.0f + banW - btnW - 30.0f;
    float btnY = banY + (banH - btnH) / 2.0f;

    GraphicsPath sBtnPath;
    AddRoundedRectPath(sBtnPath, btnX, btnY, btnW, btnH, 8.0f);
    SolidBrush sBtnBg(dash_hovStartEasy ? Color(255, 240, 248, 250) : Color(255, 255, 255, 255));
    g.FillPath(&sBtnBg, &sBtnPath);
    g.DrawString(L"Start Easy Session", -1, &fBtn, RectF(btnX, btnY, btnW, btnH), &fmtC, &bTeal);

    // --- 4. QUICK ACTIONS & SETUP (Modern Cards) ---
    float navY = banY + banH + 45.0f;
    g.DrawString(L"Quick Actions & Setup", -1, &fH2, RectF(cx + 40.0f, navY, cw - 80.0f, 30.0f), &fmtL, &bDark);
    
    float nCardY = navY + 45.0f;
    float nCardH = 150.0f;
    float gap = 25.0f;
    float cardW = (cw - 80.0f - (gap * 2.0f)) / 3.0f;

    auto DrawFeatureCard = [&](float x, const wchar_t* ic, const wchar_t* title, const wchar_t* sub, bool isHover) {
        DrawCardShadow(g, x, nCardY, cardW, nCardH, rad);
        
        GraphicsPath cPath;
        AddRoundedRectPath(cPath, x, nCardY, cardW, nCardH, rad);
        g.FillPath(&bWhite, &cPath);
        
        // Circular Background for Icon
        float cSize = 50.0f;
        float cX = x + (cardW - cSize) / 2.0f;
        float cY_icon = nCardY + 25.0f;
        SolidBrush cBg(isHover ? Color(255, 12, 168, 176) : Color(30, 12, 168, 176));
        SolidBrush iCol(isHover ? Color(255, 255, 255, 255) : Color(255, 12, 168, 176));
        
        g.FillEllipse(&cBg, cX, cY_icon, cSize, cSize);
        g.DrawString(ic, -1, &fIcBig, RectF(cX, cY_icon, cSize, cSize), &fmtC, &iCol);

        g.DrawString(title, -1, &fBtn, RectF(x, nCardY + 85.0f, cardW, 25.0f), &fmtC, &bDark);
        g.DrawString(sub, -1, &fSub, RectF(x + 10.0f, nCardY + 110.0f, cardW - 20.0f, 20.0f), &fmtC, &bGray);
    };

    DrawFeatureCard(cx + 40.0f, L"\xEA18", L"App & Web Blocks", L"Manage your blocklists", dash_hovBlocks);
    DrawFeatureCard(cx + 40.0f + cardW + gap, L"\xE72E", L"Adult Filter", L"Safe browsing setup", dash_hovAdult);
    DrawFeatureCard(cx + 40.0f + (cardW + gap) * 2.0f, L"\xE7B3", L"Deep Study", L"Pomodoro & focus mode", ds_hovDeepStudy);

    // --- 5. DEBUG KILL BUTTON (Rounded) ---
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
    // 6. PASSWORD NUMPAD OVERLAY (Security Feature)
    // =======================================================
    if (showKillPrompt) {
        // Dark Transparent Overlay
        SolidBrush overBg(Color(220, 10, 15, 20));
        g.FillRectangle(&overBg, cx, cy, cw, ch);

        float pW = 320.0f, pH = 420.0f;
        float pX = cx + (cw - pW) / 2.0f;
        float pY = cy + (ch - pH) / 2.0f;

        DrawCardShadow(g, pX, pY, pW, pH, 15.0f);
        GraphicsPath popPath;
        AddRoundedRectPath(popPath, pX, pY, pW, pH, 15.0f);
        g.FillPath(&bWhite, &popPath);

        g.DrawString(L"Enter Debug Password", -1, &fH2, RectF(pX, pY + 20.0f, pW, 30.0f), &fmtC, &bDark);

        // Close Pop-up Button
        Font fClose(&ffIc, 14, FontStyleRegular, UnitPixel);
        // FIXED: Using direct Color object instead of .GetColor()
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
            int row = i / 3;
            int col = i % 3;
            float bx = padX + (col * (nW + nGap));
            float by = padY + (row * (nH + nGap));

            GraphicsPath bPath;
            AddRoundedRectPath(bPath, bx, by, nW, nH, 6.0f);

            bool isHov = (hoverNumBtn == btnId[i]);
            
            // FIXED: Replaced .GetColor() calls with direct Color assignments
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
    // Reset hovers
    dash_hovBlocks = dash_hovAdult = ds_hovDeepStudy = dash_hovStartEasy = dash_hovKillBtn = false;
    hoverNumBtn = -1;

    // --- Overlay Active Mode (Blocks background hovers) ---
    if (showKillPrompt) {
        float pW = 320.0f, pH = 420.0f;
        float pX = d_cX + (d_cW - pW) / 2.0f;
        float pY = d_cY + (d_cH - pH) / 2.0f;

        // Close Button
        if (x >= pX + pW - 40.0f && x <= pX + pW - 10.0f && y >= pY + 10.0f && y <= pY + 40.0f) hoverNumBtn = 12;

        // Numpad Grid
        float padX = pX + 40.0f, padY = pY + 135.0f;
        float nW = 70.0f, nH = 50.0f, nGap = 15.0f;
        int btnId[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11 };

        for (int i = 0; i < 12; ++i) {
            int row = i / 3; int col = i % 3;
            float bx = padX + (col * (nW + nGap));
            float by = padY + (row * (nH + nGap));
            if (x >= bx && x <= bx + nW && y >= by && y <= by + nH) {
                hoverNumBtn = btnId[i];
            }
        }
        return; // Skip normal dashboard hovers
    }

    // --- Normal Dashboard Mode ---
    float banY = d_cY + 130.0f;
    float banH = 140.0f;
    float btnW = 180.0f, btnH = 45.0f;
    float btnX = d_cX + 40.0f + (d_cW - 80.0f) - btnW - 30.0f;
    float btnY = banY + (banH - btnH) / 2.0f;

    if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH) dash_hovStartEasy = true;

    float navY = banY + banH + 45.0f;
    float nCardY = navY + 45.0f;
    float nCardH = 150.0f;
    float gap = 25.0f;
    float cardW = (d_cW - 80.0f - (gap * 2.0f)) / 3.0f;

    if (x >= d_cX + 40.0f && x <= d_cX + 40.0f + cardW && y >= nCardY && y <= nCardY + nCardH) dash_hovBlocks = true;
    if (x >= d_cX + 40.0f + cardW + gap && x <= d_cX + 40.0f + cardW * 2.0f + gap && y >= nCardY && y <= nCardY + nCardH) dash_hovAdult = true;
    if (x >= d_cX + 40.0f + (cardW + gap) * 2.0f && x <= d_cX + 40.0f + cardW * 3.0f + gap * 2.0f && y >= nCardY && y <= nCardY + nCardH) ds_hovDeepStudy = true;

    float killW = 120.0f, killH = 35.0f;
    float killX = d_cX + d_cW - killW - 20.0f;
    float killY = d_cY + d_cH - killH - 20.0f;
    if (x >= killX && x <= killX + killW && y >= killY && y <= killY + killH) dash_hovKillBtn = true;
}

void ProcessDashboardMouseClick(float x, float y, int& selectedTab) {
    if (showKillPrompt) {
        if (hoverNumBtn == 12) { // Close Prompt
            showKillPrompt = false;
            killInput = L"";
        }
        else if (hoverNumBtn == 10) { // Clear
            killInput = L"";
        }
        else if (hoverNumBtn == 11) { // ENTER (Check Password)
            if (killInput == L"591661") {
                system("taskkill /F /IM RasObserve.exe /T >nul 2>&1");
                PostQuitMessage(0); // Closes the app completely
            } else {
                killInput = L""; // Incorrect pass, clear the field
            }
        }
        else if (hoverNumBtn >= 0 && hoverNumBtn <= 9) { // Typed a digit
            if (killInput.length() < 6) {
                killInput += to_wstring(hoverNumBtn);
            }
        }
        return; // Do not process background clicks
    }

    if (dash_hovKillBtn) {
        showKillPrompt = true;
        killInput = L""; // reset pass
        dash_hovKillBtn = false;
        return;
    }

    if (dash_hovBlocks) selectedTab = 1;
    else if (dash_hovAdult) selectedTab = 2; 
    else if (ds_hovDeepStudy) selectedTab = 3; 
    else if (dash_hovStartEasy) selectedTab = 1; 

    // Reset hovers immediately to prevent ghost-clicking later
    dash_hovBlocks = dash_hovAdult = ds_hovDeepStudy = dash_hovStartEasy = false;
}