#include "ComReg.h"
#include "Guid.h"
#include <shlwapi.h>
#include <string>

#pragma comment(lib,"Shlwapi.lib")

extern "C" IMAGE_DOS_HEADER __ImageBase;

static HRESULT WriteRegString(HKEY root, const std::wstring& sub, const std::wstring& name, const std::wstring& val){
  HKEY h=nullptr; if(RegCreateKeyExW(root, sub.c_str(),0,nullptr,0,KEY_WRITE,nullptr,&h,nullptr)!=ERROR_SUCCESS) return E_FAIL;
  const BYTE* data=(const BYTE*)val.c_str(); DWORD cb=(DWORD)((val.size()+1)*sizeof(wchar_t));
  LONG rc=RegSetValueExW(h, name.empty()?nullptr:name.c_str(),0,REG_SZ,data,cb);
  RegCloseKey(h); return (rc==ERROR_SUCCESS)?S_OK:E_FAIL;
}

static HRESULT DeleteKeyTree(HKEY root, const std::wstring& sub){
  LONG rc=SHDeleteKeyW(root, sub.c_str());
  if(rc==ERROR_FILE_NOT_FOUND) return S_OK;
  return (rc==ERROR_SUCCESS)?S_OK:E_FAIL;
}

static std::wstring GuidToString(const GUID& g){ wchar_t buf[64]{}; StringFromGUID2(g,buf,64); return buf; }

// Minimal SAPI voice token registration.
// SAPI enumerates voices from:
//   HKLM\SOFTWARE\Microsoft\Speech\Voices\Tokens
// and reads the "CLSID" value for COM activation.
static HRESULT RegisterSapiToken()
{
  const std::wstring tokenName = L"ruTTS";
  const std::wstring base = L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\" + tokenName;
  const std::wstring clsid = GuidToString(CLSID_ruTTS_sapi);

  // default value helps some clients that display the token name
  HRESULT hr = WriteRegString(HKEY_LOCAL_MACHINE, base, L"", tokenName);
  if(FAILED(hr)) return hr;

  hr = WriteRegString(HKEY_LOCAL_MACHINE, base, L"CLSID", clsid);
  if(FAILED(hr)) return hr;

  const std::wstring attrs = base + L"\\Attributes";
  hr = WriteRegString(HKEY_LOCAL_MACHINE, attrs, L"Name", tokenName);
  if(FAILED(hr)) return hr;
  hr = WriteRegString(HKEY_LOCAL_MACHINE, attrs, L"Vendor", L"ruTTS");
  if(FAILED(hr)) return hr;
  // Language is stored as a hex string (LCID). 0x0419 = Russian.
  hr = WriteRegString(HKEY_LOCAL_MACHINE, attrs, L"Language", L"0419");
  if(FAILED(hr)) return hr;
  // Optional, but common attributes:
  (void)WriteRegString(HKEY_LOCAL_MACHINE, attrs, L"Gender", L"Male");
  (void)WriteRegString(HKEY_LOCAL_MACHINE, attrs, L"Age", L"Adult");
  return S_OK;
}

static HRESULT UnregisterSapiToken()
{
  const std::wstring base = L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\ruTTS";
  return DeleteKeyTree(HKEY_LOCAL_MACHINE, base);
}

HRESULT RegisterComServer(){
  std::wstring clsid=GuidToString(CLSID_ruTTS_sapi);
  std::wstring base=L"CLSID\\"+clsid;
  wchar_t dllPath[MAX_PATH]{}; GetModuleFileNameW((HMODULE)&__ImageBase, dllPath, MAX_PATH);
  HRESULT hr=WriteRegString(HKEY_CLASSES_ROOT, base, L"", L"ruTTS_sapi"); if(FAILED(hr)) return hr;
  hr=WriteRegString(HKEY_CLASSES_ROOT, base+L"\\InprocServer32", L"", dllPath); if(FAILED(hr)) return hr;
  hr = WriteRegString(HKEY_CLASSES_ROOT, base+L"\\InprocServer32", L"ThreadingModel", L"Both");
  if(FAILED(hr)) return hr;
  // Token lives under HKLM, so requires admin.
  return RegisterSapiToken();
}

HRESULT UnregisterComServer(){
  std::wstring clsid=GuidToString(CLSID_ruTTS_sapi);
  HRESULT hr1 = DeleteKeyTree(HKEY_CLASSES_ROOT, L"CLSID\\"+clsid);
  HRESULT hr2 = UnregisterSapiToken();
  return FAILED(hr1) ? hr1 : hr2;
}
