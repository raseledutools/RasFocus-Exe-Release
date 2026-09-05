// ════════════════════════════════════════════════════════════════════
// tab_phone_remote_relay.cpp
//
// Firebase Firestore REST upload/delete — no SDK, just WinINet HTTP.
// This lets phone find PC by 6-digit code alone (no manual IP needed).
// ════════════════════════════════════════════════════════════════════

#pragma warning(disable: 4996)
#define _CRT_SECURE_NO_WARNINGS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "tab_phone_remote_relay.h"
#include <windows.h>
#include <wininet.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <string>
#include <vector>
#include <ctime>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace std;

// ── Helpers ──────────────────────────────────────────────────────

// Get machine hostname for display on phone
static string GetHostname() {
    char buf[256] = {};
    DWORD len = sizeof(buf);
    GetComputerNameA(buf, &len);
    return string(buf);
}

// Simple HTTP PATCH via WinINet (Firestore REST uses PATCH to upsert)
static bool HttpRequest(const string& method,   // "PATCH" or "DELETE"
                        const string& url,
                        const string& body) {
    // Parse URL components
    URL_COMPONENTSA uc = {};
    uc.dwStructSize = sizeof(uc);
    char host[256] = {}, path[1024] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = sizeof(host);
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = sizeof(path);
    uc.dwSchemeLength = 1;
    if (!InternetCrackUrlA(url.c_str(), 0, 0, &uc)) return false;

    bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = uc.nPort ? uc.nPort : (isHttps ? 443 : 80);

    HINTERNET hInet = InternetOpenA("RasFocus/1.0",
                                    INTERNET_OPEN_TYPE_PRECONFIG,
                                    NULL, NULL, 0);
    if (!hInet) return false;

    HINTERNET hConn = InternetConnectA(hInet, host, port,
                                       NULL, NULL,
                                       INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return false; }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (isHttps) flags |= INTERNET_FLAG_SECURE |
                          INTERNET_FLAG_IGNORE_CERT_CN_INVALID |
                          INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;

    HINTERNET hReq = HttpOpenRequestA(hConn, method.c_str(), path,
                                      NULL, NULL, NULL, flags, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return false; }

    // Content-Type header for JSON body
    const char* hdrs = "Content-Type: application/json\r\n";
    BOOL ok = HttpSendRequestA(hReq, hdrs, (DWORD)strlen(hdrs),
                               body.empty() ? NULL : (LPVOID)body.c_str(),
                               (DWORD)body.size());
    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    return ok == TRUE;
}

// ── Firestore document JSON builder ──────────────────────────────
// Firestore REST API expects a specific JSON format for fields.
static string BuildFirestoreDocument(const string& code,
                                     const string& ip,
                                     int port) {
    time_t now = time(nullptr);
    string ts = to_string((long long)now);
    string hostname = GetHostname();

    // Firestore "document" format
    // {"fields": {"key": {"stringValue": "val"}, ...}}
    string json = "{\"fields\":{"
        "\"code\":{\"stringValue\":\"" + code + "\"},"
        "\"ip\":{\"stringValue\":\"" + ip + "\"},"
        "\"port\":{\"integerValue\":\"" + to_string(port) + "\"},"
        "\"hostname\":{\"stringValue\":\"" + hostname + "\"},"
        "\"platform\":{\"stringValue\":\"windows\"},"
        "\"ts\":{\"integerValue\":\"" + ts + "\"}"
        "}}";
    return json;
}

// ── Public API ───────────────────────────────────────────────────

void RelayRegisterSession(const string& code,
                          const string& localIp,
                          int port) {
    // Run in background thread — don't block the UI
    thread([code, localIp, port]() {
        string url = FIRESTORE_BASE + code + "?key=" + FIREBASE_API_KEY;
        string body = BuildFirestoreDocument(code, localIp, port);
        // PATCH = create or update
        bool ok = HttpRequest("PATCH", url, body);
        // Silent — no UI feedback needed
        (void)ok;
    }).detach();
}

void RelayUnregisterSession(const string& code) {
    if (code.empty()) return;
    thread([code]() {
        string url = FIRESTORE_BASE + code + "?key=" + FIREBASE_API_KEY;
        HttpRequest("DELETE", url, "");
    }).detach();
}

bool RelayIsAvailable() {
    // For now, always return true — relay is always attempted as fallback
    return true;
}
