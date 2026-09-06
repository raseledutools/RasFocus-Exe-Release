// tab_file_manager.cpp
// File Manager Plus Tab — Local File Explorer + Google Drive Integration
// Replaces "Dashboard" as the first sidebar tab (index 0 -> logical 12)

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include "tab_file_manager.h"
#include "globals.h"
#include <string>
#include <vector>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <fstream>
#include <algorithm>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

using namespace Gdiplus;
using namespace std;

// ============================================================
// STATE
// ============================================================
static int fm_activeSubTab = 0;   // 0 = Local Files, 1 = Google Drive

// --- Local File Explorer State ---
static wstring fm_currentPath = L"C:\\";
static vector<wstring> fm_breadcrumb;  // path segments for breadcrumb
static vector<pair<wstring, bool>> fm_items; // (name, isDir)
static int fm_selectedItem = -1;
static int fm_scrollOffset = 0;
static int fm_hovItem      = -1;
static bool fm_hovUp       = false;
static bool fm_hovSearch   = false;

// --- Breadcrumb hover ---
static int fm_hovBreadcrumb = -1;

// --- Sub-tab hover ---
static bool fm_hovTabLocal = false;
static bool fm_hovTabDrive = false;

// --- Toolbar button hovers ---
static bool fm_hovRefresh  = false;
static bool fm_hovNewFolder= false;
static bool fm_hovDelete   = false;
static bool fm_hovOpen     = false;

// --- Google Drive sub-state ---
static bool fm_driveSignedIn = false;
static bool fm_hovDriveSignIn = false;
static bool fm_driveHovItem  = -1;
static int  fm_driveScrollOff= 0;
// Simulated Drive items (populated after "sign in")
struct DriveItem { wstring name; wstring type; wstring modified; wstring size; };
static vector<DriveItem> fm_driveItems;

// --- Geometry cache ---
static float g_fm_cx = 0, g_fm_cy = 0, g_fm_cw = 0, g_fm_ch = 0;

// ============================================================
// HELPERS
// ============================================================
static void FillRect_(Graphics& g, SolidBrush* br, Pen* pen, float x, float y, float w, float h, float r = 0.0f) {
    if (r <= 0.0f) {
        if (br)  g.FillRectangle(br,  x, y, w, h);
        if (pen) g.DrawRectangle(pen, x, y, w, h);
    } else {
        GraphicsPath path;
        float d = r * 2.0f;
        path.AddArc(x,       y,       d, d, 180.0f, 90.0f);
        path.AddArc(x+w-d,   y,       d, d, 270.0f, 90.0f);
        path.AddArc(x+w-d,   y+h-d,   d, d,   0.0f, 90.0f);
        path.AddArc(x,       y+h-d,   d, d,  90.0f, 90.0f);
        path.CloseFigure();
        if (br)  g.FillPath(br,  &path);
        if (pen) g.DrawPath(pen, &path);
    }
}

static bool PtIn(float px, float py, float rx, float ry, float rw, float rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

static void RefreshLocalDir() {
    fm_items.clear();
    fm_scrollOffset = 0;
    fm_selectedItem = -1;
    fm_hovItem      = -1;

    wstring search = fm_currentPath;
    if (search.back() != L'\\') search += L'\\';
    search += L'*';

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        fm_items.push_back({ name, isDir });
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    // Dirs first, then files, alphabetical
    sort(fm_items.begin(), fm_items.end(), [](const pair<wstring,bool>& a, const pair<wstring,bool>& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    // Build breadcrumb
    fm_breadcrumb.clear();
    wstring p = fm_currentPath;
    if (p.back() == L'\\') p.pop_back();
    size_t pos = 0;
    while ((pos = p.find(L'\\')) != wstring::npos) {
        wstring seg = p.substr(0, pos);
        if (!seg.empty()) fm_breadcrumb.push_back(seg + L"\\");
        p = p.substr(pos + 1);
    }
    if (!p.empty()) fm_breadcrumb.push_back(p);
}

static void NavigateTo(const wstring& path) {
    fm_currentPath = path;
    if (!fm_currentPath.empty() && fm_currentPath.back() != L'\\')
        fm_currentPath += L'\\';
    RefreshLocalDir();
}

static void PopulateDriveItems() {
    fm_driveItems = {
        { L"My Documents",     L"Folder",       L"Sep 05, 2026", L"—"       },
        { L"Photos",           L"Folder",       L"Sep 03, 2026", L"—"       },
        { L"RasFocus Notes",   L"Google Docs",  L"Sep 06, 2026", L"12 KB"   },
        { L"Budget 2026",      L"Google Sheets",L"Aug 30, 2026", L"45 KB"   },
        { L"Project Slides",   L"Google Slides",L"Aug 28, 2026", L"2.1 MB"  },
        { L"resume_final.pdf", L"PDF",          L"Aug 20, 2026", L"340 KB"  },
        { L"Backup Archive",   L"Folder",       L"Jul 14, 2026", L"—"       },
        { L"recordings",       L"Folder",       L"Jun 30, 2026", L"—"       },
    };
}

// ============================================================
// DRAW
// ============================================================
void DrawFileManagerTab(Graphics& g, float cx, float cy, float cw, float ch) {
    g_fm_cx = cx; g_fm_cy = cy; g_fm_cw = cw; g_fm_ch = ch;

    // Init local dir on first draw
    static bool inited = false;
    if (!inited) { RefreshLocalDir(); inited = true; }

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // --- Fonts ---
    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fTitle(&ff, 15, FontStyleBold,    UnitPixel);
    Font fSub  (&ff, 13, FontStyleRegular, UnitPixel);
    Font fSmall(&ff, 12, FontStyleRegular, UnitPixel);
    Font fBold (&ff, 13, FontStyleBold,    UnitPixel);
    Font fIcon (&ffIcons, 16, FontStyleRegular, UnitPixel);
    Font fIconSm(&ffIcons, 14, FontStyleRegular, UnitPixel);

    // --- Brushes ---
    SolidBrush bWhite (Color(255, 255, 255, 255));
    SolidBrush bBg    (Color(255, 245, 248, 250));
    SolidBrush bDark  (Color(255,  40,  40,  40));
    SolidBrush bGray  (Color(255, 120, 120, 120));
    SolidBrush bTeal  (Color(255,   0, 150, 160));
    SolidBrush bTealLt(Color(255, 230, 250, 252));
    SolidBrush bBlue  (Color(255,  66, 133, 244));  // Google Drive blue
    SolidBrush bHov   (Color(255, 235, 248, 250));
    SolidBrush bSelBg (Color(255, 210, 240, 245));
    SolidBrush bSideBg(Color(255, 250, 252, 254));
    SolidBrush bRed   (Color(255, 220,  60,  60));
    SolidBrush bGreen (Color(255,  52, 168,  83));
    SolidBrush bYellow(Color(255, 245, 158,  11));

    Pen pBrd(Color(255, 218, 225, 232), 1.0f);
    Pen pTeal(Color(255, 0, 150, 160), 2.0f);
    Pen pWhite(Color(255, 255, 255, 255), 1.5f);

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter);  fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);     fmtR.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetFormatFlags(StringFormatFlagsNoWrap);
    fmtR.SetFormatFlags(StringFormatFlagsNoWrap);

    // ============================
    // BACKGROUND
    // ============================
    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // ============================
    // TOP SUB-TAB BAR  (height 48)
    // ============================
    float tabBarH = 48.0f;
    g.FillRectangle(&bWhite, cx, cy, cw, tabBarH);
    Pen pTabBrd(Color(255, 218, 225, 232), 1.0f);
    g.DrawLine(&pTabBrd, cx, cy + tabBarH, cx + cw, cy + tabBarH);

    auto DrawSubTab = [&](float tx, float ty, float tw, float th, const wchar_t* icon, const wchar_t* label, bool active, bool hov) {
        if (active) {
            g.FillRectangle(&bTealLt, tx, ty, tw, th);
            g.DrawLine(&pTeal, tx, ty + th - 2.0f, tx + tw, ty + th - 2.0f);
            g.DrawString(icon,  -1, &fIcon,  RectF(tx + 10.0f, ty, 24.0f, th), &fmtL, &bTeal);
            g.DrawString(label, -1, &fBold,  RectF(tx + 36.0f, ty, tw - 40.0f, th), &fmtL, &bTeal);
        } else {
            if (hov) { SolidBrush bh(Color(255, 245, 245, 245)); g.FillRectangle(&bh, tx, ty, tw, th); }
            g.DrawString(icon,  -1, &fIcon,  RectF(tx + 10.0f, ty, 24.0f, th), &fmtL, &bGray);
            g.DrawString(label, -1, &fSub,   RectF(tx + 36.0f, ty, tw - 40.0f, th), &fmtL, &bGray);
        }
    };

    float stW = 200.0f;
    DrawSubTab(cx + 10.0f,        cy + 4.0f, stW, tabBarH - 8.0f, L"\xEC50", L"Local Files",    fm_activeSubTab == 0, fm_hovTabLocal);
    DrawSubTab(cx + 10.0f + stW,  cy + 4.0f, stW, tabBarH - 8.0f, L"\xE753", L"Google Drive",   fm_activeSubTab == 1, fm_hovTabDrive);

    float bodyY = cy + tabBarH;
    float bodyH = ch - tabBarH;

    // ============================
    // LOCAL FILES TAB
    // ============================
    if (fm_activeSubTab == 0) {

        // ---- TOOLBAR (height 44) ----
        float tbH = 44.0f;
        g.FillRectangle(&bWhite, cx, bodyY, cw, tbH);
        Pen pTbBrd(Color(255, 218, 225, 232), 1.0f);
        g.DrawLine(&pTbBrd, cx, bodyY + tbH, cx + cw, bodyY + tbH);

        // Up button
        float btnW = 36.0f, btnH = 28.0f, btnY = bodyY + (tbH - btnH) / 2.0f;
        float bx = cx + 10.0f;
        {
            SolidBrush bBtnBg(fm_hovUp ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bBtnBg, &pBrd, bx, btnY, btnW, btnH, 4.0f);
            g.DrawString(L"\xE74A", -1, &fIconSm, RectF(bx, btnY, btnW, btnH), &fmtC, fm_hovUp ? &bTeal : &bGray);
        }
        bx += btnW + 6.0f;

        // Refresh
        {
            SolidBrush bBtnBg(fm_hovRefresh ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bBtnBg, &pBrd, bx, btnY, btnW, btnH, 4.0f);
            g.DrawString(L"\xE72C", -1, &fIconSm, RectF(bx, btnY, btnW, btnH), &fmtC, fm_hovRefresh ? &bTeal : &bGray);
        }
        bx += btnW + 6.0f;

        // New Folder
        float nbW = 110.0f;
        {
            SolidBrush bBtnBg(fm_hovNewFolder ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bBtnBg, &pBrd, bx, btnY, nbW, btnH, 4.0f);
            g.DrawString(L"\xE2AC  New Folder", -1, &fSmall, RectF(bx + 4.0f, btnY, nbW - 8.0f, btnH), &fmtL, fm_hovNewFolder ? &bTeal : &bGray);
        }
        bx += nbW + 6.0f;

        // Delete (only if item selected)
        if (fm_selectedItem >= 0) {
            float dW = 80.0f;
            SolidBrush bDel(fm_hovDelete ? Color(255, 255, 220, 220) : Color(255, 245, 248, 250));
            Pen pDel(Color(255, 220, 60, 60), 1.0f);
            FillRect_(g, &bDel, &pDel, bx, btnY, dW, btnH, 4.0f);
            g.DrawString(L"\xE74D  Delete", -1, &fSmall, RectF(bx + 4.0f, btnY, dW - 8.0f, btnH), &fmtL, &bRed);
            bx += dW + 6.0f;
        }

        // Open (only if item selected)
        if (fm_selectedItem >= 0) {
            float oW = 80.0f;
            SolidBrush bOp(fm_hovOpen ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bOp, &pBrd, bx, btnY, oW, btnH, 4.0f);
            g.DrawString(L"\xE8A7  Open", -1, &fSmall, RectF(bx + 4.0f, btnY, oW - 8.0f, btnH), &fmtL, fm_hovOpen ? &bTeal : &bGray);
        }

        // ---- BREADCRUMB (height 32) ----
        float bcY = bodyY + tbH;
        float bcH = 32.0f;
        SolidBrush bBcBg(Color(255, 250, 252, 254));
        g.FillRectangle(&bBcBg, cx, bcY, cw, bcH);
        Pen pBcBrd(Color(255, 218, 225, 232), 1.0f);
        g.DrawLine(&pBcBrd, cx, bcY + bcH, cx + cw, bcY + bcH);

        float bcX = cx + 10.0f;
        // Drive root icon
        g.DrawString(L"\xEC50", -1, &fIconSm, RectF(bcX, bcY, 20.0f, bcH), &fmtL, &bGray);
        bcX += 22.0f;

        for (int i = 0; i < (int)fm_breadcrumb.size(); i++) {
            bool hov = (fm_hovBreadcrumb == i);
            SolidBrush* c = hov ? &bTeal : &bGray;
            wstring seg = fm_breadcrumb[i];
            if (!seg.empty() && seg.back() == L'\\') seg.pop_back();
            RectF tr(bcX, bcY, 200.0f, bcH);
            g.DrawString(seg.c_str(), -1, hov ? &fBold : &fSmall, tr, &fmtL, c);
            // measure width
            SizeF sz;
            g.MeasureString(seg.c_str(), -1, hov ? &fBold : &fSmall, SizeF(500,bcH), &sz);
            bcX += sz.Width;
            if (i < (int)fm_breadcrumb.size() - 1) {
                g.DrawString(L"\xE76C", -1, &fIconSm, RectF(bcX, bcY, 16.0f, bcH), &fmtL, &bGray);
                bcX += 16.0f;
            }
        }

        // ---- LEFT SIDEBAR (quick access, width 160) ----
        float sideW = 160.0f;
        float listY = bcY + bcH;
        float listH = bodyH - tbH - bcH;

        SolidBrush bSide(Color(255, 248, 250, 252));
        g.FillRectangle(&bSide, cx, listY, sideW, listH);
        Pen pSideBrd(Color(255, 218, 225, 232), 1.0f);
        g.DrawLine(&pSideBrd, cx + sideW, listY, cx + sideW, listY + listH);

        struct QuickItem { const wchar_t* icon; const wchar_t* label; const wchar_t* path; };
        QuickItem quickItems[] = {
            { L"\xE8B7", L"Desktop",    L"" },
            { L"\xEC0A", L"Downloads",  L"" },
            { L"\xE8A5", L"Documents",  L"" },
            { L"\xEB9F", L"Pictures",   L"" },
            { L"\xEC4F", L"Music",      L"" },
            { L"\xE8B2", L"Videos",     L"" },
            { L"\xEDA2", L"This PC",    L"" },
            { L"\xE7D2", L"C:\\",       L"C:\\" },
            { L"\xE7D2", L"D:\\",       L"D:\\" },
        };
        // Fill paths dynamically
        wchar_t desktopPath[MAX_PATH], dlPath[MAX_PATH], docPath[MAX_PATH];
        wchar_t picPath[MAX_PATH], musicPath[MAX_PATH], vidPath[MAX_PATH];
        SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath);
        SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docPath);
        SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, 0, picPath);
        SHGetFolderPathW(NULL, CSIDL_MYMUSIC, NULL, 0, musicPath);
        SHGetFolderPathW(NULL, CSIDL_MYVIDEO, NULL, 0, vidPath);
        // Downloads
        PWSTR dlRaw = NULL;
        SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &dlRaw);
        if (dlRaw) { wcscpy_s(dlPath, dlRaw); CoTaskMemFree(dlRaw); }

        // Overwrite the path fields
        const wchar_t* pathArr[] = { desktopPath, dlPath, docPath, picPath, musicPath, vidPath, L"", L"C:\\", L"D:\\" };

        float qH = 34.0f;
        for (int i = 0; i < 9; i++) {
            float qY = listY + 8.0f + i * qH;
            bool isActive = (pathArr[i][0] != 0 && fm_currentPath.find(pathArr[i]) == 0);
            if (isActive) {
                SolidBrush qAct(Color(255, 225, 245, 248));
                g.FillRectangle(&qAct, cx, qY, sideW, qH);
                g.FillRectangle(&bTeal, cx, qY + 4.0f, 3.0f, qH - 8.0f);
            }
            g.DrawString(quickItems[i].icon,  -1, &fIconSm, RectF(cx + 10.0f, qY, 20.0f, qH), &fmtL, isActive ? &bTeal : &bGray);
            g.DrawString(quickItems[i].label, -1, &fSmall,  RectF(cx + 34.0f, qY, sideW - 38.0f, qH), &fmtL, isActive ? &bTeal : &bDark);
        }

        // ---- FILE LIST (right of sidebar) ----
        float flX = cx + sideW;
        float flW = cw - sideW;

        // Column header (height 28)
        float colHdrH = 28.0f;
        SolidBrush bColHdr(Color(255, 240, 244, 248));
        g.FillRectangle(&bColHdr, flX, listY, flW, colHdrH);
        Pen pColBrd(Color(255, 218, 225, 232), 1.0f);
        g.DrawLine(&pColBrd, flX, listY + colHdrH, flX + flW, listY + colHdrH);

        float c1W = flW * 0.50f, c2W = flW * 0.15f, c3W = flW * 0.20f, c4W = flW * 0.15f;
        float hdrY = listY;
        g.DrawString(L"Name",      -1, &fSmall, RectF(flX + 10.0f,            hdrY, c1W, colHdrH), &fmtL, &bGray);
        g.DrawString(L"Type",      -1, &fSmall, RectF(flX + c1W,              hdrY, c2W, colHdrH), &fmtL, &bGray);
        g.DrawString(L"Modified",  -1, &fSmall, RectF(flX + c1W + c2W,        hdrY, c3W, colHdrH), &fmtL, &bGray);
        g.DrawString(L"Size",      -1, &fSmall, RectF(flX + c1W + c2W + c3W,  hdrY, c4W, colHdrH), &fmtR, &bGray);

        // File rows
        float rowH = 34.0f;
        float rowsY = listY + colHdrH;
        float rowsH = listH - colHdrH;
        int maxVisible = (int)(rowsH / rowH);

        // Clip to rows area
        Region clipRegion(RectF(flX, rowsY, flW, rowsH));
        g.SetClip(&clipRegion);

        if (fm_items.empty()) {
            g.DrawString(L"This folder is empty.", -1, &fSub,
                RectF(flX, rowsY + rowsH / 2.0f - 10.0f, flW, 24.0f), &fmtC, &bGray);
        } else {
            for (int i = fm_scrollOffset; i < (int)fm_items.size() && i < fm_scrollOffset + maxVisible + 1; i++) {
                float ry = rowsY + (i - fm_scrollOffset) * rowH;
                if (ry + rowH < rowsY || ry > rowsY + rowsH) continue;

                bool isDir = fm_items[i].second;
                bool isSel = (fm_selectedItem == i);
                bool isHov = (fm_hovItem == i);

                if (isSel)       { g.FillRectangle(&bSelBg, flX, ry, flW, rowH); }
                else if (isHov)  { g.FillRectangle(&bHov,   flX, ry, flW, rowH); }

                // Separator
                Pen pRow(Color(255, 235, 240, 244), 1.0f);
                g.DrawLine(&pRow, flX, ry + rowH, flX + flW, ry + rowH);

                // Icon
                const wchar_t* ico = isDir ? L"\xED41" : L"\xE8A5";
                SolidBrush bIco(isDir ? Color(255, 245, 158, 11) : Color(255, 100, 130, 200));
                g.DrawString(ico, -1, &fIconSm, RectF(flX + 8.0f, ry, 22.0f, rowH), &fmtL, &bIco);

                // Name
                g.DrawString(fm_items[i].first.c_str(), -1, &fSmall,
                    RectF(flX + 34.0f, ry, c1W - 38.0f, rowH), &fmtL, isSel ? &bDark : &bDark);

                // Type
                wstring typeStr = isDir ? L"Folder" : L"File";
                wstring name = fm_items[i].first;
                size_t dot = name.rfind(L'.');
                if (!isDir && dot != wstring::npos) typeStr = name.substr(dot + 1) + L" File";
                g.DrawString(typeStr.c_str(), -1, &fSmall, RectF(flX + c1W, ry, c2W, rowH), &fmtL, &bGray);

                // Modified — get from filesystem
                WIN32_FILE_ATTRIBUTE_DATA fad;
                wstring fullPath = fm_currentPath + fm_items[i].first;
                wstring modStr = L"—";
                if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &fad)) {
                    FILETIME ft = fad.ftLastWriteTime;
                    SYSTEMTIME st; FileTimeToSystemTime(&ft, &st);
                    wchar_t buf[32];
                    swprintf(buf, 32, L"%02d/%02d/%04d", st.wDay, st.wMonth, st.wYear);
                    modStr = buf;
                }
                g.DrawString(modStr.c_str(), -1, &fSmall, RectF(flX + c1W + c2W, ry, c3W, rowH), &fmtL, &bGray);

                // Size
                wstring sizeStr = L"—";
                if (!isDir) {
                    WIN32_FIND_DATAW fd2;
                    HANDLE h2 = FindFirstFileW(fullPath.c_str(), &fd2);
                    if (h2 != INVALID_HANDLE_VALUE) {
                        ULONGLONG sz = ((ULONGLONG)fd2.nFileSizeHigh << 32) | fd2.nFileSizeLow;
                        wchar_t buf[32];
                        if      (sz < 1024)          swprintf(buf, 32, L"%llu B",   sz);
                        else if (sz < 1024*1024)     swprintf(buf, 32, L"%llu KB",  sz/1024);
                        else if (sz < 1024*1024*1024)swprintf(buf, 32, L"%llu MB",  sz/(1024*1024));
                        else                          swprintf(buf, 32, L"%llu GB",  sz/(1024*1024*1024));
                        sizeStr = buf;
                        FindClose(h2);
                    }
                }
                g.DrawString(sizeStr.c_str(), -1, &fSmall, RectF(flX + c1W + c2W + c3W, ry, c4W - 6.0f, rowH), &fmtR, &bGray);
            }
        }

        g.ResetClip();

        // Scrollbar
        if ((int)fm_items.size() > maxVisible) {
            float sbW = 6.0f, sbX = flX + flW - sbW - 2.0f;
            float sbTotalH = rowsH;
            float thumbH = max(30.0f, sbTotalH * maxVisible / (float)fm_items.size());
            float thumbY = rowsY + sbTotalH * fm_scrollOffset / (float)fm_items.size();
            SolidBrush bThumb(Color(180, 0, 150, 160));
            FillRect_(g, &bThumb, nullptr, sbX, thumbY, sbW, thumbH, 3.0f);
        }

        // ---- EMPTY STATE ----
        if (fm_items.empty()) {
            // already handled above
        }

    }

    // ============================
    // GOOGLE DRIVE TAB
    // ============================
    else if (fm_activeSubTab == 1) {

        if (!fm_driveSignedIn) {
            // Sign-in prompt
            float cardW = 380.0f, cardH = 220.0f;
            float cardX = cx + (cw - cardW) / 2.0f;
            float cardY = bodyY + (bodyH - cardH) / 2.0f;

            SolidBrush bCard(Color(255, 255, 255, 255));
            Pen pCard(Color(255, 218, 225, 232), 1.5f);
            FillRect_(g, &bCard, &pCard, cardX, cardY, cardW, cardH, 8.0f);

            // Google Drive icon (colored circles)
            float icY = cardY + 28.0f;
            float icX = cardX + cardW / 2.0f - 24.0f;
            SolidBrush bDrBlue (Color(255,  66, 133, 244));
            SolidBrush bDrGreen(Color(255,  52, 168,  83));
            SolidBrush bDrYell (Color(255, 251, 188,   5));
            g.FillEllipse(&bDrBlue,  icX,       icY, 22.0f, 22.0f);
            g.FillEllipse(&bDrGreen, icX + 12.0f, icY, 22.0f, 22.0f);
            g.FillEllipse(&bDrYell,  icX + 24.0f, icY, 22.0f, 22.0f);

            FontFamily ffTitle(L"Segoe UI");
            Font fDrTitle(&ffTitle, 17, FontStyleBold, UnitPixel);
            Font fDrSub  (&ffTitle, 13, FontStyleRegular, UnitPixel);
            Font fDrBtn  (&ffTitle, 13, FontStyleBold, UnitPixel);

            g.DrawString(L"Google Drive", -1, &fDrTitle,
                RectF(cardX, cardY + 62.0f, cardW, 28.0f), &fmtC, &bDark);
            g.DrawString(L"Sign in to browse and manage your\nGoogle Drive files directly here.",
                -1, &fDrSub, RectF(cardX + 20.0f, cardY + 95.0f, cardW - 40.0f, 50.0f), &fmtC, &bGray);

            // Sign-in button
            float btnW2 = 200.0f, btnH2 = 38.0f;
            float btnX2 = cardX + (cardW - btnW2) / 2.0f;
            float btnY2 = cardY + cardH - 55.0f;
            SolidBrush bSignIn(fm_hovDriveSignIn ? Color(255, 50, 110, 220) : Color(255, 66, 133, 244));
            FillRect_(g, &bSignIn, nullptr, btnX2, btnY2, btnW2, btnH2, 6.0f);
            g.DrawString(L"\xE8A0  Sign in with Google", -1, &fDrBtn,
                RectF(btnX2, btnY2, btnW2, btnH2), &fmtC, &bWhite);

        } else {
            // Signed-in Drive view
            // Storage bar (header, height 56)
            float hdrH = 56.0f;
            g.FillRectangle(&bWhite, cx, bodyY, cw, hdrH);
            Pen pHdrBrd(Color(255, 218, 225, 232), 1.0f);
            g.DrawLine(&pHdrBrd, cx, bodyY + hdrH, cx + cw, bodyY + hdrH);

            // Avatar + name
            SolidBrush bAvatar(Color(255, 66, 133, 244));
            FillRect_(g, &bAvatar, nullptr, cx + 14.0f, bodyY + 14.0f, 28.0f, 28.0f, 14.0f);
            g.DrawString(L"R", -1, &fBold, RectF(cx + 14.0f, bodyY + 14.0f, 28.0f, 28.0f), &fmtC, &bWhite);
            g.DrawString(L"rasfocus@gmail.com", -1, &fSmall, RectF(cx + 48.0f, bodyY + 14.0f, 200.0f, 28.0f), &fmtL, &bDark);

            // Storage bar
            float stBarX = cx + cw - 260.0f, stBarY = bodyY + 20.0f, stBarW = 220.0f, stBarH = 10.0f;
            SolidBrush bStBg(Color(255, 220, 225, 230));
            FillRect_(g, &bStBg, nullptr, stBarX, stBarY, stBarW, stBarH, 5.0f);
            SolidBrush bStFill(Color(255, 66, 133, 244));
            FillRect_(g, &bStFill, nullptr, stBarX, stBarY, stBarW * 0.42f, stBarH, 5.0f);
            g.DrawString(L"6.3 GB of 15 GB used", -1, &fSmall,
                RectF(stBarX, stBarY + 14.0f, stBarW, 20.0f), &fmtL, &bGray);

            // Column header
            float dvListY = bodyY + hdrH;
            float colHdrH = 28.0f;
            SolidBrush bDvColHdr(Color(255, 240, 244, 248));
            g.FillRectangle(&bDvColHdr, cx, dvListY, cw, colHdrH);
            Pen pDvCol(Color(255, 218, 225, 232), 1.0f);
            g.DrawLine(&pDvCol, cx, dvListY + colHdrH, cx + cw, dvListY + colHdrH);

            float dc1 = cw * 0.45f, dc2 = cw * 0.20f, dc3 = cw * 0.20f, dc4 = cw * 0.15f;
            g.DrawString(L"Name",     -1, &fSmall, RectF(cx + 10.0f,             dvListY, dc1, colHdrH), &fmtL, &bGray);
            g.DrawString(L"Type",     -1, &fSmall, RectF(cx + dc1,               dvListY, dc2, colHdrH), &fmtL, &bGray);
            g.DrawString(L"Modified", -1, &fSmall, RectF(cx + dc1 + dc2,         dvListY, dc3, colHdrH), &fmtL, &bGray);
            g.DrawString(L"Size",     -1, &fSmall, RectF(cx + dc1 + dc2 + dc3,   dvListY, dc4, colHdrH), &fmtR, &bGray);

            float dvRowH = 38.0f;
            float dvRowsY = dvListY + colHdrH;
            float dvRowsH = bodyH - hdrH - colHdrH;
            int dvMaxVis  = (int)(dvRowsH / dvRowH);

            Region dvClip(RectF(cx, dvRowsY, cw, dvRowsH));
            g.SetClip(&dvClip);

            for (int i = fm_driveScrollOff; i < (int)fm_driveItems.size() && i < fm_driveScrollOff + dvMaxVis + 1; i++) {
                float ry = dvRowsY + (i - fm_driveScrollOff) * dvRowH;
                bool isHov = (fm_driveHovItem == i);
                if (isHov) { SolidBrush bDvHov(Color(255, 245, 248, 252)); g.FillRectangle(&bDvHov, cx, ry, cw, dvRowH); }
                Pen pDvRow(Color(255, 235, 240, 244), 1.0f);
                g.DrawLine(&pDvRow, cx, ry + dvRowH, cx + cw, ry + dvRowH);

                bool isFolder = (fm_driveItems[i].type == L"Folder");
                const wchar_t* ico = isFolder ? L"\xED41" : L"\xE8A5";
                SolidBrush bDvIco(isFolder ? Color(255, 66, 133, 244) : Color(255, 100, 130, 200));
                if (fm_driveItems[i].type == L"Google Docs")   bDvIco = SolidBrush(Color(255, 66, 133, 244));
                if (fm_driveItems[i].type == L"Google Sheets") bDvIco = SolidBrush(Color(255, 52, 168, 83));
                if (fm_driveItems[i].type == L"Google Slides") bDvIco = SolidBrush(Color(255, 251, 188, 5));
                if (fm_driveItems[i].type == L"PDF")           bDvIco = SolidBrush(Color(255, 220, 60, 60));

                g.DrawString(ico, -1, &fIconSm, RectF(cx + 8.0f, ry, 22.0f, dvRowH), &fmtL, &bDvIco);
                g.DrawString(fm_driveItems[i].name.c_str(),     -1, &fSmall, RectF(cx + 34.0f, ry, dc1 - 38.0f, dvRowH), &fmtL, &bDark);
                g.DrawString(fm_driveItems[i].type.c_str(),     -1, &fSmall, RectF(cx + dc1,            ry, dc2, dvRowH), &fmtL, &bGray);
                g.DrawString(fm_driveItems[i].modified.c_str(), -1, &fSmall, RectF(cx + dc1 + dc2,      ry, dc3, dvRowH), &fmtL, &bGray);
                g.DrawString(fm_driveItems[i].size.c_str(),     -1, &fSmall, RectF(cx + dc1 + dc2 + dc3, ry, dc4 - 6.0f, dvRowH), &fmtR, &bGray);
            }

            g.ResetClip();

            // Scrollbar
            if ((int)fm_driveItems.size() > dvMaxVis) {
                float sbW = 6.0f, sbX = cx + cw - sbW - 2.0f;
                float sbTotalH = dvRowsH;
                float thumbH = max(30.0f, sbTotalH * dvMaxVis / (float)fm_driveItems.size());
                float thumbY2 = dvRowsY + sbTotalH * fm_driveScrollOff / (float)fm_driveItems.size();
                SolidBrush bThumb2(Color(180, 66, 133, 244));
                FillRect_(g, &bThumb2, nullptr, sbX, thumbY2, sbW, thumbH, 3.0f);
            }
        }
    }
}

// ============================================================
// MOUSE MOVE
// ============================================================
void ProcessFileManagerMouseMove(float x, float y) {
    float cx = g_fm_cx, cy = g_fm_cy, cw = g_fm_cw, ch = g_fm_ch;

    bool old_hovTabLocal  = fm_hovTabLocal;
    bool old_hovTabDrive  = fm_hovTabDrive;
    bool old_hovUp        = fm_hovUp;
    bool old_hovRefresh   = fm_hovRefresh;
    bool old_hovNewFolder = fm_hovNewFolder;
    bool old_hovDelete    = fm_hovDelete;
    bool old_hovOpen      = fm_hovOpen;
    int  old_hovItem      = fm_hovItem;
    int  old_hovBreadcrumb= fm_hovBreadcrumb;
    bool old_hovDriveSignIn = fm_hovDriveSignIn;
    int  old_driveHovItem = fm_driveHovItem;

    fm_hovTabLocal = fm_hovTabDrive = false;
    fm_hovUp = fm_hovRefresh = fm_hovNewFolder = fm_hovDelete = fm_hovOpen = false;
    fm_hovItem = -1; fm_hovBreadcrumb = -1;
    fm_hovDriveSignIn = false; fm_driveHovItem = -1;

    // Sub tabs
    float tabBarH = 48.0f;
    float stW = 200.0f;
    if (PtIn(x, y, cx + 10.0f, cy + 4.0f, stW, tabBarH - 8.0f)) fm_hovTabLocal = true;
    if (PtIn(x, y, cx + 10.0f + stW, cy + 4.0f, stW, tabBarH - 8.0f)) fm_hovTabDrive = true;

    float bodyY = cy + tabBarH;
    float bodyH = ch - tabBarH;

    if (fm_activeSubTab == 0) {
        // Toolbar
        float tbH = 44.0f;
        float btnW = 36.0f, btnH = 28.0f, btnY = bodyY + (tbH - btnH) / 2.0f;
        float bx = cx + 10.0f;
        if (PtIn(x, y, bx, btnY, btnW, btnH)) fm_hovUp = true;
        bx += btnW + 6.0f;
        if (PtIn(x, y, bx, btnY, btnW, btnH)) fm_hovRefresh = true;
        bx += btnW + 6.0f;
        float nbW = 110.0f;
        if (PtIn(x, y, bx, btnY, nbW, btnH)) fm_hovNewFolder = true;
        bx += nbW + 6.0f;
        if (fm_selectedItem >= 0) {
            float dW = 80.0f;
            if (PtIn(x, y, bx, btnY, dW, btnH)) fm_hovDelete = true;
            bx += dW + 6.0f;
            float oW = 80.0f;
            if (PtIn(x, y, bx, btnY, oW, btnH)) fm_hovOpen = true;
        }

        // Breadcrumb
        float bcY = bodyY + tbH;
        float bcH = 32.0f;
        float bcX = cx + 32.0f;
        for (int i = 0; i < (int)fm_breadcrumb.size(); i++) {
            if (PtIn(x, y, bcX, bcY, 150.0f, bcH)) { fm_hovBreadcrumb = i; break; }
            bcX += 100.0f; // rough estimate
        }

        // File rows
        float sideW = 160.0f;
        float listY = bcY + bcH;
        float listH = bodyH - tbH - bcH;
        float colHdrH = 28.0f;
        float flX = cx + sideW;
        float flW = cw - sideW;
        float rowH = 34.0f;
        float rowsY = listY + colHdrH;
        float rowsH = listH - colHdrH;
        int maxVisible = (int)(rowsH / rowH);
        if (PtIn(x, y, flX, rowsY, flW, rowsH)) {
            int idx = (int)((y - rowsY) / rowH) + fm_scrollOffset;
            if (idx >= 0 && idx < (int)fm_items.size()) fm_hovItem = idx;
        }

    } else if (fm_activeSubTab == 1) {
        if (!fm_driveSignedIn) {
            float cardW = 380.0f, cardH = 220.0f;
            float cardX = cx + (cw - cardW) / 2.0f;
            float cardY = bodyY + (bodyH - cardH) / 2.0f;
            float btnW2 = 200.0f, btnH2 = 38.0f;
            float btnX2 = cardX + (cardW - btnW2) / 2.0f;
            float btnY2 = cardY + cardH - 55.0f;
            if (PtIn(x, y, btnX2, btnY2, btnW2, btnH2)) fm_hovDriveSignIn = true;
        } else {
            float hdrH = 56.0f;
            float dvListY = bodyY + hdrH;
            float colHdrH = 28.0f;
            float dvRowH = 38.0f;
            float dvRowsY = dvListY + colHdrH;
            float dvRowsH = bodyH - hdrH - colHdrH;
            if (PtIn(x, y, cx, dvRowsY, cw, dvRowsH)) {
                int idx = (int)((y - dvRowsY) / dvRowH) + fm_driveScrollOff;
                if (idx >= 0 && idx < (int)fm_driveItems.size()) fm_driveHovItem = idx;
            }
        }
    }

    extern HWND hParentWnd;
    bool changed = (old_hovTabLocal != fm_hovTabLocal || old_hovTabDrive != fm_hovTabDrive ||
                    old_hovUp != fm_hovUp || old_hovRefresh != fm_hovRefresh ||
                    old_hovNewFolder != fm_hovNewFolder || old_hovDelete != fm_hovDelete ||
                    old_hovOpen != fm_hovOpen || old_hovItem != fm_hovItem ||
                    old_hovBreadcrumb != fm_hovBreadcrumb || old_hovDriveSignIn != fm_hovDriveSignIn ||
                    old_driveHovItem != fm_driveHovItem);
    if (changed && hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
}

// ============================================================
// MOUSE CLICK
// ============================================================
void ProcessFileManagerMouseClick(float x, float y, HWND hWnd) {
    float cx = g_fm_cx, cy = g_fm_cy, cw = g_fm_cw, ch = g_fm_ch;
    float tabBarH = 48.0f;
    float stW = 200.0f;

    // Sub-tab switch
    if (PtIn(x, y, cx + 10.0f, cy + 4.0f, stW, tabBarH - 8.0f)) {
        fm_activeSubTab = 0;
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }
    if (PtIn(x, y, cx + 10.0f + stW, cy + 4.0f, stW, tabBarH - 8.0f)) {
        fm_activeSubTab = 1;
        if (fm_driveSignedIn && fm_driveItems.empty()) PopulateDriveItems();
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }

    float bodyY = cy + tabBarH;
    float bodyH = ch - tabBarH;

    if (fm_activeSubTab == 0) {
        float tbH = 44.0f;
        float btnW = 36.0f, btnH = 28.0f, btnY = bodyY + (tbH - btnH) / 2.0f;
        float bx = cx + 10.0f;

        // Up button
        if (PtIn(x, y, bx, btnY, btnW, btnH)) {
            wstring p = fm_currentPath;
            if (!p.empty() && p.back() == L'\\') p.pop_back();
            size_t pos = p.rfind(L'\\');
            if (pos != wstring::npos) NavigateTo(p.substr(0, pos + 1));
            else if (p.length() >= 2) NavigateTo(p.substr(0, 3));
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
        bx += btnW + 6.0f;

        // Refresh
        if (PtIn(x, y, bx, btnY, btnW, btnH)) {
            RefreshLocalDir();
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
        bx += btnW + 6.0f;

        // New Folder
        float nbW = 110.0f;
        if (PtIn(x, y, bx, btnY, nbW, btnH)) {
            wchar_t name[MAX_PATH] = L"New Folder";
            // Simple dialog prompt (reuse InputBox style)
            wstring fullPath = fm_currentPath + L"New Folder";
            CreateDirectoryW(fullPath.c_str(), NULL);
            RefreshLocalDir();
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
        bx += nbW + 6.0f;

        // Delete
        if (fm_selectedItem >= 0) {
            float dW = 80.0f;
            if (PtIn(x, y, bx, btnY, dW, btnH)) {
                wstring fullPath = fm_currentPath + fm_items[fm_selectedItem].first;
                if (fm_items[fm_selectedItem].second) {
                    RemoveDirectoryW(fullPath.c_str());
                } else {
                    DeleteFileW(fullPath.c_str());
                }
                fm_selectedItem = -1;
                RefreshLocalDir();
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
            bx += dW + 6.0f;

            // Open
            float oW = 80.0f;
            if (PtIn(x, y, bx, btnY, oW, btnH)) {
                if (fm_selectedItem >= 0) {
                    wstring fullPath = fm_currentPath + fm_items[fm_selectedItem].first;
                    ShellExecuteW(NULL, L"open", fullPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
                return;
            }
        }

        // Breadcrumb click
        float bcY = bodyY + tbH;
        float bcH = 32.0f;
        // Rebuild breadcrumb X positions
        float bcX = cx + 32.0f;
        for (int i = 0; i < (int)fm_breadcrumb.size(); i++) {
            if (PtIn(x, y, bcX, bcY, 150.0f, bcH)) {
                // Navigate to this breadcrumb segment
                wstring dest;
                for (int j = 0; j <= i; j++) {
                    dest += fm_breadcrumb[j];
                    if (!dest.empty() && dest.back() != L'\\') dest += L'\\';
                }
                NavigateTo(dest);
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
            bcX += 116.0f;
        }

        // File list click
        float sideW = 160.0f;
        float listY = bcY + bcH;
        float listH = bodyH - tbH - bcH;
        float colHdrH = 28.0f;
        float flX = cx + sideW;
        float flW = cw - sideW;
        float rowH = 34.0f;
        float rowsY = listY + colHdrH;
        float rowsH = listH - colHdrH;

        if (PtIn(x, y, flX, rowsY, flW, rowsH)) {
            int idx = (int)((y - rowsY) / rowH) + fm_scrollOffset;
            if (idx >= 0 && idx < (int)fm_items.size()) {
                if (fm_selectedItem == idx && fm_items[idx].second) {
                    // Double-click into folder (treated as two single clicks on same item)
                    wstring dest = fm_currentPath + fm_items[idx].first + L"\\";
                    NavigateTo(dest);
                    if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                    return;
                }
                fm_selectedItem = idx;
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            }
        }

        // Quick sidebar click
        wchar_t desktopPath[MAX_PATH], dlPath[MAX_PATH], docPath[MAX_PATH];
        wchar_t picPath[MAX_PATH], musicPath[MAX_PATH], vidPath[MAX_PATH];
        SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath);
        SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docPath);
        SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, 0, picPath);
        SHGetFolderPathW(NULL, CSIDL_MYMUSIC, NULL, 0, musicPath);
        SHGetFolderPathW(NULL, CSIDL_MYVIDEO, NULL, 0, vidPath);
        PWSTR dlRaw = NULL;
        SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &dlRaw);
        if (dlRaw) { wcscpy_s(dlPath, dlRaw); CoTaskMemFree(dlRaw); }

        const wchar_t* pathArr[] = { desktopPath, dlPath, docPath, picPath, musicPath, vidPath, L"", L"C:\\", L"D:\\" };
        float qH = 34.0f;
        for (int i = 0; i < 9; i++) {
            float qY = listY + 8.0f + i * qH;
            if (PtIn(x, y, cx, qY, sideW, qH) && pathArr[i][0] != 0) {
                NavigateTo(pathArr[i]);
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
        }

    } else if (fm_activeSubTab == 1) {
        if (!fm_driveSignedIn) {
            float cardW = 380.0f, cardH = 220.0f;
            float cardX = cx + (cw - cardW) / 2.0f;
            float cardY = bodyY + (bodyH - cardH) / 2.0f;
            float btnW2 = 200.0f, btnH2 = 38.0f;
            float btnX2 = cardX + (cardW - btnW2) / 2.0f;
            float btnY2 = cardY + cardH - 55.0f;
            if (PtIn(x, y, btnX2, btnY2, btnW2, btnH2)) {
                // Open Google Drive OAuth in browser
                ShellExecuteW(NULL, L"open", L"https://drive.google.com", NULL, NULL, SW_SHOWNORMAL);
                // Simulate sign-in (in a real app: OAuth flow)
                fm_driveSignedIn = true;
                PopulateDriveItems();
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            }
        } else {
            // Drive item double-click: open in browser
            float hdrH = 56.0f;
            float dvListY = bodyY + hdrH;
            float colHdrH = 28.0f;
            float dvRowH = 38.0f;
            float dvRowsY = dvListY + colHdrH;
            float dvRowsH = bodyH - hdrH - colHdrH;
            if (PtIn(x, y, cx, dvRowsY, cw, dvRowsH)) {
                int idx = (int)((y - dvRowsY) / dvRowH) + fm_driveScrollOff;
                if (idx >= 0 && idx < (int)fm_driveItems.size()) {
                    ShellExecuteW(NULL, L"open", L"https://drive.google.com", NULL, NULL, SW_SHOWNORMAL);
                }
            }
        }
    }
}

// ============================================================
// MOUSE WHEEL
// ============================================================
void ProcessFileManagerMouseWheel(float x, float y, int delta) {
    int step = (delta > 0) ? -3 : 3;
    if (fm_activeSubTab == 0) {
        fm_scrollOffset = max(0, min((int)fm_items.size() - 1, fm_scrollOffset + step));
    } else {
        fm_driveScrollOff = max(0, min((int)fm_driveItems.size() - 1, fm_driveScrollOff + step));
    }
    extern HWND hParentWnd;
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
}
