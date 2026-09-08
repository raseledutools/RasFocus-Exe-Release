// rasgram_net.h
// RasGram Desktop — Network & Backend Layer
// Firebase Firestore REST + LAN UDP/TCP (same protocol as Android LanChatManager)
// Audio/Video call via WASAPI + DirectShow + UDP

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

using namespace std;

// ============================================================
// CONSTANTS
// ============================================================
#define RG_FIREBASE_PROJECT   "rasfocus-c746d"
#define RG_FIRESTORE_HOST     "firestore.googleapis.com"
#define RG_STORAGE_HOST       "firebasestorage.googleapis.com"
#define RG_LAN_UDP_PORT       5555     // Beacon (same as Android)
#define RG_LAN_TCP_PORT       5556     // Message/file transfer
#define RG_CALL_UDP_PORT      5558     // Audio/Video RTP (5557 used by phone remote)
#define RG_BEACON_INTERVAL_MS 3000
#define RG_PEER_TIMEOUT_MS    10000

// ============================================================
// DATA STRUCTURES  (mirrors Android RasGram Kotlin data classes)
// ============================================================

struct RgUser {
    string uid;
    string mobile;       // phone number = unique ID in RasGram
    string name;
    string avatarUrl;
    bool   isOnline   = false;
    long long lastSeen = 0;
};

struct RgMessage {
    string   id;
    string   chatId;
    string   senderMobile;
    string   senderName;
    string   text;
    long long timestamp  = 0;
    string   timeString;
    string   fileUrl;
    string   fileName;
    string   fileType;   // "image/*", "audio/*", "video/*", etc.
    long long fileSizeBytes = 0;
    string   reaction;
    bool     read        = false;
    bool     delivered   = false;
    bool     isCallLog   = false;
    string   callStatus; // "missed", "answered", "declined"
    string   callType;   // "audio", "video"
    bool     isDeleted   = false;
    bool     isForwarded = false;
    bool     isPending   = false;
    string   replyToId;
    string   replyToText;
    string   replyToSender;
    int      duration    = 0; // voice message duration in seconds
    bool     deliveredViaLan = false;
};

struct RgChatPreview {
    string   contactMobile;
    string   contactName;
    string   contactAvatarUrl;
    string   lastMessageText;
    string   lastMessageSender;
    long long lastTimestamp = 0;
    string   lastTimeString;
    string   lastFileType;
    bool     lastIsCallLog = false;
    int      unreadCount   = 0;
    bool     isPinned      = false;
    bool     isMuted       = false;
};

struct RgLanPeer {
    string mobile;
    string name;
    string ip;
    int    port = RG_LAN_TCP_PORT;
};

// ============================================================
// CALLBACKS
// ============================================================
typedef function<void(const vector<RgChatPreview>&)>   RgChatsCallback;
typedef function<void(const vector<RgMessage>&)>       RgMessagesCallback;
typedef function<void(const RgMessage&)>               RgNewMessageCallback;
typedef function<void(const vector<RgLanPeer>&)>       RgLanPeersCallback;
typedef function<void(bool /*connected*/)>             RgCallStateCallback;
typedef function<void(const void* /*frameRGB*/,
                      int w, int h)>                   RgVideoFrameCallback;

// ============================================================
// RASGRAM NET — PUBLIC API
// ============================================================

// Initialization
void RgNet_Init(const string& myMobile, const string& myName,
                const string& myUid, const string& idToken);
void RgNet_Shutdown();

// ── Contacts & Chats ────────────────────────────────────────
// Fetch contact list from Firestore (users/{myMobile}/contacts)
void RgNet_FetchContacts(RgChatsCallback cb);

// Start polling chat list (calls cb every ~2s on background thread)
void RgNet_StartChatListPolling(RgChatsCallback cb);
void RgNet_StopChatListPolling();

// Load messages for a specific chat
void RgNet_FetchMessages(const string& chatId, RgMessagesCallback cb);

// Start polling new messages for open chat
void RgNet_StartMessagePolling(const string& chatId, long long sinceTimestamp,
                               RgNewMessageCallback cb);
void RgNet_StopMessagePolling();

// Send a text message
void RgNet_SendText(const string& chatId,
                    const string& text,
                    const string& receiverMobile);

// Mark messages as read
void RgNet_MarkRead(const string& chatId, const string& myMobile);

// ── File / Voice ─────────────────────────────────────────────
// Upload a local file to Firebase Storage, then send message
void RgNet_SendFile(const string& chatId,
                    const string& receiverMobile,
                    const wstring& localFilePath,
                    const string& mimeType);

// ── LAN Mode ─────────────────────────────────────────────────
void RgNet_StartLan(RgLanPeersCallback peersCb,
                    RgNewMessageCallback msgCb);
void RgNet_StopLan();
void RgNet_LanSendText(const RgLanPeer& peer,
                       const string& chatId,
                       const string& text);
void RgNet_LanSendFile(const RgLanPeer& peer,
                       const string& chatId,
                       const wstring& filePath,
                       const string& mimeType);

// ── Audio / Video Calls ──────────────────────────────────────
struct RgCallParams {
    string   chatId;
    string   peerMobile;
    string   peerName;
    string   peerIp;     // for LAN direct call
    bool     isVideo  = false;
    bool     isLan    = false; // true = LAN direct, false = relay via Firebase
};

void RgCall_StartOutgoing(const RgCallParams& p,
                          RgCallStateCallback stateCb,
                          RgVideoFrameCallback videoCb = nullptr);

void RgCall_AcceptIncoming(const RgCallParams& p,
                           RgCallStateCallback stateCb,
                           RgVideoFrameCallback videoCb = nullptr);

void RgCall_Hangup();
void RgCall_ToggleMute(bool mute);
void RgCall_ToggleSpeaker(bool on);
void RgCall_ToggleCamera(bool on);

bool RgCall_IsActive();
bool RgCall_IsMuted();
bool RgCall_IsVideo();
int  RgCall_GetDurationSeconds();

// ── Presence ─────────────────────────────────────────────────
void RgNet_SetOnline(bool online);

// ── Helpers ──────────────────────────────────────────────────
// Build Firestore REST path
string RgBuildPath(const string& collection, const string& docId = "",
                   const string& sub = "", const string& subId = "");
// HTTP GET to Firestore
string RgFirestoreGet(const string& path);
// HTTP POST/PATCH to Firestore
string RgFirestorePost(const string& method, const string& path,
                       const string& jsonBody);
// Parse a simple string field from Firestore JSON response
string RgParseField(const string& json, const string& field);
// Parse integer field
long long RgParseIntField(const string& json, const string& field);
// Format timestamp to "HH:MM" string
string RgFormatTime(long long timestampMs);
// Build chatId from two mobiles (same logic as Android)
string RgBuildChatId(const string& mobileA, const string& mobileB);
