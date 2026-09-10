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
static bool g_wvReady = false;

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
    <button id="btn-lan" title="LAN Mode" onclick="RG.toggleLan()">📡</button>
    <div class="logo">RasGram<span class="badge">DESKTOP</span></div>
    <button title="Settings" onclick="RG.openSettings()">⚙️</button>
  </div>

  <!-- BODY -->
  <div id="body">

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
            // Find name
            { lock_guard<mutex> lk(g_chatsMtx);
              for (auto& c:g_chats) if(c.contactMobile==mobile){cp.peerName=c.contactName;break;} }
            RgCall_StartOutgoing(cp, [](bool){});
        } else {
            RgCall_Hangup();
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
        g_rgCtrl = ctl;
        ctl->get_CoreWebView2(&g_rgWV);

        // Background
        ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2))))
            ctl2->put_DefaultBackgroundColor({255,240,242,245});

        ctl->put_Bounds(m_bounds);
        ctl->put_IsVisible(TRUE);

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
    if (g_rgCtrl) g_rgCtrl->put_IsVisible(show ? TRUE : FALSE);
    if (!show) {
        // Hide Win32 edits (none used now — WebView owns input)
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
    }).detach();
}

void DrawRasGramTab(Graphics& g, float cx, float cy, float cw, float ch) {
    g_cx = cx; g_cy = cy; g_cw = cw; g_ch = ch;

    // First draw: create the embedded WebView2
    if (!g_rgCtrl && hParentWnd) {
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
