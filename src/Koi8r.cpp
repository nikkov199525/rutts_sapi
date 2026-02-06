#include "Koi8r.h"
#include <windows.h>

std::string WideToKoi8r(const std::wstring& ws) {
    if (ws.empty()) return {};
    int needed = WideCharToMultiByte(20866, 0, ws.c_str(), (int)ws.size(), nullptr, 0, "?", nullptr);
    if (needed <= 0) return {};
    std::string out(needed, '\0');
    int ok = WideCharToMultiByte(20866, 0, ws.c_str(), (int)ws.size(), out.data(), needed, "?", nullptr);
    if (ok <= 0) return {};
    return out;
}
