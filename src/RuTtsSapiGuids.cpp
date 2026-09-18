// SAPI GUID definitions for the mingw/gcc build.
//
// mingw-w64's <sapi.h> declares the SAPI data-format GUIDs as plain
// `EXTERN_C const GUID ...;` -- a declaration, not a definition -- and ships no
// import library that defines them. So linking the engine there leaves
// SPDFID_WaveFormatEx undefined. MSVC resolves it from sapi.lib; on mingw we
// emit it ourselves. Interface IIDs (ISpTTSEngine, ISpTTSEngineSite, ...) are
// resolved through __uuidof and need nothing here.
//
// This translation unit is compiled ONLY by the mingw build (build_sapi_mingw.bat);
// the MSVC project links sapi.lib and must not also define the symbol.
#include <windows.h>
#include <sapi.h>

// {C31ADBAE-527F-4FF5-A230-F62BB61FF70C}
EXTERN_C const GUID SPDFID_WaveFormatEx =
    { 0xC31ADBAE, 0x527F, 0x4FF5, { 0xA2, 0x30, 0xF6, 0x2B, 0xB6, 0x1F, 0xF7, 0x0C } };
