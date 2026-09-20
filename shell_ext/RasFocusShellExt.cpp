// RasFocusShellExt.cpp
// Windows Shell Extension for RasFocus
// Adds right-click context menu in Windows Explorer for:
//   - Copy / Cut / Paste
//   - Zip / Unzip
//   - PDF Merge, Images→PDF, PDF Split, PDF Extract Pages
//
// Build:  cl.exe /EHsc /std:c++17 /MT /O2 /LD RasFocusShellExt.cpp
//         /link /OUT:RasFocusShellExt.dll /DLL uuid.lib ole32.lib
//         oleaut32.lib shlwapi.lib shell32.lib gdi32.lib gdiplus.lib
//         comdlg32.lib user32.lib advapi32.lib
//
// Install:   regsvr32 RasFocusShellExt.dll
// Uninstall: regsvr32 /u RasFocusShellExt.dll

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <exdisp.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <objbase.h>
#include <olectl.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <sstream>

#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

// {A7B3C2D1-E4F5-4A6B-8C9D-0E1F2A3B4C5D}
DEFINE_GUID(CLSID_RasFocusShellExt,
    0xA7B3C2D1, 0xE4F5, 0x4A6B,
    0x8C, 0x9D, 0x0E, 0x1F, 0x2A, 0x3B, 0x4C, 0x5D);

static HINSTANCE g_hModule   = NULL;
static LONG      g_refCount  = 0;
static ULONG_PTR g_gdipToken = 0;

// ============================================================
// Utility helpers
// ============================================================

static std::wstring GetExtW(const std::wstring& p) {
    size_t d = p.rfind(L'.');
    if (d == std::wstring::npos) return L"";
    std::wstring e = p.substr(d + 1);
    for (auto& c : e) c = towlower(c);
    return e;
}
static bool IsPdfW(const std::wstring& p) { return GetExtW(p) == L"pdf"; }
static bool IsImgW(const std::wstring& p) {
    auto e = GetExtW(p);
    return e==L"jpg"||e==L"jpeg"||e==L"png"||e==L"gif"||
           e==L"bmp"||e==L"webp"||e==L"tiff"||e==L"tif";
}
static bool IsZipW(const std::wstring& p) { return GetExtW(p) == L"zip"; }

static std::wstring PromptSaveW(HWND hw, const wchar_t* filter,
                                const wchar_t* defExt, const wchar_t* title,
                                const std::wstring& initDir = L"") {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hw;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = defExt;
    ofn.lpstrTitle  = title;
    ofn.lpstrInitialDir = initDir.empty() ? nullptr : initDir.c_str();
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    return GetSaveFileNameW(&ofn) ? buf : L"";
}

static std::vector<uint8_t> ReadFileBytes(const std::wstring& path) {
    std::vector<uint8_t> buf;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return buf;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz > 0) { buf.resize(sz); fread(buf.data(), 1, sz, f); }
    fclose(f);
    return buf;
}

// ============================================================
// Clipboard state (process-global for cut/copy/paste)
// ============================================================

enum class FmOp { None, Copy, Cut };
static FmOp                    g_clipOp    = FmOp::None;
static std::vector<std::wstring> g_clipPaths;

static void DoCopy(const std::vector<std::wstring>& paths, bool cut) {
    g_clipPaths = paths;
    g_clipOp    = cut ? FmOp::Cut : FmOp::Copy;
}

static bool CopyDirRec(const std::wstring& src, const std::wstring& dst) {
    CreateDirectoryW(dst.c_str(), NULL);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        std::wstring s = src + L"\\" + fd.cFileName;
        std::wstring d = dst + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) CopyDirRec(s, d);
        else CopyFileW(s.c_str(), d.c_str(), FALSE);
    } while (FindNextFileW(h, &fd));
    FindClose(h); return true;
}

static void DoPaste(HWND hw, const std::wstring& destFolder) {
    if (g_clipPaths.empty() || g_clipOp == FmOp::None) {
        MessageBoxW(hw, L"Clipboard is empty.", L"RasFocus Paste", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring dest = destFolder;
    if (!dest.empty() && dest.back() != L'\\') dest += L'\\';

    int errors = 0;
    for (auto& src : g_clipPaths) {
        std::wstring name = src;
        size_t sl = name.rfind(L'\\');
        if (sl != std::wstring::npos) name = name.substr(sl + 1);
        std::wstring d = dest + name;
        if (!_wcsicmp(src.c_str(), d.c_str())) continue;
        // Unique name if exists
        if (GetFileAttributesW(d.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring base = name, ext;
            size_t dot = name.rfind(L'.');
            if (dot != std::wstring::npos) { base = name.substr(0, dot); ext = name.substr(dot); }
            int n = 2;
            do { d = dest + base + L" (" + std::to_wstring(n++) + L")" + ext; }
            while (GetFileAttributesW(d.c_str()) != INVALID_FILE_ATTRIBUTES && n < 9999);
        }
        DWORD attr = GetFileAttributesW(src.c_str());
        bool isDir = (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
        bool ok = (g_clipOp == FmOp::Cut)
            ? (MoveFileExW(src.c_str(), d.c_str(), MOVEFILE_REPLACE_EXISTING) != 0)
            : (isDir ? CopyDirRec(src, d) : (CopyFileW(src.c_str(), d.c_str(), FALSE) != 0));
        if (!ok) errors++;
    }
    if (g_clipOp == FmOp::Cut) { g_clipPaths.clear(); g_clipOp = FmOp::None; }
    if (errors)
        MessageBoxW(hw, (std::to_wstring(errors) + L" item(s) failed.").c_str(),
                    L"RasFocus Paste", MB_OK | MB_ICONWARNING);
}

// ============================================================
// ZIP / UNZIP  (Windows Shell IShellDispatch)
// ============================================================

static void DoZip(HWND hw, const std::vector<std::wstring>& paths,
                  const std::wstring& initDir) {
    std::wstring firstName = paths[0];
    size_t sl = firstName.rfind(L'\\');
    if (sl != std::wstring::npos) firstName = firstName.substr(sl + 1);
    size_t dot = firstName.rfind(L'.');
    std::wstring base = (dot != std::wstring::npos) ? firstName.substr(0, dot) : firstName;
    if (paths.size() > 1) base = L"Archive";

    std::wstring zipPath = PromptSaveW(hw,
        L"ZIP Files\0*.zip\0All Files\0*.*\0", L"zip",
        L"Save ZIP As", initDir);
    if (zipPath.empty()) return;

    // Write empty ZIP stub
    static const uint8_t emptyZip[] = {
        0x50,0x4B,0x05,0x06, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0
    };
    FILE* f = _wfopen(zipPath.c_str(), L"wb");
    if (!f) { MessageBoxW(hw, L"Cannot create ZIP.", L"Error", MB_OK|MB_ICONERROR); return; }
    fwrite(emptyZip, 1, sizeof(emptyZip), f); fclose(f);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IShellDispatch* pShell = nullptr;
    if (FAILED(CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER,
                                IID_IShellDispatch, (void**)&pShell)) || !pShell) {
        MessageBoxW(hw, L"Shell dispatch unavailable.", L"Error", MB_OK|MB_ICONERROR);
        return;
    }
    VARIANT vZip; VariantInit(&vZip);
    vZip.vt = VT_BSTR; vZip.bstrVal = SysAllocString(zipPath.c_str());
    Folder* pZipFolder = nullptr;
    pShell->NameSpace(vZip, &pZipFolder);
    SysFreeString(vZip.bstrVal);

    if (pZipFolder) {
        for (auto& src : paths) {
            VARIANT vSrc; VariantInit(&vSrc);
            vSrc.vt = VT_BSTR; vSrc.bstrVal = SysAllocString(src.c_str());
            VARIANT vOpts; VariantInit(&vOpts);
            vOpts.vt = VT_I4; vOpts.lVal = 4 | 16 | 1024;
            pZipFolder->CopyHere(vSrc, vOpts);
            SysFreeString(vSrc.bstrVal);
            Sleep(600);
        }
        pZipFolder->Release();
    }
    pShell->Release();
    MessageBoxW(hw, (L"ZIP created:\n" + zipPath).c_str(), L"RasFocus ZIP", MB_OK|MB_ICONINFORMATION);
}

static void DoUnzip(HWND hw, const std::wstring& zipPath) {
    wchar_t destBuf[MAX_PATH] = {};
    std::wstring initDir = zipPath.substr(0, zipPath.rfind(L'\\'));
    wcscpy_s(destBuf, initDir.c_str());

    BROWSEINFOW bi = {};
    bi.hwndOwner = hw;
    bi.lpszTitle = L"Extract to folder:";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lParam    = (LPARAM)initDir.c_str();
    struct BffHelper {
        static int CALLBACK Proc(HWND hwnd, UINT msg, LPARAM, LPARAM lp) {
            if (msg == BFFM_INITIALIZED)
                SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, lp);
            return 0;
        }
    };
    bi.lpfn = BffHelper::Proc;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    SHGetPathFromIDListW(pidl, destBuf);
    CoTaskMemFree(pidl);

    std::wstring extractTo = destBuf;
    if (!extractTo.empty() && extractTo.back() != L'\\') extractTo += L'\\';

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IShellDispatch* pShell = nullptr;
    if (FAILED(CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER,
                                IID_IShellDispatch, (void**)&pShell)) || !pShell) return;

    VARIANT vZip; VariantInit(&vZip);
    vZip.vt = VT_BSTR; vZip.bstrVal = SysAllocString(zipPath.c_str());
    Folder* pZipFolder = nullptr;
    pShell->NameSpace(vZip, &pZipFolder);
    SysFreeString(vZip.bstrVal);

    VARIANT vDest; VariantInit(&vDest);
    vDest.vt = VT_BSTR; vDest.bstrVal = SysAllocString(extractTo.c_str());
    Folder* pDestFolder = nullptr;
    pShell->NameSpace(vDest, &pDestFolder);
    SysFreeString(vDest.bstrVal);

    if (pZipFolder && pDestFolder) {
        FolderItems* pItems = nullptr;
        pZipFolder->Items(&pItems);
        if (pItems) {
            VARIANT vItems; VariantInit(&vItems);
            vItems.vt = VT_DISPATCH; vItems.pdispVal = pItems;
            VARIANT vOpts; VariantInit(&vOpts);
            vOpts.vt = VT_I4; vOpts.lVal = 4 | 16 | 1024;
            pDestFolder->CopyHere(vItems, vOpts);
            Sleep(1000);
            pItems->Release();
        }
        pZipFolder->Release(); pDestFolder->Release();
    }
    pShell->Release();
    MessageBoxW(hw, (L"Extracted to:\n" + extractTo).c_str(),
                L"RasFocus Unzip", MB_OK|MB_ICONINFORMATION);
}

// ============================================================
// PDF MERGE  (PowerShell helper)
// ============================================================

static void DoMergePDFs(HWND hw, const std::vector<std::wstring>& pdfs,
                         const std::wstring& initDir) {
    std::wstring outPath = PromptSaveW(hw,
        L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf",
        L"Save Merged PDF As", initDir);
    if (outPath.empty()) return;

    // Write PowerShell script
    std::wstring ps1 = outPath + L"_merge.ps1";
    FILE* fps = _wfopen(ps1.c_str(), L"w, ccs=UTF-8");
    if (!fps) return;
    fwprintf(fps,
        L"param($Out,[string[]]$In)\n"
        L"$pdftk=Get-Command pdftk -EA SilentlyContinue\n"
        L"if($pdftk){& pdftk @In cat output $Out;exit}\n"
        L"$src=@'\nusing System;using System.IO;\n"
        L"public class M{\n"
        L"public static void Run(string[]i,string o){\n"
        L"using(var fs=File.OpenWrite(o))foreach(var f in i){var b=File.ReadAllBytes(f);fs.Write(b,0,b.Length);}\n"
        L"}}\n'@\n"
        L"Add-Type -TypeDefinition $src\n"
        L"[M]::Run($In,$Out)\n"
    );
    fclose(fps);

    std::wstring args = L"-ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + ps1 + L"\" -Out \"" + outPath + L"\" -In @(";
    for (size_t i = 0; i < pdfs.size(); i++) {
        if (i) args += L",";
        args += L"'" + pdfs[i] + L"'";
    }
    args += L")";

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize     = sizeof(sei);
    sei.fMask      = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb     = L"open";
    sei.lpFile     = L"powershell.exe";
    sei.lpParameters = args.c_str();
    sei.nShow      = SW_HIDE;
    ShellExecuteExW(&sei);
    if (sei.hProcess) { WaitForSingleObject(sei.hProcess, 30000); CloseHandle(sei.hProcess); }
    DeleteFileW(ps1.c_str());

    ShellExecuteW(NULL, L"open", outPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

// ============================================================
// IMAGES → PDF  (GDI+ JPEG encode + raw PDF writer)
// ============================================================

static int GetJpegClsid(CLSID* c) {
    using namespace Gdiplus;
    UINT n = 0, sz = 0;
    GetImageEncodersSize(&n, &sz);
    if (!sz) return -1;
    auto* info = (ImageCodecInfo*)malloc(sz);
    if (!info) return -1;
    GetImageEncoders(n, sz, info);
    for (UINT i = 0; i < n; i++)
        if (!wcscmp(info[i].MimeType, L"image/jpeg")) { *c = info[i].Clsid; free(info); return (int)i; }
    free(info); return -1;
}

static void DoImagesToPdf(HWND hw, const std::vector<std::wstring>& imgs,
                           const std::wstring& initDir) {
    std::wstring outPath = PromptSaveW(hw,
        L"PDF Files\0*.pdf\0All Files\0*.*\0", L"pdf",
        L"Save Images as PDF", initDir);
    if (outPath.empty()) return;

    using namespace Gdiplus;
    GdiplusStartupInput si; ULONG_PTR tok;
    GdiplusStartup(&tok, &si, NULL);

    CLSID jpegClsid; bool hasJpeg = (GetJpegClsid(&jpegClsid) >= 0);

    struct Page { std::vector<uint8_t> jpeg; int w, h; };
    std::vector<Page> pages;

    for (auto& path : imgs) {
        Image* img = Image::FromFile(path.c_str());
        if (!img || img->GetLastStatus() != Ok) { delete img; continue; }
        Page pg; pg.w = (int)img->GetWidth(); pg.h = (int)img->GetHeight();
        if (hasJpeg) {
            IStream* st = nullptr; CreateStreamOnHGlobal(NULL, TRUE, &st);
            EncoderParameters ep; ep.Count = 1;
            ep.Parameter[0].Guid = EncoderQuality;
            ep.Parameter[0].Type = EncoderParameterValueTypeLong;
            ep.Parameter[0].NumberOfValues = 1;
            ULONG q = 92; ep.Parameter[0].Value = &q;
            if (img->Save(st, &jpegClsid, &ep) == Ok) {
                STATSTG stat; st->Stat(&stat, STATFLAG_NONAME);
                HGLOBAL hg; GetHGlobalFromStream(st, &hg);
                void* ptr = GlobalLock(hg);
                if (ptr) { pg.jpeg.assign((uint8_t*)ptr, (uint8_t*)ptr + stat.cbSize.LowPart); }
                GlobalUnlock(hg);
            }
            st->Release();
        }
        delete img;
        if (!pg.jpeg.empty()) pages.push_back(std::move(pg));
    }
    GdiplusShutdown(tok);

    FILE* fOut = _wfopen(outPath.c_str(), L"wb");
    if (!fOut) return;

    fprintf(fOut, "%%PDF-1.4\n%%%c%c%c%c\n", 0xE2,0xE3,0xCF,0xD3);

    int N = (int)pages.size();
    std::map<int,long> offsets;

    auto startObj = [&](int n) { offsets[n] = ftell(fOut); fprintf(fOut, "%d 0 obj\n", n); };
    auto endObj   = [&]()      { fprintf(fOut, "endobj\n\n"); };

    // Obj 1 — Catalog
    startObj(1); fprintf(fOut, "<< /Type /Catalog /Pages 2 0 R >>\n"); endObj();
    // Obj 2 — Pages
    startObj(2);
    fprintf(fOut, "<< /Type /Pages /Kids [");
    for (int i = 0; i < N; i++) { if(i) fprintf(fOut," "); fprintf(fOut, "%d 0 R", 3+i*3); }
    fprintf(fOut, "] /Count %d >>\n", N); endObj();

    for (int i = 0; i < N; i++) {
        float pw = 595.0f, ph = 595.0f / ((float)pages[i].w / pages[i].h);
        if (ph > 842.0f) { ph = 842.0f; pw = 842.0f * ((float)pages[i].w / pages[i].h); }

        // Page
        startObj(3+i*3);
        fprintf(fOut, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f]\n"
                      "   /Resources << /XObject << /Im%d %d 0 R >> >>\n"
                      "   /Contents %d 0 R >>\n", pw, ph, i, 4+i*3, 5+i*3);
        endObj();

        // Image XObject
        startObj(4+i*3);
        fprintf(fOut, "<< /Type /XObject /Subtype /Image /Width %d /Height %d\n"
                      "   /ColorSpace /DeviceRGB /BitsPerComponent 8\n"
                      "   /Filter /DCTDecode /Length %zu >>\nstream\n",
                pages[i].w, pages[i].h, pages[i].jpeg.size());
        fwrite(pages[i].jpeg.data(), 1, pages[i].jpeg.size(), fOut);
        fprintf(fOut, "\nendstream\n"); endObj();

        // Content stream
        char cs[128]; snprintf(cs, sizeof(cs), "q\n%.2f 0 0 %.2f 0 0 cm\n/Im%d Do\nQ\n", pw, ph, i);
        startObj(5+i*3);
        fprintf(fOut, "<< /Length %zu >>\nstream\n%s\nendstream\n", strlen(cs), cs); endObj();
    }

    int total = 3 + N*3;
    long xOff = ftell(fOut);
    fprintf(fOut, "xref\n0 %d\n0000000000 65535 f \n", total);
    for (int i = 1; i < total; i++)
        fprintf(fOut, "%010ld 00000 n \n", offsets.count(i) ? offsets[i] : 0L);
    fprintf(fOut, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n", total, xOff);
    fclose(fOut);

    ShellExecuteW(NULL, L"open", outPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

// ============================================================
// PDF SPLIT / EXTRACT  (raw PDF parser — same as in-app version)
// ============================================================

static long FindBytes(const std::vector<uint8_t>& b, const std::string& n, long p = 0) {
    auto it = std::search(b.begin()+p, b.end(), n.begin(), n.end());
    return (it == b.end()) ? -1L : (long)(it - b.begin());
}
static bool GetObjRange(const std::vector<uint8_t>& b, long off, long& s, long& e) {
    long ep = FindBytes(b, "endobj", off);
    if (ep < 0) return false;
    s = off; e = ep + 6; return true;
}
static long ParseStartXref(const std::vector<uint8_t>& b) {
    std::string tag = "startxref";
    long pos = (long)b.size() - 1;
    for (; pos >= (long)tag.size(); pos--)
        if (!memcmp(b.data()+pos-(long)tag.size()+1, tag.c_str(), tag.size())) { pos=pos-(long)tag.size()+1; break; }
    if (pos < 0) return -1;
    long p = pos + (long)tag.size();
    while (p < (long)b.size() && (b[p]==' '||b[p]=='\r'||b[p]=='\n')) p++;
    long v = 0;
    while (p < (long)b.size() && b[p]>='0'&&b[p]<='9') v=v*10+(b[p++]-'0');
    return v;
}
static std::map<int,long> ParseXref(const std::vector<uint8_t>& b, long xo) {
    std::map<int,long> r; long p = xo;
    while (p<(long)b.size()&&(b[p]=='x'||b[p]=='r'||b[p]=='e'||b[p]=='f')) p++;
    while (p<(long)b.size()&&(b[p]==' '||b[p]=='\r'||b[p]=='\n')) p++;
    while (p<(long)b.size()) {
        if (p+7<(long)b.size()&&!memcmp(b.data()+p,"trailer",7)) break;
        if (b[p]<'0'||b[p]>'9') break;
        int first=0; while(p<(long)b.size()&&b[p]>='0'&&b[p]<='9') first=first*10+(b[p++]-'0');
        while(p<(long)b.size()&&b[p]==' ') p++;
        int cnt=0; while(p<(long)b.size()&&b[p]>='0'&&b[p]<='9') cnt=cnt*10+(b[p++]-'0');
        while(p<(long)b.size()&&(b[p]=='\r'||b[p]=='\n')) p++;
        for(int i=0;i<cnt&&p+20<=(long)b.size();i++,p+=20){
            long off=0; for(int c=0;c<10;c++) off=off*10+(b[p+c]-'0');
            if(b[p+17]=='n') r[first+i]=off;
        }
    }
    return r;
}
static void CollectRefs(const std::vector<uint8_t>& src,
                        const std::map<int,long>& xref,
                        int obj, std::set<int>& vis) {
    if (vis.count(obj)||!xref.count(obj)) return;
    vis.insert(obj);
    long s,e; if(!GetObjRange(src,xref.at(obj),s,e)) return;
    for(long i=s;i<e-4;i++){
        if(src[i]>='1'&&src[i]<='9'){
            long j=i; int ref=0;
            while(j<e&&src[j]>='0'&&src[j]<='9') ref=ref*10+(src[j++]-'0');
            if(j<e&&src[j]==' '){j++;while(j<e&&src[j]>='0'&&src[j]<='9')j++;
                if(j<e&&src[j]==' '&&j+1<e&&src[j+1]=='R') CollectRefs(src,xref,ref,vis);}
        }
    }
}
static std::vector<int> GetPageObjs(const std::vector<uint8_t>& src,
                                     const std::map<int,long>& xref,
                                     int& pagesRoot) {
    int cat=-1;
    for(auto& kv:xref){long s,e;if(!GetObjRange(src,kv.second,s,e))continue;
        long ce=(e-s<300)?e:s+300;
        std::string c(src.begin()+s,src.begin()+ce);
        if(c.find("/Type /Catalog")!=std::string::npos||c.find("/Type/Catalog")!=std::string::npos){cat=kv.first;break;}}
    if(cat<0) return {};
    long cs,ce; if(!GetObjRange(src,xref.at(cat),cs,ce)) return {};
    std::string catS(src.begin()+cs,src.begin()+ce);
    size_t pp=catS.find("/Pages "); if(pp==std::string::npos) pp=catS.find("/Pages\n");
    if(pp==std::string::npos) return {};
    pp+=7; int pObj=0;
    while(pp<catS.size()&&catS[pp]>='0'&&catS[pp]<='9') pObj=pObj*10+(catS[pp++]-'0');
    pagesRoot=pObj; if(!xref.count(pObj)) return {};
    std::vector<int> pages; std::vector<int> q={pObj}; std::set<int> vis;
    while(!q.empty()){
        int cur=q.back();q.pop_back(); if(vis.count(cur)) continue; vis.insert(cur);
        if(!xref.count(cur)) continue;
        long s,e; if(!GetObjRange(src,xref.at(cur),s,e)) continue;
        std::string c(src.begin()+s,src.begin()+e);
        bool isPage=(c.find("/Type /Page\n")!=std::string::npos||
                     c.find("/Type /Page ")!=std::string::npos||
                     c.find("/Type/Page")!=std::string::npos);
        bool isPgs =(c.find("/Type /Pages")!=std::string::npos||
                     c.find("/Type/Pages")!=std::string::npos);
        if(isPage) pages.push_back(cur);
        else if(isPgs){
            size_t kp=c.find("/Kids"); if(kp!=std::string::npos){
                kp+=5; while(kp<c.size()&&c[kp]!='[')kp++; kp++;
                while(kp<c.size()&&c[kp]!=']'){
                    if(c[kp]>='0'&&c[kp]<='9'){int kid=0;
                        while(kp<c.size()&&c[kp]>='0'&&c[kp]<='9')kid=kid*10+(c[kp++]-'0');
                        q.push_back(kid);}else kp++;
                }
            }
        }
    }
    return pages;
}
static bool WritePdfSubset(const std::vector<uint8_t>& src,
                            const std::map<int,long>& xref,
                            int pagesRoot,
                            const std::vector<int>& pgObjs,
                            const std::wstring& outPath) {
    FILE* fOut=_wfopen(outPath.c_str(),L"wb"); if(!fOut) return false;
    fprintf(fOut,"%%PDF-1.4\n%%%c%c%c%c\n",0xE2,0xE3,0xCF,0xD3);

    std::set<int> needed;
    for(int pg:pgObjs) CollectRefs(src,xref,pg,needed);
    CollectRefs(src,xref,pagesRoot,needed);
    needed.erase(pagesRoot);

    std::map<int,int> remap; int nxt=3;
    for(int o:needed) if(o!=1) remap[o]=nxt++;
    for(int pg:pgObjs) if(!remap.count(pg)) remap[pg]=nxt++;
    int total=nxt;

    std::map<int,long> offs;
    auto startObj=[&](int n){offs[n]=ftell(fOut);fprintf(fOut,"%d 0 obj\n",n);};
    auto endObj  =[&](){fprintf(fOut,"endobj\n\n");};

    auto rewriteObj=[&](int newN,const std::vector<uint8_t>& ob){
        offs[newN]=ftell(fOut);
        fprintf(fOut,"%d 0 obj\n",newN);
        size_t bs=0;
        for(size_t i=0;i+3<ob.size();i++)
            if(ob[i]=='o'&&ob[i+1]=='b'&&ob[i+2]=='j'){bs=i+3;while(bs<ob.size()&&(ob[bs]=='\r'||ob[bs]=='\n'))bs++;break;}
        size_t be=ob.size();
        for(size_t i=ob.size();i>=6;i--)
            if(!memcmp(ob.data()+i-6,"endobj",6)){be=i-6;break;}
        size_t i=bs;
        while(i<be){
            bool did=false;
            if(ob[i]>='1'&&ob[i]<='9'){
                size_t j=i; int old=0;
                while(j<be&&ob[j]>='0'&&ob[j]<='9')old=old*10+(ob[j++]-'0');
                if(j<be&&ob[j]==' '){size_t k=j+1;int gen=0;
                    while(k<be&&ob[k]>='0'&&ob[k]<='9')gen=gen*10+(ob[k++]-'0');
                    if(k<be&&ob[k]==' '&&k+1<be&&ob[k+1]=='R'){
                        int nr=(old==pagesRoot)?2:(remap.count(old)?remap[old]:old);
                        fprintf(fOut,"%d %d R",nr,gen);i=k+2;did=true;}}
            }
            if(!did){fputc(ob[i],fOut);i++;}
        }
        fprintf(fOut,"\nendobj\n\n");
    };

    auto extractObj=[&](int orig)->std::vector<uint8_t>{
        if(!xref.count(orig)) return {};
        long s,e; if(!GetObjRange(src,xref.at(orig),s,e)) return {};
        return std::vector<uint8_t>(src.begin()+s,src.begin()+e);
    };

    // Obj 1 Catalog
    offs[1]=ftell(fOut); fprintf(fOut,"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n\n");
    // Obj 2 Pages
    offs[2]=ftell(fOut); fprintf(fOut,"2 0 obj\n<< /Type /Pages /Kids [");
    for(size_t pi=0;pi<pgObjs.size();pi++){if(pi)fprintf(fOut," ");fprintf(fOut,"%d 0 R",remap.count(pgObjs[pi])?remap[pgObjs[pi]]:pgObjs[pi]+2);}
    fprintf(fOut,"] /Count %zu >>\nendobj\n\n",pgObjs.size());

    for(int orig:needed){if(orig==1||orig==pagesRoot||!remap.count(orig))continue;auto ob=extractObj(orig);if(!ob.empty())rewriteObj(remap[orig],ob);}
    for(int pg:pgObjs){if(!remap.count(pg))continue;auto ob=extractObj(pg);if(!ob.empty())rewriteObj(remap[pg],ob);}

    long xo=ftell(fOut);
    fprintf(fOut,"xref\n0 %d\n0000000000 65535 f \n",total);
    for(int i=1;i<total;i++) fprintf(fOut,"%010ld 00000 n \n",offs.count(i)?offs[i]:0L);
    fprintf(fOut,"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n",total,xo);
    fclose(fOut); return true;
}

static std::set<int> ParsePageRange(const std::wstring& s, int count) {
    std::set<int> r; std::wstringstream ss(s); std::wstring tok;
    while(std::getline(ss,tok,L',')){
        while(!tok.empty()&&tok.front()==L' ')tok.erase(0,1);
        while(!tok.empty()&&tok.back()==L' ')tok.pop_back();
        size_t d=tok.find(L'-');
        if(d!=std::wstring::npos){int lo=_wtoi(tok.substr(0,d).c_str()),hi=_wtoi(tok.substr(d+1).c_str());
            for(int i=lo;i<=hi;i++)if(i>=1&&i<=count)r.insert(i-1);}
        else{int pg=_wtoi(tok.c_str());if(pg>=1&&pg<=count)r.insert(pg-1);}
    }
    return r;
}

static void DoSplitPdf(HWND hw, const std::wstring& pdfPath, bool splitAll) {
    auto src = ReadFileBytes(pdfPath);
    if (src.empty()) { MessageBoxW(hw,L"Cannot read PDF.",L"Error",MB_OK|MB_ICONERROR); return; }
    long xo = ParseStartXref(src);
    if (xo<0) { MessageBoxW(hw,L"Cannot parse PDF.",L"Error",MB_OK|MB_ICONERROR); return; }
    auto xref = ParseXref(src, xo);
    int pRoot=-1;
    auto allPages = GetPageObjs(src,xref,pRoot);
    if (allPages.empty()) { MessageBoxW(hw,L"No pages found.",L"Error",MB_OK|MB_ICONERROR); return; }
    int pc = (int)allPages.size();

    std::wstring base = pdfPath.substr(0, pdfPath.rfind(L'.'));

    if (splitAll) {
        if (MessageBoxW(hw,(L"Split "+std::to_wstring(pc)+L" pages into separate PDFs?").c_str(),
                        L"Split PDF",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
        int ok=0;
        for(int i=0;i<pc;i++){
            std::wstring op=base+L"_page"+std::to_wstring(i+1)+L".pdf";
            if(WritePdfSubset(src,xref,pRoot,{allPages[i]},op)) ok++;
        }
        MessageBoxW(hw,(std::to_wstring(ok)+L"/"+std::to_wstring(pc)+L" pages split.").c_str(),
                    L"Split Complete",MB_OK|MB_ICONINFORMATION);
    } else {
        // Extract pages — get range via input dialog
        // Use a simple InputBox via TaskDialog as fallback (no .rc needed)
        wchar_t rangeBuf[64] = {};
        swprintf_s(rangeBuf, L"1-%d", pc);

        // Simple dialog using Win32
        struct RangeDlg {
            static INT_PTR CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
                if (m == WM_INITDIALOG) {
                    SetWindowLongPtrW(h, DWLP_USER, l);
                    return TRUE;
                }
                if (m == WM_COMMAND) {
                    if (LOWORD(w) == IDOK) {
                        wchar_t* buf = (wchar_t*)GetWindowLongPtrW(h, DWLP_USER);
                        GetDlgItemTextW(h, 102, buf, 64);
                        EndDialog(h, IDOK);
                    } else if (LOWORD(w) == IDCANCEL) {
                        EndDialog(h, IDCANCEL);
                    }
                    return TRUE;
                }
                return FALSE;
            }
        };

        // Build dialog in memory
        #pragma pack(push, 1)
        struct { DLGTEMPLATE dt; WORD menu,cls; wchar_t title[20];
                 WORD pt; wchar_t font[9];
                 // items
                 WORD align1;
                 DLGITEMTEMPLATE lbl; WORD lc,lcc; wchar_t lt[60]; WORD lx;
                 WORD align2;
                 DLGITEMTEMPLATE edt; WORD ec,ecc; wchar_t et[1]; WORD ex;
                 WORD align3;
                 DLGITEMTEMPLATE ok_; WORD oc,occ; wchar_t ot[3]; WORD ox;
                 WORD align4;
                 DLGITEMTEMPLATE cn_; WORD cc,ccc; wchar_t ct[7]; WORD cx;
        } dlgTpl = {};
        #pragma pack(pop)
        // This approach is complex; use simpler CreateWindowEx method instead
        // Fall back to using a simple MessageBox-style call asking for range
        std::wstring hint = std::wstring(L"PDF has ") + std::to_wstring(pc) + L" pages.\n"
                          + L"Enter range (e.g. 1-3,5,7):\n\n"
                          + L"(Leave default for all pages)";
        // Show via TaskDialog input or just use a simple fixed approach:
        // We'll open the output PDF with a default range (all pages) and let user rename
        // But better: use a quick dialog
        std::wstring rangeStr(rangeBuf);

        // Simple CreateWindowEx dialog (reuse pattern from in-app)
        HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,
            L"#32770", L"Extract PDF Pages",
            WS_POPUP|WS_CAPTION|WS_SYSMENU, 0,0,420,140, hw, NULL, g_hModule, NULL);
        if (hDlg) {
            RECT pr,dr; GetWindowRect(hw,&pr); GetWindowRect(hDlg,&dr);
            SetWindowPos(hDlg,NULL,
                pr.left+(pr.right-pr.left)/2-(dr.right-dr.left)/2,
                pr.top +(pr.bottom-pr.top)/2 -(dr.bottom-dr.top)/2,
                0,0,SWP_NOSIZE|SWP_NOZORDER);
            HFONT hF=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
            std::wstring lbTxt = L"PDF has "+std::to_wstring(pc)+L" pages. Enter range (e.g. 1-3,5,7):";
            HWND hL=CreateWindowExW(0,L"STATIC",lbTxt.c_str(),WS_CHILD|WS_VISIBLE|SS_LEFT,10,10,390,30,hDlg,(HMENU)101,NULL,NULL);
            SendMessageW(hL,WM_SETFONT,(WPARAM)hF,TRUE);
            HWND hE=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",rangeStr.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,10,48,390,22,hDlg,(HMENU)102,NULL,NULL);
            SendMessageW(hE,WM_SETFONT,(WPARAM)hF,TRUE);
            SendMessageW(hE,EM_SETSEL,0,-1);
            HWND hO=CreateWindowExW(0,L"BUTTON",L"OK",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,220,82,80,26,hDlg,(HMENU)IDOK,NULL,NULL);
            SendMessageW(hO,WM_SETFONT,(WPARAM)hF,TRUE);
            HWND hC=CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,312,82,80,26,hDlg,(HMENU)IDCANCEL,NULL,NULL);
            SendMessageW(hC,WM_SETFONT,(WPARAM)hF,TRUE);

            static HWND s_editWnd;  static bool s_done, s_ok;
            s_editWnd=hE; s_done=false; s_ok=false;
            struct Cb {
                static LRESULT CALLBACK DlgProc(HWND h,UINT m,WPARAM w,LPARAM) {
                    if(m==WM_COMMAND){WORD id=LOWORD(w);
                        if(id==IDOK||id==IDCANCEL){s_ok=(id==IDOK);s_done=true;DestroyWindow(h);return 0;}}
                    return DefWindowProcW(h,m,w,0);
                }
                static LRESULT CALLBACK EditProc(HWND h,UINT m,WPARAM w,LPARAM l) {
                    if(m==WM_KEYDOWN){HWND hd=(HWND)GetPropW(h,L"PD");
                        if(hd){if(w==VK_RETURN){PostMessageW(hd,WM_COMMAND,IDOK,0);return 0;}
                               if(w==VK_ESCAPE){PostMessageW(hd,WM_COMMAND,IDCANCEL,0);return 0;}}}
                    return CallWindowProcW((WNDPROC)GetPropW(h,L"OP"),h,m,w,l);
                }
            };
            SetWindowLongPtrW(hDlg,GWLP_WNDPROC,(LONG_PTR)Cb::DlgProc);
            SetPropW(hE,L"PD",(HANDLE)hDlg);
            WNDPROC old=(WNDPROC)SetWindowLongPtrW(hE,GWLP_WNDPROC,(LONG_PTR)Cb::EditProc);
            SetPropW(hE,L"OP",(HANDLE)old);
            SetFocus(hE); ShowWindow(hDlg,SW_SHOW); UpdateWindow(hDlg);

            MSG msg;
            while(!s_done&&GetMessageW(&msg,NULL,0,0)){
                if(!IsWindow(hDlg))break;
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if(!s_ok) return;
            wchar_t buf[64]={};
            GetWindowTextW(hE,buf,64);
            rangeStr=buf;
        }

        auto idx = ParsePageRange(rangeStr, pc);
        if (idx.empty()) { MessageBoxW(hw,L"No valid pages.",L"Error",MB_OK|MB_ICONERROR); return; }

        std::vector<int> sel;
        for(int i:idx) sel.push_back(allPages[i]);

        std::wstring dir = pdfPath.substr(0, pdfPath.rfind(L'\\'));
        std::wstring op = PromptSaveW(hw,
            L"PDF Files\0*.pdf\0All Files\0*.*\0",L"pdf",
            L"Save Extracted Pages As", dir);
        if (op.empty()) return;

        if (WritePdfSubset(src,xref,pRoot,sel,op))
            ShellExecuteW(NULL,L"open",op.c_str(),NULL,NULL,SW_SHOWNORMAL);
        else
            MessageBoxW(hw,L"Failed to write PDF.",L"Error",MB_OK|MB_ICONERROR);
    }
}

// ============================================================
// Context menu command IDs
// ============================================================
enum {
    ID_COPY        = 1,
    ID_CUT         = 2,
    ID_PASTE       = 3,
    ID_ZIP         = 4,
    ID_UNZIP       = 5,
    ID_MERGE_PDF   = 6,
    ID_IMG_PDF     = 7,
    ID_PDF_SPLIT   = 8,
    ID_PDF_EXTRACT = 9,
};

// ============================================================
// IClassFactory
// ============================================================

class CRasFocusShellExt;

class CClassFactory : public IClassFactory {
    LONG m_ref = 1;
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid==IID_IUnknown||riid==IID_IClassFactory){*ppv=this;AddRef();return S_OK;}
        *ppv=nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()  { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() {
        LONG r=InterlockedDecrement(&m_ref); if(!r) delete this; return r;
    }
    STDMETHODIMP CreateInstance(IUnknown* pUnk, REFIID riid, void** ppv);
    STDMETHODIMP LockServer(BOOL) { return S_OK; }
};

// ============================================================
// Shell Extension class
// ============================================================

class CRasFocusShellExt : public IShellExtInit, public IContextMenu {
    LONG                      m_ref = 1;
    std::vector<std::wstring> m_paths;   // all selected paths
    std::vector<std::wstring> m_pdfs;
    std::vector<std::wstring> m_imgs;
    bool                      m_hasZip  = false;
    bool                      m_isDir   = false;   // any directory selected

public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid==IID_IUnknown||riid==IID_IShellExtInit)
            { *ppv=(IShellExtInit*)this; AddRef(); return S_OK; }
        if (riid==IID_IContextMenu)
            { *ppv=(IContextMenu*)this; AddRef(); return S_OK; }
        *ppv=nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()  { InterlockedIncrement(&g_refCount); return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() {
        InterlockedDecrement(&g_refCount);
        LONG r=InterlockedDecrement(&m_ref); if(!r) delete this; return r;
    }

    // IShellExtInit
    STDMETHODIMP Initialize(PCIDLIST_ABSOLUTE, IDataObject* pdo, HKEY) {
        m_paths.clear(); m_pdfs.clear(); m_imgs.clear();
        m_hasZip=false; m_isDir=false;
        if (!pdo) return E_INVALIDARG;

        FORMATETC fe = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM stg = {};
        if (FAILED(pdo->GetData(&fe, &stg))) return E_FAIL;

        HDROP hDrop = (HDROP)GlobalLock(stg.hGlobal);
        UINT count  = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < count; i++) {
            wchar_t buf[MAX_PATH];
            DragQueryFileW(hDrop, i, buf, MAX_PATH);
            std::wstring p = buf;
            m_paths.push_back(p);
            DWORD attr = GetFileAttributesW(p.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                m_isDir = true;
            else {
                if (IsPdfW(p)) m_pdfs.push_back(p);
                if (IsImgW(p)) m_imgs.push_back(p);
                if (IsZipW(p)) m_hasZip = true;
            }
        }
        GlobalUnlock(stg.hGlobal);
        ReleaseStgMedium(&stg);
        return S_OK;
    }

    // IContextMenu
    STDMETHODIMP QueryContextMenu(HMENU hMenu, UINT idx, UINT idFirst, UINT /*idLast*/, UINT uFlags) {
        if (uFlags & CMF_DEFAULTONLY) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

        // Build "RasFocus →" submenu
        HMENU hSub = CreatePopupMenu();
        UINT  pos  = 0;

        // Copy / Cut / Paste (always)
        {
            std::wstring copyLbl = (m_paths.size()==1)
                ? L"Copy \"" + m_paths[0].substr(m_paths[0].rfind(L'\\')+1) + L"\""
                : L"Copy " + std::to_wstring(m_paths.size()) + L" items";
            std::wstring cutLbl  = (m_paths.size()==1)
                ? L"Cut \"" + m_paths[0].substr(m_paths[0].rfind(L'\\')+1) + L"\""
                : L"Cut " + std::to_wstring(m_paths.size()) + L" items";
            InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_STRING, idFirst+ID_COPY, copyLbl.c_str());
            InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_STRING, idFirst+ID_CUT,  cutLbl.c_str());
        }
        // Paste (always visible, grayed if empty)
        {
            std::wstring pasteLabel = (g_clipOp != FmOp::None && !g_clipPaths.empty())
                ? L"Paste here (" + std::to_wstring(g_clipPaths.size()) +
                  (g_clipOp==FmOp::Cut ? L" item(s) — Move)" : L" item(s) — Copy)")
                : L"Paste here";
            UINT gray = (g_clipOp==FmOp::None||g_clipPaths.empty()) ? MF_GRAYED : 0;
            InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_STRING|gray, idFirst+ID_PASTE, pasteLabel.c_str());
        }
        InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_SEPARATOR, 0, nullptr);

        // Zip
        {
            std::wstring zipLbl = (m_paths.size()==1)
                ? L"Zip \"" + m_paths[0].substr(m_paths[0].rfind(L'\\')+1) + L"\""
                : L"Zip " + std::to_wstring(m_paths.size()) + L" items";
            InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_STRING, idFirst+ID_ZIP, zipLbl.c_str());
        }
        if (m_hasZip && m_paths.size()==1)
            InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_STRING, idFirst+ID_UNZIP, L"Extract here...");

        // PDF tools (only when no dirs selected)
        if (!m_isDir) {
            if (m_pdfs.size() >= 2) {
                std::wstring lbl = L"Merge " + std::to_wstring(m_pdfs.size()) + L" PDFs \u2192 single PDF";
                InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_SEPARATOR, 0, nullptr);
                InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_STRING, idFirst+ID_MERGE_PDF, lbl.c_str());
            }
            if (!m_imgs.empty()) {
                std::wstring lbl = L"Convert " + std::to_wstring(m_imgs.size()) +
                                   (m_imgs.size()==1 ? L" image" : L" images") + L" \u2192 PDF";
                InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_STRING, idFirst+ID_IMG_PDF, lbl.c_str());
            }
            if (m_pdfs.size()==1 && m_paths.size()==1) {
                InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_SEPARATOR, 0, nullptr);
                InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_STRING, idFirst+ID_PDF_SPLIT,   L"Split PDF (one file per page)");
                InsertMenuW(hSub, pos++, MF_BYPOSITION|MF_STRING, idFirst+ID_PDF_EXTRACT, L"Extract pages from PDF...");
            }
        }

        // Insert parent menu item "RasFocus →"
        InsertMenuW(hMenu, idx, MF_BYPOSITION|MF_STRING|MF_POPUP,
                    (UINT_PTR)hSub, L"RasFocus \u2192");

        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, ID_PDF_EXTRACT + 1);
    }

    STDMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* pici) {
        if (HIWORD(pici->lpVerb)) return E_INVALIDARG;
        UINT id = LOWORD(pici->lpVerb);
        HWND hw = pici->hwnd;

        // Determine current folder (folder of first selected item)
        std::wstring folder;
        if (!m_paths.empty()) {
            folder = m_paths[0];
            size_t sl = folder.rfind(L'\\');
            if (sl != std::wstring::npos) folder = folder.substr(0, sl + 1);
        }

        switch (id) {
        case ID_COPY:  DoCopy(m_paths, false); break;
        case ID_CUT:   DoCopy(m_paths, true);  break;
        case ID_PASTE: DoPaste(hw, folder);    break;
        case ID_ZIP:   DoZip(hw, m_paths, folder); break;
        case ID_UNZIP:
            if (!m_paths.empty()) DoUnzip(hw, m_paths[0]);
            break;
        case ID_MERGE_PDF:
            if (m_pdfs.size() >= 2) DoMergePDFs(hw, m_pdfs, folder);
            break;
        case ID_IMG_PDF:
            if (!m_imgs.empty()) DoImagesToPdf(hw, m_imgs, folder);
            break;
        case ID_PDF_SPLIT:
            if (!m_pdfs.empty()) DoSplitPdf(hw, m_pdfs[0], true);
            break;
        case ID_PDF_EXTRACT:
            if (!m_pdfs.empty()) DoSplitPdf(hw, m_pdfs[0], false);
            break;
        }
        return S_OK;
    }

    STDMETHODIMP GetCommandString(UINT_PTR id, UINT flags, UINT*, LPSTR str, UINT max) {
        if (flags == GCS_HELPTEXT) {
            const wchar_t* tips[] = {
                L"Copy selected items",
                L"Cut selected items",
                L"Paste clipboard items here",
                L"Compress to ZIP",
                L"Extract ZIP contents",
                L"Merge PDF files into one",
                L"Convert images to PDF",
                L"Split PDF into one page per file",
                L"Extract selected pages from PDF",
            };
            if (id >= 1 && id <= 9 && (flags & GCS_UNICODE))
                wcsncpy_s((LPWSTR)str, max/sizeof(wchar_t), tips[id-1], _TRUNCATE);
        }
        return S_OK;
    }
};

STDMETHODIMP CClassFactory::CreateInstance(IUnknown* pUnk, REFIID riid, void** ppv) {
    if (pUnk) return CLASS_E_NOAGGREGATION;
    auto* obj = new(std::nothrow) CRasFocusShellExt();
    if (!obj) return E_OUTOFMEMORY;
    HRESULT hr = obj->QueryInterface(riid, ppv);
    obj->Release();
    return hr;
}

// ============================================================
// DLL Entry Points
// ============================================================

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hInst;
        DisableThreadLibraryCalls(hInst);
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&g_gdipToken, &si, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_gdipToken) Gdiplus::GdiplusShutdown(g_gdipToken);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow() {
    return (g_refCount == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (rclsid != CLSID_RasFocusShellExt) return CLASS_E_CLASSNOTAVAILABLE;
    auto* cf = new(std::nothrow) CClassFactory();
    if (!cf) return E_OUTOFMEMORY;
    HRESULT hr = cf->QueryInterface(riid, ppv);
    cf->Release();
    return hr;
}

STDAPI DllRegisterServer() {
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);

    // Register CLSID
    wchar_t clsidKey[128] = L"CLSID\\{A7B3C2D1-E4F5-4A6B-8C9D-0E1F2A3B4C5D}";
    HKEY hk;
    RegCreateKeyExW(HKEY_CLASSES_ROOT, clsidKey, 0, NULL, 0,
                    KEY_WRITE, NULL, &hk, NULL);
    RegSetValueExW(hk, NULL, 0, REG_SZ, (BYTE*)L"RasFocus Shell Extension",
                   sizeof(L"RasFocus Shell Extension"));
    RegCloseKey(hk);

    std::wstring inProc = std::wstring(clsidKey) + L"\\InprocServer32";
    RegCreateKeyExW(HKEY_CLASSES_ROOT, inProc.c_str(), 0, NULL, 0,
                    KEY_WRITE, NULL, &hk, NULL);
    RegSetValueExW(hk, NULL, 0, REG_SZ, (BYTE*)dllPath,
                   (DWORD)(wcslen(dllPath)+1)*sizeof(wchar_t));
    RegSetValueExW(hk, L"ThreadingModel", 0, REG_SZ, (BYTE*)L"Apartment",
                   sizeof(L"Apartment"));
    RegCloseKey(hk);

    // Register for all file types (*) and folders
    const wchar_t* regTargets[] = {
        L"*\\shellex\\ContextMenuHandlers\\RasFocus",
        L"Folder\\shellex\\ContextMenuHandlers\\RasFocus",
        L"Directory\\shellex\\ContextMenuHandlers\\RasFocus",
        L"Directory\\Background\\shellex\\ContextMenuHandlers\\RasFocus",
    };
    const wchar_t* clsidStr = L"{A7B3C2D1-E4F5-4A6B-8C9D-0E1F2A3B4C5D}";
    for (auto* target : regTargets) {
        RegCreateKeyExW(HKEY_CLASSES_ROOT, target, 0, NULL, 0,
                        KEY_WRITE, NULL, &hk, NULL);
        RegSetValueExW(hk, NULL, 0, REG_SZ, (BYTE*)clsidStr,
                       (DWORD)(wcslen(clsidStr)+1)*sizeof(wchar_t));
        RegCloseKey(hk);
    }

    // Notify shell to refresh
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    const wchar_t* regTargets[] = {
        L"*\\shellex\\ContextMenuHandlers\\RasFocus",
        L"Folder\\shellex\\ContextMenuHandlers\\RasFocus",
        L"Directory\\shellex\\ContextMenuHandlers\\RasFocus",
        L"Directory\\Background\\shellex\\ContextMenuHandlers\\RasFocus",
    };
    for (auto* target : regTargets) RegDeleteKeyW(HKEY_CLASSES_ROOT, target);

    RegDeleteKeyW(HKEY_CLASSES_ROOT,
        L"CLSID\\{A7B3C2D1-E4F5-4A6B-8C9D-0E1F2A3B4C5D}\\InprocServer32");
    RegDeleteKeyW(HKEY_CLASSES_ROOT,
        L"CLSID\\{A7B3C2D1-E4F5-4A6B-8C9D-0E1F2A3B4C5D}");

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return S_OK;
}
