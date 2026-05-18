// downloads_panel.cpp — RasBrowser Downloads Panel
// TODO: এখানে সব function implement করো

#include "downloads_panel.h"
#include <gdiplus.h>
#include <shellapi.h>

using namespace Gdiplus;

bool g_downloadsPanelOpen = false;
int  g_downloadsHoverIdx  = -1;

void DrawDownloadsPanel(
    Graphics& g,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    bool isDarkMode, int dpi,
    int mouseX, int mouseY
) {
    if (!g_downloadsPanelOpen) return;

    // TODO: Chrome downloads page এর মতো আঁকো
    // Full width panel, toolbar নিচ থেকে শুরু
    //
    // Header: "Downloads" + "Clear all" বাটন
    //
    // প্রতিটা DownloadInfo (g_downloads থেকে, সবচেয়ে নতুন আগে):
    //   [📄 icon]  [fileName]           [Open file] [Show in folder]
    //              [progress bar]
    //              [status: 45% • 2.3 MB of 5.1 MB • 1.2 MB/s]
    //
    // completed হলে: progress bar সবুজ, "Open file" বাটন
    // interrupted হলে: progress bar লাল, "Retry" বাটন
    // downloading হলে: animated progress bar, "Cancel" বাটন
    //
    // advanced_feature.cpp এর DrawDownloadPanel() দেখো reference হিসেবে
    // কিন্তু এটা আরো বড় এবং full panel

    int panelY = titleBarH + toolbarH;
    int panelH = windowHeight - panelY;

    Color bgColor = isDarkMode ? Color(255, 28, 28, 35) : Color(255, 248, 250, 252);
    SolidBrush bgBrush(bgColor);
    g.FillRectangle(&bgBrush, 0, panelY, windowWidth, panelH);

    int headerH = (int)(50.0f * dpi / 96.0f);
    Color headerColor = isDarkMode ? Color(255, 35, 35, 42) : Color(255, 255, 255, 255);
    SolidBrush headerBrush(headerColor);
    g.FillRectangle(&headerBrush, 0, panelY, windowWidth, headerH);

    FontFamily ff(L"Segoe UI");
    Font fTitle(&ff, 18, FontStyleBold, UnitPixel);
    Font fNorm (&ff, 13, FontStyleRegular, UnitPixel);

    SolidBrush txtBrush(isDarkMode ? Color(255,230,230,230) : Color(255,30,30,30));
    SolidBrush dimBrush(isDarkMode ? Color(255,140,140,140) : Color(255,120,120,120));

    StringFormat sfL;
    sfL.SetAlignment(StringAlignmentNear);
    sfL.SetLineAlignment(StringAlignmentCenter);

    g.DrawString(L"Downloads", -1, &fTitle,
        RectF(20.0f * dpi / 96.0f, (float)panelY, 250.0f, (float)headerH),
        &sfL, &txtBrush);

    if (g_downloads.empty()) {
        g.DrawString(L"No downloads yet.", -1, &fNorm,
            RectF(20.0f, (float)(panelY + headerH + 20), (float)windowWidth, 30.0f),
            &sfL, &dimBrush);
        return;
    }

    // TODO: প্রতিটা download item আঁকো
    // g_downloads (advanced_feature.h) থেকে reverse order এ দেখাও
}

std::wstring HandleDownloadsPanelClick(
    int mouseX, int mouseY,
    int windowWidth, int windowHeight,
    int titleBarH, int toolbarH,
    int dpi
) {
    if (!g_downloadsPanelOpen) return L"";

    // TODO: কোন বাটনে click হয়েছে detect করো
    // "Open file" → ShellExecuteW দিয়ে file open করো
    // "Show in folder" → explorer /select,<path>
    // "Clear all" → ClearCompletedDownloads()

    return L"";
}

void ClearCompletedDownloads() {
    // TODO: g_downloads থেকে isCompleted বা isInterrupted গুলো সরাও
    auto& dl = g_downloads;
    dl.erase(
        std::remove_if(dl.begin(), dl.end(),
            [](const DownloadInfo& d) { return d.isCompleted || d.isInterrupted; }),
        dl.end()
    );
}
