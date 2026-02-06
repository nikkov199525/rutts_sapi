#include <windows.h>
#include <unknwn.h>

#include <atomic>

#include "Exports.h"
#include "Guid.h"
#include "RuTtsSapiEngine.h"
#include "ComReg.h"

std::atomic<long> g_objectCount{0};
static LONG g_lockCount = 0;

class ClassFactory final : public IClassFactory {
  LONG m_ref=1;
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if(!ppv) return E_POINTER;
    *ppv=nullptr;
    if(riid==IID_IUnknown || riid==IID_IClassFactory){ *ppv=(IClassFactory*)this; AddRef(); return S_OK; }
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
  ULONG STDMETHODCALLTYPE Release() override { ULONG r=(ULONG)InterlockedDecrement(&m_ref); if(r==0) delete this; return r; }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
    if(outer) return CLASS_E_NOAGGREGATION;
    auto* obj=new RuTtsEngine();
    HRESULT hr=obj->QueryInterface(riid, ppv);
    obj->Release();
    return hr;
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) override {
    if(fLock) InterlockedIncrement(&g_lockCount);
    else InterlockedDecrement(&g_lockCount);
    return S_OK;
  }
};

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID){
  if(reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
  return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv){
  if(!ppv) return E_POINTER;
  *ppv=nullptr;
  if(clsid != CLSID_ruTTS_sapi) return CLASS_E_CLASSNOTAVAILABLE;
  auto* fac=new ClassFactory();
  HRESULT hr=fac->QueryInterface(riid, ppv);
  fac->Release();
  return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow(){
  // safe enough: COM will keep module loaded while objects exist
  if(g_lockCount!=0) return S_FALSE;
  return (g_objectCount.load(std::memory_order_relaxed)==0) ? S_OK : S_FALSE;
}

extern "C" HRESULT __stdcall DllRegisterServer(){
  return RegisterComServer();
}

extern "C" HRESULT __stdcall DllUnregisterServer(){
  return UnregisterComServer();
}
