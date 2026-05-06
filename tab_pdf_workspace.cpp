// tab_pdf_workspace.cpp
// Professional PDF Workspace Architecture

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
// 🎨 HTML/CSS/JS UI - SPLIT INTO 10 SMALL PARTS 
// ==========================================
wstring GetAcrobatHTML() {
    
    // --- PART 1: Head & CSS Variables ---
    wstring htmlPart1 = LR"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>PDF Workspace - Pro Edition</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
<script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/tesseract.js@4/dist/tesseract.min.js"></script>
<link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200" rel="stylesheet" />

<style>
:root {
    --brand-red: #EB1C24;
    --brand-red-hover: #BA1617;
    --bg-dark: #222222;
    --bg-tabs: #2D2D2D;
    --bg-panel: #F8F8F8;
    --bg-doc: #D3D3D3;
    --border-color: #CCCCCC;
    --text-primary: #1A1A1A;
    --text-light: #FFFFFF;
    --text-muted: #555555;
    --hover-bg: #EAEAEA;
    --selected-bg: #FBECEE;
    --topbar-height: 38px;
    --tabbar-height: 34px;
    --toolbar-height: 42px;
    --right-sidebar-width: 180px; /* 🟢 Reduced Sidebar Width */
}
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', system-ui, sans-serif; height: 100vh; overflow: hidden; display: flex; flex-direction: column; background: var(--bg-doc); color: var(--text-primary); user-select: none; }
.material-symbols-outlined { font-variation-settings: 'FILL' 0, 'wght' 400, 'GRAD' 0, 'opsz' 24; font-size: 20px; }
)HTML";

    // --- PART 2: UI Modals & Toasts ---
    wstring htmlPart2 = LR"HTML(
.toast-container { position: fixed; bottom: 30px; left: 50%; transform: translateX(-50%); z-index: 9999; display: flex; flex-direction: column; gap: 8px; }
.toast { padding: 10px 24px; border-radius: 4px; color: white; background: #323232; font-size: 13px; font-weight: 500; box-shadow: 0 4px 12px rgba(0,0,0,0.3); animation: fadeInUp 0.3s ease; }
@keyframes fadeInUp { from { transform: translateY(20px); opacity: 0; } to { transform: translateY(0); opacity: 1; } }

.modal-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.6); z-index: 1000; justify-content: center; align-items: center; }
.modal-overlay.show { display: flex; }
.modal { background: white; border-radius: 6px; padding: 24px; min-width: 350px; box-shadow: 0 10px 30px rgba(0,0,0,0.3); }
.modal h3 { font-size: 16px; margin-bottom: 16px; font-weight: 600; color: #222; }
.modal input[type="file"], .modal input[type="number"], .modal input[type="text"] { width: 100%; padding: 8px; margin-bottom: 16px; border: 1px solid var(--border-color); border-radius: 4px; outline: none; }
.modal-actions { display: flex; gap: 10px; justify-content: flex-end; }
.btn { padding: 8px 18px; border: none; border-radius: 4px; cursor: pointer; font-size: 13px; font-weight: 600; transition: 0.2s; }
.btn-primary { background: var(--brand-red); color: white; }
.btn-primary:hover { background: var(--brand-red-hover); }
.btn-secondary { background: transparent; border: 1px solid #aaa; color: var(--text-primary); }
.btn-secondary:hover { background: #e0e0e0; }
)HTML";

    // --- PART 3: Topbars and Tabbars ---
    wstring htmlPart3 = LR"HTML(
.topbar { height: var(--topbar-height); background: var(--bg-dark); display: flex; align-items: center; padding: 0 16px; color: var(--text-light); border-bottom: 1px solid #111; }
.topbar-menu { display: flex; gap: 20px; font-size: 13px; font-weight: 500; }
.topbar-item { cursor: pointer; opacity: 0.8; transition: 0.2s; padding: 4px 8px; border-radius: 4px; }
.topbar-item:hover { opacity: 1; background: rgba(255,255,255,0.1); }
.topbar-actions { display: flex; gap: 12px; margin-left: auto; }
.topbar-icon { cursor: pointer; opacity: 0.8; transition: 0.2s; padding: 4px; border-radius: 4px; }
.topbar-icon:hover { opacity: 1; background: rgba(255,255,255,0.1); }

.tabbar { height: var(--tabbar-height); background: var(--bg-tabs); display: flex; align-items: flex-end; padding-left: 8px; overflow-x: auto; border-bottom: 1px solid #111; }
.pdf-tab { height: 28px; background: #3c3c3c; color: #b5b5b5; padding: 0 10px 0 14px; display: flex; align-items: center; gap: 8px; font-size: 12px; border-radius: 4px 4px 0 0; margin-right: 2px; cursor: pointer; max-width: 200px; transition: 0.15s; }
.pdf-tab:hover { background: #4a4a4a; color: #fff; }
.pdf-tab.active { background: white; color: var(--text-primary); font-weight: 600; height: 31px; border-bottom: none; }
.pdf-tab .close-tab { font-size: 14px; cursor: pointer; border-radius: 50%; width: 18px; height: 18px; display: flex; align-items: center; justify-content: center; }
.pdf-tab .close-tab:hover { background: rgba(255,255,255,0.2); color: #fff; }
.pdf-tab.active .close-tab:hover { background: rgba(0,0,0,0.1); color: var(--text-primary); }
.add-tab-btn { color: #fff; opacity: 0.6; cursor: pointer; padding: 0 10px; font-weight: bold; font-size: 18px; line-height: 28px; }
.add-tab-btn:hover { opacity: 1; }
)HTML";

    // --- PART 4: Toolbar and Workspace ---
    wstring htmlPart4 = LR"HTML(
.toolbar { height: var(--toolbar-height); background: white; border-bottom: 1px solid var(--border-color); display: flex; align-items: center; padding: 0 16px; gap: 12px; font-size: 13px; }
.tool-btn { display: flex; align-items: center; justify-content: center; width: 32px; height: 32px; cursor: pointer; border-radius: 4px; color: var(--text-primary); transition: 0.15s; }
.tool-btn:hover { background: var(--hover-bg); }
.tool-btn.active { background: var(--selected-bg); color: var(--brand-red); }
.divider { width: 1px; height: 20px; background: var(--border-color); margin: 0 4px; }

.workspace { flex: 1; display: flex; overflow: hidden; position: relative; }
/* 🟢 Left Sidebar Removed */

.right-sidebar { width: var(--right-sidebar-width); background: var(--bg-panel); border-left: 1px solid var(--border-color); display: flex; flex-direction: column; overflow-y: auto; flex-shrink: 0; box-shadow: -2px 0 5px rgba(0,0,0,0.02); }
.sidebar-header { padding: 12px 14px; font-size: 11px; font-weight: 700; letter-spacing: 0.5px; text-transform: uppercase; color: var(--text-muted); border-bottom: 1px solid var(--border-color); }
.tool-pane-btn { display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 6px; padding: 16px 10px; cursor: pointer; border-bottom: 1px solid var(--border-color); background: transparent; transition: 0.15s; text-align: center; }
.tool-pane-btn:hover { background: var(--selected-bg); }
.tool-pane-btn .material-symbols-outlined { color: var(--brand-red); font-size: 26px; }
.tool-pane-text { font-size: 12px; font-weight: 500; line-height: 1.2; }
)HTML";

    // --- PART 5: Viewer, Read Mode & DOM Overlays ---
    wstring htmlPart5 = LR"HTML(
.pdf-viewer-area { flex: 1; overflow-y: auto; display: flex; justify-content: center; padding: 24px; background: var(--bg-doc); }
.pdf-container { display: flex; flex-direction: column; gap: 16px; align-items: center; width: 100%; cursor: default; }
.pdf-page-wrapper { background: white; box-shadow: 0 4px 12px rgba(0,0,0,0.2); margin-bottom: 10px; position: relative; }
.pdf-page-wrapper canvas { display: block; }

.loading-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.7); z-index: 2000; flex-direction: column; justify-content: center; align-items: center; color: white; font-size: 14px; font-weight: 500; }
.loading-overlay.show { display: flex; }
.spinner { border: 3px solid rgba(255,255,255,0.2); border-top: 3px solid white; border-radius: 50%; width: 36px; height: 36px; animation: spin 0.8s linear infinite; margin-bottom: 16px; }
@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }

/* Night Mode */
body.night-mode .pdf-viewer-area { background: #1a1a1a !important; }
body.night-mode .toolbar { background: #2b2b2b !important; border-color: #111 !important; color: #ddd !important; }
body.night-mode .toolbar .tool-btn { color: #ddd !important; }
body.night-mode .toolbar .tool-btn:hover { background: #444 !important; }
body.night-mode .right-sidebar { background: #252525 !important; border-color: #111 !important; }
body.night-mode .sidebar-header { color: #888 !important; border-color: #333 !important; }
body.night-mode .tool-pane-btn { border-color: #333 !important; color: #ddd !important; }
body.night-mode .tool-pane-btn:hover { background: #333 !important; }

/* 🟢 Read Mode (Clean View) */
body.read-mode .toolbar { display: none !important; }
body.read-mode .right-sidebar { display: none !important; }
body.read-mode .tabbar { display: none !important; }

.dom-annotation { position: absolute; pointer-events: none; z-index: 5; }
</style>
</head>
<body>
<div class="toast-container" id="toast-container"></div>
<div class="loading-overlay" id="loading-overlay"><div class="spinner"></div><div id="loading-text">Processing...</div></div>
<div class="modal-overlay" id="modal-overlay"><div class="modal" id="modal-content"></div></div>
)HTML";

    // --- PART 6: HTML Structure ---
    wstring htmlPart6 = LR"HTML(
<div class="topbar">
    <div class="topbar-menu">
        <div class="topbar-item" onclick="document.getElementById('fileInput').click()">Open</div>
        <div class="topbar-item" onclick="downloadCurrentPDF()">Save As...</div>
    </div>
    <div class="topbar-actions">
        <span class="material-symbols-outlined topbar-icon" onclick="toggleNightMode()" title="Night Mode">dark_mode</span>
        <span class="material-symbols-outlined topbar-icon" onclick="toggleReadMode()" title="Read Mode" id="read-mode-icon">menu_book</span>
        <span class="material-symbols-outlined topbar-icon" onclick="downloadCurrentPDF()" title="Save">save</span>
    </div>
</div>

<div class="tabbar" id="tabbar-strip"></div>

<div class="toolbar">
    <div class="tool-btn active" onclick="setStudyTool(null)" id="tool-pointer" title="Pointer"><span class="material-symbols-outlined">pan_tool</span></div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="setStudyTool('highlight')" id="tool-highlight" title="Highlight"><span class="material-symbols-outlined" style="color: #FBC02D;">format_ink_highlighter</span></div>
    <div class="tool-btn" onclick="setStudyTool('note')" id="tool-note" title="Sticky Note"><span class="material-symbols-outlined" style="color: #4CAF50;">speaker_notes</span></div>
    <div class="tool-btn" onclick="setStudyTool('link')" id="tool-link" title="Insert Link"><span class="material-symbols-outlined" style="color: #1976D2;">link</span></div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="zoomOut()"><span class="material-symbols-outlined">remove</span></div>
    <span id="zoom-text" style="font-size:12px;width:35px;text-align:center;font-weight:600;">100%</span>
    <div class="tool-btn" onclick="zoomIn()"><span class="material-symbols-outlined">add</span></div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="rotatePDF()"><span class="material-symbols-outlined">rotate_right</span></div>
</div>

<div class="workspace">
    <div class="pdf-viewer-area" id="viewer-area">
        <div class="pdf-container" id="pdf-container">
            <div style="margin-top: 150px; text-align: center; color: var(--text-muted);">
                <span class="material-symbols-outlined" style="font-size: 72px; opacity:0.3;">note_add</span>
                <p style="margin-top: 16px; font-size: 15px;">Click + or Open to load a PDF</p>
            </div>
        </div>
    </div>

    <div class="right-sidebar">
        <div class="sidebar-header">Tools</div>
        <div class="tool-pane-btn" onclick="uiShowMergeModal()"><span class="material-symbols-outlined">library_add</span><span class="tool-pane-text">Combine</span></div>
        <div class="tool-pane-btn" onclick="uiShowSplitModal()"><span class="material-symbols-outlined">splitscreen</span><span class="tool-pane-text">Split</span></div>
        <div class="tool-pane-btn" onclick="uiShowExtractModal()"><span class="material-symbols-outlined">file_upload</span><span class="tool-pane-text">Extract</span></div>
        <div class="tool-pane-btn" onclick="uiShowDeleteModal()"><span class="material-symbols-outlined">delete</span><span class="tool-pane-text">Delete</span></div>
        <div class="tool-pane-btn" onclick="actionPDFtoImage()"><span class="material-symbols-outlined">image</span><span class="tool-pane-text">To Image</span></div>
        <div class="tool-pane-btn" onclick="actionPDFtoText()"><span class="material-symbols-outlined">article</span><span class="tool-pane-text">To Text</span></div>
        <div class="tool-pane-btn" onclick="actionPerformOCR()"><span class="material-symbols-outlined">document_scanner</span><span class="tool-pane-text">OCR Scan</span></div>
        <div class="tool-pane-btn" onclick="actionAddWatermark()"><span class="material-symbols-outlined">branding_watermark</span><span class="tool-pane-text">Watermark</span></div>
    </div>
</div>
<input type="file" id="fileInput" accept=".pdf" style="display:none;" onchange="handleFileOpen(event)">
)HTML";

    // --- PART 7: JS Tab Management & Native Save Logic ---
    wstring htmlPart7 = LR"HTML(
<script>
let openedTabs = []; let activeTabId = null;

function showToast(msg) {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = 'toast'; toast.textContent = msg;
    container.appendChild(toast); setTimeout(() => toast.remove(), 3000);
}
function showLoading(show, text="Processing...") {
    document.getElementById('loading-overlay').classList.toggle('show', show);
    document.getElementById('loading-text').textContent = text;
}
function showModal(title, htmlContent) {
    document.getElementById('modal-content').innerHTML = `<h3>${title}</h3>${htmlContent}`;
    document.getElementById('modal-overlay').classList.add('show');
}
function closeModal() { document.getElementById('modal-overlay').classList.remove('show'); }

// 🟢 Smart Ask Location Save System
async function saveBytesToFile(blobData, suggestedName, ext="pdf", mime="application/pdf") {
    try {
        if (window.showSaveFilePicker) {
            const handle = await window.showSaveFilePicker({ suggestedName: suggestedName, types: [{ description: 'Document', accept: {[mime]: ['.'+ext]} }] });
            const writable = await handle.createWritable();
            await writable.write(blobData);
            await writable.close();
            showToast("Saved successfully!");
        } else { saveAs(blobData, suggestedName); }
    } catch (e) { if(e.name !== 'AbortError') showToast("Save failed or cancelled."); }
}

async function handleFileOpen(event) {
    const file = event.target.files[0]; if (!file) return;
    createNewTab(file.name, new Uint8Array(await file.arrayBuffer()));
}

async function loadPdfFromPath(path) {
    try {
        const response = await fetch('file:///' + path.replace(/\\/g, '/'));
        const arrayBuffer = await response.arrayBuffer();
        const pathParts = path.split('\\');
        createNewTab(pathParts[pathParts.length - 1], new Uint8Array(arrayBuffer));
    } catch (e) { showToast("Failed to load PDF."); }
}

async function createNewTab(fileName, uint8Array) {
    try {
        const pdfDoc = await pdfjsLib.getDocument({data: uint8Array}).promise;
        const tabId = 'tab_' + Date.now();
        openedTabs.push({ id: tabId, name: fileName, bytes: uint8Array, pdfjsDoc: pdfDoc, zoom: 1.0, rotation: 0, annotations: [] });
        updateTabStrip(); await switchActiveTab(tabId);
    } catch(e) { console.error(e); showToast("Invalid PDF file."); }
}

function updateTabStrip() {
    const strip = document.getElementById('tabbar-strip'); strip.innerHTML = '';
    openedTabs.forEach(tab => {
        const tabEl = document.createElement('div'); tabEl.className = `pdf-tab ${tab.id === activeTabId ? 'active' : ''}`; tabEl.onclick = () => switchActiveTab(tab.id);
        const nameEl = document.createElement('span'); nameEl.textContent = tab.name.length > 20 ? tab.name.substring(0, 17) + '...' : tab.name;
        const closeEl = document.createElement('span'); closeEl.className = 'close-tab'; closeEl.textContent = '×';
        closeEl.onclick = (e) => { e.stopPropagation(); closeTabInstance(tab.id); };
        tabEl.appendChild(nameEl); tabEl.appendChild(closeEl); strip.appendChild(tabEl);
    });
    // Add Tab Button
    const addBtn = document.createElement('div'); addBtn.className = 'add-tab-btn'; addBtn.textContent = '+';
    addBtn.onclick = () => document.getElementById('fileInput').click();
    strip.appendChild(addBtn);
}
)HTML";

    // --- PART 8: JS Rendering & Touchpad Zoom ---
    wstring htmlPart8 = LR"HTML(
async function switchActiveTab(tabId) {
    activeTabId = tabId; updateTabStrip();
    const tab = openedTabs.find(t => t.id === tabId); if (!tab) return;
    document.getElementById('zoom-text').textContent = Math.round(tab.zoom * 100) + '%';
    await renderActiveViewer();
}

function closeTabInstance(tabId) {
    openedTabs = openedTabs.filter(t => t.id !== tabId);
    if (activeTabId === tabId) activeTabId = openedTabs.length > 0 ? openedTabs[openedTabs.length - 1].id : null;
    updateTabStrip();
    if (activeTabId) switchActiveTab(activeTabId);
    else document.getElementById('pdf-container').innerHTML = `<div style="margin-top:150px;text-align:center;color:var(--text-muted);"><span class="material-symbols-outlined" style="font-size:72px;opacity:0.3;">note_add</span><p style="margin-top:16px;">Click + to load a PDF</p></div>`;
}

async function renderActiveViewer() {
    const tab = openedTabs.find(t => t.id === activeTabId); if (!tab) return;
    const container = document.getElementById('pdf-container'); container.innerHTML = '';
    for (let i = 1; i <= tab.pdfjsDoc.numPages; i++) {
        const page = await tab.pdfjsDoc.getPage(i);
        const viewport = page.getViewport({ scale: tab.zoom, rotation: tab.rotation });
        const wrapper = document.createElement('div'); wrapper.className = 'pdf-page-wrapper'; wrapper.id = `page-${i}`;
        const canvas = document.createElement('canvas'); const context = canvas.getContext('2d');
        canvas.height = viewport.height; canvas.width = viewport.width;
        wrapper.appendChild(canvas); container.appendChild(wrapper);
        await page.render({ canvasContext: context, viewport: viewport }).promise;
        redrawActiveAnnotations(i);
    }
}

function zoomIn() { const t = openedTabs.find(x => x.id === activeTabId); if (t && t.zoom < 3.0) { t.zoom += 0.2; renderActiveViewer(); document.getElementById('zoom-text').textContent = Math.round(t.zoom * 100) + '%'; } }
function zoomOut() { const t = openedTabs.find(x => x.id === activeTabId); if (t && t.zoom > 0.4) { t.zoom -= 0.2; renderActiveViewer(); document.getElementById('zoom-text').textContent = Math.round(t.zoom * 100) + '%'; } }
function rotatePDF() { const t = openedTabs.find(x => x.id === activeTabId); if (t) { t.rotation = (t.rotation + 90) % 360; renderActiveViewer(); } }

// 🟢 Touchpad Pinch-to-Zoom Precision
document.getElementById('viewer-area').addEventListener('wheel', function(e) {
    if (e.ctrlKey) {
        e.preventDefault();
        const tab = openedTabs.find(x => x.id === activeTabId); if (!tab) return;
        if (e.deltaY < 0) { if (tab.zoom < 3.0) tab.zoom += 0.05; } 
        else { if (tab.zoom > 0.4) tab.zoom -= 0.05; }
        document.getElementById('zoom-text').textContent = Math.round(tab.zoom * 100) + '%';
        if(window.zoomTimeout) clearTimeout(window.zoomTimeout);
        window.zoomTimeout = setTimeout(renderActiveViewer, 40);
    }
}, { passive: false });
)HTML";

    // --- PART 9: JS Advanced Features Fixed for Multi-Tab ---
    wstring htmlPart9 = LR"HTML(
function uiShowMergeModal() { showModal("Combine Files", `<input type="file" id="mergeFiles" accept=".pdf" multiple><div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" onclick="actionMergeFiles()">Combine</button></div>`); }
async function actionMergeFiles() {
    const files = document.getElementById('mergeFiles').files; if (files.length < 2) return showToast("Select at least 2 files.");
    closeModal(); showLoading(true, "Merging PDFs...");
    try {
        const mergedPdf = await PDFLib.PDFDocument.create();
        for (let file of files) {
            const pdf = await PDFLib.PDFDocument.load(new Uint8Array(await file.arrayBuffer()));
            const copiedPages = await mergedPdf.copyPages(pdf, pdf.getPageIndices()); copiedPages.forEach(p => mergedPdf.addPage(p));
        }
        await createNewTab("Combined_Doc.pdf", new Uint8Array(await mergedPdf.save())); showToast("Merged!");
    } catch(e) { showToast("Merge failed."); } showLoading(false);
}

function uiShowSplitModal() {
    const tab = openedTabs.find(t => t.id === activeTabId); if(!tab) return showToast("Open a PDF first.");
    showModal("Split PDF", `<p>Split after page (1 - ${tab.pdfjsDoc.numPages - 1}):</p><input type="number" id="splitPage" min="1" max="${tab.pdfjsDoc.numPages - 1}" value="1"><div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" onclick="actionSplitPDF()">Split</button></div>`);
}
async function actionSplitPDF() {
    const tab = openedTabs.find(t => t.id === activeTabId); const splitAt = parseInt(document.getElementById('splitPage').value);
    closeModal(); showLoading(true, "Splitting...");
    try {
        const srcDoc = await PDFLib.PDFDocument.load(tab.bytes); const doc1 = await PDFLib.PDFDocument.create(); const doc2 = await PDFLib.PDFDocument.create();
        const indices = srcDoc.getPageIndices();
        const copied1 = await doc1.copyPages(srcDoc, indices.slice(0, splitAt)); copied1.forEach(p => doc1.addPage(p));
        const copied2 = await doc2.copyPages(srcDoc, indices.slice(splitAt)); copied2.forEach(p => doc2.addPage(p));
        await saveBytesToFile(new Blob([await doc1.save()]), "Part1.pdf"); await saveBytesToFile(new Blob([await doc2.save()]), "Part2.pdf");
    } catch(e) { showToast("Split failed."); } showLoading(false);
}

function uiShowExtractModal() {
    if(!activeTabId) return showToast("Open a PDF first.");
    showModal("Extract Pages", `<p>Page numbers (e.g., 1, 3):</p><input type="text" id="extractPagesInput" placeholder="1, 2"><div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" onclick="actionExtractPages()">Extract</button></div>`);
}
async function actionExtractPages() {
    const tab = openedTabs.find(t => t.id === activeTabId); const pagesStr = document.getElementById('extractPagesInput').value;
    const pagesToExtract = pagesStr.split(',').map(n => parseInt(n.trim()) - 1).filter(n => !isNaN(n) && n >= 0 && n < tab.pdfjsDoc.numPages);
    if(pagesToExtract.length === 0) return showToast("Invalid pages.");
    closeModal(); showLoading(true, "Extracting...");
    try {
        const srcDoc = await PDFLib.PDFDocument.load(tab.bytes); const newDoc = await PDFLib.PDFDocument.create();
        const copied = await newDoc.copyPages(srcDoc, pagesToExtract); copied.forEach(p => newDoc.addPage(p));
        await createNewTab("Extracted.pdf", new Uint8Array(await newDoc.save()));
    } catch(e) { showToast("Extraction failed."); } showLoading(false);
}

function uiShowDeleteModal() {
    const tab = openedTabs.find(t => t.id === activeTabId); if(!tab) return showToast("Open a PDF first.");
    showModal("Delete Page", `<p>Page to delete:</p><input type="number" id="deletePage" min="1" max="${tab.pdfjsDoc.numPages}"><div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" style="background:var(--brand-red);" onclick="actionDeletePage()">Delete</button></div>`);
}
async function actionDeletePage() {
    const tab = openedTabs.find(t => t.id === activeTabId); const pageNum = parseInt(document.getElementById('deletePage').value) - 1;
    closeModal(); showLoading(true, "Deleting...");
    try {
        const srcDoc = await PDFLib.PDFDocument.load(tab.bytes); srcDoc.removePage(pageNum);
        tab.bytes = new Uint8Array(await srcDoc.save()); tab.pdfjsDoc = await pdfjsLib.getDocument({data: tab.bytes}).promise;
        renderActiveViewer(); showToast("Deleted.");
    } catch(e) { showToast("Delete failed."); } showLoading(false);
}
)HTML";

    // --- PART 10: Study Tools, Toggle Logic, View Modes ---
    wstring htmlPart10 = LR"HTML(
let currentStudyTool = null;
function setStudyTool(tool) {
    if (currentStudyTool === tool) tool = null; // 🟢 Toggle Off Logic
    currentStudyTool = tool;
    document.querySelectorAll('.toolbar .tool-btn').forEach(btn => btn.classList.remove('active'));
    if(tool === 'highlight') document.getElementById('tool-highlight').classList.add('active');
    else if(tool === 'note') document.getElementById('tool-note').classList.add('active');
    else if(tool === 'link') document.getElementById('tool-link').classList.add('active');
    else document.getElementById('tool-pointer').classList.add('active');
    document.getElementById('pdf-container').style.cursor = tool ? 'crosshair' : 'default';
}

function addDOMAnnotation(pageIndex, type, xPercent, yPercent, textData="") {
    const wrapper = document.getElementById(`page-${pageIndex + 1}`); if (!wrapper) return;
    const div = document.createElement('div'); div.className = 'dom-annotation';
    div.style.left = (xPercent * 100) + '%'; div.style.top = (yPercent * 100) + '%';
    if (type === 'highlight') { div.style.width = '120px'; div.style.height = '15px'; div.style.backgroundColor = 'rgba(255, 255, 0, 0.4)'; div.style.mixBlendMode = 'multiply'; }
    else if (type === 'note') { div.style.padding = '4px 8px'; div.style.backgroundColor = '#fffec8'; div.style.border = '1px solid #dcd777'; div.style.fontSize = '12px'; div.textContent = '📝 ' + textData; }
    else if (type === 'link') { div.style.color = 'blue'; div.style.textDecoration = 'underline'; div.style.fontSize = '11px'; div.style.cursor = 'pointer'; div.style.pointerEvents = 'auto'; div.textContent = '🔗 ' + textData; div.onclick = (e) => { e.stopPropagation(); window.open(textData, '_blank'); }; }
    wrapper.appendChild(div);
}

function redrawActiveAnnotations(pageNum) {
    const tab = openedTabs.find(t => t.id === activeTabId); if (!tab) return;
    tab.annotations.forEach(ann => { if(ann.pageIndex === (pageNum - 1) && ann.type !== 'image') addDOMAnnotation(ann.pageIndex, ann.type, ann.ratioX, 1 - ann.ratioY, ann.text); });
}

document.getElementById('pdf-container').addEventListener('click', (e) => {
    const tab = openedTabs.find(t => t.id === activeTabId); if (!tab || !currentStudyTool) return;
    const pageWrapper = e.target.closest('.pdf-page-wrapper'); if (!pageWrapper) return; 
    const pageIndex = parseInt(pageWrapper.id.split('-')[1]) - 1;
    const rect = e.target.getBoundingClientRect(); const ratioX = (e.clientX - rect.left) / rect.width; const ratioY = 1 - ((e.clientY - rect.top) / rect.height);
    if (currentStudyTool === 'highlight') { tab.annotations.push({ type: 'highlight', pageIndex, ratioX, ratioY }); addDOMAnnotation(pageIndex, 'highlight', ratioX, (e.clientY - rect.top)/rect.height); }
    else if (currentStudyTool === 'note') { const note = prompt("Enter note:"); if (note) { tab.annotations.push({ type: 'note', pageIndex, ratioX, ratioY: ratioY - 0.03, text: note }); addDOMAnnotation(pageIndex, 'note', ratioX, (e.clientY - rect.top)/rect.height, note); } }
    else if (currentStudyTool === 'link') { const url = prompt("Enter URL:"); if (url) { tab.annotations.push({ type: 'link', pageIndex, ratioX, ratioY, text: url }); addDOMAnnotation(pageIndex, 'link', ratioX, (e.clientY - rect.top)/rect.height, url); } }
});

async function downloadCurrentPDF() {
    const tab = openedTabs.find(t => t.id === activeTabId); if (!tab) return showToast("No active file.");
    showLoading(true, "Preparing PDF...");
    try {
        const pdfDoc = await PDFLib.PDFDocument.load(tab.bytes); const pages = pdfDoc.getPages();
        for (let ann of tab.annotations) {
            const page = pages[ann.pageIndex]; const { width, height } = page.getSize();
            if (ann.type === 'highlight') page.drawRectangle({ x: ann.ratioX*width, y: ann.ratioY*height, width: 120, height: 15, color: PDFLib.rgb(1, 1, 0), opacity: 0.4, blendMode: PDFLib.BlendMode.Multiply });
            else if (ann.type === 'note') { page.drawRectangle({ x: ann.ratioX*width, y: ann.ratioY*height, width: 200, height: 40, color: PDFLib.rgb(0.98, 0.96, 0.84), borderColor: PDFLib.rgb(0.8, 0.6, 0.2) }); page.drawText("📝 "+ann.text, { x: ann.ratioX*width+5, y: ann.ratioY*height+15, size: 12 }); }
            else if (ann.type === 'link') { page.drawText("🔗 "+ann.text, { x: ann.ratioX*width, y: ann.ratioY*height, size: 10, color: PDFLib.rgb(0,0,1) }); const linkAnnot = pdfDoc.context.obj({ Type: 'Annot', Subtype: 'Link', Rect: [ann.ratioX*width, ann.ratioY*height-5, ann.ratioX*width+150, ann.ratioY*height+10], Border:[0,0,0], A:{ Type:'Action', S:'URI', URI: PDFLib.PDFString.of(ann.text) } }); const ref = pdfDoc.context.register(linkAnnot); let annots = page.node.Annots(); if(!annots){ annots = pdfDoc.context.obj([]); page.node.set(PDFLib.PDFName.of('Annots'), annots); } annots.push(ref); }
        }
        await saveBytesToFile(new Blob([await pdfDoc.save()]), tab.name);
    } catch(e) { showToast("Save failed."); } showLoading(false);
}

async function actionAddWatermark() {
    const tab = openedTabs.find(t => t.id === activeTabId); if (!tab) return showToast("No file.");
    const text = prompt("Enter watermark text:"); if (!text) return;
    showLoading(true, "Applying...");
    try {
        const pdfDoc = await PDFLib.PDFDocument.load(tab.bytes); const { rgb, degrees } = PDFLib;
        pdfDoc.getPages().forEach((page) => { const { width, height } = page.getSize(); page.drawText(text, { x: width / 2 - 150, y: height / 2, size: 60, color: rgb(0.8, 0.2, 0.2), opacity: 0.25, rotate: degrees(45) }); });
        await saveBytesToFile(new Blob([await pdfDoc.save()]), "Watermarked_" + tab.name);
    } catch(e) { showToast("Failed."); } showLoading(false);
}

async function actionPDFtoImage() {
    const tab = openedTabs.find(t => t.id === activeTabId); if (!tab) return showToast("No file.");
    showLoading(true, "Zipping...");
    try {
        const zip = new JSZip();
        for (let i = 1; i <= tab.pdfjsDoc.numPages; i++) {
            const page = await tab.pdfjsDoc.getPage(i); const viewport = page.getViewport({ scale: 2.0 });
            const canvas = document.createElement('canvas'); const ctx = canvas.getContext('2d'); canvas.height = viewport.height; canvas.width = viewport.width;
            await page.render({ canvasContext: ctx, viewport: viewport }).promise;
            zip.file(`Page_${i}.png`, await new Promise(res => canvas.toBlob(res, 'image/png')));
        }
        await saveBytesToFile(await zip.generateAsync({type:"blob"}), "Images.zip", "zip", "application/zip");
    } catch(e) { showToast("Failed."); } showLoading(false);
}

async function actionPDFtoText() {
    const tab = openedTabs.find(t => t.id === activeTabId); if (!tab) return showToast("No file.");
    showLoading(true, "Extracting...");
    try {
        let txt = ""; for (let i = 1; i <= tab.pdfjsDoc.numPages; i++) { const page = await tab.pdfjsDoc.getPage(i); txt += `\n${(await page.getTextContent()).items.map(x => x.str).join(' ')}\n`; }
        await saveBytesToFile(new Blob([txt], {type: "text/plain"}), "Extracted.txt", "txt", "text/plain");
    } catch(e) { showToast("Failed."); } showLoading(false);
}

async function actionPerformOCR() {
    const tab = openedTabs.find(t => t.id === activeTabId); if (!tab) return showToast("No file.");
    showLoading(true, "OCR Scanning...");
    try {
        const page = await tab.pdfjsDoc.getPage(1); const viewport = page.getViewport({ scale: 2.0 });
        const canvas = document.createElement('canvas'); canvas.height = viewport.height; canvas.width = viewport.width;
        await page.render({ canvasContext: canvas.getContext('2d'), viewport: viewport }).promise;
        const res = await Tesseract.recognize(canvas.toDataURL('image/png'), 'eng');
        await saveBytesToFile(new Blob([res.data.text], {type: "text/plain"}), "OCR.txt", "txt", "text/plain");
    } catch(e) { showToast("OCR Failed."); } showLoading(false);
}

function toggleNightMode() { document.body.classList.toggle('night-mode'); }
function toggleReadMode() { document.body.classList.toggle('read-mode'); setTimeout(renderActiveViewer, 100); }
</script>
</body>
</html>
)HTML";

    return htmlPart1 + htmlPart2 + htmlPart3 + htmlPart4 + htmlPart5 + htmlPart6 + htmlPart7 + htmlPart8 + htmlPart9 + htmlPart10;
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
                    
                    ICoreWebView2Settings3* settings3;
                    if (SUCCEEDED(g_webView->QueryInterface(IID_PPV_ARGS(&settings3)))) {
                        settings3->put_AreFileAccessFromFileURLsEnabled(TRUE);
                        settings3->put_AreUniversalAccessFromFileURLsEnabled(TRUE);
                        settings3->Release();
                    }
                    
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
