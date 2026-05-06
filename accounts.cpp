// accounts.cpp

#include "accounts.h"
#include "mini_browser.h" 
#include <string>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;

// 🟢 গ্লোবাল ভেরিয়েবল (যাতে অন্য ট্যাব থেকেও ডেটা পড়া যায়)
bool g_isLoggedIn = false;
std::wstring g_userName = L"";
std::wstring g_userEmail = L"";
std::wstring g_userUID = L"";
bool g_isPremiumUser = false; // লগইন থাকলে প্রো ফিচার আনলক করার জন্য

// বাটনের স্টেট ট্র্যাক করার জন্য ভেরিয়েবল
static RectF btnLoginRect;
static RectF btnLogoutRect;
static bool isHoverLogin = false;
static bool isHoverLogout = false;

// --- Helper Function: Rounded Rectangle ---
static void AddRoundedRectPathAcc(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    if (d > w) d = w; if (d > h) d = h;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

// --- Helper Function: Premium Shadow ---
static void DrawPremiumShadowAcc(Graphics& g, float x, float y, float w, float h, float r) {
    for (int i = 0; i < 6; ++i) {
        GraphicsPath path;
        float expand = 6.0f - (float)i;
        AddRoundedRectPathAcc(path, x - expand, y - expand + (float)i * 1.5f, w + expand * 2.0f, h + expand * 2.0f, r + expand);
        SolidBrush shadowBrush(Color(4 + (i * 2), 0, 0, 0)); 
        g.FillPath(&shadowBrush, &path);
    }
}

// ==========================================
// 🎨 DRAW ACCOUNTS TAB UI
// ==========================================
void DrawAccountsTab(Graphics& g, float cx, float cy, float cw, float ch) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 32 * g_scaleFactor, FontStyleBold, UnitPixel);
    Font fH2(&ff, 26 * g_scaleFactor, FontStyleBold, UnitPixel);
    Font fText(&ff, 16 * g_scaleFactor, FontStyleRegular, UnitPixel);
    Font fBtn(&ff, 18 * g_scaleFactor, FontStyleBold, UnitPixel);
    Font fBadge(&ff, 14 * g_scaleFactor, FontStyleBold, UnitPixel);
    
    FontFamily ffIc(L"Segoe MDL2 Assets");
    Font fIcon(&ffIc, 80 * g_scaleFactor, FontStyleRegular, UnitPixel);
    Font fAvatar(&ffIc, 60 * g_scaleFactor, FontStyleRegular, UnitPixel);

    SolidBrush bBg(Color(255, 248, 250, 252)); 
    SolidBrush bDark(Color(255, 30, 40, 50));
    SolidBrush bWhite(Color(255, 255, 255, 255));
    SolidBrush bGray(Color(255, 120, 130, 140));
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);

    // 1. Background Fill & Header
    g.FillRectangle(&bBg, cx, cy, cw, ch);
    g.DrawString(L"Account & Cloud Sync", -1, &fH1, RectF(cx + 50.0f * g_scaleFactor, cy + 30.0f * g_scaleFactor, cw, 50.0f * g_scaleFactor), &fmtL, &bDark);
    g.DrawString(L"Securely backup your blocks, schedule, and diary to RasFocus Cloud.", -1, &fText, RectF(cx + 50.0f * g_scaleFactor, cy + 80.0f * g_scaleFactor, cw, 30.0f * g_scaleFactor), &fmtL, &bGray);

    // 2. Profile Card Setup
    float cardW = 550.0f * g_scaleFactor;
    float cardH = 380.0f * g_scaleFactor;
    float cardX = cx + (cw - cardW) / 2.0f;
    float cardY = cy + 150.0f * g_scaleFactor;

    DrawPremiumShadowAcc(g, cardX, cardY, cardW, cardH, 20.0f);
    GraphicsPath cardPath;
    AddRoundedRectPathAcc(cardPath, cardX, cardY, cardW, cardH, 20.0f);
    g.FillPath(&bWhite, &cardPath);

    if (!g_isLoggedIn) {
        // 🔴 STATE: NOT LOGGED IN
        SolidBrush avatarBg(Color(255, 235, 245, 255));
        g.FillEllipse(&avatarBg, cardX + (cardW - 120.0f * g_scaleFactor) / 2.0f, cardY + 40.0f * g_scaleFactor, 120.0f * g_scaleFactor, 120.0f * g_scaleFactor);
        
        SolidBrush avatarIconColor(Color(255, 18, 168, 176)); 
        g.DrawString(L"\xE77B", -1, &fIcon, RectF(cardX, cardY + 40.0f * g_scaleFactor, cardW, 120.0f * g_scaleFactor), &fmtC, &avatarIconColor);
        
        g.DrawString(L"Not Logged In", -1, &fH2, RectF(cardX, cardY + 180.0f * g_scaleFactor, cardW, 40.0f * g_scaleFactor), &fmtC, &bDark);
        g.DrawString(L"Sign in to unlock cross-device synchronization\nand premium cloud features.", -1, &fText, RectF(cardX + 20.0f, cardY + 220.0f * g_scaleFactor, cardW - 40.0f, 50.0f * g_scaleFactor), &fmtC, &bGray);

        float btnW = 320.0f * g_scaleFactor;
        float btnH = 55.0f * g_scaleFactor;
        float btnX = cardX + (cardW - btnW) / 2.0f;
        float btnY = cardY + 290.0f * g_scaleFactor;
        btnLoginRect = RectF(btnX, btnY, btnW, btnH);

        GraphicsPath btnPath;
        AddRoundedRectPathAcc(btnPath, btnX, btnY, btnW, btnH, 27.5f);
        SolidBrush btnColor(isHoverLogin ? Color(255, 14, 138, 145) : Color(255, 18, 168, 176));
        g.FillPath(&btnColor, &btnPath);
        g.DrawString(L"Sign In with Google / Email", -1, &fBtn, btnLoginRect, &fmtC, &bWhite);
        
        btnLogoutRect = RectF(0,0,0,0); // Hide logout button
    } 
    else {
        // 🟢 STATE: LOGGED IN (PREMIUM)
        SolidBrush avatarBg(Color(255, 18, 168, 176));
        g.FillEllipse(&avatarBg, cardX + (cardW - 100.0f * g_scaleFactor) / 2.0f, cardY + 50.0f * g_scaleFactor, 100.0f * g_scaleFactor, 100.0f * g_scaleFactor);
        g.DrawString(L"\xE77B", -1, &fAvatar, RectF(cardX, cardY + 50.0f * g_scaleFactor, cardW, 100.0f * g_scaleFactor), &fmtC, &bWhite);

        // Name and Email
        g.DrawString(g_userName.c_str(), -1, &fH2, RectF(cardX, cardY + 160.0f * g_scaleFactor, cardW, 40.0f * g_scaleFactor), &fmtC, &bDark);
        g.DrawString(g_userEmail.c_str(), -1, &fText, RectF(cardX, cardY + 200.0f * g_scaleFactor, cardW, 30.0f * g_scaleFactor), &fmtC, &bGray);

        // PRO Badge
        if (g_isPremiumUser) {
            GraphicsPath badgePath;
            float badgeW = 60.0f * g_scaleFactor;
            float badgeH = 24.0f * g_scaleFactor;
            AddRoundedRectPathAcc(badgePath, cardX + (cardW - badgeW) / 2.0f, cardY + 235.0f * g_scaleFactor, badgeW, badgeH, 12.0f);
            SolidBrush badgeColor(Color(255, 255, 180, 0)); // Golden Yellow
            g.FillPath(&badgeColor, &badgePath);
            SolidBrush badgeText(Color(255, 100, 70, 0));
            g.DrawString(L"PRO", -1, &fBadge, RectF(cardX, cardY + 235.0f * g_scaleFactor, cardW, badgeH), &fmtC, &badgeText);
        }

        // Logout Button
        float btnW = 200.0f * g_scaleFactor;
        float btnH = 45.0f * g_scaleFactor;
        float btnX = cardX + (cardW - btnW) / 2.0f;
        float btnY = cardY + 290.0f * g_scaleFactor;
        btnLogoutRect = RectF(btnX, btnY, btnW, btnH);

        GraphicsPath btnPath;
        AddRoundedRectPathAcc(btnPath, btnX, btnY, btnW, btnH, 22.5f);
        SolidBrush btnColor(isHoverLogout ? Color(255, 220, 50, 60) : Color(255, 255, 75, 75)); // Reddish color for logout
        g.FillPath(&btnColor, &btnPath);
        g.DrawString(L"Sign Out", -1, &fBtn, btnLogoutRect, &fmtC, &bWhite);

        btnLoginRect = RectF(0,0,0,0); // Hide login button
    }
}

// ==========================================
// 🖱️ MOUSE MOVE EVENT
// ==========================================
void ProcessAccountsMouseMove(float x, float y) {
    bool oldHoverIn = isHoverLogin;
    bool oldHoverOut = isHoverLogout;
    
    isHoverLogin = btnLoginRect.Contains(x, y);
    isHoverLogout = btnLogoutRect.Contains(x, y);
    
    if ((oldHoverIn != isHoverLogin || oldHoverOut != isHoverLogout) && hParentWnd) {
        InvalidateRect(hParentWnd, NULL, FALSE);
    }
}

// ==========================================
// 🖱️ MOUSE CLICK EVENT
// ==========================================
void ProcessAccountsMouseClick(float x, float y) {
    if (isHoverLogin && !g_isLoggedIn) {
        LaunchMiniBrowser(L"LOCAL_ACCOUNT_LOGIN", L"RasFocus Auth - Google Sign In");
    }
    else if (isHoverLogout && g_isLoggedIn) {
        // লগআউট করার লজিক
        g_isLoggedIn = false;
        g_userName = L"";
        g_userEmail = L"";
        g_userUID = L"";
        g_isPremiumUser = false;
        
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
    }
}

// ==========================================
// 📡 NEW: SIGNAL RECEIVER FROM FIREBASE
// ==========================================
void SetUserLoginData(std::wstring name, std::wstring email, std::wstring uid) {
    g_isLoggedIn = true;
    g_userName = name;
    
    // যদি গুগল নাম না দেয়, তবে ইমেইল দেখাবে
    if (g_userName.empty() || g_userName == L"undefined") {
        g_userName = email;
    }
    
    g_userEmail = email;
    g_userUID = uid;
    
    // লগইন করলেই আপাতত প্রো ফিচার আনলক হয়ে যাবে!
    g_isPremiumUser = true; 

    if (hParentWnd) {
        InvalidateRect(hParentWnd, NULL, FALSE);
    }
}
