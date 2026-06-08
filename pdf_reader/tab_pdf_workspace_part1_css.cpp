// ============================================================
//  tab_pdf_workspace.cpp
//  Professional PDF Workspace Architecture
// ============================================================

#define _CRT_SECURE_NO_WARNINGS
#include "tab_pdf_workspace.h"
#include "../browser/mini_browser.h"
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
  transform-origin:top center;
  will-change:transform;
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
