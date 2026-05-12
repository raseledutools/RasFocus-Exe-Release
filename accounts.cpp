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

#ifndef IDI_APP_ICON
#define IDI_APP_ICON 101
#endif

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
static bool    s_showPassword   = false;
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
// ============================================================
struct LoginResult {
    bool    success;
    string  idToken;
    string  localId;
    string  email;
    string  errorMsg;
};

static string JsonExtract(const string& json, const string& key) {
    string search = "\"" + key + "\":\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return "";
    pos += search.size();
    size_t end = json.find("\"", pos);
    if (end == string::npos) return "";
    return json.substr(pos, end - pos);
}

static LoginResult FirebaseLogin(const string& email, const string& password) {
    LoginResult res = { false, "", "", "", "" };
    const string API_KEY = "AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA";
    const string HOST    = "identitytoolkit.googleapis.com";
    const string PATH    = "/v1/accounts:signInWithPassword?key=" + API_KEY;
    string body = "{\"email\":\"" + email + "\",\"password\":\"" + password + "\",\"returnSecureToken\":true}";

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) { res.errorMsg = "No internet connection."; return res; }

    HINTERNET hConn = InternetConnectA(hInet, HOST.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); res.errorMsg = "Connection failed."; return res; }

    HINTERNET hReq = HttpOpenRequestA(hConn, "POST", PATH.c_str(), NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); res.errorMsg = "Request failed."; return res; }

    string headers = "Content-Type: application/json\r\n";
    BOOL sent = HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(), (LPVOID)body.c_str(), (DWORD)body.size());

    string response = "";
    if (sent) {
        char buf[4096]; DWORD bytesRead = 0;
        while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
            buf[bytesRead] = '\0'; response += buf;
        }
    }
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);

    if (response.empty()) { res.errorMsg = "Empty server response."; return res; }
    if (response.find("\"error\"") != string::npos) {
        string msg = JsonExtract(response, "message");
        if (msg == "EMAIL_NOT_FOUND" || msg == "INVALID_EMAIL") res.errorMsg = "Email not found. Please sign up first.";
        else if (msg == "INVALID_PASSWORD" || msg == "INVALID_LOGIN_CREDENTIALS") res.errorMsg = "Wrong password. Please try again.";
        else if (msg == "USER_DISABLED") res.errorMsg = "This account has been disabled.";
        else if (msg == "TOO_MANY_ATTEMPTS_TRY_LATER") res.errorMsg = "Too many attempts. Try later.";
        else res.errorMsg = "Login failed: " + msg;
        return res;
    }

    res.idToken = JsonExtract(response, "idToken");
    res.localId = JsonExtract(response, "localId");
    res.email   = JsonExtract(response, "email");
    res.success = !res.idToken.empty();
    if (!res.success) res.errorMsg = "Login failed. Please try again.";
    return res;
}

static bool CheckPremiumFromFirebase(const string& uid, const string& idToken) {
    string host = "rasfocus-c746d-default-rtdb.firebaseio.com";
    string path = "/users/" + uid + "/isPremium.json?auth=" + idToken;

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return false;
    HINTERNET hConn = InternetConnectA(hInet, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return false; }
    HINTERNET hReq = HttpOpenRequestA(hConn, "GET", path.c_str(), NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return false; }

    HttpSendRequestA(hReq, NULL, 0, NULL, 0);
    string response = ""; char buf[1024]; DWORD br = 0;
    while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &br) && br > 0) { buf[br] = '\0'; response += buf; }
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);
    return (response.find("true") != string::npos);
}

struct LoginThreadData { HWND hWnd; string email; string password; bool saveLogin; };

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
        ZeroMemory(s_password, sizeof(s_password));
    } else {
        s_isLoading = false;
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
    if (LoadSavedCredentials()) s_statusMsg = L"";
}

// ============================================================
//  LAYOUT EXACT CALCULATIONS (For 100% accurate Hit Testing)
// ============================================================
struct CardLayout {
    float cardX, cardY, cardW, cardH;
    float logoSz, logoX, logoY;
    float fieldX, fieldW, fldH;
    float emailY, passY, checkY, linksY;
    float loginBtnX, loginBtnY, loginBtnW, loginBtnH;
    float cancelBtnX, cancelBtnY, cancelBtnW, cancelBtnH;
    float eyeX, eyeY, eyeW;
    float signupX, signupY, signupW, signupH;
    float resetX, resetY, resetW, resetH;
    float privacyX, privacyY, privacyW, privacyH;
};

static CardLayout GetLayout(float cx, float cy, float cw, float ch) {
    CardLayout L = {};
    L.cardW = 380.0f;
    L.cardH = 440.0f;
    L.cardX = cx + (cw - L.cardW) / 2.0f;
    L.cardY = cy + (ch - L.cardH) / 2.0f;

    L.logoSz = 56.0f;
    L.logoX  = L.cardX + (L.cardW - L.logoSz) / 2.0f;
    L.logoY  = L.cardY + 25.0f;

    L.fieldW = L.cardW - 70.0f;
    L.fieldX = L.cardX + 35.0f;
    L.fldH   = 36.0f;

    L.emailY = L.logoY + L.logoSz + 45.0f;
    L.passY  = L.emailY + L.fldH + 15.0f;
    
    L.eyeW   = 32.0f;
    L.eyeX   = L.fieldX + L.fieldW - L.eyeW;
    L.eyeY   = L.passY;

    L.checkY = L.passY + L.fldH + 12.0f;
    
    float btnGap = 10.0f;
    L.loginBtnW  = (L.fieldW - btnGap) / 2.0f;
    L.loginBtnH  = 36.0f;
    L.loginBtnX  = L.fieldX;
    L.loginBtnY  = L.checkY + 30.0f;

    L.cancelBtnW = L.loginBtnW;
    L.cancelBtnH = L.loginBtnH;
    L.cancelBtnX = L.loginBtnX + L.loginBtnW + btnGap;
    L.cancelBtnY = L.loginBtnY;

    L.linksY  = L.loginBtnY + L.loginBtnH + 20.0f;
    
    // Explicit hitboxes for links
    L.signupX = L.cardX + 40.0f;
    L.signupY = L.linksY;
    L.signupW = 120.0f;
    L.signupH = 20.0f;

    L.resetX  = L.cardX + L.cardW - 120.0f;
    L.resetY  = L.linksY;
    L.resetW  = 90.0f;
    L.resetH  = 20.0f;

    L.privacyW = 80.0f;
    L.privacyH = 18.0f;
    L.privacyX = L.cardX + (L.cardW - L.privacyW) / 2.0f;
    L.privacyY = L.cardY + L.cardH - 30.0f;

    return L;
}

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

static bool HitRect(float mx, float my, float x, float y, float w, float h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

// ============================================================
//  DRAW
// ============================================================
void DrawAccountsTab(Graphics& g, float cx, float cy, float cw, float ch) {
    SolidBrush bgBrush(Color(255, 245, 248, 250));
    g.FillRectangle(&bgBrush, cx, cy, cw, ch);

    if (!g_loggedInEmail.empty()) {
        CardLayout L = GetLayout(cx, cy, cw, ch);
        GraphicsPath card;
        float rc = 10.0f, dc = rc * 2.0f;
        card.AddArc(L.cardX, L.cardY, dc, dc, 180.0f, 90.0f);
        card.AddArc(L.cardX + L.cardW - dc, L.cardY, dc, dc, 270.0f, 90.0f);
        card.AddArc(L.cardX + L.cardW - dc, L.cardY + L.cardH - dc, dc, dc, 0.0f, 90.0f);
        card.AddArc(L.cardX, L.cardY + L.cardH - dc, dc, dc, 90.0f, 90.0f);
        card.CloseFigure();
        SolidBrush cardBg(Color(255, 255, 255, 255));
        g.FillPath(&cardBg, &card);
        Pen cardShadow(Color(20, 0, 150, 160), 1.0f);
        g.DrawPath(&cardShadow, &card);

        GraphicsPath hdrPath;
        hdrPath.AddArc(L.cardX, L.cardY, dc, dc, 180.0f, 90.0f);
        hdrPath.AddArc(L.cardX + L.cardW - dc, L.cardY, dc, dc, 270.0f, 90.0f);
        hdrPath.AddLine(L.cardX + L.cardW, L.cardY + 80.0f, L.cardX, L.cardY + 80.0f);
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

        float avR = 40.0f;
        float avCX = L.cardX + L.cardW / 2.0f;
        float avCY = L.cardY + 80.0f;
        SolidBrush avBg(Color(255, 255, 255, 255));
        g.FillEllipse(&avBg, avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f);
        Pen avBorder(Color(255, 0, 150, 160), 3.0f);
        g.DrawEllipse(&avBorder, avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f);
        Font fAvIcon(&ffIcons, 32, FontStyleRegular, UnitPixel);
        g.DrawString(L"\xE77B", -1, &fAvIcon, RectF(avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f), &fmtC, &teal);

        Font fHdrTitle(&ff, 16, FontStyleBold, UnitPixel);
        g.DrawString(L"My Account", -1, &fHdrTitle, RectF(L.cardX, L.cardY, L.cardW, 50.0f), &fmtC, &white);

        float infoY = avCY + avR + 20.0f;
        Font fLabel(&ff, 11, FontStyleRegular, UnitPixel);
        Font fValue(&ff, 13, FontStyleBold, UnitPixel);

        g.DrawString(L"Email", -1, &fLabel, RectF(L.fieldX, infoY, L.fieldW, 18.0f), &fmtL, &gray);
        g.DrawString(g_loggedInEmail.c_str(), -1, &fValue, RectF(L.fieldX, infoY + 18.0f, L.fieldW, 22.0f), &fmtL, &dark);

        float badgeY = infoY + 58.0f;
        g.DrawString(L"Plan", -1, &fLabel, RectF(L.fieldX, badgeY, L.fieldW, 18.0f), &fmtL, &gray);

        float badgeW = 110.0f, badgeH = 26.0f;
        GraphicsPath badge;
        float br = 5.0f, bd = br * 2.0f;
        badge.AddArc(L.fieldX, badgeY + 20.0f, bd, bd, 180.0f, 90.0f);
        badge.AddArc(L.fieldX + badgeW - bd, badgeY + 20.0f, bd, bd, 270.0f, 90.0f);
        badge.AddArc(L.fieldX + badgeW - bd, badgeY + 20.0f + badgeH - bd, bd, bd, 0.0f, 90.0f);
        badge.AddArc(L.fieldX, badgeY + 20.0f + badgeH - bd, bd, bd, 90.0f, 90.0f);
        badge.CloseFigure();
        SolidBrush badgeBg(g_isPremiumUser ? Color(255, 243, 156, 18) : Color(255, 0, 150, 160));
        g.FillPath(&badgeBg, &badge);
        Font fBadge(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(g_isPremiumUser ? L"★ Premium" : L"Free Plan", -1, &fBadge, RectF(L.fieldX, badgeY + 20.0f, badgeW, badgeH), &fmtC, &white);

        float logoutW = 140.0f, logoutH = 36.0f;
        float logoutX = L.cardX + (L.cardW - logoutW) / 2.0f;
        float logoutY = L.cardY + L.cardH - 60.0f;
        GraphicsPath lPath;
        lPath.AddArc(logoutX, logoutY, bd, bd, 180.0f, 90.0f);
        lPath.AddArc(logoutX + logoutW - bd, logoutY, bd, bd, 270.0f, 90.0f);
        lPath.AddArc(logoutX + logoutW - bd, logoutY + logoutH - bd, bd, bd, 0.0f, 90.0f);
        lPath.AddArc(logoutX, logoutY + logoutH - bd, bd, bd, 90.0f, 90.0f);
        lPath.CloseFigure();
        SolidBrush lBg(s_hoverLogout ? Color(255, 200, 30, 30) : Color(255, 220, 50, 50));
        g.FillPath(&lBg, &lPath);
        Font fLogout(&ff, 12, FontStyleBold, UnitPixel);
        g.DrawString(L"\xE7E8 Log Out", -1, &fLogout, RectF(logoutX, logoutY, logoutW, logoutH), &fmtC, &white);
        return;
    }

    // ─── LOGIN VIEW ───
    CardLayout L = GetLayout(cx, cy, cw, ch);

    for (int i = 3; i >= 0; --i) {
        SolidBrush shadowBrush(Color(8 + i * 4, 0, 100, 120));
        GraphicsPath shadowPath;
        float sr = 10.0f, sd = sr * 2.0f;
        float sx = L.cardX - i, sy = L.cardY + i, sw2 = L.cardW + i * 2.0f, sh2 = L.cardH;
        shadowPath.AddArc(sx, sy, sd, sd, 180.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy, sd, sd, 270.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy + sh2 - sd, sd, sd, 0.0f, 90.0f);
        shadowPath.AddArc(sx, sy + sh2 - sd, sd, sd, 90.0f, 90.0f);
        shadowPath.CloseFigure();
        g.FillPath(&shadowBrush, &shadowPath);
    }

    GraphicsPath cardPath;
    float rc = 10.0f, dc = rc * 2.0f;
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
    SolidBrush dark(Color(255, 50, 50, 50));
    SolidBrush gray(Color(255, 140, 140, 140));
    SolidBrush white(Color(255, 255, 255, 255));
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    // Official App Logo
    HICON hIconLg = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, (int)L.logoSz, (int)L.logoSz, LR_SHARED);
    if (hIconLg) {
        Bitmap bmp(hIconLg);
        g.DrawImage(&bmp, L.logoX, L.logoY, L.logoSz, L.logoSz);
    }

    Font fWelcome(&ff, 15, FontStyleBold, UnitPixel);
    g.DrawString(L"Sign in to RasFocus+", -1, &fWelcome, RectF(L.cardX, L.logoY + L.logoSz + 10.0f, L.cardW, 26.0f), &fmtC, &dark);

    // Email
    bool emailFocused = (s_focusField == 1);
    SolidBrush fieldBg(Color(255, 248, 250, 252));
    g.FillRectangle(&fieldBg, L.fieldX, L.emailY, L.fieldW, L.fldH);
    Pen emailBorder(emailFocused ? Color(255, 0, 150, 160) : Color(255, 220, 225, 230), emailFocused ? 1.5f : 1.0f);
    g.DrawRectangle(&emailBorder, L.fieldX, L.emailY, L.fieldW, L.fldH);

    Font fInput(&ff, 12, FontStyleRegular, UnitPixel);
    wstring emailStr(s_email);
    SolidBrush inputColor(emailStr.empty() ? Color(255, 160, 170, 180) : dark.GetColor());
    g.DrawString(emailStr.empty() ? L"Email address" : emailStr.c_str(), -1, &fInput, RectF(L.fieldX + 32.0f, L.emailY, L.fieldW - 40.0f, L.fldH), &fmtL, &inputColor);
    
    Font fFieldIcon(&ffIcons, 13, FontStyleRegular, UnitPixel);
    g.DrawString(L"\xE715", -1, &fFieldIcon, RectF(L.fieldX + 8.0f, L.emailY, 24.0f, L.fldH), &fmtC, emailStr.empty() ? &gray : &teal);

    // Password
    bool passFocused = (s_focusField == 2);
    g.FillRectangle(&fieldBg, L.fieldX, L.passY, L.fieldW, L.fldH);
    Pen passBorder(passFocused ? Color(255, 0, 150, 160) : Color(255, 220, 225, 230), passFocused ? 1.5f : 1.0f);
    g.DrawRectangle(&passBorder, L.fieldX, L.passY, L.fieldW, L.fldH);

    wstring passStr(s_password);
    wstring passDisplay = passStr.empty() ? L"Password" : (s_showPassword ? passStr : wstring(passStr.size(), L'•'));
    SolidBrush passColor(passStr.empty() ? Color(255, 160, 170, 180) : dark.GetColor());
    g.DrawString(passDisplay.c_str(), -1, &fInput, RectF(L.fieldX + 32.0f, L.passY, L.fieldW - 65.0f, L.fldH), &fmtL, &passColor);
    g.DrawString(L"\xE72E", -1, &fFieldIcon, RectF(L.fieldX + 8.0f, L.passY, 24.0f, L.fldH), &fmtC, passStr.empty() ? &gray : &teal);

    Font fEye(&ffIcons, 13, FontStyleRegular, UnitPixel);
    SolidBrush eyeColor(s_hoverEye ? Color(255, 0, 150, 160) : Color(255, 160, 170, 180));
    g.DrawString(s_showPassword ? L"\xED1A" : L"\xE7B3", -1, &fEye, RectF(L.eyeX, L.eyeY, L.eyeW, L.fldH), &fmtC, &eyeColor);

    // Save Login
    float chkSize = 14.0f;
    float chkY = L.checkY;
    Pen chkBorder(Color(255, 0, 150, 160), 1.5f);
    if (s_saveLogin) {
        g.FillRectangle(&teal, L.fieldX, chkY, chkSize, chkSize);
        Font fCheck(&ffIcons, 10, FontStyleRegular, UnitPixel);
        g.DrawString(L"\xE73E", -1, &fCheck, RectF(L.fieldX, chkY, chkSize, chkSize), &fmtC, &white);
    } else {
        g.DrawRectangle(&chkBorder, L.fieldX, chkY, chkSize, chkSize);
    }
    Font fChkLabel(&ff, 11, FontStyleRegular, UnitPixel);
    g.DrawString(L"Remember me", -1, &fChkLabel, RectF(L.fieldX + 22.0f, chkY - 2.0f, 150.0f, 18.0f), &fmtL, &dark);

    // Status Message
    if (!s_statusMsg.empty()) {
        SolidBrush statusBrush(s_isError ? Color(255, 200, 50, 50) : Color(255, 0, 140, 80));
        Font fStatus(&ff, 10, FontStyleBold, UnitPixel);
        g.DrawString(s_statusMsg.c_str(), -1, &fStatus, RectF(L.cardX, L.loginBtnY - 20.0f, L.cardW, 16.0f), &fmtC, &statusBrush);
    } else if (s_isLoading) {
        Font fStatus(&ff, 10, FontStyleBold, UnitPixel);
        g.DrawString(L"Logging in...", -1, &fStatus, RectF(L.cardX, L.loginBtnY - 20.0f, L.cardW, 16.0f), &fmtC, &teal);
    }

    // Login Button
    GraphicsPath btnP1;
    float br = 5.0f, bd = br * 2.0f;
    btnP1.AddArc(L.loginBtnX, L.loginBtnY, bd, bd, 180.0f, 90.0f);
    btnP1.AddArc(L.loginBtnX + L.loginBtnW - bd, L.loginBtnY, bd, bd, 270.0f, 90.0f);
    btnP1.AddArc(L.loginBtnX + L.loginBtnW - bd, L.loginBtnY + L.loginBtnH - bd, bd, bd, 0.0f, 90.0f);
    btnP1.AddArc(L.loginBtnX, L.loginBtnY + L.loginBtnH - bd, bd, bd, 90.0f, 90.0f);
    btnP1.CloseFigure();
    SolidBrush loginBg(s_hoverLogin ? Color(255, 0, 120, 130) : Color(255, 0, 150, 160));
    g.FillPath(&loginBg, &btnP1);
    Font fBtn(&ff, 12, FontStyleBold, UnitPixel);
    g.DrawString(L"Log In", -1, &fBtn, RectF(L.loginBtnX, L.loginBtnY, L.loginBtnW, L.loginBtnH), &fmtC, &white);

    // Cancel Button
    GraphicsPath btnP2;
    btnP2.AddArc(L.cancelBtnX, L.cancelBtnY, bd, bd, 180.0f, 90.0f);
    btnP2.AddArc(L.cancelBtnX + L.cancelBtnW - bd, L.cancelBtnY, bd, bd, 270.0f, 90.0f);
    btnP2.AddArc(L.cancelBtnX + L.cancelBtnW - bd, L.cancelBtnY + L.cancelBtnH - bd, bd, bd, 0.0f, 90.0f);
    btnP2.AddArc(L.cancelBtnX, L.cancelBtnY + L.cancelBtnH - bd, bd, bd, 90.0f, 90.0f);
    btnP2.CloseFigure();
    SolidBrush cancelBg(s_hoverCancel ? Color(255, 240, 245, 248) : Color(255, 255, 255, 255));
    g.FillPath(&cancelBg, &btnP2);
    Pen cancelPen(Color(255, 0, 150, 160), 1.0f);
    g.DrawPath(&cancelPen, &btnP2);
    g.DrawString(L"Cancel", -1, &fBtn, RectF(L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH), &fmtC, &teal);

    // Links
    Font fLink(&ff, 10, FontStyleRegular, UnitPixel);
    Font fLinkU(&ff, 10, FontStyleUnderline, UnitPixel);
    SolidBrush linkTeal(Color(255, 0, 150, 160));
    SolidBrush linkHover(Color(255, 0, 100, 110));

    g.DrawString(L"Create an account", -1, &fLinkU, RectF(L.signupX, L.signupY, L.signupW, L.signupH), &fmtL, s_hoverSignup ? &linkHover : &linkTeal);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar); fmtR.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"Reset Password", -1, &fLinkU, RectF(L.resetX, L.resetY, L.resetW, L.resetH), &fmtR, s_hoverReset ? &linkHover : &linkTeal);

    g.DrawString(L"Privacy Policy", -1, &fLinkU, RectF(L.privacyX, L.privacyY, L.privacyW, L.privacyH), &fmtC, s_hoverPrivacy ? &linkHover : &linkTeal);
}

// ============================================================
//  MOUSE MOVE
// ============================================================
void ProcessAccountsMouseMove(float x, float y) {
    if (!g_loggedInEmail.empty()) {
        CardLayout L = GetCurrentLayout();
        float logoutW = 140.0f, logoutH = 36.0f;
        float logoutX = L.cardX + (L.cardW - logoutW) / 2.0f;
        float logoutY = L.cardY + L.cardH - 60.0f;
        s_hoverLogout = HitRect(x, y, logoutX, logoutY, logoutW, logoutH);
        return;
    }

    CardLayout L = GetCurrentLayout();
    s_hoverLogin     = HitRect(x, y, L.loginBtnX, L.loginBtnY, L.loginBtnW, L.loginBtnH);
    s_hoverCancel    = HitRect(x, y, L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH);
    s_hoverEye       = HitRect(x, y, L.eyeX, L.eyeY, L.eyeW, L.fldH);
    s_hoverSaveCheck = HitRect(x, y, L.fieldX, L.checkY - 5.0f, 120.0f, 24.0f);
    s_hoverSignup    = HitRect(x, y, L.signupX, L.signupY, L.signupW, L.signupH);
    s_hoverReset     = HitRect(x, y, L.resetX, L.resetY, L.resetW, L.resetH);
    s_hoverPrivacy   = HitRect(x, y, L.privacyX, L.privacyY, L.privacyW, L.privacyH);
}

// ============================================================
//  MOUSE CLICK
// ============================================================
void ProcessAccountsMouseClick(float x, float y, HWND hWnd) {
    if (!g_loggedInEmail.empty()) {
        CardLayout L = GetCurrentLayout();
        float logoutW = 140.0f, logoutH = 36.0f;
        float logoutX = L.cardX + (L.cardW - logoutW) / 2.0f;
        float logoutY = L.cardY + L.cardH - 60.0f;
        if (HitRect(x, y, logoutX, logoutY, logoutW, logoutH)) {
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

    if (HitRect(x, y, L.fieldX, L.emailY, L.fieldW - L.eyeW, L.fldH)) { s_focusField = 1; InvalidateRect(hWnd, NULL, FALSE); return; }
    if (HitRect(x, y, L.fieldX, L.passY,  L.fieldW - L.eyeW, L.fldH)) { s_focusField = 2; InvalidateRect(hWnd, NULL, FALSE); return; }
    if (HitRect(x, y, L.eyeX, L.eyeY, L.eyeW, L.fldH)) { s_showPassword = !s_showPassword; InvalidateRect(hWnd, NULL, FALSE); return; }
    if (HitRect(x, y, L.fieldX, L.checkY - 5.0f, 120.0f, 24.0f)) { s_saveLogin = !s_saveLogin; InvalidateRect(hWnd, NULL, FALSE); return; }

    if (HitRect(x, y, L.signupX, L.signupY, L.signupW, L.signupH)) { ShellExecuteW(NULL, L"open", L"https://raseledutools.github.io/product.html", NULL, NULL, SW_SHOWNORMAL); return; }
    if (HitRect(x, y, L.resetX, L.resetY, L.resetW, L.resetH)) { ShellExecuteW(NULL, L"open", L"https://rasfocus.com/reset-password", NULL, NULL, SW_SHOWNORMAL); return; }
    if (HitRect(x, y, L.privacyX, L.privacyY, L.privacyW, L.privacyH)) { ShellExecuteW(NULL, L"open", L"https://rasfocus.com/privacy", NULL, NULL, SW_SHOWNORMAL); return; }

    if (HitRect(x, y, L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH)) {
        s_statusMsg  = L"";
        s_focusField = 0;
        InvalidateRect(hWnd, NULL, FALSE); return;
    }

    if (HitRect(x, y, L.loginBtnX, L.loginBtnY, L.loginBtnW, L.loginBtnH)) {
        wstring emailW(s_email), passW(s_password);
        if (emailW.empty() || passW.empty()) {
            s_statusMsg = L"Please enter email and password."; s_isError = true; InvalidateRect(hWnd, NULL, FALSE); return;
        }
        s_isLoading = true; s_statusMsg = L""; s_isError = false; InvalidateRect(hWnd, NULL, FALSE);

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
//  KEYBOARD
// ============================================================
void ProcessAccountsChar(wchar_t c) {
    if (!g_loggedInEmail.empty()) return;
    if (c == L'\b') {
        if (s_focusField == 1) { int len = (int)wcslen(s_email);    if (len > 0) s_email[len-1] = L'\0'; }
        if (s_focusField == 2) { int len = (int)wcslen(s_password); if (len > 0) s_password[len-1] = L'\0'; }
    } else if (c == L'\r' || c == L'\n') {
        if (s_focusField == 1) s_focusField = 2; else s_focusField = 1;
    } else if (c >= 32) {
        if (s_focusField == 1) { int len = (int)wcslen(s_email);    if (len < 510) { s_email[len] = c; s_email[len+1] = L'\0'; } }
        if (s_focusField == 2) { int len = (int)wcslen(s_password); if (len < 510) { s_password[len] = c; s_password[len+1] = L'\0'; } }
    }
}

void ProcessAccountsKeyDown(WPARAM wp) {
    if (!g_loggedInEmail.empty()) return;
    if (wp == VK_TAB) s_focusField = (s_focusField == 1) ? 2 : 1;
    else if (wp == VK_DELETE) {
        if (s_focusField == 1) ZeroMemory(s_email, sizeof(s_email));
        if (s_focusField == 2) ZeroMemory(s_password, sizeof(s_password));
    } else if (wp == VK_ESCAPE) { s_focusField = 0; s_statusMsg = L""; }
}
