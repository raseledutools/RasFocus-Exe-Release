// ================================================================
// tab_phone_remote.cpp
// Phone থেকে PC Control — WiFi/Hotspot দিয়ে
//
// Architecture:
//   PC runs a raw HTTP+WebSocket server on port 9222.
//   Phone browser (বা RasFocus Android app) opens:
//       http://<PC-IP>:9222/
//   Server serves an HTML control panel → phone screen এ
//   দেখা যায়।  Phone থেকে command/click পাঠালে PC তে execute
//   হয় এবং result WebSocket দিয়ে phone এ ফেরত আসে।
//
//   Supported commands (JSON over WebSocket):
//     {"type":"shell","cmd":"dir"}        → CMD output
//     {"type":"files","path":"C:\\"}      → file list JSON
//     {"type":"screen"}                   → JPEG base64 frame
//     {"type":"mouse","x":0.5,"y":0.3,   → PC mouse move/click
//              "btn":"left","act":"click"}
//     {"type":"key","vk":13}             → PC keypress
//     {"type":"type","text":"hello"}     → PC keyboard type
// ================================================================

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "tab_phone_remote.h"
#include "globals.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <functional>
#include <map>

// SHA-1 for WebSocket handshake
#include <wincrypt.h>
#pragma comment(lib, "Crypt32.lib")

using namespace Gdiplus;
using namespace std;

// ── State ────────────────────────────────────────────────────────
bool  g_phoneRemoteRunning = false;
int   g_phoneRemotePort    = 9222;
int   g_connectedClients   = 0;

static atomic<bool>  s_serverActive  { false };
static SOCKET        s_listenSock    = INVALID_SOCKET;
static thread        s_serverThread;
static mutex         s_clientsMtx;
static vector<SOCKET> s_wsClients;   // active WebSocket clients

// UI hover state
static bool s_hovStart = false, s_hovStop = false, s_hovCopy = false;

// ── Base64 encode ─────────────────────────────────────────────────
static string Base64Encode(const vector<BYTE>& data) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string out; out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        BYTE b0 = data[i], b1 = (i+1<data.size())?data[i+1]:0, b2 = (i+2<data.size())?data[i+2]:0;
        out += tbl[b0 >> 2];
        out += tbl[((b0&3)<<4)|(b1>>4)];
        out += (i+1<data.size()) ? tbl[((b1&0xF)<<2)|(b2>>6)] : '=';
        out += (i+2<data.size()) ? tbl[b2&0x3F]               : '=';
    }
    return out;
}

// ── SHA-1 via WinCrypt (for WebSocket handshake) ──────────────────
static string SHA1Base64(const string& input) {
    HCRYPTPROV hProv = 0; HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return "";
    if (!CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) { CryptReleaseContext(hProv,0); return ""; }
    CryptHashData(hHash, (const BYTE*)input.c_str(), (DWORD)input.size(), 0);
    BYTE hash[20]; DWORD hashLen = 20;
    CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
    CryptDestroyHash(hHash); CryptReleaseContext(hProv, 0);
    vector<BYTE> v(hash, hash+20);
    return Base64Encode(v);
}

// ── Screen capture → JPEG base64 ─────────────────────────────────
static string CaptureScreenJpegBase64(int quality = 30) {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    // Scale down for bandwidth
    int dw = sw / 2, dh = sh / 2;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem    = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp  = CreateCompatibleBitmap(hdcScreen, dw, dh);
    HBITMAP hOld  = (HBITMAP)SelectObject(hdcMem, hBmp);
    SetStretchBltMode(hdcMem, HALFTONE);
    StretchBlt(hdcMem, 0, 0, dw, dh, hdcScreen, 0, 0, sw, sh, SRCCOPY);
    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    // GDI+ encode to JPEG in memory
    Bitmap bmp(hBmp, NULL);
    DeleteObject(hBmp);

    CLSID jpegClsid;
    {
        UINT num=0,size=0;
        GetImageEncodersSize(&num,&size);
        vector<BYTE> buf(size);
        GetImageEncoders(num,size,(ImageCodecInfo*)buf.data());
        for(UINT i=0;i<num;i++){
            if(wcscmp(((ImageCodecInfo*)buf.data())[i].MimeType,L"image/jpeg")==0){
                jpegClsid=((ImageCodecInfo*)buf.data())[i].Clsid; break;
            }
        }
    }
    EncoderParameters ep; ep.Count=1;
    ep.Parameter[0].Guid=EncoderQuality;
    ep.Parameter[0].Type=EncoderParameterValueTypeLong;
    ep.Parameter[0].NumberOfValues=1;
    ULONG q=(ULONG)quality;
    ep.Parameter[0].Value=&q;

    IStream* pStream=NULL; CreateStreamOnHGlobal(NULL,TRUE,&pStream);
    bmp.Save(pStream, &jpegClsid, &ep);

    STATSTG st; pStream->Stat(&st,STATFLAG_NONAME);
    ULONG len=(ULONG)st.cbSize.QuadPart;
    LARGE_INTEGER li; li.QuadPart=0; pStream->Seek(li,STREAM_SEEK_SET,NULL);
    vector<BYTE> imgData(len);
    pStream->Read(imgData.data(),len,NULL);
    pStream->Release();

    return Base64Encode(imgData);
}

// ── File list as JSON ─────────────────────────────────────────────
static string GetFileListJson(const string& path) {
    string json = "[";
    bool first = true;
    WIN32_FIND_DATAA fd;
    string pattern = path + "\\*";
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return "[]";
    do {
        string name = fd.cFileName;
        if (name=="."||name=="..") continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        ULONGLONG sz = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (!first) json += ",";
        json += "{\"name\":\""; 
        // escape backslashes and quotes
        for (char c : name) { if(c=='"') json+="\\\""; else json+=c; }
        json += "\",\"dir\":"; json += isDir?"true":"false";
        json += ",\"size\":"; json += to_string(sz);
        json += "}";
        first = false;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return json + "]";
}

// ── Run CMD command, capture output ──────────────────────────────
static string RunShellCommand(const string& cmd) {
    string result;
    string fullCmd = "cmd.exe /c \"" + cmd + "\" 2>&1";
    SECURITY_ATTRIBUTES sa{sizeof(sa),NULL,TRUE};
    HANDLE hR,hW;
    if (!CreatePipe(&hR,&hW,&sa,0)) return "pipe error";
    STARTUPINFOA si{}; si.cb=sizeof(si);
    si.hStdOutput=hW; si.hStdError=hW;
    si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi{};
    vector<char> buf(fullCmd.begin(),fullCmd.end()); buf.push_back(0);
    if (CreateProcessA(NULL,buf.data(),NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) {
        CloseHandle(hW);
        char tmp[1024]; DWORD rd;
        while (ReadFile(hR,tmp,sizeof(tmp)-1,&rd,NULL)&&rd>0) { tmp[rd]=0; result+=tmp; }
        WaitForSingleObject(pi.hProcess,5000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    } else { CloseHandle(hW); result="exec failed"; }
    CloseHandle(hR);
    // Trim to 8KB
    if (result.size()>8192) result=result.substr(0,8192)+"...(truncated)";
    return result;
}

// ── Simple JSON field extractor ───────────────────────────────────
static string JsonGet(const string& json, const string& key) {
    string search = "\"" + key + "\"";
    size_t p = json.find(search);
    if (p==string::npos) return "";
    p = json.find(':',p+search.size());
    if (p==string::npos) return "";
    p++;
    while (p<json.size()&&(json[p]==' '||json[p]=='\t')) p++;
    if (p>=json.size()) return "";
    if (json[p]=='"') {
        size_t e=json.find('"',p+1);
        if (e==string::npos) return "";
        return json.substr(p+1,e-p-1);
    }
    size_t e=p;
    while(e<json.size()&&json[e]!=','&&json[e]!='}'&&json[e]!=']') e++;
    string v=json.substr(p,e-p);
    while(!v.empty()&&(v.back()==' '||v.back()=='\r'||v.back()=='\n')) v.pop_back();
    return v;
}

// ── WebSocket frame send ──────────────────────────────────────────
static void WsSend(SOCKET s, const string& text) {
    size_t len = text.size();
    vector<BYTE> frame;
    frame.push_back(0x81); // FIN + text opcode
    if (len < 126) {
        frame.push_back((BYTE)len);
    } else if (len < 65536) {
        frame.push_back(126);
        frame.push_back((BYTE)((len>>8)&0xFF));
        frame.push_back((BYTE)(len&0xFF));
    } else {
        frame.push_back(127);
        for(int i=7;i>=0;i--) frame.push_back((BYTE)((len>>(8*i))&0xFF));
    }
    frame.insert(frame.end(), text.begin(), text.end());
    send(s, (char*)frame.data(), (int)frame.size(), 0);
}

// ── WebSocket frame receive ───────────────────────────────────────
static string WsRecv(SOCKET s) {
    BYTE hdr[2]; if (recv(s,(char*)hdr,2,MSG_WAITALL)!=2) return "";
    bool masked = (hdr[1]&0x80)!=0;
    size_t len = hdr[1]&0x7F;
    if (len==126) {
        BYTE ext[2]; recv(s,(char*)ext,2,MSG_WAITALL);
        len = ((size_t)ext[0]<<8)|ext[1];
    } else if (len==127) {
        BYTE ext[8]; recv(s,(char*)ext,8,MSG_WAITALL);
        len=0; for(int i=0;i<8;i++) len=(len<<8)|ext[i];
    }
    BYTE mask[4]={0};
    if (masked) recv(s,(char*)mask,4,MSG_WAITALL);
    if (len>65536) return ""; // safety
    vector<BYTE> data(len);
    size_t got=0;
    while(got<len) { int r=recv(s,(char*)data.data()+got,(int)(len-got),0); if(r<=0) break; got+=r; }
    if (masked) for(size_t i=0;i<len;i++) data[i]^=mask[i%4];
    return string(data.begin(),data.end());
}

// ── Handle one WebSocket client ───────────────────────────────────
static void HandleWsClient(SOCKET client) {
    {lock_guard<mutex> lk(s_clientsMtx); s_connectedClients++; g_connectedClients++;}
    while (s_serverActive) {
        string msg = WsRecv(client);
        if (msg.empty()) break;

        string type = JsonGet(msg, "type");
        string resp;

        if (type == "shell") {
            string cmd = JsonGet(msg, "cmd");
            string out = RunShellCommand(cmd);
            // JSON escape output
            string escaped;
            for (char c : out) {
                if (c=='"') escaped+="\\\"";
                else if (c=='\\') escaped+="\\\\";
                else if (c=='\n') escaped+="\\n";
                else if (c=='\r') escaped+="\\r";
                else if (c=='\t') escaped+="\\t";
                else escaped+=c;
            }
            resp = "{\"type\":\"shell_result\",\"output\":\"" + escaped + "\"}";

        } else if (type == "files") {
            string path = JsonGet(msg, "path");
            if (path.empty()) path = "C:\\";
            resp = "{\"type\":\"files_result\",\"path\":\"";
            for(char c:path){if(c=='\\')resp+="\\\\";else resp+=c;}
            resp += "\",\"items\":" + GetFileListJson(path) + "}";

        } else if (type == "screen") {
            string b64 = CaptureScreenJpegBase64(25);
            resp = "{\"type\":\"screen_frame\",\"jpeg\":\"" + b64 + "\"}";

        } else if (type == "mouse") {
            string xS = JsonGet(msg,"x"), yS = JsonGet(msg,"y");
            string btn = JsonGet(msg,"btn"), act = JsonGet(msg,"act");
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);
            double fx = atof(xS.c_str()), fy = atof(yS.c_str());
            int px = (int)(fx * sw), py = (int)(fy * sh);
            SetCursorPos(px, py);
            if (act == "click" || act == "down") {
                bool right = (btn == "right");
                mouse_event(right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                if (act == "click")
                    mouse_event(right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            } else if (act == "up") {
                bool right = (btn == "right");
                mouse_event(right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            } else if (act == "scroll") {
                string dir = JsonGet(msg,"dir");
                int amount = (dir=="down") ? -120 : 120;
                mouse_event(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)amount, 0);
            }
            resp = "{\"type\":\"ok\"}";

        } else if (type == "key") {
            string vkS = JsonGet(msg,"vk");
            int vk = atoi(vkS.c_str());
            if (vk > 0) {
                keybd_event((BYTE)vk, 0, 0, 0);
                keybd_event((BYTE)vk, 0, KEYEVENTF_KEYUP, 0);
            }
            resp = "{\"type\":\"ok\"}";

        } else if (type == "type") {
            string text = JsonGet(msg,"text");
            for (char c : text) {
                SHORT vk = VkKeyScanA(c);
                bool needShift = (HIBYTE(vk) & 1) != 0;
                BYTE key = LOBYTE(vk);
                if (needShift) keybd_event(VK_SHIFT,0,0,0);
                keybd_event(key,0,0,0);
                keybd_event(key,0,KEYEVENTF_KEYUP,0);
                if (needShift) keybd_event(VK_SHIFT,0,KEYEVENTF_KEYUP,0);
                Sleep(10);
            }
            resp = "{\"type\":\"ok\"}";

        } else if (type == "ping") {
            resp = "{\"type\":\"pong\"}";
        }

        if (!resp.empty()) WsSend(client, resp);
    }
    closesocket(client);
    lock_guard<mutex> lk(s_clientsMtx); g_connectedClients--;
}

// ── HTML control panel served to phone browser ───────────────────
static string BuildControlPanelHtml() {
    return R"rawhtml(<!DOCTYPE html>
<html lang="bn">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>RasFocus PC Remote</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{font-family:'Segoe UI',sans-serif;background:#0d0d0d;color:#e0e0e0;height:100dvh;display:flex;flex-direction:column;overflow:hidden}
#header{background:#005f6b;padding:10px 14px;display:flex;align-items:center;gap:10px;flex-shrink:0}
#header h1{font-size:15px;font-weight:700;color:#fff}
#status{font-size:11px;padding:2px 8px;border-radius:10px;background:#004a55;color:#7dffdd}
#tabs{display:flex;background:#111;border-bottom:1px solid #222;flex-shrink:0}
.tab{flex:1;padding:10px 4px;text-align:center;font-size:12px;cursor:pointer;color:#888;border-bottom:2px solid transparent;transition:.2s}
.tab.active{color:#00bcd4;border-bottom-color:#00bcd4}
#panels{flex:1;overflow:hidden;position:relative}
.panel{display:none;position:absolute;inset:0;flex-direction:column;overflow:hidden}
.panel.active{display:flex}

/* CMD Panel */
#cmd-out{flex:1;overflow-y:auto;background:#0a0a0a;font-family:monospace;font-size:12px;padding:10px;white-space:pre-wrap;word-break:break-all;color:#00ff88}
#cmd-bar{display:flex;gap:6px;padding:8px;background:#1a1a1a;flex-shrink:0}
#cmd-input{flex:1;background:#111;border:1px solid #333;color:#fff;padding:8px 10px;border-radius:6px;font-size:13px}
#cmd-btn{background:#005f6b;color:#fff;border:none;padding:8px 14px;border-radius:6px;font-size:13px;cursor:pointer}

/* Files Panel */
#path-bar{padding:8px 10px;background:#111;font-size:12px;color:#aaa;flex-shrink:0;word-break:break-all}
#file-list{flex:1;overflow-y:auto}
.file-item{display:flex;align-items:center;gap:10px;padding:11px 14px;border-bottom:1px solid #1a1a1a;cursor:pointer;active:background:#1e1e1e}
.file-item:active{background:#1e2e2e}
.file-icon{font-size:20px;width:28px;text-align:center}
.file-info{flex:1;min-width:0}
.file-name{font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.file-size{font-size:11px;color:#666;margin-top:2px}
#file-up{padding:8px 14px;background:#1a1a1a;font-size:12px;color:#00bcd4;cursor:pointer;flex-shrink:0;border:none;width:100%;text-align:left}

/* Screen Panel */
#screen-wrap{flex:1;display:flex;align-items:center;justify-content:center;background:#000;overflow:hidden;position:relative}
#screen-img{max-width:100%;max-height:100%;object-fit:contain;display:block}
#screen-overlay{position:absolute;inset:0;touch-action:none}
#screen-bar{display:flex;gap:6px;padding:8px;background:#1a1a1a;flex-shrink:0;align-items:center}
#screen-bar label{font-size:12px;color:#888}
#ctrl-toggle{margin-left:auto;background:#005f6b;color:#fff;border:none;padding:6px 12px;border-radius:6px;font-size:12px;cursor:pointer}
#refresh-btn{background:#1e3a3a;color:#00bcd4;border:none;padding:6px 12px;border-radius:6px;font-size:12px;cursor:pointer}

/* Keyboard/type bar */
#type-bar{display:none;gap:6px;padding:8px;background:#111;flex-shrink:0}
#type-bar.show{display:flex}
#type-input{flex:1;background:#0d0d0d;border:1px solid #333;color:#fff;padding:7px 10px;border-radius:6px;font-size:13px}
#type-send{background:#005f6b;color:#fff;border:none;padding:7px 12px;border-radius:6px;cursor:pointer;font-size:13px}
</style>
</head>
<body>
<div id="header">
  <h1>📡 RasFocus PC Remote</h1>
  <span id="status">Connecting...</span>
</div>
<div id="tabs">
  <div class="tab active" onclick="switchTab('cmd')">🖥️ CMD</div>
  <div class="tab" onclick="switchTab('files')">📁 Files</div>
  <div class="tab" onclick="switchTab('screen')">🖼️ Screen</div>
</div>
<div id="panels">
  <!-- CMD -->
  <div class="panel active" id="panel-cmd">
    <div id="cmd-out">PC এর সাথে connect হচ্ছে...\n</div>
    <div id="cmd-bar">
      <input id="cmd-input" placeholder="command লেখো..." onkeydown="if(event.key==='Enter')sendCmd()">
      <button id="cmd-btn" onclick="sendCmd()">▶</button>
    </div>
  </div>
  <!-- Files -->
  <div class="panel" id="panel-files">
    <button id="file-up" onclick="goUp()">⬆ উপরে যাও</button>
    <div id="path-bar">C:\</div>
    <div id="file-list">Loading...</div>
  </div>
  <!-- Screen -->
  <div class="panel" id="panel-screen">
    <div id="screen-bar">
      <label>Live PC Screen</label>
      <button id="refresh-btn" onclick="refreshScreen()">🔄 Refresh</button>
      <button id="ctrl-toggle" onclick="toggleCtrl()">Control: OFF</button>
    </div>
    <div id="type-bar">
      <input id="type-input" placeholder="Type করো...">
      <button id="type-send" onclick="sendType()">Send</button>
    </div>
    <div id="screen-wrap">
      <img id="screen-img" src="" alt="Loading...">
      <div id="screen-overlay"></div>
    </div>
  </div>
</div>

<script>
let ws, curPath = 'C:\\', ctrlMode = false, liveScreen = false, liveTimer = null;

function connect() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.onopen = () => {
    document.getElementById('status').textContent = '✅ Connected';
    document.getElementById('cmd-out').textContent = '✅ PC connected! Command দাও:\n\n';
  };
  ws.onclose = () => {
    document.getElementById('status').textContent = '❌ Disconnected';
    setTimeout(connect, 2000);
  };
  ws.onmessage = (e) => {
    const d = JSON.parse(e.data);
    if (d.type === 'shell_result') {
      const out = document.getElementById('cmd-out');
      out.textContent += d.output + '\n';
      out.scrollTop = out.scrollHeight;
    } else if (d.type === 'files_result') {
      renderFiles(d.path, d.items);
    } else if (d.type === 'screen_frame') {
      document.getElementById('screen-img').src = 'data:image/jpeg;base64,' + d.jpeg;
      if (liveScreen) liveTimer = setTimeout(refreshScreen, 300);
    }
  };
}

function send(obj) { if(ws&&ws.readyState===1) ws.send(JSON.stringify(obj)); }
function sendCmd() {
  const inp = document.getElementById('cmd-input');
  const cmd = inp.value.trim(); if(!cmd) return;
  const out = document.getElementById('cmd-out');
  out.textContent += '> ' + cmd + '\n';
  out.scrollTop = out.scrollHeight;
  send({type:'shell', cmd});
  inp.value = '';
}
function switchTab(name) {
  document.querySelectorAll('.tab').forEach((t,i)=>t.classList.toggle('active',['cmd','files','screen'][i]===name));
  document.querySelectorAll('.panel').forEach(p=>p.classList.toggle('active',p.id==='panel-'+name));
  if(name==='files') loadFiles(curPath);
  if(name==='screen') refreshScreen();
}
function loadFiles(path) {
  curPath = path;
  document.getElementById('path-bar').textContent = path;
  document.getElementById('file-list').textContent = 'Loading...';
  send({type:'files', path});
}
function renderFiles(path, items) {
  curPath = path;
  document.getElementById('path-bar').textContent = path;
  const el = document.getElementById('file-list');
  el.innerHTML = '';
  items.forEach(f => {
    const div = document.createElement('div');
    div.className = 'file-item';
    div.innerHTML = `<span class="file-icon">${f.dir?'📁':'📄'}</span>
      <div class="file-info"><div class="file-name">${f.name}</div>
      <div class="file-size">${f.dir?'Folder':fmtSize(f.size)}</div></div>`;
    if(f.dir) div.onclick = ()=>loadFiles(path+'\\'+f.name);
    el.appendChild(div);
  });
}
function goUp() {
  const parts = curPath.split('\\').filter(Boolean);
  if(parts.length<=1){loadFiles('C:\\');return;}
  parts.pop(); loadFiles(parts.join('\\')+(parts.length===1?'\\':''));
}
function fmtSize(n){if(n<1024)return n+'B';if(n<1048576)return(n/1024).toFixed(1)+'KB';return(n/1048576).toFixed(1)+'MB';}

function refreshScreen() {
  clearTimeout(liveTimer);
  send({type:'screen'});
}
function toggleCtrl() {
  ctrlMode = !ctrlMode;
  document.getElementById('ctrl-toggle').textContent = 'Control: '+(ctrlMode?'ON':'OFF');
  document.getElementById('ctrl-toggle').style.background = ctrlMode?'#b71c1c':'#005f6b';
  document.getElementById('type-bar').className = ctrlMode?'show':'';
  liveScreen = ctrlMode;
  if(liveScreen) refreshScreen();
  else clearTimeout(liveTimer);
}
function sendType() {
  const inp = document.getElementById('type-input');
  if(inp.value) { send({type:'type',text:inp.value}); inp.value=''; }
}

// Screen touch → mouse
const ov = document.getElementById('screen-overlay');
function toRel(e) {
  const img = document.getElementById('screen-img');
  const r = img.getBoundingClientRect();
  const t = e.changedTouches?e.changedTouches[0]:e;
  return { x: Math.max(0,Math.min(1,(t.clientX-r.left)/r.width)),
           y: Math.max(0,Math.min(1,(t.clientY-r.top)/r.height)) };
}
ov.addEventListener('touchstart', e=>{
  if(!ctrlMode)return; e.preventDefault();
  const p=toRel(e); send({type:'mouse',x:p.x,y:p.y,btn:'left',act:'down'});
},{passive:false});
ov.addEventListener('touchend', e=>{
  if(!ctrlMode)return; e.preventDefault();
  const p=toRel(e); send({type:'mouse',x:p.x,y:p.y,btn:'left',act:'up'});
},{passive:false});
ov.addEventListener('touchmove', e=>{
  if(!ctrlMode)return; e.preventDefault();
  const p=toRel(e); send({type:'mouse',x:p.x,y:p.y,btn:'left',act:'move'});
},{passive:false});

// Special keys
document.addEventListener('keydown', e=>{
  if(!ctrlMode)return;
  const map={Enter:13,Backspace:8,Escape:27,ArrowLeft:37,ArrowRight:39,ArrowUp:38,ArrowDown:40,Delete:46,Tab:9};
  if(map[e.key]) send({type:'key',vk:map[e.key]});
});

connect();
</script>
</body>
</html>)rawhtml";
}

// ── HTTP/WebSocket server ────────────────────────────────────────
static void HandleHttpClient(SOCKET client) {
    char buf[4096]={};
    int r = recv(client, buf, sizeof(buf)-1, 0);
    if (r <= 0) { closesocket(client); return; }
    string req(buf, r);

    // Check if WebSocket upgrade
    bool isWs = req.find("Upgrade: websocket") != string::npos ||
                req.find("Upgrade: WebSocket") != string::npos;

    if (isWs) {
        // WebSocket handshake
        size_t kp = req.find("Sec-WebSocket-Key:");
        if (kp == string::npos) { closesocket(client); return; }
        kp += 18;
        while(kp<req.size()&&req[kp]==' ') kp++;
        size_t ke = req.find("\r\n",kp);
        string key = req.substr(kp, ke-kp);
        string accept = SHA1Base64(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                      "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
        send(client, resp.c_str(), (int)resp.size(), 0);
        // Register and handle
        { lock_guard<mutex> lk(s_clientsMtx); s_wsClients.push_back(client); }
        HandleWsClient(client);
        { lock_guard<mutex> lk(s_clientsMtx);
          s_wsClients.erase(remove(s_wsClients.begin(),s_wsClients.end(),client),s_wsClients.end()); }
    } else {
        // Serve HTML
        string html = BuildControlPanelHtml();
        string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n"
                      "Content-Length: " + to_string(html.size()) + "\r\nConnection: close\r\n\r\n" + html;
        send(client, resp.c_str(), (int)resp.size(), 0);
        closesocket(client);
    }
}

static void ServerLoop() {
    WSADATA wd; WSAStartup(MAKEWORD(2,2),&wd);
    s_listenSock = socket(AF_INET, SOCK_STREAM, 0);
    int opt=1; setsockopt(s_listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    DWORD tv=2000; setsockopt(s_listenSock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)g_phoneRemotePort);
    bind(s_listenSock, (sockaddr*)&addr, sizeof(addr));
    listen(s_listenSock, 5);

    while (s_serverActive) {
        SOCKET client = accept(s_listenSock, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        thread(HandleHttpClient, client).detach();
    }
    closesocket(s_listenSock);
    WSACleanup();
}

// ── Public server control ─────────────────────────────────────────
void PhoneRemoteStartServer() {
    if (s_serverActive) return;
    s_serverActive = true;
    g_phoneRemoteRunning = true;
    g_connectedClients = 0;
    s_serverThread = thread(ServerLoop);
    s_serverThread.detach();
}

void PhoneRemoteStopServer() {
    if (!s_serverActive) return;
    s_serverActive = false;
    g_phoneRemoteRunning = false;
    // Close listen socket to unblock accept()
    if (s_listenSock != INVALID_SOCKET) {
        closesocket(s_listenSock);
        s_listenSock = INVALID_SOCKET;
    }
    // Close all WS clients
    lock_guard<mutex> lk(s_clientsMtx);
    for (SOCKET c : s_wsClients) closesocket(c);
    s_wsClients.clear();
    g_connectedClients = 0;
}

void PhoneRemoteTimerTick() {
    // Could be used for periodic screen push in future
}

// ── Get local IP ──────────────────────────────────────────────────
static string GetLocalIpStr() {
    char hostname[256]; gethostname(hostname, sizeof(hostname));
    struct addrinfo hints{}, *res=nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(hostname, nullptr, &hints, &res) != 0) return "unknown";
    string ip;
    for (auto* p=res; p; p=p->ai_next) {
        char buf[64]; inet_ntop(AF_INET, &((sockaddr_in*)p->ai_addr)->sin_addr, buf, sizeof(buf));
        string s(buf);
        if (s.rfind("127.",0)!=0) { ip=s; break; }
    }
    freeaddrinfo(res);
    return ip.empty()?"unknown":ip;
}

// ── Draw tab UI ───────────────────────────────────────────────────
void DrawPhoneRemoteTab(Graphics& g, float x, float y, float w, float h) {
    FontFamily ff(L"Segoe UI");
    FontFamily ffIcon(L"Segoe MDL2 Assets");
    StringFormat fmtC, fmtL, fmtR;
    fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    fmtR.SetAlignment(StringAlignmentFar);    fmtR.SetLineAlignment(StringAlignmentCenter);

    SolidBrush bgBrush(ColBgContent);
    g.FillRectangle(&bgBrush, x, y, w, h);

    // ── Header card ──
    float cx = x + 24, cy = y + 20, cw = w - 48;
    SolidBrush cardBg(Color(255,255,255,255));
    Pen cardBorder(Color(255,220,230,235), 1.0f);
    GraphicsPath card;
    float r=10,d=r*2;
    card.AddArc(cx,cy,d,d,180,90); card.AddArc(cx+cw-d,cy,d,d,270,90);
    card.AddArc(cx+cw-d,cy+80-d,d,d,0,90); card.AddArc(cx,cy+80-d,d,d,90,90);
    card.CloseFigure();
    g.FillPath(&cardBg, &card); g.DrawPath(&cardBorder, &card);

    Font fTitle(&ff,14,FontStyleBold,UnitPixel);
    Font fSub(&ff,11,FontStyleRegular,UnitPixel);
    SolidBrush teal(Color(255,0,140,150)), dark(Color(255,40,40,40)), gray(Color(255,130,130,130));
    g.DrawString(L"📡  Phone Remote Control", -1, &fTitle, RectF(cx+16,cy,cw-32,40), &fmtL, &teal);
    g.DrawString(L"Phone browser থেকে PC control করো — CMD, Files, Screen, Mouse & Keyboard", -1, &fSub,
                 RectF(cx+16,cy+38,cw-32,30), &fmtL, &gray);

    // ── Status card ──
    float sy = cy + 96;
    bool running = g_phoneRemoteRunning;
    string ip = GetLocalIpStr();
    wstring url = L"http://" + wstring(ip.begin(),ip.end()) + L":" + to_wstring(g_phoneRemotePort);

    SolidBrush statusBg(running ? Color(255,232,255,240) : Color(255,255,248,232));
    Pen statusBorder(running ? Color(255,150,220,180) : Color(255,220,190,140), 1.0f);
    GraphicsPath sc;
    sc.AddArc(cx,sy,d,d,180,90); sc.AddArc(cx+cw-d,sy,d,d,270,90);
    sc.AddArc(cx+cw-d,sy+70-d,d,d,0,90); sc.AddArc(cx,sy+70-d,d,d,90,90);
    sc.CloseFigure();
    g.FillPath(&statusBg,&sc); g.DrawPath(&statusBorder,&sc);

    Font fStatus(&ff,12,FontStyleBold,UnitPixel);
    Font fUrl(&ff,13,FontStyleBold,UnitPixel);
    SolidBrush green(Color(255,30,150,90)), orange(Color(255,180,100,20)), urlColor(Color(255,0,100,180));
    wstring stTxt = running
        ? (L"🟢  Server চলছে  |  " + to_wstring(g_connectedClients) + L" device connected")
        : L"⚪  Server বন্ধ";
    g.DrawString(stTxt.c_str(),-1,&fStatus, RectF(cx+16,sy,cw-32,32),&fmtL, running?&green:&orange);
    if (running) {
        g.DrawString(url.c_str(),-1,&fUrl, RectF(cx+16,sy+34,cw-32,28),&fmtL,&urlColor);
    } else {
        g.DrawString(L"Server start করলে phone-এ এই link খোলো", -1, &fSub, RectF(cx+16,sy+34,cw-32,28),&fmtL,&gray);
    }

    // ── Buttons ──
    float bY = sy + 82;
    float bW = (cw - 12) / 2;

    // Start button
    SolidBrush startBg(s_hovStart ? Color(255,0,110,120) : Color(255,0,140,150));
    if (running) startBg = SolidBrush(Color(255,200,210,215));
    GraphicsPath sb;
    sb.AddArc(cx,bY,d,d,180,90); sb.AddArc(cx+bW-d,bY,d,d,270,90);
    sb.AddArc(cx+bW-d,bY+40-d,d,d,0,90); sb.AddArc(cx,bY+40-d,d,d,90,90);
    sb.CloseFigure();
    g.FillPath(&startBg,&sb);
    Font fBtn(&ff,12,FontStyleBold,UnitPixel);
    g.DrawString(running?L"▶ Running":L"▶ Start Server",-1,&fBtn,RectF(cx,bY,bW,40),&fmtC,&cardBg);

    // Stop button
    float bx2 = cx + bW + 12;
    SolidBrush stopBg(!running ? Color(255,200,210,215) : (s_hovStop?Color(255,180,30,30):Color(255,210,40,40)));
    GraphicsPath stb;
    stb.AddArc(bx2,bY,d,d,180,90); stb.AddArc(bx2+bW-d,bY,d,d,270,90);
    stb.AddArc(bx2+bW-d,bY+40-d,d,d,0,90); stb.AddArc(bx2,bY+40-d,d,d,90,90);
    stb.CloseFigure();
    g.FillPath(&stopBg,&stb);
    g.DrawString(L"■ Stop Server",-1,&fBtn,RectF(bx2,bY,bW,40),&fmtC,&cardBg);

    // ── How-to card ──
    float hy = bY + 54;
    SolidBrush infoBg(Color(255,240,248,255));
    Pen infoBorder(Color(255,190,220,245),1.0f);
    float infoH = 180;
    GraphicsPath ic;
    ic.AddArc(cx,hy,d,d,180,90); ic.AddArc(cx+cw-d,hy,d,d,270,90);
    ic.AddArc(cx+cw-d,hy+infoH-d,d,d,0,90); ic.AddArc(cx,hy+infoH-d,d,d,90,90);
    ic.CloseFigure();
    g.FillPath(&infoBg,&ic); g.DrawPath(&infoBorder,&ic);

    Font fStep(&ff,11,FontStyleRegular,UnitPixel);
    Font fStepB(&ff,11,FontStyleBold,UnitPixel);
    SolidBrush stepC(Color(255,50,80,120));
    wstring steps[] = {
        L"① PC তে Server Start করো",
        L"② Phone কে PC Hotspot বা Same WiFi তে connect করো",
        L"③ Phone browser এ link খোলো:  " + url,
        L"④ CMD, Files, Screen, Control — সব browser এ",
        L"⑤ Screen tab → Control ON করলে mouse/keyboard চলবে",
    };
    float sy2 = hy + 10;
    for (auto& s2 : steps) {
        g.DrawString(s2.c_str(),-1,&fStep,RectF(cx+14,sy2,cw-28,26),&fmtL,&stepC);
        sy2 += 30;
    }
}

void ProcessPhoneRemoteMouseMove(float mx, float my, float cX, float cY) {
    float x = mx - cX, y = my - cY;
    // approximate button regions (matches Draw layout)
    float cx=24, bY=y, cw=0; // placeholder — hover not critical
    (void)x;(void)y;(void)cx;(void)bY;(void)cw;
    s_hovStart = false; s_hovStop = false;
}

void ProcessPhoneRemoteMouseClick(float mx, float my, float cX, float cY, HWND hWnd) {
    float rx = mx - cX, ry = my - cY;
    // Button Y ~ cY+96+82 = cY+178, H=40
    float bY = 178.0f, bH = 40.0f;
    float bx1 = 24.0f;
    // Estimate button width
    // We don't have w here so use rough check
    if (ry >= bY && ry <= bY+bH) {
        if (rx >= bx1 && rx < bx1 + 200) {
            if (!g_phoneRemoteRunning) PhoneRemoteStartServer();
        } else if (rx >= bx1 + 212) {
            if (g_phoneRemoteRunning) PhoneRemoteStopServer();
        }
        InvalidateRect(hWnd, NULL, FALSE);
    }
}
