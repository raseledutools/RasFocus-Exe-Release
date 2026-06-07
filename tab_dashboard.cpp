// tab_dashboard.cpp

#pragma warning(disable : 4996)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)

#include "tab_dashboard.h"
#include "browser/mini_browser.h" // 🟢 FIX: লিংকার এরর এড়াতে এটি অবশ্যই থাকতে হবে
#include "tab_blocks.h"           // SetBlockSubTab এর জন্য
#include <string>
#include <vector>
#include <ctime>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd; 
extern float g_scaleFactor;

// 🟢 FIX: PDF পপ-আপ ইঞ্জিন কল করার জন্য ডিক্লেয়ারেশন
extern void LaunchFoxitStylePdfReader(std::wstring pdfPath);

// --- Overlay & Kill States ---
static bool showKillPrompt = false;
static wstring killInput = L"";
static int hoverNumBtn = -1; 
static bool dash_hovKillBtn = false;

static float d_cX = 0.0f, d_cY = 0.0f, d_cW = 0.0f, d_cH = 0.0f;

// --- 4 Big Action Card States ---
struct ActionCard {
    wstring title;
    wstring subtitle;
    RectF bounds;
    bool isHovered;
};
static ActionCard s_actionCards[4];
static bool s_actionCardsInit = false;

// --- Dashboard Sub-Tab States ---
static int selectedDashTab = 0;
static int hoveredDashTab = -1;
static RectF s_tabRects[5]; // ৫টি ট্যাবের বাউন্ডিং বক্স

// --- Dynamic Grid Layout Structures ---
struct DashBtn {
    wstring title;
    wstring subtext; // New: Subtext for premium feel
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

    // Tab 1: Quick Blocks (Action Oriented)
    DashSec sec1 = { L"Quick Blocks" };
    sec1.btns.push_back({ L"Rest Button", L"Take a break", L"\xE7E8", RectF(), false });
    sec1.btns.push_back({ L"Internet Block", L"Disable all traffic", L"\xEB55", RectF(), false });
    sec1.btns.push_back({ L"Uninstall Block", L"Secure settings", L"\xE25B", RectF(), false });
    sec1.btns.push_back({ L"Ads Block", L"Remove distractions", L"\xE711", RectF(), false });
    sec1.btns.push_back({ L"Adult Block", L"Filter content", L"\xE72E", RectF(), false });
    sec1.btns.push_back({ L"YT Shorts Block", L"Block short videos", L"\xE8D6", RectF(), false });
    sec1.btns.push_back({ L"FB Reels Block", L"Block social reels", L"\xE8D6", RectF(), false });
    s_sections.push_back(sec1);

    // Tab 2: Web & Cloud Workspace (Tools Oriented)
    DashSec sec2 = { L"Web & Cloud Workspace" };
    sec2.btns.push_back({ L"RasBrowser", L"Secure surfing", L"\xE774", RectF(), false }); 
    sec2.btns.push_back({ L"Gemini", L"AI Assistant", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"ChatGPT", L"AI Assistant", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"DeepSeek", L"AI Assistant", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"Grok", L"AI Assistant", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"Perplexity", L"AI Search", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"MATLAB", L"Math workspace", L"\xE9A1", RectF(), false });
    sec2.btns.push_back({ L"YouTube", L"Video platform", L"\xE714", RectF(), false });
    sec2.btns.push_back({ L"Facebook", L"Social network", L"\xE774", RectF(), false });
    sec2.btns.push_back({ L"Google Colab", L"Code notebook", L"\xE753", RectF(), false });
    sec2.btns.push_back({ L"OneDrive", L"Cloud storage", L"\xE8AD", RectF(), false });
    sec2.btns.push_back({ L"Gmail", L"Email client", L"\xE715", RectF(), false });
    sec2.btns.push_back({ L"Google Docs", L"Word processor", L"\xE8A5", RectF(), false });
    sec2.btns.push_back({ L"Google Slides", L"Presentations", L"\xE8B3", RectF(), false });
    sec2.btns.push_back({ L"Google Sheets", L"Spreadsheets", L"\xE82D", RectF(), false });
    s_sections.push_back(sec2);

    // Tab 3: Professional Tools & Viewers (Utility Oriented)
    DashSec sec3 = { L"Pro Tools & Viewers" };
    sec3.btns.push_back({ L"PDF Reader", L"View documents", L"\xEA90", RectF(), false });
    sec3.btns.push_back({ L"Photo Viewer", L"View images", L"\xEB9F", RectF(), false });
    sec3.btns.push_back({ L"Docs Viewer", L"Read text files", L"\xE8A5", RectF(), false });
    sec3.btns.push_back({ L"PDF Merge", L"Combine PDFs", L"\xE8B5", RectF(), false });
    sec3.btns.push_back({ L"PDF Split", L"Extract pages", L"\xE8B6", RectF(), false });
    sec3.btns.push_back({ L"Image to PDF", L"Convert pictures", L"\xE8B5", RectF(), false });
    sec3.btns.push_back({ L"PDF to Image", L"Extract pictures", L"\xEB9F", RectF(), false });
    sec3.btns.push_back({ L"Compress PDF", L"Reduce file size", L"\xE7B8", RectF(), false }); 
    sec3.btns.push_back({ L"Job Photo", L"Resize 300x300", L"\xE7C5", RectF(), false });
    sec3.btns.push_back({ L"Job Signature", L"Resize signature", L"\xE73A", RectF(), false });
    sec3.btns.push_back({ L"Age Calculator", L"Calculate exact age", L"\xE787", RectF(), false }); 
    sec3.btns.push_back({ L"Graphic Calc", L"Plot graphs", L"\xE1D0", RectF(), false });
    sec3.btns.push_back({ L"Scientific Calc", L"Advanced math", L"\xE1D0", RectF(), false });
    s_sections.push_back(sec3);

    // Tab 4: Personal & Notes
    DashSec sec4 = { L"Personal & Notes" };
    sec4.btns.push_back({ L"Personal Diary", L"Private journal", L"\xE82D", RectF(), false });
    sec4.btns.push_back({ L"Instant Note", L"Quick jot down", L"\xE70B", RectF(), false });
    s_sections.push_back(sec4);

    // Tab 5: Student Corner
    DashSec sec5 = { L"Student Corner" };
    sec5.btns.push_back({ L"Study Materials", L"Vault & resources", L"\xE838", RectF(), false });
    sec5.btns.push_back({ L"CGPA Calc", L"Grade calculator", L"\xE1D0", RectF(), false });
    sec5.btns.push_back({ L"Exam Routine", L"Schedule tracker", L"\xE787", RectF(), false });
    s_sections.push_back(sec5);

    s_init = true;

    // --- Init 4 Action Cards ---
    if (!s_actionCardsInit) {
        s_actionCards[0] = { L"Create Blocking Profile", L"Set up Schedule, Simple & Emergency blocks", RectF(), false };
        s_actionCards[1] = { L"Advanced Adult Block",    L"Block adult sites & filter content",          RectF(), false };
        s_actionCards[2] = { L"Start Deep Study",        L"Launch a Pomodoro focus session now",         RectF(), false };
        s_actionCards[3] = { L"Allow Only Websites & Apps", L"Whitelist specific sites and applications",RectF(), false };
        s_actionCardsInit = true;
    }
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

// 🟢 NEW: Modern Soft Shadow for Cards
static void DrawSoftShadow(Graphics& g, RectF r, int rad) {
    SolidBrush b1(Color(10, 0, 0, 0));
    GraphicsPath p1;
    AddRoundedRectPath(p1, r.X, r.Y + 3, r.Width, r.Height, rad);
    g.FillPath(&b1, &p1);

    SolidBrush b2(Color(6, 0, 0, 0));
    GraphicsPath p2;
    AddRoundedRectPath(p2, r.X - 1, r.Y + 1, r.Width + 2, r.Height + 4, rad);
    g.FillPath(&b2, &p2);
}

// 🟢 NEW: Get Greeting based on time
static wstring GetGreeting() {
    time_t t = time(0);
    struct tm* now = localtime(&t);
    if (now->tm_hour < 12) return L"Good Morning, Rasel";
    if (now->tm_hour < 18) return L"Good Afternoon, Rasel";
    return L"Good Evening, Rasel";
}

void DrawDashboardTab(Graphics& g, float cx, float cy, float cw, float ch) {
    d_cX = cx; d_cY = cy; d_cW = cw; d_cH = ch;
    InitDashboardData();

    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 28, FontStyleBold, UnitPixel); // Hero Font
    Font fSub(&ff, 14, FontStyleRegular, UnitPixel); // Hero Subtext
    Font fTabTxt(&ff, 15, FontStyleBold, UnitPixel); 
    Font fBtnTitle(&ff, 15, FontStyleBold, UnitPixel);
    Font fBtnSub(&ff, 12, FontStyleRegular, UnitPixel);
    
    FontFamily ffIc(L"Segoe MDL2 Assets");
    Font fIconBig(&ffIc, 24, FontStyleRegular, UnitPixel);

    SolidBrush bWhite(Color(255, 255, 255, 255));
    SolidBrush bDark(Color(255, 30, 40, 50));
    SolidBrush bGrayText(Color(255, 120, 130, 140));
    SolidBrush bTeal(Color(255, 12, 168, 176));
    SolidBrush bTealHover(Color(255, 30, 185, 195));
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtTL; fmtTL.SetAlignment(StringAlignmentNear); fmtTL.SetLineAlignment(StringAlignmentNear);

    // ─── 1. TWO-COLUMN LAYOUT PARTITION ───────────────────────────────────
    float leftColWidth = 340.0f; // Fixed Sidebar Width
    float rightColX = cx + leftColWidth;
    float rightColWidth = cw - leftColWidth;

    SolidBrush leftBg(Color(255, 248, 250, 252)); // Light premium tint for sidebar
    SolidBrush rightBg(Color(255, 255, 255, 255)); // Clean white for content
    
    g.FillRectangle(&leftBg, cx, cy, leftColWidth, ch);
    g.FillRectangle(&rightBg, rightColX, cy, rightColWidth, ch);

    // Subtle divider line between columns
    Pen colDivider(Color(255, 230, 235, 240), 1.0f);
    g.DrawLine(&colDivider, rightColX, cy, rightColX, cy + ch);

    // ─── 2. LEFT SIDEBAR: HERO & ACTION CARDS ─────────────────────────────
    float marginX = cx + 20.0f;
    float currentY = cy + 30.0f;
    float usableLeftW = leftColWidth - 40.0f;

    // Greeting
    wstring greeting = GetGreeting();
    g.DrawString(greeting.c_str(), -1, &fH1, RectF(marginX, currentY, usableLeftW, 35.0f), &fmtL, &bDark);
    currentY += 35.0f;
    g.DrawString(L"Manage workflow & boost productivity.", -1, &fSub, RectF(marginX, currentY, usableLeftW, 40.0f), &fmtTL, &bGrayText);
    currentY += 45.0f;

    // 4 Action Cards (Stacked Vertically)
    float acH = 75.0f; // Slightly smaller height for vertical stack
    float acGapY = 12.0f;
    
    struct CardTheme { Color bg; Color border; Color textC; Color subC; };
    CardTheme acThemes[4] = {
        { Color(255, 13, 158, 126),  Color(255,  9, 110, 88),  Color(255,255,255,255), Color(200,255,255,255) }, // teal
        { Color(255,124,  58, 237),  Color(255, 91, 33, 182),  Color(255,255,255,255), Color(200,255,255,255) }, // purple
        { Color(255,245, 158,  11),  Color(255,217,119,  6),   Color(255,255,255,255), Color(200,255,255,255) }, // amber
        { Color(255, 59, 130, 246),  Color(255, 29, 78, 216),  Color(255,255,255,255), Color(200,255,255,255) }, // blue
    };
    wstring acIcons[4] = { L"\xE72E", L"\xE8D7", L"\xE728", L"\xE774" }; 

    Font fAcTitle(&ff, 14, FontStyleBold, UnitPixel);
    Font fAcSub  (&ff, 11, FontStyleRegular, UnitPixel);
    Font fAcIcon(&ffIc, 22, FontStyleRegular, UnitPixel);

    for (int i = 0; i < 4; i++) {
        s_actionCards[i].bounds = RectF(marginX, currentY, usableLeftW, acH);
        bool hov = s_actionCards[i].isHovered;
        CardTheme& th = acThemes[i];

        DrawSoftShadow(g, s_actionCards[i].bounds, 8);

        GraphicsPath acPath;
        AddRoundedRectPath(acPath, marginX, currentY, usableLeftW, acH, 8.0f);
        SolidBrush acBg(hov ? th.border : th.bg);
        g.FillPath(&acBg, &acPath);

        // Icon background circle
        float icS = 40.0f;
        RectF icBg(marginX + 12.0f, currentY + (acH - icS) / 2.0f, icS, icS);
        GraphicsPath icCirc;
        AddRoundedRectPath(icCirc, icBg.X, icBg.Y, icBg.Width, icBg.Height, 20.0f);
        SolidBrush icCircFill(Color(55, 255, 255, 255));
        g.FillPath(&icCircFill, &icCirc);

        SolidBrush acIconC(Color(255, 255, 255, 255));
        g.DrawString(acIcons[i].c_str(), -1, &fAcIcon, icBg, &fmtC, &acIconC);

        // Arrow hint (right side)
        Font fArrow(&ffIc, 12, FontStyleRegular, UnitPixel);
        SolidBrush arrowC(Color(120, 255, 255, 255));
        g.DrawString(L"\xE76C", -1, &fArrow, RectF(marginX + usableLeftW - 28.0f, currentY, 24.0f, acH), &fmtC, &arrowC);

        // Text
        float txtX = icBg.X + icS + 12.0f;
        float txtW = usableLeftW - (txtX - marginX) - 30.0f;
        SolidBrush acTxtC(th.textC);
        SolidBrush acSubC(th.subC);
        
        g.DrawString(s_actionCards[i].title.c_str(), -1, &fAcTitle, RectF(txtX, currentY + 16.0f, txtW, 20.0f), &fmtTL, &acTxtC);
        g.DrawString(s_actionCards[i].subtitle.c_str(), -1, &fAcSub, RectF(txtX, currentY + 36.0f, txtW, 30.0f), &fmtTL, &acSubC);

        currentY += acH + acGapY;
    }

    // ─── 3. LEFT SIDEBAR: VERTICAL TABS ───────────────────────────────────
    currentY += 15.0f; // Gap before tabs
    std::wstring subNames[] = { L"Quick Blocks", L"Web & Cloud", L"Pro Tools", L"Personal Notes", L"Student Corner" };
    float tabH = 40.0f;

    for (int i = 0; i < 5; i++) {
        s_tabRects[i] = RectF(marginX, currentY, usableLeftW, tabH);
        
        if (selectedDashTab == i) {
            // Selected Tab Background
            GraphicsPath tPath;
            AddRoundedRectPath(tPath, marginX, currentY, usableLeftW, tabH, 6.0f);
            SolidBrush selBg(Color(255, 235, 248, 250)); // Very light teal
            g.FillPath(&selBg, &tPath);

            // Active Left Border Indicator
            GraphicsPath indPath;
            AddRoundedRectPath(indPath, marginX, currentY + 6.0f, 4.0f, tabH - 12.0f, 2.0f);
            g.FillPath(&bTeal, &indPath);

            g.DrawString(subNames[i].c_str(), -1, &fTabTxt, RectF(marginX + 20.0f, currentY, usableLeftW - 20.0f, tabH), &fmtL, &bTeal);
        } else {
            bool isHov = (hoveredDashTab == i);
            if (isHov) {
                GraphicsPath tPath;
                AddRoundedRectPath(tPath, marginX, currentY, usableLeftW, tabH, 6.0f);
                SolidBrush hovBg(Color(50, 230, 235, 240)); 
                g.FillPath(&hovBg, &tPath);
            }
            Gdiplus::Color bDarkColor;
            bDark.GetColor(&bDarkColor);
            SolidBrush inactiveTxt(isHov ? bDarkColor : Color(255, 140, 150, 160));
            g.DrawString(subNames[i].c_str(), -1, &fTabTxt, RectF(marginX + 20.0f, currentY, usableLeftW - 20.0f, tabH), &fmtL, &inactiveTxt);
        }
        currentY += tabH + 4.0f;
    }

    // ─── 4. RIGHT CONTENT: DYNAMIC GRID ───────────────────────────────────
    float gridMarginX = rightColX + 35.0f;
    float gridMarginY = cy + 30.0f;
    float usableGridWidth = rightColWidth - 70.0f; 
    
    // Header for the selected category in the right column
    g.DrawString(s_sections[selectedDashTab].title.c_str(), -1, &fH1, RectF(gridMarginX, gridMarginY, usableGridWidth, 40.0f), &fmtL, &bDark);
    
    float currentGridY = gridMarginY + 60.0f;
    int columns = 3; 
    float gapX = 20.0f;
    float gapY = 20.0f;
    float btnW = (usableGridWidth - (gapX * (columns - 1))) / columns;
    float btnH = 80.0f; 

    float currentGridX = gridMarginX;
    int colCount = 0;

    for (auto& btn : s_sections[selectedDashTab].btns) {
        if (colCount >= columns) {
            colCount = 0; 
            currentGridX = gridMarginX; 
            currentGridY += btnH + gapY;
        }

        btn.bounds = RectF(currentGridX, currentGridY, btnW, btnH);
        
        DrawSoftShadow(g, btn.bounds, 8);

        GraphicsPath bPath;
        AddRoundedRectPath(bPath, btn.bounds.X, btn.bounds.Y, btn.bounds.Width, btn.bounds.Height, 8.0f);
        SolidBrush btnBg(Color(255, 255, 255, 255));
        g.FillPath(&btnBg, &bPath);

        if (btn.isHovered) {
            Pen hoverPen(&bTeal, 2.0f);
            g.DrawPath(&hoverPen, &bPath);
        } else {
            Pen borderPen(Color(255, 235, 240, 245), 1.0f);
            g.DrawPath(&borderPen, &bPath);
        }

        // Icon Circle
        float icSize = 40.0f;
        RectF iconBgRect(btn.bounds.X + 15.0f, btn.bounds.Y + (btnH - icSize)/2.0f, icSize, icSize);
        GraphicsPath icBgPath;
        AddRoundedRectPath(icBgPath, iconBgRect.X, iconBgRect.Y, iconBgRect.Width, iconBgRect.Height, 20.0f);
        SolidBrush icBgFill(btn.isHovered ? Color(255, 12, 168, 176) : Color(255, 240, 250, 252));
        g.FillPath(&icBgFill, &icBgPath);

        SolidBrush btnIc(btn.isHovered ? Color(255, 255, 255, 255) : Color(255, 12, 168, 176)); 
        g.DrawString(btn.icon.c_str(), -1, &fIconBig, iconBgRect, &fmtC, &btnIc);

        // Text Layout
        float textStartX = iconBgRect.X + icSize + 15.0f;
        float textW = btn.bounds.Width - (textStartX - btn.bounds.X) - 10.0f;
        
        g.DrawString(btn.title.c_str(), -1, &fBtnTitle, RectF(textStartX, btn.bounds.Y + 18.0f, textW, 20.0f), &fmtTL, &bDark);
        g.DrawString(btn.subtext.c_str(), -1, &fBtnSub, RectF(textStartX, btn.bounds.Y + 40.0f, textW, 20.0f), &fmtTL, &bGrayText);

        currentGridX += btnW + gapX;
        colCount++;
    }

    // ─── 5. DEBUG KILL BUTTON (Bottom Right of Whole Window) ──────────────
    float killW = 130.0f, killH = 35.0f;
    float killX = cx + cw - killW - 30.0f;
    float killY = cy + ch - killH - 30.0f;
    DrawSoftShadow(g, RectF(killX, killY, killW, killH), 4);
    GraphicsPath kPath;
    AddRoundedRectPath(kPath, killX, killY, killW, killH, 4.0f);
    SolidBrush redBtn(dash_hovKillBtn ? Color(255, 220, 50, 50) : Color(255, 255, 255, 255));
    SolidBrush redTxt(dash_hovKillBtn ? Color(255, 255, 255, 255) : Color(255, 200, 50, 50));
    g.FillPath(&redBtn, &kPath);
    if (!dash_hovKillBtn) {
        Pen redPen(Color(255, 200, 50, 50), 1.0f);
        g.DrawPath(&redPen, &kPath);
    }
    Font fKill(&ff, 12, FontStyleBold, UnitPixel);
    g.DrawString(L"DEBUG KILL", -1, &fKill, RectF(killX, killY, killW, killH), &fmtC, &redTxt);

    // ─── 6. PASSWORD NUMPAD OVERLAY (Centered on Screen) ──────────────────
    if (showKillPrompt) {
        SolidBrush overBg(Color(200, 10, 15, 20)); 
        g.FillRectangle(&overBg, cx, cy, cw, ch);

        float pW = 320.0f, pH = 450.0f;
        float pX = cx + (cw - pW) / 2.0f;
        float pY = cy + (ch - pH) / 2.0f;

        DrawSoftShadow(g, RectF(pX, pY, pW, pH), 12);
        GraphicsPath popPath;
        AddRoundedRectPath(popPath, pX, pY, pW, pH, 12.0f);
        g.FillPath(&bWhite, &popPath);

        Font fH2(&ff, 20, FontStyleBold, UnitPixel);
        g.DrawString(L"Enter Debug Password", -1, &fH2, RectF(pX, pY + 25.0f, pW, 30.0f), &fmtC, &bDark);

        Font fClose(&ffIc, 14, FontStyleBold, UnitPixel);
        SolidBrush closeC(hoverNumBtn == 12 ? Color(255, 230, 50, 50) : Color(255, 160, 170, 180));
        g.DrawString(L"\xE711", -1, &fClose, RectF(pX + pW - 45.0f, pY + 15.0f, 30.0f, 30.0f), &fmtC, &closeC);

        float disX = pX + 30.0f, disY = pY + 80.0f, disW = pW - 60.0f, disH = 50.0f;
        DrawSoftShadow(g, RectF(disX, disY, disW, disH), 6);
        GraphicsPath disPath;
        AddRoundedRectPath(disPath, disX, disY, disW, disH, 6.0f);
        g.FillPath(&bWhite, &disPath);
        Pen disPen(Color(255, 220, 225, 230), 1.0f);
        g.DrawPath(&disPen, &disPath);
        
        wstring stars = wstring(killInput.length(), L'*');
        Font fStar(&ff, 28, FontStyleBold, UnitPixel);
        g.DrawString(stars.c_str(), -1, &fStar, RectF(disX, disY + 8.0f, disW, disH), &fmtC, &bDark);

        float padX = pX + 40.0f; float padY = disY + 75.0f;
        float nW = 70.0f, nH = 50.0f, nGap = 15.0f;
        wstring btnText[12] = { L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"C", L"0", L"OK" };
        int btnId[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11 };

        for (int i = 0; i < 12; ++i) {
            int row = i / 3; int col = i % 3;
            float bx = padX + (col * (nW + nGap)); float by = padY + (row * (nH + nGap));
            GraphicsPath bPath;
            AddRoundedRectPath(bPath, bx, by, nW, nH, 6.0f);
            bool isHov = (hoverNumBtn == btnId[i]);
            
            Color cBgColor = Color(255, 248, 250, 252);
            Color cTxtColor = Color(255, 40, 50, 60);

            if (btnId[i] == 11) { // OK
                cBgColor = isHov ? Color(255, 30, 185, 195) : Color(255, 12, 168, 176);
                cTxtColor = Color(255, 255, 255, 255);
            } else if (btnId[i] == 10) { // Clear
                cBgColor = isHov ? Color(255, 255, 220, 220) : Color(255, 255, 240, 240);
                cTxtColor = Color(255, 230, 50, 50);
            } else if (isHov) { cBgColor = Color(255, 235, 240, 245); }

            SolidBrush numBg(cBgColor); SolidBrush numTxtC(cTxtColor);
            g.FillPath(&numBg, &bPath);
            if (btnId[i] != 11 && btnId[i] != 10) {
                 Pen nPen(Color(255, 230, 235, 240), 1.0f);
                 g.DrawPath(&nPen, &bPath);
            }
            g.DrawString(btnText[i].c_str(), -1, &fH2, RectF(bx, by, nW, nH), &fmtC, &numTxtC);
        }
    }
}

void ProcessDashboardMouseMove(float x, float y) {
    hoverNumBtn = -1;
    dash_hovKillBtn = false;
    bool needsRedraw = false;

    if (showKillPrompt) {
        float pW = 320.0f, pH = 450.0f;
        float pX = d_cX + (d_cW - pW) / 2.0f; float pY = d_cY + (d_cH - pH) / 2.0f;
        if (x >= pX + pW - 45.0f && x <= pX + pW - 15.0f && y >= pY + 15.0f && y <= pY + 45.0f) hoverNumBtn = 12;

        float padX = pX + 40.0f, padY = pY + 155.0f;
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

    int oldHoverDashTab = hoveredDashTab;
    hoveredDashTab = -1;

    // Action card hover
    for (int i = 0; i < 4; i++) {
        bool wasHov = s_actionCards[i].isHovered;
        s_actionCards[i].isHovered = s_actionCards[i].bounds.Contains(x, y);
        if (wasHov != s_actionCards[i].isHovered) needsRedraw = true;
    }

    // Tabs hover
    for (int i = 0; i < 5; i++) {
        if (s_tabRects[i].Contains(x, y)) { hoveredDashTab = i; needsRedraw = true; break; }
    }
    if (oldHoverDashTab != hoveredDashTab) needsRedraw = true;

    // Grid buttons hover
    for (auto& btn : s_sections[selectedDashTab].btns) {
        bool wasHovered = btn.isHovered;
        btn.isHovered = btn.bounds.Contains(x, y);
        if (wasHovered != btn.isHovered) needsRedraw = true;
    }

    // Debug Kill button hover
    float killW = 130.0f, killH = 35.0f;
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

    // --- Action Card Clicks ---
    for (int i = 0; i < 4; i++) {
        if (s_actionCards[i].bounds.Contains(x, y)) {
            s_actionCards[i].isHovered = false;
            if (i == 0) {
                // "Create Blocking Profile" → Schedule Blocks + Add Profile form সরাসরি
                SetBlockSubTabWithAction(1, 1);
                selectedTab = 1;
            } else if (i == 1) {
                // "Advanced Adult Block" → Adult Block sub-tab
                SetBlockSubTabWithAction(2, 0);
                selectedTab = 1;
            } else if (i == 2) {
                // "Start Deep Study" → Deep Study tab
                selectedTab = 2;
            } else if (i == 3) {
                // "Allow Only Websites & Apps" → Simple Blocks, Allow mode
                SetBlockSubTabWithAction(0, 2);
                selectedTab = 1;
            }
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
    }

    // --- Vertical Tab Clicks ---
    for (int i = 0; i < 5; i++) {
        if (s_tabRects[i].Contains(x, y)) {
            if (selectedDashTab != i) {
                selectedDashTab = i;
                if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            }
            return;
        }
    }

    // --- Right Grid Content Clicks ---
    for (auto& btn : s_sections[selectedDashTab].btns) {
        if (btn.bounds.Contains(x, y)) {
            
            // 🟢 PDF Reader
            if (btn.title == L"PDF Reader") {
                LaunchFoxitStylePdfReader(L"");
            }
            else if (btn.title == L"RasBrowser") LaunchMiniBrowser(L"RAS_BROWSER", L"RasBrowser");
            else if (btn.title == L"Internet Block" || btn.title == L"Uninstall Block" || btn.title == L"Ads Block" || btn.title == L"YT Shorts Block" || btn.title == L"FB Reels Block") selectedTab = 1; 
            else if (btn.title == L"Adult Block") selectedTab = 2;
            else if (btn.title == L"Personal Diary") selectedTab = 4;

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

            else if (btn.title == L"Instant Note") LaunchMiniBrowser(L"LOCAL_INSTANT_NOTE", L"Instant Note");

            else if (btn.title == L"Study Materials") LaunchMiniBrowser(L"LOCAL_STUDY_MATS", L"Study Materials Vault");
            else if (btn.title == L"CGPA Calc") LaunchMiniBrowser(L"LOCAL_CGPA_CALC", L"CGPA Calculator");
            else if (btn.title == L"Exam Routine") LaunchMiniBrowser(L"LOCAL_ROUTINE", L"Exam Routine Tracker");

            btn.isHovered = false; 
            if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
    }
}