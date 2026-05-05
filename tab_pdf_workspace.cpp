#include "tab_pdf_workspace.h"
#include <shobjidl.h>
#include <iostream>
#include <thread>
#include <wrl.h>
#include <wil/com.h>
#include <WebView2.h>

using namespace Gdiplus;
using namespace std;
using namespace Microsoft::WRL;

extern HWND hParentWnd;
extern float g_scaleFactor;

// 🟢 Global variables
extern wstring currentWorkspacePdf;

// --- WebView2 Engine ---
extern HWND hPdfWebView;
static wil::com_ptr<ICoreWebView2Environment> g_webViewEnv;
static wil::com_ptr<ICoreWebView2Controller> g_webViewController;
static wil::com_ptr<ICoreWebView2> g_webView;
bool isPdfLoaded = false;
bool isWebViewReady = false;

// --- Button Bounds ---
static RectF btnOpen, btnEdit, btnOrganize, btnMerge, btnSplit, btnCompress, btnProtect, btnExport, btnOCR, btnAIChat, btnBatch;
static int pdfWorkspaceHover = 0;

// ==========================================
// WEBVIEW2 INITIALIZATION
// ==========================================
void InitializeWebView2(HWND hwnd) {
    if (g_webViewEnv) return; // Already initialized

    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) {
                    MessageBoxW(hwnd, L"Failed to create WebView2 environment", L"Error", MB_OK);
                    return result;
                }

                g_webViewEnv = env;
                
                env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result)) {
                                MessageBoxW(hwnd, L"Failed to create WebView2 controller", L"Error", MB_OK);
                                return result;
                            }

                            g_webViewController = controller;
                            g_webViewController->get_CoreWebView2(&g_webView);
                            
                            // Configure WebView2
                            wil::com_ptr<ICoreWebView2Settings> settings;
                            g_webView->get_Settings(&settings);
                            if (settings) {
                                settings->put_IsScriptEnabled(TRUE);
                                settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                                settings->put_IsWebMessageEnabled(TRUE);
                            }
                            
                            isWebViewReady = true;
                            
                            // If PDF is already loaded, display it
                            if (!currentWorkspacePdf.empty()) {
                                wstring pdfUrl = L"file:///" + currentWorkspacePdf;
                                g_webView->Navigate(pdfUrl.c_str());
                            }
                            
                            return S_OK;
                        }).Get());
                
                return S_OK;
            }).Get());
}

// --- Helper: Draw Sidebar Button ---
static void DrawSidebarButton(Graphics& g, RectF bounds, wstring text, wstring icon, int hoverCode, int currentHover, bool isDisabled = false) {
    Color bgColor = isDisabled ? Color(255, 50, 55, 65) : (currentHover == hoverCode ? Color(255, 12, 168, 176) : Color(255, 30, 40, 55));
    SolidBrush bgBrush(bgColor);
    g.FillRectangle(&bgBrush, bounds);

    FontFamily ff(L"Segoe UI");
    Font fText(&ff, 13.0f * g_scaleFactor, FontStyleBold, UnitPixel);
    
    FontFamily ffIcon(L"Segoe MDL2 Assets");
    Font fIcon(&ffIcon, 15.0f * g_scaleFactor, FontStyleRegular, UnitPixel);
    
    StringFormat fmt; 
    fmt.SetAlignment(StringAlignmentNear); 
    fmt.SetLineAlignment(StringAlignmentCenter);

    Color textColor = isDisabled ? Color(255, 100, 110, 120) : Color(255, 255, 255, 255);
    SolidBrush textBrush(textColor);
    
    // Draw Icon
    RectF iconRect = bounds;
    iconRect.X += 12.0f * g_scaleFactor;
    g.DrawString(icon.c_str(), -1, &fIcon, iconRect, &fmt, &textBrush);

    // Draw Text
    RectF textRect = bounds;
    textRect.X += 40.0f * g_scaleFactor;
    g.DrawString(text.c_str(), -1, &fText, textRect, &fmt, &textBrush);
}

// --- Helper: Draw Section Header ---
static void DrawSectionHeader(Graphics& g, wstring text, float x, float y, float w) {
    FontFamily ff(L"Segoe UI");
    Font fText(&ff, 11.0f * g_scaleFactor, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 130, 145, 165));
    StringFormat fmt; fmt.SetAlignment(StringAlignmentNear); fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text.c_str(), -1, &fText, RectF(x, y, w, 18.0f * g_scaleFactor), &fmt, &textBrush);
}

// --- Edge Engine Area Calculator ---
RECT GetPdfWebViewArea(float cx, float cy, float cw, float ch) {
    float sidebarW = 280.0f * g_scaleFactor; 
    RECT r;
    r.left = (LONG)(cx + sidebarW);
    r.top = (LONG)cy;
    r.right = (LONG)(cx + cw);
    r.bottom = (LONG)(cy + ch);
    return r;
}

// --- Fast PDF Loading Function ---
void LoadPdfFast(const wstring& filePath) {
    if (filePath.empty()) return;
    
    currentWorkspacePdf = filePath;
    isPdfLoaded = true;
    
    // If WebView2 is ready, navigate to PDF
    if (isWebViewReady && g_webView) {
        // Convert file path to URI format
        wstring pdfUrl = L"file:///" + filePath;
        // Replace backslashes with forward slashes
        for (size_t i = 0; i < pdfUrl.length(); i++) {
            if (pdfUrl[i] == L'\\') pdfUrl[i] = L'/';
        }
        g_webView->Navigate(pdfUrl.c_str());
    }
    
    // Refresh UI
    if (hParentWnd) {
        InvalidateRect(hParentWnd, NULL, TRUE);
    }
}

// ==========================================
// 🎨 MAIN DRAWING FUNCTION
// ==========================================
void DrawPdfWorkspaceTab(Graphics& g, float cx, float cy, float cw, float ch) {
    float sidebarW = 280.0f * g_scaleFactor;

    // Right Side - PDF Viewer Area
    RECT webViewRect = GetPdfWebViewArea(cx, cy, cw, ch);
    
    // Initialize WebView2 with correct parent window
    static bool initialized = false;
    if (!initialized) {
        // Create a child window for WebView2
        hPdfWebView = CreateWindowExW(
            0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            webViewRect.left, webViewRect.top,
            webViewRect.right - webViewRect.left,
            webViewRect.bottom - webViewRect.top,
            hParentWnd, NULL, GetModuleHandle(NULL), NULL
        );
        
        if (hPdfWebView) {
            InitializeWebView2(hPdfWebView);
            initialized = true;
        }
    } else if (hPdfWebView) {
        // Update WebView2 position if window resized
        SetWindowPos(hPdfWebView, NULL,
            webViewRect.left, webViewRect.top,
            webViewRect.right - webViewRect.left,
            webViewRect.bottom - webViewRect.top,
            SWP_NOZORDER);
        
        // Update WebView2 controller bounds
        if (g_webViewController) {
            RECT bounds;
            GetClientRect(hPdfWebView, &bounds);
            g_webViewController->put_Bounds(bounds);
        }
    }

    // If no PDF loaded, show placeholder
    if (currentWorkspacePdf.empty()) {
        SolidBrush bgRight(Color(255, 240, 243, 248));
        g.FillRectangle(&bgRight, cx + sidebarW, cy, cw - sidebarW, ch);
        
        FontFamily ff(L"Segoe UI");
        Font fEmpty(&ff, 16.0f * g_scaleFactor, FontStyleBold, UnitPixel);
        SolidBrush txtEmpty(Color(255, 160, 170, 180));
        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"No PDF Selected\nClick 'Open Document' to start viewing PDFs", -1, &fEmpty, 
                     RectF(cx + sidebarW, cy, cw - sidebarW, ch), &fmtC, &txtEmpty);
    }

    // Left Sidebar
    SolidBrush bgSidebar(Color(255, 20, 25, 35)); 
    g.FillRectangle(&bgSidebar, cx, cy, sidebarW, ch);

    // Sidebar Title
    FontFamily ffTitle(L"Segoe UI");
    Font fTitle(&ffTitle, 22.0f * g_scaleFactor, FontStyleBold, UnitPixel);
    SolidBrush txtTitle(Color(255, 255, 255, 255));
    StringFormat fmt; fmt.SetAlignment(StringAlignmentNear); fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"Ultimate PDF Studio", -1, &fTitle, 
                 RectF(cx + 15.0f * g_scaleFactor, cy + 15.0f * g_scaleFactor, sidebarW, 40.0f * g_scaleFactor), 
                 &fmt, &txtTitle);

    // Sidebar Buttons
    float btnY = cy + 65.0f * g_scaleFactor;
    float btnH = 36.0f * g_scaleFactor; 
    float btnGap = 4.0f * g_scaleFactor;
    float sectionGap = 12.0f * g_scaleFactor;
    float padding = 12.0f * g_scaleFactor;
    float btnW = sidebarW - (padding * 2);

    // --- SECTION 1: VIEW & EDIT ---
    DrawSectionHeader(g, L"VIEW & EDIT", cx + padding, btnY, btnW);
    btnY += 22.0f * g_scaleFactor;

    btnOpen = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnOpen, L"Open Document", L"\xE8E5", 1, pdfWorkspaceHover, false);
    btnY += btnH + btnGap;

    bool pdfActive = !currentWorkspacePdf.empty();
    btnEdit = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnEdit, L"Edit Text & Images", L"\xE70F", 2, pdfWorkspaceHover, !pdfActive);
    btnY += btnH + sectionGap;

    // --- SECTION 2: AI & SMART TOOLS ---
    DrawSectionHeader(g, L"AI & SMART TOOLS", cx + padding, btnY, btnW);
    btnY += 22.0f * g_scaleFactor;

    btnAIChat = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnAIChat, L"Chat with PDF (AI)", L"\xE8BD", 10, pdfWorkspaceHover, !pdfActive); 
    btnY += btnH + btnGap;

    btnOCR = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnOCR, L"Scan to Text (OCR)", L"\xE8B3", 9, pdfWorkspaceHover, !pdfActive); 
    btnY += btnH + sectionGap;

    // --- SECTION 3: PAGE ORGANIZE ---
    DrawSectionHeader(g, L"PAGE TOOLS", cx + padding, btnY, btnW);
    btnY += 22.0f * g_scaleFactor;

    btnOrganize = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnOrganize, L"Organize Pages", L"\xE8CB", 3, pdfWorkspaceHover, !pdfActive);
    btnY += btnH + btnGap;

    btnMerge = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnMerge, L"Merge PDFs", L"\xE8D3", 4, pdfWorkspaceHover, !pdfActive);
    btnY += btnH + btnGap;

    btnSplit = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnSplit, L"Split / Extract", L"\xE8B9", 5, pdfWorkspaceHover, !pdfActive);
    btnY += btnH + sectionGap;

    // --- SECTION 4: ADVANCED & SECURITY ---
    DrawSectionHeader(g, L"ADVANCED", cx + padding, btnY, btnW);
    btnY += 22.0f * g_scaleFactor;

    btnCompress = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnCompress, L"Compress PDF", L"\xE8D5", 6, pdfWorkspaceHover, !pdfActive);
    btnY += btnH + btnGap;

    btnProtect = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnProtect, L"Protect & Sign", L"\xE72E", 7, pdfWorkspaceHover, !pdfActive);
    btnY += btnH + btnGap;

    btnExport = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnExport, L"Export to Word/Excel", L"\xE72D", 8, pdfWorkspaceHover, !pdfActive);
    btnY += btnH + btnGap;

    btnBatch = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnBatch, L"Batch Processing", L"\xE7B3", 11, pdfWorkspaceHover, !pdfActive); 
}

// ==========================================
// 🖱️ MOUSE HANDLERS
// ==========================================
void ProcessPdfWorkspaceMouseMove(float x, float y) {
    int oldHov = pdfWorkspaceHover;
    pdfWorkspaceHover = 0;

    bool pdfActive = !currentWorkspacePdf.empty();

    if (btnOpen.Contains(x, y)) pdfWorkspaceHover = 1;
    else if (pdfActive && btnEdit.Contains(x, y)) pdfWorkspaceHover = 2;
    else if (pdfActive && btnOrganize.Contains(x, y)) pdfWorkspaceHover = 3;
    else if (pdfActive && btnMerge.Contains(x, y)) pdfWorkspaceHover = 4;
    else if (pdfActive && btnSplit.Contains(x, y)) pdfWorkspaceHover = 5;
    else if (pdfActive && btnCompress.Contains(x, y)) pdfWorkspaceHover = 6;
    else if (pdfActive && btnProtect.Contains(x, y)) pdfWorkspaceHover = 7;
    else if (pdfActive && btnExport.Contains(x, y)) pdfWorkspaceHover = 8;
    else if (pdfActive && btnOCR.Contains(x, y)) pdfWorkspaceHover = 9;
    else if (pdfActive && btnAIChat.Contains(x, y)) pdfWorkspaceHover = 10;
    else if (pdfActive && btnBatch.Contains(x, y)) pdfWorkspaceHover = 11;

    if (oldHov != pdfWorkspaceHover && hParentWnd) {
        RECT updateRect = { 0, 0, (LONG)(280.0f * g_scaleFactor), 2000 }; 
        InvalidateRect(hParentWnd, &updateRect, FALSE);
    }
}

// ✅ EXTERNAL FUNCTION TO OPEN PDF FROM FOLDER
void OpenPdfFromFolder() {
    IFileOpenDialog *pFileOpen;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen)))) {
        COMDLG_FILTERSPEC rgSpec[] = { { L"PDF Files", L"*.pdf" } };
        pFileOpen->SetFileTypes(1, rgSpec);
        
        if (SUCCEEDED(pFileOpen->Show(hParentWnd))) {
            IShellItem *pItem;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    LoadPdfFast(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
}

void ProcessPdfWorkspaceMouseClick(float x, float y) {
    if (pdfWorkspaceHover == 1) {
        OpenPdfFromFolder();
    }
    else if (pdfWorkspaceHover == 2) MessageBoxW(hParentWnd, L"✏️ Edit PDF Text, Images, and Links here.\n\nFeatures:\n• Change text and fonts\n• Replace images\n• Edit links\n• Add annotations", L"Edit PDF", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 3) MessageBoxW(hParentWnd, L"🔄 Organize Pages\n\nFeatures:\n• Drag and drop pages\n• Delete pages\n• Rotate pages\n• Reorder pages", L"Organize Pages", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 4) MessageBoxW(hParentWnd, L"📎 Merge Multiple PDFs\n\nFeatures:\n• Combine multiple files\n• Custom page order\n• Merge or insert\n• Fast processing", L"Merge PDFs", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 5) MessageBoxW(hParentWnd, L"✂️ Split or Extract\n\nFeatures:\n• Split by page ranges\n• Extract specific pages\n• Extract images\n• Fast extraction", L"Split / Extract", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 6) MessageBoxW(hParentWnd, L"📦 Compress PDF\n\nFeatures:\n• Reduce file size\n• Maintain quality\n• Multiple compression levels\n• Fast compression", L"Compress PDF", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 7) MessageBoxW(hParentWnd, L"🔐 Protect & Sign PDF\n\nFeatures:\n• Add passwords\n• Add watermarks\n• Digital signatures\n• Encryption", L"Protect & Sign", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 8) MessageBoxW(hParentWnd, L"📤 Export to Other Formats\n\nFormats:\n• Word (.docx)\n• Excel (.xlsx)\n• PowerPoint (.pptx)\n• Images", L"Export PDF", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 9) MessageBoxW(hParentWnd, L"🔤 OCR - Text Recognition\n\nFeatures:\n• Extract text from scans\n• Image to text\n• Multiple languages\n• High accuracy", L"OCR", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 10) MessageBoxW(hParentWnd, L"🤖 AI Assistant for PDF\n\nFeatures:\n• Ask questions\n• Summarize content\n• Translate text\n• Extract key info", L"AI Assistant", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 11) MessageBoxW(hParentWnd, L"⚙️ Batch Processing\n\nFeatures:\n• Process multiple PDFs\n• Apply same operation\n• Password protection\n• Watermarking", L"Batch Processing", MB_OK | MB_ICONINFORMATION);
}
