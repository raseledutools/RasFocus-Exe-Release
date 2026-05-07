#include "tab_adult.h"
#include "mini_browser.h"   // For YouTubeBlockSettings, ApplyYouTubeBlocking, GetActiveWebView
#include <vector>
#include <string>
#include <fstream>
#include <codecvt>
#include <locale>
#include <psapi.h>
#include <tlhelp32.h>

using namespace Gdiplus;
using namespace std;

// ─────────────────────────────────────────────────────────────────────────────
// EXTERNAL BRIDGE — declared in mini_browser.cpp
// Call these to push UI settings into the live WebView2 instances.
// ─────────────────────────────────────────────────────────────────────────────
extern YouTubeBlockSettings  g_ytBlockSettings;
extern TikTokBlockSettings   g_ttBlockSettings;
extern InstagramBlockSettings g_igBlockSettings;
extern void ReapplyBlockingToAllTabs();   // Re-injects CSS into every open tab

// ─────────────────────────────────────────────────────────────────────────────
// AI FILTER LAYOUT STATE
// ─────────────────────────────────────────────────────────────────────────────
static float ai_cX = 0.0f, ai_cY = 0.0f, ai_cW = 800.0f, ai_cH = 600.0f;

// ─────────────────────────────────────────────────────────────────────────────
// COLORS
// ─────────────────────────────────────────────────────────────────────────────
static const Color AiClrTeal      (255,  12, 168, 176);
static const Color AiClrTealHover (255,  30, 185, 195);
static const Color AiClrDark      (255,  50,  50,  50);
static const Color AiClrGrayText  (255, 120, 120, 120);
static const Color AiClrBorder    (255, 220, 225, 230);
static const Color AiClrWhite     (255, 255, 255, 255);
static const Color AiClrBg        (255, 248, 250, 252);
static const Color AiClrBgHover   (255, 235, 248, 250);
static const Color AiClrRed       (255, 231,  76,  60);
static const Color AiClrGreen     (255,  90, 170,  20);

// ─────────────────────────────────────────────────────────────────────────────
// AI ENGINE STATE
// ─────────────────────────────────────────────────────────────────────────────
static bool isAiEngineActive   = false;
static bool hoverAiEngineBtn   = false;

static bool cbAiImageBlur        = true;  static bool hCbAiImageBlur       = false;
static bool cbFemaleDetectWeb    = false; static bool hCbFemaleDetectWeb    = false;
static bool cbFemaleDetectVideo  = false; static bool hCbFemaleDetectVideo  = false;

static int  aiSensitivityIdx     = 2;
static bool hoverAiSensDrop      = false;
static bool isAiSensDropOpen     = false;
static int  hoverAiSensOptIdx    = -1;
wstring aiSensitivityModes[]     = { L"Low (Fast)", L"Medium", L"High (Accurate)", L"Strict (Max Blur)" };

// ─────────────────────────────────────────────────────────────────────────────
// NAVIGATION
// ─────────────────────────────────────────────────────────────────────────────
static int  currentAppBlockView  = 0;   // 0=Main, 1=YouTube, 2=TikTok, 3=Instagram
static bool hoverBackBtn         = false;

// ─────────────────────────────────────────────────────────────────────────────
// SCROLL
// ─────────────────────────────────────────────────────────────────────────────
static int scrollOffsetYt = 0;
static int maxScrollYt    = 0;

// ─────────────────────────────────────────────────────────────────────────────
// APP BUTTON HOVER
// ─────────────────────────────────────────────────────────────────────────────
static bool hBtnYoutube   = false;
static bool hBtnTikTok    = false;
static bool hBtnInstagram = false;

// ─────────────────────────────────────────────────────────────────────────────
// YOUTUBE TOGGLE STATES  (UI variables — mirrored to g_ytBlockSettings)
// ─────────────────────────────────────────────────────────────────────────────
static bool ytHideHome         = false; static bool hYtHideHome         = false;
static bool ytHideShorts       = true;  static bool hYtHideShorts        = false;
static bool ytHideComments     = true;  static bool hYtHideComments      = false;
static bool ytHideRecVideos    = true;  static bool hYtHideRecVideos     = false;
static bool ytHideThumbnails   = false; static bool hYtHideThumbnails    = false;
static bool ytBlurThumbnails   = true;  static bool hYtBlurThumbnails    = false;
static bool ytHideSubs         = false; static bool hYtHideSubs          = false;
static bool ytHideExplore      = false; static bool hYtHideExplore       = false;
static bool ytHideTopBar       = false; static bool hYtHideTopBar        = false;
static bool ytDisableEndCards  = true;  static bool hYtDisableEndCards   = false;
static bool ytBlackWhiteMode   = false; static bool hYtBlackWhiteMode    = false;
static bool ytDisableAutoplay  = true;  static bool hYtDisableAutoplay   = false;

// ─────────────────────────────────────────────────────────────────────────────
// TIKTOK TOGGLE STATES  (UI variables — mirrored to g_ttBlockSettings)
// ─────────────────────────────────────────────────────────────────────────────
static bool ttHideExplore      = true;  static bool hTtHideExplore      = false;
static bool ttHideLive         = true;  static bool hTtHideLive         = false;
static bool ttHideComments     = true;  static bool hTtHideComments     = false;
static bool ttHideSearch       = false; static bool hTtHideSearch       = false;
static bool ttBlackWhiteMode   = false; static bool hTtBlackWhiteMode   = false;

// ─────────────────────────────────────────────────────────────────────────────
// INSTAGRAM TOGGLE STATES  (UI variables — mirrored to g_igBlockSettings)
// ─────────────────────────────────────────────────────────────────────────────
static bool igHideStories      = true;  static bool hIgHideStories      = false;
static bool igHideReels        = true;  static bool hIgHideReels        = false;
static bool igHideExplore      = true;  static bool hIgHideExplore      = false;
static bool igHideComments     = false; static bool hIgHideComments     = false;
static bool igHideSuggested    = true;  static bool hIgHideSuggested    = false;
static bool igBlackWhiteMode   = false; static bool hIgBlackWhiteMode   = false;

// ─────────────────────────────────────────────────────────────────────────────
// BRIDGE: sync UI bools → global structs, then re-inject into all open tabs
// Call this whenever ANY toggle changes.
// ─────────────────────────────────────────────────────────────────────────────
static void SyncSettingsToEngine() {
    // YouTube
    g_ytBlockSettings.hideHomePage      = ytHideHome;
    g_ytBlockSettings.hideShorts        = ytHideShorts;
    g_ytBlockSettings.hideComments      = ytHideComments;
    g_ytBlockSettings.hideRecommended   = ytHideRecVideos;
    g_ytBlockSettings.hideThumbnails    = ytHideThumbnails;
    g_ytBlockSettings.blurThumbnails    = ytBlurThumbnails;
    g_ytBlockSettings.hideSubscriptions = ytHideSubs;
    g_ytBlockSettings.hideExplore       = ytHideExplore;
    g_ytBlockSettings.hideTopBar        = ytHideTopBar;
    g_ytBlockSettings.disableEndCards   = ytDisableEndCards;
    g_ytBlockSettings.blackWhiteMode    = ytBlackWhiteMode;
    g_ytBlockSettings.disableAutoplay   = ytDisableAutoplay;

    // TikTok
    g_ttBlockSettings.hideExplore       = ttHideExplore;
    g_ttBlockSettings.hideLive          = ttHideLive;
    g_ttBlockSettings.hideComments      = ttHideComments;
    g_ttBlockSettings.hideSearch        = ttHideSearch;
    g_ttBlockSettings.blackWhiteMode    = ttBlackWhiteMode;

    // Instagram
    g_igBlockSettings.hideStories       = igHideStories;
    g_igBlockSettings.hideReels         = igHideReels;
    g_igBlockSettings.hideExplore       = igHideExplore;
    g_igBlockSettings.hideComments      = igHideComments;
    g_igBlockSettings.hideSuggested     = igHideSuggested;
    g_igBlockSettings.blackWhiteMode    = igBlackWhiteMode;

    // Push changes into every open WebView2 tab immediately
    ReapplyBlockingToAllTabs();
}

// ─────────────────────────────────────────────────────────────────────────────
// SAVE / LOAD SETTINGS
// ─────────────────────────────────────────────────────────────────────────────
void SaveAiSettings() {
    std::wofstream out(L"rasfocus_ai_data.txt");
    out.imbue(std::locale(out.getloc(), new std::codecvt_utf8<wchar_t>));
    if (!out.is_open()) return;

    out << isAiEngineActive << L"\n";
    out << cbAiImageBlur << L" " << cbFemaleDetectWeb << L" " << cbFemaleDetectVideo << L"\n";
    out << aiSensitivityIdx << L"\n";

    // YouTube
    out << ytHideHome      << L" " << ytHideShorts     << L" " << ytHideComments  << L" "
        << ytHideRecVideos << L" " << ytHideThumbnails << L" " << ytBlurThumbnails << L" "
        << ytHideSubs      << L" " << ytHideExplore    << L" " << ytHideTopBar     << L" "
        << ytDisableEndCards << L" " << ytBlackWhiteMode << L" " << ytDisableAutoplay << L"\n";

    // TikTok
    out << ttHideExplore << L" " << ttHideLive << L" " << ttHideComments << L" "
        << ttHideSearch  << L" " << ttBlackWhiteMode << L"\n";

    // Instagram
    out << igHideStories << L" " << igHideReels   << L" " << igHideExplore << L" "
        << igHideComments << L" " << igHideSuggested << L" " << igBlackWhiteMode << L"\n";

    out.close();
}

void LoadAiSettings() {
    std::wifstream in(L"rasfocus_ai_data.txt");
    in.imbue(std::locale(in.getloc(), new std::codecvt_utf8<wchar_t>));
    if (!in.is_open()) return;

    in >> isAiEngineActive;
    in >> cbAiImageBlur >> cbFemaleDetectWeb >> cbFemaleDetectVideo;
    in >> aiSensitivityIdx;

    in >> ytHideHome      >> ytHideShorts     >> ytHideComments  >> ytHideRecVideos
       >> ytHideThumbnails >> ytBlurThumbnails >> ytHideSubs      >> ytHideExplore
       >> ytHideTopBar    >> ytDisableEndCards >> ytBlackWhiteMode >> ytDisableAutoplay;

    in >> ttHideExplore >> ttHideLive >> ttHideComments >> ttHideSearch >> ttBlackWhiteMode;

    in >> igHideStories >> igHideReels >> igHideExplore
       >> igHideComments >> igHideSuggested >> igBlackWhiteMode;

    in.close();

    // Push loaded values into engine structs immediately
    SyncSettingsToEngine();
}

static bool aiSettingsLoaded = false;

// ─────────────────────────────────────────────────────────────────────────────
// HELPER: rounded rect path
// ─────────────────────────────────────────────────────────────────────────────
static GraphicsPath* GetAiRoundRectPath(RectF rect, int radius) {
    GraphicsPath* path = new GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X,                    rect.Y,                    d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d,   rect.Y,                    d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d,   rect.Y + rect.Height - d,  d, d,   0.0f, 90.0f);
    path->AddArc(rect.X,                    rect.Y + rect.Height - d,  d, d,  90.0f, 90.0f);
    path->CloseFigure();
    return path;
}

// ─────────────────────────────────────────────────────────────────────────────
// DRAW
// ─────────────────────────────────────────────────────────────────────────────
void DrawAiFilterTab(Graphics& g, float cx, float cy, float cw, float ch) {
    ai_cX = cx; ai_cY = cy; ai_cW = cw; ai_cH = ch;

    if (!aiSettingsLoaded) {
        LoadAiSettings();
        aiSettingsLoaded = true;
    }

    FontFamily ff(L"Segoe UI");
    Font fTitle      (&ff, 24, FontStyleBold,    UnitPixel);
    Font fSubTitle   (&ff, 20, FontStyleBold,    UnitPixel);
    Font fNorm       (&ff, 15, FontStyleRegular, UnitPixel);
    Font fBold       (&ff, 16, FontStyleBold,    UnitPixel);
    Font fSmallItalic(&ff, 13, FontStyleItalic,  UnitPixel);
    Font fTiny       (&ff, 12, FontStyleRegular, UnitPixel);
    FontFamily ffi(L"Segoe MDL2 Assets");
    Font fIcon      (&ffi, 20, FontStyleRegular, UnitPixel);
    Font fSmallIcon (&ffi, 14, FontStyleRegular, UnitPixel);

    SolidBrush bWhite(AiClrWhite), bDark(AiClrDark), bGray(AiClrGrayText);
    SolidBrush bTeal(AiClrTeal),   bBg(AiClrBg);
    Pen        pBorder(AiClrBorder, 1.5f);
    StringFormat fL; fL.SetAlignment(StringAlignmentNear);   fL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    float bX = cx + 40.0f;
    float bY = cy + 20.0f;

    // ── HELPER: toggle switch ────────────────────────────────────────────────
    auto drawToggle = [&](float x, float y, const wchar_t* txt, const wchar_t* desc, bool state) {
        RectF trackR(x, y + 2.0f, 36.0f, 18.0f);
        GraphicsPath* tp = GetAiRoundRectPath(trackR, 9);
        SolidBrush tBg(state ? (isAiEngineActive ? AiClrGrayText : AiClrTeal)
                              : Color(255, 220, 220, 220));
        g.FillPath(&tBg, tp);
        if (!state) g.DrawPath(&pBorder, tp);
        delete tp;

        float thumbX = state ? (x + 36.0f - 16.0f) : (x + 2.0f);
        SolidBrush thBg(state ? AiClrWhite : AiClrGrayText);
        g.FillEllipse(&thBg, thumbX, y + 3.0f, 14.0f, 14.0f);

        SolidBrush textBrush(isAiEngineActive ? AiClrGrayText : AiClrDark);
        g.DrawString(txt, -1, &fNorm, RectF(x + 45.0f, y - 2.0f, 300.0f, 22.0f), &fL, &textBrush);

        if (desc && wcslen(desc) > 0)
            g.DrawString(desc, -1, &fTiny, RectF(x + 45.0f, y + 20.0f, 500.0f, 20.0f), &fL, &bGray);
    };

    // ════════════════════════════════════════════════════════════════════════
    // VIEW 0: MAIN DASHBOARD
    // ════════════════════════════════════════════════════════════════════════
    if (currentAppBlockView == 0) {

        // AI Engine master button
        RectF startBtn(bX, bY, 180.0f, 40.0f);
        SolidBrush sb(isAiEngineActive ? AiClrRed : AiClrGreen);
        GraphicsPath* sbp = GetAiRoundRectPath(startBtn, 4);
        g.FillPath(&sb, sbp); delete sbp;
        g.DrawString(isAiEngineActive ? L"Stop AI Engine" : L"Start AI Engine",
                     -1, &fBold, startBtn, &fC, &bWhite);

        SolidBrush activeTextBrush(isAiEngineActive ? AiClrGrayText : AiClrDark);
        g.DrawString(isAiEngineActive
                     ? L"Status: AI Background Engine is ACTIVE"
                     : L"Status: AI Engine is OFF",
                     -1, &fBold,
                     RectF(bX + 200.0f, bY, 400.0f, 40.0f), &fL,
                     isAiEngineActive ? &bGray : &bDark);

        bY += 60.0f;
        g.DrawLine(&pBorder, bX, bY, cx + cw - 40.0f, bY); bY += 15.0f;

        // Female detection section
        g.DrawString(L"Smart Female Detection (Strict Filter):",
                     -1, &fSubTitle, RectF(bX, bY, 400.0f, 30.0f), &fL, &activeTextBrush);
        g.DrawString(L"Dynamically detects and blurs females in real-time without blocking the site.",
                     -1, &fSmallItalic, RectF(bX, bY + 28.0f, 600.0f, 20.0f), &fL, &bGray);
        bY += 60.0f;

        drawToggle(bX, bY, L"Detect & Blur Females in Browsing",
                   L"Applies blur on images containing females across all websites.",
                   cbFemaleDetectWeb);
        bY += 45.0f;
        drawToggle(bX, bY, L"Detect & Blur Females in Videos (Live)",
                   L"Real-time processing to blur females in playing videos (CPU intensive).",
                   cbFemaleDetectVideo);

        bY += 40.0f;
        g.DrawLine(&pBorder, bX, bY, cx + cw - 40.0f, bY); bY += 15.0f;

        // Sensitivity dropdown
        g.DrawString(L"AI Sensitivity Level:", -1, &fBold,
                     RectF(bX, bY, 150.0f, 35.0f), &fL, &activeTextBrush);

        auto drawBeautifulDropdown = [&](float x, float y, float w, float h,
                                         wstring text, bool hover) {
            RectF r(x, y, w, h);
            GraphicsPath* p = GetAiRoundRectPath(r, 4);
            SolidBrush dBg(hover ? AiClrBgHover : AiClrWhite);
            g.FillPath(&dBg, p); g.DrawPath(&pBorder, p); delete p;
            g.DrawString(text.c_str(), -1, &fNorm,
                         RectF(x + 10, y, w - 35, h), &fL,
                         isAiEngineActive ? &bGray : &bDark);
            g.DrawLine(&pBorder, x + w - 30, y, x + w - 30, y + h);
            g.DrawString(L"\xE70D", -1, &fSmallIcon,
                         RectF(x + w - 30, y, 30, h), &fC, &bGray);
        };

        drawBeautifulDropdown(bX + 160.0f, bY + 2.0f, 170.0f, 32.0f,
                              aiSensitivityModes[aiSensitivityIdx], hoverAiSensDrop);

        bY += 50.0f;
        g.DrawLine(&pBorder, bX, bY, cx + cw - 40.0f, bY); bY += 15.0f;

        // In-App Blocking buttons
        g.DrawString(L"In App Blocking", -1, &fSubTitle,
                     RectF(bX, bY, 400.0f, 30.0f), &fL, &activeTextBrush);
        bY += 40.0f;

        auto drawAppButton = [&](float y, const wchar_t* title, const wchar_t* desc,
                                  const wchar_t* iconCode, Color iconColor, bool hover) {
            RectF r(bX, y, cw - 80.0f, 60.0f);
            GraphicsPath* p = GetAiRoundRectPath(r, 6);
            SolidBrush btnBg(hover ? AiClrBgHover : AiClrBg);
            g.FillPath(&btnBg, p); g.DrawPath(&pBorder, p); delete p;

            SolidBrush icBr(iconColor);
            g.FillRectangle(&icBr, bX + 15.0f, y + 15.0f, 30.0f, 30.0f);
            g.DrawString(iconCode, -1, &fIcon,
                         RectF(bX + 15.0f, y + 15.0f, 30.0f, 30.0f), &fC, &bWhite);

            g.DrawString(title, -1, &fBold,
                         RectF(bX + 60.0f, y + 10.0f, 300.0f, 20.0f), &fL, &activeTextBrush);
            g.DrawString(desc, -1, &fNorm,
                         RectF(bX + 60.0f, y + 30.0f, 500.0f, 20.0f), &fL, &bGray);
            g.DrawString(L"\xE76C", -1, &fIcon,
                         RectF(r.X + r.Width - 40.0f, y, 40.0f, 60.0f), &fC, &bGray);
        };

        drawAppButton(bY, L"Youtube",
                      L"Block specific YouTube features like shorts, comments, or more.",
                      L"\xE19D", Color(255, 200, 30, 30), hBtnYoutube);
        bY += 70.0f;
        drawAppButton(bY, L"TikTok",
                      L"Block specific TikTok features like comments, explore or more.",
                      L"\xE7F6", Color(255, 20, 20, 20), hBtnTikTok);
        bY += 70.0f;
        drawAppButton(bY, L"Instagram",
                      L"Block specific Instagram features like reels, comments, or more.",
                      L"\xE7B3", Color(255, 190, 40, 140), hBtnInstagram);

        // Sensitivity dropdown overlay (drawn last to appear on top)
        if (isAiSensDropOpen && !isAiEngineActive) {
            float dropY = cy + 20.0f + 60.0f + 15.0f + 30.0f + 20.0f
                        + 60.0f + 45.0f + 40.0f + 15.0f;
            RectF dR(bX + 160.0f, dropY + 36.0f, 170.0f, 4 * 32.0f);
            GraphicsPath* dp = GetAiRoundRectPath(dR, 4);
            g.FillPath(&bWhite, dp); g.DrawPath(&pBorder, dp); delete dp;
            for (int i = 0; i < 4; i++) {
                SolidBrush hB(hoverAiSensOptIdx == i ? AiClrBgHover : AiClrWhite);
                g.FillRectangle(&hB, dR.X + 1, dR.Y + (i * 32.0f) + 1, dR.Width - 2, 30.0f);
                g.DrawString(aiSensitivityModes[i].c_str(), -1, &fNorm,
                             RectF(dR.X + 10, dR.Y + (i * 32), dR.Width - 10, 32), &fL, &bDark);
            }
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // VIEW 1: YOUTUBE SETTINGS
    // ════════════════════════════════════════════════════════════════════════
    else if (currentAppBlockView == 1) {
        // Breadcrumb header
        g.DrawString(L"In App Blocking", -1, &fNorm,
                     RectF(bX, bY, 120.0f, 30.0f), &fL, hoverBackBtn ? &bTeal : &bGray);
        g.DrawString(L"\xE76C", -1, &fSmallIcon,
                     RectF(bX + 120.0f, bY, 20.0f, 30.0f), &fC, &bGray);
        g.DrawString(L"Youtube", -1, &fTitle,
                     RectF(bX + 140.0f, bY, 200.0f, 30.0f), &fL, &bDark);
        bY += 40.0f;
        g.DrawString(L"Block specific YouTube features like shorts, comments, or more.",
                     -1, &fNorm, RectF(bX, bY, cw - 80.0f, 20.0f), &fL, &bDark);
        bY += 30.0f;

        // Scrollable list
        float listStartY  = bY;
        float viewH       = ch - listStartY - 20.0f;
        Region orgRegion; g.GetClip(&orgRegion);
        g.SetClip(RectF(cx, listStartY, cw, viewH));

        float itemY = listStartY - (float)scrollOffsetYt;

        drawToggle(bX, itemY, L"Hide Home Page",           L"",                                                     ytHideHome);         itemY += 40.0f;
        drawToggle(bX, itemY, L"Hide Shorts",              L"",                                                     ytHideShorts);       itemY += 40.0f;
        drawToggle(bX, itemY, L"Hide Comments",            L"",                                                     ytHideComments);     itemY += 40.0f;
        drawToggle(bX, itemY, L"Hide Recommended Videos",  L"",                                                     ytHideRecVideos);    itemY += 40.0f;
        drawToggle(bX, itemY, L"Hide Thumbnails",          L"",                                                     ytHideThumbnails);   itemY += 40.0f;
        drawToggle(bX, itemY, L"Blur Thumbnails",          L"",                                                     ytBlurThumbnails);   itemY += 40.0f;
        drawToggle(bX, itemY, L"Hide Subscriptions",       L"",                                                     ytHideSubs);         itemY += 40.0f;
        drawToggle(bX, itemY, L"Hide Explore",             L"",                                                     ytHideExplore);      itemY += 40.0f;
        drawToggle(bX, itemY, L"Hide Top Bar",             L"Hide the top bar including search box",                ytHideTopBar);       itemY += 55.0f;
        drawToggle(bX, itemY, L"Disable End Cards",        L"Hide video recommendations shown at the end of videos",ytDisableEndCards);   itemY += 55.0f;
        drawToggle(bX, itemY, L"Black & White Mode",       L"Use only black and white colors for any YouTube pages",ytBlackWhiteMode);   itemY += 55.0f;
        drawToggle(bX, itemY, L"Disable Autoplay",         L"Stop autoplaying the next video",                      ytDisableAutoplay);  itemY += 55.0f;

        float totalListHeight = itemY + (float)scrollOffsetYt - listStartY;
        maxScrollYt = max(0, (int)(totalListHeight - viewH));

        g.SetClip(&orgRegion);

        // Scrollbar
        if (maxScrollYt > 0) {
            float sbH = viewH * (viewH / totalListHeight);
            float sbY = listStartY + ((float)scrollOffsetYt / (float)maxScrollYt) * (viewH - sbH);
            SolidBrush sbBrush(Color(255, 200, 200, 200));
            g.FillRectangle(&sbBrush, cx + cw - 15.0f, sbY, 6.0f, sbH);
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // VIEW 2: TIKTOK SETTINGS
    // ════════════════════════════════════════════════════════════════════════
    else if (currentAppBlockView == 2) {
        g.DrawString(L"In App Blocking", -1, &fNorm,
                     RectF(bX, bY, 120.0f, 30.0f), &fL, hoverBackBtn ? &bTeal : &bGray);
        g.DrawString(L"\xE76C", -1, &fSmallIcon,
                     RectF(bX + 120.0f, bY, 20.0f, 30.0f), &fC, &bGray);
        g.DrawString(L"TikTok", -1, &fTitle,
                     RectF(bX + 140.0f, bY, 200.0f, 30.0f), &fL, &bDark);
        bY += 40.0f;
        g.DrawString(L"Block specific TikTok features like comments, explore or more.",
                     -1, &fNorm, RectF(bX, bY, cw - 80.0f, 20.0f), &fL, &bDark);
        bY += 40.0f;

        drawToggle(bX, bY, L"Hide Explore",       L"", ttHideExplore);   bY += 45.0f;
        drawToggle(bX, bY, L"Hide Live",           L"", ttHideLive);      bY += 45.0f;
        drawToggle(bX, bY, L"Hide Comments",       L"", ttHideComments);  bY += 45.0f;
        drawToggle(bX, bY, L"Hide Search",         L"", ttHideSearch);    bY += 45.0f;
        drawToggle(bX, bY, L"Black & White Mode",
                   L"Use only black and white colors for any TikTok pages",
                   ttBlackWhiteMode);
    }

    // ════════════════════════════════════════════════════════════════════════
    // VIEW 3: INSTAGRAM SETTINGS
    // ════════════════════════════════════════════════════════════════════════
    else if (currentAppBlockView == 3) {
        g.DrawString(L"In App Blocking", -1, &fNorm,
                     RectF(bX, bY, 120.0f, 30.0f), &fL, hoverBackBtn ? &bTeal : &bGray);
        g.DrawString(L"\xE76C", -1, &fSmallIcon,
                     RectF(bX + 120.0f, bY, 20.0f, 30.0f), &fC, &bGray);
        g.DrawString(L"Instagram", -1, &fTitle,
                     RectF(bX + 140.0f, bY, 200.0f, 30.0f), &fL, &bDark);
        bY += 40.0f;
        g.DrawString(L"Block specific Instagram features like reels, comments, or more.",
                     -1, &fNorm, RectF(bX, bY, cw - 80.0f, 20.0f), &fL, &bDark);
        bY += 40.0f;

        drawToggle(bX, bY, L"Hide Stories",          L"", igHideStories);   bY += 45.0f;
        drawToggle(bX, bY, L"Hide Reels",            L"", igHideReels);     bY += 45.0f;
        drawToggle(bX, bY, L"Hide Explore",          L"", igHideExplore);   bY += 45.0f;
        drawToggle(bX, bY, L"Hide Comments",         L"", igHideComments);  bY += 45.0f;
        drawToggle(bX, bY, L"Hide Suggested for You",L"", igHideSuggested); bY += 45.0f;
        drawToggle(bX, bY, L"Black & White Mode",
                   L"Use only black and white colors for any Instagram pages",
                   igBlackWhiteMode);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MOUSE MOVE
// ─────────────────────────────────────────────────────────────────────────────
void ProcessAiFilterMouseMove(float x, float y) {
    float bX = ai_cX + 40.0f;
    float bY = ai_cY + 20.0f;

    // Reset all hover flags
    hoverAiEngineBtn = hoverAiSensDrop = false;
    hCbAiImageBlur = hCbFemaleDetectWeb = hCbFemaleDetectVideo = false;
    hBtnYoutube = hBtnTikTok = hBtnInstagram = hoverBackBtn = false;
    hYtHideHome = hYtHideShorts = hYtHideComments = hYtHideRecVideos = false;
    hYtHideThumbnails = hYtBlurThumbnails = hYtHideSubs = hYtHideExplore = false;
    hYtHideTopBar = hYtDisableEndCards = hYtBlackWhiteMode = hYtDisableAutoplay = false;
    hTtHideExplore = hTtHideLive = hTtHideComments = hTtHideSearch = hTtBlackWhiteMode = false;
    hIgHideStories = hIgHideReels = hIgHideExplore = hIgHideComments = hIgHideSuggested = hIgBlackWhiteMode = false;

    if (currentAppBlockView == 0) {
        hoverAiEngineBtn = RectF(bX, bY, 180.0f, 40.0f).Contains(x, y);

        float midY = bY + 60.0f + 30.0f + 60.0f;
        hCbFemaleDetectWeb   = RectF(bX, midY,         350.0f, 40.0f).Contains(x, y); midY += 45.0f;
        hCbFemaleDetectVideo = RectF(bX, midY,         350.0f, 40.0f).Contains(x, y);

        float dropY = midY + 40.0f + 15.0f;
        if (isAiSensDropOpen) {
            hoverAiSensOptIdx = -1;
            RectF dR(bX + 160.0f, dropY + 36.0f, 170.0f, 4 * 32.0f);
            for (int i = 0; i < 4; i++)
                if (RectF(dR.X, dR.Y + (i * 32.0f), dR.Width, 32.0f).Contains(x, y))
                    hoverAiSensOptIdx = i;
            return;
        }
        hoverAiSensDrop = RectF(bX + 160.0f, dropY + 2.0f, 170.0f, 32.0f).Contains(x, y);

        float btnY = dropY + 50.0f + 15.0f + 30.0f + 40.0f;
        hBtnYoutube   = RectF(bX, btnY, ai_cW - 80.0f, 60.0f).Contains(x, y); btnY += 70.0f;
        hBtnTikTok    = RectF(bX, btnY, ai_cW - 80.0f, 60.0f).Contains(x, y); btnY += 70.0f;
        hBtnInstagram = RectF(bX, btnY, ai_cW - 80.0f, 60.0f).Contains(x, y);
    }
    else if (currentAppBlockView == 1) {
        hoverBackBtn = RectF(bX, bY, 120.0f, 30.0f).Contains(x, y);
        float itemY = bY + 70.0f - (float)scrollOffsetYt;
        hYtHideHome       = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 40.0f;
        hYtHideShorts     = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 40.0f;
        hYtHideComments   = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 40.0f;
        hYtHideRecVideos  = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 40.0f;
        hYtHideThumbnails = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 40.0f;
        hYtBlurThumbnails = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 40.0f;
        hYtHideSubs       = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 40.0f;
        hYtHideExplore    = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 40.0f;
        hYtHideTopBar     = RectF(bX, itemY, 300.0f, 40.0f).Contains(x, y); itemY += 55.0f;
        hYtDisableEndCards= RectF(bX, itemY, 300.0f, 40.0f).Contains(x, y); itemY += 55.0f;
        hYtBlackWhiteMode = RectF(bX, itemY, 300.0f, 40.0f).Contains(x, y); itemY += 55.0f;
        hYtDisableAutoplay= RectF(bX, itemY, 300.0f, 40.0f).Contains(x, y);
    }
    else if (currentAppBlockView == 2) {
        hoverBackBtn  = RectF(bX, bY, 120.0f, 30.0f).Contains(x, y);
        float itemY   = bY + 80.0f;
        hTtHideExplore  = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 45.0f;
        hTtHideLive     = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 45.0f;
        hTtHideComments = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 45.0f;
        hTtHideSearch   = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 45.0f;
        hTtBlackWhiteMode = RectF(bX, itemY, 300.0f, 40.0f).Contains(x, y);
    }
    else if (currentAppBlockView == 3) {
        hoverBackBtn  = RectF(bX, bY, 120.0f, 30.0f).Contains(x, y);
        float itemY   = bY + 80.0f;
        hIgHideStories  = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 45.0f;
        hIgHideReels    = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 45.0f;
        hIgHideExplore  = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 45.0f;
        hIgHideComments = RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 45.0f;
        hIgHideSuggested= RectF(bX, itemY, 300.0f, 22.0f).Contains(x, y); itemY += 45.0f;
        hIgBlackWhiteMode = RectF(bX, itemY, 300.0f, 40.0f).Contains(x, y);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MOUSE CLICK
// ─────────────────────────────────────────────────────────────────────────────
void ProcessAiFilterMouseClick(float x, float y) {
    if (currentAppBlockView == 0) {

        if (hoverAiEngineBtn) {
            isAiEngineActive = !isAiEngineActive;
            SaveAiSettings();
            return;
        }

        if (isAiSensDropOpen) {
            if (hoverAiSensOptIdx != -1) aiSensitivityIdx = hoverAiSensOptIdx;
            isAiSensDropOpen = false;
            SaveAiSettings();
            return;
        }

        if (hoverAiSensDrop && !isAiEngineActive) {
            isAiSensDropOpen = true;
            return;
        }

        if (hBtnYoutube)   { currentAppBlockView = 1; scrollOffsetYt = 0; return; }
        if (hBtnTikTok)    { currentAppBlockView = 2; return; }
        if (hBtnInstagram) { currentAppBlockView = 3; return; }

        // Female detect toggles (only when engine is OFF)
        if (!isAiEngineActive) {
            if (hCbFemaleDetectWeb)   { cbFemaleDetectWeb   = !cbFemaleDetectWeb;   SaveAiSettings(); }
            if (hCbFemaleDetectVideo) { cbFemaleDetectVideo = !cbFemaleDetectVideo; SaveAiSettings(); }
        }
    }
    else if (currentAppBlockView == 1) {  // YouTube
        if (hoverBackBtn) { currentAppBlockView = 0; return; }
        if (isAiEngineActive) return; // Locked while engine is active

        bool changed = false;
        auto tog = [&](bool& s, bool h) { if (h) { s = !s; changed = true; } };

        tog(ytHideHome,       hYtHideHome);
        tog(ytHideShorts,     hYtHideShorts);
        tog(ytHideComments,   hYtHideComments);
        tog(ytHideRecVideos,  hYtHideRecVideos);
        tog(ytHideThumbnails, hYtHideThumbnails);
        tog(ytBlurThumbnails, hYtBlurThumbnails);
        tog(ytHideSubs,       hYtHideSubs);
        tog(ytHideExplore,    hYtHideExplore);
        tog(ytHideTopBar,     hYtHideTopBar);
        tog(ytDisableEndCards,hYtDisableEndCards);
        tog(ytBlackWhiteMode, hYtBlackWhiteMode);
        tog(ytDisableAutoplay,hYtDisableAutoplay);

        if (changed) { SyncSettingsToEngine(); SaveAiSettings(); }
    }
    else if (currentAppBlockView == 2) {  // TikTok
        if (hoverBackBtn) { currentAppBlockView = 0; return; }
        if (isAiEngineActive) return;

        bool changed = false;
        auto tog = [&](bool& s, bool h) { if (h) { s = !s; changed = true; } };

        tog(ttHideExplore,   hTtHideExplore);
        tog(ttHideLive,      hTtHideLive);
        tog(ttHideComments,  hTtHideComments);
        tog(ttHideSearch,    hTtHideSearch);
        tog(ttBlackWhiteMode,hTtBlackWhiteMode);

        if (changed) { SyncSettingsToEngine(); SaveAiSettings(); }
    }
    else if (currentAppBlockView == 3) {  // Instagram
        if (hoverBackBtn) { currentAppBlockView = 0; return; }
        if (isAiEngineActive) return;

        bool changed = false;
        auto tog = [&](bool& s, bool h) { if (h) { s = !s; changed = true; } };

        tog(igHideStories,   hIgHideStories);
        tog(igHideReels,     hIgHideReels);
        tog(igHideExplore,   hIgHideExplore);
        tog(igHideComments,  hIgHideComments);
        tog(igHideSuggested, hIgHideSuggested);
        tog(igBlackWhiteMode,hIgBlackWhiteMode);

        if (changed) { SyncSettingsToEngine(); SaveAiSettings(); }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SCROLL
// ─────────────────────────────────────────────────────────────────────────────
void ProcessAiFilterMouseWheel(float x, float y, int delta) {
    if (currentAppBlockView == 1) {
        const int scrollSpeed = 30;
        if (delta > 0) {
            scrollOffsetYt -= scrollSpeed;
            if (scrollOffsetYt < 0) scrollOffsetYt = 0;
        } else {
            scrollOffsetYt += scrollSpeed;
            if (scrollOffsetYt > maxScrollYt) scrollOffsetYt = maxScrollYt;
        }
    }
}
