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

// --- WebView2 Headers ---
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include <wrl.h>
#include <objbase.h>

using namespace Gdiplus;
using namespace std;
using namespace Microsoft::WRL; 

// =========================================================================
// PREMIUM LOCAL HTML UI (0% Lag, C++ Memory Rendered)
// FIX [C2026]: HTML string was too large for a single literal (MSVC limit: ~16380 chars).
// Split into multiple parts and joined at runtime via GetPremiumUIHtml().
// =========================================================================

static const wchar_t* HTML_PART1 = LR"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RasFocus AI - Premium UI</title>
    <style>
        * { box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
        body { margin: 0; background-color: #212121; color: #ececec; display: flex; flex-direction: column; height: 100vh; overflow: hidden; }
        #chat-history { flex: 1; overflow-y: auto; padding: 20px; display: flex; flex-direction: column; gap: 20px; }
        .msg { display: flex; max-width: 80%; line-height: 1.6; font-size: 16px; padding: 15px 20px; border-radius: 18px; white-space: pre-wrap; word-wrap: break-word; }
        .user-msg { background-color: #2f2f2f; align-self: flex-end; border-bottom-right-radius: 4px; border: 1px solid #444; }
        .ai-msg { background-color: transparent; align-self: flex-start; }
        .sent-img { max-width: 250px; border-radius: 10px; margin-top: 10px; display: block; border: 1px solid #555; }
        #input-wrapper { padding: 20px; display: flex; justify-content: center; background: linear-gradient(to top, #212121 80%, transparent); }
        .input-container { width: 100%; max-width: 800px; background-color: #2f2f2f; border-radius: 20px; padding: 12px; border: 1px solid #444; position: relative; transition: 0.2s; }
        .input-container.dragover { background-color: #3b3b3b; border-color: #00ADB5; }
        #attachment-area { display: flex; flex-wrap: wrap; gap: 10px; margin-bottom: 5px; }
        .file-card { width: 120px; height: 120px; background-color: #212121; border: 1px solid #444; border-radius: 12px; padding: 10px; position: relative; display: flex; flex-direction: column; justify-content: space-between; overflow: hidden; }
        .file-card .remove-btn { position: absolute; top: 5px; right: 5px; background: #444; color: white; border: none; border-radius: 50%; width: 22px; height: 22px; cursor: pointer; font-size: 12px; display: flex; align-items: center; justify-content: center; }
        .file-card .remove-btn:hover { background: #ff4d4d; }
        .text-card-content { font-size: 10px; color: #888; overflow: hidden; height: 70px; word-break: break-all; }
        .text-card-label { background: #333; color: #ccc; font-size: 11px; padding: 3px 8px; border-radius: 6px; align-self: flex-start; font-weight: bold; border: 1px solid #555; }
        .img-card-preview { width: 100%; height: 70px; object-fit: cover; border-radius: 6px; }
        .input-row { display: flex; align-items: flex-end; gap: 10px; }
        #add-btn { background: transparent; border: none; color: #aaa; font-size: 24px; cursor: pointer; padding: 5px 10px; display: flex; align-items: center; justify-content: center; border-radius: 50%; height: 40px; width: 40px; }
        #add-btn:hover { background: #444; color: white; }
        textarea { flex: 1; background: transparent; border: none; color: white; font-size: 16px; padding: 8px 0; resize: none; outline: none; max-height: 200px; min-height: 24px; overflow-y: auto; }
        #send-btn { background: #444; color: #aaa; border: none; padding: 8px 12px; border-radius: 10px; cursor: pointer; font-weight: bold; height: 36px; display: flex; align-items: center; justify-content: center; transition: 0.2s; }
        #send-btn.active { background: #d9d9e3; color: #111; }
        #file-input { display: none; }
        .thinking { color: #00ADB5; font-style: italic; font-size: 14px; margin-left: 10px; display: none; }
    </style>
</head>
<body>
    <div id="chat-history">
        <div class="msg ai-msg">Welcome to RasFocus Native AI! &#x1F680;<br>Powered by <b>Llama-4 Scout</b>. Drag files, paste images, or paste large code blocks to test me.</div>
    </div>
    <div id="thinking-indicator" class="thinking" style="text-align:center;">AI is analyzing... please wait.</div>
    <div id="input-wrapper">
        <div class="input-container" id="drop-zone">
            <div id="attachment-area"></div>
            <div class="input-row">
                <input type="file" id="file-input" accept="image/*" multiple>
                <button id="add-btn" onclick="document.getElementById('file-input').click()">+</button>
                <textarea id="chat-input" placeholder="Message AI..."></textarea>
                <button id="send-btn">&#x2191;</button>
            </div>
        </div>
    </div>)HTML";

static const wchar_t* HTML_PART2 = LR"HTML(
    <script>
        const API_KEY = "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";
        const MODEL_NAME = "meta-llama/llama-4-scout-17b-16e-instruct";

        const inputContainer = document.getElementById('drop-zone');
        const chatInput = document.getElementById('chat-input');
        const attachmentArea = document.getElementById('attachment-area');
        const sendBtn = document.getElementById('send-btn');
        const chatHistory = document.getElementById('chat-history');
        const thinkingIndicator = document.getElementById('thinking-indicator');

        let attachments = [];

        chatInput.addEventListener('input', function() {
            this.style.height = '24px';
            this.style.height = (this.scrollHeight) + 'px';
            toggleSendButton();
        });

        function toggleSendButton() {
            if (chatInput.value.trim().length > 0 || attachments.length > 0) {
                sendBtn.classList.add('active');
            } else {
                sendBtn.classList.remove('active');
            }
        }

        function addTextAttachment(textData) {
            attachments.push({ id: Date.now(), type: 'text', data: textData });
            renderAttachments();
        }

        function addImageAttachment(file) {
            const reader = new FileReader();
            reader.onload = function(e) {
                attachments.push({ id: Date.now(), type: 'image', data: e.target.result, file: file });
                renderAttachments();
            };
            reader.readAsDataURL(file);
        }

        chatInput.addEventListener('paste', (e) => {
            const items = (e.clipboardData || window.clipboardData).items;
            for (let item of items) {
                if (item.type.indexOf('image') !== -1) {
                    e.preventDefault();
                    addImageAttachment(item.getAsFile());
                    return;
                }
            }
            let text = (e.clipboardData || window.clipboardData).getData('text');
            if (text.length > 300) { 
                e.preventDefault(); 
                addTextAttachment(text); 
            }
        });

        inputContainer.addEventListener('dragover', (e) => { e.preventDefault(); inputContainer.classList.add('dragover'); });
        inputContainer.addEventListener('dragleave', (e) => { e.preventDefault(); inputContainer.classList.remove('dragover'); });
        inputContainer.addEventListener('drop', (e) => {
            e.preventDefault(); inputContainer.classList.remove('dragover');
            for(let file of e.dataTransfer.files) {
                if (file.type.startsWith('image/')) addImageAttachment(file);
            }
        });

        document.getElementById('file-input').addEventListener('change', function(e) {
            for(let file of this.files) { if (file.type.startsWith('image/')) addImageAttachment(file); }
            this.value = ''; 
        });

        function renderAttachments() {
            attachmentArea.innerHTML = '';
            attachments.forEach(att => {
                let innerContent = att.type === 'text' 
                    ? `<div class="text-card-content">${att.data.substring(0, 80)}...</div><div class="text-card-label">PASTED</div>`
                    : `<img src="${att.data}" class="img-card-preview"><div class="text-card-label">IMAGE</div>`;
                attachmentArea.innerHTML += `
                    <div class="file-card">
                        <button class="remove-btn" onclick="removeAttachment(${att.id})">&#x2715;</button>
                        ${innerContent}
                    </div>`;
            });
            toggleSendButton();
        }

        window.removeAttachment = function(id) {
            attachments = attachments.filter(a => a.id !== id);
            renderAttachments();
        }

        sendBtn.addEventListener('click', async () => {
            if (!sendBtn.classList.contains('active')) return;

            let promptText = chatInput.value.trim();
            let hasImages = attachments.filter(a => a.type === 'image');
            let hasTextAtts = attachments.filter(a => a.type === 'text');
            
            let finalPrompt = promptText;
            hasTextAtts.forEach(t => { finalPrompt += "\n\n[Context]:\n" + t.data; });
            if (!finalPrompt && hasImages.length > 0) finalPrompt = "Analyze this image.";

            let userMsgHtml = `<div class="msg user-msg"><div>${promptText}</div>`;
            hasTextAtts.forEach(t => { userMsgHtml += `<div style="font-size:12px; color:#888; background:#222; padding:5px; margin-top:5px; border-radius:5px;">[ Attached Text ]</div>`; });
            hasImages.forEach(img => { userMsgHtml += `<img src="${img.data}" class="sent-img">`; });
            userMsgHtml += `</div>`;
            
            chatHistory.innerHTML += userMsgHtml;
            chatHistory.scrollTop = chatHistory.scrollHeight;

            chatInput.value = '';
            chatInput.style.height = '24px';
            attachments = [];
            renderAttachments();
            thinkingIndicator.style.display = "block";

            let messagesPayload = [];
            if (hasImages.length > 0) {
                let contentArray = [{ type: "text", text: finalPrompt }];
                hasImages.forEach(img => {
                    contentArray.push({ type: "image_url", image_url: { url: img.data } });
                });
                messagesPayload.push({ role: "user", content: contentArray });
            } else {
                messagesPayload.push({ role: "user", content: finalPrompt });
            }

            try {
                const response = await fetch("https://api.groq.com/openai/v1/chat/completions", {
                    method: "POST",
                    headers: { "Authorization": `Bearer ${API_KEY}`, "Content-Type": "application/json" },
                    body: JSON.stringify({ model: MODEL_NAME, messages: messagesPayload })
                });

                const data = await response.json();
                thinkingIndicator.style.display = "none";

                if (response.ok) {
                    chatHistory.innerHTML += `<div class="msg ai-msg">${data.choices[0].message.content}</div>`;
                } else {
                    chatHistory.innerHTML += `<div class="msg ai-msg" style="color:#ff4d4d;">Error: ${JSON.stringify(data.error.message)}</div>`;
                }
            } catch (error) {
                thinkingIndicator.style.display = "none";
                chatHistory.innerHTML += `<div class="msg ai-msg" style="color:#ff4d4d;">Network Error.</div>`;
            }
            chatHistory.scrollTop = chatHistory.scrollHeight;
        });

        chatInput.addEventListener("keydown", function(e) {
            if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); sendBtn.click(); }
        });
    </script>
</body>
</html>)HTML";

// Helper function to build the full HTML string at runtime.
// Call this instead of using PREMIUM_UI_HTML directly.
static std::wstring GetPremiumUIHtml() {
    return std::wstring(HTML_PART1) + HTML_PART2;
}

// --- Global States ---
static float s_contentX = 0, s_contentY = 0, s_contentW = 800, s_contentH = 600;
extern HWND hParentWnd; 
extern float g_scaleFactor; 

static int g_webViewMode = 0; // 0 = Home, 1 = Local Premium AI, 2 = Web Browser
static bool hoverLaunchBtn = false;
static bool hoverChatLaunchBtn = false; 
static bool hoverCloseBtn = false;
static bool hoverBackBtn = false;
static bool hoverHomeBtn = false; 
static bool hoverPopOutBtn = false;
static bool isPoppedOut = false;   
static HWND hPopOutWnd = NULL;     

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

// [KEEP PopOutWndProc EXACTLY SAME AS BEFORE]
LRESULT CALLBACK PopOutWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hWnd, message, wParam, lParam);
}

// --- Controller Completed Handler ---
class ControllerCompletedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        static const IID IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler_Local = { 0x6c4819f3, 0xc9b7, 0x4260, { 0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c } };
        // FIX [C2065]: 'IID_IUnknown_Local' was undeclared. Use standard IID_IUnknown from <objbase.h>.
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler_Local) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        if (controller != nullptr) {
            webViewController = controller;
            webViewController->get_CoreWebView2(&webView);
            webViewController->put_IsVisible(TRUE);

            // Load Content based on Selected Mode
            if (g_webViewMode == 1) {
                std::wstring html = GetPremiumUIHtml();
                webView->NavigateToString(html.c_str());
            } else if (g_webViewMode == 2) {
                webView->Navigate(L"https://gemini.google.com/?authuser=0");
            }
        }
        return S_OK;
    }
};

class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    ULONG m_refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        static const IID IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler_Local = { 0x4e8a3389, 0xc9d8, 0x4bd2, { 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d } };
        // FIX [C2065]: 'IID_IUnknown_Local' was undeclared. Use standard IID_IUnknown from <objbase.h>.
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler_Local) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG r = InterlockedDecrement(&m_refCount); if (r == 0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override {
        if (env != nullptr) {
            env->CreateCoreWebView2Controller(hParentWnd, new ControllerCompletedHandler());
        }
        return S_OK;
    }
};

// =========================================================================
// Main UI Functions
// =========================================================================

void InitGeminiControls(HWND parent) { hParentWnd = parent; }

void ShowGeminiControls(bool show) {
    if (show && hParentWnd != NULL && !isPoppedOut) { InvalidateRect(hParentWnd, NULL, TRUE); }
    if (webViewController != nullptr && !isPoppedOut) { webViewController->put_IsVisible((show && g_webViewMode > 0) ? TRUE : FALSE); }
}

void ResizeGeminiControls(int cx, int cy, int cw, int ch) {
    s_contentX = (float)cx; s_contentY = (float)cy; s_contentW = (float)cw; s_contentH = (float)ch;
    
    if (webViewController != nullptr && g_webViewMode > 0 && !isPoppedOut) {
        RECT bounds;
        bounds.left = (LONG)(cx * g_scaleFactor);
        bounds.top = (LONG)((cy + 30) * g_scaleFactor); 
        bounds.right = (LONG)((cx + cw) * g_scaleFactor);
        bounds.bottom = (LONG)((cy + ch) * g_scaleFactor);
        webViewController->put_Bounds(bounds);
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
    
    StringFormat fC; fC.SetAlignment(StringAlignmentCenter); fC.SetLineAlignment(StringAlignmentCenter);

    g.FillRectangle(&bBg, cx, cy, cw, ch);

    // --- STATE 1: HOME MENU ---
    if (g_webViewMode == 0) {
        g.DrawString(L"RasFocus AI Hub", -1, &fH1, RectF(cx, cy + (ch/2) - 130, cw, 40), &fC, &bText);
        g.DrawString(L"Select an option below", -1, &fNormal, RectF(cx, cy + (ch/2) - 90, cw, 30), &fC, &bText);

        float btnW = 280.0f; float btnH = 50.0f;
        float btnX = cx + (cw - btnW) / 2.0f; 
        
        float btnY1 = cy + (ch / 2.0f) - 30.0f;
        RectF btnRect1(btnX, btnY1, btnW, btnH);
        GraphicsPath* bp1 = GetGeminiRoundRect(btnRect1, 25);
        SolidBrush btnBrush1(hoverChatLaunchBtn ? GClrTealHover : GClrAppTeal);
        g.FillPath(&btnBrush1, bp1); delete bp1;
        g.DrawString(L"Chat with AI (Premium UI)", -1, &fBold, btnRect1, &fC, &bWhite);

        float btnY2 = btnY1 + 70.0f;
        RectF btnRect2(btnX, btnY2, btnW, btnH);
        GraphicsPath* bp2 = GetGeminiRoundRect(btnRect2, 25);
        SolidBrush btnBrush2(hoverLaunchBtn ? GClrTealHover : Color(255, 100, 100, 100)); 
        g.FillPath(&btnBrush2, bp2); delete bp2;
        g.DrawString(L"Open AI Web Browser", -1, &fBold, btnRect2, &fC, &bWhite);
    } 
    // --- STATE 2 & 3: RUNNING MODE ---
    else if (!isPoppedOut) {
        SolidBrush bNavBg(GClrAppTeal);
        g.FillRectangle(&bNavBg, cx, cy, cw, 30.0f); 

        float startX = cx + 5; 
        
        // Back to Menu Button
        RectF homeRect(startX, cy + 2, 30, 26); SolidBrush bHome(hoverHomeBtn ? GClrTealHover : GClrAppTeal);
        g.FillRectangle(&bHome, homeRect); g.DrawString(L"\xE80F", -1, &fIcons, homeRect, &fC, &bWhite); 

        // Close Button
        RectF closeRect(cx + cw - 35, cy + 2, 30, 26); SolidBrush bClose(hoverCloseBtn ? GClrDanger : Color(255, 180, 40, 40));
        g.FillRectangle(&bClose, closeRect); g.DrawString(L"\xE8BB", -1, &fIcons, closeRect, &fC, &bWhite); 
    }
}

void ProcessGeminiMouseMove(float x, float y) {
    if (g_webViewMode == 0) {
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
    } else if (!isPoppedOut) {
        bool prevHome = hoverHomeBtn; bool prevClose = hoverCloseBtn;
        hoverHomeBtn = RectF(s_contentX + 5, s_contentY + 2, 30, 26).Contains(x, y);
        hoverCloseBtn = RectF(s_contentX + s_contentW - 35, s_contentY + 2, 30, 26).Contains(x, y);

        if (prevHome != hoverHomeBtn || prevClose != hoverCloseBtn) {
            if (hParentWnd != NULL) InvalidateRect(hParentWnd, NULL, TRUE);
        }
    }
}

void ProcessGeminiMouseClick(float x, float y) {
    if (g_webViewMode == 0) {
        float btnW = 280.0f; float btnH = 50.0f;
        float btnX = s_contentX + (s_contentW - btnW) / 2.0f; 
        float btnY1 = s_contentY + (s_contentH / 2.0f) - 30.0f;
        float btnY2 = btnY1 + 70.0f;

        if (RectF(btnX, btnY1, btnW, btnH).Contains(x, y)) {
            g_webViewMode = 1; // Local AI Mode
        } else if (RectF(btnX, btnY2, btnW, btnH).Contains(x, y)) {
            g_webViewMode = 2; // Web Browser Mode
        }

        if (g_webViewMode > 0) {
            if (webView == nullptr) {
                CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
                std::wstring userDataFolder = L"C:\\Users\\" + std::wstring(_wgetenv(L"USERNAME")) + L"\\AppData\\Local\\RasFocus\\User_Data";
                auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
                CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), options.Get(), new EnvCompletedHandler());
            } else {
                if (g_webViewMode == 1) {
                    std::wstring html = GetPremiumUIHtml();
                    webView->NavigateToString(html.c_str());
                }
                else webView->Navigate(L"https://gemini.google.com/?authuser=0");
                webViewController->put_IsVisible(TRUE);
            }
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        }
    } 
    else if (!isPoppedOut) {
        // Home Button (Go back to Menu)
        if (RectF(s_contentX + 5, s_contentY + 2, 30, 26).Contains(x, y)) {
            g_webViewMode = 0;
            if (webViewController) webViewController->put_IsVisible(FALSE);
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        }
        // Close Button
        else if (RectF(s_contentX + s_contentW - 35, s_contentY + 2, 30, 26).Contains(x, y)) {
            if (webViewController != nullptr) { webViewController->Close(); webViewController = nullptr; webView = nullptr; }
            g_webViewMode = 0;
            if (hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        }
    }
}

void ProcessGeminiCommand(int id, int code) {}