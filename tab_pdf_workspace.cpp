#include "tab_pdf_workspace.h"
#include <shobjidl.h>
#include <iostream>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;

// --- State Variables ---
static wstring currentWorkspacePdf = L"";

// --- Button Bounds ---
static RectF btnOpen, btnOrganize, btnMerge, btnExtract;
static int pdfWorkspaceHover = 0; // 1=Open, 2=Organize, 3=Merge, 4=Extract

// --- Helper: Draw Button ---
static void DrawSidebarButton(Graphics& g, RectF bounds, wstring text, wstring icon, int hoverCode, int currentHover) {
    SolidBrush bgBrush(currentHover == hoverCode ? Color(255, 12, 168, 176) : Color(255, 40, 50, 70));
    g.FillRectangle(&bgBrush, bounds);

    FontFamily ff(L"Segoe UI");
    Font fText(&ff, 14.0f * g_scaleFactor, FontStyleBold, UnitPixel);
    
    FontFamily ffIcon(L"Segoe MDL2 Assets");
    Font fIcon(&ffIcon, 16.0f * g_scaleFactor, FontStyleRegular, UnitPixel);
    
    StringFormat fmt; 
    fmt.SetAlignment(StringAlignmentNear); 
    fmt.SetLineAlignment(StringAlignmentCenter);

    SolidBrush textBrush(Color(255, 255, 255, 255));
    
    // Draw Icon
    RectF iconRect = bounds;
    iconRect.X += 15.0f * g_scaleFactor;
    g.DrawString(icon.c_str(), -1, &fIcon, iconRect, &fmt, &textBrush);

    // Draw Text
    RectF textRect = bounds;
    textRect.X += 45.0f * g_scaleFactor;
    g.DrawString(text.c_str(), -1, &fText, textRect, &fmt, &textBrush);
}

// --- Edge Engine Area Calculator ---
RECT GetPdfWebViewArea(float cx, float cy, float cw, float ch) {
    float sidebarW = 260.0f * g_scaleFactor;
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
    float sidebarW = 260.0f * g_scaleFactor;

    // ১. Right Side Background (যেখানে Edge Engine বসবে)
    SolidBrush bgRight(Color(255, 240, 243, 248));
    g.FillRectangle(&bgRight, cx + sidebarW, cy, cw - sidebarW, ch);

    if (currentWorkspacePdf.empty()) {
        FontFamily ff(L"Segoe UI");
        Font fEmpty(&ff, 18.0f * g_scaleFactor, FontStyleBold, UnitPixel);
        SolidBrush txtEmpty(Color(255, 150, 160, 170));
        StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"Edge Native PDF Engine Area\nClick 'Open PDF' from the left menu.", -1, &fEmpty, RectF(cx + sidebarW, cy, cw - sidebarW, ch), &fmtC, &txtEmpty);
    }

    // ২. Left Sidebar (কাস্টম প্রো-টুলবার)
    SolidBrush bgSidebar(Color(255, 25, 35, 50));
    g.FillRectangle(&bgSidebar, cx, cy, sidebarW, ch);

    // Sidebar Title
    FontFamily ffTitle(L"Segoe UI");
    Font fTitle(&ffTitle, 22.0f * g_scaleFactor, FontStyleBold, UnitPixel);
    SolidBrush txtTitle(Color(255, 255, 255, 255));
    StringFormat fmt; fmt.SetAlignment(StringAlignmentNear); fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"PDF Workspace", -1, &fTitle, RectF(cx + 20.0f * g_scaleFactor, cy + 20.0f * g_scaleFactor, sidebarW, 40.0f * g_scaleFactor), &fmt, &txtTitle);

    // ৩. Sidebar Buttons
    float btnY = cy + 80.0f * g_scaleFactor;
    float btnH = 45.0f * g_scaleFactor;
    float btnGap = 5.0f * g_scaleFactor;
    float padding = 15.0f * g_scaleFactor;
    float btnW = sidebarW - (padding * 2);

    btnOpen = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnOpen, L"Open PDF", L"\xE8E5", 1, pdfWorkspaceHover);
    btnY += btnH + btnGap + 20.0f * g_scaleFactor; // একটু বেশি গ্যাপ

    btnOrganize = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnOrganize, L"Organize Pages", L"\xE8CB", 2, pdfWorkspaceHover);
    btnY += btnH + btnGap;

    btnMerge = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnMerge, L"Merge PDFs", L"\xE8D3", 3, pdfWorkspaceHover);
    btnY += btnH + btnGap;

    btnExtract = RectF(cx + padding, btnY, btnW, btnH);
    DrawSidebarButton(g, btnExtract, L"Extract Images", L"\xE8B9", 4, pdfWorkspaceHover);
}

// ==========================================
// 🖱️ MOUSE HANDLERS
// ==========================================
void ProcessPdfWorkspaceMouseMove(float x, float y) {
    int oldHov = pdfWorkspaceHover;
    pdfWorkspaceHover = 0;

    if (btnOpen.Contains(x, y)) pdfWorkspaceHover = 1;
    else if (btnOrganize.Contains(x, y)) pdfWorkspaceHover = 2;
    else if (btnMerge.Contains(x, y)) pdfWorkspaceHover = 3;
    else if (btnExtract.Contains(x, y)) pdfWorkspaceHover = 4;

    if (oldHov != pdfWorkspaceHover && hParentWnd) {
        // শুধু সাইডবার এরিয়া রিড্র করা, যাতে পারফরম্যান্স ভালো থাকে
        RECT updateRect = { 0, 0, (LONG)(260.0f * g_scaleFactor), 2000 }; 
        InvalidateRect(hParentWnd, &updateRect, FALSE);
    }
}

void ProcessPdfWorkspaceMouseClick(float x, float y) {
    if (pdfWorkspaceHover == 1) {
        // --- Open PDF via Windows Dialog ---
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
    else if (pdfWorkspaceHover == 2) {
        // TODO: C++ ব্যাকগ্রাউন্ড লজিক দিয়ে পেজ অর্গানাইজ করা
        MessageBox(hParentWnd, L"Organize Pages UI & Logic will open here.", L"Feature", MB_OK);
    }
    else if (pdfWorkspaceHover == 3) {
        MessageBox(hParentWnd, L"Select Multiple PDFs to Merge.", L"Feature", MB_OK);
    }
    else if (pdfWorkspaceHover == 4) {
        MessageBox(hParentWnd, L"Extracting high-quality images from PDF...", L"Feature", MB_OK);
    }
}
