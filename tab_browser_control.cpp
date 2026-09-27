// tab_browser_control.cpp — Browser Content Control Panel
// Tab 9 এর main app page (যেটা সাদা/ফাঁকা ছিল) এখন এই control panel দেখাবে।
// Toggle switch গুলো rasfocus_ai_data.txt এ state save করে;
// browser window খোলা থাকলে WM_APP+50 message পাঠিয়ে সব tab reload করে।

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "tab_browser_control.h"
#include <windows.h>
#include <gdiplus.h>
#include <fstream>
#include <string>
#include <vector>
#include <functional>
using namespace Gdiplus;

// ─────────────────────────────────────────────────────────────────────────────
// CUSTOM MESSAGE: sent to all RasBrowserWnd windows to reload tabs
// ─────────────────────────────────────────────────────────────────────────────
#define WM_RAS_BROWSER_RELOAD (WM_APP + 50)

// ─────────────────────────────────────────────────────────────────────────────
// SHARED STATE  (mirrors GetAiInjectScript's read order exactly)
// rasfocus_ai_data.txt format (space-separated booleans/int, one big line):
//   isAiEngineActive  cbAiImageBlur  cbFemaleDetectWeb  cbFemaleDetectVideo  aiSensitivityIdx
//   ytHideHome ytHideShorts ytHideComments ytHideRecVideos ytHideThumbnails
//   ytBlurThumbnails ytHideSubs ytHideExplore ytHideTopBar ytDisableEndCards
//   ytBlackWhiteMode ytDisableAutoplay
//   ttHideExplore ttHideLive ttHideComments ttHideSearch ttBlackWhiteMode
//   igHideStories igHideReels igHideExplore igHideComments igHideSuggested igBlackWhiteMode
// ─────────────────────────────────────────────────────────────────────────────
struct BcState {
    // AI (kept but not exposed in this UI — preserved across saves)
    bool aiActive=false, aiBlur=false, aiWeb=false, aiVideo=false;
    int  aiSens=0;
    // YouTube
    bool ytHome=false, ytShorts=false, ytComments=false, ytRecVideos=false,
         ytThumbs=false, ytBlurThumbs=false, ytSubs=false, ytExplore=false,
         ytTopBar=false, ytEndCards=false, ytGrayscale=false, ytAutoplay=false;
    // TikTok
    bool ttExplore=false, ttLive=false, ttComments=false, ttSearch=false, ttGrayscale=false;
    // Instagram / Facebook (ig* maps to instagram, also applied to facebook via css)
    bool igStories=false, igReels=false, igExplore=false, igComments=false,
         igSuggested=false, igGrayscale=false;
};
static BcState s_st;

// ─────────────────────────────────────────────────────────────────────────────
// FILE I/O
// ─────────────────────────────────────────────────────────────────────────────
static void SaveState() {
    std::wofstream out(L"rasfocus_ai_data.txt");
    if (!out) return;
    out << s_st.aiActive    << L" " << s_st.aiBlur  << L" "
        << s_st.aiWeb       << L" " << s_st.aiVideo << L" " << s_st.aiSens  << L"\n"
        << s_st.ytHome      << L" " << s_st.ytShorts    << L" " << s_st.ytComments << L" "
        << s_st.ytRecVideos << L" " << s_st.ytThumbs    << L" " << s_st.ytBlurThumbs << L" "
        << s_st.ytSubs      << L" " << s_st.ytExplore   << L" " << s_st.ytTopBar     << L" "
        << s_st.ytEndCards  << L" " << s_st.ytGrayscale << L" " << s_st.ytAutoplay   << L"\n"
        << s_st.ttExplore   << L" " << s_st.ttLive     << L" " << s_st.ttComments << L" "
        << s_st.ttSearch    << L" " << s_st.ttGrayscale << L"\n"
        << s_st.igStories   << L" " << s_st.igReels    << L" " << s_st.igExplore  << L" "
        << s_st.igComments  << L" " << s_st.igSuggested<< L" " << s_st.igGrayscale << L"\n";
}

void BrowserControlLoadState() {
    std::wifstream in(L"rasfocus_ai_data.txt");
    if (!in) { SaveState(); return; }
    in >> s_st.aiActive >> s_st.aiBlur >> s_st.aiWeb >> s_st.aiVideo >> s_st.aiSens;
    in >> s_st.ytHome >> s_st.ytShorts >> s_st.ytComments >> s_st.ytRecVideos >> s_st.ytThumbs
       >> s_st.ytBlurThumbs >> s_st.ytSubs >> s_st.ytExplore >> s_st.ytTopBar
       >> s_st.ytEndCards >> s_st.ytGrayscale >> s_st.ytAutoplay;
    in >> s_st.ttExplore >> s_st.ttLive >> s_st.ttComments >> s_st.ttSearch >> s_st.ttGrayscale;
    in >> s_st.igStories >> s_st.igReels >> s_st.igExplore >> s_st.igComments
       >> s_st.igSuggested >> s_st.igGrayscale;
}

void BrowserControlRefreshBrowserWindows() {
    HWND hw = NULL;
    while ((hw = FindWindowExW(NULL, hw, L"RasBrowserWnd", NULL)) != NULL)
        PostMessage(hw, WM_RAS_BROWSER_RELOAD, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// TOGGLE DESCRIPTOR
// ─────────────────────────────────────────────────────────────────────────────
struct Toggle {
    const wchar_t* label;
    const wchar_t* desc;   // short hint shown beside label
    bool*          state;
};

static std::vector<Toggle> BuildYTToggles() {
    return {
        { L"Hide Shorts",          L"Shorts shelf & sidebar link",   &s_st.ytShorts     },
        { L"Hide Home Feed",       L"Homepage video grid",           &s_st.ytHome       },
        { L"Hide Comments",        L"Comment section on videos",     &s_st.ytComments   },
        { L"Hide Recommended",     L"Side-panel recommendations",    &s_st.ytRecVideos  },
        { L"Hide Thumbnails",      L"All video thumbnails",          &s_st.ytThumbs     },
        { L"Blur Thumbnails",      L"Blur thumbnails instead of hide",&s_st.ytBlurThumbs},
        { L"Hide Subscriptions",   L"Subscriptions nav link",        &s_st.ytSubs       },
        { L"Hide Explore/Trending",L"Trending / Explore sidebar",    &s_st.ytExplore    },
        { L"Hide Top Bar",         L"YouTube header / masthead",     &s_st.ytTopBar     },
        { L"Disable End Cards",    L"End-screen video cards",        &s_st.ytEndCards   },
        { L"Disable Autoplay",     L"Auto-play next video",          &s_st.ytAutoplay   },
        { L"Grayscale Mode",       L"Full page black & white",       &s_st.ytGrayscale  },
    };
}
static std::vector<Toggle> BuildFBToggles() {
    // Facebook (Reels, Stories, Suggested) mapped via instagram-style selectors
    return {
        { L"Hide Reels",           L"Facebook Reels feed",           &s_st.igReels      },
        { L"Hide Stories",         L"Stories bar at top of feed",    &s_st.igStories    },
        { L"Hide Suggested",       L"Suggested friends / groups",    &s_st.igSuggested  },
        { L"Hide Explore",         L"Explore / Marketplace link",    &s_st.igExplore    },
        { L"Hide Comments",        L"Comment sections",              &s_st.igComments   },
        { L"Grayscale Mode",       L"Full page black & white",       &s_st.igGrayscale  },
    };
}
static std::vector<Toggle> BuildIGToggles() {
    return {
        { L"Hide Reels",           L"Reels nav link & reel feed",    &s_st.igReels      },
        { L"Hide Stories",         L"Stories bar at top",            &s_st.igStories    },
        { L"Hide Explore",         L"Explore grid / magnifier link", &s_st.igExplore    },
        { L"Hide Comments",        L"Comment sections",              &s_st.igComments   },
        { L"Hide Suggested",       L"Suggested accounts sidebar",    &s_st.igSuggested  },
        { L"Grayscale Mode",       L"Full page black & white",       &s_st.igGrayscale  },
    };
}
static std::vector<Toggle> BuildTTToggles() {
    return {
        { L"Hide Explore",         L"Discover / Explore page",       &s_st.ttExplore    },
        { L"Hide Live",            L"LIVE section in nav",           &s_st.ttLive       },
        { L"Hide Comments",        L"Comment section on videos",     &s_st.ttComments   },
        { L"Hide Search",          L"Search bar",                    &s_st.ttSearch     },
        { L"Grayscale Mode",       L"Full page black & white",       &s_st.ttGrayscale  },
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// LAYOUT HELPERS
// ─────────────────────────────────────────────────────────────────────────────
struct Section {
    const wchar_t*      title;
    const wchar_t*      emoji;
    Color               accent;
    std::vector<Toggle> toggles;
};

static std::vector<Section> BuildSections() {
    return {
        { L"YouTube",   L"\u25B6",   Color(255, 220, 60,  60),  BuildYTToggles() },
        { L"Facebook",  L"f",        Color(255, 66, 103, 178),  BuildFBToggles() },
        { L"Instagram", L"\u2665",   Color(255, 200, 60, 130),  BuildIGToggles() },
        { L"TikTok",    L"\u266B",   Color(255,  30, 215, 180), BuildTTToggles() },
    };
}

// Toggle dimensions
static const float TG_W    = 44.0f;  // toggle pill width
static const float TG_H    = 22.0f;  // toggle pill height
static const float ROW_H   = 36.0f;  // height per toggle row
static const float SEC_HDR = 38.0f;  // section header height
static const float PAD_L   = 28.0f;  // left padding inside content
static const float COLS    = 2.0f;   // 2-column layout

// Hit-test rectangles for toggle pills (rebuilt each draw)
struct HitRect { RectF r; bool* state; };
static std::vector<HitRect> s_hitRects;
static int s_hoverIdx = -1;  // index into s_hitRects

// Draw one toggle pill
static void DrawToggle(Graphics& g, float x, float y, bool on, Color accent, bool hover) {
    Color bgOn  = accent;
    Color bgOff = hover ? Color(255, 200, 200, 200) : Color(255, 210, 215, 220);
    SolidBrush bg(on ? bgOn : bgOff);
    GraphicsPath p;
    float r = TG_H / 2.0f;
    p.AddArc(x, y, TG_H, TG_H, 90, 180);
    p.AddArc(x + TG_W - TG_H, y, TG_H, TG_H, 270, 180);
    p.CloseFigure();
    g.FillPath(&bg, &p);

    float cx = on ? (x + TG_W - r - 2.0f) : (x + r + 2.0f);
    float cy = y + r;
    SolidBrush knob(Color(255, 255, 255, 255));
    float kr = r - 3.0f;
    g.FillEllipse(&knob, cx - kr, cy - kr, kr * 2.0f, kr * 2.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN DRAW
// ─────────────────────────────────────────────────────────────────────────────
void DrawBrowserControlTab(Graphics& g, float cX, float cY, float cW, float cH) {
    s_hitRects.clear();

    // Background
    SolidBrush bgBrush(Color(255, 245, 248, 250));
    g.FillRectangle(&bgBrush, cX, cY, cW, cH);

    FontFamily ff(L"Segoe UI");
    Font fHeader(&ff, 13.5f, FontStyleBold,    UnitPixel);
    Font fLabel (&ff, 12.5f, FontStyleRegular, UnitPixel);
    Font fDesc  (&ff, 11.0f, FontStyleRegular, UnitPixel);
    Font fTitle (&ff, 18.0f, FontStyleBold,    UnitPixel);
    Font fSub   (&ff, 11.5f, FontStyleRegular, UnitPixel);

    SolidBrush brkDark (Color(255,  50,  50,  50));
    SolidBrush brkGray (Color(255, 130, 130, 130));
    SolidBrush brkWhite(Color(255, 255, 255, 255));
    StringFormat sfLeft, sfCenter;
    sfCenter.SetAlignment(StringAlignmentCenter);
    sfCenter.SetLineAlignment(StringAlignmentCenter);

    // ── Page title
    float ty = cY + 18.0f;
    SolidBrush teal(Color(255, 0, 140, 150));
    g.DrawString(L"Browser Content Controls", -1, &fTitle,
        PointF(cX + PAD_L, ty), &sfLeft, &teal);
    ty += 26.0f;
    g.DrawString(L"Toggle switches below hide distracting content in RasBrowser. "
                 L"Changes apply on next page load or tab refresh.",
        -1, &fSub, RectF(cX + PAD_L, ty, cW - PAD_L * 2.0f, 32.0f), &sfLeft, &brkGray);
    ty += 38.0f;

    // Thin separator
    Pen sep(Color(255, 220, 225, 230), 1.0f);
    g.DrawLine(&sep, cX + PAD_L, ty, cX + cW - PAD_L, ty);
    ty += 14.0f;

    auto sections = BuildSections();
    // Two-column layout: each section occupies one column width
    float colW = (cW - PAD_L * 2.0f - 16.0f) / 2.0f;

    float colXs[2] = { cX + PAD_L, cX + PAD_L + colW + 16.0f };
    float colYs[2] = { ty, ty };

    for (int si = 0; si < (int)sections.size(); si++) {
        auto& sec = sections[si];
        int col = si % 2;
        float x0  = colXs[col];
        float& y0 = colYs[col];

        // Section card background
        float cardH = SEC_HDR + (float)sec.toggles.size() * ROW_H + 12.0f;
        SolidBrush cardBg(Color(255, 255, 255, 255));
        Pen   cardBorder(Color(255, 228, 232, 236), 1.0f);
        // rounded rect via path
        {
            GraphicsPath cp;
            float cr = 10.0f;
            cp.AddArc(x0, y0, cr*2, cr*2, 180, 90);
            cp.AddArc(x0+colW-cr*2, y0, cr*2, cr*2, 270, 90);
            cp.AddArc(x0+colW-cr*2, y0+cardH-cr*2, cr*2, cr*2, 0, 90);
            cp.AddArc(x0, y0+cardH-cr*2, cr*2, cr*2, 90, 90);
            cp.CloseFigure();
            g.FillPath(&cardBg, &cp);
            g.DrawPath(&cardBorder, &cp);
        }

        // Accent strip left
        SolidBrush accentBrush(sec.accent);
        g.FillRectangle(&accentBrush, x0, y0 + 8.0f, 4.0f, cardH - 16.0f);

        // Section header
        float hdrY = y0 + 8.0f;
        // Emoji/icon circle
        SolidBrush iconCircle(sec.accent);
        g.FillEllipse(&iconCircle, x0 + 12.0f, hdrY + 3.0f, 26.0f, 26.0f);
        g.DrawString(sec.emoji, -1, &fHeader,
            RectF(x0 + 12.0f, hdrY + 3.0f, 26.0f, 26.0f), &sfCenter, &brkWhite);

        g.DrawString(sec.title, -1, &fHeader,
            PointF(x0 + 44.0f, hdrY + 7.0f), &sfLeft, &brkDark);

        // Count active
        int activeCount = 0;
        for (auto& t : sec.toggles) if (*t.state) activeCount++;
        if (activeCount > 0) {
            wchar_t cbuf[32]; wsprintfW(cbuf, L"%d ON", activeCount);
            SolidBrush acBrush(sec.accent);
            g.DrawString(cbuf, -1, &fDesc,
                PointF(x0 + colW - 52.0f, hdrY + 9.0f), &sfLeft, &acBrush);
        }

        float rowY = y0 + SEC_HDR;
        for (int ti = 0; ti < (int)sec.toggles.size(); ti++) {
            auto& t = sec.toggles[ti];
            // Alternate row shading
            if (ti % 2 == 0) {
                SolidBrush rowBg(Color(255, 249, 251, 253));
                g.FillRectangle(&rowBg, x0 + 5.0f, rowY, colW - 10.0f, ROW_H);
            }

            // Label
            g.DrawString(t.label, -1, &fLabel,
                PointF(x0 + 16.0f, rowY + (ROW_H - 15.0f) / 2.0f), &sfLeft, &brkDark);

            // Toggle
            float tgX = x0 + colW - TG_W - 10.0f;
            float tgY = rowY + (ROW_H - TG_H) / 2.0f;
            RectF hitR(tgX - 4.0f, rowY, TG_W + 8.0f, ROW_H);

            int hitIdx = (int)s_hitRects.size();
            s_hitRects.push_back({hitR, t.state});
            bool hover = (s_hoverIdx == hitIdx);

            DrawToggle(g, tgX, tgY, *t.state, sec.accent, hover);

            rowY += ROW_H;
        }
        // Small divider after last toggle before card bottom
        Pen rowDiv(Color(255, 235, 240, 245), 1.0f);
        g.DrawLine(&rowDiv, x0 + 8.0f, rowY, x0 + colW - 8.0f, rowY);

        y0 += cardH + 14.0f;
    }

    // Bottom note
    float maxY = max(colYs[0], colYs[1]);
    SolidBrush noteClr(Color(255, 160, 165, 170));
    g.DrawString(
        L"\u2139  Changes take effect when the browser navigates to a new page or you press Ctrl+R in RasBrowser.",
        -1, &fDesc,
        RectF(cX + PAD_L, maxY + 8.0f, cW - PAD_L * 2.0f, 24.0f),
        &sfLeft, &noteClr);
}

// ─────────────────────────────────────────────────────────────────────────────
// MOUSE CLICK
// ─────────────────────────────────────────────────────────────────────────────
void ProcessBrowserControlMouseClick(float x, float y, float /*cX*/, float /*cY*/, float /*cW*/, float /*cH*/) {
    for (auto& hr : s_hitRects) {
        if (hr.r.Contains(x, y)) {
            *hr.state = !(*hr.state);
            SaveState();
            BrowserControlRefreshBrowserWindows();
            return;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MOUSE MOVE (hover highlight)
// ─────────────────────────────────────────────────────────────────────────────
bool ProcessBrowserControlMouseMove(float x, float y, float /*cX*/, float /*cY*/, float /*cW*/, float /*cH*/) {
    int prev = s_hoverIdx;
    s_hoverIdx = -1;
    for (int i = 0; i < (int)s_hitRects.size(); i++) {
        if (s_hitRects[i].r.Contains(x, y)) { s_hoverIdx = i; break; }
    }
    return s_hoverIdx != prev;
}
