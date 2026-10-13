#pragma once

// Ensure SAPI/REGSVR32 can find the standard COM exports by their *undecorated* names.
// regsvr32 looks up: DllRegisterServer / DllUnregisterServer.
// COM loads: DllGetClassObject / DllCanUnloadNow.
//
// MSVC warning LNK4104 requires these exports to be PRIVATE
// (exported from the DLL, but not added to the import library).

#ifdef _WIN64
#pragma comment(linker, "/export:DllGetClassObject,PRIVATE")
#pragma comment(linker, "/export:DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/export:DllRegisterServer,PRIVATE")
#pragma comment(linker, "/export:DllUnregisterServer,PRIVATE")
#else
  // x86 stdcall name decoration
#pragma comment(linker, "/export:DllGetClassObject=_DllGetClassObject@12,PRIVATE")
#pragma comment(linker, "/export:DllCanUnloadNow=_DllCanUnloadNow@0,PRIVATE")
#pragma comment(linker, "/export:DllRegisterServer=_DllRegisterServer@0,PRIVATE")
#pragma comment(linker, "/export:DllUnregisterServer=_DllUnregisterServer@0,PRIVATE")
#endif
