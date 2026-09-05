// ================================================================
// tab_phone_remote.cpp  —  PC generates 6-digit code → Phone connects
//
// Updated: Relay server integration
//   - PC registers {code, ip, port} to Firebase Firestore
//   - Phone looks up code → gets IP → connects directly (LAN)
//   - If LAN fails → relay server bridges the connection
//
// PC = WebSocket SERVER (port 9224)
//   1. "Generate Code" → random 6-digit code on screen
//      + uploads to Firestore (code → IP mapping)
//   2. Phone types code → Firestore lookup → connects ws://pc-ip:9224
//   3. Phone sends {"type":"auth","code":"XXXXXX"}
//   4. PC verifies → sends {"type":"ready","width":W,"height":H,"fps":30}
//   5. PC H.264 screen stream → phone decodes (MediaCodec) → live video
//   6. Phone sends {"type":"mouse"/"key"/"scroll"} → PC SendInput
//
// Relay fallback (different networks):
//   Phone → wss://relay.rasfocus.com/relay/<code>
//   PC    → wss://relay.rasfocus.com/relay/<code>  (host mode)
//   Relay bridges both connections transparently.
//
// Inspired by RustDesk open source (MIT License)
// ================================================================

#pragma warning(disable: 4996)
#pragma warning(disable: 4244)

#include <winsock2.h>
#include <ws2tcpip.h>
#include "tab_phone_remote.h"
#include "tab_phone_remote_relay.h"   // ← NEW: relay signaling
#include "globals.h"
#include "pc_screen_streamer.h"

#include <windows.h>
#include <gdiplus.h>
#include <mfapi.h>
#include <mftransform.h>
#include <mfidl.h>
#include <mferror.h>
#include <codecapi.h>
#include <wincrypt.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Crypt32.lib")

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

using namespace Gdiplus;
using namespace std;

// ── Globals (extern in .h) ────────────────────────────────────────
RdState     g_rdState       = RdState::Idle;
string      g_rdCode        = "";
string      g_rdPhoneName   = "";
string      g_rdPhoneIp     = "";
string      g_rdStatusMsg   = "\"Generate Code\" চাপো — Phone এ code টাইপ করো";
int         g_rdFps         = 0;
bool        g_rdInputEnabled= true;

// legacy compat
string      g_rdPhoneId     = "";
int         g_rdPhonePort   = 9224;
int         g_rdPhoneW      = 1080;
int         g_rdPhoneH      = 1920;
bool        g_phoneRemoteRunning = false;
int         g_phoneRemotePort    = 9224;
int         g_phoneRemoteUdpPort = 9225;
int         g_connectedClients   = 0;
string      g_phoneRemotePin     = "";

// ── Internal ──────────────────────────────────────────────────────
static const int  RD_PORT     = 9224;
static const int  TARGET_FPS  = 30;
static const int  TARGET_BPS  = 4'000'000; // 4 Mbps

static atomic<bool> s_active  { false };
static SOCKET       s_listenSock = INVALID_SOCKET;
static SOCKET       s_clientSock = INVALID_SOCKET; // one phone at a time
static mutex        s_sendMtx;

// UI state
static float s_drawX=0, s_drawY=0, s_drawW=0, s_drawH=0;
static bool  s_hovGenerate = false;
static bool  s_hovStop     = false;
static bool  s_hovCopyCode = false;  // ← NEW: copy code button

extern HWND hParentWnd;

// ── String helpers ────────────────────────────────────────────────
static string WStr(const wstring& w) {
    if(w.empty()) return "";
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);
    string s(n,' ');
    WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),&s[0],n,nullptr,nullptr);
    return s;
}
static wstring ToWStr(const string& s) {
    if(s.empty()) return L"";
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);
    wstring w(n,L' ');
    MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),&w[0],n);
    return w;
}
static string Jget(const string& j, const string& k) {
    string sk="\""+k+"\""; size_t p=j.find(sk);
    if(p==string::npos) return "";
    p=j.find(':',p+sk.size()); if(p==string::npos) return "";
    p++; while(p<j.size()&&(j[p]==' '||j[p]=='\t')) p++;
    if(p>=j.size()) return "";
    if(j[p]=='"'){size_t e=j.find('"',p+1);if(e==string::npos)return "";return j.substr(p+1,e-p-1);}
    size_t e=p; while(e<j.size()&&j[e]!=','&&j[e]!='}'&&j[e]!=']') e++;
    string v=j.substr(p,e-p);
    while(!v.empty()&&(v.back()==' '||v.back()=='\r'||v.back()=='\n')) v.pop_back();
    return v;
}

// ── Get local IP ──────────────────────────────────────────────────
static string GetLocalIp() {
    char host[256] = {};
    gethostname(host, sizeof(host));
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return "127.0.0.1";
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &((sockaddr_in*)res->ai_addr)->sin_addr, ip, sizeof(ip));
    freeaddrinfo(res);
    return string(ip);
}

// ── Copy text to clipboard ────────────────────────────────────────
static void CopyToClipboard(const string& text) {
    if (!OpenClipboard(hParentWnd)) return;
    EmptyClipboard();
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!hg) { CloseClipboard(); return; }
    memcpy(GlobalLock(hg), text.c_str(), text.size() + 1);
    GlobalUnlock(hg);
    SetClipboardData(CF_TEXT, hg);
    CloseClipboard();
}

// ── Code generator (6 digits) ─────────────────────────────────────
static string GenCode() {
    srand((unsigned)time(nullptr) ^ GetTickCount());
    char buf[7];
    for(int i=0;i<6;i++) buf[i]='0'+(rand()%10);
    buf[6]=0;
    return string(buf);
}

// ── SHA1 + Base64 (for WS handshake) ─────────────────────────────
static string B64Enc(const vector<BYTE>& d) {
    static const char* t="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string o; o.reserve(((d.size()+2)/3)*4);
    for(size_t i=0;i<d.size();i+=3){
        BYTE b0=d[i],b1=(i+1<d.size())?d[i+1]:0,b2=(i+2<d.size())?d[i+2]:0;
        o+=t[b0>>2]; o+=t[((b0&3)<<4)|(b1>>4)];
        o+=(i+1<d.size())?t[((b1&0xF)<<2)|(b2>>6)]:'=';
        o+=(i+2<d.size())?t[b2&0x3F]:'=';
    }
    return o;
}
static string Sha1B64(const string& s) {
    HCRYPTPROV hp=0; HCRYPTHASH hh=0;
    CryptAcquireContextA(&hp,NULL,NULL,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT);
    CryptCreateHash(hp,CALG_SHA1,0,0,&hh);
    CryptHashData(hh,(const BYTE*)s.c_str(),(DWORD)s.size(),0);
    BYTE hash[20]; DWORD hl=20;
    CryptGetHashParam(hh,HP_HASHVAL,hash,&hl,0);
    CryptDestroyHash(hh); CryptReleaseContext(hp,0);
    return B64Enc(vector<BYTE>(hash,hash+20));
}

// ── WebSocket helpers (server — no masking on send) ───────────────
static bool WsRecvAll(SOCKET s, char* buf, int n) {
    int got=0; while(got<n){int r=recv(s,buf+got,n-got,0);if(r<=0)return false;got+=r;}return true;
}
static pair<int,vector<BYTE>> WsRecvFrame(SOCKET s) {
    BYTE h[2]; if(!WsRecvAll(s,(char*)h,2)) return {-1,{}};
    int op=h[0]&0xF; bool masked=(h[1]&0x80)!=0;
    size_t len=h[1]&0x7F;
    if(len==126){BYTE e[2];if(!WsRecvAll(s,(char*)e,2))return{-1,{}};len=((size_t)e[0]<<8)|e[1];}
    else if(len==127){BYTE e[8];if(!WsRecvAll(s,(char*)e,8))return{-1,{}};
        len=0;for(int i=0;i<8;i++)len=(len<<8)|e[i];}
    BYTE mask[4]={};
    if(masked&&!WsRecvAll(s,(char*)mask,4)) return{-1,{}};
    vector<BYTE> data(len);
    if(len>0&&!WsRecvAll(s,(char*)data.data(),(int)len)) return{-1,{}};
    if(masked) for(size_t i=0;i<len;i++) data[i]^=mask[i%4];
    return{op,data};
}
static bool WsSendText(SOCKET s, const string& txt) {
    lock_guard<mutex> lk(s_sendMtx);
    vector<BYTE> frame;
    size_t len=txt.size();
    frame.push_back(0x81); // FIN + text opcode
    if(len<126){ frame.push_back((BYTE)len); }
    else if(len<65536){
        frame.push_back(126);
        frame.push_back((len>>8)&0xFF);
        frame.push_back(len&0xFF);
    } else {
        frame.push_back(127);
        for(int i=7;i>=0;i--) frame.push_back((len>>(i*8))&0xFF);
    }
    for(char c:txt) frame.push_back((BYTE)c);
    return send(s,(char*)frame.data(),(int)frame.size(),0)>0;
}
static bool WsSendBinary(SOCKET s, const BYTE* data, size_t len) {
    lock_guard<mutex> lk(s_sendMtx);
    vector<BYTE> frame;
    frame.push_back(0x82); // FIN + binary opcode
    if(len<126){ frame.push_back((BYTE)len); }
    else if(len<65536){
        frame.push_back(126);
        frame.push_back((len>>8)&0xFF);
        frame.push_back(len&0xFF);
    } else {
        frame.push_back(127);
        for(int i=7;i>=0;i--) frame.push_back((len>>(i*8))&0xFF);
    }
    frame.insert(frame.end(), data, data+len);
    return send(s,(char*)frame.data(),(int)frame.size(),0)>0;
}

// ── WebSocket HTTP upgrade handshake ─────────────────────────────
static bool DoHandshake(SOCKET s) {
    char buf[4096]={};
    int total=0;
    while(total<(int)sizeof(buf)-1){
        int r=recv(s,buf+total,(int)sizeof(buf)-1-total,0);
        if(r<=0) return false;
        total+=r;
        buf[total]=0;
        if(strstr(buf,"\r\n\r\n")) break;
    }
    const char* keyHdr=strstr(buf,"Sec-WebSocket-Key:");
    if(!keyHdr) return false;
    keyHdr+=18;
    while(*keyHdr==' ') keyHdr++;
    char key[256]={};
    int ki=0;
    while(*keyHdr&&*keyHdr!='\r'&&*keyHdr!='\n'&&ki<(int)sizeof(key)-1)
        key[ki++]=*keyHdr++;
    while(ki>0&&(key[ki-1]==' '||key[ki-1]=='\r'||key[ki-1]=='\n')) ki--;
    key[ki]=0;
    string accept=Sha1B64(string(key)+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    string resp="HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: "+accept+"\r\n\r\n";
    return send(s,resp.c_str(),(int)resp.size(),0)>0;
}

// ── GDI+ screen capture helper ────────────────────────────────────
struct CaptureFrame {
    vector<BYTE> data; // raw JPEG or H264
    int width=0, height=0;
};

// Simple JPEG fallback capture using GDI+
// (H264 is handled by pc_screen_streamer.cpp — this is for auth phase only)
static CaptureFrame CaptureJpeg(int quality=60) {
    CaptureFrame cf;
    int sw=GetSystemMetrics(SM_CXSCREEN);
    int sh=GetSystemMetrics(SM_CYSCREEN);
    // Scale down to max 1280 wide
    float scale=min(1.f, 1280.f/sw);
    cf.width=(int)(sw*scale); cf.height=(int)(sh*scale);

    HDC hScreen=GetDC(NULL);
    HDC hMem=CreateCompatibleDC(hScreen);
    HBITMAP hBmp=CreateCompatibleBitmap(hScreen,cf.width,cf.height);
    SelectObject(hMem,hBmp);
    SetStretchBltMode(hMem,HALFTONE);
    StretchBlt(hMem,0,0,cf.width,cf.height,hScreen,0,0,sw,sh,SRCCOPY);

    Bitmap bmp(hBmp, NULL);
    CLSID jpegClsid;
    // Get JPEG encoder
    UINT num=0,sz=0;
    GetImageEncodersSize(&num,&sz);
    if(sz==0){ DeleteObject(hBmp);DeleteDC(hMem);ReleaseDC(NULL,hScreen); return cf; }
    vector<BYTE> codecBuf(sz);
    ImageCodecInfo* ci=(ImageCodecInfo*)codecBuf.data();
    GetImageEncoders(num,sz,ci);
    for(UINT i=0;i<num;i++){
        if(wcscmp(ci[i].MimeType,L"image/jpeg")==0){ jpegClsid=ci[i].Clsid; break; }
    }
    EncoderParameters ep; ep.Count=1;
    ep.Parameter[0].Guid=EncoderQuality;
    ep.Parameter[0].Type=EncoderParameterValueTypeLong;
    ep.Parameter[0].NumberOfValues=1;
    ULONG q=(ULONG)quality;
    ep.Parameter[0].Value=&q;

    IStream* pStream=NULL;
    CreateStreamOnHGlobal(NULL,TRUE,&pStream);
    bmp.Save(pStream,&jpegClsid,&ep);
    STATSTG stat; pStream->Stat(&stat,STATFLAG_NONAME);
    ULARGE_INTEGER pos; pos.QuadPart=0;
    pStream->Seek({},STREAM_SEEK_SET,NULL);
    cf.data.resize((size_t)stat.cbSize.QuadPart);
    ULONG read=0;
    pStream->Read(cf.data.data(),(ULONG)cf.data.size(),&read);
    pStream->Release();

    DeleteObject(hBmp); DeleteDC(hMem); ReleaseDC(NULL,hScreen);
    return cf;
}

// ── Handle one connected phone client ────────────────────────────
static void HandlePhone(SOCKET sock, string phoneIp) {
    s_clientSock = sock;
    g_phoneRemoteRunning = true;
    g_connectedClients = 1;

    // Send ready message first (before auth in relaxed mode)
    // Wait for auth
    bool authed = false;
    string deviceName = "Unknown";

    // Receive auth frame
    auto [op, data] = WsRecvFrame(sock);
    if(op==1) { // text
        string msg(data.begin(), data.end());
        string type=Jget(msg,"type");
        string code=Jget(msg,"code");
        deviceName=Jget(msg,"device");
        if(type=="auth" && code==g_rdCode) {
            authed=true;
        } else {
            WsSendText(sock, "{\"type\":\"error\",\"msg\":\"wrong code\"}");
        }
    }

    if(!authed) { closesocket(sock); s_clientSock=INVALID_SOCKET; g_phoneRemoteRunning=false; g_connectedClients=0; return; }

    // Auth OK
    int sw=GetSystemMetrics(SM_CXSCREEN);
    int sh=GetSystemMetrics(SM_CYSCREEN);
    float scale=min(1.f,1280.f/sw);
    int vw=(int)(sw*scale), vh=(int)(sh*scale);

    string readyMsg="{\"type\":\"ready\",\"width\":"+to_string(vw)+
                    ",\"height\":"+to_string(vh)+
                    ",\"fps\":"+to_string(TARGET_FPS)+
                    ",\"mode\":\"h264\"}";
    WsSendText(sock, readyMsg);

    g_rdPhoneName=deviceName;
    g_rdPhoneIp=phoneIp;
    g_rdState=RdState::Connected;
    g_phoneRemoteRunning=true;
    g_connectedClients=1;
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);

    // Start H264 screen streamer (pc_screen_streamer.cpp)
    PcStreamerStart();

    // Input receive loop (phone → PC mouse/key events)
    DWORD lastFpsUpdate = GetTickCount();
    int fpsCount = 0;

    while(s_active && sock!=INVALID_SOCKET) {
        auto [iop, idata] = WsRecvFrame(sock);
        if(iop<0) break;

        if(iop==1) { // text: input event
            string msg(idata.begin(), idata.end());
            string type=Jget(msg,"type");

            if(type=="mouse") {
                float nx=stof(Jget(msg,"nx")); // normalized 0..1
                float ny=stof(Jget(msg,"ny"));
                int mask=stoi(Jget(msg,"mask")); // 0=move,1=ldown,2=lup,4=rdown,8=rup,16=lclick,32=rclick

                int ax=(int)(nx*GetSystemMetrics(SM_CXSCREEN));
                int ay=(int)(ny*GetSystemMetrics(SM_CYSCREEN));

                INPUT inp={};
                inp.type=INPUT_MOUSE;
                inp.mi.dx=(LONG)(nx*65535);
                inp.mi.dy=(LONG)(ny*65535);
                inp.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_MOVE;
                if(mask&1)  inp.mi.dwFlags|=MOUSEEVENTF_LEFTDOWN;
                if(mask&2)  inp.mi.dwFlags|=MOUSEEVENTF_LEFTUP;
                if(mask&4)  inp.mi.dwFlags|=MOUSEEVENTF_RIGHTDOWN;
                if(mask&8)  inp.mi.dwFlags|=MOUSEEVENTF_RIGHTUP;
                if(g_rdInputEnabled) SendInput(1,&inp,sizeof(INPUT));

            } else if(type=="key") {
                int vk=stoi(Jget(msg,"vk"));
                string action=Jget(msg,"action"); // "down" or "up"
                INPUT ki={}; ki.type=INPUT_KEYBOARD;
                ki.ki.wVk=(WORD)vk;
                if(action=="up") ki.ki.dwFlags=KEYEVENTF_KEYUP;
                if(g_rdInputEnabled) SendInput(1,&ki,sizeof(INPUT));

            } else if(type=="scroll") {
                float x=stof(Jget(msg,"nx"));
                float y=stof(Jget(msg,"ny"));
                string dir=Jget(msg,"dir");
                INPUT si={}; si.type=INPUT_MOUSE;
                si.mi.dx=(LONG)(x*65535); si.mi.dy=(LONG)(y*65535);
                si.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_WHEEL;
                si.mi.mouseData=(dir=="up")?WHEEL_DELTA:(DWORD)-(int)WHEEL_DELTA;
                if(g_rdInputEnabled) SendInput(1,&si,sizeof(INPUT));

            } else if(type=="ping") {
                WsSendText(sock,"{\"type\":\"pong\"}");
            }
        } else if(iop==8) { // close frame
            break;
        }
    }

    PcStreamerStop();
    closesocket(sock);
    s_clientSock=INVALID_SOCKET;
    g_rdState=RdState::WaitPhone; // go back to waiting (code still valid)
    g_rdPhoneName=""; g_rdPhoneIp="";
    g_rdStatusMsg="Phone disconnected — waiting for reconnect";
    g_phoneRemoteRunning=false; g_connectedClients=0; g_rdFps=0;
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}

// ── Accept loop (runs in background thread) ───────────────────────
static void AcceptLoop() {
    WSADATA wd; WSAStartup(MAKEWORD(2,2),&wd);
    s_listenSock=socket(AF_INET,SOCK_STREAM,0);
    if(s_listenSock==INVALID_SOCKET) return;

    int reuse=1;
    setsockopt(s_listenSock,SOL_SOCKET,SO_REUSEADDR,(char*)&reuse,sizeof(reuse));

    sockaddr_in addr={}; addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons((u_short)RD_PORT);
    if(bind(s_listenSock,(sockaddr*)&addr,sizeof(addr))<0){
        closesocket(s_listenSock); s_listenSock=INVALID_SOCKET; return;
    }
    listen(s_listenSock,5);

    // Set non-blocking accept with timeout
    DWORD timeout=500;
    setsockopt(s_listenSock,SOL_SOCKET,SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));

    while(s_active) {
        sockaddr_in ca={}; int cal=sizeof(ca);
        SOCKET cl=accept(s_listenSock,(sockaddr*)&ca,&cal);
        if(cl==INVALID_SOCKET) continue;
        char cip[INET_ADDRSTRLEN]={};
        inet_ntop(AF_INET,&ca.sin_addr,cip,sizeof(cip));
        if(!DoHandshake(cl)){closesocket(cl);continue;}
        // Only one phone at a time
        if(s_clientSock!=INVALID_SOCKET){
            WsSendText(cl,"{\"type\":\"error\",\"msg\":\"busy\"}");
            closesocket(cl); continue;
        }
        thread(HandlePhone,cl,string(cip)).detach();
    }
    closesocket(s_listenSock); s_listenSock=INVALID_SOCKET;
    WSACleanup();
}

// ── Public API ────────────────────────────────────────────────────
void RdGenerateCode(){
    RdStopServer();
    g_rdCode=GenCode();
    g_rdState=RdState::WaitPhone;

    string ip=GetLocalIp();
    g_rdStatusMsg="PC IP: "+ip+" | Port: "+to_string(RD_PORT);

    // ── NEW: Upload to Firebase relay so phone can find us by code alone ──
    RelayRegisterSession(g_rdCode, ip, RD_PORT);
    // Status shows both the code and relay info
    g_rdStatusMsg="Code registered to relay  •  LAN: "+ip+":"+to_string(RD_PORT);

    g_phoneRemoteRunning=false; g_connectedClients=0; g_rdFps=0;
    s_active=true;
    thread(AcceptLoop).detach();
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}

void RdStopServer(){
    s_active=false;
    // ── NEW: Remove from relay ────────────────────────────────────
    if(!g_rdCode.empty()) RelayUnregisterSession(g_rdCode);

    if(s_clientSock!=INVALID_SOCKET){closesocket(s_clientSock);s_clientSock=INVALID_SOCKET;}
    if(s_listenSock!=INVALID_SOCKET){closesocket(s_listenSock);s_listenSock=INVALID_SOCKET;}
    g_rdState=RdState::Idle;
    g_rdCode=""; g_rdPhoneName=""; g_rdPhoneIp="";
    g_rdStatusMsg="\"Generate Code\" চাপো — Phone এ code টাইপ করো";
    g_phoneRemoteRunning=false; g_connectedClients=0; g_rdFps=0;
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}

void RdTimerTick(){}

// ── Input from phone screen area (not used — PC is sender) ────────
void ProcessPhoneRemoteKey(WPARAM /*vk*/, bool /*keyDown*/){}

// ── Draw helpers ──────────────────────────────────────────────────
static void RoundRect(Graphics& g, Brush& fill, Pen* pen,
                      float x,float y,float w,float h,float r=10.f){
    GraphicsPath p; float d=r*2;
    p.AddArc(x,y,d,d,180,90); p.AddArc(x+w-d,y,d,d,270,90);
    p.AddArc(x+w-d,y+h-d,d,d,0,90); p.AddArc(x,y+h-d,d,d,90,90);
    p.CloseFigure(); g.FillPath(&fill,&p); if(pen) g.DrawPath(pen,&p);
}

// ── Draw 6 digit code boxes (RustDesk style) ─────────────────────
static void DrawCode(Graphics& g, const wstring& code,
                     float x,float y,float w,float h){
    FontFamily ff(L"Segoe UI");
    StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);

    float boxW=(w-80)/6.f, boxH=h;
    float gapX=8.f, groupGap=24.f;
    float startX=x+40;

    for(int i=0;i<6;i++){
        float bx=startX + i*(boxW+gapX) + (i>=3?groupGap:0);
        SolidBrush boxBg(Color(255,30,30,50));
        Pen boxBrd(Color(255,0,180,220),2.f);
        RoundRect(g,boxBg,&boxBrd,bx,y,boxW,boxH,8);
        if(i<(int)code.size()){
            wstring digit(1,code[i]);
            SolidBrush wh(Color(255,240,240,255));
            Font bigF(&ff,32,FontStyleBold,UnitPixel);
            g.DrawString(digit.c_str(),-1,&bigF,RectF(bx,y,boxW,boxH),&fmt,&wh);
        } else {
            SolidBrush gr(Color(255,60,60,80));
            Font bigF(&ff,28,FontStyleBold,UnitPixel);
            g.DrawString(L"—",-1,&bigF,RectF(bx,y,boxW,boxH),&fmt,&gr);
        }
    }
    // Separator dots
    float midX=startX+3*(boxW+gapX)+groupGap/2-2;
    SolidBrush dotC(Color(255,0,180,220));
    g.FillEllipse(&dotC,midX,y+boxH/2-6.f,5.f,5.f);
    g.FillEllipse(&dotC,midX,y+boxH/2+4.f,5.f,5.f);
}

// ── Main draw ─────────────────────────────────────────────────────
void DrawPhoneRemoteTab(Graphics& g, float x, float y, float w, float h){
    s_drawX=x; s_drawY=y; s_drawW=w; s_drawH=h;

    FontFamily ff(L"Segoe UI");
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    SolidBrush bg(Color(255,14,14,22)); g.FillRectangle(&bg,x,y,w,h);

    SolidBrush cyan(Color(255,0,200,230));
    SolidBrush white(Color(255,220,220,238));
    SolidBrush gray(Color(255,110,110,135));
    SolidBrush green(Color(255,40,200,100));

    float cx=x+24, cw=w-48;

    // Title
    Font fTitle(&ff,17,FontStyleBold,UnitPixel);
    g.DrawString(L"PC ↔ Phone Remote Control",-1,&fTitle,RectF(cx,y+14,cw,28),&fmtL,&cyan);
    Font fSub(&ff,11,FontStyleRegular,UnitPixel);
    g.DrawString(L"Phone এ code দিলেই connect হবে — IP দিতে হবে না (Relay Server)",-1,&fSub,
                 RectF(cx,y+44,cw,20),&fmtL,&gray);

    float py=y+76;

    // ══ CONNECTED STATE ══════════════════════════════════════════
    if(g_rdState==RdState::Connected){
        SolidBrush connBg(Color(255,12,60,32));
        Pen connBrd(Color(255,0,180,80),1.f);
        RoundRect(g,connBg,&connBrd,cx,py,cw,44,10);
        Font fB(&ff,13,FontStyleBold,UnitPixel);
        wstring lbl=L"● Streaming to: "+ToWStr(g_rdPhoneName)+L"  ("+ToWStr(g_rdPhoneIp)+L")";
        g.DrawString(lbl.c_str(),-1,&fB,RectF(cx+14,py,cw-28,44),&fmtL,&green);

        Font fR(&ff,11,FontStyleRegular,UnitPixel);
        wstring fps=ToWStr(to_string(g_rdFps))+L" fps  •  H.264  •  "+
                    ToWStr(to_string(TARGET_BPS/1000000))+L" Mbps  •  Relay ON";
        g.DrawString(fps.c_str(),-1,&fR,RectF(cx,py+54,cw,20),&fmtC,&gray);
        py+=86;

        // Code display
        Font fLbl(&ff,11,FontStyleRegular,UnitPixel);
        g.DrawString(L"Session Code",-1,&fLbl,RectF(cx,py,cw,20),&fmtC,&gray);
        py+=24;
        DrawCode(g,ToWStr(g_rdCode),cx,py,cw,68);
        py+=84;

        // Input toggle + Disconnect
        bool ienabled=g_rdInputEnabled;
        SolidBrush itBg(ienabled?Color(255,10,80,40):Color(255,45,45,65));
        Pen itBrd(ienabled?Color(255,0,160,80):Color(255,70,70,100),1.f);
        RoundRect(g,itBg,&itBrd,cx,py,cw/2-6,40,8);
        Font fBtn(&ff,12,FontStyleBold,UnitPixel);
        g.DrawString(ienabled?L"✓ Input: ON":L"✗ Input: OFF",-1,&fBtn,
                     RectF(cx,py,cw/2-6,40),&fmtC,&white);

        float dx=cx+cw/2+6;
        SolidBrush stopBg(s_hovStop?Color(255,180,30,30):Color(255,140,20,20));
        Pen stopBrd(Color(255,200,50,50),1.f);
        RoundRect(g,stopBg,&stopBrd,dx,py,cw/2-6,40,8);
        g.DrawString(L"Disconnect",-1,&fBtn,RectF(dx,py,cw/2-6,40),&fmtC,&white);
        py+=56;

        g.DrawString(L"Phone এ RasFocus খুলে code টাইপ করলে নতুন session হবে",-1,
                     &Font(&ff,10,FontStyleRegular,UnitPixel),RectF(cx,py,cw,20),&fmtC,&gray);
        return;
    }

    // ══ WAITING STATE ═════════════════════════════════════════════
    if(g_rdState==RdState::WaitPhone){
        Font fLbl(&ff,13,FontStyleRegular,UnitPixel);
        g.DrawString(L"Phone এ RasFocus খুলে এই Code দাও:",-1,&fLbl,RectF(cx,py,cw,24),&fmtL,&gray);
        py+=30;

        DrawCode(g,ToWStr(g_rdCode),cx,py,cw,80);
        py+=96;

        // Copy code button
        SolidBrush copyBg(s_hovCopyCode?Color(255,40,80,100):Color(255,25,55,75));
        Pen copyBrd(Color(255,0,140,170),1.f);
        RoundRect(g,copyBg,&copyBrd,cx,py,cw/2-4,34,8);
        Font fBtn(&ff,11,FontStyleBold,UnitPixel);
        g.DrawString(L"⎘  Copy Code",-1,&fBtn,RectF(cx,py,cw/2-4,34),&fmtC,&white);
        py+=44;

        // Blinking wait text
        DWORD tick=GetTickCount()/600;
        SolidBrush waitC(tick%2==0?Color(255,0,200,230):Color(255,0,140,170));
        Font fWait(&ff,12,FontStyleBold,UnitPixel);
        g.DrawString(L"⌛ Phone এর সংযোগের অপেক্ষায়...",-1,&fWait,
                     RectF(cx,py,cw,26),&fmtC,&waitC);
        py+=36;

        // Status (relay info)
        g.DrawString(ToWStr(g_rdStatusMsg).c_str(),-1,
                     &Font(&ff,10,FontStyleRegular,UnitPixel),
                     RectF(cx,py,cw,20),&fmtC,&gray);
        py+=36;

        // New Code button
        SolidBrush newBg(s_hovGenerate?Color(255,0,140,160):Color(255,0,110,130));
        Pen newBrd(Color(255,0,180,200),1.f);
        RoundRect(g,newBg,&newBrd,cx,py,cw,46,10);
        Font fBigBtn(&ff,13,FontStyleBold,UnitPixel);
        g.DrawString(L"↻ Generate New Code",-1,&fBigBtn,RectF(cx,py,cw,46),&fmtC,&white);
        py+=62;

        // Stop
        SolidBrush stopBg(s_hovStop?Color(255,100,25,25):Color(255,70,18,18));
        Pen stopBrd(Color(255,140,40,40),1.f);
        RoundRect(g,stopBg,&stopBrd,cx,py,cw,36,8);
        g.DrawString(L"Stop Server",-1,&Font(&ff,11,FontStyleBold,UnitPixel),
                     RectF(cx,py,cw,36),&fmtC,&white);
        return;
    }

    // ══ IDLE STATE ═══════════════════════════════════════════════
    float btnH=70;
    float btnY=y+h/2-btnH/2-30;
    SolidBrush genBg(s_hovGenerate?Color(255,0,160,185):Color(255,0,130,155));
    Pen genBrd(Color(255,0,190,220),2.f);
    RoundRect(g,genBg,&genBrd,cx,btnY,cw,btnH,14);
    Font fBig(&ff,18,FontStyleBold,UnitPixel);
    g.DrawString(L"▶  Generate Code",-1,&fBig,RectF(cx,btnY,cw,btnH),&fmtC,&white);

    // Description cards
    float dy=btnY+btnH+24;
    struct Tip { const wchar_t* icon; const wchar_t* txt; };
    Tip tips[]={
        {L"📱",L"Phone এ RasFocus খুলে \"PC Remote\" ট্যাবে যাও"},
        {L"🔢",L"6-digit code টাইপ করো — IP দিতে হবে না"},
        {L"🖥️",L"PC screen live video phone এ দেখাবে (H.264)"},
        {L"🖱️",L"Phone touch → PC mouse হিসেবে কাজ করবে"},
        {L"🌐",L"Relay server — ভিন্ন নেটওয়ার্ক থেকেও কাজ করে"},
    };
    Font fTip(&ff,11,FontStyleRegular,UnitPixel);
    Font fIcon(&ff,13,FontStyleRegular,UnitPixel);
    for(auto& t:tips){
        g.DrawString(t.icon,-1,&fIcon,RectF(cx,dy,28,22),&fmtL,&cyan);
        g.DrawString(t.txt,-1,&fTip,RectF(cx+32,dy,cw-32,22),&fmtL,&gray);
        dy+=22;
    }

    if(g_rdState==RdState::Error){
        SolidBrush errC(Color(255,215,70,70));
        g.DrawString(ToWStr(g_rdStatusMsg).c_str(),-1,&Font(&ff,11,FontStyleRegular,UnitPixel),
                     RectF(cx,dy+8,cw,50),&fmtC,&errC);
    }
}

// ── Mouse move (hover tracking) ───────────────────────────────────
void ProcessPhoneRemoteMouseMove(float mx, float my, float, float){
    float cx=s_drawX+24, cw=s_drawW-48;
    float py=s_drawY+76;

    s_hovGenerate=false; s_hovStop=false; s_hovCopyCode=false;

    if(g_rdState==RdState::Connected){
        py+=86+24+80+10;
        float dx=cx+cw/2+6;
        s_hovStop=(mx>=dx&&mx<=dx+cw/2-6&&my>=py&&my<=py+40);
    } else if(g_rdState==RdState::WaitPhone){
        py+=30+80+10; // after code
        s_hovCopyCode=(mx>=cx&&mx<=cx+cw/2-4&&my>=py&&my<=py+34);
        py+=44+36+36;
        s_hovGenerate=(mx>=cx&&mx<=cx+cw&&my>=py&&my<=py+46);
        py+=62;
        s_hovStop=(mx>=cx&&mx<=cx+cw&&my>=py&&my<=py+36);
    } else {
        float btnH=70, btnY=s_drawY+s_drawH/2-btnH/2-30;
        s_hovGenerate=(mx>=cx&&mx<=cx+cw&&my>=btnY&&my<=btnY+btnH);
    }
}

// ── Mouse click ───────────────────────────────────────────────────
void ProcessPhoneRemoteMouseClick(float mx, float my, float, float, HWND hWnd){
    float cx=s_drawX+24, cw=s_drawW-48;
    float py=s_drawY+76;

    if(g_rdState==RdState::Connected){
        py+=86+24+80+10;
        // Input toggle
        if(mx>=cx&&mx<=cx+cw/2-6&&my>=py&&my<=py+40){
            g_rdInputEnabled=!g_rdInputEnabled;
            InvalidateRect(hWnd,NULL,FALSE); return;
        }
        // Disconnect
        float dx=cx+cw/2+6;
        if(mx>=dx&&mx<=dx+cw/2-6&&my>=py&&my<=py+40){
            RdStopServer(); InvalidateRect(hWnd,NULL,FALSE); return;
        }
    } else if(g_rdState==RdState::WaitPhone){
        py+=30+80+10;
        // Copy code
        if(mx>=cx&&mx<=cx+cw/2-4&&my>=py&&my<=py+34){
            CopyToClipboard(g_rdCode);
            InvalidateRect(hWnd,NULL,FALSE); return;
        }
        py+=44+36+36;
        // New code
        if(mx>=cx&&mx<=cx+cw&&my>=py&&my<=py+46){
            RdGenerateCode(); InvalidateRect(hWnd,NULL,FALSE); return;
        }
        py+=62;
        // Stop
        if(mx>=cx&&mx<=cx+cw&&my>=py&&my<=py+36){
            RdStopServer(); InvalidateRect(hWnd,NULL,FALSE); return;
        }
    } else {
        // Idle — Generate
        float btnH=70, btnY=s_drawY+s_drawH/2-btnH/2-30;
        if(mx>=cx&&mx<=cx+cw&&my>=btnY&&my<=btnY+btnH){
            RdGenerateCode(); InvalidateRect(hWnd,NULL,FALSE); return;
        }
    }
}

extern "C" void PhoneRemoteChar(wchar_t /*ch*/){}
