#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <unknwn.h>
// Движковые (DDK) интерфейсы SAPI в переносимом виде: реальный <sapiddk.h> на
// MSVC, встроенный ABI-совместимый сабсет на mingw. Никаких ATL-хелперов
// (<sphelper.h>/<spuihelp.h>) движок не использует.
#include "RuTtsSapiDdk.h"

#include "RuTtsApi.h"
#include "RulexClient.h"
#include <atomic>
#include <string>

class RuTtsEngine final : public ISpTTSEngine, public ISpObjectWithToken {
    volatile LONG m_ref = 1;
    ISpObjectToken* m_token = nullptr;

    RuTtsApi m_api;
    RulexClient m_rulex;

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
    bool HasRulex() const { return m_rulex.HasRulex(); }
};
