// ─────────────────────────────────────────────────────────────
// PART 12 · HTML skeleton — Acrobat DC exact layout
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<div id="app">
<button id="read-edit-btn" onclick="exitReadMode()">✏ Edit</button>

<!-- ── TOPBAR ── -->
<div class="topbar">
  <div class="ac-logo">A</div>
  <div class="top-menu" id="tm-file" onclick="toggleMenu('m-file',this)">File</div>
  <div class="top-menu" id="tm-edit" onclick="toggleMenu('m-edit',this)">Edit</div>
  <div class="top-menu" id="tm-view" onclick="toggleMenu('m-view',this)">View</div>
  <div class="top-menu" id="tm-tools" onclick="toggleMenu('m-tools',this)">Tools</div>
  <div class="top-menu" id="tm-doc"  onclick="toggleMenu('m-doc',this)">Document</div>
  <div class="top-menu" id="tm-help" onclick="toggleMenu('m-help',this)">Help</div>
  <div class="top-right">
    <div class="top-icon" title="Search (Ctrl+F)" onclick="toggleFindBar()">&#128269;</div>
    <div class="top-icon" title="Night/Sepia mode" onclick="cycleViewMode()">&#9790;</div>
    <div class="top-icon" title="Fullscreen" onclick="enterPresentation()">&#9974;</div>
  </div>
</div>

<!-- ── DROPDOWNS ── -->
<div class="dropdown" id="m-file">
  <div class="dd-item" onclick="openFileDialog()">&#128196; Open...<span class="dd-shortcut">Ctrl+O</span></div>
  <div class="dd-item" onclick="openFileDialog(true)">&#128196; Open Multiple...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="downloadCurrentPDF()">&#128190; Save<span class="dd-shortcut">Ctrl+S</span></div>
  <div class="dd-item" onclick="downloadCurrentPDF(true)">&#128190; Save As...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="actionMergePDFs()">&#128214; Combine Files...</div>
  <div class="dd-item" onclick="modalSplit()">&#9986; Split PDF...</div>
  <div class="dd-item" onclick="modalExtract()">&#128196; Extract Pages...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="actionPDFtoImages()">&#128247; Export as Images...</div>
  <div class="dd-item" onclick="actionPDFtoText()">&#128220; Export as Text...</div>
  <div class="dd-item" onclick="actionCompressPDF()">&#128230; Reduce File Size...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="window.print ? window.print() : showToast('Use Ctrl+P')">&#128438; Print...<span class="dd-shortcut">Ctrl+P</span></div>
  <div class="dd-sep"></div>
  <div class="dd-item danger" onclick="closeActiveTab()">&#10006; Close</div>
</div>
<div class="dropdown" id="m-edit">
  <div class="dd-item" onclick="histUndo()">&#8592; Undo<span class="dd-shortcut">Ctrl+Z</span></div>
  <div class="dd-item" onclick="histRedo()">&#8594; Redo<span class="dd-shortcut">Ctrl+Y</span></div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="copySelectedText()">&#128203; Copy<span class="dd-shortcut">Ctrl+C</span></div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="toggleFindBar()">&#128269; Find &amp; Replace...<span class="dd-shortcut">Ctrl+F</span></div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="showDocProperties()">&#8505; Properties...</div>
</div>
<div class="dropdown" id="m-view">
  <div class="dd-item" onclick="zoomTo(0.5)">Zoom 50%</div>
  <div class="dd-item" onclick="zoomTo(0.75)">Zoom 75%</div>
  <div class="dd-item" onclick="zoomTo(1.0)">Actual Size (100%)</div>
  <div class="dd-item" onclick="zoomTo(1.25)">Zoom 125%</div>
  <div class="dd-item" onclick="zoomTo(1.5)">Zoom 150%</div>
  <div class="dd-item" onclick="zoomTo(2.0)">Zoom 200%</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="toggleGrid()">&#9638; Show Grid</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="toggleLeftPanel()">&#9776; Page Thumbnails</div>
  <div class="dd-item" onclick="toggleRightPanel()">&#9881; Properties Panel</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="cycleViewMode()">&#9790; Night / Sepia / Normal</div>
  <div class="dd-item" onclick="enterReadMode();closeAllMenus()">&#9634; Reading Mode</div>
  <div class="dd-item" onclick="enterPresentation()">&#9654; Full Screen Mode</div>
</div>
<div class="dropdown" id="m-tools">
  <div class="dd-item" onclick="setTool('hand');closeAllMenus()">&#9995; Hand Tool</div>
  <div class="dd-item" onclick="setTool('select');closeAllMenus()">&#9654; Select Tool</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="setTool('highlight');closeAllMenus()">&#9998; Highlight Text</div>
  <div class="dd-item" onclick="setTool('underline');closeAllMenus()">&#818; Underline Text</div>
  <div class="dd-item" onclick="setTool('strikethrough');closeAllMenus()">&#818; Strikethrough</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="setTool('pen');closeAllMenus()">&#9998; Draw Freehand</div>
  <div class="dd-item" onclick="setTool('textbox');closeAllMenus()">&#8633; Add Text Box</div>
  <div class="dd-item" onclick="setTool('note');closeAllMenus()">&#128204; Add Comment</div>
  <div class="dd-item" onclick="setTool('stamp');closeAllMenus()">&#9997; Add Stamp</div>
  <div class="dd-item" onclick="openSignatureModal()">&#9998; Fill &amp; Sign</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="setTool('rect');closeAllMenus()">&#9645; Rectangle</div>
  <div class="dd-item" onclick="setTool('ellipse');closeAllMenus()">&#9711; Ellipse</div>
  <div class="dd-item" onclick="setTool('line');closeAllMenus()">&#8212; Line</div>
  <div class="dd-item" onclick="setTool('arrow');closeAllMenus()">&#10145; Arrow</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="setTool('redact');closeAllMenus()">&#9644; Redact</div>
  <div class="dd-item" onclick="setTool('crop');closeAllMenus()">&#9986; Crop Pages</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="actionPerformOCR()">&#128065; Recognize Text (OCR)</div>
</div>
<div class="dropdown" id="m-doc">
  <div class="dd-item" onclick="rotatePDFAll()">&#8635; Rotate Pages</div>
  <div class="dd-item" onclick="modalDeletePages()">&#128465; Delete Pages...</div>
  <div class="dd-item" onclick="modalInsertBlank()">&#10011; Insert Blank Page...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="modalWatermark()">&#10070; Watermark...</div>
  <div class="dd-item" onclick="modalHeaderFooter()">&#9776; Header &amp; Footer...</div>
  <div class="dd-item" onclick="modalBatesNumber()">&#9839; Bates Numbering...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="modalPassword()">&#128274; Encrypt with Password...</div>
  <div class="dd-item" onclick="showDocProperties()">&#8505; Document Properties...</div>
</div>
<div class="dropdown" id="m-help">
  <div class="dd-item" onclick="showShortcutModal()">&#9881; Keyboard Shortcuts</div>
  <div class="dd-item" onclick="showToast('PDF Pro — Acrobat Edition v2.0')">&#8505; About</div>
</div>
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 13 · Tabbar
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- ── TABBAR ── -->
<div class="tabbar" id="tabbar"></div>
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 14 · Quick Toolbar  (Acrobat DC horizontal tool strip)
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- ── QUICK TOOLBAR ── -->
<div class="quick-bar" id="quick-bar">

  <!-- Navigate group -->
  <div class="qb-group">
    <div class="qb-btn active" id="rb-hand" onclick="setTool('hand')" title="Hand (H)">
      <svg viewBox="0 0 24 24"><path d="M9 11V6a1 1 0 0 1 2 0v5h1V4a1 1 0 0 1 2 0v7h1V6a1 1 0 0 1 2 0v8l-1 5H9l-3-3V9a1 1 0 0 1 2 0v2z"/></svg>
      <span class="qb-lbl">Hand</span>
    </div>
    <div class="qb-btn" id="rb-select" onclick="setTool('select')" title="Select (V)">
      <svg viewBox="0 0 24 24"><path d="M4 4l7 18 3-7 7-3z"/></svg>
      <span class="qb-lbl">Select</span>
    </div>
  </div>

  <!-- Markup group -->
  <div class="qb-group">
    <div class="qb-btn" id="rb-highlight" onclick="setTool('highlight')" title="Highlight (U)">
      <svg viewBox="0 0 24 24"><rect x="3" y="15" width="18" height="4" rx="1" fill="#F9A825" opacity=".7"/><path d="M7 14l5-10 5 10" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>
      <span class="qb-lbl">Highlight</span>
    </div>
    <div class="qb-btn" id="rb-underline" onclick="setTool('underline')" title="Underline">
      <svg viewBox="0 0 24 24"><path d="M6 3v7a6 6 0 0 0 12 0V3h-2v7a4 4 0 0 1-8 0V3H6zm-2 15h16v2H4z"/></svg>
      <span class="qb-lbl">Underline</span>
    </div>
    <div class="qb-btn" id="rb-strikethrough" onclick="setTool('strikethrough')" title="Strikethrough">
      <svg viewBox="0 0 24 24"><path d="M6 3v7a6 6 0 0 0 12 0V3h-2v7a4 4 0 0 1-8 0V3H6zM2 11h20v2H2z"/></svg>
      <span class="qb-lbl">Strike</span>
    </div>
  </div>

  <!-- Draw group -->
  <div class="qb-group">
    <div class="qb-btn" id="rb-pen" onclick="setTool('pen')" title="Draw (D)">
      <svg viewBox="0 0 24 24"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zm17.71-10.21a1 1 0 0 0 0-1.41l-2.34-2.34a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>
      <span class="qb-lbl">Draw</span>
    </div>
    <div class="qb-btn" id="rb-eraser" onclick="setTool('eraser')" title="Eraser (E)">
      <svg viewBox="0 0 24 24"><path d="M16.24 3.56l4.2 4.2a2 2 0 0 1 0 2.83L8.1 22.83a4 4 0 0 1-5.66 0l-.17-.17a4 4 0 0 1 0-5.66L13.41 5.76a2 2 0 0 1 2.83 0z"/></svg>
      <span class="qb-lbl">Erase</span>
    </div>
    <div class="qb-btn" id="rb-note" onclick="setTool('note')" title="Comment (N)">
      <svg viewBox="0 0 24 24"><path d="M20 2H4a2 2 0 0 0-2 2v14l4-4h14a2 2 0 0 0 2-2V4a2 2 0 0 0-2-2z"/></svg>
      <span class="qb-lbl">Comment</span>
    </div>
    <div class="qb-btn" id="rb-textbox" onclick="setTool('textbox')" title="Text Box (T)">
      <svg viewBox="0 0 24 24"><path d="M2 4v3h5v12h3V7h5V4H2zm19 5h-9v3h3v7h3v-7h3V9z"/></svg>
      <span class="qb-lbl">Text</span>
    </div>
  </div>

  <!-- Shapes group -->
  <div class="qb-group">
    <div class="qb-btn" id="rb-rect" onclick="setTool('rect')">
      <svg viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="14" rx="1" fill="none" stroke="currentColor" stroke-width="2"/></svg>
      <span class="qb-lbl">Rect</span>
    </div>
    <div class="qb-btn" id="rb-ellipse" onclick="setTool('ellipse')">
      <svg viewBox="0 0 24 24"><ellipse cx="12" cy="12" rx="9" ry="6" fill="none" stroke="currentColor" stroke-width="2"/></svg>
      <span class="qb-lbl">Circle</span>
    </div>
    <div class="qb-btn" id="rb-line" onclick="setTool('line')">
      <svg viewBox="0 0 24 24"><line x1="4" y1="20" x2="20" y2="4" stroke="currentColor" stroke-width="2"/></svg>
      <span class="qb-lbl">Line</span>
    </div>
    <div class="qb-btn" id="rb-arrow" onclick="setTool('arrow')">
      <svg viewBox="0 0 24 24"><path d="M4 12h16M14 6l6 6-6 6" fill="none" stroke="currentColor" stroke-width="2"/></svg>
      <span class="qb-lbl">Arrow</span>
    </div>
  </div>

  <!-- Style controls -->
  <div class="qb-group">
    <div style="display:flex;flex-direction:column;gap:2px;">
      <div class="qb-btn" style="padding:2px 6px;" onclick="showColorPicker('stroke','rb-color-stroke')">
        <div style="display:flex;align-items:center;gap:4px;">
          <div id="rb-color-stroke" style="width:14px;height:14px;border-radius:1px;background:#E8423F;border:1px solid #bbb;flex-shrink:0;"></div>
          <span class="qb-lbl">Color</span>
        </div>
      </div>
      <div class="qb-btn" style="padding:2px 6px;" onclick="showColorPicker('fill','rb-color-fill')">
        <div style="display:flex;align-items:center;gap:4px;">
          <div id="rb-color-fill" style="width:14px;height:14px;border-radius:1px;background:transparent;border:1px dashed #bbb;flex-shrink:0;"></div>
          <span class="qb-lbl">Fill</span>
        </div>
      </div>
    </div>
    <div style="display:flex;flex-direction:column;gap:3px;margin-left:4px;">
      <div style="display:flex;align-items:center;gap:3px;">
        <span style="font-size:9px;color:var(--ac-muted);min-width:24px;">Size</span>
        <input class="qb-zoom-input" style="width:36px;" type="number" id="rb-linewidth" value="2" min="1" max="30" onchange="g_lineWidth=+this.value">
      </div>
      <div style="display:flex;align-items:center;gap:3px;">
        <span style="font-size:9px;color:var(--ac-muted);min-width:24px;">Opac</span>
        <input class="qb-zoom-input" style="width:36px;" type="number" id="rb-opacity" value="100" min="1" max="100" onchange="g_opacity=+this.value/100">
      </div>
    </div>
  </div>

  <!-- Mode toggle -->
  <div class="qb-group">
    <div class="qb-btn active" id="rb-readmode" onclick="setViewerMode(true)" title="View Mode (native, fast)">
      <svg viewBox="0 0 24 24"><path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5C21.27 7.61 17 4.5 12 4.5zm0 12.5a5 5 0 1 1 0-10 5 5 0 0 1 0 10zm0-8a3 3 0 1 0 0 6 3 3 0 0 0 0-6z"/></svg>
      <span class="qb-lbl">View</span>
    </div>
    <div class="qb-btn" id="rb-editmode" onclick="setViewerMode(false)" title="Annotate Mode">
      <svg viewBox="0 0 24 24"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04a1 1 0 0 0 0-1.41l-2.34-2.34a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>
      <span class="qb-lbl">Annotate</span>
    </div>
  </div>

  <!-- Zoom controls -->
  <div class="qb-group" style="margin-left:auto;">
    <div class="qb-zoom">
      <button class="qb-zoom-btn" onclick="zoomBy(-0.12)">−</button>
      <input class="qb-zoom-input" id="qb-zoom-display" value="100%" style="width:52px;"
        onkeydown="if(event.key==='Enter'){applyZoomInput(this.value);this.blur();}"
        onfocus="this.select()">
      <button class="qb-zoom-btn" onclick="zoomBy(0.12)">+</button>
    </div>
  </div>

  <!-- Panel toggles -->
  <div style="display:flex;align-items:center;gap:2px;margin-left:6px;">
    <div class="qb-btn" onclick="toggleLeftPanel()" title="Thumbnails (F4)">
      <svg viewBox="0 0 24 24"><path d="M3 3h7v7H3zm0 11h7v7H3zM14 3h7v7h-7zm0 11h7v7h-7z"/></svg>
    </div>
    <div class="qb-btn" onclick="toggleRightPanel()" title="Properties">
      <svg viewBox="0 0 24 24"><path d="M19 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2V5a2 2 0 0 0-2-2zm-7 14l-5-5 1.41-1.41L12 14.17l7.59-7.59L21 8l-9 9z"/></svg>
    </div>
  </div>

  <!-- Stamp + Sign -->
  <div class="qb-group" style="margin-left:4px;">
    <div class="qb-btn" id="rb-stamp" onclick="setTool('stamp')" title="Stamp">
      <svg viewBox="0 0 24 24"><path d="M12 2a5 5 0 0 1 5 5c0 2.38-1.7 4.38-4 4.87V13h2v2h-2v2h-2v-2H9v-2h2v-1.13C8.7 11.38 7 9.38 7 7a5 5 0 0 1 5-5zM4 20v-1a2 2 0 0 1 2-2h12a2 2 0 0 1 2 2v1H4z"/></svg>
      <span class="qb-lbl">Stamp</span>
    </div>
    <div class="qb-btn" onclick="openSignatureModal()" title="Signature">
      <svg viewBox="0 0 24 24"><path d="M3 20h4.5L20 7.5 16.5 4 3 16.5V20zm18-13L17.5 3.5l2-2L23 5l-2 2z"/></svg>
      <span class="qb-lbl">Sign</span>
    </div>
  </div>

</div><!-- end quick-bar -->
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 15 · Workspace: left panel + viewer + right panel
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- ── WORKSPACE ── -->
<div class="workspace">

  <!-- Left panel: thumbnails + bookmarks -->
  <div class="lv-panel" id="left-panel">
    <div class="lv-tabs">
      <div class="lv-tab active" id="lpt-thumb" onclick="switchLPanel('thumb',this)">Thumbnails</div>
      <div class="lv-tab" id="lpt-bm"    onclick="switchLPanel('bm',this)">Bookmarks</div>
    </div>
    <div class="lv-body">
      <div id="lp-thumb" class="thumb-list"></div>
      <div id="lp-bm" style="display:none;"></div>
    </div>
  </div>

  <!-- PDF Viewer -->
  <div class="pdf-viewer-area" id="viewer-area">
    <div class="pdf-container" id="pdf-container">
      <!-- Empty state -->
      <div id="empty-hint" style="margin-top:0;text-align:center;color:#aaa;pointer-events:auto;width:100%;padding:40px 20px;">
        <div style="font-size:56px;opacity:.18;margin-bottom:12px;">&#128196;</div>
        <p style="font-size:18px;font-weight:700;color:#ddd;margin-bottom:6px;">Adobe Acrobat</p>
        <p style="font-size:12px;opacity:.5;margin-bottom:24px;">Open a PDF to get started</p>
        <div style="display:flex;gap:12px;justify-content:center;">
          <button onclick="openFileDialog(false)" style="padding:8px 22px;background:var(--ac-red);color:#fff;border:none;border-radius:2px;font-size:13px;font-weight:700;cursor:pointer;">Open PDF</button>
          <button onclick="openFileDialog(true)"  style="padding:8px 22px;background:var(--ac-blue);color:#fff;border:none;border-radius:2px;font-size:13px;font-weight:700;cursor:pointer;">Open Multiple</button>
        </div>
        <div id="recent-files-section" style="max-width:500px;margin:28px auto 0;text-align:left;">
          <p style="font-size:10px;font-weight:700;color:#888;text-transform:uppercase;letter-spacing:1px;margin-bottom:8px;">Recent Files</p>
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

  <!-- Right Properties Panel -->
  <div class="right-panel" id="right-panel">
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#8505; Document <span class="toggle">&#9660;</span></div>
      <div class="rp-body" id="rp-docinfo">
        <div class="rp-row"><span class="rp-label">File</span><span id="rp-filename" style="font-size:10px;word-break:break-all;">—</span></div>
        <div class="rp-row"><span class="rp-label">Pages</span><span id="rp-pages">—</span></div>
        <div class="rp-row"><span class="rp-label">Zoom</span><span id="rp-zoom">100%</span></div>
        <div class="rp-row"><span class="rp-label">Rotate</span><span id="rp-rotate">0°</span></div>
      </div>
    </div>
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#9998; Annotation Style <span class="toggle">&#9660;</span></div>
      <div class="rp-body">
        <div class="rp-row">
          <span class="rp-label">Color</span>
          <div style="width:24px;height:24px;border-radius:1px;background:#E8423F;border:1px solid #bbb;cursor:pointer;" id="rp-color" onclick="showColorPicker('stroke','rp-color')"></div>
        </div>
        <div class="rp-row"><span class="rp-label">Width</span><input class="rp-input" type="range" min="1" max="30" value="2" oninput="g_lineWidth=+this.value;document.getElementById('rb-linewidth').value=this.value"></div>
        <div class="rp-row"><span class="rp-label">Opacity</span><input class="rp-input" type="range" min="5" max="100" value="100" oninput="g_opacity=+this.value/100;document.getElementById('rb-opacity').value=this.value"></div>
        <div class="rp-row"><span class="rp-label">Font</span><select class="rp-input" id="rp-font" onchange="g_fontFamily=this.value"><option>Arial</option><option>Times New Roman</option><option>Courier New</option><option>Georgia</option></select></div>
        <div class="rp-row"><span class="rp-label">Size</span><input class="rp-input" type="number" id="rp-fontsize" value="12" min="6" max="96" onchange="g_fontSize=+this.value"></div>
      </div>
    </div>
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#9889; Quick Actions <span class="toggle">&#9660;</span></div>
      <div class="rp-body" style="gap:4px;">
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
        </select>
      </div>
    </div>
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#9679; Stats <span class="toggle">&#9660;</span></div>
      <div class="rp-body">
        <div class="rp-row"><span class="rp-label">Words</span><span id="stat-words" class="badge">0</span></div>
        <div class="rp-row"><span class="rp-label">Chars</span><span id="stat-chars" class="badge">0</span></div>
        <div class="rp-row"><span class="rp-label">Annots</span><span id="stat-annots" class="badge">0</span></div>
        <button class="rp-btn" onclick="refreshStats()" style="margin-top:4px;">Refresh</button>
      </div>
    </div>
  </div>
</div><!-- end workspace -->
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 16 · Statusbar  (Acrobat DC bottom strip)
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- ── STATUS BAR ── -->
<div class="statusbar">
  <span class="sb-item" id="sb-tool">Hand Tool</span>
  <div class="sb-sep"></div>
  <span class="sb-item" style="display:flex;align-items:center;gap:4px;">
    <button class="sb-zoom-btn" style="font-size:10px;width:auto;padding:0 4px;" onclick="jumpToPageFromInput(Math.max(1,(parseInt(document.getElementById('sb-page-input').value)||1)-1))">&#9664;</button>
    Page <input id="sb-page-input" type="number" min="1"
      style="width:36px;padding:1px 3px;border:1px solid #C0C0C0;border-radius:1px;font-size:10.5px;text-align:center;background:#fff;color:#222;"
      onkeydown="if(event.key==='Enter'){jumpToPageFromInput(this.value);}"
      onfocus="this.select()">
    <span id="sb-page-total" style="font-size:10.5px;color:var(--ac-muted);">of 0</span>
    <button class="sb-zoom-btn" style="font-size:10px;width:auto;padding:0 4px;" onclick="jumpToPageFromInput(Math.min((activeTab()||{pageOrder:[]}).pageOrder.length,(parseInt(document.getElementById('sb-page-input').value)||0)+1))">&#9654;</button>
  </span>
  <div class="sb-sep"></div>
  <div class="sb-zoom-row">
    <button class="sb-zoom-btn" onclick="zoomBy(-0.1)">−</button>
    <span id="sb-zoom-val" style="font-size:10.5px;min-width:38px;text-align:center;color:var(--ac-text);">100%</span>
    <button class="sb-zoom-btn" onclick="zoomBy(0.1)">+</button>
  </div>
  <div class="sb-sep"></div>
  <span class="sb-item" id="sb-coords">0, 0</span>
  <div class="sb-right">
    <span class="sb-item" id="sb-mode">View</span>
    <div class="sb-sep"></div>
    <span class="sb-item" style="cursor:pointer;" onclick="enterReadMode()" title="Reading Mode">&#9634;</span>
    <span class="sb-item" style="cursor:pointer;" onclick="enterPresentation()" title="Full Screen">&#9654;</span>
  </div>
</div>
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 17 · Overlays: color picker, context menu, modals
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- Color picker -->
<div class="color-picker-popup" id="color-picker-popup">
  <div class="color-grid" id="color-grid"></div>
  <div style="margin-top:8px;display:flex;align-items:center;gap:6px;">
    <label style="font-size:10px;color:#666;">Custom:</label>
    <input type="color" id="color-custom" style="width:36px;height:22px;border:none;cursor:pointer;" onchange="applyCustomColor(this.value)">
  </div>
</div>
<!-- Context menu -->
<div class="ctx-menu" id="ctx-menu">
  <div class="ctx-item" onclick="ctxCopy()">&#128203; Copy</div>
  <div class="ctx-item" onclick="setTool('pen');closeCtx()">&#9998; Draw Here</div>
  <div class="ctx-item" onclick="setTool('note');closeCtx()">&#128204; Add Comment</div>
  <div class="ctx-item" onclick="addBookmarkAtCtx()">&#9733; Add Bookmark</div>
  <div class="ctx-sep"></div>
  <div class="ctx-item" onclick="rotatePDFAll();closeCtx()">&#8635; Rotate Page</div>
  <div class="ctx-item danger" onclick="ctxDeletePage()">&#128465; Delete Page</div>
</div>
<!-- Signature modal -->
<div class="modal-overlay" id="sig-modal-overlay">
  <div class="modal">
    <h3>&#9998; Fill &amp; Sign — Draw Signature</h3>
    <canvas id="sig-modal-canvas" width="440" height="160" style="border:1px solid var(--ac-border);background:#fff;display:block;touch-action:none;"></canvas>
    <div style="display:flex;gap:6px;margin-top:8px;align-items:center;">
      <button class="btn btn-secondary" onclick="clearSigPad()">Clear</button>
      <select id="sig-color" style="padding:4px;border:1px solid var(--ac-border);border-radius:2px;" onchange="g_sigColor=this.value">
        <option value="#1a1a1a">Black</option>
        <option value="#1a3a8f">Blue</option>
        <option value="#8b0000">Dark Red</option>
      </select>
    </div>
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeSigModal()">Cancel</button>
      <button class="btn btn-primary" onclick="applySigToPage()">Place Signature</button>
    </div>
  </div>
</div>
<!-- General modal -->
<div class="modal-overlay" id="modal-overlay"><div class="modal" id="modal-body"></div></div>
<!-- Loading -->
<div class="loading-overlay" id="loading-overlay">
  <div class="spinner"></div>
  <div id="loading-txt">Processing…</div>
  <div class="progress-wrap"><div class="progress-bar" id="loading-progress" style="width:0%;"></div></div>
</div>
<!-- Toasts -->
<div class="toast-box" id="toast-box"></div>
<!-- File inputs -->
<input type="file" id="fileInput"  accept=".pdf" style="display:none" multiple onchange="handleFiles(event)">
<input type="file" id="mergeInput" accept=".pdf" style="display:none" multiple onchange="doMerge(event)">
</div><!-- #app -->
)HTML";

