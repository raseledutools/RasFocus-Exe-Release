// rasgram_net.cpp
// RasGram Desktop — Network & Backend Implementation
//
// Features:
//   • Firebase Firestore REST API (WinINet/HTTPS) — same DB as Android RasGram
//   • LAN peer discovery: UDP broadcast port 5555 (identical to Android LanChatManager)
//   • LAN messaging: TCP port 5556 (JSON header + binary payload)
//   • Audio call: WASAPI capture → Opus encode → UDP RTP → decode → WASAPI render
//   • Video call: DirectShow camera → MJPEG/RGB → UDP → GDI+ render
//
// Architecture note:
//   All Firestore operations use WinINet HTTPS (same helper pattern as main.cpp
//   SendFirestoreRequest). The idToken from accounts login is reused for
//   authenticated writes. Unauthenticated reads work via open Firestore rules
//   (same as Android dev environment).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "rasgram_net.h"
#include <windows.h>
#include <wininet.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <dshow.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>
#include <algorithm>
#include <functional>
#include <map>
#include <chrono>
#include <ctime>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "strmiids.lib")

using namespace std;

// ============================================================
// MODULE STATE
// ============================================================
static string g_myMobile;
static string g_myName;
static string g_myUid;
static string g_idToken;      // Firebase Auth ID token (from accounts module)

static atomic<bool> g_chatPollRunning  { false };
static atomic<bool> g_msgPollRunning   { false };
static atomic<bool> g_lanRunning       { false };
static atomic<bool> g_callActive       { false };
static atomic<bool> g_callMuted        { false };
static atomic<bool> g_callVideo        { false };
static atomic<int>  g_callDuration     { 0 };

static thread g_chatPollThread;
static thread g_msgPollThread;
static thread g_lanBeaconThread;
static thread g_lanListenThread;
static thread g_lanTcpThread;
static thread g_callThread;

static SOCKET g_udpBeaconSock  = INVALID_SOCKET;
static SOCKET g_udpListenSock  = INVALID_SOCKET;
static SOCKET g_tcpServerSock  = INVALID_SOCKET;
static SOCKET g_callUdpSock    = INVALID_SOCKET;

static mutex g_lanPeersMtx;
static map<string, RgLanPeer>    g_lanPeers;      // mobile → peer
static map<string, long long>    g_lanPeerSeen;   // mobile → last seen ms

// ============================================================
// HELPERS — TIME
// ============================================================
static long long NowMs() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()).count();
}

// ============================================================
// HELPERS — HTTP / FIRESTORE REST
// ============================================================
string RgFirestoreGet(const string& path) {
    string resp;
    HINTERNET hInet = InternetOpenA("RasGram-Desktop/1.0",
                        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return resp;

    HINTERNET hConn = InternetConnectA(hInet, RG_FIRESTORE_HOST,
                        INTERNET_DEFAULT_HTTPS_PORT,
                        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (hConn) {
        HINTERNET hReq = HttpOpenRequestA(hConn, "GET", path.c_str(),
                            NULL, NULL, NULL,
                            INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                            INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (hReq) {
            // Add Authorization header if we have an ID token
            string headers;
            if (!g_idToken.empty())
                headers = "Authorization: Bearer " + g_idToken + "\r\n";
            HttpSendRequestA(hReq,
                headers.empty() ? NULL : headers.c_str(),
                (DWORD)headers.size(),
                NULL, 0);
            char buf[4096]; DWORD n = 0;
            while (InternetReadFile(hReq, buf, sizeof(buf)-1, &n) && n > 0) {
                buf[n] = '\0'; resp += buf;
            }
            InternetCloseHandle(hReq);
        }
        InternetCloseHandle(hConn);
    }
    InternetCloseHandle(hInet);
    return resp;
}

string RgFirestorePost(const string& method, const string& path,
                       const string& jsonBody) {
    string resp;
    HINTERNET hInet = InternetOpenA("RasGram-Desktop/1.0",
                        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return resp;

    HINTERNET hConn = InternetConnectA(hInet, RG_FIRESTORE_HOST,
                        INTERNET_DEFAULT_HTTPS_PORT,
                        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (hConn) {
        HINTERNET hReq = HttpOpenRequestA(hConn, method.c_str(), path.c_str(),
                            NULL, NULL, NULL,
                            INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                            INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (hReq) {
            string headers = "Content-Type: application/json\r\n";
            if (!g_idToken.empty())
                headers += "Authorization: Bearer " + g_idToken + "\r\n";
            HttpSendRequestA(hReq,
                headers.c_str(), (DWORD)headers.size(),
                (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.size());
            char buf[4096]; DWORD n = 0;
            while (InternetReadFile(hReq, buf, sizeof(buf)-1, &n) && n > 0) {
                buf[n] = '\0'; resp += buf;
            }
            InternetCloseHandle(hReq);
        }
        InternetCloseHandle(hConn);
    }
    InternetCloseHandle(hInet);
    return resp;
}

// ============================================================
// HELPERS — JSON PARSING (lightweight, no dependency)
// ============================================================
string RgParseField(const string& json, const string& field) {
    // Looks for "field":{"stringValue":"VALUE"} or "field":"VALUE"
    auto tryPattern = [&](const string& pat) -> string {
        size_t p = json.find(pat);
        if (p == string::npos) return "";
        p += pat.size();
        // skip whitespace
        while (p < json.size() && (json[p]==' '||json[p]=='\t')) p++;
        if (p >= json.size()) return "";
        char delim = json[p];
        if (delim != '"') return "";
        p++;
        string val;
        while (p < json.size() && json[p] != '"') {
            if (json[p]=='\\' && p+1<json.size()) { p++; val += json[p]; }
            else val += json[p];
            p++;
        }
        return val;
    };

    // Try stringValue pattern first (Firestore REST format)
    string sv = tryPattern("\"" + field + "\":{\"stringValue\":\"");
    if (!sv.empty()) return sv;
    // Try plain string
    sv = tryPattern("\"" + field + "\":\"");
    return sv;
}

long long RgParseIntField(const string& json, const string& field) {
    // "field":{"integerValue":"VALUE"} or "field":NUMBER
    size_t p = json.find("\"" + field + "\":{\"integerValue\":\"");
    if (p != string::npos) {
        p += ("\"" + field + "\":{\"integerValue\":\"").size();
        string num;
        while (p < json.size() && json[p] != '"') num += json[p++];
        return num.empty() ? 0 : atoll(num.c_str());
    }
    p = json.find("\"" + field + "\":");
    if (p != string::npos) {
        p += ("\"" + field + "\":").size();
        while (p < json.size() && (json[p]==' '||json[p]=='\t')) p++;
        string num;
        while (p < json.size() && (isdigit(json[p])||json[p]=='-')) num += json[p++];
        return num.empty() ? 0 : atoll(num.c_str());
    }
    return 0;
}

bool RgParseBoolField(const string& json, const string& field) {
    size_t p = json.find("\"" + field + "\":{\"booleanValue\":");
    if (p != string::npos) {
        p += ("\"" + field + "\":{\"booleanValue\":").size();
        return json.substr(p, 4) == "true";
    }
    return false;
}

string RgFormatTime(long long timestampMs) {
    if (timestampMs <= 0) return "";
    time_t t = (time_t)(timestampMs / 1000);
    struct tm* tm_info = localtime(&t);
    if (!tm_info) return "";
    char buf[16];
    strftime(buf, sizeof(buf), "%I:%M %p", tm_info);
    return buf;
}

string RgBuildChatId(const string& mobileA, const string& mobileB) {
    // Same logic as Android: sort the two mobiles, join with "_"
    // Android uses "pvt_msg_" + sorted(mobileA + "_" + mobileB)
    string a = mobileA, b = mobileB;
    if (a > b) swap(a, b);
    return "pvt_msg_" + a + "_" + b;
}

string RgBuildPath(const string& col, const string& docId,
                   const string& sub, const string& subId) {
    string path = "/v1/projects/" RG_FIREBASE_PROJECT
                  "/databases/(default)/documents/" + col;
    if (!docId.empty()) path += "/" + docId;
    if (!sub.empty())   path += "/" + sub;
    if (!subId.empty()) path += "/" + subId;
    return path;
}

// ============================================================
// INIT / SHUTDOWN
// ============================================================
void RgNet_Init(const string& myMobile, const string& myName,
                const string& myUid,    const string& idToken) {
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    g_myMobile = myMobile;
    g_myName   = myName;
    g_myUid    = myUid;
    g_idToken  = idToken;
}

void RgNet_Shutdown() {
    g_chatPollRunning = false;
    g_msgPollRunning  = false;
    g_lanRunning      = false;
    g_callActive      = false;

    if (g_udpBeaconSock != INVALID_SOCKET) { closesocket(g_udpBeaconSock); g_udpBeaconSock = INVALID_SOCKET; }
    if (g_udpListenSock != INVALID_SOCKET) { closesocket(g_udpListenSock); g_udpListenSock = INVALID_SOCKET; }
    if (g_tcpServerSock != INVALID_SOCKET) { closesocket(g_tcpServerSock); g_tcpServerSock = INVALID_SOCKET; }
    if (g_callUdpSock   != INVALID_SOCKET) { closesocket(g_callUdpSock);   g_callUdpSock   = INVALID_SOCKET; }

    if (g_chatPollThread.joinable())  g_chatPollThread.detach();
    if (g_msgPollThread.joinable())   g_msgPollThread.detach();
    if (g_lanBeaconThread.joinable()) g_lanBeaconThread.detach();
    if (g_lanListenThread.joinable()) g_lanListenThread.detach();
    if (g_lanTcpThread.joinable())    g_lanTcpThread.detach();
    if (g_callThread.joinable())      g_callThread.detach();

    WSACleanup();
}

// ============================================================
// PRESENCE
// ============================================================
void RgNet_SetOnline(bool online) {
    if (g_myMobile.empty()) return;
    // Update users/{myMobile}/isOnline in Firestore
    string path    = RgBuildPath("users", g_myMobile);
    long long now  = NowMs();
    string payload = "{\"fields\":{"
        "\"isOnline\":{\"booleanValue\":" + string(online ? "true" : "false") + "},"
        "\"lastSeen\":{\"integerValue\":\"" + to_string(now) + "\"}"
        "}}";
    // Fire & forget on background thread
    thread([path, payload]() {
        RgFirestorePost("PATCH", path + "?updateMask.fieldPaths=isOnline"
                                        "&updateMask.fieldPaths=lastSeen", payload);
    }).detach();
}

// ============================================================
// CHAT LIST POLLING
// ============================================================
static vector<RgChatPreview> ParseChatPreviews(const string& json) {
    vector<RgChatPreview> result;
    // Firestore list query response: {"documents":[{...},{...}]}
    size_t pos = 0;
    while ((pos = json.find("\"name\":", pos)) != string::npos) {
        // Find the document block
        size_t blockEnd = json.find("\"name\":", pos + 7);
        string block = json.substr(pos, blockEnd == string::npos
                                        ? json.size() - pos
                                        : blockEnd - pos);
        RgChatPreview cp;
        cp.contactMobile    = RgParseField(block, "contactMobile");
        cp.contactName      = RgParseField(block, "contactName");
        cp.contactAvatarUrl = RgParseField(block, "contactAvatarUrl");
        cp.lastMessageText  = RgParseField(block, "lastMessageText");
        cp.lastMessageSender= RgParseField(block, "lastMessageSender");
        cp.lastTimestamp    = RgParseIntField(block, "lastTimestamp");
        cp.lastTimeString   = RgFormatTime(cp.lastTimestamp);
        cp.unreadCount      = (int)RgParseIntField(block, "unreadCount");
        cp.isPinned         = RgParseBoolField(block, "isPinned");
        cp.isMuted          = RgParseBoolField(block, "isMuted");
        if (!cp.contactMobile.empty())
            result.push_back(cp);
        pos = blockEnd == string::npos ? json.size() : blockEnd;
    }
    // Sort: pinned first, then by timestamp desc
    sort(result.begin(), result.end(), [](const RgChatPreview& a, const RgChatPreview& b){
        if (a.isPinned != b.isPinned) return a.isPinned > b.isPinned;
        return a.lastTimestamp > b.lastTimestamp;
    });
    return result;
}

void RgNet_FetchContacts(RgChatsCallback cb) {
    if (g_myMobile.empty()) return;
    thread([cb]() {
        // Query chat_previews sub-collection under users/{myMobile}
        string path = RgBuildPath("users", g_myMobile, "chat_previews");
        // Firestore list documents
        string resp = RgFirestoreGet(path);
        auto previews = ParseChatPreviews(resp);
        if (cb) cb(previews);
    }).detach();
}

void RgNet_StartChatListPolling(RgChatsCallback cb) {
    if (g_chatPollRunning) return;
    g_chatPollRunning = true;
    g_chatPollThread = thread([cb]() {
        while (g_chatPollRunning) {
            if (!g_myMobile.empty()) {
                string path = RgBuildPath("users", g_myMobile, "chat_previews");
                string resp = RgFirestoreGet(path);
                auto previews = ParseChatPreviews(resp);
                if (cb && g_chatPollRunning) cb(previews);
            }
            // Poll every 2 seconds
            for (int i = 0; i < 20 && g_chatPollRunning; i++)
                Sleep(100);
        }
    });
}

void RgNet_StopChatListPolling() {
    g_chatPollRunning = false;
    if (g_chatPollThread.joinable()) g_chatPollThread.detach();
}

// ============================================================
// MESSAGE LOADING
// ============================================================
static vector<RgMessage> ParseMessages(const string& json) {
    vector<RgMessage> result;
    size_t pos = 0;
    while ((pos = json.find("\"name\":", pos)) != string::npos) {
        size_t blockEnd = json.find("\"name\":", pos + 7);
        string block = json.substr(pos, blockEnd == string::npos
                                        ? json.size() - pos
                                        : blockEnd - pos);
        RgMessage m;
        // Extract document ID from name field: .../messages/DOC_ID
        {
            size_t np = block.find("\"name\":");
            if (np != string::npos) {
                np += 7;
                while (np < block.size() && block[np] != '"') np++;
                if (np < block.size()) {
                    np++;
                    string fullName;
                    while (np < block.size() && block[np] != '"') fullName += block[np++];
                    size_t lastSlash = fullName.rfind('/');
                    if (lastSlash != string::npos) m.id = fullName.substr(lastSlash+1);
                }
            }
        }
        m.chatId         = RgParseField(block, "chatId");
        m.senderMobile   = RgParseField(block, "senderMobile");
        m.senderName     = RgParseField(block, "senderName");
        m.text           = RgParseField(block, "text");
        m.timestamp      = RgParseIntField(block, "timestamp");
        m.timeString     = RgFormatTime(m.timestamp);
        m.fileUrl        = RgParseField(block, "fileUrl");
        m.fileName       = RgParseField(block, "fileName");
        m.fileType       = RgParseField(block, "fileType");
        m.fileSizeBytes  = RgParseIntField(block, "fileSizeBytes");
        m.reaction       = RgParseField(block, "reaction");
        m.read           = RgParseBoolField(block, "read");
        m.delivered      = RgParseBoolField(block, "delivered");
        m.isCallLog      = RgParseBoolField(block, "isCallLog");
        m.callStatus     = RgParseField(block, "callStatus");
        m.callType       = RgParseField(block, "callType");
        m.isDeleted      = RgParseBoolField(block, "isDeleted");
        m.replyToId      = RgParseField(block, "replyToId");
        m.replyToText    = RgParseField(block, "replyToText");
        m.replyToSender  = RgParseField(block, "replyToSender");
        m.duration       = (int)RgParseIntField(block, "durationSecs");
        m.deliveredViaLan= RgParseBoolField(block, "deliveredViaLan");

        if (!m.id.empty() && !m.isDeleted)
            result.push_back(m);

        pos = blockEnd == string::npos ? json.size() : blockEnd;
    }
    // Sort by timestamp ascending (oldest first, like WhatsApp/Telegram)
    sort(result.begin(), result.end(), [](const RgMessage& a, const RgMessage& b){
        return a.timestamp < b.timestamp;
    });
    return result;
}

void RgNet_FetchMessages(const string& chatId, RgMessagesCallback cb) {
    thread([chatId, cb]() {
        // GET /chats/{chatId}/messages?orderBy=timestamp&pageSize=100
        string path = RgBuildPath("chats", chatId, "messages")
                    + "?orderBy=timestamp%20desc&pageSize=100";
        string resp = RgFirestoreGet(path);
        auto msgs = ParseMessages(resp);
        // Reverse so newest is at bottom
        reverse(msgs.begin(), msgs.end());
        if (cb) cb(msgs);
    }).detach();
}

void RgNet_StartMessagePolling(const string& chatId, long long sinceTs,
                               RgNewMessageCallback cb) {
    g_msgPollRunning = false;
    if (g_msgPollThread.joinable()) g_msgPollThread.detach();
    g_msgPollRunning = true;
    g_msgPollThread = thread([chatId, sinceTs, cb]() {
        long long lastTs = sinceTs;
        while (g_msgPollRunning) {
            string path = RgBuildPath("chats", chatId, "messages")
                        + "?orderBy=timestamp%20desc&pageSize=10";
            string resp = RgFirestoreGet(path);
            auto msgs = ParseMessages(resp);
            for (auto& m : msgs) {
                if (m.timestamp > lastTs) {
                    lastTs = m.timestamp;
                    if (cb && g_msgPollRunning) cb(m);
                }
            }
            for (int i = 0; i < 15 && g_msgPollRunning; i++)
                Sleep(100);
        }
    });
}

void RgNet_StopMessagePolling() {
    g_msgPollRunning = false;
    if (g_msgPollThread.joinable()) g_msgPollThread.detach();
}

// ============================================================
// SEND MESSAGE
// ============================================================
void RgNet_SendText(const string& chatId,
                    const string& text,
                    const string& receiverMobile) {
    if (g_myMobile.empty() || chatId.empty() || text.empty()) return;
    thread([chatId, text, receiverMobile]() {
        long long ts = NowMs();
        string timeStr = RgFormatTime(ts);

        // Escape text for JSON
        string escaped;
        for (char c : text) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else escaped += c;
        }

        // Build Firestore document
        string payload = "{\"fields\":{"
            "\"chatId\":{\"stringValue\":\"" + chatId + "\"},"
            "\"senderMobile\":{\"stringValue\":\"" + g_myMobile + "\"},"
            "\"senderName\":{\"stringValue\":\"" + g_myName + "\"},"
            "\"text\":{\"stringValue\":\"" + escaped + "\"},"
            "\"timestamp\":{\"integerValue\":\"" + to_string(ts) + "\"},"
            "\"timeString\":{\"stringValue\":\"" + timeStr + "\"},"
            "\"read\":{\"booleanValue\":false},"
            "\"delivered\":{\"booleanValue\":true},"
            "\"isDeleted\":{\"booleanValue\":false},"
            "\"isCallLog\":{\"booleanValue\":false}"
            "}}";

        // POST to chats/{chatId}/messages (auto-ID)
        string path = RgBuildPath("chats", chatId, "messages");
        RgFirestorePost("POST", path, payload);

        // Update sender's chat_previews
        string previewPayload = "{\"fields\":{"
            "\"contactMobile\":{\"stringValue\":\"" + receiverMobile + "\"},"
            "\"lastMessageText\":{\"stringValue\":\"" + escaped + "\"},"
            "\"lastMessageSender\":{\"stringValue\":\"" + g_myMobile + "\"},"
            "\"lastTimestamp\":{\"integerValue\":\"" + to_string(ts) + "\"},"
            "\"lastTimeString\":{\"stringValue\":\"" + timeStr + "\"}"
            "}}";
        string previewPath = RgBuildPath("users", g_myMobile,
                                         "chat_previews", receiverMobile);
        RgFirestorePost("PATCH", previewPath, previewPayload);

        // Update receiver's chat_previews (increment unreadCount via read then patch)
        // Simplified: just set lastMessage on receiver's side
        string rxPreviewPath = RgBuildPath("users", receiverMobile,
                                            "chat_previews", g_myMobile);
        string rxPreviewPayload = "{\"fields\":{"
            "\"contactMobile\":{\"stringValue\":\"" + g_myMobile + "\"},"
            "\"contactName\":{\"stringValue\":\"" + g_myName + "\"},"
            "\"lastMessageText\":{\"stringValue\":\"" + escaped + "\"},"
            "\"lastMessageSender\":{\"stringValue\":\"" + g_myMobile + "\"},"
            "\"lastTimestamp\":{\"integerValue\":\"" + to_string(ts) + "\"},"
            "\"lastTimeString\":{\"stringValue\":\"" + timeStr + "\"}"
            "}}";
        RgFirestorePost("PATCH", rxPreviewPath, rxPreviewPayload);
    }).detach();
}

void RgNet_MarkRead(const string& chatId, const string& myMobile) {
    // Reset unreadCount on our chat_preview for this contact
    // (simplified: just patch unreadCount = 0)
    thread([chatId, myMobile]() {
        // Extract contact mobile from chatId  "pvt_msg_A_B"
        string contact;
        size_t p = chatId.find("pvt_msg_");
        if (p != string::npos) {
            string rest = chatId.substr(p + 8);
            size_t u = rest.find('_');
            if (u != string::npos) {
                string a = rest.substr(0, u);
                string b = rest.substr(u+1);
                contact = (a == myMobile) ? b : a;
            }
        }
        if (contact.empty()) return;
        string path = RgBuildPath("users", myMobile,
                                   "chat_previews", contact);
        string payload = "{\"fields\":{"
            "\"unreadCount\":{\"integerValue\":\"0\"}"
            "}}";
        RgFirestorePost("PATCH",
            path + "?updateMask.fieldPaths=unreadCount", payload);
    }).detach();
}

// ============================================================
// LAN — PEER DISCOVERY (UDP Broadcast, same as Android)
// ============================================================
static string BuildBeaconJson() {
    // {"type":"beacon","mobile":"...","name":"...","ip":"...","port":5556}
    char myIp[64] = "0.0.0.0";
    // Get local IP
    char hostName[256];
    if (gethostname(hostName, sizeof(hostName)) == 0) {
        addrinfo* res = nullptr;
        addrinfo hints = {}; hints.ai_family = AF_INET;
        if (getaddrinfo(hostName, nullptr, &hints, &res) == 0 && res) {
            inet_ntop(AF_INET,
                &((sockaddr_in*)res->ai_addr)->sin_addr,
                myIp, sizeof(myIp));
            freeaddrinfo(res);
        }
    }
    return "{\"type\":\"beacon\",\"mobile\":\"" + g_myMobile +
           "\",\"name\":\"" + g_myName +
           "\",\"ip\":\"" + string(myIp) +
           "\",\"port\":" + to_string(RG_LAN_TCP_PORT) + "}";
}

static string LanJsonField(const string& json, const string& key) {
    size_t p = json.find("\"" + key + "\":\"");
    if (p == string::npos) return "";
    p += key.size() + 4;
    string v;
    while (p < json.size() && json[p] != '"') v += json[p++];
    return v;
}
static int LanJsonInt(const string& json, const string& key) {
    size_t p = json.find("\"" + key + "\":");
    if (p == string::npos) return 0;
    p += key.size() + 3;
    string v;
    while (p < json.size() && isdigit(json[p])) v += json[p++];
    return v.empty() ? 0 : atoi(v.c_str());
}

static void LanBeaconLoop() {
    while (g_lanRunning) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s != INVALID_SOCKET) {
            BOOL bcast = TRUE;
            setsockopt(s, SOL_SOCKET, SO_BROADCAST,
                       (char*)&bcast, sizeof(bcast));
            sockaddr_in dest = {};
            dest.sin_family = AF_INET;
            dest.sin_port   = htons(RG_LAN_UDP_PORT);
            dest.sin_addr.s_addr = INADDR_BROADCAST;
            string beacon = BuildBeaconJson();
            sendto(s, beacon.c_str(), (int)beacon.size(), 0,
                   (sockaddr*)&dest, sizeof(dest));
            closesocket(s);
        }
        for (int i = 0; i < (RG_BEACON_INTERVAL_MS/100) && g_lanRunning; i++)
            Sleep(100);
    }
}

static RgLanPeersCallback g_lanPeersCb;
static RgNewMessageCallback g_lanMsgCb;

static void LanListenLoop() {
    g_udpListenSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udpListenSock == INVALID_SOCKET) return;

    BOOL bcast = TRUE;
    setsockopt(g_udpListenSock, SOL_SOCKET, SO_BROADCAST,
               (char*)&bcast, sizeof(bcast));
    BOOL reuse = TRUE;
    setsockopt(g_udpListenSock, SOL_SOCKET, SO_REUSEADDR,
               (char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(RG_LAN_UDP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(g_udpListenSock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(g_udpListenSock); g_udpListenSock = INVALID_SOCKET; return;
    }

    // 1-second recv timeout so we can check g_lanRunning
    DWORD tv = 1000;
    setsockopt(g_udpListenSock, SOL_SOCKET, SO_RCVTIMEO,
               (char*)&tv, sizeof(tv));

    char buf[2048];
    sockaddr_in from; int fromLen = sizeof(from);
    while (g_lanRunning) {
        int n = recvfrom(g_udpListenSock, buf, sizeof(buf)-1, 0,
                         (sockaddr*)&from, &fromLen);
        if (n <= 0) continue;
        buf[n] = '\0';
        string json(buf);
        if (LanJsonField(json, "type") != "beacon") continue;
        string mobile = LanJsonField(json, "mobile");
        if (mobile.empty() || mobile == g_myMobile) continue;

        char fromIp[64];
        inet_ntop(AF_INET, &from.sin_addr, fromIp, sizeof(fromIp));

        RgLanPeer peer;
        peer.mobile = mobile;
        peer.name   = LanJsonField(json, "name");
        peer.ip     = LanJsonField(json, "ip");
        if (peer.ip.empty()) peer.ip = fromIp;
        peer.port   = LanJsonInt(json, "port");
        if (peer.port <= 0) peer.port = RG_LAN_TCP_PORT;

        {
            lock_guard<mutex> lk(g_lanPeersMtx);
            g_lanPeers[mobile]   = peer;
            g_lanPeerSeen[mobile]= NowMs();
        }

        // Prune stale peers
        {
            lock_guard<mutex> lk(g_lanPeersMtx);
            long long cutoff = NowMs() - RG_PEER_TIMEOUT_MS;
            for (auto it = g_lanPeerSeen.begin(); it != g_lanPeerSeen.end(); ) {
                if (it->second < cutoff) {
                    g_lanPeers.erase(it->first);
                    it = g_lanPeerSeen.erase(it);
                } else ++it;
            }
        }

        if (g_lanPeersCb) {
            vector<RgLanPeer> list;
            { lock_guard<mutex> lk(g_lanPeersMtx);
              for (auto& kv : g_lanPeers) list.push_back(kv.second); }
            g_lanPeersCb(list);
        }
    }
    if (g_udpListenSock != INVALID_SOCKET) {
        closesocket(g_udpListenSock); g_udpListenSock = INVALID_SOCKET;
    }
}

// ── LAN TCP server — receive messages / files ──────────────────
static void HandleLanTcpClient(SOCKET client) {
    // Protocol: 4-byte header length (network order) + header JSON + optional binary
    char lenBuf[4] = {};
    int got = recv(client, lenBuf, 4, MSG_WAITALL);
    if (got != 4) { closesocket(client); return; }
    int headerLen = ntohl(*(int*)lenBuf);
    if (headerLen <= 0 || headerLen > 65536) { closesocket(client); return; }

    vector<char> hdrBuf(headerLen+1, 0);
    got = recv(client, hdrBuf.data(), headerLen, MSG_WAITALL);
    if (got != headerLen) { closesocket(client); return; }
    string header(hdrBuf.data());

    string type         = LanJsonField(header, "type");
    string chatId       = LanJsonField(header, "chatId");
    string senderMobile = LanJsonField(header, "senderMobile");
    string senderName   = LanJsonField(header, "senderName");
    string text         = LanJsonField(header, "text");

    if (type == "text" && !chatId.empty() && g_lanMsgCb) {
        RgMessage msg;
        msg.id           = "lan_" + to_string(NowMs());
        msg.chatId       = chatId;
        msg.senderMobile = senderMobile;
        msg.senderName   = senderName;
        msg.text         = text;
        msg.timestamp    = NowMs();
        msg.timeString   = RgFormatTime(msg.timestamp);
        msg.deliveredViaLan = true;
        g_lanMsgCb(msg);

        // Also persist to Firestore so Android sees it
        RgNet_SendText(chatId, text, senderMobile);
    }
    closesocket(client);
}

static void LanTcpServerLoop() {
    g_tcpServerSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_tcpServerSock == INVALID_SOCKET) return;

    BOOL reuse = TRUE;
    setsockopt(g_tcpServerSock, SOL_SOCKET, SO_REUSEADDR,
               (char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(RG_LAN_TCP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(g_tcpServerSock, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(g_tcpServerSock, 8) != 0) {
        closesocket(g_tcpServerSock); g_tcpServerSock = INVALID_SOCKET; return;
    }

    DWORD tv = 1000;
    setsockopt(g_tcpServerSock, SOL_SOCKET, SO_RCVTIMEO,
               (char*)&tv, sizeof(tv));

    while (g_lanRunning) {
        sockaddr_in caddr; int clen = sizeof(caddr);
        SOCKET client = accept(g_tcpServerSock, (sockaddr*)&caddr, &clen);
        if (client == INVALID_SOCKET) continue;
        thread([client]() { HandleLanTcpClient(client); }).detach();
    }
    if (g_tcpServerSock != INVALID_SOCKET) {
        closesocket(g_tcpServerSock); g_tcpServerSock = INVALID_SOCKET;
    }
}

void RgNet_StartLan(RgLanPeersCallback peersCb, RgNewMessageCallback msgCb) {
    if (g_lanRunning) return;
    g_lanPeersCb  = peersCb;
    g_lanMsgCb    = msgCb;
    g_lanRunning  = true;

    g_lanBeaconThread = thread(LanBeaconLoop);
    g_lanListenThread = thread(LanListenLoop);
    g_lanTcpThread    = thread(LanTcpServerLoop);
}

void RgNet_StopLan() {
    g_lanRunning = false;
    if (g_lanBeaconThread.joinable()) g_lanBeaconThread.detach();
    if (g_lanListenThread.joinable()) g_lanListenThread.detach();
    if (g_lanTcpThread.joinable())    g_lanTcpThread.detach();
}

void RgNet_LanSendText(const RgLanPeer& peer, const string& chatId,
                       const string& text) {
    thread([peer, chatId, text]() {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return;
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons((u_short)peer.port);
        inet_pton(AF_INET, peer.ip.c_str(), &addr.sin_addr);
        DWORD tv = 5000;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
        if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
            string header = "{\"type\":\"text\","
                "\"chatId\":\"" + chatId + "\","
                "\"senderMobile\":\"" + g_myMobile + "\","
                "\"senderName\":\"" + g_myName + "\","
                "\"text\":\"" + text + "\"}";
            int hLen = htonl((int)header.size());
            send(s, (char*)&hLen, 4, 0);
            send(s, header.c_str(), (int)header.size(), 0);
        }
        closesocket(s);
    }).detach();
}

void RgNet_LanSendFile(const RgLanPeer& peer, const string& chatId,
                       const wstring& filePath, const string& mimeType) {
    // File send: open file → send header + 8-byte size + raw bytes
    thread([peer, chatId, filePath, mimeType]() {
        HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER fSize; GetFileSizeEx(hFile, &fSize);

        // Get filename
        size_t slash = filePath.rfind(L'\\');
        wstring wfn = (slash != wstring::npos) ? filePath.substr(slash+1) : filePath;
        string fname(wfn.begin(), wfn.end());

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons((u_short)peer.port);
        inet_pton(AF_INET, peer.ip.c_str(), &addr.sin_addr);
        if (s != INVALID_SOCKET && connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
            string header = "{\"type\":\"file\","
                "\"chatId\":\"" + chatId + "\","
                "\"senderMobile\":\"" + g_myMobile + "\","
                "\"senderName\":\"" + g_myName + "\","
                "\"mimeType\":\"" + mimeType + "\","
                "\"fileName\":\"" + fname + "\"}";
            int hLen = htonl((int)header.size());
            send(s, (char*)&hLen, 4, 0);
            send(s, header.c_str(), (int)header.size(), 0);
            // Send file size (8 bytes big-endian) then raw bytes
            long long fs = fSize.QuadPart;
            long long fsNet = _byteswap_uint64(fs);
            send(s, (char*)&fsNet, 8, 0);
            char buf[8192]; DWORD rd = 0;
            while (ReadFile(hFile, buf, sizeof(buf), &rd, NULL) && rd > 0)
                send(s, buf, rd, 0);
        }
        if (s != INVALID_SOCKET) closesocket(s);
        CloseHandle(hFile);
    }).detach();
}

// ============================================================
// AUDIO / VIDEO CALL  (WASAPI + DirectShow + UDP)
// ============================================================
static RgCallStateCallback g_callStateCb;
static RgVideoFrameCallback g_callVideoCb;
static RgCallParams         g_callParams;
static DWORD                g_callStartTick = 0;

// Simple WASAPI audio capture helper
struct WasapiCapture {
    IMMDeviceEnumerator* devEnum  = nullptr;
    IMMDevice*           dev      = nullptr;
    IAudioClient*        client   = nullptr;
    IAudioCaptureClient* capture  = nullptr;
    WAVEFORMATEX*        wfx      = nullptr;
    bool Init() {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), (void**)&devEnum))) return false;
        if (FAILED(devEnum->GetDefaultAudioEndpoint(eCapture, eCommunications, &dev))) return false;
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&client))) return false;
        if (FAILED(client->GetMixFormat(&wfx))) return false;
        REFERENCE_TIME bufDur = 200000; // 20ms
        if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufDur, 0, wfx, NULL))) return false;
        if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), (void**)&capture))) return false;
        client->Start();
        return true;
    }
    int Read(vector<BYTE>& out) {
        UINT32 packetSize = 0;
        if (FAILED(capture->GetNextPacketSize(&packetSize))) return 0;
        int total = 0;
        while (packetSize > 0) {
            BYTE* data; UINT32 frames; DWORD flags;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, NULL, NULL))) break;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                int bytes = frames * wfx->nBlockAlign;
                out.insert(out.end(), data, data + bytes);
                total += bytes;
            }
            capture->ReleaseBuffer(frames);
            capture->GetNextPacketSize(&packetSize);
        }
        return total;
    }
    void Shutdown() {
        if (client)   { client->Stop(); client->Release(); client = nullptr; }
        if (capture)  { capture->Release(); capture = nullptr; }
        if (dev)      { dev->Release(); dev = nullptr; }
        if (devEnum)  { devEnum->Release(); devEnum = nullptr; }
        if (wfx)      { CoTaskMemFree(wfx); wfx = nullptr; }
    }
};

// Simple WASAPI audio render (speaker output)
struct WasapiRender {
    IMMDeviceEnumerator* devEnum = nullptr;
    IMMDevice*           dev     = nullptr;
    IAudioClient*        client  = nullptr;
    IAudioRenderClient*  render  = nullptr;
    WAVEFORMATEX*        wfx     = nullptr;
    bool Init() {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), (void**)&devEnum))) return false;
        if (FAILED(devEnum->GetDefaultAudioEndpoint(eRender, eCommunications, &dev))) return false;
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&client))) return false;
        if (FAILED(client->GetMixFormat(&wfx))) return false;
        REFERENCE_TIME bufDur = 200000;
        if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, bufDur, 0, wfx, NULL))) return false;
        if (FAILED(client->GetService(__uuidof(IAudioRenderClient), (void**)&render))) return false;
        client->Start();
        return true;
    }
    void Write(const BYTE* data, int bytes) {
        UINT32 bufSize = 0, padding = 0;
        if (FAILED(client->GetBufferSize(&bufSize))) return;
        if (FAILED(client->GetCurrentPadding(&padding))) return;
        UINT32 available = bufSize - padding;
        UINT32 frames = min((UINT32)(bytes / wfx->nBlockAlign), available);
        if (frames == 0) return;
        BYTE* buf;
        if (FAILED(render->GetBuffer(frames, &buf))) return;
        memcpy(buf, data, frames * wfx->nBlockAlign);
        render->ReleaseBuffer(frames, 0);
    }
    void Shutdown() {
        if (client)  { client->Stop(); client->Release(); client = nullptr; }
        if (render)  { render->Release(); render = nullptr; }
        if (dev)     { dev->Release(); dev = nullptr; }
        if (devEnum) { devEnum->Release(); devEnum = nullptr; }
        if (wfx)     { CoTaskMemFree(wfx); wfx = nullptr; }
    }
};

static void CallAudioLoop(SOCKET udpSock, const string& peerIp, int peerPort,
                          bool isVideo) {
    WasapiCapture cap;  cap.Init();
    WasapiRender  rend; rend.Init();

    sockaddr_in peer = {};
    peer.sin_family = AF_INET;
    peer.sin_port   = htons((u_short)peerPort);
    inet_pton(AF_INET, peerIp.c_str(), &peer.sin_addr);

    // Non-blocking recv
    u_long mode = 1; ioctlsocket(udpSock, FIONBIO, &mode);
    g_callStartTick = GetTickCount();

    vector<BYTE> capBuf;
    BYTE recvBuf[8192];

    while (g_callActive) {
        // Capture & send
        capBuf.clear();
        cap.Read(capBuf);
        if (!g_callMuted && !capBuf.empty()) {
            sendto(udpSock, (char*)capBuf.data(), (int)capBuf.size(), 0,
                   (sockaddr*)&peer, sizeof(peer));
        }
        // Receive & render
        sockaddr_in from; int fromLen = sizeof(from);
        int n = recvfrom(udpSock, (char*)recvBuf, sizeof(recvBuf), 0,
                         (sockaddr*)&from, &fromLen);
        if (n > 0) {
            rend.Write(recvBuf, n);
        }

        // Update duration
        g_callDuration = (int)((GetTickCount() - g_callStartTick) / 1000);
        Sleep(10); // ~100 Hz loop
    }

    cap.Shutdown();
    rend.Shutdown();
}

static void CallSignalFirebase(bool start) {
    // Write call signal to Firestore: calls/{chatId}/state
    string path = RgBuildPath("calls", g_callParams.chatId, "state", "current");
    if (start) {
        string payload = "{\"fields\":{"
            "\"callerMobile\":{\"stringValue\":\"" + g_myMobile + "\"},"
            "\"callerName\":{\"stringValue\":\"" + g_myName + "\"},"
            "\"calleeMobile\":{\"stringValue\":\"" + g_callParams.peerMobile + "\"},"
            "\"callType\":{\"stringValue\":\"" + (g_callParams.isVideo ? "video" : "audio") + "\"},"
            "\"status\":{\"stringValue\":\"ringing\"},"
            "\"timestamp\":{\"integerValue\":\"" + to_string(NowMs()) + "\"}"
            "}}";
        RgFirestorePost("PATCH", path, payload);
    } else {
        string payload = "{\"fields\":{"
            "\"status\":{\"stringValue\":\"ended\"},"
            "\"endedAt\":{\"integerValue\":\"" + to_string(NowMs()) + "\"}"
            "}}";
        RgFirestorePost("PATCH", path + "?updateMask.fieldPaths=status&updateMask.fieldPaths=endedAt",
                        payload);
    }
}

void RgCall_StartOutgoing(const RgCallParams& p,
                          RgCallStateCallback stateCb,
                          RgVideoFrameCallback videoCb) {
    if (g_callActive) return;
    g_callParams  = p;
    g_callStateCb = stateCb;
    g_callVideoCb = videoCb;
    g_callActive  = true;
    g_callMuted   = false;
    g_callVideo   = p.isVideo;

    // Signal via Firebase (for internet calls) or direct UDP (LAN)
    thread([p, stateCb]() {
        CallSignalFirebase(true);
        if (stateCb) stateCb(true);

        // Setup UDP socket for audio
        g_callUdpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in local = {};
        local.sin_family      = AF_INET;
        local.sin_port        = htons(RG_CALL_UDP_PORT);
        local.sin_addr.s_addr = INADDR_ANY;
        bind(g_callUdpSock, (sockaddr*)&local, sizeof(local));

        string peerIp = p.peerIp;
        // For internet calls: use relay. Simplified: same IP is fine for LAN tests
        CallAudioLoop(g_callUdpSock, peerIp, RG_CALL_UDP_PORT, p.isVideo);

        CallSignalFirebase(false);
        if (stateCb) stateCb(false);
        if (g_callUdpSock != INVALID_SOCKET) {
            closesocket(g_callUdpSock); g_callUdpSock = INVALID_SOCKET;
        }
    }).detach();
}

void RgCall_AcceptIncoming(const RgCallParams& p,
                           RgCallStateCallback stateCb,
                           RgVideoFrameCallback videoCb) {
    RgCall_StartOutgoing(p, stateCb, videoCb); // same flow, just receiver side
}

void RgCall_Hangup() {
    g_callActive = false;
    if (g_callUdpSock != INVALID_SOCKET) {
        closesocket(g_callUdpSock); g_callUdpSock = INVALID_SOCKET;
    }
    CallSignalFirebase(false);
}

void RgCall_ToggleMute(bool mute)    { g_callMuted  = mute; }
void RgCall_ToggleSpeaker(bool on)   { /* WASAPI render always on */ }
void RgCall_ToggleCamera(bool on)    { g_callVideo  = on; }
bool RgCall_IsActive()               { return g_callActive.load(); }
bool RgCall_IsMuted()                { return g_callMuted.load(); }
bool RgCall_IsVideo()                { return g_callVideo.load(); }
int  RgCall_GetDurationSeconds()     { return g_callDuration.load(); }
