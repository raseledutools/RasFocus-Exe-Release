#include "tab_pdf_workspace.h"
#include <shobjidl.h>
#include <iostream>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;

// --- State Variables ---
extern wstring currentWorkspacePdf;

// --- Button Bounds ---
static RectF btnOpen, btnEdit, btnOrganize, btnMerge, btnSplit, btnCompress, btnProtect, btnExport, btnOCR, btnAIChat, btnBatch;
static int pdfWorkspaceHover = 0; 
// 1=Open, 2=Edit, 3=Organize, 4=Merge, 5=Split, 6=Compress, 7=Protect, 8=Export, 9=OCR, 10=AIChat, 11=Batch

// --- Helper: Draw Sidebar Button ---
static void DrawSidebarButton(Graphics& g, RectF bounds, wstring text, wstring icon, int hoverCode, int currentHover) {
    SolidBrush bgBrush(currentHover == hoverCode ? Color(255, 12, 168, 176) : Color(255, 30, 40, 55));
    g.FillRectangle(&bgBrush, bounds);

    FontFamily ff(L"Segoe UI");
    Font fText(&ff, 13.0f * g_scaleFactor, FontStyleBold, UnitPixel); // একটু ছোট করা হয়েছে যাতে সব ধরে
    
    FontFamily ffIcon(L"Segoe MDL2 Assets");
    Font fIcon(&ffIcon, 15.0f * g_scaleFactor, FontStyleRegular, UnitPixel);
    
    StringFormat fmt; 
    fmt.SetAlignment(StringAlignmentNear); 
    fmt.SetLineAlignment(StringAlignmentCenter);

    SolidBrush textBrush(Color(255, 255, 255, 255));
    
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

// ==========================================
// 🎨 MAIN DRAWING FUNCTION
// ==========================================
void DrawPdfWorkspaceTab(Graphics& g, float cx, float cy, float cw, float ch) {
    float sidebarW = 280.0f * g_scaleFactor;

    // ১. Right Side Background (Edge Engine Area)
    SolidBrush bgRight(Color(255, 240, 243, 248));
    g.FillRectangle(&bgRight, cx + sidebarW, cy, cw - sidebarW, ch);

    if (currentWorkspacePdf.empty()) {
        FontFamily ff(L"Segoe UI");
        Font fEmpty(&ff, 20.0f * g_scaleFactor, FontStyleBold, UnitPixel);
        SolidBrush txtEmpty(Color(255, 160, 170, 180));
        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"No PDF Selected\nUse the left panel to open a document.", -1, &fEmpty, RectF(cx + sidebarW, cy, cw - sidebarW, ch), &fmtC, &txtEmpty);
    }

    // ২. Left Sidebar (Ultimate Pro Toolbar)
    SolidBrush bgSidebar(Color(255, 20, 25, 35)); 
    g.FillRectangle(&bgSidebar, cx, cy, sidebarW, ch);

    // Sidebar Title
    FontFamily ffTitle(L"Segoe UI");
    Font fTitle(&ffTitle, 22.0f * g_scaleFactor, FontStyleBold, UnitPixel);
    SolidBrush txtTitle(Color(255, 255, 255, 255));
    StringFormat fmt; fmt.SetAlignment(StringAlignmentNear); fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"Ultimate PDF Studio", -1, &fTitle, RectF(cx + 15.0f * g_scaleFactor, cy + 15.0f * g_scaleFactor, sidebarW, 40.0f * g_scaleFactor), &fmt, &txtTitle);

    // ৩. Sidebar Buttons Calculation
    float btnY = cy + 65.0f * g_scaleFactor;
    float btnH = 36.0f * g_scaleFactor; // স্পেস বাঁচানোর জন্য সাইজ একটু কমানো হয়েছে
    float btnGap = 4.0f * g_scaleFactor;
    float sectionGap = 12.0f * g_scaleFactor;
    float padding = 12.0f * g_scaleFactor;
    float btnW = sidebarW - (padding * 2);

    // --- SECTION 1: VIEW & EDIT ---
    DrawSectionHeader(g, L"VIEW & EDIT", cx + padding, btnY, btnW);
    btnY += 22.0f * g_scaleFactor;

    btnOpen = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnOpen, L"Open Document", L"\xE8E5", 1, pdfWorkspaceHover);
    btnY += btnH + btnGap;

    btnEdit = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnEdit, L"Edit Text & Images", L"\xE70F", 2, pdfWorkspaceHover);
    btnY += btnH + sectionGap;

    // --- SECTION 2: AI & SMART TOOLS (🟢 নতুন) ---
    DrawSectionHeader(g, L"AI & SMART TOOLS", cx + padding, btnY, btnW);
    btnY += 22.0f * g_scaleFactor;

    btnAIChat = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnAIChat, L"Chat with PDF (AI)", L"\xE8BD", 10, pdfWorkspaceHover); // Message icon
    btnY += btnH + btnGap;

    btnOCR = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnOCR, L"Scan to Text (OCR)", L"\xE8B3", 9, pdfWorkspaceHover); // Scanner icon
    btnY += btnH + sectionGap;

    // --- SECTION 3: PAGE ORGANIZE ---
    DrawSectionHeader(g, L"PAGE TOOLS", cx + padding, btnY, btnW);
    btnY += 22.0f * g_scaleFactor;

    btnOrganize = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnOrganize, L"Organize Pages", L"\xE8CB", 3, pdfWorkspaceHover);
    btnY += btnH + btnGap;

    btnMerge = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnMerge, L"Merge PDFs", L"\xE8D3", 4, pdfWorkspaceHover);
    btnY += btnH + btnGap;

    btnSplit = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnSplit, L"Split / Extract", L"\xE8B9", 5, pdfWorkspaceHover);
    btnY += btnH + sectionGap;

    // --- SECTION 4: ADVANCED & SECURITY ---
    DrawSectionHeader(g, L"ADVANCED", cx + padding, btnY, btnW);
    btnY += 22.0f * g_scaleFactor;

    btnCompress = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnCompress, L"Compress PDF", L"\xE8D5", 6, pdfWorkspaceHover);
    btnY += btnH + btnGap;

    btnProtect = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnProtect, L"Protect & Sign", L"\xE72E", 7, pdfWorkspaceHover);
    btnY += btnH + btnGap;

    btnExport = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnExport, L"Export to Word/Excel", L"\xE72D", 8, pdfWorkspaceHover);
    btnY += btnH + btnGap;

    btnBatch = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnBatch, L"Batch Processing", L"\xE7B3", 11, pdfWorkspaceHover); // Sync icon
}

// ==========================================
// 🖱️ MOUSE HANDLERS
// ==========================================
void ProcessPdfWorkspaceMouseMove(float x, float y) {
    int oldHov = pdfWorkspaceHover;
    pdfWorkspaceHover = 0;

    if (btnOpen.Contains(x, y)) pdfWorkspaceHover = 1;
    else if (btnEdit.Contains(x, y)) pdfWorkspaceHover = 2;
    else if (btnOrganize.Contains(x, y)) pdfWorkspaceHover = 3;
    else if (btnMerge.Contains(x, y)) pdfWorkspaceHover = 4;
    else if (btnSplit.Contains(x, y)) pdfWorkspaceHover = 5;
    else if (btnCompress.Contains(x, y)) pdfWorkspaceHover = 6;
    else if (btnProtect.Contains(x, y)) pdfWorkspaceHover = 7;
    else if (btnExport.Contains(x, y)) pdfWorkspaceHover = 8;
    else if (btnOCR.Contains(x, y)) pdfWorkspaceHover = 9;
    else if (btnAIChat.Contains(x, y)) pdfWorkspaceHover = 10;
    else if (btnBatch.Contains(x, y)) pdfWorkspaceHover = 11;

    if (oldHov != pdfWorkspaceHover && hParentWnd) {
        // পুরো সাইডবার এরিয়া রিড্র করা
        RECT updateRect = { 0, 0, (LONG)(280.0f * g_scaleFactor), 2000 }; 
        InvalidateRect(hParentWnd, &updateRect, FALSE);
    }
}

void ProcessPdfWorkspaceMouseClick(float x, float y) {
    if (pdfWorkspaceHover == 1) {
        // --- Open PDF ---
        IFileOpenDialog *pFileOpen;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen)))) {
            COMDLG_FILTERSPEC rgSpec[] = { { L"PDF Files", L"*.pdf" } };
            pFileOpen->SetFileTypes(1, rgSpec);
            
            if (SUCCEEDED(pFileOpen->Show(hParentWnd))) {
                IShellItem *pItem;
                if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                    PWSTR pszFilePath;
                    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_URL, &pszFilePath))) {
                        currentWorkspacePdf = pszFilePath;
                        
                        // 🟢 এখানে আপনার WebView2 কে কল করতে হবে!
                        // RECT area = GetPdfWebViewArea(...);
                        // LaunchWebView2_In_Specific_Area(currentWorkspacePdf, area);
                        
                        CoTaskMemFree(pszFilePath);
                    }
                    pItem->Release();
                }
            }
            pFileOpen->Release();
        }
        if (hParentWnd) InvalidateRect(hParentWnd, NULL, FALSE);
    }
    else if (pdfWorkspaceHover == 2) MessageBox(hParentWnd, L"Edit PDF Text, Images, and Links here.", L"Edit Mode", MB_OK);
    else if (pdfWorkspaceHover == 3) MessageBox(hParentWnd, L"Drag and drop pages to rearrange, delete, or rotate them.", L"Organize Pages", MB_OK);
    else if (pdfWorkspaceHover == 4) MessageBox(hParentWnd, L"Select multiple files to merge into a single PDF.", L"Merge PDF", MB_OK);
    else if (pdfWorkspaceHover == 5) MessageBox(hParentWnd, L"Split PDF by page ranges or extract specific images.", L"Split / Extract", MB_OK);
    else if (pdfWorkspaceHover == 6) MessageBox(hParentWnd, L"Compress PDF to reduce file size for sharing.", L"Compress PDF", MB_OK);
    else if (pdfWorkspaceHover == 7) MessageBox(hParentWnd, L"Add Password, Watermark, or Digital Signature.", L"Protect & Sign", MB_OK);
    else if (pdfWorkspaceHover == 8) MessageBox(hParentWnd, L"Convert PDF to Word, Excel, or PowerPoint format.", L"Export PDF", MB_OK);
    else if (pdfWorkspaceHover == 9) MessageBox(hParentWnd, L"Extract text from scanned PDFs or images using OCR.", L"OCR Tool", MB_OK);
    else if (pdfWorkspaceHover == 10) MessageBox(hParentWnd, L"Ask questions, summarize, or translate PDF content using AI.", L"AI Assistant", MB_OK);
    else if (pdfWorkspaceHover == 11) MessageBox(hParentWnd, L"Apply passwords or watermarks to multiple PDFs at once.", L"Batch Processing", MB_OK);
}
