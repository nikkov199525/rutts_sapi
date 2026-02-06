#pragma once

// Ensure SAPI/REGSVR32 can find the standard COM exports by their *undecorated* names.
// regsvr32 looks up: DllRegisterServer / DllUnregisterServer.
// COM loads: DllGetClassObject / DllCanUnloadNow.

#ifdef _WIN64
  #pragma comment(linker, "/export:DllGetClassObject")
  #pragma comment(linker, "/export:DllCanUnloadNow")
  #pragma comment(linker, "/export:DllRegisterServer")
  #pragma comment(linker, "/export:DllUnregisterServer")
#else
  // x86 stdcall name decoration
  #pragma comment(linker, "/export:DllGetClassObject=_DllGetClassObject@12")
  #pragma comment(linker, "/export:DllCanUnloadNow=_DllCanUnloadNow@0")
  #pragma comment(linker, "/export:DllRegisterServer=_DllRegisterServer@0")
  #pragma comment(linker, "/export:DllUnregisterServer=_DllUnregisterServer@0")
#endif
