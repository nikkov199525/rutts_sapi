#pragma once
#include <windows.h>
#include <string>
#include <cstddef>

#include "ru_tts.h"

// opaque type from ru_tts_nvda.h (in ru_tts.dll ABI)
struct TTS_t;

using ru_tts_config_init_fn = void (*)(ru_tts_conf_t*);
using ru_tts_transfer_fn = void (*)(const ru_tts_conf_t*, const char*, void*, size_t, ru_tts_callback, void*);

using tts_create_fn = TTS_t * (*)(ru_tts_callback wave_consumer);
using tts_destroy_fn = void (*)(TTS_t* tts);
using tts_speak_fn = void (*)(const TTS_t* tts, const ru_tts_conf_t* config, const char* text);
using tts_setVolume_fn = void (*)(const TTS_t* tts, float volume);
using tts_setSpeed_fn = void (*)(const TTS_t* tts, float speed);

struct RuTtsApi {
    HMODULE dll = nullptr;

    ru_tts_config_init_fn config_init = nullptr;

    // Preferred path (NVDA/sonic wrapper inside ru_tts.dll)
    tts_create_fn    tts_create = nullptr;
    tts_destroy_fn   tts_destroy = nullptr;
    tts_speak_fn     tts_speak = nullptr;
    tts_setVolume_fn tts_setVolume = nullptr;
    tts_setSpeed_fn  tts_setSpeed = nullptr;

    // Fallback (raw ru_tts)
    ru_tts_transfer_fn transfer = nullptr;

    bool LoadFromDir(const std::wstring& dir);
    void Unload();

    bool HasNvdaBridge() const {
        return tts_create && tts_destroy && tts_speak && tts_setVolume && tts_setSpeed;
    }
};
