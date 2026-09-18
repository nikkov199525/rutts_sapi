#pragma once

#include <windows.h>
#include <mutex>
#include <string>

#include "RulexDllApi.h"

class RulexClient {
public:
    ~RulexClient();

    void EnsureLoaded(const std::wstring& archDir);
    void Unload();

    bool HasRulex() const;
    std::wstring ApplyToRussianWords(const std::wstring& text) const;

private:
    using create_fn = rutts_rulex_handle(RUTTS_RULEX_CALL*)();
    using destroy_fn = void(RUTTS_RULEX_CALL*)(rutts_rulex_handle);
    using ensure_loaded_fn = BOOL(RUTTS_RULEX_CALL*)(rutts_rulex_handle, const wchar_t*);
    using has_rulex_fn = BOOL(RUTTS_RULEX_CALL*)(rutts_rulex_handle);
    using apply_fn = int(RUTTS_RULEX_CALL*)(rutts_rulex_handle, const wchar_t*, wchar_t*, int);

    bool LoadApi(const std::wstring& archDir);

    HMODULE m_dll = nullptr;
    rutts_rulex_handle m_handle = nullptr;
    create_fn m_create = nullptr;
    destroy_fn m_destroy = nullptr;
    ensure_loaded_fn m_ensureLoaded = nullptr;
    has_rulex_fn m_hasRulex = nullptr;
    apply_fn m_apply = nullptr;
    mutable std::mutex m_mutex;
};
