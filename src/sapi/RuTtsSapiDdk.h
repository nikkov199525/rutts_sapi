#pragma once
//
// SAPI engine-side (DDK) declarations, portable across MSVC and mingw/gcc.
//
// The ruTTS SAPI voice implements ISpTTSEngine, whose declaration lives in
// Microsoft's <sapiddk.h>. That header (and the <sphelper.h>/<spuihelp.h>
// helpers this engine used to include) pulls in ATL under MSVC and, worse, is
// simply absent from mingw-w64, which ships <sapi.h> but no engine-side header.
//
// So: on toolchains that have <sapiddk.h> (the Windows SDK / MSVC) we use it
// verbatim. On mingw-w64 we declare the exact subset this engine needs. The
// declarations below are ABI-identical to Microsoft's sapiddk.h (same vtable
// order, same IIDs, same struct layout) -- COM dispatch depends on that, so do
// not reorder or alter them.
//
// The engine uses no ATL helpers, so nothing from sphelper/spuihelp is needed.
// Client-side interfaces (ISpObjectWithToken, ISpObjectToken, ISpDataKey),
// event structs (SPEVENT, SPEI_TTS_BOOKMARK) and SPDFID_WaveFormatEx all come
// from <sapi.h>, which both toolchains provide.

#include <sapi.h>   // SPVSTATE, SPVACTIONS, ISpEventSink, SPDFID_WaveFormatEx, ...

#if defined(__has_include)
#  if __has_include(<sapiddk.h>)
#    define RUTTS_HAVE_SAPIDDK 1
#  endif
#elif defined(_MSC_VER)
#  define RUTTS_HAVE_SAPIDDK 1
#endif

#ifdef RUTTS_HAVE_SAPIDDK

#include <sapiddk.h>

#else  // ---- mingw-w64: vendored engine-side declarations ----------------

// SPVSKIPTYPE / SPVESACTIONS (sapiddk.h "itf_0000_0009")
typedef enum SPVSKIPTYPE {
    SPVST_SENTENCE = (1L << 0)
} SPVSKIPTYPE;

typedef enum SPVESACTIONS {
    SPVES_CONTINUE = 0,
    SPVES_ABORT    = (1L << 0),
    SPVES_SKIP     = (1L << 1),
    SPVES_RATE     = (1L << 2),
    SPVES_VOLUME   = (1L << 3)
} SPVESACTIONS;

// ISpTTSEngineSite : ISpEventSink  (IID 9880499B-CCE9-11D2-B503-00C04F797396)
MIDL_INTERFACE("9880499B-CCE9-11D2-B503-00C04F797396")
ISpTTSEngineSite : public ISpEventSink {
public:
    virtual DWORD STDMETHODCALLTYPE GetActions(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE Write(
        /* [in] */ const void* pBuff,
        /* [in] */ ULONG cb,
        /* [out] */ ULONG* pcbWritten) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRate(
        /* [out] */ long* pRateAdjust) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVolume(
        /* [out] */ USHORT* pusVolume) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSkipInfo(
        /* [out] */ SPVSKIPTYPE* peType,
        /* [out] */ long* plNumItems) = 0;
    virtual HRESULT STDMETHODCALLTYPE CompleteSkip(
        /* [in] */ long ulNumSkipped) = 0;
};
// mingw resolves __uuidof() through __mingw_uuidof<T>(), which needs this
// association (MIDL_INTERFACE expands to a plain struct on mingw, dropping the
// uuid string). Same idiom mingw-w64's own sapi headers use.
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(ISpTTSEngineSite, 0x9880499B, 0xCCE9, 0x11D2, 0xB5, 0x03, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96)
#endif

// SPVTEXTFRAG (sapiddk.h "itf_0000_0010")
typedef struct SPVTEXTFRAG {
    struct SPVTEXTFRAG* pNext;
    SPVSTATE State;
    LPCWSTR pTextStart;
    ULONG ulTextLen;
    ULONG ulTextSrcOffset;
} SPVTEXTFRAG;

// ISpTTSEngine : IUnknown  (IID A74D7C8E-4CC5-4F2F-A6EB-804DEE18500E)
MIDL_INTERFACE("A74D7C8E-4CC5-4F2F-A6EB-804DEE18500E")
ISpTTSEngine : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Speak(
        /* [in] */ DWORD dwSpeakFlags,
        /* [in] */ REFGUID rguidFormatId,
        /* [in] */ const WAVEFORMATEX* pWaveFormatEx,
        /* [in] */ const SPVTEXTFRAG* pTextFragList,
        /* [in] */ ISpTTSEngineSite* pOutputSite) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetOutputFormat(
        /* [in] */ const GUID* pTargetFmtId,
        /* [in] */ const WAVEFORMATEX* pTargetWaveFormatEx,
        /* [out] */ GUID* pOutputFormatId,
        /* [out] */ WAVEFORMATEX** ppCoMemOutputWaveFormatEx) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(ISpTTSEngine, 0xA74D7C8E, 0x4CC5, 0x4F2F, 0xA6, 0xEB, 0x80, 0x4D, 0xEE, 0x18, 0x50, 0x0E)
#endif

#endif  // RUTTS_HAVE_SAPIDDK
