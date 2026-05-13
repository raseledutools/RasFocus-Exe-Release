// tab_diary.cpp 

#include "tab_gemini.h" 
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cstdint>
#include <commdlg.h> 
#include <urlmon.h>
#include <process.h>
#include <shlwapi.h>
#include <algorithm>
#include <wininet.h>  
#include <thread>     
#include <fstream>
#include <sstream>

#pragma comment(lib, "wininet.lib") 

// --- WebView2 Headers ---
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include <wrl.h>
#include <objbase.h>

using namespace Gdiplus;
using namespace std;
using namespace Microsoft::WRL; 

// --- Base64 Encoder Function for Images ---
static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
    std::string ret;
    int i = 0, j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for(i = 0; (i <4) ; i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for(j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; (j < i + 1); j++) ret += base64_chars[char_array_4[j]];
        while((i++ < 3)) ret += '=';
    }
    return ret;
}

// --- Global States ---
static float s_contentX = 0, s_contentY = 0, s_contentW = 800, s_contentH = 600;
extern HWND hParentWnd; 
extern float g_scaleFactor; 
static bool g_controlsVisible = false;

static bool hoverLaunchBtn = false;
static bool hoverChatLaunchBtn = false; 
static bool hoverCloseBtn = false;
static bool hoverBackBtn = false;

static bool isGeminiRunning = false; 
static bool isAIChatRunning = false; 
static bool isPoppedOut = false;   
static HWND hPopOutWnd = NULL;     

// --- Native Chat Controls & Data ---
static HWND hChatHistoryBox = NULL; // ChatGPT Style History Box
static HWND hChatEdit = NULL;       // Input Box
static HWND hChatSendBtn = NULL;    // Send Button
static HWND hChatAttachBtn = NULL;  // Upload Button
static std::wstring g_aiChatHistory = L"RasFocus AI Assistant\n================================\nWelcome! Type your message or attach an image below...\n";
static std::wstring g_selectedImagePath = L""; 
static bool isAiThinking = false;

// --- WebView2 Global Pointers ---
static ComPtr<ICoreWebView2Controller> webViewController;
static ComPtr<ICoreWebView2> webView;

// --- Colors ---
static const Color GClrWhite(255, 255, 255, 255);    
static const Color GClrAppTeal(255, 12, 168, 176);   
static const Color GClrTealHover(255, 30, 185, 195); 
static const Color GClrTextDark(255, 40, 40, 40);    
static const Color GClrDanger(255, 230, 60, 60);     

static GraphicsPath* GetGeminiRoundRect(RectF rect, int radius) {
    GraphicsPath* path = new GraphicsPath();
    float d = radius * 2.0f;
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure(); return path;
}

// =========================================================================
// API Request Thread Function (Vision & Text)
// =========================================================================
void SendGroqChatRequestAsync(std::wstring prompt, std::wstring imgPath) {
    isAiThinking = true;
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);

    std::string promptStr(prompt.begin(), prompt.end());
    std::string jsonData;

    if (imgPath.empty()) {
        jsonData = "{\"model\":\"llama3-8b-8192\",\"messages\":[{\"role\":\"user\",\"content\":\"" + promptStr + "\"}]}";
    } else {
        std::ifstream file(imgPath, std::ios::binary);
        if (file) {
            std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(file), {});
            std::string base64_str = base64_encode(buffer.data(), buffer.size());
            std::string mimeType = "image/jpeg"; 
            if (imgPath.find(L".png") != std::wstring::npos) mimeType = "image/png";

            jsonData = "{\"model\":\"llama-3.2-11b-vision-preview\",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"" + promptStr + "\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:" + mimeType + ";base64," + base64_str + "\"}}]}]}";
        } else {
            g_aiChatHistory += L"\n\n[Error: Failed to read attached image]";
            SetWindowTextW(hChatHistoryBox, g_aiChatHistory.c_str());
            isAiThinking = false;
            return;
        }
    }

    HINTERNET hSession = InternetOpen(L"RasFocusClient/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    HINTERNET hConnect = InternetConnect(hSession, L"api.groq.com", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    HINTERNET hRequest = HttpOpenRequest(hConnect, L"POST", L"/openai/v1/chat/completions", NULL, NULL, NULL, INTERNET_FLAG_SECURE, 0);

    // TODO: Put your Groq API key here!
    std::wstring headers = L"Authorization: Bearer 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\r\nContent-Type: application/json\r\n";
    HttpAddRequestHeaders(hRequest, headers.c_str(), -1, HTTP_ADDREQ_FLAG_ADD);

    if (HttpSendRequest(hRequest, NULL, 0, (LPVOID)jsonData.c_str(), jsonData.length())) {
        std::string response = "";
        char buffer[4096];
        DWORD bytesRead = 0;
        while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            response += buffer;
        }

        size_t startPos = response.find("\"content\":\"");
        if (startPos != std::string::npos) {
            startPos += 11;
            size_t endPos = response.find("\"", startPos);
            std::string content = response.substr(startPos, endPos - startPos);
            
            size_t pos = 0;
            while ((pos = content.find("\\n", pos)) != std::string::npos) {
                content.replace(pos, 2, "\n"); pos += 1;
            }
            std::wstring wContent(content.begin(), content.end());
            g_aiChatHistory += L"\n\nAI: " + wContent + L"\n================================";
        } else {
            g_aiChatHistory += L"\n\n[Error: Failed to parse API response]";
        }
    } else {
        g_aiChatHistory += L"\n\n[Error: Check Internet Connection or API Key]";
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hSession);
    
    // Update Chat Box and Auto-Scroll to bottom
    if (hChatHistoryBox) {
        SetWindowTextW(hChatHistoryBox, g_aiChatHistory.c_str());
        SendMessage(hChatHistoryBox, WM_VSCROLL, SB_BOTTOM, 0);
    }
    
    isAiThinking = false;
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
}

// [Keep WebView2 Handlers empty/default if needed, removed for clean UI focus]
LRESULT CALLBACK PopOutWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hWnd, message, wParam, lParam);
}

// =========================================================================
// Main UI Functions
// =========================================================================

void InitGeminiControls(HWND parent) { hParentWnd = parent; }

void ShowGeminiControls(bool show) {
    g_controlsVisible = show;
    if (show && hParentWnd != NULL && !isPoppedOut) { InvalidateRect(hParentWnd, NULL, TRUE); }
    
    if (webViewController != nullptr && !isPoppedOut) { 
        webViewController->put_IsVisible((show && isGeminiRunning) ? TRUE : FALSE); 
    }

    if (hChatHistoryBox && hChatEdit && hChatSendBtn && hChatAttachBtn) {
        ShowWindow(hChatHistoryBox, (show && isAIChatRunning) ? SW_SHOW : SW_HIDE);
        ShowWindow(hChatEdit, (show && isAIChatRunning) ? SW_SHOW : SW_HIDE);
        ShowWindow(hChatSendBtn, (show && isAIChatRunning) ? SW_SHOW : SW_HIDE);
        ShowWindow(hChatAttachBtn, (show && isAIChatRunning) ? SW_SHOW : SW_HIDE);
    }
}

void ResizeGeminiControls(int cx, int cy, int cw, int ch) {
    s_contentX = (float)cx; s_contentY = (float)cy; s_contentW = (float)cw; s_contentH = (float)ch;
    
    if (webViewController != nullptr && isGeminiRunning && !isPoppedOut) {
        RECT bounds;
        bounds.left = (LONG)(cx * g_scaleFactor);
        bounds.top = (LONG)((cy + 30) * g_scaleFactor); 
        bounds.right = (LONG)((cx + cw) * g_scaleFactor);
        bounds.bottom = (LONG)((cy + ch) * g_scaleFactor);
        webViewController->put_Bounds(bounds);
    }

    // --- CHATGPT STYLE PERFECT LAYOUT ---
    if (isAIChatRunning && hChatHistoryBox && hChatEdit && hChatSendBtn && hChatAttachBtn) {
        int padding = 20;
        int topBarHeight = 50;
        int bottomHeight = 45; // Height of input box
        int bottomY = (int)(s_contentY + s_contentH - bottomHeight - padding); // Placed safely at the bottom

        // History Box (Takes up middle screen)
        SetWindowPos(hChatHistoryBox, NULL, 
            (int)(s_contentX + padding), 
            (int)(s_contentY + topBarHeight + padding), 
            (int)(s_contentW - padding * 2), 
            (int)(s_contentH - topBarHeight - bottomHeight - padding * 3), 
            SWP_NOZORDER);

        // Bottom Controls (Upload | Input Box | Send)
        SetWindowPos(hChatAttachBtn, NULL, (int)(s_contentX + padding), bottomY, 90, bottomHeight, SWP_NOZORDER);
        SetWindowPos(hChatEdit, NULL, (int)(s_contentX + padding + 100), bottomY, (int)(s_contentW - padding * 2 - 200), bottomHeight, SWP_NOZORDER);
        SetWindowPos(hChatSendBtn, NULL, (int)(s_contentX + s_contentW - padding - 90), bottomY, 90, bottomHeight, SWP_NOZORDER);
    }
}

void DrawGeminiTab(Graphics& g, float cx, float cy, float cw, float ch) {
    s_contentX = cx; s_contentY = cy; s_contentW = cw; s_contentH = ch;

    FontFamily ff(L"Segoe UI"); 
    FontFamily ffIcon(L"Segoe MDL2 Assets"); 
    Font fH1(&ff, 28, FontStyleBold, UnitPixel); 
    Font fBold(&ff, 14, FontStyleBold, UnitPixel);
    Font fNormal(&ff, 14, FontStyleRegular, UnitPixel); 
    Font fIcons(&ffIcon, 14, FontStyleRegular, UnitPixel); 
    
    SolidBrush bBg(GClrWhite); 
    SolidBrush bText(GClrTextDark); 
    SolidBrush bWhite(GClrWhite);
    SolidBrush bTeal(GClrAppTeal);
    
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // --- STATE 1: MAIN MENU ---
    if (!isGeminiRunning && !isAIChatRunning) {
        g.DrawString(L"RasFocus AI Hub", -1, &fH1, RectF(cx, cy + (ch/2) - 130, cw, 40), &fC, &bText);
        g.DrawString(L"Select an option to start", -1, &fNormal, RectF(cx, cy + (ch/2) - 90, cw, 30), &fC, &bText);

        float btnW = 280.0f; float btnH = 50.0f;
        float btnX = cx + (cw - btnW) / 2.0f; 
        
        float btnY1 = cy + (ch / 2.0f) - 30.0f;
        RectF btnRect1(btnX, btnY1, btnW, btnH);
        GraphicsPath* bp1 = GetGeminiRoundRect(btnRect1, 25);
        SolidBrush btnBrush1(hoverChatLaunchBtn ? GClrTealHover : GClrAppTeal);
        g.FillPath(&btnBrush1, bp1); delete bp1;
        g.DrawString(L"Chat with AI (Vision & Text)", -1, &fBold, btnRect1, &fC, &bWhite);

        float btnY2 = btnY1 + 70.0f;
        RectF btnRect2(btnX, btnY2, btnW, btnH);
        GraphicsPath* bp2 = GetGeminiRoundRect(btnRect2, 25);
        SolidBrush btnBrush2(hoverLaunchBtn ? GClrTealHover : Color(255, 100, 100, 100)); 
        g.FillPath(&btnBrush2, bp2); delete bp2;
        g.DrawString(L"Open AI Web Browser", -1, &fBold, btnRect2, &fC, &bWhite);
    } 
    
    // --- STATE 2: NATIVE API CHAT INTERFACE ---
    else if (isAIChatRunning) {
        SolidBrush bNavBg(GClrAppTeal);
        g.FillRectangle(&bNavBg, cx, cy, cw, 50.0f); 

        RectF backRect(cx + 10, cy + 10, 30, 30); 
        SolidBrush bBack(hoverBackBtn ? GClrDanger : GClrAppTeal);
        g.FillRectangle(&bBack, backRect); 
        g.DrawString(L"\xE72B", -1, &fIcons, backRect, &fC, &bWhite); 
        
        g.DrawString(L"RasFocus Native AI (Llama-3)", -1, &fBold, RectF(cx + 50, cy, cw - 100, 50), &fC, &bWhite);

        // Indicator when thinking
        if (isAiThinking) {
            g.DrawString(L"AI is typing...", -1, &fNormal, RectF(cx + cw - 120, cy, 100, 50), &fC, &bWhite);
        }
    }

    // --- STATE 3: WEBVIEW2 BROWSER ---
    else if (isGeminiRunning && !isPoppedOut) {
        SolidBrush bNavBg(GClrAppTeal);
        g.FillRectangle(&bNavBg, cx, cy, cw, 30.0f); 
        RectF closeRect(cx + cw - 35, cy + 2, 30, 26); SolidBrush bClose(hoverCloseBtn ? GClrDanger : Color(255, 180, 40, 40));
        g.FillRectangle(&bClose, closeRect); g.DrawString(L"\xE8BB", -1, &fIcons, closeRect, &fC, &bWhite); 
    }
}

void ProcessGeminiMouseMove(float x, float y) {
    if (!isGeminiRunning && !isAIChatRunning) {
        float btnW = 280.0f; float btnH = 50.0f;
        float btnX = s_contentX + (s_contentW - btnW) / 2.0f; 
        float btnY1 = s_contentY + (s_contentH / 2.0f) - 30.0f;
        float btnY2 = btnY1 + 70.0f;

        bool prevChat = hoverChatLaunchBtn; bool prevWeb = hoverLaunchBtn;
        hoverChatLaunchBtn = RectF(btnX, btnY1, btnW, btnH).Contains(x, y);
        hoverLaunchBtn = RectF(btnX, btnY2, btnW, btnH).Contains(x, y);

        if ((prevChat != hoverChatLaunchBtn || prevWeb != hoverLaunchBtn) && hParentWnd != NULL) { 
            InvalidateRect(hParentWnd, NULL, TRUE); 
        }
    } 
    else if (isAIChatRunning) {
        bool prevBack = hoverBackBtn;
        hoverBackBtn = RectF(s_contentX + 10, s_contentY + 10, 30, 30).Contains(x, y);
        if (prevBack != hoverBackBtn && hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
    }
    else if (isGeminiRunning && !isPoppedOut) {
        hoverCloseBtn = RectF(s_contentX + s_contentW - 35, s_contentY + 2, 30, 26).Contains(x, y);
    }
}

void ProcessGeminiMouseClick(float x, float y) {
    if (!isGeminiRunning && !isAIChatRunning) {
        float btnW = 280.0f; float btnH = 50.0f;
        float btnX = s_contentX + (s_contentW - btnW) / 2.0f; 
        float btnY1 = s_contentY + (s_contentH / 2.0f) - 30.0f;
        float btnY2 = btnY1 + 70.0f;

        if (RectF(btnX, btnY1, btnW, btnH).Contains(x, y)) {
            isAIChatRunning = true;
            
            if (!hChatHistoryBox) {
                // Read-Only Chat History Window with Scrollbar
                hChatHistoryBox = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", 
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 
                    0, 0, 0, 0, hParentWnd, (HMENU)2004, GetModuleHandle(NULL), NULL);

                hChatAttachBtn = CreateWindowEx(0, "BUTTON", "+ Image", 
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                    0, 0, 0, 0, hParentWnd, (HMENU)2003, GetModuleHandle(NULL), NULL);

                hChatEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", 
                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL, 
                    0, 0, 0, 0, hParentWnd, (HMENU)2001, GetModuleHandle(NULL), NULL);
                
                hChatSendBtn = CreateWindowEx(0, "BUTTON", "Send", 
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                    0, 0, 0, 0, hParentWnd, (HMENU)2002, GetModuleHandle(NULL), NULL);
                
                HFONT hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
                SendMessage(hChatHistoryBox, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessage(hChatEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessage(hChatSendBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessage(hChatAttachBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
                
                SetWindowTextW(hChatHistoryBox, g_aiChatHistory.c_str());
            } else {
                ShowWindow(hChatHistoryBox, SW_SHOW);
                ShowWindow(hChatAttachBtn, SW_SHOW);
                ShowWindow(hChatEdit, SW_SHOW); 
                ShowWindow(hChatSendBtn, SW_SHOW);
            }
            ResizeGeminiControls((int)s_contentX, (int)s_contentY, (int)s_contentW, (int)s_contentH);
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        }

        else if (RectF(btnX, btnY2, btnW, btnH).Contains(x, y)) {
            isGeminiRunning = true;
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        }
    } 
    else if (isAIChatRunning) {
        if (RectF(s_contentX + 10, s_contentY + 10, 30, 30).Contains(x, y)) {
            isAIChatRunning = false;
            g_selectedImagePath = L""; 
            ShowWindow(hChatHistoryBox, SW_HIDE);
            ShowWindow(hChatAttachBtn, SW_HIDE);
            ShowWindow(hChatEdit, SW_HIDE);
            ShowWindow(hChatSendBtn, SW_HIDE);
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        }
    }
    else if (isGeminiRunning && !isPoppedOut) {
        if (RectF(s_contentX + s_contentW - 35, s_contentY + 2, 30, 26).Contains(x, y)) {
            isGeminiRunning = false;
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        }
    }
}

// Ensure your main.cpp routes WM_COMMAND to this function!
void ProcessGeminiCommand(int id, int code) {
    if (isAIChatRunning) {
        // 1. Upload Button
        if (id == 2003 && code == BN_CLICKED) {
            OPENFILENAMEW ofn;
            wchar_t szFile[MAX_PATH] = { 0 };
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hParentWnd;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile);
            ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.webp\0All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

            if (GetOpenFileNameW(&ofn) == TRUE) {
                g_selectedImagePath = szFile;
                g_aiChatHistory += L"\n\n[ System: Image selected. Ready to send. ]";
                SetWindowTextW(hChatHistoryBox, g_aiChatHistory.c_str());
                SendMessage(hChatHistoryBox, WM_VSCROLL, SB_BOTTOM, 0);
            }
        }
        
        // 2. Send Button
        else if (id == 2002 && code == BN_CLICKED) {
            wchar_t buffer[2048];
            GetWindowTextW(hChatEdit, buffer, 2048);
            
            if (wcslen(buffer) > 0 || !g_selectedImagePath.empty()) {
                SetWindowTextW(hChatEdit, L"");
                
                std::wstring prompt(buffer);
                if (prompt.empty()) prompt = L"What is in this image?"; 
                
                g_aiChatHistory += L"\n\nYou: " + prompt + (g_selectedImagePath.empty() ? L"" : L" [Sent with Image]");
                SetWindowTextW(hChatHistoryBox, g_aiChatHistory.c_str());
                SendMessage(hChatHistoryBox, WM_VSCROLL, SB_BOTTOM, 0);
                
                std::wstring currentImg = g_selectedImagePath;
                g_selectedImagePath = L""; 
                
                std::thread apiThread(SendGroqChatRequestAsync, prompt, currentImg);
                apiThread.detach();
            }
        }
    }
}