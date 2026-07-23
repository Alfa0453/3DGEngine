#include "NativeDialog.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>   // IFileDialog, IShellItem, FOS_PICKFOLDERS
#include <commdlg.h>    // GetOpenFileNameW / OPENFILENAMEW

namespace {

std::string WideToUtf8(const wchar_t* w) {
    if (!w) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring w(static_cast<std::size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

} // namespace

namespace editor {

std::string PickFolderDialog(const std::string& title) {
    std::string result;

    // GLFW already initialises COM (apartment-threaded) for drag & drop; calling
    // CoInitializeEx again just returns S_FALSE. Only balance with CoUninitialize
    // when *we* actually initialised it (SUCCEEDED covers S_OK and S_FALSE; a
    // changed-mode failure leaves the existing apartment untouched).
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool didInit = SUCCEEDED(initHr);

    IFileDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (!title.empty()) {
            const std::wstring wtitle = Utf8ToWide(title);
            dialog->SetTitle(wtitle.c_str());
        }
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = WideToUtf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    if (didInit) CoUninitialize();
    return result;
}

std::string OpenFileDialog(const std::string& title, const std::string& filterName,
                           const std::string& filterExt) {
    wchar_t file[1024] = L"";

    // Win32 filter strings are a sequence of null-terminated pairs, ending in an
    // extra null: "Name (*.ext)\0*.ext\0All Files (*.*)\0*.*\0\0".
    const std::wstring wname = Utf8ToWide(filterName.empty() ? std::string("File") : filterName);
    const std::wstring wext  = Utf8ToWide(filterExt.empty()  ? std::string("*")    : filterExt);
    std::wstring filter;
    filter += wname + L" (*." + wext + L")"; filter.push_back(L'\0');
    filter += L"*." + wext;                   filter.push_back(L'\0');
    filter += L"All Files (*.*)";             filter.push_back(L'\0');
    filter += L"*.*";                          filter.push_back(L'\0');
    filter.push_back(L'\0');

    const std::wstring wtitle = Utf8ToWide(title);

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFile    = file;
    ofn.nMaxFile     = static_cast<DWORD>(sizeof(file) / sizeof(file[0]));
    ofn.lpstrFilter  = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle   = title.empty() ? nullptr : wtitle.c_str();
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn)) {
        return WideToUtf8(file);
    }
    return {};
}

} // namespace editor

#else  // ---- non-Windows: no native dialog available -----------------------

namespace editor {

std::string PickFolderDialog(const std::string&) { return {}; }
std::string OpenFileDialog(const std::string&, const std::string&, const std::string&) { return {}; }

} // namespace editor

#endif
