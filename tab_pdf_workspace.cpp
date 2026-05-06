// tab_pdf_workspace.cpp
// Professional PDF Workspace Architecture - Sumatra PDF Style

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
// 🎨 HTML/CSS/JS UI - SPLIT INTO 33 SMALL PARTS (wstringstream) TO FIX C2026
// ==========================================
wstring GetAcrobatHTML() {
    std::wstringstream ss;
    
    // --- PART 1: Head & CSS Variables ---
    ss << LR"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>RasFocus PDF Pro</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
<script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/tesseract.js@4/dist/tesseract.min.js"></script>
<link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200" rel="stylesheet" />
<style>
:root {
    --brand-red: #d32f2f;
    --bg-dark: #202020;
    --bg-tabs: #2a2a2a;
    --bg-panel: #f5f5f5;
    --bg-doc: #cccccc;
    --border-color: #dcdcdc;
    --text-primary: #111111;
    --text-muted: #555555;
    --selected-bg: #ffebee;
    --topbar-height: 36px;
    --tabbar-height: 32px;
    --toolbar-height: 40px;
    --right-sidebar-width: 150px;
}
)HTML";

    // --- PART 2: Global Styles & Scrollbars ---
    ss << LR"HTML(
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', Tahoma, sans-serif; height: 100vh; overflow: hidden; display: flex; flex-direction: column; background: var(--bg-doc); user-select: none; }
.material-symbols-outlined { font-variation-settings: 'FILL' 0, 'wght' 400, 'GRAD' 0, 'opsz' 24; font-size: 20px; }

::-webkit-scrollbar { width: 8px; height: 8px; }
::-webkit-scrollbar-track { background: transparent; }
::-webkit-scrollbar-thumb { background: #bbb; border-radius: 4px; }
::-webkit-scrollbar-thumb:hover { background: #999; }
)HTML";

    // --- PART 3: Toasts ---
    ss << LR"HTML(
.toast-container { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); z-index: 9999; display: flex; flex-direction: column; gap: 8px; }
.toast { padding: 10px 20px; border-radius: 6px; color: #fff; background: #333; font-size: 13px; font-weight: 500; box-shadow: 0 4px 10px rgba(0,0,0,0.3); animation: fadeInUp 0.3s ease; }
@keyframes fadeInUp { from { transform: translateY(20px); opacity: 0; } to { transform: translateY(0); opacity: 1; } }
)HTML";

    // --- PART 4: Modals ---
    ss << LR"HTML(
.modal-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.6); z-index: 1000; justify-content: center; align-items: center; }
.modal-overlay.show { display: flex; }
.modal { background: #fff; border-radius: 6px; padding: 20px; min-width: 320px; box-shadow: 0 10px 25px rgba(0,0,0,0.3); }
.modal h3 { font-size: 15px; margin-bottom: 15px; font-weight: 600; }
.modal input { width: 100%; padding: 8px; margin-bottom: 15px; border: 1px solid var(--border-color); border-radius: 4px; outline: none; }
.modal-actions { display: flex; gap: 10px; justify-content: flex-end; }
.btn { padding: 7px 16px; border: none; border-radius: 4px; cursor: pointer; font-size: 13px; font-weight: 600; transition: 0.2s; }
.btn-primary { background: var(--brand-red); color: #fff; }
.btn-primary:hover { background: #b71c1c; }
.btn-secondary { background: transparent; border: 1px solid #aaa; color: var(--text-primary); }
.btn-secondary:hover { background: #eee; }
)HTML";

    // --- PART 5: Topbar ---
    ss << LR"HTML(
.topbar { height: var(--topbar-height); background: var(--bg-dark); display: flex; align-items: center; padding: 0 16px; color: #fff; border-bottom: 1px solid #111; }
.topbar-menu { display: flex; gap: 16px; font-size: 13px; }
.topbar-item { cursor: pointer; opacity: 0.8; transition: 0.2s; padding: 4px 8px; border-radius: 4px; }
.topbar-item:hover { opacity: 1; background: rgba(255,255,255,0.1); }
.topbar-actions { display: flex; gap: 10px; margin-left: auto; }
.topbar-icon { cursor: pointer; opacity: 0.8; padding: 4px; border-radius: 4px; }
.topbar-icon:hover { opacity: 1; background: rgba(255,255,255,0.1); }
)HTML";

    // --- PART 6: Tabbar ---
    ss << LR"HTML(
.tabbar { height: var(--tabbar-height); background: var(--bg-tabs); display: flex; align-items: flex-end; padding-left: 8px; overflow-x: auto; border-bottom: 1px solid #111; }
.pdf-tab { height: 28px; background: #444; color: #ccc; padding: 0 10px; display: flex; align-items: center; gap: 8px; font-size: 12px; border-radius: 6px 6px 0 0; margin-right: 3px; cursor: pointer; max-width: 220px; transition: 0.1s; }
.pdf-tab:hover { background: #555; color: #fff; }
.pdf-tab.active { background: #fff; color: var(--text-primary); font-weight: 600; height: 31px; }
.pdf-tab .close-tab { font-size: 14px; cursor: pointer; border-radius: 50%; width: 16px; height: 16px; display: flex; align-items: center; justify-content: center; }
.pdf-tab.active .close-tab:hover { background: rgba(0,0,0,0.1); color: var(--brand-red); }
.add-tab-btn { color: #fff; opacity: 0.7; cursor: pointer; padding: 0 12px; font-weight: bold; font-size: 18px; line-height: 28px; }
.add-tab-btn:hover { opacity: 1; }
)HTML";

    // --- PART 7: Toolbar & Sidebar CSS ---
    ss << LR"HTML(
.toolbar { height: var(--toolbar-height); background: #fff; border-bottom: 1px solid var(--border-color); display: flex; align-items: center; padding: 0 16px; gap: 8px; font-size: 13px; }
.tool-btn { display: flex; align-items: center; justify-content: center; width: 30px; height: 30px; cursor: pointer; border-radius: 4px; color: var(--text-primary); transition: 0.15s; }
.tool-btn:hover { background: #eee; }
.tool-btn.active { background: var(--selected-bg); color: var(--brand-red); }
.divider { width: 1px; height: 18px; background: var(--border-color); margin: 0 6px; }

.workspace { flex: 1; display: flex; overflow: hidden; position: relative; }

.right-sidebar { width: var(--right-sidebar-width); background: var(--bg-panel); border-left: 1px solid var(--border-color); display: flex; flex-direction: column; overflow-y: auto; flex-shrink: 0; }
.sidebar-header { padding: 12px; font-size: 11px; font-weight: 700; color: var(--text-muted); border-bottom: 1px solid var(--border-color); text-align: center; }
.tool-pane-btn { display: flex; flex-direction: column; align-items: center; gap: 4px; padding: 12px 6px; cursor: pointer; border-bottom: 1px solid var(--border-color); transition: 0.15s; text-align: center; }
.tool-pane-btn:hover { background: #ebebeb; }
.tool-pane-btn .material-symbols-outlined { color: var(--text-muted); font-size: 22px; transition: 0.15s; }
.tool-pane-btn:hover .material-symbols-outlined { color: var(--brand-red); }
.tool-pane-text { font-size: 11px; font-weight: 500; }
)HTML";

    // --- PART 8: PDF Viewer Area CSS ---
    ss << LR"HTML(
.pdf-viewer-area { flex: 1; overflow-y: auto; display: flex; justify-content: center; padding: 24px; background: var(--bg-doc); }
.pdf-container { display: flex; flex-direction: column; gap: 15px; align-items: center; width: 100%; cursor: default; }
.pdf-page-wrapper { background: #fff; box-shadow: 0 2px 6px rgba(0,0,0,0.2); position: relative; }
.pdf-page-wrapper canvas { display: block; }

.loading-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.7); z-index: 2000; flex-direction: column; justify-content: center; align-items: center; color: #fff; font-size: 13px; font-weight: 500; }
.loading-overlay.show { display: flex; }
.spinner { border: 3px solid rgba(255,255,255,0.2); border-top: 3px solid #fff; border-radius: 50%; width: 32px; height: 32px; animation: spin 0.8s linear infinite; margin-bottom: 12px; }
@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
)HTML";

    // --- PART 9: Night Mode & Read Mode CSS ---
    ss << LR"HTML(
body.night-mode .pdf-viewer-area { background: #1a1a1a !important; }
body.night-mode .toolbar { background: #252525 !important; border-color: #111 !important; color: #ddd !important; }
body.night-mode .toolbar .tool-btn { color: #ddd !important; }
body.night-mode .toolbar .tool-btn:hover { background: #333 !important; }
body.night-mode .right-sidebar { background: #252525 !important; border-color: #111 !important; }
body.night-mode .sidebar-header { color: #888 !important; border-color: #333 !important; }
body.night-mode .tool-pane-btn { border-color: #333 !important; color: #ddd !important; }
body.night-mode .tool-pane-btn:hover { background: #333 !important; }
body.night-mode .pdf-page-wrapper { box-shadow: 0 4px 15px rgba(0,0,0,0.6); }

body.read-mode .toolbar, body.read-mode .right-sidebar, body.read-mode .tabbar { display: none !important; }
.dom-annotation { position: absolute; pointer-events: none; z-index: 5; }
</style>
</head>
<body>
)HTML";

    // --- PART 10: HTML Structural Overlays ---
    ss << LR"HTML(
<div class="toast-container" id="toast-container"></div>
<div class="loading-overlay" id="loading-overlay"><div class="spinner"></div><div id="loading-text">Processing...</div></div>
<div class="modal-overlay" id="modal-overlay"><div class="modal" id="modal-content"></div></div>

<div class="topbar">
    <div class="topbar-menu">
        <div class="topbar-item" onclick="document.getElementById('fileInput').click()">File > Open</div>
        <div class="topbar-item" onclick="downloadCurrentPDF()">File > Save As</div>
    </div>
    <div class="topbar-actions">
        <span class="material-symbols-outlined topbar-icon" onclick="toggleNightMode()" title="Night Mode">dark_mode</span>
        <span class="material-symbols-outlined topbar-icon" onclick="toggleReadMode()" title="Read Mode" id="read-mode-icon">menu_book</span>
    </div>
</div>
)HTML";

    // --- PART 11: Tabbar & Toolbar HTML ---
    ss << LR"HTML(
<div class="tabbar" id="tabbar-strip"></div>

<div class="toolbar">
    <div class="tool-btn active" onclick="setStudyTool('pointer')" id="tool-pointer" title="Pointer"><span class="material-symbols-outlined">pan_tool</span></div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="setStudyTool('highlight')" id="tool-highlight" title="Highlight"><span class="material-symbols-outlined" style="color: #F57F17;">format_ink_highlighter</span></div>
    <div class="tool-btn" onclick="setStudyTool('note')" id="tool-note" title="Sticky Note"><span class="material-symbols-outlined" style="color: #388E3C;">speaker_notes</span></div>
    <div class="tool-btn" onclick="setStudyTool('link')" id="tool-link" title="Insert Link"><span class="material-symbols-outlined" style="color: #1976D2;">link</span></div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="zoomOut()"><span class="material-symbols-outlined">remove</span></div>
    <span id="zoom-text" style="font-size:12px;width:40px;text-align:center;font-weight:600;">100%</span>
    <div class="tool-btn" onclick="zoomIn()"><span class="material-symbols-outlined">add</span></div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="rotatePDF()"><span class="material-symbols-outlined">rotate_right</span></div>
</div>
)HTML";

    // --- PART 12: Workspace HTML ---
    ss << LR"HTML(
<div class="workspace">
    <div class="pdf-viewer-area" id="viewer-area">
        <div class="pdf-container" id="pdf-container">
            <div style="margin-top: 150px; text-align: center; color: var(--text-muted);">
                <span class="material-symbols-outlined" style="font-size: 60px; opacity:0.3;">note_add</span>
                <p style="margin-top: 12px; font-size: 14px;">Double click file or Use + to add PDF tab</p>
            </div>
        </div>
    </div>

    <div class="right-sidebar">
        <div class="sidebar-header">Advanced Panel</div>
        <div class="tool-pane-btn" onclick="uiShowMergeModal()"><span class="material-symbols-outlined">library_add</span><span class="tool-pane-text">Combine PDFs</span></div>
        <div class="tool-pane-btn" onclick="uiShowSplitModal()"><span class="material-symbols-outlined">splitscreen</span><span class="tool-pane-text">Split PDF</span></div>
        <div class="tool-pane-btn" onclick="uiShowExtractModal()"><span class="material-symbols-outlined">file_upload</span><span class="tool-pane-text">Extract Pages</span></div>
        <div class="tool-pane-btn" onclick="uiShowDeleteModal()"><span class="material-symbols-outlined">delete</span><span class="tool-pane-text">Delete Pages</span></div>
        <div class="tool-pane-btn" onclick="actionPDFtoImage()"><span class="material-symbols-outlined">image</span><span class="tool-pane-text">Export as ZIP</span></div>
        <div class="tool-pane-btn" onclick="actionPDFtoText()"><span class="material-symbols-outlined">article</span><span class="tool-pane-text">Export as TXT</span></div>
        <div class="tool-pane-btn" onclick="actionPerformOCR()"><span class="material-symbols-outlined">document_scanner</span><span class="tool-pane-text">OCR Scanner</span></div>
        <div class="tool-pane-btn" onclick="actionAddWatermark()"><span class="material-symbols-outlined">branding_watermark</span><span class="tool-pane-text">Add Watermark</span></div>
    </div>
</div>
<input type="file" id="fileInput" accept=".pdf" style="display:none;" onchange="handleFileOpen(event)">
)HTML";

    // --- PART 13: JS Setup & Utilities ---
    ss << LR"HTML(
<script>
let openedTabs = []; let activeTabId = null;

function showToast(msg) {
    const c = document.getElementById('toast-container');
    const t = document.createElement('div'); t.className = 'toast'; t.textContent = msg;
    c.appendChild(t); setTimeout(() => t.remove(), 3000);
}
function showLoading(show, txt="Processing...") {
    document.getElementById('loading-overlay').classList.toggle('show', show);
    document.getElementById('loading-text').textContent = txt;
}
function showModal(title, html) {
    document.getElementById('modal-content').innerHTML = `<h3>${title}</h3>${html}`;
    document.getElementById('modal-overlay').classList.add('show');
}
function closeModal() { document.getElementById('modal-overlay').classList.remove('show'); }
)HTML";

    // --- PART 14: JS Native Save Feature ---
    ss << LR"HTML(
async function saveBytesToFile(blobData, suggestedName, ext="pdf", mime="application/pdf") {
    try {
        if (window.showSaveFilePicker) {
            const handle = await window.showSaveFilePicker({ suggestedName: suggestedName, types: [{ description: 'Document', accept: {[mime]: ['.'+ext]} }] });
            const writable = await handle.createWritable(); await writable.write(blobData); await writable.close();
            showToast("Saved to specific folder successfully!");
        } else { saveAs(blobData, suggestedName); }
    } catch (e) { if(e.name !== 'AbortError') showToast("Save cancelled."); }
}
)HTML";

    // --- PART 15: JS File Loading ---
    ss << LR"HTML(
async function handleFileOpen(e) {
    const f = e.target.files[0]; if (!f) return;
    createNewTab(f.name, new Uint8Array(await f.arrayBuffer()));
}

async function loadPdfFromPath(path) {
    try {
        const res = await fetch('file:///' + path.replace(/\\/g, '/'));
        const pParts = path.split('\\');
        createNewTab(pParts[pParts.length - 1], new Uint8Array(await res.arrayBuffer()));
    } catch (e) { showToast("Failed to load PDF."); }
}
)HTML";

    // --- PART 16: JS Tab Management (Create) ---
    ss << LR"HTML(
async function createNewTab(fileName, uint8Array) {
    try {
        const doc = await pdfjsLib.getDocument({data: uint8Array}).promise;
        const tId = 'tab_' + Date.now();
        openedTabs.push({ id: tId, name: fileName, bytes: uint8Array, pdfjsDoc: doc, zoom: 1.0, rotation: 0, annotations: [] });
        updateTabStrip(); await switchActiveTab(tId);
    } catch(e) { showToast("Invalid PDF file."); }
}
)HTML";

    // --- PART 17: JS Tab Management (Update UI) ---
    ss << LR"HTML(
function updateTabStrip() {
    const strip = document.getElementById('tabbar-strip'); strip.innerHTML = '';
    openedTabs.forEach(t => {
        const el = document.createElement('div'); el.className = `pdf-tab ${t.id === activeTabId ? 'active' : ''}`; el.onclick = () => switchActiveTab(t.id);
        const nameEl = document.createElement('span'); nameEl.textContent = t.name.length > 25 ? t.name.substring(0, 22) + '...' : t.name;
        const closeEl = document.createElement('span'); closeEl.className = 'close-tab'; closeEl.textContent = '×';
        closeEl.onclick = (e) => { e.stopPropagation(); closeTabInstance(t.id); };
        el.appendChild(nameEl); el.appendChild(closeEl); strip.appendChild(el);
    });
    const addBtn = document.createElement('div'); addBtn.className = 'add-tab-btn'; addBtn.textContent = '+';
    addBtn.onclick = () => document.getElementById('fileInput').click();
    strip.appendChild(addBtn);
}
)HTML";

    // --- PART 18: JS Tab Management (Switch & Close) ---
    ss << LR"HTML(
async function switchActiveTab(tId) {
    activeTabId = tId; updateTabStrip();
    const t = openedTabs.find(x => x.id === tId); if (!t) return;
    document.getElementById('zoom-text').textContent = Math.round(t.zoom * 100) + '%';
    await renderActiveViewer();
}

function closeTabInstance(tId) {
    openedTabs = openedTabs.filter(x => x.id !== tId);
    if (activeTabId === tId) activeTabId = openedTabs.length > 0 ? openedTabs[openedTabs.length - 1].id : null;
    updateTabStrip();
    if (activeTabId) switchActiveTab(activeTabId);
    else document.getElementById('pdf-container').innerHTML = `<div style="margin-top:150px;text-align:center;color:var(--text-muted);"><span class="material-symbols-outlined" style="font-size:60px;opacity:0.3;">note_add</span><p style="margin-top:12px;">Click + to add a PDF tab</p></div>`;
}
)HTML";

    // --- PART 19: JS Render Viewer ---
    ss << LR"HTML(
async function renderActiveViewer() {
    const t = openedTabs.find(x => x.id === activeTabId); if (!t) return;
    const c = document.getElementById('pdf-container'); c.innerHTML = '';
    for (let i = 1; i <= t.pdfjsDoc.numPages; i++) {
        const page = await t.pdfjsDoc.getPage(i);
        const vp = page.getViewport({ scale: t.zoom, rotation: t.rotation });
        const wrap = document.createElement('div'); wrap.className = 'pdf-page-wrapper'; wrap.id = `page-${i}`;
        const canvas = document.createElement('canvas'); const ctx = canvas.getContext('2d');
        canvas.height = vp.height; canvas.width = vp.width;
        wrap.appendChild(canvas); c.appendChild(wrap);
        await page.render({ canvasContext: ctx, viewport: vp }).promise;
        redrawActiveAnnotations(i);
    }
}
)HTML";

    // --- PART 20: JS Zoom & Rotate ---
    ss << LR"HTML(
function zoomIn() { const t = openedTabs.find(x => x.id === activeTabId); if (t && t.zoom < 3.0) { t.zoom += 0.2; document.getElementById('zoom-text').textContent = Math.round(t.zoom * 100) + '%'; renderActiveViewer(); } }
function zoomOut() { const t = openedTabs.find(x => x.id === activeTabId); if (t && t.zoom > 0.4) { t.zoom -= 0.2; document.getElementById('zoom-text').textContent = Math.round(t.zoom * 100) + '%'; renderActiveViewer(); } }
function rotatePDF() { const t = openedTabs.find(x => x.id === activeTabId); if (t) { t.rotation = (t.rotation + 90) % 360; renderActiveViewer(); } }
)HTML";

    // --- PART 21: JS Touchpad Zoom Listeners ---
    ss << LR"HTML(
// 🟢 Native Touchpad Pinch-to-Zoom
document.getElementById('viewer-area').addEventListener('wheel', function(e) {
    if (e.ctrlKey) {
        e.preventDefault();
        const t = openedTabs.find(x => x.id === activeTabId); if (!t) return;
        if (e.deltaY < 0) { if (t.zoom < 3.5) t.zoom += 0.1; } 
        else { if (t.zoom > 0.3) t.zoom -= 0.1; }
        document.getElementById('zoom-text').textContent = Math.round(t.zoom * 100) + '%';
        if(window.zoomTimeout) clearTimeout(window.zoomTimeout);
        window.zoomTimeout = setTimeout(renderActiveViewer, 40);
    }
}, { passive: false });
)HTML";

    // --- PART 22: JS Study Tool Toggles ---
    ss << LR"HTML(
let currentStudyTool = 'pointer';
function setStudyTool(tool) {
    if (currentStudyTool === tool) tool = 'pointer';
    if (!tool) tool = 'pointer';
    currentStudyTool = tool;
    
    document.querySelectorAll('.toolbar .tool-btn').forEach(btn => btn.classList.remove('active'));
    document.getElementById('tool-' + tool).classList.add('active');
    document.getElementById('pdf-container').style.cursor = tool === 'pointer' ? 'default' : 'crosshair';
}
)HTML";

    // --- PART 23: JS Annotation DOM Rendering ---
    ss << LR"HTML(
function addDOMAnnotation(pIdx, type, rx, ry, txt="") {
    const wrap = document.getElementById(`page-${pIdx + 1}`); if (!wrap) return;
    const div = document.createElement('div'); div.className = 'dom-annotation';
    div.style.left = (rx * 100) + '%'; div.style.top = (ry * 100) + '%';
    if (type === 'highlight') { div.style.width = '120px'; div.style.height = '15px'; div.style.backgroundColor = 'rgba(253, 216, 53, 0.4)'; div.style.mixBlendMode = 'multiply'; }
    else if (type === 'note') { div.style.padding = '4px'; div.style.backgroundColor = '#fffec8'; div.style.border = '1px solid #dcd777'; div.style.fontSize = '12px'; div.textContent = '📝 ' + txt; }
    else if (type === 'link') { div.style.color = 'blue'; div.style.textDecoration = 'underline'; div.style.fontSize = '12px'; div.style.cursor = 'pointer'; div.style.pointerEvents = 'auto'; div.textContent = '🔗 ' + txt; div.onclick = (e) => { e.stopPropagation(); window.open(txt, '_blank'); }; }
    wrap.appendChild(div);
}
)HTML";

    // --- PART 24: JS Redraw Annotations & Click Handler ---
    ss << LR"HTML(
function redrawActiveAnnotations(pNum) {
    const t = openedTabs.find(x => x.id === activeTabId); if (!t) return;
    t.annotations.forEach(a => { if(a.pageIndex === (pNum - 1) && a.type !== 'image') addDOMAnnotation(a.pageIndex, a.type, a.ratioX, 1 - a.ratioY, a.text); });
}

document.getElementById('pdf-container').addEventListener('click', (e) => {
    const t = openedTabs.find(x => x.id === activeTabId); if (!t || currentStudyTool === 'pointer') return;
    const wrap = e.target.closest('.pdf-page-wrapper'); if (!wrap) return; 
    const pIdx = parseInt(wrap.id.split('-')[1]) - 1;
    const rect = e.target.getBoundingClientRect(); const rx = (e.clientX - rect.left) / rect.width; const ry = 1 - ((e.clientY - rect.top) / rect.height);
    
    if (currentStudyTool === 'highlight') { t.annotations.push({ type: 'highlight', pageIndex: pIdx, ratioX: rx, ratioY: ry }); addDOMAnnotation(pIdx, 'highlight', rx, (e.clientY - rect.top)/rect.height); }
    else if (currentStudyTool === 'note') { const txt = prompt("Enter note:"); if (txt) { t.annotations.push({ type: 'note', pageIndex: pIdx, ratioX: rx, ratioY: ry - 0.03, text: txt }); addDOMAnnotation(pIdx, 'note', rx, (e.clientY - rect.top)/rect.height, txt); } }
    else if (currentStudyTool === 'link') { const url = prompt("Enter URL:"); if (url) { t.annotations.push({ type: 'link', pageIndex: pIdx, ratioX: rx, ratioY: ry, text: url }); addDOMAnnotation(pIdx, 'link', rx, (e.clientY - rect.top)/rect.height, url); } }
});
)HTML";

    // --- PART 25: JS Bake PDF Download ---
    ss << LR"HTML(
async function downloadCurrentPDF() {
    const t = openedTabs.find(x => x.id === activeTabId); if (!t) return showToast("No file.");
    showLoading(true, "Saving document...");
    try {
        const doc = await PDFLib.PDFDocument.load(t.bytes); const pages = doc.getPages();
        for (let a of t.annotations) {
            const page = pages[a.pageIndex]; const { width, height } = page.getSize();
            if (a.type === 'highlight') page.drawRectangle({ x: a.ratioX*width, y: a.ratioY*height, width: 120, height: 15, color: PDFLib.rgb(0.99, 0.84, 0.2), opacity: 0.4, blendMode: PDFLib.BlendMode.Multiply });
            else if (a.type === 'note') { page.drawRectangle({ x: a.ratioX*width, y: a.ratioY*height, width: 200, height: 40, color: PDFLib.rgb(0.98, 0.96, 0.84), borderColor: PDFLib.rgb(0.8, 0.6, 0.2) }); page.drawText("📝 "+a.text, { x: a.ratioX*width+5, y: a.ratioY*height+15, size: 12 }); }
            else if (a.type === 'link') { page.drawText("🔗 "+a.text, { x: a.ratioX*width, y: a.ratioY*height, size: 10, color: PDFLib.rgb(0,0,1) }); const la = doc.context.obj({ Type: 'Annot', Subtype: 'Link', Rect: [a.ratioX*width, a.ratioY*height-5, a.ratioX*width+150, a.ratioY*height+10], Border:[0,0,0], A:{ Type:'Action', S:'URI', URI: PDFLib.PDFString.of(a.text) } }); const ref = doc.context.register(la); let annots = page.node.Annots(); if(!annots){ annots = doc.context.obj([]); page.node.set(PDFLib.PDFName.of('Annots'), annots); } annots.push(ref); }
            else if (a.type === 'image') { let img = a.imgType==='image/png'?await doc.embedPng(a.buffer):await doc.embedJpg(a.buffer); const dims = img.scale(0.5); page.drawImage(img, { x: (a.ratioX*width)-dims.width/2, y: (a.ratioY*height)-dims.height/2, width: dims.width, height: dims.height }); }
        }
        await saveBytesToFile(new Blob([await doc.save()]), t.name);
    } catch(e) { showToast("Save failed."); } showLoading(false);
}
)HTML";

    // --- PART 26: JS Paste Image (Ctrl+V) ---
    ss << LR"HTML(
document.addEventListener('paste', async (e) => {
    const t = openedTabs.find(x => x.id === activeTabId); if (!t) return;
    const items = e.clipboardData.items; let file = null;
    for(let i=0; i<items.length; i++) { if(items[i].type.indexOf('image')!==-1){ file = items[i].getAsFile(); break; } }
    if (!file) return;
    try {
        const buf = await file.arrayBuffer();
        t.annotations.push({ type: 'image', pageIndex: 0, ratioX: 0.5, ratioY: 0.5, buffer: new Uint8Array(buf), imgType: file.type });
        const wrap = document.getElementById('page-1');
        if(wrap) { const img = document.createElement('img'); img.src = URL.createObjectURL(file); img.style.position = 'absolute'; img.style.left = '25%'; img.style.top = '25%'; img.style.width = '50%'; img.style.pointerEvents = 'none'; wrap.appendChild(img); }
        showToast("Image pasted!");
    } catch(err) {}
});
)HTML";

    // --- PART 27: JS Merge Files ---
    ss << LR"HTML(
function uiShowMergeModal() { showModal("Combine Files", `<input type="file" id="mergeFiles" accept=".pdf" multiple><div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" onclick="actionMergeFiles()">Combine</button></div>`); }
async function actionMergeFiles() {
    const f = document.getElementById('mergeFiles').files; if (f.length < 2) return showToast("Select 2 files.");
    closeModal(); showLoading(true, "Merging...");
    try {
        const md = await PDFLib.PDFDocument.create();
        for (let file of f) { const p = await PDFLib.PDFDocument.load(new Uint8Array(await file.arrayBuffer())); const cp = await md.copyPages(p, p.getPageIndices()); cp.forEach(pg => md.addPage(pg)); }
        await saveBytesToFile(new Blob([await md.save()]), "Combined.pdf");
    } catch(e) {} showLoading(false);
}
)HTML";

    // --- PART 28: JS Split Files ---
    ss << LR"HTML(
function uiShowSplitModal() { const t = openedTabs.find(x => x.id === activeTabId); if(!t) return showToast("Open a PDF first."); showModal("Split PDF", `<p>Split after page (1 - ${t.pdfjsDoc.numPages - 1}):</p><input type="number" id="splitPage" min="1" max="${t.pdfjsDoc.numPages - 1}" value="1"><div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" onclick="actionSplitPDF()">Split</button></div>`); }
async function actionSplitPDF() {
    const t = openedTabs.find(x => x.id === activeTabId); const sp = parseInt(document.getElementById('splitPage').value); closeModal(); showLoading(true, "Splitting...");
    try { const src = await PDFLib.PDFDocument.load(t.bytes); const d1 = await PDFLib.PDFDocument.create(); const d2 = await PDFLib.PDFDocument.create(); const idx = src.getPageIndices(); const c1 = await d1.copyPages(src, idx.slice(0, sp)); c1.forEach(p => d1.addPage(p)); const c2 = await d2.copyPages(src, idx.slice(sp)); c2.forEach(p => d2.addPage(p)); await saveBytesToFile(new Blob([await d1.save()]), "Part1.pdf"); await saveBytesToFile(new Blob([await d2.save()]), "Part2.pdf"); } catch(e){} showLoading(false);
}
)HTML";

    // --- PART 29: JS Extract Pages ---
    ss << LR"HTML(
function uiShowExtractModal() { if(!activeTabId) return showToast("Open a PDF first."); showModal("Extract Pages", `<p>Page numbers (e.g., 1, 3):</p><input type="text" id="extractPagesInput" placeholder="1, 2"><div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" onclick="actionExtractPages()">Extract</button></div>`); }
async function actionExtractPages() {
    const t = openedTabs.find(x => x.id === activeTabId); const pagesStr = document.getElementById('extractPagesInput').value;
    const pagesToExtract = pagesStr.split(',').map(n => parseInt(n.trim()) - 1).filter(n => !isNaN(n) && n >= 0 && n < t.pdfjsDoc.numPages);
    if(pagesToExtract.length === 0) return showToast("Invalid pages."); closeModal(); showLoading(true, "Extracting...");
    try { const src = await PDFLib.PDFDocument.load(t.bytes); const d = await PDFLib.PDFDocument.create(); const cp = await d.copyPages(src, pagesToExtract); cp.forEach(p => d.addPage(p)); await saveBytesToFile(new Blob([await d.save()]), "Extracted.pdf"); } catch(e){} showLoading(false);
}
)HTML";

    // --- PART 30: JS Delete Pages ---
    ss << LR"HTML(
function uiShowDeleteModal() { const t = openedTabs.find(x => x.id === activeTabId); if(!t) return showToast("Open a PDF first."); showModal("Delete Page", `<p>Page to delete:</p><input type="number" id="deletePage" min="1" max="${t.pdfjsDoc.numPages}"><div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" style="background:var(--brand-red);" onclick="actionDeletePage()">Delete</button></div>`); }
async function actionDeletePage() {
    const t = openedTabs.find(x => x.id === activeTabId); const pNum = parseInt(document.getElementById('deletePage').value) - 1; closeModal(); showLoading(true, "Deleting...");
    try { const src = await PDFLib.PDFDocument.load(t.bytes); src.removePage(pNum); t.bytes = new Uint8Array(await src.save()); t.pdfjsDoc = await pdfjsLib.getDocument({data: t.bytes}).promise; renderActiveViewer(); showToast("Deleted."); } catch(e){} showLoading(false);
}
)HTML";

    // --- PART 31: JS Watermark & Export Images ---
    ss << LR"HTML(
async function actionAddWatermark() { const t = openedTabs.find(x => x.id === activeTabId); if (!t) return showToast("No file."); const txt = prompt("Enter watermark text:"); if (!txt) return; showLoading(true, "Applying..."); try { const doc = await PDFLib.PDFDocument.load(t.bytes); const { rgb, degrees } = PDFLib; doc.getPages().forEach((p) => { const { width, height } = p.getSize(); p.drawText(txt, { x: width / 2 - 150, y: height / 2, size: 60, color: rgb(0.8, 0.2, 0.2), opacity: 0.25, rotate: degrees(45) }); }); await saveBytesToFile(new Blob([await doc.save()]), "Watermark.pdf"); } catch(e){} showLoading(false); }
async function actionPDFtoImage() { const t = openedTabs.find(x => x.id === activeTabId); if (!t) return showToast("No file."); showLoading(true, "Zipping..."); try { const zip = new JSZip(); for (let i = 1; i <= t.pdfjsDoc.numPages; i++) { const p = await t.pdfjsDoc.getPage(i); const vp = p.getViewport({ scale: 2.0 }); const c = document.createElement('canvas'); const ctx = c.getContext('2d'); c.height = vp.height; c.width = vp.width; await p.render({ canvasContext: ctx, viewport: vp }).promise; zip.file(`Page_${i}.png`, await new Promise(r => c.toBlob(r, 'image/png'))); } await saveBytesToFile(await zip.generateAsync({type:"blob"}), "Images.zip", "zip", "application/zip"); } catch(e){} showLoading(false); }
)HTML";

    // --- PART 32: JS Text Export, OCR & Mode Toggles ---
    ss << LR"HTML(
async function actionPDFtoText() { const t = openedTabs.find(x => x.id === activeTabId); if (!t) return showToast("No file."); showLoading(true, "Extracting..."); try { let txt = ""; for (let i = 1; i <= t.pdfjsDoc.numPages; i++) { const p = await t.pdfjsDoc.getPage(i); txt += `\n${(await p.getTextContent()).items.map(x => x.str).join(' ')}\n`; } await saveBytesToFile(new Blob([txt], {type: "text/plain"}), "Extracted.txt", "txt", "text/plain"); } catch(e){} showLoading(false); }
async function actionPerformOCR() { const t = openedTabs.find(x => x.id === activeTabId); if (!t) return showToast("No file."); showLoading(true, "OCR Scanning..."); try { const p = await t.pdfjsDoc.getPage(1); const vp = p.getViewport({ scale: 2.0 }); const c = document.createElement('canvas'); c.height = vp.height; c.width = vp.width; await p.render({ canvasContext: c.getContext('2d'), viewport: vp }).promise; const res = await Tesseract.recognize(c.toDataURL('image/png'), 'eng'); await saveBytesToFile(new Blob([res.data.text], {type: "text/plain"}), "OCR.txt", "txt", "text/plain"); } catch(e){} showLoading(false); }

function toggleNightMode() { document.body.classList.toggle('night-mode'); }
function toggleReadMode() { document.body.classList.toggle('read-mode'); setTimeout(renderActiveViewer, 100); }
</script>
)HTML";

    // --- PART 33: End HTML ---
    ss << LR"HTML(
</body>
</html>
)HTML";

    return ss.str();
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
    auto envCompletedHandler = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [hWnd, hHostWnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result)) return result;
            g_webViewEnv = env;
            
            auto controllerCompletedHandler = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
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
                    
                    g_webView->NavigateToString(GetAcrobatHTML().c_str());
                    
                    auto navCompletedHandler = Callback<ICoreWebView2NavigationCompletedEventHandler>(
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
                    );
                    g_webView->add_NavigationCompleted(navCompletedHandler.Get(), nullptr);
                    return S_OK;
                }
            );
            env->CreateCoreWebView2Controller(hHostWnd, controllerCompletedHandler.Get());
            return S_OK;
        }
    );
    return CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr, envCompletedHandler.Get());
}

// ==========================================
// 🚀 LAUNCH ACROBAT STYLE PDF VIEWER
// ==========================================
void LaunchFoxitStylePdfReader(std::wstring pdfPath) {
    g_acrobatPdfPath = pdfPath;

    if (g_hAcrobatWnd != NULL) {
        ShowWindow(g_hAcrobatWnd, SW_RESTORE);
        SetForegroundWindow(g_hAcrobatWnd);    
        
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
        0, L"AcrobatWorkspaceClass", L"RasFocus - PDF Pro Workspace",
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
    SetForegroundWindow(g_hAcrobatWnd); 
    UpdateWindow(g_hAcrobatWnd);
}

// ==========================================
// ⚠️ LEGACY FUNCTIONS
// ==========================================
void DrawPdfWorkspaceTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch) {
    FontFamily ff(L"Segoe UI");
    Font fText(&ff, 20 * g_scaleFactor, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 100, 100, 100));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"PDF Workspace is ready. Double click a PDF file to launch.",
        -1, &fText, RectF(cx, cy, cw, ch), &fmt, &textBrush);
}

void ProcessPdfWorkspaceMouseMove(float x, float y) {}
void ProcessPdfWorkspaceMouseClick(float x, float y) {}
