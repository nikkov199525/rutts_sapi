#include "RuTtsApi.h"
#include "WinUtil.h"

bool RuTtsApi::LoadFromDir(const std::wstring& dir){
  if(dll) return true;
  std::wstring path=JoinPath(dir,L"ru_tts.dll");
  dll=LoadLibraryW(path.c_str());
  if(!dll) return false;
  config_init=(ru_tts_config_init_fn)GetProcAddress(dll,"ru_tts_config_init");
  transfer=(ru_tts_transfer_fn)GetProcAddress(dll,"ru_tts_transfer");
  if(!config_init||!transfer){ Unload(); return false; }
  return true;
}

void RuTtsApi::Unload(){ if(dll) FreeLibrary(dll); dll=nullptr; config_init=nullptr; transfer=nullptr; }
