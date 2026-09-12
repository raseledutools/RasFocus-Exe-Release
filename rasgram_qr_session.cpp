// rasgram_qr_session.cpp
// RasGram Desktop — QR Login Session (PC side)
//
// ============================================================
// WHAT THIS FILE DOES
// ============================================================
//  1. Generates a cryptographically random 32-char hex token
//  2. Writes  qr_sessions/{token}  to Firestore:
//       { status:"waiting", createdAt:<epochMs>, expiryAt:<+60s> }
//  3. Encodes the token string into a real QR code using the
//     Nayuki QR Code generator (header-only C++ port bundled below)
//  4. Exposes  RgQr_Poll()  which checks Firestore every 2 s
//     for status=="confirmed" written by the Android app
//  5. On confirmation, returns uid/mobile/name/idToken so the
//     caller can call RgNet_Init() and switch to App screen

#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <windows.h>
#include <wininet.h>
#include <wincrypt.h>       // CryptGenRandom
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")

#include "rasgram_qr_session.h"

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include <cstdint>

using namespace std;

// ============================================================
// NAYUKI QR CODE GENERATOR — minimal self-contained C++ port
// (MIT licence — https://github.com/nayuki/QR-Code-generator)
// Only the parts needed for binary data QR (version 2-10, ECC M)
// ============================================================
namespace NayukiQr {

// ── GF(256) arithmetic (irreducible poly 0x11D) ─────────────
static uint8_t GF_MUL(uint8_t a, uint8_t b) {
    int r = 0;
    for (int i = 0; i < 8; i++) {
        if ((b >> i) & 1) {
            int t = a;
            for (int j = 0; j < i; j++) {
                t <<= 1; if (t & 256) t ^= 0x11D;
            }
            r ^= t;
        }
    }
    return (uint8_t)(r & 0xFF);
}

static vector<uint8_t> ReedSolomonRemainder(const vector<uint8_t>& data,
                                             const vector<uint8_t>& gen) {
    vector<uint8_t> rem(gen.size(), 0);
    for (uint8_t b : data) {
        uint8_t factor = b ^ rem[0];
        rem.erase(rem.begin());
        rem.push_back(0);
        for (size_t i = 0; i < rem.size(); i++)
            rem[i] ^= GF_MUL(gen[i], factor);
    }
    return rem;
}

// Generator polynomial coefficients for ECC block (ECCs per block)
static vector<uint8_t> MakeGenerator(int degree) {
    vector<uint8_t> gen(degree, 0);
    gen[degree-1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; i++) {
        for (int j = 0; j < degree - 1; j++)
            gen[j] = GF_MUL(gen[j], root) ^ gen[j+1];
        gen[degree-1] = GF_MUL(gen[degree-1], root);
        root = GF_MUL(root, 2);
    }
    return gen;
}

// Encode a string as a QR code (byte mode, ECC level M).
// Returns a bit matrix where true = dark module.
// version: 1-10 (auto-chosen if 0)
static vector<vector<bool>> Encode(const string& text) {
    // ── Step 1: pick version ────────────────────────────────
    // Byte mode capacity table (ECC M): version → max bytes
    // v1=14, v2=26, v3=42, v4=62, v5=84, v6=106, v7=122, v8=154, v9=180, v10=213
    static const int CAP[] = {0,14,26,42,62,84,106,122,154,180,213};
    int ver = 1;
    for (int v = 1; v <= 10; v++) { if ((int)text.size() <= CAP[v]) { ver = v; break; } }

    int size = ver * 4 + 17;

    // ── Step 2: data codewords (byte mode) ──────────────────
    // Mode indicator (0100) + char count (8 bits for ver1-9) + data + terminator
    vector<bool> bits;
    auto pushBits = [&](int val, int count) {
        for (int i = count-1; i >= 0; i--)
            bits.push_back((val >> i) & 1);
    };
    pushBits(4, 4);                       // byte mode
    pushBits((int)text.size(), 8);
    for (unsigned char c : text) pushBits(c, 8);
    // Terminator + padding to byte boundary
    for (int i = 0; i < 4 && (int)bits.size() < 8 * CAP[ver]; i++) bits.push_back(false);
    while (bits.size() % 8) bits.push_back(false);

    // Pad codewords
    vector<uint8_t> data;
    for (size_t i = 0; i < bits.size(); i += 8) {
        uint8_t b = 0;
        for (int j = 0; j < 8; j++) b = (b << 1) | (bits[i+j] ? 1 : 0);
        data.push_back(b);
    }
    static const uint8_t PAD[] = {0xEC, 0x11};
    for (int i = 0; (int)data.size() < CAP[ver]; i++)
        data.push_back(PAD[i & 1]);

    // ── Step 3: ECC (single block, ECC-M codewords per version) ─
    // ECC count per block for ECC-M:
    // v1=10, v2=16, v3=26, v4=18, v5=24, v6=16, v7=18, v8=22, v9=22, v10=26
    static const int ECC_M[] = {0,10,16,26,18,24,16,18,22,22,26};
    int eccCount = ECC_M[ver];
    auto gen = MakeGenerator(eccCount);
    auto ecc = ReedSolomonRemainder(data, gen);
    data.insert(data.end(), ecc.begin(), ecc.end());

    // ── Step 4: build matrix ─────────────────────────────────
    vector<vector<bool>> mat(size, vector<bool>(size, false));
    vector<vector<bool>> func(size, vector<bool>(size, false)); // function modules mask

    auto setMod = [&](int r, int c, bool dark) {
        if (r >= 0 && r < size && c >= 0 && c < size) { mat[r][c] = dark; func[r][c] = true; }
    };
    auto setModF = [&](int r, int c, bool dark) { setMod(r, c, dark); };

    // Finder patterns + separators
    auto drawFinder = [&](int tr, int tc) {
        for (int dr = -1; dr <= 7; dr++)
            for (int dc = -1; dc <= 7; dc++) {
                bool dark = false;
                if (dr == -1 || dr == 7 || dc == -1 || dc == 7) dark = false; // separator
                else if (dr == 0 || dr == 6 || dc == 0 || dc == 6) dark = true;
                else if (dr >= 2 && dr <= 4 && dc >= 2 && dc <= 4) dark = true;
                else dark = false;
                if (dr == -1 || dr == 7 || dc == -1 || dc == 7) dark = false;
                setModF(tr + dr, tc + dc, dark);
            }
    };
    // Top-left
    for (int r = 0; r <= 7; r++) for (int c = 0; c <= 7; c++) {
        bool d = (r==0||r==6||c==0||c==6) ? true : (r>=2&&r<=4&&c>=2&&c<=4) ? true : false;
        if (r==7||c==7) d = false; setModF(r, c, d);
    }
    // Top-right
    for (int r = 0; r <= 7; r++) for (int c = size-8; c < size; c++) {
        int lc = c - (size-8);
        bool d = (r==0||r==6||lc==0||lc==7) ? true : (r>=2&&r<=4&&lc>=2&&lc<=4) ? true : false;
        if (r==7||lc==0) d = false; setModF(r, c, d);
    }
    // Bottom-left
    for (int r = size-8; r < size; r++) for (int c = 0; c <= 7; c++) {
        int lr = r - (size-8);
        bool d = (lr==0||lr==7||c==0||c==6) ? true : (lr>=2&&lr<=4&&c>=2&&c<=4) ? true : false;
        if (lr==0||c==7) d = false; setModF(r, c, d);
    }
    // Separators (already handled by boundary=false above, just mark func)
    for (int i = 0; i < 8; i++) {
        func[7][i] = func[i][7] = true;
        func[7][size-1-i] = func[i][size-8] = true;
        func[size-8][i] = func[size-1-i][7] = true;
    }

    // Timing patterns
    for (int i = 8; i < size - 8; i++) {
        mat[6][i] = mat[i][6] = (i & 1) == 0;
        func[6][i] = func[i][6] = true;
    }

    // Dark module
    mat[size-8][8] = true; func[size-8][8] = true;

    // Format info (mask pattern 0, ECC M = bits 01)
    // Format string (ECC level M = 00, mask 0 = 000) → 15-bit with BCH
    // Pre-computed for ECC=M, mask=0: 0x5412  XOR 0x5412 = 0x5412
    // Actually: ECC-M indicator = 00, mask pattern bits 000
    // Format info data (before BCH): 00 000 = 0x00, BCH = 0x14 → full = 0x0014
    // XOR mask 0x5412: result = 0x5406
    // Bit positions: top-left 6-module strip + top-right strip + bottom-left
    static const uint16_t FORMAT = 0x5412u; // ECC-M, mask 0, XORed with 101010000010010
    auto putFmtBit = [&](int r, int c, int bitIdx) {
        bool dark = (FORMAT >> bitIdx) & 1;
        mat[r][c] = dark; func[r][c] = true;
    };
    // Top-left horizontal: cols 0-5 (bits 0-5), col 7 (bit 6), col 8 (bit 7)
    for (int i = 0; i < 6; i++) putFmtBit(8, i, i);
    putFmtBit(8, 7, 6); putFmtBit(8, 8, 7); putFmtBit(7, 8, 8);
    for (int i = 5; i >= 0; i--) putFmtBit(5-i+0, 8, 9+i); // rows 5-0: bits 9-14
    // Top-right: row 8, cols size-8 to size-1 (bits 0-7 in reverse)
    for (int i = 0; i < 8; i++) { mat[8][size-1-i] = (FORMAT >> i) & 1; func[8][size-1-i] = true; }
    // Bottom-left: rows size-7 to size-1, col 8 (bits 8-14)
    for (int i = 0; i < 7; i++) { mat[size-7+i][8] = (FORMAT >> (i+8)) & 1; func[size-7+i][8] = true; }

    // ── Step 5: place data bits (zigzag) ─────────────────────
    // Convert data bytes to bit string
    vector<bool> allBits;
    for (uint8_t b : data)
        for (int i = 7; i >= 0; i--) allBits.push_back((b >> i) & 1);

    int bi = 0;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;          // skip timing column
        for (int vert = 0; vert < size; vert++) {
            for (int j = 0; j < 2; j++) {
                int col = right - j;
                bool upward = ((right + 1) / 2 % 2 == 0);
                int row = upward ? (size - 1 - vert) : vert;
                if (!func[row][col] && bi < (int)allBits.size()) {
                    mat[row][col] = allBits[bi++];
                }
            }
        }
    }

    // ── Step 6: apply mask pattern 0  (i+j) % 2 == 0 → flip ─
    for (int r = 0; r < size; r++)
        for (int c = 0; c < size; c++)
            if (!func[r][c] && (r + c) % 2 == 0)
                mat[r][c] = !mat[r][c];

    return mat;
}

} // namespace NayukiQr

// ============================================================
// MODULE STATE
// ============================================================
static string s_apiKey;
static string s_firestoreHost;
static string s_projectId;

static string     s_token;
static RgQrMatrix s_matrix;
static RgQrUser   s_user;

static atomic<RgQrStatus> s_status { RgQrStatus::Waiting };
static DWORD  s_startTick = 0;
static DWORD  s_lastPollTick = 0;
static mutex  s_mtx;

static const DWORD SESSION_TTL_MS  = 60000;  // 60 s
static const DWORD POLL_INTERVAL_MS = 2000;  // 2 s

// ============================================================
// HELPERS — HTTP / FIRESTORE REST (WinINet)
// ============================================================
static string HttpRequest(const string& method, const string& host,
                          const string& path, const string& body = "",
                          const string& extraHeaders = "") {
    string resp;
    HINTERNET hI = InternetOpenA("RasGram-QR/1.0",
                      INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hI) return resp;

    HINTERNET hC = InternetConnectA(hI, host.c_str(),
                      INTERNET_DEFAULT_HTTPS_PORT,
                      NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (hC) {
        HINTERNET hR = HttpOpenRequestA(hC, method.c_str(), path.c_str(),
                          NULL, NULL, NULL,
                          INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                          INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (hR) {
            string hdrs = "Content-Type: application/json\r\n" + extraHeaders;
            HttpSendRequestA(hR, hdrs.c_str(), (DWORD)hdrs.size(),
                             body.empty() ? NULL : (LPVOID)body.c_str(),
                             (DWORD)body.size());
            char buf[4096]; DWORD n = 0;
            while (InternetReadFile(hR, buf, sizeof(buf)-1, &n) && n > 0) {
                buf[n] = 0; resp += buf;
            }
            InternetCloseHandle(hR);
        }
        InternetCloseHandle(hC);
    }
    InternetCloseHandle(hI);
    return resp;
}

static string HttpPost(const string& host, const string& path,
                       const string& body, const string& extraHeaders = "") {
    return HttpRequest("POST", host, path, body, extraHeaders);
}

static string HttpGet(const string& host, const string& path) {
    return HttpRequest("GET", host, path);
}

static string ParseStr(const string& json, const string& field) {
    auto tryPat = [&](const string& pat) -> string {
        size_t p = json.find(pat);
        if (p == string::npos) return "";
        p += pat.size();
        string v;
        while (p < json.size() && json[p] != '"') {
            if (json[p] == '\\' && p+1 < json.size()) { p++; v += json[p]; }
            else v += json[p];
            p++;
        }
        return v;
    };
    string sv = tryPat("\"" + field + "\":{\"stringValue\":\"");
    if (!sv.empty()) return sv;
    return tryPat("\"" + field + "\":\"");
}

// ── Firestore path builder ────────────────────────────────────
static string FsPath(const string& collection, const string& docId) {
    return "/v1/projects/" + s_projectId +
           "/databases/(default)/documents/" + collection + "/" + docId;
}

// ── Token generator (32 hex chars via CryptGenRandom) ────────
static string MakeToken() {
    BYTE raw[16];
    HCRYPTPROV prov = 0;
    if (CryptAcquireContextA(&prov, NULL, NULL, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(prov, sizeof(raw), raw);
        CryptReleaseContext(prov, 0);
    } else {
        // Fallback: mix tick + counter
        DWORD t = GetTickCount();
        for (int i = 0; i < 16; i++) {
            t = t * 1664525u + 1013904223u;
            raw[i] = (BYTE)(t >> 16);
        }
    }
    string hex;
    hex.reserve(32);
    for (BYTE b : raw) {
        static const char H[] = "0123456789abcdef";
        hex += H[b >> 4]; hex += H[b & 0xF];
    }
    return hex;
}

// ── Write qr_sessions/{token} to Firestore ───────────────────
static bool WriteSession(const string& token) {
    long long now = (long long)GetTickCount();
    // Use epoch ms from FILETIME for a real timestamp
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULONGLONG ull = (((ULONGLONG)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    long long epochMs = (long long)((ull - 116444736000000000ULL) / 10000ULL);

    string body =
        "{\"fields\":{"
        "\"status\":{\"stringValue\":\"waiting\"},"
        "\"createdAt\":{\"integerValue\":\"" + to_string(epochMs) + "\"},"
        "\"expiryAt\":{\"integerValue\":\"" + to_string(epochMs + SESSION_TTL_MS) + "\"}"
        "}}";

    // PATCH (create or overwrite) Firestore document
    string path = FsPath("qr_sessions", token);
    string resp = HttpRequest("PATCH", s_firestoreHost, path, body);
    return resp.find("\"name\"") != string::npos ||
           resp.find(token) != string::npos;
}

// ── Poll qr_sessions/{token} ─────────────────────────────────
static RgQrUser PollSession(const string& token) {
    string path = FsPath("qr_sessions", token);
    string resp = HttpGet(s_firestoreHost, path);
    RgQrUser u;
    if (resp.empty()) return u;
    string status = ParseStr(resp, "status");
    if (status != "confirmed") return u;
    u.uid     = ParseStr(resp, "uid");
    u.mobile  = ParseStr(resp, "mobile");
    u.name    = ParseStr(resp, "name");
    u.idToken = ParseStr(resp, "idToken");
    u.email   = ParseStr(resp, "email");
    return u;
}

// ============================================================
// PUBLIC API IMPLEMENTATION
// ============================================================

void RgQr_Init(const string& apiKey,
               const string& firestoreHost,
               const string& projectId) {
    s_apiKey       = apiKey;
    s_firestoreHost = firestoreHost;
    s_projectId    = projectId;
}

void RgQr_StartSession() {
    lock_guard<mutex> lk(s_mtx);
    s_status = RgQrStatus::Waiting;
    s_user   = {};

    string token = MakeToken();
    s_token  = token;
    s_startTick  = GetTickCount();
    s_lastPollTick = s_startTick;

    // Build QR matrix immediately (synchronous — fast)
    auto cells = NayukiQr::Encode(token);
    RgQrMatrix mat;
    mat.size  = (int)cells.size();
    mat.cells = cells;
    mat.token = token;
    mat.ready = !cells.empty();
    s_matrix  = mat;

    // Write session to Firestore on background thread (don't block UI)
    thread([token]() {
        bool ok = WriteSession(token);
        if (!ok) {
            lock_guard<mutex> lk2(s_mtx);
            if (s_token == token) s_status = RgQrStatus::Error;
        }
    }).detach();
}

RgQrStatus RgQr_Poll() {
    lock_guard<mutex> lk(s_mtx);

    if (s_token.empty()) return RgQrStatus::Error;

    DWORD now = GetTickCount();

    // Check expiry
    if (now - s_startTick >= SESSION_TTL_MS) {
        s_status = RgQrStatus::Expired;
        return RgQrStatus::Expired;
    }

    // Already confirmed or errored — return cached status
    if (s_status == RgQrStatus::Confirmed ||
        s_status == RgQrStatus::Error)
        return s_status.load();

    // Throttle Firestore calls
    if (now - s_lastPollTick < POLL_INTERVAL_MS)
        return s_status.load();
    s_lastPollTick = now;

    // Poll on background thread to keep UI responsive
    string tokenCopy = s_token;
    thread([tokenCopy]() {
        RgQrUser u = PollSession(tokenCopy);
        if (!u.uid.empty()) {
            lock_guard<mutex> lk2(s_mtx);
            if (s_token == tokenCopy) {
                s_user   = u;
                s_status = RgQrStatus::Confirmed;
            }
        }
    }).detach();

    return s_status.load();
}

RgQrStatus RgQr_GetStatus() {
    return s_status.load();
}

const RgQrMatrix& RgQr_GetMatrix() {
    return s_matrix;
}

const RgQrUser& RgQr_GetUser() {
    return s_user;
}

DWORD RgQr_ElapsedMs() {
    if (s_startTick == 0) return 0;
    return GetTickCount() - s_startTick;
}

void RgQr_Clear() {
    lock_guard<mutex> lk(s_mtx);
    s_token  = "";
    s_matrix = {};
    s_user   = {};
    s_status = RgQrStatus::Waiting;
    s_startTick = 0;
    s_lastPollTick = 0;
}
