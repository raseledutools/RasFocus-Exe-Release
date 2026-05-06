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
    
    // --- PART 1: HTML Head, Fonts, Libs & CSS Variables ---
    wstring htmlPart1 = LR"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>PDF Workspace - Acrobat Edition</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
<script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/tesseract.js@4/dist/tesseract.min.js"></script>
<link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200" rel="stylesheet" />

<style>
:root {
    --adobe-red: #EB1C24;
    --adobe-red-hover: #BA1617;
    --bg-dark: #333333;
    --bg-panel: #F4F4F4;
    --bg-doc: #E1E1E1;
    --border-color: #CCCCCC;
    --text-primary: #2C2C2C;
    --text-light: #FFFFFF;
    --text-muted: #757575;
    --hover-bg: #EAEAEA;
    --selected-bg: #FBECEE;
    --topbar-height: 48px;
    --toolbar-height: 40px;
    --left-sidebar-width: 240px;
    --right-sidebar-width: 280px;
}
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', system-ui, sans-serif; height: 100vh; overflow: hidden; display: flex; flex-direction: column; background: var(--bg-doc); color: var(--text-primary); user-select: none; }
.material-symbols-outlined { font-variation-settings: 'FILL' 0, 'wght' 400, 'GRAD' 0, 'opsz' 24; font-size: 20px; }
)HTML";

    // --- PART 2: UI Modals, Toasts & Layout CSS ---
    wstring htmlPart2 = LR"HTML(
.toast-container { position: fixed; bottom: 30px; left: 50%; transform: translateX(-50%); z-index: 9999; display: flex; flex-direction: column; gap: 8px; }
.toast { padding: 10px 24px; border-radius: 4px; color: white; background: #323232; font-size: 14px; box-shadow: 0 4px 12px rgba(0,0,0,0.2); animation: fadeInUp 0.3s ease; }
@keyframes fadeInUp { from { transform: translateY(20px); opacity: 0; } to { transform: translateY(0); opacity: 1; } }

.modal-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.5); z-index: 1000; justify-content: center; align-items: center; }
.modal-overlay.show { display: flex; }
.modal { background: white; border-radius: 6px; padding: 24px; min-width: 350px; box-shadow: 0 8px 24px rgba(0,0,0,0.2); }
.modal h3 { font-size: 18px; margin-bottom: 16px; font-weight: 600; }
.modal input[type="file"], .modal input[type="number"], .modal input[type="text"] { width: 100%; padding: 8px; margin-bottom: 16px; border: 1px solid var(--border-color); border-radius: 4px; }
.modal-actions { display: flex; gap: 8px; justify-content: flex-end; }
.btn { padding: 8px 16px; border: none; border-radius: 4px; cursor: pointer; font-size: 14px; font-weight: 500; transition: 0.2s; }
.btn-primary { background: var(--adobe-red); color: white; }
.btn-primary:hover { background: var(--adobe-red-hover); }
.btn-secondary { background: transparent; border: 1px solid var(--border-color); color: var(--text-primary); }
.btn-secondary:hover { background: var(--hover-bg); }

.topbar { height: var(--topbar-height); background: var(--bg-dark); display: flex; align-items: center; padding: 0 16px; color: var(--text-light); }
.topbar-menu { display: flex; gap: 20px; font-size: 13px; }
.topbar-item { cursor: pointer; opacity: 0.8; transition: opacity 0.2s; }
.topbar-item:hover { opacity: 1; }
.topbar-title { margin-left: auto; margin-right: auto; font-size: 14px; font-weight: 500; opacity: 0.9; }
.topbar-actions { display: flex; gap: 12px; margin-left: auto; }
.topbar-icon { cursor: pointer; opacity: 0.8; }
.topbar-icon:hover { opacity: 1; }
)HTML";

    // --- PART 3: Workspace Layout CSS & Night Mode ---
    wstring htmlPart3 = LR"HTML(
.toolbar { height: var(--toolbar-height); background: white; border-bottom: 1px solid var(--border-color); display: flex; align-items: center; padding: 0 16px; gap: 16px; font-size: 14px; }
.tool-btn { display: flex; align-items: center; gap: 6px; cursor: pointer; padding: 6px 10px; border-radius: 4px; color: var(--text-primary); transition: 0.2s; }
.tool-btn:hover { background: var(--hover-bg); }
.tool-btn.active { background: var(--selected-bg); color: var(--adobe-red); }
.divider { width: 1px; height: 20px; background: var(--border-color); }

.workspace { flex: 1; display: flex; overflow: hidden; }
.left-sidebar { width: var(--left-sidebar-width); background: var(--bg-panel); border-right: 1px solid var(--border-color); display: flex; flex-direction: column; }
.sidebar-header { padding: 12px 16px; font-size: 12px; font-weight: 600; text-transform: uppercase; color: var(--text-muted); border-bottom: 1px solid var(--border-color); }
.thumbnail-list { flex: 1; overflow-y: auto; padding: 12px; display: flex; flex-direction: column; gap: 12px; }
.thumb-item { border: 1px solid var(--border-color); background: white; padding: 4px; cursor: pointer; transition: 0.2s; position: relative; }
.thumb-item:hover { border-color: var(--adobe-red); }
.thumb-item.active { border: 2px solid var(--adobe-red); }
.thumb-item canvas { width: 100%; height: auto; display: block; }
.thumb-label { text-align: center; font-size: 11px; margin-top: 4px; color: var(--text-muted); }

.right-sidebar { width: var(--right-sidebar-width); background: var(--bg-panel); border-left: 1px solid var(--border-color); display: flex; flex-direction: column; overflow-y: auto; }
.tool-pane-btn { display: flex; align-items: center; gap: 12px; padding: 12px 16px; cursor: pointer; border-bottom: 1px solid var(--border-color); background: white; transition: 0.2s; }
.tool-pane-btn:hover { background: var(--selected-bg); }
.tool-pane-btn .material-symbols-outlined { color: var(--adobe-red); font-size: 24px; }
.tool-pane-text { font-size: 14px; font-weight: 500; }

.pdf-viewer-area { flex: 1; overflow-y: auto; display: flex; justify-content: center; padding: 24px; background: var(--bg-doc); }
.pdf-container { display: flex; flex-direction: column; gap: 20px; align-items: center; width: 100%; cursor: default; }
.pdf-page-wrapper { background: white; box-shadow: 0 2px 8px rgba(0,0,0,0.15); margin-bottom: 20px; position: relative; }
.pdf-page-wrapper canvas { display: block; }

.loading-overlay { display: none; position: fixed; inset: 0; background: rgba(0,0,0,0.6); z-index: 2000; flex-direction: column; justify-content: center; align-items: center; color: white; font-size: 16px; }
.loading-overlay.show { display: flex; }
.spinner { border: 4px solid rgba(255,255,255,0.3); border-top: 4px solid white; border-radius: 50%; width: 40px; height: 40px; animation: spin 1s linear infinite; margin-bottom: 16px; }
@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }

/* Night Mode & Read Mode */
body.night-mode .pdf-viewer-area { background: #1a1a1a !important; }
body.night-mode .toolbar { background: #252525 !important; border-color: #333 !important; color: #e0e0e0 !important; }
body.night-mode .toolbar .tool-btn { color: #e0e0e0 !important; }
body.night-mode .toolbar .tool-btn:hover { background: #333 !important; }
body.night-mode .toolbar .divider { background: #333 !important; }
body.night-mode .left-sidebar, body.night-mode .right-sidebar { background: #252525 !important; border-color: #333 !important; }
body.night-mode .sidebar-header { color: #aaa !important; border-color: #333 !important; }
body.night-mode .thumb-item { background: #1a1a1a !important; border-color: #333 !important; }
body.night-mode .tool-pane-btn { background: #252525 !important; border-color: #333 !important; color: #e0e0e0 !important; }
body.night-mode .tool-pane-btn:hover { background: #333 !important; }
body.night-mode .pdf-page-wrapper { box-shadow: 0 4px 15px rgba(0,0,0,0.5); }
body.read-mode .toolbar, body.read-mode .left-sidebar, body.read-mode .right-sidebar { display: none !important; }
</style>
</head>
<body>
<div class="toast-container" id="toast-container"></div>
<div class="loading-overlay" id="loading-overlay"><div class="spinner"></div><div id="loading-text">Processing...</div></div>
<div class="modal-overlay" id="modal-overlay"><div class="modal" id="modal-content"></div></div>
)HTML";

    // --- PART 4: HTML Structure (Body, Toolbar, Sidebars) ---
    wstring htmlPart4 = LR"HTML(
<div class="topbar">
    <div class="topbar-menu">
        <div class="topbar-item" onclick="document.getElementById('fileInput').click()">File</div>
        <div class="topbar-item" onclick="downloadCurrentPDF()">Save</div>
    </div>
    <div class="topbar-title" id="app-title">RasFocus PDF Pro</div>
    <div class="topbar-actions">
        <span class="material-symbols-outlined topbar-icon" onclick="toggleNightMode()" title="Toggle Night Mode">dark_mode</span>
        <span class="material-symbols-outlined topbar-icon" onclick="toggleReadMode()" title="Toggle Read Mode" id="read-mode-icon">menu_book</span>
        <span class="material-symbols-outlined topbar-icon" onclick="downloadCurrentPDF()" title="Download PDF">download</span>
    </div>
</div>

<div class="toolbar">
    <div class="tool-btn active" onclick="setStudyTool(null)" id="tool-pointer" title="Normal Pointer"><span class="material-symbols-outlined">pan_tool</span></div>
    <div class="divider"></div>
    <span style="font-weight:600; font-size:12px; color:var(--text-muted);">STUDY TOOLS:</span>
    <div class="tool-btn" onclick="setStudyTool('highlight')" id="tool-highlight" title="Highlight Text"><span class="material-symbols-outlined" style="color: #FFC107;">format_ink_highlighter</span></div>
    <div class="tool-btn" onclick="setStudyTool('note')" id="tool-note" title="Add Sticky Note"><span class="material-symbols-outlined" style="color: #4CAF50;">speaker_notes</span></div>
    <div class="tool-btn" onclick="setStudyTool('link')" id="tool-link" title="Add URL Link"><span class="material-symbols-outlined" style="color: #2196F3;">link</span></div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="zoomOut()"><span class="material-symbols-outlined">remove</span></div>
    <span id="zoom-text" style="font-size:13px;width:40px;text-align:center;">100%</span>
    <div class="tool-btn" onclick="zoomIn()"><span class="material-symbols-outlined">add</span></div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="rotatePDF()"><span class="material-symbols-outlined">rotate_right</span></div>
</div>

<div class="workspace">
    <div class="left-sidebar">
        <div class="sidebar-header">Page Thumbnails</div>
        <div class="thumbnail-list" id="thumbnail-list"></div>
    </div>

    <div class="pdf-viewer-area" id="viewer-area">
        <div class="pdf-container" id="pdf-container">
            <div style="margin-top: 100px; text-align: center; color: var(--text-muted);">
                <span class="material-symbols-outlined" style="font-size: 64px; color: #ccc;">description</span>
                <p style="margin-top: 16px; font-size: 16px;">Open a PDF to begin</p>
                <button class="btn btn-primary" style="margin-top: 16px;" onclick="document.getElementById('fileInput').click()">Open File</button>
            </div>
        </div>
    </div>

    <div class="right-sidebar">
        <div class="sidebar-header">Advanced Tools</div>
        <div class="tool-pane-btn" onclick="uiShowMergeModal()"><span class="material-symbols-outlined">library_add</span><span class="tool-pane-text">Combine Files</span></div>
        <div class="tool-pane-btn" onclick="uiShowSplitModal()"><span class="material-symbols-outlined">splitscreen</span><span class="tool-pane-text">Split PDF</span></div>
        <div class="tool-pane-btn" onclick="uiShowExtractModal()"><span class="material-symbols-outlined">file_upload</span><span class="tool-pane-text">Extract Pages</span></div>
        <div class="tool-pane-btn" onclick="uiShowDeleteModal()"><span class="material-symbols-outlined">delete</span><span class="tool-pane-text">Delete Pages</span></div>
        <div class="tool-pane-btn" onclick="actionPDFtoImage()"><span class="material-symbols-outlined">image</span><span class="tool-pane-text">Export to Image (ZIP)</span></div>
        <div class="tool-pane-btn" onclick="actionPDFtoText()"><span class="material-symbols-outlined">article</span><span class="tool-pane-text">Export Text (.txt)</span></div>
        <div class="tool-pane-btn" onclick="actionPerformOCR()"><span class="material-symbols-outlined">document_scanner</span><span class="tool-pane-text">OCR Scan (English)</span></div>
        <div class="tool-pane-btn" onclick="actionAddWatermark()"><span class="material-symbols-outlined">branding_watermark</span><span class="tool-pane-text">Add Watermark</span></div>
        <div class="tool-pane-btn" onclick="actionAddStamp()"><span class="material-symbols-outlined">verified</span><span class="tool-pane-text">Add "APPROVED" Stamp</span></div>
    </div>
</div>
<input type="file" id="fileInput" accept=".pdf" style="display:none;" onchange="handleFileOpen(event)">
)HTML";

    // --- PART 5: JS Base, Loading, Render Viewers ---
    wstring htmlPart5 = LR"HTML(
<script>
let currentPdfBytes = null; let currentPdfjsDoc = null; let currentZoom = 1.0; let currentRotation = 0;

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

async function handleFileOpen(event) {
    const file = event.target.files[0];
    if (!file) return;
    document.getElementById('app-title').textContent = file.name;
    const arrayBuffer = await file.arrayBuffer();
    await loadPDF(new Uint8Array(arrayBuffer));
}

// Called by C++ WebView2 natively
async function loadPdfFromPath(path) {
    try {
        const response = await fetch('file:///' + path.replace(/\\/g, '/'));
        const arrayBuffer = await response.arrayBuffer();
        const pathParts = path.split('\\');
        document.getElementById('app-title').textContent = pathParts[pathParts.length - 1];
        await loadPDF(new Uint8Array(arrayBuffer));
    } catch (e) { showToast("Failed to load PDF."); }
}

async function loadPDF(uint8Array) {
    try {
        currentPdfBytes = uint8Array;
        currentPdfjsDoc = await pdfjsLib.getDocument({data: uint8Array}).promise;
        currentZoom = 1.0; currentRotation = 0;
        await renderViewer(); await renderThumbnails();
        showToast("PDF Loaded Successfully");
    } catch (e) { console.error(e); showToast("Error rendering PDF"); }
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

async function renderThumbnails() {
    if (!currentPdfjsDoc) return;
    const thumbList = document.getElementById('thumbnail-list'); thumbList.innerHTML = '';
    for (let i = 1; i <= currentPdfjsDoc.numPages; i++) {
        const page = await currentPdfjsDoc.getPage(i);
        const viewport = page.getViewport({ scale: 0.2, rotation: currentRotation });
        const item = document.createElement('div'); item.className = 'thumb-item';
        item.onclick = () => document.getElementById(`page-${i}`).scrollIntoView({behavior: 'smooth'});
        const canvas = document.createElement('canvas'); canvas.height = viewport.height; canvas.width = viewport.width;
        const label = document.createElement('div'); label.className = 'thumb-label'; label.textContent = i;
        item.appendChild(canvas); item.appendChild(label); thumbList.appendChild(item);
        await page.render({ canvasContext: canvas.getContext('2d'), viewport: viewport }).promise;
    }
}
function zoomIn() { if (currentZoom < 3.0) { currentZoom += 0.2; renderViewer(); } }
function zoomOut() { if (currentZoom > 0.4) { currentZoom -= 0.2; renderViewer(); } }
function rotatePDF() { currentRotation = (currentRotation + 90) % 360; renderViewer(); renderThumbnails(); }
)HTML";

    // --- PART 6: JS Converters (Merge, Split, Extract, Delete) ---
    wstring htmlPart6 = LR"HTML(
function downloadBytes(bytes, filename) { const blob = new Blob([bytes], { type: "application/pdf" }); saveAs(blob, filename); }
function downloadCurrentPDF() { if (currentPdfBytes) downloadBytes(currentPdfBytes, "RasFocus_Document.pdf"); }

function uiShowMergeModal() {
    showModal("Combine Files", `<p>Select PDFs to combine:</p><input type="file" id="mergeFiles" accept=".pdf" multiple>
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
        const mergedBytes = await mergedPdf.save(); await loadPDF(mergedBytes); downloadBytes(mergedBytes, "Combined_Document.pdf");
    } catch(e) { showToast("Failed to merge."); }
    showLoading(false);
}

function uiShowSplitModal() {
    if(!currentPdfBytes) return showToast("Open a PDF first.");
    showModal("Split PDF", `<p>Split after page number:</p><input type="number" id="splitPage" min="1" max="${currentPdfjsDoc.numPages - 1}" value="1">
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
        downloadBytes(await doc1.save(), "Split_Part1.pdf"); downloadBytes(await doc2.save(), "Split_Part2.pdf");
    } catch(e) { showToast("Failed to split."); }
    showLoading(false);
}

function uiShowExtractModal() {
    if(!currentPdfBytes) return showToast("Open a PDF first.");
    showModal("Extract Pages", `<p>Page numbers separated by comma:</p><input type="text" id="extractPagesInput" placeholder="1, 2">
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
        const extractedBytes = await newDoc.save(); await loadPDF(extractedBytes); downloadBytes(extractedBytes, "Extracted_Pages.pdf");
    } catch(e) { showToast("Extraction failed."); }
    showLoading(false);
}

function uiShowDeleteModal() {
    if(!currentPdfBytes) return showToast("Open a PDF first.");
    showModal("Delete Page", `<p>Enter page number to delete:</p><input type="number" id="deletePage" min="1" max="${currentPdfjsDoc.numPages}">
        <div class="modal-actions"><button class="btn btn-secondary" onclick="closeModal()">Cancel</button><button class="btn btn-primary" style="background:#D13438;" onclick="actionDeletePage()">Delete</button></div>`);
}
async function actionDeletePage() {
    const pageNum = parseInt(document.getElementById('deletePage').value) - 1; closeModal(); showLoading(true, "Deleting...");
    try {
        const srcDoc = await PDFLib.PDFDocument.load(currentPdfBytes); srcDoc.removePage(pageNum);
        const newBytes = await srcDoc.save(); await loadPDF(newBytes); showToast("Page deleted.");
    } catch(e) { showToast("Failed to delete."); }
    showLoading(false);
}
)HTML";

    // --- PART 7: JS Study Tools, Watermark, Stamp ---
    wstring htmlPart7 = LR"HTML(
let currentStudyTool = null;
function setStudyTool(tool) {
    currentStudyTool = tool;
    document.querySelectorAll('.toolbar .tool-btn').forEach(btn => btn.classList.remove('active'));
    if(tool === 'highlight') document.getElementById('tool-highlight').classList.add('active');
    else if(tool === 'note') document.getElementById('tool-note').classList.add('active');
    else if(tool === 'link') document.getElementById('tool-link').classList.add('active');
    else document.getElementById('tool-pointer').classList.add('active');
    
    document.getElementById('pdf-container').style.cursor = tool ? 'crosshair' : 'default';
    showToast(tool ? `Study Tool Enabled: ${tool.toUpperCase()}` : 'Normal mode enabled.');
}

document.getElementById('pdf-container').addEventListener('click', async (e) => {
    if (!currentStudyTool || !currentPdfBytes) return;
    const pageWrapper = e.target.closest('.pdf-page-wrapper'); if (!pageWrapper) return; 
    const pageIndex = parseInt(pageWrapper.id.split('-')[1]) - 1;
    const rect = e.target.getBoundingClientRect(); const canvasX = e.clientX - rect.left; const canvasY = e.clientY - rect.top;

    showLoading(true, `Applying ${currentStudyTool}...`);
    try {
        const pdfDoc = await PDFLib.PDFDocument.load(currentPdfBytes);
        const page = pdfDoc.getPages()[pageIndex]; const { width, height } = page.getSize();
        const pdfX = (canvasX / rect.width) * width; const pdfY = height - ((canvasY / rect.height) * height); 

        if (currentStudyTool === 'highlight') {
            page.drawRectangle({ x: pdfX, y: pdfY - 5, width: 120, height: 15, color: PDFLib.rgb(1, 1, 0), opacity: 0.4, blendMode: PDFLib.BlendMode.Multiply });
        } else if (currentStudyTool === 'note') {
            const note = prompt("Enter your study note:");
            if (note) {
                page.drawRectangle({ x: pdfX, y: pdfY - 30, width: 200, height: 40, color: PDFLib.rgb(0.98, 0.96, 0.84), borderColor: PDFLib.rgb(0.8, 0.6, 0.2), borderWidth: 1 });
                page.drawText("📝 " + note, { x: pdfX + 5, y: pdfY - 15, size: 12, color: PDFLib.rgb(0,0,0) });
            }
        } else if (currentStudyTool === 'link') {
            const url = prompt("Enter link (e.g., https://...):");
            if (url) {
                page.drawText("🔗 " + url, { x: pdfX, y: pdfY, size: 10, color: PDFLib.rgb(0, 0, 1) });
                const linkAnnot = pdfDoc.context.obj({ Type: 'Annot', Subtype: 'Link', Rect: [pdfX, pdfY - 5, pdfX + 150, pdfY + 10], Border: [0, 0, 0], A: { Type: 'Action', S: 'URI', URI: PDFLib.PDFString.of(url) } });
                const linkAnnotRef = pdfDoc.context.register(linkAnnot);
                let annots = page.node.Annots();
                if (!annots) { annots = pdfDoc.context.obj([]); page.node.set(PDFLib.PDFName.of('Annots'), annots); }
                annots.push(linkAnnotRef);
            }
        }
        currentPdfBytes = await pdfDoc.save(); await loadPDF(currentPdfBytes);
    } catch (error) { showToast("Error applying tool."); }
    showLoading(false);
});

async function actionAddWatermark() {
    if (!currentPdfBytes) return showToast("Open a PDF first.");
    const watermarkText = prompt("Enter watermark text:", "CONFIDENTIAL"); if (!watermarkText) return;
    showLoading(true, "Adding Watermark...");
    try {
        const pdfDoc = await PDFLib.PDFDocument.load(currentPdfBytes); const { rgb, degrees } = PDFLib;
        pdfDoc.getPages().forEach((page) => { const { width, height } = page.getSize(); page.drawText(watermarkText, { x: width / 2 - 150, y: height / 2, size: 60, color: rgb(0.9, 0.2, 0.2), opacity: 0.3, rotate: degrees(45) }); });
        const modifiedBytes = await pdfDoc.save(); await loadPDF(modifiedBytes); downloadBytes(modifiedBytes, "Watermarked_Doc.pdf"); showToast("Watermark added!");
    } catch (e) { showToast("Failed to add watermark."); } showLoading(false);
}

async function actionAddStamp() {
    if (!currentPdfBytes) return showToast("Open a PDF first.");
    showLoading(true, "Applying Stamp...");
    try {
        const pdfDoc = await PDFLib.PDFDocument.load(currentPdfBytes); const firstPage = pdfDoc.getPages()[0]; 
        const { width, height } = firstPage.getSize(); const { rgb } = PDFLib;
        firstPage.drawRectangle({ x: width - 220, y: height - 100, width: 180, height: 50, borderColor: rgb(0.1, 0.6, 0.1), borderWidth: 3 });
        firstPage.drawText("APPROVED", { x: width - 200, y: height - 85, size: 30, color: rgb(0.1, 0.6, 0.1) });
        const modifiedBytes = await pdfDoc.save(); await loadPDF(modifiedBytes); showToast("Stamp applied!");
    } catch (e) { showToast("Failed to apply stamp."); } showLoading(false);
}
)HTML";

    // --- PART 8: JS Export, Paste (Ctrl+V), and Viewing Modes ---
    wstring htmlPart8 = LR"HTML(
async function actionPDFtoImage() {
    if(!currentPdfjsDoc) return showToast("Open a PDF first.");
    showLoading(true, "Converting to Images (ZIP)...");
    try {
        const zip = new JSZip();
        for (let i = 1; i <= currentPdfjsDoc.numPages; i++) {
            const page = await currentPdfjsDoc.getPage(i); const viewport = page.getViewport({ scale: 2.0 });
            const canvas = document.createElement('canvas'); const ctx = canvas.getContext('2d'); canvas.height = viewport.height; canvas.width = viewport.width;
            await page.render({ canvasContext: ctx, viewport: viewport }).promise;
            const blob = await new Promise(resolve => canvas.toBlob(resolve, 'image/png')); zip.file(`Page_${i}.png`, blob);
        }
        const zipBlob = await zip.generateAsync({type:"blob"}); saveAs(zipBlob, "PDF_Images.zip"); showToast("Images downloaded successfully.");
    } catch(e) { showToast("Failed to convert to image."); } showLoading(false);
}

async function actionPDFtoText() {
    if(!currentPdfjsDoc) return showToast("Open a PDF first.");
    showLoading(true, "Extracting Text...");
    try {
        let fullText = "";
        for (let i = 1; i <= currentPdfjsDoc.numPages; i++) {
            const page = await currentPdfjsDoc.getPage(i); const textContent = await page.getTextContent();
            fullText += `--- Page ${i} ---\n\n${textContent.items.map(item => item.str).join(' ')}\n\n`;
        }
        saveAs(new Blob([fullText], { type: "text/plain;charset=utf-8" }), "Extracted_Text.txt"); showToast("Text extracted.");
    } catch(e) { showToast("Failed to extract text."); } showLoading(false);
}

async function actionPerformOCR() {
    if (!currentPdfjsDoc) return showToast("Open a PDF first.");
    showLoading(true, "Scanning image to text (OCR)... This may take time.");
    try {
        const page = await currentPdfjsDoc.getPage(1); const viewport = page.getViewport({ scale: 2.0 });
        const canvas = document.createElement('canvas'); canvas.height = viewport.height; canvas.width = viewport.width;
        await page.render({ canvasContext: canvas.getContext('2d'), viewport: viewport }).promise;
        const result = await Tesseract.recognize(canvas.toDataURL('image/png'), 'eng');
        saveAs(new Blob([result.data.text], { type: "text/plain;charset=utf-8" }), "OCR_Result.txt"); showToast("OCR Completed!");
    } catch (e) { showToast("OCR Failed."); } showLoading(false);
}

document.addEventListener('paste', async (e) => {
    if (!currentPdfBytes) return;
    const items = e.clipboardData.items; let imageFile = null;
    for (let i = 0; i < items.length; i++) { if (items[i].type.indexOf('image') !== -1) { imageFile = items[i].getAsFile(); break; } }
    if (!imageFile) return;

    showLoading(true, "Pasting Image...");
    try {
        const imageBuffer = await imageFile.arrayBuffer(); const pdfDoc = await PDFLib.PDFDocument.load(currentPdfBytes);
        const page = pdfDoc.getPages()[0]; let embeddedImage;
        if (imageFile.type === 'image/png') embeddedImage = await pdfDoc.embedPng(imageBuffer);
        else if (imageFile.type === 'image/jpeg' || imageFile.type === 'image/jpg') embeddedImage = await pdfDoc.embedJpg(imageBuffer);
        else { showToast("Only PNG/JPG supported."); showLoading(false); return; }

        const { width, height } = page.getSize(); const imgDims = embeddedImage.scale(0.5); 
        page.drawImage(embeddedImage, { x: width / 2 - imgDims.width / 2, y: height / 2 - imgDims.height / 2, width: imgDims.width, height: imgDims.height });
        currentPdfBytes = await pdfDoc.save(); await loadPDF(currentPdfBytes); showToast("Image pasted!");
    } catch (error) { showToast("Failed to paste image."); } showLoading(false);
});

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
        showToast("Read Mode Active. Click exit icon to return.");
        if (currentZoom < 1.5) currentZoom = 1.5; 
    } else {
        iconEl.textContent = 'menu_book'; iconEl.style.color = '';
        showToast("Normal Mode Active.");
    }
    setTimeout(renderViewer, 100);
}
</script>
</body>
</html>
)HTML";

    // ✅ Return the complete concatenated string securely
    return htmlPart1 + htmlPart2 + htmlPart3 + htmlPart4 + htmlPart5 + htmlPart6 + htmlPart7 + htmlPart8;
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

    // যদি উইন্ডো আগে থেকেই তৈরি করা থাকে
    if (g_hAcrobatWnd != NULL) {
        ShowWindow(g_hAcrobatWnd, SW_RESTORE); // মিনিমাইজ থাকলে রিস্টোর করবে
        SetForegroundWindow(g_hAcrobatWnd);    // উইন্ডোটি সবার সামনে নিয়ে আসবে
        
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

    // প্রথমবার ওপেন করার সময়
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

    // 🟢 Fix: উইন্ডোটি ম্যাক্সিমাইজ অবস্থায় সাথে সাথেই সবার সামনে ওপেন হবে
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
