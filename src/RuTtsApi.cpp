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

    // NVDA bridge
    tts_create = reinterpret_cast<tts_create_fn>(GetProcOrNull(dll, "tts_create"));
    tts_destroy = reinterpret_cast<tts_destroy_fn>(GetProcOrNull(dll, "tts_destroy"));
    tts_speak = reinterpret_cast<tts_speak_fn>(GetProcOrNull(dll, "tts_speak"));
    tts_setVolume = reinterpret_cast<tts_setVolume_fn>(GetProcOrNull(dll, "tts_setVolume"));
    tts_setSpeed = reinterpret_cast<tts_setSpeed_fn>(GetProcOrNull(dll, "tts_setSpeed"));

    // raw fallback
    transfer = reinterpret_cast<ru_tts_transfer_fn>(GetProcOrNull(dll, "ru_tts_transfer"));
#pragma warning(pop)

    if (!config_init) {
        Unload();
        return false;
    }

    // Либо NVDA-мост, либо raw transfer (хотя у тебя в dll есть и то и то)
    if (!HasNvdaBridge() && !transfer) {
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
    tts_create = nullptr;
    tts_destroy = nullptr;
    tts_speak = nullptr;
    tts_setVolume = nullptr;
    tts_setSpeed = nullptr;
    transfer = nullptr;
}
