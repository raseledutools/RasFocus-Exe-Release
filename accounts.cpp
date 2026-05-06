// accounts.cpp

#include "accounts.h"
#include "mini_browser.h" // পপ-আপ ওপেন করার জন্য
#include <string>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;

// বাটনের স্টেট
static RectF btnLoginRect;
static bool isHoverLogin = false;

// --- Helper: Rounded Rectangle ---
static void AddRoundedRectPathAcc(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    if (d > w) d = w; if (d > h) d = h;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static void DrawPremiumShadowAcc(Graphics& g, float x, float y, float w, float h, float r) {
    for (int i = 0; i < 5; ++i) {
        GraphicsPath path;
        float expand = 5.0f - (float)i;
        AddRoundedRectPathAcc(path, x - expand, y - expand + (float)i * 1.5f, w + expand * 2.0f, h + expand * 2.0f, r + expand);
        SolidBrush shadowBrush(Color(6 + (i * 2), 0, 0, 0)); 
        g.FillPath(&shadowBrush, &path);
    }
}

void DrawAccountsTab(Graphics& g, float cx, float cy, float cw, float ch) {
    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 32 * g_scaleFactor, FontStyleBold, UnitPixel);
    Font fH2(&ff, 24 * g_scaleFactor, FontStyleBold, UnitPixel);
    Font fText(&ff, 15 * g_scaleFactor, FontStyleRegular, UnitPixel);
    Font fBtn(&ff, 16 * g_scaleFactor, FontStyleBold, UnitPixel);
    
    FontFamily ffIc(L"Segoe MDL2 Assets");
    Font fIcon(&ffIc, 60 * g_scaleFactor, FontStyleRegular, UnitPixel);

    SolidBrush bBg(Color(255, 245, 247, 250)); 
    SolidBrush bDark(Color(255, 30, 40, 50));
    SolidBrush bWhite(Color(255, 255, 255, 255));
    SolidBrush bGray(Color(255, 120, 130, 140));
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);

    // 1. Background
    g.FillRectangle(&bBg, cx, cy, cw, ch);
    g.DrawString(L"Account & Cloud Sync", -1, &fH1, RectF(cx + 40.0f * g_scaleFactor, cy + 20.0f * g_scaleFactor, cw, 50.0f * g_scaleFactor), &fmtL, &bDark);

    // 2. Profile Card
    float cardW = 500.0f * g_scaleFactor;
    float cardH = 300.0f * g_scaleFactor;
    float cardX = cx + (cw - cardW) / 2.0f;
    float cardY = cy + 120.0f * g_scaleFactor;

    DrawPremiumShadowAcc(g, cardX, cardY, cardW, cardH, 15.0f);
    GraphicsPath cardPath;
    AddRoundedRectPathAcc(cardPath, cardX, cardY, cardW, cardH, 15.0f);
    g.FillPath(&bWhite, &cardPath);

    // Avatar Placeholder
    g.DrawString(L"\xE77B", -1, &fIcon, RectF(cardX, cardY + 30.0f * g_scaleFactor, cardW, 80.0f * g_scaleFactor), &fmtC, &bGray);
    
    // Texts
    g.DrawString(L"Not Logged In", -1, &fH2, RectF(cardX, cardY + 110.0f * g_scaleFactor, cardW, 40.0f * g_scaleFactor), &fmtC, &bDark);
    g.DrawString(L"Sign in to sync your blocks, diary, and study history to the cloud.", -1, &fText, RectF(cardX + 20.0f, cardY + 150.0f * g_scaleFactor, cardW - 40.0f, 40.0f * g_scaleFactor), &fmtC, &bGray);

    // Login Button
    float btnW = 250.0f * g_scaleFactor;
    float btnH = 50.0f * g_scaleFactor;
    float btnX = cardX + (cardW - btnW) / 2.0f;
    float btnY = cardY + 210.0f * g_scaleFactor;
    btnLoginRect = RectF(btnX, btnY, btnW, btnH);

    GraphicsPath btnPath;
    AddRoundedRectPathAcc(btnPath, btnX, btnY, btnW, btnH, 8.0f);
    
    SolidBrush btnColor(isHoverLogin ? Color(255, 14, 138, 145) : Color(255, 12, 168, 176));
    g.FillPath(&btnColor, &btnPath);
    g.DrawString(L"Sign In / Create Account", -1, &fBtn, btnLoginRect, &fmtC, &bWhite);
}

void ProcessAccountsMouseMove(float x, float y) {
    bool oldHover = isHoverLogin;
    isHoverLogin = btnLoginRect.Contains(x, y);
    if (oldHover != isHoverLogin && hParentWnd) {
        InvalidateRect(hParentWnd, NULL, FALSE);
    }
}

void ProcessAccountsMouseClick(float x, float y) {
    if (isHoverLogin) {
        // 🟢 এই ইউআরএলটি আমাদের পপ-আপ ইঞ্জিনকে ফায়ারবেস লগইন পেজ খুলতে নির্দেশ দেবে
        LaunchMiniBrowser(L"LOCAL_ACCOUNT_LOGIN", L"Sign In to RasFocus Pro");
    }
}
