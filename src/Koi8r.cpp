#include "Koi8r.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <limits>


std::string WideToKoi8r(const std::wstring& ws)
{
    if (ws.empty())
        return {};

    // WideCharToMultiByte принимает длину как int
    if (ws.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return {};

    const int srcLen = static_cast<int>(ws.size());
    const int flags = WC_NO_BEST_FIT_CHARS;

    const int needed = WideCharToMultiByte(
        20866,              // KOI8-R
        flags,
        ws.c_str(),
        srcLen,
        nullptr,
        0,
        "?",
        nullptr
    );

    if (needed <= 0)
        return {};

    std::string out(static_cast<size_t>(needed), '\0');

    const int written = WideCharToMultiByte(
        20866,
        flags,
        ws.c_str(),
        srcLen,
        out.data(),
        needed,
        "?",
        nullptr
    );

    if (written <= 0)
        return {};

    out.resize(static_cast<size_t>(written));
    return out;
}
