// tab_pdf_workspace.cpp
// Professional PDF Workspace - Complete Working Code

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

// Global Variables
HWND g_hPdfWnd = NULL;
HWND g_hWebHost = NULL;
ComPtr<ICoreWebView2Environment> g_env = nullptr;
ComPtr<ICoreWebView2Controller> g_ctrl = nullptr;
ComPtr<ICoreWebView2> g_web = nullptr;
wstring g_pdfPath = L"";
bool g_ready = false;

// Forward
HRESULT InitWebView(HWND hWnd, HWND hHost);
LRESULT CALLBACK PdfWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

// ==========================================
// COMPLETE HTML WITH ALL FEATURES
// ==========================================
wstring GetHTML() {
    return LR"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>PDF Pro</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.min.js"></script>
<script src="https://unpkg.com/pdf-lib@1.17.1/dist/pdf-lib.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.5/FileSaver.min.js"></script>
<link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200" rel="stylesheet" />
<style>
:root {
    --red: #EB1C24;
    --red-h: #BA1617;
    --dark: #2C2C2C;
    --panel: #F0F0F0;
    --doc-bg: #E8E8E8;
    --border: #D0D0D0;
    --text: #222;
    --muted: #777;
    --hover: #E5E5E5;
    --tb-h: 44px;
    --tool-h: 38px;
    --ls-w: 200px;
    --rs-w: 240px;
}
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
    font-family: 'Segoe UI', sans-serif;
    height: 100vh;
    overflow: hidden;
    display: flex;
    flex-direction: column;
    background: var(--doc-bg);
    color: var(--text);
    user-select: none;
}
.ms {
    font-variation-settings: 'FILL' 0, 'wght' 400, 'GRAD' 0, 'opsz' 24;
    font-size: 20px;
}

/* Toast */
.toast-box {
    position: fixed;
    bottom: 30px;
    left: 50%;
    transform: translateX(-50%);
    z-index: 9999;
    display: flex;
    flex-direction: column;
    gap: 8px;
}
.toast {
    padding: 10px 24px;
    border-radius: 4px;
    color: white;
    background: #323232;
    font-size: 13px;
    box-shadow: 0 4px 12px rgba(0,0,0,0.2);
    animation: fadeUp 0.3s;
}
@keyframes fadeUp {
    from { transform: translateY(20px); opacity: 0; }
    to { transform: translateY(0); opacity: 1; }
}

/* Modal */
.modal-bg {
    display: none;
    position: fixed;
    inset: 0;
    background: rgba(0,0,0,0.5);
    z-index: 1000;
    justify-content: center;
    align-items: center;
}
.modal-bg.show { display: flex; }
.modal-box {
    background: white;
    border-radius: 6px;
    padding: 20px;
    min-width: 340px;
    box-shadow: 0 8px 24px rgba(0,0,0,0.2);
}
.modal-box h3 { font-size: 17px; margin-bottom: 14px; font-weight: 600; }
.modal-box input {
    width: 100%;
    padding: 8px;
    margin-bottom: 14px;
    border: 1px solid var(--border);
    border-radius: 4px;
}
.modal-btns { display: flex; gap: 8px; justify-content: flex-end; }
.btn {
    padding: 7px 14px;
    border: none;
    border-radius: 4px;
    cursor: pointer;
    font-size: 13px;
    transition: 0.2s;
}
.btn-red { background: var(--red); color: white; }
.btn-red:hover { background: var(--red-h); }
.btn-outline {
    background: transparent;
    border: 1px solid var(--border);
    color: var(--text);
}
.btn-outline:hover { background: var(--hover); }

/* Top Bar */
.topbar {
    height: var(--tb-h);
    background: var(--dark);
    display: flex;
    align-items: center;
    padding: 0 12px;
    color: white;
    gap: 8px;
}
.topbar-menu { display: flex; gap: 16px; font-size: 12px; }
.topbar-item { cursor: pointer; opacity: 0.8; transition: 0.2s; }
.topbar-item:hover { opacity: 1; }
.topbar-title {
    flex: 1;
    text-align: center;
    font-size: 13px;
    font-weight: 500;
    opacity: 0.9;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}
.topbar-actions { display: flex; gap: 8px; }
.tb-icon { cursor: pointer; opacity: 0.8; }
.tb-icon:hover { opacity: 1; }

/* Tab Bar */
.tab-bar {
    height: 34px;
    background: #444;
    display: flex;
    align-items: center;
    padding: 0 4px;
    gap: 2px;
    overflow-x: auto;
    overflow-y: hidden;
}
.tab {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 4px 10px;
    background: #555;
    color: #ccc;
    font-size: 11px;
    border-radius: 4px 4px 0 0;
    cursor: pointer;
    white-space: nowrap;
    min-width: 80px;
    max-width: 180px;
    transition: 0.2s;
}
.tab.active { background: #666; color: white; }
.tab:hover { background: #6a6a6a; }
.tab-close {
    margin-left: auto;
    font-size: 14px;
    opacity: 0.6;
    cursor: pointer;
}
.tab-close:hover { opacity: 1; color: #f44; }
.tab-add {
    display: flex;
    align-items: center;
    justify-content: center;
    width: 28px;
    height: 28px;
    border-radius: 4px;
    cursor: pointer;
    color: #aaa;
    font-size: 18px;
    flex-shrink: 0;
}
.tab-add:hover { background: #555; color: white; }

/* Toolbar */
.toolbar {
    height: var(--tool-h);
    background: white;
    border-bottom: 1px solid var(--border);
    display: flex;
    align-items: center;
    padding: 0 10px;
    gap: 12px;
    font-size: 13px;
}
.tool-btn {
    display: flex;
    align-items: center;
    gap: 4px;
    cursor: pointer;
    padding: 4px 8px;
    border-radius: 4px;
    color: var(--text);
    transition: 0.2s;
}
.tool-btn:hover { background: var(--hover); }
.tool-btn.active { background: #FBECEE; color: var(--red); }
.divider { width: 1px; height: 18px; background: var(--border); }

/* Workspace */
.workspace { flex: 1; display: flex; overflow: hidden; }

/* Left Sidebar */
.left-sidebar {
    width: var(--ls-w);
    background: var(--panel);
    border-right: 1px solid var(--border);
    display: flex;
    flex-direction: column;
    transition: width 0.3s;
}
.left-sidebar.hide { width: 0 !important; border: none !important; overflow: hidden; }
.sidebar-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 10px 12px;
    font-size: 11px;
    font-weight: 600;
    text-transform: uppercase;
    color: var(--muted);
    border-bottom: 1px solid var(--border);
}
.sidebar-toggle { cursor: pointer; opacity: 0.6; font-size: 16px; }
.sidebar-toggle:hover { opacity: 1; }
.thumb-list {
    flex: 1;
    overflow-y: auto;
    padding: 8px;
    display: flex;
    flex-direction: column;
    gap: 8px;
}
.thumb-item {
    border: 1px solid var(--border);
    background: white;
    padding: 3px;
    cursor: pointer;
    transition: 0.2s;
}
.thumb-item:hover { border-color: var(--red); }
.thumb-item.active { border: 2px solid var(--red); }
.thumb-item canvas { width: 100%; height: auto; display: block; }
.thumb-label {
    text-align: center;
    font-size: 10px;
    margin-top: 2px;
    color: var(--muted);
}

/* Right Sidebar */
.right-sidebar {
    width: var(--rs-w);
    background: var(--panel);
    border-left: 1px solid var(--border);
    display: flex;
    flex-direction: column;
    overflow-y: auto;
    transition: width 0.3s;
}
.right-sidebar.hide { width: 0 !important; border: none !important; overflow: hidden; }
.tool-item {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 10px 12px;
    cursor: pointer;
    border-bottom: 1px solid var(--border);
    background: white;
    transition: 0.2s;
}
.tool-item:hover { background: var(--hover); }
.tool-item .ms { color: var(--red); font-size: 22px; }
.tool-text { font-size: 13px; font-weight: 500; }

/* Viewer Area */
.viewer {
    flex: 1;
    overflow-y: auto;
    display: flex;
    justify-content: center;
    padding: 20px;
    background: var(--doc-bg);
}
.page-list {
    display: flex;
    flex-direction: column;
    gap: 16px;
    align-items: center;
    width: 100%;
    cursor: default;
}
.page-wrap {
    background: white;
    box-shadow: 0 2px 8px rgba(0,0,0,0.12);
    margin-bottom: 16px;
    position: relative;
}
.page-wrap canvas { display: block; max-width: 100%; height: auto; }

/* Loading */
.loading {
    display: none;
    position: fixed;
    inset: 0;
    background: rgba(0,0,0,0.6);
    z-index: 2000;
    flex-direction: column;
    justify-content: center;
    align-items: center;
    color: white;
    font-size: 15px;
}
.loading.show { display: flex; }
.spinner {
    border: 4px solid rgba(255,255,255,0.3);
    border-top: 4px solid white;
    border-radius: 50%;
    width: 36px;
    height: 36px;
    animation: spin 1s linear infinite;
    margin-bottom: 12px;
}
@keyframes spin { 100% { transform: rotate(360deg); } }

/* Night Mode */
body.night .viewer { background: #1a1a1a !important; }
body.night .toolbar { background: #252525 !important; border-color: #333 !important; color: #e0e0e0 !important; }
body.night .tool-btn { color: #e0e0e0 !important; }
body.night .tool-btn:hover { background: #333 !important; }
body.night .divider { background: #333 !important; }
body.night .left-sidebar, body.night .right-sidebar { background: #252525 !important; border-color: #333 !important; }
body.night .sidebar-head { color: #aaa !important; border-color: #333 !important; }
body.night .thumb-item { background: #1a1a1a !important; border-color: #333 !important; }
body.night .tool-item { background: #252525 !important; border-color: #333 !important; color: #e0e0e0 !important; }
body.night .tool-item:hover { background: #333 !important; }
body.night .page-wrap { box-shadow: 0 4px 15px rgba(0,0,0,0.5); }

/* Read Mode */
body.read .toolbar, body.read .left-sidebar, body.read .right-sidebar, body.read .tab-bar { display: none !important; }
</style>
</head>
<body>

<div class="toast-box" id="toastBox"></div>

<div class="loading" id="loading">
    <div class="spinner"></div>
    <div id="loadText">Processing...</div>
</div>

<div class="modal-bg" id="modalBg">
    <div class="modal-box" id="modalBox"></div>
</div>

<!-- Top Bar -->
<div class="topbar">
    <div class="topbar-menu">
        <div class="topbar-item" onclick="openFile()">File</div>
        <div class="topbar-item" onclick="saveFile()">Save</div>
        <div class="topbar-item" onclick="saveAsFile()">Save As</div>
    </div>
    <div class="topbar-title" id="docTitle">PDF Pro</div>
    <div class="topbar-actions">
        <span class="ms tb-icon" onclick="toggleNight()" title="Night Mode">dark_mode</span>
        <span class="ms tb-icon" onclick="toggleRead()" title="Read Mode" id="readIcon">menu_book</span>
        <span class="ms tb-icon" onclick="toggleLeft()" id="leftIcon" title="Toggle Left Sidebar">dock_to_left</span>
        <span class="ms tb-icon" onclick="toggleRight()" id="rightIcon" title="Toggle Right Sidebar">dock_to_right</span>
    </div>
</div>

<!-- Tab Bar -->
<div class="tab-bar" id="tabBar"></div>

<!-- Toolbar -->
<div class="toolbar">
    <div class="tool-btn active" id="toolPtr" onclick="setTool(null)">
        <span class="ms">pan_tool</span>
    </div>
    <div class="divider"></div>
    <span style="font-weight:600;font-size:11px;color:var(--muted);">TOOLS:</span>
    <div class="tool-btn" id="toolHl" onclick="setTool('highlight')">
        <span class="ms" style="color:#FFC107;">format_ink_highlighter</span>
    </div>
    <div class="tool-btn" id="toolNote" onclick="setTool('note')">
        <span class="ms" style="color:#4CAF50;">speaker_notes</span>
    </div>
    <div class="tool-btn" id="toolLink" onclick="setTool('link')">
        <span class="ms" style="color:#2196F3;">link</span>
    </div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="zoomOut()"><span class="ms">remove</span></div>
    <span id="zoomVal" style="font-size:12px;width:36px;text-align:center;">100%</span>
    <div class="tool-btn" onclick="zoomIn()"><span class="ms">add</span></div>
    <div class="divider"></div>
    <div class="tool-btn" onclick="rotate()"><span class="ms">rotate_right</span></div>
</div>

<!-- Workspace -->
<div class="workspace">
    <!-- Left Sidebar -->
    <div class="left-sidebar" id="leftSidebar">
        <div class="sidebar-head">
            <span>Pages</span>
            <span class="ms sidebar-toggle" onclick="toggleLeft()">chevron_left</span>
        </div>
        <div class="thumb-list" id="thumbList"></div>
    </div>

    <!-- Viewer -->
    <div class="viewer" id="viewerArea">
        <div class="page-list" id="pageList">
            <div style="margin-top:80px;text-align:center;color:var(--muted);">
                <span class="ms" style="font-size:60px;color:#ccc;">description</span>
                <p style="margin-top:12px;font-size:15px;">Open a PDF file to begin</p>
                <button class="btn btn-red" style="margin-top:12px;" onclick="openFile()">Open File</button>
            </div>
        </div>
    </div>

    <!-- Right Sidebar -->
    <div class="right-sidebar" id="rightSidebar">
        <div class="sidebar-head">
            <span>Tools</span>
            <span class="ms sidebar-toggle" onclick="toggleRight()">chevron_right</span>
        </div>
        <div class="tool-item" onclick="mergePdfs()"><span class="ms">library_add</span><span class="tool-text">Merge PDFs</span></div>
        <div class="tool-item" onclick="showSplitDialog()"><span class="ms">splitscreen</span><span class="tool-text">Split PDF</span></div>
        <div class="tool-item" onclick="showExtractDialog()"><span class="ms">file_upload</span><span class="tool-text">Extract Pages</span></div>
        <div class="tool-item" onclick="showDeleteDialog()"><span class="ms">delete</span><span class="tool-text">Delete Page</span></div>
        <div class="tool-item" onclick="exportImages()"><span class="ms">image</span><span class="tool-text">Export Images</span></div>
        <div class="tool-item" onclick="exportText()"><span class="ms">article</span><span class="tool-text">Export Text</span></div>
        <div class="tool-item" onclick="addWatermark()"><span class="ms">branding_watermark</span><span class="tool-text">Add Watermark</span></div>
        <div class="tool-item" onclick="addStamp()"><span class="ms">verified</span><span class="tool-text">Add Stamp</span></div>
    </div>
</div>

<input type="file" id="fileInput" accept=".pdf" style="display:none;" onchange="handleFileSelect(event)">
<input type="file" id="mergeInput" accept=".pdf" multiple style="display:none;">

<script>
// ============ GLOBAL STATE ============
let tabs = [];
let activeTab = -1;
let zoom = 1.0;
let rotation = 0;
let currentTool = null;
let showLeft = true;
let showRight = true;

// ============ UTILITY FUNCTIONS ============
function toast(msg) {
    let box = document.getElementById('toastBox');
    let t = document.createElement('div');
    t.className = 'toast';
    t.textContent = msg;
    box.appendChild(t);
    setTimeout(function() { t.remove(); }, 2500);
}

function showLoading(show, text) {
    if (!text) text = 'Working...';
    document.getElementById('loading').classList.toggle('show', show);
    document.getElementById('loadText').textContent = text;
}

function showModal(title, html) {
    document.getElementById('modalBox').innerHTML = '<h3>' + title + '</h3>' + html;
    document.getElementById('modalBg').classList.add('show');
}

function hideModal() {
    document.getElementById('modalBg').classList.remove('show');
}

function saveBlob(bytes, name) {
    let blob = new Blob([bytes], { type: 'application/pdf' });
    saveAs(blob, name);
    toast('Saved: ' + name);
}

// ============ TAB MANAGEMENT ============
function renderTabs() {
    let bar = document.getElementById('tabBar');
    bar.innerHTML = '';
    for (let i = 0; i < tabs.length; i++) {
        let t = tabs[i];
        let div = document.createElement('div');
        div.className = 'tab' + (i === activeTab ? ' active' : '');
        div.innerHTML = '<span>' + t.name + '</span><span class="tab-close" data-idx="' + i + '">&times;</span>';
        div.onclick = function() { switchTab(i); };
        bar.appendChild(div);
    }
    // Close buttons
    let closes = bar.querySelectorAll('.tab-close');
    closes.forEach(function(btn) {
        btn.onclick = function(e) {
            e.stopPropagation();
            closeTab(parseInt(this.getAttribute('data-idx')));
        };
    });
    // Add button
    let addBtn = document.createElement('div');
    addBtn.className = 'tab-add';
    addBtn.textContent = '+';
    addBtn.onclick = openFile;
    bar.appendChild(addBtn);
}

function switchTab(idx) {
    if (idx < 0 || idx >= tabs.length) return;
    activeTab = idx;
    zoom = 1.0;
    rotation = 0;
    document.getElementById('docTitle').textContent = tabs[activeTab].name;
    renderTabs();
    renderPages();
    renderThumbnails();
}

function closeTab(idx) {
    if (tabs.length === 0) return;
    tabs.splice(idx, 1);
    if (activeTab >= tabs.length) activeTab = tabs.length - 1;
    if (tabs.length === 0) {
        activeTab = -1;
        document.getElementById('docTitle').textContent = 'PDF Pro';
        document.getElementById('pageList').innerHTML = '<div style="margin-top:80px;text-align:center;color:var(--muted);"><span class="ms" style="font-size:60px;color:#ccc;">description</span><p style="margin-top:12px;font-size:15px;">Open a PDF file to begin</p><button class="btn btn-red" style="margin-top:12px;" onclick="openFile()">Open File</button></div>';
        document.getElementById('thumbList').innerHTML = '';
        document.getElementById('zoomVal').textContent = '100%';
    } else {
        switchTab(activeTab);
    }
    renderTabs();
}

// ============ FILE OPERATIONS ============
function openFile() {
    document.getElementById('fileInput').click();
}

async function handleFileSelect(event) {
    let file = event.target.files[0];
    if (!file) return;
    let reader = new FileReader();
    reader.onload = async function() {
        let bytes = new Uint8Array(reader.result);
        tabs.push({ name: file.name, bytes: bytes, doc: null });
        activeTab = tabs.length - 1;
        await loadDocument(bytes);
        renderTabs();
    };
    reader.readAsArrayBuffer(file);
    event.target.value = '';
}

// Called from C++
async function loadFromPath(path) {
    try {
        let response = await fetch('file:///' + path.replace(/\\/g, '/'));
        let buffer = await response.arrayBuffer();
        let name = path.split('\\').pop();
        let bytes = new Uint8Array(buffer);
        tabs.push({ name: name, bytes: bytes, doc: null });
        activeTab = tabs.length - 1;
        await loadDocument(bytes);
        renderTabs();
    } catch (e) {
        toast('Failed to load PDF file');
    }
}

async function loadDocument(bytes) {
    try {
        showLoading(true, 'Loading PDF...');
        let t = tabs[activeTab];
        t.bytes = bytes;
        t.doc = await pdfjsLib.getDocument({ data: bytes }).promise;
        zoom = 1.0;
        rotation = 0;
        document.getElementById('docTitle').textContent = t.name;
        await renderPages();
        await renderThumbnails();
        showLoading(false);
        toast('PDF loaded: ' + t.name);
    } catch (e) {
        showLoading(false);
        toast('Error loading PDF');
    }
}

function saveFile() {
    if (activeTab < 0 || !tabs[activeTab]) return;
    saveBlob(tabs[activeTab].bytes, tabs[activeTab].name);
}

function saveAsFile() {
    if (activeTab < 0 || !tabs[activeTab]) return;
    let name = prompt('Save as:', tabs[activeTab].name);
    if (name) saveBlob(tabs[activeTab].bytes, name);
}

// ============ RENDER FUNCTIONS ============
async function renderPages() {
    let t = tabs[activeTab];
    let container = document.getElementById('pageList');
    container.innerHTML = '';
    if (!t || !t.doc) {
        container.innerHTML = '<div style="margin-top:80px;text-align:center;color:var(--muted);"><span class="ms" style="font-size:60px;color:#ccc;">description</span><p style="margin-top:12px;font-size:15px;">No PDF open</p><button class="btn btn-red" style="margin-top:12px;" onclick="openFile()">Open File</button></div>';
        return;
    }
    document.getElementById('zoomVal').textContent = Math.round(zoom * 100) + '%';
    for (let i = 1; i <= t.doc.numPages; i++) {
        let page = await t.doc.getPage(i);
        let viewport = page.getViewport({ scale: zoom, rotation: rotation });
        let wrap = document.createElement('div');
        wrap.className = 'page-wrap';
        wrap.id = 'page-' + i;
        let canvas = document.createElement('canvas');
        canvas.height = viewport.height;
        canvas.width = viewport.width;
        wrap.appendChild(canvas);
        container.appendChild(wrap);
        await page.render({ canvasContext: canvas.getContext('2d'), viewport: viewport }).promise;
    }
}

async function renderThumbnails() {
    let t = tabs[activeTab];
    let list = document.getElementById('thumbList');
    list.innerHTML = '';
    if (!t || !t.doc) return;
    for (let i = 1; i <= t.doc.numPages; i++) {
        let page = await t.doc.getPage(i);
        let viewport = page.getViewport({ scale: 0.15, rotation: rotation });
        let item = document.createElement('div');
        item.className = 'thumb-item';
        item.onclick = function() {
            let el = document.getElementById('page-' + i);
            if (el) el.scrollIntoView({ behavior: 'smooth' });
        };
        let canvas = document.createElement('canvas');
        canvas.height = viewport.height;
        canvas.width = viewport.width;
        let label = document.createElement('div');
        label.className = 'thumb-label';
        label.textContent = i;
        item.appendChild(canvas);
        item.appendChild(label);
        list.appendChild(item);
        await page.render({ canvasContext: canvas.getContext('2d'), viewport: viewport }).promise;
    }
}

// ============ ZOOM & ROTATE ============
function zoomIn() {
    if (zoom < 3.0) { zoom += 0.2; renderPages(); }
}

function zoomOut() {
    if (zoom > 0.4) { zoom -= 0.2; renderPages(); }
}

function rotate() {
    rotation = (rotation + 90) % 360;
    renderPages();
    renderThumbnails();
}

// Mouse wheel zoom
document.addEventListener('wheel', function(e) {
    if (e.ctrlKey) {
        e.preventDefault();
        if (e.deltaY < 0) zoomIn();
        else zoomOut();
    }
}, { passive: false });

// ============ SIDEBAR TOGGLES ============
function toggleLeft() {
    showLeft = !showLeft;
    document.getElementById('leftSidebar').classList.toggle('hide', !showLeft);
    document.getElementById('leftIcon').style.color = showLeft ? '' : 'var(--red)';
    toast(showLeft ? 'Left sidebar shown' : 'Left sidebar hidden');
}

function toggleRight() {
    showRight = !showRight;
    document.getElementById('rightSidebar').classList.toggle('hide', !showRight);
    document.getElementById('rightIcon').style.color = showRight ? '' : 'var(--red)';
    toast(showRight ? 'Right sidebar shown' : 'Right sidebar hidden');
}

// ============ VIEW MODES ============
function toggleNight() {
    document.body.classList.toggle('night');
    let isNight = document.body.classList.contains('night');
    toast(isNight ? 'Night mode ON' : 'Night mode OFF');
}

function toggleRead() {
    document.body.classList.toggle('read');
    let isRead = document.body.classList.contains('read');
    let icon = document.getElementById('readIcon');
    if (isRead) {
        icon.textContent = 'fullscreen_exit';
        icon.style.color = 'var(--red)';
        if (zoom < 1.5) zoom = 1.5;
        toast('Read mode ON - Click to exit');
    } else {
        icon.textContent = 'menu_book';
        icon.style.color = '';
        zoom = 1.0;
        toast('Read mode OFF');
    }
    renderPages();
}

// ============ STUDY TOOLS ============
function setTool(tool) {
    currentTool = tool;
    // Reset all tool buttons
    document.getElementById('toolPtr').classList.remove('active');
    document.getElementById('toolHl').classList.remove('active');
    document.getElementById('toolNote').classList.remove('active');
    document.getElementById('toolLink').classList.remove('active');
    
    if (tool === 'highlight') document.getElementById('toolHl').classList.add('active');
    else if (tool === 'note') document.getElementById('toolNote').classList.add('active');
    else if (tool === 'link') document.getElementById('toolLink').classList.add('active');
    else document.getElementById('toolPtr').classList.add('active');
    
    document.getElementById('pageList').style.cursor = tool ? 'crosshair' : 'default';
    toast(tool ? 'Tool: ' + tool.toUpperCase() : 'Normal mode');
}

// Click handler for study tools
document.getElementById('pageList').addEventListener('click', async function(e) {
    if (!currentTool || activeTab < 0 || !tabs[activeTab]) return;
    
    let pageWrap = e.target.closest('.page-wrap');
    if (!pageWrap) return;
    
    let pageIdx = parseInt(pageWrap.id.split('-')[1]) - 1;
    let rect = e.target.getBoundingClientRect();
    let canvasX = e.clientX - rect.left;
    let canvasY = e.clientY - rect.top;
    
    showLoading(true, 'Applying ' + currentTool + '...');
    
    try {
        let pdfDoc = await PDFLib.PDFDocument.load(tabs[activeTab].bytes);
        let page = pdfDoc.getPages()[pageIdx];
        let { width, height } = page.getSize();
        let pdfX = (canvasX / rect.width) * width;
        let pdfY = height - ((canvasY / rect.height) * height);
        
        if (currentTool === 'highlight') {
            page.drawRectangle({
                x: pdfX, y: pdfY - 5, width: 120, height: 15,
                color: PDFLib.rgb(1, 1, 0), opacity: 0.4,
                blendMode: PDFLib.BlendMode.Multiply
            });
        } else if (currentTool === 'note') {
            let note = prompt('Enter note:');
            if (note) {
                page.drawRectangle({
                    x: pdfX, y: pdfY - 30, width: 200, height: 40,
                    color: PDFLib.rgb(0.98, 0.96, 0.84),
                    borderColor: PDFLib.rgb(0.8, 0.6, 0.2), borderWidth: 1
                });
                page.drawText('📝 ' + note, {
                    x: pdfX + 5, y: pdfY - 15, size: 12, color: PDFLib.rgb(0, 0, 0)
                });
            }
        } else if (currentTool === 'link') {
            let url = prompt('Enter URL:');
            if (url) {
                page.drawText('🔗 ' + url, {
                    x: pdfX, y: pdfY, size: 10, color: PDFLib.rgb(0, 0, 1)
                });
            }
        }
        
        tabs[activeTab].bytes = await pdfDoc.save();
        await loadDocument(tabs[activeTab].bytes);
    } catch (e) {
        toast('Error applying tool');
    }
    
    showLoading(false);
});

// ============ PDF OPERATIONS ============
function mergePdfs() {
    document.getElementById('mergeInput').click();
    document.getElementById('mergeInput').onchange = async function(e) {
        let files = e.target.files;
        if (files.length < 2) { alert('Select at least 2 PDFs'); return; }
        showLoading(true, 'Merging PDFs...');
        try {
            let merged = await PDFLib.PDFDocument.create();
            for (let file of files) {
                let pdf = await PDFLib.PDFDocument.load(new Uint8Array(await file.arrayBuffer()));
                let copied = await merged.copyPages(pdf, pdf.getPageIndices());
                copied.forEach(function(p) { merged.addPage(p); });
            }
            let bytes = await merged.save();
            tabs.push({ name: 'Merged.pdf', bytes: bytes, doc: null });
            activeTab = tabs.length - 1;
            await loadDocument(bytes);
            renderTabs();
            saveBlob(bytes, 'Merged.pdf');
        } catch (e) { toast('Merge failed'); }
        showLoading(false);
        e.target.value = '';
    };
}

function showSplitDialog() {
    if (activeTab < 0) { toast('Open a PDF first'); return; }
    showModal('Split PDF',
        '<p>Split after page:</p>' +
        '<input type="number" id="splitNum" min="1" max="' + (tabs[activeTab].doc.numPages - 1) + '" value="1">' +
        '<div class="modal-btns">' +
        '<button class="btn btn-outline" onclick="hideModal()">Cancel</button>' +
        '<button class="btn btn-red" onclick="doSplit()">Split</button>' +
        '</div>'
    );
}

async function doSplit() {
    let num = parseInt(document.getElementById('splitNum').value);
    hideModal();
    showLoading(true, 'Splitting PDF...');
    try {
        let src = await PDFLib.PDFDocument.load(tabs[activeTab].bytes);
        let d1 = await PDFLib.PDFDocument.create();
        let d2 = await PDFLib.PDFDocument.create();
        let indices = src.getPageIndices();
        let c1 = await d1.copyPages(src, indices.slice(0, num));
        c1.forEach(function(p) { d1.addPage(p); });
        let c2 = await d2.copyPages(src, indices.slice(num));
        c2.forEach(function(p) { d2.addPage(p); });
        saveBlob(await d1.save(), 'Part1.pdf');
        saveBlob(await d2.save(), 'Part2.pdf');
    } catch (e) { toast('Split failed'); }
    showLoading(false);
}

function showExtractDialog() {
    if (activeTab < 0) { toast('Open a PDF first'); return; }
    showModal('Extract Pages',
        '<p>Page numbers (comma separated):</p>' +
        '<input type="text" id="extractNums" placeholder="1, 2, 5">' +
        '<div class="modal-btns">' +
        '<button class="btn btn-outline" onclick="hideModal()">Cancel</button>' +
        '<button class="btn btn-red" onclick="doExtract()">Extract</button>' +
        '</div>'
    );
}

async function doExtract() {
    let input = document.getElementById('extractNums').value;
    let pages = input.split(',').map(function(n) { return parseInt(n.trim()) - 1; }).filter(function(n) { return !isNaN(n) && n >= 0; });
    if (pages.length === 0) { alert('Invalid page numbers'); return; }
    hideModal();
    showLoading(true, 'Extracting pages...');
    try {
        let src = await PDFLib.PDFDocument.load(tabs[activeTab].bytes);
        let doc = await PDFLib.PDFDocument.create();
        let copied = await doc.copyPages(src, pages);
        copied.forEach(function(p) { doc.addPage(p); });
        let bytes = await doc.save();
        tabs.push({ name: 'Extracted.pdf', bytes: bytes, doc: null });
        activeTab = tabs.length - 1;
        await loadDocument(bytes);
        renderTabs();
    } catch (e) { toast('Extract failed'); }
    showLoading(false);
}

function showDeleteDialog() {
    if (activeTab < 0) { toast('Open a PDF first'); return; }
    showModal('Delete Page',
        '<p>Page number to delete:</p>' +
        '<input type="number" id="deleteNum" min="1" max="' + tabs[activeTab].doc.numPages + '" value="1">' +
        '<div class="modal-btns">' +
        '<button class="btn btn-outline" onclick="hideModal()">Cancel</button>' +
        '<button class="btn btn-red" style="background:#D13438;" onclick="doDelete()">Delete</button>' +
        '</div>'
    );
}

async function doDelete() {
    let num = parseInt(document.getElementById('deleteNum').value) - 1;
    hideModal();
    showLoading(true, 'Deleting page...');
    try {
        let src = await PDFLib.PDFDocument.load(tabs[activeTab].bytes);
        src.removePage(num);
        tabs[activeTab].bytes = await src.save();
        await loadDocument(tabs[activeTab].bytes);
        toast('Page deleted');
    } catch (e) { toast('Delete failed'); }
    showLoading(false);
}

async function exportImages() {
    if (activeTab < 0) { toast('Open a PDF first'); return; }
    showLoading(true, 'Converting to images...');
    try {
        let zip = new JSZip();
        let doc = tabs[activeTab].doc;
        for (let i = 1; i <= doc.numPages; i++) {
            let page = await doc.getPage(i);
            let viewport = page.getViewport({ scale: 2.0 });
            let canvas = document.createElement('canvas');
            canvas.height = viewport.height;
            canvas.width = viewport.width;
            await page.render({ canvasContext: canvas.getContext('2d'), viewport: viewport }).promise;
            let blob = await new Promise(function(resolve) { canvas.toBlob(resolve, 'image/png'); });
            zip.file('Page_' + i + '.png', blob);
        }
        let zipBlob = await zip.generateAsync({ type: 'blob' });
        saveAs(zipBlob, 'PDF_Images.zip');
        toast('Images exported');
    } catch (e) { toast('Export failed'); }
    showLoading(false);
}

async function exportText() {
    if (activeTab < 0) { toast('Open a PDF first'); return; }
    showLoading(true, 'Extracting text...');
    try {
        let text = '';
        let doc = tabs[activeTab].doc;
        for (let i = 1; i <= doc.numPages; i++) {
            let page = await doc.getPage(i);
            let content = await page.getTextContent();
            text += '--- Page ' + i + ' ---\n\n';
            text += content.items.map(function(item) { return item.str; }).join(' ') + '\n\n';
        }
        saveAs(new Blob([text], { type: 'text/plain' }), 'PDF_Text.txt');
        toast('Text extracted');
    } catch (e) { toast('Text extraction failed'); }
    showLoading(false);
}

async function addWatermark() {
    if (activeTab < 0) { toast('Open a PDF first'); return; }
    let wm = prompt('Watermark text:', 'CONFIDENTIAL');
    if (!wm) return;
    showLoading(true, 'Adding watermark...');
    try {
        let doc = await PDFLib.PDFDocument.load(tabs[activeTab].bytes);
        let { rgb, degrees } = PDFLib;
        doc.getPages().forEach(function(page) {
            let { width, height } = page.getSize();
            page.drawText(wm, {
                x: width / 2 - 150, y: height / 2,
                size: 60, color: rgb(0.9, 0.2, 0.2),
                opacity: 0.3, rotate: degrees(45)
            });
        });
        tabs[activeTab].bytes = await doc.save();
        await loadDocument(tabs[activeTab].bytes);
        toast('Watermark added');
    } catch (e) { toast('Watermark failed'); }
    showLoading(false);
}

async function addStamp() {
    if (activeTab < 0) { toast('Open a PDF first'); return; }
    showLoading(true, 'Adding stamp...');
    try {
        let doc = await PDFLib.PDFDocument.load(tabs[activeTab].bytes);
        let page = doc.getPages()[0];
        let { width, height } = page.getSize();
        let { rgb } = PDFLib;
        page.drawRectangle({
            x: width - 220, y: height - 100,
            width: 180, height: 50,
            borderColor: rgb(0.1, 0.6, 0.1), borderWidth: 3
        });
        page.drawText('APPROVED', {
            x: width - 200, y: height - 85,
            size: 30, color: rgb(0.1, 0.6, 0.1)
        });
        tabs[activeTab].bytes = await doc.save();
        await loadDocument(tabs[activeTab].bytes);
        toast('Stamp applied');
    } catch (e) { toast('Stamp failed'); }
    showLoading(false);
}
</script>
</body>
</html>
)HTML";
}

// ==========================================
// WINDOW PROCEDURE
// ==========================================
LRESULT CALLBACK PdfWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        RECT r;
        GetClientRect(hWnd, &r);
        g_hWebHost = CreateWindowExW(0, L"STATIC", NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, r.right, r.bottom,
            hWnd, (HMENU)1001, GetModuleHandle(NULL), NULL);
        InitWebView(hWnd, g_hWebHost);
        break;
    }
    case WM_SIZE: {
        if (g_hWebHost && g_ctrl) {
            RECT r;
            GetClientRect(hWnd, &r);
            SetWindowPos(g_hWebHost, NULL, 0, 0, r.right, r.bottom, SWP_NOZORDER);
            g_ctrl->put_Bounds(RECT{0, 0, r.right, r.bottom});
        }
        break;
    }
    case WM_CLOSE: {
        ShowWindow(hWnd, SW_HIDE);
        return 0;
    }
    case WM_DESTROY: {
        if (g_ctrl) {
            g_ctrl->Close();
            g_ctrl = nullptr;
        }
        g_web = nullptr;
        g_env = nullptr;
        g_ready = false;
        if (g_hWebHost) {
            DestroyWindow(g_hWebHost);
            g_hWebHost = NULL;
        }
        g_hPdfWnd = NULL;
        break;
    }
    default:
        return DefWindowProcW(hWnd, msg, wp, lp);
    }
    return 0;
}

// ==========================================
// WEBVIEW2 INIT
// ==========================================
HRESULT InitWebView(HWND hWnd, HWND hHost) {
    auto envCB = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [hWnd, hHost](HRESULT res, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(res)) return res;
            g_env = env;
            
            auto ctrlCB = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [hWnd](HRESULT res, ICoreWebView2Controller* ctrl) -> HRESULT {
                    if (FAILED(res)) return res;
                    
                    g_ctrl = ctrl;
                    g_ctrl->get_CoreWebView2(&g_web);
                    
                    ICoreWebView2Settings* settings;
                    g_web->get_Settings(&settings);
                    settings->put_IsScriptEnabled(TRUE);
                    settings->put_IsWebMessageEnabled(TRUE);
                    
                    RECT r;
                    GetClientRect(hWnd, &r);
                    g_ctrl->put_Bounds(RECT{0, 0, r.right, r.bottom});
                    
                    g_web->NavigateToString(GetHTML().c_str());
                    
                    auto navCB = Callback<ICoreWebView2NavigationCompletedEventHandler>(
                        [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                            BOOL ok;
                            args->get_IsSuccess(&ok);
                            if (ok) {
                                g_ready = true;
                                if (!g_pdfPath.empty()) {
                                    wstring ep = g_pdfPath;
                                    size_t p = 0;
                                    while ((p = ep.find(L"\\", p)) != wstring::npos) {
                                        ep.replace(p, 1, L"\\\\");
                                        p += 2;
                                    }
                                    wstring script = L"loadFromPath('" + ep + L"');";
                                    sender->ExecuteScript(script.c_str(), nullptr);
                                }
                            }
                            return S_OK;
                        }
                    );
                    g_web->add_NavigationCompleted(navCB.Get(), nullptr);
                    return S_OK;
                }
            );
            env->CreateCoreWebView2Controller(hHost, ctrlCB.Get());
            return S_OK;
        }
    );
    
    return CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr, envCB.Get());
}

// ==========================================
// LAUNCH FUNCTION
// ==========================================
void LaunchFoxitStylePdfReader(wstring pdfPath) {
    g_pdfPath = pdfPath;
    
    // Already open?
    if (g_hPdfWnd) {
        ShowWindow(g_hPdfWnd, SW_RESTORE);
        SetForegroundWindow(g_hPdfWnd);
        if (g_ready && g_web && !pdfPath.empty()) {
            wstring ep = pdfPath;
            size_t p = 0;
            while ((p = ep.find(L"\\", p)) != wstring::npos) {
                ep.replace(p, 1, L"\\\\");
                p += 2;
            }
            wstring script = L"loadFromPath('" + ep + L"');";
            g_web->ExecuteScript(script.c_str(), nullptr);
        }
        return;
    }
    
    // Register window class
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = PdfWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"PdfProWorkspaceClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }
    
    // Create window
    g_hPdfWnd = CreateWindowExW(
        0, L"PdfProWorkspaceClass", L"PDF Pro",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        (int)(1200 * g_scaleFactor), (int)(800 * g_scaleFactor),
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    
    ShowWindow(g_hPdfWnd, SW_SHOWMAXIMIZED);
    SetForegroundWindow(g_hPdfWnd);
    UpdateWindow(g_hPdfWnd);
}

// ==========================================
// LEGACY FUNCTIONS
// ==========================================
void DrawPdfWorkspaceTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch) {
    FontFamily ff(L"Segoe UI");
    Font ft(&ff, 20 * g_scaleFactor, FontStyleBold, UnitPixel);
    SolidBrush tb(Color(255, 100, 100, 100));
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"PDF Pro Ready", -1, &ft, RectF(cx, cy, cw, ch), &sf, &tb);
}

void ProcessPdfWorkspaceMouseMove(float x, float y) {}
void ProcessPdfWorkspaceMouseClick(float x, float y) {}
