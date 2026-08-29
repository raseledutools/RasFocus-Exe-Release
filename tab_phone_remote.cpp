// ================================================================
// tab_phone_remote.cpp  —  RustDesk-style native phone remote
//
// OLD: PC = HTTP server → phone browser opens URL → HTML control
// NEW: PC = WebSocket CLIENT → connects to phone RemoteDesktopService
//      Phone streams JPEG frames → PC renders via GDI+, no browser.
//
// Flow:
//   1. User types phone IP (or PIN-based UDP discovery)
//   2. PC connects to ws://phone-ip:9224
//   3. Phone sends device info JSON first
//   4. Phone streams binary JPEG frames continuously
//   5. PC decodes JPEG → GDI+ Bitmap → renders in screen area
//   6. PC mouse/key → JSON → phone AccessibilityService injects
//
// Inspired by RustDesk open source (MIT License):
//   https://github.com/rustdesk/rustdesk
// ================================================================

#pragma warning(disable: 4996)
#pragma warning(disable: 4244)

#include <winsock2.h>
#include <ws2tcpip.h>
#include "tab_phone_remote.h"
#include "globals.h"

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <wincrypt.h>

using namespace Gdiplus;
using namespace std;

// ── Globals (extern in .h) ─────────────────────────────────────────
RdState     g_rdState        = RdState::Disconnected;
string      g_rdPhoneId      = "";
string      g_rdPhoneIp      = "";
int         g_rdPhonePort    = 9224;
string      g_rdPhoneName    = "";
int         g_rdPhoneW       = 1080;
int         g_rdPhoneH       = 1920;
string      g_rdStatusMsg    = "Phone IP লিখে Connect চাপো";
int         g_rdFps          = 0;
bool        g_rdInputEnabled = true;

// legacy compat globals
bool        g_phoneRemoteRunning = false;
int         g_phoneRemotePort    = 9222;
int         g_phoneRemoteUdpPort = 9223;
int         g_connectedClients   = 0;
string      g_phoneRemotePin     = "";

// ── Internal ───────────────────────────────────────────────────────
static SOCKET       s_sock    = INVALID_SOCKET;
static atomic<bool> s_active  { false };
static mutex        s_bmpMtx;
static Bitmap*      s_frameBmp = nullptr;
static int          s_fpsCount = 0;
static DWORD        s_fpsTimer = 0;

// UI state
static wstring s_inputIp     = L"";
static bool    s_hovConnect  = false;
static bool    s_hovDisconn  = false;
static bool    s_inputFocus  = false;
static float   s_drawX=0, s_drawY=0, s_drawW=0, s_drawH=0;
static float   s_viewX=0, s_viewY=0, s_viewW=0, s_viewH=0;

extern HWND hParentWnd;

// ── String helpers ────────────────────────────────────────────────
static string WStr(const wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);
    string s(n,' ');
    WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),&s[0],n,nullptr,nullptr);
    return s;
}
static wstring ToWStr(const string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);
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

// ── Base64 + SHA1 for WS handshake ───────────────────────────────
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
static string Sha1B64(const string& s){
    HCRYPTPROV hp=0; HCRYPTHASH hh=0;
    if(!CryptAcquireContextA(&hp,NULL,NULL,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT)) return "";
    CryptCreateHash(hp,CALG_SHA1,0,0,&hh);
    CryptHashData(hh,(const BYTE*)s.c_str(),(DWORD)s.size(),0);
    BYTE hash[20]; DWORD hl=20;
    CryptGetHashParam(hh,HP_HASHVAL,hash,&hl,0);
    CryptDestroyHash(hh); CryptReleaseContext(hp,0);
    return B64Enc(vector<BYTE>(hash,hash+20));
}

// ── WebSocket send (client — must mask) ───────────────────────────
static bool WsSendText(SOCKET s, const string& txt) {
    if(s==INVALID_SOCKET) return false;
    size_t len=txt.size(); vector<BYTE> f;
    f.push_back(0x81);
    if(len<126)      f.push_back((BYTE)(len|0x80));
    else if(len<65536){ f.push_back(0xFE); f.push_back((BYTE)(len>>8)); f.push_back((BYTE)(len&0xFF)); }
    else { f.push_back(0xFF); for(int i=7;i>=0;i--) f.push_back((BYTE)((len>>(8*i))&0xFF)); }
    BYTE mask[4]={0x37,0xC5,0xA2,0x1B};
    f.push_back(mask[0]); f.push_back(mask[1]); f.push_back(mask[2]); f.push_back(mask[3]);
    for(size_t i=0;i<len;i++) f.push_back((BYTE)(txt[i]^mask[i%4]));
    return send(s,(char*)f.data(),(int)f.size(),0) > 0;
}

// ── WebSocket recv frame ──────────────────────────────────────────
static bool RecvExact(SOCKET s, char* buf, int n){
    int got=0;
    while(got<n){ int r=recv(s,buf+got,n-got,0); if(r<=0) return false; got+=r; }
    return true;
}
// returns {opcode, payload};  opcode -1 = error/closed
static pair<int,vector<BYTE>> WsRecvFrame(SOCKET s){
    BYTE h[2]; if(!RecvExact(s,(char*)h,2)) return {-1,{}};
    int opcode=h[0]&0x0F;
    bool masked=(h[1]&0x80)!=0;
    size_t len=h[1]&0x7F;
    if(len==126){ BYTE e[2]; if(!RecvExact(s,(char*)e,2)) return {-1,{}}; len=((size_t)e[0]<<8)|e[1]; }
    else if(len==127){ BYTE e[8]; if(!RecvExact(s,(char*)e,8)) return {-1,{}};
        len=0; for(int i=0;i<8;i++) len=(len<<8)|e[i]; }
    if(len>8*1024*1024) return {-1,{}};
    BYTE mask[4]={0}; if(masked) if(!RecvExact(s,(char*)mask,4)) return {-1,{}};
    vector<BYTE> data(len); size_t got=0;
    while(got<len){
        int r=recv(s,(char*)data.data()+got,(int)min((size_t)65536,len-got),0);
        if(r<=0) return {-1,{}}; got+=r;
    }
    if(masked) for(size_t i=0;i<len;i++) data[i]^=mask[i%4];
    return {opcode,data};
}

// ── JPEG bytes → GDI+ Bitmap ─────────────────────────────────────
static Bitmap* JpegToBitmap(const vector<BYTE>& jpg){
    HGLOBAL hg=GlobalAlloc(GMEM_MOVEABLE,(SIZE_T)jpg.size());
    if(!hg) return nullptr;
    void* p=GlobalLock(hg); if(!p){GlobalFree(hg);return nullptr;}
    memcpy(p,jpg.data(),jpg.size()); GlobalUnlock(hg);
    IStream* ps=nullptr;
    if(CreateStreamOnHGlobal(hg,TRUE,&ps)!=S_OK){GlobalFree(hg);return nullptr;}
    Bitmap* bmp=Bitmap::FromStream(ps); ps->Release();
    if(!bmp||bmp->GetLastStatus()!=Ok){delete bmp;return nullptr;}
    return bmp;
}

// ── Receive loop (background thread) ─────────────────────────────
static void RecvLoop(){
    DWORD fpsTimer=GetTickCount(); int fpsCount=0;
    while(s_active && s_sock!=INVALID_SOCKET){
        auto [opcode,data]=WsRecvFrame(s_sock);
        if(opcode<0||opcode==8){
            g_rdStatusMsg="Disconnected"; g_rdState=RdState::Disconnected;
            g_phoneRemoteRunning=false; break;
        }
        if(opcode==1){
            // JSON text frame
            string msg(data.begin(),data.end());
            if(Jget(msg,"type")=="info"){
                g_rdPhoneId   =Jget(msg,"id");
                g_rdPhoneName =Jget(msg,"device");
                int pw=atoi(Jget(msg,"width").c_str());
                int ph=atoi(Jget(msg,"height").c_str());
                if(pw>0) g_rdPhoneW=pw;
                if(ph>0) g_rdPhoneH=ph;
                g_rdStatusMsg="Connected: "+(g_rdPhoneName.empty()?"Phone":g_rdPhoneName);
                g_rdState=RdState::Connected;
                g_phoneRemoteRunning=true;
            }
        } else if(opcode==2){
            // Binary frame = JPEG
            fpsCount++;
            DWORD now=GetTickCount();
            if(now-fpsTimer>=1000){ g_rdFps=fpsCount; fpsCount=0; fpsTimer=now; }
            Bitmap* bmp=JpegToBitmap(data);
            if(bmp){
                lock_guard<mutex> lk(s_bmpMtx);
                delete s_frameBmp; s_frameBmp=bmp;
            }
            if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
        }
    }
    if(s_sock!=INVALID_SOCKET){closesocket(s_sock);s_sock=INVALID_SOCKET;}
    g_rdState=RdState::Disconnected;
    g_phoneRemoteRunning=false;
    s_active=false;
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}

// ── WebSocket handshake (client) ──────────────────────────────────
static bool DoHandshake(SOCKET s, const string& host){
    string key="dGhlIHNhbXBsZSBub25jZQ=="; // fixed test key — our server accepts any
    string req=
        "GET / HTTP/1.1\r\nHost: "+host+"\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: "+key+"\r\nSec-WebSocket-Version: 13\r\n\r\n";
    if(send(s,req.c_str(),(int)req.size(),0)!=(int)req.size()) return false;
    char buf[2048]={}; int total=0;
    while(total<(int)sizeof(buf)-1){
        int r=recv(s,buf+total,1,0); if(r<=0) break;
        total+=r; if(total>=4&&strstr(buf,"\r\n\r\n")) break;
    }
    return strstr(buf,"101")!=nullptr;
}

// ── Public: Connect ───────────────────────────────────────────────
void RdConnect(const string& ip, int port){
    if(s_active) RdDisconnect();
    if(ip.empty()){g_rdStatusMsg="IP দাও আগে";return;}
    g_rdState=RdState::Connecting;
    g_rdPhoneIp=ip; g_rdPhonePort=port;
    g_rdStatusMsg="Connecting "+ip+":"+to_string(port)+"...";
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);

    thread([ip,port](){
        WSADATA wd; WSAStartup(MAKEWORD(2,2),&wd);
        SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(s==INVALID_SOCKET){g_rdStatusMsg="Socket error";g_rdState=RdState::Error;return;}
        DWORD tv=5000; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
        sockaddr_in addr{}; addr.sin_family=AF_INET;
        addr.sin_port=htons((u_short)port);
        inet_pton(AF_INET,ip.c_str(),&addr.sin_addr);
        if(connect(s,(sockaddr*)&addr,sizeof(addr))!=0){
            closesocket(s);
            g_rdStatusMsg="Connect failed — phone reachable? Service চালু?";
            g_rdState=RdState::Error;
            if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
            return;
        }
        tv=3000; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
        if(!DoHandshake(s,ip+":"+to_string(port))){
            closesocket(s);
            g_rdStatusMsg="WebSocket handshake failed";
            g_rdState=RdState::Error;
            if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
            return;
        }
        s_sock=s; s_active=true;
        g_rdStatusMsg="Connected — waiting for stream...";
        RecvLoop();
    }).detach();
}

// ── Public: Disconnect ────────────────────────────────────────────
void RdDisconnect(){
    s_active=false;
    if(s_sock!=INVALID_SOCKET){closesocket(s_sock);s_sock=INVALID_SOCKET;}
    g_rdState=RdState::Disconnected;
    g_rdStatusMsg="Disconnected";
    g_phoneRemoteRunning=false;
    lock_guard<mutex> lk(s_bmpMtx);
    delete s_frameBmp; s_frameBmp=nullptr;
}

void RdTimerTick(){}

// ── Send input to phone ───────────────────────────────────────────
static void SendJson(const string& j){
    if(s_sock!=INVALID_SOCKET&&s_active) WsSendText(s_sock,j);
}
static void SendTouch(int mask,float wx,float wy){
    if(!g_rdInputEnabled||s_viewW<=0||s_viewH<=0) return;
    float rx=(wx-s_viewX)/s_viewW, ry=(wy-s_viewY)/s_viewH;
    if(rx<0||rx>1||ry<0||ry>1) return;
    int px=(int)(rx*g_rdPhoneW), py=(int)(ry*g_rdPhoneH);
    char buf[128]; sprintf(buf,"{\"type\":\"touch\",\"mask\":%d,\"x\":%d,\"y\":%d}",mask,px,py);
    SendJson(buf);
}

void ProcessPhoneRemoteKey(WPARAM vk, bool keyDown){
    if(!g_rdInputEnabled||g_rdState!=RdState::Connected) return;
    int ak=0;
    if(vk==VK_BACK) ak=4; else if(vk==VK_HOME) ak=3; else return;
    char buf[64]; sprintf(buf,"{\"type\":\"key\",\"code\":%d,\"action\":%d}",ak,keyDown?0:1);
    SendJson(buf);
}

// ── Draw helpers ──────────────────────────────────────────────────
static void RoundRect(Graphics& g, Brush& fill, Pen* pen,
                      float x,float y,float w,float h,float r=10.f){
    GraphicsPath p; float d=r*2;
    p.AddArc(x,y,d,d,180,90); p.AddArc(x+w-d,y,d,d,270,90);
    p.AddArc(x+w-d,y+h-d,d,d,0,90); p.AddArc(x,y+h-d,d,d,90,90);
    p.CloseFigure(); g.FillPath(&fill,&p); if(pen) g.DrawPath(pen,&p);
}

// ── DrawPhoneRemoteTab ────────────────────────────────────────────
void DrawPhoneRemoteTab(Graphics& g, float x, float y, float w, float h){
    s_drawX=x; s_drawY=y; s_drawW=w; s_drawH=h;

    FontFamily ff(L"Segoe UI");
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    SolidBrush bgBr(Color(255,18,18,28)); g.FillRectangle(&bgBr,x,y,w,h);

    bool connected=(g_rdState==RdState::Connected);

    // ══ CONNECTED: live screen ════════════════════════════════════
    if(connected){
        float panelW=w*0.65f;
        float sx=x+8,sy=y+8,sw2=panelW-16,sh2=h-16;
        float ar=(float)g_rdPhoneW/(float)g_rdPhoneH, boxAr=sw2/sh2;
        float fw,fh,fx,fy;
        if(ar<boxAr){fh=sh2;fw=fh*ar;fx=sx+(sw2-fw)/2;fy=sy;}
        else{fw=sw2;fh=fw/ar;fx=sx;fy=sy+(sh2-fh)/2;}
        s_viewX=fx;s_viewY=fy;s_viewW=fw;s_viewH=fh;

        {
            lock_guard<mutex> lk(s_bmpMtx);
            if(s_frameBmp&&s_frameBmp->GetLastStatus()==Ok)
                g.DrawImage(s_frameBmp,RectF(fx,fy,fw,fh));
            else{
                SolidBrush bk(Color(255,8,8,16)); g.FillRectangle(&bk,fx,fy,fw,fh);
                Font fw2(&ff,13,FontStyleRegular,UnitPixel);
                SolidBrush gr(Color(255,80,80,100));
                g.DrawString(L"Waiting for frames...",-1,&fw2,RectF(fx,fy,fw,fh),&fmtC,&gr);
            }
        }
        Pen phoneBorder(Color(255,0,200,220),2.f);
        g.DrawRectangle(&phoneBorder,fx,fy,fw,fh);

        // Right panel
        float px2=x+panelW+4,py2=y+8,pw2=w-panelW-12;
        SolidBrush cardBg(Color(255,28,28,44));
        Pen cardBrd(Color(255,0,140,170),1.f);
        RoundRect(g,cardBg,&cardBrd,px2,py2,pw2,h-16,10);

        SolidBrush cyan(Color(255,0,200,230)),white(Color(255,220,220,238)),gray(Color(255,120,120,145));
        Font fBold(&ff,12,FontStyleBold,UnitPixel);
        Font fReg(&ff,11,FontStyleRegular,UnitPixel);

        g.DrawString(L"● Connected",-1,&fBold,RectF(px2+10,py2+10,pw2-20,22),&fmtL,&cyan);
        g.DrawString(ToWStr(g_rdPhoneName.empty()?"Phone":g_rdPhoneName).c_str(),-1,
                     &Font(&ff,13,FontStyleBold,UnitPixel),RectF(px2+10,py2+34,pw2-20,22),&fmtL,&white);
        g.DrawString(ToWStr("ID: "+g_rdPhoneId).c_str(),-1,&fReg,RectF(px2+10,py2+58,pw2-20,18),&fmtL,&gray);
        g.DrawString(ToWStr(to_string(g_rdPhoneW)+"x"+to_string(g_rdPhoneH)).c_str(),
                     -1,&fReg,RectF(px2+10,py2+78,pw2-20,18),&fmtL,&gray);
        g.DrawString(ToWStr(to_string(g_rdFps)+" fps").c_str(),
                     -1,&fReg,RectF(px2+10,py2+98,pw2-20,18),&fmtL,&cyan);

        // Disconnect btn
        float dby=py2+126;
        SolidBrush dBg(s_hovDisconn?Color(255,180,30,30):Color(255,140,20,20));
        Pen dBrd(Color(255,200,50,50),1.f);
        RoundRect(g,dBg,&dBrd,px2+8,dby,pw2-16,36,8);
        g.DrawString(L"Disconnect",-1,&fBold,RectF(px2+8,dby,pw2-16,36),&fmtC,&white);

        // Input toggle
        float ity=dby+46;
        SolidBrush itBg(g_rdInputEnabled?Color(255,10,110,55):Color(255,50,50,70));
        RoundRect(g,itBg,nullptr,px2+8,ity,pw2-16,32,8);
        g.DrawString(g_rdInputEnabled?L"Input: ON":L"Input: OFF",-1,
                     &fBold,RectF(px2+8,ity,pw2-16,32),&fmtC,&white);

        // Quick keys: Back, Home, Recents
        float qky=ity+42; float qkw=(pw2-24)/3.f;
        const wchar_t* qkL[3]={L"Back",L"Home",L"≡"};
        for(int i=0;i<3;i++){
            float qx=px2+8+i*(qkw+4);
            SolidBrush qb(Color(255,40,40,60)); Pen qp(Color(255,70,70,100),1.f);
            RoundRect(g,qb,&qp,qx,qky,qkw,30,6);
            g.DrawString(qkL[i],-1,&fReg,RectF(qx,qky,qkw,30),&fmtC,&gray);
        }
        return;
    }

    // ══ DISCONNECTED: connect UI ══════════════════════════════════
    float cx=x+20, cw=w-40;

    SolidBrush cyan(Color(255,0,200,230)),white(Color(255,220,220,238)),gray(Color(255,120,120,145));
    Font fTitle(&ff,16,FontStyleBold,UnitPixel);
    g.DrawString(L"Phone Remote",-1,&fTitle,RectF(cx,y+14,cw,28),&fmtL,&cyan);
    Font fSub(&ff,11,FontStyleRegular,UnitPixel);
    g.DrawString(L"Browser ছাড়া — RustDesk-style native control",-1,&fSub,RectF(cx,y+42,cw,20),&fmtL,&gray);

    // IP input card
    float iy=y+72;
    SolidBrush cardBg(Color(255,28,28,44)); Pen cardBrd(Color(255,55,55,85),1.f);
    RoundRect(g,cardBg,&cardBrd,cx,iy,cw,106,10);
    Font fLbl(&ff,11,FontStyleRegular,UnitPixel);
    g.DrawString(L"Phone IP Address",-1,&fLbl,RectF(cx+12,iy+8,cw-24,18),&fmtL,&gray);

    // input box
    float ibx=cx+12,iby=iy+28,ibw=cw-24,ibh=38;
    SolidBrush ibBg(s_inputFocus?Color(255,18,18,38):Color(255,12,12,24));
    Pen ibBrd(s_inputFocus?Color(255,0,200,230):Color(255,65,65,95),1.5f);
    RoundRect(g,ibBg,&ibBrd,ibx,iby,ibw,ibh,8);
    Font fInp(&ff,15,FontStyleRegular,UnitPixel);
    wstring disp=s_inputIp.empty()?L"192.168.x.x":s_inputIp+(s_inputFocus?L"|":L"");
    SolidBrush inputC(s_inputIp.empty()?Color(255,65,65,95):Color(255,220,220,240));
    g.DrawString(disp.c_str(),-1,&fInp,RectF(ibx+10,iby,ibw-20,ibh),&fmtL,&inputC);
    g.DrawString(L"Port: 9224",-1,&fLbl,RectF(cx+12,iy+72,cw-24,18),&fmtL,&gray);

    // Connect button
    float by=iy+116; bool canConn=!s_inputIp.empty()&&g_rdState!=RdState::Connecting;
    SolidBrush btnBg(g_rdState==RdState::Connecting?Color(255,40,70,90)
                    :(s_hovConnect&&canConn?Color(255,0,160,185):Color(255,0,130,155)));
    Pen btnBrd(Color(255,0,180,205),1.f);
    RoundRect(g,btnBg,&btnBrd,cx,by,cw,46,10);
    Font fBtn(&ff,13,FontStyleBold,UnitPixel);
    g.DrawString(g_rdState==RdState::Connecting?L"Connecting...":L"Connect",
                 -1,&fBtn,RectF(cx,by,cw,46),&fmtC,&white);

    // Status
    if(!g_rdStatusMsg.empty()){
        Color sc=g_rdState==RdState::Error?Color(255,215,75,75)
               :g_rdState==RdState::Connecting?Color(255,180,155,50)
               :Color(255,75,185,100);
        SolidBrush sb(sc); Font fSt(&ff,11,FontStyleRegular,UnitPixel);
        g.DrawString(ToWStr(g_rdStatusMsg).c_str(),-1,&fSt,RectF(cx,by+54,cw,50),&fmtL,&sb);
    }

    // How to use
    float hy=by+108;
    SolidBrush hBg(Color(255,24,24,40)); Pen hBrd(Color(255,48,48,72),1.f);
    RoundRect(g,hBg,&hBrd,cx,hy,cw,138,10);
    g.DrawString(L"ব্যবহার পদ্ধতি:",-1,&fLbl,RectF(cx+12,hy+8,cw-24,20),&fmtL,&cyan);
    const wchar_t* steps[]={
        L"① Phone এ RasFocus → FileManager → PC Remote",
        L"② Share Screen tab → Start Sharing চাপো",
        L"③ Settings tab এ Local IP টা কপি করো",
        L"④ এখানে ঐ IP লিখে Connect চাপো",
        L"⑤ Phone screen সরাসরি এই window তে দেখাবে",
    };
    float sy=hy+30;
    Font fStep(&ff,10.5f,FontStyleRegular,UnitPixel);
    for(auto* st:steps){ g.DrawString(st,-1,&fStep,RectF(cx+12,sy,cw-24,22),&fmtL,&gray); sy+=22; }
}

// ── Mouse move (hover) ────────────────────────────────────────────
void ProcessPhoneRemoteMouseMove(float mx, float my, float, float){
    if(g_rdState==RdState::Connected){
        float panelW=s_drawW*0.65f;
        float px2=s_drawX+panelW+4,py2=s_drawY+8,pw2=s_drawW-panelW-12;
        float dby=py2+126;
        s_hovDisconn=(mx>=px2+8&&mx<=px2+pw2-8&&my>=dby&&my<=dby+36);
    } else {
        float cx=s_drawX+20,cw=s_drawW-40;
        float by=s_drawY+72+116;
        s_hovConnect=(mx>=cx&&mx<=cx+cw&&my>=by&&my<=by+46);
    }
}

// ── Mouse click ───────────────────────────────────────────────────
void ProcessPhoneRemoteMouseClick(float mx, float my, float, float, HWND hWnd){
    if(g_rdState==RdState::Connected){
        float panelW=s_drawW*0.65f;
        float px2=s_drawX+panelW+4,py2=s_drawY+8,pw2=s_drawW-panelW-12;

        // Disconnect
        float dby=py2+126;
        if(mx>=px2+8&&mx<=px2+pw2-8&&my>=dby&&my<=dby+36){
            RdDisconnect(); InvalidateRect(hWnd,NULL,FALSE); return;
        }
        // Input toggle
        float ity=dby+46;
        if(mx>=px2+8&&mx<=px2+pw2-8&&my>=ity&&my<=ity+32){
            g_rdInputEnabled=!g_rdInputEnabled; InvalidateRect(hWnd,NULL,FALSE); return;
        }
        // Quick keys
        float qky=ity+42; float qkw=(pw2-24)/3.f;
        int androidKeys[]={4,3,187};
        if(my>=qky&&my<=qky+30){
            for(int i=0;i<3;i++){
                float qx=px2+8+i*(qkw+4);
                if(mx>=qx&&mx<=qx+qkw){
                    char buf[64];
                    sprintf(buf,"{\"type\":\"key\",\"code\":%d,\"action\":0}",androidKeys[i]); SendJson(buf);
                    sprintf(buf,"{\"type\":\"key\",\"code\":%d,\"action\":1}",androidKeys[i]); SendJson(buf);
                    return;
                }
            }
        }
        // Screen tap
        if(s_viewW>0&&s_viewH>0&&mx>=s_viewX&&mx<=s_viewX+s_viewW&&my>=s_viewY&&my<=s_viewY+s_viewH){
            SendTouch(1,mx,my); // MASK_DOWN
            Sleep(30);
            SendTouch(2,mx,my); // MASK_UP
        }
    } else {
        float cx=s_drawX+20,cw=s_drawW-40;
        float iy=s_drawY+72;
        float ibx=cx+12,iby=iy+28,ibw=cw-24,ibh=38;
        float by=iy+116;

        if(mx>=ibx&&mx<=ibx+ibw&&my>=iby&&my<=iby+ibh){
            s_inputFocus=true; InvalidateRect(hWnd,NULL,FALSE); return;
        }
        s_inputFocus=false;

        if(mx>=cx&&mx<=cx+cw&&my>=by&&my<=by+46&&!s_inputIp.empty()
           &&g_rdState!=RdState::Connecting){
            RdConnect(WStr(s_inputIp),g_rdPhonePort);
            InvalidateRect(hWnd,NULL,FALSE);
        }
    }
}

// ── WM_CHAR handler for IP input ─────────────────────────────────
extern "C" void PhoneRemoteChar(wchar_t ch){
    if(!s_inputFocus) return;
    if(ch=='\b'){ if(!s_inputIp.empty()) s_inputIp.pop_back(); }
    else if(ch=='\r'){
        if(!s_inputIp.empty()&&g_rdState!=RdState::Connecting)
            RdConnect(WStr(s_inputIp),g_rdPhonePort);
    }
    else if(ch>=L' '&&s_inputIp.size()<40) s_inputIp+=ch;
}
