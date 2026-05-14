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
bool    g_isPremiumUser    = false;
wstring g_loggedInEmail    = L"";
string  g_loggedInUserUid  = "";

static firebase::App* s_firebaseApp = nullptr;

// ── UI State ──
static wchar_t s_email   [512]  = {};
static wchar_t s_password[512]  = {};
static bool    s_saveLogin      = false;
static bool    s_showPassword   = false;
static int     s_focusField     = 0;       
static bool    s_isLoading      = false;
static wstring s_statusMsg      = L"";
static bool    s_isError        = false;
static DWORD   s_lastBlinkTime  = 0;

// ── Hover states ──
static bool s_hoverLogin      = false;
static bool s_hoverCancel     = false;
static bool s_hoverSignup     = false;
static bool s_hoverReset      = false;
static bool s_hoverPrivacy    = false;
static bool s_hoverSaveCheck  = false;
static bool s_hoverEye        = false;
static bool s_hoverLogout     = false;

// ── Sign Up State ──
static bool    s_showSignup         = false;
static wchar_t s_su_name    [512]   = {};
static wchar_t s_su_email   [512]   = {};
static wchar_t s_su_pass    [512]   = {};
static wchar_t s_su_confirm [512]   = {};
static int     s_su_focus           = 0;   
static bool    s_su_showPass        = false;
static bool    s_su_showConfirm     = false;
static bool    s_su_isLoading       = false;
static wstring s_su_statusMsg       = L"";
static bool    s_su_isError         = false;
static bool    s_su_success         = false;
static bool    s_hoverSuBtn         = false;
static bool    s_hoverSuBack        = false;
static bool    s_hoverSuEye1        = false;
static bool    s_hoverSuEye2        = false;

// ── Saved credentials path ──
static string GetCredsPath() {
    char appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData)))
        return string(appData) + "\\.rasfocus\\creds.dat";
    return "rasfocus_creds.dat";
}

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

// 🔥 (FIXED) এটি স্পেস থাকলেও JSON ডেটা পড়তে পারবে
static string JsonExtract(const string& json, const string& key) {
    string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return "";
    
    size_t colon = json.find(":", pos);
    if (colon == string::npos) return "";
    
    size_t startQuote = json.find("\"", colon);
    if (startQuote == string::npos) return "";
    
    size_t endQuote = json.find("\"", startQuote + 1);
    if (endQuote == string::npos) return "";
    
    return json.substr(startQuote + 1, endQuote - startQuote - 1);
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
    
    if (response.find("\"error\"") != string::npos || response.find("\"errors\"") != string::npos) {
        string msg = JsonExtract(response, "message");
        if (msg == "EMAIL_NOT_FOUND" || msg == "INVALID_EMAIL") res.errorMsg = "Email not found. Please sign up first.";
        else if (msg == "INVALID_PASSWORD" || msg == "INVALID_LOGIN_CREDENTIALS" || msg.find("INVALID_LOGIN_CREDENTIALS") != string::npos) res.errorMsg = "Wrong password or email. Please try again.";
        else if (msg == "USER_DISABLED") res.errorMsg = "This account has been disabled.";
        else if (msg == "TOO_MANY_ATTEMPTS_TRY_LATER" || msg.find("TOO_MANY_ATTEMPTS") != string::npos) res.errorMsg = "Too many attempts. Try later.";
        else res.errorMsg = "Login failed: " + msg;
    } else {
        res.idToken = JsonExtract(response, "idToken");
        res.localId = JsonExtract(response, "localId");
        res.email   = JsonExtract(response, "email");
        res.success = !res.localId.empty();
        if (!res.success) res.errorMsg = "Login failed. Please try again.";
    }

    // 🔥 DEBUGGER: যদি ফেইল হয়, তাহলে স্ক্রিনে আসল মেসেজটি পপ-আপ করে দেখাবে
    if (!res.success) {
        string dbg = "RAW SERVER RESPONSE:\n\n" + response;
        MessageBoxA(NULL, dbg.c_str(), "Firebase Login Debugger", MB_OK | MB_ICONWARNING);
    }

    return res;
}

// ── FIREBASE SIGNUP ──
static LoginResult FirebaseSignUp(const string& email, const string& password) {
    LoginResult res = { false, "", "", "", "" };
    const string API_KEY = "AIzaSyBVl3BuW6gfmp_K2IMYd1rbvLEA2l0yinA";
    const string HOST    = "identitytoolkit.googleapis.com";
    const string PATH    = "/v1/accounts:signUp?key=" + API_KEY;
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
    
    if (response.find("\"error\"") != string::npos || response.find("\"errors\"") != string::npos) {
        string msg = JsonExtract(response, "message");
        if (msg == "EMAIL_EXISTS")                    res.errorMsg = "This email is already registered.";
        else if (msg == "INVALID_EMAIL")              res.errorMsg = "Invalid email address.";
        else if (msg == "WEAK_PASSWORD : Password should be at least 6 characters" ||
                 msg.find("WEAK_PASSWORD") != string::npos) res.errorMsg = "Password must be at least 6 characters.";
        else if (msg == "TOO_MANY_ATTEMPTS_TRY_LATER") res.errorMsg = "Too many attempts. Try later.";
        else                                           res.errorMsg = "Sign up failed: " + msg;
    } else {
        res.idToken = JsonExtract(response, "idToken");
        res.localId = JsonExtract(response, "localId");
        res.email   = JsonExtract(response, "email");
        res.success = !res.localId.empty();
        if (!res.success) res.errorMsg = "Sign up failed. Please try again.";
    }

    // 🔥 DEBUGGER: যদি ফেইল হয়, তাহলে স্ক্রিনে আসল মেসেজটি পপ-আপ করে দেখাবে
    if (!res.success) {
        string dbg = "RAW SERVER RESPONSE:\n\n" + response;
        MessageBoxA(NULL, dbg.c_str(), "Firebase SignUp Debugger", MB_OK | MB_ICONWARNING);
    }

    return res;
}

struct SignUpThreadData { HWND hWnd; string email; string password; string name; };

static void CreateFirestoreUserDoc(const string& uid, const string& idToken,
                                   const string& email, const string& name) {
    string host = "firestore.googleapis.com";
    string path = "/v1/projects/rasfocus-c746d/databases/(default)/documents/users/" + uid;

    string body =
        "{"
        "\"fields\":{"
          "\"email\":{\"stringValue\":\"" + email + "\"},"
          "\"name\":{\"stringValue\":\"" + name + "\"},"
          "\"current_package\":{\"stringValue\":\"FREE_BASIC\"},"
          "\"created_at\":{\"stringValue\":\"" + to_string(time(nullptr)) + "\"}"
        "}"
        "}";

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return;
    HINTERNET hConn = InternetConnectA(hInet, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return; }
    HINTERNET hReq = HttpOpenRequestA(hConn, "PATCH", path.c_str(), NULL, NULL, NULL,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return; }

    string headers = "Content-Type: application/json\r\nAuthorization: Bearer " + idToken + "\r\n";
    HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(), (LPVOID)body.c_str(), (DWORD)body.size());

    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);
}

void __cdecl SignUpThread(void* param) {
    SignUpThreadData* data = (SignUpThreadData*)param;
    LoginResult res = FirebaseSignUp(data->email, data->password);
    s_su_isLoading = false;
    if (res.success) {
        if (!res.idToken.empty() && !res.localId.empty())
            CreateFirestoreUserDoc(res.localId, res.idToken, res.email, data->name);

        s_su_success   = true;
        s_su_statusMsg = L"";
        s_su_isError   = false;
    } else {
        wchar_t errW[512] = {};
        MultiByteToWideChar(CP_UTF8, 0, res.errorMsg.c_str(), -1, errW, 511);
        s_su_statusMsg = errW;
        s_su_isError   = true;
    }
    if (data->hWnd) InvalidateRect(data->hWnd, NULL, FALSE);
    delete data;
    _endthread();
}

static bool CheckPremiumFromFirebase(const string& uid, const string& idToken) {
    string host = "firestore.googleapis.com";
    string path = "/v1/projects/rasfocus-c746d/databases/(default)/documents/users/" + uid;

    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return false;

    HINTERNET hConn = InternetConnectA(hInet, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return false; }

    HINTERNET hReq = HttpOpenRequestA(hConn, "GET", path.c_str(), NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return false; }

    string headers = "Authorization: Bearer " + idToken + "\r\n";
    HttpSendRequestA(hReq, headers.c_str(), headers.length(), NULL, 0);

    string response = ""; char buf[1024]; DWORD br = 0;
    while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &br) && br > 0) { buf[br] = '\0'; response += buf; }
    InternetCloseHandle(hReq); InternetCloseHandle(hConn); InternetCloseHandle(hInet);

    return (response.find("\"current_package\"") != string::npos && 
           (response.find("\"PREMIUM\"") != string::npos || response.find("\"STUDENT\"") != string::npos));
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
        g_loggedInEmail   = emailW;
        g_loggedInUserUid = res.localId;

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

void __cdecl CursorBlinkThread(void* p) {
    while (true) {
        if (s_focusField != 0) {
            if (HWND hw = FindWindowA("RasFocusCore", NULL)) {
                InvalidateRect(hw, NULL, FALSE);
            }
        }
        Sleep(500);
    }
    _endthread();
}

void InitAccountsModule(firebase::App* app) {
    s_firebaseApp = app;
    if (LoadSavedCredentials()) s_statusMsg = L"";
    _beginthread(CursorBlinkThread, 0, NULL);
}

// ============================================================
//  LAYOUT
// ============================================================
extern int   windowWidth;
extern int   windowHeight;
extern const int SIDEBAR_WIDTH;
extern const int TITLEBAR_HEIGHT;
extern const int SUBHEADER_HEIGHT;
extern float g_scaleFactor;

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

static CardLayout GetCurrentLayout() {
    float scaledW = (float)windowWidth  / g_scaleFactor;
    float scaledH = (float)windowHeight / g_scaleFactor;
    float cx = (float)SIDEBAR_WIDTH;
    float cy = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
    float cw = scaledW - cx;
    float ch = scaledH - cy;
    return GetLayout(cx, cy, cw, ch);
}

static bool HitRect(float mx, float my, float x, float y, float w, float h) {
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

// ============================================================
//  DRAW SIGN UP FORM
// ============================================================
static void DrawSignUpForm(Graphics& g, float cx, float cy, float cw, float ch) {
    SolidBrush bgBrush(Color(255, 240, 248, 255));
    g.FillRectangle(&bgBrush, cx, cy, cw, ch);

    float cardW = 400.0f, cardH = 530.0f;
    float cardX = cx + (cw - cardW) / 2.0f;
    float cardY = cy + (ch - cardH) / 2.0f;

    for (int i = 3; i >= 0; --i) {
        SolidBrush shadowBrush(Color(10 + i * 5, 0, 80, 140));
        GraphicsPath shadowPath;
        float sr = 12.0f, sd = sr * 2.0f;
        float sx = cardX - i, sy = cardY + i, sw2 = cardW + i * 2.0f, sh2 = cardH;
        shadowPath.AddArc(sx, sy, sd, sd, 180.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy, sd, sd, 270.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy + sh2 - sd, sd, sd, 0.0f, 90.0f);
        shadowPath.AddArc(sx, sy + sh2 - sd, sd, sd, 90.0f, 90.0f);
        shadowPath.CloseFigure();
        g.FillPath(&shadowBrush, &shadowPath);
    }

    GraphicsPath cardPath;
    float rc = 12.0f, dc = rc * 2.0f;
    cardPath.AddArc(cardX, cardY, dc, dc, 180.0f, 90.0f);
    cardPath.AddArc(cardX + cardW - dc, cardY, dc, dc, 270.0f, 90.0f);
    cardPath.AddArc(cardX + cardW - dc, cardY + cardH - dc, dc, dc, 0.0f, 90.0f);
    cardPath.AddArc(cardX, cardY + cardH - dc, dc, dc, 90.0f, 90.0f);
    cardPath.CloseFigure();
    SolidBrush cardBg(Color(255, 255, 255, 255));
    g.FillPath(&cardBg, &cardPath);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    SolidBrush teal(Color(255, 0, 150, 180));
    SolidBrush dark(Color(255, 40, 40, 50));
    SolidBrush gray(Color(255, 130, 140, 150));
    SolidBrush white(Color(255, 255, 255, 255));
    SolidBrush fieldBg(Color(255, 248, 250, 252));
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    Font fTitle(&ff, 15, FontStyleBold, UnitPixel);
    g.DrawString(L"Create Account", -1, &fTitle, RectF(cardX, cardY + 22.0f, cardW, 26.0f), &fmtC, &dark);

    float fieldX = cardX + 35.0f;
    float fieldW = cardW - 70.0f;
    float fldH   = 36.0f;
    float eyeW   = 32.0f;

    float nameY    = cardY + 65.0f;
    float emailY   = nameY  + fldH + 14.0f;
    float passY    = emailY + fldH + 14.0f;
    float confirmY = passY  + fldH + 14.0f;

    Font fInput(&ff, 12, FontStyleRegular, UnitPixel);
    Font fFieldIcon(&ffIcons, 13, FontStyleRegular, UnitPixel);
    bool showCursor = ((GetTickCount() - s_lastBlinkTime) % 1000) < 500;

    if (s_su_success) {
        Font fBig(&ff, 16, FontStyleBold, UnitPixel);
        Font fSub(&ff, 12, FontStyleRegular, UnitPixel);
        SolidBrush green(Color(255, 0, 160, 90));
        g.DrawString(L"\u2713 Account Created Successfully!", -1, &fBig,
            RectF(cardX, cardY + cardH / 2.0f - 50.0f, cardW, 30.0f), &fmtC, &green);
        g.DrawString(L"You can now log in with your email.", -1, &fSub,
            RectF(cardX, cardY + cardH / 2.0f - 10.0f, cardW, 24.0f), &fmtC, &gray);

        float btnW = 160.0f, btnH = 38.0f;
        float btnX = cardX + (cardW - btnW) / 2.0f;
        float btnY = cardY + cardH / 2.0f + 30.0f;
        GraphicsPath bp;
        float br2 = 6.0f, bd2 = br2 * 2.0f;
        bp.AddArc(btnX, btnY, bd2, bd2, 180.0f, 90.0f);
        bp.AddArc(btnX + btnW - bd2, btnY, bd2, bd2, 270.0f, 90.0f);
        bp.AddArc(btnX + btnW - bd2, btnY + btnH - bd2, bd2, bd2, 0.0f, 90.0f);
        bp.AddArc(btnX, btnY + btnH - bd2, bd2, bd2, 90.0f, 90.0f);
        bp.CloseFigure();
        LinearGradientBrush btnBg(PointF(btnX, btnY), PointF(btnX + btnW, btnY + btnH),
            Color(255, 0, 120, 140), Color(255, 0, 180, 200));
        g.FillPath(&btnBg, &bp);
        Font fBtn(&ff, 13, FontStyleBold, UnitPixel);
        g.DrawString(L"Log In Now", -1, &fBtn, RectF(btnX, btnY, btnW, btnH), &fmtC, &white);
        return;
    }

    auto DrawField = [&](float fy, int focusId, const wchar_t* placeholder, wchar_t* buf,
                         bool isPass, bool showPass, bool showEye, bool hoverEye) {
        bool focused = (s_su_focus == focusId);
        g.FillRectangle(&fieldBg, fieldX, fy, fieldW, fldH);
        Pen border(focused ? Color(255, 0, 150, 160) : Color(255, 220, 225, 230), focused ? 1.5f : 1.0f);
        g.DrawRectangle(&border, fieldX, fy, fieldW, fldH);

        wstring str(buf);
        float textX = fieldX + 32.0f;
        float availW = isPass ? fieldW - 65.0f : fieldW - 40.0f;
        SolidBrush inputColor(str.empty() ? Color(255, 160, 170, 180) : Color(255, 50, 50, 50));

        if (!str.empty()) {
            wstring display = (isPass && !showPass) ? wstring(str.size(), L'\u2022') : str;
            g.DrawString(display.c_str(), -1, &fInput, RectF(textX, fy, availW, fldH), &fmtL, &inputColor);
            if (focused && showCursor) {
                RectF bbox;
                g.MeasureString(display.c_str(), -1, &fInput, PointF(textX, fy), &fmtL, &bbox);
                float curX = textX + bbox.Width;
                if (curX > fieldX + fieldW - (isPass ? 40.0f : 12.0f)) curX = fieldX + fieldW - (isPass ? 40.0f : 12.0f);
                Pen cur(Color(255, 0, 150, 160), 2.0f);
                g.DrawLine(&cur, curX, fy + 6.0f, curX, fy + fldH - 6.0f);
            }
        } else {
            if (focused && showCursor) {
                Pen cur(Color(255, 0, 150, 160), 2.0f);
                g.DrawLine(&cur, textX, fy + 8.0f, textX, fy + fldH - 8.0f);
            }
            g.DrawString(placeholder, -1, &fInput, RectF(textX, fy, availW, fldH), &fmtL, &inputColor);
        }

        if (isPass && showEye) {
            float ex = fieldX + fieldW - eyeW;
            Font fEye(&ffIcons, 13, FontStyleRegular, UnitPixel);
            SolidBrush eyeC(hoverEye ? Color(255, 0, 150, 160) : Color(255, 160, 170, 180));
            g.DrawString(showPass ? L"\xED1A" : L"\xE7B3", -1, &fEye, RectF(ex, fy, eyeW, fldH), &fmtC, &eyeC);
        }
    };

    g.DrawString(L"\xE77B", -1, &fFieldIcon, RectF(fieldX + 8.0f, nameY, 24.0f, fldH), &fmtC, wcslen(s_su_name) ? &teal : &gray);
    DrawField(nameY, 1, L"Full Name", s_su_name, false, false, false, false);

    g.DrawString(L"\xE715", -1, &fFieldIcon, RectF(fieldX + 8.0f, emailY, 24.0f, fldH), &fmtC, wcslen(s_su_email) ? &teal : &gray);
    DrawField(emailY, 2, L"Email address", s_su_email, false, false, false, false);

    g.DrawString(L"\xE72E", -1, &fFieldIcon, RectF(fieldX + 8.0f, passY, 24.0f, fldH), &fmtC, wcslen(s_su_pass) ? &teal : &gray);
    DrawField(passY, 3, L"Create Password", s_su_pass, true, s_su_showPass, true, s_hoverSuEye1);

    g.DrawString(L"\xE72E", -1, &fFieldIcon, RectF(fieldX + 8.0f, confirmY, 24.0f, fldH), &fmtC, wcslen(s_su_confirm) ? &teal : &gray);
    DrawField(confirmY, 4, L"Confirm Password", s_su_confirm, true, s_su_showConfirm, true, s_hoverSuEye2);

    float statusY = confirmY + fldH + 8.0f;
    if (!s_su_statusMsg.empty()) {
        SolidBrush statusBrush(s_su_isError ? Color(255, 220, 60, 60) : Color(255, 0, 160, 90));
        Font fStatus(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(s_su_statusMsg.c_str(), -1, &fStatus, RectF(cardX, statusY, cardW, 18.0f), &fmtC, &statusBrush);
    } else if (s_su_isLoading) {
        SolidBrush statusBrush(Color(255, 0, 150, 180));
        Font fStatus(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(L"Creating account...", -1, &fStatus, RectF(cardX, statusY, cardW, 18.0f), &fmtC, &statusBrush);
    }

    float btnY    = statusY + 26.0f;
    float btnGap  = 10.0f;
    float btnW    = (fieldW - btnGap) / 2.0f;
    float btnH    = 38.0f;
    float suBtnX  = fieldX;
    float backBtnX = fieldX + btnW + btnGap;

    GraphicsPath bp1;
    float br2 = 6.0f, bd2 = br2 * 2.0f;
    bp1.AddArc(suBtnX, btnY, bd2, bd2, 180.0f, 90.0f);
    bp1.AddArc(suBtnX + btnW - bd2, btnY, bd2, bd2, 270.0f, 90.0f);
    bp1.AddArc(suBtnX + btnW - bd2, btnY + btnH - bd2, bd2, bd2, 0.0f, 90.0f);
    bp1.AddArc(suBtnX, btnY + btnH - bd2, bd2, bd2, 90.0f, 90.0f);
    bp1.CloseFigure();
    LinearGradientBrush suBg(PointF(suBtnX, btnY), PointF(suBtnX + btnW, btnY + btnH),
        Color(255, 0, 120, 140), Color(255, 0, 180, 200));
    SolidBrush suHover(Color(255, 0, 90, 110));
    if (s_hoverSuBtn) g.FillPath(&suHover, &bp1); else g.FillPath(&suBg, &bp1);
    Font fBtn(&ff, 12, FontStyleBold, UnitPixel);
    g.DrawString(L"Create Account", -1, &fBtn, RectF(suBtnX, btnY, btnW, btnH), &fmtC, &white);

    GraphicsPath bp2;
    bp2.AddArc(backBtnX, btnY, bd2, bd2, 180.0f, 90.0f);
    bp2.AddArc(backBtnX + btnW - bd2, btnY, bd2, bd2, 270.0f, 90.0f);
    bp2.AddArc(backBtnX + btnW - bd2, btnY + btnH - bd2, bd2, bd2, 0.0f, 90.0f);
    bp2.AddArc(backBtnX, btnY + btnH - bd2, bd2, bd2, 90.0f, 90.0f);
    bp2.CloseFigure();
    SolidBrush backBg(s_hoverSuBack ? Color(255, 245, 250, 255) : Color(255, 255, 255, 255));
    g.FillPath(&backBg, &bp2);
    Pen backPen(Color(255, 0, 150, 180), 1.5f);
    g.DrawPath(&backPen, &bp2);
    g.DrawString(L"Back to Login", -1, &fBtn, RectF(backBtnX, btnY, btnW, btnH), &fmtC, &teal);
}

// ============================================================
//  DRAW
// ============================================================
void DrawAccountsTab(Graphics& g, float cx, float cy, float cw, float ch) {
    if (s_showSignup) { DrawSignUpForm(g, cx, cy, cw, ch); return; }

    SolidBrush bgBrush(Color(255, 240, 248, 255));
    g.FillRectangle(&bgBrush, cx, cy, cw, ch);

    if (!g_loggedInEmail.empty()) {
        CardLayout L = GetLayout(cx, cy, cw, ch);
        GraphicsPath card;
        float rc = 12.0f, dc = rc * 2.0f;
        card.AddArc(L.cardX, L.cardY, dc, dc, 180.0f, 90.0f);
        card.AddArc(L.cardX + L.cardW - dc, L.cardY, dc, dc, 270.0f, 90.0f);
        card.AddArc(L.cardX + L.cardW - dc, L.cardY + L.cardH - dc, dc, dc, 0.0f, 90.0f);
        card.AddArc(L.cardX, L.cardY + L.cardH - dc, dc, dc, 90.0f, 90.0f);
        card.CloseFigure();
        SolidBrush cardBg(Color(255, 255, 255, 255));
        g.FillPath(&cardBg, &card);
        Pen cardShadow(Color(30, 0, 100, 180), 1.5f);
        g.DrawPath(&cardShadow, &card);

        GraphicsPath hdrPath;
        hdrPath.AddArc(L.cardX, L.cardY, dc, dc, 180.0f, 90.0f);
        hdrPath.AddArc(L.cardX + L.cardW - dc, L.cardY, dc, dc, 270.0f, 90.0f);
        hdrPath.AddLine(L.cardX + L.cardW, L.cardY + 90.0f, L.cardX, L.cardY + 90.0f);
        hdrPath.CloseFigure();
        LinearGradientBrush hdrBg(PointF(L.cardX, L.cardY), PointF(L.cardX + L.cardW, L.cardY + 90.0f),
            Color(255, 0, 120, 160), Color(255, 0, 180, 200));
        g.FillPath(&hdrBg, &hdrPath);

        FontFamily ff(L"Segoe UI");
        FontFamily ffIcons(L"Segoe MDL2 Assets");
        SolidBrush white(Color(255, 255, 255, 255));
        SolidBrush teal(Color(255, 0, 150, 180));
        SolidBrush dark(Color(255, 40, 40, 50));
        SolidBrush gray(Color(255, 120, 130, 140));
        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
        StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

        float avR = 45.0f;
        float avCX = L.cardX + L.cardW / 2.0f;
        float avCY = L.cardY + 90.0f;
        SolidBrush avBg(Color(255, 255, 255, 255));
        g.FillEllipse(&avBg, avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f);
        Pen avBorder(Color(255, 0, 150, 180), 3.5f);
        g.DrawEllipse(&avBorder, avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f);
        Font fAvIcon(&ffIcons, 36, FontStyleRegular, UnitPixel);
        g.DrawString(L"\xE77B", -1, &fAvIcon, RectF(avCX - avR, avCY - avR, avR * 2.0f, avR * 2.0f), &fmtC, &teal);

        Font fHdrTitle(&ff, 18, FontStyleBold, UnitPixel);
        g.DrawString(L"My Account", -1, &fHdrTitle, RectF(L.cardX, L.cardY, L.cardW, 55.0f), &fmtC, &white);

        float infoY = avCY + avR + 25.0f;
        Font fLabel(&ff, 12, FontStyleRegular, UnitPixel);
        Font fValue(&ff, 14, FontStyleBold, UnitPixel);

        g.DrawString(L"Email", -1, &fLabel, RectF(L.fieldX, infoY, L.fieldW, 20.0f), &fmtL, &gray);
        g.DrawString(g_loggedInEmail.c_str(), -1, &fValue, RectF(L.fieldX, infoY + 20.0f, L.fieldW, 24.0f), &fmtL, &dark);

        float badgeY = infoY + 65.0f;
        g.DrawString(L"Plan", -1, &fLabel, RectF(L.fieldX, badgeY, L.fieldW, 20.0f), &fmtL, &gray);

        float badgeW = 120.0f, badgeH = 30.0f;
        GraphicsPath badge;
        float br = 6.0f, bd = br * 2.0f;
        badge.AddArc(L.fieldX, badgeY + 22.0f, bd, bd, 180.0f, 90.0f);
        badge.AddArc(L.fieldX + badgeW - bd, badgeY + 22.0f, bd, bd, 270.0f, 90.0f);
        badge.AddArc(L.fieldX + badgeW - bd, badgeY + 22.0f + badgeH - bd, bd, bd, 0.0f, 90.0f);
        badge.AddArc(L.fieldX, badgeY + 22.0f + badgeH - bd, bd, bd, 90.0f, 90.0f);
        badge.CloseFigure();
        SolidBrush badgeBg(g_isPremiumUser ? Color(255, 255, 140, 0) : Color(255, 0, 150, 180));
        g.FillPath(&badgeBg, &badge);
        Font fBadge(&ff, 12, FontStyleBold, UnitPixel);
        g.DrawString(g_isPremiumUser ? L"★ Premium" : L"Free Plan", -1, &fBadge, RectF(L.fieldX, badgeY + 22.0f, badgeW, badgeH), &fmtC, &white);

        float logoutW = 150.0f, logoutH = 40.0f;
        float logoutX = L.cardX + (L.cardW - logoutW) / 2.0f;
        float logoutY = L.cardY + L.cardH - 65.0f;
        GraphicsPath lPath;
        lPath.AddArc(logoutX, logoutY, bd, bd, 180.0f, 90.0f);
        lPath.AddArc(logoutX + logoutW - bd, logoutY, bd, bd, 270.0f, 90.0f);
        lPath.AddArc(logoutX + logoutW - bd, logoutY + logoutH - bd, bd, bd, 0.0f, 90.0f);
        lPath.AddArc(logoutX, logoutY + logoutH - bd, bd, bd, 90.0f, 90.0f);
        lPath.CloseFigure();
        SolidBrush lBg(s_hoverLogout ? Color(255, 180, 20, 20) : Color(255, 220, 50, 50));
        g.FillPath(&lBg, &lPath);
        Font fLogout(&ff, 13, FontStyleBold, UnitPixel);
        g.DrawString(L"\xE7E8 Log Out", -1, &fLogout, RectF(logoutX, logoutY, logoutW, logoutH), &fmtC, &white);
        return;
    }

    // ─── LOGIN VIEW ───
    CardLayout L = GetLayout(cx, cy, cw, ch);

    for (int i = 3; i >= 0; --i) {
        SolidBrush shadowBrush(Color(10 + i * 5, 0, 80, 140));
        GraphicsPath shadowPath;
        float sr = 12.0f, sd = sr * 2.0f;
        float sx = L.cardX - i, sy = L.cardY + i, sw2 = L.cardW + i * 2.0f, sh2 = L.cardH;
        shadowPath.AddArc(sx, sy, sd, sd, 180.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy, sd, sd, 270.0f, 90.0f);
        shadowPath.AddArc(sx + sw2 - sd, sy + sh2 - sd, sd, sd, 0.0f, 90.0f);
        shadowPath.AddArc(sx, sy + sh2 - sd, sd, sd, 90.0f, 90.0f);
        shadowPath.CloseFigure();
        g.FillPath(&shadowBrush, &shadowPath);
    }

    GraphicsPath cardPath;
    float rc = 12.0f, dc = rc * 2.0f;
    cardPath.AddArc(L.cardX, L.cardY, dc, dc, 180.0f, 90.0f);
    cardPath.AddArc(L.cardX + L.cardW - dc, L.cardY, dc, dc, 270.0f, 90.0f);
    cardPath.AddArc(L.cardX + L.cardW - dc, L.cardY + L.cardH - dc, dc, dc, 0.0f, 90.0f);
    cardPath.AddArc(L.cardX, L.cardY + L.cardH - dc, dc, dc, 90.0f, 90.0f);
    cardPath.CloseFigure();
    SolidBrush cardBg(Color(255, 255, 255, 255));
    g.FillPath(&cardBg, &cardPath);

    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    SolidBrush teal(Color(255, 0, 150, 180));
    SolidBrush dark(Color(255, 40, 40, 50));
    SolidBrush gray(Color(255, 130, 140, 150));
    SolidBrush white(Color(255, 255, 255, 255));
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    HICON hIconLg = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON, (int)L.logoSz, (int)L.logoSz, LR_SHARED);
    if (hIconLg) {
        Bitmap bmp(hIconLg);
        g.DrawImage(&bmp, L.logoX, L.logoY, L.logoSz, L.logoSz);
    }

    Font fWelcome(&ff, 15, FontStyleBold, UnitPixel);
    g.DrawString(L"Sign in to RasFocus+", -1, &fWelcome, RectF(L.cardX, L.logoY + L.logoSz + 10.0f, L.cardW, 26.0f), &fmtC, &dark);

    // ── Email Field ──
    bool emailFocused = (s_focusField == 1);
    SolidBrush fieldBg(Color(255, 248, 250, 252));
    g.FillRectangle(&fieldBg, L.fieldX, L.emailY, L.fieldW, L.fldH);
    Pen emailBorder(emailFocused ? Color(255, 0, 150, 160) : Color(255, 220, 225, 230), emailFocused ? 1.5f : 1.0f);
    g.DrawRectangle(&emailBorder, L.fieldX, L.emailY, L.fieldW, L.fldH);

    Font fInput(&ff, 12, FontStyleRegular, UnitPixel);
    wstring emailStr(s_email);
    SolidBrush inputColor(emailStr.empty() ? Color(255, 160, 170, 180) : Color(255, 50, 50, 50));
    float textX = L.fieldX + 32.0f;

    bool showCursor = ((GetTickCount() - s_lastBlinkTime) % 1000) < 500;

    if (!emailStr.empty()) {
        g.DrawString(emailStr.c_str(), -1, &fInput, RectF(textX, L.emailY, L.fieldW - 40.0f, L.fldH), &fmtL, &inputColor);
        if (emailFocused && showCursor) {
            RectF bbox;
            g.MeasureString(emailStr.c_str(), -1, &fInput, PointF(textX, L.emailY), &fmtL, &bbox);
            float curX = textX + bbox.Width;
            if (curX > L.fieldX + L.fieldW - 12.0f) curX = L.fieldX + L.fieldW - 12.0f;
            Pen cursorPen(Color(255, 0, 150, 160), 2.0f);
            g.DrawLine(&cursorPen, curX, L.emailY + 6.0f, curX, L.emailY + L.fldH - 6.0f);
        }
    } else if (emailFocused) {
        if (showCursor) {
            Pen cursorPen(Color(255, 0, 150, 160), 2.0f);
            g.DrawLine(&cursorPen, textX, L.emailY + 8.0f, textX, L.emailY + L.fldH - 8.0f);
        }
        g.DrawString(L"Email address", -1, &fInput, RectF(textX, L.emailY, L.fieldW - 40.0f, L.fldH), &fmtL, &inputColor);
    } else {
        g.DrawString(L"Email address", -1, &fInput, RectF(textX, L.emailY, L.fieldW - 40.0f, L.fldH), &fmtL, &inputColor);
    }

    Font fFieldIcon(&ffIcons, 13, FontStyleRegular, UnitPixel);
    g.DrawString(L"\xE715", -1, &fFieldIcon, RectF(L.fieldX + 8.0f, L.emailY, 24.0f, L.fldH), &fmtC, emailStr.empty() ? &gray : &teal);

    // ── Password Field ──
    bool passFocused = (s_focusField == 2);
    g.FillRectangle(&fieldBg, L.fieldX, L.passY, L.fieldW, L.fldH);
    Pen passBorder(passFocused ? Color(255, 0, 150, 160) : Color(255, 220, 225, 230), passFocused ? 1.5f : 1.0f);
    g.DrawRectangle(&passBorder, L.fieldX, L.passY, L.fieldW, L.fldH);

    wstring passStr(s_password);
    float passTextX = L.fieldX + 32.0f;

    if (!passStr.empty()) {
        wstring passDisplay = s_showPassword ? passStr : wstring(passStr.size(), L'\u2022');
        SolidBrush passColorVal(Color(255, 50, 50, 50));
        g.DrawString(passDisplay.c_str(), -1, &fInput, RectF(passTextX, L.passY, L.fieldW - 65.0f, L.fldH), &fmtL, &passColorVal);
        if (passFocused && showCursor) {
            RectF bbox;
            g.MeasureString(passDisplay.c_str(), -1, &fInput, PointF(passTextX, L.passY), &fmtL, &bbox);
            float curX = passTextX + bbox.Width;
            if (curX > L.fieldX + L.fieldW - 40.0f) curX = L.fieldX + L.fieldW - 40.0f;
            Pen cursorPen(Color(255, 0, 150, 160), 2.0f);
            g.DrawLine(&cursorPen, curX, L.passY + 6.0f, curX, L.passY + L.fldH - 6.0f);
        }
    } else if (passFocused) {
        if (showCursor) {
            Pen cursorPen(Color(255, 0, 150, 160), 2.0f);
            g.DrawLine(&cursorPen, passTextX, L.passY + 8.0f, passTextX, L.passY + L.fldH - 8.0f);
        }
        SolidBrush passColorVal(Color(255, 160, 170, 180));
        g.DrawString(L"Password", -1, &fInput, RectF(passTextX, L.passY, L.fieldW - 65.0f, L.fldH), &fmtL, &passColorVal);
    } else {
        SolidBrush passColorVal(Color(255, 160, 170, 180));
        g.DrawString(L"Password", -1, &fInput, RectF(passTextX, L.passY, L.fieldW - 65.0f, L.fldH), &fmtL, &passColorVal);
    }
    g.DrawString(L"\xE72E", -1, &fFieldIcon, RectF(L.fieldX + 8.0f, L.passY, 24.0f, L.fldH), &fmtC, passStr.empty() ? &gray : &teal);

    Font fEye(&ffIcons, 13, FontStyleRegular, UnitPixel);
    SolidBrush eyeColor(s_hoverEye ? Color(255, 0, 150, 160) : Color(255, 160, 170, 180));
    g.DrawString(s_showPassword ? L"\xED1A" : L"\xE7B3", -1, &fEye, RectF(L.eyeX, L.eyeY, L.eyeW, L.fldH), &fmtC, &eyeColor);

    // ── Save Login Checkbox ──
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

    // ── Status Message ──
    if (!s_statusMsg.empty()) {
        SolidBrush statusBrush(s_isError ? Color(255, 220, 60, 60) : Color(255, 0, 160, 90));
        Font fStatus(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(s_statusMsg.c_str(), -1, &fStatus, RectF(L.cardX, L.loginBtnY - 22.0f, L.cardW, 18.0f), &fmtC, &statusBrush);
    } else if (s_isLoading) {
        Font fStatus(&ff, 11, FontStyleBold, UnitPixel);
        g.DrawString(L"Logging in...", -1, &fStatus, RectF(L.cardX, L.loginBtnY - 22.0f, L.cardW, 18.0f), &fmtC, &teal);
    }

    // ── Login Button ──
    GraphicsPath btnP1;
    float br = 6.0f, bd = br * 2.0f;
    btnP1.AddArc(L.loginBtnX, L.loginBtnY, bd, bd, 180.0f, 90.0f);
    btnP1.AddArc(L.loginBtnX + L.loginBtnW - bd, L.loginBtnY, bd, bd, 270.0f, 90.0f);
    btnP1.AddArc(L.loginBtnX + L.loginBtnW - bd, L.loginBtnY + L.loginBtnH - bd, bd, bd, 0.0f, 90.0f);
    btnP1.AddArc(L.loginBtnX, L.loginBtnY + L.loginBtnH - bd, bd, bd, 90.0f, 90.0f);
    btnP1.CloseFigure();
    LinearGradientBrush loginBg(PointF(L.loginBtnX, L.loginBtnY), PointF(L.loginBtnX + L.loginBtnW, L.loginBtnY + L.loginBtnH),
        Color(255, 0, 120, 140), Color(255, 0, 180, 200));
    g.FillPath(&loginBg, &btnP1);
    Font fBtn(&ff, 13, FontStyleBold, UnitPixel);
    g.DrawString(L"Log In", -1, &fBtn, RectF(L.loginBtnX, L.loginBtnY, L.loginBtnW, L.loginBtnH), &fmtC, &white);

    // ── Cancel Button ──
    GraphicsPath btnP2;
    btnP2.AddArc(L.cancelBtnX, L.cancelBtnY, bd, bd, 180.0f, 90.0f);
    btnP2.AddArc(L.cancelBtnX + L.cancelBtnW - bd, L.cancelBtnY, bd, bd, 270.0f, 90.0f);
    btnP2.AddArc(L.cancelBtnX + L.cancelBtnW - bd, L.cancelBtnY + L.cancelBtnH - bd, bd, bd, 0.0f, 90.0f);
    btnP2.AddArc(L.cancelBtnX, L.cancelBtnY + L.cancelBtnH - bd, bd, bd, 90.0f, 90.0f);
    btnP2.CloseFigure();
    SolidBrush cancelBg(s_hoverCancel ? Color(255, 245, 250, 255) : Color(255, 255, 255, 255));
    g.FillPath(&cancelBg, &btnP2);
    Pen cancelPen(Color(255, 0, 150, 180), 1.5f);
    g.DrawPath(&cancelPen, &btnP2);
    g.DrawString(L"Cancel", -1, &fBtn, RectF(L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH), &fmtC, &teal);

    // ── Links ──
    Font fLinkU(&ff, 11, FontStyleUnderline, UnitPixel);
    SolidBrush linkTeal(Color(255, 0, 150, 180));
    SolidBrush linkHover(Color(255, 0, 100, 130));

    g.DrawString(L"Create an account", -1, &fLinkU, RectF(L.signupX, L.signupY, L.signupW, L.signupH), &fmtL, s_hoverSignup ? &linkHover : &linkTeal);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar); fmtR.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"Reset Password", -1, &fLinkU, RectF(L.resetX, L.resetY, L.resetW, L.resetH), &fmtR, s_hoverReset ? &linkHover : &linkTeal);
    g.DrawString(L"Privacy Policy", -1, &fLinkU, RectF(L.privacyX, L.privacyY, L.privacyW, L.privacyH), &fmtC, s_hoverPrivacy ? &linkHover : &linkTeal);
}

// ============================================================
//  MOUSE MOVE
// ============================================================
void ProcessAccountsMouseMove(float x, float y) {
    if (s_showSignup) {
        float scaledW = (float)windowWidth  / g_scaleFactor;
        float scaledH = (float)windowHeight / g_scaleFactor;
        float cx = (float)SIDEBAR_WIDTH;
        float cy = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
        float cw = scaledW - cx, ch = scaledH - cy;
        float cardW = 400.0f, cardH = 530.0f;
        float cardX = cx + (cw - cardW) / 2.0f;
        float cardY = cy + (ch - cardH) / 2.0f;
        float fieldX = cardX + 35.0f, fieldW = cardW - 70.0f, fldH = 36.0f;
        float nameY = cardY + 65.0f, emailY = nameY + fldH + 14.0f;
        float passY = emailY + fldH + 14.0f, confirmY = passY + fldH + 14.0f;
        float statusY = confirmY + fldH + 8.0f;
        float btnY = statusY + 26.0f, btnGap = 10.0f;
        float btnW = (fieldW - btnGap) / 2.0f, btnH = 38.0f;
        float suBtnX = fieldX, backBtnX = fieldX + btnW + btnGap;
        float eyeW = 32.0f;

        s_hoverSuBtn  = HitRect(x, y, suBtnX,   btnY, btnW, btnH);
        s_hoverSuBack = HitRect(x, y, backBtnX,  btnY, btnW, btnH);
        s_hoverSuEye1 = HitRect(x, y, fieldX + fieldW - eyeW, passY,    eyeW, fldH);
        s_hoverSuEye2 = HitRect(x, y, fieldX + fieldW - eyeW, confirmY, eyeW, fldH);

        bool onText = HitRect(x, y, fieldX, nameY, fieldW, fldH) ||
                      HitRect(x, y, fieldX, emailY, fieldW, fldH) ||
                      HitRect(x, y, fieldX, passY,  fieldW - eyeW, fldH) ||
                      HitRect(x, y, fieldX, confirmY, fieldW - eyeW, fldH);
        if (onText)                                        SetCursor(LoadCursor(NULL, IDC_IBEAM));
        else if (s_hoverSuBtn || s_hoverSuBack || s_hoverSuEye1 || s_hoverSuEye2)
                                                           SetCursor(LoadCursor(NULL, IDC_HAND));
        else                                               SetCursor(LoadCursor(NULL, IDC_ARROW));

        if (HWND hw = FindWindowA("RasFocusCore", NULL)) InvalidateRect(hw, NULL, FALSE);
        return;
    }

    if (!g_loggedInEmail.empty()) {
        CardLayout L = GetCurrentLayout();
        float logoutW = 150.0f, logoutH = 40.0f;
        float logoutX = L.cardX + (L.cardW - logoutW) / 2.0f;
        float logoutY = L.cardY + L.cardH - 65.0f;
        s_hoverLogout = HitRect(x, y, logoutX, logoutY, logoutW, logoutH);
        
        if (s_hoverLogout) SetCursor(LoadCursor(NULL, IDC_HAND));
        else SetCursor(LoadCursor(NULL, IDC_ARROW));
        return;
    }

    CardLayout L = GetCurrentLayout();
    s_hoverLogin     = HitRect(x, y, L.loginBtnX,  L.loginBtnY,  L.loginBtnW,  L.loginBtnH);
    s_hoverCancel    = HitRect(x, y, L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH);
    s_hoverEye       = HitRect(x, y, L.eyeX,       L.eyeY,       L.eyeW,       L.fldH);
    s_hoverSaveCheck = HitRect(x, y, L.fieldX,     L.checkY - 5.0f, 120.0f,    24.0f);
    s_hoverSignup    = HitRect(x, y, L.signupX,    L.signupY,    L.signupW,    L.signupH);
    s_hoverReset     = HitRect(x, y, L.resetX,     L.resetY,     L.resetW,     L.resetH);
    s_hoverPrivacy   = HitRect(x, y, L.privacyX,   L.privacyY,   L.privacyW,   L.privacyH);

    bool hoverEmailText = HitRect(x, y, L.fieldX, L.emailY, L.fieldW - L.eyeW, L.fldH);
    bool hoverPassText  = HitRect(x, y, L.fieldX, L.passY, L.fieldW - L.eyeW, L.fldH);

    if (hoverEmailText || hoverPassText) {
        SetCursor(LoadCursor(NULL, IDC_IBEAM)); 
    } else if (s_hoverLogin || s_hoverCancel || s_hoverSignup || s_hoverReset || s_hoverPrivacy || s_hoverSaveCheck || s_hoverEye) {
        SetCursor(LoadCursor(NULL, IDC_HAND)); 
    } else {
        SetCursor(LoadCursor(NULL, IDC_ARROW)); 
    }
}

// ============================================================
//  MOUSE CLICK
// ============================================================
void ProcessAccountsMouseClick(float x, float y, HWND hWnd) {
    if (s_showSignup) {
        float scaledW = (float)windowWidth  / g_scaleFactor;
        float scaledH = (float)windowHeight / g_scaleFactor;
        float cx = (float)SIDEBAR_WIDTH;
        float cy = (float)(TITLEBAR_HEIGHT + SUBHEADER_HEIGHT);
        float cw = scaledW - cx, ch = scaledH - cy;
        float cardW = 400.0f, cardH = 530.0f;
        float cardX = cx + (cw - cardW) / 2.0f;
        float cardY = cy + (ch - cardH) / 2.0f;
        float fieldX = cardX + 35.0f, fieldW = cardW - 70.0f, fldH = 36.0f;
        float nameY = cardY + 65.0f, emailY = nameY + fldH + 14.0f;
        float passY = emailY + fldH + 14.0f, confirmY = passY + fldH + 14.0f;
        float statusY = confirmY + fldH + 8.0f;
        float btnY = statusY + 26.0f, btnGap = 10.0f;
        float btnW = (fieldW - btnGap) / 2.0f, btnH = 38.0f;
        float suBtnX = fieldX, backBtnX = fieldX + btnW + btnGap;
        float eyeW = 32.0f;

        if (s_su_success) {
            float lbW = 160.0f, lbH = 38.0f;
            float lbX = cardX + (cardW - lbW) / 2.0f;
            float lbY = cardY + cardH / 2.0f + 30.0f;
            if (HitRect(x, y, lbX, lbY, lbW, lbH)) {
                s_showSignup = false;
                s_su_success = false;
                ZeroMemory(s_su_name, sizeof(s_su_name));
                ZeroMemory(s_su_email, sizeof(s_su_email));
                ZeroMemory(s_su_pass, sizeof(s_su_pass));
                ZeroMemory(s_su_confirm, sizeof(s_su_confirm));
                s_su_statusMsg = L"";
                s_su_focus = 0;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return;
        }

        if (HitRect(x, y, fieldX, nameY,    fieldW - eyeW, fldH)) { s_su_focus = 1; s_lastBlinkTime = GetTickCount(); InvalidateRect(hWnd, NULL, FALSE); return; }
        if (HitRect(x, y, fieldX, emailY,   fieldW - eyeW, fldH)) { s_su_focus = 2; s_lastBlinkTime = GetTickCount(); InvalidateRect(hWnd, NULL, FALSE); return; }
        if (HitRect(x, y, fieldX, passY,    fieldW - eyeW, fldH)) { s_su_focus = 3; s_lastBlinkTime = GetTickCount(); InvalidateRect(hWnd, NULL, FALSE); return; }
        if (HitRect(x, y, fieldX, confirmY, fieldW - eyeW, fldH)) { s_su_focus = 4; s_lastBlinkTime = GetTickCount(); InvalidateRect(hWnd, NULL, FALSE); return; }

        if (HitRect(x, y, fieldX + fieldW - eyeW, passY,    eyeW, fldH)) { s_su_showPass    = !s_su_showPass;    InvalidateRect(hWnd, NULL, FALSE); return; }
        if (HitRect(x, y, fieldX + fieldW - eyeW, confirmY, eyeW, fldH)) { s_su_showConfirm = !s_su_showConfirm; InvalidateRect(hWnd, NULL, FALSE); return; }

        if (HitRect(x, y, backBtnX, btnY, btnW, btnH)) {
            s_showSignup = false;
            s_su_statusMsg = L"";
            s_su_focus = 0;
            InvalidateRect(hWnd, NULL, FALSE);
            return;
        }

        if (HitRect(x, y, suBtnX, btnY, btnW, btnH)) {
            wstring nameW(s_su_name), emailW(s_su_email), passW(s_su_pass), confirmW(s_su_confirm);            if (nameW.empty() || emailW.empty() || passW.empty() || confirmW.empty()) {
                s_su_statusMsg = L"Please fill in all fields."; s_su_isError = true;
                InvalidateRect(hWnd, NULL, FALSE); return;
            }
            if (passW != confirmW) {
                s_su_statusMsg = L"Passwords do not match."; s_su_isError = true;
                InvalidateRect(hWnd, NULL, FALSE); return;
            }
            if (passW.size() < 6) {
                s_su_statusMsg = L"Password must be at least 6 characters."; s_su_isError = true;
                InvalidateRect(hWnd, NULL, FALSE); return;
            }
            s_su_isLoading = true; s_su_statusMsg = L""; s_su_isError = false;
            InvalidateRect(hWnd, NULL, FALSE);

            char emailA[512] = {}, passA[512] = {}, nameA[512] = {};
            WideCharToMultiByte(CP_UTF8, 0, emailW.c_str(), -1, emailA, 511, NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, passW.c_str(),  -1, passA,  511, NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, nameW.c_str(),  -1, nameA,  511, NULL, NULL);

            SignUpThreadData* data = new SignUpThreadData();
            data->hWnd     = hWnd;
            data->email    = emailA;
            data->password = passA;
            data->name     = nameA;
            _beginthread(SignUpThread, 0, data);
            return;
        }
        return;
    }

    if (!g_loggedInEmail.empty()) {
        CardLayout L = GetCurrentLayout();
        float logoutW = 150.0f, logoutH = 40.0f;
        float logoutX = L.cardX + (L.cardW - logoutW) / 2.0f;
        float logoutY = L.cardY + L.cardH - 65.0f;
        if (HitRect(x, y, logoutX, logoutY, logoutW, logoutH)) {
            g_loggedInEmail   = L"";
            g_loggedInUserUid = "";
            g_isPremiumUser   = false;
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

    if (HitRect(x, y, L.fieldX, L.emailY, L.fieldW - L.eyeW, L.fldH)) {
        s_focusField = 1; s_lastBlinkTime = GetTickCount();
        InvalidateRect(hWnd, NULL, FALSE); return;
    }
    if (HitRect(x, y, L.fieldX, L.passY, L.fieldW - L.eyeW, L.fldH)) {
        s_focusField = 2; s_lastBlinkTime = GetTickCount();
        InvalidateRect(hWnd, NULL, FALSE); return;
    }
    if (HitRect(x, y, L.eyeX, L.eyeY, L.eyeW, L.fldH)) {
        s_showPassword = !s_showPassword; InvalidateRect(hWnd, NULL, FALSE); return;
    }
    if (HitRect(x, y, L.fieldX, L.checkY - 5.0f, 120.0f, 24.0f)) {
        s_saveLogin = !s_saveLogin; InvalidateRect(hWnd, NULL, FALSE); return;
    }

    if (HitRect(x, y, L.signupX, L.signupY, L.signupW, L.signupH)) {
        s_showSignup = true;
        s_su_focus = 1;
        s_su_statusMsg = L"";
        s_su_success = false;
        s_lastBlinkTime = GetTickCount();
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }
    if (HitRect(x, y, L.resetX, L.resetY, L.resetW, L.resetH)) {
        ShellExecuteW(NULL, L"open", L"https://rasfocus.com/reset-password", NULL, NULL, SW_SHOWNORMAL); return;
    }
    if (HitRect(x, y, L.privacyX, L.privacyY, L.privacyW, L.privacyH)) {
        ShellExecuteW(NULL, L"open", L"https://rasfocus.com/privacy", NULL, NULL, SW_SHOWNORMAL); return;
    }

    if (HitRect(x, y, L.cancelBtnX, L.cancelBtnY, L.cancelBtnW, L.cancelBtnH)) {
        s_statusMsg = L""; s_focusField = 0;
        InvalidateRect(hWnd, NULL, FALSE); return;
    }

    if (HitRect(x, y, L.loginBtnX, L.loginBtnY, L.loginBtnW, L.loginBtnH)) {
        wstring emailW(s_email), passW(s_password);
        if (emailW.empty() || passW.empty()) {
            s_statusMsg = L"Please enter email and password."; s_isError = true;
            InvalidateRect(hWnd, NULL, FALSE); return;
        }
        s_isLoading = true; s_statusMsg = L""; s_isError = false;
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
//  KEYBOARD
// ============================================================
void ProcessAccountsChar(wchar_t c) {
    if (!g_loggedInEmail.empty()) return;

    if (s_showSignup && !s_su_success) {
        wchar_t* buf = nullptr;
        if      (s_su_focus == 1) buf = s_su_name;
        else if (s_su_focus == 2) buf = s_su_email;
        else if (s_su_focus == 3) buf = s_su_pass;
        else if (s_su_focus == 4) buf = s_su_confirm;
        if (!buf) return;

        if (c == L'\b') {
            int len = (int)wcslen(buf); if (len > 0) buf[len-1] = L'\0';
        } else if (c == L'\r' || c == L'\n') {
            s_su_focus = (s_su_focus % 4) + 1;
            s_lastBlinkTime = GetTickCount();
        } else if (c >= 32) {
            int len = (int)wcslen(buf); if (len < 510) { buf[len] = c; buf[len+1] = L'\0'; }
            s_lastBlinkTime = GetTickCount();
        }
        if (HWND hw = FindWindowA("RasFocusCore", NULL)) InvalidateRect(hw, NULL, FALSE);
        return;
    }

    if (c == L'\b') {
        if (s_focusField == 1) { int len = (int)wcslen(s_email);    if (len > 0) s_email[len-1] = L'\0'; }
        if (s_focusField == 2) { int len = (int)wcslen(s_password); if (len > 0) s_password[len-1] = L'\0'; }
    } else if (c == L'\r' || c == L'\n') {
        if (s_focusField == 1) s_focusField = 2; else s_focusField = 1;
        s_lastBlinkTime = GetTickCount();
    } else if (c >= 32) {
        if (s_focusField == 1) { int len = (int)wcslen(s_email);    if (len < 510) { s_email[len] = c; s_email[len+1] = L'\0'; } }
        if (s_focusField == 2) { int len = (int)wcslen(s_password); if (len < 510) { s_password[len] = c; s_password[len+1] = L'\0'; } }
        s_lastBlinkTime = GetTickCount();
    }
    if (HWND hw = FindWindowA("RasFocusCore", NULL)) InvalidateRect(hw, NULL, FALSE);
}

void ProcessAccountsKeyDown(WPARAM wp) {
    if (!g_loggedInEmail.empty()) return;

    if (s_showSignup && !s_su_success) {
        wchar_t* buf = nullptr;
        if      (s_su_focus == 1) buf = s_su_name;
        else if (s_su_focus == 2) buf = s_su_email;
        else if (s_su_focus == 3) buf = s_su_pass;
        else if (s_su_focus == 4) buf = s_su_confirm;

        if (wp == VK_TAB)    { s_su_focus = (s_su_focus % 4) + 1; s_lastBlinkTime = GetTickCount(); }
        else if (wp == VK_DELETE && buf) ZeroMemory(buf, 512 * sizeof(wchar_t));
        else if (wp == VK_ESCAPE) { s_su_focus = 0; s_su_statusMsg = L""; }
        if (HWND hw = FindWindowA("RasFocusCore", NULL)) InvalidateRect(hw, NULL, FALSE);
        return;
    }

    if (wp == VK_TAB) {
        s_focusField = (s_focusField == 1) ? 2 : 1;
        s_lastBlinkTime = GetTickCount();
    } else if (wp == VK_DELETE) {
        if (s_focusField == 1) ZeroMemory(s_email,    sizeof(s_email));
        if (s_focusField == 2) ZeroMemory(s_password, sizeof(s_password));
    } else if (wp == VK_ESCAPE) {
        s_focusField = 0; s_statusMsg = L"";
    }
    if (HWND hw = FindWindowA("RasFocusCore", NULL)) InvalidateRect(hw, NULL, FALSE);
}
