#ifndef TAB_STRICT_H
#define TAB_STRICT_H

#include <windows.h>
#include <gdiplus.h>

void DrawStrictProtocolsTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void HideStrictProtocolsTab();
void ProcessStrictProtocolsMouseMove(float x, float y);
void ProcessStrictProtocolsMouseClick(float x, float y);
void LoadStrictSettings();
void SaveStrictSettings();

// 🔴 FIX: tab_ai.cpp এ extern হিসেবে declare করা আছে — এখানে definition আছে tab_strict.cpp তে
bool RequestParentalAccess(HWND hWnd);

#endif // TAB_STRICT_H
