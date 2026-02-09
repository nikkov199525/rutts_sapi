#pragma once

#include <string>
#include "ru_tts.h"

struct RuTtsParams {
  // ВАЖНО: это частота СИНТЕЗАТОРА (уходит в confT).
  // Должна реально влиять на "поток/питч" внутри ru_tts.dll, как в первой версии.
  int samples_per_sec = 10000;

  // Отдельно: частота ВЫХОДА SAPI (качество/совместимость).
  int sapi_samples_per_sec = 22050;

  // Optional overrides for ru_tts_conf_t
  bool has_speech_rate = false;
  int speech_rate = 0;

  bool has_voice_pitch = false;
  int voice_pitch = 0;

  bool has_intonation = false;
  int intonation = 0;

  bool has_general_gap_factor = false;
  int general_gap_factor = 0;

  bool has_comma_gap_factor = false;
  int comma_gap_factor = 0;
  bool has_dot_gap_factor = false;
  int dot_gap_factor = 0;
  bool has_semicolon_gap_factor = false;
  int semicolon_gap_factor = 0;
  bool has_colon_gap_factor = false;
  int colon_gap_factor = 0;
  bool has_question_gap_factor = false;
  int question_gap_factor = 0;
  bool has_exclamation_gap_factor = false;
  int exclamation_gap_factor = 0;

  bool has_intonational_gap_factor = false;
  int intonational_gap_factor = 0;

  bool has_flags = false;
  int flags = 0;
};

class ParamReader {
public:
  static RuTtsParams Load();
  static void ApplyToConf(const RuTtsParams& p, ru_tts_conf_t& conf);
  static std::wstring IniPath();
};
