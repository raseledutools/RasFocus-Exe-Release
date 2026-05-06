// accounts.h
#pragma once
#include <windows.h>
#include <gdiplus.h>

void DrawAccountsTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessAccountsMouseMove(float x, float y);
void ProcessAccountsMouseClick(float x, float y);
