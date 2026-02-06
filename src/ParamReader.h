#pragma once

#include <string>
#include "ru_tts.h"

struct RuTtsParams {
  int samples_per_sec = 10000;

  // Optional overrides
  bool has_speech_rate = false;
  int speech_rate = 0;
  bool has_voice_pitch = false;
  int voice_pitch = 0;
  bool has_intonation = false;
  bool intonation = true;

  bool has_general_gap_factor = false;
  double general_gap_factor = 1.0;

  bool has_comma_gap_factor = false;
  double comma_gap_factor = 1.0;
  bool has_dot_gap_factor = false;
  double dot_gap_factor = 1.0;
  bool has_semicolon_gap_factor = false;
  double semicolon_gap_factor = 1.0;
  bool has_colon_gap_factor = false;
  double colon_gap_factor = 1.0;
  bool has_question_gap_factor = false;
  double question_gap_factor = 1.0;
  bool has_exclamation_gap_factor = false;
  double exclamation_gap_factor = 1.0;
};

class ParamReader {
public:
  static RuTtsParams Load();
  static void ApplyToConf(const RuTtsParams& p, ru_tts_conf_t& conf);
  static std::wstring IniPath();
};
