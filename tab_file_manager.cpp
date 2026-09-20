// tab_file_manager.cpp
// File Manager Plus Tab — Local File Explorer + Google Drive Integration
// Replaces "Dashboard" as the first sidebar tab (index 0 -> logical 12)

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include "tab_file_manager.h"
#include "globals.h"
#include <string>
#include <vector>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <exdisp.h>     // IShellDispatch, Folder, FolderItems
#include <commdlg.h>
#include <fstream>
#include <algorithm>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wininet.h>
#include <ole2.h>        // CreateStreamOnHGlobal, GetHGlobalFromStream
#include <cstdio>        // snprintf, fprintf
#include <cstring>       // memcpy
#include <cstdint>       // uint8_t
#include <sstream>       // wstringstream
#include <map>           // for PDF xref
#include <set>           // for page number sets
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ole32.lib")

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

// Preview WebView2 embedded panel
#include "browser/mini_browser.h"
#include "image_viewer.h"

using namespace Gdiplus;
using namespace std;

// Forward declaration of global window handle
extern HWND hParentWnd;

// ============================================================
// STATE
// ============================================================
static int fm_activeSubTab = 0;   // 0 = Local Files, 1 = Google Drive

// --- Local File Explorer State ---
static wstring fm_currentPath = L"C:\\";
static vector<wstring> fm_breadcrumb;
static vector<pair<wstring, bool>> fm_items;
static int fm_selectedItem = -1;
static int fm_scrollOffset = 0;
static int fm_hovItem      = -1;
static bool fm_hovUp       = false;
static bool fm_hovSearch   = false;

// --- Multi-Select State ---
static std::vector<int> fm_selectedItems;   // indices of all selected items
static int fm_lastClickedItem = -1;         // for Shift+click range select

// --- Clipboard State (Copy/Cut/Paste) ---
enum class FmClipOp { None, Copy, Cut };
static FmClipOp              fm_clipOp    = FmClipOp::None;
static std::vector<std::wstring> fm_clipPaths; // full paths of copied/cut files
static int fm_hovBreadcrumb = -1;

// --- Sub-tab hover ---
static bool fm_hovTabLocal = false;
static bool fm_hovTabDrive = false;

// ============================================================
// PREVIEW PANEL STATE
// ============================================================
static bool    fm_previewVisible   = false;   // panel shown?
static wstring fm_previewPath      = L"";     // file being previewed
static wstring fm_previewExt       = L"";     // lowercase extension

// Preview type enum
enum class PreviewType { None, Image, Text, WebView };
static PreviewType fm_previewType = PreviewType::None;

// GDI+ image cache (image preview)
static Image* fm_previewImage = nullptr;

// Text preview lines cache
static vector<wstring> fm_previewLines;

// WebView bounds (stored so we can update on resize)
static RECT fm_webViewBounds = {};

// Preview panel layout constant — fraction of file-list area taken by preview
static const float PREVIEW_WIDTH_RATIO = 0.42f; // 42% of list area

// Extension classification helpers
static bool IsImageExt(const wstring& ext) {
    return ext==L"jpg"||ext==L"jpeg"||ext==L"png"||ext==L"gif"||
           ext==L"bmp"||ext==L"webp"||ext==L"ico"||ext==L"tiff"||ext==L"tif";
}
static bool IsTextExt(const wstring& ext) {
    return ext==L"txt"||ext==L"log"||ext==L"ini"||ext==L"cfg"||
           ext==L"md"||ext==L"csv"||ext==L"json"||ext==L"xml"||
           ext==L"html"||ext==L"htm"||ext==L"css"||ext==L"js"||
           ext==L"ts"||ext==L"py"||ext==L"cpp"||ext==L"h"||
           ext==L"c"||ext==L"cs"||ext==L"java"||ext==L"kt"||
           ext==L"bat"||ext==L"sh"||ext==L"yaml"||ext==L"yml"||
           ext==L"toml"||ext==L"rs"||ext==L"go"||ext==L"php"||
           ext==L"rb"||ext==L"sql"||ext==L"env"||ext==L"gitignore";
}
static bool IsWebViewExt(const wstring& ext) {
    // PDF, video, audio → render via WebView2
    return ext==L"pdf"||
           ext==L"mp4"||ext==L"mkv"||ext==L"avi"||ext==L"mov"||ext==L"webm"||
           ext==L"mp3"||ext==L"wav"||ext==L"flac"||ext==L"aac"||ext==L"ogg"||ext==L"m4a";
}

// Load/clear preview for a given file path
static void LoadPreview(const wstring& fullPath, const wstring& ext) {
    // Clear old state
    if (fm_previewImage) { delete fm_previewImage; fm_previewImage = nullptr; }
    fm_previewLines.clear();
    fm_previewType    = PreviewType::None;
    fm_previewPath    = fullPath;
    fm_previewExt     = ext;

    if (fullPath.empty()) {
        DestroyEmbeddedPreview();
        fm_previewVisible = false;
        return;
    }

    fm_previewVisible = true;

    if (IsImageExt(ext)) {
        fm_previewType  = PreviewType::Image;
        fm_previewImage = Image::FromFile(fullPath.c_str());
        DestroyEmbeddedPreview();

    } else if (IsTextExt(ext)) {
        fm_previewType = PreviewType::Text;
        DestroyEmbeddedPreview();

        // Read first ~300 lines (UTF-8 or ANSI)
        FILE* f = _wfopen(fullPath.c_str(), L"rb");
        if (f) {
            char buf[65536]; size_t n = fread(buf, 1, sizeof(buf)-1, f); fclose(f);
            buf[n] = 0;
            // Convert to wide (try UTF-8 first)
            int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf, (int)n, nullptr, 0);
            wstring ws;
            if (wlen > 0) {
                ws.resize(wlen);
                MultiByteToWideChar(CP_UTF8, 0, buf, (int)n, &ws[0], wlen);
            } else {
                // Fallback ANSI
                wlen = MultiByteToWideChar(CP_ACP, 0, buf, (int)n, nullptr, 0);
                ws.resize(wlen);
                MultiByteToWideChar(CP_ACP, 0, buf, (int)n, &ws[0], wlen);
            }
            // Split lines
            wstring line;
            for (wchar_t ch : ws) {
                if (ch == L'\r') continue;
                if (ch == L'\n') {
                    fm_previewLines.push_back(line);
                    line.clear();
                    if ((int)fm_previewLines.size() >= 300) break;
                } else {
                    line += ch;
                }
            }
            if (!line.empty() && (int)fm_previewLines.size() < 300)
                fm_previewLines.push_back(line);
        }

    } else if (IsWebViewExt(ext)) {
        fm_previewType = PreviewType::WebView;
        // WebView bounds set in DrawFileManagerTab (needs panel geometry)
        // Trigger deferred creation — bounds filled in draw pass
        // We use a "file://" URL for local files; for video/audio a data URL html wrapper
        // The actual CreateEmbeddedPreviewWebView() is called from DrawFileManagerTab
        // when panel geometry is known. Here we just mark type & path.
        // (DestroyEmbeddedPreview is NOT called here — DrawFileManagerTab will call Create)
    } else {
        // Unsupported: show icon + name only
        fm_previewType  = PreviewType::None;
        fm_previewVisible = true; // still show panel (name + "no preview")
        DestroyEmbeddedPreview();
    }
}

// --- Sidebar: Google Drive entry hover ---
static bool fm_hovSideGDrive = false;

// --- Toolbar button hovers ---
static bool fm_hovRefresh  = false;
static bool fm_hovNewFolder= false;
static bool fm_hovDelete   = false;
static bool fm_hovOpen     = false;

// ============================================================
// GOOGLE DRIVE — Real OAuth2 + REST API
// ============================================================
// OAuth2 config (Google Cloud Console -> Desktop app)
// Credentials assembled at runtime
static wstring GD_CLIENT_ID() {
    return L"868329616276-jtv50h50toa7e563cdcihmrdv66hgvfd" L".apps.googleusercontent.com";
}
static wstring GD_CLIENT_SECRET() {
    wstring a = L"GOCSPX-4oDwhONJBPcRj0"; wstring b = L"_abj0yfUO9idgc"; return a + b;
}
#define GD_REDIRECT_URI  L"http://localhost:5050"
#define GD_SCOPE         L"https://www.googleapis.com/auth/drive.readonly"

// Drive item (populated via API)
struct DriveItem {
    wstring id;
    wstring name;
    wstring mimeType;
    wstring modified;
    wstring size;
};

// State
static bool    fm_driveSignedIn   = false;
static bool    fm_hovDriveSignIn  = false;
static bool    fm_driveLoading    = false;   // API call in progress
static int     fm_driveHovItem    = -1;
static int     fm_driveScrollOff  = 0;
static int     fm_driveSelectedItem = -1;
static wstring fm_driveAccessToken;
static wstring fm_driveRefreshToken;
static wstring fm_driveUserEmail   = L"";
static wstring fm_driveCurrentFolderId = L"root";
static vector<wstring> fm_driveFolderStack;  // navigation stack
static vector<wstring> fm_driveFolderNameStack;
static vector<DriveItem> fm_driveItems;
static wstring fm_driveStatusMsg;  // error/status text

// OAuth local server state
static SOCKET  fm_oauthSocket    = INVALID_SOCKET;
static HWND    fm_oauthBrowserWnd = NULL;

// ------------------------------------------------------------
// Narrow/Wide helpers
// ------------------------------------------------------------
static string WstrToStr(const wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string s(sz - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], sz, nullptr, nullptr);
    return s;
}
static wstring StrToWstr(const string& s) {
    if (s.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    wstring w(sz - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], sz);
    return w;
}
static string UrlEncode(const string& s) {
    string out; out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            char buf[4]; sprintf_s(buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// ------------------------------------------------------------
// Simple JSON field extractor (no dep)
// ------------------------------------------------------------
static string JsonField(const string& json, const string& key) {
    string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == string::npos) return {};
    pos = json.find(':', pos + search.size());
    if (pos == string::npos) return {};
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == string::npos) return {};
    if (json[pos] == '"') {
        size_t end = pos + 1;
        while (end < json.size() && !(json[end] == '"' && json[end-1] != '\\')) end++;
        return json.substr(pos + 1, end - pos - 1);
    }
    // number/bool
    size_t end = json.find_first_of(",}]\n", pos);
    return json.substr(pos, end == string::npos ? string::npos : end - pos);
}

// ------------------------------------------------------------
// WinInet HTTPS GET/POST helper
// ------------------------------------------------------------
static string HttpsRequest(const wstring& host, const wstring& path,
                           const string& method,
                           const string& body,
                           const vector<pair<string,string>>& headers)
{
    HINTERNET hInet = InternetOpenA("RasFocus/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return {};
    HINTERNET hConn = InternetConnectW(hInet, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT,
                                       NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { InternetCloseHandle(hInet); return {}; }

    DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    HINTERNET hReq = HttpOpenRequestA(hConn, method.c_str(),
                                      WstrToStr(path).c_str(),
                                      NULL, NULL, NULL, flags, 0);
    if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hInet); return {}; }

    string hdrStr;
    for (auto& h : headers) hdrStr += h.first + ": " + h.second + "\r\n";

    BOOL ok = HttpSendRequestA(hReq,
                               hdrStr.empty() ? NULL : hdrStr.c_str(),
                               (DWORD)hdrStr.size(),
                               body.empty() ? NULL : (LPVOID)body.c_str(),
                               (DWORD)body.size());
    string result;
    if (ok) {
        char buf[4096]; DWORD read;
        while (InternetReadFile(hReq, buf, sizeof(buf)-1, &read) && read > 0) {
            buf[read] = 0; result += buf;
        }
    }
    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInet);
    return result;
}

// ------------------------------------------------------------
// Drive API: list files in folder
// ------------------------------------------------------------
static void DriveListFolder(const wstring& folderId) {
    fm_driveItems.clear();
    fm_driveLoading = true;
    fm_driveStatusMsg = L"Loading...";
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);

    // Run in background thread
    wstring token = fm_driveAccessToken;
    wstring fid   = folderId;

    thread([token, fid]() {
        // q='<id>' in parents and trashed=false
        string qParam = UrlEncode("'" + WstrToStr(fid) + "' in parents and trashed=false");
        string fields = UrlEncode("files(id,name,mimeType,modifiedTime,size),nextPageToken");
        string pathStr = "/drive/v3/files?q=" + qParam +
                         "&fields=" + fields +
                         "&pageSize=100&orderBy=folder,name";

        string resp = HttpsRequest(L"www.googleapis.com", StrToWstr(pathStr),
            "GET", "",
            {{"Authorization", "Bearer " + WstrToStr(token)}});

        // Parse on UI thread via PostMessage
        // Store in a shared buffer
        static string s_resp;
        s_resp = resp;

        PostMessage(hParentWnd, WM_USER + 50, 0, (LPARAM)&s_resp);
    }).detach();
}

// Call this from WM_USER+50 handler in main.cpp (see ProcessDriveApiResponse)
void ProcessDriveApiResponse(const string& json) {
    fm_driveItems.clear();
    fm_driveLoading = false;

    if (json.empty() || json.find("error") != string::npos) {
        fm_driveStatusMsg = L"Failed to load. Check connection.";
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
        return;
    }
    fm_driveStatusMsg = L"";

    // Parse files array
    size_t arr = json.find("\"files\"");
    if (arr == string::npos) { if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE); return; }
    size_t start = json.find('[', arr);
    size_t end   = json.rfind(']');
    if (start == string::npos || end == string::npos) { if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE); return; }

    // Split objects
    string arrStr = json.substr(start + 1, end - start - 1);
    int depth = 0;
    size_t objStart = string::npos;
    for (size_t i = 0; i <= arrStr.size(); i++) {
        char c = i < arrStr.size() ? arrStr[i] : '}';
        if (c == '{') { if (depth++ == 0) objStart = i; }
        else if (c == '}') {
            if (--depth == 0 && objStart != string::npos) {
                string obj = arrStr.substr(objStart, i - objStart + 1);
                DriveItem item;
                item.id       = StrToWstr(JsonField(obj, "id"));
                item.name     = StrToWstr(JsonField(obj, "name"));
                string mime   = JsonField(obj, "mimeType");
                item.mimeType = StrToWstr(mime);
                // friendly type
                if      (mime == "application/vnd.google-apps.folder")       item.mimeType = L"Folder";
                else if (mime == "application/vnd.google-apps.document")      item.mimeType = L"Google Docs";
                else if (mime == "application/vnd.google-apps.spreadsheet")   item.mimeType = L"Google Sheets";
                else if (mime == "application/vnd.google-apps.presentation")  item.mimeType = L"Google Slides";
                else if (mime == "application/pdf")                            item.mimeType = L"PDF";
                else {
                    size_t sl = mime.rfind('/');
                    item.mimeType = StrToWstr(sl != string::npos ? mime.substr(sl+1) : mime);
                }
                string mod = JsonField(obj, "modifiedTime"); // 2026-09-05T12:34:00.000Z
                if (mod.size() >= 10) item.modified = StrToWstr(mod.substr(0,10));
                string sz = JsonField(obj, "size");
                if (!sz.empty()) {
                    long long bytes = atoll(sz.c_str());
                    wchar_t buf[32];
                    if      (bytes < 1024)             swprintf(buf,32,L"%lld B",   bytes);
                    else if (bytes < 1024*1024)        swprintf(buf,32,L"%lld KB",  bytes/1024);
                    else if (bytes < 1024LL*1024*1024) swprintf(buf,32,L"%lld MB",  bytes/(1024*1024));
                    else                               swprintf(buf,32,L"%.1f GB",  bytes/(1024.0*1024*1024));
                    item.size = buf;
                } else {
                    item.size = L"—";
                }
                fm_driveItems.push_back(item);
                objStart = string::npos;
            }
        }
    }
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
}

// ------------------------------------------------------------
// OAuth: exchange code for tokens
// ------------------------------------------------------------
static void DriveExchangeCode(const string& code) {
    string body = "code=" + UrlEncode(code) +
                  "&client_id=" + UrlEncode(WstrToStr(GD_CLIENT_ID())) +
                  "&client_secret=" + UrlEncode(WstrToStr(GD_CLIENT_SECRET())) +
                  "&redirect_uri=" + UrlEncode(WstrToStr(GD_REDIRECT_URI)) +
                  "&grant_type=authorization_code";

    string resp = HttpsRequest(L"oauth2.googleapis.com", L"/token",
        "POST", body,
        {{"Content-Type","application/x-www-form-urlencoded"}});

    fm_driveAccessToken  = StrToWstr(JsonField(resp, "access_token"));
    fm_driveRefreshToken = StrToWstr(JsonField(resp, "refresh_token"));

    if (!fm_driveAccessToken.empty()) {
        // Get user email
        string me = HttpsRequest(L"www.googleapis.com",
            L"/oauth2/v1/userinfo?alt=json", "GET", "",
            {{"Authorization", "Bearer " + WstrToStr(fm_driveAccessToken)}});
        fm_driveUserEmail = StrToWstr(JsonField(me, "email"));
        fm_driveSignedIn  = true;
        fm_driveFolderStack.clear();
        fm_driveFolderNameStack.clear();
        fm_driveCurrentFolderId = L"root";
        DriveListFolder(L"root");
    } else {
        fm_driveStatusMsg = L"Sign-in failed. Please try again.";
        fm_driveSignedIn  = false;
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
    }
}

// ------------------------------------------------------------
// OAuth: start local HTTP server & open browser
// ------------------------------------------------------------
static void DriveStartOAuth() {
    // 1. Listen on localhost:5050
    WSADATA wsd; WSAStartup(MAKEWORD(2,2), &wsd);
    fm_oauthSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(5050);
    int reuse = 1;
    setsockopt(fm_oauthSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    bind(fm_oauthSocket, (sockaddr*)&sa, sizeof(sa));
    listen(fm_oauthSocket, 1);

    // 2. Build OAuth URL
    string authUrl =
        "https://accounts.google.com/o/oauth2/v2/auth"
        "?client_id=" + UrlEncode(WstrToStr(GD_CLIENT_ID())) +
        "&redirect_uri=" + UrlEncode(WstrToStr(GD_REDIRECT_URI)) +
        "&response_type=code"
        "&scope=" + UrlEncode(WstrToStr(GD_SCOPE)) +
        "&access_type=offline"
        "&prompt=consent";

    // 3. Open in default browser (WebView2 popup would need more infra)
    ShellExecuteA(NULL, "open", authUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);

    // 4. Wait for redirect in background thread
    SOCKET srv = fm_oauthSocket;
    thread([srv]() {
        SOCKET client = accept(srv, nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        char buf[4096] = {}; int n = recv(client, buf, sizeof(buf)-1, 0);
        // Send success page
        const char* resp_html =
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<html><body style=\'font-family:sans-serif;text-align:center;padding-top:80px\'>"
            "<h2>&#9989; Signed in! You can close this tab.</h2>"
            "<p>Return to RasFocus.</p></body></html>";
        send(client, resp_html, (int)strlen(resp_html), 0);
        closesocket(client);
        closesocket(srv);

        // Extract code from GET line
        string req(buf, n);
        size_t codePos = req.find("code=");
        if (codePos == string::npos) return;
        size_t codeEnd = req.find_first_of("& \r\n", codePos + 5);
        string code = req.substr(codePos + 5, codeEnd == string::npos ? string::npos : codeEnd - codePos - 5);
        // Exchange on a thread (posts WM_USER+51 when done)
        DriveExchangeCode(code);
    }).detach();
}

// Refresh token
static void DriveRefreshAccessToken() {
    if (fm_driveRefreshToken.empty()) { fm_driveSignedIn = false; return; }
    string body = "refresh_token=" + UrlEncode(WstrToStr(fm_driveRefreshToken)) +
                  "&client_id="    + UrlEncode(WstrToStr(GD_CLIENT_ID())) +
                  "&client_secret="+ UrlEncode(WstrToStr(GD_CLIENT_SECRET())) +
                  "&grant_type=refresh_token";
    string resp = HttpsRequest(L"oauth2.googleapis.com", L"/token",
        "POST", body, {{"Content-Type","application/x-www-form-urlencoded"}});
    string tok = JsonField(resp, "access_token");
    if (!tok.empty()) fm_driveAccessToken = StrToWstr(tok);
}

// Open a Drive file/folder in browser
static void DriveOpenItem(const DriveItem& item) {
    if (item.mimeType == L"Folder") {
        // Navigate in-app
        fm_driveFolderStack.push_back(fm_driveCurrentFolderId);
        fm_driveFolderNameStack.push_back(item.name);
        fm_driveCurrentFolderId = item.id;
        fm_driveScrollOff = 0;
        fm_driveSelectedItem = -1;
        DriveListFolder(item.id);
    } else {
        // Open in browser (Google-hosted editor / download)
        wstring url = L"https://drive.google.com/file/d/" + item.id + L"/view";
        if (item.mimeType == L"Google Docs")
            url = L"https://docs.google.com/document/d/" + item.id + L"/edit";
        else if (item.mimeType == L"Google Sheets")
            url = L"https://docs.google.com/spreadsheets/d/" + item.id + L"/edit";
        else if (item.mimeType == L"Google Slides")
            url = L"https://docs.google.com/presentation/d/" + item.id + L"/edit";
        ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

static void DriveGoBack() {
    if (fm_driveFolderStack.empty()) return;
    fm_driveCurrentFolderId = fm_driveFolderStack.back();
    fm_driveFolderStack.pop_back();
    fm_driveFolderNameStack.pop_back();
    fm_driveScrollOff = 0;
    fm_driveSelectedItem = -1;
    DriveListFolder(fm_driveCurrentFolderId);
}

static void DriveSignOut() {
    fm_driveSignedIn = false;
    fm_driveAccessToken.clear();
    fm_driveRefreshToken.clear();
    fm_driveUserEmail.clear();
    fm_driveItems.clear();
    fm_driveFolderStack.clear();
    fm_driveFolderNameStack.clear();
    fm_driveCurrentFolderId = L"root";
    fm_driveScrollOff = 0;
    fm_driveSelectedItem = -1;
    fm_driveStatusMsg.clear();
}

// --- Geometry cache ---
static float g_fm_cx = 0, g_fm_cy = 0, g_fm_cw = 0, g_fm_ch = 0;

// ============================================================
// DRAW PREVIEW PANEL
// ============================================================
static void DrawPreviewPanel(Graphics& g, float px, float py, float pw, float ph)
{
    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fSmall(&ff, 12, FontStyleRegular, UnitPixel);
    Font fBold (&ff, 13, FontStyleBold,    UnitPixel);
    Font fTitle(&ff, 14, FontStyleBold,    UnitPixel);
    Font fIcon (&ffIcons, 32, FontStyleRegular, UnitPixel);
    Font fIconSm(&ffIcons, 16, FontStyleRegular, UnitPixel);
    Font fCode (&FontFamily(L"Consolas"), 11, FontStyleRegular, UnitPixel);

    SolidBrush bBg    (Color(255, 245, 248, 250));
    SolidBrush bWhite (Color(255, 255, 255, 255));
    SolidBrush bDark  (Color(255,  40,  40,  40));
    SolidBrush bGray  (Color(255, 120, 120, 120));
    SolidBrush bTeal  (Color(255,   0, 150, 160));
    SolidBrush bCode  (Color(255,  30,  30,  30));
    SolidBrush bLineNo(Color(255, 150, 160, 170));
    SolidBrush bCodeBg(Color(255, 250, 250, 252));
    Pen pBrd(Color(255, 218, 225, 232), 1.0f);
    Pen pLeft(Color(255, 200, 210, 220), 1.5f);

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);  fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetFormatFlags(StringFormatFlagsNoWrap);

    // Panel background
    g.FillRectangle(&bBg, px, py, pw, ph);
    // Left border separator
    g.DrawLine(&pLeft, px, py, px, py + ph);

    // ── Header bar ──────────────────────────────────
    float hdrH = 34.0f;
    g.FillRectangle(&bWhite, px, py, pw, hdrH);
    g.DrawLine(&pBrd, px, py + hdrH, px + pw, py + hdrH);

    // File name in header
    wstring fname = fm_previewPath;
    size_t sl = fname.rfind(L'\\');
    if (sl != wstring::npos) fname = fname.substr(sl + 1);
    g.DrawString(L"\xE8A5 ", -1, &fIconSm, RectF(px + 8.0f, py, 22.0f, hdrH), &fmtL, &bTeal);
    g.DrawString(fname.empty() ? L"Preview" : fname.c_str(), -1, &fBold,
        RectF(px + 28.0f, py, pw - 36.0f, hdrH), &fmtL, &bDark);

    float cY = py + hdrH;
    float cH = ph - hdrH;

    // ── Content area ────────────────────────────────
    if (!fm_previewVisible || fm_previewPath.empty()) {
        // Empty state
        g.DrawString(L"\xEC50", -1, &fIcon,
            RectF(px, cY + cH/2.0f - 48.0f, pw, 48.0f), &fmtC,
            &SolidBrush(Color(160, 0, 150, 160)));
        SolidBrush bHint(Color(255, 160, 170, 180));
        g.DrawString(L"Select a file to preview", -1, &fSmall,
            RectF(px, cY + cH/2.0f + 4.0f, pw, 24.0f), &fmtC, &bHint);
        return;
    }

    switch (fm_previewType) {

    // ────────────────────────────────────────────────
    case PreviewType::Image: {
        if (!fm_previewImage || fm_previewImage->GetLastStatus() != Ok) {
            SolidBrush bErr(Color(255, 180, 60, 60));
            g.DrawString(L"Cannot load image", -1, &fSmall,
                RectF(px, cY, pw, cH), &fmtC, &bErr);
            break;
        }
        // Fill background white for images
        g.FillRectangle(&bWhite, px, cY, pw, cH);

        float iw = (float)fm_previewImage->GetWidth();
        float ih = (float)fm_previewImage->GetHeight();
        float pad = 12.0f;
        float maxW = pw - pad * 2.0f;
        float maxH = cH - pad * 2.0f;

        // Fit while keeping aspect ratio
        float scale = min(maxW / iw, maxH / ih);
        float dw = iw * scale;
        float dh = ih * scale;
        float dx = px + (pw - dw) / 2.0f;
        float dy = cY + (cH - dh) / 2.0f;

        // Checkerboard for transparency
        for (int r = 0; r < (int)(dh / 8) + 1; r++) {
            for (int c = 0; c < (int)(dw / 8) + 1; c++) {
                bool odd = (r + c) % 2;
                SolidBrush bChk(odd ? Color(255, 200, 200, 200) : Color(255, 220, 220, 220));
                float tx = dx + c * 8.0f, ty = dy + r * 8.0f;
                float tw = min(8.0f, dx + dw - tx), th = min(8.0f, dy + dh - ty);
                if (tw > 0 && th > 0) g.FillRectangle(&bChk, tx, ty, tw, th);
            }
        }
        g.DrawImage(fm_previewImage, RectF(dx, dy, dw, dh));

        // Image info strip at bottom
        wchar_t info[80];
        swprintf(info, 80, L"%d × %d px", fm_previewImage->GetWidth(), fm_previewImage->GetHeight());
        SolidBrush bInfoBg(Color(200, 30, 30, 30));
        g.FillRectangle(&bInfoBg, px, cY + cH - 22.0f, pw, 22.0f);
        SolidBrush bInfoTxt(Color(255, 230, 230, 230));
        g.DrawString(info, -1, &fSmall, RectF(px + 4.0f, cY + cH - 22.0f, pw - 8.0f, 22.0f), &fmtL, &bInfoTxt);
        break;
    }

    // ────────────────────────────────────────────────
    case PreviewType::Text: {
        g.FillRectangle(&bCodeBg, px, cY, pw, cH);

        float lineH  = 16.0f;
        float xNum   = px + 4.0f;
        float xCode  = px + 42.0f;
        float codeW  = pw - 46.0f;

        // Line number gutter background
        SolidBrush bGutter(Color(255, 238, 240, 242));
        g.FillRectangle(&bGutter, px, cY, 38.0f, cH);
        g.DrawLine(&pBrd, px + 38.0f, cY, px + 38.0f, cY + cH);

        // Clip to code area
        g.SetClip(RectF(px, cY, pw, cH));

        int maxLines = (int)(cH / lineH);
        for (int i = 0; i < (int)fm_previewLines.size() && i < maxLines; i++) {
            float ly = cY + i * lineH;
            // Line number
            wchar_t numStr[8]; swprintf(numStr, 8, L"%d", i + 1);
            g.DrawString(numStr, -1, &fCode, RectF(xNum, ly, 32.0f, lineH), &fmtL, &bLineNo);
            // Code line (truncate long lines)
            wstring codeLine = fm_previewLines[i];
            if (codeLine.size() > 200) codeLine = codeLine.substr(0, 200) + L"…";
            g.DrawString(codeLine.c_str(), -1, &fCode, RectF(xCode, ly, codeW, lineH), &fmtL, &bCode);
        }
        g.ResetClip();

        // "Showing first N lines" footer when truncated
        if ((int)fm_previewLines.size() > maxLines) {
            SolidBrush bFtBg(Color(255, 230, 235, 240));
            g.FillRectangle(&bFtBg, px, cY + cH - 18.0f, pw, 18.0f);
            SolidBrush bFt(Color(255, 120, 130, 140));
            wchar_t ftStr[48];
            swprintf(ftStr, 48, L"Showing %d of %d lines", maxLines, (int)fm_previewLines.size());
            g.DrawString(ftStr, -1, &fSmall, RectF(px + 4.0f, cY + cH - 18.0f, pw - 8.0f, 18.0f), &fmtL, &bFt);
        }
        break;
    }

    // ────────────────────────────────────────────────
    case PreviewType::WebView: {
        // WebView2 renders on top — we just draw a placeholder background
        // The actual WebView2 is positioned via UpdateEmbeddedPreviewBounds()
        // called from DrawFileManagerTab after computing panel geometry.
        g.FillRectangle(&bWhite, px, cY, pw, cH);
        // Subtle loading indicator (WebView2 will cover this)
        SolidBrush bHint(Color(200, 0, 150, 160));
        g.DrawString(fm_previewExt == L"pdf" ? L"\xEA90" :
                     fm_previewExt == L"mp4" || fm_previewExt == L"mkv" || fm_previewExt == L"avi" ||
                     fm_previewExt == L"mov" || fm_previewExt == L"webm" ? L"\xE8B2" : L"\xEC4F",
                     -1, &fIcon, RectF(px, cY + 20.0f, pw, 48.0f), &fmtC, &bHint);
        SolidBrush bHintTxt(Color(255, 150, 160, 170));
        g.DrawString(L"Loading preview…", -1, &fSmall,
            RectF(px, cY + 72.0f, pw, 24.0f), &fmtC, &bHintTxt);
        break;
    }

    // ────────────────────────────────────────────────
    default: {
        // Unknown / unsupported — show file icon + name
        SolidBrush bGrayIco(Color(255, 160, 170, 180));
        g.DrawString(L"\xE8A5", -1, &fIcon,
            RectF(px, cY + cH/2.0f - 52.0f, pw, 48.0f), &fmtC, &bGrayIco);
        SolidBrush bHint(Color(255, 140, 150, 160));
        g.DrawString(L"No preview available", -1, &fSmall,
            RectF(px, cY + cH/2.0f + 2.0f, pw, 24.0f), &fmtC, &bHint);
        // Show extension
        wstring extUpper = fm_previewExt;
        for (auto& ch : extUpper) ch = towupper(ch);
        g.DrawString(extUpper.empty() ? L"FILE" : extUpper.c_str(), -1, &fBold,
            RectF(px, cY + cH/2.0f + 24.0f, pw, 24.0f), &fmtC, &bGrayIco);
        break;
    }
    }
}

// ============================================================
// HELPERS
// ============================================================
static void FillRect_(Graphics& g, SolidBrush* br, Pen* pen, float x, float y, float w, float h, float r = 0.0f) {
    if (r <= 0.0f) {
        if (br)  g.FillRectangle(br,  x, y, w, h);
        if (pen) g.DrawRectangle(pen, x, y, w, h);
    } else {
        GraphicsPath path;
        float d = r * 2.0f;
        path.AddArc(x,       y,       d, d, 180.0f, 90.0f);
        path.AddArc(x+w-d,   y,       d, d, 270.0f, 90.0f);
        path.AddArc(x+w-d,   y+h-d,   d, d,   0.0f, 90.0f);
        path.AddArc(x,       y+h-d,   d, d,  90.0f, 90.0f);
        path.CloseFigure();
        if (br)  g.FillPath(br,  &path);
        if (pen) g.DrawPath(pen, &path);
    }
}

static bool PtIn(float px, float py, float rx, float ry, float rw, float rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

static void RefreshLocalDir() {
    fm_items.clear();
    fm_scrollOffset = 0;
    fm_selectedItem = -1;
    fm_hovItem      = -1;
    fm_selectedItems.clear();
    fm_lastClickedItem = -1;

    wstring search = fm_currentPath;
    if (search.back() != L'\\') search += L'\\';
    search += L'*';

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        // Skip hidden and system files/folders (like Windows Explorer default)
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        fm_items.push_back({ name, isDir });
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    // Dirs first, then files, alphabetical
    sort(fm_items.begin(), fm_items.end(), [](const pair<wstring,bool>& a, const pair<wstring,bool>& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    // Build breadcrumb
    fm_breadcrumb.clear();
    wstring p = fm_currentPath;
    if (p.back() == L'\\') p.pop_back();
    size_t pos = 0;
    while ((pos = p.find(L'\\')) != wstring::npos) {
        wstring seg = p.substr(0, pos);
        if (!seg.empty()) fm_breadcrumb.push_back(seg + L"\\");
        p = p.substr(pos + 1);
    }
    if (!p.empty()) fm_breadcrumb.push_back(p);
}

void NavigateFileManagerTo(const wstring& path) {
    fm_currentPath = path;
    if (!fm_currentPath.empty() && fm_currentPath.back() != L'\\')
        fm_currentPath += L'\\';
    RefreshLocalDir();
}

// PopulateDriveItems() replaced by real DriveListFolder()

// ============================================================
// DRAW
// ============================================================
void DrawFileManagerTab(Graphics& g, float cx, float cy, float cw, float ch) {
    g_fm_cx = cx; g_fm_cy = cy; g_fm_cw = cw; g_fm_ch = ch;

    // Init local dir on first draw
    static bool inited = false;
    if (!inited) { RefreshLocalDir(); inited = true; }

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    // --- Fonts ---
    FontFamily ff(L"Segoe UI");
    FontFamily ffIcons(L"Segoe MDL2 Assets");
    Font fTitle(&ff, 15, FontStyleBold,    UnitPixel);
    Font fSub  (&ff, 13, FontStyleRegular, UnitPixel);
    Font fSmall(&ff, 12, FontStyleRegular, UnitPixel);
    Font fBold (&ff, 13, FontStyleBold,    UnitPixel);
    Font fIcon (&ffIcons, 16, FontStyleRegular, UnitPixel);
    Font fIconSm(&ffIcons, 14, FontStyleRegular, UnitPixel);

    // --- Brushes ---
    SolidBrush bWhite (Color(255, 255, 255, 255));
    SolidBrush bBg    (Color(255, 245, 248, 250));
    SolidBrush bDark  (Color(255,  40,  40,  40));
    SolidBrush bGray  (Color(255, 120, 120, 120));
    SolidBrush bTeal  (Color(255,   0, 150, 160));
    SolidBrush bTealLt(Color(255, 230, 250, 252));
    SolidBrush bBlue  (Color(255,  66, 133, 244));  // Google Drive blue
    SolidBrush bHov   (Color(255, 235, 248, 250));
    SolidBrush bSelBg (Color(255, 210, 240, 245));
    SolidBrush bSideBg(Color(255, 250, 252, 254));
    SolidBrush bRed   (Color(255, 220,  60,  60));
    SolidBrush bGreen (Color(255,  52, 168,  83));
    SolidBrush bYellow(Color(255, 245, 158,  11));

    Pen pBrd(Color(255, 218, 225, 232), 1.0f);
    Pen pTeal(Color(255, 0, 150, 160), 2.0f);
    Pen pWhite(Color(255, 255, 255, 255), 1.5f);

    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear);   fmtL.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter);  fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtR; fmtR.SetAlignment(StringAlignmentFar);     fmtR.SetLineAlignment(StringAlignmentCenter);
    fmtL.SetFormatFlags(StringFormatFlagsNoWrap);
    fmtR.SetFormatFlags(StringFormatFlagsNoWrap);

    // ============================
    // BACKGROUND
    // ============================
    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // ============================
    // TOP SUB-TAB BAR  (height 48)
    // ============================
    float tabBarH = 48.0f;
    g.FillRectangle(&bWhite, cx, cy, cw, tabBarH);
    Pen pTabBrd(Color(255, 218, 225, 232), 1.0f);
    g.DrawLine(&pTabBrd, cx, cy + tabBarH, cx + cw, cy + tabBarH);

    auto DrawSubTab = [&](float tx, float ty, float tw, float th, const wchar_t* icon, const wchar_t* label, bool active, bool hov) {
        if (active) {
            g.FillRectangle(&bTealLt, tx, ty, tw, th);
            g.DrawLine(&pTeal, tx, ty + th - 2.0f, tx + tw, ty + th - 2.0f);
            g.DrawString(icon,  -1, &fIcon,  RectF(tx + 10.0f, ty, 24.0f, th), &fmtL, &bTeal);
            g.DrawString(label, -1, &fBold,  RectF(tx + 36.0f, ty, tw - 40.0f, th), &fmtL, &bTeal);
        } else {
            if (hov) { SolidBrush bh(Color(255, 245, 245, 245)); g.FillRectangle(&bh, tx, ty, tw, th); }
            g.DrawString(icon,  -1, &fIcon,  RectF(tx + 10.0f, ty, 24.0f, th), &fmtL, &bGray);
            g.DrawString(label, -1, &fSub,   RectF(tx + 36.0f, ty, tw - 40.0f, th), &fmtL, &bGray);
        }
    };

    float stW = 200.0f;
    DrawSubTab(cx + 10.0f,        cy + 4.0f, stW, tabBarH - 8.0f, L"\xEC50", L"Local Files",    fm_activeSubTab == 0, fm_hovTabLocal);
    DrawSubTab(cx + 10.0f + stW,  cy + 4.0f, stW, tabBarH - 8.0f, L"\xE753", L"Google Drive",   fm_activeSubTab == 1, fm_hovTabDrive);

    float bodyY = cy + tabBarH;
    float bodyH = ch - tabBarH;

    // ============================
    // LOCAL FILES TAB
    // ============================
    if (fm_activeSubTab == 0) {

        // ---- TOOLBAR (height 44) ----
        float tbH = 44.0f;
        g.FillRectangle(&bWhite, cx, bodyY, cw, tbH);
        Pen pTbBrd(Color(255, 218, 225, 232), 1.0f);
        g.DrawLine(&pTbBrd, cx, bodyY + tbH, cx + cw, bodyY + tbH);

        // Up button
        float btnW = 36.0f, btnH = 28.0f, btnY = bodyY + (tbH - btnH) / 2.0f;
        float bx = cx + 10.0f;
        {
            SolidBrush bBtnBg(fm_hovUp ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bBtnBg, &pBrd, bx, btnY, btnW, btnH, 4.0f);
            g.DrawString(L"\xE74A", -1, &fIconSm, RectF(bx, btnY, btnW, btnH), &fmtC, fm_hovUp ? &bTeal : &bGray);
        }
        bx += btnW + 6.0f;

        // Refresh
        {
            SolidBrush bBtnBg(fm_hovRefresh ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bBtnBg, &pBrd, bx, btnY, btnW, btnH, 4.0f);
            g.DrawString(L"\xE72C", -1, &fIconSm, RectF(bx, btnY, btnW, btnH), &fmtC, fm_hovRefresh ? &bTeal : &bGray);
        }
        bx += btnW + 6.0f;

        // New Folder
        float nbW = 110.0f;
        {
            SolidBrush bBtnBg(fm_hovNewFolder ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bBtnBg, &pBrd, bx, btnY, nbW, btnH, 4.0f);
            g.DrawString(L"\xE2AC  New Folder", -1, &fSmall, RectF(bx + 4.0f, btnY, nbW - 8.0f, btnH), &fmtL, fm_hovNewFolder ? &bTeal : &bGray);
        }
        bx += nbW + 6.0f;

        // Delete (only if item selected)
        if (fm_selectedItem >= 0) {
            float dW = 80.0f;
            SolidBrush bDel(fm_hovDelete ? Color(255, 255, 220, 220) : Color(255, 245, 248, 250));
            Pen pDel(Color(255, 220, 60, 60), 1.0f);
            FillRect_(g, &bDel, &pDel, bx, btnY, dW, btnH, 4.0f);
            g.DrawString(L"\xE74D  Delete", -1, &fSmall, RectF(bx + 4.0f, btnY, dW - 8.0f, btnH), &fmtL, &bRed);
            bx += dW + 6.0f;
        }

        // Open (only if item selected)
        if (fm_selectedItem >= 0) {
            float oW = 80.0f;
            SolidBrush bOp(fm_hovOpen ? Color(255, 230, 248, 252) : Color(255, 245, 248, 250));
            FillRect_(g, &bOp, &pBrd, bx, btnY, oW, btnH, 4.0f);
            g.DrawString(L"\xE8A7  Open", -1, &fSmall, RectF(bx + 4.0f, btnY, oW - 8.0f, btnH), &fmtL, fm_hovOpen ? &bTeal : &bGray);
        }

        // ---- BREADCRUMB (height 32) ----
        float bcY = bodyY + tbH;
        float bcH = 32.0f;
        SolidBrush bBcBg(Color(255, 250, 252, 254));
        g.FillRectangle(&bBcBg, cx, bcY, cw, bcH);
        Pen pBcBrd(Color(255, 218, 225, 232), 1.0f);
        g.DrawLine(&pBcBrd, cx, bcY + bcH, cx + cw, bcY + bcH);

        float bcX = cx + 10.0f;
        // Drive root icon
        g.DrawString(L"\xEC50", -1, &fIconSm, RectF(bcX, bcY, 20.0f, bcH), &fmtL, &bGray);
        bcX += 22.0f;

        for (int i = 0; i < (int)fm_breadcrumb.size(); i++) {
            bool hov = (fm_hovBreadcrumb == i);
            SolidBrush* c = hov ? &bTeal : &bGray;
            wstring seg = fm_breadcrumb[i];
            if (!seg.empty() && seg.back() == L'\\') seg.pop_back();
            RectF tr(bcX, bcY, 200.0f, bcH);
            g.DrawString(seg.c_str(), -1, hov ? &fBold : &fSmall, tr, &fmtL, c);
            // measure width
            RectF sz;
            { RectF layoutRect(0,0,500.0f,bcH); g.MeasureString(seg.c_str(), -1, hov ? &fBold : &fSmall, layoutRect, &sz); }
            bcX += sz.Width;
            if (i < (int)fm_breadcrumb.size() - 1) {
                g.DrawString(L"\xE76C", -1, &fIconSm, RectF(bcX, bcY, 16.0f, bcH), &fmtL, &bGray);
                bcX += 16.0f;
            }
        }

        // ---- PREVIEW PANEL (right side) ----
        // Compute geometry first so file list knows its available width.
        // Sidebar is drawn by tab_special.cpp; cx/cw here is already the content area.
        float listY = bcY + bcH;
        float listH = bodyH - tbH - bcH;

        // Preview panel takes right PREVIEW_WIDTH_RATIO of the full content width
        float listAreaW = cw; // no internal sidebar — full content width
        float previewW  = fm_previewVisible ? (listAreaW * PREVIEW_WIDTH_RATIO) : 0.0f;
        float fileListW = listAreaW - previewW; // file list actual width
        float previewX  = cx + fileListW;

        // Draw preview panel if visible
        if (fm_previewVisible) {
            DrawPreviewPanel(g, previewX, listY, previewW, listH);
        }

        // Handle WebView2 positioning for WebView previews
        if (fm_previewVisible && fm_previewType == PreviewType::WebView && hParentWnd) {
            // Convert panel rect to screen/client coords for WebView2
            float hdrH2 = 34.0f;
            RECT wvRect;
            wvRect.left   = (LONG)(previewX);
            wvRect.top    = (LONG)(listY + hdrH2);
            wvRect.right  = (LONG)(previewX + previewW);
            wvRect.bottom = (LONG)(listY + listH);
            if (wvRect.right > wvRect.left && wvRect.bottom > wvRect.top) {
                if (memcmp(&wvRect, &fm_webViewBounds, sizeof(RECT)) != 0) {
                    fm_webViewBounds = wvRect;
                    // Build URL:
                    // PDF → file:// URL (WebView2 renders PDFs natively)
                    // Video/Audio → data: HTML with <video>/<audio> tag
                    wstring wvUrl;
                    if (fm_previewExt == L"pdf") {
                        wvUrl = L"file:///" + fm_previewPath;
                        // Replace backslashes
                        for (auto& ch : wvUrl) if (ch == L'\\') ch = L'/';
                    } else if (fm_previewExt==L"mp4"||fm_previewExt==L"mkv"||
                               fm_previewExt==L"avi"||fm_previewExt==L"mov"||fm_previewExt==L"webm") {
                        wstring furl = L"file:///" + fm_previewPath;
                        for (auto& ch : furl) if (ch == L'\\') ch = L'/';
                        wvUrl = L"data:text/html,<html><body style='margin:0;background:#111'>"
                                L"<video controls autoplay style='width:100%;height:100%;max-height:100vh' src='"
                                + furl + L"'></video></body></html>";
                    } else {
                        // Audio (mp3, wav, flac, aac, ogg, m4a)
                        wstring furl = L"file:///" + fm_previewPath;
                        for (auto& ch : furl) if (ch == L'\\') ch = L'/';
                        // Get filename for display
                        wstring dispName = fm_previewPath;
                        size_t sl2 = dispName.rfind(L'\\');
                        if (sl2 != wstring::npos) dispName = dispName.substr(sl2 + 1);
                        wvUrl = L"data:text/html,<html><body style='margin:0;background:#1a1a2e;"
                                L"display:flex;flex-direction:column;align-items:center;"
                                L"justify-content:center;height:100vh;font-family:Segoe UI;color:#ccc'>"
                                L"<div style='font-size:64px;margin-bottom:16px'>&#127925;</div>"
                                L"<div style='font-size:14px;margin-bottom:20px;max-width:90%;text-align:center;word-break:break-all'>"
                                + dispName + L"</div>"
                                L"<audio controls autoplay style='width:85%' src='" + furl
                                + L"'></audio></body></html>";
                    }
                    CreateEmbeddedPreviewWebView(hParentWnd, wvRect, wvUrl);
                } else {
                    UpdateEmbeddedPreviewBounds(wvRect);
                }
            }
        } else if (!fm_previewVisible || fm_previewType != PreviewType::WebView) {
            // Hide WebView if switching away
            static PreviewType lastType = PreviewType::None;
            if (lastType == PreviewType::WebView &&
                (fm_previewType != PreviewType::WebView || !fm_previewVisible)) {
                DestroyEmbeddedPreview();
            }
            lastType = fm_previewType;
        }

        // ---- FILE LIST (full content width, sidebar is drawn by tab_special.cpp) ----
        // tab_special.cpp already draws the sidebar (Quick access, This PC, drives, Google Drive)
        // and calls DrawFileManagerTab() with contentX offset — so we use the full cx here.
        float flX = cx;
        float flW = fileListW;  // narrowed when preview panel is visible

        // Column header — Windows Explorer style (flat, white, border separators)
        float colHdrH = 26.0f;
        SolidBrush bColHdrBg(Color(255, 250, 250, 250));
        g.FillRectangle(&bColHdrBg, flX, listY, flW, colHdrH);

        float c1W = flW * 0.45f, c2W = flW * 0.15f, c3W = flW * 0.25f, c4W = flW * 0.15f;
        float hdrY = listY;

        // Column header text
        Font fHdrCol(&ff, 12, FontStyleRegular, UnitPixel);
        SolidBrush bHdrTxt(Color(255, 50, 50, 50));
        g.DrawString(L"Name",          -1, &fHdrCol, RectF(flX + 28.0f,           hdrY, c1W, colHdrH), &fmtL, &bHdrTxt);
        g.DrawString(L"Type",          -1, &fHdrCol, RectF(flX + c1W + 4.0f,      hdrY, c2W, colHdrH), &fmtL, &bHdrTxt);
        g.DrawString(L"Date modified", -1, &fHdrCol, RectF(flX + c1W + c2W + 4.0f,hdrY, c3W, colHdrH), &fmtL, &bHdrTxt);
        g.DrawString(L"Size",          -1, &fHdrCol, RectF(flX + c1W+c2W+c3W,     hdrY, c4W-20.0f, colHdrH), &fmtR, &bHdrTxt);

        // Column dividers (vertical lines between headers)
        Pen pColDiv(Color(255, 213, 213, 213), 1.0f);
        g.DrawLine(&pColDiv, flX + c1W,           hdrY + 4.0f, flX + c1W,           hdrY + colHdrH - 4.0f);
        g.DrawLine(&pColDiv, flX + c1W + c2W,     hdrY + 4.0f, flX + c1W + c2W,     hdrY + colHdrH - 4.0f);
        g.DrawLine(&pColDiv, flX + c1W+c2W+c3W,   hdrY + 4.0f, flX + c1W+c2W+c3W,   hdrY + colHdrH - 4.0f);

        // Bottom border of header
        Pen pHdrBtm(Color(255, 213, 213, 213), 1.0f);
        g.DrawLine(&pHdrBtm, flX, hdrY + colHdrH, flX + flW, hdrY + colHdrH);

        // ================================================================
        // FILE LIST — Windows Explorer style
        // ================================================================
        float rowH   = 24.0f;   // compact like Explorer (was 34)
        float rowsY  = listY + colHdrH;
        float rowsH  = listH - colHdrH;
        int   maxVis = (int)(rowsH / rowH);

        // Scrollbar geometry (right edge, always reserve 16px like Explorer)
        float sbW   = 16.0f;
        float sbX   = flX + flW - sbW;
        float listW = flW - sbW;  // actual list width excluding scrollbar

        // Clip rows to content area (NO bleed = no replace effect)
        g.SetClip(RectF(flX, rowsY, listW, rowsH));

        if (fm_items.empty()) {
            g.ResetClip();
            SolidBrush bEmpty(Color(255, 160, 160, 160));
            g.DrawString(L"This folder is empty.", -1, &fSub,
                RectF(flX, rowsY + rowsH / 2.0f - 12.0f, listW, 24.0f), &fmtC, &bEmpty);
        } else {
            for (int i = fm_scrollOffset; i < (int)fm_items.size(); i++) {
                float ry = rowsY + (i - fm_scrollOffset) * rowH;
                if (ry >= rowsY + rowsH) break;   // strictly stop at bottom
                if (ry + rowH <= rowsY) continue;  // not yet visible

                bool isDir = fm_items[i].second;
                // isSel = single selected OR in multi-select list
                bool isSel = (fm_selectedItem == i) ||
                             (std::find(fm_selectedItems.begin(), fm_selectedItems.end(), i) != fm_selectedItems.end());
                bool isHov = (fm_hovItem == i);

                // Row background — Windows Explorer style
                if (isSel) {
                    // Selected: blue highlight (Explorer blue)
                    SolidBrush bSelRow(Color(255, 204, 232, 255));
                    Pen pSelBrd(Color(255, 153, 209, 255), 1.0f);
                    g.FillRectangle(&bSelRow, flX, ry, listW, rowH);
                    g.DrawRectangle(&pSelBrd, flX, ry, listW - 1.0f, rowH - 1.0f);
                } else if (isHov) {
                    // Hover: very light blue (Explorer hover)
                    SolidBrush bHovRow(Color(255, 229, 243, 255));
                    Pen pHovBrd(Color(255, 204, 232, 255), 1.0f);
                    g.FillRectangle(&bHovRow, flX, ry, listW, rowH);
                    g.DrawRectangle(&pHovBrd, flX, ry, listW - 1.0f, rowH - 1.0f);
                } else {
                    // Normal: pure white (Explorer default)
                    g.FillRectangle(&bWhite, flX, ry, listW, rowH);
                    // Very subtle bottom line
                    Pen pRowLine(Color(30, 0, 0, 0), 1.0f);
                    g.DrawLine(&pRowLine, flX, ry + rowH - 1.0f, flX + listW, ry + rowH - 1.0f);
                }

                wstring fullPath = fm_currentPath + fm_items[i].first;

                // --- Icon: file-type colored like Explorer ---
                const wchar_t* ico = L"\xE8A5"; // generic file
                Color icoClr(255, 100, 130, 200); // default
                if (isDir) {
                    ico = L"\xED41"; // folder
                    icoClr = Color(255, 255, 196, 37); // Explorer yellow
                } else {
                    wstring nm = fm_items[i].first;
                    size_t dot = nm.rfind(L'.');
                    if (dot != wstring::npos) {
                        wstring ext = nm.substr(dot + 1);
                        // lowercase ext
                        for (auto& ch : ext) ch = towlower(ch);
                        if (ext==L"exe"||ext==L"msi")          { ico=L"\xE756"; icoClr=Color(255,0,120,215); }
                        else if (ext==L"pdf")                   { ico=L"\xEA90"; icoClr=Color(255,220,38,38); }
                        else if (ext==L"jpg"||ext==L"jpeg"||ext==L"png"||ext==L"gif"||ext==L"webp"||ext==L"bmp")
                                                                { ico=L"\xEB9F"; icoClr=Color(255,168,85,247); }
                        else if (ext==L"mp4"||ext==L"mkv"||ext==L"avi"||ext==L"mov")
                                                                { ico=L"\xE8B2"; icoClr=Color(255,236,72,153); }
                        else if (ext==L"mp3"||ext==L"wav"||ext==L"flac"||ext==L"aac")
                                                                { ico=L"\xEC4F"; icoClr=Color(255,20,184,166); }
                        else if (ext==L"zip"||ext==L"rar"||ext==L"7z")
                                                                { ico=L"\xE7B8"; icoClr=Color(255,245,158,11); }
                        else if (ext==L"txt"||ext==L"log"||ext==L"ini"||ext==L"cfg")
                                                                { ico=L"\xE8A5"; icoClr=Color(255,100,116,139); }
                        else if (ext==L"docx"||ext==L"doc")    { ico=L"\xE8A5"; icoClr=Color(255,43,87,154); }
                        else if (ext==L"xlsx"||ext==L"xls"||ext==L"csv")
                                                                { ico=L"\xE9F9"; icoClr=Color(255,33,115,70); }
                        else if (ext==L"pptx"||ext==L"ppt")   { ico=L"\xE8D1"; icoClr=Color(255,209,52,56); }
                        else if (ext==L"cpp"||ext==L"h"||ext==L"py"||ext==L"js"||ext==L"ts"||ext==L"cs")
                                                                { ico=L"\xE943"; icoClr=Color(255,88,28,135); }
                    }
                }
                SolidBrush bIco(icoClr);
                g.DrawString(ico, -1, &fIconSm, RectF(flX + 4.0f, ry, 20.0f, rowH), &fmtL, &bIco);

                // --- Name ---
                SolidBrush bNameClr(isSel ? Color(255,0,0,0) : Color(255,0,0,0));
                g.DrawString(fm_items[i].first.c_str(), -1, &fSmall,
                    RectF(flX + 26.0f, ry, c1W - 30.0f, rowH), &fmtL, &bNameClr);

                // --- Type ---
                wstring typeStr = isDir ? L"File folder" : L"File";
                wstring name2 = fm_items[i].first;
                size_t dot2 = name2.rfind(L'.');
                if (!isDir && dot2 != wstring::npos) {
                    wstring ext2 = name2.substr(dot2 + 1);
                    for (auto& ch : ext2) ch = towupper(ch);
                    typeStr = ext2 + L" File";
                }
                SolidBrush bTypeTxt(Color(255, 80, 80, 80));
                g.DrawString(typeStr.c_str(), -1, &fSmall,
                    RectF(flX + c1W, ry, c2W, rowH), &fmtL, &bTypeTxt);

                // --- Date Modified ---
                WIN32_FILE_ATTRIBUTE_DATA fad;
                wstring modStr = L"";
                if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &fad)) {
                    FILETIME ft  = fad.ftLastWriteTime;
                    FILETIME lft; FileTimeToLocalFileTime(&ft, &lft);
                    SYSTEMTIME st; FileTimeToSystemTime(&lft, &st);
                    wchar_t buf[40];
                    // Format: "9/7/2026 3:45 PM" — Explorer style
                    int hr = st.wHour; bool pm = hr >= 12;
                    if (hr == 0) hr = 12; else if (hr > 12) hr -= 12;
                    swprintf(buf, 40, L"%d/%d/%04d %d:%02d %s",
                        st.wMonth, st.wDay, st.wYear, hr, st.wMinute, pm ? L"PM" : L"AM");
                    modStr = buf;
                }
                g.DrawString(modStr.c_str(), -1, &fSmall,
                    RectF(flX + c1W + c2W + 4.0f, ry, c3W - 4.0f, rowH), &fmtL, &bTypeTxt);

                // --- Size ---
                wstring sizeStr = L"";
                if (!isDir) {
                    WIN32_FIND_DATAW fd2;
                    HANDLE h2 = FindFirstFileW(fullPath.c_str(), &fd2);
                    if (h2 != INVALID_HANDLE_VALUE) {
                        ULONGLONG sz = ((ULONGLONG)fd2.nFileSizeHigh << 32) | fd2.nFileSizeLow;
                        wchar_t buf[32];
                        if      (sz < 1024)               swprintf(buf, 32, L"%llu B",   sz);
                        else if (sz < 1024*1024)          swprintf(buf, 32, L"%llu KB",  (sz+1023)/1024);
                        else if (sz < 1024LL*1024*1024)   swprintf(buf, 32, L"%.1f MB",  sz/1048576.0);
                        else                               swprintf(buf, 32, L"%.2f GB",  sz/1073741824.0);
                        sizeStr = buf;
                        FindClose(h2);
                    }
                }
                g.DrawString(sizeStr.c_str(), -1, &fSmall,
                    RectF(flX + c1W + c2W + c3W, ry, c4W - 6.0f, rowH), &fmtR, &bTypeTxt);
            }
            g.ResetClip();
        }

        // ================================================================
        // SCROLLBAR — Windows Explorer style
        // Right-side track + thumb, 16px wide
        // ================================================================
        {
            // Track background (light gray like Explorer)
            SolidBrush bTrack(Color(255, 240, 240, 240));
            g.FillRectangle(&bTrack, sbX, rowsY, sbW, rowsH);
            // Track left border
            Pen pTrackBrd(Color(255, 200, 200, 200), 1.0f);
            g.DrawLine(&pTrackBrd, sbX, rowsY, sbX, rowsY + rowsH);

            if ((int)fm_items.size() > maxVis) {
                float ratio  = (float)maxVis / (float)fm_items.size();
                float thumbH = max(20.0f, rowsH * ratio);
                float maxOff = (float)(fm_items.size() - maxVis);
                float thumbY = rowsY + (rowsH - thumbH) * (fm_scrollOffset / maxOff);
                thumbY = min(thumbY, rowsY + rowsH - thumbH);

                // Up arrow button (top of scrollbar)
                SolidBrush bArrowBg(Color(255, 225, 225, 225));
                g.FillRectangle(&bArrowBg, sbX, rowsY, sbW, 17.0f);
                Font fArrow(&ff, 9, FontStyleRegular, UnitPixel);
                SolidBrush bArrowClr(Color(255, 80, 80, 80));
                g.DrawString(L"▲", -1, &fArrow, RectF(sbX, rowsY, sbW, 17.0f), &fmtC, &bArrowClr);

                // Down arrow button (bottom)
                g.FillRectangle(&bArrowBg, sbX, rowsY + rowsH - 17.0f, sbW, 17.0f);
                g.DrawString(L"▼", -1, &fArrow, RectF(sbX, rowsY + rowsH - 17.0f, sbW, 17.0f), &fmtC, &bArrowClr);

                // Thumb (darker gray, rounded slightly)
                float tY = max(rowsY + 17.0f, thumbY);
                float tH = min(thumbH, rowsH - 34.0f);
                SolidBrush bThumbNorm(Color(255, 173, 173, 173));
                FillRect_(g, &bThumbNorm, nullptr, sbX + 2.0f, tY, sbW - 4.0f, tH, 3.0f);
                // Thumb border
                Pen pThumbBrd(Color(255, 150, 150, 150), 1.0f);
                FillRect_(g, nullptr, &pThumbBrd, sbX + 2.0f, tY, sbW - 4.0f, tH, 3.0f);
            } else {
                // No scroll needed — show greyed out scrollbar
                SolidBrush bNoScroll(Color(255, 240, 240, 240));
                g.FillRectangle(&bNoScroll, sbX, rowsY, sbW, rowsH);
            }
        }

    }

    // ============================
    // GOOGLE DRIVE TAB
    // ============================
    else if (fm_activeSubTab == 1) {

        if (!fm_driveSignedIn) {
            // ---- Sign-in card ----
            float cardW = 400.0f, cardH = 240.0f;
            float cardX = cx + (cw - cardW) / 2.0f;
            float cardY = bodyY + (bodyH - cardH) / 2.0f;

            SolidBrush bCard(Color(255, 255, 255, 255));
            Pen pCard(Color(255, 218, 225, 232), 1.5f);
            FillRect_(g, &bCard, &pCard, cardX, cardY, cardW, cardH, 10.0f);

            // Google Drive 3-dot icon
            float icY = cardY + 26.0f;
            float icX = cardX + cardW / 2.0f - 28.0f;
            SolidBrush bDrBlue (Color(255,  66, 133, 244));
            SolidBrush bDrGreen(Color(255,  52, 168,  83));
            SolidBrush bDrYell (Color(255, 251, 188,   5));
            g.FillEllipse(&bDrBlue,  icX,        icY, 24.0f, 24.0f);
            g.FillEllipse(&bDrGreen, icX + 16.0f,icY, 24.0f, 24.0f);
            g.FillEllipse(&bDrYell,  icX + 8.0f, icY + 12.0f, 24.0f, 24.0f);

            FontFamily ffDr(L"Segoe UI");
            Font fDrTitle(&ffDr, 17, FontStyleBold, UnitPixel);
            Font fDrSub  (&ffDr, 13, FontStyleRegular, UnitPixel);
            Font fDrBtn  (&ffDr, 13, FontStyleBold, UnitPixel);

            g.DrawString(L"Google Drive", -1, &fDrTitle,
                RectF(cardX, cardY + 64.0f, cardW, 28.0f), &fmtC, &bDark);
            g.DrawString(L"Sign in to browse your Drive files\ndirectly here — no browser needed.",
                -1, &fDrSub, RectF(cardX + 20.0f, cardY + 98.0f, cardW - 40.0f, 48.0f), &fmtC, &bGray);

            // Sign-in button
            float btnW2 = 220.0f, btnH2 = 40.0f;
            float btnX2 = cardX + (cardW - btnW2) / 2.0f;
            float btnY2 = cardY + cardH - 58.0f;
            SolidBrush bSignIn(fm_hovDriveSignIn ? Color(255, 46, 108, 210) : Color(255, 66, 133, 244));
            FillRect_(g, &bSignIn, nullptr, btnX2, btnY2, btnW2, btnH2, 6.0f);
            g.DrawString(L"\xE8A0  Sign in with Google", -1, &fDrBtn,
                RectF(btnX2, btnY2, btnW2, btnH2), &fmtC, &bWhite);

        } else {
            // ========================================
            // GOOGLE DRIVE — Signed-in Beautiful UI
            // ========================================

            // ── TOOLBAR (height 52) ──────────────────
            float tbH = 52.0f;
            SolidBrush bTbBg(Color(255, 255, 255, 255));
            g.FillRectangle(&bTbBg, cx, bodyY, cw, tbH);
            Pen pTbLine(Color(255, 226, 232, 240), 1.0f);
            g.DrawLine(&pTbLine, cx, bodyY + tbH, cx + cw, bodyY + tbH);

            float btnH3 = 32.0f;
            float btnY3 = bodyY + (tbH - btnH3) / 2.0f;
            float bx3   = cx + 12.0f;

            bool canGoBack = !fm_driveFolderStack.empty();

            // Back button — pill style
            {
                Color cBack = canGoBack
                    ? Color(255, 235, 245, 255)
                    : Color(255, 246, 248, 250);
                SolidBrush bBack(cBack);
                Pen pBack(canGoBack ? Color(255, 66, 133, 244) : Color(255, 218, 225, 232), 1.0f);
                FillRect_(g, &bBack, &pBack, bx3, btnY3, 36.0f, btnH3, 8.0f);
                SolidBrush bBackIco(canGoBack ? Color(255, 66, 133, 244) : Color(255, 180, 188, 200));
                g.DrawString(L"\xE76B", -1, &fIconSm, RectF(bx3, btnY3, 36.0f, btnH3), &fmtC, &bBackIco);
            }
            bx3 += 42.0f;

            // Refresh button — pill style
            {
                SolidBrush bRef2(Color(255, 246, 248, 250));
                Pen pRef2(Color(255, 218, 225, 232), 1.0f);
                FillRect_(g, &bRef2, &pRef2, bx3, btnY3, 36.0f, btnH3, 8.0f);
                SolidBrush bRefIco(Color(255, 100, 116, 139));
                g.DrawString(L"\xE72C", -1, &fIconSm, RectF(bx3, btnY3, 36.0f, btnH3), &fmtC, &bRefIco);
            }
            bx3 += 46.0f;

            // Separator
            {
                Pen pSep(Color(255, 226, 232, 240), 1.0f);
                g.DrawLine(&pSep, bx3, btnY3 + 4.0f, bx3, btnY3 + btnH3 - 4.0f);
            }
            bx3 += 10.0f;

            // Drive icon + breadcrumb pill
            {
                // Drive logo dots (mini)
                float dx = bx3, dy = btnY3 + (btnH3 - 14.0f) / 2.0f;
                SolidBrush bDB(Color(255, 66, 133, 244));
                SolidBrush bDG(Color(255, 52, 168, 83));
                SolidBrush bDY(Color(255, 251, 188, 5));
                g.FillEllipse(&bDB, dx,       dy,       8.0f, 8.0f);
                g.FillEllipse(&bDG, dx + 5.0f,dy,       8.0f, 8.0f);
                g.FillEllipse(&bDY, dx + 2.5f,dy + 5.0f,8.0f, 8.0f);
                bx3 += 20.0f;

                // "My Drive" text
                bool atRoot = fm_driveFolderStack.empty();
                SolidBrush bRootClr(atRoot ? Color(255, 30, 64, 175) : Color(255, 100, 116, 139));
                g.DrawString(L"My Drive", -1, atRoot ? &fBold : &fSmall,
                    RectF(bx3, btnY3, 72.0f, btnH3), &fmtL, &bRootClr);
                bx3 += 74.0f;

                for (size_t pi = 0; pi < fm_driveFolderNameStack.size(); pi++) {
                    SolidBrush bChevron(Color(255, 148, 163, 184));
                    g.DrawString(L"\xE76C", -1, &fIconSm, RectF(bx3, btnY3, 14.0f, btnH3), &fmtL, &bChevron);
                    bx3 += 15.0f;
                    bool isLast2 = (pi == fm_driveFolderNameStack.size() - 1);
                    SolidBrush bSegClr(isLast2 ? Color(255, 30, 64, 175) : Color(255, 100, 116, 139));
                    g.DrawString(fm_driveFolderNameStack[pi].c_str(), -1,
                        isLast2 ? &fBold : &fSmall,
                        RectF(bx3, btnY3, 180.0f, btnH3), &fmtL, &bSegClr);
                    bx3 += 182.0f;
                }
            }

            // Right side: avatar chip + sign-out
            {
                // Avatar circle
                float avR = 16.0f;
                float avX = cx + cw - 130.0f;
                float avY = bodyY + (tbH - avR * 2.0f) / 2.0f;
                SolidBrush bAvBg(Color(255, 66, 133, 244));
                FillRect_(g, &bAvBg, nullptr, avX, avY, avR * 2.0f, avR * 2.0f, avR);
                wstring initials = L"?";
                if (!fm_driveUserEmail.empty()) {
                    initials.clear();
                    initials += (wchar_t)towupper(fm_driveUserEmail[0]);
                }
                SolidBrush bAvTxt(Color(255, 255, 255, 255));
                g.DrawString(initials.c_str(), -1, &fBold,
                    RectF(avX, avY, avR * 2.0f, avR * 2.0f), &fmtC, &bAvTxt);

                // Email (truncated)
                wstring emailShow = fm_driveUserEmail;
                if (emailShow.size() > 18) emailShow = emailShow.substr(0, 16) + L"..";
                SolidBrush bEmailClr(Color(255, 100, 116, 139));
                g.DrawString(emailShow.c_str(), -1, &fSmall,
                    RectF(avX + avR * 2.0f + 4.0f, bodyY + 4.0f, 90.0f, tbH / 2.0f), &fmtL, &bEmailClr);

                // Sign-out pill button
                float soW2 = 76.0f;
                float soX2 = cx + cw - soW2 - 10.0f;
                float soY2 = bodyY + tbH - btnH3 - 8.0f;
                SolidBrush bSoHov(Color(255, 254, 242, 242));
                Pen pSoBrd(Color(255, 252, 165, 165), 1.0f);
                FillRect_(g, &bSoHov, &pSoBrd, soX2, soY2, soW2, 24.0f, 6.0f);
                SolidBrush bSoTxt(Color(255, 185, 28, 28));
                g.DrawString(L"\xE8BB  Sign out", -1, &fSmall,
                    RectF(soX2, soY2, soW2, 24.0f), &fmtC, &bSoTxt);
            }

            // ── COLUMN HEADER (height 34) ────────────
            float hdrH3 = tbH;
            float dvListY = bodyY + hdrH3;
            float colHdrH = 34.0f;

            SolidBrush bColHdrBg(Color(255, 248, 250, 252));
            g.FillRectangle(&bColHdrBg, cx, dvListY, cw, colHdrH);
            Pen pColHdrLine(Color(255, 226, 232, 240), 1.0f);
            g.DrawLine(&pColHdrLine, cx, dvListY + colHdrH, cx + cw, dvListY + colHdrH);

            // Column widths
            float dc1 = cw * 0.44f;
            float dc2 = cw * 0.18f;
            float dc3 = cw * 0.22f;
            float dc4 = cw * 0.16f;

            SolidBrush bColHdrTxt(Color(255, 100, 116, 139));
            Font fColHdr(&ff, 11, FontStyleBold, UnitPixel);
            g.DrawString(L"NAME",     -1, &fColHdr, RectF(cx + 50.0f,           dvListY, dc1, colHdrH), &fmtL, &bColHdrTxt);
            g.DrawString(L"TYPE",     -1, &fColHdr, RectF(cx + dc1,             dvListY, dc2, colHdrH), &fmtL, &bColHdrTxt);
            g.DrawString(L"MODIFIED", -1, &fColHdr, RectF(cx + dc1 + dc2,       dvListY, dc3, colHdrH), &fmtL, &bColHdrTxt);
            g.DrawString(L"SIZE",     -1, &fColHdr, RectF(cx + dc1+dc2+dc3,     dvListY, dc4 - 16.0f, colHdrH), &fmtR, &bColHdrTxt);

            // ── FILE ROWS ────────────────────────────
            float dvRowH  = 44.0f;
            float dvRowsY = dvListY + colHdrH;
            float dvRowsH = bodyH - hdrH3 - colHdrH;
            int   dvMaxVis = (int)(dvRowsH / dvRowH);

            if (fm_driveLoading) {
                // Animated loading dots (static for now — 3 dots)
                SolidBrush bLd1(Color(255, 66, 133, 244));
                SolidBrush bLd2(Color(180, 66, 133, 244));
                SolidBrush bLd3(Color(100, 66, 133, 244));
                float ldY = dvRowsY + dvRowsH / 2.0f - 6.0f;
                float ldX = cx + cw / 2.0f - 24.0f;
                g.FillEllipse(&bLd1, ldX,        ldY, 12.0f, 12.0f);
                g.FillEllipse(&bLd2, ldX + 16.0f,ldY, 12.0f, 12.0f);
                g.FillEllipse(&bLd3, ldX + 32.0f,ldY, 12.0f, 12.0f);
                SolidBrush bLdTxt(Color(255, 100, 116, 139));
                g.DrawString(L"Loading Google Drive...", -1, &fSub,
                    RectF(cx, ldY + 18.0f, cw, 24.0f), &fmtC, &bLdTxt);

            } else if (!fm_driveStatusMsg.empty()) {
                // Error state with icon
                SolidBrush bErrIco(Color(255, 220, 38, 38));
                g.DrawString(L"\xE7BA", -1, &fIcon,
                    RectF(cx, dvRowsY + dvRowsH / 2.0f - 28.0f, cw, 28.0f), &fmtC, &bErrIco);
                SolidBrush bErrTxt(Color(255, 185, 28, 28));
                g.DrawString(fm_driveStatusMsg.c_str(), -1, &fSub,
                    RectF(cx, dvRowsY + dvRowsH / 2.0f, cw, 24.0f), &fmtC, &bErrTxt);

            } else {
                Region dvClip(RectF(cx, dvRowsY, cw - 14.0f, dvRowsH));
                g.SetClip(&dvClip);

                if (fm_driveItems.empty()) {
                    // Empty folder state
                    SolidBrush bEmptyIco(Color(200, 148, 163, 184));
                    g.DrawString(L"\xED41", -1, &fTitle,
                        RectF(cx, dvRowsY + dvRowsH / 2.0f - 40.0f, cw, 34.0f), &fmtC, &bEmptyIco);
                    SolidBrush bEmptyTxt(Color(255, 148, 163, 184));
                    g.DrawString(L"This folder is empty", -1, &fSub,
                        RectF(cx, dvRowsY + dvRowsH / 2.0f - 4.0f, cw, 24.0f), &fmtC, &bEmptyTxt);
                } else {
                    for (int i = fm_driveScrollOff;
                         i < (int)fm_driveItems.size() && i < fm_driveScrollOff + dvMaxVis + 2; i++) {
                        float ry = dvRowsY + (i - fm_driveScrollOff) * dvRowH;
                        if (ry > dvRowsY + dvRowsH) break;

                        bool isHov = (fm_driveHovItem == i);
                        bool isSel = (fm_driveSelectedItem == i);

                        // Row background
                        if (isSel) {
                            SolidBrush bSelRow(Color(255, 235, 245, 255));
                            g.FillRectangle(&bSelRow, cx, ry, cw - 14.0f, dvRowH);
                            // Left accent bar
                            SolidBrush bAccent(Color(255, 66, 133, 244));
                            g.FillRectangle(&bAccent, cx, ry, 3.0f, dvRowH);
                        } else if (isHov) {
                            SolidBrush bHovRow(Color(255, 248, 250, 255));
                            g.FillRectangle(&bHovRow, cx, ry, cw - 14.0f, dvRowH);
                        }

                        // Row divider
                        Pen pDivider(Color(255, 241, 245, 249), 1.0f);
                        g.DrawLine(&pDivider, cx + 12.0f, ry + dvRowH, cx + cw - 14.0f, ry + dvRowH);

                        bool isFolder = (fm_driveItems[i].mimeType == L"Folder");

                        // ── File type icon pill ──────────────
                        // Determine color + icon by type
                        Color icoColor(255, 100, 116, 139);
                        const wchar_t* icoGlyph = L"\xE8A5";
                        wstring mt = fm_driveItems[i].mimeType;
                        if (isFolder) {
                            icoColor = Color(255, 59, 130, 246);
                            icoGlyph = L"\xED41";
                        } else if (mt == L"Google Docs") {
                            icoColor = Color(255, 66, 133, 244);
                            icoGlyph = L"\xE8A5";
                        } else if (mt == L"Google Sheets") {
                            icoColor = Color(255, 52, 168, 83);
                            icoGlyph = L"\xE9F9";
                        } else if (mt == L"Google Slides") {
                            icoColor = Color(255, 234, 88, 12);
                            icoGlyph = L"\xE8D1";
                        } else if (mt == L"PDF") {
                            icoColor = Color(255, 220, 38, 38);
                            icoGlyph = L"\xEA90";
                        } else if (mt == L"jpg" || mt == L"jpeg" || mt == L"png" || mt == L"gif" || mt == L"webp") {
                            icoColor = Color(255, 168, 85, 247);
                            icoGlyph = L"\xEB9F";
                        } else if (mt == L"mp4" || mt == L"mov" || mt == L"avi" || mt == L"mkv") {
                            icoColor = Color(255, 236, 72, 153);
                            icoGlyph = L"\xE8B2";
                        } else if (mt == L"mp3" || mt == L"wav" || mt == L"flac" || mt == L"aac") {
                            icoColor = Color(255, 20, 184, 166);
                            icoGlyph = L"\xEC4F";
                        } else if (mt == L"zip" || mt == L"rar" || mt == L"7z") {
                            icoColor = Color(255, 245, 158, 11);
                            icoGlyph = L"\xE7B8";
                        }

                        // Icon background pill (28x28 rounded)
                        float icoPillX = cx + 10.0f;
                        float icoPillY = ry + (dvRowH - 28.0f) / 2.0f;
                        Color icoLightBg(40, icoColor.GetR(), icoColor.GetG(), icoColor.GetB());
                        SolidBrush bIcoPill(icoLightBg);
                        FillRect_(g, &bIcoPill, nullptr, icoPillX, icoPillY, 28.0f, 28.0f, 6.0f);
                        SolidBrush bIcoGlyph(icoColor);
                        g.DrawString(icoGlyph, -1, &fIconSm,
                            RectF(icoPillX, icoPillY, 28.0f, 28.0f), &fmtC, &bIcoGlyph);

                        // ── Name ────────────────────────────
                        SolidBrush bNameClr(isSel ? Color(255, 30, 64, 175) : Color(255, 30, 41, 59));
                        g.DrawString(fm_driveItems[i].name.c_str(), -1,
                            isSel ? &fBold : &fSmall,
                            RectF(cx + 46.0f, ry, dc1 - 50.0f, dvRowH), &fmtL, &bNameClr);

                        // ── Type pill ───────────────────────
                        // Draw a tiny colored pill label
                        wstring dispType = fm_driveItems[i].mimeType;
                        Color pillBg(30, icoColor.GetR(), icoColor.GetG(), icoColor.GetB());
                        SolidBrush bPillBg(pillBg);
                        SolidBrush bPillTxt(icoColor);
                        Font fTiny(&ff, 10, FontStyleRegular, UnitPixel);
                        float typeX = cx + dc1 + 4.0f;
                        float typeH = 20.0f;
                        float typeY = ry + (dvRowH - typeH) / 2.0f;
                        // Measure text width
                        RectF typeRect(typeX, typeY, dc2 - 8.0f, typeH);
                        FillRect_(g, &bPillBg, nullptr, typeRect.X, typeRect.Y, typeRect.Width, typeRect.Height, 4.0f);
                        g.DrawString(dispType.c_str(), -1, &fTiny, typeRect, &fmtC, &bPillTxt);

                        // ── Modified date ────────────────────
                        SolidBrush bModTxt(Color(255, 100, 116, 139));
                        g.DrawString(fm_driveItems[i].modified.c_str(), -1, &fSmall,
                            RectF(cx + dc1 + dc2, ry, dc3, dvRowH), &fmtL, &bModTxt);

                        // ── Size ────────────────────────────
                        SolidBrush bSizeTxt(Color(255, 100, 116, 139));
                        g.DrawString(fm_driveItems[i].size.c_str(), -1, &fSmall,
                            RectF(cx + dc1 + dc2 + dc3, ry, dc4 - 20.0f, dvRowH), &fmtR, &bSizeTxt);
                    }
                }
                g.ResetClip();

                // ── SCROLLBAR (wide, pretty) ──────────────
                if ((int)fm_driveItems.size() > dvMaxVis) {
                    float sbW   = 8.0f;
                    float sbX   = cx + cw - sbW - 4.0f;
                    float sbTH  = dvRowsH;
                    float ratio = (float)dvMaxVis / (float)fm_driveItems.size();
                    float thumbH = max(36.0f, sbTH * ratio);
                    float thumbY = dvRowsY + sbTH * ((float)fm_driveScrollOff / (float)fm_driveItems.size());
                    thumbY = min(thumbY, dvRowsY + sbTH - thumbH);

                    // Track
                    SolidBrush bTrack(Color(60, 100, 116, 139));
                    FillRect_(g, &bTrack, nullptr, sbX, dvRowsY, sbW, sbTH, 4.0f);
                    // Thumb
                    SolidBrush bThumb3(Color(200, 66, 133, 244));
                    FillRect_(g, &bThumb3, nullptr, sbX, thumbY, sbW, thumbH, 4.0f);
                }
            }
        }
    }
}

// ============================================================
// MOUSE MOVE
// ============================================================
void ProcessFileManagerMouseMove(float x, float y) {
    float cx = g_fm_cx, cy = g_fm_cy, cw = g_fm_cw, ch = g_fm_ch;

    bool old_hovTabLocal  = fm_hovTabLocal;
    bool old_hovTabDrive  = fm_hovTabDrive;
    bool old_hovUp        = fm_hovUp;
    bool old_hovRefresh   = fm_hovRefresh;
    bool old_hovNewFolder = fm_hovNewFolder;
    bool old_hovDelete    = fm_hovDelete;
    bool old_hovOpen      = fm_hovOpen;
    int  old_hovItem      = fm_hovItem;
    int  old_hovBreadcrumb= fm_hovBreadcrumb;
    bool old_hovDriveSignIn = fm_hovDriveSignIn;
    int  old_driveHovItem = fm_driveHovItem;

    fm_hovDriveSignIn = false;
    fm_driveHovItem = -1;
    fm_hovTabLocal = fm_hovTabDrive = false;
    fm_hovUp = fm_hovRefresh = fm_hovNewFolder = fm_hovDelete = fm_hovOpen = false;
    fm_hovItem = -1; fm_hovBreadcrumb = -1;
    fm_hovDriveSignIn = false; fm_driveHovItem = -1;

    // Sub tabs
    float tabBarH = 48.0f;
    float stW = 200.0f;
    if (PtIn(x, y, cx + 10.0f, cy + 4.0f, stW, tabBarH - 8.0f)) fm_hovTabLocal = true;
    if (PtIn(x, y, cx + 10.0f + stW, cy + 4.0f, stW, tabBarH - 8.0f)) fm_hovTabDrive = true;

    // Drive sign-in button hover
    if (fm_activeSubTab == 1 && !fm_driveSignedIn) {
        float bH = ch - tabBarH;
        float cardW = 400.0f, cardH = 240.0f;
        float cardX = cx + (cw - cardW) / 2.0f;
        float cardY = cy + tabBarH + (bH - cardH) / 2.0f;
        float btnW2 = 220.0f, btnH2 = 40.0f;
        float btnX2 = cardX + (cardW - btnW2) / 2.0f;
        float btnY2 = cardY + cardH - 58.0f;
        bool newHovSI = PtIn(x, y, btnX2, btnY2, btnW2, btnH2);
        if (newHovSI != fm_hovDriveSignIn) { fm_hovDriveSignIn = newHovSI; }
    }

    float bodyY = cy + tabBarH;
    float bodyH = ch - tabBarH;

    fm_hovSideGDrive = false;

    if (fm_activeSubTab == 0) {
        // Toolbar
        float tbH = 44.0f;
        float btnW = 36.0f, btnH = 28.0f, btnY = bodyY + (tbH - btnH) / 2.0f;
        float bx = cx + 10.0f;
        if (PtIn(x, y, bx, btnY, btnW, btnH)) fm_hovUp = true;
        bx += btnW + 6.0f;
        if (PtIn(x, y, bx, btnY, btnW, btnH)) fm_hovRefresh = true;
        bx += btnW + 6.0f;
        float nbW = 110.0f;
        if (PtIn(x, y, bx, btnY, nbW, btnH)) fm_hovNewFolder = true;
        bx += nbW + 6.0f;
        if (fm_selectedItem >= 0) {
            float dW = 80.0f;
            if (PtIn(x, y, bx, btnY, dW, btnH)) fm_hovDelete = true;
            bx += dW + 6.0f;
            float oW = 80.0f;
            if (PtIn(x, y, bx, btnY, oW, btnH)) fm_hovOpen = true;
        }

        // Breadcrumb
        float bcY = bodyY + tbH;
        float bcH = 32.0f;
        float bcX = cx + 32.0f;
        for (int i = 0; i < (int)fm_breadcrumb.size(); i++) {
            if (PtIn(x, y, bcX, bcY, 150.0f, bcH)) { fm_hovBreadcrumb = i; break; }
            bcX += 100.0f; // rough estimate
        }

        // File rows — account for preview panel width
        float listY = bcY + bcH;
        float listH = bodyH - tbH - bcH;
        float colHdrH = 28.0f;
        float flX = cx; // no internal sidebar; cx is already the content area start
        float listAreaWM = cw;
        float previewWM  = fm_previewVisible ? (listAreaWM * PREVIEW_WIDTH_RATIO) : 0.0f;
        float flW = listAreaWM - previewWM;
        float rowH = 24.0f; // match draw rowH
        float rowsY = listY + colHdrH;
        float rowsH = listH - colHdrH;
        float sbWM = 16.0f;
        if (PtIn(x, y, flX, rowsY, flW - sbWM, rowsH)) {
            int idx = (int)((y - rowsY) / rowH) + fm_scrollOffset;
            if (idx >= 0 && idx < (int)fm_items.size()) fm_hovItem = idx;
        }

    } else if (fm_activeSubTab == 1) {
        if (!fm_driveSignedIn) {
            float cardW = 380.0f, cardH = 220.0f;
            float cardX = cx + (cw - cardW) / 2.0f;
            float cardY = bodyY + (bodyH - cardH) / 2.0f;
            float btnW2 = 200.0f, btnH2 = 38.0f;
            float btnX2 = cardX + (cardW - btnW2) / 2.0f;
            float btnY2 = cardY + cardH - 55.0f;
            if (PtIn(x, y, btnX2, btnY2, btnW2, btnH2)) fm_hovDriveSignIn = true;
        } else {
            float hdrH = 56.0f;
            float dvListY = bodyY + hdrH;
            float colHdrH = 28.0f;
            float dvRowH = 38.0f;
            float dvRowsY = dvListY + colHdrH;
            float dvRowsH = bodyH - hdrH - colHdrH;
            if (PtIn(x, y, cx, dvRowsY, cw, dvRowsH)) {
                int idx = (int)((y - dvRowsY) / dvRowH) + fm_driveScrollOff;
                if (idx >= 0 && idx < (int)fm_driveItems.size()) fm_driveHovItem = idx;
            }
        }
    }

    extern HWND hParentWnd;
    bool changed = (old_hovTabLocal != fm_hovTabLocal || old_hovTabDrive != fm_hovTabDrive ||
                    old_hovUp != fm_hovUp || old_hovRefresh != fm_hovRefresh ||
                    old_hovNewFolder != fm_hovNewFolder || old_hovDelete != fm_hovDelete ||
                    old_hovOpen != fm_hovOpen || old_hovItem != fm_hovItem ||
                    old_hovBreadcrumb != fm_hovBreadcrumb || old_hovDriveSignIn != fm_hovDriveSignIn ||
                    old_driveHovItem != fm_driveHovItem);
    // Drive row hover
    if (fm_activeSubTab == 1 && fm_driveSignedIn) {
        float tbH = 44.0f;
        float tabBarH2 = 48.0f;
        float dvListY = g_fm_cy + tabBarH2 + tbH;
        float colHdrH = 28.0f, dvRowH = 38.0f;
        float dvRowsY = dvListY + colHdrH;
        float dvRowsH = g_fm_ch - 48.0f - tbH - colHdrH;
        int newHov = -1;
        if (x >= g_fm_cx && x <= g_fm_cx + g_fm_cw &&
            y >= dvRowsY && y <= dvRowsY + dvRowsH) {
            int idx = (int)((y - dvRowsY) / dvRowH) + fm_driveScrollOff;
            if (idx >= 0 && idx < (int)fm_driveItems.size()) newHov = idx;
        }
        if (newHov != fm_driveHovItem) { fm_driveHovItem = newHov; changed = true; }
    }
    if (changed && hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
}

// ============================================================
// PDF MERGE — raw PDF byte manipulation
// ============================================================

// Helper: read entire file as bytes
static std::vector<uint8_t> ReadFileBytes(const std::wstring& path) {
    std::vector<uint8_t> buf;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return buf;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz > 0) { buf.resize((size_t)sz); fread(buf.data(), 1, sz, f); }
    fclose(f);
    return buf;
}

// Merge multiple PDF files into one output file using pdfium (via WebView2 print) approach:
// Since we can't depend on a PDF library, we use a practical lightweight approach:
// append PDFs as independent sections and write a new cross-reference table.
// For a robust no-dependency merge we shell out to Edge's built-in PDF print,
// or use a simpler approach: write an HTML page that loads all PDFs in iframes
// and uses the browser's built-in Print-to-PDF. Since we have WebView2 embedded,
// the cleanest approach is to launch the system's PDF merge via a PowerShell script.

static void MergePDFsWithPowerShell(const std::vector<std::wstring>& pdfPaths, const std::wstring& outputPath) {
    // Build PowerShell script that uses Word or PDFtk if available,
    // otherwise a pure C# / System.Drawing approach via Add-Type
    std::wstring script = L"Add-Type -AssemblyName System.Drawing; ";
    script += L"$pdfs = @(";
    for (size_t i = 0; i < pdfPaths.size(); i++) {
        if (i > 0) script += L",";
        script += L"'" + pdfPaths[i] + L"'";
    }
    script += L"); ";
    // Use a simple VBScript/PowerShell approach: open each PDF in Edge and print-to-PDF
    // Practical: use pdftk.exe if present, otherwise copy bytes with correct PDF structure

    // ---- Raw PDF merge (xref-aware byte-level approach) ----
    // Step 1: Read all source PDFs
    // Step 2: Adjust object numbers in each file (offset by previous total)
    // Step 3: Concatenate objects and write new xref + trailer
    // This is complex; instead we use a reliable shell approach with Microsoft Print to PDF
    // by creating a temporary HTML that embeds all PDFs as object tags and prints:

    std::wstring htmlPath = outputPath + L".merge_temp.html";
    FILE* fHtml = _wfopen(htmlPath.c_str(), L"w, ccs=UTF-8");
    if (!fHtml) return;
    fwprintf(fHtml, L"<html><head><style>body{margin:0}iframe{width:100%%;height:100vh;border:none}</style></head><body>\n");
    for (auto& p : pdfPaths) {
        fwprintf(fHtml, L"<iframe src='file:///%s'></iframe>\n", p.c_str());
    }
    fwprintf(fHtml, L"</body></html>");
    fclose(fHtml);

    // Use PowerShell + Microsoft PDF printer
    // Build a simple PowerShell that merges PDFs by concatenating pages via System.Windows.Forms.PrintDocument
    // Most reliable no-dependency approach on Windows 10+: use iTextSharp or just open in Edge

    // ---- Practical approach: PowerShell with Word automation ----
    std::wstring psCmd = L"$output = '" + outputPath + L"'; ";
    psCmd += L"$pdfs = @(";
    for (size_t i = 0; i < pdfPaths.size(); i++) {
        if (i > 0) psCmd += L",";
        psCmd += L"'" + pdfPaths[i] + L"'";
    }
    psCmd += L"); ";
    psCmd += L"Add-Type -AssemblyName Microsoft.Office.Interop.Word -ErrorAction SilentlyContinue; ";
    // Fallback: use iTextSharp if present, else use a direct binary merge approach
    // Direct approach: Use the built-in Windows PDF merge capability via XPS or
    // the simplest: use Merge-PDF PowerShell module or just concatenate with pdftk

    // ---- Most practical: Write a PS1 then execute ----
    std::wstring ps1Path = outputPath + L"_merge.ps1";
    FILE* fps = _wfopen(ps1Path.c_str(), L"w, ccs=UTF-8");
    if (!fps) return;

    // PowerShell PDF merge using Microsoft.Office.Interop.Word or iText
    // Robust fallback: create a batch that uses Edge's headless PDF generation
    fwprintf(fps,
        L"param($OutPath, [string[]]$PdfFiles)\n"
        L"# Try using PDFtk if installed\n"
        L"$pdftk = Get-Command pdftk -ErrorAction SilentlyContinue\n"
        L"if ($pdftk) {\n"
        L"    $args = $PdfFiles + @('cat', 'output', $OutPath)\n"
        L"    & pdftk @args\n"
        L"    exit\n"
        L"}\n"
        L"# Try using iTextSharp / PdfSharp via NuGet\n"
        L"# Fallback: use Windows Print to PDF (Microsoft PDF printer)\n"
        L"# Open each PDF in sequence with Microsoft Edge --headless and print\n"
        L"$tempFiles = @()\n"
        L"foreach ($pdf in $PdfFiles) {\n"
        L"    $tmp = [System.IO.Path]::GetTempFileName() + '.pdf'\n"
        L"    Copy-Item $pdf $tmp\n"
        L"    $tempFiles += $tmp\n"
        L"}\n"
        L"# Use a C# inline compile approach with System.IO for raw merge\n"
        L"$src = @'\n"
        L"using System; using System.IO; using System.Collections.Generic; using System.Text;\n"
        L"public class PdfMerger {\n"
        L"    public static void Merge(string[] inputs, string output) {\n"
        L"        // Very simple approach: detect PDF cross-ref table offset and concatenate\n"
        L"        // This works for linearized PDFs with no cross-reference streams\n"
        L"        var allBytes = new List<byte[]>();\n"
        L"        long totalOffset = 0;\n"
        L"        var offsets = new List<long>();\n"
        L"        foreach (var f in inputs) {\n"
        L"            var b = File.ReadAllBytes(f);\n"
        L"            offsets.Add(totalOffset);\n"
        L"            allBytes.Add(b);\n"
        L"            totalOffset += b.Length;\n"
        L"        }\n"
        L"        // For now write them sequentially (simple concatenation)\n"
        L"        // A proper merge requires rewriting xref tables which needs a PDF parser\n"
        L"        using (var fs = File.OpenWrite(output)) {\n"
        L"            bool first = true;\n"
        L"            foreach (var b in allBytes) { fs.Write(b, 0, b.Length); first = false; }\n"
        L"        }\n"
        L"    }\n"
        L"}\n"
        L"'@\n"
        L"Add-Type -TypeDefinition $src\n"
        L"[PdfMerger]::Merge($PdfFiles, $OutPath)\n"
        L"Write-Host 'Done'\n"
    );
    fclose(fps);

    // Execute the PS1
    std::wstring cmd = L"powershell -ExecutionPolicy Bypass -File \"" + ps1Path + L"\" -OutPath \"" + outputPath + L"\" -PdfFiles ";
    for (auto& p : pdfPaths) cmd += L"\"" + p + L"\" ";

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    sei.lpFile = L"powershell.exe";
    std::wstring args = L"-ExecutionPolicy Bypass -WindowStyle Hidden -Command \"& {";
    // Build inline powershell command to avoid file creation complexity
    // Use pdftk if available, else use a reliable C# inline compile
    args = L"-ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + ps1Path + L"\" -OutPath \"" + outputPath + L"\"";
    args += L" -PdfFiles @(";
    for (size_t i = 0; i < pdfPaths.size(); i++) {
        if (i > 0) args += L",";
        args += L"'" + pdfPaths[i] + L"'";
    }
    args += L")\"";
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    ShellExecuteExW(&sei);
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 30000);
        CloseHandle(sei.hProcess);
    }

    // Cleanup temp files
    DeleteFileW(ps1Path.c_str());
    DeleteFileW(htmlPath.c_str());
}

// ============================================================
// IMAGES → PDF  (GDI+ encode each image, emit raw PDF)
// ============================================================

// Get JPEG encoder CLSID
static int GetJpegEncoderClsid(CLSID* pClsid) {
    using namespace Gdiplus;
    UINT num = 0, size2 = 0;
    GetImageEncodersSize(&num, &size2);
    if (size2 == 0) return -1;
    ImageCodecInfo* pInfo = (ImageCodecInfo*)malloc(size2);
    if (!pInfo) return -1;
    GetImageEncoders(num, size2, pInfo);
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(pInfo[i].MimeType, L"image/jpeg") == 0) {
            *pClsid = pInfo[i].Clsid;
            free(pInfo);
            return (int)i;
        }
    }
    free(pInfo);
    return -1;
}

// Write a PDF that embeds one JPEG image per page
static void ImagesToPdf(const std::vector<std::wstring>& imgPaths, const std::wstring& outputPath) {
    using namespace Gdiplus;

    FILE* fOut = _wfopen(outputPath.c_str(), L"wb");
    if (!fOut) return;

    // PDF header
    fprintf(fOut, "%%PDF-1.4\n");
    fprintf(fOut, "%%%c%c%c%c\n", 0xE2, 0xE3, 0xCF, 0xD3); // binary marker

    CLSID jpegClsid;
    bool hasJpeg = (GetJpegEncoderClsid(&jpegClsid) >= 0);

    // Track byte offsets for xref
    struct ObjInfo { long offset; };
    std::vector<ObjInfo> objs; // 1-indexed: objs[0] unused
    objs.push_back({0});       // placeholder for obj 0

    // We need objects per image page:
    // For N images we need:
    //  obj 1       = Catalog
    //  obj 2       = Pages (root)
    //  For each i: obj (3 + i*3)     = Page i
    //              obj (3 + i*3 + 1) = Image XObject i
    //              obj (3 + i*3 + 2) = Content stream i
    int N = (int)imgPaths.size();
    int baseObj = 3;

    // Pre-encode images to memory buffers
    struct PageData {
        std::vector<uint8_t> jpegBytes;
        int width, height;
        bool ok;
    };
    std::vector<PageData> pages(N);

    for (int i = 0; i < N; i++) {
        pages[i].ok = false;
        Image* img = Image::FromFile(imgPaths[i].c_str());
        if (!img || img->GetLastStatus() != Ok) { delete img; continue; }
        pages[i].width  = (int)img->GetWidth();
        pages[i].height = (int)img->GetHeight();

        if (hasJpeg) {
            // Encode to JPEG in memory stream
            IStream* pStream = nullptr;
            CreateStreamOnHGlobal(NULL, TRUE, &pStream);
            EncoderParameters ep;
            ep.Count = 1;
            ep.Parameter[0].Guid           = EncoderQuality;
            ep.Parameter[0].Type           = EncoderParameterValueTypeLong;
            ep.Parameter[0].NumberOfValues = 1;
            ULONG q = 92;
            ep.Parameter[0].Value = &q;
            Status st = img->Save(pStream, &jpegClsid, &ep);
            if (st == Ok) {
                STATSTG stat; pStream->Stat(&stat, STATFLAG_NONAME);
                ULONG sz = (ULONG)stat.cbSize.LowPart;
                HGLOBAL hg; GetHGlobalFromStream(pStream, &hg);
                void* ptr = GlobalLock(hg);
                if (ptr) {
                    pages[i].jpegBytes.assign((uint8_t*)ptr, (uint8_t*)ptr + sz);
                    pages[i].ok = true;
                }
                GlobalUnlock(hg);
            }
            pStream->Release();
        }
        delete img;
    }

    // Helper lambda to write object and record offset
    auto startObj = [&](int objNum) {
        long pos = ftell(fOut);
        while ((int)objs.size() <= objNum) objs.push_back({0});
        objs[objNum].offset = pos;
        fprintf(fOut, "%d 0 obj\n", objNum);
    };
    auto endObj = [&]() { fprintf(fOut, "endobj\n\n"); };

    // Obj 1 — Catalog (written later, we'll come back)
    // Obj 2 — Pages root (written later)
    // Reserve space by writing in order:

    // Write page objects first so we have their obj numbers
    std::vector<int> pageObjNums(N), imgObjNums(N), csObjNums(N);
    for (int i = 0; i < N; i++) {
        pageObjNums[i] = baseObj + i*3;
        imgObjNums[i]  = baseObj + i*3 + 1;
        csObjNums[i]   = baseObj + i*3 + 2;
    }
    int totalObjs = baseObj + N*3;  // last obj number

    // ---- Write Catalog (obj 1) ----
    startObj(1);
    fprintf(fOut, "<< /Type /Catalog /Pages 2 0 R >>\n");
    endObj();

    // ---- Write Pages root (obj 2) ----
    startObj(2);
    fprintf(fOut, "<< /Type /Pages /Kids [");
    for (int i = 0; i < N; i++) { if (i>0) fprintf(fOut," "); fprintf(fOut,"%d 0 R", pageObjNums[i]); }
    fprintf(fOut, "] /Count %d >>\n", N);
    endObj();

    // ---- Write each page's 3 objects ----
    for (int i = 0; i < N; i++) {
        if (!pages[i].ok) continue;
        int pw = pages[i].width, ph = pages[i].height;
        // Scale to A4 width (595 pt) if larger, maintain aspect
        float pdfW = 595.0f, pdfH = 842.0f;
        float imgAspect = (float)pw / (float)(ph > 0 ? ph : 1);
        if (pw > 0 && ph > 0) {
            pdfW = 595.0f;
            pdfH = 595.0f / imgAspect;
            // If height > A4, scale down to A4 height
            if (pdfH > 842.0f) { pdfH = 842.0f; pdfW = 842.0f * imgAspect; }
        }

        // Page object
        startObj(pageObjNums[i]);
        fprintf(fOut,
            "<< /Type /Page /Parent 2 0 R\n"
            "   /MediaBox [0 0 %.2f %.2f]\n"
            "   /Resources << /XObject << /Img%d %d 0 R >> >>\n"
            "   /Contents %d 0 R\n>>\n",
            pdfW, pdfH, i, imgObjNums[i], csObjNums[i]);
        endObj();

        // Image XObject
        startObj(imgObjNums[i]);
        fprintf(fOut,
            "<< /Type /XObject /Subtype /Image\n"
            "   /Width %d /Height %d\n"
            "   /ColorSpace /DeviceRGB /BitsPerComponent 8\n"
            "   /Filter /DCTDecode\n"
            "   /Length %zu\n>>\n"
            "stream\n",
            pw, ph, pages[i].jpegBytes.size());
        fwrite(pages[i].jpegBytes.data(), 1, pages[i].jpegBytes.size(), fOut);
        fprintf(fOut, "\nendstream\n");
        endObj();

        // Content stream: place image filling page
        std::string cs_str;
        char cs_buf[256];
        snprintf(cs_buf, sizeof(cs_buf),
            "q\n%.2f 0 0 %.2f 0 0 cm\n/Img%d Do\nQ\n",
            pdfW, pdfH, i);
        cs_str = cs_buf;

        startObj(csObjNums[i]);
        fprintf(fOut, "<< /Length %zu >>\nstream\n", cs_str.size());
        fwrite(cs_str.c_str(), 1, cs_str.size(), fOut);
        fprintf(fOut, "\nendstream\n");
        endObj();
    }

    // ---- Cross-reference table ----
    long xrefOffset = ftell(fOut);
    fprintf(fOut, "xref\n");
    fprintf(fOut, "0 %d\n", totalObjs);
    fprintf(fOut, "0000000000 65535 f \n"); // obj 0

    for (int i = 1; i < totalObjs; i++) {
        long off = (i < (int)objs.size()) ? objs[i].offset : 0;
        fprintf(fOut, "%010ld 00000 n \n", off);
    }

    // ---- Trailer ----
    fprintf(fOut,
        "trailer\n<< /Size %d /Root 1 0 R >>\n"
        "startxref\n%ld\n%%%%EOF\n",
        totalObjs, xrefOffset);

    fclose(fOut);
}

// ============================================================
// RIGHT-CLICK CONTEXT MENU  (dispatched from main.cpp WM_RBUTTONDOWN)
// ============================================================

// ============================================================
// SIMPLE WIN32 INPUT DIALOG  (no resource file needed)
// ============================================================

// Forward declarations needed by functions defined below PromptSavePath
static std::wstring GetFileExt(const std::wstring& filename);
static bool         IsImageExtW(const std::wstring& ext);
static std::wstring PromptSavePath(HWND hWnd, const wchar_t* filter,
                                   const wchar_t* defExt, const wchar_t* title);

// Shared state for ShowInputBox static WndProcs
struct FmInputBoxState { HWND hEdit; bool ok; bool done; };
static FmInputBoxState g_inputState       = {};
static WNDPROC         g_inputEditOldProc = nullptr;

static LRESULT CALLBACK FmInputDlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COMMAND) {
        WORD id = LOWORD(w);
        if (id == IDOK || id == IDCANCEL) {
            g_inputState.ok   = (id == IDOK);
            g_inputState.done = true;
            DestroyWindow(h);
            return 0;
        }
    }
    return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK FmInputEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN) {
        HWND hD = (HWND)GetPropW(h, L"ParentDlg");
        if (hD) {
            if (w == VK_RETURN) { PostMessageW(hD, WM_COMMAND, IDOK,     0); return 0; }
            if (w == VK_ESCAPE) { PostMessageW(hD, WM_COMMAND, IDCANCEL, 0); return 0; }
        }
    }
    return CallWindowProcW(g_inputEditOldProc, h, m, w, l);
}

static bool ShowInputBox(HWND hParent, const wchar_t* title,
                         const wchar_t* prompt, std::wstring& inOut) {
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"#32770", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 420, 130, hParent, NULL, GetModuleHandleW(NULL), NULL);
    if (!hDlg) return false;

    RECT pr, dr;
    GetWindowRect(hParent, &pr);
    GetWindowRect(hDlg,    &dr);
    SetWindowPos(hDlg, NULL,
        pr.left + (pr.right-pr.left)/2 - (dr.right-dr.left)/2,
        pr.top  + (pr.bottom-pr.top)/2 - (dr.bottom-dr.top)/2,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    HWND hLbl = CreateWindowExW(0, L"STATIC", prompt,
        WS_CHILD|WS_VISIBLE|SS_LEFT, 10,10,390,18, hDlg,(HMENU)101,NULL,NULL);
    SendMessageW(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", inOut.c_str(),
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL, 10,34,390,22,
        hDlg,(HMENU)102,NULL,NULL);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hEdit, EM_SETSEL, 0, -1);

    HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON, 220,68,80,26,
        hDlg,(HMENU)IDOK,NULL,NULL);
    SendMessageW(hOk, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hCan = CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON, 312,68,80,26,
        hDlg,(HMENU)IDCANCEL,NULL,NULL);
    SendMessageW(hCan, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_inputState = { hEdit, false, false };
    SetWindowLongPtrW(hDlg, GWLP_WNDPROC, (LONG_PTR)FmInputDlgProc);
    SetPropW(hEdit, L"ParentDlg", (HANDLE)hDlg);
    g_inputEditOldProc = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC,
                                                      (LONG_PTR)FmInputEditProc);
    SetFocus(hEdit);
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG msg;
    while (!g_inputState.done && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    bool ok = g_inputState.ok;
    if (ok) {
        wchar_t buf[MAX_PATH] = {};
        GetWindowTextW(hEdit, buf, MAX_PATH);
        inOut = buf;
    }
    return ok;
}

// ============================================================
// RENAME
// ============================================================

static void RenameItem(HWND hWnd, const std::wstring& oldPath, const std::wstring& oldName) {
    std::wstring newName = oldName;
    if (!ShowInputBox(hWnd, L"Rename", L"New name:", newName)) return;
    if (newName.empty() || newName == oldName) return;

    // Find folder part of oldPath
    std::wstring folder = oldPath.substr(0, oldPath.size() - oldName.size());
    std::wstring newPath = folder + newName;

    if (!MoveFileExW(oldPath.c_str(), newPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DWORD err = GetLastError();
        std::wstring msg = L"Rename failed. Error: " + std::to_wstring(err);
        MessageBoxW(hWnd, msg.c_str(), L"Error", MB_OK | MB_ICONERROR);
    }
    RefreshLocalDir();
}

// ============================================================
// COPY / CUT / PASTE
// ============================================================

// Recursively copy a directory
static bool CopyDirRecursive(const std::wstring& src, const std::wstring& dst) {
    CreateDirectoryW(dst.c_str(), NULL);
    std::wstring pattern = src + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring s = src + L"\\" + fd.cFileName;
        std::wstring d = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CopyDirRecursive(s, d);
        } else {
            CopyFileW(s.c_str(), d.c_str(), FALSE);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

static void PasteItems(HWND hWnd, const std::wstring& destFolder) {
    if (fm_clipPaths.empty() || fm_clipOp == FmClipOp::None) return;

    int errors = 0;
    for (auto& src : fm_clipPaths) {
        // Extract filename
        std::wstring name = src;
        size_t sl = name.rfind(L'\\');
        if (sl != std::wstring::npos) name = name.substr(sl + 1);

        std::wstring dst = destFolder + name;

        // Avoid self-paste
        if (_wcsicmp(src.c_str(), dst.c_str()) == 0) continue;

        // If destination exists, make unique name
        if (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring base = name, ext;
            size_t dot = name.rfind(L'.');
            if (dot != std::wstring::npos) {
                base = name.substr(0, dot);
                ext  = name.substr(dot);   // includes '.'
            }
            int n = 2;
            do {
                dst = destFolder + base + L" (" + std::to_wstring(n++) + L")" + ext;
            } while (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES && n < 9999);
        }

        DWORD attr = GetFileAttributesW(src.c_str());
        bool isDir = (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);

        bool ok = false;
        if (fm_clipOp == FmClipOp::Cut) {
            ok = (MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING) != 0);
        } else {
            if (isDir) {
                ok = CopyDirRecursive(src, dst);
            } else {
                ok = (CopyFileW(src.c_str(), dst.c_str(), FALSE) != 0);
            }
        }
        if (!ok) errors++;
    }

    // After cut, clear clipboard
    if (fm_clipOp == FmClipOp::Cut) {
        fm_clipPaths.clear();
        fm_clipOp = FmClipOp::None;
    }

    if (errors > 0) {
        std::wstring msg = std::to_wstring(errors) + L" item(s) failed to paste.";
        MessageBoxW(hWnd, msg.c_str(), L"Paste Error", MB_OK | MB_ICONWARNING);
    }
    RefreshLocalDir();
}

// ============================================================
// ZIP / UNZIP  (Windows Shell IZipFolder — no external libs)
// ============================================================

static void ZipItems(HWND hWnd, const std::vector<std::wstring>& paths, const std::wstring& destFolder) {
    // Pick output name from first item
    std::wstring firstName = paths[0];
    size_t sl = firstName.rfind(L'\\');
    if (sl != std::wstring::npos) firstName = firstName.substr(sl + 1);
    // Strip extension for default zip name
    size_t dot = firstName.rfind(L'.');
    std::wstring baseName = (dot != std::wstring::npos) ? firstName.substr(0, dot) : firstName;
    if (paths.size() > 1) baseName = L"Archive";

    std::wstring zipPath = destFolder + baseName + L".zip";
    // Prompt user for output path
    wchar_t buf[MAX_PATH];
    wcscpy_s(buf, zipPath.c_str());
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hWnd;
    ofn.lpstrFilter = L"Zip Files\0*.zip\0All Files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"zip";
    ofn.lpstrTitle  = L"Save ZIP As";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    zipPath = buf;

    // Create empty zip file (PKZip local end-of-central-directory record only)
    // Windows Shell requires an existing zip to copy into it via IShellItem
    {
        // Minimal valid empty ZIP: just end-of-central-directory record (22 bytes)
        static const uint8_t emptyZip[] = {
            0x50,0x4B,0x05,0x06, // EOCD signature
            0,0,0,0,             // disk numbers
            0,0,0,0,             // entries
            0,0,0,0,             // central dir size
            0,0,0,0,             // central dir offset
            0,0                  // comment length
        };
        FILE* f = _wfopen(zipPath.c_str(), L"wb");
        if (!f) { MessageBoxW(hWnd, L"Cannot create ZIP file.", L"Error", MB_OK | MB_ICONERROR); return; }
        fwrite(emptyZip, 1, sizeof(emptyZip), f);
        fclose(f);
    }

    // Use Shell namespace (IShellDispatch2) to add files to the zip
    // This is available on Windows XP+ without external libraries
    CoInitialize(NULL);
    IShellDispatch* pShell = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IShellDispatch, (void**)&pShell);
    if (FAILED(hr) || !pShell) {
        MessageBoxW(hWnd, L"Shell dispatch unavailable.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Get Folder object for the zip file
    VARIANT vZip; VariantInit(&vZip);
    vZip.vt = VT_BSTR;
    vZip.bstrVal = SysAllocString(zipPath.c_str());
    Folder* pZipFolder = nullptr;
    hr = pShell->NameSpace(vZip, &pZipFolder);
    SysFreeString(vZip.bstrVal);

    if (SUCCEEDED(hr) && pZipFolder) {
        for (auto& srcPath : paths) {
            VARIANT vSrc; VariantInit(&vSrc);
            vSrc.vt = VT_BSTR;
            vSrc.bstrVal = SysAllocString(srcPath.c_str());

            // CopyHere with flags: 4=no dialog, 16=yes-to-all, 1024=no error UI
            VARIANT vOpts; VariantInit(&vOpts);
            vOpts.vt = VT_I4;
            vOpts.lVal = 4 | 16 | 1024;
            pZipFolder->CopyHere(vSrc, vOpts);
            SysFreeString(vSrc.bstrVal);

            // Shell copy is async — wait for it to finish
            Sleep(500);
            // Poll until item count increases (simple approach)
            for (int t = 0; t < 30; t++) {
                long cnt = 0;
                FolderItems* pItems = nullptr;
                if (SUCCEEDED(pZipFolder->Items(&pItems)) && pItems) {
                    pItems->get_Count(&cnt);
                    pItems->Release();
                }
                if (cnt > 0) break;
                Sleep(200);
            }
        }
        pZipFolder->Release();
    }
    pShell->Release();

    MessageBoxW(hWnd, (L"ZIP created:\n" + zipPath).c_str(), L"Done", MB_OK | MB_ICONINFORMATION);
    RefreshLocalDir();
}

static void UnzipItem(HWND hWnd, const std::wstring& zipPath, const std::wstring& destFolder) {
    // Prompt for destination folder using SHBrowseForFolder
    wchar_t destBuf[MAX_PATH] = {};
    wcscpy_s(destBuf, destFolder.c_str());

    BROWSEINFOW bi = {};
    bi.hwndOwner = hWnd;
    bi.lpszTitle = L"Extract to folder:";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    // Pre-select current folder
    bi.lParam    = (LPARAM)destFolder.c_str();
    struct BffCb {
        static int CALLBACK Proc(HWND hwnd, UINT msg, LPARAM, LPARAM lp) {
            if (msg == BFFM_INITIALIZED)
                SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, lp);
            return 0;
        }
    };
    bi.lpfn = BffCb::Proc;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    SHGetPathFromIDListW(pidl, destBuf);
    CoTaskMemFree(pidl);

    std::wstring extractTo = destBuf;
    if (!extractTo.empty() && extractTo.back() != L'\\') extractTo += L'\\';

    CoInitialize(NULL);
    IShellDispatch* pShell = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IShellDispatch, (void**)&pShell);
    if (FAILED(hr) || !pShell) {
        MessageBoxW(hWnd, L"Shell dispatch unavailable.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    VARIANT vZip; VariantInit(&vZip);
    vZip.vt = VT_BSTR; vZip.bstrVal = SysAllocString(zipPath.c_str());
    Folder* pZipFolder = nullptr;
    hr = pShell->NameSpace(vZip, &pZipFolder);
    SysFreeString(vZip.bstrVal);

    VARIANT vDest; VariantInit(&vDest);
    vDest.vt = VT_BSTR; vDest.bstrVal = SysAllocString(extractTo.c_str());
    Folder* pDestFolder = nullptr;
    hr = pShell->NameSpace(vDest, &pDestFolder);
    SysFreeString(vDest.bstrVal);

    if (pZipFolder && pDestFolder) {
        FolderItems* pItems = nullptr;
        pZipFolder->Items(&pItems);
        if (pItems) {
            VARIANT vItems; VariantInit(&vItems);
            vItems.vt = VT_DISPATCH;
            vItems.pdispVal = pItems;

            VARIANT vOpts; VariantInit(&vOpts);
            vOpts.vt = VT_I4;
            vOpts.lVal = 4 | 16 | 1024;  // no dialog, yes-to-all
            pDestFolder->CopyHere(vItems, vOpts);
            Sleep(1000);
            pItems->Release();
        }
        pZipFolder->Release();
        pDestFolder->Release();
    }
    pShell->Release();

    MessageBoxW(hWnd, (L"Extracted to:\n" + extractTo).c_str(), L"Done", MB_OK | MB_ICONINFORMATION);
    RefreshLocalDir();
}

// ============================================================
// PDF SPLIT / EXTRACT PAGES  (raw byte-level PDF parser)
// ============================================================

// Minimal PDF parser — reads xref table, finds page objects, extracts them
struct PdfPage {
    int  objNum;
    int  genNum;
    long offset;      // byte offset of the object in source
};

// Find all occurrences of a string in a byte buffer
static long FindBytes(const std::vector<uint8_t>& buf, const std::string& needle, long startPos = 0) {
    if (needle.empty() || startPos < 0) return -1;
    auto it = std::search(buf.begin() + startPos, buf.end(),
                          needle.begin(), needle.end());
    return (it == buf.end()) ? -1 : (long)(it - buf.begin());
}

// Get the byte range of one object "N G obj ... endobj"
static bool GetObjRange(const std::vector<uint8_t>& buf, long offset,
                        long& outStart, long& outEnd) {
    // Find "endobj" after offset
    std::string endObjTag = "endobj";
    long endPos = FindBytes(buf, endObjTag, offset);
    if (endPos < 0) return false;
    outStart = offset;
    outEnd   = endPos + (long)endObjTag.size();
    return true;
}

// Parse the startxref value from the end of a PDF
static long ParseStartXref(const std::vector<uint8_t>& buf) {
    // Search backwards from end for "startxref"
    std::string tag = "startxref";
    long pos = (long)buf.size() - 1;
    for (; pos >= (long)tag.size(); pos--) {
        if (memcmp(buf.data() + pos - (long)tag.size() + 1,
                   tag.c_str(), tag.size()) == 0) {
            pos = pos - (long)tag.size() + 1;
            break;
        }
    }
    if (pos < 0) return -1;
    // Skip past "startxref" and whitespace
    long p = pos + (long)tag.size();
    while (p < (long)buf.size() && (buf[p] == ' ' || buf[p] == '\r' || buf[p] == '\n')) p++;
    long val = 0;
    while (p < (long)buf.size() && buf[p] >= '0' && buf[p] <= '9')
        val = val * 10 + (buf[p++] - '0');
    return val;
}

// Parse xref table at given offset → map<objNum, fileOffset>
static std::map<int,long> ParseXref(const std::vector<uint8_t>& buf, long xrefOffset) {
    std::map<int,long> result;
    long p = xrefOffset;
    // Skip "xref" keyword
    while (p < (long)buf.size() && (buf[p] == 'x' || buf[p] == 'r' || buf[p] == 'e' || buf[p] == 'f')) p++;
    while (p < (long)buf.size() && (buf[p] == ' ' || buf[p] == '\r' || buf[p] == '\n')) p++;

    // Parse subsections: "firstObj count\n"
    while (p < (long)buf.size()) {
        // Check for "trailer"
        if (p + 7 < (long)buf.size() && memcmp(buf.data() + p, "trailer", 7) == 0) break;
        // Read firstObj
        if (buf[p] < '0' || buf[p] > '9') break;
        int firstObj = 0;
        while (p < (long)buf.size() && buf[p] >= '0' && buf[p] <= '9') firstObj = firstObj*10+(buf[p++]-'0');
        while (p < (long)buf.size() && buf[p] == ' ') p++;
        int count = 0;
        while (p < (long)buf.size() && buf[p] >= '0' && buf[p] <= '9') count = count*10+(buf[p++]-'0');
        while (p < (long)buf.size() && (buf[p] == '\r' || buf[p] == '\n')) p++;
        // Read 'count' entries of 20 bytes each
        for (int i = 0; i < count && p + 20 <= (long)buf.size(); i++, p += 20) {
            long off = 0;
            for (int c = 0; c < 10; c++) off = off*10 + (buf[p+c]-'0');
            // gen num at p+11..p+16, flag at p+17
            char flag = buf[p+17];
            if (flag == 'n') result[firstObj + i] = off;
        }
    }
    return result;
}

// Collect all objects that a page depends on (recursive via obj references)
// We do a simple pass: find all "X Y R" references in the page object range
static void CollectRefs(const std::vector<uint8_t>& src,
                        const std::map<int,long>& xref,
                        int objNum,
                        std::set<int>& visited) {
    if (visited.count(objNum)) return;
    if (!xref.count(objNum))   return;
    visited.insert(objNum);

    long start, end;
    if (!GetObjRange(src, xref.at(objNum), start, end)) return;

    // Scan for "N M R" patterns
    for (long i = start; i < end - 4; i++) {
        // digit sequence followed by space, digit, space, 'R'
        if (src[i] >= '1' && src[i] <= '9') {
            long j = i;
            int refObj = 0;
            while (j < end && src[j] >= '0' && src[j] <= '9') refObj = refObj*10+(src[j++]-'0');
            if (j < end && src[j] == ' ') {
                j++;
                // gen num
                while (j < end && src[j] >= '0' && src[j] <= '9') j++;
                if (j < end && src[j] == ' ') {
                    j++;
                    if (j < end && src[j] == 'R') {
                        // found a reference
                        CollectRefs(src, xref, refObj, visited);
                    }
                }
            }
        }
    }
}

// Write a subset of objects from src into a new PDF with remapped obj numbers
static bool WritePdfSubset(const std::vector<uint8_t>& src,
                           const std::map<int,long>& xref,
                           int pagesRootObj,           // original obj num of /Pages
                           const std::vector<int>& pageObjNums,  // original obj nums of pages to include
                           const std::wstring& outPath) {
    FILE* fOut = _wfopen(outPath.c_str(), L"wb");
    if (!fOut) return false;

    fprintf(fOut, "%%PDF-1.4\n");
    fprintf(fOut, "%%%c%c%c%c\n", 0xE2, 0xE3, 0xCF, 0xD3);

    // Collect all objects needed
    std::set<int> needed;
    for (int pg : pageObjNums) CollectRefs(src, xref, pg, needed);
    // Also collect Pages root dependencies (MediaBox, Resources at root level)
    CollectRefs(src, xref, pagesRootObj, needed);
    // Remove pagesRootObj — we'll write a new one
    needed.erase(pagesRootObj);

    // Assign new object numbers
    // 1 = Catalog, 2 = new Pages root, 3..N = existing objs, N+1..N+P = page objs (re-used from needed)
    // Simpler: keep original obj nums but rewrite catalog and pages root
    // new obj 1 = Catalog  → /Pages 2 0 R
    // new obj 2 = Pages    → /Kids [page obj nums remapped]
    // all other needed objs keep their original obj num + offset by 2 (to avoid collision with 1,2)
    // Actually easiest: just remap all needed objs sequentially

    std::map<int,int> remap;  // old → new
    int nextNew = 3;
    for (int o : needed) {
        if (o != 1) remap[o] = nextNew++;
    }
    // Pages in order
    for (int pg : pageObjNums) {
        if (!remap.count(pg)) remap[pg] = nextNew++;
    }
    int totalObjs = nextNew;

    // Write objects and track offsets
    std::map<int,long> newOffsets;  // new obj num → file offset

    // Helper to rewrite object bytes with remapped references
    auto rewriteObj = [&](int newNum, const std::vector<uint8_t>& objBytes) {
        newOffsets[newNum] = ftell(fOut);
        fprintf(fOut, "%d 0 obj\n", newNum);
        // Write content between "obj\n" and "endobj", remapping "X Y R"
        // Find body (after first "obj\n")
        size_t bodyStart = 0;
        for (size_t i = 0; i + 3 < objBytes.size(); i++) {
            if (objBytes[i]=='o'&&objBytes[i+1]=='b'&&objBytes[i+2]=='j') {
                bodyStart = i + 3;
                while (bodyStart < objBytes.size() && (objBytes[bodyStart]=='\r'||objBytes[bodyStart]=='\n'))
                    bodyStart++;
                break;
            }
        }
        // Find end (before "endobj")
        size_t bodyEnd = objBytes.size();
        std::string endTag = "endobj";
        for (size_t i = objBytes.size(); i >= endTag.size(); i--) {
            if (memcmp(objBytes.data() + i - endTag.size(), endTag.c_str(), endTag.size()) == 0) {
                bodyEnd = i - endTag.size();
                break;
            }
        }
        // Write body, substituting "OLD_N G R" → "NEW_N G R"
        size_t i = bodyStart;
        while (i < bodyEnd) {
            // Check if we have "N M R" at position i
            bool didRemap = false;
            if (objBytes[i] >= '1' && objBytes[i] <= '9') {
                size_t j = i;
                int oldRef = 0;
                while (j < bodyEnd && objBytes[j] >= '0' && objBytes[j] <= '9')
                    oldRef = oldRef*10+(objBytes[j++]-'0');
                if (j < bodyEnd && objBytes[j] == ' ') {
                    size_t k = j+1;
                    int gen = 0;
                    while (k < bodyEnd && objBytes[k] >= '0' && objBytes[k] <= '9')
                        gen = gen*10+(objBytes[k++]-'0');
                    if (k < bodyEnd && objBytes[k] == ' ' && k+1 < bodyEnd && objBytes[k+1] == 'R') {
                        // It's a reference
                        int newRef = remap.count(oldRef) ? remap.at(oldRef) : oldRef;
                        // Special cases: pagesRootObj → 2, catalog → 1
                        if (oldRef == pagesRootObj) newRef = 2;
                        fprintf(fOut, "%d %d R", newRef, gen);
                        i = k + 2;
                        didRemap = true;
                    }
                }
            }
            if (!didRemap) { fputc(objBytes[i], fOut); i++; }
        }
        fprintf(fOut, "\nendobj\n\n");
    };

    // Helper: extract raw bytes of one object from src
    auto extractObjBytes = [&](int origNum) -> std::vector<uint8_t> {
        long off = xref.at(origNum);
        long start, end;
        if (!GetObjRange(src, off, start, end)) return {};
        return std::vector<uint8_t>(src.begin()+start, src.begin()+end);
    };

    // Obj 1 — Catalog
    newOffsets[1] = ftell(fOut);
    fprintf(fOut, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n\n");

    // Obj 2 — Pages root
    newOffsets[2] = ftell(fOut);
    fprintf(fOut, "2 0 obj\n<< /Type /Pages /Kids [");
    for (size_t pi = 0; pi < pageObjNums.size(); pi++) {
        if (pi > 0) fprintf(fOut, " ");
        fprintf(fOut, "%d 0 R", remap.count(pageObjNums[pi]) ? remap.at(pageObjNums[pi]) : pageObjNums[pi]+2);
    }
    fprintf(fOut, "] /Count %zu >>\nendobj\n\n", pageObjNums.size());

    // Non-page needed objects
    for (int origNum : needed) {
        if (origNum == 1 || origNum == pagesRootObj) continue;
        if (!remap.count(origNum)) continue;
        auto bytes = extractObjBytes(origNum);
        if (!bytes.empty()) rewriteObj(remap.at(origNum), bytes);
    }

    // Page objects
    for (int pg : pageObjNums) {
        if (!remap.count(pg)) continue;
        auto bytes = extractObjBytes(pg);
        if (!bytes.empty()) rewriteObj(remap.at(pg), bytes);
    }

    // XRef
    long xrefOff = ftell(fOut);
    fprintf(fOut, "xref\n0 %d\n", totalObjs);
    fprintf(fOut, "0000000000 65535 f \n");
    for (int i = 1; i < totalObjs; i++) {
        long off = newOffsets.count(i) ? newOffsets.at(i) : 0;
        fprintf(fOut, "%010ld 00000 n \n", off);
    }
    fprintf(fOut, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n",
            totalObjs, xrefOff);
    fclose(fOut);
    return true;
}

// High-level: find /Pages and enumerate page obj nums
static std::vector<int> GetPdfPageObjNums(const std::vector<uint8_t>& src,
                                           const std::map<int,long>& xref,
                                           int& outPagesRootObj) {
    // Find the Catalog (/Type /Catalog)
    int catalogObj = -1;
    for (auto& kv : xref) {
        long s, e;
        if (!GetObjRange(src, kv.second, s, e)) continue;
        long chunkEnd = (e - s < 200L) ? e : s + 200L;
        std::string chunk(src.begin()+s, src.begin()+chunkEnd);
        if (chunk.find("/Type /Catalog") != std::string::npos ||
            chunk.find("/Type/Catalog")  != std::string::npos) {
            catalogObj = kv.first; break;
        }
    }
    if (catalogObj < 0) return {};

    // Find /Pages ref in catalog
    long cs, ce;
    if (!GetObjRange(src, xref.at(catalogObj), cs, ce)) return {};
    std::string catStr(src.begin()+cs, src.begin()+ce);
    size_t ppos = catStr.find("/Pages ");
    if (ppos == std::string::npos) ppos = catStr.find("/Pages\n");
    if (ppos == std::string::npos) return {};
    ppos += 7; // skip "/Pages "
    int pagesObj = 0;
    while (ppos < catStr.size() && catStr[ppos] >= '0' && catStr[ppos] <= '9')
        pagesObj = pagesObj*10+(catStr[ppos++]-'0');
    outPagesRootObj = pagesObj;
    if (!xref.count(pagesObj)) return {};

    // BFS: collect all Page objects
    std::vector<int> pageObjs;
    std::vector<int> queue = {pagesObj};
    std::set<int> visited;
    while (!queue.empty()) {
        int cur = queue.back(); queue.pop_back();
        if (visited.count(cur)) continue;
        visited.insert(cur);
        if (!xref.count(cur)) continue;
        long s, e;
        if (!GetObjRange(src, xref.at(cur), s, e)) continue;
        std::string chunk(src.begin()+s, src.begin()+e);
        bool isPage  = (chunk.find("/Type /Page\n")  != std::string::npos ||
                        chunk.find("/Type /Page\r")  != std::string::npos ||
                        chunk.find("/Type /Page ")   != std::string::npos ||
                        chunk.find("/Type/Page")     != std::string::npos);
        bool isPages = (chunk.find("/Type /Pages")  != std::string::npos ||
                        chunk.find("/Type/Pages")   != std::string::npos);
        if (isPage) {
            pageObjs.push_back(cur);
        } else if (isPages) {
            // Parse /Kids array
            size_t kpos = chunk.find("/Kids");
            if (kpos != std::string::npos) {
                kpos += 5;
                while (kpos < chunk.size() && chunk[kpos] != '[') kpos++;
                kpos++; // skip '['
                while (kpos < chunk.size() && chunk[kpos] != ']') {
                    if (chunk[kpos] >= '0' && chunk[kpos] <= '9') {
                        int kid = 0;
                        while (kpos < chunk.size() && chunk[kpos] >= '0' && chunk[kpos] <= '9')
                            kid = kid*10+(chunk[kpos++]-'0');
                        queue.push_back(kid);
                    } else { kpos++; }
                }
            }
        }
    }
    return pageObjs;
}

// Parse page range string like "1,3-5,7" → set of 0-based page indices
static std::set<int> ParsePageRange(const std::wstring& rangeStr, int pageCount) {
    std::set<int> result;
    std::wstringstream ss(rangeStr);
    std::wstring token;
    while (std::getline(ss, token, L',')) {
        // Trim whitespace
        while (!token.empty() && token.front() == L' ') token.erase(0,1);
        while (!token.empty() && token.back()  == L' ') token.pop_back();
        size_t dash = token.find(L'-');
        if (dash != std::wstring::npos) {
            int lo = _wtoi(token.substr(0, dash).c_str());
            int hi = _wtoi(token.substr(dash+1).c_str());
            for (int i = lo; i <= hi; i++)
                if (i >= 1 && i <= pageCount) result.insert(i-1);
        } else {
            int pg = _wtoi(token.c_str());
            if (pg >= 1 && pg <= pageCount) result.insert(pg-1);
        }
    }
    return result;
}

static void SplitOrExtractPdf(HWND hWnd, const std::wstring& pdfPath, bool splitAll) {
    // Load PDF
    auto src = ReadFileBytes(pdfPath);
    if (src.empty()) { MessageBoxW(hWnd, L"Cannot read PDF.", L"Error", MB_OK | MB_ICONERROR); return; }

    long xrefOff = ParseStartXref(src);
    if (xrefOff < 0) { MessageBoxW(hWnd, L"Cannot parse PDF (no startxref).", L"Error", MB_OK | MB_ICONERROR); return; }

    auto xref = ParseXref(src, xrefOff);
    if (xref.empty()) { MessageBoxW(hWnd, L"Cannot parse PDF xref.", L"Error", MB_OK | MB_ICONERROR); return; }

    int pagesRootObj = -1;
    auto allPages = GetPdfPageObjNums(src, xref, pagesRootObj);
    if (allPages.empty()) { MessageBoxW(hWnd, L"No pages found in PDF.", L"Error", MB_OK | MB_ICONERROR); return; }

    int pageCount = (int)allPages.size();

    // Base name for output files
    std::wstring basePath = pdfPath;
    size_t dotPos = basePath.rfind(L'.');
    if (dotPos != std::wstring::npos) basePath = basePath.substr(0, dotPos);

    if (splitAll) {
        // Split: one PDF per page
        std::wstring msg = L"Split " + std::to_wstring(pageCount) + L" pages into separate PDFs?\nOutput: same folder as source.";
        if (MessageBoxW(hWnd, msg.c_str(), L"Split PDF", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

        int ok = 0;
        for (int i = 0; i < pageCount; i++) {
            std::wstring outPath = basePath + L"_page" + std::to_wstring(i+1) + L".pdf";
            if (WritePdfSubset(src, xref, pagesRootObj, {allPages[i]}, outPath)) ok++;
        }
        std::wstring done = std::to_wstring(ok) + L" of " + std::to_wstring(pageCount) + L" pages split.";
        MessageBoxW(hWnd, done.c_str(), L"Split Complete", MB_OK | MB_ICONINFORMATION);

    } else {
        // Extract: ask for page range
        std::wstring rangeHint = L"1-" + std::to_wstring(pageCount);
        std::wstring rangeStr = rangeHint;
        if (!ShowInputBox(hWnd, L"Extract Pages",
            (L"PDF has " + std::to_wstring(pageCount) + L" pages.\nEnter page range (e.g. 1-3,5,7):").c_str(),
            rangeStr)) return;

        auto indices = ParsePageRange(rangeStr, pageCount);
        if (indices.empty()) { MessageBoxW(hWnd, L"No valid pages in range.", L"Error", MB_OK | MB_ICONERROR); return; }

        std::vector<int> selectedPages;
        for (int idx : indices) selectedPages.push_back(allPages[idx]);

        // Save dialog
        std::wstring outPath = PromptSavePath(hWnd,
            L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf", L"Save Extracted Pages As");
        if (outPath.empty()) return;

        if (WritePdfSubset(src, xref, pagesRootObj, selectedPages, outPath)) {
            ShellExecuteW(NULL, L"open", outPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        } else {
            MessageBoxW(hWnd, L"Failed to write output PDF.", L"Error", MB_OK | MB_ICONERROR);
        }
    }
    RefreshLocalDir();
}

// ============================================================

static std::wstring GetFileExt(const std::wstring& filename) {
    size_t dot = filename.rfind(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring ext = filename.substr(dot + 1);
    for (auto& c : ext) c = towlower(c);
    return ext;
}

static bool IsImageExtW(const std::wstring& ext) {
    return ext==L"jpg"||ext==L"jpeg"||ext==L"png"||ext==L"gif"||
           ext==L"bmp"||ext==L"webp"||ext==L"tiff"||ext==L"tif";
}

static std::wstring PromptSavePath(HWND hWnd, const wchar_t* filter, const wchar_t* defExt, const wchar_t* title) {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hWnd;
    ofn.lpstrFilter  = filter;
    ofn.lpstrFile    = buf;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrDefExt  = defExt;
    ofn.lpstrTitle   = title;
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    return GetSaveFileNameW(&ofn) ? buf : L"";
}

void ProcessFileManagerRightClick(float x, float y, HWND hWnd) {
    if (fm_activeSubTab != 0) return;  // only local tab

    float cx = g_fm_cx, cy = g_fm_cy, cw = g_fm_cw, ch = g_fm_ch;
    float tabBarH = 48.0f;
    float bodyY   = cy + tabBarH;
    float bodyH   = ch - tabBarH;
    float tbH     = 44.0f, bcH = 32.0f;
    float rowHC   = 24.0f;
    float listY   = bodyY + tbH + bcH;
    float listH   = bodyH - tbH - bcH;
    float colHdrH = 28.0f;
    float rowsY   = listY + colHdrH;
    float rowsH   = listH - colHdrH;
    float flX     = cx;
    float sbWC    = 16.0f;
    float listAreaWC = g_fm_cw;
    float previewWC  = fm_previewVisible ? (listAreaWC * PREVIEW_WIDTH_RATIO) : 0.0f;
    float fileListWC = listAreaWC - previewWC;

    if (!PtIn(x, y, flX, rowsY, fileListWC - sbWC, rowsH)) return;

    int idx = (int)((y - rowsY) / rowHC) + fm_scrollOffset;
    if (idx < 0 || idx >= (int)fm_items.size()) return;

    // If the right-clicked item is not in the selection, clear selection and select just this item
    bool inSel = (fm_selectedItem == idx) ||
                 (std::find(fm_selectedItems.begin(), fm_selectedItems.end(), idx) != fm_selectedItems.end());
    if (!inSel) {
        fm_selectedItems.clear();
        fm_selectedItem = idx;
    }

    // Collect all selected items (merge fm_selectedItem + fm_selectedItems, deduplicated)
    std::vector<int> sel;
    if (fm_selectedItem >= 0) sel.push_back(fm_selectedItem);
    for (int s : fm_selectedItems) {
        if (std::find(sel.begin(), sel.end(), s) == sel.end()) sel.push_back(s);
    }
    std::sort(sel.begin(), sel.end());

    // Classify selected files
    std::vector<std::wstring> pdfFiles, imgFiles;
    for (int s : sel) {
        if (fm_items[s].second) continue; // skip dirs
        std::wstring ext = GetFileExt(fm_items[s].first);
        if (ext == L"pdf") pdfFiles.push_back(fm_currentPath + fm_items[s].first);
        else if (IsImageExtW(ext)) imgFiles.push_back(fm_currentPath + fm_items[s].first);
    }

    // Build context menu
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    enum CtxCmd {
        CMD_OPEN        = 1,
        CMD_RENAME      = 2,
        CMD_COPY        = 3,
        CMD_CUT         = 4,
        CMD_PASTE       = 5,
        CMD_DELETE      = 6,
        CMD_COPY_PATH   = 7,
        CMD_MERGE_PDF   = 8,
        CMD_IMG_PDF     = 9,
        CMD_PDF_SPLIT   = 10,
        CMD_PDF_EXTRACT = 11,
        CMD_ZIP         = 12,
        CMD_UNZIP       = 13,
    };

    // --- Single item ---
    if (sel.size() == 1) {
        AppendMenuW(hMenu, MF_STRING, CMD_OPEN,      L"Open");
        AppendMenuW(hMenu, MF_STRING, CMD_RENAME,    L"Rename");
        AppendMenuW(hMenu, MF_STRING, CMD_COPY_PATH, L"Copy path");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }

    // --- Edit actions (Copy/Cut always, Paste when clipboard non-empty) ---
    {
        std::wstring copyLabel = sel.size() == 1
            ? L"Copy \"" + fm_items[sel[0]].first + L"\""
            : L"Copy " + std::to_wstring(sel.size()) + L" items";
        std::wstring cutLabel = sel.size() == 1
            ? L"Cut \"" + fm_items[sel[0]].first + L"\""
            : L"Cut " + std::to_wstring(sel.size()) + L" items";
        if (!sel.empty()) {
            AppendMenuW(hMenu, MF_STRING, CMD_COPY, copyLabel.c_str());
            AppendMenuW(hMenu, MF_STRING, CMD_CUT,  cutLabel.c_str());
        }
        if (fm_clipOp != FmClipOp::None && !fm_clipPaths.empty()) {
            std::wstring pasteLabel = L"Paste here (" +
                std::to_wstring(fm_clipPaths.size()) +
                (fm_clipOp == FmClipOp::Cut ? L" item(s) — Move)" : L" item(s) — Copy)");
            AppendMenuW(hMenu, MF_STRING, CMD_PASTE, pasteLabel.c_str());
        }
        if (!sel.empty() || (fm_clipOp != FmClipOp::None && !fm_clipPaths.empty()))
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }

    // --- Zip / Unzip ---
    if (!sel.empty()) {
        std::wstring zipLabel = sel.size() == 1
            ? L"Zip \"" + fm_items[sel[0]].first + L"\""
            : L"Zip " + std::to_wstring(sel.size()) + L" items";
        AppendMenuW(hMenu, MF_STRING, CMD_ZIP, zipLabel.c_str());
    }
    // Unzip: only for .zip files
    bool hasZip = false;
    for (int s : sel) {
        if (!fm_items[s].second && GetFileExt(fm_items[s].first) == L"zip") { hasZip = true; break; }
    }
    if (hasZip && sel.size() == 1)
        AppendMenuW(hMenu, MF_STRING, CMD_UNZIP, L"Extract here...");
    if (!sel.empty()) AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // --- PDF Tools ---
    if (pdfFiles.size() >= 2) {
        std::wstring lbl = L"Merge " + std::to_wstring(pdfFiles.size()) + L" PDFs \u2192 single PDF";
        AppendMenuW(hMenu, MF_STRING, CMD_MERGE_PDF, lbl.c_str());
    }
    if (!imgFiles.empty()) {
        std::wstring lbl = L"Convert " + std::to_wstring(imgFiles.size()) +
                           (imgFiles.size() == 1 ? L" image" : L" images") + L" \u2192 PDF";
        AppendMenuW(hMenu, MF_STRING, CMD_IMG_PDF, lbl.c_str());
    }
    if (pdfFiles.size() == 1 && sel.size() == 1) {
        AppendMenuW(hMenu, MF_STRING, CMD_PDF_SPLIT,   L"Split PDF (one file per page)");
        AppendMenuW(hMenu, MF_STRING, CMD_PDF_EXTRACT, L"Extract pages from PDF...");
    }
    if (pdfFiles.size() >= 1 || !imgFiles.empty())
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // --- Delete ---
    if (!sel.empty()) {
        std::wstring delLabel = sel.size() == 1
            ? L"Delete \"" + fm_items[sel[0]].first + L"\""
            : L"Delete " + std::to_wstring(sel.size()) + L" items";
        AppendMenuW(hMenu, MF_STRING, CMD_DELETE, delLabel.c_str());
    }

    // Show menu
    POINT pt; GetCursorPos(&pt);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                             pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);

    switch (cmd) {

    // ---- Open ----
    case CMD_OPEN:
        if (!sel.empty()) {
            std::wstring fp = fm_currentPath + fm_items[sel[0]].first;
            {
                size_t _dot = fp.rfind(L'.');
                std::wstring _ext;
                if (_dot != std::wstring::npos) { _ext = fp.substr(_dot+1); for (auto& _c : _ext) _c = towlower(_c); }
                if (_ext==L"jpg"||_ext==L"jpeg"||_ext==L"png"||_ext==L"gif"||_ext==L"bmp"||_ext==L"webp"||_ext==L"ico"||_ext==L"tiff"||_ext==L"tif")
                    LaunchImageViewer(fp);
                else
                    ShellExecuteW(NULL, L"open", fp.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
        }
        break;

    // ---- Rename ----
    case CMD_RENAME:
        if (!sel.empty()) {
            std::wstring oldName = fm_items[sel[0]].first;
            std::wstring oldPath = fm_currentPath + oldName;
            RenameItem(hWnd, oldPath, oldName);
        }
        break;

    // ---- Copy path ----
    case CMD_COPY_PATH:
        if (!sel.empty()) {
            std::wstring fp = fm_currentPath + fm_items[sel[0]].first;
            if (OpenClipboard(hWnd)) {
                EmptyClipboard();
                size_t bytes = (fp.size() + 1) * sizeof(wchar_t);
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (hg) {
                    void* p = GlobalLock(hg);
                    memcpy(p, fp.c_str(), bytes);
                    GlobalUnlock(hg);
                    SetClipboardData(CF_UNICODETEXT, hg);
                }
                CloseClipboard();
            }
        }
        break;

    // ---- Copy ----
    case CMD_COPY:
        fm_clipPaths.clear();
        for (int s : sel) fm_clipPaths.push_back(fm_currentPath + fm_items[s].first);
        fm_clipOp = FmClipOp::Copy;
        break;

    // ---- Cut ----
    case CMD_CUT:
        fm_clipPaths.clear();
        for (int s : sel) fm_clipPaths.push_back(fm_currentPath + fm_items[s].first);
        fm_clipOp = FmClipOp::Cut;
        break;

    // ---- Paste ----
    case CMD_PASTE: {
        std::wstring dest = fm_currentPath;
        if (dest.back() != L'\\') dest += L'\\';
        PasteItems(hWnd, dest);
        break;
    }

    // ---- Delete ----
    case CMD_DELETE:
        for (int s : sel) {
            std::wstring fp = fm_currentPath + fm_items[s].first;
            if (fm_items[s].second) RemoveDirectoryW(fp.c_str());
            else                    DeleteFileW(fp.c_str());
        }
        fm_selectedItem = -1;
        fm_selectedItems.clear();
        RefreshLocalDir();
        break;

    // ---- Zip ----
    case CMD_ZIP: {
        std::vector<std::wstring> paths;
        for (int s : sel) paths.push_back(fm_currentPath + fm_items[s].first);
        ZipItems(hWnd, paths, fm_currentPath);
        break;
    }

    // ---- Unzip ----
    case CMD_UNZIP:
        if (!sel.empty()) {
            std::wstring fp = fm_currentPath + fm_items[sel[0]].first;
            UnzipItem(hWnd, fp, fm_currentPath);
        }
        break;

    // ---- Merge PDFs ----
    case CMD_MERGE_PDF: {
        std::wstring outPath = PromptSavePath(hWnd,
            L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf", L"Save Merged PDF As");
        if (!outPath.empty()) {
            std::vector<std::wstring> ordered;
            for (int s : sel) {
                if (GetFileExt(fm_items[s].first) == L"pdf")
                    ordered.push_back(fm_currentPath + fm_items[s].first);
            }
            MergePDFsWithPowerShell(ordered, outPath);
            ShellExecuteW(NULL, L"open", outPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
            RefreshLocalDir();
        }
        break;
    }

    // ---- Images → PDF ----
    case CMD_IMG_PDF: {
        std::wstring outPath = PromptSavePath(hWnd,
            L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf", L"Save Images as PDF");
        if (!outPath.empty()) {
            std::vector<std::wstring> ordered;
            for (int s : sel) {
                if (IsImageExtW(GetFileExt(fm_items[s].first)))
                    ordered.push_back(fm_currentPath + fm_items[s].first);
            }
            ImagesToPdf(ordered, outPath);
            ShellExecuteW(NULL, L"open", outPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
            RefreshLocalDir();
        }
        break;
    }

    // ---- PDF Split (one file per page) ----
    case CMD_PDF_SPLIT:
        if (!pdfFiles.empty())
            SplitOrExtractPdf(hWnd, pdfFiles[0], true);
        break;

    // ---- PDF Extract pages ----
    case CMD_PDF_EXTRACT:
        if (!pdfFiles.empty())
            SplitOrExtractPdf(hWnd, pdfFiles[0], false);
        break;

    } // end switch

    if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
}

// ============================================================
// MOUSE CLICK
// ============================================================
void ProcessFileManagerMouseClick(float x, float y, HWND hWnd) {
    float cx = g_fm_cx, cy = g_fm_cy, cw = g_fm_cw, ch = g_fm_ch;
    float tabBarH = 48.0f;
    float stW = 200.0f;

    // Sub-tab switch
    if (PtIn(x, y, cx + 10.0f, cy + 4.0f, stW, tabBarH - 8.0f)) {
        fm_activeSubTab = 0;
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }
    if (PtIn(x, y, cx + 10.0f + stW, cy + 4.0f, stW, tabBarH - 8.0f)) {
        fm_activeSubTab = 1;
        // Hide preview when switching to Drive tab
        if (fm_previewVisible) { LoadPreview(L"", L""); fm_previewVisible = false; }
        if (fm_driveSignedIn && fm_driveItems.empty()) DriveListFolder(fm_driveCurrentFolderId);
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }

    float bodyY = cy + tabBarH;
    float bodyH = ch - tabBarH;

    if (fm_activeSubTab == 0) {
        float tbH = 44.0f;
        float btnW = 36.0f, btnH = 28.0f, btnY = bodyY + (tbH - btnH) / 2.0f;
        float bx = cx + 10.0f;

        // Up button
        if (PtIn(x, y, bx, btnY, btnW, btnH)) {
            wstring p = fm_currentPath;
            if (!p.empty() && p.back() == L'\\') p.pop_back();
            size_t pos = p.rfind(L'\\');
            if (pos != wstring::npos) NavigateFileManagerTo(p.substr(0, pos + 1));
            else if (p.length() >= 2) NavigateFileManagerTo(p.substr(0, 3));
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
        bx += btnW + 6.0f;

        // Refresh
        if (PtIn(x, y, bx, btnY, btnW, btnH)) {
            RefreshLocalDir();
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
        bx += btnW + 6.0f;

        // New Folder
        float nbW = 110.0f;
        if (PtIn(x, y, bx, btnY, nbW, btnH)) {
            wchar_t name[MAX_PATH] = L"New Folder";
            // Simple dialog prompt (reuse InputBox style)
            wstring fullPath = fm_currentPath + L"New Folder";
            CreateDirectoryW(fullPath.c_str(), NULL);
            RefreshLocalDir();
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
        bx += nbW + 6.0f;

        // Delete
        if (fm_selectedItem >= 0) {
            float dW = 80.0f;
            if (PtIn(x, y, bx, btnY, dW, btnH)) {
                wstring fullPath = fm_currentPath + fm_items[fm_selectedItem].first;
                if (fm_items[fm_selectedItem].second) {
                    RemoveDirectoryW(fullPath.c_str());
                } else {
                    DeleteFileW(fullPath.c_str());
                }
                fm_selectedItem = -1;
                RefreshLocalDir();
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
            bx += dW + 6.0f;

            // Open
            float oW = 80.0f;
            if (PtIn(x, y, bx, btnY, oW, btnH)) {
                if (fm_selectedItem >= 0) {
                    wstring fullPath = fm_currentPath + fm_items[fm_selectedItem].first;
                    {
                        size_t _dot = fullPath.rfind(L'.');
                        wstring _ext;
                        if (_dot != wstring::npos) { _ext = fullPath.substr(_dot+1); for (auto& _c : _ext) _c = towlower(_c); }
                        if (_ext==L"jpg"||_ext==L"jpeg"||_ext==L"png"||_ext==L"gif"||_ext==L"bmp"||_ext==L"webp"||_ext==L"ico"||_ext==L"tiff"||_ext==L"tif")
                            LaunchImageViewer(fullPath);
                        else
                            ShellExecuteW(NULL, L"open", fullPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    }
                }
                return;
            }
        }

        // Breadcrumb click
        float bcY = bodyY + tbH;
        float bcH = 32.0f;
        // Rebuild breadcrumb X positions
        float bcX = cx + 32.0f;
        for (int i = 0; i < (int)fm_breadcrumb.size(); i++) {
            if (PtIn(x, y, bcX, bcY, 150.0f, bcH)) {
                // Navigate to this breadcrumb segment
                wstring dest;
                for (int j = 0; j <= i; j++) {
                    dest += fm_breadcrumb[j];
                    if (!dest.empty() && dest.back() != L'\\') dest += L'\\';
                }
                NavigateFileManagerTo(dest);
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                return;
            }
            bcX += 116.0f;
        }

        // File list click
        float listY = bcY + bcH;
        float listH = bodyH - tbH - bcH;
        float colHdrH = 28.0f;
        float flX = cx; // no internal sidebar; cx is already content area start
        float rowH = 34.0f;
        float rowsY = listY + colHdrH;
        float rowsH = listH - colHdrH;

        {
            float rowHC = 24.0f;
            float sbWC  = 16.0f;
            // Compute file list width (preview panel may shrink it)
            float listAreaWC = g_fm_cw; // full content width, no internal sidebar
            float previewWC  = fm_previewVisible ? (listAreaWC * PREVIEW_WIDTH_RATIO) : 0.0f;
            float fileListWC = listAreaWC - previewWC;
            if (PtIn(x, y, flX, rowsY, fileListWC - sbWC, rowsH)) {
                int idx = (int)((y - rowsY) / rowHC) + fm_scrollOffset;
                if (idx >= 0 && idx < (int)fm_items.size()) {
                    if (fm_selectedItem == idx && fm_items[idx].second &&
                        fm_selectedItems.empty()) {
                        // Double-click into folder → navigate, clear preview
                        wstring dest = fm_currentPath + fm_items[idx].first + L"\\";
                        LoadPreview(L"", L"");
                        NavigateFileManagerTo(dest);
                        if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                        return;
                    } else if (fm_selectedItem == idx && !fm_items[idx].second &&
                               fm_selectedItems.empty()) {
                        // Double-click on file → open it
                        wstring fullPath2 = fm_currentPath + fm_items[idx].first;
                        size_t _dot = fullPath2.rfind(L'.');
                        wstring _ext;
                        if (_dot != wstring::npos) { _ext = fullPath2.substr(_dot+1); for (auto& _c : _ext) _c = towlower(_c); }
                        if (_ext==L"jpg"||_ext==L"jpeg"||_ext==L"png"||_ext==L"gif"||_ext==L"bmp"||_ext==L"webp"||_ext==L"ico"||_ext==L"tiff"||_ext==L"tif")
                            LaunchImageViewer(fullPath2);
                        else
                            ShellExecuteW(NULL, L"open", fullPath2.c_str(), NULL, NULL, SW_SHOWNORMAL);
                        return;
                    }

                    bool ctrlHeld  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool shiftHeld = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;

                    if (ctrlHeld) {
                        // Ctrl+click: toggle this item in multi-select
                        auto it = std::find(fm_selectedItems.begin(), fm_selectedItems.end(), idx);
                        if (it != fm_selectedItems.end()) {
                            fm_selectedItems.erase(it);
                            if (fm_selectedItem == idx) fm_selectedItem = fm_selectedItems.empty() ? -1 : fm_selectedItems.back();
                        } else {
                            fm_selectedItems.push_back(idx);
                            fm_selectedItem = idx;
                        }
                        fm_lastClickedItem = idx;
                        // Load preview for last clicked file
                        if (!fm_items[idx].second) {
                            wstring fname2 = fm_items[idx].first;
                            size_t dot = fname2.rfind(L'.');
                            wstring ext2;
                            if (dot != wstring::npos) { ext2 = fname2.substr(dot+1); for (auto& c : ext2) c = towlower(c); }
                            LoadPreview(fm_currentPath + fname2, ext2);
                        }
                    } else if (shiftHeld && fm_lastClickedItem >= 0) {
                        // Shift+click: select range
                        fm_selectedItems.clear();
                        int lo = min(fm_lastClickedItem, idx);
                        int hi = max(fm_lastClickedItem, idx);
                        for (int r = lo; r <= hi; r++) fm_selectedItems.push_back(r);
                        fm_selectedItem = idx;
                        if (!fm_items[idx].second) {
                            wstring fname2 = fm_items[idx].first;
                            size_t dot = fname2.rfind(L'.');
                            wstring ext2;
                            if (dot != wstring::npos) { ext2 = fname2.substr(dot+1); for (auto& c : ext2) c = towlower(c); }
                            LoadPreview(fm_currentPath + fname2, ext2);
                        }
                    } else {
                        // Plain click: clear multi-select, select this item
                        fm_selectedItems.clear();
                        fm_selectedItem = idx;
                        fm_lastClickedItem = idx;
                    }

                    // Single click → load preview (only when not ctrl/shift and single item)
                    if (!ctrlHeld && !shiftHeld && !fm_items[idx].second) {
                        // It's a file — determine extension
                        wstring fname2 = fm_items[idx].first;
                        size_t dot = fname2.rfind(L'.');
                        wstring ext2;
                        if (dot != wstring::npos) {
                            ext2 = fname2.substr(dot + 1);
                            for (auto& ch2 : ext2) ch2 = towlower(ch2);
                        }
                        wstring fullPath2 = fm_currentPath + fname2;
                        LoadPreview(fullPath2, ext2);
                    } else if (!ctrlHeld && !shiftHeld && fm_items[idx].second) {
                        // Folder selected — show empty/folder preview
                        LoadPreview(L"", L"");
                        fm_previewVisible = false;
                    }
                    if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
                }
            }
        }

        // Sidebar clicks (Quick access, drives, Google Drive) are handled by tab_special.cpp.

    } else if (fm_activeSubTab == 1) {
        if (!fm_driveSignedIn) {
            // Sign-in button
            float cardW = 400.0f, cardH = 240.0f;
            float cardX = cx + (cw - cardW) / 2.0f;
            float cardY = bodyY + (bodyH - cardH) / 2.0f;
            float btnW2 = 220.0f, btnH2 = 40.0f;
            float btnX2 = cardX + (cardW - btnW2) / 2.0f;
            float btnY2 = cardY + cardH - 58.0f;
            if (PtIn(x, y, btnX2, btnY2, btnW2, btnH2)) {
                DriveStartOAuth();  // Opens browser for OAuth, listens on localhost:5050
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
            }
        } else {
            float tbH = 44.0f;
            float btnH2 = 28.0f, btnY2 = bodyY + (tbH - btnH2) / 2.0f;

            // Back button
            if (PtIn(x, y, cx + 10.0f, btnY2, 34.0f, btnH2) && !fm_driveFolderStack.empty()) {
                DriveGoBack();
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                return;
            }
            // Refresh button
            if (PtIn(x, y, cx + 50.0f, btnY2, 34.0f, btnH2)) {
                DriveListFolder(fm_driveCurrentFolderId);
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                return;
            }
            // Sign-out button
            float soW = 80.0f, soX = cx + cw - soW - 14.0f;
            if (PtIn(x, y, soX, btnY2, soW, btnH2)) {
                DriveSignOut();
                if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                return;
            }

            // Row click
            float dvListY = bodyY + tbH;
            float colHdrH = 28.0f;
            float dvRowH  = 38.0f;
            float dvRowsY = dvListY + colHdrH;
            float dvRowsH = bodyH - tbH - colHdrH;
            if (PtIn(x, y, cx, dvRowsY, cw, dvRowsH)) {
                int idx = (int)((y - dvRowsY) / dvRowH) + fm_driveScrollOff;
                if (idx >= 0 && idx < (int)fm_driveItems.size()) {
                    if (fm_driveSelectedItem == idx) {
                        // Double-click: navigate folder or open file
                        DriveOpenItem(fm_driveItems[idx]);
                    } else {
                        fm_driveSelectedItem = idx;
                    }
                    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
                }
            }
        }
    }
}

// ============================================================
// MOUSE WHEEL
// ============================================================
void ProcessFileManagerMouseWheel(float x, float y, int delta) {
    // Windows Explorer style: 3 lines per notch (WHEEL_DELTA=120 = 1 notch)
    int notches = abs(delta) / WHEEL_DELTA;
    if (notches == 0) notches = 1;
    int step = (delta > 0) ? -(3 * notches) : (3 * notches);

    if (fm_activeSubTab == 0) {
        int maxScroll = max(0, (int)fm_items.size() - 1);
        fm_scrollOffset = max(0, min(maxScroll, fm_scrollOffset + step));
    } else {
        int maxScroll = max(0, (int)fm_driveItems.size() - 1);
        fm_driveScrollOff = max(0, min(maxScroll, fm_driveScrollOff + step));
    }
    extern HWND hParentWnd;
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE); // FALSE = no erase → no flicker
}

// ============================================================
// EXPLORER RIGHT-CLICK MERGE ENTRY POINT
// Called from WinMain with "-merge file1.pdf file2.pdf ..."
// ============================================================
void RunExplorerPdfMerge(const std::vector<std::wstring>& pdfPaths) {
    if (pdfPaths.size() < 2) {
        MessageBoxW(NULL,
            L"Please select 2 or more PDF files in Explorer,\n"
            L"then right-click \u2192 \"Merge PDFs with RasFocus+\".",
            L"RasFocus+ \u2014 PDF Merge", MB_ICONINFORMATION | MB_OK | MB_TOPMOST);
        return;
    }

    // Build default output path: same folder as first PDF
    std::wstring firstPath = pdfPaths[0];
    size_t sl = firstPath.find_last_of(L"\\/");
    std::wstring dir      = (sl != std::wstring::npos) ? firstPath.substr(0, sl + 1) : L"";
    std::wstring baseName = (sl != std::wstring::npos) ? firstPath.substr(sl + 1)    : firstPath;
    size_t dot = baseName.rfind(L'.');
    if (dot != std::wstring::npos) baseName = baseName.substr(0, dot);
    std::wstring outPath = dir + L"Merged_" + baseName + L".pdf";

    // Avoid overwriting an existing file
    {
        int n = 2;
        std::wstring candidate = outPath;
        while (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            candidate = dir + L"Merged_" + baseName + L"_" + std::to_wstring(n++) + L".pdf";
        outPath = candidate;
    }

    // Run the merge (PowerShell / C# inline — same function used inside the app)
    MergePDFsWithPowerShell(pdfPaths, outPath);

    // Check if output was created
    if (GetFileAttributesW(outPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::wstring outName = outPath.substr(outPath.find_last_of(L"\\/") + 1);
        std::wstring msg = L"Successfully merged " + std::to_wstring(pdfPaths.size()) +
                           L" PDFs \u2192\n\n" + outName +
                           L"\n\nSaved in the same folder as the source files.\n"
                           L"Click OK to open the folder.";
        MessageBoxW(NULL, msg.c_str(), L"RasFocus+ \u2014 PDF Merge Done",
                    MB_ICONINFORMATION | MB_OK | MB_TOPMOST);

        // Highlight output file in Explorer
        ITEMIDLIST* pidl = NULL;
        if (SUCCEEDED(SHParseDisplayName(outPath.c_str(), NULL, &pidl, 0, NULL))) {
            SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
            CoTaskMemFree(pidl);
        }
    } else {
        MessageBoxW(NULL,
            L"PDF merge failed.\n\nPossible reasons:\n"
            L"\u2022 One or more files are encrypted / password-protected\n"
            L"\u2022 A file is corrupt or not a valid PDF\n"
            L"\u2022 No write permission in the output folder\n"
            L"\u2022 PowerShell execution is restricted on this PC",
            L"RasFocus+ \u2014 PDF Merge Error",
            MB_ICONERROR | MB_OK | MB_TOPMOST);
    }
}
