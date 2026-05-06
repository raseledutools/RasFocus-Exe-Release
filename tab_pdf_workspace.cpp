// tab_pdf_workspace.cpp
// Professional PDF Workspace Architecture - SumatraPDF Style, Acrobat-level features

#define _CRT_SECURE_NO_WARNINGS
#include "tab_pdf_workspace.h"
#include "mini_browser.h"
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <Shlwapi.h>
#include <WebView2.h>
#include <wrl.h>
#include <wrl/event.h>

#pragma comment(lib, "Shlwapi.lib")

using namespace Gdiplus;
using namespace Microsoft::WRL;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;
extern wstring currentWorkspacePdf;

// --- WebView2 Global Variables ---
HWND g_hAcrobatWnd = NULL;
HWND g_hWebViewWnd = NULL;
ComPtr<ICoreWebView2Environment> g_webViewEnv = nullptr;
ComPtr<ICoreWebView2Controller> g_webViewController = nullptr;
ComPtr<ICoreWebView2> g_webView = nullptr;
wstring g_acrobatPdfPath = L"";
bool g_webViewInitialized = false;

// Forward Declarations
HRESULT InitializeWebView2(HWND hWnd, HWND hHostWnd);
LRESULT CALLBACK AcrobatViewerWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

// ==========================================
// HTML/CSS/JS - Split into ~35 small parts to avoid MSVC C2026
// ==========================================
wstring GetAcrobatHTML() {
    std::wstringstream ss;

    // --- PART 1: Head, CDN scripts, CSS variables ---
    ss << LR"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>RasFocus PDF Pro</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
<script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/tesseract.js@4/dist/tesseract.min.js"></script>
<style>
:root {
  --bg-topbar: #1e1e1e;
  --bg-tabbar: #252526;
  --bg-toolbar: #f3f3f3;
  --bg-viewer: #b0b0b0;
  --bg-sidebar: #f0f0f0;
  --border: #e0e0e0;
  --accent: #c0392b;
  --accent2: #e74c3c;
  --text-dark: #1a1a1a;
  --text-muted: #666;
  --text-light: #ccc;
  --topbar-h: 28px;
  --tabbar-h: 26px;
  --toolbar-h: 34px;
  --sidebar-w: 136px;
  --thumb-panel-w: 160px;
  --shadow: 0 2px 8px rgba(0,0,0,.22);
}
</style>
</head>
<body>
)HTML";

    // --- PART 2: Global Reset & Body ---
    ss << LR"HTML(
<style>
*{margin:0;padding:0;box-sizing:border-box;}
html,body{height:100vh;overflow:hidden;font-family:'Segoe UI',sans-serif;font-size:12px;background:var(--bg-viewer);user-select:none;}
::-webkit-scrollbar{width:6px;height:6px;}
::-webkit-scrollbar-track{background:transparent;}
::-webkit-scrollbar-thumb{background:#aaa;border-radius:3px;}
::-webkit-scrollbar-thumb:hover{background:#888;}
#app{display:flex;flex-direction:column;height:100vh;overflow:hidden;}
</style>
)HTML";

    // --- PART 3: Topbar CSS ---
    ss << LR"HTML(
<style>
.topbar{
  height:var(--topbar-h);background:var(--bg-topbar);display:flex;align-items:center;
  padding:0 8px;gap:2px;flex-shrink:0;border-bottom:1px solid #111;
}
.topbar-btn{
  color:var(--text-light);font-size:11px;padding:2px 8px;border-radius:2px;cursor:pointer;
  white-space:nowrap;line-height:var(--topbar-h);transition:background .12s;
}
.topbar-btn:hover{background:rgba(255,255,255,.1);color:#fff;}
.topbar-sep{width:1px;height:14px;background:#444;margin:0 3px;}
.topbar-right{margin-left:auto;display:flex;align-items:center;gap:4px;}
.topbar-icon{
  color:var(--text-light);cursor:pointer;padding:2px 5px;border-radius:2px;
  font-size:13px;line-height:1;transition:background .12s;
}
.topbar-icon:hover{background:rgba(255,255,255,.12);color:#fff;}
</style>
)HTML";

    // --- PART 4: Tabbar CSS ---
    ss << LR"HTML(
<style>
.tabbar{
  height:var(--tabbar-h);background:var(--bg-tabbar);display:flex;
  align-items:flex-end;padding-left:4px;overflow-x:auto;flex-shrink:0;
  border-bottom:1px solid #111;
}
.tabbar::-webkit-scrollbar{height:0;}
.pdf-tab{
  height:22px;background:#3a3a3a;color:#aaa;padding:0 8px;
  display:flex;align-items:center;gap:6px;font-size:11px;border-radius:3px 3px 0 0;
  margin-right:2px;cursor:pointer;max-width:180px;white-space:nowrap;
  border:1px solid #111;border-bottom:none;transition:background .1s;
  position:relative;top:1px;
}
.pdf-tab:hover{background:#484848;color:#ddd;}
.pdf-tab.active{background:var(--bg-toolbar);color:var(--text-dark);font-weight:600;}
.tab-name{overflow:hidden;text-overflow:ellipsis;max-width:130px;}
.tab-close{
  font-size:13px;line-height:1;cursor:pointer;opacity:.6;
  border-radius:2px;width:14px;height:14px;display:flex;align-items:center;justify-content:center;
  flex-shrink:0;
}
.tab-close:hover{opacity:1;background:rgba(0,0,0,.15);color:var(--accent);}
.add-tab{
  color:#aaa;cursor:pointer;padding:0 8px;font-size:15px;
  line-height:22px;border-radius:3px 3px 0 0;
}
.add-tab:hover{color:#fff;background:#444;}
</style>
)HTML";

    // --- PART 5: Toolbar CSS ---
    ss << LR"HTML(
<style>
.toolbar{
  height:var(--toolbar-h);background:var(--bg-toolbar);display:flex;align-items:center;
  padding:0 10px;gap:3px;flex-shrink:0;border-bottom:1px solid var(--border);
}
.tbtn{
  width:26px;height:26px;display:flex;align-items:center;justify-content:center;
  cursor:pointer;border-radius:3px;color:var(--text-dark);transition:background .1s;
  font-size:14px;border:none;background:transparent;
}
.tbtn:hover{background:#e0e0e0;}
.tbtn.active{background:#fce4e4;color:var(--accent);}
.tbtn svg{width:15px;height:15px;fill:currentColor;pointer-events:none;}
.tsep{width:1px;height:18px;background:var(--border);margin:0 4px;flex-shrink:0;}
#zoom-display{
  font-size:11px;font-weight:600;width:42px;text-align:center;
  color:var(--text-dark);cursor:default;
}
.toolbar-right{margin-left:auto;display:flex;align-items:center;gap:3px;}
</style>
)HTML";

    // --- PART 6: Main workspace layout CSS ---
    ss << LR"HTML(
<style>
.workspace{flex:1;display:flex;overflow:hidden;position:relative;}
.thumb-panel{
  width:var(--thumb-panel-w);background:#e8e8e8;border-right:1px solid var(--border);
  display:flex;flex-direction:column;overflow:hidden;flex-shrink:0;
  transition:width .2s;
}
.thumb-panel.collapsed{width:0;border:none;}
.thumb-panel-header{
  height:28px;background:#ddd;border-bottom:1px solid var(--border);
  display:flex;align-items:center;padding:0 8px;font-size:10px;
  font-weight:700;color:var(--text-muted);text-transform:uppercase;letter-spacing:.5px;
  flex-shrink:0;justify-content:space-between;
}
.thumb-list{flex:1;overflow-y:auto;padding:6px;display:flex;flex-direction:column;gap:4px;}
.thumb-item{
  background:#fff;border:2px solid transparent;border-radius:3px;cursor:pointer;
  position:relative;transition:border-color .15s;box-shadow:0 1px 3px rgba(0,0,0,.15);
}
.thumb-item:hover{border-color:#aaa;}
.thumb-item.selected{border-color:var(--accent);}
.thumb-item canvas{width:100%;display:block;border-radius:2px;}
.thumb-num{
  position:absolute;bottom:2px;left:0;right:0;text-align:center;font-size:9px;
  color:#555;background:rgba(255,255,255,.7);
}
.thumb-del{
  position:absolute;top:2px;right:2px;width:14px;height:14px;
  background:var(--accent);color:#fff;border-radius:50%;font-size:9px;
  display:none;align-items:center;justify-content:center;cursor:pointer;
}
.thumb-item:hover .thumb-del{display:flex;}
</style>
)HTML";

    // --- PART 7: PDF viewer area + page CSS ---
    ss << LR"HTML(
<style>
.pdf-viewer-area{
  flex:1;overflow:auto;background:var(--bg-viewer);
  display:flex;justify-content:center;
  cursor:default;position:relative;
}
.pdf-container{
  display:flex;flex-direction:column;gap:12px;
  align-items:center;padding:20px;width:100%;
}
.page-wrapper{
  position:relative;background:#fff;
  box-shadow:var(--shadow);flex-shrink:0;
  display:block;
}
.page-wrapper canvas.pdf-canvas{display:block;position:relative;z-index:1;}
.page-wrapper canvas.draw-canvas{
  position:absolute;top:0;left:0;z-index:2;pointer-events:none;
}
.page-wrapper canvas.draw-canvas.active-draw{pointer-events:auto;}
</style>
)HTML";

    // --- PART 8: Right sidebar CSS ---
    ss << LR"HTML(
<style>
.right-sidebar{
  width:var(--sidebar-w);background:var(--bg-sidebar);border-left:1px solid var(--border);
  display:flex;flex-direction:column;overflow-y:auto;flex-shrink:0;
}
.sb-header{
  padding:6px 8px;font-size:9px;font-weight:700;color:var(--text-muted);
  border-bottom:1px solid var(--border);text-transform:uppercase;letter-spacing:.5px;
}
.sb-btn{
  display:flex;align-items:center;gap:6px;padding:7px 10px;
  cursor:pointer;border-bottom:1px solid var(--border);
  transition:background .12s;color:var(--text-dark);
}
.sb-btn:hover{background:#e6e6e6;}
.sb-btn svg{width:14px;height:14px;fill:var(--text-muted);flex-shrink:0;}
.sb-btn span{font-size:11px;}
</style>
)HTML";

    // --- PART 9: Sticky notes, overlays, modals CSS ---
    ss << LR"HTML(
<style>
.sticky-note{
  position:absolute;z-index:10;background:#fff9c4;border:1px solid #f0d000;
  border-radius:2px;padding:4px;font-size:11px;min-width:80px;min-height:40px;
  box-shadow:2px 2px 6px rgba(0,0,0,.2);cursor:move;resize:both;overflow:auto;
  color:#333;white-space:pre-wrap;word-break:break-word;
}
.sticky-note .note-close{
  position:absolute;top:2px;right:4px;font-size:11px;cursor:pointer;
  color:#999;line-height:1;
}
.sticky-note .note-close:hover{color:var(--accent);}

/* Paste image overlay */
.paste-img-wrapper{
  position:absolute;z-index:15;cursor:move;
  border:2px solid var(--accent);
}
.paste-img-wrapper img{display:block;width:100%;height:100%;pointer-events:none;}
.resize-handle{
  position:absolute;width:10px;height:10px;background:var(--accent);
  border-radius:1px;z-index:16;
}
.resize-handle.nw{top:-5px;left:-5px;cursor:nw-resize;}
.resize-handle.ne{top:-5px;right:-5px;cursor:ne-resize;}
.resize-handle.sw{bottom:-5px;left:-5px;cursor:sw-resize;}
.resize-handle.se{bottom:-5px;right:-5px;cursor:se-resize;}

/* Modals */
.modal-overlay{
  display:none;position:fixed;inset:0;background:rgba(0,0,0,.55);
  z-index:1000;align-items:center;justify-content:center;
}
.modal-overlay.show{display:flex;}
.modal{
  background:#fff;border-radius:4px;padding:18px;min-width:300px;
  box-shadow:0 8px 30px rgba(0,0,0,.3);
}
.modal h3{font-size:13px;font-weight:600;margin-bottom:12px;}
.modal input[type=text],.modal input[type=number]{
  width:100%;padding:6px 8px;border:1px solid var(--border);border-radius:3px;
  outline:none;font-size:12px;margin-bottom:10px;
}
.modal-actions{display:flex;gap:8px;justify-content:flex-end;}
.btn{
  padding:5px 14px;border:none;border-radius:3px;cursor:pointer;
  font-size:12px;font-weight:600;transition:background .15s;
}
.btn-primary{background:var(--accent);color:#fff;}
.btn-primary:hover{background:#a93226;}
.btn-secondary{background:transparent;border:1px solid #bbb;color:var(--text-dark);}
.btn-secondary:hover{background:#eee;}
</style>
)HTML";

    // --- PART 10: Toast + Loading CSS ---
    ss << LR"HTML(
<style>
.toast-box{
  position:fixed;bottom:16px;left:50%;transform:translateX(-50%);
  z-index:9999;display:flex;flex-direction:column;gap:6px;pointer-events:none;
}
.toast{
  padding:8px 18px;border-radius:4px;background:#2d2d2d;color:#fff;
  font-size:12px;font-weight:500;box-shadow:0 3px 10px rgba(0,0,0,.25);
  animation:fadeUp .25s ease;
}
@keyframes fadeUp{from{transform:translateY(12px);opacity:0}to{transform:translateY(0);opacity:1}}
.loading-overlay{
  display:none;position:fixed;inset:0;background:rgba(0,0,0,.6);
  z-index:2000;flex-direction:column;align-items:center;justify-content:center;color:#fff;
}
.loading-overlay.show{display:flex;}
.spinner{
  width:28px;height:28px;border:3px solid rgba(255,255,255,.2);
  border-top:3px solid #fff;border-radius:50%;animation:spin .7s linear infinite;margin-bottom:10px;
}
@keyframes spin{to{transform:rotate(360deg)}}
#loading-txt{font-size:12px;font-weight:500;}
body.night-mode .pdf-viewer-area{background:#181818!important;}
body.night-mode .page-wrapper{box-shadow:0 4px 16px rgba(0,0,0,.6);}
body.night-mode .toolbar{background:#252525!important;border-color:#333!important;}
body.night-mode .tbtn{color:#ccc!important;}
body.night-mode .tbtn:hover{background:#333!important;}
body.night-mode .right-sidebar{background:#252525!important;border-color:#333!important;}
body.night-mode .sb-btn{color:#ccc!important;border-color:#333!important;}
body.night-mode .sb-btn:hover{background:#2e2e2e!important;}
body.read-mode .tabbar,.read-mode .toolbar,.read-mode .right-sidebar,.read-mode .thumb-panel{display:none!important;}
</style>
)HTML";

    // --- PART 11: HTML Structure - app skeleton ---
    ss << LR"HTML(
<div id="app">
  <!-- Topbar -->
  <div class="topbar">
    <div class="topbar-btn" onclick="document.getElementById('fileInput').click()">File</div>
    <div class="topbar-btn" onclick="downloadCurrentPDF()">Save</div>
    <div class="topbar-sep"></div>
    <div class="topbar-btn" onclick="uiShowMergeModal()">Combine</div>
    <div class="topbar-btn" onclick="uiShowSplitModal()">Split</div>
    <div class="topbar-btn" onclick="uiShowExtractModal()">Extract</div>
    <div class="topbar-right">
      <div class="topbar-icon" onclick="toggleThumbPanel()" title="Page Organizer">&#9783;</div>
      <div class="topbar-icon" onclick="toggleNightMode()" title="Night Mode">&#9790;</div>
      <div class="topbar-icon" onclick="toggleReadMode()" title="Read Mode">&#9634;</div>
    </div>
  </div>
  <!-- Tabbar -->
  <div class="tabbar" id="tabbar"></div>
  <!-- Toolbar -->
  <div class="toolbar" id="toolbar">
)HTML";

    // --- PART 12: Toolbar buttons HTML (SVG icons) ---
    ss << LR"HTML(
    <!-- Hand tool -->
    <button class="tbtn active" id="tool-hand" onclick="setTool('hand')" title="Hand (H)">
      <svg viewBox="0 0 24 24"><path d="M9 11V6a1 1 0 0 1 2 0v5h1V4a1 1 0 0 1 2 0v7h1V6a1 1 0 0 1 2 0v8l-1 5H9l-3-3V9a1 1 0 0 1 2 0v2z"/></svg>
    </button>
    <!-- Select -->
    <button class="tbtn" id="tool-select" onclick="setTool('select')" title="Select (S)">
      <svg viewBox="0 0 24 24"><path d="M4 4l7 18 3-7 7-3z"/></svg>
    </button>
    <div class="tsep"></div>
    <!-- Highlight -->
    <button class="tbtn" id="tool-highlight" onclick="setTool('highlight')" title="Highlight (H)">
      <svg viewBox="0 0 24 24"><rect x="3" y="14" width="18" height="5" rx="1" opacity=".4" fill="#f9a825"/><path d="M6 13l6-9 6 9" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>
    </button>
    <!-- Pen -->
    <button class="tbtn" id="tool-pen" onclick="setTool('pen')" title="Pen (P)">
      <svg viewBox="0 0 24 24"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zm17.71-10.21a1 1 0 0 0 0-1.41l-2.34-2.34a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>
    </button>
    <!-- Sticky Note -->
    <button class="tbtn" id="tool-note" onclick="setTool('note')" title="Sticky Note (N)">
      <svg viewBox="0 0 24 24"><path d="M20 2H4a2 2 0 0 0-2 2v14l4-4h14a2 2 0 0 0 2-2V4a2 2 0 0 0-2-2z"/></svg>
    </button>
    <div class="tsep"></div>
    <!-- Zoom out -->
    <button class="tbtn" onclick="zoomBy(-0.15)" title="Zoom Out">
      <svg viewBox="0 0 24 24"><path d="M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"/><path d="M7 9h5v1H7z"/></svg>
    </button>
    <span id="zoom-display">100%</span>
    <button class="tbtn" onclick="zoomBy(0.15)" title="Zoom In">
      <svg viewBox="0 0 24 24"><path d="M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"/><path d="M7 9h5v1H7z"/><path d="M9 7v5h1V7z" transform="rotate(0)"/><path d="M9 7h1v5H9z" style="display:none"/><path d="M9.5 7v2H7v1h2.5v2h1v-2H13V9h-2.5V7z"/></svg>
    </button>
    <div class="tsep"></div>
    <!-- Rotate -->
    <button class="tbtn" onclick="rotatePDF()" title="Rotate">
      <svg viewBox="0 0 24 24"><path d="M7.11 8.53L5.7 7.11C4.8 8.27 4.24 9.61 4.07 11h2.02c.14-.87.49-1.72 1.02-2.47zM6.09 13H4.07c.17 1.39.72 2.73 1.62 3.89l1.41-1.42c-.52-.75-.87-1.59-1.01-2.47zm1.01 5.32c1.16.9 2.51 1.44 3.9 1.61V17.9c-.87-.15-1.71-.49-2.46-1.03L7.1 18.32zM13 4.07V1L8.45 5.55 13 10V6.09c2.84.48 5 2.94 5 5.91s-2.16 5.43-5 5.91v2.02c3.95-.49 7-3.85 7-7.93s-3.05-7.44-7-7.93z"/></svg>
    </button>
    <div class="toolbar-right">
      <button class="tbtn" onclick="actionPDFtoImage()" title="Export as ZIP Images">
        <svg viewBox="0 0 24 24"><path d="M21 19V5H3v14h18zm-9-7l3 4H9l2-3 1 1.5L12 12zm-4.5-2.5a1.5 1.5 0 1 0 3 0 1.5 1.5 0 0 0-3 0z"/></svg>
      </button>
      <button class="tbtn" onclick="actionPDFtoText()" title="Export as TXT">
        <svg viewBox="0 0 24 24"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8l-6-6zm-1 9H7v-1h6v1zm2 4H7v-1h8v1zm0-8V3.5L18.5 7H15z"/></svg>
      </button>
      <button class="tbtn" onclick="actionAddWatermark()" title="Watermark">
        <svg viewBox="0 0 24 24"><path d="M18.5 12a6.5 6.5 0 0 0-13 0 6.5 6.5 0 0 0 13 0zm-6.5 8.5a8.5 8.5 0 1 1 0-17 8.5 8.5 0 0 1 0 17zM9 12l1.5 2 2-3 2.5 4H8z"/></svg>
      </button>
    </div>
  </div>
)HTML";

    // --- PART 13: Workspace + Thumb Panel + Viewer HTML ---
    ss << LR"HTML(
  <div class="workspace">
    <!-- Thumbnail Organizer -->
    <div class="thumb-panel collapsed" id="thumb-panel">
      <div class="thumb-panel-header">
        <span>Pages</span>
        <span style="cursor:pointer;font-size:14px;" onclick="toggleThumbPanel()">&#x2715;</span>
      </div>
      <div class="thumb-list" id="thumb-list"></div>
    </div>
    <!-- PDF Viewer -->
    <div class="pdf-viewer-area" id="viewer-area">
      <div class="pdf-container" id="pdf-container">
        <div id="empty-hint" style="margin-top:120px;text-align:center;color:#888;pointer-events:none;">
          <div style="font-size:48px;opacity:.25;">&#128196;</div>
          <p style="margin-top:10px;font-size:13px;">Open a PDF to get started</p>
          <p style="margin-top:4px;font-size:11px;opacity:.7;">File > Open or drag &amp; drop</p>
        </div>
      </div>
    </div>
    <!-- Right Sidebar -->
    <div class="right-sidebar">
      <div class="sb-header">Tools</div>
      <div class="sb-btn" onclick="uiShowDeleteModal()">
        <svg viewBox="0 0 24 24"><path d="M6 19a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
        <span>Delete Pages</span>
      </div>
      <div class="sb-btn" onclick="actionPerformOCR()">
        <svg viewBox="0 0 24 24"><path d="M9 2H5a1 1 0 0 0-1 1v4h2V4h3V2zm6 0v2h3v3h2V3a1 1 0 0 0-1-1h-4zM4 15H2v4a2 2 0 0 0 2 2h4v-2H4v-4zm16 0v4h-4v2h4a2 2 0 0 0 2-2v-4h-2zM7 7h10v10H7z"/></svg>
        <span>OCR Scanner</span>
      </div>
    </div>
  </div>
  <!-- Static overlays -->
  <div class="toast-box" id="toast-box"></div>
  <div class="loading-overlay" id="loading-overlay">
    <div class="spinner"></div><div id="loading-txt">Processing...</div>
  </div>
  <div class="modal-overlay" id="modal-overlay">
    <div class="modal" id="modal-body"></div>
  </div>
  <input type="file" id="fileInput" accept=".pdf" style="display:none" multiple onchange="handleFileInputChange(event)">
</div>
)HTML";

    // --- PART 14: JS State variables & utils ---
    ss << LR"HTML(
<script>
pdfjsLib.GlobalWorkerOptions.workerSrc = 'https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.worker.min.js';

let tabs = [], activeId = null, currentTool = 'hand';
let isDrawing = false, drawCtx = null, drawCanvas = null;
let drawLastX = 0, drawLastY = 0;
let isPanning = false, panStartX = 0, panStartY = 0, panScrollLeft = 0, panScrollTop = 0;
let renderingScheduled = false;

function activeTab() { return tabs.find(t => t.id === activeId); }

function showToast(msg, dur=3000) {
  const box = document.getElementById('toast-box');
  const el = document.createElement('div'); el.className='toast'; el.textContent=msg;
  box.appendChild(el); setTimeout(()=>el.remove(), dur);
}
function showLoading(v, txt='Processing...') {
  document.getElementById('loading-overlay').classList.toggle('show',v);
  document.getElementById('loading-txt').textContent=txt;
}
function showModal(title, html) {
  document.getElementById('modal-body').innerHTML=`<h3>${title}</h3>${html}`;
  document.getElementById('modal-overlay').classList.add('show');
}
function closeModal() { document.getElementById('modal-overlay').classList.remove('show'); }
document.getElementById('modal-overlay').addEventListener('mousedown', e => { if(e.target.id==='modal-overlay') closeModal(); });

async function saveBytesToFile(blob, name, ext='pdf', mime='application/pdf') {
  try {
    if (window.showSaveFilePicker) {
      const h = await window.showSaveFilePicker({ suggestedName: name, types:[{description:'File',accept:{[mime]:['.'+ext]}}] });
      const w = await h.createWritable(); await w.write(blob); await w.close();
      showToast('Saved successfully!');
    } else { saveAs(blob, name); }
  } catch(e) { if(e.name!=='AbortError') showToast('Save cancelled.'); }
}
</script>
)HTML";

    // --- PART 15: JS File loading & tab management ---
    ss << LR"HTML(
<script>
async function handleFileInputChange(e) {
  for (const f of e.target.files) {
    if (!f.name.toLowerCase().endsWith('.pdf')) continue;
    const bytes = new Uint8Array(await f.arrayBuffer());
    await createTab(f.name, bytes);
  }
  e.target.value='';
}

async function loadPdfFromPath(path) {
  try {
    const res = await fetch('file:///'+path.replace(/\\/g,'/'));
    const bytes = new Uint8Array(await res.arrayBuffer());
    const name = path.split('\\').pop();
    await createTab(name, bytes);
  } catch(e) { showToast('Failed to load PDF.'); }
}

async function createTab(name, bytes) {
  try {
    document.getElementById('empty-hint').style.display='none';
    const doc = await pdfjsLib.getDocument({data: bytes}).promise;
    const id = 'tab_' + Date.now() + Math.random().toString(36).slice(2);
    tabs.push({ id, name, bytes, doc, zoom: 1.0, rotation: 0, annotations: [], pasteImages: [], pageOrder: Array.from({length:doc.numPages},(_,i)=>i+1) });
    renderTabStrip();
    await switchTab(id);
  } catch(e) { showToast('Invalid PDF file.'); }
}

function renderTabStrip() {
  const strip = document.getElementById('tabbar'); strip.innerHTML='';
  tabs.forEach(t => {
    const el = document.createElement('div');
    el.className = 'pdf-tab' + (t.id===activeId?' active':'');
    el.innerHTML = `<span class="tab-name">${t.name.length>22?t.name.slice(0,20)+'…':t.name}</span><span class="tab-close">&#x2715;</span>`;
    el.querySelector('.tab-close').onclick = ev => { ev.stopPropagation(); closeTab(t.id); };
    el.onclick = () => switchTab(t.id);
    strip.appendChild(el);
  });
  const add = document.createElement('div'); add.className='add-tab'; add.textContent='+';
  add.onclick = () => document.getElementById('fileInput').click();
  strip.appendChild(add);
}

async function switchTab(id) {
  activeId = id; renderTabStrip();
  const t = activeTab(); if (!t) return;
  document.getElementById('zoom-display').textContent = Math.round(t.zoom*100)+'%';
  await renderViewer(); buildThumbs();
}

function closeTab(id) {
  tabs = tabs.filter(t=>t.id!==id);
  if (activeId===id) activeId = tabs.length>0?tabs[tabs.length-1].id:null;
  renderTabStrip();
  if (activeId) switchTab(activeId);
  else {
    document.getElementById('pdf-container').innerHTML='<div id="empty-hint" style="margin-top:120px;text-align:center;color:#888;pointer-events:none;"><div style="font-size:48px;opacity:.25;">&#128196;</div><p style="margin-top:10px;font-size:13px;">Open a PDF to get started</p></div>';
    document.getElementById('thumb-list').innerHTML='';
  }
}
</script>
)HTML";

    // --- PART 16: JS Rendering engine ---
    ss << LR"HTML(
<script>
async function renderViewer(scrollToPage) {
  const t = activeTab(); if (!t) return;
  const container = document.getElementById('pdf-container');
  // Keep pasted images and notes before clearing
  container.innerHTML = '';
  for (let i = 0; i < t.pageOrder.length; i++) {
    const pNum = t.pageOrder[i];
    const page = await t.doc.getPage(pNum);
    const vp = page.getViewport({ scale: t.zoom, rotation: t.rotation });
    const wrapper = document.createElement('div');
    wrapper.className = 'page-wrapper';
    wrapper.id = `pw-${i}`;
    wrapper.dataset.pageIndex = i;
    wrapper.dataset.pdfPage = pNum;
    wrapper.style.width = vp.width + 'px';
    wrapper.style.height = vp.height + 'px';

    const pdfCanvas = document.createElement('canvas');
    pdfCanvas.className = 'pdf-canvas';
    pdfCanvas.width = vp.width; pdfCanvas.height = vp.height;

    const drawCanvas = document.createElement('canvas');
    drawCanvas.className = 'draw-canvas';
    drawCanvas.width = vp.width; drawCanvas.height = vp.height;
    drawCanvas.style.width = vp.width+'px'; drawCanvas.style.height = vp.height+'px';

    wrapper.appendChild(pdfCanvas);
    wrapper.appendChild(drawCanvas);
    container.appendChild(wrapper);

    // Render PDF page async
    page.render({ canvasContext: pdfCanvas.getContext('2d'), viewport: vp });

    // Restore draw annotations for this page index
    restoreDrawAnnotations(i, drawCanvas, vp.width, vp.height);
    // Restore pasted images
    restorePasteImages(t, i, wrapper, vp.width, vp.height);
    // Restore sticky notes
    restoreStickyNotes(t, i, wrapper);
  }
  if (scrollToPage !== undefined) {
    const pw = document.getElementById(`pw-${scrollToPage}`);
    if (pw) pw.scrollIntoView({block:'start',behavior:'smooth'});
  }
}

function restoreDrawAnnotations(pageIndex, canvas, w, h) {
  const t = activeTab(); if (!t) return;
  const ctx = canvas.getContext('2d');
  t.annotations.filter(a=>a.pageIndex===pageIndex).forEach(a=>{
    if (a.type==='stroke') {
      ctx.save();
      ctx.strokeStyle = a.color; ctx.lineWidth = a.lineWidth;
      ctx.lineCap='round'; ctx.lineJoin='round'; ctx.globalAlpha=a.alpha||1;
      ctx.beginPath();
      a.points.forEach((p,i)=>{ const px=p.rx*w, py=p.ry*h; i===0?ctx.moveTo(px,py):ctx.lineTo(px,py); });
      ctx.stroke(); ctx.restore();
    }
  });
}
</script>
)HTML";

    // --- PART 17: JS Zoom, Rotate, keyboard ---
    ss << LR"HTML(
<script>
function zoomBy(delta) {
  const t = activeTab(); if (!t) return;
  t.zoom = Math.min(4, Math.max(0.2, t.zoom + delta));
  document.getElementById('zoom-display').textContent = Math.round(t.zoom*100)+'%';
  scheduleRender();
}
function rotatePDF() {
  const t = activeTab(); if (!t) return;
  t.rotation = (t.rotation + 90) % 360;
  scheduleRender();
}
let renderTimer = null;
function scheduleRender(delay=60) {
  if (renderTimer) clearTimeout(renderTimer);
  renderTimer = setTimeout(()=>renderViewer(), delay);
}

// Smooth wheel zoom
document.getElementById('viewer-area').addEventListener('wheel', e=>{
  if (!e.ctrlKey) return;
  e.preventDefault();
  const t = activeTab(); if (!t) return;
  const delta = e.deltaY > 0 ? -0.08 : 0.08;
  t.zoom = Math.min(4, Math.max(0.2, t.zoom + delta));
  document.getElementById('zoom-display').textContent = Math.round(t.zoom*100)+'%';
  scheduleRender(50);
}, {passive:false});

document.addEventListener('keydown', e=>{
  if (e.key==='h'||e.key==='H') setTool('hand');
  if (e.key==='p'||e.key==='P') setTool('pen');
  if (e.key==='n'||e.key==='N') setTool('note');
  if (e.key==='s'||e.key==='S') setTool('select');
  if (e.key==='+') zoomBy(0.15);
  if (e.key==='-') zoomBy(-0.15);
  if (e.key==='Escape') closeModal();
});
</script>
)HTML";

    // --- PART 18: JS Tool selection & Hand pan ---
    ss << LR"HTML(
<script>
function setTool(tool) {
  currentTool = tool;
  document.querySelectorAll('.tbtn[id^="tool-"]').forEach(b=>b.classList.remove('active'));
  const btn = document.getElementById('tool-'+tool);
  if (btn) btn.classList.add('active');

  const area = document.getElementById('viewer-area');
  // Toggle draw canvas pointer events
  document.querySelectorAll('.draw-canvas').forEach(c=>{
    if (tool==='pen'||tool==='highlight') {
      c.classList.add('active-draw');
    } else {
      c.classList.remove('active-draw');
    }
  });
  area.style.cursor = tool==='hand' ? 'grab' : (tool==='pen'||tool==='highlight') ? 'crosshair' : 'default';
}

// Hand pan logic on viewer area
const viewerArea = document.getElementById('viewer-area');
viewerArea.addEventListener('mousedown', e=>{
  if (currentTool !== 'hand') return;
  if (e.button !== 0) return;
  isPanning=true; panStartX=e.clientX; panStartY=e.clientY;
  panScrollLeft=viewerArea.scrollLeft; panScrollTop=viewerArea.scrollTop;
  viewerArea.style.cursor='grabbing'; e.preventDefault();
});
window.addEventListener('mousemove', e=>{
  if (!isPanning) return;
  viewerArea.scrollLeft = panScrollLeft - (e.clientX - panStartX);
  viewerArea.scrollTop = panScrollTop - (e.clientY - panStartY);
});
window.addEventListener('mouseup', e=>{
  if (isPanning) { isPanning=false; if(currentTool==='hand') viewerArea.style.cursor='grab'; }
});
</script>
)HTML";

    // --- PART 19: JS Real-time canvas drawing (pen & highlight) ---
    ss << LR"HTML(
<script>
let currentStroke = null;

function getCanvasPos(canvas, e) {
  const rect = canvas.getBoundingClientRect();
  return { x: e.clientX - rect.left, y: e.clientY - rect.top };
}

document.getElementById('pdf-container').addEventListener('mousedown', e=>{
  if (currentTool !== 'pen' && currentTool !== 'highlight') return;
  const canvas = e.target;
  if (!canvas.classList.contains('draw-canvas')) return;
  isDrawing = true;
  drawCanvas = canvas; drawCtx = canvas.getContext('2d');
  const pos = getCanvasPos(canvas, e);
  drawLastX = pos.x; drawLastY = pos.y;
  const t = activeTab(); if (!t) return;
  const wrapper = canvas.parentElement;
  const pIdx = parseInt(wrapper.dataset.pageIndex);
  currentStroke = {
    type: 'stroke',
    pageIndex: pIdx,
    color: currentTool==='highlight' ? 'rgba(255,220,0,0.38)' : '#e53935',
    lineWidth: currentTool==='highlight' ? 14 : 2,
    alpha: currentTool==='highlight' ? 0.45 : 1.0,
    points: [{ rx: pos.x / canvas.width, ry: pos.y / canvas.height }]
  };
  drawCtx.save();
  drawCtx.strokeStyle = currentStroke.color;
  drawCtx.lineWidth = currentStroke.lineWidth;
  drawCtx.lineCap = 'round'; drawCtx.lineJoin = 'round';
  drawCtx.globalAlpha = currentStroke.alpha;
  drawCtx.beginPath(); drawCtx.moveTo(pos.x, pos.y);
});

window.addEventListener('mousemove', e=>{
  if (!isDrawing || !drawCtx || !drawCanvas) return;
  const pos = getCanvasPos(drawCanvas, e);
  drawCtx.lineTo(pos.x, pos.y);
  drawCtx.stroke();
  drawCtx.beginPath(); drawCtx.moveTo(pos.x, pos.y);
  drawLastX = pos.x; drawLastY = pos.y;
  if (currentStroke) {
    currentStroke.points.push({ rx: pos.x / drawCanvas.width, ry: pos.y / drawCanvas.height });
  }
});

window.addEventListener('mouseup', e=>{
  if (!isDrawing) return;
  isDrawing = false;
  if (drawCtx) { drawCtx.restore(); drawCtx = null; }
  const t = activeTab();
  if (t && currentStroke && currentStroke.points.length > 1) {
    t.annotations.push(currentStroke);
  }
  currentStroke = null; drawCanvas = null;
});
</script>
)HTML";

    // --- PART 20: JS Sticky Notes ---
    ss << LR"HTML(
<script>
function restoreStickyNotes(tab, pageIndex, wrapper) {
  if (!tab.annotations) return;
  tab.annotations.filter(a=>a.type==='note'&&a.pageIndex===pageIndex).forEach(a=>{
    createNoteElement(a, wrapper);
  });
}

function createNoteElement(noteData, wrapper) {
  const note = document.createElement('div');
  note.className = 'sticky-note';
  note.style.left = (noteData.rx * 100) + '%';
  note.style.top = (noteData.ry * 100) + '%';
  note.contentEditable = 'true';
  note.textContent = noteData.text || 'Note...';
  note.dataset.annotId = noteData.id;
  const close = document.createElement('span');
  close.className='note-close'; close.textContent='×';
  close.onclick = ()=>{
    const t = activeTab(); if (!t) return;
    t.annotations = t.annotations.filter(a=>a.id!==noteData.id);
    note.remove();
  };
  note.appendChild(close);
  note.addEventListener('input', ()=>{ noteData.text = note.textContent; });
  makeDraggable(note, wrapper, noteData);
  wrapper.appendChild(note);
}

document.getElementById('pdf-container').addEventListener('click', e=>{
  if (currentTool !== 'note') return;
  const wrapper = e.target.closest('.page-wrapper'); if (!wrapper) return;
  const rect = wrapper.getBoundingClientRect();
  const rx = (e.clientX - rect.left) / rect.width;
  const ry = (e.clientY - rect.top) / rect.height;
  const t = activeTab(); if (!t) return;
  const pageIndex = parseInt(wrapper.dataset.pageIndex);
  const noteData = { type:'note', id:'n_'+Date.now(), pageIndex, rx, ry, text:'' };
  t.annotations.push(noteData);
  createNoteElement(noteData, wrapper);
});

function makeDraggable(el, container, data) {
  let sx=0, sy=0, ex=0, ey=0, dragging=false;
  el.addEventListener('mousedown', e=>{
    if (e.target.contentEditable==='true'&&e.target!==el) return;
    dragging=true; sx=e.clientX; sy=e.clientY;
    ex=el.offsetLeft; ey=el.offsetTop; e.preventDefault();
  });
  window.addEventListener('mousemove', e=>{
    if (!dragging) return;
    const nx=ex+(e.clientX-sx), ny=ey+(e.clientY-sy);
    el.style.left=nx+'px'; el.style.top=ny+'px';
    if (data) { data.rx=nx/container.offsetWidth; data.ry=ny/container.offsetHeight; }
  });
  window.addEventListener('mouseup', ()=>{ dragging=false; });
}
</script>
)HTML";

    // --- PART 21: JS Paste Image (interactive) ---
    ss << LR"HTML(
<script>
function restorePasteImages(tab, pageIndex, wrapper, w, h) {
  if (!tab.pasteImages) return;
  tab.pasteImages.filter(img=>img.pageIndex===pageIndex).forEach(imgData=>{
    createImageOverlay(imgData, wrapper, w, h);
  });
}

function createImageOverlay(imgData, wrapper, pw, ph) {
  const div = document.createElement('div');
  div.className = 'paste-img-wrapper';
  div.style.left = (imgData.rx * 100) + '%';
  div.style.top = (imgData.ry * 100) + '%';
  div.style.width = (imgData.rw * 100) + '%';
  div.style.height = (imgData.rh * 100) + '%';
  div.dataset.imgId = imgData.id;
  const img = document.createElement('img');
  img.src = imgData.src; div.appendChild(img);
  // Resize handles
  ['nw','ne','sw','se'].forEach(dir=>{
    const h = document.createElement('div');
    h.className='resize-handle '+dir;
    h.addEventListener('mousedown', e=>startResize(e, div, dir, wrapper, imgData));
    div.appendChild(h);
  });
  makeDraggable(div, wrapper, null);
  div.addEventListener('mouseup', ()=>updateImgDataFromEl(div, imgData, wrapper));
  wrapper.appendChild(div);
}

function updateImgDataFromEl(el, imgData, wrapper) {
  imgData.rx = el.offsetLeft / wrapper.offsetWidth;
  imgData.ry = el.offsetTop / wrapper.offsetHeight;
  imgData.rw = el.offsetWidth / wrapper.offsetWidth;
  imgData.rh = el.offsetHeight / wrapper.offsetHeight;
}

function startResize(e, el, dir, container, imgData) {
  e.stopPropagation(); e.preventDefault();
  const startX=e.clientX, startY=e.clientY;
  const startW=el.offsetWidth, startH=el.offsetHeight;
  const startL=el.offsetLeft, startT=el.offsetTop;
  function onMove(ev) {
    const dx=ev.clientX-startX, dy=ev.clientY-startY;
    if (dir==='se') { el.style.width=Math.max(40,startW+dx)+'px'; el.style.height=Math.max(30,startH+dy)+'px'; }
    if (dir==='sw') { el.style.width=Math.max(40,startW-dx)+'px'; el.style.left=Math.max(0,startL+dx)+'px'; el.style.height=Math.max(30,startH+dy)+'px'; }
    if (dir==='ne') { el.style.width=Math.max(40,startW+dx)+'px'; el.style.height=Math.max(30,startH-dy)+'px'; el.style.top=Math.max(0,startT+dy)+'px'; }
    if (dir==='nw') { el.style.width=Math.max(40,startW-dx)+'px'; el.style.left=Math.max(0,startL+dx)+'px'; el.style.height=Math.max(30,startH-dy)+'px'; el.style.top=Math.max(0,startT+dy)+'px'; }
  }
  function onUp() {
    window.removeEventListener('mousemove',onMove);
    window.removeEventListener('mouseup',onUp);
    updateImgDataFromEl(el, imgData, container);
  }
  window.addEventListener('mousemove',onMove);
  window.addEventListener('mouseup',onUp);
}

document.addEventListener('paste', async e=>{
  const t = activeTab(); if (!t) return;
  const items = e.clipboardData.items;
  let file=null;
  for (let i=0;i<items.length;i++) { if(items[i].type.startsWith('image')){file=items[i].getAsFile();break;} }
  if (!file) return;
  const buf = new Uint8Array(await file.arrayBuffer());
  const src = URL.createObjectURL(file);
  const container = document.getElementById('pdf-container');
  const firstWrapper = container.querySelector('.page-wrapper');
  if (!firstWrapper) { showToast('Open a PDF first.'); return; }
  const pageIndex = parseInt(firstWrapper.dataset.pageIndex)||0;
  const imgData = {
    id: 'img_'+Date.now(), type:'pasteImage', pageIndex,
    rx:0.1, ry:0.1, rw:0.4, rh:0.3,
    src, buffer:buf, mimeType:file.type
  };
  t.pasteImages.push(imgData);
  createImageOverlay(imgData, firstWrapper, firstWrapper.offsetWidth, firstWrapper.offsetHeight);
  showToast('Image pasted — drag and resize freely!');
});
</script>
)HTML";

    // --- PART 22: JS Page Thumbnail Builder ---
    ss << LR"HTML(
<script>
async function buildThumbs() {
  const t = activeTab(); if (!t) return;
  const list = document.getElementById('thumb-list'); list.innerHTML='';
  for (let i=0; i<t.pageOrder.length; i++) {
    const pNum = t.pageOrder[i];
    const page = await t.doc.getPage(pNum);
    const vp = page.getViewport({scale:0.18});
    const item = document.createElement('div');
    item.className='thumb-item'; item.dataset.orderIndex=i;
    const canvas = document.createElement('canvas');
    canvas.width=vp.width; canvas.height=vp.height;
    page.render({canvasContext:canvas.getContext('2d'), viewport:vp});
    const num = document.createElement('div'); num.className='thumb-num'; num.textContent=i+1;
    const del = document.createElement('div'); del.className='thumb-del'; del.textContent='×';
    del.onclick = e=>{ e.stopPropagation(); deletePageByOrderIndex(i); };
    item.appendChild(canvas); item.appendChild(num); item.appendChild(del);
    item.onclick = ()=>{ document.getElementById(`pw-${i}`)?.scrollIntoView({block:'start',behavior:'smooth'}); };
    setupThumbDrag(item, list, i);
    list.appendChild(item);
  }
}

function deletePageByOrderIndex(orderIndex) {
  const t = activeTab(); if (!t) return;
  if (t.pageOrder.length<=1) { showToast('Cannot delete the only page.'); return; }
  t.pageOrder.splice(orderIndex,1);
  t.annotations = t.annotations.filter(a=>a.pageIndex!==orderIndex)
    .map(a=>({...a, pageIndex: a.pageIndex>orderIndex?a.pageIndex-1:a.pageIndex}));
  t.pasteImages = t.pasteImages.filter(img=>img.pageIndex!==orderIndex)
    .map(img=>({...img, pageIndex: img.pageIndex>orderIndex?img.pageIndex-1:img.pageIndex}));
  renderViewer(); buildThumbs(); showToast('Page deleted.');
}

function setupThumbDrag(item, list, startIdx) {
  item.draggable=true;
  item.addEventListener('dragstart', e=>{ e.dataTransfer.setData('text/plain',startIdx); item.style.opacity='0.4'; });
  item.addEventListener('dragend', ()=>{ item.style.opacity='1'; });
  item.addEventListener('dragover', e=>{ e.preventDefault(); item.style.outline='2px dashed var(--accent)'; });
  item.addEventListener('dragleave', ()=>{ item.style.outline=''; });
  item.addEventListener('drop', e=>{
    e.preventDefault(); item.style.outline='';
    const fromIdx=parseInt(e.dataTransfer.getData('text/plain'));
    const toIdx=startIdx;
    if (fromIdx===toIdx) return;
    const t=activeTab(); if (!t) return;
    const moved=t.pageOrder.splice(fromIdx,1)[0];
    t.pageOrder.splice(toIdx,0,moved);
    renderViewer(); buildThumbs();
  });
}

function toggleThumbPanel() {
  document.getElementById('thumb-panel').classList.toggle('collapsed');
  const t=activeTab(); if(t) buildThumbs();
}
</script>
)HTML";

    // --- PART 23: JS Save PDF with baked annotations ---
    ss << LR"HTML(
<script>
async function downloadCurrentPDF() {
  const t = activeTab(); if (!t) { showToast('No file open.'); return; }
  showLoading(true, 'Baking annotations…');
  try {
    const srcDoc = await PDFLib.PDFDocument.load(t.bytes);
    const newDoc = await PDFLib.PDFDocument.create();
    // Reorder pages
    const copiedPages = await newDoc.copyPages(srcDoc, t.pageOrder.map(n=>n-1));
    copiedPages.forEach(p=>newDoc.addPage(p));
    const pages = newDoc.getPages();

    for (let a of t.annotations) {
      if (a.pageIndex >= pages.length) continue;
      const page = pages[a.pageIndex];
      const { width, height } = page.getSize();
      if (a.type === 'stroke') {
        if (!a.points||a.points.length<2) continue;
        const isHL = a.alpha<1;
        page.drawLine({
          start: {x:a.points[0].rx*width, y:height-(a.points[0].ry*height)},
          end: {x:a.points[a.points.length-1].rx*width, y:height-(a.points[a.points.length-1].ry*height)},
          thickness: a.lineWidth,
          color: isHL ? PDFLib.rgb(1,0.86,0) : PDFLib.rgb(0.9,0.22,0.21),
          opacity: a.alpha||1
        });
        // For multi-point strokes, draw all segments
        for (let i=1;i<a.points.length;i++) {
          page.drawLine({
            start:{x:a.points[i-1].rx*width,y:height-(a.points[i-1].ry*height)},
            end:{x:a.points[i].rx*width,y:height-(a.points[i].ry*height)},
            thickness:a.lineWidth,
            color: isHL?PDFLib.rgb(1,0.86,0):PDFLib.rgb(0.9,0.22,0.21),
            opacity:a.alpha||1
          });
        }
      } else if (a.type==='note') {
        const x=a.rx*width, y=height-(a.ry*height);
        page.drawRectangle({x,y,width:160,height:32,color:PDFLib.rgb(1,0.97,0.77),borderColor:PDFLib.rgb(0.8,0.75,0.1),borderWidth:1,opacity:0.9});
        page.drawText(a.text||'', {x:x+4,y:y+10,size:9,color:PDFLib.rgb(0.15,0.15,0.15),maxWidth:150});
      }
    }

    // Bake paste images
    for (let img of t.pasteImages) {
      if (img.pageIndex >= pages.length) continue;
      const page = pages[img.pageIndex];
      const { width, height } = page.getSize();
      let embedded;
      try {
        if (img.mimeType==='image/png') embedded = await newDoc.embedPng(img.buffer);
        else embedded = await newDoc.embedJpg(img.buffer);
        const imgW = img.rw*width, imgH = img.rh*height;
        page.drawImage(embedded, {
          x: img.rx*width,
          y: height-(img.ry*height)-imgH,
          width: imgW, height: imgH
        });
      } catch(e) {}
    }
    const saved = await newDoc.save();
    await saveBytesToFile(new Blob([saved],{type:'application/pdf'}), t.name);
  } catch(e) { showToast('Save failed: '+e.message); }
  showLoading(false);
}
</script>
)HTML";

    // --- PART 24: JS Merge PDFs ---
    ss << LR"HTML(
<script>
function uiShowMergeModal() {
  showModal('Combine PDFs', `
    <p style="font-size:11px;color:#666;margin-bottom:8px;">Select 2 or more PDF files to merge.</p>
    <input type="file" id="merge-files" accept=".pdf" multiple style="font-size:11px;margin-bottom:12px;">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="actionMerge()">Combine</button>
    </div>`);
}
async function actionMerge() {
  const files = document.getElementById('merge-files').files;
  if (files.length<2) { showToast('Select at least 2 files.'); return; }
  closeModal(); showLoading(true,'Merging…');
  try {
    const out = await PDFLib.PDFDocument.create();
    for (const f of files) {
      const bytes = new Uint8Array(await f.arrayBuffer());
      const src = await PDFLib.PDFDocument.load(bytes);
      const pages = await out.copyPages(src, src.getPageIndices());
      pages.forEach(p=>out.addPage(p));
    }
    await saveBytesToFile(new Blob([await out.save()],{type:'application/pdf'}), 'Combined.pdf');
  } catch(e) { showToast('Merge failed.'); }
  showLoading(false);
}
</script>
)HTML";

    // --- PART 25: JS Split PDF ---
    ss << LR"HTML(
<script>
function uiShowSplitModal() {
  const t=activeTab(); if(!t){showToast('Open a PDF.');return;}
  showModal('Split PDF', `
    <p style="font-size:11px;color:#666;margin-bottom:8px;">Split after page (1–${t.doc.numPages-1}):</p>
    <input type="number" id="split-at" min="1" max="${t.doc.numPages-1}" value="1">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="actionSplit()">Split</button>
    </div>`);
}
async function actionSplit() {
  const t=activeTab(); const sp=parseInt(document.getElementById('split-at').value);
  if (!t||isNaN(sp)) return;
  closeModal(); showLoading(true,'Splitting…');
  try {
    const src = await PDFLib.PDFDocument.load(t.bytes);
    const all = src.getPageIndices();
    const d1=await PDFLib.PDFDocument.create(), d2=await PDFLib.PDFDocument.create();
    (await d1.copyPages(src,all.slice(0,sp))).forEach(p=>d1.addPage(p));
    (await d2.copyPages(src,all.slice(sp))).forEach(p=>d2.addPage(p));
    await saveBytesToFile(new Blob([await d1.save()]),'Part1.pdf');
    await saveBytesToFile(new Blob([await d2.save()]),'Part2.pdf');
  } catch(e){showToast('Split failed.');}
  showLoading(false);
}
</script>
)HTML";

    // --- PART 26: JS Extract pages ---
    ss << LR"HTML(
<script>
function uiShowExtractModal() {
  const t=activeTab(); if(!t){showToast('Open a PDF.');return;}
  showModal('Extract Pages', `
    <p style="font-size:11px;color:#666;margin-bottom:8px;">Page numbers e.g. <code>1, 3, 5</code>:</p>
    <input type="text" id="extract-pages" placeholder="1, 2, 3">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="actionExtract()">Extract</button>
    </div>`);
}
async function actionExtract() {
  const t=activeTab(); const val=document.getElementById('extract-pages').value;
  const pageNums = val.split(',').map(s=>parseInt(s.trim())-1).filter(n=>!isNaN(n)&&n>=0&&n<t.doc.numPages);
  if (!pageNums.length){showToast('No valid pages.');return;}
  closeModal(); showLoading(true,'Extracting…');
  try {
    const src=await PDFLib.PDFDocument.load(t.bytes);
    const out=await PDFLib.PDFDocument.create();
    (await out.copyPages(src,pageNums)).forEach(p=>out.addPage(p));
    await saveBytesToFile(new Blob([await out.save()]),'Extracted.pdf');
  } catch(e){showToast('Extract failed.');}
  showLoading(false);
}
</script>
)HTML";

    // --- PART 27: JS Delete pages ---
    ss << LR"HTML(
<script>
function uiShowDeleteModal() {
  const t=activeTab(); if(!t){showToast('Open a PDF.');return;}
  showModal('Delete Pages', `
    <p style="font-size:11px;color:#666;margin-bottom:8px;">Page number(s) to delete e.g. <code>2, 4</code>:</p>
    <input type="text" id="delete-pages" placeholder="1">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" style="background:#c0392b;" onclick="actionDeletePages()">Delete</button>
    </div>`);
}
async function actionDeletePages() {
  const t=activeTab(); const val=document.getElementById('delete-pages').value;
  const toDelete=new Set(val.split(',').map(s=>parseInt(s.trim())-1).filter(n=>!isNaN(n)&&n>=0&&n<t.pageOrder.length));
  if (!toDelete.size){showToast('No valid pages.');return;}
  if (t.pageOrder.length-toDelete.size<1){showToast('Cannot delete all pages.');return;}
  closeModal();
  toDelete.forEach(idx=>{
    t.pageOrder.splice(idx,1);
    t.annotations=t.annotations.filter(a=>a.pageIndex!==idx).map(a=>({...a,pageIndex:a.pageIndex>idx?a.pageIndex-1:a.pageIndex}));
    t.pasteImages=t.pasteImages.filter(img=>img.pageIndex!==idx).map(img=>({...img,pageIndex:img.pageIndex>idx?img.pageIndex-1:img.pageIndex}));
  });
  renderViewer(); buildThumbs(); showToast('Page(s) deleted.');
}
</script>
)HTML";

    // --- PART 28: JS Watermark ---
    ss << LR"HTML(
<script>
async function actionAddWatermark() {
  const t=activeTab(); if(!t){showToast('Open a PDF.');return;}
  const txt=prompt('Watermark text:'); if(!txt) return;
  showLoading(true,'Applying watermark…');
  try {
    const doc=await PDFLib.PDFDocument.load(t.bytes);
    doc.getPages().forEach(page=>{
      const {width,height}=page.getSize();
      page.drawText(txt,{
        x:width/2-100, y:height/2,
        size:52, color:PDFLib.rgb(0.8,0.1,0.1), opacity:0.2,
        rotate:PDFLib.degrees(40)
      });
    });
    await saveBytesToFile(new Blob([await doc.save()]),'Watermarked.pdf');
  } catch(e){showToast('Watermark failed.');}
  showLoading(false);
}
</script>
)HTML";

    // --- PART 29: JS Export images/text/OCR ---
    ss << LR"HTML(
<script>
async function actionPDFtoImage() {
  const t=activeTab(); if(!t){showToast('Open a PDF.');return;}
  showLoading(true,'Exporting images…');
  try {
    const zip=new JSZip();
    for (let i=0;i<t.pageOrder.length;i++) {
      const pNum=t.pageOrder[i];
      const page=await t.doc.getPage(pNum);
      const vp=page.getViewport({scale:2.0});
      const c=document.createElement('canvas'); c.width=vp.width; c.height=vp.height;
      await page.render({canvasContext:c.getContext('2d'),viewport:vp}).promise;
      zip.file(`Page_${i+1}.png`, await new Promise(r=>c.toBlob(r,'image/png')));
    }
    await saveBytesToFile(await zip.generateAsync({type:'blob'}),'Images.zip','zip','application/zip');
  } catch(e){showToast('Export failed.');}
  showLoading(false);
}

async function actionPDFtoText() {
  const t=activeTab(); if(!t){showToast('Open a PDF.');return;}
  showLoading(true,'Extracting text…');
  try {
    let out='';
    for (let i=0;i<t.pageOrder.length;i++) {
      const page=await t.doc.getPage(t.pageOrder[i]);
      const content=await page.getTextContent();
      out+=`\n--- Page ${i+1} ---\n`+content.items.map(x=>x.str).join(' ')+'\n';
    }
    await saveBytesToFile(new Blob([out],{type:'text/plain'}),'Extracted.txt','txt','text/plain');
  } catch(e){showToast('Text export failed.');}
  showLoading(false);
}

async function actionPerformOCR() {
  const t=activeTab(); if(!t){showToast('Open a PDF.');return;}
  showLoading(true,'Running OCR…');
  try {
    const page=await t.doc.getPage(t.pageOrder[0]);
    const vp=page.getViewport({scale:2.0});
    const c=document.createElement('canvas'); c.width=vp.width; c.height=vp.height;
    await page.render({canvasContext:c.getContext('2d'),viewport:vp}).promise;
    const result=await Tesseract.recognize(c.toDataURL('image/png'),'eng');
    await saveBytesToFile(new Blob([result.data.text],{type:'text/plain'}),'OCR_Result.txt','txt','text/plain');
    showToast('OCR complete!');
  } catch(e){showToast('OCR failed.');}
  showLoading(false);
}
</script>
)HTML";

    // --- PART 30: JS Night mode, read mode, drag-drop ---
    ss << LR"HTML(
<script>
function toggleNightMode() { document.body.classList.toggle('night-mode'); }
function toggleReadMode() { document.body.classList.toggle('read-mode'); setTimeout(()=>{const t=activeTab();if(t)renderViewer();},80); }

// Drag and drop open PDF from OS
const viewerArea=document.getElementById('viewer-area');
viewerArea.addEventListener('dragover',e=>{e.preventDefault();viewerArea.style.outline='3px dashed var(--accent)';});
viewerArea.addEventListener('dragleave',()=>{viewerArea.style.outline='';});
viewerArea.addEventListener('drop',async e=>{
  e.preventDefault(); viewerArea.style.outline='';
  for (const f of e.dataTransfer.files) {
    if (!f.name.toLowerCase().endsWith('.pdf')) continue;
    const bytes=new Uint8Array(await f.arrayBuffer());
    await createTab(f.name,bytes);
  }
});
</script>
)HTML";

    // --- PART 31: JS Init & WebView2 bridge ---
    ss << LR"HTML(
<script>
// Auto-fit on window resize
window.addEventListener('resize', ()=>{
  if (g_webViewController_bridge) return;
  scheduleRender(100);
});

// Try to load path from C++ side if injected early
window.pendingPdfPath = null;
window.loadPdfFromPath = async function(path) {
  await loadPdfFromPath(path);
};

// Keyboard shortcut reference
document.addEventListener('keydown', e=>{
  if ((e.ctrlKey||e.metaKey) && e.key==='o') {
    e.preventDefault(); document.getElementById('fileInput').click();
  }
  if ((e.ctrlKey||e.metaKey) && e.key==='s') {
    e.preventDefault(); downloadCurrentPDF();
  }
  if ((e.ctrlKey||e.metaKey) && e.key==='0') {
    e.preventDefault();
    const t=activeTab(); if(t){t.zoom=1.0;document.getElementById('zoom-display').textContent='100%';scheduleRender();}
  }
});

// Tool hotkeys (only when not typing in note)
document.addEventListener('keydown', e=>{
  if (e.target.contentEditable==='true') return;
  if (e.key==='1') setTool('hand');
  if (e.key==='2') setTool('select');
  if (e.key==='3') setTool('highlight');
  if (e.key==='4') setTool('pen');
  if (e.key==='5') setTool('note');
});
</script>
)HTML";

    // --- PART 32: Close HTML ---
    ss << LR"HTML(
</body>
</html>
)HTML";

    return ss.str();
}

// ==========================================
// WINDOW PROCEDURE
// ==========================================
LRESULT CALLBACK AcrobatViewerWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        RECT r; GetClientRect(hWnd, &r);
        g_hWebViewWnd = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, r.right, r.bottom, hWnd, (HMENU)1001, GetModuleHandle(NULL), NULL);
        InitializeWebView2(hWnd, g_hWebViewWnd);
        break;
    }
    case WM_SIZE: {
        if (g_hWebViewWnd && g_webViewController) {
            RECT r; GetClientRect(hWnd, &r);
            SetWindowPos(g_hWebViewWnd, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER);
            g_webViewController->put_Bounds(RECT{ 0, 0, r.right, r.bottom });
        }
        break;
    }
    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    case WM_DESTROY: {
        if (g_webViewController) { g_webViewController->Close(); g_webViewController = nullptr; }
        g_webView = nullptr; g_webViewEnv = nullptr; g_webViewInitialized = false;
        if (g_hWebViewWnd) { DestroyWindow(g_hWebViewWnd); g_hWebViewWnd = NULL; }
        g_hAcrobatWnd = NULL;
        break;
    }
    default: return DefWindowProcW(hWnd, msg, wp, lp);
    }
    return 0;
}

// ==========================================
// WEBVIEW2 INITIALIZATION
// ==========================================
HRESULT InitializeWebView2(HWND hWnd, HWND hHostWnd) {
    auto envHandler = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [hWnd, hHostWnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result)) return result;
            g_webViewEnv = env;

            auto ctrlHandler = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [hWnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                    if (FAILED(result)) return result;
                    g_webViewController = controller;
                    g_webViewController->get_CoreWebView2(&g_webView);

                    ICoreWebView2Settings* settings;
                    g_webView->get_Settings(&settings);
                    settings->put_IsScriptEnabled(TRUE);
                    settings->put_IsWebMessageEnabled(TRUE);

                    RECT r; GetClientRect(hWnd, &r);
                    g_webViewController->put_Bounds(RECT{ 0, 0, r.right, r.bottom });

                    g_webView->NavigateToString(GetAcrobatHTML().c_str());

                    auto navHandler = Callback<ICoreWebView2NavigationCompletedEventHandler>(
                        [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                            BOOL success; args->get_IsSuccess(&success);
                            if (success) {
                                g_webViewInitialized = true;
                                if (!g_acrobatPdfPath.empty()) {
                                    std::wstring escaped = g_acrobatPdfPath;
                                    size_t pos = 0;
                                    while ((pos = escaped.find(L"\\", pos)) != std::wstring::npos) {
                                        escaped.replace(pos, 1, L"\\\\");
                                        pos += 2;
                                    }
                                    std::wstring script = L"loadPdfFromPath('" + escaped + L"');";
                                    sender->ExecuteScript(script.c_str(), nullptr);
                                }
                            }
                            return S_OK;
                        }
                    );
                    g_webView->add_NavigationCompleted(navHandler.Get(), nullptr);
                    return S_OK;
                }
            );
            env->CreateCoreWebView2Controller(hHostWnd, ctrlHandler.Get());
            return S_OK;
        }
    );
    return CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr, envHandler.Get());
}

// ==========================================
// LAUNCH PDF VIEWER
// ==========================================
void LaunchFoxitStylePdfReader(std::wstring pdfPath) {
    g_acrobatPdfPath = pdfPath;

    if (g_hAcrobatWnd != NULL) {
        ShowWindow(g_hAcrobatWnd, SW_RESTORE);
        SetForegroundWindow(g_hAcrobatWnd);
        if (g_webViewInitialized && g_webView && !pdfPath.empty()) {
            std::wstring escaped = pdfPath;
            size_t pos = 0;
            while ((pos = escaped.find(L"\\", pos)) != std::wstring::npos) {
                escaped.replace(pos, 1, L"\\\\");
                pos += 2;
            }
            std::wstring script = L"loadPdfFromPath('" + escaped + L"');";
            g_webView->ExecuteScript(script.c_str(), nullptr);
        }
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = AcrobatViewerWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"AcrobatWorkspaceClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    g_hAcrobatWnd = CreateWindowExW(
        0, L"AcrobatWorkspaceClass", L"RasFocus — PDF Pro",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        (int)(1280 * g_scaleFactor), (int)(820 * g_scaleFactor),
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101));
    if (hIcon) {
        SendMessage(g_hAcrobatWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(g_hAcrobatWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }

    ShowWindow(g_hAcrobatWnd, SW_SHOWMAXIMIZED);
    SetForegroundWindow(g_hAcrobatWnd);
    UpdateWindow(g_hAcrobatWnd);
}

// ==========================================
// LEGACY STUBS
// ==========================================
void DrawPdfWorkspaceTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch) {
    FontFamily ff(L"Segoe UI");
    Font fText(&ff, 18 * g_scaleFactor, FontStyleRegular, UnitPixel);
    SolidBrush textBrush(Color(255, 120, 120, 120));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"PDF Workspace — double-click a PDF to open.", -1, &fText, RectF(cx, cy, cw, ch), &fmt, &textBrush);
}
void ProcessPdfWorkspaceMouseMove(float x, float y) {}
void ProcessPdfWorkspaceMouseClick(float x, float y) {}
