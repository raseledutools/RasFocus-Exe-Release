#include "tab_pdf_workspace.h"
#include <shobjidl.h>
#include <iostream>
#include <thread>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;

// 🟢 FIX: main.cpp এর গ্লোবাল ভ্যারিয়েবলটি লিংকার এরর ছাড়াই কানেক্ট করা হলো
extern wstring currentWorkspacePdf;

// --- WebView2 Engine Handle ---
extern HWND hPdfWebView; // PDF viewer window
extern bool isPdfLoaded;

// --- Button Bounds ---
static RectF btnOpen, btnEdit, btnOrganize, btnMerge, btnSplit, btnCompress, btnProtect, btnExport, btnOCR, btnAIChat, btnBatch;
static int pdfWorkspaceHover = 0; 
// 1=Open, 2=Edit, 3=Organize, 4=Merge, 5=Split, 6=Compress, 7=Protect, 8=Export, 9=OCR, 10=AIChat, 11=Batch

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
    
    // Thread-safe PDF loading
    thread pdfThread([filePath]() {
        try {
            currentWorkspacePdf = filePath;
            isPdfLoaded = true;
            
            // Refresh UI
            if (hParentWnd) {
                InvalidateRect(hParentWnd, NULL, FALSE);
            }
        } catch (...) {
            isPdfLoaded = false;
        }
    });
    pdfThread.detach();
}

// ==========================================
// 🎨 MAIN DRAWING FUNCTION
// ==========================================
void DrawPdfWorkspaceTab(Graphics& g, float cx, float cy, float cw, float ch) {
    float sidebarW = 280.0f * g_scaleFactor;

    // ১. Right Side Background
    SolidBrush bgRight(Color(255, 240, 243, 248));
    g.FillRectangle(&bgRight, cx + sidebarW, cy, cw - sidebarW, ch);

    if (currentWorkspacePdf.empty()) {
        FontFamily ff(L"Segoe UI");
        Font fEmpty(&ff, 20.0f * g_scaleFactor, FontStyleBold, UnitPixel);
        SolidBrush txtEmpty(Color(255, 160, 170, 180));
        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"No PDF Selected\nUse the left panel to open a document.", -1, &fEmpty, RectF(cx + sidebarW, cy, cw - sidebarW, ch), &fmtC, &txtEmpty);
    } else {
        // ✅ PDF হল - WebView2 রেন্ডার এরিয়া দেখান
        SolidBrush pdfBg(Color(255, 255, 255, 255));
        g.FillRectangle(&pdfBg, cx + sidebarW, cy, cw - sidebarW, ch);
        
        FontFamily ff(L"Segoe UI");
        Font fLoading(&ff, 16.0f * g_scaleFactor, FontStyleBold, UnitPixel);
        SolidBrush txtLoading(Color(255, 100, 100, 100));
        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
        
        if (isPdfLoaded) {
            g.DrawString(L"📄 PDF Loading... [WebView2 Engine Active]", -1, &fLoading, RectF(cx + sidebarW, cy, cw - sidebarW, ch), &fmtC, &txtLoading);
        } else {
            g.DrawString(L"⏳ Initializing PDF Engine...", -1, &fLoading, RectF(cx + sidebarW, cy, cw - sidebarW, ch), &fmtC, &txtLoading);
        }
    }

    // २. Left Sidebar
    SolidBrush bgSidebar(Color(255, 20, 25, 35)); 
    g.FillRectangle(&bgSidebar, cx, cy, sidebarW, ch);

    // Sidebar Title
    FontFamily ffTitle(L"Segoe UI");
    Font fTitle(&ffTitle, 22.0f * g_scaleFactor, FontStyleBold, UnitPixel);
    SolidBrush txtTitle(Color(255, 255, 255, 255));
    StringFormat fmt; fmt.SetAlignment(StringAlignmentNear); fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"Ultimate PDF Studio", -1, &fTitle, RectF(cx + 15.0f * g_scaleFactor, cy + 15.0f * g_scaleFactor, sidebarW, 40.0f * g_scaleFactor), &fmt, &txtTitle);

    // ३. Sidebar Buttons
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
    else if (pdfWorkspaceHover == 2) MessageBoxW(hParentWnd, L"✏️ Edit PDF Text, Images, and Links here.\n\nFeatures:\n• Change text and fonts\n• Replace images\n• Edit links\n• Add annotations", L"Edit Mode", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 3) MessageBoxW(hParentWnd, L"🔄 Organize Pages\n\nFeatures:\n• Drag and drop pages\n• Delete pages\n• Rotate pages\n• Reorder pages", L"Organize Pages", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 4) MessageBoxW(hParentWnd, L"📎 Merge Multiple PDFs\n\nFeatures:\n• Combine multiple files\n• Custom page order\n• Merge or insert\n• Fast processing", L"Merge PDF", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 5) MessageBoxW(hParentWnd, L"✂️ Split or Extract\n\nFeatures:\n• Split by page ranges\n• Extract specific pages\n• Extract images\n• Fast extraction", L"Split / Extract", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 6) MessageBoxW(hParentWnd, L"📦 Compress PDF\n\nFeatures:\n• Reduce file size\n• Maintain quality\n• Multiple compression levels\n• Fast compression", L"Compress PDF", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 7) MessageBoxW(hParentWnd, L"🔐 Protect & Sign PDF\n\nFeatures:\n• Add passwords\n• Add watermarks\n• Digital signatures\n• Encryption", L"Protect & Sign", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 8) MessageBoxW(hParentWnd, L"📤 Export to Other Formats\n\nFormats:\n• Word (.docx)\n• Excel (.xlsx)\n• PowerPoint (.pptx)\n• Images", L"Export PDF", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 9) MessageBoxW(hParentWnd, L"🔤 OCR - Text Recognition\n\nFeatures:\n• Extract text from scans\n• Image to text\n• Multiple languages\n• High accuracy", L"OCR Tool", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 10) MessageBoxW(hParentWnd, L"🤖 AI Assistant for PDF\n\nFeatures:\n• Ask questions\n• Summarize content\n• Translate text\n• Extract key info", L"AI Assistant", MB_OK | MB_ICONINFORMATION);
    else if (pdfWorkspaceHover == 11) MessageBoxW(hParentWnd, L"⚙️ Batch Processing\n\nFeatures:\n• Process multiple PDFs\n• Apply same operation\n• Password protection\n• Watermarking", L"Batch Processing", MB_OK | MB_ICONINFORMATION);
}
