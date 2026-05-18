// history_panel.cpp — RasBrowser History Panel
// TODO: এখানে সব function implement করো

#include "history_panel.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>

using namespace Gdiplus;

std::vector<HistoryItem> g_history;
bool g_historyPanelOpen  = false;
int  g_historyHoverIdx   = -1;
int  g_historyScrollOffset = 0;

static std::wstring GetHistoryFilePath() {
    wchar_t path[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path);
    return std::wstring(path) + L"\\RasBrowserData\\rasbrowser_history.txt";
}

void LoadHistory() {
    g_history.clear();

    // TODO: GetHistoryFilePath() থেকে পড়ো
    // advanced_feature.cpp এ SaveToHistory() যেভাবে লেখে সেই format:
    // "[2025-01-01 10:00:00] Page Title | https://url.com"
    // Parse করে g_history তে push করো
    // সবচেয়ে নতুন first দেখাও (reverse order)

    std::wifstream in(GetHistoryFilePath());
    if (!in.is_open()) return;

    std::wstring line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        HistoryItem item;

        // Parse: "[timestamp] title | url"
        size_t closeBracket = line.find(L']');
        size_t pipePos      = line.rfind(L'|');

        if (closeBracket != std::wstring::npos && pipePos != std::wstring::npos) {
            item.timestamp = line.substr(0, closeBracket + 1);
            item.title     = line.substr(closeBracket + 2, pipePos - closeBracket - 3);
            item.url       = line.substr(pipePos + 2);
            g_history.insert(g_history.begin(), item); // newest first
        }
    }
}

void ClearHistory() {
    g_history.clear();
    // TODO: GetHistoryFilePath() এর file delete বা truncate করো
    std::wofstream out(GetHistoryFilePath(), std::ios::trunc);
    out.close();
}

void DrawHistoryPanel(
    Graphics& g,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    bool isDarkMode, int dpi,
    int mouseX, int mouseY
) {
    if (!g_historyPanelOpen) return;

    // TODO: Chrome history page এর মতো panel আঁকো
    // Full width, toolbar নিচ থেকে শুরু
    // Header: "History" title + "Clear all" বাটন
    // প্রতিটা item: timestamp + title + url + X (delete) বাটন
    // g_historyScrollOffset দিয়ে scroll handle করো
    // g_historyHoverIdx দিয়ে hover highlight করো

    int panelY = titleBarH + toolbarH;
    int panelH = windowHeight - panelY;

    // Background
    Color bgColor = isDarkMode ? Color(255, 28, 28, 35) : Color(255, 248, 250, 252);
    SolidBrush bgBrush(bgColor);
    g.FillRectangle(&bgBrush, 0, panelY, windowWidth, panelH);

    // Header
    Color headerColor = isDarkMode ? Color(255, 35, 35, 42) : Color(255, 255, 255, 255);
    SolidBrush headerBrush(headerColor);
    int headerH = (int)(50.0f * dpi / 96.0f);
    g.FillRectangle(&headerBrush, 0, panelY, windowWidth, headerH);

    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff, 18, FontStyleBold, UnitPixel);
    Font fNorm (&ff, 13, FontStyleRegular, UnitPixel);
    Font fSmall(&ff, 11, FontStyleRegular, UnitPixel);

    SolidBrush txtBrush(isDarkMode ? Color(255,230,230,230) : Color(255,30,30,30));
    SolidBrush dimBrush(isDarkMode ? Color(255,140,140,140) : Color(255,120,120,120));

    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);

    g.DrawString(L"History", -1, &fTitle,
        RectF(20.0f * dpi / 96.0f, (float)panelY, 200.0f, (float)headerH),
        &sfL, &txtBrush);

    // TODO: item list আঁকো (g_historyScrollOffset থেকে শুরু করো)
    if (g_history.empty()) {
        g.DrawString(L"No history yet.", -1, &fNorm,
            RectF(20.0f, (float)(panelY + headerH + 20), (float)windowWidth, 30.0f),
            &sfL, &dimBrush);
    }
}

std::wstring HandleHistoryPanelClick(
    int mouseX, int mouseY,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    int dpi
) {
    if (!g_historyPanelOpen) return L"";

    // TODO: কোন item এ click হয়েছে calculate করো
    // X বাটনে click হলে সেই item delete করো
    // Item এ click হলে সেই url return করো

    return L"";
}

void HandleHistoryScroll(int delta) {
    // TODO: delta (mouse wheel) দিয়ে g_historyScrollOffset পরিবর্তন করো
    // Clamp করো: 0 থেকে max(0, g_history.size() - visible_items)
    g_historyScrollOffset -= delta / 120;
    if (g_historyScrollOffset < 0) g_historyScrollOffset = 0;
    int maxScroll = (int)g_history.size() - 10;
    if (g_historyScrollOffset > maxScroll) g_historyScrollOffset = maxScroll;
}
