#pragma once

#include <string>
#include "ru_tts.h"

enum class UnicodeNormalizationForm {
  NFC,
  NFKC,
  NFD,
  NFKD
};

enum class InterpolationAlgorithm {
  Linear,
  ZeroOrderHold
};

struct RuTtsParams {
  // 0 - не использовать rulex, 1 - использовать rulex
  bool use_rulex = true;
  // 0 - основной (мужской) голос, 1 - альтернативный (женский) голос
  bool use_alternative_voice = false;
  // 0 - без доп. ускорения, 1 - включить ускорение (x2)
  bool use_rate_boost = false;
  // Старый (линейный) алгоритм изменения скорости в ядре. По умолчанию ядро
  // использует адаптивный powf-кроссфейд; флаг USE_LEGACY_RATE_ALGO возвращает
  // прежнее линейное поведение. Понимает только новое ядро ru_tts.dll.
  bool use_legacy_rate_algo = false;

  // Исходное имя параметра проекта: частота дискретизации выходного потока.
  int samples_per_sec = 10000;

  // Множитель апсемплинга: 1/2/4.
  int interpolation_multiplier = 1;
  // Алгоритм интерполяции.
  InterpolationAlgorithm interpolation_algorithm = InterpolationAlgorithm::Linear;
  // Размер исходного буфера ru_tts_transfer (в байтах).
  int wave_buffer_size = 4096;
  // Тишина в начале/конце синтеза (мс).
  int silence_at_begin = 0;
  int silence_at_end = 0;

  // Эффективная выходная частота.
  // Вычисляется как samples_per_sec * interpolation_multiplier.
  int output_sample_rate = 10000;

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

  bool dec_sep_point = false;
  bool dec_sep_comma = true;
  bool has_intonation = false;
  int intonation = 0;

  bool use_unicode_normalization = false;
  UnicodeNormalizationForm unicode_normalization_form = UnicodeNormalizationForm::NFC;
};

class ParamReader {
public:
  static RuTtsParams Load();
  static void ApplyToConf(const RuTtsParams& p, ru_tts_conf_t& conf);
  static std::wstring IniPath();
};
