// tab_special.cpp
// Special Tab — Windows File Explorer–style shell with
//   left sidebar (Quick Access, This PC, Drives, Google Drive)
//   top sub-tab bar (File Manager Plus | Professional Diary | Student Utilities)
//   content area delegates to existing sub-tab renderers

#include "tab_special.h"
#include "tab_gemini.h"        // Diary Tab
#include "tab_utilities.h"     // Utilities Tab
#include "tab_file_manager.h"  // File Manager Plus Sub-Tab
#include <string>
#include <vector>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>
#include <time.h>
#include <fstream>

using namespace Gdiplus;
using namespace std;

// ============================================================
// STATE
// ============================================================
int sf_activeSubTab = 0; // 0 = File Manager Plus, 1 = Diary, 2 = Utilities

// Motivational popup state (kept from original)
static bool sf_chkMotivation      = false;
static int  sf_langSel            = 0; // 0 = English, 1 = Bangla
static bool motivationThreadRunning = false;
static wstring currentMotiveQuote = L"";

// Sidebar hover / selection
static int  sf_hovSideItem  = -1;  // quick-access row
static int  sf_hovDriveItem = -1;  // drive row
static bool sf_hovGDrive    = false;

// Sub-tab hover
static bool sf_hovTabFM    = false;
static bool sf_hovTabDiary = false;
static bool sf_hovTabUtils = false;

// Layout cache (set each draw, used by mouse handlers)
static float g_cx = 0, g_cy = 0, g_cw = 0, g_ch = 0;
static float g_sideW   = 200.0f;  // sidebar width (Win11 Explorer style)
static float g_headerH =  52.0f;  // sub-tab bar height

// Sidebar item rects cache (for hit testing)
struct SideRect { float x, y, w, h; };
static vector<SideRect> g_quickRects;   // quick-access items
static vector<SideRect> g_driveRects;   // drive items
static SideRect          g_gdriveRect;  // google drive item

// Motivational quotes
static vector<wstring> quotesEng = {
    L"\"Don't watch the clock; do what it does. Keep going.\" - Sam Levenson",
    L"\"The future depends on what you do today.\" - Mahatma Gandhi",
    L"\"Focus on your goal. Don't look in any direction but ahead.\"",
    L"\"Time is what we want most, but what we use worst.\" - William Penn",
    L"\"Push yourself, because no one else is going to do it for you.\""
};
static vector<wstring> quotesBen = {
    L"\"ঘড়ির দিকে তাকিও না; ঘড়ি যা করে তা করো। চলতে থাকো।\"",
    L"\"তোমার ভবিষ্যৎ নির্ভর করে তুমি আজ কী করছো তার ওপর।\"",
    L"\"শুধু লক্ষ্যের দিকে ফোকাস করো। অন্য কোথাও তাকানোর সময় নেই।\"",
    L"\"সময়ই আমাদের সবচেয়ে বেশি দরকার, অথচ এটাকেই আমরা সবচেয়ে বাজেভাবে ব্যবহার করি।\"",
    L"\"নিজেকে নিজে পুশ করো, কারণ অন্য কেউ তোমার হয়ে এটা করে দেবে না।\""
};

// ============================================================
// EXTERNALS
// ============================================================
extern bool IsRunAsAdmin();
extern string GetSecretDir();
extern HWND hParentWnd;

// Diary
extern void ShowGeminiControls(bool show);
extern void DrawGeminiTab(Graphics& g, float cx, float cy, float cw, float ch);
extern void ResizeGeminiControls(int cx, int cy, int cw, int ch);
extern void ProcessGeminiMouseMove(float x, float y);
extern void ProcessGeminiMouseClick(float x, float y);

// Utilities
extern void DrawUtilitiesTab(Graphics& g, float cx, float cy, float cw, float ch);
extern void ProcessUtilitiesMouseMove(float x, float y);
extern void ProcessUtilitiesMouseClick(float x, float y);

// ============================================================
// HELPERS
// ============================================================
static void FillRoundRect(Graphics& g, Brush* br, Pen* pen,
                          float x, float y, float w, float h, float r = 6.0f) {
    GraphicsPath path;
    path.AddArc(x,         y,         r*2, r*2, 180, 90);
    path.AddArc(x+w-r*2,   y,         r*2, r*2, 270, 90);
    path.AddArc(x+w-r*2,   y+h-r*2,   r*2, r*2,   0, 90);
    path.AddArc(x,         y+h-r*2,   r*2, r*2,  90, 90);
    path.CloseFigure();
    if (br)  g.FillPath(br,  &path);
    if (pen) g.DrawPath(pen, &path);
}

// ============================================================
// MOTIVATIONAL POPUP (kept from original)
// ============================================================
LRESULT CALLBACK MotivationWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        Graphics g(hdc); g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT r; GetClientRect(hwnd, &r);
        GraphicsPath path; int d = 20;
        path.AddArc(0,0,d,d,180,90); path.AddArc(r.right-d,0,d,d,270,90);
        path.AddArc(r.right-d,r.bottom-d,d,d,0,90); path.AddArc(0,r.bottom-d,d,d,90,90);
        path.CloseFigure();
        SolidBrush bg(Color(250,30,41,59));  g.FillPath(&bg, &path);
        Pen border(Color(255,245,158,11),2.0f); g.DrawPath(&border,&path);
        FontFamily ff(sf_langSel==1 ? L"Vrinda" : L"Segoe UI");
        Font fQ(&ff,22,FontStyleBold,UnitPixel);
        SolidBrush wBr(Color(255,255,255,255));
        StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(currentMotiveQuote.c_str(),-1,&fQ,RectF(20,20,(float)r.right-40,(float)r.bottom-40),&fmt,&wBr);
        EndPaint(hwnd,&ps); return 0;
    }
    if (msg==WM_TIMER) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}
static void ShowMotivationalPopup() {
    thread([](){
        srand((unsigned)time(0));
        if (sf_langSel==0) currentMotiveQuote = quotesEng[rand()%quotesEng.size()];
        else               currentMotiveQuote = quotesBen[rand()%quotesBen.size()];
        static bool reg=false;
        if (!reg) {
            WNDCLASSW wc={0}; wc.lpfnWndProc=MotivationWndProc; wc.hInstance=GetModuleHandle(NULL);
            wc.lpszClassName=L"RasMotivClass"; RegisterClassW(&wc); reg=true;
        }
        int w=550,h=120,x=(GetSystemMetrics(SM_CXSCREEN)-w)/2;
        HWND hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,
            L"RasMotivClass",L"",WS_POPUP,x,50,w,h,NULL,NULL,NULL,NULL);
        SetLayeredWindowAttributes(hwnd,0,245,LWA_ALPHA);
        ShowWindow(hwnd,SW_SHOWNOACTIVATE);
        SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetTimer(hwnd,1,6000,NULL);
        MSG msg; while(GetMessage(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);}
        DestroyWindow(hwnd);
    }).detach();
}
static void MotivationBackgroundThread() {
    while(true) {
        Sleep(1000);
        if (sf_chkMotivation) {
            static DWORD last=GetTickCount();
            DWORD now=GetTickCount();
            if (now-last>=900000){last=now;ShowMotivationalPopup();}
        }
    }
}

// ============================================================
// DRAW SIDEBAR  —  Windows 11 File Explorer exact style
// ============================================================
static void DrawSidebar(Graphics& g,
                        float sx, float sy, float sw, float sh,
                        const FontFamily& ff, const FontFamily& ffIc)
{
    g_quickRects.clear();
    g_driveRects.clear();

    // --- Fonts (match Win11 Explorer: Segoe UI 12px) ---
    Font fItem (&ff, 12, FontStyleRegular, UnitPixel);
    Font fLabel(&ff, 11, FontStyleRegular, UnitPixel);
    Font fIc   (&ffIc, 15, FontStyleRegular, UnitPixel);
    Font fIcSm (&ffIc, 13, FontStyleRegular, UnitPixel);
    Font fChevron(&ffIc, 10, FontStyleRegular, UnitPixel);

    // --- Colours (Win11 sidebar: nearly white bg, dark text) ---
    SolidBrush bBg   (Color(255, 243, 243, 243)); // sidebar bg
    SolidBrush bText (Color(255,  30,  30,  30)); // primary text
    SolidBrush bMuted(Color(255, 100, 100, 100)); // icons / labels
    SolidBrush bPin  (Color(255, 140, 140, 140)); // pin icon colour
    SolidBrush bHov  (Color(255, 222, 222, 222)); // hover bg
    SolidBrush bSel  (Color(255, 205, 228, 255)); // selected bg (Win11 blue tint)
    SolidBrush bGray2(Color(255, 160, 160, 160));

    Pen pBorder(Color(255, 225, 225, 225), 1.0f); // right border
    Pen pSep   (Color(200, 190, 190, 190), 1.0f); // section separator

    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetFormatFlags(StringFormatFlagsNoWrap);
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);

    // --- Sidebar background + right border ---
    g.FillRectangle(&bBg, sx, sy, sw, sh);
    g.DrawLine(&pBorder, sx + sw - 1.0f, sy, sx + sw - 1.0f, sy + sh);

    float curY   = sy + 6.0f;
    float rowH   = 30.0f;   // Win11 row height
    float padL   = 10.0f;   // left padding
    float icW    = 18.0f;   // icon column width
    float icGap  = 6.0f;    // gap between icon and text
    float pinW   = 18.0f;   // pin icon area on right

    // Helper: draw one sidebar row
    // Returns the rect stored for hit-testing
    auto DrawRow = [&](float ry, const wchar_t* icon, const wchar_t* label,
                       bool hovered, bool selected, bool showPin,
                       Color iconColor) -> SideRect
    {
        float rh = rowH;
        // Background
        if (selected) {
            FillRoundRect(g, &bSel, nullptr, sx + 2.0f, ry + 1.0f, sw - 4.0f, rh - 2.0f, 4.0f);
        } else if (hovered) {
            FillRoundRect(g, &bHov, nullptr, sx + 2.0f, ry + 1.0f, sw - 4.0f, rh - 2.0f, 4.0f);
        }
        // Icon
        SolidBrush bIcoClr(iconColor);
        g.DrawString(icon, -1, &fIc,
            RectF(sx + padL, ry, icW, rh), &fmtC, &bIcoClr);
        // Label
        float txtX = sx + padL + icW + icGap;
        float txtW = sw - padL - icW - icGap - (showPin ? pinW + 4.0f : 8.0f);
        g.DrawString(label, -1, &fItem,
            RectF(txtX, ry, txtW, rh), &fmtL, &bText);
        // Pin icon (📌 \xE840 in Segoe MDL2) — shown on hovered pinned items
        if (showPin && hovered) {
            g.DrawString(L"\xE840", -1, &fIcSm,
                RectF(sx + sw - pinW - 4.0f, ry, pinW, rh), &fmtC, &bPin);
        }
        return { sx, ry, sw, rh };
    };

    // ── HOME ──────────────────────────────────────────
    {
        bool hov = (sf_hovSideItem == 0); // index 0 in g_quickRects
        auto r = DrawRow(curY, L"\xEA8A", L"Home", hov, false, false,
                         Color(255, 60, 60, 60));
        g_quickRects.push_back({r.x, r.y, r.w, r.h}); // index 0 = Home
        curY += rowH;
    }

    // ── GALLERY ───────────────────────────────────────
    {
        bool hov = (sf_hovSideItem == 1); // index 1 in g_quickRects
        auto r = DrawRow(curY, L"\xE91B", L"Gallery", hov, false, false,
                         Color(255, 60, 60, 60));
        g_quickRects.push_back({r.x, r.y, r.w, r.h}); // index 1 = Gallery
        curY += rowH;
    }

    // ── Separator after Home/Gallery ──────────────────
    curY += 4.0f;
    g.DrawLine(&pSep, sx + 8.0f, curY, sx + sw - 8.0f, curY);
    curY += 4.0f;

    // ── QUICK ACCESS pinned items ──────────────────────
    // Resolve shell paths
    wchar_t desktopPath[MAX_PATH]={}, dlPath[MAX_PATH]={};
    wchar_t docPath[MAX_PATH]={},    picPath[MAX_PATH]={};
    wchar_t musicPath[MAX_PATH]={},  vidPath[MAX_PATH]={};
    SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath);
    SHGetFolderPathW(NULL, CSIDL_PERSONAL,          NULL, 0, docPath);
    SHGetFolderPathW(NULL, CSIDL_MYPICTURES,        NULL, 0, picPath);
    SHGetFolderPathW(NULL, CSIDL_MYMUSIC,           NULL, 0, musicPath);
    SHGetFolderPathW(NULL, CSIDL_MYVIDEO,           NULL, 0, vidPath);
    PWSTR dlRaw = NULL;
    SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &dlRaw);
    if (dlRaw) { wcscpy_s(dlPath, dlRaw); CoTaskMemFree(dlRaw); }

    struct QItem { const wchar_t* icon; const wchar_t* label; Color iconColor; };
    QItem qa[] = {
        { L"\xE8B7", L"Desktop",   Color(255,  70, 130, 180) }, // steel blue folder
        { L"\xEC0A", L"Downloads", Color(255,  70, 130, 180) },
        { L"\xE8A5", L"Documents", Color(255,  70, 130, 180) },
        { L"\xEB9F", L"Pictures",  Color(255,  70, 130, 180) },
        { L"\xEC4F", L"Music",     Color(255,  70, 130, 180) },
        { L"\xE8B2", L"Videos",    Color(255,  70, 130, 180) },
    };

    for (int i = 0; i < 6; i++) {
        int idx = i + 2; // g_quickRects index: 2=Desktop, 3=Downloads, ... 7=Videos
        bool hov = (sf_hovSideItem == idx);
        auto r = DrawRow(curY, qa[i].icon, qa[i].label, hov, false, true,
                         qa[i].iconColor);
        g_quickRects.push_back({r.x, r.y, r.w, r.h}); // index 2..7
        curY += rowH;
    }

    // ── Separator before This PC ───────────────────────
    curY += 4.0f;
    g.DrawLine(&pSep, sx + 8.0f, curY, sx + sw - 8.0f, curY);
    curY += 4.0f;

    // ── THIS PC header row (chevron + label, not clickable as nav) ──
    // In Win11: "This PC" has a collapse chevron, we draw it static
    g.DrawString(L"\xE76C", -1, &fChevron,
        RectF(sx + padL - 2.0f, curY, 12.0f, rowH), &fmtC, &bMuted); // right chevron = expanded
    g.DrawString(L"This PC", -1, &fLabel,
        RectF(sx + padL + 12.0f, curY, sw - padL - 16.0f, rowH), &fmtL, &bMuted);
    curY += rowH;

    // ── DRIVES ────────────────────────────────────────
    wchar_t driveStrings[512] = {};
    GetLogicalDriveStringsW(511, driveStrings);
    vector<wstring> drives;
    for (wchar_t* p = driveStrings; *p; p += wcslen(p) + 1) {
        UINT t = GetDriveTypeW(p);
        if (t == DRIVE_FIXED || t == DRIVE_REMOVABLE || t == DRIVE_REMOTE || t == DRIVE_RAMDISK)
            drives.push_back(p);
    }

    for (int di = 0; di < (int)drives.size(); di++) {
        bool hov = (sf_hovDriveItem == di);
        wstring lbl = drives[di];
        if (!lbl.empty() && lbl.back() == L'\\') lbl.pop_back(); // "C:"

        // Get volume label for friendly name  e.g. "Windows-SSD (C:)"
        wchar_t volName[MAX_PATH] = {};
        if (GetVolumeInformationW(drives[di].c_str(), volName, MAX_PATH,
                                  NULL, NULL, NULL, NULL, 0) && volName[0]) {
            wstring friendly = wstring(volName) + L" (" + lbl + L")";
            lbl = friendly;
        }

        UINT dtype = GetDriveTypeW(drives[di].c_str());
        const wchar_t* dIcon = L"\xE88E"; // USB/generic
        Color dIconColor(255, 80, 80, 80);
        if (dtype == DRIVE_FIXED) {
            dIcon      = L"\xE7D2"; // HDD
            dIconColor = Color(255, 60, 60, 60);
        } else if (dtype == DRIVE_REMOTE) {
            dIcon      = L"\xE753"; // Network
            dIconColor = Color(255, 60, 120, 200);
        }

        auto r = DrawRow(curY, dIcon, lbl.c_str(), hov, false, false, dIconColor);
        g_driveRects.push_back({r.x, r.y, r.w, r.h});
        curY += rowH;
    }

    // ── Separator before Network ───────────────────────
    curY += 4.0f;
    g.DrawLine(&pSep, sx + 8.0f, curY, sx + sw - 8.0f, curY);
    curY += 4.0f;

    // ── NETWORK row ────────────────────────────────────
    {
        bool hov = sf_hovGDrive; // reuse gdriveRect for Network
        auto r = DrawRow(curY, L"\xEC27", L"Network", hov, false, false,
                         Color(255, 60, 60, 60));
        g_gdriveRect = {r.x, r.y, r.w, r.h};
        curY += rowH;
    }
}

// ============================================================
// DRAW SUB-TAB HEADER BAR
// ============================================================
static void DrawSubTabBar(Graphics& g,
                          float tx, float ty, float tw, float th,
                          const FontFamily& ff, const FontFamily& ffIc)
{
    Font fBold(&ff, 13, FontStyleBold,    UnitPixel);
    Font fReg (&ff, 13, FontStyleRegular, UnitPixel);
    Font fIcSm(&ffIc,14, FontStyleRegular, UnitPixel);

    SolidBrush bWhite(Color(255,255,255,255));
    SolidBrush bTeal (Color(255,  0,150,160));
    SolidBrush bGray (Color(255,130,130,130));
    SolidBrush bDiaryBlue(Color(255,35,137,215));
    SolidBrush bUtilPurple(Color(255,155,89,182));

    Pen pBrd(Color(255,218,225,232),1.0f);
    Pen pTeal(Color(255,0,150,160),2.5f);
    Pen pDiaryBlue(Color(255,35,137,215),2.5f);
    Pen pUtilPurple(Color(255,155,89,182),2.5f);

    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);

    // Background
    g.FillRectangle(&bWhite, tx, ty, tw, th);
    g.DrawLine(&pBrd, tx, ty+th, tx+tw, ty+th);

    struct TabDef {
        const wchar_t* icon;
        const wchar_t* label;
        int            idx;
        bool           hov;
        SolidBrush*    activeColor;
        Pen*           activePen;
    };
    TabDef tabs[] = {
        { L"\xEC50", L"File Manager Plus",   0, sf_hovTabFM,    &bTeal,       &pTeal       },
        { L"\xE7BC", L"Professional Diary",  1, sf_hovTabDiary, &bDiaryBlue,  &pDiaryBlue  },
        { L"\xE943", L"Student Utilities",   2, sf_hovTabUtils, &bUtilPurple, &pUtilPurple },
    };

    float tabW = tw / 3.0f;
    for (int i = 0; i < 3; i++) {
        float tbx = tx + i * tabW;
        bool active = (sf_activeSubTab == tabs[i].idx);

        if (active) {
            SolidBrush bActBg(Color(30, 0, 150, 160));
            g.FillRectangle(&bActBg, tbx, ty, tabW, th);
            // Bottom accent line
            g.DrawLine(tabs[i].activePen, tbx+4, ty+th-2, tbx+tabW-4, ty+th-2);
            // Icon + label
            g.DrawString(tabs[i].icon,  -1, &fIcSm, RectF(tbx+12, ty, 22, th), &fmtL, tabs[i].activeColor);
            g.DrawString(tabs[i].label, -1, &fBold,  RectF(tbx+36, ty, tabW-40, th), &fmtL, tabs[i].activeColor);
        } else {
            if (tabs[i].hov) {
                SolidBrush bH(Color(255,245,245,245));
                g.FillRectangle(&bH, tbx, ty, tabW, th);
            }
            g.DrawString(tabs[i].icon,  -1, &fIcSm, RectF(tbx+12, ty, 22, th), &fmtL, &bGray);
            g.DrawString(tabs[i].label, -1, &fReg,  RectF(tbx+36, ty, tabW-40, th), &fmtL, &bGray);
        }

        // Vertical divider between tabs
        if (i < 2) {
            Pen pDiv(Color(255,225,228,232),1.0f);
            g.DrawLine(&pDiv, tbx+tabW, ty+8, tbx+tabW, ty+th-8);
        }
    }
}

// ============================================================
// MAIN DRAW
// ============================================================
void DrawSpecialFeatureTab(Graphics& g, float cx, float cy, float cw, float ch) {
    g_cx=cx; g_cy=cy; g_cw=cw; g_ch=ch;

    if (!motivationThreadRunning) {
        thread t(MotivationBackgroundThread); t.detach();
        motivationThreadRunning = true;
    }

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    FontFamily ff  (L"Segoe UI");
    FontFamily ffIc(L"Segoe MDL2 Assets");

    // ---- Outer background ----
    SolidBrush bBg(Color(255,248,250,252));
    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // ---- Sub-tab header (full width, at top) ----
    DrawSubTabBar(g, cx, cy, cw, g_headerH, ff, ffIc);

    float bodyY = cy + g_headerH;
    float bodyH = ch - g_headerH;

    // ---- Sidebar (left of content) ----
    DrawSidebar(g, cx, bodyY, g_sideW, bodyH, ff, ffIc);

    // ---- Content area ----
    float contentX = cx + g_sideW;
    float contentW = cw - g_sideW;

    if (sf_activeSubTab == 0) {
        ShowGeminiControls(false);
        DrawFileManagerTab(g, contentX, bodyY, contentW, bodyH);
    }
    else if (sf_activeSubTab == 1) {
        DrawGeminiTab(g, contentX, bodyY, contentW, bodyH);
        ResizeGeminiControls((int)contentX, (int)bodyY, (int)contentW, (int)bodyH);
        ShowGeminiControls(true);
    }
    else if (sf_activeSubTab == 2) {
        ShowGeminiControls(false);
        DrawUtilitiesTab(g, contentX, bodyY, contentW, bodyH);
    }
}

// ============================================================
// MOUSE MOVE
// ============================================================
void ProcessSpecialFeatureMouseMove(float x, float y) {
    // ---- Sub-tab bar hover ----
    bool old_hFM    = sf_hovTabFM;
    bool old_hDiary = sf_hovTabDiary;
    bool old_hUtils = sf_hovTabUtils;

    float tabW = g_cw / 3.0f;
    sf_hovTabFM    = (y >= g_cy && y <= g_cy+g_headerH && x >= g_cx          && x < g_cx+tabW);
    sf_hovTabDiary = (y >= g_cy && y <= g_cy+g_headerH && x >= g_cx+tabW     && x < g_cx+tabW*2);
    sf_hovTabUtils = (y >= g_cy && y <= g_cy+g_headerH && x >= g_cx+tabW*2   && x < g_cx+g_cw);

    // ---- Sidebar hover ----
    int old_hSide  = sf_hovSideItem;
    int old_hDrive = sf_hovDriveItem;
    bool old_hGD   = sf_hovGDrive;

    sf_hovSideItem  = -1;
    sf_hovDriveItem = -1;
    sf_hovGDrive    = false;

    for (int i=0; i<(int)g_quickRects.size(); i++) {
        auto& r = g_quickRects[i];
        if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) { sf_hovSideItem=i; break; }
    }
    if (sf_hovSideItem < 0) {
        for (int i=0; i<(int)g_driveRects.size(); i++) {
            auto& r = g_driveRects[i];
            if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) { sf_hovDriveItem=i; break; }
        }
    }
    if (sf_hovSideItem<0 && sf_hovDriveItem<0) {
        auto& r = g_gdriveRect;
        if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) sf_hovGDrive=true;
    }

    // ---- Delegate to active sub-tab ----
    float bodyY    = g_cy + g_headerH;
    float contentX = g_cx + g_sideW;
    float contentW = g_cw - g_sideW;

    if (sf_activeSubTab == 0)
        ProcessFileManagerMouseMove(x, y);
    else if (sf_activeSubTab == 1)
        ProcessGeminiMouseMove(x, y);
    else if (sf_activeSubTab == 2)
        ProcessUtilitiesMouseMove(x, y);

    bool changed = (old_hFM    != sf_hovTabFM    ||
                    old_hDiary != sf_hovTabDiary  ||
                    old_hUtils != sf_hovTabUtils  ||
                    old_hSide  != sf_hovSideItem  ||
                    old_hDrive != sf_hovDriveItem ||
                    old_hGD    != sf_hovGDrive);
    if (changed && hParentWnd)
        InvalidateRect(hParentWnd, NULL, TRUE);
}

// ============================================================
// MOUSE CLICK
// ============================================================
void ProcessSpecialFeatureMouseClick(float x, float y) {
    // ---- Sub-tab bar clicks ----
    if (y >= g_cy && y <= g_cy + g_headerH) {
        float tabW = g_cw / 3.0f;
        if      (x >= g_cx          && x < g_cx+tabW)   sf_activeSubTab = 0;
        else if (x >= g_cx+tabW     && x < g_cx+tabW*2) sf_activeSubTab = 1;
        else if (x >= g_cx+tabW*2   && x < g_cx+g_cw)   sf_activeSubTab = 2;
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }

    float bodyY    = g_cy + g_headerH;
    float contentX = g_cx + g_sideW;

    // ---- Sidebar quick-access clicks (navigate File Manager Plus) ----
    if (x >= g_cx && x < g_cx + g_sideW) {
        // Quick access items
        for (int i=0; i<(int)g_quickRects.size(); i++) {
            auto& r = g_quickRects[i];
            if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
                // Switch to File Manager tab and navigate
                sf_activeSubTab = 0;
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
        }
        // Drive items — switch to file manager
        for (int i=0; i<(int)g_driveRects.size(); i++) {
            auto& r = g_driveRects[i];
            if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
                sf_activeSubTab = 0;
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
        }
        // Google Drive
        {
            auto& r = g_gdriveRect;
            if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
                sf_activeSubTab = 0; // Switch to File Manager (Drive tab inside)
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
        }
        return; // click was in sidebar but missed all items
    }

    // ---- Content area clicks — guard below header ----
    if (y <= bodyY) return;

    if (sf_activeSubTab == 0)
        ProcessFileManagerMouseClick(x, y, hParentWnd);
    else if (sf_activeSubTab == 1)
        ProcessGeminiMouseClick(x, y);
    else if (sf_activeSubTab == 2)
        ProcessUtilitiesMouseClick(x, y);
}
