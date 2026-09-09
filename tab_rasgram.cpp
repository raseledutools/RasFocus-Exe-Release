// tab_rasgram.cpp
// RasGram Desktop — Full Telegram-style Native C++ UI
//
// Layout:
//   ┌─────────────────────────────────────────────────────────────┐
//   │  [🔍 Search...]          RasGram         [⚙]  [📡 LAN]    │  ← Top bar
//   ├──────────────────┬──────────────────────────────────────────┤
//   │                  │  ┌──────────────────────────────────┐    │
//   │  Contact / Chat  │  │  Avatar  Name      [📞] [📹]    │    │  ← Chat header
//   │  List            │  ├──────────────────────────────────┤    │
//   │                  │  │                                  │    │
//   │  [●] Name        │  │    Message Bubbles (scrollable)  │    │
//   │      Last msg    │  │                                  │    │
//   │      Time  [3]   │  │                                  │    │
//   │  ─────────────   │  ├──────────────────────────────────┤    │
//   │  [●] Name        │  │  [📎]  [Type a message...] [🎤][➤]│  │  ← Input bar
//   │      ...         │  └──────────────────────────────────┘    │
//   └──────────────────┴──────────────────────────────────────────┘

#include "tab_rasgram.h"
#include "rasgram_net.h"
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <algorithm>
#include <commdlg.h>
#include <shlobj.h>

#pragma comment(lib, "comdlg32.lib")

using namespace Gdiplus;
using namespace std;

// Forward declaration
static long long NowMs_safe();

// ── Externals from main.cpp ──────────────────────────────────
extern HWND   hParentWnd;
extern string g_loggedInUserUid;
extern wstring g_loggedInName;
extern wstring g_loggedInEmail;

// ── Module State ─────────────────────────────────────────────
static bool   g_rgInitDone    = false;
static string g_rgInitWithUid = "";   // UID that was used at last init
static bool   g_rgVisible     = false;

// Layout cache (set on each draw)
static float g_cx = 0, g_cy = 0, g_cw = 0, g_ch = 0;
static float g_listW   = 280.0f;   // contact list width
static float g_topBarH =  52.0f;   // top bar height

// ── Login / Account ───────────────────────────────────────────
static string  g_myMobile;          // populated from accounts on init
static wstring g_myName_w;

// ── Contact / Chat list ───────────────────────────────────────
static mutex              g_chatsMtx;
static vector<RgChatPreview> g_chats;
static int    g_selectedChat  = -1;   // index into g_chats
static int    g_hoveredChat   = -1;
static float  g_chatListScroll = 0.0f;

// ── Messages ─────────────────────────────────────────────────
static mutex              g_msgsMtx;
static vector<RgMessage>  g_messages;
static float  g_msgScroll     = 0.0f;
static bool   g_scrollToBottom = true;
static long long g_lastMsgTs  = 0;

// ── Input bar ─────────────────────────────────────────────────
static wstring g_inputText;
static bool    g_inputFocused = false;
static HWND    g_hInputEdit   = NULL;   // Win32 EDIT control for composing
static HWND    g_hSearchEdit  = NULL;   // Win32 EDIT control for search

// ── Search ────────────────────────────────────────────────────
static wstring g_searchText;
static bool    g_searchFocused = false;

// ── LAN mode ─────────────────────────────────────────────────
static bool                 g_lanMode     = false;
static vector<RgLanPeer>    g_lanPeers;
static mutex                g_lanPeersMtx;

// ── Active call UI state ──────────────────────────────────────
static bool    g_callWindowVisible = false;
static bool    g_callIsVideo       = false;
static HWND    g_hCallWnd          = NULL;

// ── Hover states ──────────────────────────────────────────────
static bool g_hovSend      = false;
static bool g_hovAttach    = false;
static bool g_hovVoice     = false;
static bool g_hovSettings  = false;
static bool g_hovLanToggle = false;
static bool g_hovCallAudio = false;
static bool g_hovCallVideo = false;
static bool g_hovSearch    = false;

// ─────────────────────────────────────────────────────────────
// HELPER: FillRoundRect
// ─────────────────────────────────────────────────────────────
static void RgRoundRect(Graphics& g, Brush* br, Pen* pen,
                        float x, float y, float w, float h, float r = 8.0f) {
    GraphicsPath path;
    path.AddArc(x,         y,         r*2, r*2, 180, 90);
    path.AddArc(x+w-r*2,   y,         r*2, r*2, 270, 90);
    path.AddArc(x+w-r*2,   y+h-r*2,   r*2, r*2,   0, 90);
    path.AddArc(x,         y+h-r*2,   r*2, r*2,  90, 90);
    path.CloseFigure();
    if (br)  g.FillPath(br,  &path);
    if (pen) g.DrawPath(pen, &path);
}

// ─────────────────────────────────────────────────────────────
// HELPER: Draw avatar circle with initial letter
// ─────────────────────────────────────────────────────────────
static void DrawAvatar(Graphics& g, float ax, float ay, float r,
                       const string& name, Color bg) {
    SolidBrush bgBr(bg);
    g.FillEllipse(&bgBr, ax, ay, r, r);
    if (!name.empty()) {
        FontFamily ff(L"Segoe UI");
        Font fLetter(&ff, r * 0.45f, FontStyleBold, UnitPixel);
        SolidBrush white(Color(255,255,255,255));
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        wstring initial(1, (wchar_t)toupper(name[0]));
        g.DrawString(initial.c_str(), -1, &fLetter,
                     RectF(ax, ay, r, r), &fmt, &white);
    }
}

// ─────────────────────────────────────────────────────────────
// HELPER: Avatar color from mobile string
// ─────────────────────────────────────────────────────────────
static Color AvatarColor(const string& mobile) {
    static Color palette[] = {
        Color(255, 229,  57,  53),   // red
        Color(255,  33, 150, 243),   // blue
        Color(255,  76, 175,  80),   // green
        Color(255, 156,  39, 176),   // purple
        Color(255, 255, 152,   0),   // orange
        Color(255,   0, 188, 212),   // cyan
        Color(255, 233,  30,  99),   // pink
        Color(255, 121,  85,  72),   // brown
    };
    int idx = 0;
    for (char c : mobile) idx = (idx * 31 + c) % 8;
    return palette[idx < 0 ? 0 : idx];
}

// ─────────────────────────────────────────────────────────────
// HELPER: Truncate string to fit width
// ─────────────────────────────────────────────────────────────
static wstring TruncateW(const wstring& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;
    return s.substr(0, maxLen) + L"...";
}
static wstring Utf8ToWide(const string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    if (n <= 0) return L"";
    wstring ws(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], n);
    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
    return ws;
}
static string WideToUtf8(const wstring& ws) {
    if (ws.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, NULL, 0, NULL, NULL);
    if (n <= 0) return "";
    string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &s[0], n, NULL, NULL);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// ─────────────────────────────────────────────────────────────
// CREATE WIN32 INPUT CONTROLS (called once)
// ─────────────────────────────────────────────────────────────
static void CreateInputControls() {
    if (!hParentWnd) return;

    // Search box (hidden by default, shown when Special > RasGram tab is visible)
    if (!g_hSearchEdit) {
        g_hSearchEdit = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | ES_LEFT | ES_AUTOHSCROLL,
            0, 0, 1, 1, hParentWnd, (HMENU)9001, GetModuleHandle(NULL), NULL);
        HFONT hf = CreateFont(14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
        SendMessage(g_hSearchEdit, WM_SETFONT, (WPARAM)hf, TRUE);
        SendMessageW(g_hSearchEdit, EM_SETCUEBANNER, 0, (LPARAM)L"🔍 Search...");
    }

    // Message compose box (multi-line)
    if (!g_hInputEdit) {
        g_hInputEdit = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | ES_LEFT | ES_AUTOHSCROLL | ES_MULTILINE | ES_WANTRETURN,
            0, 0, 1, 1, hParentWnd, (HMENU)9002, GetModuleHandle(NULL), NULL);
        HFONT hf = CreateFont(14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
        SendMessage(g_hInputEdit, WM_SETFONT, (WPARAM)hf, TRUE);
        SendMessageW(g_hInputEdit, EM_SETCUEBANNER, 0, (LPARAM)L"Type a message...");
    }
}

// ─────────────────────────────────────────────────────────────
// SHOW / HIDE CONTROLS
// ─────────────────────────────────────────────────────────────
void ShowRasGramControls(bool show) {
    g_rgVisible = show;
    if (g_hSearchEdit) ShowWindow(g_hSearchEdit, show ? SW_SHOW : SW_HIDE);
    if (g_hInputEdit && g_selectedChat >= 0)
        ShowWindow(g_hInputEdit, show ? SW_SHOW : SW_HIDE);
    else if (g_hInputEdit)
        ShowWindow(g_hInputEdit, SW_HIDE);
}

// ─────────────────────────────────────────────────────────────
// REFRESH CONTROLS POSITION (call on resize / tab switch)
// ─────────────────────────────────────────────────────────────
static void RepositionControls() {
    if (!g_rgVisible) return;

    float scale = 1.0f;

    // Search box inside top bar
    float sx = g_cx + 12, sy = g_cy + 10;
    float sw = g_listW - 24, sh = 30;
    if (g_hSearchEdit)
        SetWindowPos(g_hSearchEdit, NULL,
                     (int)sx, (int)sy, (int)sw, (int)sh,
                     SWP_NOZORDER | SWP_NOACTIVATE);

    // Message input box — bottom of chat area
    if (g_selectedChat >= 0 && g_hInputEdit) {
        float inputH = 36.0f;
        float chatX  = g_cx + g_listW;
        float inputY = g_cy + g_ch - inputH - 8.0f;
        float inputW = g_cw - g_listW - 44.0f - 44.0f - 12.0f;  // minus send + attach buttons
        SetWindowPos(g_hInputEdit, NULL,
                     (int)(chatX + 44), (int)inputY,
                     (int)inputW, (int)inputH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(g_hInputEdit, SW_SHOW);
    } else if (g_hInputEdit) {
        ShowWindow(g_hInputEdit, SW_HIDE);
    }
}

// ─────────────────────────────────────────────────────────────
// SEND MESSAGE
// ─────────────────────────────────────────────────────────────
static void DoSendMessage() {
    if (g_selectedChat < 0) return;
    wchar_t buf[2048] = {};
    if (g_hInputEdit)
        GetWindowTextW(g_hInputEdit, buf, 2047);
    wstring ws(buf);
    if (ws.empty()) return;
    SetWindowTextW(g_hInputEdit, L"");

    string text = WideToUtf8(ws);
    RgChatPreview chat;
    {
        lock_guard<mutex> lk(g_chatsMtx);
        if (g_selectedChat >= (int)g_chats.size()) return;
        chat = g_chats[g_selectedChat];
    }
    string chatId = RgBuildChatId(g_myMobile, chat.contactMobile);

    // Optimistic local add
    RgMessage m;
    m.id           = "pending_" + to_string(NowMs_safe());
    m.chatId       = chatId;
    m.senderMobile = g_myMobile;
    m.senderName   = WideToUtf8(g_myName_w);
    m.text         = text;
    m.timestamp    = NowMs_safe();
    m.timeString   = RgFormatTime(m.timestamp);
    m.isPending    = true;
    {
        lock_guard<mutex> lk(g_msgsMtx);
        g_messages.push_back(m);
        g_scrollToBottom = true;
    }
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);

    // Network send
    if (g_lanMode) {
        lock_guard<mutex> lk(g_lanPeersMtx);
        for (auto& p : g_lanPeers) {
            if (p.mobile == chat.contactMobile) {
                RgNet_LanSendText(p, chatId, text);
                break;
            }
        }
    }
    RgNet_SendText(chatId, text, chat.contactMobile);
}

static long long NowMs_safe() {
    return (long long)GetTickCount64();
}

// ─────────────────────────────────────────────────────────────
// INIT
// ─────────────────────────────────────────────────────────────
void InitRasGramDesktop() {
    // Get logged-in user info from accounts module globals
    extern string g_loggedInUserUid;
    extern wstring g_loggedInEmail;
    extern wstring g_loggedInName;

    // Not logged in — do nothing; DrawRasGramTab will show login prompt
    if (g_loggedInUserUid.empty()) return;

    // Already initialised with the same UID — skip
    if (g_rgInitDone && g_rgInitWithUid == g_loggedInUserUid) return;

    // (Re-)initialise: user just logged in, or switched accounts
    g_rgInitDone    = true;
    g_rgInitWithUid = g_loggedInUserUid;

    // Reset chat state on reinit
    { lock_guard<mutex> lk(g_chatsMtx); g_chats.clear(); }
    { lock_guard<mutex> lk(g_msgsMtx);  g_messages.clear(); }
    g_selectedChat   = -1;
    g_chatListScroll = 0.0f;
    g_msgScroll      = 0.0f;

    CreateInputControls();

    g_myName_w = g_loggedInName;

    // Derive mobile from email prefix — matches Android RasGram's identity scheme.
    // Android uses phone number; desktop falls back to email-prefix so chats
    // created on this device are consistent within the same Firebase project.
    // If the user has a real mobile registered via Android, contacts should still
    // appear via Firestore chat_previews written by the Android app.
    string emailUtf8 = WideToUtf8(g_loggedInEmail);
    size_t atPos = emailUtf8.find('@');
    g_myMobile = (atPos != string::npos) ? emailUtf8.substr(0, atPos) : g_loggedInUserUid;

    RgNet_Init(g_myMobile, WideToUtf8(g_myName_w),
               g_loggedInUserUid, "");
    RgNet_SetOnline(true);

    // Start chat list polling
    RgNet_StopChatListPolling();
    RgNet_StartChatListPolling([](const vector<RgChatPreview>& chats) {
        { lock_guard<mutex> lk(g_chatsMtx); g_chats = chats; }
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
    });
}

// ─────────────────────────────────────────────────────────────
// OPEN CHAT
// ─────────────────────────────────────────────────────────────
static void OpenChat(int idx) {
    g_selectedChat   = idx;
    g_scrollToBottom = true;
    g_msgScroll      = 0.0f;
    {
        lock_guard<mutex> lk(g_msgsMtx);
        g_messages.clear();
    }
    g_lastMsgTs = 0;

    RgNet_StopMessagePolling();
    RepositionControls();
    if (g_hInputEdit) {
        ShowWindow(g_hInputEdit, SW_SHOW);
        SetFocus(g_hInputEdit);
    }

    RgChatPreview chat;
    { lock_guard<mutex> lk(g_chatsMtx);
      if (idx < 0 || idx >= (int)g_chats.size()) return;
      chat = g_chats[idx]; }

    string chatId = RgBuildChatId(g_myMobile, chat.contactMobile);

    // Load last 100 messages
    RgNet_FetchMessages(chatId, [](const vector<RgMessage>& msgs) {
        { lock_guard<mutex> lk(g_msgsMtx); g_messages = msgs; }
        if (!msgs.empty()) g_lastMsgTs = msgs.back().timestamp;
        g_scrollToBottom = true;
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
    });

    // Mark read
    RgNet_MarkRead(chatId, g_myMobile);

    // Poll new messages
    RgNet_StartMessagePolling(chatId, g_lastMsgTs,
        [](const RgMessage& msg) {
            { lock_guard<mutex> lk(g_msgsMtx);
              // Avoid duplicate
              for (auto& m : g_messages)
                  if (m.id == msg.id) return;
              g_messages.push_back(msg);
              g_lastMsgTs = msg.timestamp; }
            g_scrollToBottom = true;
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        });
}

// ─────────────────────────────────────────────────────────────
// DRAW: TOP BAR
// ─────────────────────────────────────────────────────────────
static void DrawTopBar(Graphics& g,
                       float tx, float ty, float tw, float th,
                       const FontFamily& ff, const FontFamily& ffIc) {
    // Background
    SolidBrush bBg(Color(255, 36, 47, 62));   // Telegram dark header color
    g.FillRectangle(&bBg, tx, ty, tw, th);

    // "RasGram" title in center
    Font fTitle(&ff, 15, FontStyleBold, UnitPixel);
    SolidBrush bWhite(Color(255, 255, 255, 255));
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"RasGram", -1, &fTitle,
                 RectF(tx, ty, tw, th), &fmtC, &bWhite);

    // Settings icon (top right)
    Font fIc(&ffIc, 18, FontStyleRegular, UnitPixel);
    SolidBrush bIcGray(Color(200, 255, 255, 255));
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);
    float btnSz = 38.0f;
    // Settings gear
    float settX = tx + tw - btnSz - 4;
    if (g_hovSettings) {
        SolidBrush bHov(Color(40, 255, 255, 255));
        g.FillEllipse(&bHov, settX, ty + (th-btnSz)/2, btnSz, btnSz);
    }
    g.DrawString(L"\xE713", -1, &fIc,
                 RectF(settX, ty, btnSz, th), &fmtC, &bIcGray);

    // LAN toggle button
    float lanX = settX - btnSz - 4;
    if (g_hovLanToggle) {
        SolidBrush bHov(Color(40, 255, 255, 255));
        g.FillEllipse(&bHov, lanX, ty + (th-btnSz)/2, btnSz, btnSz);
    }
    SolidBrush bLan(g_lanMode ? Color(255, 100, 220, 100) : Color(200, 255, 255, 255));
    Font fLanIc(&ffIc, 16, FontStyleRegular, UnitPixel);
    g.DrawString(L"\xEC06", -1, &fLanIc,    // Network icon
                 RectF(lanX, ty, btnSz, th), &fmtC, &bLan);

    // "DESKTOP" badge (small)
    Font fSmall(&ff, 10, FontStyleBold, UnitPixel);
    SolidBrush bTeal(Color(255, 0, 188, 212));
    g.DrawString(L"DESKTOP", -1, &fSmall,
                 RectF(tx + tw/2 + 45, ty + th/2 - 8, 70, 16),
                 &fmtL, &bTeal);
}

// ─────────────────────────────────────────────────────────────
// DRAW: CONTACT/CHAT LIST (left panel)
// ─────────────────────────────────────────────────────────────
static void DrawChatList(Graphics& g,
                         float lx, float ly, float lw, float lh,
                         const FontFamily& ff, const FontFamily& ffIc) {
    // Panel background
    SolidBrush bPanel(Color(255, 255, 255, 255));
    g.FillRectangle(&bPanel, lx, ly, lw, lh);

    // Right border
    Pen pBrd(Color(255, 220, 222, 225), 1.0f);
    g.DrawLine(&pBrd, lx+lw, ly, lx+lw, ly+lh);

    // Search box area (Win32 EDIT control draws itself — just draw background)
    float searchH = 50.0f;
    SolidBrush bSearchBg(Color(255, 247, 248, 250));
    g.FillRectangle(&bSearchBg, lx, ly, lw, searchH);
    // Search icon behind the Edit control
    Font fIcSm(&ffIc, 14, FontStyleRegular, UnitPixel);
    SolidBrush bGray(Color(255, 160, 160, 160));
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);

    // Chat rows
    Font fName(&ff, 13, FontStyleBold, UnitPixel);
    Font fMsg (&ff, 12, FontStyleRegular, UnitPixel);
    Font fTime(&ff, 10, FontStyleRegular, UnitPixel);
    Font fBadge(&ff, 10, FontStyleBold, UnitPixel);

    SolidBrush bDark(Color(255, 30, 30, 30));
    SolidBrush bGrayText(Color(255, 140, 140, 140));
    SolidBrush bTeal(Color(255, 0, 150, 160));
    SolidBrush bActive(Color(255, 37, 211, 102));  // WhatsApp-style green for active

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);
    fmtR.SetLineAlignment(StringAlignmentNear);

    float rowH  = 64.0f;
    float startY = ly + searchH;
    float clipH  = lh - searchH;

    Region oldClip;
    g.GetClip(&oldClip);
    g.SetClip(RectF(lx, startY, lw, clipH));

    vector<RgChatPreview> chats;
    { lock_guard<mutex> lk(g_chatsMtx); chats = g_chats; }

    wstring searchFilter = g_searchText;
    // Filter by search
    if (!searchFilter.empty()) {
        vector<RgChatPreview> filtered;
        for (auto& c : chats) {
            wstring name = Utf8ToWide(c.contactName);
            wstring mobile = Utf8ToWide(c.contactMobile);
            wstring q = searchFilter;
            transform(name.begin(), name.end(), name.begin(), ::towlower);
            transform(q.begin(), q.end(), q.begin(), ::towlower);
            if (name.find(q) != wstring::npos || mobile.find(q) != wstring::npos)
                filtered.push_back(c);
        }
        chats = filtered;
    }

    for (int i = 0; i < (int)chats.size(); i++) {
        float ry = startY + i * rowH - g_chatListScroll;
        if (ry + rowH < startY || ry > startY + clipH) continue;

        bool isSelected = (g_selectedChat == i && searchFilter.empty());
        bool isHovered  = (g_hoveredChat  == i && !isSelected);

        // Row background
        if (isSelected) {
            SolidBrush bSel(Color(255, 227, 242, 253));
            g.FillRectangle(&bSel, lx, ry, lw, rowH);
            // Teal left accent
            SolidBrush bAccent(Color(255, 0, 150, 160));
            g.FillRectangle(&bAccent, lx, ry, 3.0f, rowH);
        } else if (isHovered) {
            SolidBrush bHov(Color(255, 245, 246, 248));
            g.FillRectangle(&bHov, lx, ry, lw, rowH);
        }

        // Avatar
        float avR = 44.0f;
        float avX = lx + 10, avY = ry + (rowH - avR) / 2;
        DrawAvatar(g, avX, avY, avR,
                   chats[i].contactName,
                   AvatarColor(chats[i].contactMobile));

        // Online dot
        // (simplified — show green dot if lanMode and peer present)
        bool isOnline = false;
        if (g_lanMode) {
            lock_guard<mutex> lk(g_lanPeersMtx);
            for (auto& p : g_lanPeers)
                if (p.mobile == chats[i].contactMobile) { isOnline = true; break; }
        }
        if (isOnline) {
            SolidBrush bOnline(Color(255, 76, 175, 80));
            Pen pWhite(Color(255, 255, 255, 255), 2.0f);
            float dotR = 10.0f;
            float dotX = avX + avR - dotR, dotY = avY + avR - dotR;
            g.FillEllipse(&bOnline, dotX, dotY, dotR, dotR);
            g.DrawEllipse(&pWhite,  dotX, dotY, dotR, dotR);
        }

        float textX  = avX + avR + 8;
        float textW  = lw - textX + lx - 10;

        // Name
        wstring name = Utf8ToWide(chats[i].contactName);
        if (name.empty()) name = Utf8ToWide(chats[i].contactMobile);
        g.DrawString(TruncateW(name, 22).c_str(), -1, &fName,
                     RectF(textX, ry + 10, textW - 50, 18), &fmtL, &bDark);

        // Timestamp (top right)
        wstring ts = Utf8ToWide(chats[i].lastTimeString);
        SolidBrush* tsBr = chats[i].unreadCount > 0 ? &bTeal : &bGrayText;
        g.DrawString(ts.c_str(), -1, &fTime,
                     RectF(lx + lw - 55, ry + 10, 50, 18), &fmtR, tsBr);

        // Last message
        wstring lastMsg = Utf8ToWide(chats[i].lastMessageText);
        if (chats[i].lastIsCallLog) lastMsg = L"📞 " + lastMsg;
        else if (!chats[i].lastFileType.empty()) {
            if (chats[i].lastFileType.find("image") != string::npos) lastMsg = L"📷 Photo";
            else if (chats[i].lastFileType.find("audio") != string::npos) lastMsg = L"🎙 Voice";
            else if (chats[i].lastFileType.find("video") != string::npos) lastMsg = L"🎬 Video";
            else lastMsg = L"📎 File";
        }
        g.DrawString(TruncateW(lastMsg, 30).c_str(), -1, &fMsg,
                     RectF(textX, ry + 32, textW - 24, 16), &fmtL, &bGrayText);

        // Unread badge
        if (chats[i].unreadCount > 0) {
            float bW = 22, bH = 22;
            float bX = lx + lw - bW - 8, bY = ry + rowH - bH - 8;
            SolidBrush bTealBr(Color(255, 0, 150, 160));
            g.FillEllipse(&bTealBr, bX, bY, bW, bH);
            wstring cnt = to_wstring(chats[i].unreadCount > 99 ? 99 : chats[i].unreadCount);
            SolidBrush bWh(Color(255,255,255,255));
            g.DrawString(cnt.c_str(), -1, &fBadge,
                         RectF(bX, bY, bW, bH), &fmtC, &bWh);
        }

        // Divider
        if (i < (int)chats.size()-1) {
            Pen pDiv(Color(60, 0, 0, 0), 0.5f);
            g.DrawLine(&pDiv, textX, ry + rowH - 1, lx + lw, ry + rowH - 1);
        }
    }

    // Empty state
    if (chats.empty()) {
        Font fEm(&ff, 13, FontStyleRegular, UnitPixel);
        SolidBrush bEm(Color(255, 180, 180, 180));
        g.DrawString(L"No chats yet.\nContacts appear here\nafter first message.",
                     -1, &fEm,
                     RectF(lx + 20, startY + clipH/2 - 40, lw-40, 80),
                     &fmtC, &bEm);
    }

    g.SetClip(&oldClip);
}

// ─────────────────────────────────────────────────────────────
// DRAW: CHAT HEADER (top of right panel)
// ─────────────────────────────────────────────────────────────
static void DrawChatHeader(Graphics& g,
                            float hx, float hy, float hw, float hh,
                            const RgChatPreview& chat,
                            const FontFamily& ff, const FontFamily& ffIc) {
    SolidBrush bHdrBg(Color(255, 255, 255, 255));
    g.FillRectangle(&bHdrBg, hx, hy, hw, hh);
    Pen pBrd(Color(255, 220, 222, 225), 1.0f);
    g.DrawLine(&pBrd, hx, hy+hh, hx+hw, hy+hh);

    // Avatar
    float avR = 36.0f;
    DrawAvatar(g, hx + 12, hy + (hh-avR)/2, avR,
               chat.contactName, AvatarColor(chat.contactMobile));

    // Name + status
    Font fName(&ff, 14, FontStyleBold, UnitPixel);
    Font fStatus(&ff, 11, FontStyleRegular, UnitPixel);
    SolidBrush bDark(Color(255, 30, 30, 30));
    SolidBrush bGreen(Color(255, 76, 175, 80));
    SolidBrush bGray(Color(255, 140, 140, 140));
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentCenter);

    wstring name = Utf8ToWide(chat.contactName);
    if (name.empty()) name = Utf8ToWide(chat.contactMobile);
    g.DrawString(name.c_str(), -1, &fName,
                 RectF(hx+12+avR+8, hy+6, hw-200.0f, 20.0f), &fmtL, &bDark);

    // Online status
    bool isLanOnline = false;
    { lock_guard<mutex> lk(g_lanPeersMtx);
      for (auto& p : g_lanPeers)
          if (p.mobile == chat.contactMobile) { isLanOnline = true; break; } }
    g.DrawString(isLanOnline ? L"● online (LAN)" : L"tap to view profile",
                 -1, &fStatus,
                 RectF(hx+12+avR+8, hy+26, hw-200.0f, 16.0f),
                 &fmtL, isLanOnline ? &bGreen : &bGray);

    // Call buttons (top right)
    Font fIc(&ffIc, 20, FontStyleRegular, UnitPixel);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    float btnSz = 40.0f;

    // Video call button
    float vcX = hx + hw - btnSz - 8;
    if (g_hovCallVideo) {
        SolidBrush bH(Color(30, 0, 150, 160));
        g.FillEllipse(&bH, vcX, hy+(hh-btnSz)/2, btnSz, btnSz);
    }
    SolidBrush bTealIc(Color(255, 0, 150, 160));
    g.DrawString(L"\xE714", -1, &fIc,    // Video camera icon
                 RectF(vcX, hy, btnSz, hh), &fmtC, &bTealIc);

    // Audio call button
    float acX = vcX - btnSz - 4;
    if (g_hovCallAudio) {
        SolidBrush bH(Color(30, 0, 150, 160));
        g.FillEllipse(&bH, acX, hy+(hh-btnSz)/2, btnSz, btnSz);
    }
    g.DrawString(L"\xE717", -1, &fIc,    // Phone icon
                 RectF(acX, hy, btnSz, hh), &fmtC, &bTealIc);
}

// ─────────────────────────────────────────────────────────────
// DRAW: MESSAGE BUBBLES
// ─────────────────────────────────────────────────────────────
static void DrawMessages(Graphics& g,
                         float mx, float my, float mw, float mh,
                         const FontFamily& ff, const FontFamily& ffIc) {
    SolidBrush bBg(Color(255, 234, 238, 243));  // Telegram-ish light blue-grey
    g.FillRectangle(&bBg, mx, my, mw, mh);

    vector<RgMessage> msgs;
    { lock_guard<mutex> lk(g_msgsMtx); msgs = g_messages; }

    Font fMsg (&ff, 13, FontStyleRegular, UnitPixel);
    Font fTime (&ff, 10, FontStyleRegular, UnitPixel);
    Font fIcSm (&ffIc, 16, FontStyleRegular, UnitPixel);
    Font fSystem(&ff, 11, FontStyleItalic, UnitPixel);

    SolidBrush bMsgOut    (Color(255, 220, 248, 198));   // sent: light green
    SolidBrush bMsgIn     (Color(255, 255, 255, 255));   // received: white
    SolidBrush bTextDark  (Color(255,  30,  30,  30));
    SolidBrush bTextGray  (Color(255, 140, 140, 140));
    SolidBrush bCallLog   (Color(255, 230, 240, 255));
    Pen pBubble(Color(100, 180, 180, 180), 0.5f);

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);
    fmtL.SetLineAlignment(StringAlignmentNear);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);
    fmtR.SetLineAlignment(StringAlignmentNear);
    StringFormat fmtLC; fmtLC.SetAlignment(StringAlignmentNear);
    fmtLC.SetLineAlignment(StringAlignmentCenter);

    // Estimate total height for scroll
    float rowEstH = 52.0f;
    float totalH  = msgs.size() * rowEstH;
    if (g_scrollToBottom) {
        g_msgScroll = max(0.0f, totalH - mh + 20.0f);
        g_scrollToBottom = false;
    }

    // Clip to message area
    Region oldClip;
    g.GetClip(&oldClip);
    g.SetClip(RectF(mx, my, mw, mh));

    float bubbleMaxW = mw * 0.65f;
    float curY = my - g_msgScroll + 10.0f;
    string lastDate;

    for (auto& msg : msgs) {
        if (msg.isDeleted) continue;

        bool isMine = (msg.senderMobile == g_myMobile);

        // Date separator
        {
            // Format date from timestamp
            time_t t = (time_t)(msg.timestamp / 1000);
            struct tm* tm_info = localtime(&t);
            char dateBuf[32] = {};
            if (tm_info) strftime(dateBuf, sizeof(dateBuf), "%d %b %Y", tm_info);
            string thisDate(dateBuf);
            if (thisDate != lastDate && !thisDate.empty()) {
                lastDate = thisDate;
                // Draw date chip
                wstring wdate(thisDate.begin(), thisDate.end());
                SolidBrush bDateBg(Color(180, 200, 200, 200));
                Font fDate(&ff, 11, FontStyleRegular, UnitPixel);
                SolidBrush bDateTxt(Color(255, 80, 80, 80));
                StringFormat fmtDate;
                fmtDate.SetAlignment(StringAlignmentCenter);
                fmtDate.SetLineAlignment(StringAlignmentCenter);
                float chipW = 120, chipH = 22;
                float chipX = mx + (mw - chipW) / 2;
                RgRoundRect(g, &bDateBg, nullptr, chipX, curY, chipW, chipH, 11.0f);
                g.DrawString(wdate.c_str(), -1, &fDate,
                             RectF(chipX, curY, chipW, chipH), &fmtDate, &bDateTxt);
                curY += chipH + 6.0f;
            }
        }

        // Call log bubble
        if (msg.isCallLog) {
            wstring callTxt = L"📞 ";
            if (msg.callType == "video") callTxt = L"📹 ";
            callTxt += Utf8ToWide(msg.callStatus == "missed" ? "Missed Call" :
                                  msg.callStatus == "answered" ? "Call ended" : "Call");
            float chipW = 200, chipH = 30;
            float chipX = mx + (mw - chipW) / 2;
            RgRoundRect(g, &bCallLog, &pBubble, chipX, curY, chipW, chipH, 15.0f);
            StringFormat fmtCC; fmtCC.SetAlignment(StringAlignmentCenter);
            fmtCC.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(callTxt.c_str(), -1, &fMsg,
                         RectF(chipX, curY, chipW, chipH), &fmtCC, &bTextDark);
            curY += chipH + 8.0f;
            continue;
        }

        // Text bubble
        wstring text = Utf8ToWide(msg.text);
        if (text.empty() && !msg.fileName.empty())
            text = L"📎 " + Utf8ToWide(msg.fileName);
        if (text.empty()) continue;

        // Measure text (rough)
        float lineH    = 18.0f;
        int   charsPerLine = (int)(bubbleMaxW / 7.5f);
        int   lines    = max(1, (int)(text.size() / max(1, charsPerLine)) + 1);
        float bubH     = lines * lineH + 30.0f;
        float bubW     = min(bubbleMaxW,
                             (float)text.size() * 7.5f + 30.0f);
        bubW = max(bubW, 80.0f);

        float bx = isMine ? (mx + mw - bubW - 10) : (mx + 10);
        float by = curY;

        // Draw bubble
        SolidBrush& bBubBg = isMine ? bMsgOut : bMsgIn;
        RgRoundRect(g, &bBubBg, nullptr, bx, by, bubW, bubH, 10.0f);

        // Pending indicator
        if (msg.isPending) {
            SolidBrush bPend(Color(180, 200, 200, 200));
            RgRoundRect(g, &bPend, nullptr, bx, by, bubW, bubH, 10.0f);
        }

        // Message text (word wrap via StringFormat)
        StringFormat fmtWrap;
        fmtWrap.SetAlignment(StringAlignmentNear);
        fmtWrap.SetLineAlignment(StringAlignmentNear);
        fmtWrap.SetFormatFlags(0); // word wrap
        g.DrawString(text.c_str(), -1, &fMsg,
                     RectF(bx + 8, by + 8, bubW - 16, bubH - 26),
                     &fmtWrap, &bTextDark);

        // Time + read receipt (bottom right of bubble)
        wstring ts = Utf8ToWide(msg.timeString);
        if (isMine) {
            ts += msg.read ? L" ✓✓" : L" ✓";
        }
        g.DrawString(ts.c_str(), -1, &fTime,
                     RectF(bx, by + bubH - 18, bubW - 6, 16),
                     &fmtR, &bTextGray);

        curY += bubH + 6.0f;
    }

    g.SetClip(&oldClip);
}

// ─────────────────────────────────────────────────────────────
// DRAW: INPUT BAR (bottom of right panel)
// ─────────────────────────────────────────────────────────────
static void DrawInputBar(Graphics& g,
                         float ix, float iy, float iw, float ih,
                         const FontFamily& ff, const FontFamily& ffIc) {
    SolidBrush bBg(Color(255, 255, 255, 255));
    g.FillRectangle(&bBg, ix, iy, iw, ih);
    Pen pTop(Color(255, 220, 222, 225), 1.0f);
    g.DrawLine(&pTop, ix, iy, ix+iw, iy);

    float btnSz = 38.0f;
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    Font fIc(&ffIc, 20, FontStyleRegular, UnitPixel);

    // Attach button (left)
    {
        SolidBrush bIcGray(g_hovAttach ? Color(255, 0, 150, 160) : Color(255, 140, 140, 140));
        if (g_hovAttach) {
            SolidBrush bH(Color(30, 0, 150, 160));
            g.FillEllipse(&bH, ix + 4, iy + (ih-btnSz)/2, btnSz, btnSz);
        }
        g.DrawString(L"\xE723", -1, &fIc,    // Attach/link icon
                     RectF(ix + 4, iy, btnSz, ih), &fmtC, &bIcGray);
    }

    // (Win32 EDIT control draws itself in the middle)

    // Voice / Send button (right)
    {
        bool hasTxt = (g_hInputEdit && GetWindowTextLengthW(g_hInputEdit) > 0);
        wstring btnIcon = hasTxt ? L"\xE724" : L"\xEF61"; // Send or Mic
        SolidBrush bSendBg(hasTxt ? Color(255, 0, 150, 160) : Color(255, 140, 140, 140));
        float sendX = ix + iw - btnSz - 4;
        if (g_hovSend) {
            SolidBrush bH(Color(255, 0, 130, 140));
            g.FillEllipse(&bH, sendX, iy + (ih-btnSz)/2, btnSz, btnSz);
        } else if (hasTxt) {
            g.FillEllipse(&bSendBg, sendX, iy + (ih-btnSz)/2, btnSz, btnSz);
        }
        SolidBrush bSendIc(hasTxt ? Color(255,255,255,255) : Color(255,140,140,140));
        g.DrawString(btnIcon.c_str(), -1, &fIc,
                     RectF(sendX, iy, btnSz, ih), &fmtC, &bSendIc);
    }
}

// ─────────────────────────────────────────────────────────────
// DRAW: EMPTY STATE (no chat selected)
// ─────────────────────────────────────────────────────────────
static void DrawEmptyState(Graphics& g,
                           float ex, float ey, float ew, float eh,
                           const FontFamily& ff) {
    SolidBrush bBg(Color(255, 240, 242, 245));
    g.FillRectangle(&bBg, ex, ey, ew, eh);

    Font fBig(&ff, 22, FontStyleBold, UnitPixel);
    Font fSub(&ff, 14, FontStyleRegular, UnitPixel);
    SolidBrush bDark(Color(255, 60, 60, 60));
    SolidBrush bGray(Color(255, 160, 160, 160));
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);

    g.DrawString(L"RasGram Desktop", -1, &fBig,
                 RectF(ex, ey + eh/2 - 60, ew, 36), &fmtC, &bDark);
    g.DrawString(
        L"Select a chat to start messaging.\n"
        L"Audio & Video calls supported.\n"
        L"Same account as your RasGram Android app.",
        -1, &fSub,
        RectF(ex + 30, ey + eh/2 - 20, ew - 60, 80), &fmtC, &bGray);
}

// ─────────────────────────────────────────────────────────────
// MAIN DRAW
// ─────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────
// DRAW: NOT LOGGED IN SCREEN
// ─────────────────────────────────────────────────────────────
static void DrawNotLoggedInScreen(Graphics& g,
                                  float cx, float cy, float cw, float ch,
                                  const FontFamily& ff, const FontFamily& ffIc) {
    // Background — same dark header as top bar
    SolidBrush bBg(Color(255, 240, 242, 245));
    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // RasGram icon (chat bubble icon from Segoe MDL2)
    Font fIcBig(&ffIc, 56, FontStyleRegular, UnitPixel);
    SolidBrush bTeal(Color(255, 0, 150, 160));
    StringFormat fmtC;
    fmtC.SetAlignment(StringAlignmentCenter);
    fmtC.SetLineAlignment(StringAlignmentCenter);
    float iconY = cy + ch / 2.0f - 100.0f;
    g.DrawString(L"\xE8BD", -1, &fIcBig,
                 RectF(cx, iconY, cw, 70), &fmtC, &bTeal);

    // "RasGram Desktop" title
    Font fBig(&ff, 24, FontStyleBold, UnitPixel);
    SolidBrush bDark(Color(255, 40, 40, 40));
    g.DrawString(L"RasGram Desktop", -1, &fBig,
                 RectF(cx, iconY + 76, cw, 34), &fmtC, &bDark);

    // Sub-message
    Font fSub(&ff, 14, FontStyleRegular, UnitPixel);
    SolidBrush bGray(Color(255, 120, 120, 120));
    g.DrawString(
        L"You are not logged in.\n"
        L"Please go to the \u0022My Account\u0022 tab and log in\n"
        L"to use RasGram Desktop.",
        -1, &fSub,
        RectF(cx + 40, iconY + 118, cw - 80, 80),
        &fmtC, &bGray);

    // Arrow hint
    Font fHint(&ff, 12, FontStyleItalic, UnitPixel);
    SolidBrush bHint(Color(255, 170, 170, 170));
    g.DrawString(L"Tip: Use the sidebar \u2192 My Account to sign in.",
                 -1, &fHint,
                 RectF(cx + 40, iconY + 210, cw - 80, 24),
                 &fmtC, &bHint);
}

void DrawRasGramTab(Graphics& g, float cx, float cy, float cw, float ch) {
    g_cx = cx; g_cy = cy; g_cw = cw; g_ch = ch;

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    FontFamily ff  (L"Segoe UI");
    FontFamily ffIc(L"Segoe MDL2 Assets");

    // ── Login check ──────────────────────────────────────────
    // g_loggedInUserUid is set by accounts.cpp after successful login.
    // Show a friendly prompt instead of a broken chat UI.
    extern string g_loggedInUserUid;
    if (g_loggedInUserUid.empty()) {
        DrawNotLoggedInScreen(g, cx, cy, cw, ch, ff, ffIc);
        return;
    }

    // (Re-)initialise if this is the first draw after login
    InitRasGramDesktop();

    // 1. Top bar
    DrawTopBar(g, cx, cy, cw, g_topBarH, ff, ffIc);

    float bodyY = cy + g_topBarH;
    float bodyH = ch - g_topBarH;

    // 2. Chat list (left panel includes search bar)
    DrawChatList(g, cx, bodyY, g_listW, bodyH, ff, ffIc);

    // 3. Right panel
    float rx = cx + g_listW;
    float rw = cw - g_listW;

    vector<RgChatPreview> chats;
    { lock_guard<mutex> lk(g_chatsMtx); chats = g_chats; }

    if (g_selectedChat >= 0 && g_selectedChat < (int)chats.size()) {
        const RgChatPreview& chat = chats[g_selectedChat];

        // Chat header
        float chatHdrH = 58.0f;
        DrawChatHeader(g, rx, bodyY, rw, chatHdrH, chat, ff, ffIc);

        // Messages area
        float inputBarH = 56.0f;
        float msgY = bodyY + chatHdrH;
        float msgH = bodyH - chatHdrH - inputBarH;
        DrawMessages(g, rx, msgY, rw, msgH, ff, ffIc);

        // Input bar
        DrawInputBar(g, rx, msgY + msgH, rw, inputBarH, ff, ffIc);
    } else {
        // No chat selected
        DrawEmptyState(g, rx, bodyY, rw, bodyH, ff);
    }

    // Reposition Win32 edit controls
    RepositionControls();
}

// ─────────────────────────────────────────────────────────────
// MOUSE MOVE
// ─────────────────────────────────────────────────────────────
void ProcessRasGramMouseMove(float x, float y) {
    bool changed = false;

    auto Set = [&](bool& v, bool nv) { if (v != nv) { v = nv; changed = true; } };

    float bodyY   = g_cy + g_topBarH;
    float bodyH   = g_ch - g_topBarH;

    // Top bar buttons
    float btnSz = 38.0f;
    float settX = g_cx + g_cw - btnSz - 4;
    float lanX  = settX - btnSz - 4;

    Set(g_hovSettings,  (y >= g_cy && y <= g_cy+g_topBarH &&
                          x >= settX && x <= settX+btnSz));
    Set(g_hovLanToggle, (y >= g_cy && y <= g_cy+g_topBarH &&
                          x >= lanX  && x <= lanX +btnSz));

    // Chat list hover
    float searchH = 50.0f;
    float startY  = bodyY + searchH;
    float rowH    = 64.0f;
    int oldHov    = g_hoveredChat;
    g_hoveredChat = -1;

    vector<RgChatPreview> chats;
    { lock_guard<mutex> lk(g_chatsMtx); chats = g_chats; }
    if (x >= g_cx && x <= g_cx + g_listW) {
        for (int i = 0; i < (int)chats.size(); i++) {
            float ry = startY + i * rowH - g_chatListScroll;
            if (y >= ry && y < ry + rowH) { g_hoveredChat = i; break; }
        }
    }
    if (oldHov != g_hoveredChat) changed = true;

    // Chat header buttons
    if (g_selectedChat >= 0 && g_selectedChat < (int)chats.size()) {
        float chatHdrH = 58.0f;
        float rx = g_cx + g_listW;
        float rw = g_cw - g_listW;
        btnSz = 40.0f;
        float vcX = rx + rw - btnSz - 8;
        float acX = vcX - btnSz - 4;
        Set(g_hovCallVideo, (y >= bodyY && y <= bodyY+chatHdrH &&
                              x >= vcX && x <= vcX+btnSz));
        Set(g_hovCallAudio, (y >= bodyY && y <= bodyY+chatHdrH &&
                              x >= acX && x <= acX+btnSz));

        // Input bar
        float inputBarH = 56.0f;
        float inputY    = g_cy + g_ch - inputBarH;
        float sendX     = rx + rw - 42;
        float attachX   = rx + 4;
        Set(g_hovSend,   (y >= inputY && y <= inputY+inputBarH &&
                           x >= sendX && x <= sendX+38));
        Set(g_hovAttach, (y >= inputY && y <= inputY+inputBarH &&
                           x >= attachX && x <= attachX+38));
    }

    if (changed && hParentWnd)
        InvalidateRect(hParentWnd, NULL, FALSE);
}

// ─────────────────────────────────────────────────────────────
// MOUSE CLICK
// ─────────────────────────────────────────────────────────────
void ProcessRasGramMouseClick(float x, float y) {
    float bodyY   = g_cy + g_topBarH;

    // Top bar
    if (y >= g_cy && y <= g_cy + g_topBarH) {
        if (g_hovLanToggle) {
            g_lanMode = !g_lanMode;
            if (g_lanMode) {
                RgNet_StartLan(
                    [](const vector<RgLanPeer>& peers) {
                        { lock_guard<mutex> lk(g_lanPeersMtx); g_lanPeers = peers; }
                        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                    },
                    [](const RgMessage& msg) {
                        { lock_guard<mutex> lk(g_msgsMtx); g_messages.push_back(msg); }
                        g_scrollToBottom = true;
                        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                    });
            } else {
                RgNet_StopLan();
                { lock_guard<mutex> lk(g_lanPeersMtx); g_lanPeers.clear(); }
            }
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        }
        return;
    }

    // Chat list
    if (x >= g_cx && x <= g_cx + g_listW) {
        float searchH = 50.0f;
        float startY  = bodyY + searchH;
        float rowH    = 64.0f;
        vector<RgChatPreview> chats;
        { lock_guard<mutex> lk(g_chatsMtx); chats = g_chats; }
        for (int i = 0; i < (int)chats.size(); i++) {
            float ry = startY + i * rowH - g_chatListScroll;
            if (y >= ry && y < ry + rowH) {
                OpenChat(i);
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
        }
        return;
    }

    // Right panel
    vector<RgChatPreview> chats;
    { lock_guard<mutex> lk(g_chatsMtx); chats = g_chats; }
    if (g_selectedChat < 0 || g_selectedChat >= (int)chats.size()) return;
    const RgChatPreview& chat = chats[g_selectedChat];

    float chatHdrH  = 58.0f;
    float inputBarH = 56.0f;
    float rx        = g_cx + g_listW;
    float rw        = g_cw - g_listW;
    float inputY    = g_cy + g_ch - inputBarH;

    // Chat header — call buttons
    if (y >= bodyY && y <= bodyY + chatHdrH) {
        float btnSz = 40.0f;
        float vcX = rx + rw - btnSz - 8;
        float acX = vcX - btnSz - 4;
        if (g_hovCallVideo || g_hovCallAudio) {
            if (!RgCall_IsActive()) {
                // Find peer IP for LAN call
                string peerIp;
                { lock_guard<mutex> lk(g_lanPeersMtx);
                  for (auto& p : g_lanPeers)
                      if (p.mobile == chat.contactMobile) { peerIp = p.ip; break; } }
                RgCallParams cp;
                cp.chatId      = RgBuildChatId(g_myMobile, chat.contactMobile);
                cp.peerMobile  = chat.contactMobile;
                cp.peerName    = chat.contactName;
                cp.peerIp      = peerIp;
                cp.isVideo     = g_hovCallVideo;
                cp.isLan       = !peerIp.empty();
                RgCall_StartOutgoing(cp,
                    [](bool connected) {
                        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                    });
            } else {
                RgCall_Hangup();
            }
        }
        return;
    }

    // Input bar — send button
    if (y >= inputY && y <= inputY + inputBarH) {
        if (g_hovSend) {
            DoSendMessage();
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        } else if (g_hovAttach) {
            // File picker
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
                // Determine MIME type roughly
                wstring ext;
                size_t dot = path.rfind(L'.');
                if (dot != wstring::npos) ext = path.substr(dot);
                string mime = "application/octet-stream";
                if (ext == L".jpg" || ext == L".jpeg") mime = "image/jpeg";
                else if (ext == L".png")               mime = "image/png";
                else if (ext == L".mp3")               mime = "audio/mpeg";
                else if (ext == L".mp4")               mime = "video/mp4";
                else if (ext == L".pdf")               mime = "application/pdf";
                RgNet_SendFile(
                    RgBuildChatId(g_myMobile, chat.contactMobile),
                    chat.contactMobile, path, mime);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
// MOUSE WHEEL — scroll chat list or messages
// ─────────────────────────────────────────────────────────────
void ProcessRasGramMouseWheel(int delta) {
    float scrollAmt = 60.0f;
    // Determine which panel the mouse is likely over (simplified: by last x)
    // We'll just scroll messages if chat selected, else scroll chat list
    if (g_selectedChat >= 0) {
        g_msgScroll -= (delta / 120.0f) * scrollAmt;
        g_msgScroll  = max(0.0f, g_msgScroll);
    } else {
        g_chatListScroll -= (delta / 120.0f) * scrollAmt;
        g_chatListScroll  = max(0.0f, g_chatListScroll);
    }
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
}

// ─────────────────────────────────────────────────────────────
// KEYBOARD — Enter to send
// ─────────────────────────────────────────────────────────────
void ProcessRasGramChar(wchar_t c) {
    // Handled by Win32 EDIT control
}

void ProcessRasGramKeyDown(WPARAM vk) {
    if (vk == VK_RETURN && GetAsyncKeyState(VK_CONTROL) & 0x8000) {
        // Ctrl+Enter = send
        DoSendMessage();
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
    }
}
