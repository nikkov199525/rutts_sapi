#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <sapi.h>
#include <spuihelp.h>
#include <unknwn.h>

#include "RuTtsApi.h"
#include <atomic>
#include <string>

class RuTtsEngine final : public ISpTTSEngine, public ISpObjectWithToken {
    volatile LONG m_ref = 1;
    ISpObjectToken* m_token = nullptr;

    RuTtsApi m_api;

    // Rulex (optional)
    HMODULE m_rulexDll = nullptr;
    void*   m_rulexDb = nullptr;

    using rulexdb_open_fn   = void* (*)(const char* path, int mode);
    using rulexdb_search_fn = int   (*)(void* db, const char* key, char* outBuf, int flags);
    using rulexdb_close_fn  = void  (*)(void* db);

    rulexdb_open_fn   m_rulexdb_open = nullptr;
    rulexdb_search_fn m_rulexdb_search = nullptr;
    rulexdb_close_fn  m_rulexdb_close = nullptr;

public:
    RuTtsEngine();
    ~RuTtsEngine();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE SetObjectToken(ISpObjectToken* pToken) override;
    HRESULT STDMETHODCALLTYPE GetObjectToken(ISpObjectToken** ppToken) override;

    HRESULT STDMETHODCALLTYPE Speak(DWORD dwSpeakFlags, REFGUID rguidFormatId, const WAVEFORMATEX* pWaveFormatEx,
                                    const SPVTEXTFRAG* pTextFragList, ISpTTSEngineSite* pOutputSite) override;

    HRESULT STDMETHODCALLTYPE GetOutputFormat(const GUID* pTargetFmtId, const WAVEFORMATEX* pTargetWaveFormatEx,
                                              GUID* pOutputFormatId, WAVEFORMATEX** ppCoMemOutputWaveFormatEx) override;

private:
    void EnsureRulexLoaded(const std::wstring& archDir);
    void UnloadRulex();

    bool HasRulex() const { return m_rulexDb != nullptr && m_rulexdb_search != nullptr; }
    std::wstring RulexSearchWord(const std::wstring& word);
    std::wstring ApplyRulexToRussianWords(const std::wstring& text);
};
