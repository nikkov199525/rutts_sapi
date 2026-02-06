#pragma once
#include <windows.h>
#include <sapi.h>
#include <spuihelp.h>
#include <unknwn.h>
#include "RuTtsApi.h"
#include <atomic>

class RuTtsEngine final : public ISpTTSEngine, public ISpObjectWithToken {
  LONG m_ref=1;
  ISpObjectToken* m_token=nullptr;
  RuTtsApi m_api;
public:
  RuTtsEngine();
  ~RuTtsEngine();

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;

  HRESULT STDMETHODCALLTYPE SetObjectToken(ISpObjectToken* pToken) override;
  HRESULT STDMETHODCALLTYPE GetObjectToken(ISpObjectToken** ppToken) override;

  HRESULT STDMETHODCALLTYPE Speak(DWORD dwSpeakFlags, REFGUID rguidFormatId, const WAVEFORMATEX* pWaveFormatEx,
                                  const SPVTEXTFRAG* pTextFragList, ISpTTSEngineSite* pOutputSite) override;

  HRESULT STDMETHODCALLTYPE GetOutputFormat(const GUID* pTargetFmtId, const WAVEFORMATEX* pTargetWaveFormatEx,
                                            GUID* pOutputFormatId, WAVEFORMATEX** ppCoMemOutputWaveFormatEx) override;
};
