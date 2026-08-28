// ================================================================
// tab_phone_remote.cpp  —  RustDesk-style PIN Connect
//
// How it works:
//   1. PC generates a stable 6-digit PIN from its local IP.
//   2. PC runs two servers:
//        - UDP beacon on port 9223  →  responds to phone discovery
//        - HTTP+WebSocket on port 9222  →  control panel
//   3. Phone (RasFocus app) enters PIN once.
//      App broadcasts UDP discovery packet containing the PIN.
//      PC beacon responds with its IP.
//      Phone saves IP → next time auto-connects (no PIN needed).
//   4. Phone opens http://<discovered-ip>:9222 → full control.
// ================================================================

// winsock2 must come before windows.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include "tab_phone_remote.h"
#include "globals.h"
#include "phone_remote_html.h"
#include <wincrypt.h>
#include <shellapi.h>

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <sstream>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;
using namespace std;

// ── Globals ──────────────────────────────────────────────────────
bool        g_phoneRemoteRunning = false;
int         g_phoneRemotePort    = 9222;
int         g_phoneRemoteUdpPort = 9223;
int         g_connectedClients   = 0;
std::string g_phoneRemotePin     = "";

static atomic<bool>   s_active { false };
static SOCKET         s_httpSock = INVALID_SOCKET;
static SOCKET         s_udpSock  = INVALID_SOCKET;
static vector<SOCKET> s_wsClients;
static mutex          s_mtx;
static bool           s_hovStart = false, s_hovStop = false;

// ── Get local IP ─────────────────────────────────────────────────
static string GetLocalIp() {
    WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd);
    char host[256] = {}; gethostname(host, sizeof(host));
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    getaddrinfo(host, nullptr, &hints, &res);
    string ip;
    for (auto* p = res; p; p = p->ai_next) {
        char buf[64];
        inet_ntop(AF_INET, &((sockaddr_in*)p->ai_addr)->sin_addr, buf, sizeof(buf));
        if (strncmp(buf, "127.", 4) != 0) { ip = buf; break; }
    }
    if (res) freeaddrinfo(res);
    return ip.empty() ? "127.0.0.1" : ip;
}

// ── Generate 6-digit PIN from IP ─────────────────────────────────
// PIN is stable for a given IP — same IP always gives same PIN.
// Format: 6 decimal digits, e.g. "483920"
static string GeneratePin(const string& ip) {
    // Simple but stable: use last two octets + fixed offset
    // e.g. 192.168.43.159 → "43159" padded/truncated to 6
    unsigned int a=0,b=0,c=0,d=0;
    sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d);
    // Combine to make exactly 6 digits deterministically
    unsigned long val = ((a ^ 0xAB) * 7919UL + (b ^ 0x5C) * 6271UL
                       + (c ^ 0xD3) * 4973UL + (d ^ 0x9E) * 3877UL) % 900000UL + 100000UL;
    char buf[8]; sprintf(buf, "%06lu", val % 900000UL + 100000UL);
    return string(buf, 6);
}

// ── Base64 ───────────────────────────────────────────────────────
static string B64Enc(const vector<BYTE>& d) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string o; o.reserve(((d.size()+2)/3)*4);
    for (size_t i=0; i<d.size(); i+=3) {
        BYTE b0=d[i], b1=(i+1<d.size())?d[i+1]:0, b2=(i+2<d.size())?d[i+2]:0;
        o+=t[b0>>2]; o+=t[((b0&3)<<4)|(b1>>4)];
        o+=(i+1<d.size())?t[((b1&0xF)<<2)|(b2>>6)]:'=';
        o+=(i+2<d.size())?t[b2&0x3F]:'=';
    }
    return o;
}

// ── SHA-1 (WinCrypt) for WebSocket handshake ──────────────────────
static string Sha1B64(const string& s) {
    HCRYPTPROV hp=0; HCRYPTHASH hh=0;
    if (!CryptAcquireContextA(&hp,NULL,NULL,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT)) return "";
    CryptCreateHash(hp,CALG_SHA1,0,0,&hh);
    CryptHashData(hh,(const BYTE*)s.c_str(),(DWORD)s.size(),0);
    BYTE hash[20]; DWORD hl=20;
    CryptGetHashParam(hh,HP_HASHVAL,hash,&hl,0);
    CryptDestroyHash(hh); CryptReleaseContext(hp,0);
    return B64Enc(vector<BYTE>(hash,hash+20));
}

// ── Screen capture → JPEG base64 ─────────────────────────────────
static string ScreenJpeg(int q=25) {
    int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
    int dw=sw/2, dh=sh/2;
    HDC hScr=GetDC(NULL), hMem=CreateCompatibleDC(hScr);
    HBITMAP hBmp=CreateCompatibleBitmap(hScr,dw,dh);
    HBITMAP hOld=(HBITMAP)SelectObject(hMem,hBmp);
    SetStretchBltMode(hMem,HALFTONE);
    StretchBlt(hMem,0,0,dw,dh,hScr,0,0,sw,sh,SRCCOPY);
    SelectObject(hMem,hOld); DeleteDC(hMem); ReleaseDC(NULL,hScr);
    Bitmap bmp(hBmp,NULL); DeleteObject(hBmp);
    CLSID jc; UINT n=0,sz=0;
    GetImageEncodersSize(&n,&sz);
    vector<BYTE> eb(sz);
    GetImageEncoders(n,sz,(ImageCodecInfo*)eb.data());
    for(UINT i=0;i<n;i++) if(!wcscmp(((ImageCodecInfo*)eb.data())[i].MimeType,L"image/jpeg")){jc=((ImageCodecInfo*)eb.data())[i].Clsid;break;}
    EncoderParameters ep; ep.Count=1;
    ep.Parameter[0].Guid=EncoderQuality; ep.Parameter[0].Type=EncoderParameterValueTypeLong;
    ep.Parameter[0].NumberOfValues=1; ULONG qv=(ULONG)q; ep.Parameter[0].Value=&qv;
    IStream* ps=NULL; CreateStreamOnHGlobal(NULL,TRUE,&ps);
    bmp.Save(ps,&jc,&ep);
    STATSTG st; ps->Stat(&st,STATFLAG_NONAME);
    ULONG len=(ULONG)st.cbSize.QuadPart;
    LARGE_INTEGER li; li.QuadPart=0; ps->Seek(li,STREAM_SEEK_SET,NULL);
    vector<BYTE> img(len); ps->Read(img.data(),len,NULL); ps->Release();
    return B64Enc(img);
}

// ── File list JSON ────────────────────────────────────────────────
static string FilesJson(const string& path) {
    string j="["; bool first=true;
    WIN32_FIND_DATAA fd; string pat=path+"\\*";
    HANDLE h=FindFirstFileA(pat.c_str(),&fd);
    if(h==INVALID_HANDLE_VALUE) return "[]";
    do {
        string nm=fd.cFileName; if(nm=="."||nm=="..") continue;
        bool dir=(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        ULONGLONG sz=((ULONGLONG)fd.nFileSizeHigh<<32)|fd.nFileSizeLow;
        if(!first) j+=","; first=false;
        j+="{\"name\":\"";
        for(char c:nm){if(c=='"')j+="\\\""; else j+=c;} j+="\",";
        j+="\"dir\":"; j+=dir?"true":"false"; j+=",\"size\":"; j+=to_string(sz); j+="}";
    } while(FindNextFileA(h,&fd));
    FindClose(h); return j+"]";
}

// ── CMD execute ───────────────────────────────────────────────────
static string RunCmd(const string& cmd) {
    string out, fc="cmd.exe /c \""+cmd+"\" 2>&1";
    SECURITY_ATTRIBUTES sa{sizeof(sa),NULL,TRUE};
    HANDLE hR,hW; if(!CreatePipe(&hR,&hW,&sa,0)) return "pipe error";
    STARTUPINFOA si{}; si.cb=sizeof(si);
    si.hStdOutput=hW; si.hStdError=hW;
    si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi{};
    vector<char> buf(fc.begin(),fc.end()); buf.push_back(0);
    if(CreateProcessA(NULL,buf.data(),NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)){
        CloseHandle(hW); char tmp[1024]; DWORD rd;
        while(ReadFile(hR,tmp,sizeof(tmp)-1,&rd,NULL)&&rd>0){tmp[rd]=0;out+=tmp;}
        WaitForSingleObject(pi.hProcess,5000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    } else { CloseHandle(hW); out="exec failed"; }
    CloseHandle(hR);
    if(out.size()>8192) out=out.substr(0,8192)+"...(truncated)";
    return out;
}

// ── JSON field extract ────────────────────────────────────────────
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

// ── JSON string escape ────────────────────────────────────────────
static string Jescape(const string& s) {
    string o; for(char c:s){
        if(c=='"') o+="\\\""; else if(c=='\\') o+="\\\\";
        else if(c=='\n') o+="\\n"; else if(c=='\r') o+="\\r";
        else if(c=='\t') o+="\\t"; else o+=c;
    } return o;
}

// ── WebSocket frame ───────────────────────────────────────────────
static void WsSend(SOCKET s, const string& txt) {
    size_t len=txt.size(); vector<BYTE> f;
    f.push_back(0x81);
    if(len<126) f.push_back((BYTE)len);
    else if(len<65536){f.push_back(126);f.push_back((BYTE)(len>>8));f.push_back((BYTE)(len&0xFF));}
    else{f.push_back(127);for(int i=7;i>=0;i--)f.push_back((BYTE)((len>>(8*i))&0xFF));}
    f.insert(f.end(),txt.begin(),txt.end());
    send(s,(char*)f.data(),(int)f.size(),0);
}
static string WsRecv(SOCKET s) {
    BYTE h[2]; if(recv(s,(char*)h,2,MSG_WAITALL)!=2) return "";
    bool masked=(h[1]&0x80)!=0; size_t len=h[1]&0x7F;
    if(len==126){BYTE e[2];recv(s,(char*)e,2,MSG_WAITALL);len=((size_t)e[0]<<8)|e[1];}
    else if(len==127){BYTE e[8];recv(s,(char*)e,8,MSG_WAITALL);len=0;for(int i=0;i<8;i++)len=(len<<8)|e[i];}
    BYTE mask[4]={0}; if(masked) recv(s,(char*)mask,4,MSG_WAITALL);
    if(len>65536) return "";
    vector<BYTE> data(len); size_t got=0;
    while(got<len){int r=recv(s,(char*)data.data()+got,(int)(len-got),0);if(r<=0)break;got+=r;}
    if(masked) for(size_t i=0;i<len;i++) data[i]^=mask[i%4];
    return string(data.begin(),data.end());
}

// ── WebSocket client handler ──────────────────────────────────────
static void WsClient(SOCKET client) {
    {lock_guard<mutex> lk(s_mtx); s_wsClients.push_back(client); g_connectedClients++;}
    while(s_active) {
        string msg=WsRecv(client); if(msg.empty()) break;
        string type=Jget(msg,"type"), resp;
        if(type=="shell"){
            resp="{\"type\":\"shell_result\",\"output\":\""+Jescape(RunCmd(Jget(msg,"cmd")))+"\"}";
        } else if(type=="files"){
            string path=Jget(msg,"path"); if(path.empty()) path="C:\\";
            string ep; for(char c:path){if(c=='\\')ep+="\\\\";else ep+=c;}
            resp="{\"type\":\"files_result\",\"path\":\""+ep+"\",\"items\":"+FilesJson(path)+"}";
        } else if(type=="screen"){
            resp="{\"type\":\"screen_frame\",\"jpeg\":\""+ScreenJpeg()+"\"}";
        } else if(type=="mouse"){
            int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
            int px=(int)(atof(Jget(msg,"x").c_str())*sw);
            int py=(int)(atof(Jget(msg,"y").c_str())*sh);
            SetCursorPos(px,py);
            string btn=Jget(msg,"btn"), act=Jget(msg,"act");
            bool right=(btn=="right");
            if(act=="click"||act=="down") mouse_event(right?MOUSEEVENTF_RIGHTDOWN:MOUSEEVENTF_LEFTDOWN,0,0,0,0);
            if(act=="click"||act=="up")   mouse_event(right?MOUSEEVENTF_RIGHTUP:MOUSEEVENTF_LEFTUP,0,0,0,0);
            if(act=="scroll"){string dir=Jget(msg,"dir");mouse_event(MOUSEEVENTF_WHEEL,0,0,(DWORD)(dir=="down"?-120:120),0);}
            if(act=="dblclick"){mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0);mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0);Sleep(50);mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0);mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0);}
            resp="{\"type\":\"ok\"}";
        } else if(type=="key"){
            int vk=atoi(Jget(msg,"vk").c_str());
            if(vk>0){keybd_event((BYTE)vk,0,0,0);keybd_event((BYTE)vk,0,KEYEVENTF_KEYUP,0);}
            resp="{\"type\":\"ok\"}";
        } else if(type=="type"){
            string txt=Jget(msg,"text");
            for(char c:txt){SHORT vk=VkKeyScanA(c);bool sh=(HIBYTE(vk)&1)!=0;BYTE k=LOBYTE(vk);
                if(sh)keybd_event(VK_SHIFT,0,0,0);keybd_event(k,0,0,0);keybd_event(k,0,KEYEVENTF_KEYUP,0);if(sh)keybd_event(VK_SHIFT,0,KEYEVENTF_KEYUP,0);Sleep(8);}
            resp="{\"type\":\"ok\"}";
        } else if(type=="ping"){
            resp="{\"type\":\"pong\",\"pin\":\""+g_phoneRemotePin+"\"}";
        }
        if(!resp.empty()) WsSend(client,resp);
    }
    closesocket(client);
    lock_guard<mutex> lk(s_mtx);
    s_wsClients.erase(remove(s_wsClients.begin(),s_wsClients.end(),client),s_wsClients.end());
    g_connectedClients--;
}

// ── HTTP handler ──────────────────────────────────────────────────
static void HttpClient(SOCKET client) {
    char buf[4096]={}; int r=recv(client,buf,sizeof(buf)-1,0);
    if(r<=0){closesocket(client);return;}
    string req(buf,r);
    bool isWs=req.find("Upgrade: websocket")!=string::npos||req.find("Upgrade: WebSocket")!=string::npos;
    if(isWs){
        size_t kp=req.find("Sec-WebSocket-Key:");
        if(kp==string::npos){closesocket(client);return;}
        kp+=18; while(kp<req.size()&&req[kp]==' ')kp++;
        size_t ke=req.find("\r\n",kp);
        string key=req.substr(kp,ke-kp);
        string acc=Sha1B64(key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        string resp="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+acc+"\r\n\r\n";
        send(client,resp.c_str(),(int)resp.size(),0);
        thread(WsClient,client).detach();
    } else {
        string html=BuildRemoteHtml(g_phoneRemotePin);
        string resp="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: "+to_string(html.size())+"\r\nConnection: close\r\n\r\n"+html;
        send(client,resp.c_str(),(int)resp.size(),0);
        closesocket(client);
    }
}

// ── UDP Beacon — phone sends PIN, PC replies with IP:port ─────────
// Packet from phone: "RASPIN:XXXXXX"
// Reply from PC:     "RASACK:XXXXXX:<ip>:<port>"
static void UdpBeacon() {
    SOCKET s=socket(AF_INET,SOCK_DGRAM,0);
    int opt=1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));
    DWORD tv=500; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons((u_short)g_phoneRemoteUdpPort);
    bind(s,(sockaddr*)&addr,sizeof(addr));
    s_udpSock=s;
    string myIp=GetLocalIp();
    char buf[128]={};
    while(s_active) {
        sockaddr_in from{}; int fl=sizeof(from);
        int r=recvfrom(s,buf,sizeof(buf)-1,0,(sockaddr*)&from,(int*)&fl);
        if(r<=0){memset(buf,0,sizeof(buf));continue;}
        buf[r]=0;
        string msg(buf);
        // Expected: "RASPIN:XXXXXX"
        if(msg.rfind("RASPIN:",0)==0) {
            string pin=msg.substr(7,6);
            if(pin==g_phoneRemotePin) {
                // Reply with our IP and port
                string ack="RASACK:"+g_phoneRemotePin+":"+myIp+":"+to_string(g_phoneRemotePort);
                sendto(s,ack.c_str(),(int)ack.size(),0,(sockaddr*)&from,fl);
            }
        }
        memset(buf,0,sizeof(buf));
    }
    closesocket(s); s_udpSock=INVALID_SOCKET;
}

// ── HTTP server loop ──────────────────────────────────────────────
static void HttpLoop() {
    WSADATA wd; WSAStartup(MAKEWORD(2,2),&wd);
    s_httpSock=socket(AF_INET,SOCK_STREAM,0);
    int opt=1; setsockopt(s_httpSock,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));
    DWORD tv=500; setsockopt(s_httpSock,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
    sockaddr_in addr{}; addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons((u_short)g_phoneRemotePort);
    bind(s_httpSock,(sockaddr*)&addr,sizeof(addr));
    listen(s_httpSock,8);
    while(s_active){
        SOCKET cl=accept(s_httpSock,NULL,NULL);
        if(cl==INVALID_SOCKET) continue;
        thread(HttpClient,cl).detach();
    }
    closesocket(s_httpSock); s_httpSock=INVALID_SOCKET;
    WSACleanup();
}

// ── Public API ────────────────────────────────────────────────────
void PhoneRemoteStartServer() {
    if(s_active) return;
    string ip=GetLocalIp();
    g_phoneRemotePin=GeneratePin(ip);
    s_active=true;
    g_phoneRemoteRunning=true;
    g_connectedClients=0;
    thread(HttpLoop).detach();
    thread(UdpBeacon).detach();
}

void PhoneRemoteStopServer() {
    if(!s_active) return;
    s_active=false;
    g_phoneRemoteRunning=false;
    g_phoneRemotePin="";
    if(s_httpSock!=INVALID_SOCKET){closesocket(s_httpSock);s_httpSock=INVALID_SOCKET;}
    if(s_udpSock!=INVALID_SOCKET){closesocket(s_udpSock);s_udpSock=INVALID_SOCKET;}
    lock_guard<mutex> lk(s_mtx);
    for(SOCKET c:s_wsClients) closesocket(c);
    s_wsClients.clear(); g_connectedClients=0;
}

void PhoneRemoteTimerTick() {}

// ── Draw Tab UI ───────────────────────────────────────────────────
void DrawPhoneRemoteTab(Graphics& g, float x, float y, float w, float h) {
    FontFamily ff(L"Segoe UI");
    StringFormat fmtC,fmtL;
    fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    // Background
    SolidBrush bg(ColBgContent); g.FillRectangle(&bg,x,y,w,h);

    float cx=x+24, cy=y+20, cw=w-48, r=10, d=r*2;
    auto roundRect=[&](GraphicsPath& p2,float rx,float ry,float rw,float rh){
        p2.AddArc(rx,ry,d,d,180,90);p2.AddArc(rx+rw-d,ry,d,d,270,90);
        p2.AddArc(rx+rw-d,ry+rh-d,d,d,0,90);p2.AddArc(rx,ry+rh-d,d,d,90,90);p2.CloseFigure();
    };

    // ── Header ──
    SolidBrush white(Color(255,255,255,255)); Pen border(Color(255,220,230,235),1.0f);
    GraphicsPath hdr; roundRect(hdr,cx,cy,cw,72);
    g.FillPath(&white,&hdr); g.DrawPath(&border,&hdr);
    Font ft(&ff,14,FontStyleBold,UnitPixel);
    Font fs(&ff,11,FontStyleRegular,UnitPixel);
    SolidBrush teal(Color(255,0,140,150)),gray(Color(255,130,130,130));
    g.DrawString(L"📡  Phone Remote  —  RustDesk style",-1,&ft,RectF(cx+16,cy,cw-32,36),&fmtL,&teal);
    g.DrawString(L"PIN একবার দিলেই হবে, পরের বার auto-connect",-1,&fs,RectF(cx+16,cy+36,cw-32,28),&fmtL,&gray);

    bool running=g_phoneRemoteRunning;

    // ── PIN Display (big, center) ──
    float py=cy+84;
    SolidBrush pinBg(running?Color(255,225,245,235):Color(255,245,245,245));
    Pen pinBorder(running?Color(255,150,210,180):Color(255,210,210,210),1.5f);
    GraphicsPath pinCard; roundRect(pinCard,cx,py,cw,100);
    g.FillPath(&pinBg,&pinCard); g.DrawPath(&pinBorder,&pinCard);

    Font fPinLabel(&ff,11,FontStyleRegular,UnitPixel);
    Font fPin(&ff,42,FontStyleBold,UnitPixel);
    SolidBrush dark(Color(255,30,30,30)),green(Color(255,30,150,90)),orange(Color(255,180,100,20));

    if(running && !g_phoneRemotePin.empty()) {
        g.DrawString(L"Phone এ এই PIN দাও:",-1,&fPinLabel,RectF(cx,py+8,cw,20),&fmtC,&gray);
        wstring wpin(g_phoneRemotePin.begin(),g_phoneRemotePin.end());
        // Space between digits: "4 8 3 9 2 0"
        wstring spaced;
        for(size_t i=0;i<wpin.size();i++){spaced+=wpin[i];if(i+1<wpin.size())spaced+=L' ';}
        g.DrawString(spaced.c_str(),-1,&fPin,RectF(cx,py+24,cw,60),&fmtC,&green);
        // Connected count
        wstring conn2=to_wstring(g_connectedClients)+L" device connected";
        g.DrawString(conn2.c_str(),-1,&fPinLabel,RectF(cx,py+80,cw,16),&fmtC,&gray);
    } else {
        g.DrawString(L"Server বন্ধ",-1,&fPinLabel,RectF(cx,py+36,cw,28),&fmtC,&gray);
        g.DrawString(L"START করলে PIN দেখাবে",-1,&fPinLabel,RectF(cx,py+60,cw,24),&fmtC,&gray);
    }

    // ── Buttons ──
    float bY=py+110, bW=(cw-12)/2;
    // Start
    SolidBrush startBg(running?Color(255,200,210,215):(s_hovStart?Color(255,0,110,120):Color(255,0,140,150)));
    GraphicsPath sb; roundRect(sb,cx,bY,bW,44); g.FillPath(&startBg,&sb);
    Font fBtn(&ff,12,FontStyleBold,UnitPixel);
    SolidBrush whiteBr(Color(255,255,255,255));
    g.DrawString(running?L"▶ Running":L"▶ Start",-1,&fBtn,RectF(cx,bY,bW,44),&fmtC,&whiteBr);
    // Stop
    float bx2=cx+bW+12;
    SolidBrush stopBg(!running?Color(255,200,210,215):(s_hovStop?Color(255,160,30,30):Color(255,200,40,40)));
    GraphicsPath stb; roundRect(stb,bx2,bY,bW,44); g.FillPath(&stopBg,&stb);
    g.DrawString(L"■ Stop",-1,&fBtn,RectF(bx2,bY,bW,44),&fmtC,&whiteBr);

    // ── How to use ──
    float hy=bY+56;
    SolidBrush infoBg(Color(255,240,248,255)); Pen infoBorder(Color(255,190,215,245),1.0f);
    GraphicsPath ic; roundRect(ic,cx,hy,cw,140); g.FillPath(&infoBg,&ic); g.DrawPath(&infoBorder,&ic);
    Font fStep(&ff,11,FontStyleRegular,UnitPixel);
    SolidBrush stepC(Color(255,50,80,120));
    wstring steps[]={
        L"① PC তে Start চাপো — 6-digit PIN দেখাবে",
        L"② Phone এ RasFocus → FileManager → PC Remote",
        L"③ PIN লেখো → Connect চাপো",
        L"④ পরের বার auto-connect হবে (PIN লাগবে না)",
        L"⑤ Browser এ CMD, Files, Screen, Control সব পাবে",
    };
    float sy=hy+10;
    for(auto& s2:steps){g.DrawString(s2.c_str(),-1,&fStep,RectF(cx+12,sy,cw-24,26),&fmtL,&stepC);sy+=26;}
}

void ProcessPhoneRemoteMouseMove(float mx,float my,float cX,float cY){
    (void)mx;(void)my;(void)cX;(void)cY;
}

void ProcessPhoneRemoteMouseClick(float mx,float my,float cX,float cY,HWND hWnd){
    float rx=mx-cX, ry=my-cY;
    // Buttons at approx y=84+100+110=294, height=44
    float bY=294.0f, bH=44.0f, bx1=24.0f, bW=(g_phoneRemotePort>0?(float)(600-48-12)/2:150.0f);
    if(ry>=bY && ry<=bY+bH){
        if(rx>=bx1 && rx<bx1+bW){if(!g_phoneRemoteRunning)PhoneRemoteStartServer();}
        else if(rx>=bx1+bW+12){if(g_phoneRemoteRunning)PhoneRemoteStopServer();}
        InvalidateRect(hWnd,NULL,FALSE);
    }
}
