// tab_rasgram_native.cpp
// RasGram Desktop — Pure Win32 GDI+ Implementation (NO WebView2)
// Replaces the WebView2-based tab_rasgram.cpp for the 3rd sub-tab of Special Tab
//
// Design: Telegram-style dark messaging UI
//   Left panel  : Chat list with search
//   Right panel : Message thread + input bar
//   Login screen: Phone number + OTP flow
//
// Architecture: All drawing via GDI+, all input via WM_LBUTTONDOWN / WM_CHAR
// No external dependencies beyond GDI+ and Win32 API

// MSVC: must define these before any windows header
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>   // IStream, ISequentialStream — required before gdiplus.h
#include <ole2.h>     // PROPID and OLE types
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "tab_rasgram.h"

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <ctime>
#include <cmath>

using namespace Gdiplus;
using namespace std;

// ── Externals ─────────────────────────────────────────────────
extern HWND   hParentWnd;
extern float  g_scaleFactor;
extern string g_loggedInUserUid;
extern wstring g_loggedInName;

// ═══════════════════════════════════════════════════════════════
// COLOUR PALETTE  (Telegram-inspired dark teal)
// ═══════════════════════════════════════════════════════════════
namespace RgC {
    // Backgrounds
    const Color BgApp      (255, 14,  22,  33);   // deep navy
    const Color BgPanel    (255, 23,  33,  43);   // sidebar / panel
    const Color BgCard     (255, 28,  40,  51);   // chat-row bg
    const Color BgCardHov  (255, 35,  51,  65);   // hovered row
    const Color BgCardSel  (255, 32,  81,  109);  // selected row
    const Color BgInput    (255, 24,  37,  51);   // input field bg
    const Color BgBubMine  (255, 42,  133, 105);  // my bubble (teal-green)
    const Color BgBubTheir (255, 35,  52,  68);   // their bubble (dark)
    const Color BgHeader   (255, 19,  29,  38);   // top bars
    const Color BgDivider  (255, 30,  46,  60);   // separator lines
    const Color BgSearch   (255, 20,  32,  45);   // search field
    const Color BgLogin    (255, 11,  20,  26);   // login screen

    // Accents
    const Color Teal       (255,  0,  168, 132);  // #00A884 WhatsApp-ish teal
    const Color TealHov    (255,  0,  196, 155);
    const Color TealDim    (255,  0,  128, 100);

    // Text
    const Color TextPrim   (255, 233, 237, 239);  // #E9EDEF
    const Color TextSec    (255, 134, 150, 160);  // #8696A0
    const Color TextMuted  (255,  90, 110, 125);
    const Color TextOnTeal (255, 255, 255, 255);

    // Status
    const Color Online     (255,  0,  168, 132);
    const Color Unread     (255,  0,  168, 132);

    // Misc
    const Color Overlay    (180,  0,   0,   0);
    const Color Border     (255, 42,  57,  66);   // #2A3942
}

// ═══════════════════════════════════════════════════════════════
// DATA STRUCTURES
// ═══════════════════════════════════════════════════════════════

struct RgMessage {
    wstring id;
    wstring text;
    wstring senderName;
    bool    isMine    = false;
    bool    isRead    = false;
    wstring timeStr;
    bool    isPending = false;
};

struct RgChat {
    wstring id;
    wstring name;
    wstring mobile;
    wstring lastMsg;
    wstring lastTime;
    int     unread  = 0;
    int     avatarSeed = 0;
};

// ═══════════════════════════════════════════════════════════════
// GLOBAL STATE
// ═══════════════════════════════════════════════════════════════

// Layout cache — updated each draw
static float g_cx = 0, g_cy = 0, g_cw = 0, g_ch = 0;

// Panel split
static const float CHAT_LIST_W = 280.0f;
static const float HEADER_H    =  54.0f;
static const float INPUT_H     =  62.0f;
static const float SEARCH_H    =  52.0f;

// Login screen state
enum class RgScreen { Login, App };
static RgScreen g_screen = RgScreen::Login;

// Login step
enum class LoginStep { Phone, OTP, Name };
static LoginStep g_loginStep = LoginStep::Phone;

// Input buffers
static wchar_t g_phoneInput [16] = {};
static wchar_t g_otpInput   [6]  = {};
static wchar_t g_nameInput  [48] = {};
static wchar_t g_msgInput   [512]= {};
static wchar_t g_searchInput[64] = {};
static wstring g_loginError;
static wstring g_loginOtpPhone;
static int     g_activeInput = 0;   // 0=none, 1=phone, 2=otp, 3=name, 4=msg, 5=search

// Scroll offsets
static float g_msgScrollY    = 0.0f;
static float g_msgScrollMax  = 0.0f;
static float g_chatScrollY   = 0.0f;
static float g_chatScrollMax = 0.0f;

// Hover states
static int  g_hovChat = -1;
static int  g_hovMsg  = -1;
static bool g_hovSend = false;
static bool g_hovLoginBtn = false;
static bool g_hovLanBtn   = false;
static bool g_hovBack     = false;

// Country selector
static struct Country { const wchar_t* flag; const wchar_t* code; } g_countries[] = {
    { L"🇧🇩", L"+880" },
    { L"🇺🇸", L"+1"   },
    { L"🇬🇧", L"+44"  },
    { L"🇮🇳", L"+91"  },
    { L"🇦🇪", L"+971" },
    { L"🇸🇦", L"+966" },
};
static int  g_selCountry  = 0;
static bool g_showCountry = false;
static bool g_hovCountry[6] = {};

// Conversation data
static vector<RgChat>    g_chats;
static vector<RgMessage> g_messages;
static int               g_openChatIdx = -1;
static wstring           g_myMobile;
static wstring           g_myName;
static bool              g_lanMode = false;

// Hit-test rectangles (set during draw)
struct HRect { float x,y,w,h; };
static vector<HRect> g_chatRects;
static HRect         g_sendBtnRect;
static HRect         g_loginBtnRect;
static HRect         g_inputRect;
static HRect         g_searchRect;
static HRect         g_countryBtnRect;
static vector<HRect> g_countryDropRects;
static HRect         g_lanBtnRect;
static HRect         g_backBtnRect;
static vector<HRect> g_msgRects;

// ═══════════════════════════════════════════════════════════════
// HELPER UTILITIES
// ═══════════════════════════════════════════════════════════════

static bool HitTest(const HRect& r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static Color AvatarColor(int seed) {
    static Color palette[] = {
        Color(255, 229, 57,  53),
        Color(255,  33, 150, 243),
        Color(255,  76, 175,  80),
        Color(255, 156,  39, 176),
        Color(255, 255, 152,   0),
        Color(255,   0, 188, 212),
        Color(255, 233,  30,  99),
        Color(255, 121,  85,  72),
    };
    return palette[((seed < 0 ? -seed : seed)) % 8];
}

static wstring Initials(const wstring& name) {
    if (name.empty()) return L"?";
    return wstring(1, towupper(name[0]));
}

static wstring TimeNow() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[8];
    swprintf_s(buf, L"%02d:%02d", st.wHour, st.wMinute);
    return buf;
}

static void Invalidate() {
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
}

// Populate with demo data (used when not connected to backend)
static void PopulateDemoChats() {
    if (!g_chats.empty()) return;
    g_chats = {
        { L"chat_1", L"Rafi Ahmed",    L"+8801700000001", L"Hey! Are you available?",   L"10:45", 2, 1 },
        { L"chat_2", L"Mitu Akter",    L"+8801800000002", L"ok done ✓",                L"10:30", 0, 2 },
        { L"chat_3", L"Sadia Islam",   L"+8801900000003", L"Send me the file please",   L"09:58", 3, 3 },
        { L"chat_4", L"Arif Hossain",  L"+8801600000004", L"Meeting at 3PM today",      L"09:14", 0, 4 },
        { L"chat_5", L"Nadia Rahman",  L"+8801500000005", L"Thanks! Will do 😊",         L"Yesterday", 0, 5 },
        { L"chat_6", L"RasFocus Bot",  L"bot",            L"Welcome to RasGram Desktop",L"Yesterday", 0, 6 },
    };
}

static void LoadDemoMessages(int chatIdx) {
    g_messages.clear();
    if (chatIdx < 0 || chatIdx >= (int)g_chats.size()) return;
    auto& c = g_chats[chatIdx];
    g_messages = {
        { L"m1", L"Hey! কেমন আছো?",            c.name,   false, true,  L"10:30" },
        { L"m2", L"ভালো আছি, তুমি?",           g_myName, true,  true,  L"10:31" },
        { L"m3", L"আমিও ভালো আছি।",            c.name,   false, true,  L"10:32" },
        { L"m4", L"আজকে কোনো কাজ আছে?",        g_myName, true,  true,  L"10:35" },
        { L"m5", c.lastMsg,                      c.name,   false, false, c.lastTime },
    };
    g_msgScrollY = 999999.0f; // scroll to bottom
}

// ═══════════════════════════════════════════════════════════════
// DRAWING PRIMITIVES
// ═══════════════════════════════════════════════════════════════

static void FillRR(Graphics& g, Brush* br, float x, float y,
                   float w, float h, float r = 8.0f)
{
    if (r <= 0) { g.FillRectangle(br, x, y, w, h); return; }
    float d = r * 2.0f;
    GraphicsPath p;
    p.AddArc(x,       y,       d, d, 180, 90);
    p.AddArc(x+w-d,   y,       d, d, 270, 90);
    p.AddArc(x+w-d,   y+h-d,   d, d,   0, 90);
    p.AddArc(x,       y+h-d,   d, d,  90, 90);
    p.CloseFigure();
    g.FillPath(br, &p);
}

static void DrawRR(Graphics& g, Pen* pen, float x, float y,
                   float w, float h, float r = 8.0f)
{
    float d = r * 2.0f;
    GraphicsPath p;
    p.AddArc(x,       y,       d, d, 180, 90);
    p.AddArc(x+w-d,   y,       d, d, 270, 90);
    p.AddArc(x+w-d,   y+h-d,   d, d,   0, 90);
    p.AddArc(x,       y+h-d,   d, d,  90, 90);
    p.CloseFigure();
    g.DrawPath(pen, &p);
}

// Avatar circle with letter
static void DrawAvatar(Graphics& g, float x, float y, float sz,
                       const wstring& initials, Color col)
{
    float r = sz / 2.0f;
    SolidBrush br(col);
    g.FillEllipse(&br, x, y, sz, sz);

    FontFamily ff(L"Segoe UI");
    float fs = sz * 0.4f;
    Font font(&ff, fs, FontStyleBold, UnitPixel);
    SolidBrush white(Color(255,255,255,255));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(initials.c_str(), -1, &font,
                 RectF(x, y, sz, sz), &fmt, &white);
}

// Tick marks ✓ / ✓✓
static void DrawTicks(Graphics& g, float x, float y, bool read,
                      const FontFamily& ff)
{
    Font fTick(&ff, 10, FontStyleRegular, UnitPixel);
    SolidBrush col(read ? Color(255,83,197,174) : Color(255,160,175,185));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(read ? L"✓✓" : L"✓", -1, &fTick,
                 PointF(x, y), &fmt, &col);
}

// ═══════════════════════════════════════════════════════════════
// LOGIN SCREEN
// ═══════════════════════════════════════════════════════════════

static void DrawLoginScreen(Graphics& g, float cx, float cy,
                             float cw, float ch)
{
    FontFamily ff(L"Segoe UI");
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);

    // Full background
    SolidBrush bgBr(RgC::BgLogin);
    g.FillRectangle(&bgBr, cx, cy, cw, ch);

    // Subtle radial glow at top centre
    {
        float gx = cx + cw / 2.0f - 200.0f;
        float gy = cy - 60.0f;
        for (int i = 5; i >= 0; i--) {
            float rad = 200.0f + i * 30.0f;
            float alpha = 8.0f - i * 1.2f;
            SolidBrush glowBr(Color((BYTE)max(0.0f,alpha), 0, 168, 132));
            g.FillEllipse(&glowBr, gx + 200.0f - rad, gy + rad/2.0f - rad/2.0f, rad*2.0f, rad);
        }
    }

    float midX  = cx + cw / 2.0f;
    float cardW = min(400.0f, cw - 40.0f);
    float cardX = midX - cardW / 2.0f;

    float curY = cy + 48.0f;

    // ── Logo circle ──
    float logoSz = 80.0f;
    SolidBrush logoBg(RgC::Teal);
    g.FillEllipse(&logoBg, midX - logoSz/2.0f, curY, logoSz, logoSz);

    // Telegram-style paper-plane icon drawn manually
    {
        Pen planePen(Color(255,255,255,255), 2.5f);
        planePen.SetLineJoin(LineJoinRound);
        planePen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        float px = midX - 18.0f, py = curY + 22.0f;
        // Body
        PointF pts[] = {
            { px,       py + 18.0f },
            { px + 36.0f, py },
            { px + 6.0f,  py + 12.0f },
            { px + 6.0f,  py + 24.0f },
            { px + 14.0f, py + 18.0f },
            { px + 36.0f, py },
        };
        g.DrawLines(&planePen, pts, 4);
    }
    curY += logoSz + 20.0f;

    // App name
    Font fTitle(&ff, 28, FontStyleBold, UnitPixel);
    SolidBrush white(RgC::TextPrim);
    g.DrawString(L"RasGram", -1, &fTitle,
                 RectF(cx, curY, cw, 40.0f), &fmtC, &white);
    curY += 44.0f;

    // Tagline
    Font fTag(&ff, 13, FontStyleRegular, UnitPixel);
    SolidBrush teal(RgC::Teal);
    SolidBrush muted(RgC::TextSec);

    if (g_loginStep == LoginStep::Phone) {
        g.DrawString(L"বাংলাদেশের সেরা মেসেজিং অ্যাপ", -1, &fTag,
                     RectF(cx, curY, cw, 24.0f), &fmtC, &teal);
        curY += 32.0f;

        g.DrawString(L"আপনার ফোন নম্বর দিন", -1, &fTag,
                     RectF(cx, curY, cw, 22.0f), &fmtC, &muted);
        curY += 34.0f;

        // ── Phone row: country button + phone input ──
        float rowH  = 52.0f;
        float cBtnW = 90.0f;
        float phW   = cardW - cBtnW - 10.0f;
        float rowX  = cardX;

        // Country button
        Color cBtnBg = g_showCountry ? RgC::TealDim : RgC::BgCard;
        SolidBrush cBtnBr(cBtnBg);
        FillRR(g, &cBtnBr, rowX, curY, cBtnW, rowH, 12.0f);
        Pen cBtnBorder(RgC::Border, 1.0f);
        DrawRR(g, &cBtnBorder, rowX, curY, cBtnW, rowH, 12.0f);

        Font fCBtn(&ff, 13, FontStyleRegular, UnitPixel);
        wstring cLabel = wstring(g_countries[g_selCountry].flag) +
                         L" " + g_countries[g_selCountry].code;
        g.DrawString(cLabel.c_str(), -1, &fCBtn,
                     RectF(rowX + 6, curY, cBtnW - 6, rowH), &fmtL, &white);

        g_countryBtnRect = { rowX, curY, cBtnW, rowH };

        // Phone input
        bool phFocus = (g_activeInput == 1);
        Color phBg = RgC::BgInput;
        SolidBrush phBr(phBg);
        FillRR(g, &phBr, rowX + cBtnW + 10.0f, curY, phW, rowH, 12.0f);
        Pen phBorder(phFocus ? RgC::Teal : RgC::Border, phFocus ? 1.8f : 1.0f);
        DrawRR(g, &phBorder, rowX + cBtnW + 10.0f, curY, phW, rowH, 12.0f);

        Font fInput(&ff, 14, FontStyleRegular, UnitPixel);
        wstring phText(g_phoneInput);
        if (phText.empty()) {
            g.DrawString(L"Phone number", -1, &fInput,
                         RectF(rowX+cBtnW+20.0f, curY, phW-20.0f, rowH),
                         &fmtL, &muted);
        } else {
            g.DrawString(phText.c_str(), -1, &fInput,
                         RectF(rowX+cBtnW+20.0f, curY, phW-20.0f, rowH),
                         &fmtL, &white);
        }
        if (phFocus) {
            // Cursor
            RectF sz;
            g.MeasureString(phText.c_str(), -1, &fInput,
                            PointF(rowX+cBtnW+20.0f, curY + rowH/2.0f - 8.0f), &sz);
            SolidBrush cur(RgC::Teal);
            g.FillRectangle(&cur, rowX+cBtnW+20.0f + sz.Width, curY+14.0f, 2.0f, 24.0f);
        }

        g_inputRect = { rowX + cBtnW + 10.0f, curY, phW, rowH };

        // Country dropdown
        if (g_showCountry) {
            g_countryDropRects.clear();
            float dropY = curY + rowH + 4.0f;
            float dropH = 44.0f * 6.0f;
            SolidBrush dropBg(Color(255,28,40,51));
            FillRR(g, &dropBg, rowX, dropY, cBtnW + 10.0f, dropH, 8.0f);
            Pen dropBorder(RgC::Border, 1.0f);
            DrawRR(g, &dropBorder, rowX, dropY, cBtnW + 10.0f, dropH, 8.0f);
            for (int i = 0; i < 6; i++) {
                float iy = dropY + i * 44.0f;
                if (g_hovCountry[i]) {
                    SolidBrush hov(Color(40,0,168,132));
                    FillRR(g, &hov, rowX, iy, cBtnW + 10.0f, 44.0f, 4.0f);
                }
                wstring lbl = wstring(g_countries[i].flag) +
                              L"  " + g_countries[i].code;
                g.DrawString(lbl.c_str(), -1, &fCBtn,
                             RectF(rowX+8, iy, cBtnW, 44.0f), &fmtL, &white);
                g_countryDropRects.push_back({ rowX, iy, (float)(cBtnW+10), 44.0f });
            }
        }

        curY += rowH + 18.0f;

        // Error
        if (!g_loginError.empty()) {
            Font fErr(&ff, 11, FontStyleRegular, UnitPixel);
            SolidBrush red(Color(255,234,0,56));
            g.DrawString(g_loginError.c_str(), -1, &fErr,
                         RectF(cardX, curY, cardW, 20.0f), &fmtL, &red);
            curY += 24.0f;
        }

        // Continue button
        float btnH = 52.0f;
        bool hov   = g_hovLoginBtn;
        SolidBrush btnBg(hov ? RgC::TealHov : RgC::Teal);
        FillRR(g, &btnBg, cardX, curY, cardW, btnH, 14.0f);
        Font fBtn(&ff, 15, FontStyleBold, UnitPixel);
        g.DrawString(L"Continue", -1, &fBtn,
                     RectF(cardX, curY, cardW, btnH), &fmtC,
                     &SolidBrush(Color(255,0,0,0)));
        g_loginBtnRect = { cardX, curY, cardW, btnH };
        curY += btnH + 24.0f;

    } else if (g_loginStep == LoginStep::OTP) {

        g.DrawString(L"Verification Code", -1, &fTag,
                     RectF(cx, curY, cw, 24.0f), &fmtC, &teal);
        curY += 28.0f;

        wstring subText = L"Code sent to RasGram on  " + g_loginOtpPhone;
        Font fSub(&ff, 12, FontStyleRegular, UnitPixel);
        g.DrawString(subText.c_str(), -1, &fSub,
                     RectF(cx, curY, cw, 22.0f), &fmtC, &muted);
        curY += 30.0f;

        // Back button
        Font fBack(&ff, 12, FontStyleRegular, UnitPixel);
        SolidBrush backBr(g_hovBack ? RgC::TealHov : RgC::TextSec);
        g.DrawString(L"← Back", -1, &fBack,
                     RectF(cardX, curY, 80.0f, 26.0f), &fmtL, &backBr);
        g_backBtnRect = { cardX, curY, 80.0f, 26.0f };
        curY += 34.0f;

        // 5-digit OTP boxes
        float boxW  = 48.0f, boxH = 58.0f, gap = 10.0f;
        float totalW = 5 * boxW + 4 * gap;
        float startX = midX - totalW / 2.0f;
        wstring otp(g_otpInput);

        for (int i = 0; i < 5; i++) {
            float bx = startX + i * (boxW + gap);
            bool filled = (i < (int)otp.size());
            bool active = (g_activeInput == 2) && (i == (int)otp.size() || (i == 4 && (int)otp.size() == 5));

            Color bbg = filled ? RgC::BgCard : RgC::BgInput;
            SolidBrush bboxBg(bbg);
            FillRR(g, &bboxBg, bx, curY, boxW, boxH, 8.0f);
            Pen bboxBorder(active ? RgC::Teal : (filled ? RgC::TealDim : RgC::Border),
                          active ? 2.0f : 1.0f);
            DrawRR(g, &bboxBorder, bx, curY, boxW, boxH, 8.0f);

            if (filled) {
                // Show bullet
                Font fDot(&ff, 28, FontStyleBold, UnitPixel);
                g.DrawString(L"•", -1, &fDot,
                             RectF(bx, curY, boxW, boxH), &fmtC, &white);
            }
        }
        g_inputRect = { startX, curY, totalW, boxH };
        curY += boxH + 20.0f;

        // Error
        if (!g_loginError.empty()) {
            Font fErr(&ff, 11, FontStyleRegular, UnitPixel);
            SolidBrush red(Color(255,234,0,56));
            g.DrawString(g_loginError.c_str(), -1, &fErr,
                         RectF(cardX, curY, cardW, 20.0f), &fmtC, &red);
            curY += 24.0f;
        }

        // Verify button
        float btnH = 52.0f;
        bool hov   = g_hovLoginBtn && (int)otp.size() == 5;
        bool ready = (int)otp.size() == 5;
        SolidBrush btnBg2(ready ? (hov ? RgC::TealHov : RgC::Teal)
                                : Color(255,42,57,66));
        FillRR(g, &btnBg2, cardX, curY, cardW, btnH, 14.0f);
        Font fBtn(&ff, 15, FontStyleBold, UnitPixel);
        SolidBrush btnTxt(ready ? Color(255,0,0,0) : RgC::TextMuted);
        g.DrawString(L"Verify", -1, &fBtn,
                     RectF(cardX, curY, cardW, btnH), &fmtC, &btnTxt);
        g_loginBtnRect = { cardX, curY, cardW, btnH };

    } else { // Name step
        g.DrawString(L"Your Name", -1, &fTag,
                     RectF(cx, curY, cw, 24.0f), &fmtC, &teal);
        curY += 28.0f;

        Font fSub(&ff, 12, FontStyleRegular, UnitPixel);
        g.DrawString(L"আপনার নাম দিন — বন্ধুরা এটাই দেখবে", -1, &fSub,
                     RectF(cx, curY, cw, 22.0f), &fmtC, &muted);
        curY += 32.0f;

        float rowH = 52.0f;
        bool focus = (g_activeInput == 3);
        SolidBrush inBg(RgC::BgInput);
        FillRR(g, &inBg, cardX, curY, cardW, rowH, 12.0f);
        Pen inBorder(focus ? RgC::Teal : RgC::Border, focus ? 1.8f : 1.0f);
        DrawRR(g, &inBorder, cardX, curY, cardW, rowH, 12.0f);

        Font fInput2(&ff, 14, FontStyleRegular, UnitPixel);
        wstring nameStr(g_nameInput);
        if (nameStr.empty()) {
            g.DrawString(L"Your full name", -1, &fInput2,
                         RectF(cardX+16, curY, cardW-20, rowH), &fmtL, &muted);
        } else {
            g.DrawString(nameStr.c_str(), -1, &fInput2,
                         RectF(cardX+16, curY, cardW-20, rowH), &fmtL, &white);
            if (focus) {
                RectF sz;
                g.MeasureString(nameStr.c_str(), -1, &fInput2,
                                PointF(cardX+16, curY+14), &sz);
                SolidBrush cur(RgC::Teal);
                g.FillRectangle(&cur, cardX+16+sz.Width, curY+14, 2.0f, 24.0f);
            }
        }
        g_inputRect = { cardX, curY, cardW, rowH };
        curY += rowH + 18.0f;

        float btnH = 52.0f;
        bool ready = (int)wcslen(g_nameInput) > 1;
        bool hov   = g_hovLoginBtn && ready;
        SolidBrush btnBg3(ready ? (hov ? RgC::TealHov : RgC::Teal)
                                : Color(255,42,57,66));
        FillRR(g, &btnBg3, cardX, curY, cardW, btnH, 14.0f);
        Font fBtn3(&ff, 15, FontStyleBold, UnitPixel);
        SolidBrush btnTxt3(ready ? Color(255,0,0,0) : RgC::TextMuted);
        g.DrawString(L"Start Messaging", -1, &fBtn3,
                     RectF(cardX, curY, cardW, btnH), &fmtC, &btnTxt3);
        g_loginBtnRect = { cardX, curY, cardW, btnH };
    }

    // E2E badge at bottom
    {
        float bY = cy + ch - 44.0f;
        Font fBadge(&ff, 12, FontStyleRegular, UnitPixel);
        SolidBrush bMuted(RgC::TextMuted);
        g.DrawString(L"🔒  End-to-end encrypted", -1, &fBadge,
                     RectF(cx, bY, cw, 28.0f), &fmtC, &bMuted);
    }
}

// ═══════════════════════════════════════════════════════════════
// CHAT LIST PANEL
// ═══════════════════════════════════════════════════════════════

static void DrawChatListPanel(Graphics& g, float px, float py,
                               float pw, float ph)
{
    FontFamily ff(L"Segoe UI");
    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);

    // Panel background
    SolidBrush panelBg(RgC::BgPanel);
    g.FillRectangle(&panelBg, px, py, pw, ph);

    // Right border
    Pen border(RgC::Border, 1.0f);
    g.DrawLine(&border, px+pw, py, px+pw, py+ph);

    // ── TOP BAR ──
    {
        SolidBrush hdrBg(RgC::BgHeader);
        g.FillRectangle(&hdrBg, px, py, pw, HEADER_H);

        Font fName(&ff, 14, FontStyleBold, UnitPixel);
        SolidBrush white(RgC::TextPrim);
        g.DrawString(L"RasGram", -1, &fName,
                     RectF(px+16, py, pw-120, HEADER_H), &fmtL, &white);

        // LAN mode toggle button
        bool lanHov = g_hovLanBtn;
        SolidBrush lanBg(g_lanMode ? Color(60,0,168,132) : Color(30,255,255,255));
        FillRR(g, &lanBg, px+pw-52, py+10, 38, HEADER_H-20, 6.0f);
        Font fIcon(&ff, 16, FontStyleRegular, UnitPixel);
        SolidBrush lanIcColor(g_lanMode ? RgC::Teal : RgC::TextSec);
        g.DrawString(L"📡", -1, &fIcon,
                     RectF(px+pw-52, py+10, 38, HEADER_H-20), &fmtC, &lanIcColor);
        g_lanBtnRect = { px+pw-52, py+10, 38, HEADER_H-20 };
    }

    // ── SEARCH BAR ──
    float searchY = py + HEADER_H;
    {
        SolidBrush sBg(RgC::BgHeader);
        g.FillRectangle(&sBg, px, searchY, pw, SEARCH_H);

        // Input field
        float siX = px + 12, siY = searchY + 9;
        float siW = pw - 24, siH = 34;
        bool focus = (g_activeInput == 5);
        SolidBrush siBg(RgC::BgSearch);
        FillRR(g, &siBg, siX, siY, siW, siH, 17.0f);

        Font fSrch(&ff, 13, FontStyleRegular, UnitPixel);
        SolidBrush muted(RgC::TextMuted);
        SolidBrush white(RgC::TextPrim);

        wstring srchStr(g_searchInput);
        if (srchStr.empty()) {
            g.DrawString(L"🔍  Search", -1, &fSrch,
                         RectF(siX+12, siY, siW-20, siH), &fmtL, &muted);
        } else {
            g.DrawString(srchStr.c_str(), -1, &fSrch,
                         RectF(siX+12, siY, siW-20, siH), &fmtL, &white);
        }
        g_searchRect = { siX, siY, siW, siH };

        Pen sepPen(RgC::Border, 1.0f);
        g.DrawLine(&sepPen, px, searchY+SEARCH_H-1, px+pw, searchY+SEARCH_H-1);
    }

    // ── CHAT ROWS ──
    float listY = searchY + SEARCH_H;
    float listH = ph - HEADER_H - SEARCH_H;

    // Clipping
    Region oldClip;
    g.GetClip(&oldClip);
    g.SetClip(RectF(px, listY, pw, listH));

    // Filter by search
    wstring srchStr(g_searchInput);
    for (auto& c : srchStr) c = towlower(c);

    g_chatRects.clear();
    float rowH = 72.0f;
    float yOff = listY - g_chatScrollY;

    for (int i = 0; i < (int)g_chats.size(); i++) {
        auto& chat = g_chats[i];

        // Search filter
        if (!srchStr.empty()) {
            wstring nameLow = chat.name;
            for (auto& c2 : nameLow) c2 = towlower(c2);
            if (nameLow.find(srchStr) == wstring::npos) {
                g_chatRects.push_back({0,0,0,0}); // placeholder
                continue;
            }
        }

        float ry = yOff + i * rowH;
        g_chatRects.push_back({ px, ry, pw, rowH });

        if (ry + rowH < listY || ry > listY + listH) continue;

        bool sel = (g_openChatIdx == i);
        bool hov = (g_hovChat == i && !sel);

        Color rowBg = sel ? RgC::BgCardSel :
                     hov ? RgC::BgCardHov : RgC::BgCard;
        // Alternate rows for visual texture
        if (!sel && !hov && i % 2 == 0) {
            rowBg = Color(255,
                          min(255,(int)rowBg.GetRed()+3),
                          min(255,(int)rowBg.GetGreen()+3),
                          min(255,(int)rowBg.GetBlue()+3));
        }
        SolidBrush rowBr(rowBg);
        g.FillRectangle(&rowBr, px, ry, pw, rowH);

        // Left accent for selected
        if (sel) {
            SolidBrush accent(RgC::Teal);
            g.FillRectangle(&accent, px, ry, 3.0f, rowH);
        }

        // Avatar
        float avSz = 46.0f;
        float avX  = px + 12.0f;
        float avY  = ry + (rowH - avSz) / 2.0f;
        DrawAvatar(g, avX, avY, avSz, Initials(chat.name),
                   AvatarColor(chat.avatarSeed));

        // Online dot
        SolidBrush onlineDot(RgC::Online);
        g.FillEllipse(&onlineDot, avX+avSz-10.0f, avY+avSz-10.0f, 10.0f, 10.0f);

        // Name
        float textX = avX + avSz + 12.0f;
        float textW = pw - (textX - px) - 70.0f;

        Font fName(&ff, 13, FontStyleBold, UnitPixel);
        SolidBrush white(RgC::TextPrim);
        StringFormat fmtClip;
        fmtClip.SetAlignment(StringAlignmentNear);
        fmtClip.SetLineAlignment(StringAlignmentNear);
        fmtClip.SetFormatFlags(StringFormatFlagsNoWrap);
        fmtClip.SetTrimming(StringTrimmingEllipsisCharacter);
        g.DrawString(chat.name.c_str(), -1, &fName,
                     RectF(textX, ry + 12.0f, textW, 20.0f), &fmtClip, &white);

        // Last message
        Font fLast(&ff, 11, FontStyleRegular, UnitPixel);
        SolidBrush muted(chat.unread > 0 ? RgC::TextPrim : RgC::TextSec);
        g.DrawString(chat.lastMsg.c_str(), -1, &fLast,
                     RectF(textX, ry + 36.0f, textW, 18.0f), &fmtClip, &muted);

        // Time
        Font fTime(&ff, 10, FontStyleRegular, UnitPixel);
        SolidBrush timeClr(RgC::TextMuted);
        StringFormat fmtR;
        fmtR.SetAlignment(StringAlignmentFar);
        fmtR.SetLineAlignment(StringAlignmentNear);
        g.DrawString(chat.lastTime.c_str(), -1, &fTime,
                     RectF(px+4, ry + 12.0f, pw-12.0f, 20.0f), &fmtR, &timeClr);

        // Unread badge
        if (chat.unread > 0) {
            float badgeSz = 20.0f;
            float badgeX  = px + pw - 22.0f;
            float badgeY  = ry + 36.0f;
            SolidBrush badgeBg(RgC::Unread);
            g.FillEllipse(&badgeBg, badgeX, badgeY, badgeSz, badgeSz);
            Font fBadge(&ff, 9, FontStyleBold, UnitPixel);
            StringFormat fmtBC;
            fmtBC.SetAlignment(StringAlignmentCenter);
            fmtBC.SetLineAlignment(StringAlignmentCenter);
            wchar_t numBuf[8];
            swprintf_s(numBuf, L"%d", min(99, chat.unread));
            SolidBrush badgeTxt(Color(255,0,0,0));
            g.DrawString(numBuf, -1, &fBadge,
                         RectF(badgeX, badgeY, badgeSz, badgeSz), &fmtBC, &badgeTxt);
        }

        // Row separator
        Pen sepPen(Color(30,255,255,255), 0.5f);
        g.DrawLine(&sepPen, px+textX-px, ry+rowH-1, px+pw-12, ry+rowH-1);
    }

    g_chatScrollMax = max(0.0f, g_chats.size() * rowH - listH);
    g_chatScrollY   = max(0.0f, min(g_chatScrollY, g_chatScrollMax));

    g.SetClip(&oldClip);
}

// ═══════════════════════════════════════════════════════════════
// MESSAGE PANEL (right side)
// ═══════════════════════════════════════════════════════════════

static void DrawEmptyState(Graphics& g, float px, float py,
                            float pw, float ph)
{
    FontFamily ff(L"Segoe UI");
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);

    SolidBrush bg(RgC::BgApp);
    g.FillRectangle(&bg, px, py, pw, ph);

    // Large icon
    float midX = px + pw/2.0f;
    float midY = py + ph/2.0f;

    SolidBrush iconBg(Color(30,0,168,132));
    g.FillEllipse(&iconBg, midX-60.0f, midY-80.0f, 120.0f, 120.0f);

    Font fBig(&ff, 52, FontStyleRegular, UnitPixel);
    SolidBrush teal(RgC::Teal);
    g.DrawString(L"💬", -1, &fBig,
                 RectF(midX-60, midY-80, 120, 120), &fmtC, &teal);

    Font fTitle(&ff, 22, FontStyleBold, UnitPixel);
    SolidBrush white(RgC::TextPrim);
    g.DrawString(L"RasGram Desktop", -1, &fTitle,
                 RectF(px, midY+56, pw, 36), &fmtC, &white);

    Font fSub(&ff, 13, FontStyleRegular, UnitPixel);
    SolidBrush muted(RgC::TextSec);
    g.DrawString(L"বাম দিক থেকে একটি চ্যাট বেছে নিন\nঅথবা নতুন কথোপকথন শুরু করুন",
                 -1, &fSub,
                 RectF(px, midY+96, pw, 50), &fmtC, &muted);
}

static void DrawMessagePanel(Graphics& g, float px, float py,
                              float pw, float ph)
{
    if (g_openChatIdx < 0 || g_openChatIdx >= (int)g_chats.size()) {
        DrawEmptyState(g, px, py, pw, ph);
        return;
    }

    FontFamily ff(L"Segoe UI");
    auto& chat = g_chats[g_openChatIdx];

    StringFormat fmtL;
    fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR;
    fmtR.SetAlignment(StringAlignmentFar);
    fmtR.SetLineAlignment(StringAlignmentCenter);

    // Full panel background — WhatsApp-style wallpaper pattern
    SolidBrush bgBr(RgC::BgApp);
    g.FillRectangle(&bgBr, px, py, pw, ph);

    // Subtle dot pattern (decorative wallpaper)
    SolidBrush dotBr(Color(10,255,255,255));
    for (float dy = py; dy < py+ph; dy += 28) {
        for (float dx = px; dx < px+pw; dx += 28) {
            g.FillEllipse(&dotBr, dx, dy, 3.0f, 3.0f);
        }
    }

    // ── CHAT HEADER ──
    {
        SolidBrush hdrBg(RgC::BgHeader);
        g.FillRectangle(&hdrBg, px, py, pw, HEADER_H);

        float avSz = 38.0f;
        float avX  = px + 14.0f;
        float avY  = py + (HEADER_H - avSz) / 2.0f;
        DrawAvatar(g, avX, avY, avSz, Initials(chat.name),
                   AvatarColor(chat.avatarSeed));

        // Online dot
        SolidBrush onlineDot(RgC::Online);
        g.FillEllipse(&onlineDot, avX+avSz-8.0f, avY+avSz-8.0f, 9.0f, 9.0f);

        Font fName(&ff, 14, FontStyleBold, UnitPixel);
        SolidBrush white(RgC::TextPrim);
        g.DrawString(chat.name.c_str(), -1, &fName,
                     RectF(avX+avSz+10, py+4, pw-avSz-80, 28), &fmtL, &white);

        Font fStatus(&ff, 11, FontStyleRegular, UnitPixel);
        SolidBrush onlineClr(RgC::Teal);
        g.DrawString(L"online", -1, &fStatus,
                     RectF(avX+avSz+10, py+30, 80, 20), &fmtL, &onlineClr);

        // Action buttons (audio + video call)
        float btnY2 = py + (HEADER_H-30)/2.0f;
        float btnX2 = px + pw - 14 - 30;
        for (int bi = 0; bi < 2; bi++) {
            float bx = btnX2 - bi*42.0f;
            SolidBrush bBg(Color(25,0,168,132));
            FillRR(g, &bBg, bx, btnY2, 32, 30, 6.0f);
            Font fBIco(&ff, 16, FontStyleRegular, UnitPixel);
            SolidBrush bIcClr(RgC::Teal);
            g.DrawString(bi == 0 ? L"📹" : L"📞", -1, &fBIco,
                         RectF(bx, btnY2, 32, 30), &fmtC, &bIcClr);
        }

        Pen hdrBorder(RgC::Border, 1.0f);
        g.DrawLine(&hdrBorder, px, py+HEADER_H, px+pw, py+HEADER_H);
    }

    // ── MESSAGE LIST ──
    float msgAreaY = py + HEADER_H;
    float msgAreaH = ph - HEADER_H - INPUT_H;

    Region oldClip;
    g.GetClip(&oldClip);
    g.SetClip(RectF(px, msgAreaY, pw, msgAreaH));

    float bubMaxW   = pw * 0.65f;
    float bubPadX   = 12.0f;
    float bubPadY   = 8.0f;
    float rowSpac   = 10.0f;
    float marginX   = 14.0f;

    // Measure total height first
    float totalH = 8.0f;
    Font fMsgMeas(&ff, 13, FontStyleRegular, UnitPixel);
    for (auto& msg : g_messages) {
        SizeF sz;
        StringFormat wrapFmt;
        wrapFmt.SetAlignment(StringAlignmentNear);
        wrapFmt.SetLineAlignment(StringAlignmentNear);
        wrapFmt.SetFormatFlags(StringFormatFlagsNoClip);
        wrapFmt.SetTrimming(StringTrimmingNone);
        RectF bounds(0,0, bubMaxW - bubPadX*2, 1000);
        RectF measured;
        g.MeasureString(msg.text.c_str(), -1, &fMsgMeas, bounds, &wrapFmt, &measured);
        float bubH = measured.Height + bubPadY*2 + 20.0f; // +20 for time row
        totalH += bubH + rowSpac;
    }
    g_msgScrollMax = max(0.0f, totalH - msgAreaH);
    if (g_msgScrollY > 999000.0f) g_msgScrollY = g_msgScrollMax;
    g_msgScrollY = max(0.0f, min(g_msgScrollY, g_msgScrollMax));

    float yOff = msgAreaY + 8.0f - g_msgScrollY;
    g_msgRects.clear();

    for (auto& msg : g_messages) {
        StringFormat wrapFmt;
        wrapFmt.SetAlignment(StringAlignmentNear);
        wrapFmt.SetLineAlignment(StringAlignmentNear);
        wrapFmt.SetFormatFlags(StringFormatFlagsNoClip);
        wrapFmt.SetTrimming(StringTrimmingNone);

        RectF bounds(0, 0, bubMaxW - bubPadX*2, 1000);
        RectF measured;
        g.MeasureString(msg.text.c_str(), -1, &fMsgMeas, bounds, &wrapFmt, &measured);
        float bubW  = max(120.0f, measured.Width  + bubPadX*2 + 4);
        float bubH  = measured.Height + bubPadY*2 + 22.0f;

        float bubX = msg.isMine ? (px + pw - marginX - bubW)
                                 : (px + marginX);

        // Bubble background
        Color bubBg = msg.isMine ? RgC::BgBubMine : RgC::BgBubTheir;

        // Slightly different corner radius to indicate direction
        float tr = msg.isMine ? 0.0f : 12.0f;
        float tl = msg.isMine ? 12.0f : 0.0f;

        // Draw bubble with one asymmetric corner
        {
            float r2 = 12.0f;
            float d  = r2 * 2.0f;
            GraphicsPath bubPath;

            if (msg.isMine) {
                // Top-right corner sharp
                bubPath.AddArc(bubX, yOff, d, d, 180, 90);       // TL
                bubPath.AddLine(bubX+bubW-2, yOff, bubX+bubW, yOff); // TR (sharp)
                bubPath.AddLine(bubX+bubW, yOff, bubX+bubW, yOff+bubH); // right side
                bubPath.AddArc(bubX+bubW-d, yOff+bubH-d, d, d, 0, 90); // BR
                bubPath.AddArc(bubX, yOff+bubH-d, d, d, 90, 90);  // BL
            } else {
                // Top-left corner sharp
                bubPath.AddLine(bubX, yOff, bubX+bubW, yOff);         // TL (sharp)
                bubPath.AddArc(bubX+bubW-d, yOff, d, d, 270, 90);     // TR
                bubPath.AddArc(bubX+bubW-d, yOff+bubH-d, d, d, 0, 90); // BR
                bubPath.AddArc(bubX, yOff+bubH-d, d, d, 90, 90);      // BL
                bubPath.AddLine(bubX, yOff+bubH, bubX, yOff);
            }
            bubPath.CloseFigure();

            SolidBrush bubBrush(bubBg);
            g.FillPath(&bubBrush, &bubPath);

            // Subtle shadow
            // (skipped for performance — already has bg contrast)
        }

        g_msgRects.push_back({ bubX, yOff, bubW, bubH });

        // Message text
        Font fMsg(&ff, 13, FontStyleRegular, UnitPixel);
        SolidBrush white(RgC::TextPrim);
        g.DrawString(msg.text.c_str(), -1, &fMsg,
                     RectF(bubX+bubPadX, yOff+bubPadY,
                           bubW-bubPadX*2, bubH-bubPadY*2-18),
                     &wrapFmt, &white);

        // Time + read ticks
        Font fTime(&ff, 10, FontStyleRegular, UnitPixel);
        SolidBrush timeClr(msg.isMine ? Color(200,255,255,255)
                                       : RgC::TextMuted);
        StringFormat fmtTR;
        fmtTR.SetAlignment(StringAlignmentFar);
        fmtTR.SetLineAlignment(StringAlignmentCenter);
        float tickW = msg.isMine ? 22.0f : 0.0f;
        g.DrawString(msg.timeStr.c_str(), -1, &fTime,
                     RectF(bubX+4, yOff+bubH-20, bubW-8-tickW, 18),
                     &fmtTR, &timeClr);

        if (msg.isMine) {
            DrawTicks(g, bubX+bubW-22, yOff+bubH-18, msg.isRead, ff);
        }

        yOff += bubH + rowSpac;
    }

    g.SetClip(&oldClip);

    // Scrollbar (thin)
    if (g_msgScrollMax > 0) {
        float sbH  = msgAreaH * msgAreaH / (totalH + 1);
        float sbY2 = msgAreaY + (g_msgScrollY / g_msgScrollMax) * (msgAreaH - sbH);
        SolidBrush sbBr(Color(50,255,255,255));
        FillRR(g, &sbBr, px+pw-5, sbY2, 4, sbH, 2.0f);
    }

    // ── INPUT BAR ──
    {
        float inpY = py + ph - INPUT_H;
        SolidBrush inpBg(RgC::BgHeader);
        g.FillRectangle(&inpBg, px, inpY, pw, INPUT_H);
        Pen inpTopBorder(RgC::Border, 1.0f);
        g.DrawLine(&inpTopBorder, px, inpY, px+pw, inpY);

        // Attach button
        Font fAtt(&ff, 20, FontStyleRegular, UnitPixel);
        SolidBrush attClr(RgC::TextSec);
        g.DrawString(L"📎", -1, &fAtt,
                     RectF(px+8, inpY+4, 36, INPUT_H-8), &fmtC, &attClr);

        // Text field
        float tfX = px + 50.0f;
        float tfW = pw - 50.0f - 56.0f - 8.0f;
        float tfY = inpY + 10.0f;
        float tfH = INPUT_H - 20.0f;
        bool focus = (g_activeInput == 4);

        SolidBrush tfBg(RgC::BgSearch);
        FillRR(g, &tfBg, tfX, tfY, tfW, tfH, tfH/2.0f);
        Pen tfBorder(focus ? RgC::Teal : RgC::Border, focus ? 1.5f : 0.8f);
        DrawRR(g, &tfBorder, tfX, tfY, tfW, tfH, tfH/2.0f);

        Font fMsgIn(&ff, 13, FontStyleRegular, UnitPixel);
        wstring msgStr(g_msgInput);
        if (msgStr.empty()) {
            SolidBrush ph2(RgC::TextMuted);
            g.DrawString(L"Type a message…", -1, &fMsgIn,
                         RectF(tfX+14, tfY, tfW-20, tfH), &fmtL, &ph2);
        } else {
            SolidBrush white2(RgC::TextPrim);
            g.DrawString(msgStr.c_str(), -1, &fMsgIn,
                         RectF(tfX+14, tfY, tfW-20, tfH), &fmtL, &white2);
            if (focus) {
                RectF sz;
                g.MeasureString(msgStr.c_str(), -1, &fMsgIn,
                                PointF(tfX+14, tfY+8), &sz);
                SolidBrush cur(RgC::Teal);
                g.FillRectangle(&cur, tfX+14+sz.Width, tfY+8, 2.0f, tfH-16);
            }
        }
        g_inputRect = { tfX, tfY, tfW, tfH };

        // Send button
        float sendX = tfX + tfW + 8.0f;
        float sendY = inpY + (INPUT_H - 42.0f) / 2.0f;
        bool  hasMsg = wcslen(g_msgInput) > 0;

        SolidBrush sendBg(hasMsg ? (g_hovSend ? RgC::TealHov : RgC::Teal)
                                  : Color(255,42,57,66));
        g.FillEllipse(&sendBg, sendX, sendY, 42.0f, 42.0f);
        Font fSendIco(&ff, 18, FontStyleRegular, UnitPixel);
        SolidBrush sendIcClr(hasMsg ? Color(255,0,0,0) : RgC::TextMuted);
        g.DrawString(L"➤", -1, &fSendIco,
                     RectF(sendX, sendY, 42.0f, 42.0f), &fmtC, &sendIcClr);
        g_sendBtnRect = { sendX, sendY, 42.0f, 42.0f };
    }
}

// ═══════════════════════════════════════════════════════════════
// MAIN DRAW
// ═══════════════════════════════════════════════════════════════

void DrawRasGramTab(Graphics& g, float cx, float cy, float cw, float ch)
{
    g_cx = cx; g_cy = cy; g_cw = cw; g_ch = ch;

    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // Populate demo data if no backend
    if (g_myName.empty()) {
        g_myName   = g_loggedInName.empty() ? L"You" : g_loggedInName;
        g_myMobile = L"+8801700000000";
    }

    // Auto-login if RasFocus user is logged in
    if (!g_loggedInUserUid.empty() && g_screen == RgScreen::Login) {
        g_screen = RgScreen::App;
        if (g_myName.empty()) g_myName = g_loggedInName;
        PopulateDemoChats();
    }

    if (g_screen == RgScreen::Login) {
        DrawLoginScreen(g, cx, cy, cw, ch);
        return;
    }

    // ── APP LAYOUT ──
    SolidBrush bg(RgC::BgApp);
    g.FillRectangle(&bg, cx, cy, cw, ch);

    // Determine if sidebar is visible (narrow layout)
    bool showSidebar = (cw >= 500.0f);
    float listW = showSidebar ? CHAT_LIST_W : cw;

    if (showSidebar) {
        DrawChatListPanel(g, cx, cy, listW, ch);
        DrawMessagePanel (g, cx+listW+1, cy, cw-listW-1, ch);
    } else {
        // Mobile-style: toggle between list and chat
        if (g_openChatIdx < 0)
            DrawChatListPanel(g, cx, cy, cw, ch);
        else
            DrawMessagePanel (g, cx, cy, cw, ch);
    }
}

// ═══════════════════════════════════════════════════════════════
// MOUSE MOVE
// ═══════════════════════════════════════════════════════════════

void ProcessRasGramMouseMove(float x, float y)
{
    if (g_screen == RgScreen::Login) {
        bool oldHov = g_hovLoginBtn;
        g_hovLoginBtn = HitTest(g_loginBtnRect, x, y);
        bool oldBack  = g_hovBack;
        g_hovBack     = HitTest(g_backBtnRect, x, y);

        if (g_showCountry) {
            for (int i = 0; i < 6 && i < (int)g_countryDropRects.size(); i++)
                g_hovCountry[i] = HitTest(g_countryDropRects[i], x, y);
        }

        if (oldHov != g_hovLoginBtn || oldBack != g_hovBack) Invalidate();
        return;
    }

    // Chat list hover
    bool needRed = false;
    int oldHovChat = g_hovChat;
    g_hovChat = -1;
    for (int i = 0; i < (int)g_chatRects.size(); i++) {
        if (HitTest(g_chatRects[i], x, y)) { g_hovChat = i; break; }
    }
    if (g_hovChat != oldHovChat) needRed = true;

    bool oldHovSend = g_hovSend;
    g_hovSend = HitTest(g_sendBtnRect, x, y);
    if (g_hovSend != oldHovSend) needRed = true;

    if (needRed) Invalidate();
}

// ═══════════════════════════════════════════════════════════════
// MOUSE CLICK
// ═══════════════════════════════════════════════════════════════

static void DoSendMessage()
{
    if (wcslen(g_msgInput) == 0) return;
    if (g_openChatIdx < 0) return;

    RgMessage msg;
    msg.id      = L"m_new_" + to_wstring(GetTickCount64());
    msg.text    = wstring(g_msgInput);
    msg.isMine  = true;
    msg.isRead  = false;
    msg.timeStr = TimeNow();
    msg.senderName = g_myName;
    g_messages.push_back(msg);

    // Update last message in chat list
    g_chats[g_openChatIdx].lastMsg  = msg.text;
    g_chats[g_openChatIdx].lastTime = msg.timeStr;

    ZeroMemory(g_msgInput, sizeof(g_msgInput));
    g_msgScrollY = 999999.0f; // scroll to bottom
    Invalidate();
}

void ProcessRasGramMouseClick(float x, float y)
{
    if (g_screen == RgScreen::Login) {
        // Country dropdown toggle
        if (HitTest(g_countryBtnRect, x, y)) {
            g_showCountry = !g_showCountry;
            g_activeInput = 1;
            Invalidate(); return;
        }

        // Country dropdown selection
        if (g_showCountry) {
            for (int i = 0; i < 6 && i < (int)g_countryDropRects.size(); i++) {
                if (HitTest(g_countryDropRects[i], x, y)) {
                    g_selCountry  = i;
                    g_showCountry = false;
                    Invalidate(); return;
                }
            }
            g_showCountry = false;
            Invalidate(); return;
        }

        // Back button (OTP step)
        if (g_loginStep == LoginStep::OTP && HitTest(g_backBtnRect, x, y)) {
            g_loginStep   = LoginStep::Phone;
            g_loginError  = L"";
            ZeroMemory(g_otpInput, sizeof(g_otpInput));
            g_activeInput = 1;
            Invalidate(); return;
        }

        // Input field focus
        if (HitTest(g_inputRect, x, y)) {
            g_activeInput = (g_loginStep == LoginStep::Phone) ? 1 :
                            (g_loginStep == LoginStep::OTP)   ? 2 : 3;
            Invalidate(); return;
        }

        // Login button
        if (HitTest(g_loginBtnRect, x, y)) {
            if (g_loginStep == LoginStep::Phone) {
                wstring ph(g_phoneInput);
                if (ph.length() < 9) {
                    g_loginError = L"সঠিক ফোন নম্বর দিন";
                } else {
                    g_loginOtpPhone = wstring(g_countries[g_selCountry].code) + ph;
                    g_loginError    = L"";
                    g_loginStep     = LoginStep::OTP;
                    g_activeInput   = 2;
                    // Simulate OTP sent (demo: code 12345)
                }
            } else if (g_loginStep == LoginStep::OTP) {
                wstring code(g_otpInput);
                if (code.length() == 5) {
                    // Demo: accept any 5-digit code
                    g_loginError  = L"";
                    g_loginStep   = LoginStep::Name;
                    g_activeInput = 3;
                } else {
                    g_loginError = L"৫ সংখ্যার কোড দিন";
                }
            } else {
                wstring name(g_nameInput);
                if (name.length() < 2) {
                    g_loginError = L"নাম দিন";
                } else {
                    g_myName = name;
                    g_myMobile = g_loginOtpPhone;
                    g_screen   = RgScreen::App;
                    g_activeInput = 0;
                    PopulateDemoChats();
                }
            }
            Invalidate(); return;
        }

        return;
    }

    // App screen

    // Close country dropdown
    if (g_showCountry) { g_showCountry = false; Invalidate(); return; }

    // LAN toggle
    if (HitTest(g_lanBtnRect, x, y)) {
        g_lanMode = !g_lanMode;
        Invalidate(); return;
    }

    // Chat list clicks
    for (int i = 0; i < (int)g_chatRects.size(); i++) {
        if (HitTest(g_chatRects[i], x, y)) {
            if (g_openChatIdx != i) {
                g_openChatIdx = i;
                g_chats[i].unread = 0;
                g_msgScrollY = 999999.0f;
                LoadDemoMessages(i);
            }
            g_activeInput = 4;
            Invalidate(); return;
        }
    }

    // Send button
    if (HitTest(g_sendBtnRect, x, y)) {
        DoSendMessage(); return;
    }

    // Message input focus
    if (HitTest(g_inputRect, x, y)) {
        g_activeInput = 4;
        Invalidate(); return;
    }

    // Search focus
    if (HitTest(g_searchRect, x, y)) {
        g_activeInput = 5;
        Invalidate(); return;
    }

    // Clicking elsewhere clears focus
    g_activeInput = 0;
    Invalidate();
}

// ═══════════════════════════════════════════════════════════════
// MOUSE WHEEL
// ═══════════════════════════════════════════════════════════════

void ProcessRasGramMouseWheel(int delta)
{
    float step = 60.0f;
    float scroll = (delta > 0) ? -step : step;

    if (g_openChatIdx >= 0) {
        // If mouse is likely over message area
        g_msgScrollY = max(0.0f, min(g_msgScrollY + scroll, g_msgScrollMax));
    } else {
        g_chatScrollY = max(0.0f, min(g_chatScrollY + scroll, g_chatScrollMax));
    }
    Invalidate();
}

// ═══════════════════════════════════════════════════════════════
// CHARACTER INPUT
// ═══════════════════════════════════════════════════════════════

void ProcessRasGramChar(wchar_t c)
{
    auto appendChar = [](wchar_t* buf, int maxLen, wchar_t ch) {
        int len = (int)wcslen(buf);
        if (len < maxLen - 1) {
            buf[len]   = ch;
            buf[len+1] = L'\0';
        }
    };
    auto backspace = [](wchar_t* buf) {
        int len = (int)wcslen(buf);
        if (len > 0) buf[len-1] = L'\0';
    };

    if (g_activeInput == 1) { // phone
        if (c == L'\b') backspace(g_phoneInput);
        else if (c >= L'0' && c <= L'9') appendChar(g_phoneInput, 12, c);
    } else if (g_activeInput == 2) { // OTP
        if (c == L'\b') { backspace(g_otpInput); }
        else if (c >= L'0' && c <= L'9' && (int)wcslen(g_otpInput) < 5)
            appendChar(g_otpInput, 6, c);
        // Auto-verify when 5 digits entered
        if ((int)wcslen(g_otpInput) == 5) {
            g_loginStep   = LoginStep::Name;
            g_activeInput = 3;
            g_loginError  = L"";
        }
    } else if (g_activeInput == 3) { // name
        if (c == L'\b') backspace(g_nameInput);
        else if (c >= L' ') appendChar(g_nameInput, 48, c);
    } else if (g_activeInput == 4) { // message
        if (c == L'\b') backspace(g_msgInput);
        else if (c == L'\r' || c == L'\n') { DoSendMessage(); return; }
        else if (c >= L' ') appendChar(g_msgInput, 512, c);
    } else if (g_activeInput == 5) { // search
        if (c == L'\b') backspace(g_searchInput);
        else if (c >= L' ') appendChar(g_searchInput, 64, c);
    }
    Invalidate();
}

// ═══════════════════════════════════════════════════════════════
// KEY DOWN
// ═══════════════════════════════════════════════════════════════

void ProcessRasGramKeyDown(WPARAM vk)
{
    if (vk == VK_RETURN && g_activeInput == 4) {
        DoSendMessage(); return;
    }
    if (vk == VK_ESCAPE) {
        g_activeInput = 0;
        g_showCountry = false;
        Invalidate(); return;
    }
    if (vk == VK_BACK) {
        // Handled in WM_CHAR as '\b'
    }
}

// ═══════════════════════════════════════════════════════════════
// SHOW / HIDE (compatibility with tab_special.cpp)
// ═══════════════════════════════════════════════════════════════

void ShowRasGramControls(bool show)
{
    // Pure GDI+ — no Win32 child windows to show/hide
    (void)show;
}

void InitRasGramDesktop()
{
    // Called once when Special tab initialises.
    // If RasFocus user is already logged in, skip login screen.
    if (!g_loggedInUserUid.empty()) {
        g_screen   = RgScreen::App;
        g_myName   = g_loggedInName.empty() ? L"Me" : g_loggedInName;
        PopulateDemoChats();
    }
}

// ═══════════════════════════════════════════════════════════════
// WM_ MESSAGE HANDLER (stub — no WebView2 messages)
// ═══════════════════════════════════════════════════════════════

bool RgHandleParentWndMsg(HWND, UINT, WPARAM, LPARAM)
{
    return false;
}

void RgNotify_Destroy()        {}
void RgNet_StopIncomingCallPolling() {}

