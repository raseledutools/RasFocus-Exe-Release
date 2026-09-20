#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>

void DrawFileManagerTab(Gdiplus::Graphics& g, float cx, float cy, float cw, float ch);
void ProcessFileManagerMouseMove(float x, float y);
void ProcessFileManagerMouseClick(float x, float y, HWND hWnd);
void ProcessFileManagerMouseWheel(float x, float y, int delta);
void ProcessFileManagerRightClick(float x, float y, HWND hWnd);  // right-click context menu
void NavigateFileManagerTo(const std::wstring& path);   // navigate sidebar/drive clicks
// Call from WM_USER+50 handler in main message loop (Google Drive API response)
void ProcessDriveApiResponse(const std::string& json);
// Called from WinMain when launched with "-merge file1.pdf file2.pdf ..."
// (Windows Explorer right-click → "Merge PDFs with RasFocus+")
void RunExplorerPdfMerge(const std::vector<std::wstring>& pdfPaths);
