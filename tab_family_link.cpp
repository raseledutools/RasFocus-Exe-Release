#include "tab_family_link.h"
#include <string>

using namespace Gdiplus;
using namespace std;

// মেইন ফাইল থেকে ফাংশন ও ভেরিয়েবল ইমপোর্ট করা হচ্ছে (Real Connections)
extern string g_currentPackage; 
extern bool g_isPremiumUser;
extern string SendFirestoreRequest(const string& method, const string& path, const string& payload = "");
extern string GetHardwareID();

// পেজের স্টেট ভেরিয়েবল
wchar_t fl_pinCode[7] = L""; 
bool fl_isPinFocused = false;
bool fl_hoverConnectBtn = false;
int fl_connectionState = 0; // 0=Idle, 1=Connecting, 2=Success, 3=Error
wstring fl_statusMsg = L"";

// রাউন্ডেড রেক্ট্যাঙ্গেল (Rounded Rectangle) ড্র করার হেল্পার
void AddRoundRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void DrawFamilyLinkTab(Graphics& g, float x, float y, float w, float h) {
    SolidBrush bgContent(Color(255, 245, 248, 250));
    g.FillRectangle(&bgContent, x, y, w, h);

    FontFamily ff(L"Segoe UI");
    StringFormat fmtC; fmtC.SetAlignment(StringAlignmentCenter); fmtC.SetLineAlignment(StringAlignmentCenter);

    // ── ১. প্যাকেজ চেকিং ──
    bool hasAccess = (g_currentPackage == "PREMIUM" || g_currentPackage == "PARENTAL" || g_currentPackage == "TRIAL");
    if (!hasAccess) {
        SolidBrush textDark(Color(255, 50, 50, 50));
        Font fTitle(&ff, 24, FontStyleBold, UnitPixel);
        Font fSub(&ff, 14, FontStyleRegular, UnitPixel);
        
        g.DrawString(L"Premium Feature", -1, &fTitle, RectF(x, y + 150.0f, w, 40.0f), &fmtC, &textDark);
        g.DrawString(L"Upgrade to RasFocus+ Pro or Combo package to link parent device.", -1, &fSub, RectF(x, y + 190.0f, w, 30.0f), &fmtC, &textDark);
        return; 
    }

    // ── ২. ফ্যামিলি লিংক ড্যাশবোর্ড ডিজাইন ──
    SolidBrush textTeal(Color(255, 0, 150, 160));
    SolidBrush textGray(Color(255, 100, 100, 100));
    Font fHeader(&ff, 28, FontStyleBold, UnitPixel);
    Font fDesc(&ff, 14, FontStyleRegular, UnitPixel);

    float centerY = y + 80.0f;
    g.DrawString(L"Family Link Setup", -1, &fHeader, RectF(x, centerY, w, 40.0f), &fmtC, &textTeal);
    g.DrawString(L"Enter the 6-digit connection code from your parent's phone.", -1, &fDesc, RectF(x, centerY + 40.0f, w, 30.0f), &fmtC, &textGray);

    // ── ৩. পিন ইনপুট বক্স ──
    float boxW = 220.0f, boxH = 50.0f;
    float boxX = x + (w - boxW) / 2.0f;
    float boxY = centerY + 100.0f;

    GraphicsPath boxPath;
    AddRoundRect(boxPath, boxX, boxY, boxW, boxH, 8.0f);
    SolidBrush boxBg(Color(255, 255, 255, 255));
    g.FillPath(&boxBg, &boxPath);
    Pen boxBorder(fl_isPinFocused ? Color(255, 0, 150, 160) : Color(255, 200, 200, 200), 2.0f);
    g.DrawPath(&boxBorder, &boxPath);

    Font fPin(&ff, 26, FontStyleBold, UnitPixel);
    SolidBrush pinText(Color(255, 50, 50, 50));
    wstring displayPin(fl_pinCode);
    if (displayPin.empty() && !fl_isPinFocused) {
        SolidBrush placeholderColor(Color(255, 180, 180, 180));
        g.DrawString(L"000000", -1, &fPin, RectF(boxX, boxY + 4.0f, boxW, boxH), &fmtC, &placeholderColor);
    } else {
        wstring spacedPin = L"";
        for (size_t i = 0; i < displayPin.length(); i++) { spacedPin += displayPin[i]; spacedPin += L" "; }
        g.DrawString(spacedPin.c_str(), -1, &fPin, RectF(boxX, boxY + 4.0f, boxW, boxH), &fmtC, &pinText);
    }

    // ── ৪. কানেক্ট বাটন ──
    float btnW = 180.0f, btnH = 45.0f;
    float btnX = x + (w - btnW) / 2.0f;
    float btnY = boxY + 80.0f;

    GraphicsPath btnPath;
    AddRoundRect(btnPath, btnX, btnY, btnW, btnH, 8.0f);
    SolidBrush btnBg(fl_hoverConnectBtn ? Color(255, 0, 120, 130) : Color(255, 0, 150, 160));
    g.FillPath(&btnBg, &btnPath);
    Font fBtn(&ff, 15, FontStyleBold, UnitPixel);
    SolidBrush btnWhite(Color(255, 255, 255, 255));
    g.DrawString(L"Connect Device", -1, &fBtn, RectF(btnX, btnY, btnW, btnH), &fmtC, &btnWhite);

    // ── ৫. স্ট্যাটাস মেসেজ ──
    if (!fl_statusMsg.empty()) {
        SolidBrush statusCol = (fl_connectionState == 2) ? SolidBrush(Color(255, 0, 180, 70)) : SolidBrush(Color(255, 232, 17, 35));
        if (fl_connectionState == 1) statusCol.SetColor(Color(255, 0, 150, 160));
        Font fStatus(&ff, 13, FontStyleBold, UnitPixel);
        g.DrawString(fl_statusMsg.c_str(), -1, &fStatus, RectF(x, btnY + 60.0f, w, 30.0f), &fmtC, &statusCol);
    }
}

void ProcessFamilyLinkMouseMove(float mx, float my, float cX, float cY) {
    float contentW = 1024.0f - 170.0f; 
    float btnW = 180.0f, btnH = 45.0f;
    float btnX = cX + (contentW - btnW) / 2.0f;
    float btnY = cY + 80.0f + 100.0f + 80.0f; 
    fl_hoverConnectBtn = (mx >= btnX && mx <= btnX + btnW && my >= btnY && my <= btnY + btnH);
}

// 🔥 একদম রিয়েল ফায়ারবেস লজিক (১০০% প্রোডাকশন কোড)
void ProcessFamilyLinkMouseClick(float mx, float my, float cX, float cY, HWND hWnd) {
    float contentW = 1024.0f - 170.0f; 
    float boxW = 220.0f, boxH = 50.0f;
    float boxX = cX + (contentW - boxW) / 2.0f;
    float boxY = cY + 80.0f + 100.0f;

    fl_isPinFocused = (mx >= boxX && mx <= boxX + boxW && my >= boxY && my <= boxY + boxH);

    if (fl_hoverConnectBtn) {
        wstring currentPin(fl_pinCode);
        if (currentPin.length() == 6) {
            
            fl_connectionState = 1;
            fl_statusMsg = L"Verifying PIN with Server...";
            InvalidateRect(hWnd, NULL, FALSE); // UI তে Connecting লেখাটা দেখানোর জন্য
            UpdateWindow(hWnd);

            string pinStr(currentPin.begin(), currentPin.end());
            
            // ── ধাপ ১: ফায়ারবেস থেকে পিনটা চেক করা ──
            string getPath = "/v1/projects/rasfocus-c746d/databases/(default)/documents/pairing_codes/" + pinStr;
            string response = SendFirestoreRequest("GET", getPath);

            // যদি পিন ফায়ারবেসে পাওয়া যায় (error বা NOT_FOUND না থাকে)
            if (response.find("\"error\"") == string::npos && response.find("NOT_FOUND") == string::npos) {
                
                string parentUid = "";
                // JSON থেকে parent_uid বের করা (তোমার মেইন ফাইলের স্টাইলে)
                size_t pUidPos = response.find("\"parent_uid\"");
                if(pUidPos != string::npos) {
                    size_t strValPos = response.find("\"stringValue\": \"", pUidPos);
                    if(strValPos != string::npos) {
                        strValPos += 16; // "stringValue": " এর পরের জায়গা
                        size_t endQuote = response.find("\"", strValPos);
                        if(endQuote != string::npos) {
                            parentUid = response.substr(strValPos, endQuote - strValPos);
                        }
                    }
                }

                if(!parentUid.empty()) {
                    // ── ধাপ ২: পিসির সাথে প্যারেন্টকে লিংক করা ──
                    string hwId = GetHardwareID(); // পিসির আইডি
                    string patchPath = "/v1/projects/rasfocus-c746d/databases/(default)/documents/devices/" + hwId + "?updateMask.fieldPaths=parent_uid";
                    
                    // পিসির ডেটাবেসে parent_uid আপডেট করে দেওয়া
                    string payload = "{\"fields\":{\"parent_uid\":{\"stringValue\":\"" + parentUid + "\"}}}";
                    SendFirestoreRequest("PATCH", patchPath, payload);

                    fl_connectionState = 2;
                    fl_statusMsg = L"Successfully Linked to Parent Device!";
                    
                } else {
                    fl_connectionState = 3;
                    fl_statusMsg = L"Data Error. Parent UID not found.";
                }
            } else {
                fl_connectionState = 3;
                fl_statusMsg = L"Invalid or Expired PIN. Try again.";
            }
        } else {
            fl_connectionState = 3;
            fl_statusMsg = L"Please enter a valid 6-digit PIN.";
        }
    }
}

// কীবোর্ড ইনপুট
void ProcessFamilyLinkChar(wchar_t c) {
    if (!fl_isPinFocused) return;
    int len = lstrlenW(fl_pinCode);
    if (c == L'\b') { 
        if (len > 0) fl_pinCode[len - 1] = L'\0';
    } else if (c >= L'0' && c <= L'9') { 
        if (len < 6) { fl_pinCode[len] = c; fl_pinCode[len + 1] = L'\0'; }
    }
}

void ProcessFamilyLinkKeyDown(WPARAM wp) {
    // এন্টার চাপলে আর ফোকাস থাকবে না
    if (fl_isPinFocused && wp == VK_RETURN) {
        fl_isPinFocused = false;
    }
}
