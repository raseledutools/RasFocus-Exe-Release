// image_viewer.cpp
// RasFocus+ Native GDI+ Image Viewer
// Opens as a standalone full-screen window when an image file is launched.
// Features:
//   - Smooth Zoom  : Ctrl+Wheel  OR  +/- keys  OR toolbar buttons
//   - Pan          : Left-drag (when zoomed)
//   - Fit/Actual   : F key OR double-click
//   - Prev / Next  : Left/Right arrow keys  OR  A/D  OR  on-screen buttons
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
static const wchar_t* IV_CLASS = L"RasFocusImageViewer";
static const int      TOOLBAR_H = 48;   // px (unscaled logical)
static const int      STATUSBAR_H = 22;
static const float    ZOOM_STEP   = 0.15f;  // 15% per step
static const float    ZOOM_MIN    = 0.05f;
static const float    ZOOM_MAX    = 32.0f;

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
// VIEWER STATE  (one global instance — only one viewer window)
// ============================================================
struct IVState {
    // image list in same folder
    vector<wstring> files;   // absolute paths
    int             index = 0;

    // current image
    Image*  img    = nullptr;
    wstring path;
    int     rotation = 0;   // 0/90/180/270

    // view transform
    float   zoom    = 1.0f;
    float   panX    = 0.0f;   // offset of image centre from window centre
    float   panY    = 0.0f;

    // drag state
    bool    dragging = false;
    int     dragStartX = 0, dragStartY = 0;
    float   panXAtDrag = 0, panYAtDrag = 0;

    // hover state for toolbar buttons
    int     hovBtn  = -1;   // -1=none, 0=prev,1=zoomOut,2=fit,3=zoomIn,4=rotate,5=next,6=close

    // window handle
    HWND    hWnd    = nullptr;

    // GDI+ token
    ULONG_PTR gdipToken = 0;
};
static IVState iv;

// ============================================================
// HELPERS
// ============================================================
static wstring ExtLower(const wstring& path) {
    size_t dot = path.rfind(L'.');
    if (dot == wstring::npos) return L"";
    wstring e = path.substr(dot + 1);
    for (auto& c : e) c = towlower(c);
    return e;
}

static wstring Filename(const wstring& path) {
    size_t sl = path.find_last_of(L"\\/");
    return (sl == wstring::npos) ? path : path.substr(sl + 1);
}

static void BuildFileList(const wstring& startPath) {
    iv.files.clear();
    iv.index = 0;

    size_t sl = startPath.find_last_of(L"\\/");
    if (sl == wstring::npos) { iv.files.push_back(startPath); return; }

    wstring dir = startPath.substr(0, sl + 1);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) { iv.files.push_back(startPath); return; }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wstring name = fd.cFileName;
        wstring ext  = ExtLower(name);
        if (IsViewableExt(ext)) iv.files.push_back(dir + name);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    sort(iv.files.begin(), iv.files.end(), [](const wstring& a, const wstring& b){
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });

    // Find starting index
    wstring startLower = startPath;
    for (auto& c : startLower) c = towlower(c);
    for (int i = 0; i < (int)iv.files.size(); i++) {
        wstring fl = iv.files[i];
        for (auto& c : fl) c = towlower(c);
        if (fl == startLower) { iv.index = i; break; }
    }
}

static void LoadCurrentImage() {
    if (iv.img) { delete iv.img; iv.img = nullptr; }
    if (iv.files.empty()) return;

    iv.path     = iv.files[iv.index];
    iv.rotation = 0;
    iv.img      = Image::FromFile(iv.path.c_str());

    // Reset zoom to fit
    if (iv.img && iv.img->GetLastStatus() == Ok && iv.hWnd) {
        RECT rc; GetClientRect(iv.hWnd, &rc);
        int cw = rc.right - rc.left;
        int ch = rc.bottom - rc.top - TOOLBAR_H - STATUSBAR_H;
        if (ch < 1) ch = 1;
        float iw = (float)iv.img->GetWidth();
        float ih = (float)iv.img->GetHeight();
        if (iw < 1) iw = 1; if (ih < 1) ih = 1;
        iv.zoom = min((float)cw / iw, (float)ch / ih);
        if (iv.zoom > 1.0f) iv.zoom = 1.0f; // don't upscale on initial load
    } else {
        iv.zoom = 1.0f;
    }
    iv.panX = 0; iv.panY = 0;

    if (iv.hWnd) {
        // Update title
        wstring title = L"RasFocus+ Photo — " + Filename(iv.path);
        SetWindowTextW(iv.hWnd, title.c_str());
        InvalidateRect(iv.hWnd, NULL, FALSE);
    }
}

static void StepZoom(float delta, int pivotX = -1, int pivotY = -1) {
    if (!iv.hWnd) return;
    RECT rc; GetClientRect(iv.hWnd, &rc);
    int cw = rc.right;
    int ch = rc.bottom - TOOLBAR_H - STATUSBAR_H;

    // pivot defaults to centre
    float px = (pivotX >= 0) ? (float)pivotX       : cw / 2.0f;
    float py = (pivotY >= 0) ? (float)(pivotY - TOOLBAR_H) : ch / 2.0f;

    float oldZoom = iv.zoom;
    float newZoom = oldZoom * (1.0f + delta);
    newZoom = max(ZOOM_MIN, min(ZOOM_MAX, newZoom));
    if (newZoom == oldZoom) return;

    // adjust pan so the pixel under cursor stays fixed
    float ratio = newZoom / oldZoom;
    iv.panX = px - ratio * (px - iv.panX);
    iv.panY = py - ratio * (py - iv.panY);
    iv.zoom = newZoom;
    InvalidateRect(iv.hWnd, NULL, FALSE);
}

static void FitToWindow() {
    if (!iv.img || !iv.hWnd) return;
    RECT rc; GetClientRect(iv.hWnd, &rc);
    int cw = rc.right;
    int ch = rc.bottom - TOOLBAR_H - STATUSBAR_H;
    float iw = (float)iv.img->GetWidth();
    float ih = (float)iv.img->GetHeight();
    if (iv.rotation == 90 || iv.rotation == 270) swap(iw, ih);
    if (iw < 1) iw = 1; if (ih < 1) ih = 1;
    iv.zoom = min((float)cw / iw, (float)ch / ih);
    if (iv.zoom > 1.0f) iv.zoom = 1.0f;
    iv.panX = 0; iv.panY = 0;
    InvalidateRect(iv.hWnd, NULL, FALSE);
}

static void ActualSize() {
    iv.zoom = 1.0f; iv.panX = 0; iv.panY = 0;
    if (iv.hWnd) InvalidateRect(iv.hWnd, NULL, FALSE);
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
            SolidBrush br(odd ? Color(255, 200, 200, 200) : Color(255, 240, 240, 240));
            float tx = x + c * sz, ty = y + r * sz;
            float tw = min((float)sz, x + w - tx);
            float th = min((float)sz, y + h - ty);
            if (tw > 0 && th > 0) g.FillRectangle(&br, tx, ty, tw, th);
        }
    }
    g.ResetClip();
}

// Toolbar button definitions  [icon, label]
struct BtnDef { const wchar_t* icon; const wchar_t* tip; };
static BtnDef s_btns[] = {
    { L"\xE76B", L"Prev (←)" },      // 0 prev
    { L"\xE71F", L"Zoom Out (-)" },   // 1 zoom-out
    { L"\xE9A6", L"Fit (F)" },        // 2 fit
    { L"\xE8A3", L"Zoom In (+)" },    // 3 zoom-in
    { L"\xE7AD", L"Rotate (R)" },     // 4 rotate
    { L"\xE76C", L"Next (→)" },       // 5 next
    { L"\xE711", L"Close (Esc)" },    // 6 close
};
static const int BTN_COUNT = 7;
static const float BTN_W   = 44.0f;
static const float BTN_H   = 36.0f;

static void GetBtnRect(int i, float winW, float& bx, float& by, float& bw, float& bh) {
    // Layout: [Prev] [ZoomOut] [Fit] [ZoomIn] [Rotate] ……  [Next] [Close]
    // Group them: left cluster (0-4), right cluster (5-6)
    bw = BTN_W; bh = BTN_H;
    by = (TOOLBAR_H - BTN_H) / 2.0f;

    if (i <= 4) {
        bx = 8.0f + i * (BTN_W + 6.0f);
    } else if (i == 5) {
        bx = winW - 2 * (BTN_W + 8.0f);
    } else { // 6 = close
        bx = winW - (BTN_W + 8.0f);
    }
}

static void OnPaint(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right;
    int H = rc.bottom;

    // double-buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

    Graphics g(memDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);

    FontFamily ffUI(L"Segoe UI");
    FontFamily ffIco(L"Segoe MDL2 Assets");
    Font fSmall(&ffUI, 11, FontStyleRegular, UnitPixel);
    Font fBtnIco(&ffIco, 16, FontStyleRegular, UnitPixel);
    Font fStatus(&ffUI, 11, FontStyleRegular, UnitPixel);
    StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);

    // ── Toolbar background ────────────────────────────────────
    SolidBrush bToolbar(Color(255, 28, 28, 32));
    g.FillRectangle(&bToolbar, 0, 0, (float)W, (float)TOOLBAR_H);
    // separator line
    Pen pLine(Color(255, 60, 60, 70), 1.0f);
    g.DrawLine(&pLine, 0, TOOLBAR_H, W, TOOLBAR_H);

    // ── Toolbar buttons ───────────────────────────────────────
    for (int i = 0; i < BTN_COUNT; i++) {
        float bx, by, bw, bh;
        GetBtnRect(i, (float)W, bx, by, bw, bh);

        bool hov = (iv.hovBtn == i);
        bool dis = ((i == 0 && (iv.files.size() <= 1)) ||
                    (i == 5 && (iv.files.size() <= 1)));

        Color cBg   = hov ? Color(255, 70, 140, 200)  : Color(0, 0, 0, 0);
        Color cIcon = dis ? Color(255, 80, 80, 90)
                          : (hov ? Color(255, 255, 255, 255) : Color(255, 180, 185, 195));

        if (hov) {
            SolidBrush bh2(cBg);
            // rounded rect via FillRectangle (GDI+ no built-in rounding at this scale)
            g.FillRectangle(&bh2, bx, by, bw, bh);
        }
        SolidBrush bIco(cIcon);
        g.DrawString(s_btns[i].icon, -1, &fBtnIco,
            RectF(bx, by, bw, bh), &sfC, &bIco);
    }

    // Zoom % label in toolbar centre
    if (iv.img) {
        wchar_t zoomBuf[32];
        swprintf(zoomBuf, 32, L"%.0f%%", iv.zoom * 100.0f);
        SolidBrush bZoomTxt(Color(255, 160, 165, 175));
        g.DrawString(zoomBuf, -1, &fSmall,
            RectF((float)W / 2.0f - 30.0f, 0, 60.0f, (float)TOOLBAR_H),
            &sfC, &bZoomTxt);
    }

    // ── Image area ────────────────────────────────────────────
    float imgAreaY = (float)TOOLBAR_H;
    float imgAreaH = (float)(H - TOOLBAR_H - STATUSBAR_H);
    float imgAreaW = (float)W;
    float cx = imgAreaW / 2.0f + iv.panX;
    float cy = imgAreaH / 2.0f + iv.panY;

    // Background
    SolidBrush bBg(Color(255, 18, 18, 22));
    g.FillRectangle(&bBg, 0.0f, imgAreaY, imgAreaW, imgAreaH);

    if (iv.img && iv.img->GetLastStatus() == Ok) {
        float iw = (float)iv.img->GetWidth();
        float ih = (float)iv.img->GetHeight();

        float dw, dh;
        if (iv.rotation == 90 || iv.rotation == 270) {
            dw = ih * iv.zoom;
            dh = iw * iv.zoom;
        } else {
            dw = iw * iv.zoom;
            dh = ih * iv.zoom;
        }

        float dx = cx - dw / 2.0f;
        float dy = imgAreaY + cy - dh / 2.0f;

        // Checkerboard for transparency
        DrawCheckerboard(g, dx, dy, dw, dh);

        // Apply rotation around image centre
        if (iv.rotation != 0) {
            Matrix m;
            m.RotateAt((float)iv.rotation, PointF(dx + dw / 2.0f, dy + dh / 2.0f));
            g.SetTransform(&m);
        }

        if (iv.rotation == 90 || iv.rotation == 270) {
            // When rotated 90/270, we draw the image in its natural orientation
            // but the coordinate math has to account for the swap
            g.DrawImage(iv.img,
                RectF(dx, dy, dw, dh),
                0, 0, iw, ih, UnitPixel);
        } else {
            g.DrawImage(iv.img, RectF(dx, dy, dw, dh));
        }

        g.ResetTransform();
    } else {
        // No image / error
        SolidBrush bErr(Color(255, 200, 80, 80));
        SolidBrush bMsg(Color(255, 140, 140, 150));
        FontFamily ffIco2(L"Segoe MDL2 Assets");
        Font fBigIco(&ffIco2, 48, FontStyleRegular, UnitPixel);
        g.DrawString(L"\xE7C5", -1, &fBigIco,
            RectF(0, imgAreaY, imgAreaW, imgAreaH / 2.0f), &sfC, &bErr);
        g.DrawString(L"Cannot load image", -1, &fSmall,
            RectF(0, imgAreaY + imgAreaH / 2.0f, imgAreaW, imgAreaH / 2.0f), &sfC, &bMsg);
    }

    // ── Status bar ────────────────────────────────────────────
    float sbY = (float)(H - STATUSBAR_H);
    SolidBrush bSbBg(Color(255, 22, 22, 28));
    g.FillRectangle(&bSbBg, 0.0f, sbY, (float)W, (float)STATUSBAR_H);
    Pen pSbLine(Color(255, 50, 50, 60), 1.0f);
    g.DrawLine(&pSbLine, 0, (int)sbY, W, (int)sbY);

    if (iv.img && iv.img->GetLastStatus() == Ok) {
        wchar_t statusBuf[256];
        wstring fname = Filename(iv.path);
        swprintf(statusBuf, 256, L"  %s   |   %d × %d px   |   %.0f%%   |   %d / %d",
            fname.c_str(),
            iv.img->GetWidth(), iv.img->GetHeight(),
            iv.zoom * 100.0f,
            iv.index + 1, (int)iv.files.size());

        SolidBrush bSbTxt(Color(255, 150, 155, 165));
        StringFormat sfL; sfL.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(statusBuf, -1, &fStatus,
            RectF(0, sbY, (float)W, (float)STATUSBAR_H), &sfL, &bSbTxt);
    }

    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    EndPaint(hWnd, &ps);
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
        return 1; // handled in WM_PAINT (double-buffer)

    // ── Keyboard ──────────────────────────────────────────────
    case WM_KEYDOWN: {
        switch (wParam) {
        case VK_ESCAPE:
            DestroyWindow(hWnd);
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
        case VK_ADD: case VK_OEM_PLUS:
            StepZoom(ZOOM_STEP);
            break;
        case VK_SUBTRACT: case VK_OEM_MINUS:
            StepZoom(-ZOOM_STEP);
            break;
        }
        return 0;
    }

    // ── Mouse wheel : Ctrl held = zoom, else nothing ──────────
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        // Convert screen → client
        POINT pt = { mx, my };
        ScreenToClient(hWnd, &pt);
        float frac = (float)delta / WHEEL_DELTA; // +1 or -1 usually
        StepZoom(frac * ZOOM_STEP, pt.x, pt.y);
        return 0;
    }

    // ── Left button : drag pan ────────────────────────────────
    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        // Toolbar button hit test
        RECT rc; GetClientRect(hWnd, &rc);
        if (my < TOOLBAR_H) {
            for (int i = 0; i < BTN_COUNT; i++) {
                float bx, by, bw, bh;
                GetBtnRect(i, (float)rc.right, bx, by, bw, bh);
                if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
                    switch (i) {
                    case 0: // prev
                        if (!iv.files.empty()) {
                            iv.index = (iv.index - 1 + (int)iv.files.size()) % (int)iv.files.size();
                            LoadCurrentImage();
                        }
                        break;
                    case 1: StepZoom(-ZOOM_STEP); break;
                    case 2: FitToWindow(); break;
                    case 3: StepZoom(ZOOM_STEP);  break;
                    case 4:
                        iv.rotation = (iv.rotation + 90) % 360;
                        InvalidateRect(hWnd, NULL, FALSE);
                        break;
                    case 5: // next
                        if (!iv.files.empty()) {
                            iv.index = (iv.index + 1) % (int)iv.files.size();
                            LoadCurrentImage();
                        }
                        break;
                    case 6: DestroyWindow(hWnd); break;
                    }
                    return 0;
                }
            }
            return 0;
        }

        // Image area → start drag
        iv.dragging    = true;
        iv.dragStartX  = mx;
        iv.dragStartY  = my;
        iv.panXAtDrag  = iv.panX;
        iv.panYAtDrag  = iv.panY;
        SetCapture(hWnd);
        return 0;
    }

    case WM_LBUTTONUP:
        if (iv.dragging) {
            iv.dragging = false;
            ReleaseCapture();
        }
        return 0;

    case WM_LBUTTONDBLCLK: {
        int my = GET_Y_LPARAM(lParam);
        if (my >= TOOLBAR_H) {
            // Double-click image area: toggle fit / actual size
            if (iv.zoom == 1.0f) FitToWindow();
            else                  ActualSize();
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        if (iv.dragging) {
            iv.panX = iv.panXAtDrag + (mx - iv.dragStartX);
            iv.panY = iv.panYAtDrag + (my - iv.dragStartY);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        // Update hover state for toolbar
        if (my < TOOLBAR_H) {
            RECT rc2; GetClientRect(hWnd, &rc2);
            int oldHov = iv.hovBtn;
            iv.hovBtn = -1;
            for (int i = 0; i < BTN_COUNT; i++) {
                float bx, by, bw, bh;
                GetBtnRect(i, (float)rc2.right, bx, by, bw, bh);
                if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
                    iv.hovBtn = i; break;
                }
            }
            if (iv.hovBtn != oldHov) InvalidateRect(hWnd, NULL, FALSE);
        } else {
            if (iv.hovBtn != -1) { iv.hovBtn = -1; InvalidateRect(hWnd, NULL, FALSE); }
            // Change cursor to grab when zoomed
            if (iv.zoom > 1.0f)
                SetCursor(iv.dragging ? LoadCursor(NULL, IDC_SIZEALL) : LoadCursor(NULL, IDC_HAND));
            else
                SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (iv.hovBtn != -1) { iv.hovBtn = -1; InvalidateRect(hWnd, NULL, FALSE); }
        return 0;

    case WM_SIZE:
        FitToWindow();  // re-fit when window resizes
        return 0;

    case WM_DESTROY:
        // Clean up
        if (iv.img) { delete iv.img; iv.img = nullptr; }
        iv.hWnd = nullptr;
        GdiplusShutdown(iv.gdipToken);
        iv.gdipToken = 0;
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

    // GDI+
    GdiplusStartupInput gsi;
    GdiplusStartup(&iv.gdipToken, &gsi, NULL);

    HINSTANCE hInst = GetModuleHandleW(NULL);

    // Register window class (once)
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = IV_WndProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = IV_CLASS;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = CreateSolidBrush(RGB(18, 18, 22));
        wc.hIcon = LoadIconW(hInst, L"IDI_APP_ICON");
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    // Build image file list
    BuildFileList(startPath);

    // Create window
    HWND hWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        IV_CLASS,
        L"RasFocus+ Photo Viewer",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 800,
        NULL, NULL, hInst, NULL
    );
    if (!hWnd) {
        GdiplusShutdown(iv.gdipToken); iv.gdipToken = 0;
        return 1;
    }
    iv.hWnd = hWnd;

    // Load the first image (sets zoom/pan)
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
    // Run in its own thread so it doesn't block the caller
    IVParams* p = new IVParams{ imagePath };
    CreateThread(NULL, 0, IV_Thread, p, 0, NULL);
}
