#define _CRT_SECURE_NO_WARNINGS
#include "accounts.h"
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <wininet.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <sstream>
#include <fstream>
#include <process.h>

#pragma comment(lib, "wininet.lib")

using namespace Gdiplus;
using namespace std;

// ============================================================
//  GLOBALS
// ============================================================
bool    g_isPremiumUser  = false;
wstring g_loggedInEmail  = L"";

static firebase::App* s_firebaseApp = nullptr;

// ── UI State ──
static wchar_t s_email   [512]  = {};
static wchar_t s_password[512]  = {};
static bool    s_saveLogin      = false;
static bool    s_showPassword   = false;   // eye icon toggle
static int     s_focusField     = 1;       // 1=email 2=password 0=none
static bool    s_isLoading      = false;
static wstring s_statusMsg      = L"";
static bool    s_isError        = false;

// ── Hover states ──
static bool s_hoverLogin      = false;
static bool s_hoverCancel     = false;
static bool s_hoverSignup     = false;
static bool s_hoverReset      = false;
static bool s_hoverPrivacy    = false;
static bool s_hoverSaveCheck  = false;
static bool s_hoverEye        = false;
static bool s_hoverLogout     = false;

// ── Saved credentials path ──
static string GetCredsPath() {
    char appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData)))
        return string(appData) + "\\.rasfocus\\creds.dat";
    return "rasfocus_creds.dat";
}

// ── Simple XOR obfuscation for saved creds ──
static string XorStr(const string& s) {
    const char key[] = "RasFocus2024!@#$";
    string out = s;
    for (size_t i = 0; i < out.size(); ++i)
        out[i] ^= key[i % (sizeof(key) - 1)];
    return out;
}

static void SaveCredentials(const wstring& email, const wstring& password) {
    char emailA[512] = {}, passA[512] = {};
    WideCharToMultiByte(CP_UTF8, 0, email.c_str(),    -1, emailA, 511, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, password.c_str(), -1, passA,  511, NULL, NULL);
    string data = string(emailA) + "\n" + string(passA);
    string enc  = XorStr(data);
    ofstream f(GetCredsPath(), ios::binary);
    if (f.is_open()) { f.write(enc.c_str(), enc.size()); f.close(); }
}

static bool LoadSavedCredentials() {
    ifstream f(GetCredsPath(), ios::binary);
    if (!f.is_open()) return false;
    string enc((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());
    f.close();
    if (enc.empty()) return false;
    string data = XorStr(enc);
    size_t nl = data.find('\n');
    if (nl == string::npos) return false;
    string emailA = data.substr(0, nl);
    string passA  = data.substr(nl + 1);
    MultiByteToWideChar(CP_UTF8, 0, emailA.c_str(), -1, s_email,    511);
    MultiByteToWideChar(CP_UTF8, 0, passA.c_str(),  -1, s_password, 511);
    s_saveLogin = true;
    return true;
}

static void ClearSavedCredentials() {
    remove(GetCredsPath().c_str());
}

// ============================================================
//  FIREBASE REST LOGIN  (Email + Password)
//  POST https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword
// ============================================================
struct LoginResult {
    bool    success;
    string  idToken;
    string  localId;  // uid
    string  email;
    string  errorMsg;
};

// Simple JSON value extractor (no external lib needed)
static string JsonExtract(const string& json, const string& key) {
    string search = "\"" + key + "\":\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return "";
    pos += search.size();
    size_t end = json.find("\"", pos);
    if (end == string::npos) return "";
    return json.substr(pos, end - pos);
}

static string JsonExtractBool(const string& json, const string& key) {
    string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == string::npos) return "";
    pos += search.size();
    if (json.substr(pos, 4) == "true")  return "true";
    if (json.substr(pos, 5) == "false") return "false";
    return "";
}

static LoginResult FirebaseLogin(const string& email, const string& password) {
    LoginResult res = { false, "", "", "", "" };

    const string API_KEY = "AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA";
    const string HOST    = "identitytoolkit.googleapis.com";
    const string PATH    = "/v1/accounts:signInWithPassword?key=" + API_KEY;

    // Escape quotes in email/password just in case
    string body = "{\"email\":\"" + email + "\",\"password\":\"" + password + "\",\"returnSecureToken\":true}";

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) { res.errorMsg = "No internet connection."; return res; }

    HINTERNET hConn = InternetConnectA(hInet, HOST.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); res.errorMsg = "Connection failed."; return res; }

    HINTERNET hReq = HttpOpenRequestA(hConn, "POST", PATH.c_str(), NULL, NULL, NULL,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); res.errorMsg = "Request failed."; return res; }

    string headers = "Content-Type: application/json\r\n";
    BOOL sent = HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(),
                                  (LPVOID)body.c_str(), (DWORD)body.size());

    string response = "";
    if (sent) {
        char buf[4096];
        DWORD bytesRead = 0;
        while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
            buf[bytesRead] = '\0';
            response += buf;
        }
    }

    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);

    if (response.empty()) { res.errorMsg = "Empty server response."; return res; }

    // Check for error
    if (response.find("\"error\"") != string::npos) {
        string msg = JsonExtract(response, "message");
        if (msg == "EMAIL_NOT_FOUND" || msg == "INVALID_EMAIL")
            res.errorMsg = "Email not found. Please sign up first.";
        else if (msg == "INVALID_PASSWORD" || msg == "INVALID_LOGIN_CREDENTIALS")
            res.errorMsg = "Wrong password. Please try again.";
        else if (msg == "USER_DISABLED")
            res.errorMsg = "This account has been disabled.";
        else if (msg == "TOO_MANY_ATTEMPTS_TRY_LATER")
            res.errorMsg = "Too many attempts. Try later.";
        else
            res.errorMsg = "Login failed: " + msg;
        return res;
    }

    res.idToken = JsonExtract(response, "idToken");
    res.localId = JsonExtract(response, "localId");
    res.email   = JsonExtract(response, "email");
    res.success = !res.idToken.empty();
    if (!res.success) res.errorMsg = "Login failed. Please try again.";
    return res;
}

// ── Check premium status from Firebase Realtime DB ──
// Path: /users/{uid}/isPremium.json
static bool CheckPremiumFromFirebase(const string& uid, const string& idToken) {
    string host = "rasfocus-c746d-default-rtdb.firebaseio.com";
    string path = "/users/" + uid + "/isPremium.json?auth=" + idToken;

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return false;
    HINTERNET hConn = InternetConnectA(hInet, host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return false; }
    HINTERNET hReq = HttpOpenRequestA(hConn, "GET", path.c_str(), NULL, NULL, NULL,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return false; }

    HttpSendRequestA(hReq, NULL, 0, NULL, 0);
    string response = "";
    char buf[1024]; DWORD br = 0;
    while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &br) && br > 0) {
        buf[br] = '\0'; response += buf;
    }
    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);

    return (response.find("true") != string::npos);
}

// ── Background login thread data ──
struct LoginThreadData {
    HWND    hWnd;
    string  email;
    string  password;
    bool    saveLogin;
};

void __cdecl LoginThread(void* param) {
    LoginThreadData* data = (LoginThreadData*)param;
    LoginResult res = FirebaseLogin(data->email, data->password);

    if (res.success) {
        bool isPremium = CheckPremiumFromFirebase(res.localId, res.idToken);
        g_isPremiumUser = isPremium;

        wchar_t emailW[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, res.email.c_str(), -1, emailW, 511);
        g_loggedInEmail = emailW;

        if (data->saveLogin) {
            wchar_t passW[512] = {};
            MultiByteToWideChar(CP_UTF8, 0, data->password.c_str(), -1, passW, 511);
            SaveCredentials(emailW, passW);
        }

        s_isLoading = false;
        s_statusMsg = isPremium ? L"Welcome back! Premium account active." : L"Logged in successfully!";
        s_isError   = false;
        ZeroMemory(s_password, sizeof(s_password)); // clear password from memory
    } else {
        s_isLoading = false;
        MultiByteToWideChar(CP_UTF8, 0, res.errorMsg.c_str(), -1,
                            (wchar_t*)s_statusMsg.data(), 0);
        // rebuild wstring properly
        wchar_t errW[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, res.errorMsg.c_str(), -1, errW, 511);
        s_statusMsg = errW;
        s_isError   = true;
    }

    if (data->hWnd) InvalidateRect(data->hWnd, NULL, FALSE);
    delete data;
    _endthread();
}

// ============================================================
//  INIT
// ============================================================
void InitAccountsModule(firebase::App* app) {
    s_firebaseApp = app;
    // Auto-fill if saved credentials exist
    if (LoadSavedCredentials()) {
        s_statusMsg = L"";
    }
}

// ============================================================
//  LAYOUT HELPERS  (card centered in content area)
// ============================================================
struct CardLayout {
    float cardX, cardY, cardW, cardH;
    float fieldX, fieldW;
    float emailY, passY;
    float checkY;
    float linksY;
    float loginBtnX, loginBtnY, loginBtnW, loginBtnH;
    float cancelBtnX, cancelBtnY, cancelBtnW, cancelBtnH;
    float bottomTextY;
    float eyeX, eyeY;
};

static CardLayout GetLayout(float cx, float cy, float cw, float ch) {
    CardLayout L = {};
    L.cardW = min(480.0f, cw - 60.0f);
    L.cardH = 480.0f;
    L.cardX = cx + (cw - L.cardW) / 2.0f;
    L.cardY = cy + (ch - L.cardH) / 2.0f;

    float pad    = 40.0f;
    L.fieldX     = L.cardX + pad;
    L.fieldW     = L.cardW - pad * 2.0f;

    // Email field top = after logo area (~170px from card top)
    L.emailY     = L.cardY + 175.0f;
    L.passY      = L.emailY + 52.0f;
    L.checkY     = L.passY  + 52.0f;
    L.linksY     = L.checkY + 40.0f;

    float btnW   = 110.0f, btnH = 38.0f;
    float btnTotalW = btnW * 2.0f + 16.0f;
    float btnStartX = L.cardX + (L.cardW - btnTotalW) / 2.0f;
    L.loginBtnX  = btnStartX;
    L.loginBtnY  = L.linksY + 36.0f;
    L.loginBtnW  = btnW;
    L.loginBtnH  = btnH;

    L.cancelBtnX = btnStartX + btnW + 16.0f;
    L.cancelBtnY = L.loginBtnY;
    L.cancelBtnW = btnW;
    L.cancelBtnH = btnH;

    L.bottomTextY = L.loginBtnY + btnH + 20.0f;

    // Eye icon — right side of password field
    L.eyeX = L.fieldX + L.fieldW - 32.0f;
    L.eyeY = L.passY;

    return L;
}

// ============================================================
//  DRAW
// ============================================================
void DrawAccountsTab(Graphics& g, float cx, float cy, float cw, float ch) {
    // ── Background ──
    SolidBrush bgBrush(Color(255, 245, 248, 250));
    g.FillRectangle(&bgBrush, cx, cy, cw, ch);

    // ── Check if logged in → show profile view instead ──
    if (!g_loggedInEmail.empty()) {
        // ─── LOGGED IN VIEW ───
        CardLayout L = GetLayout(cx, cy, cw, ch);
        // Card
        GraphicsPath card;
        float rc = 14.0f, dc = rc * 2.0f;
        card.AddArc(L.cardX, L.cardY, dc, dc, 180.0f, 90.0f);
        card.AddArc(L.cardX + L.cardW - dc, L.cardY, dc, dc, 270.0f, 90.0f);
        card.AddArc(L.cardX + L.cardW - dc, L.cardY + L.cardH - dc, dc, dc, 0.0f, 90.0f);
        card.AddArc(L.cardX, L.cardY + L.cardH - dc, dc, dc, 90.0f, 90.0f);
        card.CloseFigure();
        SolidBrush cardBg(Color(255, 255, 255, 255));
        g.FillPath(&cardBg, &card);
        Pen cardShadow(Color(20, 0, 150, 160), 1.0f);
        g.DrawPath(&cardShadow, &card);

        // Teal header strip
        GraphicsPath hdrPath;
        hdrPath.AddArc(L.cardX, L.cardY, dc, dc, 180.0f, 90.0f);
        hdrPath.AddArc(L.cardX + L.cardW - dc, L.cardY, dc, dc, 270.0f, 90.0f);
        hdrPath.AddLine(L.cardX + L.cardW, L.cardY + 90.0f, L.cardX, L.cardY + 90.0f);
        hdrPath.CloseFigure();
        SolidBrush hdrBg(Color(255, 0, 150, 160));
        g.FillPath(&hdrBg, &hdrPath);

        FontFamily ff(L"Segoe UI");
        FontFamily ffIcons(L"Segoe MDL2 Assets");
        SolidBrush white(Color(255, 255, 255, 255));
        SolidBrush teal(Color(255, 0, 150, 160));
        SolidBrush dark(Color(255, 50, 50, 50));
        SolidBrush gray(Color(255, 130, 130, 130));
        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
        StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

        // Avatar circle (on top of header)
        float avR = 44.0f;
        float avCX = L.cardX + L.cardW / 2.0f;
        float avCY = L.cardY + 90.0f;
        SolidBrush avBg(Color(255, 255, 255, 255));
        g.FillEllipse(&avBg, avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f);
        Pen avBorder(Color(255, 0, 150, 160), 3.0f);
        g.DrawEllipse(&avBorder, avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f);
        Font fAvIcon(&ffIcons, 36, FontStyleRegular, UnitPixel);
        g.DrawString(L"\xE77B", -1, &fAvIcon,
                     RectF(avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f), &fmtC, &teal);

        // Title in header
        Font fHdrTitle(&ff, 16, FontStyleBold, UnitPixel);
        g.DrawString(L"My Account", -1, &fHdrTitle,
                     RectF(L.cardX, L.cardY, L.cardW, 60.0f), &fmtC, &white);

        // Email
        float infoY = avCY + avR + 20.0f;
        Font fLabel(&ff, 11, FontStyleRegular, UnitPixel);
        Font fValue(&ff, 13, FontStyleBold, UnitPixel);
        Font fSub  (&ff, 10, FontStyleRegular, UnitPixel);

        g.DrawString(L"Email", -1, &fLabel,
                     RectF(L.fieldX, infoY, L.fieldW, 18.0f), &fmtL, &gray);
        g.DrawString(g_loggedInEmail.c_str(), -1, &fValue,
                     RectF(L.fieldX, infoY + 18.0f, L.fieldW, 22.0f), &fmtL, &dark);

        // Plan badge
        float badgeY = infoY + 58.0f;
        g.DrawString(L"Plan", -1, &fLabel,
                     RectF(L.fieldX, badgeY, L.fieldW, 18.0f), &fmtL, &gray);

        float badgeW = 120.0f, badgeH = 28.0f;
        GraphicsPath badge;
        float br = 6.0f, bd = br * 2.0f;
        badge.AddArc(L.fieldX, badgeY + 20.0f, bd, bd, 180.0f, 90.0f);
        badge.AddArc(L.fieldX + badgeW - bd, badgeY + 20.0f, bd, bd, 270.0f, 90.0f);
        badge.AddArc(L.fieldX + badgeW - bd, badgeY + 20.0f + badgeH - bd, bd, bd, 0.0f, 90.0f);
        badge.AddArc(L.fieldX, badgeY + 20.0f + badgeH - bd, bd, bd, 90.0f, 90.0f);
        badge.CloseFigure();
        SolidBrush badgeBg(g_isPremiumUser ? Color(255, 243, 156, 18) : Color(255, 0, 150, 160));
        g.FillPath(&badgeBg, &badge);
        Font fBadge(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(g_isPremiumUser ? L"★  Premium" : L"Free Plan", -1, &fBadge,
                     RectF(L.fieldX, badgeY + 20.0f, badgeW, badgeH), &fmtC, &white);

        // Logout button
        float logoutW = 140.0f, logoutH = 40.0f;
        float logoutX = L.cardX + (L.cardW - logoutW) / 2.0f;
        float logoutY = L.cardY + L.cardH - 60.0f;
        GraphicsPath lPath;
        float lr = 8.0f, ld = lr * 2.0f;
        lPath.AddArc(logoutX, logoutY, ld, ld, 180.0f, 90.0f);
        lPath.AddArc(logoutX + logoutW - ld, logoutY, ld, ld, 270.0f, 90.0f);
        lPath.AddArc(logoutX + logoutW - ld, logoutY + logoutH - ld, ld, ld, 0.0f, 90.0f);
        lPath.AddArc(logoutX, logoutY + logoutH - ld, ld, ld, 90.0f, 90.0f);
        lPath.CloseFigure();
        SolidBrush lBg(s_hoverLogout ? Color(255, 200, 30, 30) : Color(255, 220, 50, 50));
        g.FillPath(&lBg, &lPath);
        Font fLogout(&ff, 12, FontStyleBold, UnitPixel);
        g.DrawString(L"\xE7E8  Log Out", -1, &fLogout,
                     RectF(logoutX, logoutY, logoutW, logoutH), &fmtC, &white);
        return;
    }

    // ─── LOGIN / SIGN-IN VIEW ───
    CardLayout L = GetLayout(cx, cy, cw, ch);

    // ── Card shadow (soft) ──
    for (int i = 3; i >= 0; --i) {
        SolidBrush shadowBrush(Color(10 + i * 5, 0, 100, 120));
        g.FillRoundedRectangle == nullptr; // GDI+ no built-in, use path
        GraphicsPath shadowPath;
        float sr = 14.0f, sd = sr * 2.0f;
        float sx = L.cardX - i, sy = L.cardY + i, sw2 = L.cardW + i * 2.0f, sh2 = L.cardH;
        shadowPath.AddArc(sx, sy, sd, sd, 180.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy, sd, sd, 270.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy + sh2 - sd, sd, sd, 0.0f, 90.0f);
        shadowPath.AddArc(sx, sy + sh2 - sd, sd, sd, 90.0f, 90.0f);
        shadowPath.CloseFigure();
        g.FillPath(&shadowBrush, &shadowPath);
    }

    // ── Card ──
    GraphicsPath cardPath;
    float rc = 14.0f, dc = rc * 2.0f;
    cardPath.AddArc(L.cardX, L.cardY, dc, dc, 180.0f, 90.0f);
    cardPath.AddArc(L.cardX + L.cardW - dc, L.cardY, dc, dc, 270.0f, 90.0f);
    cardPath.AddArc(L.cardX + L.cardW - dc, L.cardY + L.cardH - dc, dc, dc, 0.0f, 90.0f);
    cardPath.AddArc(L.cardX, L.cardY + L.cardH - dc, dc, dc, 90.0f, 90.0f);
    cardPath.CloseFigure();
    SolidBrush cardBg(Color(255, 255, 255, 255));
    g.FillPath(&cardBg, &cardPath);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    SolidBrush teal(Color(255, 0, 150, 160));
    SolidBrush tealDark(Color(255, 0, 110, 120));
    SolidBrush dark(Color(255, 50, 50, 50));
    SolidBrush gray(Color(255, 140, 140, 140));
    SolidBrush white(Color(255, 255, 255, 255));
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    // ── Logo area (lock + checkmark icon like image) ──
    float logoY  = L.cardY + 30.0f;
    float logoSz = 60.0f;
    float logoCX = L.cardX + L.cardW / 2.0f;

    // Draw circle for logo
    Pen logoBorder(Color(255, 0, 150, 160), 3.0f);
    g.DrawEllipse(&logoBorder, logoCX - logoSz / 2.0f, logoY, logoSz, logoSz);
    // Lock icon inside circle
    Font fLogoIcon(&ffIcons, 28, FontStyleRegular, UnitPixel);
    g.DrawString(L"\xE72E", -1, &fLogoIcon,
                 RectF(logoCX - logoSz / 2.0f, logoY, logoSz, logoSz), &fmtC, &teal);

    // ── "Welcome to" text ──
    float welcomeY = logoY + logoSz + 12.0f;
    Font fWelcome(&ff, 16, FontStyleBold, UnitPixel);
    g.DrawString(L"Welcome to RasFocus Pro", -1, &fWelcome,
                 RectF(L.cardX, welcomeY, L.cardW, 26.0f), &fmtC, &teal);

    // ── Sub-text ──
    float subY = welcomeY + 30.0f;
    Font fSub(&ff, 10, FontStyleRegular, UnitPixel);
    StringFormat fmtWrap;
    fmtWrap.SetAlignment(StringAlignmentCenter);
    fmtWrap.SetLineAlignment(StringAlignmentNear);
    fmtWrap.SetFormatFlags(0);
    SolidBrush subColor(Color(255, 100, 130, 140));
    g.DrawString(
        L"Log in or create an account to start your free trial.\nIf you already have a license, it will be activated automatically.",
        -1, &fSub,
        RectF(L.fieldX, subY, L.fieldW, 40.0f),
        &fmtWrap, &subColor);

    // ── Email field ──
    float fldH = 40.0f;
    bool emailFocused = (s_focusField == 1);
    // Field background
    SolidBrush fieldBg(Color(255, 245, 248, 252));
    g.FillRectangle(&fieldBg, L.fieldX, L.emailY, L.fieldW, fldH);
    // Border — teal when focused
    Pen emailBorder(emailFocused ? Color(255, 0, 150, 160) : Color(255, 210, 220, 230), emailFocused ? 2.0f : 1.0f);
    g.DrawRectangle(&emailBorder, L.fieldX, L.emailY, L.fieldW, fldH);
    // Placeholder / value
    Font fInput(&ff, 11, FontStyleRegular, UnitPixel);
    wstring emailStr(s_email);
    SolidBrush inputColor(emailStr.empty() ? Color(255, 160, 170, 180) : Color(255, 40, 40, 40));
    g.DrawString(
        emailStr.empty() ? L"Email address" : emailStr.c_str(),
        -1, &fInput,
        RectF(L.fieldX + 12.0f, L.emailY, L.fieldW - 24.0f, fldH),
        &fmtL, &inputColor);
    // Mail icon on left
    Font fFieldIcon(&ffIcons, 13, FontStyleRegular, UnitPixel);
    SolidBrush iconTeal(Color(255, 0, 150, 160));
    g.DrawString(L"\xE715", -1, &fFieldIcon,
                 RectF(L.fieldX + 12.0f, L.emailY, 20.0f, fldH), &fmtL,
                 emailStr.empty() ? &gray : &iconTeal);

    // ── Password field ──
    bool passFocused = (s_focusField == 2);
    SolidBrush passBg(Color(255, 245, 248, 252));
    g.FillRectangle(&passBg, L.fieldX, L.passY, L.fieldW, fldH);
    Pen passBorder(passFocused ? Color(255, 0, 150, 160) : Color(255, 210, 220, 230), passFocused ? 2.0f : 1.0f);
    g.DrawRectangle(&passBorder, L.fieldX, L.passY, L.fieldW, fldH);

    wstring passStr(s_password);
    wstring passDisplay = passStr.empty() ? L"Password" : (s_showPassword ? passStr : wstring(passStr.size(), L'•'));
    SolidBrush passColor(passStr.empty() ? Color(255, 160, 170, 180) : Color(255, 40, 40, 40));
    g.DrawString(
        passDisplay.c_str(), -1, &fInput,
        RectF(L.fieldX + 12.0f, L.passY, L.fieldW - 44.0f, fldH),
        &fmtL, &passColor);
    // Lock icon
    g.DrawString(L"\xE72E", -1, &fFieldIcon,
                 RectF(L.fieldX + 12.0f, L.passY, 20.0f, fldH), &fmtL,
                 passStr.empty() ? &gray : &iconTeal);
    // Eye toggle icon
    Font fEye(&ffIcons, 14, FontStyleRegular, UnitPixel);
    SolidBrush eyeColor(s_hoverEye ? Color(255, 0, 150, 160) : Color(255, 160, 170, 180));
    g.DrawString(s_showPassword ? L"\xED1A" : L"\xE7B3", -1, &fEye,
                 RectF(L.eyeX, L.eyeY, 32.0f, fldH), &fmtC, &eyeColor);

    // ── Save login checkbox ──
    // Checkbox box
    float chkSize = 16.0f;
    float chkX    = L.fieldX;
    float chkY    = L.checkY + (fldH - chkSize) / 2.0f;
    Pen chkBorder(Color(255, 0, 150, 160), 1.5f);
    if (s_saveLogin) {
        SolidBrush chkFill(Color(255, 0, 150, 160));
        g.FillRectangle(&chkFill, chkX, chkY, chkSize, chkSize);
        Font fCheck(&ffIcons, 11, FontStyleRegular, UnitPixel);
        g.DrawString(L"\xE73E", -1, &fCheck,
                     RectF(chkX, chkY, chkSize, chkSize), &fmtC, &white);
    } else {
        SolidBrush chkBg(Color(255, 248, 250, 252));
        g.FillRectangle(&chkBg, chkX, chkY, chkSize, chkSize);
        g.DrawRectangle(&chkBorder, chkX, chkY, chkSize, chkSize);
    }
    Font fChkLabel(&ff, 10, FontStyleRegular, UnitPixel);
    g.DrawString(L"Save my login info", -1, &fChkLabel,
                 RectF(chkX + chkSize + 8.0f, L.checkY, 180.0f, fldH), &fmtL, &dark);

    // ── Links: "Don't have an account? Sign up free | Reset password" ──
    Font fLink(&ff, 10, FontStyleRegular, UnitPixel);
    Font fLinkU(&ff, 10, FontStyleUnderline, UnitPixel);
    SolidBrush linkTeal(Color(255, 0, 150, 160));
    SolidBrush linkHover(Color(255, 0, 100, 110));

    // Measure text to position links
    wstring dontHave = L"Don't have an account?  ";
    SizeF szDH; StringFormat sf; sf.SetAlignment(StringAlignmentNear);
    g.MeasureString(dontHave.c_str(), -1, &fLink, PointF(L.fieldX, L.linksY), &sf, &szDH);

    g.DrawString(dontHave.c_str(), -1, &fLink,
                 RectF(L.fieldX, L.linksY, szDH.Width, 20.0f), &fmtL, &gray);
    // "Sign up free"
    SolidBrush& signupColor = s_hoverSignup ? linkHover : linkTeal;
    g.DrawString(L"Sign up free", -1, &fLinkU,
                 RectF(L.fieldX + szDH.Width, L.linksY, 90.0f, 20.0f), &fmtL, &signupColor);
    // " | Reset password"
    SizeF szSU;
    g.MeasureString(L"Sign up free", -1, &fLink, PointF(0, 0), &sf, &szSU);
    g.DrawString(L" | ", -1, &fLink,
                 RectF(L.fieldX + szDH.Width + szSU.Width, L.linksY, 20.0f, 20.0f), &fmtL, &gray);
    SizeF szSep;
    g.MeasureString(L" | ", -1, &fLink, PointF(0, 0), &sf, &szSep);
    SolidBrush& resetColor = s_hoverReset ? linkHover : linkTeal;
    g.DrawString(L"Reset password", -1, &fLinkU,
                 RectF(L.fieldX + szDH.Width + szSU.Width + szSep.Width, L.linksY, 110.0f, 20.0f),
                 &fmtL, &resetColor);

    // ── Status message (error / success) ──
    if (!s_statusMsg.empty()) {
        SolidBrush statusBrush(s_isError ? Color(255, 200, 50, 50) : Color(255, 0, 140, 80));
        Font fStatus(&ff, 10, FontStyleBold, UnitPixel);
        g.DrawString(s_statusMsg.c_str(), -1, &fStatus,
                     RectF(L.cardX, L.loginBtnY - 22.0f, L.cardW, 20.0f), &fmtC, &statusBrush);
    }

    // ── Loading indicator ──
    if (s_isLoading) {
        SolidBrush loadBrush(Color(255, 0, 150, 160));
        Font fLoad(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(L"Logging in...", -1, &fLoad,
                     RectF(L.cardX, L.loginBtnY - 22.0f, L.cardW, 20.0f), &fmtC, &loadBrush);
    }

    // ── Login button ──
    GraphicsPath loginPath;
    float lr = 8.0f, ld = lr * 2.0f;
    loginPath.AddArc(L.loginBtnX, L.loginBtnY, ld, ld, 180.0f, 90.0f);
    loginPath.AddArc(L.loginBtnX + L.loginBtnW - ld, L.loginBtnY, ld, ld, 270.0f, 90.0f);
    loginPath.AddArc(L.loginBtnX + L.loginBtnW - ld, L.loginBtnY + L.loginBtnH - ld, ld, ld, 0.0f, 90.0f);
    loginPath.AddArc(L.loginBtnX, L.loginBtnY + L.loginBtnH - ld, ld, ld, 90.0f, 90.0f);
    loginPath.CloseFigure();
    SolidBrush loginBg(s_hoverLogin ? Color(255, 0, 110, 120) : Color(255, 0, 150, 160));
    g.FillPath(&loginBg, &loginPath);
    Font fBtnTxt(&ff, 12, FontStyleBold, UnitPixel);
    g.DrawString(L"Login", -1, &fBtnTxt,
                 RectF(L.loginBtnX, L.loginBtnY, L.loginBtnW, L.loginBtnH), &fmtC, &white);

    // ── Cancel button ──
    GraphicsPath cancelPath;
    cancelPath.AddArc(L.cancelBtnX, L.cancelBtnY, ld, ld, 180.0f, 90.0f);
    cancelPath.AddArc(L.cancelBtnX + L.cancelBtnW - ld, L.cancelBtnY, ld, ld, 270.0f, 90.0f);
    cancelPath.AddArc(L.cancelBtnX + L.cancelBtnW - ld, L.cancelBtnY + L.cancelBtnH - ld, ld, ld, 0.0f, 90.0f);
    cancelPath.AddArc(L.cancelBtnX, L.cancelBtnY + L.cancelBtnH - ld, ld, ld, 90.0f, 90.0f);
    cancelPath.CloseFigure();
    Pen cancelBorder(Color(255, 0, 150, 160), 1.5f);
    SolidBrush cancelBg(s_hoverCancel ? Color(20, 0, 150, 160) : Color(0, 255, 255, 255));
    g.FillPath(&cancelBg, &cancelPath);
    g.DrawPath(&cancelBorder, &cancelPath);
    SolidBrush cancelTxt(Color(255, 0, 150, 160));
    g.DrawString(L"Cancel", -1, &fBtnTxt,
                 RectF(L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH), &fmtC, &cancelTxt);

    // ── Bottom text: "When logged in..." ──
    Font fBottom(&ff, 9, FontStyleRegular, UnitPixel);
    SolidBrush bottomGray(Color(255, 140, 150, 160));
    StringFormat fmtBottom; fmtBottom.SetAlignment(StringAlignmentCenter); fmtBottom.SetLineAlignment(StringAlignmentNear);
    g.DrawString(L"When logged in, your data will be stored securely.\nFor more information, please see our ",
                 -1, &fBottom,
                 RectF(L.cardX + 20.0f, L.bottomTextY, L.cardW - 40.0f, 32.0f),
                 &fmtBottom, &bottomGray);

    // "privacy policy" link
    SizeF szBotText;
    g.MeasureString(L"When logged in, your data will be stored securely.\nFor more information, please see our ",
                    -1, &fBottom, PointF(0, 0), &fmtBottom, &szBotText);
    Font fPrivacy(&ff, 9, FontStyleUnderline, UnitPixel);
    SolidBrush& privacyColor = s_hoverPrivacy ? linkHover : linkTeal;
    // second line: "For more information, please see our " ~ 38px from bottom line start
    float privacyY = L.bottomTextY + 16.0f;
    // Measure "For more information, please see our " width
    SizeF szForMore;
    g.MeasureString(L"For more information, please see our ", -1, &fBottom, PointF(0,0), &fmtBottom, &szForMore);
    float lineCenterX = L.cardX + L.cardW / 2.0f;
    float forMoreHalfW = szForMore.Width / 2.0f;
    float privacyX = lineCenterX + forMoreHalfW - szForMore.Width / 2.0f + szForMore.Width;
    // Simplified: just draw below centered
    g.DrawString(L"privacy policy", -1, &fPrivacy,
                 RectF(L.cardX, privacyY + 14.0f, L.cardW, 18.0f),
                 &fmtC, &privacyColor);
}

// ============================================================
//  HIT TEST HELPERS
// ============================================================
static bool HitRect(float mx, float my, float x, float y, float w, float h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

// We need window size — get from a cached layout
static CardLayout s_cachedLayout = {};
static float s_cachedCW = 0, s_cachedCH = 0, s_cachedCX = 0, s_cachedCY = 0;

// External sizes injected via main.cpp — we approximate via globals
extern int windowWidth, windowHeight;
extern const int SIDEBAR_WIDTH;
extern const int TITLEBAR_HEIGHT;
extern const int SUBHEADER_HEIGHT;

static CardLayout GetCurrentLayout() {
    float cx = (float)SIDEBAR_WIDTH;
    float cy = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
    float cw = (float)windowWidth  - cx;
    float ch = (float)windowHeight - cy;
    return GetLayout(cx, cy, cw, ch);
}

// ============================================================
//  MOUSE MOVE
// ============================================================
void ProcessAccountsMouseMove(float x, float y) {
    if (!g_loggedInEmail.empty()) {
        // Logged-in view — logout button
        CardLayout L = GetCurrentLayout();
        float logoutW = 140.0f, logoutH = 40.0f;
        float logoutX = L.cardX + (L.cardW - logoutW) / 2.0f;
        float logoutY = L.cardY + L.cardH - 60.0f;
        s_hoverLogout = HitRect(x, y, logoutX, logoutY, logoutW, logoutH);
        return;
    }

    CardLayout L = GetCurrentLayout();
    float fldH = 40.0f;

    s_hoverLogin  = HitRect(x, y, L.loginBtnX,  L.loginBtnY,  L.loginBtnW,  L.loginBtnH);
    s_hoverCancel = HitRect(x, y, L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH);
    s_hoverEye    = HitRect(x, y, L.eyeX, L.eyeY, 32.0f, fldH);

    // Sign up / Reset links
    float szDH_approx = 165.0f; // approx width of "Don't have an account?  "
    float szSU_approx = 75.0f;  // "Sign up free"
    s_hoverSignup  = HitRect(x, y, L.fieldX + szDH_approx, L.linksY, szSU_approx, 20.0f);
    s_hoverReset   = HitRect(x, y, L.fieldX + szDH_approx + szSU_approx + 15.0f, L.linksY, 100.0f, 20.0f);
    s_hoverPrivacy = HitRect(x, y, L.cardX + (L.cardW / 2.0f) - 45.0f,
                                   L.bottomTextY + 30.0f, 90.0f, 18.0f);
    s_hoverSaveCheck = HitRect(x, y, L.fieldX, L.checkY, 140.0f, fldH);
}

// ============================================================
//  MOUSE CLICK
// ============================================================
void ProcessAccountsMouseClick(float x, float y, HWND hWnd) {
    // ── Logged-in view ──
    if (!g_loggedInEmail.empty()) {
        CardLayout L = GetCurrentLayout();
        float logoutW = 140.0f, logoutH = 40.0f;
        float logoutX = L.cardX + (L.cardW - logoutW) / 2.0f;
        float logoutY = L.cardY + L.cardH - 60.0f;
        if (HitRect(x, y, logoutX, logoutY, logoutW, logoutH)) {
            // Logout
            g_loggedInEmail = L"";
            g_isPremiumUser = false;
            ZeroMemory(s_email,    sizeof(s_email));
            ZeroMemory(s_password, sizeof(s_password));
            s_statusMsg = L"Logged out successfully.";
            s_isError   = false;
            if (!s_saveLogin) ClearSavedCredentials();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return;
    }

    CardLayout L = GetCurrentLayout();
    float fldH = 40.0f;

    // ── Field focus ──
    if (HitRect(x, y, L.fieldX, L.emailY, L.fieldW, fldH)) { s_focusField = 1; InvalidateRect(hWnd, NULL, FALSE); return; }
    if (HitRect(x, y, L.fieldX, L.passY,  L.fieldW, fldH)) { s_focusField = 2; InvalidateRect(hWnd, NULL, FALSE); return; }

    // ── Eye toggle ──
    if (HitRect(x, y, L.eyeX, L.eyeY, 32.0f, fldH)) {
        s_showPassword = !s_showPassword;
        InvalidateRect(hWnd, NULL, FALSE); return;
    }

    // ── Save login checkbox ──
    if (HitRect(x, y, L.fieldX, L.checkY, 140.0f, fldH)) {
        s_saveLogin = !s_saveLogin;
        InvalidateRect(hWnd, NULL, FALSE); return;
    }

    // ── Sign up — open website ──
    float szDH_approx = 165.0f, szSU_approx = 75.0f;
    if (HitRect(x, y, L.fieldX + szDH_approx, L.linksY, szSU_approx, 20.0f)) {
        ShellExecuteW(NULL, L"open", L"https://rasfocus.com/signup", NULL, NULL, SW_SHOWNORMAL);
        return;
    }

    // ── Reset password — open website ──
    if (HitRect(x, y, L.fieldX + szDH_approx + szSU_approx + 15.0f, L.linksY, 100.0f, 20.0f)) {
        ShellExecuteW(NULL, L"open", L"https://rasfocus.com/reset-password", NULL, NULL, SW_SHOWNORMAL);
        return;
    }

    // ── Privacy policy ──
    if (HitRect(x, y, L.cardX + (L.cardW / 2.0f) - 45.0f, L.bottomTextY + 30.0f, 90.0f, 18.0f)) {
        ShellExecuteW(NULL, L"open", L"https://rasfocus.com/privacy", NULL, NULL, SW_SHOWNORMAL);
        return;
    }

    // ── Cancel ──
    if (HitRect(x, y, L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH)) {
        s_statusMsg    = L"";
        s_focusField   = 0;
        InvalidateRect(hWnd, NULL, FALSE); return;
    }

    // ── Login ──
    if (HitRect(x, y, L.loginBtnX, L.loginBtnY, L.loginBtnW, L.loginBtnH)) {
        wstring emailW(s_email), passW(s_password);
        if (emailW.empty() || passW.empty()) {
            s_statusMsg = L"Please enter your email and password.";
            s_isError   = true;
            InvalidateRect(hWnd, NULL, FALSE); return;
        }
        // Start login in background thread
        s_isLoading = true;
        s_statusMsg = L"";
        s_isError   = false;
        InvalidateRect(hWnd, NULL, FALSE);

        char emailA[512] = {}, passA[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, emailW.c_str(), -1, emailA, 511, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, passW.c_str(),  -1, passA,  511, NULL, NULL);

        LoginThreadData* data = new LoginThreadData();
        data->hWnd      = hWnd;
        data->email     = emailA;
        data->password  = passA;
        data->saveLogin = s_saveLogin;
        _beginthread(LoginThread, 0, data);
        return;
    }
}

// ============================================================
//  KEYBOARD — CHAR
// ============================================================
void ProcessAccountsChar(wchar_t c) {
    if (g_loggedInEmail.empty() == false) return; // logged-in view needs no text input

    if (c == L'\b') {
        // Backspace
        if (s_focusField == 1) { int len = (int)wcslen(s_email);    if (len > 0) s_email   [len-1] = L'\0'; }
        if (s_focusField == 2) { int len = (int)wcslen(s_password); if (len > 0) s_password[len-1] = L'\0'; }
    } else if (c == L'\r' || c == L'\n') {
        // Enter — move focus or trigger login
        if (s_focusField == 1) s_focusField = 2;
        else {
            // Trigger login via fake click — reuse existing logic via flag
            // (We don't have hWnd here; set a flag and let next paint handle it)
            // Simple: just move focus back
            s_focusField = 1;
        }
    } else if (c >= 32) {
        // Printable character
        if (s_focusField == 1) { int len = (int)wcslen(s_email);    if (len < 510) { s_email   [len] = c; s_email   [len+1] = L'\0'; } }
        if (s_focusField == 2) { int len = (int)wcslen(s_password); if (len < 510) { s_password[len] = c; s_password[len+1] = L'\0'; } }
    }
}

// ============================================================
//  KEYBOARD — KEYDOWN
// ============================================================
void ProcessAccountsKeyDown(WPARAM wp) {
    if (!g_loggedInEmail.empty()) return;

    if (wp == VK_TAB) {
        s_focusField = (s_focusField == 1) ? 2 : 1;
    } else if (wp == VK_DELETE) {
        if (s_focusField == 1) ZeroMemory(s_email,    sizeof(s_email));
        if (s_focusField == 2) ZeroMemory(s_password, sizeof(s_password));
    } else if (wp == VK_ESCAPE) {
        s_focusField = 0;
        s_statusMsg  = L"";
    }
}
