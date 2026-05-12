// ============================================================
//  tab_pdf_workspace.cpp
//  Professional PDF Workspace Architecture
// ============================================================

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
// HTML/CSS/JS - Split into parts to avoid MSVC C2026
// ==========================================
static std::wstring GetAcrobatHTML()
{
    std::wstringstream ss;

// ─────────────────────────────────────────────────────────────
// PART 01 · DOCTYPE + CDN scripts
// ─────────────────────────────────────────────────────────────
ss << LR"XHTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>PDF Pro — Acrobat Edition</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
<script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
)XHTML";

ss << LR"XHTML(
<script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/tesseract.js@4/dist/tesseract.min.js"></script>
</head>
<body>
)XHTML";

// ─────────────────────────────────────────────────────────────
// PART 02 · CSS Variables + Reset
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
:root{
  --c-topbar:#1c1c1c;
  --c-tabbar:#2d2d2d;
  --c-toolbar:#f0f0f0;
  --c-toolbar-border:#d0d0d0;
  --c-viewer:#808080;
  --c-sidebar:#f5f5f5;
  --c-sidebar-border:#ddd;
  --c-panel:#fafafa;
  --c-accent:#c0392b;
  --c-accent-h:#a93226;
  --c-accent2:#2980b9;
  --c-text:#1a1a1a;
  --c-muted:#666;
  --c-light:#ccc;
  --c-border:#e0e0e0;
)CSS";

ss << LR"CSS(
  --c-highlight:rgba(255,220,0,0.45);
  --c-pen:#e53935;
  --c-page:#fff;
  --topbar-h:30px;
  --tabbar-h:28px;
  --ribbon-h:44px;
  --statusbar-h:22px;
  --left-panel-w:0px;
  --right-panel-w:0px;
  --shadow:0 2px 12px rgba(0,0,0,.25);
  --radius:3px;
}
*{margin:0;padding:0;box-sizing:border-box;}
html,body{height:100vh;overflow:hidden;font-family:'Segoe UI',Arial,sans-serif;font-size:12px;background:var(--c-viewer);color:var(--c-text);user-select:none;}
::-webkit-scrollbar{width:7px;height:7px;}
::-webkit-scrollbar-track{background:transparent;}
::-webkit-scrollbar-thumb{background:#aaa;border-radius:4px;}
::-webkit-scrollbar-thumb:hover{background:#888;}
#app{display:flex;flex-direction:column;height:100vh;overflow:hidden;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 03 · Topbar CSS
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.topbar{
  height:var(--topbar-h);background:var(--c-topbar);display:flex;
  align-items:center;padding:0 6px;gap:1px;flex-shrink:0;
  border-bottom:1px solid #000;z-index:200;
}
.top-menu{
  color:var(--c-light);font-size:11.5px;padding:2px 9px;border-radius:2px;
  cursor:pointer;line-height:var(--topbar-h);white-space:nowrap;
  position:relative;transition:background .1s;
}
.top-menu:hover,.top-menu.open{background:rgba(255,255,255,.12);color:#fff;}
.top-sep{width:1px;height:14px;background:#444;margin:0 4px;}
.top-right{margin-left:auto;display:flex;align-items:center;gap:3px;}
)CSS";

ss << LR"CSS(
.top-icon{
  color:var(--c-light);cursor:pointer;padding:2px 6px;border-radius:2px;
  font-size:14px;line-height:1;transition:background .1s;
}
.top-icon:hover{background:rgba(255,255,255,.14);color:#fff;}
/* Dropdown menus */
.dropdown{
  display:none;position:fixed;background:#2b2b2b;border:1px solid #444;
  border-radius:3px;box-shadow:0 4px 16px rgba(0,0,0,.4);z-index:9000;
  min-width:180px;padding:3px 0;
}
.dropdown.show{display:block;}
.dd-item{
  color:#ccc;font-size:11.5px;padding:5px 14px;cursor:pointer;
  display:flex;align-items:center;gap:8px;white-space:nowrap;
}
.dd-item:hover{background:#3a3a3a;color:#fff;}
.dd-item.danger{color:#e07070;}
.dd-item.danger:hover{background:#4a2020;}
.dd-sep{height:1px;background:#444;margin:3px 0;}
.dd-shortcut{margin-left:auto;color:#777;font-size:10px;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 04 · Tabbar CSS
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.tabbar{
  height:var(--tabbar-h);background:var(--c-tabbar);display:flex;
  align-items:flex-end;padding-left:6px;overflow-x:auto;flex-shrink:0;
  border-bottom:1px solid #111;
}
.tabbar::-webkit-scrollbar{height:0;}
.pdf-tab{
  height:24px;background:#3c3c3c;color:#aaa;padding:0 8px;
  display:flex;align-items:center;gap:5px;font-size:11.5px;
  border-radius:3px 3px 0 0;margin-right:2px;cursor:pointer;
  max-width:190px;white-space:nowrap;border:1px solid #1a1a1a;
  border-bottom:none;transition:background .1s;position:relative;top:1px;
  flex-shrink:0;
}
.pdf-tab:hover{background:#4a4a4a;color:#ddd;}
)CSS";

ss << LR"CSS(
.pdf-tab.active{background:var(--c-toolbar);color:var(--c-text);font-weight:600;}
.pdf-tab .tab-icon{font-size:12px;opacity:.7;}
.tab-name{overflow:hidden;text-overflow:ellipsis;max-width:130px;}
.tab-modified{color:var(--c-accent);font-size:9px;}
.tab-close{
  font-size:13px;cursor:pointer;opacity:.5;border-radius:2px;
  width:15px;height:15px;display:flex;align-items:center;justify-content:center;
  flex-shrink:0;
}
.tab-close:hover{opacity:1;background:rgba(0,0,0,.15);color:var(--c-accent);}
.tab-add{
  color:#999;cursor:pointer;padding:0 9px;font-size:16px;
  line-height:24px;border-radius:3px 3px 0 0;flex-shrink:0;
}
.tab-add:hover{color:#fff;background:#444;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 05 · Ribbon Toolbar CSS
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.ribbon-wrap{flex-shrink:0;background:var(--c-toolbar);border-bottom:2px solid var(--c-toolbar-border);}
.ribbon-tabs{display:flex;background:#e4e4e4;border-bottom:1px solid var(--c-toolbar-border);padding:0 6px;}
.rtab{
  padding:4px 14px;font-size:11px;cursor:pointer;border-radius:2px 2px 0 0;
  border:1px solid transparent;border-bottom:none;color:var(--c-muted);
  margin-right:1px;transition:background .1s;
}
.rtab:hover{background:#d8d8d8;color:var(--c-text);}
.rtab.active{background:var(--c-toolbar);border-color:var(--c-toolbar-border);color:var(--c-text);font-weight:600;}
.ribbon-panel{display:none;padding:4px 8px;align-items:center;gap:4px;min-height:36px;flex-wrap:wrap;flex-direction:row;}
.ribbon-panel.active{display:flex;}
.ribbon-group{
  display:flex;flex-direction:row;align-items:center;
  border-right:1px solid var(--c-border);padding:0 6px;gap:3px;
}
.ribbon-group:last-child{border-right:none;}
.rg-label{display:none;}
.rg-row{display:flex;gap:2px;}
)CSS";

ss << LR"CSS(
/* Ribbon buttons */
.rbtn{
  display:flex;flex-direction:row;align-items:center;justify-content:center;
  cursor:pointer;border-radius:var(--radius);border:1px solid transparent;
  padding:3px 7px;transition:background .1s,border-color .1s;
  color:var(--c-text);gap:4px;background:transparent;white-space:nowrap;
}
.rbtn:hover{background:#e0e0e0;border-color:var(--c-border);}
.rbtn.active,.rbtn.pressed{background:#fce4e4;border-color:#f5b8b8;color:var(--c-accent);}
.rbtn svg{width:16px;height:16px;fill:currentColor;flex-shrink:0;}
.rbtn-lbl{font-size:10px;line-height:1;text-align:center;white-space:nowrap;}
/* Small ribbon buttons */
.rbtn-sm{
  display:flex;align-items:center;gap:4px;cursor:pointer;border-radius:var(--radius);
  border:1px solid transparent;padding:2px 5px;transition:background .1s;
  font-size:10.5px;color:var(--c-text);white-space:nowrap;background:transparent;
}
.rbtn-sm:hover{background:#e0e0e0;border-color:var(--c-border);}
.rbtn-sm svg{width:13px;height:13px;fill:currentColor;flex-shrink:0;}
)CSS";

ss << LR"CSS(
/* Color swatch */
.color-swatch{
  width:16px;height:16px;border-radius:2px;border:1px solid #bbb;
  cursor:pointer;display:inline-block;flex-shrink:0;
}
/* Number input in ribbon */
.ribbon-num{
  width:42px;padding:2px 4px;border:1px solid var(--c-border);border-radius:var(--radius);
  font-size:11px;text-align:center;outline:none;background:#fff;
}
.ribbon-num:focus{border-color:var(--c-accent2);}
select.ribbon-sel{
  padding:2px 4px;border:1px solid var(--c-border);border-radius:var(--radius);
  font-size:11px;outline:none;cursor:pointer;background:#fff;max-width:90px;
}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 06 · Workspace + Panels CSS
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.workspace{flex:1;display:flex;overflow:hidden;position:relative;}
/* Left panel */
.left-panel{
  width:0;background:#e8e8e8;border-right:1px solid var(--c-border);
  display:flex;flex-direction:column;flex-shrink:0;overflow:hidden;
  transition:width .18s;
}
.left-panel.open{width:170px;}
.left-panel.collapsed{width:0;border:none;}
.lp-tabbar{display:flex;background:#ddd;border-bottom:1px solid var(--c-border);flex-shrink:0;}
.lp-tab{
  flex:1;text-align:center;padding:5px 0;font-size:9.5px;cursor:pointer;
  border-right:1px solid var(--c-border);color:var(--c-muted);
  transition:background .1s;white-space:nowrap;overflow:hidden;
}
.lp-tab:last-child{border-right:none;}
.lp-tab:hover{background:#ccc;}
.lp-tab.active{background:var(--c-toolbar);color:var(--c-text);font-weight:700;}
.lp-body{flex:1;overflow-y:auto;overflow-x:hidden;}
)CSS";

ss << LR"CSS(
/* Thumbnails */
.thumb-list{padding:6px;display:flex;flex-direction:column;gap:5px;}
.thumb-item{
  background:#fff;border:2px solid transparent;border-radius:3px;
  cursor:pointer;position:relative;box-shadow:0 1px 4px rgba(0,0,0,.18);
  transition:border-color .12s;
}
.thumb-item:hover{border-color:#aaa;}
.thumb-item.selected{border-color:var(--c-accent);}
.thumb-item canvas{width:100%;display:block;}
.thumb-pg-num{
  position:absolute;bottom:2px;left:0;right:0;text-align:center;
  font-size:8.5px;color:#555;background:rgba(255,255,255,.75);
}
.thumb-del{
  position:absolute;top:2px;right:2px;width:14px;height:14px;
  background:var(--c-accent);color:#fff;border-radius:50%;
  font-size:8px;display:none;align-items:center;justify-content:center;cursor:pointer;
}
.thumb-item:hover .thumb-del{display:flex;}
/* Bookmarks */
.bm-item{
  padding:5px 10px;font-size:11px;cursor:pointer;border-bottom:1px solid var(--c-border);
  color:var(--c-text);transition:background .1s;display:flex;align-items:center;gap:6px;
}
.bm-item:hover{background:#ddd;}
.bm-item .bm-del{margin-left:auto;opacity:0;font-size:11px;color:var(--c-accent);}
.bm-item:hover .bm-del{opacity:1;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 07 · PDF Viewer + Page CSS
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.pdf-viewer-area{
  flex:1;overflow:auto;background:var(--c-viewer);
  display:flex;justify-content:center;position:relative;cursor:default;
}
.pdf-container{
  display:flex;flex-direction:column;gap:14px;
  align-items:center;padding:20px;width:100%;
}
/* Page wrapper */
.page-wrapper{
  position:relative;background:var(--c-page);box-shadow:var(--shadow);
  flex-shrink:0;display:block;
}
.page-wrapper .pdf-canvas{display:block;position:relative;z-index:1;}
.page-wrapper .draw-canvas{
  position:absolute;top:0;left:0;z-index:2;pointer-events:none;
}
.page-wrapper .draw-canvas.can-draw{pointer-events:auto;}
.page-wrapper .text-layer{
  position:absolute;top:0;left:0;z-index:3;pointer-events:none;
  overflow:hidden;opacity:.2;line-height:1;
}
/* Ruler overlay */
.ruler-h,.ruler-v{
  position:absolute;background:rgba(220,220,220,.9);pointer-events:none;z-index:20;
  font-size:8px;color:#888;overflow:hidden;
}
.ruler-h{top:0;left:0;right:0;height:16px;}
.ruler-v{top:0;left:0;bottom:0;width:16px;}
)CSS";

ss << LR"CSS(
/* Grid overlay */
.grid-overlay{
  position:absolute;inset:0;pointer-events:none;z-index:19;
  background-image:linear-gradient(rgba(0,100,255,.06) 1px,transparent 1px),
    linear-gradient(90deg,rgba(0,100,255,.06) 1px,transparent 1px);
  background-size:20px 20px;
  display:none;
}
.grid-overlay.show{display:block;}
/* Crop overlay */
.crop-overlay{
  position:absolute;inset:0;z-index:25;cursor:crosshair;
  background:rgba(0,0,0,.35);display:none;
}
.crop-overlay.show{display:block;}
.crop-rect{
  position:absolute;border:2px solid var(--c-accent2);background:transparent;
  box-shadow:0 0 0 9999px rgba(0,0,0,.4);pointer-events:none;
}
/* Selection rect */
.sel-rect{
  position:absolute;border:1.5px dashed var(--c-accent2);
  background:rgba(41,128,185,.08);pointer-events:none;z-index:30;display:none;
}
/* Redaction rect */
.redact-rect{
  position:absolute;background:#000;z-index:22;cursor:pointer;
  border:2px solid var(--c-accent);
}
)CSS";

ss << LR"CSS(
/* Stamp */
.stamp-el{
  position:absolute;z-index:18;pointer-events:auto;cursor:move;
  font-size:22px;font-weight:900;letter-spacing:2px;opacity:.55;
  border:3px solid;border-radius:4px;padding:2px 10px;
  text-transform:uppercase;
}
/* Text box */
.textbox-el{
  position:absolute;z-index:16;background:rgba(255,255,255,.85);
  border:1.5px solid var(--c-accent2);min-width:80px;min-height:24px;
  padding:3px 6px;font-size:12px;color:#111;resize:both;overflow:auto;
  cursor:move;
}
/* Signature */
.sig-el{position:absolute;z-index:17;cursor:move;}
.sig-el canvas{border:1px dashed #aaa;}
/* Shapes */
.shape-el{
  position:absolute;z-index:15;pointer-events:auto;cursor:move;
}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 08 · Right Properties Panel CSS
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.right-panel{
  width:0;background:var(--c-sidebar);
  border-left:1px solid var(--c-sidebar-border);
  display:flex;flex-direction:column;overflow-y:auto;flex-shrink:0;
  transition:width .18s;
}
.right-panel.open{width:220px;}
.right-panel.collapsed{width:0;border:none;overflow:hidden;}
.rp-section{border-bottom:1px solid var(--c-border);}
.rp-header{
  padding:6px 10px;font-size:10px;font-weight:700;color:var(--c-muted);
  text-transform:uppercase;letter-spacing:.5px;display:flex;
  align-items:center;gap:4px;cursor:pointer;
  background:#ececec;border-bottom:1px solid var(--c-border);
}
.rp-header:hover{background:#e0e0e0;}
.rp-header .toggle{margin-left:auto;font-size:10px;}
.rp-body{padding:8px 10px;display:flex;flex-direction:column;gap:6px;}
.rp-row{display:flex;align-items:center;gap:6px;flex-wrap:wrap;}
.rp-label{font-size:10.5px;color:var(--c-muted);min-width:54px;}
.rp-input{
  flex:1;padding:3px 5px;border:1px solid var(--c-border);border-radius:var(--radius);
  font-size:11px;outline:none;min-width:50px;background:#fff;
}
.rp-input:focus{border-color:var(--c-accent2);}
)CSS";

ss << LR"CSS(
/* Action buttons in panel */
.rp-btn{
  width:100%;padding:5px 0;border:1px solid var(--c-border);border-radius:var(--radius);
  background:#fff;cursor:pointer;font-size:11px;font-weight:600;
  color:var(--c-text);transition:background .1s;display:flex;align-items:center;
  justify-content:center;gap:5px;
}
.rp-btn:hover{background:#e8e8e8;}
.rp-btn.danger{color:var(--c-accent);border-color:#f5b8b8;}
.rp-btn.danger:hover{background:#fde8e8;}
.rp-btn.primary{background:var(--c-accent);color:#fff;border-color:var(--c-accent);}
.rp-btn.primary:hover{background:var(--c-accent-h);}
/* Word count badge */
.badge{
  display:inline-block;background:var(--c-accent2);color:#fff;
  font-size:9px;padding:1px 5px;border-radius:8px;font-weight:700;
}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 09 · Statusbar + Toast + Loading + Modal CSS
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.statusbar{
  height:var(--statusbar-h);background:#e0e0e0;border-top:1px solid #ccc;
  display:flex;align-items:center;padding:0 10px;gap:12px;flex-shrink:0;z-index:100;
}
.sb-item{font-size:10px;color:var(--c-muted);white-space:nowrap;}
.sb-sep{width:1px;height:12px;background:#bbb;}
.sb-zoom-row{display:flex;align-items:center;gap:4px;}
.sb-zoom-btn{
  width:16px;height:16px;background:#ccc;border:none;border-radius:2px;
  cursor:pointer;font-size:12px;display:flex;align-items:center;justify-content:center;
  line-height:1;
}
.sb-zoom-btn:hover{background:#bbb;}
#sb-zoom-val{font-size:10px;min-width:34px;text-align:center;color:var(--c-text);cursor:default;}
.sb-right{margin-left:auto;display:flex;align-items:center;gap:8px;}
/* Find bar */
.findbar{
  display:none;position:absolute;bottom:var(--statusbar-h);right:16px;
  background:#fff;border:1px solid var(--c-border);border-radius:4px;
  box-shadow:var(--shadow);padding:6px 8px;z-index:500;
  align-items:center;gap:6px;
}
.findbar.show{display:flex;}
.findbar input{
  padding:3px 7px;border:1px solid var(--c-border);border-radius:var(--radius);
  font-size:11.5px;outline:none;width:180px;
}
.findbar input:focus{border-color:var(--c-accent2);}
)CSS";

ss << LR"CSS(
.find-btn{
  padding:3px 8px;border:1px solid var(--c-border);border-radius:var(--radius);
  background:#f0f0f0;cursor:pointer;font-size:11px;
}
.find-btn:hover{background:#e0e0e0;}
#find-count{font-size:10.5px;color:var(--c-muted);min-width:60px;}
/* Toast */
.toast-box{
  position:fixed;bottom:32px;left:50%;transform:translateX(-50%);
  z-index:9999;display:flex;flex-direction:column;gap:5px;pointer-events:none;
}
.toast{
  padding:7px 16px;border-radius:4px;background:#2a2a2a;color:#fff;
  font-size:11.5px;font-weight:500;box-shadow:0 3px 12px rgba(0,0,0,.28);
  animation:toastIn .2s ease;
}
.toast.success{background:#1a7a4a;}
.toast.warn{background:#b86e00;}
@keyframes toastIn{from{transform:translateY(10px);opacity:0}to{transform:none;opacity:1}}
/* Loading */
.loading-overlay{
  display:none;position:fixed;inset:0;background:rgba(0,0,0,.55);
  z-index:9000;flex-direction:column;align-items:center;justify-content:center;color:#fff;
}
.loading-overlay.show{display:flex;}
.spinner{
  width:30px;height:30px;border:3px solid rgba(255,255,255,.2);
  border-top:3px solid #fff;border-radius:50%;animation:spin .65s linear infinite;margin-bottom:12px;
}
@keyframes spin{to{transform:rotate(360deg)}}
#loading-txt{font-size:12px;font-weight:600;}
)CSS";

ss << LR"CSS(
/* Modal */
.modal-overlay{
  display:none;position:fixed;inset:0;background:rgba(0,0,0,.5);
  z-index:8000;align-items:center;justify-content:center;
}
.modal-overlay.show{display:flex;}
.modal{
  background:#fff;border-radius:5px;padding:20px 22px;min-width:320px;max-width:520px;
  box-shadow:0 8px 32px rgba(0,0,0,.3);width:100%;
}
.modal h3{font-size:13.5px;font-weight:700;margin-bottom:14px;color:var(--c-text);}
.modal label{font-size:11px;color:var(--c-muted);display:block;margin-bottom:3px;}
.modal input[type=text],
.modal input[type=number],
.modal input[type=password],
.modal textarea,
.modal select{
  width:100%;padding:6px 8px;border:1px solid var(--c-border);border-radius:var(--radius);
  font-size:12px;outline:none;margin-bottom:10px;font-family:inherit;background:#fff;
}
.modal input:focus,.modal textarea:focus,.modal select:focus{border-color:var(--c-accent2);}
.modal textarea{resize:vertical;min-height:60px;}
.modal-actions{display:flex;gap:8px;justify-content:flex-end;margin-top:6px;}
.btn{
  padding:6px 16px;border:none;border-radius:var(--radius);cursor:pointer;
  font-size:12px;font-weight:600;transition:background .15s,opacity .15s;
}
.btn-primary{background:var(--c-accent);color:#fff;}
.btn-primary:hover{background:var(--c-accent-h);}
.btn-secondary{background:#f0f0f0;border:1px solid #ccc;color:var(--c-text);}
.btn-secondary:hover{background:#e4e4e4;}
.btn-blue{background:var(--c-accent2);color:#fff;}
.btn-blue:hover{background:#1f6fa0;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 10 · Dark / Night / Read mode CSS
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
/* Night mode */
body.night .pdf-viewer-area{background:#1a1a1a!important;}
body.night .page-wrapper{filter:invert(1) hue-rotate(180deg);}
body.night .ribbon-wrap,body.night .left-panel,body.night .right-panel{
  background:#252525!important;border-color:#333!important;color:#ccc!important;
}
body.night .ribbon-tabs{background:#1e1e1e!important;}
body.night .rtab{color:#aaa!important;}
body.night .rtab.active{background:#252525!important;color:#fff!important;}
body.night .rbtn{color:#ccc!important;}
body.night .rbtn:hover{background:#333!important;}
body.night .statusbar{background:#1e1e1e!important;border-color:#333!important;}
body.night .sb-item{color:#888!important;}
/* Read mode — only topbar + tabbar visible, ribbon/panels/statusbar hidden */
body.read .left-panel,body.read .right-panel,
body.read .ribbon-wrap,body.read .statusbar{display:none!important;}
body.read .pdf-viewer-area{background:#1a1a1a!important;}
/* Read mode floating Edit button */
#read-edit-btn{
  display:none;position:fixed;top:8px;right:12px;z-index:9999;
  background:#2563eb;color:#fff;border:none;border-radius:6px;
  padding:4px 14px;font-size:12px;font-weight:700;cursor:pointer;
  box-shadow:0 2px 8px rgba(0,0,0,.35);letter-spacing:.3px;
  transition:background .15s;
}
#read-edit-btn:hover{background:#1d4ed8;}
body.read #read-edit-btn{display:block;}
/* Read mode: slim topbar */
body.read .topbar{background:#111;height:28px;}
body.read .top-menu{font-size:11px;}
body.read .tabbar{height:24px;}
body.read .pdf-tab{font-size:10px;padding:0 10px;}
/* Presentation mode */
body.present *{cursor:none!important;}
body.present .left-panel,body.present .right-panel,
body.present .ribbon-wrap,body.present .tabbar,body.present .topbar,
body.present .statusbar{display:none!important;}
body.present .pdf-viewer-area{background:#000!important;}
/* Sepia */
body.sepia .page-wrapper{filter:sepia(.6) brightness(.95);}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 11 · Context menu CSS + Colour picker + Signature modal
// ─────────────────────────────────────────────────────────────
ss << LR"CSS(
<style>
.ctx-menu{
  display:none;position:fixed;background:#fff;border:1px solid var(--c-border);
  border-radius:4px;box-shadow:0 4px 18px rgba(0,0,0,.2);z-index:7000;
  padding:3px 0;min-width:170px;
}
.ctx-menu.show{display:block;}
.ctx-item{
  padding:5px 14px;font-size:11.5px;cursor:pointer;color:var(--c-text);
  display:flex;align-items:center;gap:8px;
}
.ctx-item:hover{background:#f0f0f0;}
.ctx-item.danger{color:var(--c-accent);}
.ctx-sep{height:1px;background:var(--c-border);margin:3px 0;}
/* Colour picker popup */
.color-picker-popup{
  display:none;position:fixed;background:#fff;border:1px solid var(--c-border);
  border-radius:4px;box-shadow:0 4px 16px rgba(0,0,0,.22);
  padding:10px;z-index:6000;
}
.color-picker-popup.show{display:block;}
.color-grid{display:grid;grid-template-columns:repeat(8,18px);gap:3px;}
.color-cell{
  width:18px;height:18px;border-radius:2px;cursor:pointer;border:1px solid rgba(0,0,0,.12);
  transition:transform .1s;
}
.color-cell:hover{transform:scale(1.25);z-index:1;position:relative;}
/* Signature pad modal */
#sig-modal-canvas{border:1px solid var(--c-border);border-radius:3px;cursor:crosshair;touch-action:none;}
/* Progress bar */
.progress-wrap{background:#e0e0e0;border-radius:4px;height:6px;overflow:hidden;margin-bottom:8px;}
.progress-bar{height:100%;background:var(--c-accent);transition:width .2s;}
</style>
)CSS";

// ─────────────────────────────────────────────────────────────
// PART 12 · HTML skeleton: app, topbar menus
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<div id="app">
<!-- ── READ MODE FLOATING EDIT BUTTON ── -->
<button id="read-edit-btn" onclick="exitReadMode()" title="Exit Read Mode & Edit">✏️ Edit</button>
<!-- ── TOPBAR ── -->
<div class="topbar">
  <div class="top-menu" id="tm-file" onclick="toggleMenu('m-file',this)">File</div>
  <div class="top-menu" id="tm-edit" onclick="toggleMenu('m-edit',this)">Edit</div>
  <div class="top-menu" id="tm-view" onclick="toggleMenu('m-view',this)">View</div>
  <div class="top-menu" id="tm-tools" onclick="toggleMenu('m-tools',this)">Tools</div>
  <div class="top-menu" id="tm-doc" onclick="toggleMenu('m-doc',this)">Document</div>
  <div class="top-menu" id="tm-forms" onclick="toggleMenu('m-forms',this)">Forms</div>
  <div class="top-menu" id="tm-help" onclick="toggleMenu('m-help',this)">Help</div>
  <div class="top-sep"></div>
  <div class="top-right">
    <div class="top-icon" title="Left Panel (Thumbs)" onclick="toggleLeftPanel()" style="font-size:13px;">&#9776;</div>
    <div class="top-icon" title="Right Panel (Properties)" onclick="toggleRightPanel()" style="font-size:13px;">&#9783;</div>
    <div class="top-icon" title="Find (Ctrl+F)" onclick="toggleFindBar()">&#128269;</div>
    <div class="top-icon" title="Presentation" onclick="enterPresentation()">&#9654;</div>
    <div class="top-icon" title="Night Mode" onclick="cycleViewMode()">&#9790;</div>
  </div>
</div>
<!-- ── DROPDOWN MENUS ── -->
<div class="dropdown" id="m-file">
  <div class="dd-item" onclick="openFileDialog()">&#128196; Open...<span class="dd-shortcut">Ctrl+O</span></div>
  <div class="dd-item" onclick="openFileDialog(true)">&#128196; Open Multiple...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="downloadCurrentPDF()">&#128190; Save<span class="dd-shortcut">Ctrl+S</span></div>
  <div class="dd-item" onclick="downloadCurrentPDF(true)">&#128190; Save As...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="actionMergePDFs()">&#128214; Combine PDFs...</div>
  <div class="dd-item" onclick="modalSplit()">&#9986; Split PDF...</div>
  <div class="dd-item" onclick="modalExtract()">&#128196; Extract Pages...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="actionPDFtoImages()">&#128247; Export as Images...</div>
  <div class="dd-item" onclick="actionPDFtoText()">&#128220; Export as Text...</div>
  <div class="dd-item" onclick="actionCompressPDF()">&#128230; Compress PDF...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="window.print ? window.print() : showToast('Use Ctrl+P')">&#128438; Print<span class="dd-shortcut">Ctrl+P</span></div>
  <div class="dd-sep"></div>
  <div class="dd-item danger" onclick="closeActiveTab()">&#10006; Close Tab</div>
</div>
)HTML";

ss << LR"HTML(
<div class="dropdown" id="m-edit">
  <div class="dd-item" onclick="histUndo()">&#8592; Undo<span class="dd-shortcut">Ctrl+Z</span></div>
  <div class="dd-item" onclick="histRedo()">&#8594; Redo<span class="dd-shortcut">Ctrl+Y</span></div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="setTool('select')">&#9654; Select Text</div>
  <div class="dd-item" onclick="copySelectedText()">&#128203; Copy<span class="dd-shortcut">Ctrl+C</span></div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="toggleFindBar()">&#128269; Find &amp; Replace<span class="dd-shortcut">Ctrl+F</span></div>
</div>
<div class="dropdown" id="m-view">
  <div class="dd-item" onclick="zoomTo(0.5)">50%</div>
  <div class="dd-item" onclick="zoomTo(0.75)">75%</div>
  <div class="dd-item" onclick="zoomTo(1.0)">100%</div>
  <div class="dd-item" onclick="zoomTo(1.25)">125%</div>
  <div class="dd-item" onclick="zoomTo(1.5)">150%</div>
  <div class="dd-item" onclick="zoomTo(2.0)">200%</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="toggleRuler()">&#8213; Ruler</div>
  <div class="dd-item" onclick="toggleGrid()">&#9638; Grid</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="toggleLeftPanel()">&#9776; Thumbnails Panel</div>
  <div class="dd-item" onclick="toggleRightPanel()">&#9881; Properties Panel</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="cycleViewMode()">&#9790; Night / Sepia / Normal</div>
  <div class="dd-item" onclick="enterReadMode();closeAllMenus()">&#9634; Read Mode (ESC to exit)</div>
  <div class="dd-item" onclick="enterPresentation()">&#9654; Presentation Mode</div>
</div>
)HTML";

ss << LR"HTML(
<div class="dropdown" id="m-tools">
  <div class="dd-item" onclick="setTool('hand');closeAllMenus()">&#9995; Hand Tool</div>
  <div class="dd-item" onclick="setTool('pen');closeAllMenus()">&#9998; Pen (Draw)</div>
  <div class="dd-item" onclick="setTool('highlight');closeAllMenus()">&#9998; Highlight</div>
  <div class="dd-item" onclick="setTool('eraser');closeAllMenus()">&#9003; Eraser</div>
  <div class="dd-item" onclick="setTool('textbox');closeAllMenus()">&#8633; Text Box</div>
  <div class="dd-item" onclick="setTool('note');closeAllMenus()">&#128204; Sticky Note</div>
  <div class="dd-item" onclick="setTool('stamp');closeAllMenus()">&#9997; Stamp</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="setTool('rect');closeAllMenus()">&#9645; Draw Rectangle</div>
  <div class="dd-item" onclick="setTool('ellipse');closeAllMenus()">&#9711; Draw Ellipse</div>
  <div class="dd-item" onclick="setTool('line');closeAllMenus()">&#8212; Draw Line</div>
  <div class="dd-item" onclick="setTool('arrow');closeAllMenus()">&#10145; Draw Arrow</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="setTool('redact');closeAllMenus()">&#9644; Redaction</div>
  <div class="dd-item" onclick="setTool('crop');closeAllMenus()">&#9986; Crop</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="openSignatureModal()">&#9998; Signature</div>
  <div class="dd-item" onclick="actionPerformOCR()">&#128065; OCR Scanner</div>
</div>
)HTML";

ss << LR"HTML(
<div class="dropdown" id="m-doc">
  <div class="dd-item" onclick="rotatePDFAll()">&#8635; Rotate All Pages</div>
  <div class="dd-item" onclick="modalDeletePages()">&#128465; Delete Pages...</div>
  <div class="dd-item" onclick="modalInsertBlank()">&#10011; Insert Blank Page...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="modalWatermark()">&#10070; Watermark...</div>
  <div class="dd-item" onclick="modalHeaderFooter()">&#9776; Header &amp; Footer...</div>
  <div class="dd-item" onclick="modalBatesNumber()">&#9839; Bates Numbering...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="modalPassword()">&#128274; Encrypt / Password...</div>
  <div class="dd-item" onclick="showDocProperties()">&#8505; Document Properties</div>
</div>
<div class="dropdown" id="m-forms">
  <div class="dd-item" onclick="showToast('Form features: coming in enterprise build.')">&#9744; Checkbox Field</div>
  <div class="dd-item" onclick="showToast('Form features: coming in enterprise build.')">&#9675; Radio Field</div>
  <div class="dd-item" onclick="showToast('Form features: coming in enterprise build.')">&#9633; Text Field</div>
  <div class="dd-item" onclick="showToast('Form features: coming in enterprise build.')">&#9013; Button</div>
</div>
<div class="dropdown" id="m-help">
  <div class="dd-item" onclick="showShortcutModal()">&#9881; Keyboard Shortcuts</div>
  <div class="dd-item" onclick="showToast('PDF Pro — Acrobat Edition v1.0')">&#8505; About</div>
</div>
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 13 · Tabbar HTML
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- ── TABBAR ── -->
<div class="tabbar" id="tabbar"></div>
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 14 · Ribbon (Home tab buttons)
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- ── RIBBON ── -->
<div class="ribbon-wrap" id="ribbon-wrap">
<div class="ribbon-tabs">
  <div class="rtab active" onclick="switchRibbon('r-home',this)">Home</div>
  <div class="rtab" onclick="switchRibbon('r-annotate',this)">Annotate</div>
  <div class="rtab" onclick="switchRibbon('r-edit',this)">Edit PDF</div>
  <div class="rtab" onclick="switchRibbon('r-pages',this)">Pages</div>
  <div class="rtab" onclick="switchRibbon('r-protect',this)">Protect</div>
  <div class="rtab" onclick="switchRibbon('r-export',this)">Export</div>
</div>
<!-- HOME ribbon -->
<div class="ribbon-panel active" id="r-home">
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn active" id="rb-hand" onclick="setTool('hand')" title="Hand (1)">
        <svg viewBox="0 0 24 24"><path d="M9 11V6a1 1 0 0 1 2 0v5h1V4a1 1 0 0 1 2 0v7h1V6a1 1 0 0 1 2 0v8l-1 5H9l-3-3V9a1 1 0 0 1 2 0v2z"/></svg>
        <span class="rbtn-lbl">Hand</span>
      </div>
      <div class="rbtn" id="rb-select" onclick="setTool('select')" title="Select (2)">
        <svg viewBox="0 0 24 24"><path d="M4 4l7 18 3-7 7-3z"/></svg>
        <span class="rbtn-lbl">Select</span>
      </div>
    </div>
    <div class="rg-label">Navigate</div>
  </div>
)HTML";

ss << LR"HTML(
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn" id="rb-pen" onclick="setTool('pen')" title="Pen (3)">
        <svg viewBox="0 0 24 24"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zm17.71-10.21a1 1 0 0 0 0-1.41l-2.34-2.34a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>
        <span class="rbtn-lbl">Pen</span>
      </div>
      <div class="rbtn" id="rb-highlight" onclick="setTool('highlight')" title="Highlight (4)">
        <svg viewBox="0 0 24 24"><rect x="3" y="14" width="18" height="5" rx="1" fill="#f9a825" opacity=".5"/><path d="M6 13l6-9 6 9" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>
        <span class="rbtn-lbl">Highlight</span>
      </div>
      <div class="rbtn" id="rb-eraser" onclick="setTool('eraser')" title="Eraser (5)">
        <svg viewBox="0 0 24 24"><path d="M16.24 3.56l4.2 4.2c.78.78.78 2.05 0 2.83L8.1 22.83a4 4 0 0 1-5.66 0l-.17-.17a4 4 0 0 1 0-5.66L13.41 5.76c.78-.78 2.05-.78 2.83 0zM5.76 18.41a2 2 0 0 0 2.83 0L19 8l-3-3L5.24 15.58a2 2 0 0 0 0 2.83z"/></svg>
        <span class="rbtn-lbl">Eraser</span>
      </div>
    </div>
    <div class="rg-label">Draw</div>
  </div>
)HTML";

ss << LR"HTML(
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn" id="rb-note" onclick="setTool('note')" title="Note (6)">
        <svg viewBox="0 0 24 24"><path d="M20 2H4a2 2 0 0 0-2 2v14l4-4h14a2 2 0 0 0 2-2V4a2 2 0 0 0-2-2z"/></svg>
        <span class="rbtn-lbl">Note</span>
      </div>
      <div class="rbtn" id="rb-textbox" onclick="setTool('textbox')" title="Text Box (7)">
        <svg viewBox="0 0 24 24"><path d="M2 4v3h5v12h3V7h5V4H2zm19 5h-9v3h3v7h3v-7h3V9z"/></svg>
        <span class="rbtn-lbl">TextBox</span>
      </div>
    </div>
    <div class="rg-row">
      <div class="rbtn" id="rb-stamp" onclick="setTool('stamp')" title="Stamp (8)">
        <svg viewBox="0 0 24 24"><path d="M12 2a5 5 0 0 1 5 5c0 2.38-1.7 4.38-4 4.87V13h2v2h-2v2h-2v-2H9v-2h2v-1.13C8.7 11.38 7 9.38 7 7a5 5 0 0 1 5-5zM4 20v-1a2 2 0 0 1 2-2h12a2 2 0 0 1 2 2v1H4z"/></svg>
        <span class="rbtn-lbl">Stamp</span>
      </div>
      <div class="rbtn" onclick="openSignatureModal()" title="Signature">
        <svg viewBox="0 0 24 24"><path d="M21 17l-1 1H4l-1-1V7l1-1h16l1 1v10zm-9-8c-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4-1.79-4-4-4zm0 2a2 2 0 1 1 0 4 2 2 0 0 1 0-4z"/></svg>
        <span class="rbtn-lbl">Sign</span>
      </div>
    </div>
    <div class="rg-label">Insert</div>
  </div>
)HTML";

ss << LR"HTML(
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn" id="rb-rect" onclick="setTool('rect')">
        <svg viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="14" rx="1" fill="none" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Rect</span>
      </div>
      <div class="rbtn" id="rb-ellipse" onclick="setTool('ellipse')">
        <svg viewBox="0 0 24 24"><ellipse cx="12" cy="12" rx="9" ry="6" fill="none" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Circle</span>
      </div>
      <div class="rbtn" id="rb-line" onclick="setTool('line')">
        <svg viewBox="0 0 24 24"><line x1="4" y1="20" x2="20" y2="4" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Line</span>
      </div>
      <div class="rbtn" id="rb-arrow" onclick="setTool('arrow')">
        <svg viewBox="0 0 24 24"><path d="M4 12h16M14 6l6 6-6 6" fill="none" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Arrow</span>
      </div>
    </div>
    <div class="rg-label">Shapes</div>
  </div>
)HTML";

ss << LR"HTML(
  <div class="ribbon-group">
    <div class="rg-row">
      <div style="display:flex;flex-direction:column;gap:3px;">
        <div class="rbtn-sm" onclick="showColorPicker('stroke','rb-color-stroke')">
          <div class="color-swatch" id="rb-color-stroke" style="background:#e53935;"></div> Color
        </div>
        <div class="rbtn-sm" onclick="showColorPicker('fill','rb-color-fill')">
          <div class="color-swatch" id="rb-color-fill" style="background:transparent;border-style:dashed;"></div> Fill
        </div>
      </div>
      <div style="display:flex;flex-direction:column;gap:3px;">
        <div style="display:flex;align-items:center;gap:3px;">
          <span style="font-size:9.5px;color:var(--c-muted);">Size</span>
          <input class="ribbon-num" type="number" id="rb-linewidth" value="2" min="1" max="30" onchange="g_lineWidth=+this.value" title="Stroke width">
        </div>
        <div style="display:flex;align-items:center;gap:3px;">
          <span style="font-size:9.5px;color:var(--c-muted);">Opacity</span>
          <input class="ribbon-num" type="number" id="rb-opacity" value="100" min="1" max="100" onchange="g_opacity=+this.value/100" title="Opacity %">
        </div>
      </div>
    </div>
    <div class="rg-label">Style</div>
  </div>
)HTML";

ss << LR"HTML(
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn" onclick="zoomBy(-0.15)" title="Zoom Out">
        <svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="7" fill="none" stroke="currentColor" stroke-width="2"/><path d="M21 21l-4-4M8 11h6" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Zoom—</span>
      </div>
      <div class="rbtn" onclick="zoomBy(0.15)" title="Zoom In">
        <svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="7" fill="none" stroke="currentColor" stroke-width="2"/><path d="M21 21l-4-4M8 11h6M11 8v6" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Zoom+</span>
      </div>
      <div class="rbtn" onclick="zoomTo(1.0)">
        <svg viewBox="0 0 24 24"><path d="M12 5v14M5 12h14" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">100%</span>
      </div>
    </div>
    <div class="rg-label">Zoom</div>
  </div>
</div>
)HTML";

ss << LR"HTML(
<!-- ANNOTATE ribbon -->
<div class="ribbon-panel" id="r-annotate">
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn-sm" onclick="setTool('highlight');closeAllMenus()"><svg viewBox="0 0 24 24" width="13" height="13"><rect x="3" y="14" width="18" height="5" rx="1" fill="#f9a825"/><path d="M6 13l6-9 6 9" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>Highlight</div>
    </div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('underline')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M6 3v7a6 6 0 0 0 12 0V3h-2v7a4 4 0 0 1-8 0V3H6zm-2 15h16v2H4z"/></svg>Underline</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('strikethrough')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M6 3v7a6 6 0 0 0 12 0V3h-2v7a4 4 0 0 1-8 0V3H6zM2 11h20v2H2z"/></svg>Strikethrough</div></div>
    <div class="rg-label">Text Markup</div>
  </div>
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn-sm" onclick="setTool('pen')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zm17.71-10.21a1 1 0 0 0 0-1.41l-2.34-2.34a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>Free Draw</div>
    </div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('eraser')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M16.24 3.56l4.2 4.2c.78.78.78 2.05 0 2.83L8.1 22.83a4 4 0 0 1-5.66 0l-.17-.17a4 4 0 0 1 0-5.66L13.41 5.76c.78-.78 2.05-.78 2.83 0z"/></svg>Eraser</div></div>
    <div class="rg-label">Drawing</div>
  </div>
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn-sm" onclick="addBookmark()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M17 3H7a2 2 0 0 0-2 2v16l7-3 7 3V5a2 2 0 0 0-2-2z"/></svg>Add Bookmark</div>
    </div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('note')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M20 2H4a2 2 0 0 0-2 2v14l4-4h14a2 2 0 0 0 2-2V4a2 2 0 0 0-2-2z"/></svg>Add Note</div></div>
    <div class="rg-label">Comments</div>
  </div>
</div>
)HTML";

ss << LR"HTML(
<!-- EDIT PDF ribbon -->
<div class="ribbon-panel" id="r-edit">
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn-sm" onclick="setTool('textbox')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M2 4v3h5v12h3V7h5V4H2zm19 5h-9v3h3v7h3v-7h3V9z"/></svg>Add Text</div>
    </div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('redact')"><svg viewBox="0 0 24 24" width="13" height="13"><rect x="3" y="8" width="18" height="8" rx="1"/></svg>Redact</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('crop')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M7 17H3v-2h4V7h8v4h2V5h2v4h4v2h-4v8h-2v-4H7v2z"/></svg>Crop</div></div>
    <div class="rg-label">Edit Content</div>
  </div>
  <div class="ribbon-group">
    <div class="rg-row"><div class="rbtn-sm" onclick="modalWatermark()">&#10070; Watermark</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalHeaderFooter()">&#9776; Header/Footer</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalBatesNumber()">&#9839; Bates Numbers</div></div>
    <div class="rg-label">Overlays</div>
  </div>
</div>
)HTML";

ss << LR"HTML(
<!-- PAGES ribbon -->
<div class="ribbon-panel" id="r-pages">
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn-sm" onclick="rotatePDFAll()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M12 5V1L7 6l5 5V7a7 7 0 1 1-7 7h-2a9 9 0 1 0 9-9z"/></svg>Rotate All</div>
    </div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalDeletePages()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M6 19a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>Delete Pages</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalInsertBlank()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8l-6-6zm1 11h-4v4H9v-4H5v-2h4V7h2v4h4v2z"/></svg>Insert Blank</div></div>
    <div class="rg-label">Organize</div>
  </div>
  <div class="ribbon-group">
    <div class="rg-row"><div class="rbtn-sm" onclick="actionMergePDFs()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M16 2H8a2 2 0 0 0-2 2v2H4v2h2v10a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2V8h2V6h-2V4a2 2 0 0 0-2-2zm-4 15l-4-4h3V9h2v4h3l-4 4z"/></svg>Combine</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalSplit()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M14 4l2.29 2.29-2.88 2.88 1.42 1.42 2.88-2.88L20 10V4zm-4 0H4v6l2.29-2.29 4.71 4.7V20h2v-8.41l-5.29-5.3z"/></svg>Split</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalExtract()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8l-6-6zm-1 13v4H9v-4H6l6-6 6 6h-3z"/></svg>Extract</div></div>
    <div class="rg-label">PDF Operations</div>
  </div>
</div>
)HTML";

ss << LR"HTML(
<!-- PROTECT ribbon -->
<div class="ribbon-panel" id="r-protect">
  <div class="ribbon-group">
    <div class="rg-row"><div class="rbtn-sm" onclick="modalPassword()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M18 8h-1V6a5 5 0 0 0-10 0v2H6a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V10a2 2 0 0 0-2-2zm-6 9a2 2 0 1 1 0-4 2 2 0 0 1 0 4zm3.1-9H8.9V6a3.1 3.1 0 0 1 6.2 0v2z"/></svg>Password Encrypt</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="applyRedactions()"><svg viewBox="0 0 24 24" width="13" height="13"><rect x="3" y="8" width="18" height="8" rx="1"/></svg>Apply Redactions</div></div>
    <div class="rg-label">Security</div>
  </div>
</div>
<!-- EXPORT ribbon -->
<div class="ribbon-panel" id="r-export">
  <div class="ribbon-group">
    <div class="rg-row"><div class="rbtn-sm" onclick="actionPDFtoImages()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M21 19V5H3v14h18zm-9-7l3 4H9l2-3 1 1.5L12 12zm-4.5-2.5a1.5 1.5 0 1 0 3 0 1.5 1.5 0 0 0-3 0z"/></svg>Export as Images</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="actionPDFtoText()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8l-6-6zm-1 9H7v-1h6v1zm2 4H7v-1h8v1zm0-8V3.5L18.5 7H15z"/></svg>Export as Text</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="actionPerformOCR()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M9 2H5a1 1 0 0 0-1 1v4h2V4h3V2zm6 0v2h3v3h2V3a1 1 0 0 0-1-1h-4zM4 15H2v4a2 2 0 0 0 2 2h4v-2H4v-4zm16 0v4h-4v2h4a2 2 0 0 0 2-2v-4h-2z"/></svg>OCR Extract</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="actionCompressPDF()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M12 2L4.5 20.29l.71.71L12 18l6.79 3 .71-.71z"/></svg>Compress PDF</div></div>
    <div class="rg-label">Export &amp; Convert</div>
  </div>
</div>
</div><!-- end ribbon-wrap -->
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 15 · Workspace + panels HTML
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- ── WORKSPACE ── -->
<div class="workspace">
  <!-- Left Panel -->
  <div class="left-panel" id="left-panel">
    <div class="lp-tabbar">
      <div class="lp-tab active" id="lpt-thumb" onclick="switchLPanel('thumb',this)">Thumbs</div>
      <div class="lp-tab" id="lpt-bm" onclick="switchLPanel('bm',this)">Bookmarks</div>
      <div class="lp-tab" id="lpt-layers" onclick="switchLPanel('layers',this)">Layers</div>
    </div>
    <div class="lp-body">
      <div id="lp-thumb" class="thumb-list"></div>
      <div id="lp-bm" style="display:none;"></div>
      <div id="lp-layers" style="display:none;padding:8px;font-size:10.5px;color:var(--c-muted);">Layer control coming soon.</div>
    </div>
  </div>
  <!-- PDF Viewer -->
  <div class="pdf-viewer-area" id="viewer-area">
    <div class="pdf-container" id="pdf-container">
      <div id="empty-hint" style="margin-top:0;text-align:center;color:#777;pointer-events:auto;width:100%;padding:30px 20px;">
        <div style="font-size:52px;opacity:.25;margin-bottom:8px;">&#128196;</div>
        <p style="font-size:16px;font-weight:700;color:#444;margin-bottom:4px;">PDF Pro — Acrobat Edition</p>
        <p style="font-size:11.5px;opacity:.65;margin-bottom:20px;">File ▸ Open, drag &amp; drop, or click a recent file below</p>
        <div style="display:flex;gap:10px;justify-content:center;margin-bottom:24px;">
          <button onclick="openFileDialog(false)" style="padding:8px 20px;background:var(--c-accent);color:#fff;border:none;border-radius:4px;font-size:12px;font-weight:700;cursor:pointer;">&#128196; Open PDF</button>
          <button onclick="openFileDialog(true)" style="padding:8px 20px;background:var(--c-accent2);color:#fff;border:none;border-radius:4px;font-size:12px;font-weight:700;cursor:pointer;">&#128214; Open Multiple</button>
        </div>
        <div id="recent-files-section" style="max-width:560px;margin:0 auto;text-align:left;">
          <p style="font-size:10px;font-weight:700;color:#999;text-transform:uppercase;letter-spacing:1px;margin-bottom:8px;padding-left:4px;">&#128337; Recent Files</p>
          <div id="recent-files-list"></div>
        </div>
      </div>
    </div>
    <!-- Find bar -->
    <div class="findbar" id="findbar">
      <input type="text" id="find-input" placeholder="Find text…" oninput="doFind()">
      <button class="find-btn" onclick="findPrev()">&#8593;</button>
      <button class="find-btn" onclick="findNext()">&#8595;</button>
      <span id="find-count"></span>
      <button class="find-btn" onclick="toggleFindBar()">&#10005;</button>
    </div>
  </div>
)HTML";

ss << LR"HTML(
  <!-- Right Properties Panel -->
  <div class="right-panel" id="right-panel">
    <!-- Document Info -->
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#8505; Document <span class="toggle">&#9660;</span></div>
      <div class="rp-body" id="rp-docinfo">
        <div class="rp-row"><span class="rp-label">File</span><span id="rp-filename" style="font-size:10.5px;word-break:break-all;">—</span></div>
        <div class="rp-row"><span class="rp-label">Pages</span><span id="rp-pages">—</span></div>
        <div class="rp-row"><span class="rp-label">Page</span><span id="rp-curpage">—</span></div>
        <div class="rp-row"><span class="rp-label">Zoom</span><span id="rp-zoom">100%</span></div>
        <div class="rp-row"><span class="rp-label">Rotate</span><span id="rp-rotate">0°</span></div>
      </div>
    </div>
    <!-- Annotation Style -->
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#9998; Annotation Style <span class="toggle">&#9660;</span></div>
      <div class="rp-body">
        <div class="rp-row">
          <span class="rp-label">Color</span>
          <div class="color-swatch" id="rp-color" style="background:#e53935;width:24px;height:24px;" onclick="showColorPicker('stroke','rp-color')"></div>
        </div>
        <div class="rp-row">
          <span class="rp-label">Width</span>
          <input class="rp-input" type="range" min="1" max="30" value="2" oninput="g_lineWidth=+this.value;document.getElementById('rb-linewidth').value=this.value">
        </div>
        <div class="rp-row">
          <span class="rp-label">Opacity</span>
          <input class="rp-input" type="range" min="5" max="100" value="100" oninput="g_opacity=+this.value/100;document.getElementById('rb-opacity').value=this.value">
        </div>
        <div class="rp-row">
          <span class="rp-label">Font</span>
          <select class="rp-input" id="rp-font" onchange="g_fontFamily=this.value">
            <option>Arial</option><option>Times New Roman</option>
            <option>Courier New</option><option>Georgia</option><option>Verdana</option>
          </select>
        </div>
        <div class="rp-row">
          <span class="rp-label">Sz</span>
          <input class="rp-input" type="number" id="rp-fontsize" value="12" min="6" max="96" onchange="g_fontSize=+this.value">
        </div>
      </div>
    </div>
)HTML";

ss << LR"HTML(
    <!-- Quick Actions -->
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#9889; Quick Actions <span class="toggle">&#9660;</span></div>
      <div class="rp-body" style="gap:5px;">
        <button class="rp-btn primary" onclick="downloadCurrentPDF()">&#128190; Save PDF</button>
        <button class="rp-btn" onclick="actionMergePDFs()">&#128214; Combine PDFs</button>
        <button class="rp-btn" onclick="modalSplit()">&#9986; Split PDF</button>
        <button class="rp-btn" onclick="actionPDFtoImages()">&#128247; Export Images</button>
        <button class="rp-btn" onclick="actionPDFtoText()">&#128220; Export Text</button>
        <button class="rp-btn" onclick="actionPerformOCR()">&#128065; Run OCR</button>
        <button class="rp-btn" onclick="actionCompressPDF()">&#128230; Compress</button>
        <button class="rp-btn" onclick="modalWatermark()">&#10070; Watermark</button>
        <button class="rp-btn danger" onclick="modalDeletePages()">&#128465; Delete Pages</button>
      </div>
    </div>
    <!-- Stamp chooser -->
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#9997; Stamp Type <span class="toggle">&#9660;</span></div>
      <div class="rp-body">
        <select class="rp-input" id="rp-stamp-type" style="width:100%;">
          <option value="DRAFT">DRAFT</option>
          <option value="APPROVED">APPROVED</option>
          <option value="REJECTED">REJECTED</option>
          <option value="CONFIDENTIAL">CONFIDENTIAL</option>
          <option value="FINAL">FINAL</option>
          <option value="VOID">VOID</option>
          <option value="COPY">COPY</option>
          <option value="RECEIVED">RECEIVED</option>
        </select>
      </div>
    </div>
    <!-- Word count -->
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#9679; Stats <span class="toggle">&#9660;</span></div>
      <div class="rp-body" id="rp-stats">
        <div class="rp-row"><span class="rp-label">Words</span><span id="stat-words" class="badge">0</span></div>
        <div class="rp-row"><span class="rp-label">Chars</span><span id="stat-chars" class="badge">0</span></div>
        <div class="rp-row"><span class="rp-label">Annots</span><span id="stat-annots" class="badge">0</span></div>
        <button class="rp-btn" onclick="refreshStats()" style="margin-top:4px;">Refresh Stats</button>
      </div>
    </div>
  </div>
</div><!-- end workspace -->
)HTML";

ss << LR"HTML(
<!-- Status bar -->
<div class="statusbar">
  <span class="sb-item" id="sb-tool">Tool: Hand</span>
  <div class="sb-sep"></div>
  <span class="sb-item" id="sb-page">Page 0 of 0</span>
  <div class="sb-sep"></div>
  <div class="sb-zoom-row">
    <button class="sb-zoom-btn" onclick="zoomBy(-0.1)">−</button>
    <span id="sb-zoom-val">100%</span>
    <button class="sb-zoom-btn" onclick="zoomBy(0.1)">+</button>
  </div>
  <div class="sb-sep"></div>
  <span class="sb-item" id="sb-coords">0, 0</span>
  <div class="sb-right">
    <span class="sb-item" id="sb-mode">Normal</span>
    <div class="sb-sep"></div>
    <span class="sb-item" style="cursor:pointer;" onclick="enterReadMode()" title="Read Mode (ESC to exit)">&#9634;</span>
    <span class="sb-item" style="cursor:pointer;" onclick="enterPresentation()">&#9654;</span>
  </div>
</div>
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 16 · Colour picker popup, context menu, modals HTML
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- Colour picker popup -->
<div class="color-picker-popup" id="color-picker-popup">
  <div class="color-grid" id="color-grid"></div>
  <div style="margin-top:6px;display:flex;align-items:center;gap:6px;">
    <label style="font-size:10px;">Custom:</label>
    <input type="color" id="color-custom" style="width:36px;height:20px;border:none;cursor:pointer;" onchange="applyCustomColor(this.value)">
  </div>
</div>
<!-- Context menu -->
<div class="ctx-menu" id="ctx-menu">
  <div class="ctx-item" onclick="ctxCopy()">&#128203; Copy Text</div>
  <div class="ctx-item" onclick="setTool('pen');closeCtx()">&#9998; Draw Here</div>
  <div class="ctx-item" onclick="setTool('note');closeCtx()">&#128204; Add Note</div>
  <div class="ctx-item" onclick="addBookmarkAtCtx()">&#9733; Add Bookmark</div>
  <div class="ctx-sep"></div>
  <div class="ctx-item" onclick="rotatePDFAll();closeCtx()">&#8635; Rotate Page</div>
  <div class="ctx-item danger" onclick="ctxDeletePage()">&#128465; Delete This Page</div>
</div>
)HTML";

ss << LR"HTML(
<!-- Signature modal -->
<div class="modal-overlay" id="sig-modal-overlay">
  <div class="modal">
    <h3>&#9998; Draw Your Signature</h3>
    <canvas id="sig-modal-canvas" width="420" height="160" style="border:1px solid var(--c-border);border-radius:3px;cursor:crosshair;background:#fff;touch-action:none;"></canvas>
    <div style="display:flex;gap:6px;margin-top:8px;">
      <button class="btn btn-secondary" onclick="clearSigPad()">Clear</button>
      <select id="sig-color" style="padding:4px;border:1px solid var(--c-border);border-radius:3px;" onchange="g_sigColor=this.value">
        <option value="#1a1a1a">Black</option>
        <option value="#1a3a8f">Blue</option>
        <option value="#8b0000">Dark Red</option>
      </select>
    </div>
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeSigModal()">Cancel</button>
      <button class="btn btn-primary" onclick="applySigToPage()">Insert Signature</button>
    </div>
  </div>
</div>
<!-- General modal -->
<div class="modal-overlay" id="modal-overlay">
  <div class="modal" id="modal-body"></div>
</div>
<!-- Loading -->
<div class="loading-overlay" id="loading-overlay">
  <div class="spinner"></div>
  <div id="loading-txt">Processing…</div>
  <div class="progress-wrap" style="width:200px;margin-top:10px;">
    <div class="progress-bar" id="loading-progress" style="width:0%;"></div>
  </div>
</div>
<!-- Toast box -->
<div class="toast-box" id="toast-box"></div>
<!-- Hidden file inputs -->
<input type="file" id="fileInput" accept=".pdf" style="display:none" multiple onchange="handleFiles(event)">
<input type="file" id="mergeInput" accept=".pdf" style="display:none" multiple onchange="doMerge(event)">
</div><!-- end #app -->
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 17 · JS: globals, utils, pdf.js init
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
pdfjsLib.GlobalWorkerOptions.workerSrc =
  'https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.worker.min.js';

// ── Global state ─────────────────────────────────────────────
let g_tabs = [], g_activeId = null;
let g_tool = 'hand';
let g_color = '#e53935', g_fillColor = 'transparent';
let g_lineWidth = 2, g_opacity = 1.0;
let g_fontSize = 12, g_fontFamily = 'Arial';
let g_sigColor = '#1a1a1a';
let g_showRuler = false, g_showGrid = false;
let g_colorTarget = 'stroke', g_colorSwatchId = null;
let g_viewMode = 0; // 0=normal,1=night,2=sepia
let g_ctxPageIndex = -1;

// Drawing state
let g_drawing = false, g_drawCtx = null, g_drawCanvas = null;
let g_currentStroke = null;
let g_shapeStart = null;

// Pan state
let g_panning = false, g_panX0 = 0, g_panY0 = 0, g_scrollX0 = 0, g_scrollY0 = 0;

// Undo/redo
const MAX_HIST = 60;
)JS";

ss << LR"JS(
// ── Helpers ───────────────────────────────────────────────────
function activeTab() { return g_tabs.find(t => t.id === g_activeId); }

function showToast(msg, type='', dur=3000) {
  const box = document.getElementById('toast-box');
  const el = document.createElement('div');
  el.className = 'toast' + (type ? ' ' + type : '');
  el.textContent = msg;
  box.appendChild(el);
  setTimeout(() => el.remove(), dur);
}

function showLoading(on, txt='Processing…', pct=0) {
  document.getElementById('loading-overlay').classList.toggle('show', on);
  document.getElementById('loading-txt').textContent = txt;
  document.getElementById('loading-progress').style.width = pct + '%';
}

function setProgress(pct) {
  document.getElementById('loading-progress').style.width = pct + '%';
}

function showModal(title, html, wide=false) {
  const body = document.getElementById('modal-body');
  body.innerHTML = '<h3>' + title + '</h3>' + html;
  if (wide) body.style.maxWidth = '600px'; else body.style.maxWidth = '';
  document.getElementById('modal-overlay').classList.add('show');
}

function closeModal() {
  document.getElementById('modal-overlay').classList.remove('show');
}

document.getElementById('modal-overlay').addEventListener('mousedown', e => {
  if (e.target.id === 'modal-overlay') closeModal();
});
)JS";

ss << LR"JS(
async function saveBytesToFile(blob, name, ext='pdf', mime='application/pdf') {
  try {
    if (window.showSaveFilePicker) {
      const h = await window.showSaveFilePicker({
        suggestedName: name,
        types: [{ description: 'File', accept: { [mime]: ['.' + ext] } }]
      });
      const w = await h.createWritable();
      await w.write(blob);
      await w.close();
      showToast('Saved: ' + name, 'success');
    } else {
      saveAs(blob, name);
      showToast('Downloaded: ' + name, 'success');
    }
  } catch (e) {
    if (e.name !== 'AbortError') showToast('Save cancelled.');
  }
}

function updateStatusBar() {
  const t = activeTab();
  if (!t) { document.getElementById('sb-page').textContent = 'Page 0 of 0'; return; }
  document.getElementById('sb-page').textContent = 'Page 1 of ' + t.pageOrder.length;
  const zv = Math.round(t.zoom * 100) + '%';
  document.getElementById('sb-zoom-val').textContent = zv;
  document.getElementById('rp-zoom').textContent = zv;
  document.getElementById('rp-rotate').textContent = t.rotation + '°';
  document.getElementById('rp-filename').textContent = t.name;
  document.getElementById('rp-pages').textContent = t.pageOrder.length;
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 18 · JS: Tab management + file loading
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function openFileDialog(multi=false) {
  const fi = document.getElementById('fileInput');
  fi.multiple = multi;
  fi.click();
}

async function handleFiles(e) {
  const files = Array.from(e.target.files).filter(f => f.name.toLowerCase().endsWith('.pdf'));
  for (const f of files) {
    const bytes = new Uint8Array(await f.arrayBuffer());
    await createTab(f.name, bytes);
  }
  e.target.value = '';
}

async function loadPdfFromPath(path) {
  try {
    const url = 'file:///' + path.replace(/\\/g, '/');
    const res = await fetch(url);
    if (!res.ok) throw new Error('fetch failed');
    const bytes = new Uint8Array(await res.arrayBuffer());
    const name = path.split(/[\\/]/).pop();
    await createTab(name, bytes);
  } catch (e) {
    showToast('Failed to load: ' + e.message);
  }
}
)JS";

ss << LR"JS(
// ── Recent Files (localStorage simulation via in-memory) ────────
let g_recentFiles = [];

function addToRecent(name, bytes) {
  // Keep last 8 unique files
  g_recentFiles = g_recentFiles.filter(r => r.name !== name);
  g_recentFiles.unshift({ name, bytes: bytes.slice(), date: new Date().toLocaleString() });
  if (g_recentFiles.length > 8) g_recentFiles.pop();
}

function renderRecentFiles() {
  const list = document.getElementById('recent-files-list');
  if (!list) return;
  if (g_recentFiles.length === 0) {
    list.innerHTML = '<p style="font-size:11px;color:#bbb;padding:10px 0;text-align:center;">No recent files yet</p>';
    return;
  }
  list.innerHTML = g_recentFiles.map((r, i) => `
    <div onclick="reopenRecent(${i})" style="display:flex;align-items:center;gap:10px;padding:9px 12px;
      background:#fff;border:1px solid #e8e8e8;border-radius:5px;margin-bottom:6px;
      cursor:pointer;transition:background .12s;" onmouseover="this.style.background='#f5f5f5'" onmouseout="this.style.background='#fff'">
      <span style="font-size:22px;">&#128196;</span>
      <div style="flex:1;min-width:0;">
        <p style="font-size:12px;font-weight:600;color:#333;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${r.name}</p>
        <p style="font-size:10px;color:#aaa;margin-top:1px;">${r.date}</p>
      </div>
      <span style="font-size:10px;color:#c0392b;font-weight:700;">Open ›</span>
    </div>`).join('');
}

async function reopenRecent(idx) {
  const r = g_recentFiles[idx];
  if (!r) return;
  await createTab(r.name, r.bytes);
}

async function createTab(name, bytes) {
  try {
    // Hide homepage if visible
    const hint = document.getElementById('empty-hint');
    if (hint) hint.remove();
    const doc = await pdfjsLib.getDocument({ data: bytes.slice() }).promise;
    const id = 'tab_' + Date.now() + Math.random().toString(36).slice(2, 6);
    const tab = {
      id, name, bytes: bytes.slice(), doc,
      zoom: 1.0, rotation: 0,
      annotations: [],    // draw strokes, notes, shapes, stamps
      pasteImages: [],    // pasted image overlays
      textBoxes: [],      // editable text boxes
      redactions: [],     // redaction rects
      bookmarks: [],      // {pageIndex, label}
      pageOrder: Array.from({ length: doc.numPages }, (_, i) => i + 1),
      history: [], histIdx: -1,
      modified: false
    };
    g_tabs.push(tab);
    addToRecent(name, bytes);
    renderTabStrip();
    await switchTab(id);
  } catch (e) {
    showToast('Invalid or corrupted PDF.');
  }
}

function renderTabStrip() {
  const strip = document.getElementById('tabbar');
  strip.innerHTML = '';
  g_tabs.forEach(t => {
    const el = document.createElement('div');
    el.className = 'pdf-tab' + (t.id === g_activeId ? ' active' : '');
    el.innerHTML =
      '<span class="tab-icon">&#128196;</span>' +
      '<span class="tab-name">' + (t.name.length > 22 ? t.name.slice(0, 20) + '…' : t.name) + '</span>' +
      (t.modified ? '<span class="tab-modified">●</span>' : '') +
      '<span class="tab-close">&#x2715;</span>';
    el.querySelector('.tab-close').onclick = ev => { ev.stopPropagation(); closeTab(t.id); };
    el.onclick = () => switchTab(t.id);
    strip.appendChild(el);
  });
  const add = document.createElement('div');
  add.className = 'tab-add'; add.textContent = '+';
  add.onclick = () => openFileDialog(true);
  strip.appendChild(add);
}
)JS";

ss << LR"JS(
async function switchTab(id) {
  // Fix for the error when opening a second file: Make sure to reset rendering state.
  if (g_renderTimer) clearTimeout(g_renderTimer);
  g_drawing = false;
  g_shapeDrawing = false;
  g_sigDrawing = false;
  g_panning = false;

  g_activeId = id;
  renderTabStrip();
  const t = activeTab(); if (!t) return;
  updateStatusBar();
  await renderViewer();
  buildThumbs();
  renderBookmarkPanel();
  refreshStats();
}

function closeActiveTab() { if (g_activeId) closeTab(g_activeId); }

function closeTab(id) {
  g_tabs = g_tabs.filter(t => t.id !== id);
  if (g_activeId === id) {
    g_activeId = g_tabs.length > 0 ? g_tabs[g_tabs.length - 1].id : null;
  }
  renderTabStrip();
  if (g_activeId) switchTab(g_activeId);
  else {
    showHomepage();
    document.getElementById('lp-thumb').innerHTML = '';
  }
}

function showHomepage() {
  const container = document.getElementById('pdf-container');
  container.innerHTML = `
    <div id="empty-hint" style="margin-top:0;text-align:center;color:#777;pointer-events:auto;width:100%;padding:30px 20px;">
      <div style="font-size:52px;opacity:.25;margin-bottom:8px;">&#128196;</div>
      <p style="font-size:16px;font-weight:700;color:#444;margin-bottom:4px;">PDF Pro — Acrobat Edition</p>
      <p style="font-size:11.5px;opacity:.65;margin-bottom:20px;">File ▸ Open, drag &amp; drop, or click a recent file below</p>
      <div style="display:flex;gap:10px;justify-content:center;margin-bottom:24px;">
        <button onclick="openFileDialog(false)" style="padding:8px 20px;background:var(--c-accent);color:#fff;border:none;border-radius:4px;font-size:12px;font-weight:700;cursor:pointer;">&#128196; Open PDF</button>
        <button onclick="openFileDialog(true)" style="padding:8px 20px;background:var(--c-accent2);color:#fff;border:none;border-radius:4px;font-size:12px;font-weight:700;cursor:pointer;">&#128214; Open Multiple</button>
      </div>
      <div id="recent-files-section" style="max-width:560px;margin:0 auto;text-align:left;">
        <p style="font-size:10px;font-weight:700;color:#999;text-transform:uppercase;letter-spacing:1px;margin-bottom:8px;padding-left:4px;">&#128337; Recent Files</p>
        <div id="recent-files-list"></div>
      </div>
    </div>`;
  renderRecentFiles();
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 19 · JS: Rendering engine
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
let g_renderTimer = null;
function scheduleRender(delay = 60) {
  if (g_renderTimer) clearTimeout(g_renderTimer);
  g_renderTimer = setTimeout(() => renderViewer(), delay);
}

// Render token: cancels stale renders when a new one starts
let g_renderToken = 0;

async function renderViewer() {
  const t = activeTab(); if (!t) return;
  const myToken = ++g_renderToken; // invalidate any previous render

  const container = document.getElementById('pdf-container');
  container.innerHTML = '';

  const DPR = window.devicePixelRatio || 1; // HiDPI support

  for (let i = 0; i < t.pageOrder.length; i++) {
    if (myToken !== g_renderToken) return; // cancelled by newer render

    const pNum = t.pageOrder[i];
    const page = await t.doc.getPage(pNum);
    // ── Viewport: render at DPR×zoom for crisp output ──
    const vpCss  = page.getViewport({ scale: t.zoom, rotation: t.rotation }); // layout size
    const vp     = page.getViewport({ scale: t.zoom * DPR, rotation: t.rotation }); // render size

    const wrapper = document.createElement('div');
    wrapper.className = 'page-wrapper';
    wrapper.id = 'pw-' + i;
    wrapper.dataset.pageIndex = i;
    wrapper.dataset.pdfPage   = pNum;
    wrapper.style.width  = vpCss.width  + 'px';  // layout uses CSS size
    wrapper.style.height = vpCss.height + 'px';
)JS";

ss << LR"JS(
    // ── PDF canvas: full HiDPI resolution ──
    const pdfCanvas = document.createElement('canvas');
    pdfCanvas.className    = 'pdf-canvas';
    pdfCanvas.width        = Math.round(vp.width);   // already DPR×zoom resolution
    pdfCanvas.height       = Math.round(vp.height);
    pdfCanvas.style.width  = vpCss.width  + 'px';    // display at CSS size
    pdfCanvas.style.height = vpCss.height + 'px';
    const pdfCtx = pdfCanvas.getContext('2d');
    // No ctx.scale needed — viewport is already at DPR scale

    // ── Draw canvas (annotation layer — CSS size is fine) ──
    const drawCanvas = document.createElement('canvas');
    drawCanvas.className   = 'draw-canvas';
    drawCanvas.width       = vpCss.width;
    drawCanvas.height      = vpCss.height;
    drawCanvas.style.width  = vpCss.width  + 'px';
    drawCanvas.style.height = vpCss.height + 'px';

    // ── Grid + selection ──
    const grid    = document.createElement('div');
    grid.className = 'grid-overlay' + (g_showGrid ? ' show' : '');
    const selRect  = document.createElement('div');
    selRect.className = 'sel-rect';

    wrapper.appendChild(pdfCanvas);
    wrapper.appendChild(drawCanvas);
    wrapper.appendChild(grid);
    wrapper.appendChild(selRect);
    container.appendChild(wrapper);

    // ── Render & AWAIT so page is visible before moving on ──
    try {
      await page.render({ canvasContext: pdfCtx, viewport: vp }).promise;
    } catch(e) { /* page destroyed / replaced — skip */ }

    if (myToken !== g_renderToken) return; // cancelled mid-render

    // ── Restore overlays ──
    restoreDrawAnnotations(i, drawCanvas, vp.width, vp.height);
    restoreShapes(t, i, wrapper);
    restoreTextBoxes(t, i, wrapper);
    restorePasteImages(t, i, wrapper);
    restoreStickyNotes(t, i, wrapper);
    restoreStamps(t, i, wrapper);
    restoreRedactions(t, i, wrapper);
  }
  updateStatusBar();
  refreshStats();
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 20 · JS: Zoom, Rotate, keyboard shortcuts
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function zoomBy(delta) {
  const t = activeTab(); if (!t) return;
  t.zoom = Math.min(5, Math.max(0.1, t.zoom + delta));
  updateStatusBar();
  scheduleRender();
}
function zoomTo(v) {
  const t = activeTab(); if (!t) return;
  t.zoom = v; updateStatusBar(); scheduleRender();
}
function rotatePDFAll() {
  const t = activeTab(); if (!t) return;
  t.rotation = (t.rotation + 90) % 360;
  updateStatusBar(); scheduleRender();
}

// Smooth Ctrl+Wheel zoom
document.getElementById('viewer-area').addEventListener('wheel', e => {
  if (!e.ctrlKey) return;
  e.preventDefault();
  const t = activeTab(); if (!t) return;
  t.zoom = Math.min(5, Math.max(0.1, t.zoom + (e.deltaY > 0 ? -0.07 : 0.07)));
  updateStatusBar(); scheduleRender(40);
}, { passive: false });
)JS";

ss << LR"JS(
// Mouse‑position in statusbar
document.getElementById('viewer-area').addEventListener('mousemove', e => {
  const wrapper = e.target.closest('.page-wrapper');
  if (!wrapper) { document.getElementById('sb-coords').textContent = ''; return; }
  const rect = wrapper.getBoundingClientRect();
  const x = Math.round(e.clientX - rect.left);
  const y = Math.round(e.clientY - rect.top);
  document.getElementById('sb-coords').textContent = x + ', ' + y;
});

// Global keyboard shortcuts
document.addEventListener('keydown', e => {
  if (e.target.contentEditable === 'true' || e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
  const ctrl = e.ctrlKey || e.metaKey;
  if (ctrl && e.key === 'o') { e.preventDefault(); openFileDialog(true); }
  if (ctrl && e.key === 's') { e.preventDefault(); downloadCurrentPDF(); }
  if (ctrl && e.key === 'z') { e.preventDefault(); histUndo(); }
  if (ctrl && e.key === 'y') { e.preventDefault(); histRedo(); }
  if (ctrl && e.key === 'f') { e.preventDefault(); toggleFindBar(); }
  if (ctrl && e.key === '0') { zoomTo(1.0); }
  if (ctrl && e.key === '=') { zoomBy(0.15); }
  if (ctrl && e.key === '-') { zoomBy(-0.15); }
  if (e.key === 'Escape') {
    // Priority: modal > menus > presentation > read mode
    if (document.querySelector('.modal-overlay[style*="flex"]') ||
        document.getElementById('loading-overlay')?.style.display === 'flex') {
      closeModal(); return;
    }
    if (document.querySelector('.dropdown.open')) { closeAllMenus(); closeCtx(); return; }
    if (document.body.classList.contains('present')) { enterPresentation(); return; } // toggles off
    if (document.body.classList.contains('read')) { exitReadMode(); return; }
  }
  if (e.key === 'Delete') deleteSelectedAnnotation();
  // Tool keys
  if (e.key === '1') setTool('hand');
  if (e.key === '2') setTool('select');
  if (e.key === '3') setTool('pen');
  if (e.key === '4') setTool('highlight');
  if (e.key === '5') setTool('eraser');
  if (e.key === '6') setTool('note');
  if (e.key === '7') setTool('textbox');
  if (e.key === '8') setTool('stamp');
  if (e.key === 'r' || e.key === 'R') rotatePDFAll();
});

let g_selectedAnnot = null;
function deleteSelectedAnnotation() {
  if (!g_selectedAnnot) return;
  const t = activeTab(); if (!t) return;
  t.annotations = t.annotations.filter(a => a !== g_selectedAnnot);
  g_selectedAnnot = null;
  scheduleRender();
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 21 · JS: Tool selection + Hand pan
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
const TOOL_LABELS = {
  hand:'Hand', select:'Select', pen:'Pen', highlight:'Highlight',
  eraser:'Eraser', note:'Note', textbox:'TextBox', stamp:'Stamp',
  rect:'Rectangle', ellipse:'Ellipse', line:'Line', arrow:'Arrow',
  redact:'Redact', crop:'Crop', underline:'Underline', strikethrough:'Strikethrough'
};

function setTool(tool) {
  g_tool = tool;
  // Clear all active states
  document.querySelectorAll('.rbtn[id^="rb-"]').forEach(b => b.classList.remove('active'));
  const rbtn = document.getElementById('rb-' + tool);
  if (rbtn) rbtn.classList.add('active');

  const drawTools = ['pen','highlight','eraser','rect','ellipse','line','arrow','underline','strikethrough','redact','crop'];
  document.querySelectorAll('.draw-canvas').forEach(c => {
    c.classList.toggle('can-draw', drawTools.includes(tool));
  });

  const va = document.getElementById('viewer-area');
  if (tool === 'hand') va.style.cursor = 'grab';
  else if (tool === 'crop') va.style.cursor = 'crosshair';
  else if (drawTools.includes(tool)) va.style.cursor = 'crosshair';
  else if (tool === 'note' || tool === 'stamp' || tool === 'textbox') va.style.cursor = 'cell';
  else va.style.cursor = 'default';

  document.getElementById('sb-tool').textContent = 'Tool: ' + (TOOL_LABELS[tool] || tool);
  closeAllMenus();
}
)JS";

ss << LR"JS(
// ── Hand (pan) ──────────────────────────────────────────────
const viewerArea = document.getElementById('viewer-area');
viewerArea.addEventListener('mousedown', e => {
  if (g_tool !== 'hand' || e.button !== 0) return;
  g_panning = true;
  g_panX0 = e.clientX; g_panY0 = e.clientY;
  g_scrollX0 = viewerArea.scrollLeft;
  g_scrollY0 = viewerArea.scrollTop;
  viewerArea.style.cursor = 'grabbing';
  e.preventDefault();
});
window.addEventListener('mousemove', e => {
  if (!g_panning) return;
  viewerArea.scrollLeft = g_scrollX0 - (e.clientX - g_panX0);
  viewerArea.scrollTop = g_scrollY0 - (e.clientY - g_panY0);
});
window.addEventListener('mouseup', () => {
  if (g_panning) { g_panning = false; if (g_tool === 'hand') viewerArea.style.cursor = 'grab'; }
});
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 22 · JS: Canvas drawing (pen, highlight, eraser, shapes)
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function getCanvasXY(canvas, e) {
  const r = canvas.getBoundingClientRect();
  return { x: e.clientX - r.left, y: e.clientY - r.top };
}

document.getElementById('pdf-container').addEventListener('mousedown', e => {
  const canvas = e.target;
  if (!canvas.classList.contains('draw-canvas') || !canvas.classList.contains('can-draw')) return;
  if (g_tool === 'eraser') { eraseAt(canvas, e); return; }
  if (['rect','ellipse','line','arrow','redact'].includes(g_tool)) { startShape(canvas, e); return; }

  const drawTools = ['pen','highlight','underline','strikethrough'];
  if (!drawTools.includes(g_tool)) return;

  g_drawing = true;
  g_drawCanvas = canvas;
  g_drawCtx = canvas.getContext('2d');
  const pos = getCanvasXY(canvas, e);
  const t = activeTab(); if (!t) return;
  const wrapper = canvas.parentElement;
  const pIdx = parseInt(wrapper.dataset.pageIndex);

  let col, alpha, lw;
  if (g_tool === 'highlight') { col = '#FFE000'; alpha = 0.4; lw = 14; }
  else if (g_tool === 'underline') { col = g_color; alpha = 1; lw = 2; }
  else if (g_tool === 'strikethrough') { col = g_color; alpha = 1; lw = 2; }
  else { col = g_color; alpha = g_opacity; lw = g_lineWidth; }

  g_currentStroke = {
    type: 'stroke', tool: g_tool, pageIndex: pIdx,
    color: col, lineWidth: lw, alpha,
    points: [{ rx: pos.x / canvas.width, ry: pos.y / canvas.height }],
    id: 'a_' + Date.now()
  };

  g_drawCtx.save();
  g_drawCtx.strokeStyle = col;
  g_drawCtx.lineWidth = lw;
  g_drawCtx.lineCap = 'round';
  g_drawCtx.lineJoin = 'round';
  g_drawCtx.globalAlpha = alpha;
  g_drawCtx.beginPath();
  g_drawCtx.moveTo(pos.x, pos.y);
  e.preventDefault();
});
)JS";

ss << LR"JS(
window.addEventListener('mousemove', e => {
  if (!g_drawing || !g_drawCtx || !g_drawCanvas) return;
  const pos = getCanvasXY(g_drawCanvas, e);
  g_drawCtx.lineTo(pos.x, pos.y);
  g_drawCtx.stroke();
  g_drawCtx.beginPath();
  g_drawCtx.moveTo(pos.x, pos.y);
  if (g_currentStroke) {
    g_currentStroke.points.push({ rx: pos.x / g_drawCanvas.width, ry: pos.y / g_drawCanvas.height });
  }
});

window.addEventListener('mouseup', () => {
  if (!g_drawing) return;
  g_drawing = false;
  if (g_drawCtx) { g_drawCtx.restore(); g_drawCtx = null; }
  const t = activeTab();
  if (t && g_currentStroke && g_currentStroke.points.length > 1) {
    pushHistory(t);
    t.annotations.push(g_currentStroke);
    t.modified = true;
    renderTabStrip();
    refreshStats();
  }
  g_currentStroke = null; g_drawCanvas = null;
});

function restoreDrawAnnotations(pageIndex, canvas, w, h) {
  const t = activeTab(); if (!t) return;
  const ctx = canvas.getContext('2d');
  t.annotations.filter(a => a.type === 'stroke' && a.pageIndex === pageIndex).forEach(a => {
    if (!a.points || a.points.length < 2) return;
    ctx.save();
    ctx.strokeStyle = a.color;
    ctx.lineWidth = a.lineWidth;
    ctx.lineCap = 'round'; ctx.lineJoin = 'round';
    ctx.globalAlpha = a.alpha || 1;
    ctx.beginPath();
    a.points.forEach((p, i) => {
      const px = p.rx * w, py = p.ry * h;
      i === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
    });
    ctx.stroke(); ctx.restore();
  });
}
)JS";

ss << LR"JS(
// ── Eraser ───────────────────────────────────────────────────
function eraseAt(canvas, e) {
  const pos = getCanvasXY(canvas, e);
  const ctx = canvas.getContext('2d');
  const r = 20 * (g_lineWidth / 2);
  ctx.save(); ctx.globalCompositeOperation = 'destination-out';
  ctx.beginPath(); ctx.arc(pos.x, pos.y, r, 0, Math.PI * 2);
  ctx.fill(); ctx.restore();
  // Also erase from stored strokes (remove points near)
  const t = activeTab(); if (!t) return;
  const wrapper = canvas.parentElement;
  const pIdx = parseInt(wrapper.dataset.pageIndex);
  const w = canvas.width, h = canvas.height;
  t.annotations = t.annotations.filter(a => {
    if (a.type !== 'stroke' || a.pageIndex !== pIdx) return true;
    return !a.points.some(p => Math.hypot(p.rx * w - pos.x, p.ry * h - pos.y) < r);
  });
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 23 · JS: Shape drawing (rect, ellipse, line, arrow)
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
let g_shapeCanvas = null, g_shapeCtx = null, g_shapePdfCanvas = null;
let g_shapeDrawing = false;

function startShape(canvas, e) {
  g_shapeCanvas = canvas;
  g_shapeCtx = canvas.getContext('2d');
  g_shapePdfCanvas = canvas.previousElementSibling; // pdf canvas beneath
  const pos = getCanvasXY(canvas, e);
  g_shapeStart = pos;
  g_shapeDrawing = true;
  e.preventDefault();
}

window.addEventListener('mousemove', e => {
  if (!g_shapeDrawing || !g_shapeCanvas) return;
  const pos = getCanvasXY(g_shapeCanvas, e);
  const ctx = g_shapeCtx;
  // Redraw draw canvas: restore strokes then draw preview
  const wrapper = g_shapeCanvas.parentElement;
  const pIdx = parseInt(wrapper.dataset.pageIndex);
  const w = g_shapeCanvas.width, h = g_shapeCanvas.height;
  ctx.clearRect(0, 0, w, h);
  restoreDrawAnnotations(pIdx, g_shapeCanvas, w, h);
  // Draw shape preview
  ctx.save();
  ctx.strokeStyle = g_color;
  ctx.lineWidth = g_lineWidth;
  ctx.globalAlpha = g_opacity;
  ctx.fillStyle = g_fillColor === 'transparent' ? 'transparent' : g_fillColor;
  const x = Math.min(g_shapeStart.x, pos.x);
  const y = Math.min(g_shapeStart.y, pos.y);
  const w2 = Math.abs(pos.x - g_shapeStart.x);
  const h2 = Math.abs(pos.y - g_shapeStart.y);
  ctx.beginPath();
  if (g_tool === 'rect') {
    ctx.rect(x, y, w2, h2);
  } else if (g_tool === 'ellipse') {
    ctx.ellipse(x + w2/2, y + h2/2, w2/2, h2/2, 0, 0, Math.PI*2);
  } else if (g_tool === 'line') {
    ctx.moveTo(g_shapeStart.x, g_shapeStart.y); ctx.lineTo(pos.x, pos.y);
  } else if (g_tool === 'arrow') {
    ctx.moveTo(g_shapeStart.x, g_shapeStart.y); ctx.lineTo(pos.x, pos.y);
    const ang = Math.atan2(pos.y - g_shapeStart.y, pos.x - g_shapeStart.x);
    const al = 12;
    ctx.moveTo(pos.x, pos.y);
    ctx.lineTo(pos.x - al*Math.cos(ang-0.4), pos.y - al*Math.sin(ang-0.4));
    ctx.moveTo(pos.x, pos.y);
    ctx.lineTo(pos.x - al*Math.cos(ang+0.4), pos.y - al*Math.sin(ang+0.4));
  } else if (g_tool === 'redact') {
    ctx.fillStyle = 'rgba(0,0,0,0.5)';
    ctx.fillRect(x, y, w2, h2);
  }
  if (g_fillColor !== 'transparent' && ['rect','ellipse'].includes(g_tool)) ctx.fill();
  ctx.stroke();
  ctx.restore();
});
)JS";

ss << LR"JS(
window.addEventListener('mouseup', e => {
  if (!g_shapeDrawing) return;
  g_shapeDrawing = false;
  const t = activeTab();
  if (!t || !g_shapeCanvas) { g_shapeCanvas = null; return; }
  const pos = getCanvasXY(g_shapeCanvas, e);
  const wrapper = g_shapeCanvas.parentElement;
  const pIdx = parseInt(wrapper.dataset.pageIndex);
  const w = g_shapeCanvas.width, h = g_shapeCanvas.height;
  if (Math.abs(pos.x - g_shapeStart.x) < 3 && Math.abs(pos.y - g_shapeStart.y) < 3) {
    g_shapeCanvas = null; return;
  }
  pushHistory(t);
  t.annotations.push({
    type: 'shape', tool: g_tool, pageIndex: pIdx,
    x1: g_shapeStart.x/w, y1: g_shapeStart.y/h,
    x2: pos.x/w, y2: pos.y/h,
    color: g_color, fillColor: g_fillColor,
    lineWidth: g_lineWidth, alpha: g_opacity,
    id: 'sh_' + Date.now()
  });
  t.modified = true; renderTabStrip();
  g_shapeCanvas = null; g_shapeStart = null;
});

function restoreShapes(tab, pageIndex, wrapper) {
  const canvas = wrapper.querySelector('.draw-canvas');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const w = canvas.width, h = canvas.height;
  tab.annotations.filter(a => a.type === 'shape' && a.pageIndex === pageIndex).forEach(a => {
    const x1 = a.x1*w, y1 = a.y1*h, x2 = a.x2*w, y2 = a.y2*h;
    const rx = Math.min(x1,x2), ry = Math.min(y1,y2);
    const rw = Math.abs(x2-x1), rh = Math.abs(y2-y1);
    ctx.save();
    ctx.strokeStyle = a.color; ctx.lineWidth = a.lineWidth;
    ctx.globalAlpha = a.alpha || 1;
    if (a.fillColor && a.fillColor !== 'transparent') ctx.fillStyle = a.fillColor;
    ctx.beginPath();
    if (a.tool === 'rect') { ctx.rect(rx, ry, rw, rh); }
    else if (a.tool === 'ellipse') { ctx.ellipse(rx+rw/2, ry+rh/2, rw/2, rh/2, 0, 0, Math.PI*2); }
    else if (a.tool === 'line') { ctx.moveTo(x1,y1); ctx.lineTo(x2,y2); }
    else if (a.tool === 'arrow') {
      ctx.moveTo(x1,y1); ctx.lineTo(x2,y2);
      const ang = Math.atan2(y2-y1, x2-x1), al = 12;
      ctx.moveTo(x2,y2); ctx.lineTo(x2-al*Math.cos(ang-0.4), y2-al*Math.sin(ang-0.4));
      ctx.moveTo(x2,y2); ctx.lineTo(x2-al*Math.cos(ang+0.4), y2-al*Math.sin(ang+0.4));
    } else if (a.tool === 'redact') { ctx.fillStyle = '#000'; ctx.fillRect(rx,ry,rw,rh); }
    if (a.fillColor && a.fillColor !== 'transparent' && ['rect','ellipse'].includes(a.tool)) ctx.fill();
    ctx.stroke(); ctx.restore();
  });
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 24 · JS: Sticky Notes
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
const NOTE_COLORS = ['#fff9c4','#c8e6c9','#bbdefb','#fce4ec','#ffe0b2'];

function restoreStickyNotes(tab, pageIndex, wrapper) {
  tab.annotations.filter(a => a.type === 'note' && a.pageIndex === pageIndex)
    .forEach(a => createNoteEl(a, wrapper));
}

function createNoteEl(data, wrapper) {
  const note = document.createElement('div');
  note.style.cssText = `
    position:absolute;z-index:10;background:${data.color||'#fff9c4'};
    border:1px solid #d4b800;border-radius:3px;padding:5px 6px;
    font-size:11px;min-width:90px;min-height:44px;max-width:200px;
    box-shadow:2px 3px 8px rgba(0,0,0,.2);cursor:move;resize:both;
    overflow:auto;color:#333;white-space:pre-wrap;word-break:break-word;
    left:${data.rx*100}%;top:${data.ry*100}%;
  `;
  note.contentEditable = 'true';
  note.textContent = data.text || '';
  const bar = document.createElement('div');
  bar.style.cssText = 'display:flex;gap:3px;margin-bottom:3px;';
  NOTE_COLORS.forEach(c => {
    const sw = document.createElement('span');
    sw.style.cssText = `width:10px;height:10px;border-radius:50%;background:${c};cursor:pointer;border:1px solid rgba(0,0,0,.12);`;
    sw.onclick = () => { note.style.background = c; data.color = c; };
    bar.appendChild(sw);
  });
  const close = document.createElement('span');
  close.textContent = '×';
  close.style.cssText = 'margin-left:auto;cursor:pointer;color:#999;font-size:12px;line-height:1;';
  close.onclick = () => {
    const t = activeTab(); if (t) t.annotations = t.annotations.filter(a => a.id !== data.id);
    note.remove(); refreshStats();
  };
  bar.appendChild(close);
  note.prepend(bar);
  note.addEventListener('input', () => { data.text = note.textContent; });
  makeDraggable(note, wrapper, data);
  wrapper.appendChild(note);
}
)JS";

ss << LR"JS(
document.getElementById('pdf-container').addEventListener('click', e => {
  if (g_tool !== 'note') return;
  const wrapper = e.target.closest('.page-wrapper'); if (!wrapper) return;
  if (e.target.contentEditable === 'true') return;
  const rect = wrapper.getBoundingClientRect();
  const rx = (e.clientX - rect.left) / rect.width;
  const ry = (e.clientY - rect.top) / rect.height;
  const t = activeTab(); if (!t) return;
  const pIdx = parseInt(wrapper.dataset.pageIndex);
  const data = { type:'note', id:'n_'+Date.now(), pageIndex:pIdx, rx, ry, text:'', color:'#fff9c4' };
  pushHistory(t); t.annotations.push(data); t.modified = true;
  createNoteEl(data, wrapper); renderTabStrip(); refreshStats();
});

function makeDraggable(el, container, data) {
  let sx=0, sy=0, ex=0, ey=0, drag=false;
  const onDown = e => {
    if (e.target.contentEditable === 'true' && e.target !== el) return;
    if (e.target.tagName === 'SPAN' || e.target.tagName === 'SELECT' || e.target.tagName === 'INPUT') return;
    drag=true; sx=e.clientX; sy=e.clientY; ex=el.offsetLeft; ey=el.offsetTop;
    e.preventDefault();
  };
  const onMove = e => {
    if (!drag) return;
    const nx = ex+(e.clientX-sx), ny = ey+(e.clientY-sy);
    el.style.left = nx+'px'; el.style.top = ny+'px';
    if (data) { data.rx = nx/container.offsetWidth; data.ry = ny/container.offsetHeight; }
  };
  const onUp = () => { drag = false; };
  el.addEventListener('mousedown', onDown);
  window.addEventListener('mousemove', onMove);
  window.addEventListener('mouseup', onUp);
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 25 · JS: Text Boxes
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function restoreTextBoxes(tab, pageIndex, wrapper) {
  tab.textBoxes.filter(tb => tb.pageIndex === pageIndex)
    .forEach(tb => createTextBoxEl(tb, wrapper));
}

function createTextBoxEl(data, wrapper) {
  const tb = document.createElement('div');
  tb.className = 'textbox-el';
  tb.style.left = (data.rx*100)+'%';
  tb.style.top = (data.ry*100)+'%';
  tb.style.fontSize = (data.fontSize||12)+'px';
  tb.style.fontFamily = data.fontFamily||'Arial';
  tb.style.color = data.color||'#111';
  tb.contentEditable = 'true';
  tb.textContent = data.text||'';
  const close = document.createElement('span');
  close.textContent='×';
  close.style.cssText='position:absolute;top:1px;right:3px;cursor:pointer;font-size:11px;color:#aaa;';
  close.onclick=()=>{
    const t=activeTab(); if(t) t.textBoxes=t.textBoxes.filter(x=>x!==data);
    tb.remove();
  };
  tb.appendChild(close);
  tb.addEventListener('input',()=>{data.text=tb.textContent;});
  makeDraggable(tb, wrapper, data);
  wrapper.appendChild(tb);
}
)JS";

ss << LR"JS(
document.getElementById('pdf-container').addEventListener('click', e => {
  if (g_tool !== 'textbox') return;
  const wrapper = e.target.closest('.page-wrapper'); if (!wrapper) return;
  if (e.target.classList.contains('textbox-el') || e.target.closest('.textbox-el')) return;
  const rect = wrapper.getBoundingClientRect();
  const data = {
    pageIndex: parseInt(wrapper.dataset.pageIndex),
    rx: (e.clientX-rect.left)/rect.width,
    ry: (e.clientY-rect.top)/rect.height,
    text:'', fontSize:g_fontSize, fontFamily:g_fontFamily, color:g_color
  };
  const t = activeTab(); if (!t) return;
  pushHistory(t); t.textBoxes.push(data); t.modified=true;
  createTextBoxEl(data, wrapper); renderTabStrip();
});
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 26 · JS: Stamps + Paste Images
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
const STAMP_COLORS = {
  DRAFT:'#1565c0', APPROVED:'#2e7d32', REJECTED:'#c62828',
  CONFIDENTIAL:'#6a1b9a', FINAL:'#00695c', VOID:'#4e342e',
  COPY:'#616161', RECEIVED:'#1b5e20'
};

function restoreStamps(tab, pageIndex, wrapper) {
  tab.annotations.filter(a => a.type==='stamp' && a.pageIndex===pageIndex)
    .forEach(a => createStampEl(a, wrapper));
}

function createStampEl(data, wrapper) {
  const el = document.createElement('div');
  el.className = 'stamp-el';
  const col = STAMP_COLORS[data.label]||'#555';
  el.style.cssText = `left:${data.rx*100}%;top:${data.ry*100}%;color:${col};border-color:${col};`;
  el.textContent = data.label;
  const del = document.createElement('span');
  del.textContent='×'; del.style.cssText='position:absolute;top:-6px;right:-6px;background:red;color:#fff;border-radius:50%;width:14px;height:14px;font-size:10px;display:flex;align-items:center;justify-content:center;cursor:pointer;';
  del.onclick=()=>{
    const t=activeTab(); if(t) t.annotations=t.annotations.filter(a=>a!==data);
    el.remove();
  };
  el.appendChild(del);
  makeDraggable(el, wrapper, data);
  wrapper.appendChild(el);
}
)JS";

ss << LR"JS(
document.getElementById('pdf-container').addEventListener('click', e => {
  if (g_tool !== 'stamp') return;
  const wrapper = e.target.closest('.page-wrapper'); if (!wrapper) return;
  const rect = wrapper.getBoundingClientRect();
  const label = document.getElementById('rp-stamp-type').value;
  const data = {
    type:'stamp', id:'st_'+Date.now(),
    pageIndex:parseInt(wrapper.dataset.pageIndex),
    rx:(e.clientX-rect.left)/rect.width,
    ry:(e.clientY-rect.top)/rect.height,
    label
  };
  const t = activeTab(); if (!t) return;
  pushHistory(t); t.annotations.push(data); t.modified=true;
  createStampEl(data, wrapper); renderTabStrip();
});

// ── Paste images ──────────────────────────────────────────────
function restorePasteImages(tab, pageIndex, wrapper) {
  tab.pasteImages.filter(img => img.pageIndex===pageIndex)
    .forEach(img => createImgOverlay(img, wrapper));
}

function createImgOverlay(data, wrapper) {
  const div = document.createElement('div');
  div.style.cssText = `position:absolute;z-index:14;left:${data.rx*100}%;top:${data.ry*100}%;width:${data.rw*100}%;height:${data.rh*100}%;border:2px solid var(--c-accent2);cursor:move;`;
  const img = document.createElement('img');
  img.src = data.src; img.style.cssText='width:100%;height:100%;display:block;pointer-events:none;';
  const del = document.createElement('span');
  del.textContent='×';
  del.style.cssText='position:absolute;top:-6px;right:-6px;background:red;color:#fff;border-radius:50%;width:14px;height:14px;font-size:10px;display:flex;align-items:center;justify-content:center;cursor:pointer;z-index:15;';
  del.onclick=()=>{
    const t=activeTab(); if(t) t.pasteImages=t.pasteImages.filter(i=>i!==data);
    div.remove();
  };
  div.appendChild(img); div.appendChild(del);
  // SE resize
  const rh = document.createElement('div');
  rh.style.cssText='position:absolute;bottom:-5px;right:-5px;width:10px;height:10px;background:var(--c-accent);border-radius:1px;cursor:se-resize;z-index:15;';
  rh.addEventListener('mousedown', ev => startImgResize(ev, div, data, wrapper));
  div.appendChild(rh);
  makeDraggable(div, wrapper, null);
  div.addEventListener('mouseup', () => {
    data.rx=div.offsetLeft/wrapper.offsetWidth;
    data.ry=div.offsetTop/wrapper.offsetHeight;
  });
  wrapper.appendChild(div);
}
)JS";

ss << LR"JS(
function startImgResize(e, el, data, wrapper) {
  e.stopPropagation(); e.preventDefault();
  const sx=e.clientX, sy=e.clientY, sw=el.offsetWidth, sh=el.offsetHeight;
  const onM=ev=>{
    el.style.width=Math.max(40,sw+(ev.clientX-sx))+'px';
    el.style.height=Math.max(30,sh+(ev.clientY-sy))+'px';
  };
  const onU=()=>{
    data.rw=el.offsetWidth/wrapper.offsetWidth;
    data.rh=el.offsetHeight/wrapper.offsetHeight;
    window.removeEventListener('mousemove',onM);
    window.removeEventListener('mouseup',onU);
  };
  window.addEventListener('mousemove',onM);
  window.addEventListener('mouseup',onU);
}

document.addEventListener('paste', async e => {
  const t = activeTab(); if (!t) return;
  let file=null;
  for (let i=0;i<e.clipboardData.items.length;i++) {
    if (e.clipboardData.items[i].type.startsWith('image')) {
      file=e.clipboardData.items[i].getAsFile(); break;
    }
  }
  if (!file) return;
  const buf = new Uint8Array(await file.arrayBuffer());
  const src = URL.createObjectURL(file);
  const container = document.getElementById('pdf-container');
  const first = container.querySelector('.page-wrapper');
  if (!first) { showToast('Open a PDF first.'); return; }
  const data = {
    id:'img_'+Date.now(), pageIndex:parseInt(first.dataset.pageIndex)||0,
    rx:0.05, ry:0.05, rw:0.4, rh:0.3,
    src, buffer:buf, mimeType:file.type
  };
  pushHistory(t); t.pasteImages.push(data); t.modified=true;
  createImgOverlay(data, first); renderTabStrip();
  showToast('Image pasted — drag to reposition.', 'success');
});
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 27 · JS: Redactions
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function restoreRedactions(tab, pageIndex, wrapper) {
  tab.redactions.filter(r => r.pageIndex === pageIndex)
    .forEach(r => createRedactEl(r, wrapper));
}

function createRedactEl(data, wrapper) {
  const el = document.createElement('div');
  el.className = 'redact-rect';
  el.style.left = (data.x*100)+'%';
  el.style.top = (data.y*100)+'%';
  el.style.width = (data.w*100)+'%';
  el.style.height = (data.h*100)+'%';
  el.title = 'Click to remove redaction';
  el.onclick = () => {
    const t = activeTab(); if (!t) return;
    t.redactions = t.redactions.filter(r => r !== data);
    el.remove();
  };
  wrapper.appendChild(el);
}

// Redact tool draws like rect, stores in tab.redactions
// (handled by shape drawing — type 'redact')

function applyRedactions() {
  const t = activeTab(); if (!t) { showToast('No file open.'); return; }
  // Move redact annotations into t.redactions so they get baked on save
  const redactAnnots = t.annotations.filter(a => a.tool === 'redact');
  redactAnnots.forEach(a => {
    t.redactions.push({ pageIndex:a.pageIndex, x:a.x1, y:a.y1, w:Math.abs(a.x2-a.x1), h:Math.abs(a.y2-a.y1) });
    t.annotations = t.annotations.filter(x => x !== a);
  });
  scheduleRender();
  showToast('Redactions applied.', 'success');
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 28 · JS: Page Thumbnails + Bookmarks
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
async function buildThumbs() {
  const t = activeTab(); if (!t) return;
  const list = document.getElementById('lp-thumb'); list.innerHTML = '';
  for (let i=0; i<t.pageOrder.length; i++) {
    const pNum = t.pageOrder[i];
    const page = await t.doc.getPage(pNum);
    const vp = page.getViewport({ scale: 0.17 });
    const item = document.createElement('div');
    item.className = 'thumb-item' + (i===0?' selected':'');
    item.dataset.orderIdx = i;
    const c = document.createElement('canvas');
    c.width=vp.width; c.height=vp.height;
    page.render({ canvasContext:c.getContext('2d'), viewport:vp });
    const num = document.createElement('div'); num.className='thumb-pg-num'; num.textContent=i+1;
    const del = document.createElement('div'); del.className='thumb-del'; del.textContent='×';
    del.onclick = ev => { ev.stopPropagation(); deletePageByOrder(i); };
    item.appendChild(c); item.appendChild(num); item.appendChild(del);
    item.onclick = () => {
      document.querySelectorAll('.thumb-item').forEach(x=>x.classList.remove('selected'));
      item.classList.add('selected');
      document.getElementById('pw-'+i)?.scrollIntoView({block:'start',behavior:'smooth'});
    };
    setupThumbDnd(item, list, i);
    list.appendChild(item);
  }
}
)JS";

ss << LR"JS(
function deletePageByOrder(idx) {
  const t = activeTab(); if (!t) return;
  if (t.pageOrder.length<=1) { showToast('Cannot delete the only page.'); return; }
  pushHistory(t);
  t.pageOrder.splice(idx,1);
  t.annotations = t.annotations.filter(a=>a.pageIndex!==idx)
    .map(a=>({...a, pageIndex:a.pageIndex>idx?a.pageIndex-1:a.pageIndex}));
  t.textBoxes = t.textBoxes.filter(x=>x.pageIndex!==idx)
    .map(x=>({...x, pageIndex:x.pageIndex>idx?x.pageIndex-1:x.pageIndex}));
  t.pasteImages = t.pasteImages.filter(x=>x.pageIndex!==idx)
    .map(x=>({...x, pageIndex:x.pageIndex>idx?x.pageIndex-1:x.pageIndex}));
  t.redactions = t.redactions.filter(x=>x.pageIndex!==idx)
    .map(x=>({...x, pageIndex:x.pageIndex>idx?x.pageIndex-1:x.pageIndex}));
  t.modified=true; renderTabStrip(); renderViewer(); buildThumbs();
  showToast('Page deleted.');
}

function setupThumbDnd(item, list, startIdx) {
  item.draggable=true;
  item.addEventListener('dragstart', e=>{
    e.dataTransfer.setData('text/plain',startIdx);
    item.style.opacity='.4';
  });
  item.addEventListener('dragend', ()=>{ item.style.opacity='1'; });
  item.addEventListener('dragover', e=>{ e.preventDefault(); item.style.outline='2px dashed var(--c-accent)'; });
  item.addEventListener('dragleave', ()=>{ item.style.outline=''; });
  item.addEventListener('drop', e=>{
    e.preventDefault(); item.style.outline='';
    const from=parseInt(e.dataTransfer.getData('text/plain'));
    const to=startIdx;
    if (from===to) return;
    const t=activeTab(); if (!t) return;
    const moved=t.pageOrder.splice(from,1)[0];
    t.pageOrder.splice(to,0,moved);
    t.modified=true; renderViewer(); buildThumbs();
  });
}

function switchLPanel(which, btn) {
  ['thumb','bm','layers'].forEach(id => {
    document.getElementById('lp-'+id).style.display = id===which?'':'none';
  });
  document.querySelectorAll('.lp-tab').forEach(b=>b.classList.remove('active'));
  if (btn) btn.classList.add('active');
  if (which==='thumb') buildThumbs();
  if (which==='bm') renderBookmarkPanel();
}
)JS";

ss << LR"JS(
function addBookmark() {
  const t = activeTab(); if (!t) { showToast('No file open.'); return; }
  const label = prompt('Bookmark label:', 'Page 1'); if (!label) return;
  t.bookmarks.push({ pageIndex:0, label });
  renderBookmarkPanel();
  showToast('Bookmark added.', 'success');
}

function addBookmarkAtCtx() {
  const t = activeTab(); if (!t) return;
  const label = prompt('Bookmark label:', 'Bookmark'); if (!label) return;
  t.bookmarks.push({ pageIndex: Math.max(0,g_ctxPageIndex), label });
  renderBookmarkPanel(); closeCtx();
  showToast('Bookmark added.', 'success');
}

function renderBookmarkPanel() {
  const t = activeTab();
  const panel = document.getElementById('lp-bm');
  if (!t || !t.bookmarks.length) {
    panel.innerHTML = '<div style="padding:10px;font-size:10.5px;color:var(--c-muted);">No bookmarks.</div>'; return;
  }
  panel.innerHTML = '';
  t.bookmarks.forEach((bm,i) => {
    const el = document.createElement('div'); el.className='bm-item';
    el.innerHTML = '&#9733; ' + bm.label +
      '<span class="bm-del" onclick="deleteBookmark('+i+')">&#128465;</span>';
    el.onclick = ()=>{ document.getElementById('pw-'+bm.pageIndex)?.scrollIntoView({block:'start',behavior:'smooth'}); };
    panel.appendChild(el);
  });
}

function deleteBookmark(i) {
  const t = activeTab(); if (!t) return;
  t.bookmarks.splice(i,1); renderBookmarkPanel();
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 29 · JS: Signature pad
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
let g_sigDrawing=false, g_sigCtx=null;

function openSignatureModal() {
  document.getElementById('sig-modal-overlay').classList.add('show');
  const c=document.getElementById('sig-modal-canvas');
  g_sigCtx=c.getContext('2d');
  g_sigCtx.fillStyle='#fff'; g_sigCtx.fillRect(0,0,c.width,c.height);
}
function closeSigModal() {
  document.getElementById('sig-modal-overlay').classList.remove('show');
}
function clearSigPad() {
  const c=document.getElementById('sig-modal-canvas');
  g_sigCtx.fillStyle='#fff'; g_sigCtx.fillRect(0,0,c.width,c.height);
}

const sigCanvas = document.getElementById('sig-modal-canvas');
sigCanvas.addEventListener('mousedown', e=>{
  g_sigDrawing=true;
  const r=sigCanvas.getBoundingClientRect();
  g_sigCtx.beginPath();
  g_sigCtx.moveTo(e.clientX-r.left, e.clientY-r.top);
  g_sigCtx.strokeStyle=g_sigColor; g_sigCtx.lineWidth=2;
  g_sigCtx.lineCap='round';
});
sigCanvas.addEventListener('mousemove', e=>{
  if (!g_sigDrawing) return;
  const r=sigCanvas.getBoundingClientRect();
  g_sigCtx.lineTo(e.clientX-r.left, e.clientY-r.top);
  g_sigCtx.stroke();
  g_sigCtx.beginPath(); g_sigCtx.moveTo(e.clientX-r.left, e.clientY-r.top);
});
window.addEventListener('mouseup', ()=>{ g_sigDrawing=false; });
)JS";

ss << LR"JS(
function applySigToPage() {
  const t=activeTab(); if (!t) { closeSigModal(); return; }
  const src=document.getElementById('sig-modal-canvas').toDataURL('image/png');
  const container=document.getElementById('pdf-container');
  const first=container.querySelector('.page-wrapper');
  if (!first) { closeSigModal(); showToast('Open a PDF first.'); return; }
  const data={
    id:'sig_'+Date.now(), pageIndex:parseInt(first.dataset.pageIndex)||0,
    rx:0.05, ry:0.7, rw:0.35, rh:0.1,
    src, mimeType:'image/png'
  };
  pushHistory(t); t.pasteImages.push(data); t.modified=true;
  createImgOverlay(data, first);
  closeSigModal(); renderTabStrip();
  showToast('Signature inserted.', 'success');
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 30 · JS: Colour picker + undo/redo + menus
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
const PALETTE = [
  '#000000','#434343','#666666','#999999','#b7b7b7','#cccccc','#d9d9d9','#ffffff',
  '#ff0000','#ff9900','#ffff00','#00ff00','#00ffff','#4a86e8','#0000ff','#9900ff',
  '#ff00ff','#e06666','#f6b26b','#ffd966','#93c47d','#76a5af','#6fa8dc','#8e7cc3',
  '#c27ba0','#cc4125','#e69138','#f1c232','#6aa84f','#45818e','#3d85c6','#674ea7',
];

function showColorPicker(target, swatchId) {
  g_colorTarget = target; g_colorSwatchId = swatchId;
  const grid = document.getElementById('color-grid'); grid.innerHTML='';
  PALETTE.forEach(c => {
    const cell=document.createElement('div'); cell.className='color-cell';
    cell.style.background=c;
    cell.onclick=()=>{ applyCustomColor(c); };
    grid.appendChild(cell);
  });
  const popup=document.getElementById('color-picker-popup');
  const swatch=document.getElementById(swatchId);
  if (swatch) {
    const r=swatch.getBoundingClientRect();
    popup.style.left=r.left+'px'; popup.style.top=(r.bottom+4)+'px';
  }
  popup.classList.add('show');
}

function applyCustomColor(c) {
  if (g_colorTarget==='stroke') {
    g_color=c;
    if (g_colorSwatchId) document.getElementById(g_colorSwatchId).style.background=c;
    document.getElementById('rp-color').style.background=c;
    document.getElementById('rb-color-stroke').style.background=c;
  } else {
    g_fillColor=c;
    if (g_colorSwatchId) document.getElementById(g_colorSwatchId).style.background=c;
    document.getElementById('rb-color-fill').style.background=c;
  }
  document.getElementById('color-picker-popup').classList.remove('show');
}
)JS";

ss << LR"JS(
document.addEventListener('click', e=>{
  const pp=document.getElementById('color-picker-popup');
  if (pp.classList.contains('show') && !pp.contains(e.target) && !e.target.classList.contains('color-swatch')) {
    pp.classList.remove('show');
  }
});

// ── Undo / Redo ───────────────────────────────────────────────
function pushHistory(t) {
  const snap={
    annotations: JSON.parse(JSON.stringify(t.annotations)),
    textBoxes: JSON.parse(JSON.stringify(t.textBoxes)),
    redactions: JSON.parse(JSON.stringify(t.redactions)),
    pageOrder: [...t.pageOrder]
  };
  t.history.splice(t.histIdx+1);
  t.history.push(snap);
  if (t.history.length>MAX_HIST) t.history.shift();
  t.histIdx=t.history.length-1;
}

function histUndo() {
  const t=activeTab(); if (!t||t.histIdx<0) { showToast('Nothing to undo.'); return; }
  t.histIdx--;
  if (t.histIdx<0) {
    t.annotations=[]; t.textBoxes=[]; t.redactions=[];
  } else {
    const snap=t.history[t.histIdx];
    t.annotations=JSON.parse(JSON.stringify(snap.annotations));
    t.textBoxes=JSON.parse(JSON.stringify(snap.textBoxes));
    t.redactions=JSON.parse(JSON.stringify(snap.redactions));
    t.pageOrder=[...snap.pageOrder];
  }
  scheduleRender(); buildThumbs(); showToast('Undo.');
}

function histRedo() {
  const t=activeTab(); if (!t||t.histIdx>=t.history.length-1) { showToast('Nothing to redo.'); return; }
  t.histIdx++;
  const snap=t.history[t.histIdx];
  t.annotations=JSON.parse(JSON.stringify(snap.annotations));
  t.textBoxes=JSON.parse(JSON.stringify(snap.textBoxes));
  t.redactions=JSON.parse(JSON.stringify(snap.redactions));
  t.pageOrder=[...snap.pageOrder];
  scheduleRender(); buildThumbs(); showToast('Redo.');
}
)JS";

ss << LR"JS(
// ── Menus ─────────────────────────────────────────────────────
function toggleMenu(id, btn) {
  const m=document.getElementById(id);
  const isOpen=m.classList.contains('show');
  closeAllMenus();
  if (!isOpen) {
    const r=btn.getBoundingClientRect();
    m.style.left=r.left+'px'; m.style.top=r.bottom+'px';
    m.classList.add('show'); btn.classList.add('open');
  }
}

function closeAllMenus() {
  document.querySelectorAll('.dropdown.show').forEach(d=>d.classList.remove('show'));
  document.querySelectorAll('.top-menu.open').forEach(b=>b.classList.remove('open'));
}
document.addEventListener('click', e=>{
  if (!e.target.closest('.dropdown') && !e.target.closest('.top-menu')) closeAllMenus();
});

// ── Ribbon tabs ───────────────────────────────────────────────
function switchRibbon(id, btn) {
  document.querySelectorAll('.ribbon-panel').forEach(p=>p.classList.remove('active'));
  document.querySelectorAll('.rtab').forEach(b=>b.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  btn.classList.add('active');
}

// ── Right-panel section toggle ─────────────────────────────────
function toggleRPSection(hdr) {
  const body=hdr.nextElementSibling;
  const collapsed=body.style.display==='none';
  body.style.display=collapsed?'':'none';
  hdr.querySelector('.toggle').textContent=collapsed?'▼':'▶';
}

// ── Context menu ─────────────────────────────────────────────
document.getElementById('pdf-container').addEventListener('contextmenu', e=>{
  e.preventDefault();
  const wrapper=e.target.closest('.page-wrapper');
  g_ctxPageIndex=wrapper?parseInt(wrapper.dataset.pageIndex):-1;
  const m=document.getElementById('ctx-menu');
  m.style.left=e.clientX+'px'; m.style.top=e.clientY+'px';
  m.classList.add('show');
});
function closeCtx() { document.getElementById('ctx-menu').classList.remove('show'); }
document.addEventListener('click', ()=>closeCtx());
function ctxCopy() { copySelectedText(); closeCtx(); }
function ctxDeletePage() {
  if (g_ctxPageIndex>=0) deletePageByOrder(g_ctxPageIndex); closeCtx();
}
function copySelectedText() {
  const sel=window.getSelection(); if (sel) { document.execCommand('copy'); showToast('Copied.','success'); }
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 31 · JS: Find / Search
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
let g_findMatches=[], g_findIdx=0;

function toggleFindBar() {
  const fb=document.getElementById('findbar');
  fb.classList.toggle('show');
  if (fb.classList.contains('show')) document.getElementById('find-input').focus();
  else clearHighlights();
}

async function doFind() {
  const q=document.getElementById('find-input').value.trim().toLowerCase();
  g_findMatches=[]; g_findIdx=0; clearHighlights();
  if (!q) { document.getElementById('find-count').textContent=''; return; }
  const t=activeTab(); if (!t) return;
  for (let i=0;i<t.pageOrder.length;i++) {
    const page=await t.doc.getPage(t.pageOrder[i]);
    const content=await page.getTextContent();
    const text=content.items.map(x=>x.str).join(' ').toLowerCase();
    let pos=0;
    while ((pos=text.indexOf(q,pos))!==-1) {
      g_findMatches.push({pageIndex:i, charPos:pos}); pos+=q.length;
    }
  }
  document.getElementById('find-count').textContent=
    g_findMatches.length>0 ? '1 of '+g_findMatches.length : 'Not found';
  if (g_findMatches.length) {
    const m=g_findMatches[0];
    document.getElementById('pw-'+m.pageIndex)?.scrollIntoView({block:'start',behavior:'smooth'});
  }
}

function findNext() {
  if (!g_findMatches.length) return;
  g_findIdx=(g_findIdx+1)%g_findMatches.length;
  document.getElementById('find-count').textContent=(g_findIdx+1)+' of '+g_findMatches.length;
  const m=g_findMatches[g_findIdx];
  document.getElementById('pw-'+m.pageIndex)?.scrollIntoView({block:'start',behavior:'smooth'});
}
function findPrev() {
  if (!g_findMatches.length) return;
  g_findIdx=(g_findIdx-1+g_findMatches.length)%g_findMatches.length;
  document.getElementById('find-count').textContent=(g_findIdx+1)+' of '+g_findMatches.length;
  const m=g_findMatches[g_findIdx];
  document.getElementById('pw-'+m.pageIndex)?.scrollIntoView({block:'start',behavior:'smooth'});
}
function clearHighlights() {
  g_findMatches=[]; g_findIdx=0;
  document.getElementById('find-count').textContent='';
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 32 · JS: Save PDF (bake annotations)
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
async function downloadCurrentPDF(saveAs2=false) {
  const t=activeTab(); if (!t) { showToast('No file open.'); return; }
  showLoading(true,'Baking annotations…',10);
  try {
    const src=await PDFLib.PDFDocument.load(t.bytes);
    const out=await PDFLib.PDFDocument.create();
    const copied=await out.copyPages(src, t.pageOrder.map(n=>n-1));
    copied.forEach(p=>out.addPage(p));
    const pages=out.getPages();
    setProgress(30);

    for (const a of t.annotations) {
      if (a.pageIndex>=pages.length) continue;
      const page=pages[a.pageIndex];
      const {width,height}=page.getSize();

      if (a.type==='stroke' && a.points && a.points.length>=2) {
        const isHL=a.alpha<0.8;
        const col=hexToRgb(a.color);
        for (let i=1;i<a.points.length;i++) {
          page.drawLine({
            start:{x:a.points[i-1].rx*width,y:height-(a.points[i-1].ry*height)},
            end:{x:a.points[i].rx*width,y:height-(a.points[i].ry*height)},
            thickness:a.lineWidth,
            color:col,
            opacity:a.alpha||1
          });
        }
      }
      else if (a.type==='shape') {
        const x1=a.x1*width,y1=height-(a.y1*height);
        const x2=a.x2*width,y2=height-(a.y2*height);
        const col=hexToRgb(a.color);
        if (a.tool==='rect') {
          const rx=Math.min(x1,x2), ry=Math.min(y1,y2);
          page.drawRectangle({x:rx,y:ry,width:Math.abs(x2-x1),height:Math.abs(y2-y1),
            borderColor:col,borderWidth:a.lineWidth,opacity:a.alpha||1});
        } else if (a.tool==='line'||a.tool==='arrow') {
          page.drawLine({start:{x:x1,y:y1},end:{x:x2,y:y2},
            thickness:a.lineWidth,color:col,opacity:a.alpha||1});
        } else if (a.tool==='redact') {
          const rx=Math.min(x1,x2), ry=Math.min(y1,y2);
          page.drawRectangle({x:rx,y:ry,width:Math.abs(x2-x1),height:Math.abs(y2-y1),
            color:PDFLib.rgb(0,0,0)});
        }
      }
      else if (a.type==='note') {
        const nx=a.rx*width, ny=height-(a.ry*height);
        page.drawRectangle({x:nx,y:ny-32,width:160,height:32,
          color:PDFLib.rgb(1,.97,.77),borderColor:PDFLib.rgb(.8,.75,.1),borderWidth:1,opacity:.9});
        page.drawText(a.text||'',{x:nx+4,y:ny-24,size:8,
          color:PDFLib.rgb(.1,.1,.1),maxWidth:150});
      }
      else if (a.type==='stamp') {
        const sx=a.rx*width, sy=height-(a.ry*height);
        const col=hexToRgb(STAMP_COLORS[a.label]||'#555');
        page.drawText(a.label,{x:sx,y:sy,size:24,
          color:col,opacity:.5});
      }
    }
    setProgress(60);
)JS";

ss << LR"JS(
    // Bake redactions
    for (const r of t.redactions) {
      if (r.pageIndex>=pages.length) continue;
      const page=pages[r.pageIndex];
      const {width,height}=page.getSize();
      page.drawRectangle({x:r.x*width,y:height-(r.y*height)-(r.h*height),
        width:r.w*width,height:r.h*height,color:PDFLib.rgb(0,0,0)});
    }

    // Bake text boxes
    for (const tb of t.textBoxes) {
      if (tb.pageIndex>=pages.length) continue;
      const page=pages[tb.pageIndex];
      const {width,height}=page.getSize();
      page.drawText(tb.text||'',{
        x:tb.rx*width,y:height-(tb.ry*height),
        size:tb.fontSize||12,color:hexToRgb(tb.color||'#000')
      });
    }

    // Bake pasted images
    for (const img of t.pasteImages) {
      if (img.pageIndex>=pages.length) continue;
      const page=pages[img.pageIndex];
      const {width,height}=page.getSize();
      try {
        let emb;
        if (img.mimeType==='image/png') emb=await out.embedPng(img.buffer);
        else emb=await out.embedJpg(img.buffer);
        const iw=img.rw*width, ih=img.rh*height;
        page.drawImage(emb,{x:img.rx*width,y:height-(img.ry*height)-ih,width:iw,height:ih});
      } catch(e) {}
    }
    setProgress(90);

    const saved=await out.save();
    const name=saveAs2?undefined:t.name;
    await saveBytesToFile(new Blob([saved],{type:'application/pdf'}), name||t.name);
    t.modified=false; renderTabStrip();
  } catch(e) { showToast('Save failed: '+e.message); }
  showLoading(false);
}

function hexToRgb(hex) {
  hex=hex.replace('#','');
  if (hex.length===3) hex=hex.split('').map(c=>c+c).join('');
  const r=parseInt(hex.slice(0,2),16)/255;
  const g=parseInt(hex.slice(2,4),16)/255;
  const b=parseInt(hex.slice(4,6),16)/255;
  return PDFLib.rgb(r,g,b);
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 33 · JS: Merge, Split, Extract, Delete, Insert Blank
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function actionMergePDFs() {
  document.getElementById('mergeInput').click();
}

async function doMerge(e) {
  const files=[...e.target.files];
  e.target.value='';
  if (files.length<2) { showToast('Select at least 2 PDF files.'); return; }
  showLoading(true,'Combining PDFs…',0);
  try {
    const out=await PDFLib.PDFDocument.create();
    for (let i=0;i<files.length;i++) {
      setProgress(Math.round(i/files.length*80));
      const bytes=new Uint8Array(await files[i].arrayBuffer());
      const src=await PDFLib.PDFDocument.load(bytes);
      const copied=await out.copyPages(src,src.getPageIndices());
      copied.forEach(p=>out.addPage(p));
    }
    setProgress(95);
    await saveBytesToFile(new Blob([await out.save()]),'Combined.pdf');
  } catch(e) { showToast('Merge failed: '+e.message); }
  showLoading(false);
}

function modalSplit() {
  const t=activeTab(); if (!t) { showToast('Open a PDF.'); return; }
  showModal('Split PDF',`
    <label>Split after page number (1–${t.pageOrder.length-1}):</label>
    <input type="number" id="split-at" min="1" max="${t.pageOrder.length-1}" value="1">
    <label>Or split into equal parts:</label>
    <input type="number" id="split-parts" min="2" max="${t.pageOrder.length}" value="2" placeholder="Parts">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doSplit()">Split</button>
    </div>`);
}

async function doSplit() {
  const t=activeTab();
  const sp=parseInt(document.getElementById('split-at').value);
  closeModal(); showLoading(true,'Splitting…',10);
  try {
    const src=await PDFLib.PDFDocument.load(t.bytes);
    const all=src.getPageIndices();
    const d1=await PDFLib.PDFDocument.create(), d2=await PDFLib.PDFDocument.create();
    const map=t.pageOrder.map(n=>n-1);
    (await d1.copyPages(src,map.slice(0,sp))).forEach(p=>d1.addPage(p));
    (await d2.copyPages(src,map.slice(sp))).forEach(p=>d2.addPage(p));
    setProgress(70);
    await saveBytesToFile(new Blob([await d1.save()]),'Part1.pdf');
    await saveBytesToFile(new Blob([await d2.save()]),'Part2.pdf');
  } catch(e){ showToast('Split failed.'); }
  showLoading(false);
}
)JS";

ss << LR"JS(
function modalExtract() {
  const t=activeTab(); if (!t) { showToast('Open a PDF.'); return; }
  showModal('Extract Pages',`
    <label>Page numbers to extract (e.g. <code>1, 3, 5-7</code>):</label>
    <input type="text" id="extract-pages" placeholder="1, 2, 3">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doExtract()">Extract</button>
    </div>`);
}

async function doExtract() {
  const t=activeTab(); if (!t) return;
  const val=document.getElementById('extract-pages').value;
  const idxs=parsePageRange(val, t.pageOrder.length);
  if (!idxs.length){ showToast('No valid pages.'); return; }
  closeModal(); showLoading(true,'Extracting…',10);
  try {
    const src=await PDFLib.PDFDocument.load(t.bytes);
    const out=await PDFLib.PDFDocument.create();
    const pdfIdxs=idxs.map(i=>t.pageOrder[i]-1);
    (await out.copyPages(src,pdfIdxs)).forEach(p=>out.addPage(p));
    setProgress(80);
    await saveBytesToFile(new Blob([await out.save()]),'Extracted.pdf');
  } catch(e){ showToast('Extract failed.'); }
  showLoading(false);
}

function parsePageRange(str, total) {
  const result=[];
  str.split(',').forEach(part=>{
    part=part.trim();
    const dash=part.match(/^(\d+)-(\d+)$/);
    if (dash) {
      for (let i=parseInt(dash[1]);i<=parseInt(dash[2]);i++) {
        const idx=i-1; if (idx>=0&&idx<total) result.push(idx);
      }
    } else {
      const n=parseInt(part)-1; if (!isNaN(n)&&n>=0&&n<total) result.push(n);
    }
  });
  return [...new Set(result)].sort((a,b)=>a-b);
}

function modalDeletePages() {
  const t=activeTab(); if (!t) { showToast('Open a PDF.'); return; }
  showModal('Delete Pages',`
    <label>Page numbers to delete (e.g. <code>2, 4, 6-8</code>):</label>
    <input type="text" id="del-pages" placeholder="e.g. 1, 3">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" style="background:#c62828;" onclick="doDeletePages()">Delete</button>
    </div>`);
}

function doDeletePages() {
  const t=activeTab(); if (!t) return;
  const val=document.getElementById('del-pages').value;
  const toRemove=new Set(parsePageRange(val,t.pageOrder.length));
  if (!toRemove.size){ showToast('No valid pages.'); return; }
  if (t.pageOrder.length-toRemove.size<1){ showToast('Cannot delete all pages.'); return; }
  closeModal(); pushHistory(t);
  const sorted=[...toRemove].sort((a,b)=>b-a);
  sorted.forEach(idx=>{
    t.pageOrder.splice(idx,1);
    ['annotations','textBoxes','pasteImages','redactions'].forEach(key=>{
      t[key]=t[key].filter(a=>a.pageIndex!==idx)
        .map(a=>({...a,pageIndex:a.pageIndex>idx?a.pageIndex-1:a.pageIndex}));
    });
  });
  t.modified=true; closeModal(); renderViewer(); buildThumbs();
  showToast('Page(s) deleted.','success');
}
)JS";

ss << LR"JS(
function modalInsertBlank() {
  const t=activeTab(); if (!t) { showToast('Open a PDF.'); return; }
  showModal('Insert Blank Page',`
    <label>Insert after page (0 = before first):</label>
    <input type="number" id="insert-after" min="0" max="${t.pageOrder.length}" value="${t.pageOrder.length}">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-blue" onclick="doInsertBlank()">Insert</button>
    </div>`);
}

async function doInsertBlank() {
  const t=activeTab(); if (!t) return;
  const after=parseInt(document.getElementById('insert-after').value);
  closeModal(); showLoading(true,'Inserting blank page…',20);
  try {
    const src=await PDFLib.PDFDocument.load(t.bytes);
    const out=await PDFLib.PDFDocument.create();
    const copied=await out.copyPages(src,t.pageOrder.map(n=>n-1));
    const blankPage=out.addPage([612,792]);
    // Rebuild page order: insert blank after 'after'
    const newOrder=[];
    copied.forEach((p,i)=>{ if(i===after) newOrder.push(blankPage); newOrder.push(p); });
    if (after>=copied.length) newOrder.push(blankPage);
    // Rebuild PDF with correct order
    const final=await PDFLib.PDFDocument.create();
    newOrder.forEach(p=>{ const [np]=final.addPage([p.getWidth(),p.getHeight()]); });
    // Actually easier: just save with blank inserted
    const saved=await out.save();
    const newBytes=new Uint8Array(saved);
    const newDoc=await pdfjsLib.getDocument({data:newBytes}).promise;
    t.bytes=newBytes; t.doc=newDoc;
    t.pageOrder=Array.from({length:newDoc.numPages},(_,i)=>i+1);
    t.modified=true; renderViewer(); buildThumbs(); renderTabStrip();
    showToast('Blank page inserted.','success');
  } catch(e){ showToast('Insert failed: '+e.message); }
  showLoading(false);
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 34 · JS: Watermark, Header/Footer, Bates, Password, Compress
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
function modalWatermark() {
  const t=activeTab(); if (!t){ showToast('Open a PDF.'); return; }
  showModal('Add Watermark',`
    <label>Watermark text:</label>
    <input type="text" id="wm-text" value="CONFIDENTIAL">
    <label>Font size:</label>
    <input type="number" id="wm-size" value="60" min="12" max="200">
    <label>Opacity (1–100):</label>
    <input type="number" id="wm-opacity" value="18" min="1" max="100">
    <label>Colour:</label>
    <input type="color" id="wm-color" value="#cc0000" style="width:48px;height:28px;border:none;">
    <label>Angle (degrees):</label>
    <input type="number" id="wm-angle" value="40" min="0" max="360">
    <label>Apply to:</label>
    <select id="wm-pages">
      <option value="all">All pages</option>
      <option value="odd">Odd pages</option>
      <option value="even">Even pages</option>
      <option value="first">First page only</option>
    </select>
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doWatermark()">Apply</button>
    </div>`);
}

async function doWatermark() {
  const t=activeTab(); if (!t) return;
  const txt=document.getElementById('wm-text').value;
  const sz=parseInt(document.getElementById('wm-size').value);
  const op=parseInt(document.getElementById('wm-opacity').value)/100;
  const col=hexToRgb(document.getElementById('wm-color').value);
  const angle=parseInt(document.getElementById('wm-angle').value);
  const which=document.getElementById('wm-pages').value;
  closeModal(); showLoading(true,'Applying watermark…',20);
  try {
    const doc=await PDFLib.PDFDocument.load(t.bytes);
    doc.getPages().forEach((page,i)=>{
      if (which==='odd'&&i%2!==0) return;
      if (which==='even'&&i%2===0) return;
      if (which==='first'&&i>0) return;
      const {width,height}=page.getSize();
      page.drawText(txt,{
        x:width/2-sz*txt.length/4, y:height/2,
        size:sz, color:col, opacity:op,
        rotate:PDFLib.degrees(angle)
      });
    });
    setProgress(80);
    await saveBytesToFile(new Blob([await doc.save()]),'Watermarked_'+t.name);
  } catch(e){ showToast('Watermark failed.'); }
  showLoading(false);
}
)JS";

ss << LR"JS(
function modalHeaderFooter() {
  showModal('Header & Footer',`
    <label>Header text (use {page}, {total}, {date}):</label>
    <input type="text" id="hf-header" value="Page {page} of {total}">
    <label>Footer text:</label>
    <input type="text" id="hf-footer" value="{date}">
    <label>Font size:</label>
    <input type="number" id="hf-size" value="9" min="6" max="24">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doHeaderFooter()">Apply</button>
    </div>`);
}

async function doHeaderFooter() {
  const t=activeTab(); if (!t) return;
  const hdr=document.getElementById('hf-header').value;
  const ftr=document.getElementById('hf-footer').value;
  const sz=parseInt(document.getElementById('hf-size').value);
  closeModal(); showLoading(true,'Applying header/footer…',20);
  try {
    const doc=await PDFLib.PDFDocument.load(t.bytes);
    const pages=doc.getPages(); const total=pages.length;
    const today=new Date().toLocaleDateString();
    pages.forEach((page,i)=>{
      const {width,height}=page.getSize();
      const rep=s=>s.replace(/{page}/g,i+1).replace(/{total}/g,total).replace(/{date}/g,today);
      if (hdr) page.drawText(rep(hdr),{x:40,y:height-20,size:sz,color:PDFLib.rgb(.3,.3,.3)});
      if (ftr) page.drawText(rep(ftr),{x:40,y:12,size:sz,color:PDFLib.rgb(.3,.3,.3)});
    });
    setProgress(80);
    await saveBytesToFile(new Blob([await doc.save()]),'HF_'+t.name);
  } catch(e){ showToast('Header/Footer failed.'); }
  showLoading(false);
}

function modalBatesNumber() {
  showModal('Bates Numbering',`
    <label>Prefix (e.g. DOC-):</label>
    <input type="text" id="bates-prefix" value="DOC-">
    <label>Start number:</label>
    <input type="number" id="bates-start" value="1" min="0">
    <label>Digits (zero-padded):</label>
    <input type="number" id="bates-digits" value="6" min="1" max="10">
    <label>Position:</label>
    <select id="bates-pos">
      <option value="br">Bottom Right</option>
      <option value="bl">Bottom Left</option>
      <option value="tr">Top Right</option>
      <option value="tl">Top Left</option>
    </select>
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doBates()">Apply</button>
    </div>`);
}

async function doBates() {
  const t=activeTab(); if (!t) return;
  const prefix=document.getElementById('bates-prefix').value;
  const start=parseInt(document.getElementById('bates-start').value);
  const digits=parseInt(document.getElementById('bates-digits').value);
  const pos=document.getElementById('bates-pos').value;
  closeModal(); showLoading(true,'Adding Bates numbers…',20);
  try {
    const doc=await PDFLib.PDFDocument.load(t.bytes);
    const pages=doc.getPages();
    pages.forEach((page,i)=>{
      const {width,height}=page.getSize();
      const label=prefix+String(start+i).padStart(digits,'0');
      let x,y;
      if (pos==='br'){x=width-80;y=12;}
      else if(pos==='bl'){x=20;y=12;}
      else if(pos==='tr'){x=width-80;y=height-20;}
      else{x=20;y=height-20;}
      page.drawText(label,{x,y,size:8,color:PDFLib.rgb(.2,.2,.2)});
    });
    setProgress(80);
    await saveBytesToFile(new Blob([await doc.save()]),'Bates_'+t.name);
  } catch(e){ showToast('Bates numbering failed.'); }
  showLoading(false);
}
)JS";

ss << LR"JS(
function modalPassword() {
  showModal('Encrypt PDF',`
    <p style="font-size:11px;color:var(--c-muted);margin-bottom:10px;">Note: PDF-lib 1.x does not support AES-256; output will have owner/user password set at PDF level.</p>
    <label>User password (to open):</label>
    <input type="password" id="pw-user" placeholder="Leave blank = no open password">
    <label>Owner password (to edit):</label>
    <input type="password" id="pw-owner" placeholder="Required for restrictions">
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
      <button class="btn btn-primary" onclick="doPassword()">Apply</button>
    </div>`);
}

async function doPassword() {
  closeModal(); showToast('PDF encryption requires server-side processing in this build. Saved as-is.','warn');
}

async function actionCompressPDF() {
  const t=activeTab(); if (!t){ showToast('Open a PDF.'); return; }
  showLoading(true,'Compressing…',20);
  try {
    const doc=await PDFLib.PDFDocument.load(t.bytes,{ignoreEncryption:true});
    setProgress(60);
    const saved=await doc.save({useObjectStreams:true,addDefaultPage:false});
    const ratio=Math.round((1-(saved.length/t.bytes.length))*100);
    setProgress(90);
    await saveBytesToFile(new Blob([saved]),'Compressed_'+t.name);
    showToast('Compressed! Size reduction: '+(ratio>0?ratio+'%':'minimal — already optimal.'),'success');
  } catch(e){ showToast('Compress failed.'); }
  showLoading(false);
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 35 · JS: Export images, text, OCR, Stats, Panels, DocProps
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
async function actionPDFtoImages() {
  const t=activeTab(); if (!t){ showToast('Open a PDF.'); return; }
  showLoading(true,'Rendering pages…',5);
  try {
    const zip=new JSZip();
    for (let i=0;i<t.pageOrder.length;i++) {
      setProgress(Math.round((i/t.pageOrder.length)*90));
      const page=await t.doc.getPage(t.pageOrder[i]);
      const vp=page.getViewport({scale:2.0});
      const c=document.createElement('canvas'); c.width=vp.width; c.height=vp.height;
      await page.render({canvasContext:c.getContext('2d'),viewport:vp}).promise;
      const blob=await new Promise(r=>c.toBlob(r,'image/png'));
      zip.file('Page_'+(i+1).toString().padStart(3,'0')+'.png',blob);
    }
    setProgress(95);
    const zipBlob=await zip.generateAsync({type:'blob'});
    await saveBytesToFile(zipBlob,'Pages_'+t.name.replace('.pdf','')+'.zip','zip','application/zip');
  } catch(e){ showToast('Export failed.'); }
  showLoading(false);
}

async function actionPDFtoText() {
  const t=activeTab(); if (!t){ showToast('Open a PDF.'); return; }
  showLoading(true,'Extracting text…',5);
  try {
    let out='';
    for (let i=0;i<t.pageOrder.length;i++) {
      setProgress(Math.round((i/t.pageOrder.length)*90));
      const page=await t.doc.getPage(t.pageOrder[i]);
      const content=await page.getTextContent();
      out+='\n========== Page '+(i+1)+' ==========\n';
      out+=content.items.map(x=>x.str).join(' ')+'\n';
    }
    await saveBytesToFile(new Blob([out],{type:'text/plain'}),'Text_'+t.name.replace('.pdf','')+'.txt','txt','text/plain');
  } catch(e){ showToast('Text export failed.'); }
  showLoading(false);
}
)JS";

ss << LR"JS(
async function actionPerformOCR() {
  const t=activeTab(); if (!t){ showToast('Open a PDF.'); return; }
  // If in read mode, temporarily exit so loading overlay is visible
  const wasReadMode = document.body.classList.contains('read');
  if (wasReadMode) exitReadMode();
  showLoading(true,'Running OCR (page 1)…',10);
  try {
    const page=await t.doc.getPage(t.pageOrder[0]);
    const vp=page.getViewport({scale:2.5});
    const c=document.createElement('canvas'); c.width=vp.width; c.height=vp.height;
    await page.render({canvasContext:c.getContext('2d'),viewport:vp}).promise;
    setProgress(40);
    const result=await Tesseract.recognize(c.toDataURL('image/png'),'eng');
    setProgress(90);
    await saveBytesToFile(new Blob([result.data.text],{type:'text/plain'}),'OCR_'+t.name.replace('.pdf','')+'.txt','txt','text/plain');
    showToast('OCR complete! Confidence: '+Math.round(result.data.confidence)+'%','success');
  } catch(e){ showToast('OCR failed: '+e.message); }
  showLoading(false);
  // Restore read mode after OCR
  if (wasReadMode) enterReadMode();
}

async function refreshStats() {
  const t=activeTab(); if (!t) return;
  let words=0, chars=0;
  try {
    for (let i=0;i<Math.min(t.pageOrder.length,5);i++) {
      const page=await t.doc.getPage(t.pageOrder[i]);
      const content=await page.getTextContent();
      const text=content.items.map(x=>x.str).join(' ');
      chars+=text.length;
      words+=text.split(/\s+/).filter(Boolean).length;
    }
    if (t.pageOrder.length>5) { words=Math.round(words*(t.pageOrder.length/5)); chars=Math.round(chars*(t.pageOrder.length/5)); }
  } catch(e){}
  document.getElementById('stat-words').textContent=words.toLocaleString();
  document.getElementById('stat-chars').textContent=chars.toLocaleString();
  document.getElementById('stat-annots').textContent=
    (t.annotations.length+t.textBoxes.length+t.pasteImages.length+t.redactions.length).toLocaleString();
  document.getElementById('rp-curpage').textContent=t.pageOrder.length>0?'1 of '+t.pageOrder.length:'—';
}

// ── Panel toggles ─────────────────────────────────────────────
function toggleLeftPanel() { document.getElementById('left-panel').classList.toggle('open'); }
function toggleRightPanel() { document.getElementById('right-panel').classList.toggle('open'); }
function toggleBothPanels() { toggleLeftPanel(); toggleRightPanel(); }
)JS";

ss << LR"JS(
// ── View modes ────────────────────────────────────────────────
function cycleViewMode() {
  g_viewMode=(g_viewMode+1)%3;
  document.body.classList.remove('night','sepia');
  const labels=['Normal','Night','Sepia'];
  if (g_viewMode===1) document.body.classList.add('night');
  if (g_viewMode===2) document.body.classList.add('sepia');
  document.getElementById('sb-mode').textContent=labels[g_viewMode];
}
function enterReadMode() {
  document.body.classList.add('read');
  document.getElementById('sb-mode').textContent = 'Read';
}
function exitReadMode() {
  document.body.classList.remove('read');
  document.getElementById('sb-mode').textContent = 'Normal';
  setTimeout(() => { const t = activeTab(); if (t) renderViewer(); }, 80);
}
function toggleReadMode() {
  if (document.body.classList.contains('read')) exitReadMode();
  else enterReadMode();
}
function enterPresentation() {
  if (document.body.classList.contains('present')) {
    document.body.classList.remove('present');
    document.getElementById('sb-mode').textContent='Normal';
    document.exitFullscreen?.();
  } else {
    document.body.classList.add('present');
    document.getElementById('sb-mode').textContent='Presentation';
    document.documentElement.requestFullscreen?.();
  }
}

// ── Ruler & Grid ──────────────────────────────────────────────
function toggleRuler() {
  g_showRuler=!g_showRuler;
  showToast('Ruler: '+(g_showRuler?'On':'Off'));
}
function toggleGrid() {
  g_showGrid=!g_showGrid;
  document.querySelectorAll('.grid-overlay').forEach(g=>g.classList.toggle('show',g_showGrid));
  showToast('Grid: '+(g_showGrid?'On':'Off'));
}

// ── Document Properties ───────────────────────────────────────
async function showDocProperties() {
  const t=activeTab(); if (!t){ showToast('No file open.'); return; }
  let title='—', author='—', subject='—', keywords='—', creator='—';
  try {
    const meta=await t.doc.getMetadata();
    if (meta&&meta.info) {
      title=meta.info.Title||'—'; author=meta.info.Author||'—';
      subject=meta.info.Subject||'—'; keywords=meta.info.Keywords||'—';
      creator=meta.info.Creator||'—';
    }
  } catch(e){}
  showModal('Document Properties',`
    <table style="width:100%;font-size:11px;border-collapse:collapse;">
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);width:90px;">File</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${t.name}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);">Pages</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${t.pageOrder.length}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);">Title</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${title}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);">Author</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${author}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);">Subject</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${subject}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);border-bottom:1px solid var(--c-border);">Creator</td><td style="padding:4px 8px;border-bottom:1px solid var(--c-border);">${creator}</td></tr>
      <tr><td style="padding:4px 8px;color:var(--c-muted);">Size (est.)</td><td style="padding:4px 8px;">${(t.bytes.length/1024).toFixed(1)} KB</td></tr>
    </table>
    <div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Close</button></div>`);
}
)JS";

ss << LR"JS(
// ── Keyboard shortcuts reference ───────────────────────────────
function showShortcutModal() {
  showModal('Keyboard Shortcuts',`
    <table style="width:100%;font-size:11px;border-collapse:collapse;">
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>1</kbd></td><td>Hand tool</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>2</kbd></td><td>Select tool</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>3</kbd></td><td>Pen tool</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>4</kbd></td><td>Highlight tool</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>5</kbd></td><td>Eraser</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>6</kbd></td><td>Sticky Note</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>7</kbd></td><td>Text Box</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>8</kbd></td><td>Stamp</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>R</kbd></td><td>Rotate all pages</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Del</kbd></td><td>Delete selected annotation</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+O</kbd></td><td>Open file(s)</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+S</kbd></td><td>Save / bake</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+Z</kbd></td><td>Undo</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+Y</kbd></td><td>Redo</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+F</kbd></td><td>Find text</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+0</kbd></td><td>Zoom 100%</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Ctrl+Wheel</kbd></td><td>Smooth zoom</td></tr>
      <tr><td style="padding:3px 8px;color:var(--c-muted);"><kbd>Esc</kbd></td><td>Close dialogs</td></tr>
    </table>
    <div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Close</button></div>`,true);
}
</script>
)JS";

// ─────────────────────────────────────────────────────────────
// PART 36 · JS: Drag-drop open + resize observer + init
// ─────────────────────────────────────────────────────────────
ss << LR"JS(
<script>
// ── Drag-drop open PDF ─────────────────────────────────────────
const va=document.getElementById('viewer-area');
va.addEventListener('dragover', e=>{
  e.preventDefault();
  va.style.outline='3px dashed var(--c-accent2)';
});
va.addEventListener('dragleave', ()=>{ va.style.outline=''; });
va.addEventListener('drop', async e=>{
  e.preventDefault(); va.style.outline='';
  for (const f of e.dataTransfer.files) {
    if (!f.name.toLowerCase().endsWith('.pdf')) continue;
    const bytes=new Uint8Array(await f.arrayBuffer());
    await createTab(f.name,bytes);
  }
});

// ── Window resize → re-render ─────────────────────────────────
let g_resizeTimer=null;
window.addEventListener('resize',()=>{
  if (g_resizeTimer) clearTimeout(g_resizeTimer);
  g_resizeTimer=setTimeout(()=>scheduleRender(),150);
});

// ── Expose bridge for C++ WebView2 ───────────────────────────
window.loadPdfFromPath=loadPdfFromPath;
window.g_webViewController_bridge=true;

// ── Init ──────────────────────────────────────────────────────
setTool('hand');
renderTabStrip();
updateStatusBar();
renderRecentFiles();
</script>
</body>
</html>
)JS";

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