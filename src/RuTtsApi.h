#pragma once
#include <windows.h>
#include <string>
#include <cstddef>   // size_t

// Use the official ru_tts.h from $(SolutionDir)src
#include "ru_tts.h"

// Windows GetProcAddress returns FARPROC (unknown calling conv). We store as raw pointers.
using ru_tts_config_init_fn = void (*)(ru_tts_conf_t*);
using ru_tts_transfer_fn = void (*)(const ru_tts_conf_t*, const char*, void*, size_t, ru_tts_callback, void*);

struct RuTtsApi {
	HMODULE dll = nullptr;
	ru_tts_config_init_fn config_init = nullptr;
	ru_tts_transfer_fn transfer = nullptr;

	bool LoadFromDir(const std::wstring& dir);
	void Unload();
};
