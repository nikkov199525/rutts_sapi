#pragma once

#include <windows.h>
#include <array>
#include <string>

class RulexApi {
public:
    ~RulexApi();

    void EnsureLoaded(const std::wstring& archDir);
    void Unload();

    bool HasRulex() const { return m_rulexDb != nullptr && m_rulexdb_search != nullptr; }
    std::wstring SearchWord(const std::wstring& word) const;
    std::wstring ApplyToRussianWords(const std::wstring& text) const;

private:
    HMODULE m_rulexDll = nullptr;
    void* m_rulexDb = nullptr;
    std::string m_dbPathA;
    mutable std::array<char, 256> m_searchBuf{};

    using rulexdb_open_fn = void* (*)(const char* path, int mode);
    using rulexdb_search_fn = int (*)(void* db, const char* key, char* outBuf, int flags);
    using rulexdb_close_fn = void (*)(void* db);

    rulexdb_open_fn m_rulexdb_open = nullptr;
    rulexdb_search_fn m_rulexdb_search = nullptr;
    rulexdb_close_fn m_rulexdb_close = nullptr;
};
