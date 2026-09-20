// image_viewer.cpp
// RasFocus+ Native GDI+ Image Viewer  -  v3.0
//
// TWO MODES (toggle with Tab or toolbar button):
//
// [Single Mode]  - one image at a time, fast prev/next (background cache)
//   Zoom anchored to cursor, smooth animation, F11 full-screen
//
// [Continuous/Strip Mode]  - PDF-style vertical strip
//   All folder images stacked vertically, scroll freely with mouse wheel
//   or drag, zoom applies to whole strip, current image tracked by centre
//   of viewport, lazy-loads images as they scroll into view
//
// Keys (both modes):
//   Tab        - toggle Single <-> Continuous mode
//   F11        - full screen
//   ESC        - exit full-screen first, then close
//   F          - fit to window
//   1          - actual size (100%)
//   R          - rotate 90 CW  (single mode only)
//   +/-        - zoom in/out
//   Ctrl+Home  - jump to first image (continuous)
//   Ctrl+End   - jump to last  image (continuous)
//
// Single mode extras:
//   Left/Right  or  A/D  or  toolbar  - prev / next image

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
#include <map>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "Shlwapi.lib")

using namespace Gdiplus;
using namespace std;

// ---------------------------------------------------------------
// CONSTANTS
// ---------------------------------------------------------------
static const wchar_t* IV_CLASS      = L"RasFocusImageViewer";
static const int      TOOLBAR_H     = 48;
static const int      STATUSBAR_H   = 22;
static const float    ZOOM_MIN      = 0.05f;
static const float    ZOOM_MAX      = 32.0f;
static const float    ANIM_SPEED    = 0.20f;   // lerp factor
static const int      ANIM_TIMER    = 1;
static const int      ANIM_MS       = 10;

// Continuous mode
static const int      STRIP_GAP     = 16;      // px between images
static const int      STRIP_MARGIN  = 24;      // px side margin
static const int      LAZY_RADIUS   = 2;       // load N images around viewport

// ---------------------------------------------------------------
// HELPERS
// ---------------------------------------------------------------
static wstring ExtLower(const wstring& p) {
    size_t d = p.rfind(L'.');
    if (d == wstring::npos) return L"";
    wstring e = p.substr(d+1);
    for (auto& c : e) c = towlower(c);
    return e;
}
static wstring Filename(const wstring& p) {
    size_t s = p.find_last_of(L"\\/");
    return s == wstring::npos ? p : p.substr(s+1);
}
static bool IsViewableExt(const wstring& e) {
    return e==L"jpg"||e==L"jpeg"||e==L"png"||e==L"gif"||
           e==L"bmp"||e==L"webp"||e==L"ico"||e==L"tiff"||e==L"tif";
}

// ---------------------------------------------------------------
// STRIP ENTRY  (one slot in continuous mode)
// ---------------------------------------------------------------
struct StripEntry {
    wstring  path;
    Image*   img      = nullptr;   // null = not loaded yet
    bool     loading  = false;
    float    top      = 0;         // Y position in strip space (zoom=1)
    float    height   = 0;         // display height at zoom=1 (0 = unknown, uses placeholder)
    int      srcW     = 0, srcH = 0;
};

// ---------------------------------------------------------------
// GLOBAL STATE
// ---------------------------------------------------------------
struct IVState {
    // --- file list ---
    vector<wstring>  files;
    int              index = 0;   // "current" image (single mode / strip centre)

    // --- single mode ---
    Image*   img      = nullptr;
    wstring  path;
    int      rotation = 0;

    // single-mode neighbour cache
    Image*   cache[2]       = {nullptr,nullptr};
    int      cacheIdx[2]    = {-1,-1};
    HANDLE   cacheThread[2] = {NULL,NULL};
    CRITICAL_SECTION cacheCS;

    // --- mode ---
    bool     continuousMode = false;

    // --- view transform (animated) ---
    float    zoom       = 1.0f, zoomTarget = 1.0f;
    float    scrollY    = 0.0f, scrollYTarget = 0.0f;  // continuous strip offset
    float    panX       = 0.0f, panXTarget    = 0.0f;  // used in both modes
    float    panY       = 0.0f, panYTarget    = 0.0f;  // single mode only

    // --- drag ---
    bool     dragging   = false;
    int      dragSX=0, dragSY=0;
    float    panXD=0, panYD=0, scrollYD=0;

    // --- toolbar hover ---
    int      hovBtn = -1;

    // --- full screen ---
    bool     isFullScreen = false;
    RECT     savedRect    = {};
    DWORD    savedStyle=0, savedExStyle=0;

    // --- strip ---
    vector<StripEntry>      strip;          // parallel to files
    CRITICAL_SECTION        stripCS;
    float                   stripTotalH = 0;  // total height at zoom=1
    bool                    stripBuilt  = false;

    // lazy-load thread pool (simple: one at a time via PostMessage)
    HANDLE   lazyThread = NULL;

    // --- window ---
    HWND       hWnd      = nullptr;
    ULONG_PTR  gdipToken = 0;
};
static IVState iv;

// Custom message for lazy-load completion
#define WM_STRIP_LOADED (WM_USER + 42)

// ---------------------------------------------------------------
// IMAGE AREA RECT
// ---------------------------------------------------------------
static void GetImgArea(RECT& out) {
    RECT rc; GetClientRect(iv.hWnd, &rc);
    bool tb = !iv.isFullScreen;
    out.left   = 0;
    out.top    = tb ? TOOLBAR_H : 0;
    out.right  = rc.right;
    out.bottom = tb ? (rc.bottom - STATUSBAR_H) : rc.bottom;
}
static float AreaW() { RECT a; GetImgArea(a); return (float)(a.right-a.left); }
static float AreaH() { RECT a; GetImgArea(a); return (float)(a.bottom-a.top); }

// ---------------------------------------------------------------
// FILE LIST
// ---------------------------------------------------------------
static void BuildFileList(const wstring& startPath) {
    iv.files.clear(); iv.index = 0;
    size_t sl = startPath.find_last_of(L"\\/");
    if (sl == wstring::npos) { iv.files.push_back(startPath); return; }
    wstring dir = startPath.substr(0, sl+1);
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW((dir+L"*").c_str(), &fd);
    if (hf == INVALID_HANDLE_VALUE) { iv.files.push_back(startPath); return; }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wstring nm = fd.cFileName;
        if (IsViewableExt(ExtLower(nm))) iv.files.push_back(dir+nm);
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
    sort(iv.files.begin(), iv.files.end(),
         [](const wstring& a,const wstring& b){ return _wcsicmp(a.c_str(),b.c_str())<0; });
    wstring sl2 = startPath; for (auto& c:sl2) c=towlower(c);
    for (int i=0;i<(int)iv.files.size();i++) {
        wstring f=iv.files[i]; for(auto&c:f)c=towlower(c);
        if (f==sl2){iv.index=i;break;}
    }
}

// ---------------------------------------------------------------
// STRIP LAYOUT  (builds StripEntry list with Y positions)
// ---------------------------------------------------------------
static const float PLACEHOLDER_H = 240.0f;  // height when image not yet loaded

static void RebuildStripLayout() {
    float aW = AreaW();
    float usable = aW - 2*STRIP_MARGIN;
    if (usable < 10) usable = 10;

    float y = STRIP_GAP;
    for (auto& e : iv.strip) {
        e.top = y;
        float dispH;
        if (e.srcW > 0 && e.srcH > 0) {
            float scale = usable / (float)e.srcW;
            dispH = (float)e.srcH * scale;
        } else {
            dispH = PLACEHOLDER_H;
        }
        e.height = dispH;
        y += dispH + STRIP_GAP;
    }
    iv.stripTotalH = y;
}

static void InitStrip() {
    EnterCriticalSection(&iv.stripCS);
    iv.strip.clear();
    iv.strip.resize(iv.files.size());
    for (int i=0;i<(int)iv.files.size();i++)
        iv.strip[i].path = iv.files[i];
    iv.stripBuilt = false;
    LeaveCriticalSection(&iv.stripCS);
    RebuildStripLayout();
    iv.stripBuilt = true;
}

// ---------------------------------------------------------------
// LAZY LOAD THREAD  (loads ONE image, posts WM_STRIP_LOADED)
// ---------------------------------------------------------------
struct LazyJob { int index; wstring path; };

static DWORD WINAPI LazyThread(LPVOID param) {
    LazyJob* job = (LazyJob*)param;
    int idx       = job->index;
    wstring path  = job->path;
    delete job;

    Image* img = Image::FromFile(path.c_str());
    int w=0, h=0;
    if (img && img->GetLastStatus()==Ok) {
        w = (int)img->GetWidth();
        h = (int)img->GetHeight();
    }

    EnterCriticalSection(&iv.stripCS);
    bool valid = (idx < (int)iv.strip.size() &&
                  iv.strip[idx].path == path &&
                  iv.strip[idx].img  == nullptr);
    if (valid) {
        iv.strip[idx].img    = img;
        iv.strip[idx].srcW   = w;
        iv.strip[idx].srcH   = h;
        iv.strip[idx].loading= false;
    } else {
        delete img;
    }
    iv.lazyThread = NULL;
    LeaveCriticalSection(&iv.stripCS);

    if (iv.hWnd) PostMessageW(iv.hWnd, WM_STRIP_LOADED, (WPARAM)idx, 0);
    return 0;
}

// Figure out which strip images are visible and schedule loads
static void KickLazyLoads() {
    if (!iv.stripBuilt) return;
    float aH  = AreaH();
    float top = iv.scrollY / iv.zoom;         // strip-space viewport top
    float bot = top + aH   / iv.zoom;         // strip-space viewport bottom

    // expand by LAZY_RADIUS images worth
    EnterCriticalSection(&iv.stripCS);
    for (int i=0;i<(int)iv.strip.size();i++) {
        auto& e = iv.strip[i];
        float eBot = e.top + e.height;
        bool visible = (eBot >= top - PLACEHOLDER_H*LAZY_RADIUS &&
                        e.top <= bot + PLACEHOLDER_H*LAZY_RADIUS);
        if (visible && !e.img && !e.loading && iv.lazyThread==NULL) {
            e.loading = true;
            LazyJob* job = new LazyJob{i, e.path};
            HANDLE h = CreateThread(NULL,0,LazyThread,job,0,NULL);
            iv.lazyThread = h;
            // only start one at a time; next tick will catch more
            break;
        }
    }
    LeaveCriticalSection(&iv.stripCS);
}

// Find which image is centred in viewport
static int StripCentreIndex() {
    float aH  = AreaH();
    float mid  = (iv.scrollY + aH/2.0f) / iv.zoom;  // strip-space midpoint
    int best = 0; float bestD = 1e9f;
    EnterCriticalSection(&iv.stripCS);
    for (int i=0;i<(int)iv.strip.size();i++) {
        float c = iv.strip[i].top + iv.strip[i].height/2.0f;
        float d = fabsf(c - mid);
        if (d < bestD) { bestD=d; best=i; }
    }
    LeaveCriticalSection(&iv.stripCS);
    return best;
}

// Scroll so that image[idx] is at the top of viewport
static void ScrollToIndex(int idx) {
    if (idx < 0 || idx >= (int)iv.strip.size()) return;
    EnterCriticalSection(&iv.stripCS);
    float stripTop = iv.strip[idx].top;
    LeaveCriticalSection(&iv.stripCS);
    iv.scrollYTarget = stripTop * iv.zoomTarget;
    iv.scrollY       = iv.scrollYTarget;
}

// ---------------------------------------------------------------
// SINGLE-MODE CACHE
// ---------------------------------------------------------------
struct CacheJob { wstring path; int slot; };
static DWORD WINAPI CacheThread(LPVOID param) {
    CacheJob* job = (CacheJob*)param;
    Image* loaded = Image::FromFile(job->path.c_str());
    int slot = job->slot; wstring path = job->path;
    delete job;
    EnterCriticalSection(&iv.cacheCS);
    int ei = (slot==0)
        ? (iv.index-1+(int)iv.files.size())%(int)iv.files.size()
        : (iv.index+1)%(int)iv.files.size();
    bool valid = !iv.files.empty();
    if (valid) {
        wstring ex=iv.files[ei]; for(auto&c:ex)c=towlower(c);
        wstring p2=path;         for(auto&c:p2)c=towlower(c);
        valid=(p2==ex);
    }
    if (valid) { if(iv.cache[slot])delete iv.cache[slot]; iv.cache[slot]=loaded; iv.cacheIdx[slot]=ei; }
    else delete loaded;
    iv.cacheThread[slot]=NULL;
    LeaveCriticalSection(&iv.cacheCS);
    return 0;
}
static void StartSingleCacheJobs() {
    if (iv.files.size()<=1) return;
    for (int s=0;s<2;s++) {
        int ti=(s==0)?(iv.index-1+(int)iv.files.size())%(int)iv.files.size()
                     :(iv.index+1)%(int)iv.files.size();
        EnterCriticalSection(&iv.cacheCS);
        bool done=(iv.cacheIdx[s]==ti&&iv.cache[s]!=nullptr);
        bool busy=(iv.cacheThread[s]!=NULL);
        LeaveCriticalSection(&iv.cacheCS);
        if (!done&&!busy) {
            CacheJob* job=new CacheJob{iv.files[ti],s};
            HANDLE h=CreateThread(NULL,0,CacheThread,job,0,NULL);
            EnterCriticalSection(&iv.cacheCS); iv.cacheThread[s]=h; LeaveCriticalSection(&iv.cacheCS);
        }
    }
}

// ---------------------------------------------------------------
// LOAD SINGLE IMAGE
// ---------------------------------------------------------------
static void LoadCurrentImage() {
    if (iv.files.empty()) return;
    wstring np = iv.files[iv.index];
    if (np==iv.path && iv.img) return;

    Image* cached=nullptr;
    EnterCriticalSection(&iv.cacheCS);
    for (int s=0;s<2;s++) {
        if (iv.cacheIdx[s]==iv.index && iv.cache[s]) {
            cached=iv.cache[s]; iv.cache[s]=nullptr; iv.cacheIdx[s]=-1; break;
        }
    }
    LeaveCriticalSection(&iv.cacheCS);

    if (iv.img){delete iv.img; iv.img=nullptr;}
    iv.img = cached ? cached : Image::FromFile(np.c_str());
    iv.path=np; iv.rotation=0;

    RECT ia; GetImgArea(ia);
    float aW=(float)(ia.right-ia.left), aH=(float)(ia.bottom-ia.top);
    float iw=(float)iv.img->GetWidth(), ih=(float)iv.img->GetHeight();
    if(iw<1)iw=1; if(ih<1)ih=1;
    float fit=min(aW/iw, aH/ih);
    if(fit>1)fit=1;
    iv.zoomTarget=fit; iv.panXTarget=0; iv.panYTarget=0;
    iv.zoom=iv.zoomTarget; iv.panX=iv.panXTarget; iv.panY=iv.panYTarget;

    wstring t=L"RasFocus+ Photo \u2014 "+Filename(iv.path);
    SetWindowTextW(iv.hWnd,t.c_str());
    InvalidateRect(iv.hWnd,NULL,FALSE);
    StartSingleCacheJobs();
}

// ---------------------------------------------------------------
// ZOOM HELPERS
// ---------------------------------------------------------------
static void StartAnim() { SetTimer(iv.hWnd, ANIM_TIMER, ANIM_MS, NULL); }

// Single mode: zoom at cursor (areaX,areaY relative to image-area top-left)
static void SingleZoom(float newZ, float areaX, float areaY) {
    RECT ia; GetImgArea(ia);
    float aW=(float)(ia.right-ia.left), aH=(float)(ia.bottom-ia.top);
    float ratio=newZ/iv.zoomTarget;
    iv.panXTarget = areaX - ratio*(areaX - aW/2.0f - iv.panXTarget) - aW/2.0f;
    iv.panYTarget = areaY - ratio*(areaY - aH/2.0f - iv.panYTarget) - aH/2.0f;
    iv.zoomTarget = max(ZOOM_MIN, min(ZOOM_MAX, newZ));
    StartAnim();
}

// Continuous mode: zoom anchored to cursor (areaX,areaY)
static void StripZoom(float newZ, float areaX, float areaY) {
    newZ = max(ZOOM_MIN, min(ZOOM_MAX, newZ));
    float ratio = newZ / iv.zoomTarget;
    // scroll: the strip-space point under cursor stays fixed
    // stripY = (areaY + scrollY) / zoom
    // after zoom: newScrollY = stripY*newZ - areaY
    float stripY = (areaY + iv.scrollYTarget) / iv.zoomTarget;
    iv.scrollYTarget = stripY * newZ - areaY;
    if (iv.scrollYTarget < 0) iv.scrollYTarget = 0;
    iv.zoomTarget = newZ;
    StartAnim();
}

static void FitToWindow() {
    if (iv.continuousMode) {
        // fit width: zoom so one image fills width
        float aW = AreaW();
        float usable = aW - 2*STRIP_MARGIN;
        // find first loaded image for reference width
        float refW = 800;
        EnterCriticalSection(&iv.stripCS);
        for (auto& e:iv.strip) if(e.srcW>0){refW=(float)e.srcW;break;}
        LeaveCriticalSection(&iv.stripCS);
        float fit = usable/refW;
        if (fit > 2.0f) fit = 2.0f;
        StripZoom(fit, AreaW()/2.0f, AreaH()/2.0f);
    } else {
        if (!iv.img) return;
        float aW=AreaW(), aH=AreaH();
        float iw=(float)iv.img->GetWidth(), ih=(float)iv.img->GetHeight();
        if(iv.rotation==90||iv.rotation==270) swap(iw,ih);
        if(iw<1)iw=1; if(ih<1)ih=1;
        float fit=min(aW/iw,aH/ih); if(fit>1)fit=1;
        iv.zoomTarget=fit; iv.panXTarget=0; iv.panYTarget=0;
        StartAnim();
    }
}

static void ActualSize() {
    if (iv.continuousMode) {
        StripZoom(1.0f, AreaW()/2.0f, AreaH()/2.0f);
    } else {
        iv.zoomTarget=1; iv.panXTarget=0; iv.panYTarget=0;
        StartAnim();
    }
}

// ---------------------------------------------------------------
// FULL SCREEN
// ---------------------------------------------------------------
static void ToggleFullScreen() {
    if (!iv.isFullScreen) {
        iv.savedStyle  =GetWindowLongW(iv.hWnd,GWL_STYLE);
        iv.savedExStyle=GetWindowLongW(iv.hWnd,GWL_EXSTYLE);
        GetWindowRect(iv.hWnd,&iv.savedRect);
        DWORD ns=iv.savedStyle&~(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU);
        SetWindowLongW(iv.hWnd,GWL_STYLE,ns);
        SetWindowLongW(iv.hWnd,GWL_EXSTYLE,iv.savedExStyle&~WS_EX_APPWINDOW);
        HMONITOR hm=MonitorFromWindow(iv.hWnd,MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi={sizeof(mi)}; GetMonitorInfoW(hm,&mi);
        RECT& mr=mi.rcMonitor;
        SetWindowPos(iv.hWnd,HWND_TOP,mr.left,mr.top,mr.right-mr.left,mr.bottom-mr.top,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
        iv.isFullScreen=true;
    } else {
        SetWindowLongW(iv.hWnd,GWL_STYLE,iv.savedStyle);
        SetWindowLongW(iv.hWnd,GWL_EXSTYLE,iv.savedExStyle);
        SetWindowPos(iv.hWnd,NULL,iv.savedRect.left,iv.savedRect.top,
            iv.savedRect.right-iv.savedRect.left,iv.savedRect.bottom-iv.savedRect.top,
            SWP_FRAMECHANGED|SWP_SHOWWINDOW|SWP_NOZORDER);
        iv.isFullScreen=false;
    }
    FitToWindow();
    iv.zoom=iv.zoomTarget; iv.panX=iv.panXTarget; iv.panY=iv.panYTarget;
    iv.scrollY=iv.scrollYTarget;
    InvalidateRect(iv.hWnd,NULL,FALSE);
}

// ---------------------------------------------------------------
// MODE SWITCH
// ---------------------------------------------------------------
static void SwitchMode(bool toContinuous) {
    if (toContinuous == iv.continuousMode) return;
    iv.continuousMode = toContinuous;

    if (toContinuous) {
        // Build strip if not yet done
        if (iv.strip.empty()) InitStrip();
        // Scroll to current image
        float aW=AreaW();
        float usable=aW-2*STRIP_MARGIN;
        float refW=800;
        EnterCriticalSection(&iv.stripCS);
        if (!iv.strip.empty()&&iv.strip[iv.index].srcW>0) refW=(float)iv.strip[iv.index].srcW;
        LeaveCriticalSection(&iv.stripCS);
        iv.zoomTarget=min(usable/refW,2.0f);
        iv.zoom=iv.zoomTarget;
        ScrollToIndex(iv.index);
        KickLazyLoads();
    } else {
        // Switch back: update index from strip viewport
        iv.index = StripCentreIndex();
        // keep same zoom roughly
        LoadCurrentImage();
    }
    InvalidateRect(iv.hWnd,NULL,FALSE);
}

// ---------------------------------------------------------------
// TOOLBAR  (9 buttons now)
// ---------------------------------------------------------------
struct BtnDef { const wchar_t* icon; const wchar_t* tip; };
// 0=prev 1=zoomOut 2=fit 3=zoomIn 4=rotate 5=mode 6=fullscreen 7=next 8=close
static BtnDef s_btns[] = {
    { L"\xE76B", L"Prev (\u2190 / A)" },
    { L"\xE71F", L"Zoom Out (-)" },
    { L"\xE9A6", L"Fit (F)" },
    { L"\xE8A3", L"Zoom In (+)" },
    { L"\xE7AD", L"Rotate (R)" },
    { L"\xE8A9", L"Continuous / Single (Tab)" },  // 5 = mode toggle
    { L"\xE740", L"Full Screen (F11)" },            // 6
    { L"\xE76C", L"Next (\u2192 / D)" },            // 7
    { L"\xE711", L"Close (Esc)" },                  // 8
};
static const int BTN_COUNT = 9;
static const float BTN_W = 44.0f, BTN_H = 36.0f;

static void GetBtnRect(int i, float wW, float& bx, float& by, float& bw, float& bh) {
    bw=BTN_W; bh=BTN_H; by=(TOOLBAR_H-BTN_H)/2.0f;
    if (i<=6)      bx = 8.0f + i*(BTN_W+6.0f);
    else if (i==7) bx = wW - 2*(BTN_W+8.0f);
    else           bx = wW -   (BTN_W+8.0f);
}

// ---------------------------------------------------------------
// DRAWING
// ---------------------------------------------------------------
static void DrawCheckerboard(Graphics& g, float x, float y, float w, float h) {
    if (w<=0||h<=0) return;
    int sz=10;
    g.SetClip(RectF(x,y,w,h));
    for (int r=0;r<(int)(h/sz)+1;r++)
        for (int c=0;c<(int)(w/sz)+1;c++) {
            bool odd=(r+c)%2;
            SolidBrush br(odd?Color(255,200,200,200):Color(255,240,240,240));
            float tx=x+c*sz,ty=y+r*sz,tw=min((float)sz,x+w-tx),th=min((float)sz,y+h-ty);
            if(tw>0&&th>0) g.FillRectangle(&br,tx,ty,tw,th);
        }
    g.ResetClip();
}

static void DrawToolbar(Graphics& g, int W) {
    FontFamily ffUI(L"Segoe UI"), ffIco(L"Segoe MDL2 Assets");
    Font fSmall(&ffUI,11,FontStyleRegular,UnitPixel);
    Font fBtnIco(&ffIco,16,FontStyleRegular,UnitPixel);
    StringFormat sfC; sfC.SetAlignment(StringAlignmentCenter); sfC.SetLineAlignment(StringAlignmentCenter);

    SolidBrush bTB(Color(255,28,28,32));
    g.FillRectangle(&bTB, 0.0f,0.0f,(float)W,(float)TOOLBAR_H);
    Pen pLine(Color(255,60,60,70),1.0f);
    g.DrawLine(&pLine,0.0f,(float)TOOLBAR_H,(float)W,(float)TOOLBAR_H);

    for (int i=0;i<BTN_COUNT;i++) {
        // Hide prev/next/rotate in continuous mode
        if (iv.continuousMode && (i==0||i==7||i==4)) continue;
        float bx,by,bw,bh; GetBtnRect(i,(float)W,bx,by,bw,bh);
        bool hov=(iv.hovBtn==i);
        bool dis=(i==0&&iv.files.size()<=1)||(i==7&&iv.files.size()<=1);
        bool act=(i==5&&iv.continuousMode)||(i==6&&iv.isFullScreen);
        Color cBg=(hov||act)?Color(255,70,140,200):Color(0,0,0,0);
        Color cIco=dis?Color(255,80,80,90):((hov||act)?Color(255,255,255,255):Color(255,180,185,195));
        if (hov||act){SolidBrush bh2(cBg);g.FillRectangle(&bh2,bx,by,bw,bh);}
        SolidBrush bIco(cIco);
        g.DrawString(s_btns[i].icon,-1,&fBtnIco,RectF(bx,by,bw,bh),&sfC,&bIco);
    }

    // Centre label
    if (iv.continuousMode) {
        int ci=StripCentreIndex();
        wchar_t lb[64]; swprintf(lb,64,L"%.0f%%  %d / %d",iv.zoom*100,(ci+1),(int)iv.files.size());
        SolidBrush bZ(Color(255,160,165,175));
        g.DrawString(lb,-1,&fSmall,RectF((float)W/2-60,0,120,(float)TOOLBAR_H),&sfC,&bZ);
    } else {
        wchar_t lb[32]; swprintf(lb,32,L"%.0f%%",iv.zoom*100);
        SolidBrush bZ(Color(255,160,165,175));
        g.DrawString(lb,-1,&fSmall,RectF((float)W/2-30,0,60,(float)TOOLBAR_H),&sfC,&bZ);
    }
}

static void DrawStatusBar(Graphics& g, int W, int H) {
    FontFamily ffUI(L"Segoe UI");
    Font fStatus(&ffUI,11,FontStyleRegular,UnitPixel);
    StringFormat sfL; sfL.SetLineAlignment(StringAlignmentCenter);
    float sbY=(float)(H-STATUSBAR_H);
    SolidBrush bSbBg(Color(255,22,22,28));
    g.FillRectangle(&bSbBg,0.0f,sbY,(float)W,(float)STATUSBAR_H);
    Pen pSbLine(Color(255,50,50,60),1.0f);
    g.DrawLine(&pSbLine,0.0f,sbY,(float)W,sbY);

    wchar_t buf[256];
    if (iv.continuousMode) {
        int ci=StripCentreIndex();
        swprintf(buf,256,L"  %s   |   %.0f%%   |   %d / %d   |   Continuous Mode (Tab to switch)",
            Filename(iv.files[ci]).c_str(), iv.zoom*100, ci+1, (int)iv.files.size());
    } else if (iv.img&&iv.img->GetLastStatus()==Ok) {
        swprintf(buf,256,L"  %s   |   %d \xD7 %d px   |   %.0f%%   |   %d / %d   |   Single Mode (Tab to switch)",
            Filename(iv.path).c_str(),
            iv.img->GetWidth(),iv.img->GetHeight(),iv.zoom*100,iv.index+1,(int)iv.files.size());
    } else { buf[0]=0; }

    SolidBrush bTxt(Color(255,150,155,165));
    g.DrawString(buf,-1,&fStatus,RectF(0,sbY,(float)W,(float)STATUSBAR_H),&sfL,&bTxt);
}

// Draw the continuous strip
static void DrawStrip(Graphics& g, const RECT& ia) {
    float aW=(float)(ia.right-ia.left);
    float aH=(float)(ia.bottom-ia.top);
    float aY=(float)ia.top;

    float usable = aW - 2*STRIP_MARGIN;

    // clip to image area
    g.SetClip(RectF(0,aY,aW,aH));

    EnterCriticalSection(&iv.stripCS);

    for (int i=0;i<(int)iv.strip.size();i++) {
        auto& e = iv.strip[i];
        // screen Y of image top
        float screenTop = aY + e.top * iv.zoom - iv.scrollY;
        float dispH     = e.height * iv.zoom;
        float dispW     = (e.srcW>0) ? (float)e.srcW*(dispH/(float)e.srcH) : usable*iv.zoom;
        if (e.srcW>0&&e.srcH>0) {
            float scale = usable*iv.zoom / ((float)e.srcW);
            dispW = usable*iv.zoom;
            dispH = (float)e.srcH * scale;
        } else {
            dispW = usable*iv.zoom;
            dispH = PLACEHOLDER_H*iv.zoom;
        }
        float screenX = (aW - dispW)/2.0f;

        // cull: off screen
        if (screenTop > aY+aH+10 || screenTop+dispH < aY-10) continue;

        if (e.img && e.img->GetLastStatus()==Ok) {
            DrawCheckerboard(g, screenX, screenTop, dispW, dispH);
            g.DrawImage(e.img, RectF(screenX, screenTop, dispW, dispH));
        } else {
            // placeholder
            SolidBrush bPh(Color(255,32,32,38));
            g.FillRectangle(&bPh, screenX, screenTop, dispW, max(dispH,40.0f));
            // loading indicator text
            FontFamily ff(L"Segoe UI"); Font fLoad(&ff,13,FontStyleRegular,UnitPixel);
            StringFormat sfc; sfc.SetAlignment(StringAlignmentCenter); sfc.SetLineAlignment(StringAlignmentCenter);
            SolidBrush bLd(Color(255,100,105,115));
            wchar_t lb[128]; swprintf(lb,128,L"%s",Filename(e.path).c_str());
            g.DrawString(lb,-1,&fLoad,RectF(screenX,screenTop,dispW,max(dispH,40.0f)),&sfc,&bLd);
        }

        // thin separator line between images
        Pen pSep(Color(60,120,120,140),1.0f);
        g.DrawRectangle(&pSep, screenX, screenTop, dispW, max(dispH,40.0f));
    }

    LeaveCriticalSection(&iv.stripCS);
    g.ResetClip();
}

static void DrawSingle(Graphics& g, const RECT& ia) {
    float aW=(float)(ia.right-ia.left);
    float aH=(float)(ia.bottom-ia.top);
    float aY=(float)ia.top;
    float cx=aW/2.0f+iv.panX, cy=aH/2.0f+iv.panY;

    if (iv.img&&iv.img->GetLastStatus()==Ok) {
        float iw=(float)iv.img->GetWidth(), ih=(float)iv.img->GetHeight();
        float dw,dh;
        if (iv.rotation==90||iv.rotation==270){dw=ih*iv.zoom;dh=iw*iv.zoom;}
        else {dw=iw*iv.zoom;dh=ih*iv.zoom;}
        float dx=cx-dw/2, dy=aY+cy-dh/2;
        DrawCheckerboard(g,dx,dy,dw,dh);
        if (iv.rotation!=0){
            Matrix m; m.RotateAt((float)iv.rotation,PointF(dx+dw/2,dy+dh/2));
            g.SetTransform(&m);
        }
        if (iv.rotation==90||iv.rotation==270)
            g.DrawImage(iv.img,RectF(dx,dy,dw,dh),0,0,iw,ih,UnitPixel);
        else
            g.DrawImage(iv.img,RectF(dx,dy,dw,dh));
        g.ResetTransform();
    } else {
        FontFamily ffIco(L"Segoe MDL2 Assets"),ffUI(L"Segoe UI");
        Font fBig(&ffIco,48,FontStyleRegular,UnitPixel),fSm(&ffUI,11,FontStyleRegular,UnitPixel);
        StringFormat sfc; sfc.SetAlignment(StringAlignmentCenter); sfc.SetLineAlignment(StringAlignmentCenter);
        SolidBrush bE(Color(255,200,80,80)),bM(Color(255,140,140,150));
        g.DrawString(L"\xE7C5",-1,&fBig,RectF(0,aY,aW,aH/2),&sfc,&bE);
        g.DrawString(L"Cannot load image",-1,&fSm,RectF(0,aY+aH/2,aW,aH/2),&sfc,&bM);
    }
}

static void OnPaint(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC hdc=BeginPaint(hWnd,&ps);
    RECT rc; GetClientRect(hWnd,&rc);
    int W=rc.right,H=rc.bottom;

    HDC mem=CreateCompatibleDC(hdc);
    HBITMAP bmp=CreateCompatibleBitmap(hdc,W,H);
    HBITMAP oldBmp=(HBITMAP)SelectObject(mem,bmp);
    Graphics g(mem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    bool tb=!iv.isFullScreen;

    // Toolbar
    if (tb) DrawToolbar(g,W);

    // Image area background
    RECT ia; GetImgArea(ia);
    SolidBrush bBg(Color(255,18,18,22));
    g.FillRectangle(&bBg,(float)ia.left,(float)ia.top,(float)(ia.right-ia.left),(float)(ia.bottom-ia.top));

    if (iv.continuousMode)  DrawStrip(g,ia);
    else                    DrawSingle(g,ia);

    // Status bar
    if (tb) DrawStatusBar(g,W,H);

    BitBlt(hdc,0,0,W,H,mem,0,0,SRCCOPY);
    SelectObject(mem,oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hWnd,&ps);
}

// ---------------------------------------------------------------
// ANIMATION TICK
// ---------------------------------------------------------------
static bool AnimTick() {
    float dz=iv.zoomTarget-iv.zoom;
    float dx=iv.panXTarget-iv.panX, dy=iv.panYTarget-iv.panY;
    float ds=iv.scrollYTarget-iv.scrollY;
    bool moving=(fabsf(dz)>0.0003f||fabsf(dx)>0.3f||fabsf(dy)>0.3f||fabsf(ds)>0.3f);
    if (!moving){
        iv.zoom=iv.zoomTarget; iv.panX=iv.panXTarget; iv.panY=iv.panYTarget;
        iv.scrollY=iv.scrollYTarget;
        return false;
    }
    float s=ANIM_SPEED;
    iv.zoom    +=dz*s; iv.panX+=dx*s; iv.panY+=dy*s; iv.scrollY+=ds*s;
    return true;
}

// ---------------------------------------------------------------
// CLAMP SCROLL
// ---------------------------------------------------------------
static void ClampScroll() {
    float maxS = max(0.0f, iv.stripTotalH * iv.zoomTarget - AreaH());
    if (iv.scrollYTarget < 0) iv.scrollYTarget = 0;
    if (iv.scrollYTarget > maxS) iv.scrollYTarget = maxS;
}

// ---------------------------------------------------------------
// WINDOW PROC
// ---------------------------------------------------------------
static LRESULT CALLBACK IV_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_PAINT: OnPaint(hWnd); return 0;
    case WM_ERASEBKGND: return 1;

    case WM_STRIP_LOADED:
        // A strip image finished loading - rebuild layout and repaint
        RebuildStripLayout();
        KickLazyLoads();  // maybe more to load
        InvalidateRect(hWnd,NULL,FALSE);
        return 0;

    case WM_TIMER:
        if (wParam==ANIM_TIMER) {
            bool still=AnimTick();
            if (iv.continuousMode) KickLazyLoads();
            InvalidateRect(hWnd,NULL,FALSE);
            if (!still) KillTimer(hWnd,ANIM_TIMER);
        }
        return 0;

    case WM_KEYDOWN: {
        bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        switch (wParam) {
        case VK_ESCAPE:
            if (iv.isFullScreen) ToggleFullScreen();
            else DestroyWindow(hWnd);
            break;
        case VK_F11: ToggleFullScreen(); break;
        case VK_TAB: SwitchMode(!iv.continuousMode); break;
        case 'F': FitToWindow(); break;
        case '1': ActualSize(); break;
        case 'R':
            if (!iv.continuousMode) {
                iv.rotation=(iv.rotation+90)%360;
                InvalidateRect(hWnd,NULL,FALSE);
            }
            break;
        case VK_LEFT: case 'A':
            if (!iv.continuousMode && !iv.files.empty()) {
                iv.index=(iv.index-1+(int)iv.files.size())%(int)iv.files.size();
                LoadCurrentImage();
            }
            break;
        case VK_RIGHT: case 'D':
            if (!iv.continuousMode && !iv.files.empty()) {
                iv.index=(iv.index+1)%(int)iv.files.size();
                LoadCurrentImage();
            }
            break;
        case VK_HOME:
            if (iv.continuousMode&&ctrl){iv.scrollYTarget=0;iv.scrollY=0;ClampScroll();StartAnim();}
            break;
        case VK_END:
            if (iv.continuousMode&&ctrl){
                iv.scrollYTarget=max(0.0f,iv.stripTotalH*iv.zoomTarget-AreaH());
                iv.scrollY=iv.scrollYTarget; ClampScroll(); StartAnim();
            }
            break;
        case VK_ADD: case VK_OEM_PLUS: {
            float cx=AreaW()/2, cy=AreaH()/2;
            if(iv.continuousMode) StripZoom(iv.zoomTarget*1.20f,cx,cy);
            else SingleZoom(iv.zoomTarget*1.20f,cx,cy);
            break;
        }
        case VK_SUBTRACT: case VK_OEM_MINUS: {
            float cx=AreaW()/2, cy=AreaH()/2;
            if(iv.continuousMode) StripZoom(iv.zoomTarget/1.20f,cx,cy);
            else SingleZoom(iv.zoomTarget/1.20f,cx,cy);
            break;
        }
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta=GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        ScreenToClient(hWnd,&pt);
        bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        RECT ia; GetImgArea(ia);
        float ax=(float)pt.x, ay=(float)(pt.y-ia.top);

        if (iv.continuousMode) {
            if (ctrl) {
                // Ctrl+wheel = zoom in strip mode
                float f=(delta>0)?1.12f:1.0f/1.12f;
                StripZoom(iv.zoomTarget*f, ax, ay);
            } else {
                // Plain scroll = scroll strip (smooth, like PDF)
                float pixels = (float)delta * 0.8f;  // 120 delta -> 96px scroll
                iv.scrollYTarget -= pixels;
                ClampScroll();
                StartAnim();
            }
        } else {
            // Single mode: always zoom (no Ctrl needed)
            float f=(delta>0)?1.15f:1.0f/1.15f;
            SingleZoom(iv.zoomTarget*f, ax, ay);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx=GET_X_LPARAM(lParam),my=GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hWnd,&rc);
        int tbH=iv.isFullScreen?0:TOOLBAR_H;
        if (my<tbH && !iv.isFullScreen) {
            for (int i=0;i<BTN_COUNT;i++){
                if (iv.continuousMode&&(i==0||i==7||i==4)) continue;
                float bx,by,bw,bh; GetBtnRect(i,(float)rc.right,bx,by,bw,bh);
                if(mx>=bx&&mx<=bx+bw&&my>=by&&my<=by+bh){
                    switch(i){
                    case 0: if(!iv.files.empty()){iv.index=(iv.index-1+(int)iv.files.size())%(int)iv.files.size();LoadCurrentImage();} break;
                    case 1: {float cx=AreaW()/2,cy=AreaH()/2;if(iv.continuousMode)StripZoom(iv.zoomTarget/1.20f,cx,cy);else SingleZoom(iv.zoomTarget/1.20f,cx,cy);} break;
                    case 2: FitToWindow(); break;
                    case 3: {float cx=AreaW()/2,cy=AreaH()/2;if(iv.continuousMode)StripZoom(iv.zoomTarget*1.20f,cx,cy);else SingleZoom(iv.zoomTarget*1.20f,cx,cy);} break;
                    case 4: if(!iv.continuousMode){iv.rotation=(iv.rotation+90)%360;InvalidateRect(hWnd,NULL,FALSE);} break;
                    case 5: SwitchMode(!iv.continuousMode); break;
                    case 6: ToggleFullScreen(); break;
                    case 7: if(!iv.files.empty()){iv.index=(iv.index+1)%(int)iv.files.size();LoadCurrentImage();} break;
                    case 8: DestroyWindow(hWnd); break;
                    }
                    return 0;
                }
            }
            return 0;
        }
        iv.dragging=true; iv.dragSX=mx; iv.dragSY=my;
        iv.panXD=iv.panXTarget; iv.panYD=iv.panYTarget; iv.scrollYD=iv.scrollYTarget;
        SetCapture(hWnd);
        return 0;
    }
    case WM_LBUTTONUP:
        if(iv.dragging){iv.dragging=false;ReleaseCapture();}
        return 0;

    case WM_LBUTTONDBLCLK: {
        int my=GET_Y_LPARAM(lParam);
        int tbH=iv.isFullScreen?0:TOOLBAR_H;
        if (my>=tbH&&!iv.continuousMode){
            if(fabsf(iv.zoomTarget-1.0f)<0.02f) FitToWindow(); else ActualSize();
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx=GET_X_LPARAM(lParam),my=GET_Y_LPARAM(lParam);
        int tbH=iv.isFullScreen?0:TOOLBAR_H;
        if (iv.dragging) {
            int dx=mx-iv.dragSX, dy=my-iv.dragSY;
            if (iv.continuousMode) {
                iv.scrollYTarget = iv.scrollYD - dy;
                ClampScroll();
                iv.scrollY = iv.scrollYTarget;  // instant drag, no lag
                KickLazyLoads();
            } else {
                iv.panXTarget = iv.panXD + dx;
                iv.panYTarget = iv.panYD + dy;
                iv.panX=iv.panXTarget; iv.panY=iv.panYTarget;
            }
            InvalidateRect(hWnd,NULL,FALSE);
            return 0;
        }
        if (my<tbH&&!iv.isFullScreen) {
            RECT rc2; GetClientRect(hWnd,&rc2);
            int oh=iv.hovBtn; iv.hovBtn=-1;
            for(int i=0;i<BTN_COUNT;i++){
                if(iv.continuousMode&&(i==0||i==7||i==4)) continue;
                float bx,by,bw,bh; GetBtnRect(i,(float)rc2.right,bx,by,bw,bh);
                if(mx>=bx&&mx<=bx+bw&&my>=by&&my<=by+bh){iv.hovBtn=i;break;}
            }
            if(iv.hovBtn!=oh) InvalidateRect(hWnd,NULL,FALSE);
        } else {
            if(iv.hovBtn!=-1){iv.hovBtn=-1;InvalidateRect(hWnd,NULL,FALSE);}
            bool canPan=iv.continuousMode||(iv.zoom>1.0f);
            SetCursor(canPan?(iv.dragging?LoadCursor(NULL,IDC_SIZEALL):LoadCursor(NULL,IDC_HAND))
                            :LoadCursor(NULL,IDC_ARROW));
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if(iv.hovBtn!=-1){iv.hovBtn=-1;InvalidateRect(hWnd,NULL,FALSE);}
        return 0;

    case WM_SIZE:
        RebuildStripLayout();
        FitToWindow();
        iv.zoom=iv.zoomTarget; iv.panX=iv.panXTarget; iv.panY=iv.panYTarget;
        iv.scrollY=iv.scrollYTarget;
        InvalidateRect(hWnd,NULL,FALSE);
        return 0;

    case WM_DESTROY:
        for(int s=0;s<2;s++){
            EnterCriticalSection(&iv.cacheCS);
            HANDLE t=iv.cacheThread[s]; iv.cacheThread[s]=NULL;
            LeaveCriticalSection(&iv.cacheCS);
            if(t) WaitForSingleObject(t,300);
        }
        {EnterCriticalSection(&iv.stripCS);
        if(iv.lazyThread) WaitForSingleObject(iv.lazyThread,300);
        for(auto&e:iv.strip) if(e.img){delete e.img; e.img=nullptr;}
        iv.strip.clear();
        LeaveCriticalSection(&iv.stripCS);}
        for(int s=0;s<2;s++) if(iv.cache[s]){delete iv.cache[s];iv.cache[s]=nullptr;}
        if(iv.img){delete iv.img;iv.img=nullptr;}
        DeleteCriticalSection(&iv.cacheCS);
        DeleteCriticalSection(&iv.stripCS);
        iv.hWnd=nullptr;
        GdiplusShutdown(iv.gdipToken); iv.gdipToken=0;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd,msg,wParam,lParam);
}

// ---------------------------------------------------------------
// VIEWER THREAD
// ---------------------------------------------------------------
struct IVParams { wstring path; };

static DWORD WINAPI IV_Thread(LPVOID param) {
    IVParams* p=(IVParams*)param;
    wstring start=p->path; delete p;

    InitializeCriticalSection(&iv.cacheCS);
    InitializeCriticalSection(&iv.stripCS);
    iv.cache[0]=iv.cache[1]=nullptr;
    iv.cacheIdx[0]=iv.cacheIdx[1]=-1;
    iv.cacheThread[0]=iv.cacheThread[1]=NULL;
    iv.lazyThread=NULL;
    iv.isFullScreen=false;
    iv.continuousMode=false;
    iv.stripBuilt=false;

    GdiplusStartupInput gsi;
    GdiplusStartup(&iv.gdipToken,&gsi,NULL);

    HINSTANCE hInst=GetModuleHandleW(NULL);
    static bool reg=false;
    if (!reg) {
        WNDCLASSEXW wc={};
        wc.cbSize=sizeof(wc); wc.lpfnWndProc=IV_WndProc; wc.hInstance=hInst;
        wc.lpszClassName=IV_CLASS; wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        wc.style=CS_DBLCLKS|CS_HREDRAW|CS_VREDRAW;
        wc.hbrBackground=CreateSolidBrush(RGB(18,18,22));
        wc.hIcon=LoadIconW(hInst,L"IDI_APP_ICON");
        RegisterClassExW(&wc); reg=true;
    }

    BuildFileList(start);

    HWND hWnd=CreateWindowExW(WS_EX_APPWINDOW,IV_CLASS,L"RasFocus+ Photo Viewer",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1280,800,NULL,NULL,hInst,NULL);
    if(!hWnd){DeleteCriticalSection(&iv.cacheCS);DeleteCriticalSection(&iv.stripCS);
              GdiplusShutdown(iv.gdipToken);iv.gdipToken=0;return 1;}
    iv.hWnd=hWnd;

    // Load first image in single mode
    LoadCurrentImage();

    ShowWindow(hWnd,SW_SHOWMAXIMIZED);
    SetForegroundWindow(hWnd);
    UpdateWindow(hWnd);

    MSG msg;
    while(GetMessage(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);}
    return 0;
}

// ---------------------------------------------------------------
// PUBLIC API
// ---------------------------------------------------------------
void LaunchImageViewer(const wstring& imagePath) {
    IVParams* p=new IVParams{imagePath};
    CreateThread(NULL,0,IV_Thread,p,0,NULL);
}
