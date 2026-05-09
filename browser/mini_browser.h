#ifndef MINI_BROWSER_H
#define MINI_BROWSER_H

#include <string>
#include <windows.h>

// ড্যাশবোর্ড বা অন্য যেকোনো ফাইল থেকে এই ফাংশনটি কল করা যাবে
void LaunchMiniBrowser(std::wstring url, std::wstring title);

#endif
