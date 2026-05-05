// tab_pdf_workspace.cpp
// Adobe Acrobat Style PDF Workspace with HTML/CSS/JS UI in WebView2

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
// 🎨 HTML/CSS/JS UI (Adobe Acrobat Clone)
// ==========================================
const wchar_t* GetAcrobatHTML() {
    return LR"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>PDF Workspace</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
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
}

* { margin: 0; padding: 0; box-sizing: border-box; }
body { 
    font-family: 'Segoe UI', system-ui, sans-serif;
    height: 100vh; overflow: hidden; display: flex; flex-direction: column;
    user-select: none;
}

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
}

/* Page Thumbnails */
.thumbnail-list { display: flex; flex-direction: column; gap: 8px; }
.thumbnail-item {
    background: white; border: 1px solid var(--border); border-radius: 4px;
    padding: 6px; cursor: pointer; transition: all 0.15s;
}
.thumbnail-item:hover { border-color: var(--purple); box-shadow: 0 1px 4px rgba(0,0,0,0.1); }
.thumbnail-item.active { border-color: var(--purple); border-width: 2px; }
.thumbnail-item canvas { width: 100%; height: auto; border-radius: 2px; }
.thumbnail-item .page-num {
    text-align: center; font-size: 10px; color: var(--text-secondary); margin-top: 4px;
}

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

/* ============== LOADING ============== */
.loading-overlay {
    display: none; position: fixed; top: 50%; left: 50%;
    transform: translate(-50%, -50%); background: rgba(0,0,0,0.8);
    color: white; padding: 16px 32px; border-radius: 8px;
    font-size: 14px; z-index: 1000;
}
.loading-overlay.show { display: block; }
</style>
</head>
<body>

<!-- ==================== RIBBON ==================== -->
<div class="ribbon">
    <div class="tab-bar">
        <div class="file-btn" onclick="onFileClick()">📄 File</div>
        <div class="tab active" data-tab="home" onclick="switchTab('home')">Home</div>
        <div class="tab" data-tab="tools" onclick="switchTab('tools')">Tools</div>
        <div class="tab" data-tab="edit" onclick="switchTab('edit')">Edit</div>
        <div class="tab" data-tab="comment" onclick="switchTab('comment')">Comment</div>
        <div class="tab" data-tab="view" onclick="switchTab('view')">View</div>
        <div class="tab" data-tab="protect" onclick="switchTab('protect')">Protect</div>
        <div class="tab-spacer"></div>
        <div class="quick-actions">
            <button class="icon-btn" title="Undo" onclick="onUndo()">↩</button>
            <button class="icon-btn" title="Redo" onclick="onRedo()">↪</button>
            <button class="icon-btn" title="Save" onclick="onSave()">💾</button>
            <button class="icon-btn" title="Print" onclick="onPrint()">🖨</button>
        </div>
    </div>
    
    <!-- Home Tab Toolbar -->
    <div class="toolbar" id="toolbar-home">
        <div class="tool-group">
            <div class="tool-item" onclick="setTool('hand')" id="tool-hand">
                <span class="tool-icon">✋</span>
                <span class="tool-label">Hand</span>
            </div>
            <div class="tool-item" onclick="setTool('select')" id="tool-select">
                <span class="tool-icon">👆</span>
                <span class="tool-label">Select</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="zoomOut()">
                <span class="tool-icon">🔍</span>
                <span class="tool-label">Zoom Out</span>
            </div>
            <div class="tool-item" onclick="fitPage()">
                <span class="tool-icon">📄</span>
                <span class="tool-label">Fit Page</span>
            </div>
            <div class="tool-item" onclick="fitWidth()">
                <span class="tool-icon">↔</span>
                <span class="tool-label">Fit Width</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="rotateCW()">
                <span class="tool-icon">↻</span>
                <span class="tool-label">Rotate</span>
            </div>
            <div class="tool-item" onclick="extractPages()">
                <span class="tool-icon">📤</span>
                <span class="tool-label">Extract</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="exportPDF()">
                <span class="tool-icon">💾</span>
                <span class="tool-label">Export PDF</span>
            </div>
            <div class="tool-item" onclick="onPrint()">
                <span class="tool-icon">🖨</span>
                <span class="tool-label">Print</span>
            </div>
        </div>
    </div>
    
    <!-- Edit Tab Toolbar (hidden by default) -->
    <div class="toolbar" id="toolbar-edit" style="display:none;">
        <div class="tool-group">
            <div class="tool-item" onclick="editText()">
                <span class="tool-icon">✏️</span>
                <span class="tool-label">Edit Text</span>
            </div>
            <div class="tool-item" onclick="addImage()">
                <span class="tool-icon">🖼</span>
                <span class="tool-label">Add Image</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="addLink()">
                <span class="tool-icon">🔗</span>
                <span class="tool-label">Add Link</span>
            </div>
            <div class="tool-item" onclick="cropPage()">
                <span class="tool-icon">✂️</span>
                <span class="tool-label">Crop</span>
            </div>
        </div>
    </div>
    
    <!-- View Tab Toolbar (hidden by default) -->
    <div class="toolbar" id="toolbar-view" style="display:none;">
        <div class="tool-group">
            <div class="tool-item" onclick="toggleReadMode()">
                <span class="tool-icon">📖</span>
                <span class="tool-label">Read Mode</span>
            </div>
            <div class="tool-item" onclick="toggleFullScreen()">
                <span class="tool-icon">🖥</span>
                <span class="tool-label">Full Screen</span>
            </div>
        </div>
        <div class="tool-separator"></div>
        <div class="tool-group">
            <div class="tool-item" onclick="toggleTheme()">
                <span class="tool-icon">🌓</span>
                <span class="tool-label">Theme</span>
            </div>
        </div>
    </div>
</div>

<!-- ==================== MAIN CONTENT ==================== -->
<div class="main-content">
    <!-- Sidebar -->
    <div class="sidebar" id="sidebar">
        <div class="sidebar-strip">
            <div class="strip-icon active" data-panel="pages" onclick="switchSidebar('pages')" title="Pages">📑</div>
            <div class="strip-icon" data-panel="bookmarks" onclick="switchSidebar('bookmarks')" title="Bookmarks">🔖</div>
            <div class="strip-icon" data-panel="annotations" onclick="switchSidebar('annotations')" title="Annotations">💬</div>
            <div class="strip-icon" data-panel="attachments" onclick="switchSidebar('attachments')" title="Attachments">📎</div>
        </div>
        <div class="sidebar-panel" id="sidebar-content">
            <h3>Pages</h3>
            <div class="thumbnail-list" id="thumbnail-list"></div>
            <div id="bookmarks-list" style="display:none;"></div>
            <div id="annotations-list" style="display:none; color:var(--text-secondary); font-size:12px;">
                <p>No annotations yet.</p>
            </div>
            <div id="attachments-list" style="display:none; color:var(--text-secondary); font-size:12px;">
                <p>No attachments.</p>
            </div>
        </div>
        </div>
    
    <!-- PDF Viewer -->
    <div class="pdf-viewer" id="pdf-viewer">
        <div class="pdf-container" id="pdf-container">
            <div style="color: var(--text-secondary); font-size: 16px; padding: 40px;">
                📄 No PDF loaded. Use File menu to open a PDF.
            </div>
        </div>
    </div>
</div>

<!-- ==================== STATUS BAR ==================== -->
<div class="status-bar">
    <span class="status-item">📄 <span id="status-page-info">Page 1 of 1</span></span>
    <span class="status-item">📏 <span id="status-size">8.5 x 11 in</span></span>
    <div class="zoom-control">
        <button onclick="zoomOut()">−</button>
        <span class="zoom-value" id="zoom-value" onclick="showZoomMenu()">100%</span>
        <button onclick="zoomIn()">+</button>
    </div>
</div>

<!-- ==================== LOADING OVERLAY ==================== -->
<div class="loading-overlay" id="loading-overlay">Loading PDF...</div>

<!-- ==================== JAVASCRIPT ==================== -->
<script>
// ============ PDF.JS SETUP ============
pdfjsLib.GlobalWorkerOptions.workerSrc = 'https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.worker.min.js';

let pdfDoc = null;
let currentPage = 1;
let currentZoom = 100;
let currentRotation = 0;
let numPages = 0;
let currentTool = 'hand';
let currentSidebar = 'pages';
let zoomMode = 'custom'; // 'page-fit', 'page-width', 'custom'

// ============ INITIALIZATION ============
document.addEventListener('DOMContentLoaded', () => {
    setupKeyboardShortcuts();
    // Check for PDF path from host
    checkForPdfPath();
});

// Check if host (C++) sent a PDF path
function checkForPdfPath() {
    // The host can call loadPdfFromPath via postMessage or direct call
    if (window.chrome && window.chrome.webview) {
        window.chrome.webview.addEventListener('message', (event) => {
            if (event.data && event.data.type === 'loadPdf') {
                loadPdfFromPath(event.data.path);
            }
        });
    }
}

// ============ PDF LOADING ============
function loadPdfFromPath(path) {
    showLoading(true);
    
    // Convert Windows path to file URL
    let fileUrl = path.replace(/\\/g, '/');
    if (!fileUrl.startsWith('file:///')) {
        fileUrl = 'file:///' + fileUrl;
    }
    
    pdfjsLib.getDocument({ url: fileUrl, withCredentials: false }).promise
        .then((pdf) => {
            pdfDoc = pdf;
            numPages = pdf.numPages;
            currentPage = 1;
            currentRotation = 0;
            currentZoom = 100;
            
            renderAllPages();
            renderThumbnails();
            updateStatusBar();
            showLoading(false);
        })
        .catch((error) => {
            console.error('Error loading PDF:', error);
            document.getElementById('pdf-container').innerHTML = 
                '<div style="color:red;padding:40px;">❌ Error loading PDF: ' + error.message + '</div>';
            showLoading(false);
        });
}

// Load PDF from file input
function loadPdfFromFile(file) {
    showLoading(true);
    const reader = new FileReader();
    reader.onload = (e) => {
        const typedarray = new Uint8Array(e.target.result);
        pdfjsLib.getDocument({ data: typedarray }).promise
            .then((pdf) => {
                pdfDoc = pdf;
                numPages = pdf.numPages;
                currentPage = 1;
                currentRotation = 0;
                currentZoom = 100;
                
                renderAllPages();
                renderThumbnails();
                updateStatusBar();
                showLoading(false);
            })
            .catch((error) => {
                console.error('Error loading PDF:', error);
                showLoading(false);
            });
    };
    reader.readAsArrayBuffer(file);
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
    
    // Scroll to current page
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
    
    // Apply zoom
    const scale = currentZoom / 100;
    const scaledViewport = page.getViewport({
        rotation: currentRotation,
        scale: viewport.scale * scale
    });
    
    canvas.width = scaledViewport.width;
    canvas.height = scaledViewport.height;
    
    const renderContext = {
        canvasContext: context,
        viewport: scaledViewport
    };
    
    await page.render(renderContext).promise;
}

// ============ THUMBNAILS ============
async function renderThumbnails() {
    if (!pdfDoc) return;
    
    const thumbList = document.getElementById('thumbnail-list');
    thumbList.innerHTML = '';
    
    for (let i = 1; i <= numPages; i++) {
        const thumbDiv = document.createElement('div');
        thumbDiv.className = 'thumbnail-item';
        thumbDiv.dataset.page = i;
        thumbDiv.onclick = () => goToPage(i);
        
        const canvas = document.createElement('canvas');
        canvas.id = 'thumb-' + i;
        thumbDiv.appendChild(canvas);
        
        const pageNum = document.createElement('div');
        pageNum.className = 'page-num';
        pageNum.textContent = 'Page ' + i;
        thumbDiv.appendChild(pageNum);
        
        thumbList.appendChild(thumbDiv);
        
        // Render thumbnail
        const page = await pdfDoc.getPage(i);
        const viewport = page.getViewport({ rotation: currentRotation, scale: 0.3 });
        const thumbCanvas = document.getElementById('thumb-' + i);
        if (thumbCanvas) {
            thumbCanvas.width = viewport.width;
            thumbCanvas.height = viewport.height;
            const ctx = thumbCanvas.getContext('2d');
            await page.render({ canvasContext: ctx, viewport: viewport }).promise;
        }
    }
    
    updateThumbnailHighlight();
}

function updateThumbnailHighlight() {
    document.querySelectorAll('.thumbnail-item').forEach(item => {
        item.classList.toggle('active', parseInt(item.dataset.page) === currentPage);
    });
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

function nextPage() {
    if (currentPage < numPages) goToPage(currentPage + 1);
}

function prevPage() {
    if (currentPage > 1) goToPage(currentPage - 1);
}

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
    // Calculate zoom to fit page
    const viewer = document.getElementById('pdf-viewer');
    const availableHeight = viewer.clientHeight - 40;
    // Approximate fit
    currentZoom = Math.floor((availableHeight / 792) * 100); // Letter size approximation
    currentZoom = Math.max(25, Math.min(400, currentZoom));
    refreshRender();
}

function fitWidth() {
    zoomMode = 'page-width';
    const viewer = document.getElementById('pdf-viewer');
    const availableWidth = viewer.clientWidth - 60;
    currentZoom = Math.floor((availableWidth / 612) * 100); // Letter size approximation
    currentZoom = Math.max(25, Math.min(400, currentZoom));
    refreshRender();
}

function setZoom(value) {
    currentZoom = parseInt(value);
    zoomMode = 'custom';
    refreshRender();
}

function showZoomMenu() {
    const zooms = ['25%', '50%', '75%', '100%', '125%', '150%', '200%', '300%', '400%'];
    const current = currentZoom + '%';
    const menu = zooms.map(z => z === current ? `[${z}]` : z).join('\n');
    const choice = prompt('Select zoom level:\n\n' + menu, current);
    if (choice) {
        const val = parseInt(choice.replace('%', ''));
        if (val >= 25 && val <= 400) setZoom(val);
    }
}

function refreshRender() {
    if (!pdfDoc) return;
    renderAllPages().then(() => {
        updateStatusBar();
        updateThumbnailHighlight();
    });
}

// ============ ROTATION ============
function rotateCW() {
    currentRotation = (currentRotation + 90) % 360;
    refreshRender();
}

// ============ TOOLS ============
function setTool(tool) {
    currentTool = tool;
    document.querySelectorAll('.tool-item').forEach(item => {
        item.style.background = '';
    });
    const toolEl = document.getElementById('tool-' + tool);
    if (toolEl) toolEl.style.background = 'var(--selected-bg)';
}

// ============ TAB SWITCHING ============
function switchTab(tabName) {
    // Update tab styling
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    document.querySelector(`.tab[data-tab="${tabName}"]`).classList.add('active');
    
    // Show/hide toolbars
    document.querySelectorAll('.toolbar').forEach(tb => tb.style.display = 'none');
    const toolbar = document.getElementById('toolbar-' + tabName);
    if (toolbar) toolbar.style.display = 'flex';
}

// ============ SIDEBAR ============
function switchSidebar(panel) {
    currentSidebar = panel;
    
    // Update strip icons
    document.querySelectorAll('.strip-icon').forEach(icon => {
        icon.classList.toggle('active', icon.dataset.panel === panel);
    });
    
    // Update panel content
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
        document.querySelector('#sidebar-content h3').textContent = info.title;
        document.querySelector('#sidebar-content h3').style.display = 'block';
        const listEl = document.getElementById(info.list);
        if (listEl) listEl.style.display = '';
    }
}

function toggleSidebar() {
    document.getElementById('sidebar').classList.toggle('collapsed');
}

// ============ FILE OPERATIONS ============
function onFileClick() {
    // Create file input
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.pdf';
    input.onchange = (e) => {
        if (e.target.files.length > 0) {
            loadPdfFromFile(e.target.files[0]);
        }
    };
    input.click();
}

function onSave() {
    alert('💾 Save PDF - This feature would save the current PDF with any modifications.');
}

function onPrint() {
    window.print();
}

function onUndo() {
    console.log('Undo action');
}

function onRedo() {
    console.log('Redo action');
}

function exportPDF() {
    alert('📤 Export PDF - This feature would export the PDF in different formats.');
}

// ============ EDIT OPERATIONS ============
function editText() { alert('✏️ Edit Text feature'); }
function addImage() { alert('🖼 Add Image feature'); }
function addLink() { alert('🔗 Add Link feature'); }
function cropPage() { alert('✂️ Crop Page feature'); }
function extractPages() { alert('📤 Extract Pages feature'); }

// ============ VIEW OPERATIONS ============
function toggleReadMode() {
    alert('📖 Read Mode toggled');
}

function toggleFullScreen() {
    if (document.fullscreenElement) {
        document.exitFullscreen();
    } else {
        document.documentElement.requestFullscreen();
    }
}

function toggleTheme() {
    document.body.classList.toggle('dark-theme');
}

// ============ STATUS BAR ============
function updateStatusBar() {
    document.getElementById('status-page-info').textContent = 
        `Page ${currentPage} of ${numPages}`;
    document.getElementById('status-size').textContent = 
        `8.5 × 11 in`; // Could be dynamic based on PDF
    document.getElementById('zoom-value').textContent = currentZoom + '%';
}

// ============ KEYBOARD SHORTCUTS ============
function setupKeyboardShortcuts() {
    document.addEventListener('keydown', (e) => {
        // Don't capture if in input
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
        
        switch(e.key) {
            case 'ArrowDown': case 'PageDown': nextPage(); e.preventDefault(); break;
            case 'ArrowUp': case 'PageUp': prevPage(); e.preventDefault(); break;
            case 'Home': goToPage(1); e.preventDefault(); break;
            case 'End': goToPage(numPages); e.preventDefault(); break;
            case '+': case '=': zoomIn(); e.preventDefault(); break;
            case '-': zoomOut(); e.preventDefault(); break;
            case '0': if (e.ctrlKey) fitPage(); e.preventDefault(); break;
            case 'f': if (e.ctrlKey) e.preventDefault(); break; // Search
            case 'F4': toggleSidebar(); e.preventDefault(); break;
            case 'r': if (e.ctrlKey && e.shiftKey) { rotateCW(); e.preventDefault(); } break;
        }
    });
}

// ============ LOADING ============
function showLoading(show) {
    const overlay = document.getElementById('loading-overlay');
    overlay.classList.toggle('show', show);
}

// ============ SCROLL TRACKING ============
document.getElementById('pdf-viewer').addEventListener('scroll', () => {
    if (!pdfDoc) return;
    
    // Find which page is most visible
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

// Expose functions globally
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
    // Just draw a subtle border to indicate frame
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

        // Create WebView2 host window (fills entire client area)
        g_hWebViewWnd = CreateWindowExW(
            0, L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, r.right, r.bottom,
            hWnd, (HMENU)1001, GetModuleHandle(NULL), NULL
        );

        // Initialize WebView2
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
        // Cleanup WebView2
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
    // Create environment
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hWnd, hHostWnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) return result;
                
                g_webViewEnv = env;
                
                // Create controller
                env->CreateCoreWebView2Controller(
                    hHostWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hWnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result)) return result;
                            
                            g_webViewController = controller;
                            g_webViewController->get_CoreWebView2(&g_webView);
                            
                            // Configure WebView2 settings
                            ICoreWebView2Settings* settings;
                            g_webView->get_Settings(&settings);
                            settings->put_IsScriptEnabled(TRUE);
                            settings->put_IsWebMessageEnabled(TRUE);
                            settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                            settings->put_AreDevToolsEnabled(TRUE);
                            
                            // Set window bounds
                            RECT r;
                            GetClientRect(hWnd, &r);
                            g_webViewController->put_Bounds(RECT{0, 0, r.right, r.bottom});
                            
                            // Load HTML content
                            g_webView->NavigateToString(GetAcrobatHTML());
                            
                            // Setup navigation completed handler
                            g_webView->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        BOOL success;
                                        args->get_IsSuccess(&success);
                                        if (success) {
                                            g_webViewInitialized = true;
                                            
                                            // If there's a PDF path, load it
                                            if (!g_acrobatPdfPath.empty()) {
                                                std::wstring escapedPath = g_acrobatPdfPath;
                                                size_t pos = 0;
                                                while ((pos = escapedPath.find(L"\\", pos)) != std::wstring::npos) {
                                                    escapedPath.replace(pos, 1, L"\\\\");
                                                    pos += 2;
                                                }
                                                
                                                std::wstring script = 
                                                    L"loadPdfFromPath('" + escapedPath + L"');";
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

    // If window already exists
    if (g_hAcrobatWnd != NULL) {
        SetForegroundWindow(g_hAcrobatWnd);
        ShowWindow(g_hAcrobatWnd, SW_SHOW);
        
        // Load PDF if WebView2 is initialized
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

    // Register window class
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

    // Create window
    g_hAcrobatWnd = CreateWindowExW(
        0, L"AcrobatWorkspaceClass", L"RasFocus - PDF Workspace",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        (int)(1200 * g_scaleFactor), (int)(800 * g_scaleFactor),
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    // Set window icon
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
