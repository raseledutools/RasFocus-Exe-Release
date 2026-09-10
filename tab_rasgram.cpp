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

using namespace Microsoft::WRL;
using namespace Gdiplus;   // still needed for DrawRasGramTab signature
using namespace std;

// ── Externals ────────────────────────────────────────────────
extern HWND    hParentWnd;
extern string  g_loggedInUserUid;
extern wstring g_loggedInName;
extern wstring g_loggedInEmail;

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

/* not-logged-in */
#not-logged-in{display:none;flex-direction:column;align-items:center;justify-content:center;
               height:100%;gap:14px;background:#f0f2f5}
#not-logged-in .icon{font-size:64px;color:#00969f;opacity:.5}
#not-logged-in h2{font-size:22px;color:#333;font-weight:700}
#not-logged-in p{font-size:14px;color:#888;text-align:center;max-width:320px}
#not-logged-in .hint{font-size:12px;color:#bbb;font-style:italic}

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

<!-- NOT LOGGED IN -->
<div id="not-logged-in">
  <div class="icon">💬</div>
  <h2>RasGram Desktop</h2>
  <p>You are not logged in.<br>Please go to the <strong>My Account</strong> tab and sign in.</p>
  <div class="hint">After login, come back here — your chats will load automatically.</div>
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
    state.myName   = name;
    state.myMobile = mobile;
    document.getElementById('not-logged-in').style.display = 'none';
    document.getElementById('app').style.display = 'flex';
  },
  showNotLoggedIn() {
    document.getElementById('not-logged-in').style.display = 'flex';
    document.getElementById('app').style.display = 'none';
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

        // Navigate to HTML string
        g_rgWV->NavigateToString(GetRasGramHTML());
        g_wvReady = true;

        // If the RasGram tab is already the active tab (ShowRasGramControls(true)
        // was called before WebView finished creating), apply the correct bounds and
        // make it visible now.  Otherwise it stays offscreen/hidden until the user
        // switches to the RasGram sub-tab.
        if (g_rgVisible && g_cx > 0 && g_cw > 0) {
            RECT r = {
                (LONG)g_cx, (LONG)g_cy,
                (LONG)(g_cx + g_cw), (LONG)(g_cy + g_ch)
            };
            ctl->put_Bounds(r);
            ctl->put_IsVisible(TRUE);
        }

        // Immediately push login state and chats if already available
        if (!g_loggedInUserUid.empty()) RgSendLoginState();

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
static void RgPositionWebView() {
    if (!g_rgCtrl) return;
    RECT r = {
        (LONG)g_cx, (LONG)g_cy,
        (LONG)(g_cx + g_cw), (LONG)(g_cy + g_ch)
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
        // Restore to the last known content-area bounds, then make visible.
        RECT r = {
            (LONG)g_cx, (LONG)g_cy,
            (LONG)(g_cx + g_cw), (LONG)(g_cy + g_ch)
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
        RECT bounds = { (LONG)cx, (LONG)cy, (LONG)(cx+cw), (LONG)(cy+ch) };
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

    default:
        return false;
    }
}
