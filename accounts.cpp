// accounts.cpp

#include "accounts.h"
#include <shellapi.h>
#include <string>
#include <vector>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;

// 🟢 GLOBAL STATE (Shared with main app)
bool g_isLoggedIn = false;
std::wstring g_userName = L"";
std::wstring g_userEmail = L"";
std::wstring g_userUID = L"";
bool g_isPremiumUser = false; // User handles subscription logic outside this file

// 📦 LOGIN UI STATE (Private to this file)
static RectF cardRect; // Entire central form area
static RectF fieldEmailRect;
static RectF fieldPassRect;
static RectF checkSaveRect;
static RectF linkSignupRect;
static RectF linkForgotRect;
static RectF btnLoginSubmitRect;
static RectF btnCancelRect;
static RectF btnGoogleRect;
static RectF btnLogoutRect; // Used when logged in

static bool isHoverEmail = false;
static bool isHoverPass = false;
static bool isHoverSave = false;
static bool isHoverSignup = false;
static bool isHoverForgot = false;
static bool isHoverLoginSubmit = false;
static bool isHoverCancel = false;
static bool isHoverGoogle = false;
static bool isHoverLogout = false; // Used when logged in

// Text input tracking
static std::wstring s_emailInput = L"";
static std::wstring s_passInput = L""; // Keep as plain text for rendering, security handled at submission
static int s_focusedField = 0; // 0=none, 1=email, 2=password
static bool s_saveLoginInfo = false;

// Color Palette (derived from original app theme)
const Color ColTeal(255, 18, 168, 176);
const Color ColTealHover(255, 14, 138, 145);
const Color ColTealLight(255, 235, 245, 255);
const Color ColTealBadge(255, 0, 110, 120);
const Color ColProBadgeBg(255, 255, 180, 0);
const Color ColProBadgeTxt(255, 100, 70, 0);
const Color ColFreeBadgeBg(255, 210, 215, 220); // Gray for free trial
const Color ColFreeBadgeTxt(255, 80, 90, 100);

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
    for (int i = 0; i < 4; ++i) { 
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

    // Dynamic scale based on factor and small window constraints
    float dynamicScale = g_scaleFactor;
    if (cw < 700.0f || ch < 600.0f) dynamicScale = g_scaleFactor * 0.85f; 

    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 26 * dynamicScale, FontStyleBold, UnitPixel);
    Font fH2(&ff, 20 * dynamicScale, FontStyleBold, UnitPixel);
    Font fLabel(&ff, 14 * dynamicScale, FontStyleBold, UnitPixel);
    Font fText(&ff, 14 * dynamicScale, FontStyleRegular, UnitPixel);
    Font fSmall(&ff, 12 * dynamicScale, FontStyleRegular, UnitPixel);
    Font fBtn(&ff, 16 * dynamicScale, FontStyleBold, UnitPixel);
    Font fBadge(&ff, 11 * dynamicScale, FontStyleBold, UnitPixel);
    Font fPlaceholder(&ff, 14 * dynamicScale, FontStyleRegular, UnitPixel);
    Font fTypedText(&ff, 14 * dynamicScale, FontStyleRegular, UnitPixel);
    
    FontFamily ffIc(L"Segoe MDL2 Assets");
    Font fAvatarBig(&ffIc, 80 * dynamicScale, FontStyleRegular, UnitPixel);
    Font fAvatarSmall(&ffIc, 50 * dynamicScale, FontStyleRegular, UnitPixel);

    SolidBrush bBg(Color(255, 248, 250, 252)); 
    SolidBrush bDark(Color(255, 30, 40, 50));
    SolidBrush bWhite(Color(255, 255, 255, 255));
    SolidBrush bGray(Color(255, 120, 130, 140));
    SolidBrush bPlaceholder(Color(255, 180, 185, 190));
    SolidBrush bLink(ColTeal);
    SolidBrush bError(Color(255, 220, 50, 60));
    SolidBrush bTealBg(ColTealLight);
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtNL; fmtNL.SetAlignment(StringAlignmentNear); fmtNL.SetLineAlignment(StringAlignmentCenter);
    
    // 1. Background Fill
    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // Define standard responsive positions
    float titleAreaH = 80.0f * dynamicScale;
    float contentY = cy + titleAreaH;
    float contentH = ch - titleAreaH;

    // Header Title
    g.DrawString(L"Account & Cloud Sync", -1, &fH1, RectF(cx, cy, cw, titleAreaH * 0.7f), &fmtC, &bDark);
    g.DrawString(L"Securely backup your data to RasFocus Cloud.", -1, &fText, RectF(cx, cy + titleAreaH * 0.6f, cw, titleAreaH * 0.4f), &fmtC, &bGray);

    // ==================================
    // 🔴 STATE: NOT LOGGED IN
    // ==================================
    if (!g_isLoggedIn) {
        // Inspired by image_3.png, made unique to app theme (native GDI+)
        
        // Form positioning
        float loginW = min(cw * 0.9f, 400.0f * dynamicScale);
        float loginH = 430.0f * dynamicScale; // Allow slightly more height for native feel
        float loginX = cx + (cw - loginW) / 2.0f;
        float loginY = contentY + (contentH - loginH) / 2.0f;
        cardRect = RectF(loginX, loginY, loginW, loginH);

        // Form Card drawing
        DrawPremiumShadowAcc(g, loginX, loginY, loginW, loginH, 15.0f);
        GraphicsPath loginPath;
        AddRoundedRectPathAcc(loginPath, loginX, loginY, loginW, loginH, 15.0f);
        g.FillPath(&bWhite, &loginPath);

        Pen borderPen(Color(255, 220, 225, 230), 1.0f);
        Pen focusedPen(ColTeal, 2.0f);

        // Content areas inside form (relative to loginX, loginY)
        float pad = 25.0f * dynamicScale;
        float currY = loginY + pad;

        // Big Avatar Area
        float avatarSize = 90.0f * dynamicScale;
        g.FillEllipse(&bTealBg, loginX + (loginW - avatarSize) / 2.0f, currY, avatarSize, avatarSize);
        SolidBrush avatarIconColor(ColTeal); 
        g.DrawString(L"\xE77B", -1, &fAvatarBig, RectF(loginX, currY, loginW, avatarSize), &fmtC, &avatarIconColor);
        currY += avatarSize + 20.0f * dynamicScale;

        // "Welcome to RasFocus"
        g.DrawString(L"Welcome to RasFocus", -1, &fH2, RectF(loginX, currY, loginW, 30.0f * dynamicScale), &fmtC, &bDark);
        currY += 30.0f * dynamicScale + 10.0f * dynamicScale;

        // Input Fields Area (inspired by j.png placeholder/typing flow)
        float inputPad = 10.0f * dynamicScale;
        float fieldH = 45.0f * dynamicScale;
        float fieldW = loginW - pad * 2.0f;
        float fieldX = loginX + pad;

        // Email address field
        fieldEmailRect = RectF(fieldX, currY, fieldW, fieldH);
        Pen* emailPen = (s_focusedField == 1) ? &focusedPen : &borderPen;
        g.DrawRectangle(emailPen, fieldEmailRect.X, fieldEmailRect.Y, fieldEmailRect.Width, fieldEmailRect.Height);
        
        if (s_emailInput.empty()) {
            g.DrawString(L"Email address", -1, &fPlaceholder, RectF(fieldX + inputPad, currY, fieldW - inputPad*2.0f, fieldH), &fmtNL, &bPlaceholder);
        } else {
            g.DrawString(s_emailInput.c_str(), -1, &fTypedText, RectF(fieldX + inputPad, currY, fieldW - inputPad*2.0f, fieldH), &fmtNL, &bDark);
        }
        currY += fieldH + 15.0f * dynamicScale;

        // Password field
        fieldPassRect = RectF(fieldX, currY, fieldW, fieldH);
        Pen* passPen = (s_focusedField == 2) ? &focusedPen : &borderPen;
        g.DrawRectangle(passPen, fieldPassRect.X, fieldPassRect.Y, fieldPassRect.Width, fieldPassRect.Height);
        
        if (s_passInput.empty()) {
            g.DrawString(L"Password", -1, &fPlaceholder, RectF(fieldX + inputPad, currY, fieldW - inputPad*2.0f, fieldH), &fmtNL, &bPlaceholder);
        } else {
            // Draw unmasked pass for simplicity in testing rendering logic
            g.DrawString(s_passInput.c_str(), -1, &fTypedText, RectF(fieldX + inputPad, currY, fieldW - inputPad*2.0f, fieldH), &fmtNL, &bDark);
        }
        currY += fieldH + 20.0f * dynamicScale;

        // Main Submit Button (Inspired by j.png teal theme)
        float btnW = min(300.0f * dynamicScale, loginW * 0.85f);
        float btnH = 50.0f * dynamicScale;
        float btnX = loginX + (loginW - btnW) / 2.0f;
        btnLoginSubmitRect = RectF(btnX, currY, btnW, btnH);

        GraphicsPath btnPath;
        AddRoundedRectPathAcc(btnPath, btnX, currY, btnW, btnH, btnH/2.0f);
        SolidBrush btnColor(isHoverLoginSubmit ? ColTealHover : ColTeal);
        g.FillPath(&btnColor, &btnPath);
        g.DrawString(L"Sign In / Creat Account", -1, &fBtn, btnLoginSubmitRect, &fmtC, &bWhite);
        currY += btnH + 15.0f * dynamicScale;

        // Setup for Google login or navigation links below form
        btnLogoutRect = RectF(0,0,0,0); // Clear other tab elements

    } 
    // ==================================
    // 🟢 STATE: LOGGED IN (Updated logic)
    // ==================================
    else {
        // Standard user card (responsive, updated for Trial vs PRO)
        float cardW = min(500.0f * dynamicScale, cw * 0.9f); 
        float cardH = 340.0f * dynamicScale;
        float cardX = cx + (cw - cardW) / 2.0f;
        float cardY = contentY + (contentH - cardH) / 2.0f;
        cardRect = RectF(cardX, cardY, cardW, cardH);

        DrawPremiumShadowAcc(g, cardX, cardY, cardW, cardH, 15.0f);
        GraphicsPath cardPath;
        AddRoundedRectPathAcc(cardPath, cardX, cardY, cardW, cardH, 15.0f);
        g.FillPath(&bWhite, &cardPath);

        // Avatar Area
        float avatarSize = 90.0f * dynamicScale;
        float avatarX = cardX + (cardW - avatarSize) / 2.0f;
        float avatarY = cardY + 40.0f * dynamicScale;
        SolidBrush avatarBg(ColTeal);
        g.FillEllipse(&avatarBg, avatarX, avatarY, avatarSize, avatarSize);
        g.DrawString(L"\xE77B", -1, &fAvatarSmall, RectF(cardX, avatarY, cardW, avatarSize), &fmtC, &bWhite);

        // User Details
        float detailsY = cardY + 140.0f * dynamicScale;
        g.DrawString(g_userName.c_str(), -1, &fH2, RectF(cardX, detailsY, cardW, 40.0f * dynamicScale), &fmtC, &bDark);
        g.DrawString(g_userEmail.c_str(), -1, &fText, RectF(cardX, detailsY + 35.0f * dynamicScale, cardW, 30.0f * dynamicScale), &fmtC, &bGray);

        // 🟠 STATUS BADGE (PRO or FREE TRIAL)🟠
        // (Inspired by requirements for outer My Account text replacement)
        float badgeY = detailsY + 70.0f * dynamicScale;
        if (g_isPremiumUser) {
            // "PRO" Badge (original teal color preserved)
            GraphicsPath badgePath;
            float badgeW = 60.0f * dynamicScale;
            float badgeH = 24.0f * dynamicScale;
            AddRoundedRectPathAcc(badgePath, cardX + (cardW - badgeW) / 2.0f, badgeY, badgeW, badgeH, 10.0f);
            SolidBrush badgeColor(ColTealBadge); 
            g.FillPath(&badgeColor, &badgePath);
            g.DrawString(L"PRO", -1, &fBadge, RectF(cardX, badgeY, cardW, badgeH), &fmtC, &bWhite);
        } else {
            // "FREE TRIAL" Badge (gray style from image_3.png concept)
            GraphicsPath badgePath;
            float badgeW = 100.0f * dynamicScale;
            float badgeH = 24.0f * dynamicScale;
            AddRoundedRectPathAcc(badgePath, cardX + (cardW - badgeW) / 2.0f, badgeY, badgeW, badgeH, 10.0f);
            SolidBrush badgeColor(ColFreeBadgeBg); 
            g.FillPath(&badgeColor, &badgePath);
            SolidBrush badgeText(ColFreeBadgeTxt);
            g.DrawString(L"FREE TRIAL", -1, &fBadge, RectF(cardX, badgeY, cardW, badgeH), &fmtC, &badgeText);
        }

        // Action Button (Sign Out)
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

        // Clear unused rects
        fieldEmailRect = RectF(0,0,0,0);
        fieldPassRect = RectF(0,0,0,0);
        btnLoginSubmitRect = RectF(0,0,0,0);
    }
}

// ==========================================
// 🖱️ MOUSE MOVE EVENT (Responsive tracking)
// ==========================================
void ProcessAccountsMouseMove(float x, float y) {
    bool oldHoverInSub = isHoverLoginSubmit;
    bool oldHoverPass = isHoverPass;
    bool oldHoverEmail = isHoverEmail;
    bool oldHoverOut = isHoverLogout;
    
    // Track new login page states
    isHoverLoginSubmit = btnLoginSubmitRect.Contains(x, y);
    isHoverPass = fieldPassRect.Contains(x, y);
    isHoverEmail = fieldEmailRect.Contains(x, y);

    // Track original states
    isHoverLogout = btnLogoutRect.Contains(x, y);
    
    // Change cursor to I-beam over text input fields if they are active
    static HCURSOR hIBeam = LoadCursor(NULL, IDC_IBEAM);
    static HCURSOR hArrow = LoadCursor(NULL, IDC_ARROW);
    if (!g_isLoggedIn && (isHoverEmail || isHoverPass)) {
        SetCursor(hIBeam);
    } else {
        SetCursor(hArrow); // Only if you are managing the cursor manually, else Windows handles
    }

    if ((oldHoverInSub != isHoverLoginSubmit || oldHoverPass != isHoverPass || 
         oldHoverEmail != isHoverEmail || oldHoverOut != isHoverLogout) && hParentWnd) {
        InvalidateRect(hParentWnd, NULL, FALSE);
    }
}

// ==========================================
// 🖱️ MOUSE CLICK EVENT
// ==========================================
void ProcessAccountsMouseClick(float x, float y) {
    bool handled = false;

    if (!g_isLoggedIn) {
        // Handle input field focus
        if (isHoverEmail) {
            s_focusedField = 1; handled = true;
        } else if (isHoverPass) {
            s_focusedField = 2; handled = true;
        } else {
            // Clicking elsewhere clears focus, unless clicking links/buttons
            if (!cardRect.Contains(x,y)) {
                 s_focusedField = 0; handled = true;
            }
        }

        // Handle Main Login Submission (inspired by simulated testing in original code)
        if (isHoverLoginSubmit) {
            std::wstring simulatedName = L"Trial User";
            std::wstring simulatedEmail = s_emailInput.empty() ? L"guest@rasfocus.app" : s_emailInput;
            
            // Simulating a successful login with subscription data
            // In a real app, you'd perform Firebase authentication here using Email/Password
            SetUserLoginData(simulatedName, simulatedEmail, L"FIREBASE_TRIAL_123");
            
            // Simulation: decide PRO vs Trial based on some logic (e.g., specific email) for testing
            g_isPremiumUser = (simulatedEmail.find(L"@rasfocus.app") != std::wstring::npos); 

            // Clear buffers on success
            s_emailInput = L"";
            s_passInput = L"";
            s_focusedField = 0;
            handled = true;
        }
    }
    else {
        // Handle Sign Out
        if (isHoverLogout) {
            g_isLoggedIn = false;
            g_userName = L"";
            g_userEmail = L"";
            g_userUID = L"";
            g_isPremiumUser = false;
            handled = true;
        }
    }

    if (handled && hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
}

// ==========================================
// 🎹 KEYBOARD INPUT (Typing in fields)
// ==========================================
// Instructions: Call from your main window WndProc when selectedTab is Accounts
void ProcessAccountsChar(wchar_t c) {
    if (g_isLoggedIn || s_focusedField == 0) return;

    if (c == L'\b') { // Backspace
        if (s_focusedField == 1) { // Email field
            if (!s_emailInput.empty()) s_emailInput.pop_back();
        } else if (s_focusedField == 2) { // Password field
            if (!s_passInput.empty()) s_passInput.pop_back();
        }
    } else if (c >= L' ' || c == L'\t') { // Accept printable characters and tabs
        if (s_focusedField == 1) { // Email field
            if (s_emailInput.length() < 256) s_emailInput += c;
        } else if (s_focusedField == 2) { // Password field
            if (s_passInput.length() < 128) s_passInput += c;
        }
    }
}

// ==========================================
// 🎹 KEYDOWN EVENT (Special keys like Tab/Enter)
// ==========================================
// Instructions: Call from your main window WndProc when selectedTab is Accounts
void ProcessAccountsKeyDown(WPARAM wp) {
    if (g_isLoggedIn || s_focusedField == 0) return;

    if (wp == VK_TAB) {
        // Tab navigation between Email/Password fields
        if (s_focusedField == 1) s_focusedField = 2;
        else if (s_focusedField == 2) s_focusedField = 1;
    } else if (wp == VK_RETURN) {
        // Handle Enter key to submit login form if focus is on fields
        ProcessAccountsMouseMove(LOGIN_SUBMIT_SIM_X, LOGIN_SUBMIT_SIM_Y); // Simulate hover for click logic
        ProcessAccountsMouseClick(LOGIN_SUBMIT_SIM_X, LOGIN_SUBMIT_SIM_Y); // (need proper coords in practice)
        // For simulation, we just directly call the submit logic if focused
        if (s_focusedField > 0) {
            std::wstring simulatedEmail = s_emailInput.empty() ? L"guest@rasfocus.app" : s_emailInput;
            SetUserLoginData(L"Keyboard Entry", simulatedEmail, L"FKBD_123");
             g_isPremiumUser = (simulatedEmail.find(L"@rasfocus.app") != std::wstring::npos); 
             s_emailInput = L""; s_passInput = L""; s_focusedField = 0;
        }
    }
}

// ==========================================
// 📡 SIGNAL RECEIVER (Internal data update)
// ==========================================
// Instructs other app parts to update global tracking
void SetUserLoginData(std::wstring name, std::wstring email, std::wstring uid) {
    g_isLoggedIn = true;
    g_userName = name;
    
    // Ensure "undefined" state from web sources is handled
    if (g_userName.empty() || g_userName == L"undefined" || g_userName == L"Keyboard Entry") {
        g_userName = email;
    }
    
    g_userEmail = email;
    g_userUID = uid;
    
    // You should use proper Firebase subscription verification here to set PRO vs TRIAL
    // g_isPremiumUser = VerifyFirebaseSubscriptionStatus(uid); // Conceptual only

    if (hParentWnd) {
        InvalidateRect(hParentWnd, NULL, FALSE);
    }
}
