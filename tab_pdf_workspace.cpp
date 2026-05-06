// tab_pdf_workspace.cpp
// Professional PDF Workspace (Sumatra PDF Style Tabs, Native Save, Advanced Tools)

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
// 🎨 HTML/CSS/JS UI - SPLIT INTO MULTIPLE SMALL PARTS 
// ==========================================
wstring GetAcrobatHTML() {
    
    // --- PART 1: HTML Head, Fonts & Libraries ---
    wstring htmlPart1 = LR"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>RasFocus PDF Pro Workspace</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
<script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/tesseract.js@4/dist/tesseract.min.js"></script>
<link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200" rel="stylesheet" />
)HTML";

    // --- PART 2: Base CSS & Variables ---
    wstring htmlPart2 = LR"HTML(
<style>
:root {
    --brand-color: #EB1C24;
    --brand-hover: #BA1617;
    --bg-dark: #2B2B2B;
    --bg-panel: #F8F9FA;
    --bg-doc: #E9ECEF;
    --border-color: #DEE2E6;
    --text-primary: #212529;
    --text-light: #F8F9FA;
    --text-muted: #6C757D;
    --hover-bg: #E2E6EA;
    --selected-bg: #FBECEE;
    --topbar-height: 40px;
    --toolbar-height: 44px;
    --tabs-height: 36px;
    --right-sidebar-width: 220px; /* Made smaller as requested */
}
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', system-ui, sans-serif; height: 100vh; overflow: hidden; display: flex; flex-direction: column; background: var(--bg-doc); color: var(--text-primary); user-select: none; }
.material-symbols-outlined { font-variation-settings: 'FILL' 0, 'wght' 400, 'GRAD' 0, 'opsz' 24; font-size: 20px; }
::-webkit-scrollbar { width: 8px; height: 8px; }
::-webkit-scrollbar-track { background: transparent; }
::-webkit-scrollbar-thumb { background: #bbb; border-radius: 4px; }
::-webkit-scrollbar-thumb:hover { background: #888; }
)HTML";

    // --- PART 3: Topbar & Sumatra Style Tabs CSS ---
    wstring htmlPart3 = LR"HTML(
.topbar { height: var(--topbar-height); background: var(--bg-dark); display: flex; align-items: center; padding: 0 16px; color: var(--text-light); z-index: 10; }
.topbar-menu { display: flex; gap: 20px; font-size: 13px; font-weight: 500; }
.topbar-item { cursor: pointer; opacity: 0.8; transition: opacity 0.2s; }
.topbar-item:hover { opacity: 1; }
.topbar-actions { display: flex; gap: 16px; margin-left: auto; }
.topbar-icon { cursor: pointer; opacity: 0.8; font-size: 22px; }
.topbar-icon:hover { opacity: 1; color: #FFCDD2; }

/* Sumatra PDF Style Tabs */
.tabs-container { height: var(--tabs-height); background: #D4D4D4; display: flex; align-items: flex-end; padding: 0 8px; gap: 4px; border-bottom: 1px solid var(--border-color); overflow-x: auto; }
.tab { background: #EAEAEA; padding: 6px 14px; border-radius: 6px 6px 0 0; font-size: 13px; display: flex; align-items: center; gap: 8px; cursor: pointer; max-width: 180px; border: 1px solid #CCC; border-bottom: none; box-shadow: inset 0 -2px 5px rgba(0,0,0,0.02); }
.tab.active { background: white; font-weight: 600; border-color: var(--border-color); box-shadow: 0 -2px 5px rgba(0,0,0,0.05); }
.tab-title { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.tab-close { font-size: 14px; width: 18px; height: 18px; display: flex; justify-content: center; align-items: center; border-radius: 50%; opacity: 0.6; }
.tab-close:hover { background: #FFCDD2; color: #D32F2F; opacity: 1; }
.tab-add { padding: 4px 10px; cursor: pointer; font-size: 20px; font-weight: 400; opacity: 0.7; border-radius: 4px; margin-bottom: 2px; }
.tab-add:hover { background: #C4C4C4; opacity: 1; }
)HTML";

    // --- PART 4: Toolbar & Workspace CSS ---
    wstring htmlPart4 = LR"HTML(
.toolbar { height: var(--toolbar-height); background: white; border-bottom: 1px solid var(--border-color); display: flex; align-items: center; padding: 0 16px; gap: 12px; font-size: 14px; box-shadow: 0 2px 4px rgba(0,0,0,0.02); z-index: 5; }
.tool-btn { display: flex; align-items: center; gap: 6px; cursor: pointer; padding: 6px 8px; border-radius: 4px; color: var(--text-primary); transition: 0.2s; }
.tool-btn:hover { background: var(--hover-bg); }
.tool-btn.active { background: var(--selected-bg); color: var(--brand-color); border: 1px solid #F8BBD0; }
.divider { width: 1px; height: 24px; background: var(--border-color); margin: 0 4px; }

.workspace { flex: 1; display: flex; overflow: hidden; position: relative; }
.right-sidebar { width: var(--right-sidebar-width); background: white; border-left: 1px solid var(--border-color); display: flex; flex-direction: column; overflow-y: auto; box-shadow: -2px 0 5px rgba(0,0,0,0.02); z-index: 4; }
.sidebar-header { padding: 12px 16px; font-size: 12px; font-weight: 700; letter-spacing: 0.5px; text-transform: uppercase; color: var(--text-muted); border-bottom: 1px solid var(--border-color); background: #FAFAFA; }
.tool-pane-btn { display: flex; align-items: center; gap: 10px; padding: 10px 16px; cursor: pointer; border-bottom: 1px solid #F0F0F0; transition: 0.2s; }
.tool-pane-btn:hover { background: var(--selected-bg); padding-left: 20px; }
.tool-pane-btn .material-symbols-outlined { color: var(--brand-color); font-size: 20px; }
.tool-pane-text { font-size: 13px; font-weight: 500; }
)HTML";

    // --- PART 5: Document Viewer & Modals CSS ---
    wstring htmlPart5 = LR"HTML(
.pdf-viewer-area { flex: 1; overflow-y: auto; overflow-x: hidden; display: flex; justify-content: center; padding: 24px 0; background: var(--bg-doc); }
.pdf-container { display: flex; flex-direction: column; gap: 16px; align-items: center; width: 100%; cursor: default; }
.pdf-page-wrapper { background: white; box-shadow: 0 4px 12px rgba(0,0,0,0.1); margin-bottom: 10px; position: relative; }
.pdf-page-wrapper canvas { display: block; }

.toast-container { position: fixed; bottom: 30px; left: 50%; transform: translateX(-50%); z-index: 9999; display: flex; flex-direction: column; gap: 8px; pointer-events: none; }
.toast { padding: 10px 24px; border-radius: 6px; color: white; background: rgba(40,40,40,0.95); font-size: 14px; box-shadow: 0 4px 12px rgba(0,0,0,0.2); animation: fadeInUp 0.3s ease; backdrop-filter: blur(4px); }
@keyframes fadeInUp { from { transform: translateY(20px); opacity: 0; } to { transform: translateY(0); opacity: 1; } }

.loading-overlay, .modal-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.6); z-index: 2000; justify-content: center; align-items: center; backdrop-filter: blur(2px); }
.loading-overlay.show, .modal-overlay.show { display: flex; flex-direction: column; }
.spinner { border: 4px solid rgba(255,255,255,0.2); border-top: 4px solid white; border-radius: 50%; width: 40px; height: 40px; animation: spin 1s linear infinite; margin-bottom: 16px; }
@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
.modal { background: white; border-radius: 8px; padding: 24px; min-width: 350px; box-shadow: 0 10px 30px rgba(0,0,0,0.3); }
.modal h3 { font-size: 18px; margin-bottom: 16px; font-weight: 600; color: var(--text-primary); }
.modal input { width: 100%; padding: 10px; margin-bottom: 16px; border: 1px solid var(--border-color); border-radius: 4px; outline: none; }
.modal input:focus { border-color: var(--brand-color); }
.modal-actions { display: flex; gap: 10px; justify-content: flex-end; }
.btn { padding: 8px 16px; border: none; border-radius: 4px; cursor: pointer; font-size: 14px; font-weight: 500; transition: 0.2s; }
.btn-primary { background: var(--brand-color); color: white; }
.btn-primary:hover { background: var(--brand-hover); }
.btn-secondary { background: #E0E0E0; color: var(--text-primary); }
.btn-secondary:hover { background: #D0D0D0; }
)HTML";

    // --- PART 6: Night Mode, Read Mode & HTML Body ---
    wstring htmlPart6 = LR"HTML(
/* Night & Read Mode Logic */
body.night-mode .pdf-viewer-area { background: #1E1E1E !important; }
body.night-mode .toolbar, body.night-mode .tabs-container { background: #2D2D2D !important; border-color: #444 !important; color: #E0E0E0 !important; }
body.night-mode .tab { background: #3A3A3A; color: #E0E0E0; border-color: #555; }
body.night-mode .tab.active { background: #1E1E1E; color: white; border-color: #444; }
body.night-mode .right-sidebar { background: #252525 !important; border-color: #444 !important; }
body.night-mode .tool-pane-btn { border-color: #333 !important; color: #E0E0E0 !important; }
body.night-mode .tool-pane-btn:hover { background: #333 !important; }
body.night-mode .pdf-page-wrapper { box-shadow: 0 4px 15px rgba(0,0,0,0.6); }

/* READ MODE: Hides Tabs, Toolbar, and Sidebar */
body.read-mode .tabs-container, body.read-mode .toolbar, body.read-mode .right-sidebar { display: none !important; }

</style>
</head>
<body>
<div class="toast-container" id="toast-container"></div>
<div class="loading-overlay" id="loading-overlay"><div class="spinner"></div><div id="loading-text" style="color:white; font-size:16px;">Processing...</div></div>
<div class="modal-overlay" id="modal-overlay"><div class="modal" id="modal-content"></div></div>

<div class="topbar">
    <div class="topbar-menu">
        <div class="topbar-item" onclick="document.getElementById('fileInput').click()">File</div>
        <div class="topbar-item" onclick="downloadCurrentPDF()">Save As...</div>
    </div>
    <div class="topbar-actions">
        <span class="material-symbols-outlined topbar-icon" onclick="toggleNightMode()" title="Night Mode">dark_mode</span>
        <span class="material-symbols-outlined topbar-icon" onclick="toggleReadMode()" title="Read Mode" id="read-mode-icon">menu_book</span>
        <span class="material-symbols-outlined topbar-icon" onclick="downloadCurrentPDF()" title="Save to Folder">save</span>
    </div>
</div>

<div class="tabs-container" id="tabs-container">
    </div>
)HTML";

    // --- PART 7: Toolbar & Sidebar HTML ---
    wstring htmlPart7 = LR"HTML(
<div class="toolbar">
    <div class="tool-btn active" onclick="setStudyTool('pointer')" id="tool-pointer" title="Select Tool"><span class="material-symbols-outlined">near_me</span></div>
    <div class="tool-btn" onclick="setStudyTool('hand')" id="tool-hand" title="Pan/Hand Tool"><span class="material-symbols-outlined">pan_tool</span></div>
    <div class="divider"></div>
    <span style="font-weight:600; font-size:11px; color:var(--text-muted); letter-spacing:0.5px;">MARKUP:</span>
    <div class="tool-btn" onclick="setStudyTool('highlight')" id="tool-highlight" title="Highlight"><span class="material-symbols-outlined" style="color: #FBC02D;">format_ink_highlighter</span></div>
    <div class="tool-btn" onclick="setStudyTool('note')" id="tool-note" title="Sticky Note"><span class="material-symbols-outlined" style="color: #4CAF50;">speaker_notes</span></div>
    <div class="tool-btn" onclick="setStudyTool('link')" id="tool-link" title="Insert Link"><span class="material-symbols-outlined" style="color: #1976D2;">link</span></div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="zoomOut()"><span class="material-symbols-outlined">remove</span></div>
    <span id="zoom-text" style="font-size:13px; width:45px; text-align:center; font-weight:500;">100%</span>
    <div class="tool-btn" onclick="zoomIn()"><span class="material-symbols-outlined">add</span></div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="rotatePDF()"><span class="material-symbols-outlined">rotate_right</span></div>
</div>

<div class="workspace">
    <div class="pdf-viewer-area" id="viewer-area">
        <div class="pdf-container" id="pdf-container">
            <div style="margin-top: 15vh; text-align: center; color: var(--text-muted);">
                <span class="material-symbols-outlined" style="font-size: 72px; opacity: 0.5;">note_add</span>
                <p style="margin-top: 16px; font-size: 18px; font-weight:500;">No Document Open</p>
                <p style="margin-top: 8px; font-size: 14px; opacity: 0.8;">Click + or File > Open to start</p>
            </div>
        </div>
    </div>

    <div class="right-sidebar">
        <div class="sidebar-header">Advanced Tools</div>
        <div class="tool-pane-btn" onclick="uiShowMergeModal()"><span class="material-symbols-outlined">library_add</span><span class="tool-pane-text">Combine PDFs</span></div>
        <div class="tool-pane-btn" onclick="uiShowSplitModal()"><span class="material-symbols-outlined">splitscreen</span><span class="tool-pane-text">Split PDF</span></div>
        <div class="tool-pane-btn" onclick="uiShowExtractModal()"><span class="material-symbols-outlined">file_upload</span><span class="tool-pane-text">Extract Pages</span></div>
        <div class="tool-pane-btn" onclick="uiShowDeleteModal()"><span class="material-symbols-outlined">delete</span><span class="tool-pane-text">Delete Pages</span></div>
        <div class="tool-pane-btn" onclick="actionPDFtoImage()"><span class="material-symbols-outlined">image</span><span class="tool-pane-text">Export as Images</span></div>
        <div class="tool-pane-btn" onclick="actionPDFtoText()"><span class="material-symbols-outlined">article</span><span class="tool-pane-text">Export Text</span></div>
        <div class="tool-pane-btn" onclick="actionPerformOCR()"><span class="material-symbols-outlined">document_scanner</span><span class="tool-pane-text">OCR Scan</span></div>
        <div class="tool-pane-btn" onclick="actionAddWatermark()"><span class="material-symbols-outlined">branding_watermark</span><span class="tool-pane-text">Watermark</span></div>
        <div class="tool-pane-btn" onclick="actionAddStamp()"><span class="material-symbols-outlined">verified</span><span class="tool-pane-text">Approved Stamp</span></div>
    </div>
</div>
<input type="file" id="fileInput" accept=".pdf" multiple style="display:none;" onchange="handleFileOpen(event)">
)HTML";

    // --- PART 8: JS Core logic, Sumatra Tabs & Touchpad Zoom ---
    wstring htmlPart8 = LR"HTML(
<script>
let appTabs = [];
let activeTabId = null;
let currentPdfBytes = null; 
let currentPdfjsDoc = null; 
let currentZoom = 1.0; 
let currentRotation = 0;
let currentStudyTool = 'pointer';

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

// NATIVE FOLDER SAVE DIALOG (Prevents Auto-Download)
async function saveFileNative(blob, defaultName) {
    try {
        if (window.showSaveFilePicker) {
            const ext = defaultName.split('.').pop();
            const types = [{ description: ext.toUpperCase() + ' File', accept: { '*/*': ['.'+ext] } }];
            const handle = await window.showSaveFilePicker({ suggestedName: defaultName, types: types });
            const writable = await handle.createWritable();
            await writable.write(blob);
            await writable.close();
            showToast("Saved to your chosen folder!");
        } else {
            saveAs(blob, defaultName); // Fallback if API restricted
        }
    } catch (err) {
        if (err.name !== 'AbortError') showToast("Save cancelled or failed.");
    }
}
async function downloadBytes(bytes, filename) {
    const blob = new Blob([bytes], { type: "application/pdf" });
    await saveFileNative(blob, filename);
}
function downloadCurrentPDF() { 
    if (currentPdfBytes && activeTabId) {
        const tab = appTabs.find(t => t.id === activeTabId);
        downloadBytes(currentPdfBytes, tab.name); 
    } else { showToast("Open a PDF to save."); }
}

// SUMATRA PDF STYLE TABS LOGIC
async function handleFileOpen(event) {
    const files = event.target.files;
    if (!files.length) return;
    for (let file of files) {
        const arrayBuffer = await file.arrayBuffer();
        await addTab(file.name, new Uint8Array(arrayBuffer));
    }
    event.target.value = ''; // Reset
}
async function loadPdfFromPath(path) { // C++ Native Call
    try {
        const response = await fetch('file:///' + path.replace(/\\/g, '/'));
        const arrayBuffer = await response.arrayBuffer();
        const pathParts = path.split('\\');
        await addTab(pathParts[pathParts.length - 1], new Uint8Array(arrayBuffer));
    } catch (e) { showToast("Failed to load PDF."); }
}

async function addTab(name, bytes) {
    showLoading(true, "Loading Document...");
    try {
        const doc = await pdfjsLib.getDocument({data: bytes}).promise;
        const tabId = 'tab_' + Date.now() + Math.random().toString(36).substr(2, 5);
        appTabs.push({ id: tabId, name: name, bytes: bytes, doc: doc, zoom: 1.0, rotation: 0 });
        switchTab(tabId);
    } catch(e) { showToast("Error rendering " + name); }
    showLoading(false);
}

function renderTabsBar() {
    const container = document.getElementById('tabs-container');
    container.innerHTML = '';
    appTabs.forEach(t => {
        const div = document.createElement('div');
        div.className = 'tab' + (t.id === activeTabId ? ' active' : '');
        div.onclick = () => switchTab(t.id);
        div.innerHTML = `<span class="tab-title">${t.name}</span> <span class="tab-close" onclick="event.stopPropagation(); closeTab('${t.id}')">✕</span>`;
        container.appendChild(div);
    });
    const addBtn = document.createElement('div');
    addBtn.className = 'tab-add'; addBtn.textContent = '+';
    addBtn.onclick = () => document.getElementById('fileInput').click();
    container.appendChild(addBtn);
}

function switchTab(id) {
    if(activeTabId !== null) { // Save current state before switching
        let oldTab = appTabs.find(t => t.id === activeTabId);
        if(oldTab) { oldTab.zoom = currentZoom; oldTab.rotation = currentRotation; }
    }
    activeTabId = id; renderTabsBar();
    const tab = appTabs.find(t => t.id === id);
    if(tab) {
        currentPdfBytes = tab.bytes; currentPdfjsDoc = tab.doc;
        currentZoom = tab.zoom; currentRotation = tab.rotation;
        renderViewer();
    } else {
        currentPdfBytes = null; currentPdfjsDoc = null;
        document.getElementById('pdf-container').innerHTML = `<div style="margin-top: 15vh; text-align: center; color: var(--text-muted);"><span class="material-symbols-outlined" style="font-size: 72px; opacity: 0.5;">note_add</span><p style="margin-top: 16px; font-size: 18px; font-weight:500;">No Document Open</p></div>`;
    }
}

function closeTab(id) {
    appTabs = appTabs.filter(t => t.id !== id);
    if (activeTabId === id) {
        if (appTabs.length > 0) switchTab(appTabs[appTabs.length-1].id);
        else switchTab(null);
    } else { renderTabsBar(); }
}

async function renderViewer() {
    if (!currentPdfjsDoc) return;
    const container = document.getElementById('pdf-container'); container.innerHTML = '';
    document.getElementById('zoom-text').textContent = Math.round(currentZoom * 100) + '%';
    for (let i = 1; i <= currentPdfjsDoc.numPages; i++) {
        const page = await currentPdfjsDoc.getPage(i);
        const viewport = page.getViewport({ scale: currentZoom, rotation: currentRotation });
        const wrapper = document.createElement('div'); wrapper.className = 'pdf-page-wrapper'; wrapper.id = `page-${i}`;
        const canvas = document.createElement('canvas'); const context = canvas.getContext('2d');
        canvas.height = viewport.height; canvas.width = viewport.width;
        wrapper.appendChild(canvas); container.appendChild(wrapper);
        await page.render({ canvasContext: context, viewport: viewport }).promise;
    }
}

// Touchpad Zoom (Ctrl + Wheel)
document.getElementById('viewer-area').addEventListener('wheel', (e) => {
    if (e.ctrlKey) {
        e.preventDefault();
        if (e.deltaY < 0) zoomIn(); else zoomOut();
    }
}, { passive: false });

function zoomIn() { if (currentZoom < 3.0) { currentZoom += 0.2; renderViewer(); } }
function zoomOut() { if (currentZoom > 0.4) { currentZoom -= 0.2; renderViewer(); } }
function rotatePDF() { currentRotation = (currentRotation + 90) % 360; renderViewer(); }
)HTML";

    // --- PART 9: Advanced PDF Features (Merge, Split, Watermark) ---
    wstring htmlPart9 = LR"HTML(
function uiShowMergeModal() {
    showModal("Combine Files", `<p style="margin-bottom:8px;font-size:13px;">Select PDFs to combine:</p><input type="file" id="mergeFiles" accept=".pdf" multiple>
        <div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" onclick="actionMergeFiles()">Combine</button></div>`);
}
async function actionMergeFiles() {
    const files = document.getElementById('mergeFiles').files; if (files.length < 2) return alert("Select at least 2 files.");
    closeModal(); showLoading(true, "Merging PDFs...");
    try {
        const mergedPdf = await PDFLib.PDFDocument.create();
        for (let file of files) {
            const pdf = await PDFLib.PDFDocument.load(new Uint8Array(await file.arrayBuffer()));
            const copiedPages = await mergedPdf.copyPages(pdf, pdf.getPageIndices());
            copiedPages.forEach((page) => mergedPdf.addPage(page));
        }
        await addTab("Combined_Doc.pdf", await mergedPdf.save()); showToast("Merged Successfully");
    } catch(e) { showToast("Failed to merge."); }
    showLoading(false);
}

function uiShowSplitModal() {
    if(!currentPdfBytes) return showToast("Open a PDF first.");
    showModal("Split PDF", `<p style="margin-bottom:8px;font-size:13px;">Split after page number:</p><input type="number" id="splitPage" min="1" max="${currentPdfjsDoc.numPages - 1}" value="1">
        <div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" onclick="actionSplitPDF()">Split</button></div>`);
}
async function actionSplitPDF() {
    const splitAt = parseInt(document.getElementById('splitPage').value); closeModal(); showLoading(true, "Splitting...");
    try {
        const srcDoc = await PDFLib.PDFDocument.load(currentPdfBytes);
        const doc1 = await PDFLib.PDFDocument.create(); const doc2 = await PDFLib.PDFDocument.create();
        const indices = srcDoc.getPageIndices();
        const copied1 = await doc1.copyPages(srcDoc, indices.slice(0, splitAt)); copied1.forEach(p => doc1.addPage(p));
        const copied2 = await doc2.copyPages(srcDoc, indices.slice(splitAt)); copied2.forEach(p => doc2.addPage(p));
        await downloadBytes(await doc1.save(), "Split_Part1.pdf"); await downloadBytes(await doc2.save(), "Split_Part2.pdf");
    } catch(e) { showToast("Failed to split."); }
    showLoading(false);
}

function uiShowExtractModal() {
    if(!currentPdfBytes) return showToast("Open a PDF first.");
    showModal("Extract Pages", `<p style="margin-bottom:8px;font-size:13px;">Page numbers (e.g., 1, 3):</p><input type="text" id="extractPagesInput" placeholder="1, 3">
        <div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" onclick="actionExtractPages()">Extract</button></div>`);
}
async function actionExtractPages() {
    const input = document.getElementById('extractPagesInput').value;
    const pagesToExtract = input.split(',').map(n => parseInt(n.trim()) - 1).filter(n => !isNaN(n) && n >= 0 && n < currentPdfjsDoc.numPages);
    if(pagesToExtract.length === 0) return alert("Invalid page numbers");
    closeModal(); showLoading(true, "Extracting...");
    try {
        const srcDoc = await PDFLib.PDFDocument.load(currentPdfBytes); const newDoc = await PDFLib.PDFDocument.create();
        const copied = await newDoc.copyPages(srcDoc, pagesToExtract); copied.forEach(p => newDoc.addPage(p));
        await addTab("Extracted_Pages.pdf", await newDoc.save()); showToast("Extracted Successfully");
    } catch(e) { showToast("Extraction failed."); }
    showLoading(false);
}

function uiShowDeleteModal() {
    if(!currentPdfBytes) return showToast("Open a PDF first.");
    showModal("Delete Page", `<p style="margin-bottom:8px;font-size:13px;">Enter page number to delete:</p><input type="number" id="deletePage" min="1" max="${currentPdfjsDoc.numPages}">
        <div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" style="background:#D32F2F;" onclick="actionDeletePage()">Delete</button></div>`);
}
async function actionDeletePage() {
    const pageNum = parseInt(document.getElementById('deletePage').value) - 1; closeModal(); showLoading(true, "Deleting...");
    try {
        const srcDoc = await PDFLib.PDFDocument.load(currentPdfBytes); srcDoc.removePage(pageNum);
        const newBytes = await srcDoc.save();
        const tab = appTabs.find(t => t.id === activeTabId);
        tab.bytes = newBytes; tab.doc = await pdfjsLib.getDocument({data: newBytes}).promise;
        switchTab(activeTabId); showToast("Page deleted.");
    } catch(e) { showToast("Failed to delete."); }
    showLoading(false);
}
)HTML";

    // --- PART 10: Study Tools (Toggleable), Export, OCR & Modes ---
    wstring htmlPart10 = LR"HTML(
function setStudyTool(tool) {
    // If clicked again, cancel tool
    if (currentStudyTool === tool) {
        currentStudyTool = 'pointer';
    } else {
        currentStudyTool = tool;
    }
    
    document.querySelectorAll('.toolbar .tool-btn').forEach(btn => btn.classList.remove('active'));
    if(currentStudyTool === 'highlight') document.getElementById('tool-highlight').classList.add('active');
    else if(currentStudyTool === 'note') document.getElementById('tool-note').classList.add('active');
    else if(currentStudyTool === 'link') document.getElementById('tool-link').classList.add('active');
    else if(currentStudyTool === 'hand') document.getElementById('tool-hand').classList.add('active');
    else document.getElementById('tool-pointer').classList.add('active');
    
    const container = document.getElementById('pdf-container');
    if (currentStudyTool === 'hand') container.style.cursor = 'grab';
    else if (currentStudyTool === 'pointer') container.style.cursor = 'default';
    else container.style.cursor = 'crosshair';
}

document.getElementById('pdf-container').addEventListener('click', async (e) => {
    if (!currentStudyTool || currentStudyTool === 'pointer' || currentStudyTool === 'hand' || !currentPdfBytes) return;
    const pageWrapper = e.target.closest('.pdf-page-wrapper'); if (!pageWrapper) return; 
    const pageIndex = parseInt(pageWrapper.id.split('-')[1]) - 1;
    const rect = e.target.getBoundingClientRect(); const canvasX = e.clientX - rect.left; const canvasY = e.clientY - rect.top;

    showLoading(true, `Applying Tool...`);
    try {
        const pdfDoc = await PDFLib.PDFDocument.load(currentPdfBytes);
        const page = pdfDoc.getPages()[pageIndex]; const { width, height } = page.getSize();
        const pdfX = (canvasX / rect.width) * width; const pdfY = height - ((canvasY / rect.height) * height); 

        if (currentStudyTool === 'highlight') {
            page.drawRectangle({ x: pdfX, y: pdfY - 5, width: 120, height: 15, color: PDFLib.rgb(1, 1, 0), opacity: 0.4 });
        } else if (currentStudyTool === 'note') {
            const note = prompt("Enter your study note:");
            if (note) {
                page.drawRectangle({ x: pdfX, y: pdfY - 30, width: 200, height: 40, color: PDFLib.rgb(1, 0.98, 0.8), borderColor: PDFLib.rgb(0.9, 0.7, 0.3), borderWidth: 1 });
                page.drawText("📝 " + note, { x: pdfX + 5, y: pdfY - 15, size: 12, color: PDFLib.rgb(0,0,0) });
            } else { showLoading(false); return; }
        } else if (currentStudyTool === 'link') {
            const url = prompt("Enter link (e.g., https://...):");
            if (url) {
                page.drawText("🔗 " + url, { x: pdfX, y: pdfY, size: 10, color: PDFLib.rgb(0, 0, 1) });
                const linkAnnot = pdfDoc.context.obj({ Type: 'Annot', Subtype: 'Link', Rect: [pdfX, pdfY - 5, pdfX + 150, pdfY + 10], Border: [0, 0, 0], A: { Type: 'Action', S: 'URI', URI: PDFLib.PDFString.of(url) } });
                const linkAnnotRef = pdfDoc.context.register(linkAnnot);
                let annots = page.node.Annots();
                if (!annots) { annots = pdfDoc.context.obj([]); page.node.set(PDFLib.PDFName.of('Annots'), annots); }
                annots.push(linkAnnotRef);
            } else { showLoading(false); return; }
        }
        
        const newBytes = await pdfDoc.save();
        const tab = appTabs.find(t => t.id === activeTabId);
        tab.bytes = newBytes; tab.doc = await pdfjsLib.getDocument({data: newBytes}).promise;
        switchTab(activeTabId);
    } catch (error) { showToast("Error applying tool."); }
    showLoading(false);
});

async function actionPDFtoImage() {
    if(!currentPdfjsDoc) return showToast("Open a PDF first.");
    showLoading(true, "Exporting Images...");
    try {
        const zip = new JSZip();
        for (let i = 1; i <= currentPdfjsDoc.numPages; i++) {
            const page = await currentPdfjsDoc.getPage(i); const viewport = page.getViewport({ scale: 2.0 });
            const canvas = document.createElement('canvas'); const ctx = canvas.getContext('2d'); canvas.height = viewport.height; canvas.width = viewport.width;
            await page.render({ canvasContext: ctx, viewport: viewport }).promise;
            const blob = await new Promise(resolve => canvas.toBlob(resolve, 'image/png')); zip.file(`Page_${i}.png`, blob);
        }
        const zipBlob = await zip.generateAsync({type:"blob"}); await saveFileNative(zipBlob, "PDF_Images.zip");
    } catch(e) { showToast("Failed to convert."); } showLoading(false);
}

async function actionPDFtoText() {
    if(!currentPdfjsDoc) return showToast("Open a PDF first.");
    showLoading(true, "Extracting Text...");
    try {
        let fullText = "";
        for (let i = 1; i <= currentPdfjsDoc.numPages; i++) {
            const page = await currentPdfjsDoc.getPage(i); const textContent = await page.getTextContent();
            fullText += `\n--- Page ${i} ---\n${textContent.items.map(item => item.str).join(' ')}\n`;
        }
        await saveFileNative(new Blob([fullText], { type: "text/plain;charset=utf-8" }), "Extracted_Text.txt");
    } catch(e) { showToast("Extraction failed."); } showLoading(false);
}

async function actionPerformOCR() {
    if (!currentPdfjsDoc) return showToast("Open a PDF first.");
    showLoading(true, "Scanning OCR...");
    try {
        const page = await currentPdfjsDoc.getPage(1); const viewport = page.getViewport({ scale: 2.0 });
        const canvas = document.createElement('canvas'); canvas.height = viewport.height; canvas.width = viewport.width;
        await page.render({ canvasContext: canvas.getContext('2d'), viewport: viewport }).promise;
        const result = await Tesseract.recognize(canvas.toDataURL('image/png'), 'eng');
        await saveFileNative(new Blob([result.data.text], { type: "text/plain;charset=utf-8" }), "OCR_Result.txt");
    } catch (e) { showToast("OCR Failed."); } showLoading(false);
}

async function actionAddWatermark() {
    if (!currentPdfBytes) return showToast("Open a PDF first.");
    const watermarkText = prompt("Enter watermark text:", "CONFIDENTIAL"); if (!watermarkText) return;
    showLoading(true, "Adding Watermark...");
    try {
        const pdfDoc = await PDFLib.PDFDocument.load(currentPdfBytes); const { rgb, degrees } = PDFLib;
        pdfDoc.getPages().forEach((page) => { const { width, height } = page.getSize(); page.drawText(watermarkText, { x: width / 2 - 150, y: height / 2, size: 60, color: rgb(0.9, 0.2, 0.2), opacity: 0.3, rotate: degrees(45) }); });
        const newBytes = await pdfDoc.save(); const tab = appTabs.find(t => t.id === activeTabId);
        tab.bytes = newBytes; tab.doc = await pdfjsLib.getDocument({data: newBytes}).promise; switchTab(activeTabId); showToast("Watermark added!");
    } catch (e) { showToast("Failed."); } showLoading(false);
}

async function actionAddStamp() {
    if (!currentPdfBytes) return showToast("Open a PDF first.");
    showLoading(true, "Applying Stamp...");
    try {
        const pdfDoc = await PDFLib.PDFDocument.load(currentPdfBytes); const firstPage = pdfDoc.getPages()[0]; 
        const { width, height } = firstPage.getSize(); const { rgb } = PDFLib;
        firstPage.drawRectangle({ x: width - 220, y: height - 100, width: 180, height: 50, borderColor: rgb(0.1, 0.6, 0.1), borderWidth: 3 });
        firstPage.drawText("APPROVED", { x: width - 200, y: height - 85, size: 30, color: rgb(0.1, 0.6, 0.1) });
        const newBytes = await pdfDoc.save(); const tab = appTabs.find(t => t.id === activeTabId);
        tab.bytes = newBytes; tab.doc = await pdfjsLib.getDocument({data: newBytes}).promise; switchTab(activeTabId); showToast("Stamp applied!");
    } catch (e) { showToast("Failed."); } showLoading(false);
}

function toggleNightMode() {
    document.body.classList.toggle('night-mode');
    showToast(document.body.classList.contains('night-mode') ? "Night Mode Activated 🌙" : "Light Mode Activated ☀️");
}

function toggleReadMode() {
    document.body.classList.toggle('read-mode');
    const isRead = document.body.classList.contains('read-mode');
    const iconEl = document.getElementById('read-mode-icon');
    if (isRead) {
        iconEl.textContent = 'fullscreen_exit'; iconEl.style.color = '#EB1C24';
        showToast("Read Mode Active. Only Topbar is visible.");
        if (currentZoom < 1.3) { currentZoom = 1.3; renderViewer(); } 
    } else {
        iconEl.textContent = 'menu_book'; iconEl.style.color = '';
        showToast("Normal Mode Active.");
    }
}
</script>
</body>
</html>
)HTML";

    // ✅ Return the complete concatenated string safely
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
        0, L"AcrobatWorkspaceClass", L"RasFocus PDF Pro",
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
void DrawPdfWorkspaceTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch) {}
void ProcessPdfWorkspaceMouseMove(float x, float y) {}
void ProcessPdfWorkspaceMouseClick(float x, float y) {}
