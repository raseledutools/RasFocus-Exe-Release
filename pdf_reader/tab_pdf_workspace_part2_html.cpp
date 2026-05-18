// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<div id="app">
<!-- ── READ MODE FLOATING EDIT BUTTON ── -->
<button id="read-edit-btn" onclick="exitReadMode()" title="Exit Read Mode & Edit">✏️ Edit</button>
<!-- ── TOPBAR ── -->
<div class="topbar">
  <div class="top-menu" id="tm-file" onclick="toggleMenu('m-file',this)">File</div>
  <div class="top-menu" id="tm-edit" onclick="toggleMenu('m-edit',this)">Edit</div>
  <div class="top-menu" id="tm-view" onclick="toggleMenu('m-view',this)">View</div>
  <div class="top-menu" id="tm-tools" onclick="toggleMenu('m-tools',this)">Tools</div>
  <div class="top-menu" id="tm-doc" onclick="toggleMenu('m-doc',this)">Document</div>
  <div class="top-menu" id="tm-forms" onclick="toggleMenu('m-forms',this)">Forms</div>
  <div class="top-menu" id="tm-help" onclick="toggleMenu('m-help',this)">Help</div>
  <div class="top-sep"></div>
  <div class="top-right">
    <div class="top-icon" title="Left Panel (Thumbs)" onclick="toggleLeftPanel()" style="font-size:13px;">&#9776;</div>
    <div class="top-icon" title="Right Panel (Properties)" onclick="toggleRightPanel()" style="font-size:13px;">&#9783;</div>
    <div class="top-icon" title="Find (Ctrl+F)" onclick="toggleFindBar()">&#128269;</div>
    <div class="top-icon" title="Presentation" onclick="enterPresentation()">&#9654;</div>
    <div class="top-icon" title="Night Mode" onclick="cycleViewMode()">&#9790;</div>
  </div>
</div>
<!-- ── DROPDOWN MENUS ── -->
<div class="dropdown" id="m-file">
  <div class="dd-item" onclick="openFileDialog()">&#128196; Open...<span class="dd-shortcut">Ctrl+O</span></div>
  <div class="dd-item" onclick="openFileDialog(true)">&#128196; Open Multiple...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="downloadCurrentPDF()">&#128190; Save<span class="dd-shortcut">Ctrl+S</span></div>
  <div class="dd-item" onclick="downloadCurrentPDF(true)">&#128190; Save As...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="actionMergePDFs()">&#128214; Combine PDFs...</div>
  <div class="dd-item" onclick="modalSplit()">&#9986; Split PDF...</div>
  <div class="dd-item" onclick="modalExtract()">&#128196; Extract Pages...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="actionPDFtoImages()">&#128247; Export as Images...</div>
  <div class="dd-item" onclick="actionPDFtoText()">&#128220; Export as Text...</div>
  <div class="dd-item" onclick="actionCompressPDF()">&#128230; Compress PDF...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="window.print ? window.print() : showToast('Use Ctrl+P')">&#128438; Print<span class="dd-shortcut">Ctrl+P</span></div>
  <div class="dd-sep"></div>
  <div class="dd-item danger" onclick="closeActiveTab()">&#10006; Close Tab</div>
</div>
)HTML";

ss << LR"HTML(
<div class="dropdown" id="m-edit">
  <div class="dd-item" onclick="histUndo()">&#8592; Undo<span class="dd-shortcut">Ctrl+Z</span></div>
  <div class="dd-item" onclick="histRedo()">&#8594; Redo<span class="dd-shortcut">Ctrl+Y</span></div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="setTool('select')">&#9654; Select Text</div>
  <div class="dd-item" onclick="copySelectedText()">&#128203; Copy<span class="dd-shortcut">Ctrl+C</span></div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="toggleFindBar()">&#128269; Find &amp; Replace<span class="dd-shortcut">Ctrl+F</span></div>
</div>
<div class="dropdown" id="m-view">
  <div class="dd-item" onclick="zoomTo(0.5)">50%</div>
  <div class="dd-item" onclick="zoomTo(0.75)">75%</div>
  <div class="dd-item" onclick="zoomTo(1.0)">100%</div>
  <div class="dd-item" onclick="zoomTo(1.25)">125%</div>
  <div class="dd-item" onclick="zoomTo(1.5)">150%</div>
  <div class="dd-item" onclick="zoomTo(2.0)">200%</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="toggleRuler()">&#8213; Ruler</div>
  <div class="dd-item" onclick="toggleGrid()">&#9638; Grid</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="toggleLeftPanel()">&#9776; Thumbnails Panel</div>
  <div class="dd-item" onclick="toggleRightPanel()">&#9881; Properties Panel</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="cycleViewMode()">&#9790; Night / Sepia / Normal</div>
  <div class="dd-item" onclick="enterReadMode();closeAllMenus()">&#9634; Read Mode (ESC to exit)</div>
  <div class="dd-item" onclick="enterPresentation()">&#9654; Presentation Mode</div>
</div>
)HTML";

ss << LR"HTML(
<div class="dropdown" id="m-tools">
  <div class="dd-item" onclick="setTool('hand');closeAllMenus()">&#9995; Hand Tool</div>
  <div class="dd-item" onclick="setTool('pen');closeAllMenus()">&#9998; Pen (Draw)</div>
  <div class="dd-item" onclick="setTool('highlight');closeAllMenus()">&#9998; Highlight</div>
  <div class="dd-item" onclick="setTool('eraser');closeAllMenus()">&#9003; Eraser</div>
  <div class="dd-item" onclick="setTool('textbox');closeAllMenus()">&#8633; Text Box</div>
  <div class="dd-item" onclick="setTool('note');closeAllMenus()">&#128204; Sticky Note</div>
  <div class="dd-item" onclick="setTool('stamp');closeAllMenus()">&#9997; Stamp</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="setTool('rect');closeAllMenus()">&#9645; Draw Rectangle</div>
  <div class="dd-item" onclick="setTool('ellipse');closeAllMenus()">&#9711; Draw Ellipse</div>
  <div class="dd-item" onclick="setTool('line');closeAllMenus()">&#8212; Draw Line</div>
  <div class="dd-item" onclick="setTool('arrow');closeAllMenus()">&#10145; Draw Arrow</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="setTool('redact');closeAllMenus()">&#9644; Redaction</div>
  <div class="dd-item" onclick="setTool('crop');closeAllMenus()">&#9986; Crop</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="openSignatureModal()">&#9998; Signature</div>
  <div class="dd-item" onclick="actionPerformOCR()">&#128065; OCR Scanner</div>
</div>
)HTML";

ss << LR"HTML(
<div class="dropdown" id="m-doc">
  <div class="dd-item" onclick="rotatePDFAll()">&#8635; Rotate All Pages</div>
  <div class="dd-item" onclick="modalDeletePages()">&#128465; Delete Pages...</div>
  <div class="dd-item" onclick="modalInsertBlank()">&#10011; Insert Blank Page...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="modalWatermark()">&#10070; Watermark...</div>
  <div class="dd-item" onclick="modalHeaderFooter()">&#9776; Header &amp; Footer...</div>
  <div class="dd-item" onclick="modalBatesNumber()">&#9839; Bates Numbering...</div>
  <div class="dd-sep"></div>
  <div class="dd-item" onclick="modalPassword()">&#128274; Encrypt / Password...</div>
  <div class="dd-item" onclick="showDocProperties()">&#8505; Document Properties</div>
</div>
<div class="dropdown" id="m-forms">
  <div class="dd-item" onclick="showToast('Form features: coming in enterprise build.')">&#9744; Checkbox Field</div>
  <div class="dd-item" onclick="showToast('Form features: coming in enterprise build.')">&#9675; Radio Field</div>
  <div class="dd-item" onclick="showToast('Form features: coming in enterprise build.')">&#9633; Text Field</div>
  <div class="dd-item" onclick="showToast('Form features: coming in enterprise build.')">&#9013; Button</div>
</div>
<div class="dropdown" id="m-help">
  <div class="dd-item" onclick="showShortcutModal()">&#9881; Keyboard Shortcuts</div>
  <div class="dd-item" onclick="showToast('PDF Pro — Acrobat Edition v1.0')">&#8505; About</div>
</div>
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 13 · Tabbar HTML
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- ── TABBAR ── -->
<div class="tabbar" id="tabbar"></div>
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 14 · Ribbon (Home tab buttons)
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- ── RIBBON ── -->
<div class="ribbon-wrap" id="ribbon-wrap">
<div class="ribbon-tabs">
  <div class="rtab active" onclick="switchRibbon('r-home',this)">Home</div>
  <div class="rtab" onclick="switchRibbon('r-annotate',this)">Annotate</div>
  <div class="rtab" onclick="switchRibbon('r-edit',this)">Edit PDF</div>
  <div class="rtab" onclick="switchRibbon('r-pages',this)">Pages</div>
  <div class="rtab" onclick="switchRibbon('r-protect',this)">Protect</div>
  <div class="rtab" onclick="switchRibbon('r-export',this)">Export</div>
</div>
<!-- HOME ribbon -->
<div class="ribbon-panel active" id="r-home">
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn active" id="rb-hand" onclick="setTool('hand')" title="Hand (1)">
        <svg viewBox="0 0 24 24"><path d="M9 11V6a1 1 0 0 1 2 0v5h1V4a1 1 0 0 1 2 0v7h1V6a1 1 0 0 1 2 0v8l-1 5H9l-3-3V9a1 1 0 0 1 2 0v2z"/></svg>
        <span class="rbtn-lbl">Hand</span>
      </div>
      <div class="rbtn" id="rb-select" onclick="setTool('select')" title="Select (2)">
        <svg viewBox="0 0 24 24"><path d="M4 4l7 18 3-7 7-3z"/></svg>
        <span class="rbtn-lbl">Select</span>
      </div>
    </div>
    <div class="rg-label">Navigate</div>
  </div>
)HTML";

ss << LR"HTML(
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn" id="rb-pen" onclick="setTool('pen')" title="Pen (3)">
        <svg viewBox="0 0 24 24"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zm17.71-10.21a1 1 0 0 0 0-1.41l-2.34-2.34a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>
        <span class="rbtn-lbl">Pen</span>
      </div>
      <div class="rbtn" id="rb-highlight" onclick="setTool('highlight')" title="Highlight (4)">
        <svg viewBox="0 0 24 24"><rect x="3" y="14" width="18" height="5" rx="1" fill="#f9a825" opacity=".5"/><path d="M6 13l6-9 6 9" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>
        <span class="rbtn-lbl">Highlight</span>
      </div>
      <div class="rbtn" id="rb-eraser" onclick="setTool('eraser')" title="Eraser (5)">
        <svg viewBox="0 0 24 24"><path d="M16.24 3.56l4.2 4.2c.78.78.78 2.05 0 2.83L8.1 22.83a4 4 0 0 1-5.66 0l-.17-.17a4 4 0 0 1 0-5.66L13.41 5.76c.78-.78 2.05-.78 2.83 0zM5.76 18.41a2 2 0 0 0 2.83 0L19 8l-3-3L5.24 15.58a2 2 0 0 0 0 2.83z"/></svg>
        <span class="rbtn-lbl">Eraser</span>
      </div>
    </div>
    <div class="rg-label">Draw</div>
  </div>
)HTML";

ss << LR"HTML(
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn" id="rb-note" onclick="setTool('note')" title="Note (6)">
        <svg viewBox="0 0 24 24"><path d="M20 2H4a2 2 0 0 0-2 2v14l4-4h14a2 2 0 0 0 2-2V4a2 2 0 0 0-2-2z"/></svg>
        <span class="rbtn-lbl">Note</span>
      </div>
      <div class="rbtn" id="rb-textbox" onclick="setTool('textbox')" title="Text Box (7)">
        <svg viewBox="0 0 24 24"><path d="M2 4v3h5v12h3V7h5V4H2zm19 5h-9v3h3v7h3v-7h3V9z"/></svg>
        <span class="rbtn-lbl">TextBox</span>
      </div>
    </div>
    <div class="rg-row">
      <div class="rbtn" id="rb-stamp" onclick="setTool('stamp')" title="Stamp (8)">
        <svg viewBox="0 0 24 24"><path d="M12 2a5 5 0 0 1 5 5c0 2.38-1.7 4.38-4 4.87V13h2v2h-2v2h-2v-2H9v-2h2v-1.13C8.7 11.38 7 9.38 7 7a5 5 0 0 1 5-5zM4 20v-1a2 2 0 0 1 2-2h12a2 2 0 0 1 2 2v1H4z"/></svg>
        <span class="rbtn-lbl">Stamp</span>
      </div>
      <div class="rbtn" onclick="openSignatureModal()" title="Signature">
        <svg viewBox="0 0 24 24"><path d="M21 17l-1 1H4l-1-1V7l1-1h16l1 1v10zm-9-8c-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4-1.79-4-4-4zm0 2a2 2 0 1 1 0 4 2 2 0 0 1 0-4z"/></svg>
        <span class="rbtn-lbl">Sign</span>
      </div>
    </div>
    <div class="rg-label">Insert</div>
  </div>
)HTML";

ss << LR"HTML(
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn" id="rb-rect" onclick="setTool('rect')">
        <svg viewBox="0 0 24 24"><rect x="3" y="5" width="18" height="14" rx="1" fill="none" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Rect</span>
      </div>
      <div class="rbtn" id="rb-ellipse" onclick="setTool('ellipse')">
        <svg viewBox="0 0 24 24"><ellipse cx="12" cy="12" rx="9" ry="6" fill="none" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Circle</span>
      </div>
      <div class="rbtn" id="rb-line" onclick="setTool('line')">
        <svg viewBox="0 0 24 24"><line x1="4" y1="20" x2="20" y2="4" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Line</span>
      </div>
      <div class="rbtn" id="rb-arrow" onclick="setTool('arrow')">
        <svg viewBox="0 0 24 24"><path d="M4 12h16M14 6l6 6-6 6" fill="none" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Arrow</span>
      </div>
    </div>
    <div class="rg-label">Shapes</div>
  </div>
)HTML";

ss << LR"HTML(
  <div class="ribbon-group">
    <div class="rg-row">
      <div style="display:flex;flex-direction:column;gap:3px;">
        <div class="rbtn-sm" onclick="showColorPicker('stroke','rb-color-stroke')">
          <div class="color-swatch" id="rb-color-stroke" style="background:#e53935;"></div> Color
        </div>
        <div class="rbtn-sm" onclick="showColorPicker('fill','rb-color-fill')">
          <div class="color-swatch" id="rb-color-fill" style="background:transparent;border-style:dashed;"></div> Fill
        </div>
      </div>
      <div style="display:flex;flex-direction:column;gap:3px;">
        <div style="display:flex;align-items:center;gap:3px;">
          <span style="font-size:9.5px;color:var(--c-muted);">Size</span>
          <input class="ribbon-num" type="number" id="rb-linewidth" value="2" min="1" max="30" onchange="g_lineWidth=+this.value" title="Stroke width">
        </div>
        <div style="display:flex;align-items:center;gap:3px;">
          <span style="font-size:9.5px;color:var(--c-muted);">Opacity</span>
          <input class="ribbon-num" type="number" id="rb-opacity" value="100" min="1" max="100" onchange="g_opacity=+this.value/100" title="Opacity %">
        </div>
      </div>
    </div>
    <div class="rg-label">Style</div>
  </div>
)HTML";

ss << LR"HTML(
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn" onclick="zoomBy(-0.15)" title="Zoom Out">
        <svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="7" fill="none" stroke="currentColor" stroke-width="2"/><path d="M21 21l-4-4M8 11h6" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Zoom—</span>
      </div>
      <div class="rbtn" onclick="zoomBy(0.15)" title="Zoom In">
        <svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="7" fill="none" stroke="currentColor" stroke-width="2"/><path d="M21 21l-4-4M8 11h6M11 8v6" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">Zoom+</span>
      </div>
      <div class="rbtn" onclick="zoomTo(1.0)">
        <svg viewBox="0 0 24 24"><path d="M12 5v14M5 12h14" stroke="currentColor" stroke-width="2"/></svg>
        <span class="rbtn-lbl">100%</span>
      </div>
    </div>
    <div class="rg-label">Zoom</div>
  </div>
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn active" id="rb-readmode" onclick="setViewerMode(true)" title="Native Read Mode — vector sharp, text select (Ctrl+R)">
        <svg viewBox="0 0 24 24"><path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5C21.27 7.61 17 4.5 12 4.5zm0 12.5a5 5 0 1 1 0-10 5 5 0 0 1 0 10zm0-8a3 3 0 1 0 0 6 3 3 0 0 0 0-6z" fill="currentColor"/></svg>
        <span class="rbtn-lbl">Read</span>
      </div>
      <div class="rbtn" id="rb-editmode" onclick="setViewerMode(false)" title="Edit Mode — annotation tools (Ctrl+R)">
        <svg viewBox="0 0 24 24"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04a1 1 0 0 0 0-1.41l-2.34-2.34a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z" fill="currentColor"/></svg>
        <span class="rbtn-lbl">Annotate</span>
      </div>
    </div>
    <div class="rg-label">Mode</div>
  </div>
</div>
)HTML";

ss << LR"HTML(
<!-- ANNOTATE ribbon -->
<div class="ribbon-panel" id="r-annotate">
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn-sm" onclick="setTool('highlight');closeAllMenus()"><svg viewBox="0 0 24 24" width="13" height="13"><rect x="3" y="14" width="18" height="5" rx="1" fill="#f9a825"/><path d="M6 13l6-9 6 9" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>Highlight</div>
    </div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('underline')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M6 3v7a6 6 0 0 0 12 0V3h-2v7a4 4 0 0 1-8 0V3H6zm-2 15h16v2H4z"/></svg>Underline</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('strikethrough')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M6 3v7a6 6 0 0 0 12 0V3h-2v7a4 4 0 0 1-8 0V3H6zM2 11h20v2H2z"/></svg>Strikethrough</div></div>
    <div class="rg-label">Text Markup</div>
  </div>
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn-sm" onclick="setTool('pen')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zm17.71-10.21a1 1 0 0 0 0-1.41l-2.34-2.34a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>Free Draw</div>
    </div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('eraser')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M16.24 3.56l4.2 4.2c.78.78.78 2.05 0 2.83L8.1 22.83a4 4 0 0 1-5.66 0l-.17-.17a4 4 0 0 1 0-5.66L13.41 5.76c.78-.78 2.05-.78 2.83 0z"/></svg>Eraser</div></div>
    <div class="rg-label">Drawing</div>
  </div>
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn-sm" onclick="addBookmark()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M17 3H7a2 2 0 0 0-2 2v16l7-3 7 3V5a2 2 0 0 0-2-2z"/></svg>Add Bookmark</div>
    </div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('note')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M20 2H4a2 2 0 0 0-2 2v14l4-4h14a2 2 0 0 0 2-2V4a2 2 0 0 0-2-2z"/></svg>Add Note</div></div>
    <div class="rg-label">Comments</div>
  </div>
</div>
)HTML";

ss << LR"HTML(
<!-- EDIT PDF ribbon -->
<div class="ribbon-panel" id="r-edit">
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn-sm" onclick="setTool('textbox')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M2 4v3h5v12h3V7h5V4H2zm19 5h-9v3h3v7h3v-7h3V9z"/></svg>Add Text</div>
    </div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('redact')"><svg viewBox="0 0 24 24" width="13" height="13"><rect x="3" y="8" width="18" height="8" rx="1"/></svg>Redact</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="setTool('crop')"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M7 17H3v-2h4V7h8v4h2V5h2v4h4v2h-4v8h-2v-4H7v2z"/></svg>Crop</div></div>
    <div class="rg-label">Edit Content</div>
  </div>
  <div class="ribbon-group">
    <div class="rg-row"><div class="rbtn-sm" onclick="modalWatermark()">&#10070; Watermark</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalHeaderFooter()">&#9776; Header/Footer</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalBatesNumber()">&#9839; Bates Numbers</div></div>
    <div class="rg-label">Overlays</div>
  </div>
</div>
)HTML";

ss << LR"HTML(
<!-- PAGES ribbon -->
<div class="ribbon-panel" id="r-pages">
  <div class="ribbon-group">
    <div class="rg-row">
      <div class="rbtn-sm" onclick="rotatePDFAll()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M12 5V1L7 6l5 5V7a7 7 0 1 1-7 7h-2a9 9 0 1 0 9-9z"/></svg>Rotate All</div>
    </div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalDeletePages()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M6 19a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>Delete Pages</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalInsertBlank()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8l-6-6zm1 11h-4v4H9v-4H5v-2h4V7h2v4h4v2z"/></svg>Insert Blank</div></div>
    <div class="rg-label">Organize</div>
  </div>
  <div class="ribbon-group">
    <div class="rg-row"><div class="rbtn-sm" onclick="actionMergePDFs()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M16 2H8a2 2 0 0 0-2 2v2H4v2h2v10a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2V8h2V6h-2V4a2 2 0 0 0-2-2zm-4 15l-4-4h3V9h2v4h3l-4 4z"/></svg>Combine</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalSplit()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M14 4l2.29 2.29-2.88 2.88 1.42 1.42 2.88-2.88L20 10V4zm-4 0H4v6l2.29-2.29 4.71 4.7V20h2v-8.41l-5.29-5.3z"/></svg>Split</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="modalExtract()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8l-6-6zm-1 13v4H9v-4H6l6-6 6 6h-3z"/></svg>Extract</div></div>
    <div class="rg-label">PDF Operations</div>
  </div>
</div>
)HTML";

ss << LR"HTML(
<!-- PROTECT ribbon -->
<div class="ribbon-panel" id="r-protect">
  <div class="ribbon-group">
    <div class="rg-row"><div class="rbtn-sm" onclick="modalPassword()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M18 8h-1V6a5 5 0 0 0-10 0v2H6a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V10a2 2 0 0 0-2-2zm-6 9a2 2 0 1 1 0-4 2 2 0 0 1 0 4zm3.1-9H8.9V6a3.1 3.1 0 0 1 6.2 0v2z"/></svg>Password Encrypt</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="applyRedactions()"><svg viewBox="0 0 24 24" width="13" height="13"><rect x="3" y="8" width="18" height="8" rx="1"/></svg>Apply Redactions</div></div>
    <div class="rg-label">Security</div>
  </div>
</div>
<!-- EXPORT ribbon -->
<div class="ribbon-panel" id="r-export">
  <div class="ribbon-group">
    <div class="rg-row"><div class="rbtn-sm" onclick="actionPDFtoImages()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M21 19V5H3v14h18zm-9-7l3 4H9l2-3 1 1.5L12 12zm-4.5-2.5a1.5 1.5 0 1 0 3 0 1.5 1.5 0 0 0-3 0z"/></svg>Export as Images</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="actionPDFtoText()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8l-6-6zm-1 9H7v-1h6v1zm2 4H7v-1h8v1zm0-8V3.5L18.5 7H15z"/></svg>Export as Text</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="actionPerformOCR()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M9 2H5a1 1 0 0 0-1 1v4h2V4h3V2zm6 0v2h3v3h2V3a1 1 0 0 0-1-1h-4zM4 15H2v4a2 2 0 0 0 2 2h4v-2H4v-4zm16 0v4h-4v2h4a2 2 0 0 0 2-2v-4h-2z"/></svg>OCR Extract</div></div>
    <div class="rg-row"><div class="rbtn-sm" onclick="actionCompressPDF()"><svg viewBox="0 0 24 24" width="13" height="13"><path d="M12 2L4.5 20.29l.71.71L12 18l6.79 3 .71-.71z"/></svg>Compress PDF</div></div>
    <div class="rg-label">Export &amp; Convert</div>
  </div>
</div>
</div><!-- end ribbon-wrap -->
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 15 · Workspace + panels HTML
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- ── WORKSPACE ── -->
<div class="workspace">
  <!-- Left Panel -->
  <div class="left-panel" id="left-panel">
    <div class="lp-tabbar">
      <div class="lp-tab active" id="lpt-thumb" onclick="switchLPanel('thumb',this)">Thumbs</div>
      <div class="lp-tab" id="lpt-bm" onclick="switchLPanel('bm',this)">Bookmarks</div>
      <div class="lp-tab" id="lpt-layers" onclick="switchLPanel('layers',this)">Layers</div>
    </div>
    <div class="lp-body">
      <div id="lp-thumb" class="thumb-list"></div>
      <div id="lp-bm" style="display:none;"></div>
      <div id="lp-layers" style="display:none;padding:8px;font-size:10.5px;color:var(--c-muted);">Layer control coming soon.</div>
    </div>
  </div>
  <!-- PDF Viewer -->
  <div class="pdf-viewer-area" id="viewer-area">
    <div class="pdf-container" id="pdf-container">
      <div id="empty-hint" style="margin-top:0;text-align:center;color:#777;pointer-events:auto;width:100%;padding:30px 20px;">
        <div style="font-size:52px;opacity:.25;margin-bottom:8px;">&#128196;</div>
        <p style="font-size:16px;font-weight:700;color:#444;margin-bottom:4px;">PDF Pro — Acrobat Edition</p>
        <p style="font-size:11.5px;opacity:.65;margin-bottom:20px;">File ▸ Open, drag &amp; drop, or click a recent file below</p>
        <div style="display:flex;gap:10px;justify-content:center;margin-bottom:24px;">
          <button onclick="openFileDialog(false)" style="padding:8px 20px;background:var(--c-accent);color:#fff;border:none;border-radius:4px;font-size:12px;font-weight:700;cursor:pointer;">&#128196; Open PDF</button>
          <button onclick="openFileDialog(true)" style="padding:8px 20px;background:var(--c-accent2);color:#fff;border:none;border-radius:4px;font-size:12px;font-weight:700;cursor:pointer;">&#128214; Open Multiple</button>
        </div>
        <div id="recent-files-section" style="max-width:560px;margin:0 auto;text-align:left;">
          <p style="font-size:10px;font-weight:700;color:#999;text-transform:uppercase;letter-spacing:1px;margin-bottom:8px;padding-left:4px;">&#128337; Recent Files</p>
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
)HTML";

ss << LR"HTML(
  <!-- Right Properties Panel -->
  <div class="right-panel" id="right-panel">
    <!-- Document Info -->
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#8505; Document <span class="toggle">&#9660;</span></div>
      <div class="rp-body" id="rp-docinfo">
        <div class="rp-row"><span class="rp-label">File</span><span id="rp-filename" style="font-size:10.5px;word-break:break-all;">—</span></div>
        <div class="rp-row"><span class="rp-label">Pages</span><span id="rp-pages">—</span></div>
        <div class="rp-row"><span class="rp-label">Page</span><span id="rp-curpage">—</span></div>
        <div class="rp-row"><span class="rp-label">Zoom</span><span id="rp-zoom">100%</span></div>
        <div class="rp-row"><span class="rp-label">Rotate</span><span id="rp-rotate">0°</span></div>
      </div>
    </div>
    <!-- Annotation Style -->
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#9998; Annotation Style <span class="toggle">&#9660;</span></div>
      <div class="rp-body">
        <div class="rp-row">
          <span class="rp-label">Color</span>
          <div class="color-swatch" id="rp-color" style="background:#e53935;width:24px;height:24px;" onclick="showColorPicker('stroke','rp-color')"></div>
        </div>
        <div class="rp-row">
          <span class="rp-label">Width</span>
          <input class="rp-input" type="range" min="1" max="30" value="2" oninput="g_lineWidth=+this.value;document.getElementById('rb-linewidth').value=this.value">
        </div>
        <div class="rp-row">
          <span class="rp-label">Opacity</span>
          <input class="rp-input" type="range" min="5" max="100" value="100" oninput="g_opacity=+this.value/100;document.getElementById('rb-opacity').value=this.value">
        </div>
        <div class="rp-row">
          <span class="rp-label">Font</span>
          <select class="rp-input" id="rp-font" onchange="g_fontFamily=this.value">
            <option>Arial</option><option>Times New Roman</option>
            <option>Courier New</option><option>Georgia</option><option>Verdana</option>
          </select>
        </div>
        <div class="rp-row">
          <span class="rp-label">Sz</span>
          <input class="rp-input" type="number" id="rp-fontsize" value="12" min="6" max="96" onchange="g_fontSize=+this.value">
        </div>
      </div>
    </div>
)HTML";

ss << LR"HTML(
    <!-- Quick Actions -->
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#9889; Quick Actions <span class="toggle">&#9660;</span></div>
      <div class="rp-body" style="gap:5px;">
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
    <!-- Stamp chooser -->
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
          <option value="COPY">COPY</option>
          <option value="RECEIVED">RECEIVED</option>
        </select>
      </div>
    </div>
    <!-- Word count -->
    <div class="rp-section">
      <div class="rp-header" onclick="toggleRPSection(this)">&#9679; Stats <span class="toggle">&#9660;</span></div>
      <div class="rp-body" id="rp-stats">
        <div class="rp-row"><span class="rp-label">Words</span><span id="stat-words" class="badge">0</span></div>
        <div class="rp-row"><span class="rp-label">Chars</span><span id="stat-chars" class="badge">0</span></div>
        <div class="rp-row"><span class="rp-label">Annots</span><span id="stat-annots" class="badge">0</span></div>
        <button class="rp-btn" onclick="refreshStats()" style="margin-top:4px;">Refresh Stats</button>
      </div>
    </div>
  </div>
</div><!-- end workspace -->
)HTML";

ss << LR"HTML(
<!-- Status bar -->
<div class="statusbar">
  <span class="sb-item" id="sb-tool">Tool: Hand</span>
  <div class="sb-sep"></div>
  <span class="sb-item" id="sb-page">Page 0 of 0</span>
  <div class="sb-sep"></div>
  <div class="sb-zoom-row">
    <button class="sb-zoom-btn" onclick="zoomBy(-0.1)">−</button>
    <span id="sb-zoom-val">100%</span>
    <button class="sb-zoom-btn" onclick="zoomBy(0.1)">+</button>
  </div>
  <div class="sb-sep"></div>
  <span class="sb-item" id="sb-coords">0, 0</span>
  <div class="sb-right">
    <span class="sb-item" id="sb-mode">Normal</span>
    <div class="sb-sep"></div>
    <span class="sb-item" style="cursor:pointer;" onclick="enterReadMode()" title="Read Mode (ESC to exit)">&#9634;</span>
    <span class="sb-item" style="cursor:pointer;" onclick="enterPresentation()">&#9654;</span>
  </div>
</div>
)HTML";

// ─────────────────────────────────────────────────────────────
// PART 16 · Colour picker popup, context menu, modals HTML
// ─────────────────────────────────────────────────────────────
ss << LR"HTML(
<!-- Colour picker popup -->
<div class="color-picker-popup" id="color-picker-popup">
  <div class="color-grid" id="color-grid"></div>
  <div style="margin-top:6px;display:flex;align-items:center;gap:6px;">
    <label style="font-size:10px;">Custom:</label>
    <input type="color" id="color-custom" style="width:36px;height:20px;border:none;cursor:pointer;" onchange="applyCustomColor(this.value)">
  </div>
</div>
<!-- Context menu -->
<div class="ctx-menu" id="ctx-menu">
  <div class="ctx-item" onclick="ctxCopy()">&#128203; Copy Text</div>
  <div class="ctx-item" onclick="setTool('pen');closeCtx()">&#9998; Draw Here</div>
  <div class="ctx-item" onclick="setTool('note');closeCtx()">&#128204; Add Note</div>
  <div class="ctx-item" onclick="addBookmarkAtCtx()">&#9733; Add Bookmark</div>
  <div class="ctx-sep"></div>
  <div class="ctx-item" onclick="rotatePDFAll();closeCtx()">&#8635; Rotate Page</div>
  <div class="ctx-item danger" onclick="ctxDeletePage()">&#128465; Delete This Page</div>
</div>
)HTML";

ss << LR"HTML(
<!-- Signature modal -->
<div class="modal-overlay" id="sig-modal-overlay">
  <div class="modal">
    <h3>&#9998; Draw Your Signature</h3>
    <canvas id="sig-modal-canvas" width="420" height="160" style="border:1px solid var(--c-border);border-radius:3px;cursor:crosshair;background:#fff;touch-action:none;"></canvas>
    <div style="display:flex;gap:6px;margin-top:8px;">
      <button class="btn btn-secondary" onclick="clearSigPad()">Clear</button>
      <select id="sig-color" style="padding:4px;border:1px solid var(--c-border);border-radius:3px;" onchange="g_sigColor=this.value">
        <option value="#1a1a1a">Black</option>
        <option value="#1a3a8f">Blue</option>
        <option value="#8b0000">Dark Red</option>
      </select>
    </div>
    <div class="modal-actions">
      <button class="btn btn-secondary" onclick="closeSigModal()">Cancel</button>
      <button class="btn btn-primary" onclick="applySigToPage()">Insert Signature</button>
    </div>
  </div>
</div>
<!-- General modal -->
<div class="modal-overlay" id="modal-overlay">
  <div class="modal" id="modal-body"></div>
</div>
<!-- Loading -->
<div class="loading-overlay" id="loading-overlay">
  <div class="spinner"></div>
  <div id="loading-txt">Processing…</div>
  <div class="progress-wrap" style="width:200px;margin-top:10px;">
    <div class="progress-bar" id="loading-progress" style="width:0%;"></div>
  </div>
</div>
<!-- Toast box -->
<div class="toast-box" id="toast-box"></div>
<!-- Hidden file inputs -->
<input type="file" id="fileInput" accept=".pdf" style="display:none" multiple onchange="handleFiles(event)">
<input type="file" id="mergeInput" accept=".pdf" style="display:none" multiple onchange="doMerge(event)">
</div><!-- end #app -->
)HTML";

// ─────────────────────────────────────────────────────────────
