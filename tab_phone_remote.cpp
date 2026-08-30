// ================================================================
// tab_phone_remote.cpp  —  PC generates 6-digit code → Phone connects
//
// PC = WebSocket SERVER (port 9224)
//   1. "Generate Code" → random 6-digit code on screen
//   2. Phone types code → connects ws://pc-ip:9224
//   3. Phone sends {"type":"auth","code":"XXXXXX"}
//   4. PC verifies → sends {"type":"ready","width":W,"height":H,"fps":30}
//   5. PC H.264 screen stream → phone decodes (MediaCodec) → live video
//   6. Phone sends {"type":"mouse"/"key"/"scroll"} → PC SendInput
//
// Inspired by RustDesk open source (MIT License)
// ================================================================

#pragma warning(disable: 4996)
#pragma warning(disable: 4244)

#include <winsock2.h>
#include <ws2tcpip.h>
#include "tab_phone_remote.h"
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
    BYTE mask[4]={0}; if(masked)if(!WsRecvAll(s,(char*)mask,4))return{-1,{}};
    if(len>8*1024*1024) return {-1,{}};
    vector<BYTE> data(len); size_t got=0;
    while(got<len){int r=recv(s,(char*)data.data()+got,(int)min((size_t)65536,len-got),0);if(r<=0)return{-1,{}};got+=r;}
    if(masked) for(size_t i=0;i<len;i++) data[i]^=mask[i%4];
    return {op,data};
}
static bool WsSendBin(SOCKET s, const BYTE* data, size_t len) {
    if(s==INVALID_SOCKET) return false;
    vector<BYTE> f;
    f.push_back(0x82); // FIN + binary
    if(len<126)       f.push_back((BYTE)len);
    else if(len<65536){f.push_back(126);f.push_back((BYTE)(len>>8));f.push_back((BYTE)(len&0xFF));}
    else{f.push_back(127);for(int i=7;i>=0;i--)f.push_back((BYTE)((len>>(8*i))&0xFF));}
    f.insert(f.end(),data,data+len);
    lock_guard<mutex> lk(s_sendMtx);
    return send(s,(char*)f.data(),(int)f.size(),0)>0;
}
static bool WsSendText(SOCKET s, const string& txt) {
    if(s==INVALID_SOCKET) return false;
    size_t len=txt.size(); vector<BYTE> f;
    f.push_back(0x81);
    if(len<126)       f.push_back((BYTE)len);
    else if(len<65536){f.push_back(126);f.push_back((BYTE)(len>>8));f.push_back((BYTE)(len&0xFF));}
    f.insert(f.end(),txt.begin(),txt.end());
    lock_guard<mutex> lk(s_sendMtx);
    return send(s,(char*)f.data(),(int)f.size(),0)>0;
}

// ── Code generator ────────────────────────────────────────────────
static string GenCode() {
    srand((unsigned)time(nullptr)^GetTickCount());
    char buf[7];
    snprintf(buf,sizeof(buf),"%06d", rand()%1000000);
    return string(buf);
}

// ── Local IP ─────────────────────────────────────────────────────
static string GetLocalIp() {
    char host[256]={}; gethostname(host,sizeof(host));
    addrinfo hints={},*res=nullptr; hints.ai_family=AF_INET;
    if(getaddrinfo(host,"",&hints,&res)==0&&res){
        char ip[INET_ADDRSTRLEN]={};
        inet_ntop(AF_INET,&((sockaddr_in*)res->ai_addr)->sin_addr,ip,sizeof(ip));
        freeaddrinfo(res); return string(ip);
    }
    return "?.?.?.?";
}

// ── H.264 MFT encoder ────────────────────────────────────────────
class H264Enc {
public:
    IMFTransform* mft=nullptr;
    int W=0,H=0; bool ready=false;

    bool Init(int w,int h){
        W=w; H=h; MFStartup(MF_VERSION);
        IMFActivate** acts=nullptr; UINT32 cnt=0;
        MFT_REGISTER_TYPE_INFO oi{MFMediaType_Video,MFVideoFormat_H264};
        if(FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
            MFT_ENUM_FLAG_SYNCMFT|MFT_ENUM_FLAG_LOCALMFT|MFT_ENUM_FLAG_SORTANDFILTER,
            nullptr,&oi,&acts,&cnt))||cnt==0) return false;
        acts[0]->ActivateObject(__uuidof(IMFTransform),(void**)&mft);
        for(UINT32 i=0;i<cnt;i++) acts[i]->Release();
        CoTaskMemFree(acts);

        // Output: H264
        IMFMediaType* ot=nullptr; MFCreateMediaType(&ot);
        ot->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
        ot->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264);
        MFSetAttributeSize(ot,MF_MT_FRAME_SIZE,w,h);
        MFSetAttributeRatio(ot,MF_MT_FRAME_RATE,TARGET_FPS,1);
        ot->SetUINT32(MF_MT_AVG_BITRATE,TARGET_BPS);
        ot->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
        ot->SetUINT32(CODECAPI_AVEncCommonRateControlMode,eAVEncCommonRateControlMode_CBR);
        ot->SetUINT32(CODECAPI_AVLowLatencyMode,TRUE);
        mft->SetOutputType(0,ot,0); ot->Release();

        // Input: RGB32
        IMFMediaType* it=nullptr; MFCreateMediaType(&it);
        it->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
        it->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
        MFSetAttributeSize(it,MF_MT_FRAME_SIZE,w,h);
        MFSetAttributeRatio(it,MF_MT_FRAME_RATE,TARGET_FPS,1);
        it->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
        it->SetUINT32(MF_MT_DEFAULT_STRIDE,w*4);
        mft->SetInputType(0,it,0); it->Release();

        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0);
        mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0);
        ready=true; return true;
    }

    vector<BYTE> Encode(const BYTE* rgb,size_t sz,LONGLONG pts){
        if(!ready) return {};
        IMFSample* ins=nullptr; MFCreateSample(&ins);
        IMFMediaBuffer* ib=nullptr; MFCreateMemoryBuffer((DWORD)sz,&ib);
        BYTE* p=nullptr; ib->Lock(&p,nullptr,nullptr);
        memcpy(p,rgb,sz); ib->Unlock();
        ib->SetCurrentLength((DWORD)sz);
        ins->AddBuffer(ib); ib->Release();
        ins->SetSampleTime(pts);
        ins->SetSampleDuration(10000000LL/TARGET_FPS);
        mft->ProcessInput(0,ins,0); ins->Release();

        MFT_OUTPUT_DATA_BUFFER od{}; DWORD st=0;
        IMFSample* os=nullptr; MFCreateSample(&os);
        IMFMediaBuffer* ob=nullptr; MFCreateMemoryBuffer(W*H*4,&ob);
        os->AddBuffer(ob); ob->Release(); od.pSample=os;
        vector<BYTE> result;
        if(SUCCEEDED(mft->ProcessOutput(0,1,&od,&st))){
            IMFMediaBuffer* buf=nullptr; os->ConvertToContiguousBuffer(&buf);
            if(buf){ BYTE* d=nullptr; DWORD len=0;
                buf->Lock(&d,nullptr,&len);
                result.assign(d,d+len);
                buf->Unlock(); buf->Release();
            }
        }
        os->Release(); return result;
    }

    void Shutdown(){
        if(mft){mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM,0);mft->Release();mft=nullptr;}
        MFShutdown(); ready=false;
    }
};

// ── Screen capture (RGB32, scaled for streaming) ──────────────────
static vector<BYTE> CaptureScreen(int& outW,int& outH){
    int sw=GetSystemMetrics(SM_CXSCREEN),sh=GetSystemMetrics(SM_CYSCREEN);
    float scale=max(1.f,(float)max(sw,sh)/1280.f);
    int dw=((int)(sw/scale)/16)*16;
    int dh=((int)(sh/scale)/16)*16;
    outW=dw; outH=dh;
    HDC hScr=GetDC(NULL),hMem=CreateCompatibleDC(hScr);
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth=dw; bi.bmiHeader.biHeight=-dh;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    bi.bmiHeader.biCompression=BI_RGB;
    BYTE* bits=nullptr;
    HBITMAP hBmp=CreateDIBSection(hMem,&bi,DIB_RGB_COLORS,(void**)&bits,NULL,0);
    HBITMAP hOld=(HBITMAP)SelectObject(hMem,hBmp);
    SetStretchBltMode(hMem,HALFTONE);
    StretchBlt(hMem,0,0,dw,dh,hScr,0,0,sw,sh,SRCCOPY);
    GdiFlush();
    vector<BYTE> data(bits,bits+(size_t)dw*dh*4);
    SelectObject(hMem,hOld); DeleteObject(hBmp); DeleteDC(hMem); ReleaseDC(NULL,hScr);
    return data;
}

// ── Input injection: Phone → PC ───────────────────────────────────
static void InjectMouse(int mask,float rx,float ry){
    int sw=GetSystemMetrics(SM_CXSCREEN),sh=GetSystemMetrics(SM_CYSCREEN);
    INPUT inp{}; inp.type=INPUT_MOUSE;
    inp.mi.dx=(LONG)(rx*65535); inp.mi.dy=(LONG)(ry*65535);
    inp.mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_MOVE;
    SendInput(1,&inp,sizeof(INPUT));
    DWORD click=0;
    if(mask==1)      click=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_LEFTDOWN;
    else if(mask==2) click=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_LEFTUP;
    else if(mask==3) click=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_RIGHTDOWN;
    else if(mask==4) click=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_RIGHTUP;
    if(click){inp.mi.dwFlags=click; SendInput(1,&inp,sizeof(INPUT));}
}
static void InjectKey(int vk,int action){
    INPUT inp{}; inp.type=INPUT_KEYBOARD;
    inp.ki.wVk=(WORD)vk;
    inp.ki.dwFlags=(action==1)?KEYEVENTF_KEYUP:0;
    SendInput(1,&inp,sizeof(INPUT));
}
static void InjectScroll(float /*rx*/,float /*ry*/,const string& dir){
    INPUT inp{}; inp.type=INPUT_MOUSE;
    inp.mi.dwFlags=MOUSEEVENTF_WHEEL;
    inp.mi.mouseData=(dir=="up")?(DWORD)120:(DWORD)((DWORD)-120);
    SendInput(1,&inp,sizeof(INPUT));
}

// ── Capture + encode + stream loop ───────────────────────────────
static void StreamLoop(SOCKET client){
    H264Enc enc;
    int fw=0,fh=0;
    auto first=CaptureScreen(fw,fh);
    if(!enc.Init(fw,fh)){
        g_rdStatusMsg="H264 encoder init failed"; g_rdState=RdState::Error;
        if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
        return;
    }

    LONGLONG pts=0;
    LONGLONG frameDur=10000000LL/TARGET_FPS;
    DWORD frameMs=1000/TARGET_FPS;
    int fpsCount=0; DWORD fpsTimer=GetTickCount();

    while(s_active && client!=INVALID_SOCKET){
        DWORD t0=GetTickCount();
        int w,h; auto rgb=CaptureScreen(w,h);
        auto nal=enc.Encode(rgb.data(),rgb.size(),pts);
        if(!nal.empty()){
            // Packet header: [flags 1B][pts_ms 4B] + NAL data
            bool kf=(nal.size()>4&&(nal[4]&0x1F)==7); // NAL type=7 is SPS (keyframe)
            BYTE flags=(BYTE)((kf?1:0)|(kf?2:0));
            DWORD pts_ms=(DWORD)(pts/10000);
            vector<BYTE> pkt(5+nal.size());
            pkt[0]=flags;
            pkt[1]=(BYTE)((pts_ms>>24)&0xFF); pkt[2]=(BYTE)((pts_ms>>16)&0xFF);
            pkt[3]=(BYTE)((pts_ms>>8)&0xFF);  pkt[4]=(BYTE)(pts_ms&0xFF);
            memcpy(pkt.data()+5,nal.data(),nal.size());
            if(!WsSendBin(client,pkt.data(),pkt.size())) break;
            fpsCount++;
        }
        pts+=frameDur;

        DWORD elapsed=GetTickCount()-t0;
        if(elapsed<frameMs) Sleep(frameMs-elapsed);

        DWORD now=GetTickCount();
        if(now-fpsTimer>=1000){
            g_rdFps=fpsCount; fpsCount=0; fpsTimer=now;
            if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
        }
    }
    enc.Shutdown();
}

// ── Input recv loop (runs parallel to stream) ─────────────────────
static void InputRecvLoop(SOCKET client){
    DWORD tv=100; setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
    while(s_active && client!=INVALID_SOCKET){
        auto [op,data]=WsRecvFrame(client);
        if(op<0||op==8) { s_active=false; break; }
        if(op!=1||data.empty()) continue;
        if(!g_rdInputEnabled) continue;
        string msg(data.begin(),data.end());
        string type=Jget(msg,"type");
        if(type=="mouse"||type=="touch"){
            float rx=(float)atof(Jget(msg,"x").c_str());
            float ry=(float)atof(Jget(msg,"y").c_str());
            int mask=atoi(Jget(msg,"mask").c_str());
            InjectMouse(mask,rx,ry);
        } else if(type=="key"){
            InjectKey(atoi(Jget(msg,"vk").c_str()),atoi(Jget(msg,"action").c_str()));
        } else if(type=="scroll"){
            float rx=(float)atof(Jget(msg,"x").c_str());
            float ry=(float)atof(Jget(msg,"y").c_str());
            InjectScroll(rx,ry,Jget(msg,"dir"));
        }
    }
}

// ── WS handshake (server-side) ────────────────────────────────────
static bool DoHandshake(SOCKET s){
    char buf[4096]={}; int r=recv(s,buf,sizeof(buf)-1,0);
    if(r<=0) return false;
    string req(buf,r);
    if(req.find("Upgrade: websocket")==string::npos &&
       req.find("Upgrade: WebSocket")==string::npos) return false;
    size_t kp=req.find("Sec-WebSocket-Key:"); if(kp==string::npos) return false;
    kp+=18; while(kp<req.size()&&req[kp]==' ')kp++;
    size_t ke=req.find("\r\n",kp);
    string key=req.substr(kp,ke-kp);
    string acc=Sha1B64(key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    string resp="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                "Connection: Upgrade\r\nSec-WebSocket-Accept: "+acc+"\r\n\r\n";
    return send(s,resp.c_str(),(int)resp.size(),0)>0;
}

// ── Phone connection handler ──────────────────────────────────────
static void HandlePhone(SOCKET client, const string& clientIp){
    // 1. Verify auth code
    DWORD tv=8000; setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
    auto [op,data]=WsRecvFrame(client);
    if(op!=1||data.empty()){
        WsSendText(client,"{\"type\":\"error\",\"msg\":\"auth timeout\"}");
        closesocket(client); return;
    }
    string msg(data.begin(),data.end());
    string code=Jget(msg,"code");
    if(code!=g_rdCode){
        WsSendText(client,"{\"type\":\"error\",\"msg\":\"wrong code\"}");
        closesocket(client); return;
    }

    // 2. Code OK — send ready
    g_rdPhoneIp   = clientIp;
    g_rdPhoneName = Jget(msg,"device");
    if(g_rdPhoneName.empty()) g_rdPhoneName="Phone";
    g_rdState     = RdState::Connected;
    g_phoneRemoteRunning=true;
    g_connectedClients=1;
    s_clientSock  = client;

    int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
    float scale=max(1.f,(float)max(sw,sh)/1280.f);
    int dw=((int)(sw/scale)/16)*16, dh=((int)(sh/scale)/16)*16;
    char ready[256];
    snprintf(ready,sizeof(ready),
        "{\"type\":\"ready\",\"width\":%d,\"height\":%d,\"fps\":%d,\"codec\":\"h264\"}",
        dw,dh,TARGET_FPS);
    WsSendText(client,ready);

    g_rdStatusMsg="Streaming to "+g_rdPhoneName+" ("+clientIp+")";
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);

    // 3. Input recv in background, stream on this thread
    thread(InputRecvLoop,client).detach();
    StreamLoop(client);

    // 4. Cleanup
    closesocket(client);
    s_clientSock=INVALID_SOCKET;
    g_rdState=RdState::WaitPhone;
    g_rdStatusMsg="Phone disconnected. Waiting for new connection...";
    g_phoneRemoteRunning=false; g_connectedClients=0; g_rdFps=0;
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}

// ── Accept loop ───────────────────────────────────────────────────
static void AcceptLoop(){
    WSADATA wd; WSAStartup(MAKEWORD(2,2),&wd);
    s_listenSock=socket(AF_INET,SOCK_STREAM,0);
    int opt=1; setsockopt(s_listenSock,SOL_SOCKET,SO_REUSEADDR,(char*)&opt,sizeof(opt));
    DWORD tv=500; setsockopt(s_listenSock,SOL_SOCKET,SO_RCVTIMEO,(char*)&tv,sizeof(tv));
    sockaddr_in addr{}; addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY; addr.sin_port=htons((u_short)RD_PORT);
    if(bind(s_listenSock,(sockaddr*)&addr,sizeof(addr))!=0){
        g_rdStatusMsg="Port "+to_string(RD_PORT)+" bind failed";
        g_rdState=RdState::Error;
        if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
        return;
    }
    listen(s_listenSock,4);

    while(s_active){
        sockaddr_in ca{}; int cal=sizeof(ca);
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
    g_phoneRemoteRunning=false; g_connectedClients=0; g_rdFps=0;
    s_active=true;
    thread(AcceptLoop).detach();
    if(hParentWnd) InvalidateRect(hParentWnd,NULL,FALSE);
}

void RdStopServer(){
    s_active=false;
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

// ── Draw large digits (RustDesk style code display) ───────────────
static void DrawCode(Graphics& g, const wstring& code,
                     float x,float y,float w,float h){
    FontFamily ff(L"Segoe UI");
    StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter); fmt.SetLineAlignment(StringAlignmentCenter);

    // Draw 6 digit boxes: 3-space-3
    float boxW=(w-80)/6.f, boxH=h;
    float gapX=8.f, groupGap=24.f;
    float startX=x+40;

    for(int i=0;i<6;i++){
        float bx=startX + i*(boxW+gapX) + (i>=3?groupGap:0);

        // box background
        SolidBrush boxBg(Color(255,30,30,50));
        Pen boxBrd(Color(255,0,180,220),2.f);
        RoundRect(g,boxBg,&boxBrd,bx,y,boxW,boxH,8);

        // digit
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

    // Separator dot between groups
    float midX=startX+3*(boxW+gapX)+groupGap/2-2;
    SolidBrush dotC(Color(255,0,180,220));
    g.FillEllipse(&dotC,midX,y+boxH/2-6,5,5);
    g.FillEllipse(&dotC,midX,y+boxH/2+4,5,5);
}

// ── Main draw ─────────────────────────────────────────────────────
void DrawPhoneRemoteTab(Graphics& g, float x, float y, float w, float h){
    s_drawX=x; s_drawY=y; s_drawW=w; s_drawH=h;

    FontFamily ff(L"Segoe UI");
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);

    // Background
    SolidBrush bg(Color(255,14,14,22)); g.FillRectangle(&bg,x,y,w,h);

    SolidBrush cyan(Color(255,0,200,230));
    SolidBrush white(Color(255,220,220,238));
    SolidBrush gray(Color(255,110,110,135));
    SolidBrush green(Color(255,40,200,100));

    float cx=x+24, cw=w-48;

    // ── Title ──
    Font fTitle(&ff,17,FontStyleBold,UnitPixel);
    g.DrawString(L"Phone Remote Control",-1,&fTitle,RectF(cx,y+14,cw,28),&fmtL,&cyan);
    Font fSub(&ff,11,FontStyleRegular,UnitPixel);
    g.DrawString(L"PC screen → Phone live video | Phone touches → PC mouse",-1,&fSub,
                 RectF(cx,y+44,cw,20),&fmtL,&gray);

    float py=y+76;

    // ══ CONNECTED STATE ══════════════════════════════════════════
    if(g_rdState==RdState::Connected){
        // Status bar
        SolidBrush connBg(Color(255,12,60,32));
        Pen connBrd(Color(255,0,180,80),1.f);
        RoundRect(g,connBg,&connBrd,cx,py,cw,44,10);
        Font fB(&ff,13,FontStyleBold,UnitPixel);
        wstring lbl=L"● Streaming to: "+ToWStr(g_rdPhoneName)+L"  ("+ToWStr(g_rdPhoneIp)+L")";
        g.DrawString(lbl.c_str(),-1,&fB,RectF(cx+14,py,cw-28,44),&fmtL,&green);

        // FPS
        Font fR(&ff,11,FontStyleRegular,UnitPixel);
        wstring fps=ToWStr(to_string(g_rdFps))+L" fps  •  H.264  •  "+
                    ToWStr(to_string(TARGET_BPS/1000000))+L" Mbps target";
        g.DrawString(fps.c_str(),-1,&fR,RectF(cx,py+54,cw,20),&fmtC,&gray);
        py+=86;

        // Code (still shown for reconnect)
        Font fLbl(&ff,11,FontStyleRegular,UnitPixel);
        g.DrawString(L"Session Code",-1,&fLbl,RectF(cx,py,cw,20),&fmtC,&gray);
        py+=24;
        DrawCode(g,ToWStr(g_rdCode),cx,py,cw,68);
        py+=84;

        // Input toggle
        bool ienabled=g_rdInputEnabled;
        SolidBrush itBg(ienabled?Color(255,10,80,40):Color(255,45,45,65));
        Pen itBrd(ienabled?Color(255,0,160,80):Color(255,70,70,100),1.f);
        RoundRect(g,itBg,&itBrd,cx,py,cw/2-6,40,8);
        Font fBtn(&ff,12,FontStyleBold,UnitPixel);
        g.DrawString(ienabled?L"✓ Input: ON":L"✗ Input: OFF",-1,&fBtn,
                     RectF(cx,py,cw/2-6,40),&fmtC,&white);

        // Disconnect button
        float dx=cx+cw/2+6;
        SolidBrush stopBg(s_hovStop?Color(255,180,30,30):Color(255,140,20,20));
        Pen stopBrd(Color(255,200,50,50),1.f);
        RoundRect(g,stopBg,&stopBrd,dx,py,cw/2-6,40,8);
        g.DrawString(L"Disconnect",-1,&fBtn,RectF(dx,py,cw/2-6,40),&fmtC,&white);
        py+=56;

        // Tip
        g.DrawString(L"Phone এ RasFocus খুলে code টাইপ করলে নতুন session হবে",-1,
                     &Font(&ff,10,FontStyleRegular,UnitPixel),RectF(cx,py,cw,20),&fmtC,&gray);
        return;
    }

    // ══ WAITING STATE ═════════════════════════════════════════════
    if(g_rdState==RdState::WaitPhone){
        // Code label
        Font fLbl(&ff,13,FontStyleRegular,UnitPixel);
        g.DrawString(L"Phone এ এই Code টাইপ করো:",-1,&fLbl,RectF(cx,py,cw,24),&fmtL,&gray);
        py+=30;

        // 6-digit code boxes — big, centered
        DrawCode(g,ToWStr(g_rdCode),cx,py,cw,80);
        py+=96;

        // Pulsing "waiting" text
        DWORD tick=GetTickCount()/600; // blink every 600ms
        SolidBrush waitC(tick%2==0?Color(255,0,200,230):Color(255,0,140,170));
        Font fWait(&ff,12,FontStyleBold,UnitPixel);
        g.DrawString(L"⌛ Waiting for phone to connect...",-1,&fWait,
                     RectF(cx,py,cw,26),&fmtC,&waitC);
        py+=36;

        // Status (IP info)
        g.DrawString(ToWStr(g_rdStatusMsg).c_str(),-1,
                     &Font(&ff,10,FontStyleRegular,UnitPixel),
                     RectF(cx,py,cw,20),&fmtC,&gray);
        py+=36;

        // New Code button
        SolidBrush newBg(s_hovGenerate?Color(255,0,140,160):Color(255,0,110,130));
        Pen newBrd(Color(255,0,180,200),1.f);
        RoundRect(g,newBg,&newBrd,cx,py,cw,46,10);
        Font fBtn(&ff,13,FontStyleBold,UnitPixel);
        g.DrawString(L"↻ Generate New Code",-1,&fBtn,RectF(cx,py,cw,46),&fmtC,&white);
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
    // Big generate button
    float btnH=70;
    float btnY=y+h/2-btnH/2-20;
    SolidBrush genBg(s_hovGenerate?Color(255,0,160,185):Color(255,0,130,155));
    Pen genBrd(Color(255,0,190,220),2.f);
    RoundRect(g,genBg,&genBrd,cx,btnY,cw,btnH,14);
    Font fBig(&ff,18,FontStyleBold,UnitPixel);
    g.DrawString(L"▶  Generate Code",-1,&fBig,RectF(cx,btnY,cw,btnH),&fmtC,&white);

    // Description
    float dy=btnY+btnH+18;
    const wchar_t* lines[]={
        L"Phone এ RasFocus খুলে \"PC Remote\" → code টাইপ করো",
        L"PC screen live video phone এ দেখাবে (H.264)",
        L"Phone touch → PC mouse হিসেবে কাজ করবে"
    };
    Font fTip(&ff,11,FontStyleRegular,UnitPixel);
    for(auto* l:lines){
        g.DrawString(l,-1,&fTip,RectF(cx,dy,cw,20),&fmtC,&gray); dy+=22;
    }

    // Error
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

    if(g_rdState==RdState::Connected){
        py+=86+24+80+10;
        float dx=cx+cw/2+6;
        s_hovStop=(mx>=dx&&mx<=dx+cw/2-6&&my>=py&&my<=py+40);
        s_hovGenerate=false;
    } else if(g_rdState==RdState::WaitPhone){
        py+=30+80+10+36+36;
        s_hovGenerate=(mx>=cx&&mx<=cx+cw&&my>=py&&my<=py+46);
        py+=62;
        s_hovStop=(mx>=cx&&mx<=cx+cw&&my>=py&&my<=py+36);
    } else {
        float btnH=70, btnY=s_drawY+s_drawH/2-btnH/2-20;
        s_hovGenerate=(mx>=cx&&mx<=cx+cw&&my>=btnY&&my<=btnY+btnH);
        s_hovStop=false;
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
        py+=30+80+10+36+36;
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
        float btnH=70, btnY=s_drawY+s_drawH/2-btnH/2-20;
        if(mx>=cx&&mx<=cx+cw&&my>=btnY&&my<=btnY+btnH){
            RdGenerateCode(); InvalidateRect(hWnd,NULL,FALSE); return;
        }
    }
}

// ── WM_CHAR (not needed — no text input on PC side) ──────────────
extern "C" void PhoneRemoteChar(wchar_t /*ch*/){}
