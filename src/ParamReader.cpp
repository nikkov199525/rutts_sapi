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
  try { std::filesystem::create_directories(std::filesystem::path(path).parent_path()); } catch(...) {}

  const char* ini =
    "[Parameters]\r\n"
    "samples_per_sec = 10000\r\n"
    "sapi_samples_per_sec = 22050\r\n"
    "\r\n"
    "comma_gap_factor = 100\r\n"
    "dot_gap_factor = 100\r\n"
    "semicolon_gap_factor = 100\r\n"
    "colon_gap_factor = 100\r\n"
    "question_gap_factor = 100\r\n"
    "exclamation_gap_factor = 100\r\n"
    "intonational_gap_factor = 100\r\n"
    "\r\n"
    "dec_sep_point = False\r\n"
    "dec_sep_comma = True\r\n"
    "use_alternative_voice = False\r\n";

  std::ofstream f(std::filesystem::path(path), std::ios::binary);
  if(!f) return;
  f.write(ini, (std::streamsize)strlen(ini));
}

static bool FileExists(const std::wstring& path){
  DWORD attrs = GetFileAttributesW(path.c_str());
  return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool ReadInt(const std::wstring& ini, const wchar_t* key, int& out){
  wchar_t buf[64]{};
  DWORD n = GetPrivateProfileStringW(L"Parameters", key, L"", buf, 64, ini.c_str());
  if(n==0) return false;
  out = (int)wcstol(buf, nullptr, 10);
  return true;
}

static bool ReadBool(const std::wstring& ini, const wchar_t* key, bool& out){
  wchar_t buf[64]{};
  DWORD n = GetPrivateProfileStringW(L"Parameters", key, L"", buf, 64, ini.c_str());
  if(n==0) return false;

  if(_wcsicmp(buf, L"true")==0 || _wcsicmp(buf, L"yes")==0) { out=true; return true; }
  if(_wcsicmp(buf, L"false")==0 || _wcsicmp(buf, L"no")==0) { out=false; return true; }

  out = (wcstol(buf,nullptr,10)!=0);
  return true;
}

static int ClampInt(int v, int lo, int hi){
  if(v < lo) return lo;
  if(v > hi) return hi;
  return v;
}

RuTtsParams ParamReader::Load(){
  RuTtsParams p{};
  std::wstring ini = IniPath();
  if(!FileExists(ini)) WriteDefaultIni(ini);

  // legacy (оставляем как есть)
  p.samples_per_sec = GetPrivateProfileIntW(L"Parameters", L"samples_per_sec", 10000, ini.c_str());

  // SAPI output format
  p.sapi_samples_per_sec = GetPrivateProfileIntW(L"Parameters", L"sapi_samples_per_sec", 22050, ini.c_str());
  p.sapi_samples_per_sec = ClampInt(p.sapi_samples_per_sec, 8000, 48000);

  int v=0;
  if(ReadInt(ini, L"speech_rate", v)) { p.has_speech_rate = true; p.speech_rate = v; }
  if(ReadInt(ini, L"voice_pitch", v)) { p.has_voice_pitch = true; p.voice_pitch = v; }
  if(ReadInt(ini, L"intonation", v)) { p.has_intonation = true; p.intonation = v; }

  if(ReadInt(ini, L"general_gap_factor", v)) { p.has_general_gap_factor = true; p.general_gap_factor = v; }
  if(ReadInt(ini, L"comma_gap_factor", v)) { p.has_comma_gap_factor = true; p.comma_gap_factor = v; }
  if(ReadInt(ini, L"dot_gap_factor", v)) { p.has_dot_gap_factor = true; p.dot_gap_factor = v; }
  if(ReadInt(ini, L"semicolon_gap_factor", v)) { p.has_semicolon_gap_factor = true; p.semicolon_gap_factor = v; }
  if(ReadInt(ini, L"colon_gap_factor", v)) { p.has_colon_gap_factor = true; p.colon_gap_factor = v; }
  if(ReadInt(ini, L"question_gap_factor", v)) { p.has_question_gap_factor = true; p.question_gap_factor = v; }
  if(ReadInt(ini, L"exclamation_gap_factor", v)) { p.has_exclamation_gap_factor = true; p.exclamation_gap_factor = v; }
  if(ReadInt(ini, L"intonational_gap_factor", v)) { p.has_intonational_gap_factor = true; p.intonational_gap_factor = v; }

  bool b=false;
  int flags = 0;
  bool anyFlag=false;

  if(ReadBool(ini, L"dec_sep_point", b)) { if(b) flags |= DEC_SEP_POINT; anyFlag=true; }
  if(ReadBool(ini, L"dec_sep_comma", b)) { if(b) flags |= DEC_SEP_COMMA; anyFlag=true; }
  if(ReadBool(ini, L"use_alternative_voice", b)) { if(b) flags |= USE_ALTERNATIVE_VOICE; anyFlag=true; }

  if(anyFlag) { p.has_flags = true; p.flags = flags; }

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
  if(p.has_intonational_gap_factor) conf.intonational_gap_factor = p.intonational_gap_factor;

  if(p.has_flags) conf.flags = p.flags;
}
