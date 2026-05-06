// accounts.cpp

#include "accounts.h"
#include <shellapi.h>
#include <string>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;

// 🟢 গ্লোবাল ভেরিয়েবল 
bool g_isLoggedIn = false;
std::wstring g_userName = L"";
std::wstring g_userEmail = L"";
std::wstring g_userUID = L"";
bool g_isPremiumUser = false; 

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
    for (int i = 0; i < 4; ++i) { // শ্যাডো একটু কমানো হয়েছে পারফরম্যান্সের জন্য
        GraphicsPath path;
        float expand = 4.0f - (float)i;
        AddRoundedRectPathAcc(path, x - expand, y - expand + (float)i * 1.0f, w + expand * 2.0f, h + expand * 2.0f, r + expand);
        SolidBrush shadowBrush(Color(6 + (i * 2), 0, 0, 0)); 
        g.FillPath(&shadowBrush, &path);
    }
}

// ==========================================
// 🎨 DRAW ACCOUNTS TAB UI (Responsive)
// ==========================================
void DrawAccountsTab(Graphics& g, float cx, float cy, float cw, float ch) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // ডাইনামিক ফন্ট সাইজ (উইন্ডো খুব ছোট হলে ফন্টও হালকা ছোট হবে)
    float dynamicScale = g_scaleFactor;
    if (cw < 700.0f) dynamicScale = g_scaleFactor * 0.85f; 

    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 28 * dynamicScale, FontStyleBold, UnitPixel);
    Font fH2(&ff, 22 * dynamicScale, FontStyleBold, UnitPixel);
    Font fText(&ff, 14 * dynamicScale, FontStyleRegular, UnitPixel);
    Font fBtn(&ff, 16 * dynamicScale, FontStyleBold, UnitPixel);
    Font fBadge(&ff, 12 * dynamicScale, FontStyleBold, UnitPixel);
    
    FontFamily ffIc(L"Segoe MDL2 Assets");
    Font fIcon(&ffIc, 70 * dynamicScale, FontStyleRegular, UnitPixel);
    Font fAvatar(&ffIc, 50 * dynamicScale, FontStyleRegular, UnitPixel);

    SolidBrush bBg(Color(255, 248, 250, 252)); 
    SolidBrush bDark(Color(255, 30, 40, 50));
    SolidBrush bWhite(Color(255, 255, 255, 255));
    SolidBrush bGray(Color(255, 120, 130, 140));
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    
    // 1. Background Fill
    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // 2. Responsive Card Setup
    // কার্ডের সাইজ উইন্ডোর উপর ভিত্তি করে ছোট-বড় হবে, তবে একটা ম্যাক্সিমাম সাইজ থাকবে
    float cardW = min(500.0f * dynamicScale, cw * 0.9f); 
    float cardH = 340.0f * dynamicScale;
    
    // কার্ড একদম মাঝখানে পজিশন করা
    float cardX = cx + (cw - cardW) / 2.0f;
    float cardY = cy + (ch - cardH) / 2.0f; // ভার্টিকালিও মাঝখানে

    // Header Title (কার্ডের ঠিক উপরে থাকবে)
    float headerY = cardY - 80.0f * dynamicScale;
    g.DrawString(L"Account & Cloud Sync", -1, &fH1, RectF(cx, headerY, cw, 40.0f * dynamicScale), &fmtC, &bDark);
    g.DrawString(L"Securely backup your data to RasFocus Cloud.", -1, &fText, RectF(cx, headerY + 40.0f * dynamicScale, cw, 30.0f * dynamicScale), &fmtC, &bGray);

    // Draw Card Shape
    DrawPremiumShadowAcc(g, cardX, cardY, cardW, cardH, 15.0f);
    GraphicsPath cardPath;
    AddRoundedRectPathAcc(cardPath, cardX, cardY, cardW, cardH, 15.0f);
    g.FillPath(&bWhite, &cardPath);

    // ==================================
    // 🔴 STATE: NOT LOGGED IN
    // ==================================
    if (!g_isLoggedIn) {
        float avatarSize = 100.0f * dynamicScale;
        SolidBrush avatarBg(Color(255, 235, 245, 255));
        g.FillEllipse(&avatarBg, cardX + (cardW - avatarSize) / 2.0f, cardY + 30.0f * dynamicScale, avatarSize, avatarSize);
        
        SolidBrush avatarIconColor(Color(255, 18, 168, 176)); 
        g.DrawString(L"\xE77B", -1, &fIcon, RectF(cardX, cardY + 30.0f * dynamicScale, cardW, avatarSize), &fmtC, &avatarIconColor);
        
        g.DrawString(L"Not Logged In", -1, &fH2, RectF(cardX, cardY + 140.0f * dynamicScale, cardW, 40.0f * dynamicScale), &fmtC, &bDark);
        g.DrawString(L"Sign in to unlock premium cloud features.", -1, &fText, RectF(cardX + 10.0f, cardY + 180.0f * dynamicScale, cardW - 20.0f, 30.0f * dynamicScale), &fmtC, &bGray);

        float btnW = min(300.0f * dynamicScale, cardW * 0.85f);
        float btnH = 50.0f * dynamicScale;
        float btnX = cardX + (cardW - btnW) / 2.0f;
        float btnY = cardY + 250.0f * dynamicScale;
        btnLoginRect = RectF(btnX, btnY, btnW, btnH);

        GraphicsPath btnPath;
        AddRoundedRectPathAcc(btnPath, btnX, btnY, btnW, btnH, btnH/2.0f);
        SolidBrush btnColor(isHoverLogin ? Color(255, 14, 138, 145) : Color(255, 18, 168, 176));
        g.FillPath(&btnColor, &btnPath);
        g.DrawString(L"Sign In with Google", -1, &fBtn, btnLoginRect, &fmtC, &bWhite);
        
        btnLogoutRect = RectF(0,0,0,0); 
    } 
    // ==================================
    // 🟢 STATE: LOGGED IN (PREMIUM)
    // ==================================
    else {
        float avatarSize = 90.0f * dynamicScale;
        SolidBrush avatarBg(Color(255, 18, 168, 176));
        g.FillEllipse(&avatarBg, cardX + (cardW - avatarSize) / 2.0f, cardY + 40.0f * dynamicScale, avatarSize, avatarSize);
        g.DrawString(L"\xE77B", -1, &fAvatar, RectF(cardX, cardY + 40.0f * dynamicScale, cardW, avatarSize), &fmtC, &bWhite);

        g.DrawString(g_userName.c_str(), -1, &fH2, RectF(cardX, cardY + 140.0f * dynamicScale, cardW, 40.0f * dynamicScale), &fmtC, &bDark);
        g.DrawString(g_userEmail.c_str(), -1, &fText, RectF(cardX, cardY + 175.0f * dynamicScale, cardW, 30.0f * dynamicScale), &fmtC, &bGray);

        if (g_isPremiumUser) {
            GraphicsPath badgePath;
            float badgeW = 55.0f * dynamicScale;
            float badgeH = 22.0f * dynamicScale;
            AddRoundedRectPathAcc(badgePath, cardX + (cardW - badgeW) / 2.0f, cardY + 210.0f * dynamicScale, badgeW, badgeH, 10.0f);
            SolidBrush badgeColor(Color(255, 255, 180, 0)); 
            g.FillPath(&badgeColor, &badgePath);
            SolidBrush badgeText(Color(255, 100, 70, 0));
            g.DrawString(L"PRO", -1, &fBadge, RectF(cardX, cardY + 210.0f * dynamicScale, cardW, badgeH), &fmtC, &badgeText);
        }

        float btnW = min(200.0f * dynamicScale, cardW * 0.7f);
        float btnH = 45.0f * dynamicScale;
        float btnX = cardX + (cardW - btnW) / 2.0f;
        float btnY = cardY + 260.0f * dynamicScale;
        btnLogoutRect = RectF(btnX, btnY, btnW, btnH);

        GraphicsPath btnPath;
        AddRoundedRectPathAcc(btnPath, btnX, btnY, btnW, btnH, btnH/2.0f);
        SolidBrush btnColor(isHoverLogout ? Color(255, 220, 50, 60) : Color(255, 255, 75, 75)); 
        g.FillPath(&btnColor, &btnPath);
        g.DrawString(L"Sign Out", -1, &fBtn, btnLogoutRect, &fmtC, &bWhite);

        btnLoginRect = RectF(0,0,0,0); 
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
        ShellExecuteW(NULL, L"open", L"https://rasfocus-c746d.firebaseapp.com", NULL, NULL, SW_SHOWNORMAL);
        
        int msgboxID = MessageBoxW(hParentWnd, 
            L"Google Chrome ব্রাউজারে লগইন পেজ ওপেন করা হয়েছে।\n\nব্রাউজারে লগইন কমপ্লিট করার পর এখানে 'OK' বাটনে ক্লিক করুন।", 
            L"RasFocus Cloud Authentication", 
            MB_OKCANCEL | MB_ICONINFORMATION);
            
        if (msgboxID == IDOK) {
            SetUserLoginData(L"Rasel Mia", L"rasel@duet.ac.bd", L"DUET_PRO_123");
        }
    }
    else if (isHoverLogout && g_isLoggedIn) {
        g_isLoggedIn = false;
        g_userName = L"";
        g_userEmail = L"";
        g_userUID = L"";
        g_isPremiumUser = false;
        
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
    }
}

// ==========================================
// 📡 SIGNAL RECEIVER 
// ==========================================
void SetUserLoginData(std::wstring name, std::wstring email, std::wstring uid) {
    g_isLoggedIn = true;
    g_userName = name;
    
    if (g_userName.empty() || g_userName == L"undefined") {
        g_userName = email;
    }
    
    g_userEmail = email;
    g_userUID = uid;
    
    g_isPremiumUser = true; 

    if (hParentWnd) {
        InvalidateRect(hParentWnd, NULL, FALSE);
    }
}
