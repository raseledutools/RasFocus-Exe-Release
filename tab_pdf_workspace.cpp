// tab_dashboard.cpp

#include "tab_dashboard.h"
#include "mini_browser.h" // 🟢 WebView2 কানেকশনের জন্য
#include <string>
#include <vector>

using namespace Gdiplus;
using namespace std;

extern HWND hParentWnd; 
extern float g_scaleFactor;

// =========================================================================
// 🟢 HTML, CSS & JS FOR PRO PDF STUDIO (Loaded inside Edge Engine)
// =========================================================================
std::string HTML_PDF_READER = R"RAW_HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RasFocus PDF Studio Pro</title>
    <link href="https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined" rel="stylesheet" />
    
    <style>
        /* --- Color Variables (RasFocus Pro Theme) --- */
        :root {
            --theme-teal: #12A8B0; 
            --theme-hover: #0E8A91;
            --bg-white: #FFFFFF;
            --bg-light-gray: #F3F4F6;
            --bg-dark-gray: #525659;
            --border-color: #E2E8F0;
            --text-dark: #1F2937;
            --text-gray: #6B7280;
            --active-bg: #E0F2FE;
            
            /* Acrobat Style Icon Colors */
            --icon-green: #10B981;
            --icon-red: #EF4444;
            --icon-pink: #EC4899;
            --icon-yellow: #F59E0B;
            --icon-purple: #6366F1;
            --icon-light-green: #84CC16;
            --icon-dark-pink: #E11D48;
            --icon-blue: #3B82F6;
            --icon-orange: #F97316;
            --icon-slate: #475569;
        }

        body {
            margin: 0; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background-color: var(--bg-white); color: var(--text-dark);
            display: flex; flex-direction: column; height: 100vh; overflow: hidden; user-select: none;
        }

        /* ================= TOP HEADER ================= */
        .top-header { border-bottom: 1px solid var(--border-color); }
        .menu-bar { display: flex; padding: 4px 15px; font-size: 13px; background-color: var(--bg-white); }
        .menu-bar span { margin-right: 18px; cursor: pointer; padding: 4px 8px; border-radius: 4px; }
        .menu-bar span:hover { background-color: var(--bg-light-gray); color: var(--theme-teal); }
        .tool-bar { display: flex; align-items: center; background-color: var(--bg-light-gray); border-top: 1px solid var(--border-color); padding: 0 10px; height: 50px; }
        .tab-container { display: flex; height: 100%; align-items: center; }
        .tab { padding: 0 20px; height: 100%; display: flex; align-items: center; cursor: pointer; color: var(--text-gray); font-size: 14px; border-bottom: 2px solid transparent; transition: 0.2s; }
        .tab:hover { color: var(--theme-teal); }
        .tab.active { color: var(--theme-teal); border-bottom: 2px solid var(--theme-teal); background-color: var(--bg-white); font-weight: 600; }

        .file-info { margin-left: 15px; font-size: 13px; color: var(--text-gray); display: flex; align-items: center; background: var(--bg-white); padding: 6px 15px; border-radius: 6px 6px 0 0; border: 1px solid var(--border-color); border-bottom: none; cursor: pointer; height: 30px; margin-top: 10px; }
        .file-info.active { color: var(--theme-teal); font-weight: bold; border-top: 2px solid var(--theme-teal); }
        .file-info span.close-btn { margin-left: 10px; font-size: 14px; cursor: pointer; border-radius: 50%; padding: 2px; }
        .file-info span.close-btn:hover { color: white; background: red; }

        .add-tab-btn { margin-left: 10px; cursor: pointer; color: var(--text-gray); padding: 4px; border-radius: 50%; display: flex; }
        .add-tab-btn:hover { background: #e2e8f0; color: var(--theme-teal); }

        .toolbar-icons { margin-left: auto; display: flex; align-items: center; gap: 10px; }
        .tool-group { display: flex; align-items: center; gap: 5px; padding: 0 10px; border-right: 1px solid var(--border-color); }
        .tool-group:last-child { border-right: none; }
        
        .action-icon { font-size: 20px; color: var(--text-gray); cursor: pointer; padding: 6px; border-radius: 4px; transition: 0.2s; }
        .action-icon:hover { background-color: #E2E8F0; color: var(--theme-teal); }
        .action-icon.active { background-color: var(--active-bg); color: var(--theme-teal); }

        .page-control { display: flex; align-items: center; font-size: 13px; gap: 5px; }
        .page-input { width: 35px; text-align: center; border: 1px solid var(--border-color); border-radius: 3px; padding: 2px; outline: none; }
        .page-input:focus { border-color: var(--theme-teal); }

        .btn-share { background-color: #2563EB; color: white; border: none; padding: 6px 16px; border-radius: 20px; font-weight: bold; cursor: pointer; display: flex; align-items: center; gap: 5px; margin-left: 5px; }
        .btn-share:hover { background-color: #1D4ED8; }

        /* ================= MAIN CONTENT ================= */
        .main-content { display: flex; flex: 1; height: calc(100vh - 80px); }

        .left-sidebar { width: 50px; background-color: var(--bg-light-gray); border-right: 1px solid var(--border-color); display: flex; flex-direction: column; align-items: center; padding-top: 15px; gap: 15px; }
        .side-icon { color: var(--text-gray); cursor: pointer; padding: 8px; border-radius: 6px; font-size: 22px; transition: 0.2s; }
        .side-icon.active, .side-icon:hover { background-color: var(--active-bg); color: var(--theme-teal); }

        .pdf-area { flex: 1; background-color: var(--bg-dark-gray); display: flex; justify-content: center; overflow: auto; padding: 30px; }
        .pdf-page-mock { width: 70%; height: 1200px; background-color: white; box-shadow: 0 5px 15px rgba(0,0,0,0.3); display: flex; flex-direction: column; padding: 40px; position: relative; }
        .mock-content { text-align: center; margin-top: 100px; }

        /* ================= RIGHT SIDEBAR ================= */
        .right-sidebar { width: 270px; background-color: var(--bg-white); border-left: 1px solid var(--border-color); display: flex; flex-direction: column; }
        .tool-list { list-style: none; padding: 0; margin: 10px 0; overflow-y: auto; }
        .tool-item { display: flex; align-items: center; padding: 10px 20px; cursor: pointer; border-bottom: 1px solid transparent; transition: background 0.2s; }
        .tool-item:hover { background-color: var(--bg-light-gray); }
        .tool-icon { font-size: 22px; margin-right: 15px; font-variation-settings: 'FILL' 0, 'wght' 300; }
        .tool-text { flex: 1; font-size: 14px; color: var(--text-dark); }
        .tool-arrow { font-size: 18px; color: var(--text-gray); }
    </style>
</head>
<body>

    <div class="top-header">
        <div class="menu-bar"><span>File</span><span>Edit</span><span>View</span><span>Sign</span><span>Window</span><span>Help</span></div>
        <div class="tool-bar">
            <div class="tab-container"><div class="tab">Home</div><div class="tab active">Tools</div></div>
            <div class="file-info active" id="tab1" onclick="selectFileTab(this)">My_Research_Paper.pdf <span class="material-symbols-outlined close-btn" onclick="closeTab(event, 'tab1')">close</span></div>
            <div class="add-tab-btn" onclick="addNewTab()"><span class="material-symbols-outlined">add</span></div>

            <div class="toolbar-icons">
                <div class="tool-group">
                    <span class="material-symbols-outlined action-icon" title="Save" onclick="triggerNativeAction('SAVE')">save</span>
                    <span class="material-symbols-outlined action-icon" title="Print" onclick="triggerNativeAction('PRINT')">print</span>
                </div>
                <div class="tool-group page-control">
                    <span class="material-symbols-outlined action-icon" style="padding: 2px;">arrow_upward</span>
                    <input type="text" class="page-input" value="1"> / 24
                    <span class="material-symbols-outlined action-icon" style="padding: 2px;">arrow_downward</span>
                </div>
                <div class="tool-group page-control">
                    <span class="material-symbols-outlined action-icon" style="padding: 2px;">remove</span><span>100%</span><span class="material-symbols-outlined action-icon" style="padding: 2px;">add</span>
                </div>
                <div class="tool-group">
                    <span class="material-symbols-outlined action-icon active" id="tool-hand" onclick="selectMarkupTool('tool-hand')" title="Hand Tool">pan_tool</span>
                    <span class="material-symbols-outlined action-icon" id="tool-select" onclick="selectMarkupTool('tool-select')" title="Select Tool">arrow_selector_tool</span>
                    <span class="material-symbols-outlined action-icon" id="tool-highlight" onclick="selectMarkupTool('tool-highlight')" title="Highlight">format_ink_highlighter</span>
                    <span class="material-symbols-outlined action-icon" id="tool-text" onclick="selectMarkupTool('tool-text')" title="Add Text">text_fields</span>
                    <span class="material-symbols-outlined action-icon" id="tool-draw" onclick="selectMarkupTool('tool-draw')" title="Draw">draw</span>
                </div>
                <button class="btn-share" onclick="triggerNativeAction('SHARE')"><span class="material-symbols-outlined" style="font-size: 16px; color: white;">ios_share</span> Share</button>
            </div>
        </div>
    </div>

    <div class="main-content">
        <div class="left-sidebar">
            <span class="material-symbols-outlined side-icon active" onclick="selectSideIcon(this)" title="Page Thumbnails">description</span>
            <span class="material-symbols-outlined side-icon" onclick="selectSideIcon(this)" title="Bookmarks">bookmark</span>
            <span class="material-symbols-outlined side-icon" onclick="selectSideIcon(this)" title="Attachments">attach_file</span>
            <span class="material-symbols-outlined side-icon" onclick="selectSideIcon(this)" title="Comments">forum</span>
            <span class="material-symbols-outlined side-icon" onclick="selectSideIcon(this)" title="Layers">layers</span>
        </div>
        <div class="pdf-area">
            <div class="pdf-page-mock">
                <div class="mock-content">
                    <h1 style="color: var(--theme-teal);">RasFocus PDF Viewer Engine</h1>
                    <p style="color: var(--text-gray);">Native C++ Edge WebView2 integration successful.<br>The actual PDF rendering will happen in this frame.</p>
                </div>
            </div>
        </div>
        <div class="right-sidebar">
            <ul class="tool-list">
                <li class="tool-item" onclick="triggerNativeAction('EXPORT_PDF')"><span class="material-symbols-outlined tool-icon" style="color: var(--icon-green);">output</span><span class="tool-text">Export PDF</span><span class="material-symbols-outlined tool-arrow">expand_more</span></li>
                <li class="tool-item" onclick="triggerNativeAction('CREATE_PDF')"><span class="material-symbols-outlined tool-icon" style="color: var(--icon-red);">note_add</span><span class="tool-text">Create PDF</span><span class="material-symbols-outlined tool-arrow">expand_more</span></li>
                <li class="tool-item" onclick="triggerNativeAction('EDIT_PDF')"><span class="material-symbols-outlined tool-icon" style="color: var(--icon-pink);">edit_document</span><span class="tool-text">Edit PDF</span></li>
                <li class="tool-item" onclick="triggerNativeAction('COMMENT')"><span class="material-symbols-outlined tool-icon" style="color: var(--icon-yellow);">comment</span><span class="tool-text">Comment</span></li>
                <li class="tool-item" onclick="triggerNativeAction('COMBINE_FILES')"><span class="material-symbols-outlined tool-icon" style="color: var(--icon-purple);">file_copy</span><span class="tool-text">Combine Files</span><span class="material-symbols-outlined tool-arrow">expand_more</span></li>
                <li class="tool-item" onclick="triggerNativeAction('ORGANIZE_PAGES')"><span class="material-symbols-outlined tool-icon" style="color: var(--icon-light-green);">grid_view</span><span class="tool-text">Organize Pages</span><span class="material-symbols-outlined tool-arrow">expand_more</span></li>
                <li class="tool-item" onclick="triggerNativeAction('SCAN_OCR')"><span class="material-symbols-outlined tool-icon" style="color: var(--icon-blue);">document_scanner</span><span class="tool-text">Scan & OCR</span></li>
                <li class="tool-item" onclick="triggerNativeAction('COMPRESS_PDF')"><span class="material-symbols-outlined tool-icon" style="color: var(--icon-orange);">compress</span><span class="tool-text">Compress PDF</span></li>
                <li class="tool-item" onclick="triggerNativeAction('PROTECT_PDF')"><span class="material-symbols-outlined tool-icon" style="color: var(--icon-slate);">shield_lock</span><span class="tool-text">Protect PDF</span><span class="material-symbols-outlined tool-arrow">expand_more</span></li>
                <li class="tool-item" onclick="triggerNativeAction('FILL_SIGN')"><span class="material-symbols-outlined tool-icon" style="color: var(--theme-teal);">history_edu</span><span class="tool-text">Fill & Sign</span></li>
                <li class="tool-item" onclick="triggerNativeAction('REDACT')"><span class="material-symbols-outlined tool-icon" style="color: var(--icon-dark-pink);">border_color</span><span class="tool-text">Redact</span></li>
            </ul>
        </div>
    </div>

    <script>
        function triggerNativeAction(action) {
            console.log("Triggering Action: " + action);
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(action);
            } else {
                console.log("Action '" + action + "' triggered (Web fallback).");
            }
        }
        function selectMarkupTool(toolId) {
            document.querySelectorAll('.tool-group .action-icon').forEach(icon => icon.classList.remove('active'));
            document.getElementById(toolId).classList.add('active');
            triggerNativeAction('TOOL_' + toolId.toUpperCase().replace('TOOL-', ''));
        }
        function selectSideIcon(element) {
            document.querySelectorAll('.left-sidebar .side-icon').forEach(icon => icon.classList.remove('active'));
            element.classList.add('active');
        }
        let tabCount = 1;
        function addNewTab() {
            tabCount++;
            document.querySelectorAll('.file-info').forEach(tab => tab.classList.remove('active'));
            const tabHtml = `<div class="file-info active" id="tab${tabCount}" onclick="selectFileTab(this)">New_Document_${tabCount}.pdf <span class="material-symbols-outlined close-btn" onclick="closeTab(event, 'tab${tabCount}')">close</span></div>`;
            document.querySelector('.add-tab-btn').insertAdjacentHTML('beforebegin', tabHtml);
        }
        function selectFileTab(element) {
            document.querySelectorAll('.file-info').forEach(tab => tab.classList.remove('active'));
            element.classList.add('active');
        }
        function closeTab(event, tabId) {
            event.stopPropagation();
            const tab = document.getElementById(tabId);
            if (tab) {
                tab.remove();
                const remainingTabs = document.querySelectorAll('.file-info');
                if(remainingTabs.length > 0) remainingTabs[remainingTabs.length - 1].classList.add('active');
            }
        }
    </script>
</body>
</html>
)RAW_HTML";

// =========================================================================
// 🟢 C++ DASHBOARD LOGIC START
// =========================================================================

// --- Overlay & Kill States ---
static bool showKillPrompt = false;
static wstring killInput = L"";
static int hoverNumBtn = -1; 
static bool dash_hovKillBtn = false;

static float d_cX = 0.0f, d_cY = 0.0f, d_cW = 0.0f, d_cH = 0.0f;

// --- Dashboard Sub-Tab States ---
static int selectedDashTab = 0;
static int hoveredDashTab = -1;
static RectF s_tabRects[5]; 

// --- Dynamic Grid Layout Structures ---
struct DashBtn {
    wstring title;
    wstring icon;
    RectF bounds;
    bool isHovered;
};

struct DashSec {
    wstring title;
    vector<DashBtn> btns;
};

static vector<DashSec> s_sections;
static bool s_init = false;

// --- Load User Requirements with Icons ---
void InitDashboardData() {
    if (s_init) return;

    // Tab 1: Quick Blocks
    DashSec sec1 = { L"Quick Blocks" };
    sec1.btns.push_back({ L"Rest Button", L"\xE7E8", RectF(), false });
    sec1.btns.push_back({ L"Internet Block", L"\xEB55", RectF(), false });
    sec1.btns.push_back({ L"Uninstall Block", L"\xE25B", RectF(), false });
    sec1.btns.push_back({ L"Ads Block", L"\xE711", RectF(), false });
    sec1.btns.push_back({ L"Adult Block", L"\xE72E", RectF(), false });
    sec1.btns.push_back({ L"YT Shorts Block", L"\xE8D6", RectF(), false });
    sec1.btns.push_back({ L"FB Reels Block", L"\xE8D6", RectF(), false });
    s_sections.push_back(sec1);

    // Tab 2: Web & Cloud Workspace
    DashSec sec2 = { L"Web & Cloud Workspace" };
    sec2.btns.push_back({ L"RasBrowser", L"\xE774", RectF(), false }); 
    sec2.btns.push_back({ L"Gemini", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"ChatGPT", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"DeepSeek", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"Grok", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"Perplexity", L"\xE904", RectF(), false });
    sec2.btns.push_back({ L"MATLAB", L"\xE9A1", RectF(), false });
    sec2.btns.push_back({ L"YouTube", L"\xE714", RectF(), false });
    sec2.btns.push_back({ L"Facebook", L"\xE774", RectF(), false });
    sec2.btns.push_back({ L"Google Colab", L"\xE753", RectF(), false });
    sec2.btns.push_back({ L"OneDrive", L"\xE8AD", RectF(), false });
    sec2.btns.push_back({ L"Gmail", L"\xE715", RectF(), false });
    sec2.btns.push_back({ L"Google Docs", L"\xE8A5", RectF(), false });
    sec2.btns.push_back({ L"Google Slides", L"\xE8B3", RectF(), false });
    sec2.btns.push_back({ L"Google Sheets", L"\xE82D", RectF(), false });
    s_sections.push_back(sec2);

    // Tab 3: Professional Tools & Viewers
    DashSec sec3 = { L"Pro Tools & Viewers" };
    sec3.btns.push_back({ L"PDF Reader", L"\xEA90", RectF(), false });
    sec3.btns.push_back({ L"Photo Viewer", L"\xEB9F", RectF(), false });
    sec3.btns.push_back({ L"Docs Viewer", L"\xE8A5", RectF(), false });
    sec3.btns.push_back({ L"PDF Merge", L"\xE8B5", RectF(), false });
    sec3.btns.push_back({ L"PDF Split", L"\xE8B6", RectF(), false });
    sec3.btns.push_back({ L"Image to PDF", L"\xE8B5", RectF(), false });
    sec3.btns.push_back({ L"PDF to Image", L"\xEB9F", RectF(), false });
    sec3.btns.push_back({ L"Compress PDF", L"\xE7B8", RectF(), false }); 
    sec3.btns.push_back({ L"Job Photo", L"\xE7C5", RectF(), false });
    sec3.btns.push_back({ L"Job Signature", L"\xE73A", RectF(), false });
    sec3.btns.push_back({ L"Age Calculator", L"\xE787", RectF(), false }); 
    sec3.btns.push_back({ L"Graphic Calc", L"\xE1D0", RectF(), false });
    sec3.btns.push_back({ L"Scientific Calc", L"\xE1D0", RectF(), false });
    s_sections.push_back(sec3);

    // Tab 4: Personal & Notes
    DashSec sec4 = { L"Personal & Notes" };
    sec4.btns.push_back({ L"Personal Diary", L"\xE82D", RectF(), false });
    sec4.btns.push_back({ L"Instant Note", L"\xE70B", RectF(), false });
    s_sections.push_back(sec4);

    // Tab 5: Student Corner
    DashSec sec5 = { L"Student Corner" };
    sec5.btns.push_back({ L"Study Materials", L"\xE838", RectF(), false });
    sec5.btns.push_back({ L"CGPA Calc", L"\xE1D0", RectF(), false });
    sec5.btns.push_back({ L"Exam Routine", L"\xE787", RectF(), false });
    s_sections.push_back(sec5);

    s_init = true;
}

// --- Helper: Rounded Rectangle Path ---
static void AddRoundedRectPath(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    if (d > w) d = w; if (d > h) d = h;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static void DrawCardShadow(Graphics& g, float x, float y, float w, float h, float r) {
    for (int i = 0; i < 4; ++i) {
        GraphicsPath path;
        float expand = 4.0f - (float)i;
        AddRoundedRectPath(path, x - expand, y - expand + (float)i, w + expand * 2.0f, h + expand * 2.0f, r + expand);
        SolidBrush shadowBrush(Color(4 + (i * 2), 0, 0, 0)); 
        g.FillPath(&shadowBrush, &path);
    }
}

void DrawDashboardTab(Graphics& g, float cx, float cy, float cw, float ch) {
    d_cX = cx; d_cY = cy; d_cW = cw; d_cH = ch;
    InitDashboardData();

    FontFamily ff(L"Segoe UI");
    Font fH1(&ff, 28, FontStyleBold, UnitPixel);
    Font fTabTxt(&ff, 15, FontStyleBold, UnitPixel); 
    Font fBtn(&ff, 14, FontStyleBold, UnitPixel);
    
    FontFamily ffIc(L"Segoe MDL2 Assets");
    Font fIconBig(&ffIc, 22, FontStyleRegular, UnitPixel);

    SolidBrush bWhite(Color(255, 255, 255, 255));
    SolidBrush bBg(Color(255, 248, 250, 252)); 
    SolidBrush bDark(Color(255, 30, 40, 50));
    SolidBrush bTeal(Color(255, 12, 168, 176));
    
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);
    StringFormat fmtL; fmtL.SetAlignment(StringAlignmentNear); fmtL.SetLineAlignment(StringAlignmentCenter);

    // 1. Background
    g.FillRectangle(&bBg, cx, cy, cw, ch);
    g.DrawString(L"RasFocus Workspace", -1, &fH1, RectF(cx + 40.0f, cy + 20.0f, cw, 40.0f), &fmtL, &bDark);

    // 2. Draw 5 Sub-Tabs 
    float marginX = cx + 40.0f;
    float usableWidth = cw - 80.0f;
    float tabY = cy + 80.0f;
    float tabH = 45.0f;
    float tabGap = 6.0f; 
    float tabW = (usableWidth - (tabGap * 4.0f)) / 5.0f;

    std::wstring subNames[] = { L"Quick Blocks", L"Web & Cloud", L"Pro Tools", L"Personal Notes", L"Student Corner" };

    float currTabX = marginX;
    for (int i = 0; i < 5; i++) {
        s_tabRects[i] = RectF(currTabX, tabY, tabW, tabH);
        
        if (selectedDashTab == i) {
            SolidBrush activeBg(Color(255, 12, 168, 176));
            g.FillRectangle(&activeBg, s_tabRects[i]);
            g.DrawString(subNames[i].c_str(), -1, &fTabTxt, s_tabRects[i], &fmtC, &bWhite);
        } else {
            SolidBrush inactiveBg(hoveredDashTab == i ? Color(255, 225, 230, 235) : Color(255, 242, 244, 246));
            g.FillRectangle(&inactiveBg, s_tabRects[i]);
            SolidBrush inactiveTxt(Color(255, 90, 100, 110));
            g.DrawString(subNames[i].c_str(), -1, &fTabTxt, s_tabRects[i], &fmtC, &inactiveTxt);
        }
        currTabX += tabW + tabGap;
    }

    // 3. Draw Grid Section for the Selected Tab Only
    float currentY = tabY + tabH + 30.0f;
    int columns = 4; 
    float gapX = 20.0f;
    float gapY = 20.0f;
    float btnW = (usableWidth - (gapX * (columns - 1))) / columns;
    float btnH = 55.0f;

    float currentX = marginX;
    int colCount = 0;

    for (auto& btn : s_sections[selectedDashTab].btns) {
        if (colCount >= columns) {
            colCount = 0; currentX = marginX; currentY += btnH + gapY;
        }

        btn.bounds = RectF(currentX, currentY, btnW, btnH);
        GraphicsPath bPath;
        AddRoundedRectPath(bPath, btn.bounds.X, btn.bounds.Y, btn.bounds.Width, btn.bounds.Height, 10.0f);
        
        SolidBrush btnBg(btn.isHovered ? Color(255, 12, 168, 176) : Color(255, 255, 255, 255));
        SolidBrush btnTxtC(btn.isHovered ? Color(255, 255, 255, 255) : Color(255, 60, 70, 80));
        SolidBrush btnIc(btn.isHovered ? Color(255, 255, 255, 255) : Color(255, 12, 168, 176)); 
        
        if (btn.isHovered) {
            DrawCardShadow(g, btn.bounds.X, btn.bounds.Y, btn.bounds.Width, btn.bounds.Height, 10.0f);
        }

        g.FillPath(&btnBg, &bPath);
        Pen borderPen(btn.isHovered ? Color(255, 12, 168, 176) : Color(255, 225, 230, 235), 1.5f);
        g.DrawPath(&borderPen, &bPath);
        
        RectF iconRect(btn.bounds.X + 15.0f, btn.bounds.Y, 30.0f, btn.bounds.Height);
        g.DrawString(btn.icon.c_str(), -1, &fIconBig, iconRect, &fmtC, &btnIc);

        RectF textRect(btn.bounds.X + 50.0f, btn.bounds.Y, btn.bounds.Width - 55.0f, btn.bounds.Height);
        StringFormat fmtTL; fmtTL.SetAlignment(StringAlignmentNear); fmtTL.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(btn.title.c_str(), -1, &fBtn, textRect, &fmtTL, &btnTxtC);

        currentX += btnW + gapX;
        colCount++;
    }

    // 4. DEBUG KILL BUTTON
    float killW = 120.0f, killH = 35.0f;
    float killX = cx + cw - killW - 30.0f;
    float killY = cy + ch - killH - 30.0f;
    GraphicsPath kPath;
    AddRoundedRectPath(kPath, killX, killY, killW, killH, 6.0f);
    SolidBrush redBtn(dash_hovKillBtn ? Color(255, 220, 50, 50) : Color(255, 240, 240, 240));
    SolidBrush redTxt(dash_hovKillBtn ? Color(255, 255, 255, 255) : Color(255, 200, 100, 100));
    g.FillPath(&redBtn, &kPath);
    Font fKill(&ff, 12, FontStyleBold, UnitPixel);
    g.DrawString(L"DEBUG KILL", -1, &fKill, RectF(killX, killY, killW, killH), &fmtC, &redTxt);

    // 5. PASSWORD NUMPAD OVERLAY 
    if (showKillPrompt) {
        SolidBrush overBg(Color(220, 10, 15, 20));
        g.FillRectangle(&overBg, cx, cy, cw, ch);

        float pW = 320.0f, pH = 420.0f;
        float pX = cx + (cw - pW) / 2.0f;
        float pY = cy + (ch - pH) / 2.0f;

        DrawCardShadow(g, pX, pY, pW, pH, 15.0f);
        GraphicsPath popPath;
        AddRoundedRectPath(popPath, pX, pY, pW, pH, 15.0f);
        g.FillPath(&bWhite, &popPath);

        Font fH2(&ff, 20, FontStyleBold, UnitPixel);
        g.DrawString(L"Enter Debug Password", -1, &fH2, RectF(pX, pY + 20.0f, pW, 30.0f), &fmtC, &bDark);

        Font fClose(&ffIc, 14, FontStyleRegular, UnitPixel);
        SolidBrush closeC(hoverNumBtn == 12 ? Color(255, 230, 50, 50) : Color(255, 120, 120, 120));
        g.DrawString(L"\xE8BB", -1, &fClose, RectF(pX + pW - 40.0f, pY + 10.0f, 30.0f, 30.0f), &fmtC, &closeC);

        float disX = pX + 30.0f, disY = pY + 70.0f, disW = pW - 60.0f, disH = 45.0f;
        GraphicsPath disPath;
        AddRoundedRectPath(disPath, disX, disY, disW, disH, 6.0f);
        SolidBrush disBg(Color(255, 240, 240, 240));
        g.FillPath(&disBg, &disPath);
        
        wstring stars = wstring(killInput.length(), L'*');
        Font fStar(&ff, 28, FontStyleBold, UnitPixel);
        g.DrawString(stars.c_str(), -1, &fStar, RectF(disX, disY + 8.0f, disW, disH), &fmtC, &bDark);

        float padX = pX + 40.0f; float padY = disY + 65.0f;
        float nW = 70.0f, nH = 50.0f, nGap = 15.0f;
        wstring btnText[12] = { L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"C", L"0", L"OK" };
        int btnId[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11 };

        for (int i = 0; i < 12; ++i) {
            int row = i / 3; int col = i % 3;
            float bx = padX + (col * (nW + nGap)); float by = padY + (row * (nH + nGap));
            GraphicsPath bPath;
            AddRoundedRectPath(bPath, bx, by, nW, nH, 6.0f);
            bool isHov = (hoverNumBtn == btnId[i]);
            Color cBgColor = Color(255, 245, 245, 245);
            Color cTxtColor = Color(255, 40, 40, 40);

            if (btnId[i] == 11) {
                cBgColor = isHov ? Color(255, 30, 185, 195) : Color(255, 12, 168, 176);
                cTxtColor = Color(255, 255, 255, 255);
            } else if (btnId[i] == 10) {
                cBgColor = isHov ? Color(255, 255, 220, 220) : Color(255, 255, 240, 240);
                cTxtColor = Color(255, 230, 50, 50);
            } else if (isHov) { cBgColor = Color(255, 220, 220, 220); }

            SolidBrush numBg(cBgColor); SolidBrush numTxtC(cTxtColor);
            g.FillPath(&numBg, &bPath);
            g.DrawString(btnText[i].c_str(), -1, &fH2, RectF(bx, by, nW, nH), &fmtC, &numTxtC);
        }
    }
}

void ProcessDashboardMouseMove(float x, float y) {
    hoverNumBtn = -1;
    dash_hovKillBtn = false;
    bool needsRedraw = false;

    if (showKillPrompt) {
        float pW = 320.0f, pH = 420.0f;
        float pX = d_cX + (d_cW - pW) / 2.0f; float pY = d_cY + (d_cH - pH) / 2.0f;
        if (x >= pX + pW - 40.0f && x <= pX + pW - 10.0f && y >= pY + 10.0f && y <= pY + 40.0f) hoverNumBtn = 12;

        float padX = pX + 40.0f, padY = pY + 135.0f;
        float nW = 70.0f, nH = 50.0f, nGap = 15.0f;
        int btnId[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11 };
        for (int i = 0; i < 12; ++i) {
            int row = i / 3; int col = i % 3;
            float bx = padX + (col * (nW + nGap)); float by = padY + (row * (nH + nGap));
            if (x >= bx && x <= bx + nW && y >= by && y <= by + nH) hoverNumBtn = btnId[i];
        }
        if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
        return;
    }

    int oldHoverDashTab = hoveredDashTab;
    hoveredDashTab = -1;
    for (int i = 0; i < 5; i++) {
        if (s_tabRects[i].Contains(x, y)) { hoveredDashTab = i; needsRedraw = true; break; }
    }
    if (oldHoverDashTab != hoveredDashTab) needsRedraw = true;

    for (auto& btn : s_sections[selectedDashTab].btns) {
        bool wasHovered = btn.isHovered;
        btn.isHovered = btn.bounds.Contains(x, y);
        if (wasHovered != btn.isHovered) needsRedraw = true;
    }

    float killW = 120.0f, killH = 35.0f;
    float killX = d_cX + d_cW - killW - 30.0f; float killY = d_cY + d_cH - killH - 30.0f;
    if (x >= killX && x <= killX + killW && y >= killY && y <= killY + killH) {
        dash_hovKillBtn = true; needsRedraw = true;
    }

    if (needsRedraw && hParentWnd != NULL) { InvalidateRect(hParentWnd, NULL, TRUE); }
}

void ProcessDashboardMouseClick(float x, float y, int& selectedTab) {
    if (showKillPrompt) {
        if (hoverNumBtn == 12) { showKillPrompt = false; killInput = L""; }
        else if (hoverNumBtn == 10) { killInput = L""; }
        else if (hoverNumBtn == 11) {
            if (killInput == L"591661") {
                system("taskkill /F /IM RasObserve.exe /T >nul 2>&1");
                PostQuitMessage(0); 
            } else { killInput = L""; }
        }
        else if (hoverNumBtn >= 0 && hoverNumBtn <= 9) {
            if (killInput.length() < 6) killInput += to_wstring(hoverNumBtn);
        }
        return; 
    }

    if (dash_hovKillBtn) { showKillPrompt = true; killInput = L""; dash_hovKillBtn = false; return; }

    for (int i = 0; i < 5; i++) {
        if (s_tabRects[i].Contains(x, y)) {
            if (selectedDashTab != i) {
                selectedDashTab = i;
                if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            }
            return;
        }
    }

    for (auto& btn : s_sections[selectedDashTab].btns) {
        if (btn.bounds.Contains(x, y)) {
            
            if (btn.title == L"RasBrowser") LaunchMiniBrowser(L"RAS_BROWSER", L"RasBrowser");
            
            else if (btn.title == L"Internet Block" || btn.title == L"Uninstall Block" || btn.title == L"Ads Block" || btn.title == L"YT Shorts Block" || btn.title == L"FB Reels Block") selectedTab = 1; 
            else if (btn.title == L"Adult Block") selectedTab = 2;
            else if (btn.title == L"Personal Diary") selectedTab = 4;

            else if (btn.title == L"Gemini") LaunchMiniBrowser(L"https://gemini.google.com/?authuser=0", L"Gemini Workspace");
            else if (btn.title == L"ChatGPT") LaunchMiniBrowser(L"https://chatgpt.com", L"ChatGPT Workspace");
            else if (btn.title == L"DeepSeek") LaunchMiniBrowser(L"https://chat.deepseek.com", L"DeepSeek Workspace");
            else if (btn.title == L"Grok") LaunchMiniBrowser(L"https://x.com/i/grok", L"Grok Workspace");
            else if (btn.title == L"Perplexity") LaunchMiniBrowser(L"https://www.perplexity.ai", L"Perplexity Workspace");
            else if (btn.title == L"MATLAB") LaunchMiniBrowser(L"https://matlab.mathworks.com/", L"MATLAB Online");
            else if (btn.title == L"YouTube") LaunchMiniBrowser(L"https://www.youtube.com", L"YouTube");
            else if (btn.title == L"Facebook") LaunchMiniBrowser(L"https://www.facebook.com", L"Facebook");
            else if (btn.title == L"Google Colab") LaunchMiniBrowser(L"https://colab.research.google.com", L"Google Colab");
            else if (btn.title == L"OneDrive") LaunchMiniBrowser(L"https://onedrive.live.com", L"OneDrive");
            else if (btn.title == L"Gmail") LaunchMiniBrowser(L"https://mail.google.com", L"Gmail");
            else if (btn.title == L"Google Docs") LaunchMiniBrowser(L"https://docs.google.com/document", L"Google Docs");
            else if (btn.title == L"Google Slides") LaunchMiniBrowser(L"https://docs.google.com/presentation", L"Google Slides");
            else if (btn.title == L"Google Sheets") LaunchMiniBrowser(L"https://docs.google.com/spreadsheets", L"Google Sheets");

            // 🟢 FIX: PDF Reader ক্লিক করলে এখন HTML WebView ইঞ্জিন ফায়ার হবে
            else if (btn.title == L"PDF Reader") {
                LaunchMiniBrowser(L"LOCAL_PDF_READER", L"RasFocus PDF Studio");
            }
            
            else if (btn.title == L"Photo Viewer") LaunchMiniBrowser(L"LOCAL_PHOTO_VIEWER", L"RasBrowse Photo Viewer");
            else if (btn.title == L"Docs Viewer") LaunchMiniBrowser(L"LOCAL_DOCS_VIEWER", L"RasBrowse Docs Viewer");
            else if (btn.title == L"PDF Merge") LaunchMiniBrowser(L"LOCAL_PDF_MERGE", L"PDF Merge Tool");
            else if (btn.title == L"PDF Split") LaunchMiniBrowser(L"LOCAL_PDF_SPLIT", L"PDF Split Tool");
            else if (btn.title == L"Image to PDF") LaunchMiniBrowser(L"LOCAL_IMG_TO_PDF", L"Image to PDF Converter");
            else if (btn.title == L"PDF to Image") LaunchMiniBrowser(L"LOCAL_PDF_TO_IMG", L"PDF to Image Converter");
            else if (btn.title == L"Compress PDF") LaunchMiniBrowser(L"LOCAL_COMPRESS_PDF", L"RasBrowse Compress PDF");
            else if (btn.title == L"Job Photo") LaunchMiniBrowser(L"LOCAL_JOB_PHOTO", L"Job Photo Maker (300x300)");
            else if (btn.title == L"Job Signature") LaunchMiniBrowser(L"LOCAL_JOB_SIGN", L"Job Signature Maker");
            else if (btn.title == L"Age Calculator") LaunchMiniBrowser(L"LOCAL_AGE_CALC", L"RasBrowse Age Calculator");
            else if (btn.title == L"Graphic Calc") LaunchMiniBrowser(L"https://www.desmos.com/calculator", L"Graphic Calculator");
            else if (btn.title == L"Scientific Calc") LaunchMiniBrowser(L"https://web2.0calc.com", L"Scientific Calculator");

            else if (btn.title == L"Instant Note") LaunchMiniBrowser(L"LOCAL_INSTANT_NOTE", L"Instant Note");

            else if (btn.title == L"Study Materials") LaunchMiniBrowser(L"LOCAL_STUDY_MATS", L"Study Materials Vault");
            else if (btn.title == L"CGPA Calc") LaunchMiniBrowser(L"LOCAL_CGPA_CALC", L"CGPA Calculator");
            else if (btn.title == L"Exam Routine") LaunchMiniBrowser(L"LOCAL_ROUTINE", L"Exam Routine Tracker");

            btn.isHovered = false; 
            if(hParentWnd) InvalidateRect(hParentWnd, NULL, TRUE);
            return;
        }
    }
}
