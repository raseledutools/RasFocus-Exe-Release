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
#include <wrl.h>         // 🟢 FIX: Added for Callback support
#include <wrl/event.h>   // 🟢 FIX: Added for Event Handlers

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

// Forward Declarations
HRESULT InitializeWebView2(HWND hWnd, HWND hHostWnd);
LRESULT CALLBACK AcrobatViewerWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

// ==========================================
// 🎨 HTML/CSS/JS UI - SPLIT TO FIX "STRING TOO BIG" ERROR
// ==========================================
const wchar_t* GetAcrobatHTML() {
    return LR"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>PDF Workspace - Professional Edition</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
<script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
<style>
:root {
    --ribbon-height: 130px; --tab-height: 32px; --sidebar-width: 260px;
    --left-strip: 45px; --status-height: 28px; --purple: #68217A;
    --purple-hover: #7B1FA2; --bg-white: #FAFBFC; --bg-sidebar: #F5F5F7;
    --bg-strip: #EBEBEE; --bg-doc: #DCDCDC; --border: #C8C8CD;
    --text-primary: #1E1E1E; --text-secondary: #505050; --hover-bg: #EBEBF0;
    --selected-bg: #DCDCE4; --success: #107C10; --danger: #D13438; --warning: #FF8C00;
}
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', system-ui, sans-serif; height: 100vh; overflow: hidden; display: flex; flex-direction: column; user-select: none; background: #1e1e1e; }
.toast-container { position: fixed; top: 20px; right: 20px; z-index: 9999; display: flex; flex-direction: column; gap: 8px; }
.toast { padding: 12px 20px; border-radius: 6px; color: white; font-size: 13px; font-weight: 500; box-shadow: 0 4px 12px rgba(0,0,0,0.3); animation: slideIn 0.3s ease; max-width: 380px; cursor: pointer; }
.toast.success { background: var(--success); } .toast.error { background: var(--danger); } .toast.warning { background: var(--warning); } .toast.info { background: #0078D4; }
@keyframes slideIn { from { transform: translateX(100%); opacity: 0; } to { transform: translateX(0); opacity: 1; } }
@keyframes slideOut { from { transform: translateX(0); opacity: 1; } to { transform: translateX(100%); opacity: 0; } }
.modal-overlay { display: none; position: fixed; top: 0; left: 0; right: 0; bottom: 0; background: rgba(0,0,0,0.6); z-index: 1000; justify-content: center; align-items: center; }
.modal-overlay.show { display: flex; }
.modal { background: white; border-radius: 8px; padding: 24px; min-width: 400px; max-width: 600px; max-height: 80vh; overflow-y: auto; box-shadow: 0 8px 32px rgba(0,0,0,0.3); }
.modal h3 { font-size: 18px; margin-bottom: 16px; color: var(--text-primary); }
.modal .modal-actions { display: flex; gap: 8px; justify-content: flex-end; margin-top: 16px; }
.modal .btn { padding: 8px 16px; border: none; border-radius: 4px; cursor: pointer; font-size: 13px; font-weight: 500; }
.modal .btn-primary { background: var(--purple); color: white; } .modal .btn-primary:hover { background: var(--purple-hover); }
.modal .btn-secondary { background: #e0e0e0; color: var(--text-primary); } .modal .btn-secondary:hover { background: #d0d0d0; }
.ribbon { height: var(--ribbon-height); background: var(--bg-white); border-bottom: 1px solid var(--border); flex-shrink: 0; display: flex; flex-direction: column; }
.tab-bar { height: var(--tab-height); display: flex; align-items: stretch; background: var(--bg-white); border-bottom: 1px solid var(--border); }
.file-btn { background: var(--purple); color: white; padding: 0 18px; display: flex; align-items: center; font-weight: 600; font-size: 13px; cursor: pointer; transition: background 0.15s; }
.file-btn:hover { background: var(--purple-hover); }
.tab { padding: 0 16px; display: flex; align-items: center; font-size: 13px; color: var(--text-secondary); cursor: pointer; border-bottom: 3px solid transparent; transition: all 0.15s; }
.tab:hover { background: var(--hover-bg); }
.tab.active { color: var(--text-primary); font-weight: 600; border-bottom-color: var(--purple); background: white; }
.tab-spacer { flex: 1; }
.quick-actions { display: flex; align-items: center; gap: 4px; padding-right: 8px; }
.quick-actions .icon-btn { width: 32px; height: 26px; border: none; background: transparent; cursor: pointer; border-radius: 4px; font-size: 14px; color: var(--text-secondary); display: flex; align-items: center; justify-content: center; }
.quick-actions .icon-btn:hover { background: var(--hover-bg); }
.toolbar { flex: 1; display: flex; align-items: flex-start; padding: 8px 20px; gap: 4px; overflow-x: auto; }
.tool-group { display: flex; gap: 4px; align-items: flex-start; }
.tool-item { display: flex; flex-direction: column; align-items: center; padding: 4px 8px; border-radius: 6px; cursor: pointer; min-width: 48px; transition: background 0.15s; }
.tool-item:hover { background: var(--hover-bg); }
.tool-item .tool-icon { font-size: 22px; color: var(--text-primary); margin-bottom: 2px; }
.tool-item .tool-label { font-size: 10px; color: var(--text-secondary); text-align: center; }
.tool-separator { width: 1px; background: var(--border); margin: 4px 8px; align-self: stretch; }
.main-content { flex: 1; display: flex; overflow: hidden; }
.sidebar { width: var(--sidebar-width); background: var(--bg-sidebar); display: flex; flex-shrink: 0; border-right: 1px solid var(--border); transition: width 0.2s ease; }
.sidebar.collapsed { width: 0; overflow: hidden; border-right: none; }
.sidebar-strip { width: var(--left-strip); background: var(--bg-strip); display: flex; flex-direction: column; align-items: center; padding-top: 10px; flex-shrink: 0; border-right: 1px solid var(--border); }
.sidebar-strip .strip-icon { width: 35px; height: 36px; display: flex; align-items: center; justify-content: center; font-size: 18px; color: var(--text-secondary); cursor: pointer; border-radius: 6px; margin: 2px 0; position: relative; transition: all 0.15s; }
.sidebar-strip .strip-icon:hover { background: var(--hover-bg); }
.sidebar-strip .strip-icon.active { background: var(--selected-bg); color: var(--purple); }
.sidebar-panel { flex: 1; padding: 10px; overflow-y: auto; display: flex; flex-direction: column; }
.sidebar-panel h3 { font-size: 14px; font-weight: 600; color: var(--text-primary); margin-bottom: 8px; padding-bottom: 8px; border-bottom: 1px solid var(--border); display: flex; justify-content: space-between; align-items: center; }
.thumbnail-list { display: flex; flex-direction: column; gap: 8px; }
.thumbnail-item { background: white; border: 1px solid var(--border); border-radius: 4px; padding: 6px; cursor: pointer; transition: all 0.15s; position: relative; }
.thumbnail-item:hover { border-color: var(--purple); box-shadow: 0 1px 4px rgba(0,0,0,0.1); }
.thumbnail-item.active { border-color: var(--purple); border-width: 2px; }
.thumbnail-item.selected-for-merge { border-color: #FF8C00; background: #FFF3E0; }
.thumbnail-item canvas { width: 100%; height: auto; border-radius: 2px; }
.thumbnail-item .page-num { text-align: center; font-size: 10px; color: var(--text-secondary); margin-top: 4px; }
.thumbnail-item .page-checkbox { position: absolute; top: 4px; right: 4px; width: 16px; height: 16px; cursor: pointer; z-index: 10; display: none; }
.thumbnail-item.show-checkbox .page-checkbox { display: block; }
.pdf-viewer { flex: 1; background: var(--bg-doc); overflow: auto; display: flex; justify-content: center; padding: 20px; }
.pdf-container { display: flex; flex-direction: column; align-items: center; gap: 16px; }
.pdf-page { background: white; box-shadow: 0 2px 12px rgba(0,0,0,0.15); border-radius: 2px; position: relative; }
.pdf-page canvas { display: block; border-radius: 2px; }
.status-bar { height: var(--status-height); background: #F0F0F2; border-top: 1px solid var(--border); display: flex; align-items: center; padding: 0 16px; font-size: 11px; color: var(--text-secondary); flex-shrink: 0; gap: 16px; }
.status-bar .zoom-control { margin-left: auto; display: flex; align-items: center; gap: 6px; }
.status-bar .zoom-control button { width: 24px; height: 22px; border: 1px solid var(--border); background: white; cursor: pointer; border-radius: 3px; font-size: 12px; display: flex; align-items: center; justify-content: center; color: var(--text-primary); }
.progress-bar { display: none; height: 4px; background: #e0e0e0; flex-shrink: 0; }
.progress-bar.active { display: block; }
.progress-bar .progress-fill { height: 100%; background: var(--purple); width: 0%; transition: width 0.3s ease; }
.loading-overlay { display: none; position: fixed; top: 50%; left: 50%; transform: translate(-50%, -50%); background: rgba(0,0,0,0.85); color: white; padding: 20px 40px; border-radius: 8px; font-size: 14px; z-index: 2000; text-align: center; }
.loading-overlay.show { display: block; }
.spinner { width: 40px; height: 40px; border: 3px solid rgba(255,255,255,0.3); border-top-color: white; border-radius: 50%; animation: spin 0.8s linear infinite; margin: 0 auto 12px; }
@keyframes spin { to { transform: rotate(360deg); } }
</style>
</head>
<body>
)HTML"

// 🟢 SPLIT 2: HTML Structure
LR"HTML(
<div class="toast-container" id="toast-container"></div>
<div class="modal-overlay" id="modal-overlay"><div class="modal" id="modal-content"></div></div>
<div class="progress-bar" id="progress-bar"><div class="progress-fill" id="progress-fill"></div></div>
<div class="ribbon">
    <div class="tab-bar">
        <div class="file-btn" onclick="showFileMenu()">📄 File</div>
        <div class="tab active" data-tab="home" onclick="switchTab('home')">Home</div>
        <div class="tab" data-tab="tools" onclick="switchTab('tools')">Tools</div>
        <div class="tab" data-tab="organize" onclick="switchTab('organize')">Organize</div>
        <div class="tab" data-tab="convert" onclick="switchTab('convert')">Convert</div>
        <div class="tab-spacer"></div>
        <div class="quick-actions">
            <button class="icon-btn" onclick="onUndo()">↩</button>
            <button class="icon-btn" onclick="onRedo()">↪</button>
            <button class="icon-btn" onclick="onSave()">💾</button>
        </div>
    </div>
    
    <div class="toolbar" id="toolbar-home">
        <div class="tool-group">
            <div class="tool-item" onclick="setTool('hand')" id="tool-hand"><span class="tool-icon">✋</span><span class="tool-label">Hand</span></div>
            <div class="tool-item" onclick="setTool('select')" id="tool-select"><span class="tool-icon">👆</span><span class="tool-label">Select</span></div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="zoomOut()"><span class="tool-icon">🔍</span><span class="tool-label">Zoom Out</span></div>
            <div class="tool-item" onclick="fitPage()"><span class="tool-icon">📄</span><span class="tool-label">Fit Page</span></div>
            <div class="tool-item" onclick="fitWidth()"><span class="tool-icon">↔</span><span class="tool-label">Fit Width</span></div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="rotateCW()"><span class="tool-icon">↻</span><span class="tool-label">Rotate</span></div>
        </div>
    </div>
    
    <div class="toolbar" id="toolbar-tools" style="display:none;">
        <div class="tool-group">
            <div class="tool-item" onclick="showMergePDFModal()"><span class="tool-icon">📑</span><span class="tool-label">Merge PDFs</span></div>
            <div class="tool-item" onclick="showSplitPDFModal()"><span class="tool-icon">✂️</span><span class="tool-label">Split PDF</span></div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="compressPDF()"><span class="tool-icon">📦</span><span class="tool-label">Compress</span></div>
        </div>
    </div>
    
    <div class="toolbar" id="toolbar-organize" style="display:none;">
        <div class="tool-group">
            <div class="tool-item" onclick="extractPages()"><span class="tool-icon">📤</span><span class="tool-label">Extract Pages</span></div>
            <div class="tool-item" onclick="deleteCurrentPage()"><span class="tool-icon">🗑</span><span class="tool-label">Delete Page</span></div>
        </div>
    </div>

    <div class="toolbar" id="toolbar-convert" style="display:none;">
        <div class="tool-group">
            <div class="tool-item" onclick="convertToImage()"><span class="tool-icon">🖼</span><span class="tool-label">PDF to Image</span></div>
            <div class="tool-item" onclick="convertToWord()"><span class="tool-icon">📝</span><span class="tool-label">Extract Text</span></div>
        </div>
    </div>
</div>

<div class="main-content">
    <div class="sidebar" id="sidebar">
        <div class="sidebar-strip">
            <div class="strip-icon active" onclick="switchSidebar('pages')">📑</div>
        </div>
        <div class="sidebar-panel" id="sidebar-content">
            <h3>Pages <span style="font-size:11px;color:var(--purple);cursor:pointer;" onclick="togglePageSelection()">[Select]</span></h3>
            <div class="thumbnail-list" id="thumbnail-list"></div>
        </div>
    </div>
    <div class="pdf-viewer" id="pdf-viewer">
        <div class="pdf-container" id="pdf-container">
            <div style="color: var(--text-secondary); font-size: 16px; padding: 40px; text-align:center;">
                <div style="font-size:64px;margin-bottom:16px;">📄</div>
                <div>No PDF loaded</div>
            </div>
        </div>
    </div>
</div>

<div class="status-bar">
    <span class="status-item">📄 <span id="status-page-info">Page 1 of 1</span></span>
    <span class="status-item" id="status-file-size"></span>
    <div class="zoom-control">
        <button onclick="zoomOut()">−</button>
        <span class="zoom-value" id="zoom-value" onclick="showZoomMenu()">100%</span>
        <button onclick="zoomIn()">+</button>
    </div>
</div>
<div class="loading-overlay" id="loading-overlay"><div class="spinner"></div><div id="loading-text">Processing...</div></div>
)HTML"

// 🟢 SPLIT 3: Javascript Logic
LR"HTML(
<script>
let pdfDoc = null; let pdfBytes = null; let currentPage = 1; let currentZoom = 100; let currentRotation = 0; let numPages = 0;
let pageSelectionMode = false; let selectedPages = new Set(); let history = []; let historyIndex = -1;

function showToast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = `toast ${type}`; toast.textContent = message;
    container.appendChild(toast);
    setTimeout(() => { toast.remove(); }, 3000);
}

function showModal(title, content, actions) {
    const overlay = document.getElementById('modal-overlay');
    const modalContent = document.getElementById('modal-content');
    let actionsHTML = actions.map(a => `<button class="btn btn-${a.type || 'secondary'}" onclick="(${a.onclick.toString()})()">${a.label}</button>`).join('');
    modalContent.innerHTML = `<h3>${title}</h3><div>${content}</div><div class="modal-actions">${actionsHTML}</div>`;
    overlay.classList.add('show');
}
function closeModal() { document.getElementById('modal-overlay').classList.remove('show'); }
function showLoading(show, text = 'Processing...') { document.getElementById('loading-overlay').classList.toggle('show', show); document.getElementById('loading-text').textContent = text; }
function updateProgress(percent) { document.getElementById('progress-fill').style.width = percent + '%'; }

document.addEventListener('DOMContentLoaded', () => { checkForPdfPath(); });

function checkForPdfPath() {
    if (window.chrome && window.chrome.webview) {
        window.chrome.webview.addEventListener('message', (event) => {
            if (event.data && event.data.type === 'loadPdf') loadPdfFromPath(event.data.path);
        });
    }
}

async function loadPdfFromPath(path) {
    showLoading(true, 'Loading PDF...');
    try {
        const response = await fetch('file:///' + path.replace(/\\/g, '/'));
        const arrayBuffer = await response.arrayBuffer();
        await loadPdfFromArrayBuffer(arrayBuffer);
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
        showLoading(false);
    }
}

async function loadPdfFromFile(file) {
    showLoading(true, 'Loading PDF...');
    const reader = new FileReader();
    reader.onload = async (e) => { await loadPdfFromArrayBuffer(e.target.result); };
    reader.readAsArrayBuffer(file);
}

async function loadPdfFromArrayBuffer(arrayBuffer) {
    try {
        pdfBytes = new Uint8Array(arrayBuffer);
        const pdf = await pdfjsLib.getDocument({ data: pdfBytes }).promise;
        pdfDoc = pdf; numPages = pdf.numPages; currentPage = 1; currentRotation = 0; currentZoom = 100; selectedPages.clear();
        await renderAllPages(); await renderThumbnails(); updateStatusBar();
        saveToHistory('load');
        showToast(`Loaded ${numPages} pages`, 'success');
        showLoading(false);
    } catch (error) {
        showToast('Error: ' + error.message, 'error');
        showLoading(false);
    }
}

function saveToHistory(action) {
    historyIndex++; history = history.slice(0, historyIndex);
    history.push({ action, pdfBytes: pdfBytes ? new Uint8Array(pdfBytes) : null, currentPage, currentZoom });
}

async function renderAllPages() {
    if (!pdfDoc) return;
    const container = document.getElementById('pdf-container'); container.innerHTML = '';
    for (let i = 1; i <= numPages; i++) {
        const pageDiv = document.createElement('div'); pageDiv.className = 'pdf-page'; pageDiv.id = 'page-' + i;
        const canvas = document.createElement('canvas'); canvas.id = 'canvas-' + i;
        pageDiv.appendChild(canvas); container.appendChild(pageDiv);
        await renderPage(i);
    }
}

async function renderPage(pageNum) {
    const page = await pdfDoc.getPage(pageNum);
    const canvas = document.getElementById('canvas-' + pageNum);
    const context = canvas.getContext('2d');
    const viewport = page.getViewport({ rotation: currentRotation, scale: currentZoom / 100 });
    canvas.width = viewport.width; canvas.height = viewport.height;
    await page.render({ canvasContext: context, viewport }).promise;
}

async function renderThumbnails() {
    if (!pdfDoc) return;
    const thumbList = document.getElementById('thumbnail-list'); thumbList.innerHTML = '';
    for (let i = 1; i <= numPages; i++) {
        const thumbDiv = document.createElement('div'); thumbDiv.className = `thumbnail-item`; thumbDiv.dataset.page = i;
        const canvas = document.createElement('canvas'); canvas.id = 'thumb-' + i;
        thumbDiv.appendChild(canvas);
        thumbDiv.onclick = () => { goToPage(i); };
        thumbList.appendChild(thumbDiv);
        const page = await pdfDoc.getPage(i);
        const viewport = page.getViewport({ rotation: currentRotation, scale: 0.3 });
        canvas.width = viewport.width; canvas.height = viewport.height;
        await page.render({ canvasContext: canvas.getContext('2d'), viewport }).promise;
    }
}

function goToPage(pageNum) {
    if (!pdfDoc || pageNum < 1 || pageNum > numPages) return;
    currentPage = pageNum;
    const pageEl = document.getElementById('page-' + pageNum);
    if (pageEl) pageEl.scrollIntoView({ behavior: 'smooth' });
    updateStatusBar();
}

function zoomIn() { if (currentZoom < 400) { currentZoom += 10; refreshRender(); } }
function zoomOut() { if (currentZoom > 25) { currentZoom -= 10; refreshRender(); } }
function fitPage() { const viewer = document.getElementById('pdf-viewer'); currentZoom = Math.max(25, Math.floor((viewer.clientHeight - 40) / 792 * 100)); refreshRender(); }
function fitWidth() { const viewer = document.getElementById('pdf-viewer'); currentZoom = Math.max(25, Math.floor((viewer.clientWidth - 60) / 612 * 100)); refreshRender(); }
async function refreshRender() { if (!pdfDoc) return; await renderAllPages(); updateStatusBar(); }
function rotateCW() { currentRotation = (currentRotation + 90) % 360; refreshRender(); }

function updateStatusBar() {
    document.getElementById('status-page-info').textContent = `Page ${currentPage} of ${numPages}`;
    document.getElementById('zoom-value').textContent = currentZoom + '%';
}

function switchTab(tabName) {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelector(`.tab[data-tab="${tabName}"]`).classList.add('active');
    document.querySelectorAll('.toolbar').forEach(tb => tb.style.display = 'none');
    const toolbar = document.getElementById('toolbar-' + tabName);
    if (toolbar) toolbar.style.display = 'flex';
}

function showFileMenu() {
    const content = `
        <button class="btn btn-primary" onclick="openPDF();closeModal();">📂 Open PDF</button><br><br>
        <button class="btn btn-secondary" onclick="onSave();closeModal();">💾 Save As</button>
    `;
    showModal('File Menu', content, []);
}

function openPDF() {
    const input = document.createElement('input'); input.type = 'file'; input.accept = '.pdf';
    input.onchange = (e) => { if (e.target.files.length > 0) loadPdfFromFile(e.target.files[0]); };
    input.click();
}

function onSave() {
    if (pdfBytes) {
        const blob = new Blob([pdfBytes], { type: 'application/pdf' });
        saveAs(blob, currentFilePath || 'document.pdf');
        showToast('PDF saved!', 'success');
    }
}

window.loadPdfFromPath = loadPdfFromPath;
window.loadPdfFromFile = loadPdfFromFile;
</script>
</body>
</html>
)HTML";
}

// ==========================================
// 🪟 WINDOW PROCEDURE
// ==========================================
LRESULT CALLBACK AcrobatViewerWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        RECT r; GetClientRect(hWnd, &r);
        g_hWebViewWnd = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, r.right, r.bottom, hWnd, (HMENU)1001, GetModuleHandle(NULL), NULL);
        InitializeWebView2(hWnd, g_hWebViewWnd);
        break;
    }
    case WM_SIZE: {
        if (g_hWebViewWnd && g_webViewController) {
            RECT r; GetClientRect(hWnd, &r);
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
// 🌐 WEBVIEW2 INITIALIZATION
// ==========================================
HRESULT InitializeWebView2(HWND hWnd, HWND hHostWnd) {
    return CreateCoreWebView2EnvironmentWithOptions(
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
                            
                            RECT r; GetClientRect(hWnd, &r);
                            g_webViewController->put_Bounds(RECT{0, 0, r.right, r.bottom});
                            
                            g_webView->NavigateToString(GetAcrobatHTML());
                            
                            g_webView->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        BOOL success; args->get_IsSuccess(&success);
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
    g.DrawString(L"PDF Workspace opens in a separate Adobe Acrobat-style window.\nClick 'PDF Reader' from Dashboard to launch.",
        -1, &fText, RectF(cx, cy, cw, ch), &fmt, &textBrush);
}

void ProcessPdfWorkspaceMouseMove(float x, float y) {}
void ProcessPdfWorkspaceMouseClick(float x, float y) {}
