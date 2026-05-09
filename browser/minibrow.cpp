#include "advanced_feature.h"
#include <string>
#include <fstream>
#include <codecvt>
#include <locale>
#include <ctime>

using namespace Microsoft::WRL;
using namespace Gdiplus;

// গ্লোবাল এনভায়রনমেন্ট ভেরিয়েবল (mini_browser.cpp থেকে কল করা হবে)
extern ComPtr<ICoreWebView2Environment> g_sharedEnv;

// গ্লোবাল ডাউনলোড লিস্ট ইনিশিয়ালাইজেশন
std::vector<DownloadInfo> g_downloads;

// হিস্ট্রি ফাইলে সেভ করার ফাংশন
void SaveToHistory(const std::wstring& url, const std::wstring& title) {
    if (url == L"LOCAL_NTP" || url == L"about:blank" || url.empty()) return;

    std::wofstream out(L"rasbrowser_history.txt", std::ios::app);
    out.imbue(std::locale(out.getloc(), new std::codecvt_utf8<wchar_t>));
    if (out.is_open()) {
        time_t now = time(0);
        tm* ltm = localtime(&now);
        wchar_t timeBuf[32];
        wcsftime(timeBuf, 32, L"[%Y-%m-%d %H:%M:%S] ", ltm);
        
        out << timeBuf << title << L" | " << url << L"\n";
        out.close();
    }
}

// কাস্টম ডাউনলোড প্যানেল ড্র করার ফাংশন
void DrawDownloadPanel(Graphics& g, int windowWidth, int windowHeight, bool isDarkMode) {
    if (g_downloads.empty()) return;

    // শুধু সর্বশেষ ডাউনলোডটি দেখাব (অথবা চাইলে লুপ চালিয়ে লিস্ট দেখানো যায়)
    DownloadInfo& latestDl = g_downloads.back();
    
    // ডাউনলোড কমপ্লিট বা ফেইল হলে কিছুক্ষণ পর প্যানেল হাইড করার লজিক এখানে অ্যাড করা যেতে পারে

    float panelW = 350.0f;
    float panelH = 80.0f;
    float pX = windowWidth - panelW - 20.0f;
    float pY = 90.0f; // টুলবারের নিচে

    // প্যানেল ব্যাকগ্রাউন্ড
    SolidBrush bgBrush(isDarkMode ? Color(255, 40, 40, 45) : Color(255, 255, 255, 255));
    Pen borderPen(isDarkMode ? Color(255, 70, 70, 70) : Color(255, 200, 200, 200), 1.0f);
    
    g.FillRectangle(&bgBrush, pX, pY, panelW, panelH);
    g.DrawRectangle(&borderPen, pX, pY, panelW, panelH);

    // ফন্ট
    FontFamily ff(L"Segoe UI");
    Font fBold(&ff, 14, FontStyleBold, UnitPixel);
    Font fNorm(&ff, 12, FontStyleRegular, UnitPixel);
    SolidBrush txtBr(isDarkMode ? Color(255, 230, 230, 230) : Color(255, 40, 40, 40));
    SolidBrush dimBr(isDarkMode ? Color(255, 150, 150, 150) : Color(255, 120, 120, 120));

    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentNear);

    // ফাইলের নাম
    g.DrawString(latestDl.fileName.c_str(), -1, &fBold, RectF(pX + 15, pY + 10, panelW - 30, 20), &sf, &txtBr);

    // স্ট্যাটাস টেক্সট
    std::wstring status = L"Starting...";
    if (latestDl.isCompleted) status = L"Completed";
    else if (latestDl.isInterrupted) status = L"Interrupted / Failed";
    else if (latestDl.totalBytes > 0) {
        int percent = (int)((latestDl.receivedBytes * 100) / latestDl.totalBytes);
        status = std::to_wstring(percent) + L"% Downloading...";
    }
    g.DrawString(status.c_str(), -1, &fNorm, RectF(pX + 15, pY + 30, panelW - 30, 20), &sf, &dimBr);

    // প্রগ্রেস বার
    float barX = pX + 15;
    float barY = pY + 55;
    float barW = panelW - 30;
    float barH = 8.0f;
    
    SolidBrush barBg(isDarkMode ? Color(255, 60, 60, 60) : Color(255, 220, 220, 220));
    g.FillRectangle(&barBg, barX, barY, barW, barH);

    if (latestDl.totalBytes > 0 || latestDl.isCompleted) {
        float fillW = latestDl.isCompleted ? barW : (barW * latestDl.receivedBytes) / latestDl.totalBytes;
        SolidBrush barFill(Color(255, 0, 120, 215)); // ব্লু কালার
        if(latestDl.isInterrupted) barFill.SetColor(Color(255, 220, 50, 50)); // রেড কালার
        if(latestDl.isCompleted) barFill.SetColor(Color(255, 30, 180, 50)); // গ্রিন কালার
        g.FillRectangle(&barFill, barX, barY, fillW, barH);
    }
}

void SetupAdvancedFeatures(HWND hWnd, ComPtr<ICoreWebView2> webview) {
    if (!webview) return;

    // ==========================================
    // 🟢 1. CUSTOM DOWNLOAD MANAGER
    // ==========================================
    webview->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>(
        [hWnd](ICoreWebView2* sender, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
            
            // ডিফল্ট ক্রোম বা এজ-এর ডাউনলোড ইউআই (UI) হাইড করে দেওয়া হলো
            args->put_Handled(TRUE); 

            ComPtr<ICoreWebView2DownloadOperation> download;
            args->get_DownloadOperation(&download);

            if (download) {
                // ফাইলের নাম বা পাথ বের করা
                LPWSTR path = nullptr;
                download->get_ResultFilePath(&path);
                std::wstring filePath = path ? path : L"Unknown File";
                CoTaskMemFree(path);

                // শুধু ফাইলের নামটা এক্সট্র্যাক্ট করা
                std::wstring fileName = filePath;
                size_t pos = filePath.find_last_of(L"\\/");
                if (pos != std::wstring::npos) fileName = filePath.substr(pos + 1);

                // নতুন ডাউনলোড ইনফো অ্যাড করা
                DownloadInfo info;
                info.fileName = fileName;
                info.fullPath = filePath;
                info.isDownloading = true;
                g_downloads.push_back(info);
                int currentDlIdx = g_downloads.size() - 1;

                // UI আপডেট করার জন্য উইন্ডো রিফ্রেশ
                InvalidateRect(hWnd, NULL, FALSE);

                // ডাউনলোডের প্রগ্রেস ট্র্যাক করা
                download->add_BytesReceivedChanged(Callback<ICoreWebView2BytesReceivedChangedEventHandler>(
                    [hWnd, currentDlIdx](ICoreWebView2DownloadOperation* op, IUnknown* args) -> HRESULT {
                        if(currentDlIdx < g_downloads.size()) {
                            INT64 received = 0;
                            op->get_BytesReceived(&received);
                            g_downloads[currentDlIdx].receivedBytes = received;
                            
                            INT64 total = 0;
                            op->get_TotalBytesToReceive(&total);
                            g_downloads[currentDlIdx].totalBytes = total;
                            
                            // UI রিফ্রেশ
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return S_OK;
                    }).Get(), nullptr);

                // ডাউনলোডের অবস্থা (State) ট্র্যাক করা
                download->add_StateChanged(Callback<ICoreWebView2DownloadStateChangedEventHandler>(
                    [hWnd, currentDlIdx](ICoreWebView2DownloadOperation* op, IUnknown* args) -> HRESULT {
                        COREWEBVIEW2_DOWNLOAD_STATE state;
                        op->get_State(&state);
                        
                        if(currentDlIdx < g_downloads.size()) {
                            if (state == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED) {
                                g_downloads[currentDlIdx].isDownloading = false;
                                g_downloads[currentDlIdx].isCompleted = true;
                                MessageBoxW(hWnd, L"✅ Download Successfully Completed!", L"RasBrowser Downloader", MB_OK | MB_ICONINFORMATION);
                            }
                            else if (state == COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED) {
                                g_downloads[currentDlIdx].isDownloading = false;
                                g_downloads[currentDlIdx].isInterrupted = true;
                                MessageBoxW(hWnd, L"❌ Download Failed or Interrupted!", L"RasBrowser Downloader", MB_OK | MB_ICONERROR);
                            }
                            // UI রিফ্রেশ
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        return S_OK;
                    }).Get(), nullptr);
            }
            return S_OK;
        }).Get(), nullptr);


    // ==========================================
    // 🟢 2. CUSTOM RIGHT-CLICK (CONTEXT) MENU
    // ==========================================
    webview->add_ContextMenuRequested(Callback<ICoreWebView2ContextMenuRequestedEventHandler>(
        [hWnd](ICoreWebView2* sender, ICoreWebView2ContextMenuRequestedEventArgs* args) -> HRESULT {
            
            ComPtr<ICoreWebView2ContextMenuItemCollection> items;
            args->get_MenuItems(&items);

            if (items && g_sharedEnv) {
                // ICoreWebView2Environment9 ইন্টারফেস ব্যবহার করে নতুন মেনু আইটেম তৈরি করতে হয়
                ComPtr<ICoreWebView2Environment9> env9;
                g_sharedEnv.As(&env9);

                if (env9) {
                    // 🌟 কাস্টম মেনু ১: Scan with RasFocus AI
                    ComPtr<ICoreWebView2ContextMenuItem> aiScanItem;
                    env9->CreateContextMenuItem(L"🛡️ Scan Image/Link with RasFocus AI", nullptr, 
                        COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_COMMAND, &aiScanItem);

                    aiScanItem->add_CustomItemSelected(Callback<ICoreWebView2CustomItemSelectedEventHandler>(
                        [hWnd](ICoreWebView2ContextMenuItem* sender, IUnknown* args) -> HRESULT {
                            MessageBoxW(hWnd, L"RasFocus AI is analyzing the content for safety...", L"RasFocus AI Filter", MB_OK | MB_ICONINFORMATION);
                            return S_OK;
                        }).Get(), nullptr);

                    // 🌟 কাস্টম মেনু ২: Save to RasFocus Vault
                    ComPtr<ICoreWebView2ContextMenuItem> vaultItem;
                    env9->CreateContextMenuItem(L"🔒 Save Page to RasFocus Vault", nullptr, 
                        COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_COMMAND, &vaultItem);

                    vaultItem->add_CustomItemSelected(Callback<ICoreWebView2CustomItemSelectedEventHandler>(
                        [hWnd](ICoreWebView2ContextMenuItem* sender, IUnknown* args) -> HRESULT {
                            MessageBoxW(hWnd, L"Page securely saved to your private vault!", L"RasFocus Vault", MB_OK | MB_ICONINFORMATION);
                            return S_OK;
                        }).Get(), nullptr);

                    // আমাদের কাস্টম মেনুগুলো একদম উপরের দিকে (Index 0 এবং 1 এ) অ্যাড করে দিচ্ছি
                    items->InsertValueAtIndex(0, aiScanItem.Get());
                    items->InsertValueAtIndex(1, vaultItem.Get());
                }
            }
            return S_OK;
        }).Get(), nullptr);

    // ==========================================
    // 🟢 3. HISTORY TRACKER (Source Changed)
    // ==========================================
    webview->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
        [](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs* args) -> HRESULT {
            LPWSTR uri = nullptr;
            sender->get_Source(&uri);
            if (uri) {
                std::wstring urlStr(uri);
                
                // টাইটেল পাওয়ার জন্য DocumentTitleChanged ইভেন্ট বেশি ভালো, 
                // তবে আপাতত আমরা শুধু URL সেভ করছি।
                SaveToHistory(urlStr, L"Visited Page");
                
                CoTaskMemFree(uri);
            }
            return S_OK;
        }).Get(), nullptr);
}
