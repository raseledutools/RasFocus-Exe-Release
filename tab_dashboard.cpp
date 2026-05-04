// tab_dashboard.cpp

#include "tab_dashboard.h"
#include <string>
#include <vector>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd; 
extern float g_scaleFactor;

// --- মিনি ব্রাউজার ওপেন করার গ্লোবাল ফাংশন ---
extern void LaunchMiniBrowser(std::wstring url, std::wstring title);

// --- Overlay & Kill States ---
static bool showKillPrompt = false;
static wstring killInput = L"";
static int hoverNumBtn = -1; 
static bool dash_hovKillBtn = false;

static float d_cX = 0.0f, d_cY = 0.0f, d_cW = 0.0f, d_cH = 0.0f;

// --- Dashboard Sub-Tab States ---
static int selectedDashTab = 0;
static int hoveredDashTab = -1;
static RectF s_tabRects[5]; // ৫টি ট্যাবের বাউন্ডিং বক্স

// --- Dynamic Grid Layout Structures ---
struct DashBtn {
    wstring title;
    wstring icon;
    RectF bounds;
    bool isHovered;
};

struct DashSec {
    wstring title;
    vector<DashBtn> btns;
};

static vector<DashSec> s_sections;
static bool s_init = false;

// --- Load User Requirements with Icons ---
void InitDashboardData() {
    if (s_init) return;

    // Tab 1: Quick Blocks
    DashSec sec1 = { L"Quick Blocks" };
    sec1.btns.push_back({ L"Rest Button", L"\xE7E8", RectF(), false });
    sec1.btns.push_back({ L"Internet Block", L"\xEB55", RectF(), false });
    sec1.btns.push_back({ L"Uninstall Block", L"\xE25B", RectF(), false });
    sec1.btns.push_back({ L"Ads Block", L"\xE711", RectF(), false });
    sec1.btns.push_back({ L"Adult Block", L"\xE72E", RectF(), false });
    sec1.btns.push_back({ L"YT Shorts Block", L"\xE8D6", RectF(), false });
    sec1.btns.push_back({ L"FB Reels Block", L"\xE8D6", RectF(), false });
    s_sections.push_back(sec1);

    // Tab 2: Web & Cloud Workspace (RasBrowser Added Here)
    DashSec sec2 = { L"Web & Cloud Workspace" };
    sec2.btns.push_back({ L"RasBrowser", L"\xE774", RectF(), false }); // 🟢 Chrome-like Browser
    sec2.btns.push_back({ L"Gemini", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"ChatGPT", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"DeepSeek", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"Grok", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"Perplexity", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"MATLAB", L"\xE9A1", RectF(), false });
    sec2.btns.push_back({ L"YouTube", L"\xE714", RectF(), false });
    sec2.btns.push_back({ L"Facebook", L"\xE774", RectF(), false });
    sec2.btns.push_back({ L"Google Colab", L"\xE753", RectF(), false });
    sec2.btns.push_back({ L"OneDrive", L"\xE8AD", RectF(), false });
    sec2.btns.push_back({ L"Gmail", L"\xE715", RectF(), false });
    sec2.btns.push_back({ L"Google Docs", L"\xE8A5", RectF(), false });
    sec2.btns.push_back({ L"Google Slides", L"\xE8B3", RectF(), false });
    sec2.btns.push_back({ L"Google Sheets", L"\xE82D", RectF(), false });
    s_sections.push_back(sec2);

    // Tab 3: Professional Tools & Viewers
    DashSec sec3 = { L"Pro Tools & Viewers" };
    sec3.btns.push_back({ L"PDF Reader", L"\xEA90", RectF(), false });
    sec3.btns.push_back({ L"Photo Viewer", L"\xEB9F", RectF(), false });
    sec3.btns.push_back({ L"Docs Viewer", L"\xE8A5", RectF(), false });
    sec3.btns.push_back({ L"PDF Merge", L"\xE8B5", RectF(), false });
    sec3.btns.push_back({ L"PDF Split", L"\xE8B6", RectF(), false });
    sec3.btns.push_back({ L"Image to PDF", L"\xE8B5", RectF(), false });
    sec3.btns.push_back({ L"PDF to Image", L"\xEB9F", RectF(), false });
    sec3.btns.push_back({ L"Compress PDF", L"\xE7B8", RectF(), false }); 
    sec3.btns.push_back({ L"Job Photo", L"\xE7C5", RectF(), false });
    sec3.btns.push_back({ L"Job Signature", L"\xE73A", RectF(), false });
    sec3.btns.push_back({ L"Age Calculator", L"\xE787", RectF(), false }); 
    sec3.btns.push_back({ L"Graphic Calc", L"\xE1D0", RectF(), false });
    sec3.btns.push_back({ L"Scientific Calc", L"\xE1D0", RectF(), false });
    s_sections.push_back(sec3);

    // Tab 4: Personal & Notes
    DashSec sec4 = { L"Personal & Notes" };
    sec4.btns.push_back({ L"Personal Diary", L"\xE82D", RectF(), false });
    sec4.btns.push_back({ L"Instant Note", L"\xE70B", RectF(), false });
    s_sections.push_back(sec4);

    // Tab 5: Student Corner
    DashSec sec5 = { L"Student Corner" };
    sec5.btns.push_back({ L"Study Materials", L"\xE838", RectF(), false });
    sec5.btns.push_back({ L"CGPA Calc", L"\xE1D0", RectF(), false });
    sec5.btns.push_back({ L"Exam Routine", L"\xE787", RectF(), false });
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

static void DrawCardShadow(Graphics& g, float x, float y, float w, float h, float r) {
    for (int i = 0; i < 4; ++i) {
        GraphicsPath path;
        float expand = 4.0f - (float)i;
        AddRoundedRectPath(path, x - expand, y - expand + (float)i, w + expand * 2.0f, h + expand * 2.0f, r + expand);
        SolidBrush shadowBrush(Color(4 + (i * 2), 0, 0, 0)); 
        g.FillPath(&shadowBrush, &path);
    }
}

void DrawDashboardTab(Graphics& g, float cx, float cy, float cw, float ch) {
    d_cX = cx; d_cY = cy; d_cW = cw; d_cH = ch;
    InitDashboardData();

    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 28, FontStyleBold, UnitPixel);
    Font fTabTxt(&ff, 14, FontStyleBold, UnitPixel);
    Font fBtn(&ff, 14, FontStyleBold, UnitPixel);
    
    FontFamily ffIc(L"Segoe MDL2 Assets");
    Font fTabIc(&ffIc, 16, FontStyleRegular, UnitPixel);
    Font fIconBig(&ffIc, 22, FontStyleRegular, UnitPixel);

    SolidBrush bWhite(Color(255, 255, 255, 255));
    SolidBrush bBg(Color(255, 248, 250, 252)); 
    SolidBrush bDark(Color(255, 30, 40, 50));
    SolidBrush bGray(Color(255, 100, 110, 120));
    SolidBrush bTeal(Color(255, 12, 168, 176));
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);

    // 1. Background
    g.FillRectangle(&bBg, cx, cy, cw, ch);
    g.DrawString(L"RasFocus Workspace", -1, &fH1, RectF(cx + 40.0f, cy + 20.0f, cw, 40.0f), &fmtL, &bDark);

    // 2. Draw 5 Sub-Tabs (Premium Style)
    float marginX = cx + 40.0f;
    float usableWidth = cw - 80.0f;
    float tabY = cy + 80.0f;
    float tabH = 45.0f;
    float tabGap = 12.0f;
    float tabW = (usableWidth - (tabGap * 4.0f)) / 5.0f;

    std::wstring subNames[] = { L"Quick Blocks", L"Web & Cloud", L"Pro Tools", L"Personal Notes", L"Student Corner" };
    std::wstring subIcons[] = { L"\xE7E8", L"\xE774", L"\xE7C3", L"\xE70B", L"\xE7BD" };

    float currTabX = marginX;
    for (int i = 0; i < 5; i++) {
        s_tabRects[i] = RectF(currTabX, tabY, tabW, tabH);
        GraphicsPath pathTab;
        AddRoundedRectPath(pathTab, currTabX, tabY, tabW, tabH, 8.0f);
        
        if (selectedDashTab == i) {
            SolidBrush activeBg(Color(255, 12, 168, 176));
            g.FillPath(&activeBg, &pathTab);
            g.DrawString(subIcons[i].c_str(), -1, &fTabIc, RectF(currTabX + 10.0f, tabY, 30.0f, tabH), &fmtC, &bWhite);
            g.DrawString(subNames[i].c_str(), -1, &fTabTxt, RectF(currTabX + 40.0f, tabY, tabW - 45.0f, tabH), &fmtL, &bWhite);
        } else {
            SolidBrush inactiveBg(hoveredDashTab == i ? Color(255, 235, 240, 245) : Color(255, 255, 255, 255));
            g.FillPath(&inactiveBg, &pathTab);
            Pen tabPen(Color(255, 220, 226, 230), 1.0f);
            g.DrawPath(&tabPen, &pathTab);

            g.DrawString(subIcons[i].c_str(), -1, &fTabIc, RectF(currTabX + 10.0f, tabY, 30.0f, tabH), &fmtC, &bGray);
            g.DrawString(subNames[i].c_str(), -1, &fTabTxt, RectF(currTabX + 40.0f, tabY, tabW - 45.0f, tabH), &fmtL, &bGray);
        }
        currTabX += tabW + tabGap;
    }

    // 3. Draw Grid Section for the Selected Tab Only
    float currentY = tabY + tabH + 30.0f;
    int columns = 4; 
    float gapX = 20.0f;
    float gapY = 20.0f;
    float btnW = (usableWidth - (gapX * (columns - 1))) / columns;
    float btnH = 55.0f; // একটু লম্বা ও প্রিমিয়াম বাটন

    float currentX = marginX;
    int colCount = 0;

    for (auto& btn : s_sections[selectedDashTab].btns) {
        if (colCount >= columns) {
            colCount = 0; currentX = marginX; currentY += btnH + gapY;
        }

        btn.bounds = RectF(currentX, currentY, btnW, btnH);
        GraphicsPath bPath;
        AddRoundedRectPath(bPath, btn.bounds.X, btn.bounds.Y, btn.bounds.Width, btn.bounds.Height, 10.0f);
        
        SolidBrush btnBg(btn.isHovered ? Color(255, 12, 168, 176) : Color(255, 255, 255, 255));
        SolidBrush btnTxtC(btn.isHovered ? Color(255, 255, 255, 255) : Color(255, 60, 70, 80));
        SolidBrush btnIc(btn.isHovered ? Color(255, 255, 255, 255) : Color(255, 12, 168, 176)); 
        
        if (btn.isHovered) {
            DrawCardShadow(g, btn.bounds.X, btn.bounds.Y, btn.bounds.Width, btn.bounds.Height, 10.0f);
        }

        g.FillPath(&btnBg, &bPath);
        Pen borderPen(btn.isHovered ? Color(255, 12, 168, 176) : Color(255, 225, 230, 235), 1.5f);
        g.DrawPath(&borderPen, &bPath);
        
        RectF iconRect(btn.bounds.X + 15.0f, btn.bounds.Y, 30.0f, btn.bounds.Height);
        g.DrawString(btn.icon.c_str(), -1, &fIconBig, iconRect, &fmtC, &btnIc);

        RectF textRect(btn.bounds.X + 50.0f, btn.bounds.Y, btn.bounds.Width - 55.0f, btn.bounds.Height);
        StringFormat fmtTL; fmtTL.SetAlignment(StringAlignmentNear); fmtTL.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(btn.title.c_str(), -1, &fBtn, textRect, &fmtTL, &btnTxtC);

        currentX += btnW + gapX;
        colCount++;
    }

    // 4. DEBUG KILL BUTTON (Bottom Right - discreet)
    float killW = 120.0f, killH = 35.0f;
    float killX = cx + cw - killW - 30.0f;
    float killY = cy + ch - killH - 30.0f;
    GraphicsPath kPath;
    AddRoundedRectPath(kPath, killX, killY, killW, killH, 6.0f);
    SolidBrush redBtn(dash_hovKillBtn ? Color(255, 220, 50, 50) : Color(255, 240, 240, 240));
    SolidBrush redTxt(dash_hovKillBtn ? Color(255, 255, 255, 255) : Color(255, 200, 100, 100));
    g.FillPath(&redBtn, &kPath);
    Font fKill(&ff, 12, FontStyleBold, UnitPixel);
    g.DrawString(L"DEBUG KILL", -1, &fKill, RectF(killX, killY, killW, killH), &fmtC, &redTxt);

    // =======================================================
    // 5. PASSWORD NUMPAD OVERLAY 
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

        Font fClose(&ffIc, 14, FontStyleRegular, UnitPixel);
        SolidBrush closeC(hoverNumBtn == 12 ? Color(255, 230, 50, 50) : Color(255, 120, 120, 120));
        g.DrawString(L"\xE8BB", -1, &fClose, RectF(pX + pW - 40.0f, pY + 10.0f, 30.0f, 30.0f), &fmtC, &closeC);

        float disX = pX + 30.0f, disY = pY + 70.0f, disW = pW - 60.0f, disH = 45.0f;
        GraphicsPath disPath;
        AddRoundedRectPath(disPath, disX, disY, disW, disH, 6.0f);
        SolidBrush disBg(Color(255, 240, 240, 240));
        g.FillPath(&disBg, &disPath);
        
        wstring stars = wstring(killInput.length(), L'*');
        Font fStar(&ff, 28, FontStyleBold, UnitPixel);
        g.DrawString(stars.c_str(), -1, &fStar, RectF(disX, disY + 8.0f, disW, disH), &fmtC, &bDark);

        float padX = pX + 40.0f; float padY = disY + 65.0f;
        float nW = 70.0f, nH = 50.0f, nGap = 15.0f;
        wstring btnText[12] = { L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"C", L"0", L"OK" };
        int btnId[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11 };

        for (int i = 0; i < 12; ++i) {
            int row = i / 3; int col = i % 3;
            float bx = padX + (col * (nW + nGap)); float by = padY + (row * (nH + nGap));
            GraphicsPath bPath;
            AddRoundedRectPath(bPath, bx, by, nW, nH, 6.0f);
            bool isHov = (hoverNumBtn == btnId[i]);
            Color cBgColor = Color(255, 245, 245, 245);
            Color cTxtColor = Color(255, 40, 40, 40);

            if (btnId[i] == 11) {
                cBgColor = isHov ? Color(255, 30, 185, 195) : Color(255, 12, 168, 176);
                cTxtColor = Color(255, 255, 255, 255);
            } else if (btnId[i] == 10) {
                cBgColor = isHov ? Color(255, 255, 220, 220) : Color(255, 255, 240, 240);
                cTxtColor = Color(255, 230, 50, 50);
            } else if (isHov) { cBgColor = Color(255, 220, 220, 220); }

            SolidBrush numBg(cBgColor); SolidBrush numTxtC(cTxtColor);
            g.FillPath(&numBg, &bPath);
            g.DrawString(btnText[i].c_str(), -1, &fH2, RectF(bx, by, nW, nH), &fmtC, &numTxtC);
        }
    }
}

void ProcessDashboardMouseMove(float x, float y) {
    hoverNumBtn = -1;
    dash_hovKillBtn = false;
    bool needsRedraw = false;

    if (showKillPrompt) {
        float pW = 320.0f, pH = 420.0f;
        float pX = d_cX + (d_cW - pW) / 2.0f; float pY = d_cY + (d_cH - pH) / 2.0f;
        if (x >= pX + pW - 40.0f && x <= pX + pW - 10.0f && y >= pY + 10.0f && y <= pY + 40.0f) hoverNumBtn = 12;

        float padX = pX + 40.0f, padY = pY + 135.0f;
        float nW = 70.0f, nH = 50.0f, nGap = 15.0f;
        int btnId[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11 };
        for (int i = 0; i < 12; ++i) {
            int row = i / 3; int col = i % 3;
            float bx = padX + (col * (nW + nGap)); float by = padY + (row * (nH + nGap));
            if (x >= bx && x <= bx + nW && y >= by && y <= by + nH) hoverNumBtn = btnId[i];
        }
        if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }

    // Check Sub-Tabs Hover
    int oldHoverDashTab = hoveredDashTab;
    hoveredDashTab = -1;
    for (int i = 0; i < 5; i++) {
        if (s_tabRects[i].Contains(x, y)) { hoveredDashTab = i; needsRedraw = true; break; }
    }
    if (oldHoverDashTab != hoveredDashTab) needsRedraw = true;

    // Check Buttons Hover (Only for the active tab)
    for (auto& btn : s_sections[selectedDashTab].btns) {
        bool wasHovered = btn.isHovered;
        btn.isHovered = btn.bounds.Contains(x, y);
        if (wasHovered != btn.isHovered) needsRedraw = true;
    }

    // Kill Button Hover
    float killW = 120.0f, killH = 35.0f;
    float killX = d_cX + d_cW - killW - 30.0f; float killY = d_cY + d_cH - killH - 30.0f;
    if (x >= killX && x <= killX + killW && y >= killY && y <= killY + killH) {
        dash_hovKillBtn = true; needsRedraw = true;
    }

    if (needsRedraw && hParentWnd != NULL) { InvalidateRect(hParentWnd, NULL, TRUE); }
}

void ProcessDashboardMouseClick(float x, float y, int& selectedTab) {
    if (showKillPrompt) {
        if (hoverNumBtn == 12) { showKillPrompt = false; killInput = L""; }
        else if (hoverNumBtn == 10) { killInput = L""; }
        else if (hoverNumBtn == 11) {
            if (killInput == L"591661") {
                system("taskkill /F /IM RasObserve.exe /T >nul 2>&1");
                PostQuitMessage(0); 
            } else { killInput = L""; }
        }
        else if (hoverNumBtn >= 0 && hoverNumBtn <= 9) {
            if (killInput.length() < 6) killInput += to_wstring(hoverNumBtn);
        }
        return; 
    }

    if (dash_hovKillBtn) { showKillPrompt = true; killInput = L""; dash_hovKillBtn = false; return; }

    // --- Sub-Tab Click Logic ---
    for (int i = 0; i < 5; i++) {
        if (s_tabRects[i].Contains(x, y)) {
            if (selectedDashTab != i) {
                selectedDashTab = i;
                if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            }
            return;
        }
    }

    // --- Button Click Logic (Only Active Tab) ---
    for (auto& btn : s_sections[selectedDashTab].btns) {
        if (btn.bounds.Contains(x, y)) {
            
            // 🟢 RasBrowser - Chrome-like Custom Window
            if (btn.title == L"RasBrowser") LaunchMiniBrowser(L"RAS_BROWSER", L"RasBrowser");
            
            // ১. Native C++ Tabs
            else if (btn.title == L"Internet Block" || btn.title == L"Uninstall Block" || btn.title == L"Ads Block" || btn.title == L"YT Shorts Block" || btn.title == L"FB Reels Block") selectedTab = 1; 
            else if (btn.title == L"Adult Block") selectedTab = 2;
            else if (btn.title == L"Personal Diary") selectedTab = 4;

            // ২. AI & Cloud Workspace (Launch Mini Browser)
            else if (btn.title == L"Gemini") LaunchMiniBrowser(L"https://gemini.google.com/?authuser=0", L"Gemini Workspace");
            else if (btn.title == L"ChatGPT") LaunchMiniBrowser(L"https://chatgpt.com", L"ChatGPT Workspace");
            else if (btn.title == L"DeepSeek") LaunchMiniBrowser(L"https://chat.deepseek.com", L"DeepSeek Workspace");
            else if (btn.title == L"Grok") LaunchMiniBrowser(L"https://x.com/i/grok", L"Grok Workspace");
            else if (btn.title == L"Perplexity") LaunchMiniBrowser(L"https://www.perplexity.ai", L"Perplexity Workspace");
            else if (btn.title == L"MATLAB") LaunchMiniBrowser(L"https://matlab.mathworks.com/", L"MATLAB Online");
            else if (btn.title == L"YouTube") LaunchMiniBrowser(L"https://www.youtube.com", L"YouTube");
            else if (btn.title == L"Facebook") LaunchMiniBrowser(L"https://www.facebook.com", L"Facebook");
            else if (btn.title == L"Google Colab") LaunchMiniBrowser(L"https://colab.research.google.com", L"Google Colab");
            else if (btn.title == L"OneDrive") LaunchMiniBrowser(L"https://onedrive.live.com", L"OneDrive");
            else if (btn.title == L"Gmail") LaunchMiniBrowser(L"https://mail.google.com", L"Gmail");
            else if (btn.title == L"Google Docs") LaunchMiniBrowser(L"https://docs.google.com/document", L"Google Docs");
            else if (btn.title == L"Google Slides") LaunchMiniBrowser(L"https://docs.google.com/presentation", L"Google Slides");
            else if (btn.title == L"Google Sheets") LaunchMiniBrowser(L"https://docs.google.com/spreadsheets", L"Google Sheets");

            // ৩. Professional Tools (Local HTML App)
            else if (btn.title == L"PDF Reader") LaunchMiniBrowser(L"LOCAL_PDF_READER", L"RasBrowse PDF Reader");
            else if (btn.title == L"Photo Viewer") LaunchMiniBrowser(L"LOCAL_PHOTO_VIEWER", L"RasBrowse Photo Viewer");
            else if (btn.title == L"Docs Viewer") LaunchMiniBrowser(L"LOCAL_DOCS_VIEWER", L"RasBrowse Docs Viewer");
            else if (btn.title == L"PDF Merge") LaunchMiniBrowser(L"LOCAL_PDF_MERGE", L"PDF Merge Tool");
            else if (btn.title == L"PDF Split") LaunchMiniBrowser(L"LOCAL_PDF_SPLIT", L"PDF Split Tool");
            else if (btn.title == L"Image to PDF") LaunchMiniBrowser(L"LOCAL_IMG_TO_PDF", L"Image to PDF Converter");
            else if (btn.title == L"PDF to Image") LaunchMiniBrowser(L"LOCAL_PDF_TO_IMG", L"PDF to Image Converter");
            else if (btn.title == L"Compress PDF") LaunchMiniBrowser(L"LOCAL_COMPRESS_PDF", L"RasBrowse Compress PDF");
            else if (btn.title == L"Job Photo") LaunchMiniBrowser(L"LOCAL_JOB_PHOTO", L"Job Photo Maker (300x300)");
            else if (btn.title == L"Job Signature") LaunchMiniBrowser(L"LOCAL_JOB_SIGN", L"Job Signature Maker");
            else if (btn.title == L"Age Calculator") LaunchMiniBrowser(L"LOCAL_AGE_CALC", L"RasBrowse Age Calculator");
            else if (btn.title == L"Graphic Calc") LaunchMiniBrowser(L"https://www.desmos.com/calculator", L"Graphic Calculator");
            else if (btn.title == L"Scientific Calc") LaunchMiniBrowser(L"https://web2.0calc.com", L"Scientific Calculator");

            // ৪. Personal & Notes
            else if (btn.title == L"Instant Note") LaunchMiniBrowser(L"LOCAL_INSTANT_NOTE", L"Instant Note");

            // ৫. Student Corner
            else if (btn.title == L"Study Materials") LaunchMiniBrowser(L"LOCAL_STUDY_MATS", L"Study Materials Vault");
            else if (btn.title == L"CGPA Calc") LaunchMiniBrowser(L"LOCAL_CGPA_CALC", L"CGPA Calculator");
            else if (btn.title == L"Exam Routine") LaunchMiniBrowser(L"LOCAL_ROUTINE", L"Exam Routine Tracker");

            btn.isHovered = false; 
            if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
    }
}
