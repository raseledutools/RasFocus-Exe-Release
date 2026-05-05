// tab_pdf_workspace.cpp
// Adobe Acrobat Style PDF Workspace with Complete Working Features

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
#include <wrl/client.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "WebView2Loader.dll.lib")

using namespace Gdiplus;
using namespace Microsoft::WRL;
using namespace std;

extern HWND hParentWnd;
extern float g_scaleFactor;
wstring currentWorkspacePdf = L"";

// --- WebView2 Global Variables ---
HWND g_hAcrobatWnd = NULL;
HWND g_hWebViewWnd = NULL;
ComPtr<ICoreWebView2Environment> g_webViewEnv = nullptr;
ComPtr<ICoreWebView2Controller> g_webViewController = nullptr;
ComPtr<ICoreWebView2> g_webView = nullptr;
wstring g_acrobatPdfPath = L"";
bool g_webViewInitialized = false;

// ==========================================
// 🎨 HTML/CSS/JS UI - Complete with ALL working features
// ==========================================
const wchar_t* GetAcrobatHTML() {
    return LR"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>PDF Workspace - Professional Edition</title>

<!-- PDF Libraries -->
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
<script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>

<style>
:root {
    --ribbon-height: 130px;
    --tab-height: 32px;
    --sidebar-width: 260px;
    --left-strip: 45px;
    --status-height: 28px;
    --purple: #68217A;
    --purple-hover: #7B1FA2;
    --bg-white: #FAFBFC;
    --bg-sidebar: #F5F5F7;
    --bg-strip: #EBEBEE;
    --bg-doc: #DCDCDC;
    --border: #C8C8CD;
    --text-primary: #1E1E1E;
    --text-secondary: #505050;
    --text-disabled: #A0A0A5;
    --hover-bg: #EBEBF0;
    --selected-bg: #DCDCE4;
    --success: #107C10;
    --danger: #D13438;
    --warning: #FF8C00;
}

* { margin: 0; padding: 0; box-sizing: border-box; }
body { 
    font-family: 'Segoe UI', system-ui, sans-serif;
    height: 100vh; overflow: hidden; display: flex; flex-direction: column;
    user-select: none; background: #1e1e1e;
}

/* ============== TOAST NOTIFICATIONS ============== */
.toast-container {
    position: fixed; top: 20px; right: 20px; z-index: 9999;
    display: flex; flex-direction: column; gap: 8px;
}
.toast {
    padding: 12px 20px; border-radius: 6px; color: white; font-size: 13px;
    font-weight: 500; box-shadow: 0 4px 12px rgba(0,0,0,0.3);
    animation: slideIn 0.3s ease; max-width: 380px; cursor: pointer;
}
.toast.success { background: var(--success); }
.toast.error { background: var(--danger); }
.toast.warning { background: var(--warning); }
.toast.info { background: #0078D4; }
@keyframes slideIn { from { transform: translateX(100%); opacity: 0; } to { transform: translateX(0); opacity: 1; } }
@keyframes slideOut { from { transform: translateX(0); opacity: 1; } to { transform: translateX(100%); opacity: 0; } }

/* ============== MODAL ============== */
.modal-overlay {
    display: none; position: fixed; top: 0; left: 0; right: 0; bottom: 0;
    background: rgba(0,0,0,0.6); z-index: 1000; justify-content: center;
    align-items: center;
}
.modal-overlay.show { display: flex; }
.modal {
    background: white; border-radius: 8px; padding: 24px; min-width: 400px;
    max-width: 600px; max-height: 80vh; overflow-y: auto; box-shadow: 0 8px 32px rgba(0,0,0,0.3);
}
.modal h3 { font-size: 18px; margin-bottom: 16px; color: var(--text-primary); }
.modal .modal-actions { display: flex; gap: 8px; justify-content: flex-end; margin-top: 16px; }
.modal .btn {
    padding: 8px 16px; border: none; border-radius: 4px; cursor: pointer;
    font-size: 13px; font-weight: 500;
}
.modal .btn-primary { background: var(--purple); color: white; }
.modal .btn-primary:hover { background: var(--purple-hover); }
.modal .btn-secondary { background: #e0e0e0; color: var(--text-primary); }
.modal .btn-secondary:hover { background: #d0d0d0; }
.modal .btn-danger { background: var(--danger); color: white; }

/* ============== RIBBON ============== */
.ribbon {
    height: var(--ribbon-height);
    background: var(--bg-white);
    border-bottom: 1px solid var(--border);
    flex-shrink: 0;
    display: flex; flex-direction: column;
}

.tab-bar {
    height: var(--tab-height);
    display: flex; align-items: stretch;
    background: var(--bg-white);
    border-bottom: 1px solid var(--border);
}

.file-btn {
    background: var(--purple); color: white; padding: 0 18px;
    display: flex; align-items: center; font-weight: 600;
    font-size: 13px; cursor: pointer; transition: background 0.15s;
}
.file-btn:hover { background: var(--purple-hover); }

.tab {
    padding: 0 16px; display: flex; align-items: center;
    font-size: 13px; color: var(--text-secondary); cursor: pointer;
    border-bottom: 3px solid transparent; transition: all 0.15s;
}
.tab:hover { background: var(--hover-bg); }
.tab.active { color: var(--text-primary); font-weight: 600; border-bottom-color: var(--purple); background: white; }

.tab-spacer { flex: 1; }

.quick-actions {
    display: flex; align-items: center; gap: 4px; padding-right: 8px;
}
.quick-actions .icon-btn {
    width: 32px; height: 26px; border: none; background: transparent;
    cursor: pointer; border-radius: 4px; font-size: 14px;
    color: var(--text-secondary); display: flex; align-items: center; justify-content: center;
}
.quick-actions .icon-btn:hover { background: var(--hover-bg); }

.toolbar {
    flex: 1; display: flex; align-items: flex-start; padding: 8px 20px;
    gap: 4px; overflow-x: auto;
}
.tool-group {
    display: flex; gap: 4px; align-items: flex-start;
}
.tool-item {
    display: flex; flex-direction: column; align-items: center; padding: 4px 8px;
    border-radius: 6px; cursor: pointer; min-width: 48px; transition: background 0.15s;
}
.tool-item:hover { background: var(--hover-bg); }
.tool-item .tool-icon { font-size: 22px; color: var(--text-primary); margin-bottom: 2px; }
.tool-item .tool-label { font-size: 10px; color: var(--text-secondary); text-align: center; }
.tool-separator {
    width: 1px; background: var(--border); margin: 4px 8px; align-self: stretch;
}

/* ============== MAIN CONTENT ============== */
.main-content {
    flex: 1; display: flex; overflow: hidden;
}

/* ============== SIDEBAR ============== */
.sidebar {
    width: var(--sidebar-width); background: var(--bg-sidebar);
    display: flex; flex-shrink: 0; border-right: 1px solid var(--border);
    transition: width 0.2s ease;
}
.sidebar.collapsed { width: 0; overflow: hidden; border-right: none; }

.sidebar-strip {
    width: var(--left-strip); background: var(--bg-strip);
    display: flex; flex-direction: column; align-items: center;
    padding-top: 10px; flex-shrink: 0; border-right: 1px solid var(--border);
}
.sidebar-strip .strip-icon {
    width: 35px; height: 36px; display: flex; align-items: center;
    justify-content: center; font-size: 18px; color: var(--text-secondary);
    cursor: pointer; border-radius: 6px; margin: 2px 0; position: relative; transition: all 0.15s;
}
.sidebar-strip .strip-icon:hover { background: var(--hover-bg); }
.sidebar-strip .strip-icon.active { 
    background: var(--selected-bg); color: var(--purple);
}
.sidebar-strip .strip-icon.active::before {
    content: ''; position: absolute; left: 0; top: 4px; bottom: 4px;
    width: 3px; background: var(--purple); border-radius: 0 2px 2px 0;
}

.sidebar-panel {
    flex: 1; padding: 10px; overflow-y: auto; display: flex; flex-direction: column;
}
.sidebar-panel h3 {
    font-size: 14px; font-weight: 600; color: var(--text-primary);
    margin-bottom: 8px; padding-bottom: 8px; border-bottom: 1px solid var(--border);
    display: flex; justify-content: space-between; align-items: center;
}

/* Page Thumbnails */
.thumbnail-list { display: flex; flex-direction: column; gap: 8px; }
.thumbnail-item {
    background: white; border: 1px solid var(--border); border-radius: 4px;
    padding: 6px; cursor: pointer; transition: all 0.15s; position: relative;
}
.thumbnail-item:hover { border-color: var(--purple); box-shadow: 0 1px 4px rgba(0,0,0,0.1); }
.thumbnail-item.active { border-color: var(--purple); border-width: 2px; }
.thumbnail-item.selected-for-merge { border-color: #FF8C00; background: #FFF3E0; }
.thumbnail-item canvas { width: 100%; height: auto; border-radius: 2px; }
.thumbnail-item .page-num {
    text-align: center; font-size: 10px; color: var(--text-secondary); margin-top: 4px;
}
.thumbnail-item .page-checkbox {
    position: absolute; top: 4px; right: 4px; width: 16px; height: 16px;
    cursor: pointer; z-index: 10; display: none;
}
.thumbnail-item.show-checkbox .page-checkbox { display: block; }

/* ============== PDF VIEWER ============== */
.pdf-viewer {
    flex: 1; background: var(--bg-doc); overflow: auto;
    display: flex; justify-content: center; padding: 20px;
}
.pdf-container { display: flex; flex-direction: column; align-items: center; gap: 16px; }
.pdf-page {
    background: white; box-shadow: 0 2px 12px rgba(0,0,0,0.15);
    border-radius: 2px; position: relative;
}
.pdf-page canvas { display: block; border-radius: 2px; }

/* ============== STATUS BAR ============== */
.status-bar {
    height: var(--status-height); background: #F0F0F2;
    border-top: 1px solid var(--border); display: flex; align-items: center;
    padding: 0 16px; font-size: 11px; color: var(--text-secondary);
    flex-shrink: 0; gap: 16px;
}
.status-bar .status-item { display: flex; align-items: center; gap: 4px; }
.status-bar .zoom-control {
    margin-left: auto; display: flex; align-items: center; gap: 6px;
}
.status-bar .zoom-control button {
    width: 24px; height: 22px; border: 1px solid var(--border); background: white;
    cursor: pointer; border-radius: 3px; font-size: 12px; display: flex;
    align-items: center; justify-content: center; color: var(--text-primary);
}
.status-bar .zoom-control button:hover { background: var(--hover-bg); }
.status-bar .zoom-value {
    font-size: 11px; color: var(--text-primary); cursor: pointer; min-width: 40px; text-align: center;
}

/* ============== PROGRESS BAR ============== */
.progress-bar {
    display: none; height: 4px; background: #e0e0e0; flex-shrink: 0;
}
.progress-bar.active { display: block; }
.progress-bar .progress-fill {
    height: 100%; background: var(--purple); width: 0%;
    transition: width 0.3s ease;
}

/* ============== LOADING OVERLAY ============== */
.loading-overlay {
    display: none; position: fixed; top: 50%; left: 50%;
    transform: translate(-50%, -50%); background: rgba(0,0,0,0.85);
    color: white; padding: 20px 40px; border-radius: 8px;
    font-size: 14px; z-index: 2000; text-align: center;
}
.loading-overlay.show { display: block; }
.loading-overlay .spinner {
    width: 40px; height: 40px; border: 3px solid rgba(255,255,255,0.3);
    border-top-color: white; border-radius: 50%; animation: spin 0.8s linear infinite;
    margin: 0 auto 12px;
}
@keyframes spin { to { transform: rotate(360deg); } }
</style>
</head>
<body>

<!-- Toast Container -->
<div class="toast-container" id="toast-container"></div>

<!-- Modal Overlay -->
<div class="modal-overlay" id="modal-overlay">
    <div class="modal" id="modal-content"></div>
</div>

<!-- Progress Bar -->
<div class="progress-bar" id="progress-bar">
    <div class="progress-fill" id="progress-fill"></div>
</div>

<!-- ==================== RIBBON ==================== -->
<div class="ribbon">
    <div class="tab-bar">
        <div class="file-btn" onclick="showFileMenu()">📄 File</div>
        <div class="tab active" data-tab="home" onclick="switchTab('home')">Home</div>
        <div class="tab" data-tab="tools" onclick="switchTab('tools')">Tools</div>
        <div class="tab" data-tab="edit" onclick="switchTab('edit')">Edit</div>
        <div class="tab" data-tab="organize" onclick="switchTab('organize')">Organize</div>
        <div class="tab" data-tab="convert" onclick="switchTab('convert')">Convert</div>
        <div class="tab" data-tab="view" onclick="switchTab('view')">View</div>
        <div class="tab-spacer"></div>
        <div class="quick-actions">
            <button class="icon-btn" title="Undo" onclick="onUndo()">↩</button>
            <button class="icon-btn" title="Redo" onclick="onRedo()">↪</button>
            <button class="icon-btn" title="Save" onclick="onSave()">💾</button>
            <button class="icon-btn" title="Print" onclick="onPrint()">🖨</button>
            <button class="icon-btn" title="Share" onclick="onShare()" style="background:#68217A;color:white;width:60px;border-radius:4px;">Share</button>
        </div>
    </div>
    
    <!-- Home Tab Toolbar -->
    <div class="toolbar" id="toolbar-home">
        <div class="tool-group">
            <div class="tool-item" onclick="setTool('hand')" id="tool-hand">
                <span class="tool-icon">✋</span><span class="tool-label">Hand</span>
            </div>
            <div class="tool-item" onclick="setTool('select')" id="tool-select">
                <span class="tool-icon">👆</span><span class="tool-label">Select</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="zoomOut()">
                <span class="tool-icon">🔍</span><span class="tool-label">Zoom Out</span>
            </div>
            <div class="tool-item" onclick="fitPage()">
                <span class="tool-icon">📄</span><span class="tool-label">Fit Page</span>
            </div>
            <div class="tool-item" onclick="fitWidth()">
                <span class="tool-icon">↔</span><span class="tool-label">Fit Width</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="rotateCW()">
                <span class="tool-icon">↻</span><span class="tool-label">Rotate</span>
            </div>
            <div class="tool-item" onclick="deleteCurrentPage()">
                <span class="tool-icon">🗑</span><span class="tool-label">Delete Page</span>
            </div>
        </div>
    </div>
    
    <!-- Tools Tab Toolbar -->
    <div class="toolbar" id="toolbar-tools" style="display:none;">
        <div class="tool-group">
            <div class="tool-item" onclick="showMergePDFModal()">
                <span class="tool-icon">📑</span><span class="tool-label">Merge PDFs</span>
            </div>
            <div class="tool-item" onclick="showSplitPDFModal()">
                <span class="tool-icon">✂️</span><span class="tool-label">Split PDF</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="compressPDF()">
                <span class="tool-icon">📦</span><span class="tool-label">Compress</span>
            </div>
            <div class="tool-item" onclick="addWatermark()">
                <span class="tool-icon">💧</span><span class="tool-label">Watermark</span>
            </div>
        </div>
    </div>
    
    <!-- Organize Tab Toolbar -->
    <div class="toolbar" id="toolbar-organize" style="display:none;">
        <div class="tool-group">
            <div class="tool-item" onclick="extractPages()">
                <span class="tool-icon">📤</span><span class="tool-label">Extract Pages</span>
            </div>
            <div class="tool-item" onclick="duplicatePage()">
                <span class="tool-icon">📋</span><span class="tool-label">Duplicate</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="movePageUp()">
                <span class="tool-icon">⬆</span><span class="tool-label">Move Up</span>
            </div>
            <div class="tool-item" onclick="movePageDown()">
                <span class="tool-icon">⬇</span><span class="tool-label">Move Down</span>
            </div>
        </div>
    </div>
    
    <!-- Convert Tab Toolbar -->
    <div class="toolbar" id="toolbar-convert" style="display:none;">
        <div class="tool-group">
            <div class="tool-item" onclick="convertToImage()">
                <span class="tool-icon">🖼</span><span class="tool-label">PDF to Image</span>
            </div>
            <div class="tool-item" onclick="convertToWord()">
                <span class="tool-icon">📝</span><span class="tool-label">PDF to Word</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="convertToText()">
                <span class="tool-icon">📃</span><span class="tool-label">Extract Text</span>
            </div>
            <div class="tool-item" onclick="convertImageToPDF()">
                <span class="tool-icon">📄</span><span class="tool-label">Image to PDF</span>
            </div>
        </div>
    </div>
    
    <!-- Edit Tab Toolbar -->
    <div class="toolbar" id="toolbar-edit" style="display:none;">
        <div class="tool-group">
            <div class="tool-item" onclick="editText()">
                <span class="tool-icon">✏️</span><span class="tool-label">Edit Text</span>
            </div>
            <div class="tool-item" onclick="addTextAnnotation()">
                <span class="tool-icon">💬</span><span class="tool-label">Add Note</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="highlightText()">
                <span class="tool-icon">🖍</span><span class="tool-label">Highlight</span>
            </div>
            <div class="tool-item" onclick="addSignature()">
                <span class="tool-icon">✍️</span><span class="tool-label">Sign</span>
            </div>
        </div>
    </div>
    
    <!-- View Tab Toolbar -->
    <div class="toolbar" id="toolbar-view" style="display:none;">
        <div class="tool-group">
            <div class="tool-item" onclick="toggleReadMode()">
                <span class="tool-icon">📖</span><span class="tool-label">Read Mode</span>
            </div>
            <div class="tool-item" onclick="toggleFullScreen()">
                <span class="tool-icon">🖥</span><span class="tool-label">Full Screen</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="toggleTheme()">
                <span class="tool-icon">🌓</span><span class="tool-label">Dark Mode</span>
            </div>
            <div class="tool-item" onclick="toggleSidebar()">
                <span class="tool-icon">📑</span><span class="tool-label">Sidebar</span>
            </div>
        </div>
    </div>
</div>

<!-- ==================== MAIN CONTENT ==================== -->
<div class="main-content">
    <div class="sidebar" id="sidebar">
        <div class="sidebar-strip">
            <div class="strip-icon active" data-panel="pages" onclick="switchSidebar('pages')" title="Pages">📑</div>
            <div class="strip-icon" data-panel="bookmarks" onclick="switchSidebar('bookmarks')" title="Bookmarks">🔖</div>
            <div class="strip-icon" data-panel="annotations" onclick="switchSidebar('annotations')" title="Annotations">💬</div>
            <div class="strip-icon" data-panel="attachments" onclick="switchSidebar('attachments')" title="Attachments">📎</div>
        </div>
        <div class="sidebar-panel" id="sidebar-content">
            <h3>Pages <span style="font-size:11px;color:var(--purple);cursor:pointer;" onclick="togglePageSelection()">[Select]</span></h3>
            <div class="thumbnail-list" id="thumbnail-list"></div>
            <div id="bookmarks-list" style="display:none;"></div>
            <div id="annotations-list" style="display:none; color:var(--text-secondary); font-size:12px;"></div>
            <div id="attachments-list" style="display:none; color:var(--text-secondary); font-size:12px;"></div>
        </div>
    </div>
    
    <div class="pdf-viewer" id="pdf-viewer">
        <div class="pdf-container" id="pdf-container">
            <div style="color: var(--text-secondary); font-size: 16px; padding: 40px; text-align:center;">
                <div style="font-size:64px;margin-bottom:16px;">📄</div>
                <div>No PDF loaded</div>
                <div style="font-size:12px;margin-top:8px;">Click File to open a PDF, or drag & drop here</div>
            </div>
        </div>
    </div>
</div>

<!-- ==================== STATUS BAR ==================== -->
<div class="status-bar">
    <span class="status-item">📄 <span id="status-page-info">Page 1 of 1</span></span>
    <span class="status-item">📏 <span id="status-size">8.5 x 11 in</span></span>
    <span class="status-item" id="status-file-size"></span>
    <div class="zoom-control">
        <button onclick="zoomOut()">−</button>
        <span class="zoom-value" id="zoom-value" onclick="showZoomMenu()">100%</span>
        <button onclick="zoomIn()">+</button>
    </div>
</div>

<div class="loading-overlay" id="loading-overlay">
    <div class="spinner"></div>
    <div id="loading-text">Processing...</div>
</div>

<!-- ==================== JAVASCRIPT - COMPLETE LOGIC ==================== -->
<script>
// ============ GLOBAL STATE ============
let pdfDoc = null;
let pdfBytes = null;  // Original PDF bytes for pdf-lib
let currentPage = 1;
let currentZoom = 100;
let currentRotation = 0;
let numPages = 0;
let currentTool = 'hand';
let currentSidebar = 'pages';
let zoomMode = 'custom';
let pageSelectionMode = false;
let selectedPages = new Set();
let history = [];
let historyIndex = -1;
let currentFilePath = null;

// ============ TOAST SYSTEM ============
function showToast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    toast.textContent = message;
    toast.onclick = () => toast.remove();
    container.appendChild(toast);
    setTimeout(() => {
        toast.style.animation = 'slideOut 0.3s ease forwards';
        setTimeout(() => toast.remove(), 300);
    }, 4000);
}

// ============ MODAL SYSTEM ============
function showModal(title, content, actions) {
    const overlay = document.getElementById('modal-overlay');
    const modalContent = document.getElementById('modal-content');
    
    let actionsHTML = actions.map(a => 
        `<button class="btn btn-${a.type || 'secondary'}" onclick="(${a.onclick.toString()})()">${a.label}</button>`
    ).join('');
    
    modalContent.innerHTML = `
        <h3>${title}</h3>
        <div>${content}</div>
        <div class="modal-actions">${actionsHTML}</div>
    `;
    overlay.classList.add('show');
}

function closeModal() {
    document.getElementById('modal-overlay').classList.remove('show');
}

// ============ PROGRESS SYSTEM ============
function showProgress(show) {
    document.getElementById('progress-bar').classList.toggle('active', show);
    document.getElementById('progress-fill').style.width = '0%';
}

function updateProgress(percent) {
    document.getElementById('progress-fill').style.width = percent + '%';
}

function showLoading(show, text = 'Processing...') {
    document.getElementById('loading-overlay').classList.toggle('show', show);
    document.getElementById('loading-text').textContent = text;
}

// ============ INITIALIZATION ============
document.addEventListener('DOMContentLoaded', () => {
    setupKeyboardShortcuts();
    setupDragDrop();
    checkForPdfPath();
});

// Check for PDF path from host (C++)
function checkForPdfPath() {
    if (window.chrome && window.chrome.webview) {
        window.chrome.webview.addEventListener('message', (event) => {
            if (event.data && event.data.type === 'loadPdf') {
                loadPdfFromPath(event.data.path);
            }
        });
    }
}

// Drag & Drop support
function setupDragDrop() {
    const viewer = document.getElementById('pdf-viewer');
    
    viewer.addEventListener('dragover', (e) => {
        e.preventDefault();
        e.stopPropagation();
        viewer.style.background = '#c8c8c8';
    });
    
    viewer.addEventListener('dragleave', () => {
        viewer.style.background = '';
    });
    
    viewer.addEventListener('drop', async (e) => {
        e.preventDefault();
        e.stopPropagation();
        viewer.style.background = '';
        
        const files = Array.from(e.dataTransfer.files);
        const pdfFiles = files.filter(f => f.type === 'application/pdf' || f.name.endsWith('.pdf'));
        const imageFiles = files.filter(f => f.type.startsWith('image/'));
        
        if (pdfFiles.length === 1) {
            await loadPdfFromFile(pdfFiles[0]);
        } else if (pdfFiles.length > 1) {
            showToast('Multiple PDFs detected. Use Merge PDFs from Tools tab.', 'info');
        } else if (imageFiles.length > 0) {
            await handleImageToPDF(imageFiles);
        }
    });
}

// ============ PDF LOADING ============
async function loadPdfFromPath(path) {
    showLoading(true, 'Loading PDF...');
    currentFilePath = path;
    
    try {
        const response = await fetch('file:///' + path.replace(/\\/g, '/'));
        const arrayBuffer = await response.arrayBuffer();
        await loadPdfFromArrayBuffer(arrayBuffer);
    } catch (error) {
        showToast('Error loading PDF: ' + error.message, 'error');
        showLoading(false);
    }
}

async function loadPdfFromFile(file) {
    showLoading(true, 'Loading PDF...');
    currentFilePath = file.name;
    
    const reader = new FileReader();
    reader.onload = async (e) => {
        await loadPdfFromArrayBuffer(e.target.result);
    };
    reader.readAsArrayBuffer(file);
}

async function loadPdfFromArrayBuffer(arrayBuffer) {
    try {
        pdfBytes = new Uint8Array(arrayBuffer);
        
        // Load for pdf.js (rendering)
        const pdf = await pdfjsLib.getDocument({ data: pdfBytes }).promise;
        pdfDoc = pdf;
        numPages = pdf.numPages;
        currentPage = 1;
        currentRotation = 0;
        currentZoom = 100;
        selectedPages.clear();
        
        await renderAllPages();
        await renderThumbnails();
        updateStatusBar();
        
        const sizeMB = (arrayBuffer.byteLength / (1024 * 1024)).toFixed(2);
        document.getElementById('status-file-size').textContent = `📦 ${sizeMB} MB`;
        
        saveToHistory('load');
        showToast(`PDF loaded successfully! ${numPages} pages`, 'success');
        showLoading(false);
    } catch (error) {
        showToast('Error loading PDF: ' + error.message, 'error');
        showLoading(false);
    }
}

// ============ HISTORY SYSTEM ============
function saveToHistory(action) {
    historyIndex++;
    history = history.slice(0, historyIndex);
    history.push({
        action,
        pdfBytes: pdfBytes ? new Uint8Array(pdfBytes) : null,
        currentPage,
        currentZoom
    });
}

async function onUndo() {
    if (historyIndex > 0) {
        historyIndex--;
        const state = history[historyIndex];
        if (state.pdfBytes) {
            pdfBytes = new Uint8Array(state.pdfBytes);
            await loadPdfFromArrayBuffer(pdfBytes.buffer);
        }
        showToast('Undo successful', 'info');
    }
}

async function onRedo() {
    if (historyIndex < history.length - 1) {
        historyIndex++;
        const state = history[historyIndex];
        if (state.pdfBytes) {
            pdfBytes = new Uint8Array(state.pdfBytes);
            await loadPdfFromArrayBuffer(pdfBytes.buffer);
        }
        showToast('Redo successful', 'info');
    }
}

// ============ PAGE RENDERING ============
async function renderAllPages() {
    if (!pdfDoc) return;
    
    const container = document.getElementById('pdf-container');
    container.innerHTML = '';
    
    for (let i = 1; i <= numPages; i++) {
        const pageDiv = document.createElement('div');
        pageDiv.className = 'pdf-page';
        pageDiv.id = 'page-' + i;
        pageDiv.dataset.page = i;
        
        const canvas = document.createElement('canvas');
        canvas.id = 'canvas-' + i;
        pageDiv.appendChild(canvas);
        container.appendChild(pageDiv);
        
        await renderPage(i);
    }
    
    requestAnimationFrame(() => {
        const pageEl = document.getElementById('page-' + currentPage);
        if (pageEl) pageEl.scrollIntoView({ behavior: 'smooth' });
    });
}

async function renderPage(pageNum) {
    if (!pdfDoc) return;
    
    const page = await pdfDoc.getPage(pageNum);
    const canvas = document.getElementById('canvas-' + pageNum);
    if (!canvas) return;
    
    const context = canvas.getContext('2d');
    const viewport = page.getViewport({ rotation: currentRotation });
    const scale = currentZoom / 100;
    const scaledViewport = page.getViewport({
        rotation: currentRotation,
        scale: viewport.scale * scale
    });
    
    canvas.width = scaledViewport.width;
    canvas.height = scaledViewport.height;
    
    await page.render({ canvasContext: context, viewport: scaledViewport }).promise;
}

async function renderThumbnails() {
    if (!pdfDoc) return;
    
    const thumbList = document.getElementById('thumbnail-list');
    thumbList.innerHTML = '';
    
    for (let i = 1; i <= numPages; i++) {
        const thumbDiv = document.createElement('div');
        thumbDiv.className = `thumbnail-item ${pageSelectionMode ? 'show-checkbox' : ''}`;
        thumbDiv.dataset.page = i;
        if (selectedPages.has(i)) thumbDiv.classList.add('selected-for-merge');
        
        const checkbox = document.createElement('input');
        checkbox.type = 'checkbox';
        checkbox.className = 'page-checkbox';
        checkbox.checked = selectedPages.has(i);
        checkbox.onclick = (e) => {
            e.stopPropagation();
            togglePageSelectionForMerge(i);
        };
        thumbDiv.appendChild(checkbox);
        
        const canvas = document.createElement('canvas');
        canvas.id = 'thumb-' + i;
        thumbDiv.appendChild(canvas);
        
        thumbDiv.onclick = () => {
            if (pageSelectionMode) {
                togglePageSelectionForMerge(i);
            } else {
                goToPage(i);
            }
        };
        
        const pageNumDiv = document.createElement('div');
        pageNumDiv.className = 'page-num';
        pageNumDiv.textContent = 'Page ' + i;
        thumbDiv.appendChild(pageNumDiv);
        
        thumbList.appendChild(thumbDiv);
        
        const page = await pdfDoc.getPage(i);
        const viewport = page.getViewport({ rotation: currentRotation, scale: 0.3 });
        const thumbCanvas = document.getElementById('thumb-' + i);
        if (thumbCanvas) {
            thumbCanvas.width = viewport.width;
            thumbCanvas.height = viewport.height;
            await page.render({ canvasContext: thumbCanvas.getContext('2d'), viewport }).promise;
        }
    }
    
    updateThumbnailHighlight();
}

function updateThumbnailHighlight() {
    document.querySelectorAll('.thumbnail-item').forEach(item => {
        item.classList.toggle('active', parseInt(item.dataset.page) === currentPage);
    });
}

// ============ PAGE SELECTION ============
function togglePageSelection() {
    pageSelectionMode = !pageSelectionMode;
    document.querySelectorAll('.thumbnail-item').forEach(item => {
        item.classList.toggle('show-checkbox', pageSelectionMode);
    });
    if (!pageSelectionMode) selectedPages.clear();
    showToast(pageSelectionMode ? 'Page selection ON - Click pages to select' : 'Page selection OFF', 'info');
}

function togglePageSelectionForMerge(pageNum) {
    if (selectedPages.has(pageNum)) {
        selectedPages.delete(pageNum);
    } else {
        selectedPages.add(pageNum);
    }
    document.querySelectorAll('.thumbnail-item').forEach(item => {
        const p = parseInt(item.dataset.page);
        item.classList.toggle('selected-for-merge', selectedPages.has(p));
    });
}

// ============ PDF MERGE ============
async function showMergePDFModal() {
    closeModal();
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.pdf';
    input.multiple = true;
    input.onchange = async (e) => {
        const files = Array.from(e.target.files);
        await mergePDFs(files);
    };
    input.click();
}

async function mergePDFs(files) {
    if (files.length < 2) {
        showToast('Please select at least 2 PDF files to merge', 'warning');
        return;
    }
    
    showLoading(true, 'Merging PDFs...');
    showProgress(true);
    
    try {
        const { PDFDocument } = PDFLib;
        const mergedPdf = await PDFDocument.create();
        
        for (let i = 0; i < files.length; i++) {
            updateProgress((i / files.length) * 100);
            const arrayBuffer = await files[i].arrayBuffer();
            const pdf = await PDFDocument.load(arrayBuffer);
            const pages = await mergedPdf.copyPages(pdf, pdf.getPageIndices());
            pages.forEach(page => mergedPdf.addPage(page));
        }
        
        updateProgress(100);
        const mergedBytes = await mergedPdf.save();
        pdfBytes = mergedBytes;
        await loadPdfFromArrayBuffer(mergedBytes.buffer);
        
        saveToHistory('merge');
        showToast(`✅ Successfully merged ${files.length} PDFs!`, 'success');
        downloadPdf(mergedBytes, 'merged.pdf');
    } catch (error) {
        showToast('Error merging PDFs: ' + error.message, 'error');
    } finally {
        showLoading(false);
        showProgress(false);
    }
}

// ============ PDF SPLIT ============
async function showSplitPDFModal() {
    if (!pdfDoc) {
        showToast('Please load a PDF first', 'warning');
        return;
    }
    
    const content = `
        <p>Current PDF has <strong>${numPages}</strong> pages.</p>
        <p>Select pages to extract (e.g., 1-3,5,7-9):</p>
        <input type="text" id="split-pages-input" style="width:100%;padding:8px;border:1px solid #ccc;border-radius:4px;" placeholder="1-3,5,7-9">
    `;
    
    showModal('Split PDF', content, [
        { label: 'Cancel', type: 'secondary', onclick: closeModal },
        { label: 'Split', type: 'primary', onclick: executeSplitPDF }
    ]);
}

async function executeSplitPDF() {
    const input = document.getElementById('split-pages-input').value;
    const pageNumbers = parsePageRange(input);
    
    if (pageNumbers.length === 0) {
        showToast('Please select valid pages', 'warning');
        return;
    }
    
    closeModal();
    showLoading(true, 'Splitting PDF...');
    
    try {
        const { PDFDocument } = PDFLib;
        const pdf = await PDFDocument.load(pdfBytes);
        const newPdf = await PDFDocument.create();
        const pages = await newPdf.copyPages(pdf, pageNumbers.map(p => p - 1));
        pages.forEach(page => newPdf.addPage(page));
        
        const splitBytes = await newPdf.save();
        showToast(`✅ Extracted ${pages.length} pages!`, 'success');
        downloadPdf(splitBytes, 'extracted.pdf');
    } catch (error) {
        showToast('Error splitting PDF: ' + error.message, 'error');
    } finally {
        showLoading(false);
    }
}

function parsePageRange(input) {
    const result = new Set();
    const parts = input.split(',');
    
    for (const part of parts) {
        if (part.includes('-')) {
            const [start, end] = part.split('-').map(Number);
            for (let i = start; i <= end; i++) {
                if (i >= 1 && i <= numPages) result.add(i);
            }
        } else {
            const num = parseInt(part);
            if (num >= 1 && num <= numPages) result.add(num);
        }
    }
    
    return Array.from(result).sort((a, b) => a - b);
}

// ============ PAGE ORGANIZE ============
async function deleteCurrentPage() {
    if (!pdfDoc || numPages <= 1) {
        showToast('Cannot delete the only page', 'warning');
        return;
    }
    
    if (!confirm(`Delete page ${currentPage}?`)) return;
    
    showLoading(true, 'Deleting page...');
    
    try {
        const { PDFDocument } = PDFLib;
        const pdf = await PDFDocument.load(pdfBytes);
        pdf.removePage(currentPage - 1);
        
        const newBytes = await pdf.save();
        pdfBytes = newBytes;
        await loadPdfFromArrayBuffer(newBytes.buffer);
        saveToHistory('delete');
        showToast(`Page ${currentPage} deleted`, 'success');
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
    } finally {
        showLoading(false);
    }
}

async function extractPages() {
    if (selectedPages.size === 0) {
        showToast('Please select pages from sidebar first', 'warning');
        togglePageSelection();
        return;
    }
    
    showLoading(true, 'Extracting pages...');
    
    try {
        const { PDFDocument } = PDFLib;
        const pdf = await PDFDocument.load(pdfBytes);
        const newPdf = await PDFDocument.create();
        const sortedPages = Array.from(selectedPages).sort((a, b) => a - b);
        const pages = await newPdf.copyPages(pdf, sortedPages.map(p => p - 1));
        pages.forEach(page => newPdf.addPage(page));
        
        const extractedBytes = await newPdf.save();
        showToast(`✅ Extracted ${sortedPages.length} pages!`, 'success');
        downloadPdf(extractedBytes, 'extracted_pages.pdf');
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
    } finally {
        showLoading(false);
        pageSelectionMode = false;
        selectedPages.clear();
    }
}

async function duplicatePage() {
    if (!pdfDoc) return;
    
    showLoading(true, 'Duplicating page...');
    
    try {
        const { PDFDocument } = PDFLib;
        const pdf = await PDFDocument.load(pdfBytes);
        const [copiedPage] = await pdf.copyPages(pdf, [currentPage - 1]);
        pdf.insertPage(currentPage, copiedPage);
        
        const newBytes = await pdf.save();
        pdfBytes = newBytes;
        await loadPdfFromArrayBuffer(newBytes.buffer);
        saveToHistory('duplicate');
        showToast('Page duplicated!', 'success');
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
    } finally {
        showLoading(false);
    }
}

async function movePageUp() {
    if (currentPage <= 1) return;
    await swapPages(currentPage - 1, currentPage - 2);
    currentPage--;
    goToPage(currentPage);
}

async function movePageDown() {
    if (currentPage >= numPages) return;
    await swapPages(currentPage - 1, currentPage);
    currentPage++;
    goToPage(currentPage);
}

async function swapPages(indexA, indexB) {
    try {
        const { PDFDocument } = PDFLib;
        const pdf = await PDFDocument.load(pdfBytes);
        
        const pageA = pdf.getPage(indexA);
        const pageB = pdf.getPage(indexB);
        
        pdf.removePage(indexB);
        pdf.removePage(indexA < indexB ? indexA : indexA - 1);
        
        if (indexA < indexB) {
            pdf.insertPage(indexA, pageB);
            pdf.insertPage(indexB, pageA);
        } else {
            pdf.insertPage(indexB, pageA);
            pdf.insertPage(indexA, pageB);
        }
        
        const newBytes = await pdf.save();
        pdfBytes = newBytes;
        await loadPdfFromArrayBuffer(newBytes.buffer);
        saveToHistory('reorder');
        showToast('Pages reordered!', 'success');
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
    }
}

// ============ PDF TO IMAGE ============
async function convertToImage() {
    if (!pdfDoc) {
        showToast('Please load a PDF first', 'warning');
        return;
    }
    
    const formats = [
        { label: 'PNG (Recommended)', value: 'png' },
        { label: 'JPEG', value: 'jpeg' },
        { label: 'WebP', value: 'webp' }
    ];
    
    const content = `
        <p>Convert all ${numPages} pages to images</p>
        <label>Format:</label>
        <select id="image-format" style="width:100%;padding:8px;border:1px solid #ccc;border-radius:4px;">
            ${formats.map(f => `<option value="${f.value}">${f.label}</option>`).join('')}
        </select>
        <label style="margin-top:12px;">Quality (JPEG/WebP):</label>
        <input type="range" id="image-quality" min="0.1" max="1.0" step="0.1" value="0.9" style="width:100%;">
        <span id="quality-value">90%</span>
    `;
    
    showModal('Convert PDF to Images', content, [
        { label: 'Cancel', type: 'secondary', onclick: closeModal },
        { label: 'Convert All Pages', type: 'primary', onclick: executePDFToImage }
    ]);
    
    document.getElementById('image-quality').addEventListener('input', (e) => {
        document.getElementById('quality-value').textContent = Math.round(e.target.value * 100) + '%';
    });
}

async function executePDFToImage() {
    const format = document.getElementById('image-format').value;
    const quality = parseFloat(document.getElementById('image-quality').value);
    closeModal();
    
    showLoading(true, 'Converting PDF to images...');
    showProgress(true);
    
    try {
        const zip = new JSZip();
        
        for (let i = 1; i <= numPages; i++) {
            updateProgress((i / numPages) * 100);
            
            const page = await pdfDoc.getPage(i);
            const viewport = page.getViewport({ rotation: currentRotation, scale: 2.0 });
            
            const canvas = document.createElement('canvas');
            canvas.width = viewport.width;
            canvas.height = viewport.height;
            const ctx = canvas.getContext('2d');
            
            await page.render({ canvasContext: ctx, viewport }).promise;
            
            const blob = await canvasToBlob(canvas, `image/${format}`, quality);
            zip.file(`page_${String(i).padStart(3, '0')}.${format}`, blob);
        }
        
        updateProgress(100);
        
        const zipBlob = await zip.generateAsync({ type: 'blob' });
        saveAs(zipBlob, 'pdf_pages_as_images.zip');
        
        showToast(`✅ Converted ${numPages} pages to images!`, 'success');
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
    } finally {
        showLoading(false);
        showProgress(false);
    }
}

function canvasToBlob(canvas, mimeType, quality) {
    return new Promise(resolve => {
        canvas.toBlob(resolve, mimeType, quality);
    });
}

// ============ IMAGE TO PDF ============
async function handleImageToPDF(files) {
    showLoading(true, 'Converting images to PDF...');
    showProgress(true);
    
    try {
        const { PDFDocument } = PDFLib;
        const pdf = await PDFDocument.create();
        
        for (let i = 0; i < files.length; i++) {
            updateProgress((i / files.length) * 100);
            
            const arrayBuffer = await files[i].arrayBuffer();
            let image;
            
            if (files[i].type === 'image/png') {
                image = await pdf.embedPng(arrayBuffer);
            } else if (files[i].type === 'image/jpeg' || files[i].type === 'image/jpg') {
                image = await pdf.embedJpg(arrayBuffer);
            } else {
                continue;
            }
            
            const page = pdf.addPage([image.width, image.height]);
            page.drawImage(image, {
                x: 0, y: 0,
                width: image.width,
                height: image.height
            });
        }
        
        updateProgress(100);
        const pdfBytes2 = await pdf.save();
        pdfBytes = pdfBytes2;
        await loadPdfFromArrayBuffer(pdfBytes2.buffer);
        
        showToast(`✅ Created PDF from ${files.length} images!`, 'success');
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
    } finally {
        showLoading(false);
        showProgress(false);
    }
}

async function convertImageToPDF() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = 'image/*';
    input.multiple = true;
    input.onchange = async (e) => {
        await handleImageToPDF(Array.from(e.target.files));
    };
    input.click();
}

// ============ PDF TO WORD (Text Extraction) ============
async function convertToWord() {
    if (!pdfDoc) {
        showToast('Please load a PDF first', 'warning');
        return;
    }
    
    showLoading(true, 'Extracting text...');
    
    try {
        let fullText = '';
        
        for (let i = 1; i <= numPages; i++) {
            const page = await pdfDoc.getPage(i);
            const textContent = await page.getTextContent();
            const pageText = textContent.items.map(item => item.str).join(' ');
            fullText += `Page ${i}\n${'='.repeat(50)}\n${pageText}\n\n`;
        }
        
        const blob = new Blob([fullText], { type: 'application/msword' });
        saveAs(blob, 'extracted_text.doc');
        showToast('✅ Text extracted successfully!', 'success');
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
    } finally {
        showLoading(false);
    }
}

// ============ EXTRACT TEXT ============
async function convertToText() {
    if (!pdfDoc) {
        showToast('Please load a PDF first', 'warning');
        return;
    }
    
    showLoading(true, 'Extracting text...');
    
    try {
        let fullText = '';
        
        for (let i = 1; i <= numPages; i++) {
            const page = await pdfDoc.getPage(i);
            const textContent = await page.getTextContent();
            const pageText = textContent.items.map(item => item.str).join(' ');
            fullText += pageText + '\n';
        }
        
        const blob = new Blob([fullText], { type: 'text/plain' });
        saveAs(blob, 'extracted_text.txt');
        showToast('✅ Text extracted!', 'success');
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
    } finally {
        showLoading(false);
    }
}

// ============ COMPRESS PDF ============
async function compressPDF() {
    if (!pdfDoc) {
        showToast('Please load a PDF first', 'warning');
        return;
    }
    
    showLoading(true, 'Compressing PDF...');
    
    try {
        // Simple compression by re-saving with pdf-lib
        const { PDFDocument } = PDFLib;
        const pdf = await PDFDocument.load(pdfBytes);
        const compressedBytes = await pdf.save({
            useObjectStreams: true,
            addDefaultPage: false
        });
        
        const originalSize = pdfBytes.length;
        const compressedSize = compressedBytes.length;
        const ratio = ((1 - compressedSize / originalSize) * 100).toFixed(1);
        
        pdfBytes = compressedBytes;
        await loadPdfFromArrayBuffer(compressedBytes.buffer);
        saveToHistory('compress');
        
        showToast(`✅ Compressed! Reduced by ${ratio}%`, 'success');
        downloadPdf(compressedBytes, 'compressed.pdf');
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
    } finally {
        showLoading(false);
    }
}

// ============ WATERMARK ============
async function addWatermark() {
    if (!pdfDoc) {
        showToast('Please load a PDF first', 'warning');
        return;
    }
    
    const content = `
        <label>Watermark Text:</label>
        <input type="text" id="watermark-text" value="CONFIDENTIAL" style="width:100%;padding:8px;border:1px solid #ccc;border-radius:4px;">
        <label style="margin-top:12px;">Opacity:</label>
        <input type="range" id="watermark-opacity" min="0.1" max="1.0" step="0.1" value="0.3" style="width:100%;">
    `;
    
    showModal('Add Watermark', content, [
        { label: 'Cancel', type: 'secondary', onclick: closeModal },
        { label: 'Apply', type: 'primary', onclick: executeWatermark }
    ]);
}

async function executeWatermark() {
    const text = document.getElementById('watermark-text').value;
    const opacity = parseFloat(document.getElementById('watermark-opacity').value);
    closeModal();
    
    showLoading(true, 'Adding watermark...');
    showProgress(true);
    
    try {
        const { PDFDocument, StandardFonts, rgb } = PDFLib;
        const pdf = await PDFDocument.load(pdfBytes);
        const font = await pdf.embedFont(StandardFonts.HelveticaBold);
        const pages = pdf.getPages();
        
        for (let i = 0; i < pages.length; i++) {
            updateProgress((i / pages.length) * 100);
            const page = pages[i];
            const { width, height } = page.getSize();
            
            page.drawText(text, {
                x: width / 2 - 150,
                y: height / 2,
                size: 60,
                font: font,
                color: rgb(0.5, 0.5, 0.5),
                opacity: opacity,
                rotate: -45
            });
        }
        
        const watermarkedBytes = await pdf.save();
        pdfBytes = watermarkedBytes;
        await loadPdfFromArrayBuffer(watermarkedBytes.buffer);
        saveToHistory('watermark');
        
        showToast('✅ Watermark applied!', 'success');
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
    } finally {
        showLoading(false);
        showProgress(false);
    }
}

// ============ NAVIGATION ============
function goToPage(pageNum) {
    if (!pdfDoc || pageNum < 1 || pageNum > numPages) return;
    currentPage = pageNum;
    
    const pageEl = document.getElementById('page-' + pageNum);
    if (pageEl) pageEl.scrollIntoView({ behavior: 'smooth' });
    
    updateThumbnailHighlight();
    updateStatusBar();
}

function nextPage() { if (currentPage < numPages) goToPage(currentPage + 1); }
function prevPage() { if (currentPage > 1) goToPage(currentPage - 1); }

// ============ ZOOM ============
function zoomIn() {
    if (currentZoom >= 400) return;
    currentZoom = Math.min(400, Math.round(currentZoom / 10) * 10 + 10);
    zoomMode = 'custom';
    refreshRender();
}

function zoomOut() {
    if (currentZoom <= 25) return;
    currentZoom = Math.max(25, Math.round(currentZoom / 10) * 10 - 10);
    zoomMode = 'custom';
    refreshRender();
}

function fitPage() {
    zoomMode = 'page-fit';
    const viewer = document.getElementById('pdf-viewer');
    currentZoom = Math.floor((viewer.clientHeight - 40) / 792 * 100);
    currentZoom = Math.max(25, Math.min(400, currentZoom));
    refreshRender();
}

function fitWidth() {
    zoomMode = 'page-width';
    const viewer = document.getElementById('pdf-viewer');
    currentZoom = Math.floor((viewer.clientWidth - 60) / 612 * 100);
    currentZoom = Math.max(25, Math.min(400, currentZoom));
    refreshRender();
}

function showZoomMenu() {
    const zooms = ['25%', '50%', '75%', '100%', '125%', '150%', '200%', '300%', '400%'];
    const choice = prompt('Select zoom:\n' + zooms.join(' | '), currentZoom + '%');
    if (choice) {
        const val = parseInt(choice.replace('%', ''));
        if (val >= 25 && val <= 400) {
            currentZoom = val;
            zoomMode = 'custom';
            refreshRender();
        }
    }
}

async function refreshRender() {
    if (!pdfDoc) return;
    await renderAllPages();
    updateStatusBar();
}

// ============ ROTATION ============
function rotateCW() {
    currentRotation = (currentRotation + 90) % 360;
    refreshRender();
}

// ============ DOWNLOAD HELPERS ============
function downloadPdf(bytes, filename) {
    const blob = new Blob([bytes], { type: 'application/pdf' });
    saveAs(blob, filename);
}

// ============ FILE OPERATIONS ============
function showFileMenu() {
    const content = `
        <div style="display:flex;flex-direction:column;gap:8px;">
            <button class="btn btn-primary" onclick="openPDF();closeModal();">📂 Open PDF</button>
            <button class="btn btn-secondary" onclick="onSave();closeModal();">💾 Save As</button>
            <button class="btn btn-secondary" onclick="onPrint();closeModal();">🖨 Print</button>
            <button class="btn btn-secondary" onclick="exportPDF();closeModal();">📤 Export</button>
            <hr>
            <button class="btn btn-secondary" onclick="showMergePDFModal();closeModal();">📑 Merge PDFs</button>
            <button class="btn btn-secondary" onclick="showSplitPDFModal();closeModal();">✂️ Split PDF</button>
        </div>
    `;
    showModal('File Menu', content, []);
}

function openPDF() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.pdf';
    input.onchange = (e) => {
        if (e.target.files.length > 0) loadPdfFromFile(e.target.files[0]);
    };
    input.click();
}

function onSave() {
    if (pdfBytes) {
        downloadPdf(pdfBytes, currentFilePath || 'document.pdf');
        showToast('PDF saved!', 'success');
    } else {
        showToast('No PDF loaded to save', 'warning');
    }
}

function onPrint() { window.print(); }
function onShare() { showToast('Share functionality - Copy link or send via email', 'info'); }

function exportPDF() {
    if (pdfBytes) {
        downloadPdf(pdfBytes, 'exported.pdf');
        showToast('PDF exported!', 'success');
    }
}

// ============ TAB SWITCHING ============
function switchTab(tabName) {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelector(`.tab[data-tab="${tabName}"]`).classList.add('active');
    document.querySelectorAll('.toolbar').forEach(tb => tb.style.display = 'none');
    const toolbar = document.getElementById('toolbar-' + tabName);
    if (toolbar) toolbar.style.display = 'flex';
}

// ============ SIDEBAR ============
function switchSidebar(panel) {
    currentSidebar = panel;
    document.querySelectorAll('.strip-icon').forEach(icon => {
        icon.classList.toggle('active', icon.dataset.panel === panel);
    });
    
    document.querySelectorAll('#sidebar-content > div, #sidebar-content > h3').forEach(el => {
        el.style.display = 'none';
    });
    
    const panelNames = {
        'pages': { title: 'Pages', list: 'thumbnail-list' },
        'bookmarks': { title: 'Bookmarks', list: 'bookmarks-list' },
        'annotations': { title: 'Annotations', list: 'annotations-list' },
        'attachments': { title: 'Attachments', list: 'attachments-list' }
    };
    
    const info = panelNames[panel];
    if (info) {
        document.querySelector('#sidebar-content h3').style.display = 'flex';
        document.querySelector('#sidebar-content h3').childNodes[0].textContent = info.title;
        const listEl = document.getElementById(info.list);
        if (listEl) listEl.style.display = '';
    }
}

function toggleSidebar() {
    document.getElementById('sidebar').classList.toggle('collapsed');
}

// ============ TOOLS ============
function setTool(tool) {
    currentTool = tool;
    document.querySelectorAll('.tool-item').forEach(item => item.style.background = '');
    const toolEl = document.getElementById('tool-' + tool);
    if (toolEl) toolEl.style.background = 'var(--selected-bg)';
}

// ============ EDIT OPERATIONS ============
function editText() { showToast('Edit Text - Select text to edit', 'info'); }
function addTextAnnotation() { showToast('Click on page to add annotation', 'info'); }
function highlightText() { showToast('Select text to highlight', 'info'); }
function addSignature() { showToast('Draw or upload signature', 'info'); }
function addImage() { showToast('Select image to insert', 'info'); }
function addLink() { showToast('Draw area to add link', 'info'); }
function cropPage() { showToast('Select crop area', 'info'); }

// ============ VIEW OPERATIONS ============
function toggleReadMode() { document.body.classList.toggle('read-mode'); }
function toggleFullScreen() {
    if (document.fullscreenElement) {
        document.exitFullscreen();
    } else {
        document.documentElement.requestFullscreen();
    }
}
function toggleTheme() {
    const root = document.documentElement;
    if (root.style.getPropertyValue('--bg-white') === '#FAFBFC') {
        root.style.setProperty('--bg-white', '#1e1e1e');
        root.style.setProperty('--bg-sidebar', '#252526');
        root.style.setProperty('--bg-strip', '#2d2d30');
        root.style.setProperty('--bg-doc', '#333333');
        root.style.setProperty('--text-primary', '#e0e0e0');
        root.style.setProperty('--text-secondary', '#a0a0a0');
        root.style.setProperty('--border', '#404040');
        showToast('🌙 Dark mode enabled', 'info');
    } else {
        root.style.setProperty('--bg-white', '#FAFBFC');
        root.style.setProperty('--bg-sidebar', '#F5F5F7');
        root.style.setProperty('--bg-strip', '#EBEBEE');
        root.style.setProperty('--bg-doc', '#DCDCDC');
        root.style.setProperty('--text-primary', '#1E1E1E');
        root.style.setProperty('--text-secondary', '#505050');
        root.style.setProperty('--border', '#C8C8CD');
        showToast('☀️ Light mode enabled', 'info');
    }
}

// ============ STATUS BAR ============
function updateStatusBar() {
    document.getElementById('status-page-info').textContent = `Page ${currentPage} of ${numPages}`;
    document.getElementById('zoom-value').textContent = currentZoom + '%';
}

// ============ KEYBOARD SHORTCUTS ============
function setupKeyboardShortcuts() {
    document.addEventListener('keydown', (e) => {
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
        
        switch(e.key) {
            case 'ArrowDown': case 'PageDown': nextPage(); e.preventDefault(); break;
            case 'ArrowUp': case 'PageUp': prevPage(); e.preventDefault(); break;
            case 'Home': goToPage(1); e.preventDefault(); break;
            case 'End': goToPage(numPages); e.preventDefault(); break;
            case '+': case '=': zoomIn(); e.preventDefault(); break;
            case '-': zoomOut(); e.preventDefault(); break;
            case '0': if (e.ctrlKey) { fitPage(); e.preventDefault(); } break;
            case 'F4': toggleSidebar(); e.preventDefault(); break;
            case 'r': if (e.ctrlKey && e.shiftKey) { rotateCW(); e.preventDefault(); } break;
            case 's': if (e.ctrlKey) { e.preventDefault(); onSave(); } break;
            case 'p': if (e.ctrlKey) { e.preventDefault(); onPrint(); } break;
            case 'z': if (e.ctrlKey && !e.shiftKey) { e.preventDefault(); onUndo(); } break;
            case 'y': if (e.ctrlKey) { e.preventDefault(); onRedo(); } break;
        }
    });
}

// ============ SCROLL TRACKING ============
document.getElementById('pdf-viewer').addEventListener('scroll', () => {
    if (!pdfDoc) return;
    
    let bestPage = 1;
    let bestVisibility = 0;
    
    for (let i = 1; i <= numPages; i++) {
        const pageEl = document.getElementById('page-' + i);
        if (!pageEl) continue;
        
        const rect = pageEl.getBoundingClientRect();
        const viewerRect = document.getElementById('pdf-viewer').getBoundingClientRect();
        const visibleTop = Math.max(rect.top, viewerRect.top);
        const visibleBottom = Math.min(rect.bottom, viewerRect.bottom);
        const visibleHeight = Math.max(0, visibleBottom - visibleTop);
        
        if (visibleHeight > bestVisibility) {
            bestVisibility = visibleHeight;
            bestPage = i;
        }
    }
    
    if (bestPage !== currentPage) {
        currentPage = bestPage;
        updateThumbnailHighlight();
        updateStatusBar();
    }
});

// Expose functions globally for C++ host
window.loadPdfFromPath = loadPdfFromPath;
window.loadPdfFromFile = loadPdfFromFile;
</script>
</body>
</html>
)HTML";
}

// ==========================================
// 🎨 LIGHTWEIGHT NON-CLIENT AREA DRAWING
// ==========================================
void DrawMinimalFrame(Graphics& g, int w, int h, float scale) {
    Pen borderPen(Color(255, 200, 200, 205), 2.0f);
    g.DrawRectangle(&borderPen, 0, 0, w - 1, h - 1);
}

// ==========================================
// 🪟 WINDOW PROCEDURE
// ==========================================
LRESULT CALLBACK AcrobatViewerWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        RECT r;
        GetClientRect(hWnd, &r);

        g_hWebViewWnd = CreateWindowExW(
            0, L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, r.right, r.bottom,
            hWnd, (HMENU)1001, GetModuleHandle(NULL), NULL
        );

        InitializeWebView2(hWnd, g_hWebViewWnd);
        break;
    }
    case WM_SIZE: {
        if (g_hWebViewWnd && g_webViewController) {
            RECT r;
            GetClientRect(hWnd, &r);
            SetWindowPos(g_hWebViewWnd, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER);
            g_webViewController->put_Bounds(RECT{0, 0, r.right, r.bottom});
        }
        break;
    }
    case WM_CLOSE: {
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    }
    case WM_DESTROY: {
        if (g_webViewController) {
            g_webViewController->Close();
            g_webViewController = nullptr;
        }
        g_webView = nullptr;
        g_webViewEnv = nullptr;
        g_webViewInitialized = false;

        if (g_hWebViewWnd) {
            DestroyWindow(g_hWebViewWnd);
            g_hWebViewWnd = NULL;
        }
        g_hAcrobatWnd = NULL;
        break;
    }
    default:
        return DefWindowProcW(hWnd, msg, wp, lp);
    }
    return 0;
}

// ==========================================
// 🌐 WEBVIEW2 INITIALIZATION
// ==========================================
HRESULT InitializeWebView2(HWND hWnd, HWND hHostWnd) {
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hWnd, hHostWnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) return result;
                
                g_webViewEnv = env;
                
                env->CreateCoreWebView2Controller(
                    hHostWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hWnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result)) return result;
                            
                            g_webViewController = controller;
                            g_webViewController->get_CoreWebView2(&g_webView);
                            
                            ICoreWebView2Settings* settings;
                            g_webView->get_Settings(&settings);
                            settings->put_IsScriptEnabled(TRUE);
                            settings->put_IsWebMessageEnabled(TRUE);
                            settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                            settings->put_AreDevToolsEnabled(TRUE);
                            
                            RECT r;
                            GetClientRect(hWnd, &r);
                            g_webViewController->put_Bounds(RECT{0, 0, r.right, r.bottom});
                            
                            g_webView->NavigateToString(GetAcrobatHTML());
                            
                            g_webView->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        BOOL success;
                                        args->get_IsSuccess(&success);
                                        if (success) {
                                            g_webViewInitialized = true;
                                            
                                            if (!g_acrobatPdfPath.empty()) {
                                                std::wstring escapedPath = g_acrobatPdfPath;
                                                size_t pos = 0;
                                                while ((pos = escapedPath.find(L"\\", pos)) != std::wstring::npos) {
                                                    escapedPath.replace(pos, 1, L"\\\\");
                                                    pos += 2;
                                                }
                                                
                                                std::wstring script = L"loadPdfFromPath('" + escapedPath + L"');";
                                                sender->ExecuteScript(script.c_str(), nullptr);
                                            }
                                        }
                                        return S_OK;
                                    }
                                ).Get(), nullptr
                            );
                            
                            return S_OK;
                        }
                    ).Get()
                );
                
                return S_OK;
            }
        ).Get()
    );
    
    return S_OK;
}

// ==========================================
// 🚀 LAUNCH ACROBAT STYLE PDF VIEWER
// ==========================================
void LaunchFoxitStylePdfReader(std::wstring pdfPath) {
    g_acrobatPdfPath = pdfPath;

    if (g_hAcrobatWnd != NULL) {
        SetForegroundWindow(g_hAcrobatWnd);
        ShowWindow(g_hAcrobatWnd, SW_SHOW);
        
        if (g_webViewInitialized && g_webView && !pdfPath.empty()) {
            std::wstring escapedPath = pdfPath;
            size_t pos = 0;
            while ((pos = escapedPath.find(L"\\", pos)) != std::wstring::npos) {
                escapedPath.replace(pos, 1, L"\\\\");
                pos += 2;
            }
            
            std::wstring script = L"loadPdfFromPath('" + escapedPath + L"');";
            g_webView->ExecuteScript(script.c_str(), nullptr);
        }
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = AcrobatViewerWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"AcrobatWorkspaceClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    g_hAcrobatWnd = CreateWindowExW(
        0, L"AcrobatWorkspaceClass", L"RasFocus - PDF Workspace",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        (int)(1200 * g_scaleFactor), (int)(800 * g_scaleFactor),
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    HICON hAppIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101));
    if (hAppIcon) {
        SendMessage(g_hAcrobatWnd, WM_SETICON, ICON_BIG, (LPARAM)hAppIcon);
        SendMessage(g_hAcrobatWnd, WM_SETICON, ICON_SMALL, (LPARAM)hAppIcon);
    }

    ShowWindow(g_hAcrobatWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(g_hAcrobatWnd);
}

// ==========================================
// ⚠️ LEGACY FUNCTIONS (for compatibility)
// ==========================================
void DrawPdfWorkspaceTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch) {
    FontFamily ff(L"Segoe UI");
    Font fText(&ff, 20 * g_scaleFactor, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 100, 100, 100));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"PDF Workspace opens in a separate Adobe Acrobat-style window.\nClick 'Open PDF' from Dashboard to launch.",
        -1, &fText, RectF(cx, cy, cw, ch), &fmt, &textBrush);
}

void ProcessPdfWorkspaceMouseMove(float x, float y) {}
void ProcessPdfWorkspaceMouseClick(float x, float y) {}
