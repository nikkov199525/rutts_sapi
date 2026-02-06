#include <atomic>

extern std::atomic<long> g_objectCount;

#include "RuTtsSapiEngine.h"
#include "WinUtil.h"
#include "ParamReader.h"
#include "Koi8r.h"

#include <vector>
#include <string>

struct EngineContext {
  ISpTTSEngineSite* site = nullptr;
  float volume01 = 1.0f;
};

static int __cdecl RuWaveConsumer(void* buffer, size_t size, void* user_data){
  auto* ctx = reinterpret_cast<EngineContext*>(user_data);
  if(!ctx || !ctx->site || !buffer || size == 0) return 1;

  const DWORD act = ctx->site->GetActions();
  if(act & SPVES_ABORT) return 1;

  // ru_tts produces signed 8-bit PCM. SAPI expects PCM; we output 16-bit signed.
  const int8_t* in8 = reinterpret_cast<const int8_t*>(buffer);
  const int frames = static_cast<int>(size);

  static thread_local std::vector<int16_t> out16;
  out16.resize(frames);

  for(int i=0;i<frames;++i){
    float fs = static_cast<float>(static_cast<int16_t>(in8[i]) << 8) * ctx->volume01;
    if(fs > 32767.f) fs = 32767.f;
    if(fs < -32768.f) fs = -32768.f;
    out16[i] = static_cast<int16_t>(fs);
  }

  ULONG written = 0;
  const ULONG cb = static_cast<ULONG>(out16.size() * sizeof(int16_t));
  HRESULT hr = ctx->site->Write(out16.data(), cb, &written);
  return FAILED(hr) ? 1 : 0;
}

RuTtsEngine::RuTtsEngine(){
  g_objectCount.fetch_add(1, std::memory_order_relaxed);
}

RuTtsEngine::~RuTtsEngine(){
  if(m_token){ m_token->Release(); m_token=nullptr; }
  m_api.Unload();
  g_objectCount.fetch_sub(1, std::memory_order_relaxed);
}

HRESULT RuTtsEngine::QueryInterface(REFIID riid, void** ppv){
  if(!ppv) return E_POINTER;
  *ppv = nullptr;

  if(riid == IID_IUnknown || riid == __uuidof(ISpTTSEngine))
    *ppv = static_cast<ISpTTSEngine*>(this);
  else if(riid == __uuidof(ISpObjectWithToken))
    *ppv = static_cast<ISpObjectWithToken*>(this);
  else
    return E_NOINTERFACE;

  AddRef();
  return S_OK;
}

ULONG RuTtsEngine::AddRef(){ return (ULONG)InterlockedIncrement(&m_ref); }

ULONG RuTtsEngine::Release(){
  ULONG r = (ULONG)InterlockedDecrement(&m_ref);
  if(r==0) delete this;
  return r;
}

HRESULT RuTtsEngine::SetObjectToken(ISpObjectToken* pToken){
  if(m_token){ m_token->Release(); m_token=nullptr; }
  if(pToken){ pToken->AddRef(); m_token = pToken; }
  return S_OK;
}

HRESULT RuTtsEngine::GetObjectToken(ISpObjectToken** ppToken){
  if(!ppToken) return E_POINTER;
  *ppToken = m_token;
  if(m_token) m_token->AddRef();
  return S_OK;
}

HRESULT RuTtsEngine::GetOutputFormat(const GUID*, const WAVEFORMATEX*, GUID* pid, WAVEFORMATEX** ppw){
  if(!pid || !ppw) return E_POINTER;

  RuTtsParams params = ParamReader::Load();

  *pid = SPDFID_WaveFormatEx;
  auto* wfx = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
  if(!wfx) return E_OUTOFMEMORY;

  wfx->wFormatTag = WAVE_FORMAT_PCM;
  wfx->nChannels = 1;
  wfx->nSamplesPerSec = (DWORD)params.samples_per_sec;
  wfx->wBitsPerSample = 16;
  wfx->nBlockAlign = (wfx->nChannels * wfx->wBitsPerSample) / 8;
  wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
  wfx->cbSize = 0;

  *ppw = wfx;
  return S_OK;
}

HRESULT RuTtsEngine::Speak(DWORD, REFGUID, const WAVEFORMATEX*, const SPVTEXTFRAG* frags, ISpTTSEngineSite* site){
  if(!site) return E_POINTER;

  RuTtsParams params = ParamReader::Load();

  // ru_tts.dll is expected to live next to this adapter DLL.
  std::wstring dir = GetModuleDir();
  if(!m_api.LoadFromDir(dir)) return E_FAIL;

  std::wstring wtext;
  for(auto* f = frags; f; f = f->pNext){
    if(f->pTextStart && f->ulTextLen)
      wtext.append(f->pTextStart, f->pTextStart + f->ulTextLen);
  }
  if(wtext.empty()) return S_OK;

  std::string koi8 = WideToKoi8r(wtext);
  if(koi8.empty()) return S_OK;

  ru_tts_conf_t conf{};
  m_api.config_init(&conf);
  ParamReader::ApplyToConf(params, conf);

  EngineContext ctx{};
  ctx.site = site;
  ctx.volume01 = 1.0f;

  std::vector<char> waveBuf(4096);
  m_api.transfer(&conf, koi8.c_str(), waveBuf.data(), (unsigned int)waveBuf.size(), RuWaveConsumer, &ctx);
  return S_OK;
}
