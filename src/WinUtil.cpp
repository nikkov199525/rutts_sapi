#include "WinUtil.h"
#include <windows.h>
#include <shlobj.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW((HMODULE)&__ImageBase, path, MAX_PATH);
    std::wstring p(path);
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return p.substr(0, pos);
}

std::wstring GetAppDataDir() {
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path)) && path) {
        out = path;
        CoTaskMemFree(path);
    } else {
        wchar_t buf[MAX_PATH]{};
        GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        out = buf;
    }
    return out;
}
