#pragma once
#include <string>

std::wstring GetModuleDir();
std::wstring GetAppDataDir();
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
