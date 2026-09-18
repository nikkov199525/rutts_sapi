#include "ParamReader.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::wstring AppDataDir() {
  wchar_t path[MAX_PATH]{};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))) {
    return path;
  }

  DWORD n = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
  if (n > 0 && n < MAX_PATH) return path;
  return L".";
}

bool FileExists(const std::wstring& path) {
  DWORD attrs = GetFileAttributesW(path.c_str());
  return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

void TrimInplace(std::wstring& s) {
  while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
  while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
}

bool ReadString(const std::wstring& ini, const wchar_t* key, std::wstring& out) {
  wchar_t buf[128]{};
  DWORD n = GetPrivateProfileStringW(L"Parameters", key, L"", buf, 128, ini.c_str());
  if (n == 0) return false;

  out.assign(buf, n);
  TrimInplace(out);
  return !out.empty();
}

bool ReadInt(const std::wstring& ini, const wchar_t* key, int& out) {
  std::wstring raw;
  if (!ReadString(ini, key, raw)) return false;

  wchar_t* end = nullptr;
  long value = wcstol(raw.c_str(), &end, 10);
  if (end == raw.c_str()) return false;
  out = static_cast<int>(value);
  return true;
}

bool ReadBool(const std::wstring& ini, const wchar_t* key, bool& out) {
  std::wstring raw;
  if (!ReadString(ini, key, raw)) return false;

  if (_wcsicmp(raw.c_str(), L"true") == 0 || _wcsicmp(raw.c_str(), L"yes") == 0) {
    out = true;
    return true;
  }
  if (_wcsicmp(raw.c_str(), L"false") == 0 || _wcsicmp(raw.c_str(), L"no") == 0) {
    out = false;
    return true;
  }

  out = (wcstol(raw.c_str(), nullptr, 10) != 0);
  return true;
}

int ClampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

int ClampInterpolationMultiplier(int v) {
  if (v < 2) return 1;
  if (v > 2) return 4;
  return 2;
}

InterpolationAlgorithm ParseInterpolationAlgorithm(int raw) {
  return (raw <= 0) ? InterpolationAlgorithm::Linear : InterpolationAlgorithm::ZeroOrderHold;
}

UnicodeNormalizationForm ParseNormalizationForm(const std::wstring& raw) {
  if (_wcsicmp(raw.c_str(), L"NFKC") == 0) return UnicodeNormalizationForm::NFKC;
  if (_wcsicmp(raw.c_str(), L"NFD") == 0) return UnicodeNormalizationForm::NFD;
  if (_wcsicmp(raw.c_str(), L"NFKD") == 0) return UnicodeNormalizationForm::NFKD;
  return UnicodeNormalizationForm::NFC;
}

void WriteDefaultIni(const std::wstring& path) {
  try {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  } catch (...) {
  }

  const wchar_t* ini = LR"ini([Parameters]
# Частота дискретизации синтезатора. При interpolation_multiplier = 1 это же выходная частота.
# Допустимые значения от 8000 до 16000. По умолчанию 10000
samples_per_sec = 10000

# Множитель интерполяции выходного сигнала. Допустимые значения: 1, 2 или 4. По умолчанию 1.
# Итоговая выходная частота = samples_per_sec * interpolation_multiplier.
interpolation_multiplier = 1

# Алгоритм интерполяции. 0 - линейный, 1 - нулевого порядка (ZOH). По умолчанию 0
interpolation_algorithm = 0

# Размер буфера для ru_tts_transfer в байтах. Допустимые значения от 256 до 1024000. По умолчанию 4096
wave_buffer_size = 4096

# Тишина в начале синтеза, миллисекунды. Допустимые значения от 0 до 100. По умолчанию 0
silence_at_begin = 0

# Тишина в конце синтеза, миллисекунды. Допустимые значения от 0 до 100. По умолчанию 0
silence_at_end = 0

# Общая пауза между фразами. Допустимые значения от 0 до 5000. По умолчанию 100
general_gap_factor = 100

# Пауза после запятой. Допустимые значения от 0 до 750. По умолчанию 100
comma_gap_factor = 100

# Пауза после точки. Допустимые значения от 0 до 500. По умолчанию 100
dot_gap_factor = 100

# Пауза после точки с запятой. Допустимые значения от 0 до 600. По умолчанию 100
semicolon_gap_factor = 100

# Пауза после двоеточия. Допустимые значения от 0 до 600. По умолчанию 100
colon_gap_factor = 100

# Пауза после вопросительного знака. Допустимые значения от 0 до 375. По умолчанию 100
question_gap_factor = 100

# Пауза после восклицательного знака. Допустимые значения от 0 до 300. По умолчанию 100
exclamation_gap_factor = 100

# Интонационная пауза. Допустимые значения от 0 до 1000. По умолчанию 100
intonational_gap_factor = 100

# Использовать точку в качестве десятичного разделителя. Допустимые значения True или False. По умолчанию False
dec_sep_point = False

# Использовать запятую в качестве десятичного разделителя. Допустимые значения True или False. По умолчанию True
dec_sep_comma = True

# Использовать Unicode-нормализацию читаемого текста. Допустимые значения True или False. По умолчанию False
# Форма нормализации определяется параметром unicode_normalization_form
use_unicode_normalization = False

# Форма Unicode-нормализации читаемого текста. Учитывается только если параметр use_unicode_normalization имеет значение True
# Допустимые значения: NFC, NFKC, NFD или NFKD. По умолчанию NFC
unicode_normalization_form = NFC

# Использовать rutts_rulex. Допустимые значения True или False. По умолчанию True
use_rulex = True

# Использовать альтернативный голос ruTTS (женский). Допустимые значения True или False. По умолчанию False
use_alternative_voice = False

# Использовать ускорение синтеза (x2). Допустимые значения True или False. По умолчанию False
use_rate_boost = False

# Старый (линейный) алгоритм изменения скорости. Допустимые значения True или False. По умолчанию False.
# Понимает только новое ядро ru_tts.dll; на старом ядре флаг игнорируется.
use_legacy_rate_algo = False

# Интонация речи. Допустимые значения от 0 до 140. По умолчанию 100
intonation = 100

[Characters]
j = дж
q = ку
w = в
x = кс

[SingleCharacters]
б = бэ
в = вэ
с = эс
к = ка
ь = мягкий знак
ъ = твёрдый знак

a = эй
b = би
c = си
d = ди
e = и
f = эф
g = джи
h = эйчь
i = ай
j = джей
k = кей
l = эл
m = эм
n = эн
o = оу
p = пи
q = къю
r = ар
s = эс
t = ти
u = ю
v = ви
w = даблъю
x = экс
y = вай
z = зэт
)ini";

  std::ofstream f(std::filesystem::path(path), std::ios::binary);
  if (!f) return;
  const unsigned char bom[2] = { 0xFF, 0xFE };
  f.write(reinterpret_cast<const char*>(bom), sizeof(bom));
  f.write(reinterpret_cast<const char*>(ini), static_cast<std::streamsize>(wcslen(ini) * sizeof(wchar_t)));
}

} // namespace

std::wstring ParamReader::IniPath() {
  std::filesystem::path base(AppDataDir());
  base /= L"rutts";
  base /= L"rutts.ini";
  return base.wstring();
}

RuTtsParams ParamReader::Load() {
  RuTtsParams p{};
  const std::wstring ini = IniPath();
  if (!FileExists(ini)) WriteDefaultIni(ini);

  bool b = false;
  if (ReadBool(ini, L"use_rulex", b)) p.use_rulex = b;
  else if (ReadBool(ini, L"userulex", b)) p.use_rulex = b; // legacy compatibility

  int v = 0;
  if (ReadInt(ini, L"samples_per_sec", v)) {
    p.samples_per_sec = ClampInt(v, 8000, 16000);
  } else {
    p.samples_per_sec = 10000;
  }

  if (ReadInt(ini, L"interpolation_multiplier", v)) {
    p.interpolation_multiplier = ClampInterpolationMultiplier(v);
  }
  if (ReadInt(ini, L"interpolation_algorithm", v)) {
    p.interpolation_algorithm = ParseInterpolationAlgorithm(v);
  }
  if (ReadInt(ini, L"wave_buffer_size", v)) {
    p.wave_buffer_size = ClampInt(v, 256, 1024000);
  }
  if (ReadInt(ini, L"silence_at_begin", v)) {
    p.silence_at_begin = ClampInt(v, 0, 100);
  }
  if (ReadInt(ini, L"silence_at_end", v)) {
    p.silence_at_end = ClampInt(v, 0, 100);
  }

  if (ReadInt(ini, L"general_gap_factor", v)) {
    p.has_general_gap_factor = true;
    p.general_gap_factor = ClampInt(v, 0, 5000);
  }
  if (ReadInt(ini, L"comma_gap_factor", v)) {
    p.has_comma_gap_factor = true;
    p.comma_gap_factor = ClampInt(v, 0, 750);
  }
  if (ReadInt(ini, L"dot_gap_factor", v)) {
    p.has_dot_gap_factor = true;
    p.dot_gap_factor = ClampInt(v, 0, 500);
  }
  if (ReadInt(ini, L"semicolon_gap_factor", v)) {
    p.has_semicolon_gap_factor = true;
    p.semicolon_gap_factor = ClampInt(v, 0, 600);
  }
  if (ReadInt(ini, L"colon_gap_factor", v)) {
    p.has_colon_gap_factor = true;
    p.colon_gap_factor = ClampInt(v, 0, 600);
  }
  if (ReadInt(ini, L"question_gap_factor", v)) {
    p.has_question_gap_factor = true;
    p.question_gap_factor = ClampInt(v, 0, 375);
  }
  if (ReadInt(ini, L"exclamation_gap_factor", v)) {
    p.has_exclamation_gap_factor = true;
    p.exclamation_gap_factor = ClampInt(v, 0, 300);
  }
  if (ReadInt(ini, L"intonational_gap_factor", v)) {
    p.has_intonational_gap_factor = true;
    p.intonational_gap_factor = ClampInt(v, 0, 1000);
  }

  if (ReadBool(ini, L"dec_sep_point", b)) p.dec_sep_point = b;
  if (ReadBool(ini, L"dec_sep_comma", b)) p.dec_sep_comma = b;

  if (ReadBool(ini, L"use_unicode_normalization", b)) p.use_unicode_normalization = b;
  std::wstring form;
  if (ReadString(ini, L"unicode_normalization_form", form)) {
    p.unicode_normalization_form = ParseNormalizationForm(form);
  }
  if (ReadBool(ini, L"use_alternative_voice", b)) p.use_alternative_voice = b;
  if (ReadBool(ini, L"use_rate_boost", b)) p.use_rate_boost = b;
  else if (ReadBool(ini, L"rate_boost", b)) p.use_rate_boost = b; // legacy compatibility
  if (ReadBool(ini, L"use_legacy_rate_algo", b)) p.use_legacy_rate_algo = b;
  if (ReadInt(ini, L"intonation", v)) {
    p.has_intonation = true;
    p.intonation = ClampInt(v, 0, 140);
  }

  const long long outRate =
      static_cast<long long>(p.samples_per_sec) * static_cast<long long>(p.interpolation_multiplier);
  p.output_sample_rate = ClampInt(static_cast<int>(outRate), 8000, 192000);

  return p;
}

void ParamReader::ApplyToConf(const RuTtsParams& p, ru_tts_conf_t& conf) {
  if (p.has_general_gap_factor) conf.general_gap_factor = p.general_gap_factor;
  if (p.has_comma_gap_factor) conf.comma_gap_factor = p.comma_gap_factor;
  if (p.has_dot_gap_factor) conf.dot_gap_factor = p.dot_gap_factor;
  if (p.has_semicolon_gap_factor) conf.semicolon_gap_factor = p.semicolon_gap_factor;
  if (p.has_colon_gap_factor) conf.colon_gap_factor = p.colon_gap_factor;
  if (p.has_question_gap_factor) conf.question_gap_factor = p.question_gap_factor;
  if (p.has_exclamation_gap_factor) conf.exclamation_gap_factor = p.exclamation_gap_factor;
  if (p.has_intonational_gap_factor) conf.intonational_gap_factor = p.intonational_gap_factor;
  if (p.has_intonation) conf.intonation = p.intonation;

  conf.flags &= ~(DEC_SEP_POINT | DEC_SEP_COMMA | USE_ALTERNATIVE_VOICE | USE_LEGACY_RATE_ALGO);
  if (p.dec_sep_point) conf.flags |= DEC_SEP_POINT;
  if (p.dec_sep_comma) conf.flags |= DEC_SEP_COMMA;
  if (p.use_alternative_voice) conf.flags |= USE_ALTERNATIVE_VOICE;
  if (p.use_legacy_rate_algo) conf.flags |= USE_LEGACY_RATE_ALGO;
}
