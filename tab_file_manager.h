#pragma once
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>

void DrawFileManagerTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessFileManagerMouseMove(float x, float y);
void ProcessFileManagerMouseClick(float x, float y, HWND hWnd);
void ProcessFileManagerMouseWheel(float x, float y, int delta);
void ProcessFileManagerRightClick(float x, float y, HWND hWnd);
void NavigateFileManagerTo(const std::wstring& path);
void ProcessDriveApiResponse(const std::string& json);
void RunExplorerPdfMerge(const std::vector<std::wstring>& pdfPaths);
