// image_viewer.cpp
// RasFocus+ Native GDI+ Image Viewer  –  v2.0
// Features:
//   - Smooth Animated Zoom : Mouse Wheel  OR  +/- keys  OR toolbar buttons
//   - Zoom anchored to cursor position (zoom centre = mouse position)
//   - Pan          : Left-drag (when zoomed)
//   - Fit/Actual   : F key OR double-click
//   - Prev / Next  : Left/Right arrow keys  OR  A/D  OR  on-screen buttons
//   - FAST nav     : images pre-cached in background thread
//   - Full Screen  : F11  OR  toolbar button (toggle)
//   - Rotate       : R key (90° CW)
//   - Close        : ESC  OR  Alt+F4  OR  title-bar X
//   - Checkerboard : shown behind transparent PNG/GIF images
//   - Status bar   : filename  |  resolution  |  zoom %  |  index / total
//   - Supported    : jpg, jpeg, png, gif, bmp, webp, ico, tiff, tif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include "image_viewer.h"

#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <shlwapi.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "Shlwapi.lib")

using namespace Gdiplus;
using namespace std;

// ============================================================
// CONSTANTS
// ============================================================
static const wchar_t* IV_CLASS    = L"RasFocusImageViewer";
static const int      TOOLBAR_H   = 48;
static const int      STATUSBAR_H = 22;
static const float    ZOOM_MIN    = 0.05f;
static const float    ZOOM_MAX    = 32.0f;

// Smooth zoom animation
static const float    ZOOM_ANIM_SPEED = 0.18f;  // lerp factor per timer tick
static const int      ZOOM_TIMER_ID   = 1;
static const int      ZOOM_TIMER_MS   = 10;     // ~100fps animation

// Pre-cache: keep prev+next loaded
static const int      CACHE_PREV  = 0;
static const int      CACHE_NEXT  = 1;

// ============================================================
// IMAGE EXTENSIONS
// ============================================================
static bool IsViewableExt(const wstring& extLower) {
    return extLower == L"jpg"  || extLower == L"jpeg" ||
           extLower == L"png"  || extLower == L"gif"  ||
           extLower == L"bmp"  || extLower == L"webp" ||
           extLower == L"ico"  || extLower == L"tiff" ||
           extLower == L"tif";
}

// ============================================================
// VIEWER STATE
// ============================================================
struct IVState {
    // image list
    vector<wstring> files;
    int             index = 0;

    // current image
    Image*  img    = nullptr;
    wstring path;
    int     rotation = 0;

    // neighbour cache  [0]=prev  [1]=next
    Image*  cache[2]      = { nullptr, nullptr };
    int     cacheIdx[2]   = { -1, -1 };
    HANDLE  cacheThread[2]= { NULL, NULL };
    CRITICAL_SECTION cacheCS;

    // view transform  (target = where we're animating toward)
    float   zoom       = 1.0f;
    float   zoomTarget = 1.0f;
    float   panX       = 0.0f;
    float   panY       = 0.0f;
    float   panXTarget = 0.0f;
    float   panYTarget = 0.0f;

    // zoom pivot (cursor position in IMAGE AREA coords, relative to area centre)
    float   pivotX = 0.0f;
    float   pivotY = 0.0f;

    // drag
    bool    dragging   = false;
    int     dragStartX = 0, dragStartY = 0;
    float   panXAtDrag = 0, panYAtDrag = 0;

    // toolbar hover
    int     hovBtn = -1;

    // full-screen state
    bool    isFullScreen = false;
    RECT    savedWndRect = {};
    DWORD   savedStyle   = 0;
    DWORD   savedExStyle = 0;

    // window
    HWND       hWnd     = nullptr;
    ULONG_PTR  gdipToken= 0;
};
static IVState iv;

// ============================================================
// HELPERS
// ============================================================
static wstring ExtLower(const wstring& p) {
    size_t dot = p.rfind(L'.');
    if (dot == wstring::npos) return L"";
    wstring e = p.substr(dot + 1);
    for (auto& c : e) c = towlower(c);
    return e;
}
static wstring Filename(const wstring& p) {
    size_t sl = p.find_last_of(L"\\/");
    return (sl == wstring::npos) ? p : p.substr(sl + 1);
}

// Image-area dimensions (client coords)
static void GetImgArea(RECT& out) {
    RECT rc; GetClientRect(iv.hWnd, &rc);
    out.left   = 0;
    out.top    = TOOLBAR_H;
    out.right  = rc.right;
    out.bottom = rc.bottom - STATUSBAR_H;
}

// ============================================================
// FILE LIST
// ============================================================
static void BuildFileList(const wstring& startPath) {
    iv.files.clear(); iv.index = 0;
    size_t sl = startPath.find_last_of(L"\\/");
    if (sl == wstring::npos) { iv.files.push_back(startPath); return; }

    wstring dir = startPath.substr(0, sl + 1);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) { iv.files.push_back(startPath); return; }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wstring name = fd.cFileName;
        if (IsViewableExt(ExtLower(name))) iv.files.push_back(dir + name);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    sort(iv.files.begin(), iv.files.end(),
         [](const wstring& a, const wstring& b){ return _wcsicmp(a.c_str(), b.c_str()) < 0; });

    wstring sl_path = startPath;
    for (auto& c : sl_path) c = towlower(c);
    for (int i = 0; i < (int)iv.files.size(); i++) {
        wstring fl = iv.files[i];
        for (auto& c : fl) c = towlower(c);
        if (fl == sl_path) { iv.index = i; break; }
    }
}

// ============================================================
// BACKGROUND CACHE THREAD
// ============================================================
struct CacheJob { wstring path; int slot; };  // slot: 0=prev, 1=next

static DWORD WINAPI CacheThread(LPVOID param) {
    CacheJob* job = (CacheJob*)param;
    Image* loaded = Image::FromFile(job->path.c_str());
    int slot = job->slot;
    wstring path = job->path;
    delete job;

    EnterCriticalSection(&iv.cacheCS);
    // Only store if still valid (user hasn't navigated away again)
    int expectedIdx = (slot == CACHE_PREV)
        ? (iv.index - 1 + (int)iv.files.size()) % (int)iv.files.size()
        : (iv.index + 1) % (int)iv.files.size();

    bool valid = !iv.files.empty();
    if (valid) {
        wstring expected = iv.files[expectedIdx];
        for (auto& c : expected) c = towlower(c);
        wstring p2 = path;
        for (auto& c : p2) c = towlower(c);
        valid = (p2 == expected);
    }

    if (valid) {
        if (iv.cache[slot]) { delete iv.cache[slot]; }
        iv.cache[slot]    = loaded;
        iv.cacheIdx[slot] = expectedIdx;
    } else {
        delete loaded;
    }
    iv.cacheThread[slot] = NULL;
    LeaveCriticalSection(&iv.cacheCS);
    return 0;
}

static void StartCacheJobs() {
    if (iv.files.size() <= 1) return;
    for (int slot = 0; slot < 2; slot++) {
        int targetIdx = (slot == CACHE_PREV)
            ? (iv.index - 1 + (int)iv.files.size()) % (int)iv.files.size()
            : (iv.index + 1) % (int)iv.files.size();

        EnterCriticalSection(&iv.cacheCS);
        bool alreadyCached = (iv.cacheIdx[slot] == targetIdx && iv.cache[slot] != nullptr);
        bool threadRunning = (iv.cacheThread[slot] != NULL);
        LeaveCriticalSection(&iv.cacheCS);

        if (!alreadyCached && !threadRunning) {
            CacheJob* job = new CacheJob{ iv.files[targetIdx], slot };
            HANDLE h = CreateThread(NULL, 0, CacheThread, job, 0, NULL);
            EnterCriticalSection(&iv.cacheCS);
            iv.cacheThread[slot] = h;
            LeaveCriticalSection(&iv.cacheCS);
        }
    }
}

// ============================================================
// ZOOM & FIT  (operates on TARGET values; animation handles actual)
// ============================================================
static void StartAnimTimer() {
    SetTimer(iv.hWnd, ZOOM_TIMER_ID, ZOOM_TIMER_MS, NULL);
}

static void SetZoomTarget(float newZoom, float cursorAreaX, float cursorAreaY) {
    // cursorAreaX/Y: position in image-area coords (0,0 = top-left of image area)
    // We want the image point under the cursor to stay fixed.
    RECT ia; GetImgArea(ia);
    float areaW = (float)(ia.right  - ia.left);
    float areaH = (float)(ia.bottom - ia.top);

    // Centre of image in area coords
    float cx = areaW / 2.0f + iv.panXTarget;
    float cy = areaH / 2.0f + iv.panYTarget;

    // Image-space point under cursor
    float ratio = newZoom / iv.zoomTarget;
    // After zoom, new cx/cy must shift so the cursor point stays:
    // cursorAreaX = newCx - (cursorAreaX - cx) * ratio  <- wrong
    // Fixed: newPanX = cursorAreaX - ratio*(cursorAreaX - areaW/2 - iv.panXTarget) - areaW/2
    float newPanX = cursorAreaX - ratio * (cursorAreaX - areaW / 2.0f - iv.panXTarget) - areaW / 2.0f;
    float newPanY = cursorAreaY - ratio * (cursorAreaY - areaH / 2.0f - iv.panYTarget) - areaH / 2.0f;

    iv.zoomTarget  = max(ZOOM_MIN, min(ZOOM_MAX, newZoom));
    iv.panXTarget  = newPanX;
    iv.panYTarget  = newPanY;
    StartAnimTimer();
}

static void FitToWindow() {
    if (!iv.img || !iv.hWnd) return;
    RECT ia; GetImgArea(ia);
    float areaW = (float)(ia.right - ia.left);
    float areaH = (float)(ia.bottom - ia.top);
    float iw = (float)iv.img->GetWidth();
    float ih = (float)iv.img->GetHeight();
    if (iv.rotation == 90 || iv.rotation == 270) swap(iw, ih);
    if (iw < 1) iw = 1; if (ih < 1) ih = 1;
    float fit = min(areaW / iw, areaH / ih);
    if (fit > 1.0f) fit = 1.0f;
    iv.zoomTarget = fit; iv.panXTarget = 0; iv.panYTarget = 0;
    StartAnimTimer();
}

static void ActualSize() {
    iv.zoomTarget = 1.0f; iv.panXTarget = 0; iv.panYTarget = 0;
    StartAnimTimer();
}

// Snap actual values immediately (used on load)
static void SnapToTarget() {
    iv.zoom = iv.zoomTarget;
    iv.panX = iv.panXTarget;
    iv.panY = iv.panYTarget;
}

// ============================================================
// LOAD IMAGE (main + cache)
// ============================================================
static void LoadCurrentImage() {
    if (iv.files.empty()) return;

    wstring newPath = iv.files[iv.index];
    if (newPath == iv.path && iv.img) return;  // already loaded

    // Check cache first
    Image* cached = nullptr;
    int slot = -1;
    EnterCriticalSection(&iv.cacheCS);
    for (int s = 0; s < 2; s++) {
        if (iv.cacheIdx[s] == iv.index && iv.cache[s]) {
            cached = iv.cache[s];
            iv.cache[s]    = nullptr;
            iv.cacheIdx[s] = -1;
            slot = s;
            break;
        }
    }
    LeaveCriticalSection(&iv.cacheCS);

    if (iv.img) { delete iv.img; iv.img = nullptr; }

    if (cached) {
        iv.img = cached;
    } else {
        iv.img = Image::FromFile(newPath.c_str());
    }

    iv.path     = newPath;
    iv.rotation = 0;

    // Fit on load
    if (iv.img && iv.img->GetLastStatus() == Ok && iv.hWnd) {
        RECT ia; GetImgArea(ia);
        float areaW = (float)(ia.right - ia.left);
        float areaH = (float)(ia.bottom - ia.top);
        float iw = (float)iv.img->GetWidth();
        float ih = (float)iv.img->GetHeight();
        if (iw < 1) iw = 1; if (ih < 1) ih = 1;
        float fit = min(areaW / iw, areaH / ih);
        if (fit > 1.0f) fit = 1.0f;
        iv.zoomTarget = fit;
    } else {
        iv.zoomTarget = 1.0f;
    }
    iv.panXTarget = 0; iv.panYTarget = 0;
    SnapToTarget();

    if (iv.hWnd) {
        wstring title = L"RasFocus+ Photo \u2014 " + Filename(iv.path);
        SetWindowTextW(iv.hWnd, title.c_str());
        InvalidateRect(iv.hWnd, NULL, FALSE);
    }

    // Kick off background cache for neighbours
    StartCacheJobs();
}

// ============================================================
// FULL SCREEN TOGGLE
// ============================================================
static void ToggleFullScreen() {
    if (!iv.isFullScreen) {
        // Save current state
        iv.savedStyle   = GetWindowLongW(iv.hWnd, GWL_STYLE);
        iv.savedExStyle = GetWindowLongW(iv.hWnd, GWL_EXSTYLE);
        GetWindowRect(iv.hWnd, &iv.savedWndRect);

        // Remove borders/title
        DWORD newStyle = (iv.savedStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU));
        SetWindowLongW(iv.hWnd, GWL_STYLE,   newStyle);
        SetWindowLongW(iv.hWnd, GWL_EXSTYLE, iv.savedExStyle & ~WS_EX_APPWINDOW);

        // Get monitor bounds
        HMONITOR hMon = MonitorFromWindow(iv.hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(hMon, &mi);
        RECT& mr = mi.rcMonitor;

        SetWindowPos(iv.hWnd, HWND_TOP,
            mr.left, mr.top, mr.right - mr.left, mr.bottom - mr.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        iv.isFullScreen = true;
    } else {
        // Restore
        SetWindowLongW(iv.hWnd, GWL_STYLE,   iv.savedStyle);
        SetWindowLongW(iv.hWnd, GWL_EXSTYLE, iv.savedExStyle);
        SetWindowPos(iv.hWnd, NULL,
            iv.savedWndRect.left, iv.savedWndRect.top,
            iv.savedWndRect.right  - iv.savedWndRect.left,
            iv.savedWndRect.bottom - iv.savedWndRect.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOZORDER);
        iv.isFullScreen = false;
    }
    // Re-fit after size change
    FitToWindow();
    SnapToTarget();
    InvalidateRect(iv.hWnd, NULL, FALSE);
}

// ============================================================
// TOOLBAR LAYOUT
// ============================================================
struct BtnDef { const wchar_t* icon; const wchar_t* tip; };
// Buttons: 0=prev 1=zoomOut 2=fit 3=zoomIn 4=rotate 5=fullscreen 6=next 7=close
static BtnDef s_btns[] = {
    { L"\xE76B", L"Prev (\u2190)" },
    { L"\xE71F", L"Zoom Out (-)" },
    { L"\xE9A6", L"Fit (F)" },
    { L"\xE8A3", L"Zoom In (+)" },
    { L"\xE7AD", L"Rotate (R)" },
    { L"\xE740", L"Full Screen (F11)" },  // 5 = fullscreen
    { L"\xE76C", L"Next (\u2192)" },       // 6 = next
    { L"\xE711", L"Close (Esc)" },         // 7 = close
};
static const int BTN_COUNT = 8;
static const float BTN_W = 44.0f;
static const float BTN_H = 36.0f;

static void GetBtnRect(int i, float winW, float& bx, float& by, float& bw, float& bh) {
    bw = BTN_W; bh = BTN_H;
    by = (TOOLBAR_H - BTN_H) / 2.0f;
    if (i <= 5) {
        bx = 8.0f + i * (BTN_W + 6.0f);
    } else if (i == 6) {   // next
        bx = winW - 2 * (BTN_W + 8.0f);
    } else {               // close
        bx = winW - (BTN_W + 8.0f);
    }
}

// ============================================================
// DRAWING
// ============================================================
static void DrawCheckerboard(Graphics& g, float x, float y, float w, float h) {
    int sz = 10;
    g.SetClip(RectF(x, y, w, h));
    for (int r = 0; r < (int)(h / sz) + 1; r++) {
        for (int c = 0; c < (int)(w / sz) + 1; c++) {
            bool odd = (r + c) % 2;
            SolidBrush br(odd ? Color(255,200,200,200) : Color(255,240,240,240));
            float tx = x + c*sz, ty = y + r*sz;
            float tw = min((float)sz, x+w-tx);
            float th = min((float)sz, y+h-ty);
            if (tw>0 && th>0) g.FillRectangle(&br, tx, ty, tw, th);
        }
    }
    g.ResetClip();
}

static void OnPaint(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

    Graphics g(memDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    FontFamily ffUI(L"Segoe UI");
    FontFamily ffIco(L"Segoe MDL2 Assets");
    Font fSmall(&ffUI, 11, FontStyleRegular, UnitPixel);
    Font fBtnIco(&ffIco, 16, FontStyleRegular, UnitPixel);
    Font fStatus(&ffUI, 11, FontStyleRegular, UnitPixel);
    StringFormat sfC;
    sfC.SetAlignment(StringAlignmentCenter);
    sfC.SetLineAlignment(StringAlignmentCenter);

    // Hide toolbar in fullscreen (unless mouse near top)
    bool showToolbar = !iv.isFullScreen;
    int tbH = showToolbar ? TOOLBAR_H : 0;
    int sbH = showToolbar ? STATUSBAR_H : 0;

    // ── Toolbar ──────────────────────────────────────────────
    if (showToolbar) {
        SolidBrush bTB(Color(255, 28, 28, 32));
        g.FillRectangle(&bTB, 0.0f, 0.0f, (float)W, (float)TOOLBAR_H);
        Pen pLine(Color(255, 60, 60, 70), 1.0f);
        g.DrawLine(&pLine, 0.0f, (float)TOOLBAR_H, (float)W, (float)TOOLBAR_H);

        for (int i = 0; i < BTN_COUNT; i++) {
            float bx, by, bw, bh;
            GetBtnRect(i, (float)W, bx, by, bw, bh);
            bool hov = (iv.hovBtn == i);
            bool dis = ((i == 0 && iv.files.size() <= 1) ||
                        (i == 6 && iv.files.size() <= 1));
            bool active = (i == 5 && iv.isFullScreen); // fullscreen active

            Color cBg   = (active||hov) ? Color(255, 70, 140, 200) : Color(0,0,0,0);
            Color cIcon = dis  ? Color(255, 80, 80, 90)
                               : ((hov||active) ? Color(255,255,255,255)
                                                : Color(255,180,185,195));
            if (hov || active) {
                SolidBrush bh2(cBg);
                g.FillRectangle(&bh2, bx, by, bw, bh);
            }
            SolidBrush bIco(cIcon);
            g.DrawString(s_btns[i].icon, -1, &fBtnIco, RectF(bx,by,bw,bh), &sfC, &bIco);
        }

        // Zoom % label in centre
        if (iv.img) {
            wchar_t zb[32];
            swprintf(zb, 32, L"%.0f%%", iv.zoom * 100.0f);
            SolidBrush bZ(Color(255, 160, 165, 175));
            g.DrawString(zb, -1, &fSmall,
                RectF((float)W/2.0f - 30.0f, 0, 60.0f, (float)TOOLBAR_H), &sfC, &bZ);
        }
    }

    // ── Image area ───────────────────────────────────────────
    float imgAreaY = (float)tbH;
    float imgAreaH = (float)(H - tbH - sbH);
    float imgAreaW = (float)W;
    float cx = imgAreaW / 2.0f + iv.panX;
    float cy = imgAreaH / 2.0f + iv.panY;

    SolidBrush bBg(Color(255, 18, 18, 22));
    g.FillRectangle(&bBg, 0.0f, imgAreaY, imgAreaW, imgAreaH);

    if (iv.img && iv.img->GetLastStatus() == Ok) {
        float iw = (float)iv.img->GetWidth();
        float ih = (float)iv.img->GetHeight();
        float dw, dh;
        if (iv.rotation == 90 || iv.rotation == 270) { dw = ih*iv.zoom; dh = iw*iv.zoom; }
        else                                          { dw = iw*iv.zoom; dh = ih*iv.zoom; }
        float dx = cx - dw / 2.0f;
        float dy = imgAreaY + cy - dh / 2.0f;

        DrawCheckerboard(g, dx, dy, dw, dh);

        if (iv.rotation != 0) {
            Matrix m;
            m.RotateAt((float)iv.rotation, PointF(dx + dw/2.0f, dy + dh/2.0f));
            g.SetTransform(&m);
        }
        if (iv.rotation == 90 || iv.rotation == 270)
            g.DrawImage(iv.img, RectF(dx,dy,dw,dh), 0, 0, iw, ih, UnitPixel);
        else
            g.DrawImage(iv.img, RectF(dx, dy, dw, dh));
        g.ResetTransform();
    } else {
        SolidBrush bErr(Color(255, 200, 80, 80));
        SolidBrush bMsg(Color(255, 140, 140, 150));
        FontFamily ffIco2(L"Segoe MDL2 Assets");
        Font fBigIco(&ffIco2, 48, FontStyleRegular, UnitPixel);
        g.DrawString(L"\xE7C5", -1, &fBigIco,
            RectF(0, imgAreaY, imgAreaW, imgAreaH/2.0f), &sfC, &bErr);
        g.DrawString(L"Cannot load image", -1, &fSmall,
            RectF(0, imgAreaY + imgAreaH/2.0f, imgAreaW, imgAreaH/2.0f), &sfC, &bMsg);
    }

    // ── Status bar ───────────────────────────────────────────
    if (showToolbar) {
        float sbY = (float)(H - STATUSBAR_H);
        SolidBrush bSbBg(Color(255, 22, 22, 28));
        g.FillRectangle(&bSbBg, 0.0f, sbY, (float)W, (float)STATUSBAR_H);
        Pen pSbLine(Color(255, 50, 50, 60), 1.0f);
        g.DrawLine(&pSbLine, 0.0f, sbY, (float)W, sbY);

        if (iv.img && iv.img->GetLastStatus() == Ok) {
            wchar_t statusBuf[256];
            swprintf(statusBuf, 256, L"  %s   |   %d \xD7 %d px   |   %.0f%%   |   %d / %d",
                Filename(iv.path).c_str(),
                iv.img->GetWidth(), iv.img->GetHeight(),
                iv.zoom * 100.0f,
                iv.index + 1, (int)iv.files.size());
            SolidBrush bSbTxt(Color(255, 150, 155, 165));
            StringFormat sfL; sfL.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(statusBuf, -1, &fStatus,
                RectF(0, sbY, (float)W, (float)STATUSBAR_H), &sfL, &bSbTxt);
        }
    }

    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    EndPaint(hWnd, &ps);
}

// ============================================================
// ANIMATION TICK
// ============================================================
static bool AnimTick() {
    // Returns true if still animating
    float dz = iv.zoomTarget - iv.zoom;
    float dx = iv.panXTarget  - iv.panX;
    float dy = iv.panYTarget  - iv.panY;

    bool moving = (fabsf(dz) > 0.0002f || fabsf(dx) > 0.2f || fabsf(dy) > 0.2f);
    if (!moving) {
        iv.zoom = iv.zoomTarget;
        iv.panX = iv.panXTarget;
        iv.panY = iv.panYTarget;
        return false;
    }
    float spd = ZOOM_ANIM_SPEED;
    iv.zoom += dz * spd;
    iv.panX += dx * spd;
    iv.panY += dy * spd;
    return true;
}

// ============================================================
// WINDOW PROC
// ============================================================
static LRESULT CALLBACK IV_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_PAINT:
        OnPaint(hWnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        if (wParam == ZOOM_TIMER_ID) {
            bool still = AnimTick();
            InvalidateRect(hWnd, NULL, FALSE);
            if (!still) KillTimer(hWnd, ZOOM_TIMER_ID);
        }
        return 0;

    case WM_KEYDOWN: {
        switch (wParam) {
        case VK_ESCAPE:
            if (iv.isFullScreen) ToggleFullScreen();
            else DestroyWindow(hWnd);
            break;
        case VK_F11:
            ToggleFullScreen();
            break;
        case VK_LEFT: case 'A':
            if (!iv.files.empty()) {
                iv.index = (iv.index - 1 + (int)iv.files.size()) % (int)iv.files.size();
                LoadCurrentImage();
            }
            break;
        case VK_RIGHT: case 'D':
            if (!iv.files.empty()) {
                iv.index = (iv.index + 1) % (int)iv.files.size();
                LoadCurrentImage();
            }
            break;
        case 'F':
            FitToWindow();
            break;
        case '1':
            ActualSize();
            break;
        case 'R':
            iv.rotation = (iv.rotation + 90) % 360;
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        case VK_ADD: case VK_OEM_PLUS: {
            RECT ia; GetImgArea(ia);
            float cx = (float)(ia.right - ia.left) / 2.0f;
            float cy = (float)(ia.bottom - ia.top) / 2.0f;
            SetZoomTarget(iv.zoomTarget * 1.20f, cx, cy);
            break;
        }
        case VK_SUBTRACT: case VK_OEM_MINUS: {
            RECT ia; GetImgArea(ia);
            float cx = (float)(ia.right - ia.left) / 2.0f;
            float cy = (float)(ia.bottom - ia.top) / 2.0f;
            SetZoomTarget(iv.zoomTarget / 1.20f, cx, cy);
            break;
        }
        }
        return 0;
    }

    // Mouse wheel: always zoom (no Ctrl required), anchored to cursor
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);

        // Convert to image-area-local coords
        float areaX = (float)pt.x;
        float areaY = (float)(pt.y - TOOLBAR_H);

        float factor = (delta > 0) ? 1.15f : (1.0f / 1.15f);
        SetZoomTarget(iv.zoomTarget * factor, areaX, areaY);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hWnd, &rc);

        if (my < TOOLBAR_H && !iv.isFullScreen) {
            for (int i = 0; i < BTN_COUNT; i++) {
                float bx, by, bw, bh;
                GetBtnRect(i, (float)rc.right, bx, by, bw, bh);
                if (mx >= bx && mx <= bx+bw && my >= by && my <= by+bh) {
                    switch (i) {
                    case 0: // prev
                        if (!iv.files.empty()) {
                            iv.index = (iv.index - 1 + (int)iv.files.size()) % (int)iv.files.size();
                            LoadCurrentImage();
                        }
                        break;
                    case 1: { RECT ia; GetImgArea(ia);
                        float cx=(float)(ia.right-ia.left)/2,cy=(float)(ia.bottom-ia.top)/2;
                        SetZoomTarget(iv.zoomTarget/1.20f, cx, cy); break; }
                    case 2: FitToWindow(); break;
                    case 3: { RECT ia; GetImgArea(ia);
                        float cx=(float)(ia.right-ia.left)/2,cy=(float)(ia.bottom-ia.top)/2;
                        SetZoomTarget(iv.zoomTarget*1.20f, cx, cy); break; }
                    case 4:
                        iv.rotation = (iv.rotation + 90) % 360;
                        InvalidateRect(hWnd, NULL, FALSE);
                        break;
                    case 5: ToggleFullScreen(); break;
                    case 6: // next
                        if (!iv.files.empty()) {
                            iv.index = (iv.index + 1) % (int)iv.files.size();
                            LoadCurrentImage();
                        }
                        break;
                    case 7: DestroyWindow(hWnd); break;
                    }
                    return 0;
                }
            }
            return 0;
        }

        // Image drag
        iv.dragging   = true;
        iv.dragStartX = mx;
        iv.dragStartY = my;
        iv.panXAtDrag = iv.panXTarget;
        iv.panYAtDrag = iv.panYTarget;
        SetCapture(hWnd);
        return 0;
    }

    case WM_LBUTTONUP:
        if (iv.dragging) { iv.dragging = false; ReleaseCapture(); }
        return 0;

    case WM_LBUTTONDBLCLK: {
        int my = GET_Y_LPARAM(lParam);
        int tbH = iv.isFullScreen ? 0 : TOOLBAR_H;
        if (my >= tbH) {
            if (fabsf(iv.zoomTarget - 1.0f) < 0.02f) FitToWindow();
            else ActualSize();
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        int tbH2 = iv.isFullScreen ? 0 : TOOLBAR_H;

        if (iv.dragging) {
            iv.panXTarget = iv.panXAtDrag + (mx - iv.dragStartX);
            iv.panYTarget = iv.panYAtDrag + (my - iv.dragStartY);
            iv.panX = iv.panXTarget;  // instant pan (no lag on drag)
            iv.panY = iv.panYTarget;
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        if (my < tbH2 && !iv.isFullScreen) {
            RECT rc2; GetClientRect(hWnd, &rc2);
            int oldHov = iv.hovBtn; iv.hovBtn = -1;
            for (int i = 0; i < BTN_COUNT; i++) {
                float bx, by, bw, bh;
                GetBtnRect(i, (float)rc2.right, bx, by, bw, bh);
                if (mx >= bx && mx <= bx+bw && my >= by && my <= by+bh) {
                    iv.hovBtn = i; break;
                }
            }
            if (iv.hovBtn != oldHov) InvalidateRect(hWnd, NULL, FALSE);
        } else {
            if (iv.hovBtn != -1) { iv.hovBtn = -1; InvalidateRect(hWnd, NULL, FALSE); }
            SetCursor(iv.zoom > 1.0f
                ? (iv.dragging ? LoadCursor(NULL, IDC_SIZEALL) : LoadCursor(NULL, IDC_HAND))
                : LoadCursor(NULL, IDC_ARROW));
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (iv.hovBtn != -1) { iv.hovBtn = -1; InvalidateRect(hWnd, NULL, FALSE); }
        return 0;

    case WM_SIZE:
        FitToWindow();
        SnapToTarget();
        return 0;

    case WM_DESTROY:
        // Cancel cache threads (best-effort: just wait a bit)
        for (int s = 0; s < 2; s++) {
            EnterCriticalSection(&iv.cacheCS);
            HANDLE t = iv.cacheThread[s]; iv.cacheThread[s] = NULL;
            LeaveCriticalSection(&iv.cacheCS);
            if (t) WaitForSingleObject(t, 200);
        }
        for (int s = 0; s < 2; s++) if (iv.cache[s]) { delete iv.cache[s]; iv.cache[s] = nullptr; }
        if (iv.img) { delete iv.img; iv.img = nullptr; }
        DeleteCriticalSection(&iv.cacheCS);
        iv.hWnd = nullptr;
        GdiplusShutdown(iv.gdipToken); iv.gdipToken = 0;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============================================================
// VIEWER THREAD
// ============================================================
struct IVParams { wstring path; };

static DWORD WINAPI IV_Thread(LPVOID param) {
    IVParams* p = (IVParams*)param;
    wstring startPath = p->path;
    delete p;

    InitializeCriticalSection(&iv.cacheCS);
    iv.cache[0] = iv.cache[1] = nullptr;
    iv.cacheIdx[0] = iv.cacheIdx[1] = -1;
    iv.cacheThread[0] = iv.cacheThread[1] = NULL;
    iv.isFullScreen = false;

    GdiplusStartupInput gsi;
    GdiplusStartup(&iv.gdipToken, &gsi, NULL);

    HINSTANCE hInst = GetModuleHandleW(NULL);
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = IV_WndProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = IV_CLASS;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = CreateSolidBrush(RGB(18,18,22));
        wc.hIcon         = LoadIconW(hInst, L"IDI_APP_ICON");
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    BuildFileList(startPath);

    HWND hWnd = CreateWindowExW(
        WS_EX_APPWINDOW, IV_CLASS,
        L"RasFocus+ Photo Viewer",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        NULL, NULL, hInst, NULL);
    if (!hWnd) {
        DeleteCriticalSection(&iv.cacheCS);
        GdiplusShutdown(iv.gdipToken); iv.gdipToken = 0;
        return 1;
    }
    iv.hWnd = hWnd;
    LoadCurrentImage();

    ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    SetForegroundWindow(hWnd);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

// ============================================================
// PUBLIC API
// ============================================================
void LaunchImageViewer(const wstring& imagePath) {
    IVParams* p = new IVParams{ imagePath };
    CreateThread(NULL, 0, IV_Thread, p, 0, NULL);
}
