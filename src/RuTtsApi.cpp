#include "RuTtsApi.h"
#include "WinUtil.h"

static FARPROC GetProcOrNull(HMODULE m, const char* name) {
    return m ? ::GetProcAddress(m, name) : nullptr;
}

bool RuTtsApi::LoadFromDir(const std::wstring& dir) {
    if (dll) return true;

    const std::wstring path = JoinPath(dir, L"ru_tts.dll");
    dll = ::LoadLibraryW(path.c_str());
    if (!dll) return false;

#pragma warning(push)
#pragma warning(disable : 4191) // FARPROC -> function pointer (normal practice with GetProcAddress)
    config_init = reinterpret_cast<ru_tts_config_init_fn>(GetProcOrNull(dll, "ru_tts_config_init"));
    transfer = reinterpret_cast<ru_tts_transfer_fn>(GetProcOrNull(dll, "ru_tts_transfer"));
#pragma warning(pop)

    if (!config_init || !transfer) {
        Unload();
        return false;
    }

    return true;
}

void RuTtsApi::Unload() {
    if (dll) {
        ::FreeLibrary(dll);
        dll = nullptr;
    }
    config_init = nullptr;
    transfer = nullptr;
}
