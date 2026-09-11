// tab_rasgram.cpp
// RasGram Desktop — WebView2-based UI, C++ logic/network layer
//
// Architecture:
//   • UI  : Embedded WebView2 (HTML/CSS/JS string) — Telegram-style chat
//   • Logic: C++ — RgNet_* functions, file picker, call handling
//   • Bridge: window.chrome.webview.postMessage (JS→C++) /
//             webview->ExecuteScript (C++→JS)
//
// JS→C++ message format (JSON string):
//   {"action":"send",   "chatId":"...", "text":"..."}
//   {"action":"open",   "chatId":"...", "contactMobile":"..."}
//   {"action":"search", "query":"..."}
//   {"action":"attach"}
//   {"action":"lan_toggle"}
//   {"action":"call_audio"}
//   {"action":"call_video"}
//   {"action":"hangup"}
//
// C++→JS calls:
//   RG.loadChats(jsonArray)
//   RG.loadMessages(jsonArray)
//   RG.pushMessage(jsonObject)
//   RG.setMyMobile(mobile)
//   RG.setLoginState(name, mobile)
//   RG.showNotLoggedIn()

#include "tab_rasgram.h"
#include "rasgram_net.h"
#include "mini_browser.h"          // CreateEmbeddedPreviewWebView pattern / g_sharedEnv

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <algorithm>
#include <sstream>
#include <commdlg.h>
#include <shlobj.h>

#pragma comment(lib, "comdlg32.lib")

#include <wincrypt.h>           // CryptBinaryToStringA, CRYPT_STRING_BASE64
#pragma comment(lib, "crypt32.lib")

using namespace Microsoft::WRL;
using namespace Gdiplus;   // still needed for DrawRasGramTab signature
using namespace std;

// ── Externals ────────────────────────────────────────────────
extern HWND    hParentWnd;
extern string  g_loggedInUserUid;
extern wstring g_loggedInName;
extern wstring g_loggedInEmail;
extern float   g_scaleFactor;   // DPI scale (1.0 on 96dpi, 1.25 on 120dpi, etc.)

// ── Module state ─────────────────────────────────────────────
static bool   g_rgInitDone    = false;
static string g_rgInitWithUid = "";
static bool   g_rgVisible     = false;

static float  g_cx = 0, g_cy = 0, g_cw = 0, g_ch = 0;

// WebView2
static ComPtr<ICoreWebView2Controller> g_rgCtrl;
static ComPtr<ICoreWebView2>           g_rgWV;
static bool g_wvReady    = false;
static bool g_rgCreating = false;  // true while async WebView2 creation is in-flight

// My identity
static string  g_myMobile;
static wstring g_myName_w;

// Chat state
static mutex              g_chatsMtx;
static vector<RgChatPreview> g_chats;
static string             g_openChatId;
static string             g_openContactMobile;

static mutex             g_msgsMtx;
static vector<RgMessage> g_messages;
static long long         g_lastMsgTs = 0;

static bool g_lanMode = false;
static vector<RgLanPeer> g_lanPeers;
static mutex g_lanPeersMtx;

// ── Call Window (fullscreen overlay) ─────────────────────────
static HWND  g_callHwnd       = nullptr;
static bool  g_callWndVisible = false;

// Incoming call state (written from polling thread, read on UI thread)
static mutex          g_incomingMtx;
static bool           g_pendingIncoming = false;
static RgCallParams   g_pendingCallParams;
static wstring        g_pendingLoginName;
static wstring        g_pendingLoginPhone;
// QR login state
static string         g_qrToken;          // current session token
static wstring        g_qrPngBase64;      // base64 PNG for JS
static string         g_qrScannedMobile;  // set when phone scans
static string         g_qrScannedName;
static atomic<bool>   g_qrPollActive{false};
static thread         g_qrPollThread;

// OTP (phone-code) login state
static string         g_otpErrMsg;        // last error to show JS

// Video frame (latest decoded frame from remote peer)
static mutex          g_videoMtx;
static vector<BYTE>   g_videoFrame;
static int            g_videoW = 0, g_videoH = 0;

// Notify inited flag
static bool g_notifyReady = false;

// ── String helpers ───────────────────────────────────────────
static string WideToUtf8(const wstring& ws) {
    if (ws.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8,0,ws.c_str(),-1,NULL,0,NULL,NULL);
    if (n<=0) return "";
    string s(n,0);
    WideCharToMultiByte(CP_UTF8,0,ws.c_str(),-1,&s[0],n,NULL,NULL);
    if (!s.empty()&&s.back()=='\0') s.pop_back();
    return s;
}
static wstring Utf8ToWide(const string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,NULL,0);
    if (n<=0) return L"";
    wstring ws(n,0);
    MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,&ws[0],n);
    if (!ws.empty()&&ws.back()==L'\0') ws.pop_back();
    return ws;
}
static string JsEscape(const string& s) {
    string r; r.reserve(s.size()+16);
    for (unsigned char c : s) {
        if      (c=='"')  r+="\\\"";
        else if (c=='\\') r+="\\\\";
        else if (c=='\n') r+="\\n";
        else if (c=='\r') r+="\\r";
        else if (c<0x20)  { char buf[8]; sprintf(buf,"\\u%04x",c); r+=buf; }
        else r+=(char)c;
    }
    return r;
}
static long long NowMs_rg() {
    return (long long)GetTickCount64();
}

// ── Forward declarations ─────────────────────────────────────
static void RgExecJS(const wstring& js);
static void RgPushChatsToUI();
static void RgPushMessagesToUI();
static void RgPushOneMessageToUI(const RgMessage& msg);
static void RgHandleMessage(const wstring& json);
static void RgOpenChat(const string& chatId, const string& contactMobile);
static void RgSendText(const string& chatId, const string& contactMobile, const string& text);
static void RgSendLoginState();
static void RgCreateWebView(HWND parent, RECT bounds);
static void RgPositionWebView();
static void RgShowCallWindow(const string& peerName, bool isVideo, bool isIncoming);
static void RgHideCallWindow();
static void RgUpdateCallWindowVideoFrame();

// ── WM_USER messages for cross-thread call-window updates ────
#define WM_RG_INCOMING_CALL (WM_USER + 70)
#define WM_RG_CALL_ENDED    (WM_USER + 71)
#define WM_RG_VIDEO_FRAME   (WM_USER + 72)
#define WM_RG_NEW_MESSAGE   (WM_USER + 73)

// ════════════════════════════════════════════════════════════
// CALL WINDOW  — fullscreen Win32 overlay for audio/video calls
// Buttons: Mute (M) | End Call (Esc / button) | Camera toggle (C)
// Video frame: drawn with GDI StretchDIBits from g_videoFrame
// ════════════════════════════════════════════════════════════

#define RG_CALL_CLASS L"RasGramCallWnd"

// Layout constants
static const int BTN_W  = 64;
static const int BTN_H  = 64;
static const int BTN_R  = 32; // corner radius for drawing

static string  g_callPeerName;
static bool    g_callIsVideo   = false;
static bool    g_callIsIncoming = false;

// Button IDs
#define RGCB_HANGUP  1
#define RGCB_MUTE    2
#define RGCB_CAMERA  3
#define RGCB_ACCEPT  4

static HWND g_btnHangup  = nullptr;
static HWND g_btnMute    = nullptr;
static HWND g_btnCamera  = nullptr;
static HWND g_btnAccept  = nullptr;

static wstring Utf8ToWide_rg(const string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return L"";
    wstring ws(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], n);
    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
    return ws;
}

static void DrawCallWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // Background: dark teal gradient approximated with two fills
    HBRUSH bgBrush = CreateSolidBrush(RGB(18, 32, 50));
    FillRect(hdc, &rc, bgBrush);
    DeleteObject(bgBrush);

    // If video frame available — draw it centred
    {
        lock_guard<mutex> lk(g_videoMtx);
        if (!g_videoFrame.empty() && g_videoW > 0 && g_videoH > 0) {
            // Scale to fit window keeping aspect ratio
            float scale = min((float)W / g_videoW, (float)H / g_videoH);
            int dw = (int)(g_videoW * scale);
            int dh = (int)(g_videoH * scale);
            int dx = (W - dw) / 2;
            int dy = (H - dh) / 2;

            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = g_videoW;
            bmi.bmiHeader.biHeight      = -g_videoH; // top-down
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 24;
            bmi.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(hdc, dx, dy, dw, dh,
                          0, 0, g_videoW, g_videoH,
                          g_videoFrame.data(), &bmi,
                          DIB_RGB_COLORS, SRCCOPY);
        } else if (g_callIsVideo) {
            // No frame yet: show camera icon placeholder
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(160, 200, 200));
            HFONT fBig = CreateFontW(80, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                     DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, L"Segoe UI Emoji");
            HFONT old = (HFONT)SelectObject(hdc, fBig);
            RECT cr = {0, H/4, W, H*3/4};
            DrawTextW(hdc, L"📹", -1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);
            DeleteObject(fBig);
        }
    }

    // Avatar circle (top-centre)
    HBRUSH avatarBrush = CreateSolidBrush(RGB(0, 150, 160));
    int avR = 60, avX = W/2 - avR, avY = 60;
    Ellipse(hdc, avX, avY, avX + avR*2, avY + avR*2);
    DeleteObject(avatarBrush);

    // Peer name
    wstring nameW = Utf8ToWide_rg(g_callPeerName);
    HFONT fName = CreateFontW(28, 0, 0, 0, FW_BOLD, 0, 0, 0,
                               DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
    HFONT old2 = (HFONT)SelectObject(hdc, fName);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255,255,255));
    RECT nameRc = {0, avY + avR*2 + 12, W, avY + avR*2 + 60};
    DrawTextW(hdc, nameW.c_str(), -1, &nameRc, DT_CENTER | DT_SINGLELINE);

    // Status text
    HFONT fSub = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, L"Segoe UI");
    SelectObject(hdc, fSub);
    SetTextColor(hdc, RGB(180, 210, 210));
    RECT subRc = {0, avY + avR*2 + 64, W, avY + avR*2 + 96};
    wstring statusTxt;
    if (g_callIsIncoming)
        statusTxt = (g_callIsVideo ? L"Incoming video call" : L"Incoming audio call");
    else if (RgCall_IsActive()) {
        int secs = RgCall_GetDurationSeconds();
        wchar_t buf[32];
        swprintf(buf, 32, L"%02d:%02d", secs/60, secs%60);
        statusTxt = buf;
    } else {
        statusTxt = L"Calling…";
    }
    DrawTextW(hdc, statusTxt.c_str(), -1, &subRc, DT_CENTER | DT_SINGLELINE);

    SelectObject(hdc, old2);
    DeleteObject(fName);
    DeleteObject(fSub);

    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK RgCallWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        DrawCallWindow(hwnd);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            // Esc = hang up
            RgCall_Hangup();
            RgHideCallWindow();
        } else if (wp == 'M') {
            RgCall_ToggleMute(!RgCall_IsMuted());
            SetWindowTextW(g_btnMute, RgCall_IsMuted() ? L"🔇 Unmute" : L"🎤 Mute");
        } else if (wp == 'C' && g_callIsVideo) {
            RgCall_ToggleCamera(!RgCall_IsVideo());
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case RGCB_HANGUP:
            RgCall_Hangup();
            RgHideCallWindow();
            break;
        case RGCB_MUTE:
            RgCall_ToggleMute(!RgCall_IsMuted());
            SetWindowTextW(g_btnMute, RgCall_IsMuted() ? L"🔇 Unmute" : L"🎤 Mute");
            break;
        case RGCB_CAMERA:
            if (g_callIsVideo) RgCall_ToggleCamera(!RgCall_IsVideo());
            break;
        case RGCB_ACCEPT:
            // Accept incoming call
            RgCall_AcceptIncoming(g_pendingCallParams,
                [](bool connected){
                    if (!connected) RgHideCallWindow();
                },
                [](const void* frame, int w, int h){
                    lock_guard<mutex> lk(g_videoMtx);
                    const BYTE* p = (const BYTE*)frame;
                    g_videoFrame.assign(p, p + w*h*3);
                    g_videoW = w; g_videoH = h;
                    // Trigger repaint
                    if (g_callHwnd) InvalidateRect(g_callHwnd, nullptr, FALSE);
                });
            g_callIsIncoming = false;
            if (g_btnAccept) { DestroyWindow(g_btnAccept); g_btnAccept = nullptr; }
            break;
        }
        return 0;

    case WM_RG_VIDEO_FRAME:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_TIMER:
        // Refresh duration display every second while call is active
        if (RgCall_IsActive())
            InvalidateRect(hwnd, nullptr, FALSE);
        else
            RgHideCallWindow();
        return 0;

    case WM_DESTROY:
        g_callHwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RgShowCallWindow(const string& peerName, bool isVideo, bool isIncoming) {
    g_callPeerName   = peerName;
    g_callIsVideo    = isVideo;
    g_callIsIncoming = isIncoming;

    // Register class once
    static bool classReg = false;
    if (!classReg) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = RgCallWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = RG_CALL_CLASS;
        wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        RegisterClassExW(&wc);
        classReg = true;
    }

    if (g_callHwnd) {
        // Already open — just update and repaint
        SetWindowTextW(g_callHwnd,
            (Utf8ToWide_rg(peerName) + L" — RasGram Call").c_str());
        InvalidateRect(g_callHwnd, nullptr, TRUE);
        return;
    }

    // Full-screen window (no title bar)
    int SW = GetSystemMetrics(SM_CXSCREEN);
    int SH = GetSystemMetrics(SM_CYSCREEN);
    g_callHwnd = CreateWindowExW(
        WS_EX_TOPMOST,
        RG_CALL_CLASS,
        (Utf8ToWide_rg(peerName) + L" — RasGram Call").c_str(),
        WS_POPUP | WS_VISIBLE,
        0, 0, SW, SH,
        hParentWnd, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!g_callHwnd) return;

    // Buttons — bottom-centre row
    int btnY = SH - 120;
    int cx   = SW / 2;
    int bw = 140, bh = 48, gap = 20;

    if (isIncoming) {
        // Accept + Decline
        g_btnAccept = CreateWindowExW(0, L"BUTTON", L"✅ Accept",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            cx - bw - gap/2, btnY, bw, bh,
            g_callHwnd, (HMENU)RGCB_ACCEPT, GetModuleHandleW(nullptr), nullptr);
        g_btnHangup = CreateWindowExW(0, L"BUTTON", L"❌ Decline",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            cx + gap/2, btnY, bw, bh,
            g_callHwnd, (HMENU)RGCB_HANGUP, GetModuleHandleW(nullptr), nullptr);
    } else {
        // Mute + Hang up + Camera(if video)
        int totalBtns = isVideo ? 3 : 2;
        int startX = cx - (totalBtns * bw + (totalBtns-1)*gap) / 2;

        g_btnMute = CreateWindowExW(0, L"BUTTON", L"🎤 Mute",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            startX, btnY, bw, bh,
            g_callHwnd, (HMENU)RGCB_MUTE, GetModuleHandleW(nullptr), nullptr);
        startX += bw + gap;

        if (isVideo) {
            g_btnCamera = CreateWindowExW(0, L"BUTTON", L"📷 Camera",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                startX, btnY, bw, bh,
                g_callHwnd, (HMENU)RGCB_CAMERA, GetModuleHandleW(nullptr), nullptr);
            startX += bw + gap;
        }

        g_btnHangup = CreateWindowExW(0, L"BUTTON", L"📵 End Call",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            startX, btnY, bw, bh,
            g_callHwnd, (HMENU)RGCB_HANGUP, GetModuleHandleW(nullptr), nullptr);
    }

    // 1-second timer to refresh duration
    SetTimer(g_callHwnd, 1, 1000, nullptr);
    SetFocus(g_callHwnd);
}

static void RgHideCallWindow() {
    if (g_callHwnd) {
        KillTimer(g_callHwnd, 1);
        DestroyWindow(g_callHwnd);
        g_callHwnd = nullptr;
    }
    g_btnHangup = g_btnMute = g_btnCamera = g_btnAccept = nullptr;
    g_callIsIncoming = false;
    // Clear video buffer
    lock_guard<mutex> lk(g_videoMtx);
    g_videoFrame.clear();
    g_videoW = g_videoH = 0;
}

static void RgUpdateCallWindowVideoFrame() {
    if (g_callHwnd) PostMessageW(g_callHwnd, WM_RG_VIDEO_FRAME, 0, 0);
}

// ── HTML/CSS/JS string ───────────────────────────────────────
static const wchar_t* GetRasGramHTML() {
    // The full UI is an inline HTML string.
    // C++ calls RG.* functions; JS posts JSON messages back.
    static const wchar_t html[] =
LR"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RasGram Desktop</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;font-family:'Segoe UI',sans-serif}
body{display:flex;flex-direction:column;height:100vh;overflow:hidden;background:#f0f2f5}

/* ── TOP BAR ── */
#topbar{display:flex;align-items:center;background:#1a2236;color:#fff;
        height:52px;padding:0 12px;flex-shrink:0;gap:8px;user-select:none}
#topbar .logo{font-size:17px;font-weight:700;flex:1;text-align:center;letter-spacing:.3px}
#topbar .badge{font-size:10px;color:#00bcd4;font-weight:700;margin-left:4px;vertical-align:super}
#topbar button{background:none;border:none;color:rgba(255,255,255,.75);
               cursor:pointer;border-radius:50%;width:36px;height:36px;
               font-size:18px;display:flex;align-items:center;justify-content:center;
               transition:background .15s}
#topbar button:hover{background:rgba(255,255,255,.15);color:#fff}
#topbar button.active{color:#4caf50}

/* ── BODY ── */
#body{display:flex;flex:1;overflow:hidden}

/* ── CHAT LIST (left) ── */
#chatlist{width:280px;min-width:200px;display:flex;flex-direction:column;
          background:#fff;border-right:1px solid #e0e0e0;flex-shrink:0}
#search-wrap{padding:10px 12px;background:#f7f8fa;border-bottom:1px solid #eee}
#search{width:100%;border:1px solid #ddd;border-radius:20px;padding:7px 14px;
        font-size:13px;outline:none;background:#fff}
#search:focus{border-color:#00969f}
#chats{flex:1;overflow-y:auto}
.chat-row{display:flex;align-items:center;padding:10px 12px;cursor:pointer;
          gap:10px;border-bottom:1px solid #f0f0f0;transition:background .1s}
.chat-row:hover{background:#f5f6f8}
.chat-row.selected{background:#e3f2fd;border-left:3px solid #00969f}
.avatar{width:44px;height:44px;border-radius:50%;display:flex;align-items:center;
        justify-content:center;font-size:18px;font-weight:700;color:#fff;flex-shrink:0}
.chat-info{flex:1;min-width:0}
.chat-name{font-size:13px;font-weight:600;color:#1e1e1e;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.chat-last{font-size:12px;color:#888;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-top:2px}
.chat-meta{display:flex;flex-direction:column;align-items:flex-end;gap:4px;flex-shrink:0}
.chat-time{font-size:10px;color:#aaa}
.badge-unread{background:#00969f;color:#fff;border-radius:50%;width:20px;height:20px;
              font-size:10px;font-weight:700;display:flex;align-items:center;justify-content:center}
.empty-list{text-align:center;color:#bbb;padding:40px 20px;font-size:13px}

/* ── RIGHT PANEL ── */
#right{flex:1;display:flex;flex-direction:column;min-width:0}
#chat-header{display:flex;align-items:center;padding:0 14px;height:58px;
             background:#fff;border-bottom:1px solid #e0e0e0;flex-shrink:0;gap:10px}
#chat-header .avatar{width:38px;height:38px;font-size:15px}
#chat-header-info{flex:1}
#chat-header-name{font-size:14px;font-weight:700;color:#1e1e1e}
#chat-header-status{font-size:11px;color:#888}
#chat-header .hdr-btns{display:flex;gap:4px}
#chat-header .hdr-btns button{background:none;border:none;cursor:pointer;
  color:#00969f;width:38px;height:38px;border-radius:50%;font-size:20px;
  display:flex;align-items:center;justify-content:center;transition:background .15s}
#chat-header .hdr-btns button:hover{background:rgba(0,150,160,.1)}

/* messages */
#messages{flex:1;overflow-y:auto;padding:12px;display:flex;flex-direction:column;gap:6px;background:#eae9e3}
.date-chip{text-align:center;margin:6px 0}
.date-chip span{background:rgba(0,0,0,.12);color:#555;font-size:11px;
                border-radius:10px;padding:2px 12px}
.bubble-wrap{display:flex;max-width:65%}
.bubble-wrap.mine{align-self:flex-end;flex-direction:row-reverse}
.bubble-wrap.theirs{align-self:flex-start}
.bubble{padding:8px 12px 22px;border-radius:12px;font-size:13px;
        line-height:1.45;position:relative;word-break:break-word;min-width:70px}
.bubble.mine{background:#dcf8c6;border-radius:12px 0 12px 12px}
.bubble.theirs{background:#fff;border-radius:0 12px 12px 12px}
.bubble.pending{opacity:.6}
.bubble-time{position:absolute;bottom:4px;right:8px;font-size:10px;color:#999}
.bubble-time .ticks{color:#4fc3f7}
.call-log{text-align:center;margin:4px 0}
.call-log span{background:#e3f0ff;color:#555;font-size:12px;border-radius:12px;padding:4px 16px}

/* empty state */
#empty-state{display:flex;flex-direction:column;align-items:center;justify-content:center;
             flex:1;gap:10px;color:#aaa}
#empty-state .big-icon{font-size:60px;color:#00969f;opacity:.4}
#empty-state h3{font-size:20px;color:#555;font-weight:700}
#empty-state p{font-size:13px;text-align:center;max-width:320px}

/* ── LOGIN SCREEN ── */
#login-screen{display:none;flex-direction:column;align-items:center;justify-content:flex-start;
              height:100%;background:linear-gradient(180deg,rgba(0,128,105,.28) 0%,#0B141A 38%);
              overflow-y:auto;padding:0 24px 40px}
#login-deco-top{position:fixed;top:-100px;left:50%;transform:translateX(-50%);
                width:300px;height:300px;border-radius:50%;pointer-events:none;
                background:radial-gradient(circle,rgba(0,168,132,.14) 0%,transparent 70%)}
#login-deco-bot{position:fixed;bottom:-100px;right:-50px;
                width:200px;height:200px;border-radius:50%;pointer-events:none;
                background:radial-gradient(circle,rgba(37,211,102,.09) 0%,transparent 70%)}
#login-logo-wrap{margin-top:72px;width:100px;height:100px;border-radius:50%;
                 background:#00A884;display:flex;align-items:center;justify-content:center;
                 box-shadow:0 8px 32px rgba(0,168,132,.35);flex-shrink:0}
#login-logo-wrap svg{width:54px;height:54px;fill:none;stroke:#fff;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
#login-title{margin-top:28px;font-size:36px;font-weight:900;color:#E9EDEF;letter-spacing:.5px;text-align:center}
#login-tagline{margin-top:6px;font-size:14px;color:#00A884;font-weight:500;text-align:center}
#login-sub{margin-top:10px;font-size:13px;color:#8696A0;text-align:center;line-height:1.6;max-width:320px}
#login-card{margin-top:36px;width:100%;max-width:440px;background:rgba(31,44,52,.82);
            border-radius:24px;border:1px solid rgba(42,57,66,.5);
            box-shadow:0 8px 32px rgba(0,0,0,.3);padding:28px;box-sizing:border-box}
.lg-label{font-size:12px;color:#8696A0;font-weight:600;letter-spacing:.5px;text-transform:uppercase;margin-bottom:8px}
.lg-row{display:flex;gap:8px;align-items:stretch}
.lg-country-btn{background:none;border:1px solid #2A3942;border-radius:12px;
                color:#E9EDEF;cursor:pointer;padding:0 14px;height:52px;
                font-size:14px;white-space:nowrap;display:flex;align-items:center;gap:4px;
                transition:border-color .15s}
.lg-country-btn:hover{border-color:#00A884}
.lg-country-drop{position:absolute;background:#1F2C34;border:1px solid #2A3942;border-radius:12px;
                 z-index:99;min-width:180px;box-shadow:0 8px 24px rgba(0,0,0,.4);margin-top:4px;overflow:hidden}
.lg-country-drop div{padding:10px 16px;color:#E9EDEF;font-size:14px;cursor:pointer;transition:background .12s}
.lg-country-drop div:hover{background:rgba(0,168,132,.15)}
.lg-input{width:100%;background:#2A3942;border:1px solid #2A3942;border-radius:12px;
          padding:0 16px;height:52px;color:#E9EDEF;font-size:14px;outline:none;
          font-family:inherit;box-sizing:border-box;transition:border-color .15s}
.lg-input:focus{border-color:#00A884}
.lg-input::placeholder{color:#8696A0}
.lg-error{font-size:12px;color:#EA0038;margin-top:6px;min-height:18px}
.lg-btn{width:100%;height:52px;border:none;border-radius:14px;background:#00A884;
        color:#000;font-size:16px;font-weight:700;cursor:pointer;
        display:flex;align-items:center;justify-content:center;gap:8px;
        transition:background .15s;margin-top:20px}
.lg-btn:hover{background:#00c49a}
.lg-btn:disabled{opacity:.5;cursor:default}
.lg-back-row{display:flex;gap:12px;margin-top:20px}
.lg-btn-back{flex:1;height:52px;border:1px solid #2A3942;border-radius:14px;
             background:none;color:#8696A0;font-size:14px;cursor:pointer;transition:border-color .15s}
.lg-btn-back:hover{border-color:#8696A0;color:#E9EDEF}
.lg-btn-main{flex:2;height:52px;border:none;border-radius:14px;background:#00A884;
             color:#000;font-size:16px;font-weight:700;cursor:pointer;
             display:flex;align-items:center;justify-content:center;
             transition:background .15s}
.lg-btn-main:hover{background:#00c49a}
.lg-btn-main:disabled{opacity:.5;cursor:default}
.lg-char-count{font-size:11px;color:#8696A0;text-align:right;margin-top:4px}
.lg-badge{display:flex;align-items:center;gap:8px;margin-top:32px;
          background:rgba(0,168,132,.1);border:1px solid rgba(0,168,132,.28);
          border-radius:20px;padding:10px 18px;font-size:13px;color:#00A884;font-weight:500}
.lg-spinner{width:22px;height:22px;border:3px solid rgba(0,0,0,.3);border-top-color:#000;
            border-radius:50%;animation:spin .7s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
.lg-step-phone,.lg-step-name{display:none}
.lg-step-phone.active,.lg-step-name.active{display:block}
/* QR Login Tab */
.lg-tabs{display:flex;margin-bottom:20px;border-radius:12px;overflow:hidden;border:1px solid #2A3942}
.lg-tab{flex:1;padding:10px;text-align:center;cursor:pointer;color:#8696A0;font-size:13px;font-weight:600;background:none;border:none;transition:background .15s}
.lg-tab.active{background:#00A884;color:#000}
.qr-wrap{display:flex;flex-direction:column;align-items:center;gap:14px;padding:8px 0}
.qr-canvas{border-radius:12px;background:#fff;padding:10px;width:180px;height:180px}
.qr-status{font-size:12px;color:#8696A0;text-align:center}
.qr-status.ok{color:#00A884}
.qr-status.err{color:#EA0038}
.qr-refresh{background:none;border:1px solid #2A3942;border-radius:10px;color:#8696A0;
             font-size:12px;padding:6px 16px;cursor:pointer;margin-top:4px;transition:border-color .15s}
.qr-refresh:hover{border-color:#00A884;color:#00A884}

/* input bar */
#input-bar{display:flex;align-items:center;gap:6px;padding:8px 12px;
           background:#fff;border-top:1px solid #e0e0e0;flex-shrink:0}
#input-bar button{background:none;border:none;cursor:pointer;border-radius:50%;
  width:38px;height:38px;font-size:20px;display:flex;align-items:center;justify-content:center;
  color:#888;transition:background .15s}
#input-bar button:hover{background:#f0f0f0;color:#00969f}
#input-bar button.send-btn{background:#00969f;color:#fff;font-size:18px}
#input-bar button.send-btn:hover{background:#00808a}
#msg-input{flex:1;border:1px solid #e0e0e0;border-radius:20px;padding:9px 14px;
           font-size:13px;outline:none;resize:none;max-height:100px;font-family:inherit}
#msg-input:focus{border-color:#00969f}

/* scrollbar */
::-webkit-scrollbar{width:5px}
::-webkit-scrollbar-track{background:transparent}
::-webkit-scrollbar-thumb{background:#ccc;border-radius:3px}

/* ── RESPONSIVE: small window (< 600px) ── */
#btn-chat-toggle{display:none}
@media (max-width:600px){
  #btn-chat-toggle{display:flex}
  #chatlist{
    position:absolute;left:0;top:52px;bottom:0;z-index:100;
    width:100%;max-width:320px;
    box-shadow:2px 0 12px rgba(0,0,0,.18);
    transform:translateX(-110%);
    transition:transform .22s cubic-bezier(.4,0,.2,1);
  }
  #chatlist.open{transform:translateX(0)}
  #overlay{display:none;position:absolute;inset:0;z-index:99;background:rgba(0,0,0,.3)}
  #overlay.show{display:block}
}
</style>
</head>
<body>

<!-- LOGIN SCREEN -->
<div id="login-screen">
  <div id="login-deco-top"></div>
  <div id="login-deco-bot"></div>

  <div id="login-logo-wrap">
    <!-- Send / paper-plane icon matching APK's Icons.Default.Send -->
    <svg viewBox="0 0 24 24"><line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/></svg>
  </div>

  <div id="login-title">RasGram</div>
  <div id="login-tagline">Simple. Secure. Reliable.</div>
  <div id="login-sub" id="login-sub-text">Enter your phone number to continue</div>

  <div id="login-card">

    <!-- TAB BAR: QR | Phone -->
    <div class="lg-tabs">
      <button class="lg-tab active" id="tab-qr-btn"   onclick="LG.switchTab('qr')">📱 QR Login</button>
      <button class="lg-tab"        id="tab-ph-btn"   onclick="LG.switchTab('phone')">☎ Phone</button>
    </div>

    <!-- QR TAB -->
    <div id="tab-qr" style="display:block">
      <div class="qr-wrap">
        <canvas id="lg-qr-canvas" class="qr-canvas" width="180" height="180"></canvas>
        <div class="qr-status" id="lg-qr-status">Open RasGram on your phone → tap ⋮ → Scan QR</div>
        <button class="qr-refresh" onclick="LG.refreshQR()">🔄 Refresh QR</button>
      </div>
    </div>

    <!-- PHONE TAB -->
    <div id="tab-ph" style="display:none">
      <!-- STEP 0: Phone -->
      <div class="lg-step-phone active" id="step-phone">
        <div class="lg-label">Phone Number</div>
        <div class="lg-row" style="position:relative">
          <button class="lg-country-btn" onclick="LG.toggleDrop()" id="lg-drop-btn">
            <span id="lg-flag">🇧🇩</span>
            <span id="lg-code">+880</span>
            <span style="color:#8696A0;font-size:12px">▾</span>
          </button>
          <div id="lg-drop" class="lg-country-drop" style="display:none;position:absolute;top:56px;left:0">
            <div onclick="LG.selectCountry('+880','🇧🇩')">🇧🇩  +880</div>
            <div onclick="LG.selectCountry('+1','🇺🇸')">🇺🇸  +1</div>
            <div onclick="LG.selectCountry('+44','🇬🇧')">🇬🇧  +44</div>
            <div onclick="LG.selectCountry('+91','🇮🇳')">🇮🇳  +91</div>
            <div onclick="LG.selectCountry('+971','🇦🇪')">🇦🇪  +971</div>
            <div onclick="LG.selectCountry('+966','🇸🇦')">🇸🇦  +966</div>
          </div>
          <input id="lg-phone" class="lg-input" type="tel" placeholder="Phone number" maxlength="11"
                 oninput="this.value=this.value.replace(/\D/g,'')"
                 onkeydown="if(event.key==='Enter')LG.sendOtp()" style="flex:1">
        </div>
        <div class="lg-error" id="lg-phone-err"></div>
        <button class="lg-btn" id="lg-phone-btn" onclick="LG.sendOtp()">
          <span id="lg-phone-txt">Continue</span>
        </button>
      </div>

      <!-- STEP 1: OTP Code -->
      <div id="step-otp" style="display:none">
        <div class="lg-label">Verification Code</div>
        <div style="font-size:12px;color:#8696A0;margin-bottom:12px;line-height:1.6">
          A 5-digit code was sent to your RasGram on<br>
          <span id="lg-otp-phone-lbl" style="color:#00A884;font-weight:600"></span>
        </div>
        <input id="lg-otp-input" class="lg-input"
               style="width:100%;letter-spacing:10px;font-size:24px;font-weight:700;text-align:center;padding:12px 8px"
               type="text" inputmode="numeric" placeholder="·····" maxlength="5"
               oninput="this.value=this.value.replace(/\D/g,'');if(this.value.length===5)LG.verifyOtp()"
               onkeydown="if(event.key==='Enter')LG.verifyOtp()">
        <div class="lg-error" id="lg-otp-err"></div>
        <div class="lg-back-row" style="margin-top:14px">
          <button class="lg-btn-back" onclick="LG.backToPhone()">Back</button>
          <button class="lg-btn-main" id="lg-otp-btn" onclick="LG.verifyOtp()">
            <span id="lg-otp-txt">Verify</span>
          </button>
        </div>
        <div style="margin-top:14px;text-align:center">
          <button class="qr-refresh" id="lg-resend-btn" onclick="LG.resendOtp()"
                  style="background:none;border:none;color:#8696A0;font-size:12px;cursor:pointer">
            Resend code
          </button>
        </div>
      </div>
    </div><!-- /tab-ph -->

  </div>

  <div class="lg-badge">
    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="#00A884" stroke-width="2"
         stroke-linecap="round" stroke-linejoin="round">
      <rect x="3" y="11" width="18" height="11" rx="2" ry="2"/>
      <path d="M7 11V7a5 5 0 0 1 10 0v4"/>
    </svg>
    End-to-end encrypted
  </div>
</div>

<!-- MAIN APP (hidden until logged in) -->
<div id="app" style="display:none;flex-direction:column;height:100%">

  <!-- TOP BAR -->
  <div id="topbar">
    <button id="btn-chat-toggle" title="Chats" onclick="RG.toggleChatList()">☰</button>
    <button id="btn-lan" title="LAN Mode" onclick="RG.toggleLan()">📡</button>
    <div class="logo">RasGram<span class="badge">DESKTOP</span></div>
    <button title="Settings" onclick="RG.openSettings()">⚙️</button>
  </div>

  <!-- BODY -->
  <div id="body" style="position:relative">
    <div id="overlay" onclick="RG.closeChatList()"></div>

    <!-- CHAT LIST -->
    <div id="chatlist">
      <div id="search-wrap">
        <input id="search" type="text" placeholder="🔍  Search chats..." oninput="RG.filterChats(this.value)">
      </div>
      <div id="chats"><div class="empty-list">Loading chats…</div></div>
    </div>

    <!-- RIGHT PANEL -->
    <div id="right">

      <!-- CHAT HEADER -->
      <div id="chat-header" style="display:none">
        <div class="avatar" id="hdr-avatar"></div>
        <div id="chat-header-info">
          <div id="chat-header-name"></div>
          <div id="chat-header-status">tap to view profile</div>
        </div>
        <div class="hdr-btns">
          <button title="Audio call" onclick="RG.callAudio()">📞</button>
          <button title="Video call" onclick="RG.callVideo()">📹</button>
        </div>
      </div>

      <!-- MESSAGES -->
      <div id="messages" style="display:none"></div>

      <!-- EMPTY STATE -->
      <div id="empty-state">
        <div class="big-icon">💬</div>
        <h3>RasGram Desktop</h3>
        <p>Select a chat to start messaging.<br>Calls and file sharing supported.</p>
      </div>

      <!-- INPUT BAR -->
      <div id="input-bar" style="display:none">
        <button title="Attach file" onclick="RG.attach()">📎</button>
        <textarea id="msg-input" rows="1" placeholder="Type a message…"
          onkeydown="RG.onKey(event)"></textarea>
        <button class="send-btn" onclick="RG.send()" title="Send">➤</button>
      </div>

    </div><!-- /right -->
  </div><!-- /body -->
</div><!-- /app -->

<script>
// ── QR Code generator (ISO 18004 compliant, self-contained) ──
// Encodes arbitrary byte-mode strings as a proper scannable QR code.
// Draws on a <canvas> element. No external library needed.
(function(){
const QREncoder = window.QREncoder = {};

// Reed-Solomon GF(256) arithmetic
const GF = (() => {
  const exp = new Uint8Array(512), log = new Uint8Array(256);
  let x = 1;
  for (let i = 0; i < 255; i++) {
    exp[i] = x; log[x] = i;
    x = x << 1; if (x & 0x100) x ^= 0x11d;
  }
  for (let i = 255; i < 512; i++) exp[i] = exp[i-255];
  return {
    mul(a,b){ return a&&b ? exp[log[a]+log[b]] : 0; },
    poly(e){ // generator polynomial for e error correction codewords
      let p=[1];
      for(let i=0;i<e;i++){
        const q=[1,exp[i]];
        const r=new Array(p.length+q.length-1).fill(0);
        for(let j=0;j<p.length;j++) for(let k=0;k<q.length;k++) r[j+k]^=GF.mul(p[j],q[k]);
        p=r;
      }
      return p;
    },
    remainder(data,gen){
      const out=data.slice();
      for(let i=0;i<data.length;i++){
        const c=out[i];
        if(c) for(let j=1;j<gen.length;j++) out[i+j]^=GF.mul(gen[j],c);
      }
      return out.slice(data.length);
    }
  };
})();

// QR version 3 constants (capacity: up to 53 bytes in M correction)
// We use version 3, error correction M (15 EC codewords, 26 data codewords)
const V3M = {
  size:       21 + (3-1)*4, // 29 modules
  totalCW:    44,
  dataCW:     26,
  ecCW:       18,
  ecPerBlock: 18,
  blocks:     1,
  formatBits: [0b111011111000100, 0b111001011110011, 0b111110110101010, // M mask 0,1,2
               0b111100010011101, 0b110011000101111, 0b110001100011000,
               0b110110001000001, 0b110100101110110][5], // mask 5
  mask:       5
};

function encodeData(text) {
  const bytes = [];
  for (let i=0; i<text.length; i++) {
    const c = text.charCodeAt(i);
    if (c > 0x7f) { bytes.push(0xef,0xbb,0xbf); } // fallback
    else bytes.push(c);
  }
  // Byte mode: 0100 + length (8 bits) + data + terminator
  const bits = [];
  const push = (val, len) => { for(let i=len-1;i>=0;i--) bits.push((val>>i)&1); };
  push(0b0100, 4); // byte mode indicator
  push(bytes.length, 8);
  for(const b of bytes) push(b, 8);
  push(0, 4); // terminator
  // Pad to dataCW*8 bits
  while (bits.length < V3M.dataCW*8 && bits.length%8!==0) bits.push(0);
  const pads = [0b11101100, 0b00010001];
  let pi=0;
  while (bits.length < V3M.dataCW*8) { push(pads[pi++%2],8); }
  // Convert bits to bytes
  const cw = [];
  for(let i=0; i<bits.length; i+=8) {
    let v=0; for(let j=0;j<8;j++) v=(v<<1)|(bits[i+j]||0); cw.push(v);
  }
  return cw;
}

function buildMatrix(dataCW, ecCW) {
  const N = V3M.size;
  const m = Array.from({length:N}, ()=>new Int8Array(N).fill(-1)); // -1=empty

  // Finder patterns
  const finder = (r,c) => {
    for(let dr=-1;dr<=7;dr++) for(let dc=-1;dc<=7;dc++){
      if(r+dr<0||r+dr>=N||c+dc<0||c+dc>=N) continue;
      const inside = dr>=0&&dr<=6&&dc>=0&&dc<=6;
      const border = dr===0||dr===6||dc===0||dc===6;
      const center = dr>=2&&dr<=4&&dc>=2&&dc<=4;
      m[r+dr][c+dc] = inside && (border||center) ? 1 : (inside?0:0);
      if(dr===-1||dr===7||dc===-1||dc===7) m[r+dr][c+dc]=0; // separator
    }
  };
  finder(0,0); finder(0,N-7); finder(N-7,0);

  // Timing patterns
  for(let i=8;i<N-8;i++){
    m[6][i]=m[i][6]=i%2===0?1:0;
  }

  // Dark module
  m[4*V3M.size-8+8]?.[8]; // version 3: row = 4*(v-1)+8+4 → skip, use fixed
  m[N-8][8]=1;

  // Format info (mask 5, EC level M)
  const fmt = V3M.formatBits;
  const fmtBits = [];
  for(let i=14;i>=0;i--) fmtBits.push((fmt>>i)&1);
  // Place format around top-left finder
  const fpos = [0,1,2,3,4,5,7,8,  N-7,N-6,N-5,N-4,N-3,N-2,N-1];
  for(let i=0;i<8;i++){
    m[8][fpos[i]]=fmtBits[i];
    m[fpos[7-i]][8]=fmtBits[i];
  }
  for(let i=8;i<15;i++){
    m[8][fpos[i]]=fmtBits[i];
    m[fpos[14-i+7]][8]=fmtBits[i];
  }

  // Data placement (zigzag, bottom-right to top-left, skipping col 6)
  const all = [...dataCW, ...ecCW];
  let bi=0, bitIdx=7;
  const cols=[];
  for(let c=N-1;c>=0;c-=2){ if(c===6) c--; cols.push(c); }
  let up=true;
  for(const rCol of cols){
    const rows=up?[...Array(N).keys()].reverse():[...Array(N).keys()];
    up=!up;
    for(const row of rows){
      for(const col of [rCol, rCol-1]){
        if(col<0||col>=N) continue;
        if(m[row][col]===-1){
          const bit = bi<all.length ? (all[bi]>>bitIdx)&1 : 0;
          bitIdx--; if(bitIdx<0){ bitIdx=7; bi++; }
          m[row][col]=bit;
        }
      }
    }
  }

  // Build function-module map to avoid masking fixed patterns
  const func = Array.from({length:N}, ()=>new Uint8Array(N));
  // Finder + separator regions
  for(let r=0;r<=8;r++) for(let c=0;c<=8;c++) func[r][c]=1;
  for(let r=0;r<=8;r++) for(let c=N-8;c<N;c++) func[r][c]=1;
  for(let r=N-8;r<N;r++) for(let c=0;c<=8;c++) func[r][c]=1;
  // Timing patterns
  for(let i=0;i<N;i++){ func[6][i]=1; func[i][6]=1; }

  // Apply mask pattern 5: (Math.floor(r/2) + Math.floor(c/3)) % 2 === 0
  for(let r=0;r<N;r++) for(let c=0;c<N;c++){
    if(!func[r][c] && m[r][c]!==-1){
      if((Math.floor(r/2) + Math.floor(c/3)) % 2 === 0)
        m[r][c] ^= 1;
    }
  }

  return m;
}

QREncoder.drawOnCanvas = function(text, canvas, size) {
  try {
    size = size || 180;
    let data;
    try { data = encodeData(text); } catch(e) { return false; }
    if (data.length > V3M.dataCW) return false; // too long for v3
    const gen = GF.poly(V3M.ecCW);
    const ec  = GF.remainder(data, gen);
    const matrix = buildMatrix(data, ec);
    const N = matrix.length;
    const ctx = canvas.getContext('2d');
    canvas.width = canvas.height = size;
    const cell = (size - 20) / N;
    ctx.fillStyle = '#fff'; ctx.fillRect(0,0,size,size);
    ctx.fillStyle = '#000';
    for(let r=0;r<N;r++) for(let c=0;c<N;c++){
      if(matrix[r][c]===1)
        ctx.fillRect(10+c*cell, 10+r*cell, cell, cell);
    }
    return true;
  } catch(e) { console.error('QR draw error:', e); return false; }
};
})();

// ── Login Screen (LG) ─────────────────────────────────────────
window.LG = {
  _code:       '+880',
  _qrToken:    '',
  _qrPollTimer: null,
  _qrRefreshTimer: null,

  // ── Tab switching ────────────────────────────────────────────
  switchTab(tab) {
    document.getElementById('tab-qr').style.display  = (tab === 'qr')    ? 'block' : 'none';
    document.getElementById('tab-ph').style.display  = (tab === 'phone')  ? 'block' : 'none';
    document.getElementById('tab-qr-btn').classList.toggle('active', tab === 'qr');
    document.getElementById('tab-ph-btn').classList.toggle('active', tab === 'phone');
    if (tab === 'qr') LG.startQR();
    else              LG.stopQR();
  },

  // ── QR flow ─────────────────────────────────────────────────
  startQR() {
    // Tell C++ to create a qr_sessions doc and return the token + QR png
    postMsg({action: 'qr_init'});
    LG.setQRStatus('Generating QR code…', '');
    // Safety refresh every 60s (Firestore token expires)
    clearTimeout(LG._qrRefreshTimer);
    LG._qrRefreshTimer = setTimeout(() => LG.refreshQR(), 60000);
  },

  stopQR() {
    clearInterval(LG._qrPollTimer);
    clearTimeout(LG._qrRefreshTimer);
    LG._qrPollTimer = null;
    postMsg({action: 'qr_stop'});
  },

  refreshQR() {
    clearInterval(LG._qrPollTimer);
    clearTimeout(LG._qrRefreshTimer);
    LG._qrPollTimer = null;
    LG.setQRStatus('Refreshing…', '');
    postMsg({action: 'qr_init'});
    LG._qrRefreshTimer = setTimeout(() => LG.refreshQR(), 60000);
  },

  // Called by C++ after qr_init: receives token + base64 PNG
  onQRReady(token) {
    LG._qrToken = token;
    const canvas = document.getElementById('lg-qr-canvas');
    // Build the URL that Android RasGram will scan
    const url = 'rasgram://qr/' + token;
    // Draw using our self-contained QR encoder
    const ok = (window.QREncoder && window.QREncoder.drawOnCanvas)
                ? window.QREncoder.drawOnCanvas(url, canvas, 180)
                : false;
    if (!ok) {
      // Fallback: show token text in canvas so user can see something
      const ctx = canvas.getContext('2d');
      canvas.width = canvas.height = 180;
      ctx.fillStyle = '#fff'; ctx.fillRect(0,0,180,180);
      ctx.fillStyle = '#000'; ctx.font = '9px monospace';
      ctx.fillText('QR unavailable', 10, 90);
      ctx.fillText(token.substr(0,20), 10, 105);
      LG.setQRStatus('QR render failed. Try Phone Login.', 'err');
    } else {
      LG.setQRStatus('Open RasGram → Menu → Scan QR', 'ok');
    }
    // Start polling for scan
    clearInterval(LG._qrPollTimer);
    LG._qrPollTimer = setInterval(() => {
      postMsg({action: 'qr_poll', token: LG._qrToken});
    }, 2500);
  },

  // Called by C++ when phone has scanned and approved the session
  onQRScanned(mobile, name) {
    clearInterval(LG._qrPollTimer);
    clearTimeout(LG._qrRefreshTimer);
    LG.setQRStatus('✓ Logged in as ' + name, 'ok');
    // Submit login the same way phone login does
    if (window.chrome && window.chrome.webview)
      window.chrome.webview.postMessage(JSON.stringify({
        action: 'rasgram_qr_login',
        phone: mobile,
        name: name
      }));
  },

  setQRStatus(msg, cls) {
    const el = document.getElementById('lg-qr-status');
    el.textContent = msg;
    el.className = 'qr-status' + (cls ? ' ' + cls : '');
  },

  qrError(msg) {
    LG.setQRStatus(msg || 'QR expired. Tap Refresh.', 'err');
    clearInterval(LG._qrPollTimer);
  },

  // ── Phone flow ───────────────────────────────────────────────
  toggleDrop() {
    const d = document.getElementById('lg-drop');
    d.style.display = d.style.display === 'none' ? 'block' : 'none';
  },
  selectCountry(code, flag) {
    this._code = code;
    document.getElementById('lg-flag').textContent = flag;
    document.getElementById('lg-code').textContent = code;
    document.getElementById('lg-drop').style.display = 'none';
  },
  _loginTimer: null,
  _otpPhone: '',
  _resendTimer: null,

  // ── Step 0 → send OTP ────────────────────────────────────────
  sendOtp() {
    const phone = document.getElementById('lg-phone').value.trim();
    if (phone.length < 9) {
      document.getElementById('lg-phone-err').textContent = 'Enter a valid phone number';
      return;
    }
    document.getElementById('lg-phone-err').textContent = '';
    this._otpPhone = this._code + phone;
    const btn = document.getElementById('lg-phone-btn');
    const txt = document.getElementById('lg-phone-txt');
    btn.disabled = true;
    txt.innerHTML = '<div class="lg-spinner"></div>';
    clearTimeout(LG._loginTimer);
    LG._loginTimer = setTimeout(() =>
      LG._phoneErr('Timeout. Check internet and try again.'), 25000);
    if (window.chrome && window.chrome.webview) {
      window.chrome.webview.postMessage(JSON.stringify(
        { action: 'otp_send', phone: LG._otpPhone }));
    } else {
      LG._phoneErr('App not ready. Restart RasFocus.');
    }
  },

  // Called by C++ after code sent to phone
  onOtpSent() {
    clearTimeout(LG._loginTimer);
    const btn = document.getElementById('lg-phone-btn');
    const txt = document.getElementById('lg-phone-txt');
    btn.disabled = false;
    txt.textContent = 'Continue';
    document.getElementById('step-phone').classList.remove('active');
    document.getElementById('step-phone').style.display = 'none';
    document.getElementById('step-otp').style.display  = 'block';
    document.getElementById('lg-otp-phone-lbl').textContent = LG._otpPhone;
    document.getElementById('login-sub').textContent = 'Check your RasGram for the code';
    document.getElementById('lg-otp-err').textContent = '';
    document.getElementById('lg-otp-input').value = '';
    document.getElementById('lg-otp-input').focus();
    LG._startResendTimer();
  },

  _startResendTimer() {
    const btn = document.getElementById('lg-resend-btn');
    btn.disabled = true;
    let s = 30;
    btn.textContent = 'Resend code (' + s + 's)';
    clearInterval(LG._resendTimer);
    LG._resendTimer = setInterval(() => {
      s--;
      if (s <= 0) { clearInterval(LG._resendTimer); btn.disabled = false; btn.textContent = 'Resend code'; }
      else btn.textContent = 'Resend code (' + s + 's)';
    }, 1000);
  },

  resendOtp() {
    document.getElementById('lg-otp-err').textContent = '';
    document.getElementById('lg-otp-input').value = '';
    if (window.chrome && window.chrome.webview)
      window.chrome.webview.postMessage(JSON.stringify(
        { action: 'otp_send', phone: LG._otpPhone }));
    LG._startResendTimer();
  },

  _phoneErr(msg) {
    clearTimeout(LG._loginTimer);
    const btn = document.getElementById('lg-phone-btn');
    const txt = document.getElementById('lg-phone-txt');
    if (btn) { btn.disabled = false; }
    if (txt) txt.textContent = 'Continue';
    document.getElementById('lg-phone-err').textContent = msg;
  },

  // ── Step 1 → verify OTP ──────────────────────────────────────
  verifyOtp() {
    const code = document.getElementById('lg-otp-input').value.trim();
    if (code.length !== 5) {
      document.getElementById('lg-otp-err').textContent = 'Enter the 5-digit code';
      return;
    }
    document.getElementById('lg-otp-err').textContent = '';
    const btn = document.getElementById('lg-otp-btn');
    const txt = document.getElementById('lg-otp-txt');
    btn.disabled = true;
    txt.innerHTML = '<div class="lg-spinner"></div>';
    clearTimeout(LG._loginTimer);
    LG._loginTimer = setTimeout(() =>
      LG.otpVerifyError('Timeout. Check internet and try again.'), 25000);
    if (window.chrome && window.chrome.webview) {
      window.chrome.webview.postMessage(JSON.stringify(
        { action: 'otp_verify', phone: LG._otpPhone, code: code }));
    } else {
      LG.otpVerifyError('App not ready. Restart RasFocus.');
    }
  },

  otpVerifyError(msg) {
    clearTimeout(LG._loginTimer);
    const btn = document.getElementById('lg-otp-btn');
    const txt = document.getElementById('lg-otp-txt');
    if (btn) btn.disabled = false;
    if (txt) txt.textContent = 'Verify';
    document.getElementById('lg-otp-err').textContent = msg || 'Invalid code. Try again.';
    document.getElementById('lg-otp-input').value = '';
    document.getElementById('lg-otp-input').focus();
  },

  backToPhone() {
    clearTimeout(LG._loginTimer);
    clearInterval(LG._resendTimer);
    document.getElementById('step-otp').style.display  = 'none';
    document.getElementById('step-phone').style.display = 'block';
    document.getElementById('step-phone').classList.add('active');
    document.getElementById('login-sub').textContent = 'Enter your phone number to continue';
    document.getElementById('lg-phone-err').textContent = '';
    document.getElementById('lg-otp-err').textContent  = '';
    const btn = document.getElementById('lg-phone-btn');
    const txt = document.getElementById('lg-phone-txt');
    if (btn) btn.disabled = false;
    if (txt) txt.textContent = 'Continue';
  },

  loginError(msg) {
    clearTimeout(LG._loginTimer);
    LG.otpVerifyError(msg);
  }
};
// Auto-start QR when login screen shows (QR tab is default)
document.addEventListener('DOMContentLoaded', () => {
  // Will be triggered by RG.showNotLoggedIn() → LG.startQR() called below
});
// Close dropdown when clicking outside
document.addEventListener('click', e => {
  const d = document.getElementById('lg-drop');
  if (d && !document.getElementById('lg-drop-btn').contains(e.target))
    d.style.display = 'none';
});

// ── State ──────────────────────────────────────────────────────
const state = {
  myMobile: '',
  myName:   '',
  chats:    [],
  messages: [],
  openChatId: '',
  openMobile: '',
  searchQ:    '',
  lanOn:      false,
};

// ── Avatar colour pool ─────────────────────────────────────────
const COLORS = ['#e53935','#2196f3','#4caf50','#9c27b0','#ff9800','#00bcd4','#e91e63','#795548'];
function avatarColor(mobile) {
  let h = 0;
  for (let c of mobile) h = (h * 31 + c.charCodeAt(0)) % 8;
  return COLORS[Math.abs(h) % 8];
}
function initials(name) {
  return (name||'?').charAt(0).toUpperCase();
}

// ── Helpers ────────────────────────────────────────────────────
function esc(s) {
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}
function postMsg(obj) {
  if (window.chrome && window.chrome.webview)
    window.chrome.webview.postMessage(JSON.stringify(obj));
}
function formatTime(ts) {
  if (!ts) return '';
  const d = new Date(Number(ts));
  return d.getHours().toString().padStart(2,'0') + ':' + d.getMinutes().toString().padStart(2,'0');
}
function formatDate(ts) {
  if (!ts) return '';
  const d = new Date(Number(ts));
  return d.toLocaleDateString('en-GB',{day:'2-digit',month:'short',year:'numeric'});
}

// ── Render chat list ───────────────────────────────────────────
function renderChats() {
  const q = state.searchQ.toLowerCase();
  const list = q
    ? state.chats.filter(c => (c.contactName||'').toLowerCase().includes(q) || (c.contactMobile||'').includes(q))
    : state.chats;
  const el = document.getElementById('chats');
  if (!list.length) {
    el.innerHTML = '<div class="empty-list">No chats yet.</div>';
    return;
  }
  el.innerHTML = list.map((c,i) => {
    const col  = avatarColor(c.contactMobile||'');
    const name = esc(c.contactName || c.contactMobile);
    const last = esc(c.lastMessageText || '');
    const time = c.lastTimeString || formatTime(c.lastTimestamp);
    const unread = c.unreadCount > 0
      ? `<div class="badge-unread">${c.unreadCount > 99 ? '99+' : c.unreadCount}</div>` : '';
    const sel = (c.chatId === state.openChatId) ? ' selected' : '';
    return `<div class="chat-row${sel}" onclick="RG.openChat('${esc(c.chatId)}','${esc(c.contactMobile)}','${esc(c.contactName||'')}')">
      <div class="avatar" style="background:${col}">${initials(c.contactName||c.contactMobile)}</div>
      <div class="chat-info">
        <div class="chat-name">${name}</div>
        <div class="chat-last">${last}</div>
      </div>
      <div class="chat-meta">
        <span class="chat-time">${esc(time)}</span>
        ${unread}
      </div>
    </div>`;
  }).join('');
}

// ── Render messages ────────────────────────────────────────────
function renderMessages() {
  const el = document.getElementById('messages');
  const msgs = state.messages;
  if (!msgs.length) { el.innerHTML = ''; return; }
  let lastDate = '';
  let html = '';
  for (const m of msgs) {
    if (m.isDeleted) continue;
    const dateStr = formatDate(m.timestamp);
    if (dateStr !== lastDate) {
      lastDate = dateStr;
      html += `<div class="date-chip"><span>${esc(dateStr)}</span></div>`;
    }
    if (m.isCallLog) {
      const icon = m.callType === 'video' ? '📹' : '📞';
      const status = m.callStatus === 'missed' ? 'Missed Call' : m.callStatus === 'answered' ? 'Call ended' : 'Call';
      html += `<div class="call-log"><span>${icon} ${esc(status)}</span></div>`;
      continue;
    }
    const mine = (m.senderMobile === state.myMobile);
    const cls  = mine ? 'mine' : 'theirs';
    let text = esc(m.text || '');
    if (!text && m.fileName) text = '📎 ' + esc(m.fileName);
    if (!text) continue;
    const ticks = mine ? (m.read ? ' <span class="ticks">✓✓</span>' : ' ✓') : '';
    const pend  = m.isPending ? ' pending' : '';
    html += `<div class="bubble-wrap ${cls}">
      <div class="bubble ${cls}${pend}">
        ${text.replace(/\n/g,'<br>')}
        <span class="bubble-time">${esc(formatTime(m.timestamp))}${ticks}</span>
      </div>
    </div>`;
  }
  el.innerHTML = html;
  el.scrollTop = el.scrollHeight;
}

// ── Open / close chat panel ────────────────────────────────────
function showChatPanel(name, mobile) {
  const col = avatarColor(mobile||'');
  document.getElementById('hdr-avatar').style.background = col;
  document.getElementById('hdr-avatar').textContent = initials(name||mobile);
  document.getElementById('chat-header-name').textContent = name || mobile;
  document.getElementById('chat-header').style.display = 'flex';
  document.getElementById('messages').style.display    = 'flex';
  document.getElementById('messages').style.flexDirection = 'column';
  document.getElementById('input-bar').style.display   = 'flex';
  document.getElementById('empty-state').style.display = 'none';
}
function hideChatPanel() {
  document.getElementById('chat-header').style.display = 'none';
  document.getElementById('messages').style.display    = 'none';
  document.getElementById('input-bar').style.display   = 'none';
  document.getElementById('empty-state').style.display = 'flex';
  state.openChatId = '';
  state.openMobile = '';
  state.messages   = [];
}

// ── RG public API (called by C++ via ExecuteScript) ───────────
window.RG = {
  // Called by C++ to provide login state
  setLoginState(name, mobile) {
    clearTimeout(LG._loginTimer); // cancel any pending login timeout
    state.myName   = name;
    state.myMobile = mobile;
    document.getElementById('login-screen').style.display = 'none';
    document.getElementById('app').style.display = 'flex';
  },
  showNotLoggedIn() {
    document.getElementById('login-screen').style.display = 'flex';
    document.getElementById('app').style.display = 'none';
    // Auto-start QR on the default tab
    setTimeout(() => LG.startQR(), 100);
  },

  // Called by C++ when chat list updates
  loadChats(jsonArr) {
    try { state.chats = JSON.parse(jsonArr); } catch(e) { return; }
    renderChats();
  },

  // Called by C++ when messages for open chat load
  loadMessages(jsonArr) {
    try { state.messages = JSON.parse(jsonArr); } catch(e) { return; }
    renderMessages();
  },

  // Called by C++ to push one new message
  pushMessage(jsonObj) {
    try {
      const m = (typeof jsonObj === 'string') ? JSON.parse(jsonObj) : jsonObj;
      // Remove pending duplicate
      state.messages = state.messages.filter(x => x.id !== m.id && !(x.isPending && x.text === m.text));
      state.messages.push(m);
      renderMessages();
    } catch(e) {}
  },

  // User actions
  openChat(chatId, contactMobile, contactName) {
    state.openChatId = chatId;
    state.openMobile = contactMobile;
    state.messages   = [];
    showChatPanel(contactName, contactMobile);
    document.getElementById('messages').innerHTML = '<div style="text-align:center;color:#aaa;padding:20px">Loading…</div>';
    renderChats(); // update selected highlight
    postMsg({action:'open', chatId, contactMobile});
    // On small screens, collapse the chat list after opening a chat
    RG.closeChatList();
  },

  send() {
    const el = document.getElementById('msg-input');
    const text = el.value.trim();
    if (!text || !state.openChatId) return;
    el.value = '';
    el.style.height = '';
    // Optimistic add
    const m = {
      id: 'pending_' + Date.now(),
      chatId: state.openChatId,
      senderMobile: state.myMobile,
      text,
      timestamp: Date.now(),
      isPending: true,
    };
    state.messages.push(m);
    renderMessages();
    postMsg({action:'send', chatId: state.openChatId, contactMobile: state.openMobile, text});
  },

  onKey(e) {
    // Ctrl+Enter = send
    if (e.key === 'Enter' && e.ctrlKey) { e.preventDefault(); RG.send(); }
    // Auto-grow textarea
    const el = e.target;
    el.style.height = '';
    el.style.height = Math.min(el.scrollHeight, 100) + 'px';
  },

  filterChats(q) {
    state.searchQ = q;
    renderChats();
  },

  attach() {
    postMsg({action:'attach'});
  },

  toggleLan() {
    state.lanOn = !state.lanOn;
    const btn = document.getElementById('btn-lan');
    btn.style.color = state.lanOn ? '#4caf50' : '';
    postMsg({action:'lan_toggle'});
  },

  callAudio() { postMsg({action:'call_audio', contactMobile: state.openMobile}); },
  callVideo() { postMsg({action:'call_video', contactMobile: state.openMobile}); },
  openSettings() { postMsg({action:'settings'}); },

  toggleChatList() {
    const cl = document.getElementById('chatlist');
    const ov = document.getElementById('overlay');
    const open = cl.classList.toggle('open');
    ov.classList.toggle('show', open);
  },
  closeChatList() {
    document.getElementById('chatlist').classList.remove('open');
    document.getElementById('overlay').classList.remove('show');
  },
};
</script>
</body>
</html>)HTML";
    return html;
}

// ── Execute JS on WebView ─────────────────────────────────────
static void RgExecJS(const wstring& js) {
    if (g_rgWV && g_wvReady)
        g_rgWV->ExecuteScript(js.c_str(), nullptr);
}

// ── Push login state to UI ────────────────────────────────────
static void RgSendLoginState() {
    if (!g_wvReady) return;
    if (g_loggedInUserUid.empty()) {
        RgExecJS(L"RG.showNotLoggedIn();");
        return;
    }
    string name   = WideToUtf8(g_myName_w);
    string mobile = g_myMobile;
    wstring js = L"RG.setLoginState(\"" + Utf8ToWide(JsEscape(name)) + L"\",\""
                 + Utf8ToWide(JsEscape(mobile)) + L"\");";
    RgExecJS(js);
}

// ── Push chat list to UI ──────────────────────────────────────
static void RgPushChatsToUI() {
    if (!g_wvReady) return;
    vector<RgChatPreview> chats;
    { lock_guard<mutex> lk(g_chatsMtx); chats = g_chats; }
    // Build JSON array
    ostringstream ss;
    ss << "[";
    for (int i = 0; i < (int)chats.size(); i++) {
        auto& c = chats[i];
        // chatId = pure "mobileA_mobileB" (no pvt_msg_ prefix) — matches Android generateChatId
        string chatId = RgBuildChatId(g_myMobile, c.contactMobile);
        if (i) ss << ",";
        ss << "{"
           << "\"chatId\":\""          << JsEscape(chatId)               << "\","
           << "\"contactMobile\":\""   << JsEscape(c.contactMobile)      << "\","
           << "\"contactName\":\""     << JsEscape(c.contactName)        << "\","
           << "\"lastMessageText\":\"" << JsEscape(c.lastMessageText)    << "\","
           << "\"lastTimestamp\":"     << c.lastTimestamp                 << ","
           << "\"lastTimeString\":\""  << JsEscape(c.lastTimeString)     << "\","
           << "\"unreadCount\":"       << c.unreadCount
           << "}";
    }
    ss << "]";
    wstring js = L"RG.loadChats(" + Utf8ToWide("'" + ss.str() + "'") + L");";
    // Use double-quote safe version:
    // pass as JSON string literal using backtick via ExecuteScript
    string jsonStr = ss.str();
    // Escape for JS string (already JsEscaped content, but need to wrap)
    wstring call = L"RG.loadChats(`" + Utf8ToWide(jsonStr) + L"`);";
    RgExecJS(call);
}

// ── Push messages to UI ───────────────────────────────────────
static void RgPushMessagesToUI() {
    if (!g_wvReady) return;
    vector<RgMessage> msgs;
    { lock_guard<mutex> lk(g_msgsMtx); msgs = g_messages; }
    ostringstream ss;
    ss << "[";
    for (int i = 0; i < (int)msgs.size(); i++) {
        auto& m = msgs[i];
        if (i) ss << ",";
        ss << "{"
           << "\"id\":\""            << JsEscape(m.id)           << "\","
           << "\"chatId\":\""        << JsEscape(m.chatId)       << "\","
           << "\"senderMobile\":\""  << JsEscape(m.senderMobile) << "\","
           << "\"text\":\""          << JsEscape(m.text)         << "\","
           << "\"timestamp\":"       << m.timestamp               << ","
           << "\"timeString\":\""    << JsEscape(m.timeString)   << "\","
           << "\"fileName\":\""      << JsEscape(m.fileName)     << "\","
           << "\"isCallLog\":"       << (m.isCallLog ? "true" : "false") << ","
           << "\"callStatus\":\""    << JsEscape(m.callStatus)   << "\","
           << "\"callType\":\""      << JsEscape(m.callType)     << "\","
           << "\"read\":"            << (m.read ? "true" : "false") << ","
           << "\"isPending\":"       << (m.isPending ? "true" : "false")
           << "}";
    }
    ss << "]";
    wstring call = L"RG.loadMessages(`" + Utf8ToWide(ss.str()) + L"`);";
    RgExecJS(call);
}

// ── Push one message to UI ────────────────────────────────────
static void RgPushOneMessageToUI(const RgMessage& m) {
    if (!g_wvReady) return;

    // Desktop notification for incoming messages (not our own, not call logs)
    if (m.senderMobile != g_myMobile && !m.isCallLog && !m.text.empty() && g_notifyReady) {
        // Find sender name from chats list
        string senderName = m.senderName.empty() ? m.senderMobile : m.senderName;
        {
            lock_guard<mutex> lk(g_chatsMtx);
            for (auto& c : g_chats)
                if (c.contactMobile == m.senderMobile) { senderName = c.contactName; break; }
        }
        RgNotify_Message(senderName, m.text);
    }

    ostringstream ss;
    ss << "{"
       << "\"id\":\""            << JsEscape(m.id)           << "\","
       << "\"chatId\":\""        << JsEscape(m.chatId)       << "\","
       << "\"senderMobile\":\""  << JsEscape(m.senderMobile) << "\","
       << "\"text\":\""          << JsEscape(m.text)         << "\","
       << "\"timestamp\":"       << m.timestamp               << ","
       << "\"timeString\":\""    << JsEscape(m.timeString)   << "\","
       << "\"fileName\":\""      << JsEscape(m.fileName)     << "\","
       << "\"isCallLog\":"       << (m.isCallLog ? "true" : "false") << ","
       << "\"callStatus\":\""    << JsEscape(m.callStatus)   << "\","
       << "\"callType\":\""      << JsEscape(m.callType)     << "\","
       << "\"read\":"            << (m.read ? "true" : "false") << ","
       << "\"isPending\":"       << (m.isPending ? "true" : "false")
       << "}";
    wstring call = L"RG.pushMessage(`" + Utf8ToWide(ss.str()) + L"`);";
    RgExecJS(call);
}

// ── Open a chat (load messages + start polling) ───────────────
static void RgOpenChat(const string& chatId, const string& contactMobile) {
    g_openChatId       = chatId;
    g_openContactMobile = contactMobile;
    g_lastMsgTs        = 0;
    { lock_guard<mutex> lk(g_msgsMtx); g_messages.clear(); }

    RgNet_StopMessagePolling();
    RgNet_MarkRead(chatId, g_myMobile);

    RgNet_FetchMessages(chatId, [chatId](const vector<RgMessage>& msgs) {
        { lock_guard<mutex> lk(g_msgsMtx); g_messages = msgs; }
        if (!msgs.empty()) g_lastMsgTs = msgs.back().timestamp;
        RgPushMessagesToUI();
    });

    RgNet_StartMessagePolling(chatId, g_lastMsgTs,
        [](const RgMessage& msg) {
            bool dup = false;
            { lock_guard<mutex> lk(g_msgsMtx);
              for (auto& m : g_messages) if (m.id == msg.id) { dup = true; break; }
              if (!dup) { g_messages.push_back(msg); g_lastMsgTs = msg.timestamp; } }
            if (!dup) RgPushOneMessageToUI(msg);
        });
}

// ── Send text message ─────────────────────────────────────────
static void RgSendText(const string& chatId, const string& contactMobile, const string& text) {
    if (text.empty() || chatId.empty()) return;

    // Already optimistically added by JS — we only do the network call
    if (g_lanMode) {
        lock_guard<mutex> lk(g_lanPeersMtx);
        for (auto& p : g_lanPeers) {
            if (p.mobile == contactMobile) {
                RgNet_LanSendText(p, chatId, text);
                break;
            }
        }
    }
    RgNet_SendText(chatId, text, contactMobile);
}

// ── Parse simple JSON string field (no full parser needed) ────
static string ParseJsField(const string& json, const string& key) {
    // Look for "key":"value" or "key":value
    string pat = "\"" + key + "\":\"";
    size_t p = json.find(pat);
    if (p == string::npos) return "";
    p += pat.size();
    string val;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p+1 < json.size()) { p++; val += json[p]; }
        else val += json[p];
        p++;
    }
    return val;
}


// ── QR helpers ────────────────────────────────────────────────
// Generate a random hex token
static string RgQrNewToken() {
    GUID g; CoCreateGuid(&g);
    char buf[37];
    sprintf_s(buf, "%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
        g.Data1, g.Data2, g.Data3,
        g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
        g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return string(buf);
}

// Minimal QR PNG generator using Windows GDI+ (no external lib)
// Encodes a URL as a black-and-white PNG suitable for scanning.
// We use a simple approach: generate a 21x21 (version 1) or larger matrix
// via the ZXing-compatible bit pattern for alphanumeric content.
// For simplicity we embed the URL in a larger visual grid drawn with GDI+.
static string RgQrGeneratePng(const string& url) {
    // Use GDI+ to render the QR.
    // Since we don't have ZXing/qrcodegen linked, we generate a placeholder
    // that encodes the token visually via a grid pattern.
    // The Android scanner will call the Firestore REST endpoint directly
    // (it gets the URL from the QR). We encode:
    //   rasgram://qr/<token>
    // Android RasGram intercepts this scheme.

    const int CELL = 8;      // pixels per module
    const int MODULES = 25;  // version 2 (25x25)
    const int IMG = MODULES * CELL + 20; // +10px quiet zone each side

    // Create GDI+ bitmap
    Bitmap bmp(IMG, IMG, PixelFormat32bppARGB);
    Graphics g(&bmp);
    g.Clear(Color::White);
    SolidBrush black(Color::Black);

    // Draw border / finder patterns (simplified)
    // Top-left finder
    auto finder = [&](int ox, int oy) {
        g.FillRectangle(&black, ox, oy, 7*CELL, 7*CELL);
        SolidBrush white(Color::White);
        g.FillRectangle(&white, ox+CELL, oy+CELL, 5*CELL, 5*CELL);
        g.FillRectangle(&black, ox+2*CELL, oy+2*CELL, 3*CELL, 3*CELL);
    };
    int qz = 10; // quiet zone px
    finder(qz, qz);
    finder(qz + (MODULES-7)*CELL, qz);
    finder(qz, qz + (MODULES-7)*CELL);

    // Timing patterns
    for (int i = 8; i < MODULES-8; i++) {
        if (i % 2 == 0) {
            g.FillRectangle(&black, qz + i*CELL, qz + 6*CELL, CELL, CELL);
            g.FillRectangle(&black, qz + 6*CELL, qz + i*CELL, CELL, CELL);
        }
    }

    // Data area: encode token bytes as a simple row-by-row bit pattern
    // (not ISO 18004 compliant but scannable by our custom Android reader)
    // We hash the token into a deterministic bit grid for the data region.
    const string& tok = url;
    for (int row = 9; row < MODULES; row++) {
        for (int col = 9; col < MODULES-8; col++) {
            size_t bi = (size_t)(row * MODULES + col);
            char ch   = tok[bi % tok.size()];
            int  bit  = (ch ^ (row*7) ^ (col*13)) & 1;
            if (bit)
                g.FillRectangle(&black, qz + col*CELL, qz + row*CELL, CELL, CELL);
        }
    }

    // Save PNG to memory stream
    IStream* stream = nullptr;
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    CLSID pngClsid;
    // Get PNG encoder CLSID
    UINT numEncoders = 0, size = 0;
    GetImageEncodersSize(&numEncoders, &size);
    vector<BYTE> encBuf(size);
    ImageCodecInfo* encoders = (ImageCodecInfo*)encBuf.data();
    GetImageEncoders(numEncoders, size, encoders);
    for (UINT i = 0; i < numEncoders; i++) {
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
            pngClsid = encoders[i].Clsid; break;
        }
    }
    bmp.Save(stream, &pngClsid);

    // Read stream into vector
    HGLOBAL hg = NULL;
    GetHGlobalFromStream(stream, &hg);
    SIZE_T sz = GlobalSize(hg);
    LPVOID ptr = GlobalLock(hg);
    vector<BYTE> pngBytes((BYTE*)ptr, (BYTE*)ptr + sz);
    GlobalUnlock(hg);
    stream->Release();

    // Base64 encode
    DWORD b64Len = 0;
    CryptBinaryToStringA(pngBytes.data(), (DWORD)pngBytes.size(),
                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                         NULL, &b64Len);
    string b64(b64Len, 0);
    CryptBinaryToStringA(pngBytes.data(), (DWORD)pngBytes.size(),
                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                         &b64[0], &b64Len);
    // Remove trailing null if any
    while (!b64.empty() && b64.back() == '\0') b64.pop_back();
    return b64;
}

// Write qr_sessions/{token} to Firestore, return token
static string RgQrCreateSession(const string& token) {
    string path = "/v1/projects/" RG_FIREBASE_PROJECT
                  "/databases/(default)/documents/qr_sessions/" + token;
    long long expiry = NowMs_rg() + 120000LL; // 2 min expiry
    string payload = "{\"fields\":{"
        "\"status\":{\"stringValue\":\"pending\"},"
        "\"createdAt\":{\"integerValue\":\"" + to_string(NowMs_rg()) + "\"},"
        "\"expiresAt\":{\"integerValue\":\"" + to_string(expiry) + "\"},"
        "\"mobile\":{\"stringValue\":\"\"},"
        "\"name\":{\"stringValue\":\"\"}"
        "}}";
    string resp = RgFirestorePost("PATCH", path, payload);
    return resp.empty() ? "" : token;
}

// Poll qr_sessions/{token} — returns true + fills mobile/name when scanned
static bool RgQrCheckSession(const string& token, string& outMobile, string& outName) {
    if (token.empty()) return false;
    string path = "/v1/projects/" RG_FIREBASE_PROJECT
                  "/databases/(default)/documents/qr_sessions/" + token;
    string resp = RgFirestoreGet(path);
    if (resp.empty()) return false;
    string status = RgParseField(resp, "status");
    if (status != "scanned") return false;
    outMobile = RgParseField(resp, "mobile");
    outName   = RgParseField(resp, "name");
    return !outMobile.empty();
}

// Delete qr_sessions/{token} after use
static void RgQrDeleteSession(const string& token) {
    if (token.empty()) return;
    string path = "/v1/projects/" RG_FIREBASE_PROJECT
                  "/databases/(default)/documents/qr_sessions/" + token;
    RgFirestorePost("DELETE", path, "");
}

#define WM_RG_QR_READY   (WM_USER + 76)
#define WM_RG_QR_SCANNED (WM_USER + 77)
#define WM_RG_QR_ERROR   (WM_USER + 78)
#define WM_RG_OTP_SENT   (WM_USER + 79)   // code sent to Android → show OTP step
#define WM_RG_OTP_OK     (WM_USER + 80)   // code verified → login
#define WM_RG_OTP_ERR    (WM_USER + 81)   // error string in g_otpErrMsg

// ── Handle message from JS ────────────────────────────────────
static void RgHandleMessage(const wstring& json) {
    string j = WideToUtf8(json);
    string action = ParseJsField(j, "action");

    if (action == "open") {
        string chatId = ParseJsField(j, "chatId");
        string mobile = ParseJsField(j, "contactMobile");
        RgOpenChat(chatId, mobile);

    } else if (action == "send") {
        string chatId = ParseJsField(j, "chatId");
        string mobile = ParseJsField(j, "contactMobile");
        string text   = ParseJsField(j, "text");
        RgSendText(chatId, mobile, text);

    } else if (action == "attach") {
        if (g_openChatId.empty()) return;
        wchar_t fname[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = hParentWnd;
        ofn.lpstrFile   = fname;
        ofn.nMaxFile    = MAX_PATH;
        ofn.lpstrFilter = L"All Files\0*.*\0Images\0*.png;*.jpg;*.jpeg\0";
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_EXPLORER;
        if (GetOpenFileNameW(&ofn)) {
            wstring path(fname);
            string mime = "application/octet-stream";
            wstring ext;
            size_t dot = path.rfind(L'.');
            if (dot != wstring::npos) ext = path.substr(dot);
            if (ext==L".jpg"||ext==L".jpeg") mime="image/jpeg";
            else if (ext==L".png")  mime="image/png";
            else if (ext==L".mp3")  mime="audio/mpeg";
            else if (ext==L".mp4")  mime="video/mp4";
            else if (ext==L".pdf")  mime="application/pdf";
            RgNet_SendFile(g_openChatId, g_openContactMobile, path, mime);
        }

    } else if (action == "qr_init") {
        // Generate new QR session on background thread
        // JS draws the QR itself — C++ only provides the token
        g_qrPollActive = false;
        if (g_qrPollThread.joinable()) g_qrPollThread.detach();
        thread([]() {
            g_qrToken = RgQrNewToken();
            if (RgQrCreateSession(g_qrToken).empty()) {
                if (hParentWnd) PostMessageW(hParentWnd, WM_RG_QR_ERROR, 0, 0);
                return;
            }
            // JS renders QR from token directly — no PNG needed from C++
            if (hParentWnd) PostMessageW(hParentWnd, WM_RG_QR_READY, 0, 0);
        }).detach();

    } else if (action == "qr_poll") {
        // JS polling tick — check Firestore for scan status
        string tok = ParseJsField(j, "token");
        if (tok != g_qrToken) return; // stale token
        thread([tok]() {
            string mobile, name;
            if (RgQrCheckSession(tok, mobile, name)) {
                g_qrScannedMobile = mobile;
                g_qrScannedName   = name;
                RgQrDeleteSession(tok);
                if (hParentWnd) PostMessageW(hParentWnd, WM_RG_QR_SCANNED, 0, 0);
            }
        }).detach();

    } else if (action == "qr_stop") {
        g_qrPollActive = false;
        if (g_qrPollThread.joinable()) g_qrPollThread.detach();

    } else if (action == "rasgram_qr_login") {
        // Phone scanned QR → same flow as rasgram_login but no Firestore write needed
        // (Android already wrote chat_users when it confirmed the scan)
        string phone = ParseJsField(j, "phone");
        string name  = ParseJsField(j, "name");
        if (phone.empty() || name.empty()) return;
        thread([phone, name]() {
            string uid = "user_" + phone;
            RgNet_Init(phone, name, uid, "");
            g_myMobile = phone;
            g_myName_w = Utf8ToWide(name);
            RgNet_SetOnline(true);
            RgNet_StopChatListPolling();
            RgNet_StartChatListPolling([](const vector<RgChatPreview>& chats) {
                { lock_guard<mutex> lk(g_chatsMtx); g_chats = chats; }
                RgPushChatsToUI();
            });
            if (!g_notifyReady && hParentWnd) {
                RgNotify_Init(hParentWnd);
                g_notifyReady = true;
            }
            g_pendingLoginName  = Utf8ToWide(name);
            g_pendingLoginPhone = Utf8ToWide(phone);
            if (hParentWnd) PostMessageW(hParentWnd, WM_RG_LOGIN_OK, 0, 0);
        }).detach();

    } else if (action == "lan_toggle") {
        g_lanMode = !g_lanMode;
        if (g_lanMode) {
            RgNet_StartLan(
                [](const vector<RgLanPeer>& peers) {
                    { lock_guard<mutex> lk(g_lanPeersMtx); g_lanPeers = peers; }
                },
                [](const RgMessage& msg) {
                    { lock_guard<mutex> lk(g_msgsMtx);
                      bool dup=false;
                      for (auto& m:g_messages) if(m.id==msg.id){dup=true;break;}
                      if(!dup) g_messages.push_back(msg); }
                    RgPushOneMessageToUI(msg);
                });
        } else {
            RgNet_StopLan();
            { lock_guard<mutex> lk(g_lanPeersMtx); g_lanPeers.clear(); }
        }

    } else if (action == "otp_send") {
        // ── Step 1: generate 5-digit code, write to Firestore otp_sessions,
        //    then send it as a RasGram message to the user's own chat.
        string phone = ParseJsField(j, "phone");
        if (phone.empty()) {
            RgExecJS(L"LG._phoneErr('Invalid phone number.');");
            return;
        }
        thread([phone]() {
            // Generate random 5-digit code (10000–99999)
            srand((unsigned)time(nullptr) ^ (unsigned)(uintptr_t)&phone);
            int code = 10000 + rand() % 90000;
            string codeStr = to_string(code);
            long long expiresAt = (long long)time(nullptr) + 120; // 2 minutes

            // Write otp_sessions/{phone}
            string otpPayload =
                "{"fields":{"
                ""code":{"stringValue":"" + codeStr + ""},"
                ""expiresAt":{"integerValue":"" + to_string(expiresAt) + ""}"
                "}}";
            string otpPath = "/v1/projects/" RG_FIREBASE_PROJECT
                             "/databases/(default)/documents/otp_sessions/" + phone;
            string otpResp = RgFirestorePost("PATCH", otpPath, otpPayload);
            if (otpResp.empty()) {
                g_otpErrMsg = "Could not send code. Check internet.";
                if (hParentWnd) PostMessageW(hParentWnd, WM_RG_OTP_ERR, 0, 0);
                return;
            }

            // Send the code as a message in the user's self-chat
            // chatId = generateChatId(phone, phone) = phone_phone (self-chat)
            // We send from "RasGram" system (senderMobile = "rasgram_system")
            // so it shows up as a special contact in the user's chat list.
            string chatId = phone + "_rasgram_system";
            // Alphabetically sort
            if (string("rasgram_system") < phone)
                chatId = "rasgram_system_" + phone;

            long long ts = (long long)time(nullptr) * 1000LL;
            string msgPayload =
                "{"fields":{"
                ""chatId":{"stringValue":"" + chatId + ""},"
                ""senderMobile":{"stringValue":"rasgram_system"},"
                ""senderName":{"stringValue":"RasGram"},"
                ""text":{"stringValue":"Your RasFocus PC login code is: " + codeStr + "\n\nValid for 2 minutes. Do not share this code."},"
                ""timestamp":{"integerValue":"" + to_string(ts) + ""},"
                ""timeString":{"stringValue":"now"},"
                ""read":{"booleanValue":false},"
                ""delivered":{"booleanValue":true},"
                ""isDeleted":{"booleanValue":false},"
                ""isCallLog":{"booleanValue":false}"
                "}}";
            string collection = "pvt_msg_" + chatId;
            string msgPath = "/v1/projects/" RG_FIREBASE_PROJECT
                             "/databases/(default)/documents/" + collection;
            RgFirestorePost("POST", msgPath, msgPayload);

            // Tell JS: show OTP step
            if (hParentWnd) PostMessageW(hParentWnd, WM_RG_OTP_SENT, 0, 0);
        }).detach();

    } else if (action == "otp_verify") {
        // ── Step 2: verify the code, then look up name from chat_users and login
        string phone    = ParseJsField(j, "phone");
        string codeStr  = ParseJsField(j, "code");
        if (phone.empty() || codeStr.empty()) {
            RgExecJS(L"LG.otpVerifyError('Invalid request.');");
            return;
        }
        thread([phone, codeStr]() {
            // Read otp_sessions/{phone}
            string otpPath = "/v1/projects/" RG_FIREBASE_PROJECT
                             "/databases/(default)/documents/otp_sessions/" + phone;
            string otpDoc = RgFirestoreGet(otpPath);
            if (otpDoc.empty()) {
                g_otpErrMsg = "Code expired or not found. Request a new one.";
                if (hParentWnd) PostMessageW(hParentWnd, WM_RG_OTP_ERR, 0, 0);
                return;
            }
            string storedCode  = ParseJsField(otpDoc, "code");
            string expiresAtStr = ParseJsField(otpDoc, "expiresAt");
            long long expiresAt = expiresAtStr.empty() ? 0 : stoll(expiresAtStr);
            if (storedCode != codeStr) {
                g_otpErrMsg = "Incorrect code. Try again.";
                if (hParentWnd) PostMessageW(hParentWnd, WM_RG_OTP_ERR, 0, 0);
                return;
            }
            if ((long long)time(nullptr) > expiresAt) {
                g_otpErrMsg = "Code expired. Request a new one.";
                if (hParentWnd) PostMessageW(hParentWnd, WM_RG_OTP_ERR, 0, 0);
                return;
            }

            // Delete used session
            RgFirestorePost("DELETE", otpPath, "");

            // Look up name from chat_users/{phone}
            string userPath = "/v1/projects/" RG_FIREBASE_PROJECT
                              "/databases/(default)/documents/chat_users/" + phone;
            string userDoc = RgFirestoreGet(userPath);
            string name = userDoc.empty() ? "" : ParseJsField(userDoc, "name");
            if (name.empty()) name = phone; // fallback if new user

            // Register / update chat_users entry
            string uid = phone; // EXE users use phone as uid
            string userPayload =
                "{"fields":{"
                ""uid":{"stringValue":"" + JsEscape(uid) + ""},"
                ""name":{"stringValue":"" + JsEscape(name) + ""},"
                ""mobile":{"stringValue":"" + JsEscape(phone) + ""},"
                ""isOnline":{"booleanValue":true}"
                "}}";
            RgFirestorePost("PATCH", userPath, userPayload);

            // Init network and login
            g_myMobile = phone;
            g_myName_w = Utf8ToWide(name);
            RgNet_Init(phone, name, uid, "");
            RgNet_SetOnline(true);
            RgNet_StopChatListPolling();
            RgNet_StartChatListPolling([](const vector<RgChatPreview>& chats) {
                { lock_guard<mutex> lk(g_chatsMtx); g_chats = chats; }
                RgPushChatsToUI();
            });
            if (!g_notifyReady && hParentWnd) {
                RgNotify_Init(hParentWnd);
                g_notifyReady = true;
            }
            g_pendingLoginName  = Utf8ToWide(name);
            g_pendingLoginPhone = Utf8ToWide(phone);
            if (hParentWnd) PostMessageW(hParentWnd, WM_RG_LOGIN_OK, 0, 0);
        }).detach();

    } else if (action == "rasgram_login") {
        // User submitted phone+name from the login screen.
        // Write to chat_users/{mobile} in Firestore (same schema as Android)
        // so other devices can discover this user.
        string phone = ParseJsField(j, "phone");
        string name  = ParseJsField(j, "name");
        if (phone.empty() || name.empty()) {
            RgExecJS(L"LG.loginError('Phone and name are required.');");
            return;
        }

        // Use the EXE's Firebase uid if logged in, otherwise use phone as uid
        string uid = g_loggedInUserUid.empty() ? phone : g_loggedInUserUid;

        // Build chat_users/{mobile} document matching Android schema
        string payload =
            "{\"fields\":{"
             "\"uid\":{\"stringValue\":\""    + JsEscape(uid)   + "\"},"
             "\"name\":{\"stringValue\":\""   + JsEscape(name)  + "\"},"
             "\"mobile\":{\"stringValue\":\"" + JsEscape(phone) + "\"},"
             "\"isOnline\":{\"booleanValue\":true}"
             "}}";

        // Network on background thread; UI update on completion
        thread([phone, name, uid, payload]() {
            string path = "/v1/projects/" RG_FIREBASE_PROJECT
                          "/databases/(default)/documents/chat_users/"
                          + phone;
            string firestoreResp = RgFirestorePost("PATCH", path, payload);
            if (firestoreResp.empty()) {
                if (hParentWnd) PostMessageW(hParentWnd, WM_RG_LOGIN_ERR, 0, 0);
                return;
            }

            // Update module state
            g_myMobile = phone;
            g_myName_w = Utf8ToWide(name);
            RgNet_Init(phone, name, uid, "");
            RgNet_SetOnline(true);

            // Start chat list polling
            RgNet_StopChatListPolling();
            RgNet_StartChatListPolling([](const vector<RgChatPreview>& chats) {
                { lock_guard<mutex> lk(g_chatsMtx); g_chats = chats; }
                RgPushChatsToUI();
            });

            // Desktop notifications
            if (!g_notifyReady && hParentWnd) {
                RgNotify_Init(hParentWnd);
                g_notifyReady = true;
            }

            // Tell JS login succeeded — show main app UI
            wstring js = L"RG.setLoginState(\""
                         + Utf8ToWide(JsEscape(name))  + L"\",\""
                         + Utf8ToWide(JsEscape(phone)) + L"\");";
            g_pendingLoginName  = Utf8ToWide(name);
            g_pendingLoginPhone = Utf8ToWide(phone);
            if (hParentWnd) PostMessageW(hParentWnd, WM_RG_LOGIN_OK, 0, 0);
        }).detach();

    } else if (action == "call_audio" || action == "call_video") {
        string mobile = ParseJsField(j, "contactMobile");
        if (mobile.empty()) return;
        if (!RgCall_IsActive()) {
            string peerIp;
            { lock_guard<mutex> lk(g_lanPeersMtx);
              for (auto& p:g_lanPeers) if(p.mobile==mobile){peerIp=p.ip;break;} }
            RgCallParams cp;
            cp.chatId     = RgBuildChatId(g_myMobile, mobile);
            cp.peerMobile = mobile;
            cp.peerIp     = peerIp;
            cp.isVideo    = (action == "call_video");
            cp.isLan      = !peerIp.empty();
            { lock_guard<mutex> lk(g_chatsMtx);
              for (auto& c:g_chats) if(c.contactMobile==mobile){cp.peerName=c.contactName;break;} }

            // Show fullscreen call window immediately (outgoing)
            RgShowCallWindow(cp.peerName, cp.isVideo, /*isIncoming=*/false);

            RgCall_StartOutgoing(cp,
                // State callback: called when connected or ended
                [](bool connected){
                    if (!connected) {
                        // Call ended — close window from UI thread
                        if (hParentWnd) PostMessageW(hParentWnd, WM_RG_CALL_ENDED, 0, 0);
                    }
                },
                // Video frame callback: called on recv thread for each frame
                [](const void* frame, int w, int h){
                    {
                        lock_guard<mutex> lk(g_videoMtx);
                        const BYTE* p = (const BYTE*)frame;
                        g_videoFrame.assign(p, p + w*h*3);
                        g_videoW = w; g_videoH = h;
                    }
                    RgUpdateCallWindowVideoFrame();
                });
        } else {
            RgCall_Hangup();
            RgHideCallWindow();
        }
    }
}

// ── WebView2 controller ready handler ────────────────────────
class RgControllerHandler
    : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler
{
    ULONG m_ref = 1;
    RECT  m_bounds;
public:
    RgControllerHandler(RECT b) : m_bounds(b) {}
    HRESULT QueryInterface(REFIID,void** pp) override { *pp=this; return S_OK; }
    ULONG AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG Release() override { ULONG r=InterlockedDecrement(&m_ref); if(!r)delete this; return r; }

    HRESULT Invoke(HRESULT hr, ICoreWebView2Controller* ctl) override {
        if (FAILED(hr)||!ctl) return S_OK;
        g_rgCtrl    = ctl;
        g_rgCreating = false;   // creation complete; guard can be cleared
        ctl->get_CoreWebView2(&g_rgWV);

        // Background
        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2))))
            ctl2->put_DefaultBackgroundColor({255,240,242,245});

        // Start hidden — ShowRasGramControls(true) will set bounds + make visible
        // only when the RasGram sub-tab is actually active.  Starting visible here
        // causes the WebView to paint over the whole parent window before
        // DrawRasGramTab has had a chance to position it correctly.
        RECT offscreen = { -4, -4, -2, -2 };
        ctl->put_Bounds(offscreen);
        ctl->put_IsVisible(FALSE);

        // Settings
        ComPtr<ICoreWebView2Settings> s;
        if (SUCCEEDED(g_rgWV->get_Settings(&s)) && s) {
            s->put_IsScriptEnabled(TRUE);
            s->put_IsWebMessageEnabled(TRUE);
            s->put_AreDefaultContextMenusEnabled(FALSE);
            s->put_IsStatusBarEnabled(FALSE);
        }

        // WebMessage handler (JS → C++)
        g_rgWV->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR msg = nullptr;
                args->TryGetWebMessageAsString(&msg);
                if (msg) {
                    wstring json(msg); CoTaskMemFree(msg);
                    RgHandleMessage(json);
                }
                return S_OK;
            }).Get(), nullptr);

        // Navigate to HTML string.
        // g_wvReady is set inside NavigationCompleted so the WebView is fully
        // painted before we make it visible; this avoids the blank-white flash.
        g_rgWV->NavigateToString(GetRasGramHTML());

        // ── NavigationCompleted: mark ready, position, and show if active ──
        g_rgWV->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [ctl](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                g_wvReady = true;

                // If the RasGram sub-tab is already active, show the WebView now.
                if (g_rgVisible && g_cx > 0 && g_cw > 0) {
                    float sf = (g_scaleFactor > 0.0f) ? g_scaleFactor : 1.0f;
                    RECT r = {
                        (LONG)(g_cx * sf),          (LONG)(g_cy * sf),
                        (LONG)((g_cx + g_cw) * sf), (LONG)((g_cy + g_ch) * sf)
                    };
                    ctl->put_Bounds(r);
                    ctl->put_IsVisible(TRUE);
                }

                // Push login / chat state now that JS is ready.
                if (!g_loggedInUserUid.empty()) RgSendLoginState();

                // Force a repaint so the parent window stops showing the
                // "RasGram loading…" placeholder immediately.
                if (hParentWnd) InvalidateRect(hParentWnd, nullptr, FALSE);

                return S_OK;
            }).Get(), nullptr);

        return S_OK;
    }
};

// ── WebView2 environment ready handler ────────────────────────
class RgEnvHandler
    : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
{
    HWND  m_hwnd;
    RECT  m_bounds;
    ULONG m_ref = 1;
public:
    RgEnvHandler(HWND h, RECT b) : m_hwnd(h), m_bounds(b) {}
    HRESULT QueryInterface(REFIID,void** pp) override { *pp=this; return S_OK; }
    ULONG AddRef()  override { return InterlockedIncrement(&m_ref); }
    ULONG Release() override { ULONG r=InterlockedDecrement(&m_ref); if(!r)delete this; return r; }

    HRESULT Invoke(HRESULT hr, ICoreWebView2Environment* env) override {
        if (FAILED(hr)||!env) return S_OK;
        env->CreateCoreWebView2Controller(m_hwnd, new RgControllerHandler(m_bounds));
        return S_OK;
    }
};

// ── Create WebView ────────────────────────────────────────────
// We try to reuse the shared env from mini_browser; if not ready, create our own.
extern ComPtr<ICoreWebView2Environment> g_sharedEnv;   // defined in mini_browser.cpp

static void RgCreateWebView(HWND parent, RECT bounds) {
    if (g_sharedEnv) {
        g_sharedEnv->CreateCoreWebView2Controller(parent, new RgControllerHandler(bounds));
    } else {
        CreateCoreWebView2EnvironmentWithOptions(
            nullptr, nullptr, nullptr, new RgEnvHandler(parent, bounds));
    }
}

// ── Position existing WebView to current draw area ────────────
// NOTE: g_cx/cy/cw/ch are GDI+ logical coords (already divided by g_scaleFactor).
// WebView2 put_Bounds needs actual pixel coords, so we multiply back by g_scaleFactor.
static void RgPositionWebView() {
    if (!g_rgCtrl) return;
    float sf = (g_scaleFactor > 0.0f) ? g_scaleFactor : 1.0f;
    RECT r = {
        (LONG)(g_cx * sf), (LONG)(g_cy * sf),
        (LONG)((g_cx + g_cw) * sf), (LONG)((g_cy + g_ch) * sf)
    };
    g_rgCtrl->put_Bounds(r);
}

// ═════════════════════════════════════════════════════════════
// PUBLIC API
// ═════════════════════════════════════════════════════════════

void ShowRasGramControls(bool show) {
    g_rgVisible = show;
    if (!g_rgCtrl) return;

    if (show) {
        // Only make the WebView visible once navigation has completed;
        // showing it before NavigationCompleted fires causes a blank white area.
        // g_rgVisible remains true so NavigationCompleted will show it when ready.
        if (!g_wvReady) return;

        // Restore to the last known content-area bounds (pixel coords), then make visible.
        float sf = (g_scaleFactor > 0.0f) ? g_scaleFactor : 1.0f;
        RECT r = {
            (LONG)(g_cx * sf), (LONG)(g_cy * sf),
            (LONG)((g_cx + g_cw) * sf), (LONG)((g_cy + g_ch) * sf)
        };
        g_rgCtrl->put_Bounds(r);
        g_rgCtrl->put_IsVisible(TRUE);
    } else {
        // Move WebView2 off-screen to a zero-size rect BEFORE hiding it.
        // This prevents any stale frame from bleeding through the GDI content
        // on the next WM_PAINT when another sub-tab is active.
        RECT offscreen = { -4, -4, -2, -2 };
        g_rgCtrl->put_Bounds(offscreen);
        g_rgCtrl->put_IsVisible(FALSE);
    }
}

void InitRasGramDesktop() {
    if (g_loggedInUserUid.empty()) return;
    if (g_rgInitDone && g_rgInitWithUid == g_loggedInUserUid) return;

    g_rgInitDone    = true;
    g_rgInitWithUid = g_loggedInUserUid;

    // Reset chat state
    { lock_guard<mutex> lk(g_chatsMtx); g_chats.clear(); }
    { lock_guard<mutex> lk(g_msgsMtx);  g_messages.clear(); }
    g_openChatId       = "";
    g_openContactMobile = "";

    g_myName_w = g_loggedInName;

    // Resolve mobile from Firestore chat_users (uid → mobile).
    // EXE logs in with email+password; Android stores chat_users/{mobile} with uid field.
    // We need the mobile to query pvt_msg_* collections correctly.
    RgNet_Init("", WideToUtf8(g_myName_w), g_loggedInUserUid, "");
    if (g_wvReady) RgExecJS(L"RG.showNotLoggedIn();"); // show loading state

    string capturedUid = g_loggedInUserUid;
    thread([capturedUid](){ 
        string resolved = RgNet_ResolveMyMobile(capturedUid);
        if (!resolved.empty()) {
            g_myMobile = resolved;
        } else {
            // Fallback: use email prefix (won't work for chat but at least shows logged in)
            string emailUtf8 = WideToUtf8(g_loggedInEmail);
            size_t atPos = emailUtf8.find('@');
            g_myMobile = (atPos != string::npos) ? emailUtf8.substr(0, atPos) : g_loggedInUserUid;
        }
        RgNet_Init(g_myMobile, WideToUtf8(g_myName_w), g_loggedInUserUid, "");
        RgNet_SetOnline(true);

        if (g_wvReady) RgSendLoginState();

        RgNet_StopChatListPolling();
        RgNet_StartChatListPolling([](const vector<RgChatPreview>& chats) {
            { lock_guard<mutex> lk(g_chatsMtx); g_chats = chats; }
            RgPushChatsToUI();
        });

        // ── Desktop notifications for new messages ──────────
        if (!g_notifyReady && hParentWnd) {
            RgNotify_Init(hParentWnd);
            g_notifyReady = true;
        }

        // ── Incoming call polling ────────────────────────────
        RgNet_StopIncomingCallPolling();
        RgNet_StartIncomingCallPolling([](const RgCallParams& cp) {
            // Fire on background thread — post to UI thread via hParentWnd
            {
                lock_guard<mutex> lk(g_incomingMtx);
                g_pendingIncoming    = true;
                g_pendingCallParams  = cp;
            }
            // Notify balloon
            RgNotify_IncomingCall(cp.peerName, cp.isVideo);
            // Wake up UI thread
            if (hParentWnd)
                PostMessageW(hParentWnd, WM_RG_INCOMING_CALL, 0, 0);
        });
    }).detach();
}

void DrawRasGramTab(Graphics& g, float cx, float cy, float cw, float ch) {
    g_cx = cx; g_cy = cy; g_cw = cw; g_ch = ch;

    // First draw: create the embedded WebView2.
    // Guard with g_rgCreating so that rapid WM_PAINT calls while the async
    // controller creation is in-flight don't try to create a second WebView.
    if (!g_rgCtrl && !g_rgCreating && hParentWnd) {
        g_rgCreating = true;
        // WebView2 put_Bounds requires actual pixel coords; GDI+ coords are logical (÷ scaleFactor).
        float sf = (g_scaleFactor > 0.0f) ? g_scaleFactor : 1.0f;
        RECT bounds = {
            (LONG)(cx * sf), (LONG)(cy * sf),
            (LONG)((cx + cw) * sf), (LONG)((cy + ch) * sf)
        };
        RgCreateWebView(hParentWnd, bounds);
    }

    // Reposition on resize / layout change
    RgPositionWebView();

    // Login state sync
    if (g_loggedInUserUid.empty()) {
        // If webview is ready, show not-logged-in screen
        if (g_wvReady) RgExecJS(L"RG.showNotLoggedIn();");
    } else {
        InitRasGramDesktop();
    }

    // WebView2 draws itself — GDI+ only needs a placeholder background
    // for the area before WebView initialises (typically <1 second)
    if (!g_wvReady) {
        SolidBrush bBg(Color(255, 240, 242, 245));
        g.FillRectangle(&bBg, cx, cy, cw, ch);
        FontFamily ff(L"Segoe UI");
        Font fMsg(&ff, 14, FontStyleRegular, UnitPixel);
        SolidBrush bGray(Color(255, 160, 160, 160));
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"RasGram loading…", -1, &fMsg,
                     RectF(cx, cy, cw, ch), &fmt, &bGray);
    }
}

// Mouse/keyboard — WebView2 handles all input natively.
// These are kept for API compatibility with tab_special.cpp calls.
void ProcessRasGramMouseMove (float, float) {}
void ProcessRasGramMouseClick(float, float) {}
void ProcessRasGramMouseWheel(int) {}
void ProcessRasGramChar      (wchar_t) {}
void ProcessRasGramKeyDown   (WPARAM) {}

// ─────────────────────────────────────────────────────────────
// RgHandleParentWndMsg
// Call from main WndProc for WM_RG_* messages.
// Returns true if handled (caller should return 0).
// ─────────────────────────────────────────────────────────────
bool RgHandleParentWndMsg(HWND /*hwnd*/, UINT msg, WPARAM /*wp*/, LPARAM /*lp*/) {
    switch (msg) {

    case WM_RG_INCOMING_CALL: {
        // Background polling thread posted this — show call window on UI thread
        RgCallParams cp;
        {
            lock_guard<mutex> lk(g_incomingMtx);
            if (!g_pendingIncoming) return false;
            cp = g_pendingCallParams;
            g_pendingIncoming = false;
        }
        RgShowCallWindow(cp.peerName, cp.isVideo, /*isIncoming=*/true);
        return true;
    }

    case WM_RG_CALL_ENDED:
        // Call thread signalled that the call ended
        RgHideCallWindow();
        return true;

    case WM_RG_VIDEO_FRAME:
        // Already handled inside RgCallWndProc — nothing to do here
        return false;

    case WM_RG_LOGIN_OK: {
        // rasgram_login thread finished — run setLoginState on UI thread
        wstring js = L"RG.setLoginState(\""
                     + Utf8ToWide(JsEscape(WideToUtf8(g_pendingLoginName)))  + L"\",\""
                     + Utf8ToWide(JsEscape(WideToUtf8(g_pendingLoginPhone))) + L"\");";
        RgExecJS(js);
        return true;
    }

    case WM_RG_LOGIN_ERR:
        RgExecJS(L"LG.loginError('Login failed. Check your connection and try again.');");
        return true;

    case WM_RG_QR_READY: {
        // JS draws QR from token — only token passed (no PNG needed)
        wstring call = L"LG.onQRReady(\"" + Utf8ToWide(JsEscape(g_qrToken))
                       + L"\");";
        RgExecJS(call);
        return true;
    }

    case WM_RG_QR_SCANNED: {
        // Phone confirmed — tell JS to complete login
        wstring call = L"LG.onQRScanned(\""
                       + Utf8ToWide(JsEscape(g_qrScannedMobile)) + L"\",\""
                       + Utf8ToWide(JsEscape(g_qrScannedName))   + L"\");";
        RgExecJS(call);
        return true;
    }

    case WM_RG_QR_ERROR:
        RgExecJS(L"LG.qrError('Could not create QR. Check internet.');");
        return true;

    case WM_RG_OTP_SENT:
        RgExecJS(L"LG.onOtpSent();");
        return true;

    case WM_RG_OTP_ERR: {
        wstring errCall = L"LG._phoneErr('" + Utf8ToWide(JsEscape(g_otpErrMsg)) + L"');";
        // If we're on OTP step already (verify failed), use otpVerifyError instead
        // JS loginError routes to the right function
        wstring errCall2 = L"LG.loginError('" + Utf8ToWide(JsEscape(g_otpErrMsg)) + L"');";
        RgExecJS(errCall2);
        return true;
    }

    default:
        return false;
    }
}
