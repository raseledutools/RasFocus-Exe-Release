#pragma once
#include <windows.h>
#include <string>

// Launch a standalone full-screen image viewer window.
// imagePath   : absolute path of the image to open first
// The viewer finds all sibling images in the same folder automatically,
// allowing Prev/Next navigation with arrow keys or on-screen buttons.
// The window is self-contained; when the user closes it the process continues.
void LaunchImageViewer(const std::wstring& imagePath);
