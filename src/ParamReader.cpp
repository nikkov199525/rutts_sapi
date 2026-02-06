#include "ParamReader.h"

#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>

static std::wstring AppDataDir(){
  wchar_t path[MAX_PATH]{};
  if(SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path)))
    return path;
  DWORD n = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
  if(n>0 && n < MAX_PATH) return path;
  return L".";
}

std::wstring ParamReader::IniPath(){
  std::filesystem::path base(AppDataDir());
  base /= L"rutts";
  base /= L"rutts.ini";
  return base.wstring();
}

static void WriteDefaultIni(const std::wstring& path){
  try {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  } catch(...) {}

  // Minimal default ini. All other ru_tts settings stay at library defaults.
  const char* ini =
    "[Parameters]\r\n"
    "# Adapter output sample rate (SAPI output format). ru_tts itself outputs 10000 Hz in this build.\r\n"
    "samples_per_sec = 10000\r\n"
    "\r\n"
    "# Optional overrides for ru_tts.conf fields (only if specified).\r\n"
    "# speech_rate = 120\r\n"
    "# voice_pitch = 45\r\n"
    "# intonation = 1\r\n"
    "# general_gap_factor = 1.0\r\n"
    "# comma_gap_factor = 1.0\r\n"
    "# dot_gap_factor = 1.0\r\n"
    "# semicolon_gap_factor = 1.0\r\n"
    "# colon_gap_factor = 1.0\r\n"
    "# question_gap_factor = 1.0\r\n"
    "# exclamation_gap_factor = 1.0\r\n";

  std::ofstream f(std::filesystem::path(path), std::ios::binary);
  if(!f) return;
  f.write(ini, (std::streamsize)strlen(ini));
}

static bool FileExists(const std::wstring& path){
  DWORD attrs = GetFileAttributesW(path.c_str());
  return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool ReadDouble(const std::wstring& ini, const wchar_t* key, double& out){
  wchar_t buf[64]{};
  DWORD n = GetPrivateProfileStringW(L"Parameters", key, L"", buf, 64, ini.c_str());
  if(n==0) return false;
  out = wcstod(buf, nullptr);
  return true;
}

static bool ReadInt(const std::wstring& ini, const wchar_t* key, int& out){
  wchar_t buf[64]{};
  DWORD n = GetPrivateProfileStringW(L"Parameters", key, L"", buf, 64, ini.c_str());
  if(n==0) return false;
  out = (int)wcstol(buf, nullptr, 10);
  return true;
}

RuTtsParams ParamReader::Load(){
  RuTtsParams p{};
  std::wstring ini = IniPath();
  if(!FileExists(ini)) WriteDefaultIni(ini);

  p.samples_per_sec = GetPrivateProfileIntW(L"Parameters", L"samples_per_sec", 10000, ini.c_str());
  if(p.samples_per_sec < 8000) p.samples_per_sec = 8000;
  if(p.samples_per_sec > 48000) p.samples_per_sec = 48000;

  if(ReadInt(ini, L"speech_rate", p.speech_rate)) p.has_speech_rate = true;
  if(ReadInt(ini, L"voice_pitch", p.voice_pitch)) p.has_voice_pitch = true;

  int i=0;
  if(ReadInt(ini, L"intonation", i)) { p.has_intonation=true; p.intonation = (i!=0); }

  if(ReadDouble(ini, L"general_gap_factor", p.general_gap_factor)) p.has_general_gap_factor = true;

  if(ReadDouble(ini, L"comma_gap_factor", p.comma_gap_factor)) p.has_comma_gap_factor = true;
  if(ReadDouble(ini, L"dot_gap_factor", p.dot_gap_factor)) p.has_dot_gap_factor = true;
  if(ReadDouble(ini, L"semicolon_gap_factor", p.semicolon_gap_factor)) p.has_semicolon_gap_factor = true;
  if(ReadDouble(ini, L"colon_gap_factor", p.colon_gap_factor)) p.has_colon_gap_factor = true;
  if(ReadDouble(ini, L"question_gap_factor", p.question_gap_factor)) p.has_question_gap_factor = true;
  if(ReadDouble(ini, L"exclamation_gap_factor", p.exclamation_gap_factor)) p.has_exclamation_gap_factor = true;

  return p;
}

void ParamReader::ApplyToConf(const RuTtsParams& p, ru_tts_conf_t& conf){
  if(p.has_speech_rate) conf.speech_rate = p.speech_rate;
  if(p.has_voice_pitch) conf.voice_pitch = p.voice_pitch;
  if(p.has_intonation) conf.intonation = p.intonation;

  if(p.has_general_gap_factor) conf.general_gap_factor = p.general_gap_factor;
  if(p.has_comma_gap_factor) conf.comma_gap_factor = p.comma_gap_factor;
  if(p.has_dot_gap_factor) conf.dot_gap_factor = p.dot_gap_factor;
  if(p.has_semicolon_gap_factor) conf.semicolon_gap_factor = p.semicolon_gap_factor;
  if(p.has_colon_gap_factor) conf.colon_gap_factor = p.colon_gap_factor;
  if(p.has_question_gap_factor) conf.question_gap_factor = p.question_gap_factor;
  if(p.has_exclamation_gap_factor) conf.exclamation_gap_factor = p.exclamation_gap_factor;
}
