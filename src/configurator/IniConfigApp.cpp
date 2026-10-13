#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ParamReader.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")

namespace {

constexpr wchar_t kWindowClassName[] = L"RuTtsIniConfiguratorWindow";
constexpr wchar_t kWindowTitleBase[] = L"RU TTS Configurator";

constexpr wchar_t kTabMainText[] = L"\u041F\u0430\u0440\u0430\u043C\u0435\u0442\u0440\u044B";
constexpr wchar_t kTabDictionaryText[] = L"\u0421\u043B\u043E\u0432\u0430\u0440\u044C";

constexpr wchar_t kBtnApplyText[] = L"\u041F\u0440\u0438\u043C\u0435\u043D\u0438\u0442\u044C";
constexpr wchar_t kBtnRestoreText[] = L"\u0412\u043E\u0441\u0441\u0442\u0430\u043D\u043E\u0432\u0438\u0442\u044C \u0434\u043E \u0437\u0430\u0432\u043E\u0434\u0430";
constexpr wchar_t kBtnExitText[] = L"\u0412\u044B\u0445\u043E\u0434";

constexpr wchar_t kGroupCharsText[] = L"\u0417\u0430\u043C\u0435\u043D\u0430 \u0441\u0438\u043C\u0432\u043E\u043B\u043E\u0432";
constexpr wchar_t kGroupSingleCharsText[] = L"\u041E\u0434\u0438\u043D\u043E\u0447\u043D\u044B\u0435 \u0441\u0438\u043C\u0432\u043E\u043B\u044B";
constexpr wchar_t kColumnSymbolText[] = L"\u0421\u0438\u043C\u0432\u043E\u043B";
constexpr wchar_t kColumnReplacementText[] = L"\u0417\u0430\u043C\u0435\u043D\u0430";
constexpr wchar_t kLabelSymbolText[] = L"\u0421\u0438\u043C\u0432\u043E\u043B:";
constexpr wchar_t kLabelReplacementText[] = L"\u0417\u0430\u043C\u0435\u043D\u0430:";
constexpr wchar_t kBtnUpsertText[] = L"\u0414\u043E\u0431\u0430\u0432\u0438\u0442\u044C/\u043E\u0431\u043D\u043E\u0432\u0438\u0442\u044C";
constexpr wchar_t kBtnDeleteText[] = L"\u0423\u0434\u0430\u043B\u0438\u0442\u044C";

enum : int {
  ID_TAB = 100,

  ID_BTN_APPLY = 101,
  ID_BTN_RESTORE = 102,
  ID_BTN_EXIT = 103,

  ID_BOOL_BASE = 200,
  ID_COMBO_NORMALIZATION = 260,
  ID_COMBO_INTERPOLATION_MULTIPLIER = 261,
  ID_COMBO_INTERPOLATION_ALGORITHM = 262,

  ID_SLIDER_BASE = 300,
  ID_VALUE_LABEL_BASE = 350,
  ID_ADV_SLIDER_BASE = 500,
  ID_ADV_VALUE_LABEL_BASE = 550,
  ID_SPIN_EDIT_BASE = 600,
  ID_SPIN_UPDOWN_BASE = 700,
  ID_ADV_SPIN_EDIT_BASE = 800,
  ID_ADV_SPIN_UPDOWN_BASE = 900,

  ID_LIST_CHARACTERS = 400,
  ID_LIST_SINGLE_CHARACTERS = 401,

  ID_EDIT_CHARS_KEY = 410,
  ID_EDIT_CHARS_VALUE = 411,
  ID_BTN_CHARS_UPSERT = 412,
  ID_BTN_CHARS_DELETE = 413,

  ID_EDIT_SINGLE_KEY = 420,
  ID_EDIT_SINGLE_VALUE = 421,
  ID_BTN_SINGLE_UPSERT = 422,
  ID_BTN_SINGLE_DELETE = 423
};

struct BoolSpec {
  const wchar_t* key;
  const wchar_t* label;
  bool defaultValue;
};

enum BoolIndex : int {
  BI_USE_RULEX = 0,
  BI_USE_ALTERNATIVE_VOICE = 1,
  BI_USE_RATE_BOOST = 2,
  BI_DEC_SEP_POINT = 3,
  BI_DEC_SEP_COMMA = 4,
  BI_USE_UNICODE_NORMALIZATION = 5,
  BI_USE_LEGACY_RATE_ALGO = 6
};

constexpr BoolSpec kBoolSpecs[] = {
  { L"use_rulex", L"\u0418\u0441\u043F\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u0442\u044C rulex", true },
  { L"use_alternative_voice", L"\u0418\u0441\u043F\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u0442\u044C \u0430\u043B\u044C\u0442\u0435\u0440\u043D\u0430\u0442\u0438\u0432\u043D\u044B\u0439 \u0433\u043E\u043B\u043E\u0441", false },
  { L"use_rate_boost", L"\u0418\u0441\u043F\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u0442\u044C \u0443\u0441\u043A\u043E\u0440\u0435\u043D\u0438\u0435 \u0441\u0438\u043D\u0442\u0435\u0437\u0430 (x2)", false },
  { L"dec_sep_point", L"\u0418\u0441\u043F\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u0442\u044C \u0442\u043E\u0447\u043A\u0443 \u043A\u0430\u043A \u0434\u0435\u0441\u044F\u0442\u0438\u0447\u043D\u044B\u0439 \u0440\u0430\u0437\u0434\u0435\u043B\u0438\u0442\u0435\u043B\u044C", false },
  { L"dec_sep_comma", L"\u0418\u0441\u043F\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u0442\u044C \u0437\u0430\u043F\u044F\u0442\u0443\u044E \u043A\u0430\u043A \u0434\u0435\u0441\u044F\u0442\u0438\u0447\u043D\u044B\u0439 \u0440\u0430\u0437\u0434\u0435\u043B\u0438\u0442\u0435\u043B\u044C", true },
  { L"use_unicode_normalization", L"\u0418\u0441\u043F\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u0442\u044C Unicode-\u043D\u043E\u0440\u043C\u0430\u043B\u0438\u0437\u0430\u0446\u0438\u044E", false },
  { L"use_legacy_rate_algo", L"\u0421\u0442\u0430\u0440\u044B\u0439 \u0430\u043B\u0433\u043E\u0440\u0438\u0442\u043C \u0438\u0437\u043C\u0435\u043D\u0435\u043D\u0438\u044F \u0441\u043A\u043E\u0440\u043E\u0441\u0442\u0438", false }
};

struct NumericSpec {
  const wchar_t* key;
  const wchar_t* label;
  int minValue;
  int maxValue;
  int defaultValue;
};

enum NumericIndex : int {
  NI_SAMPLES_PER_SEC = 0,
  NI_COMMA_GAP_FACTOR = 1,
  NI_DOT_GAP_FACTOR = 2,
  NI_SEMICOLON_GAP_FACTOR = 3,
  NI_COLON_GAP_FACTOR = 4,
  NI_QUESTION_GAP_FACTOR = 5,
  NI_EXCLAMATION_GAP_FACTOR = 6,
  NI_INTONATIONAL_GAP_FACTOR = 7,
  NI_INTONATION = 8
};

constexpr NumericSpec kNumericSpecs[] = {
  { L"samples_per_sec", L"\u0427\u0430\u0441\u0442\u043E\u0442\u0430 \u0434\u0438\u0441\u043A\u0440\u0435\u0442\u0438\u0437\u0430\u0446\u0438\u0438", 8000, 16000, 10000 },
  { L"comma_gap_factor", L"\u041F\u0430\u0443\u0437\u0430 \u043F\u043E\u0441\u043B\u0435 \u0437\u0430\u043F\u044F\u0442\u043E\u0439", 0, 750, 100 },
  { L"dot_gap_factor", L"\u041F\u0430\u0443\u0437\u0430 \u043F\u043E\u0441\u043B\u0435 \u0442\u043E\u0447\u043A\u0438", 0, 500, 100 },
  { L"semicolon_gap_factor", L"\u041F\u0430\u0443\u0437\u0430 \u043F\u043E\u0441\u043B\u0435 \u0442\u043E\u0447\u043A\u0438 \u0441 \u0437\u0430\u043F\u044F\u0442\u043E\u0439", 0, 600, 100 },
  { L"colon_gap_factor", L"\u041F\u0430\u0443\u0437\u0430 \u043F\u043E\u0441\u043B\u0435 \u0434\u0432\u043E\u0435\u0442\u043E\u0447\u0438\u044F", 0, 600, 100 },
  { L"question_gap_factor", L"\u041F\u0430\u0443\u0437\u0430 \u043F\u043E\u0441\u043B\u0435 \u0432\u043E\u043F\u0440\u043E\u0441\u0438\u0442\u0435\u043B\u044C\u043D\u043E\u0433\u043E \u0437\u043D\u0430\u043A\u0430", 0, 375, 100 },
  { L"exclamation_gap_factor", L"\u041F\u0430\u0443\u0437\u0430 \u043F\u043E\u0441\u043B\u0435 \u0432\u043E\u0441\u043A\u043B\u0438\u0446\u0430\u0442\u0435\u043B\u044C\u043D\u043E\u0433\u043E \u0437\u043D\u0430\u043A\u0430", 0, 300, 100 },
  { L"intonational_gap_factor", L"\u0418\u043D\u0442\u043E\u043D\u0430\u0446\u0438\u043E\u043D\u043D\u0430\u044F \u043F\u0430\u0443\u0437\u0430", 0, 1000, 100 },
  { L"intonation", L"\u0418\u043D\u0442\u043E\u043D\u0430\u0446\u0438\u044F \u0440\u0435\u0447\u0438", 0, 140, 100 }
};

enum AdvancedNumericIndex : int {
  ANI_GENERAL_GAP_FACTOR = 0,
  ANI_INTERPOLATION_MULTIPLIER = 1,
  ANI_INTERPOLATION_ALGORITHM = 2,
  ANI_WAVE_BUFFER_SIZE = 3,
  ANI_SILENCE_AT_BEGIN = 4,
  ANI_SILENCE_AT_END = 5
};

constexpr NumericSpec kAdvancedNumericSpecs[] = {
  { L"general_gap_factor", L"\u041E\u0431\u0449\u0430\u044F \u043F\u0430\u0443\u0437\u0430 \u043C\u0435\u0436\u0434\u0443 \u0444\u0440\u0430\u0437\u0430\u043C\u0438", 0, 5000, 100 },
  { L"interpolation_multiplier", L"\u041C\u043D\u043E\u0436\u0438\u0442\u0435\u043B\u044C \u0438\u043D\u0442\u0435\u0440\u043F\u043E\u043B\u044F\u0446\u0438\u0438", 1, 4, 1 },
  { L"interpolation_algorithm", L"\u0410\u043B\u0433\u043E\u0440\u0438\u0442\u043C \u0438\u043D\u0442\u0435\u0440\u043F\u043E\u043B\u044F\u0446\u0438\u0438", 0, 1, 0 },
  { L"wave_buffer_size", L"\u0420\u0430\u0437\u043C\u0435\u0440 \u0430\u0443\u0434\u0438\u043E\u0431\u0443\u0444\u0435\u0440\u0430 (\u0431\u0430\u0439\u0442)", 256, 1024000, 4096 },
  { L"silence_at_begin", L"\u0422\u0438\u0448\u0438\u043D\u0430 \u0432 \u043D\u0430\u0447\u0430\u043B\u0435 (\u043C\u0441)", 0, 100, 0 },
  { L"silence_at_end", L"\u0422\u0438\u0448\u0438\u043D\u0430 \u0432 \u043A\u043E\u043D\u0446\u0435 (\u043C\u0441)", 0, 100, 0 }
};

constexpr const wchar_t* kInterpolationMultiplierItems[] = {
  L"1x", L"2x", L"4x"
};

constexpr const wchar_t* kInterpolationAlgorithmItems[] = {
  L"\u041B\u0438\u043D\u0435\u0439\u043D\u0430\u044F",
  L"\u041D\u0443\u043B\u0435\u0432\u043E\u0433\u043E \u043F\u043E\u0440\u044F\u0434\u043A\u0430"
};

constexpr const wchar_t* kNormalizationForms[] = {
  L"NFC",
  L"NFKC",
  L"NFD",
  L"NFKD"
};

struct DictEntry {
  std::wstring key;
  std::wstring value;
};

struct AppState {
  HWND mainWindow = nullptr;
  HWND tab = nullptr;
  HWND pageMain = nullptr;
  HWND pageDictionary = nullptr;

  HWND btnApply = nullptr;
  HWND btnRestore = nullptr;
  HWND btnExit = nullptr;

  HWND boolCheckboxes[_countof(kBoolSpecs)]{};
  HWND sliders[_countof(kNumericSpecs)]{};
  HWND valueLabels[_countof(kNumericSpecs)]{};
  HWND spinEdits[_countof(kNumericSpecs)]{};
  HWND spinUpDowns[_countof(kNumericSpecs)]{};
  HWND advancedSpinEdits[_countof(kAdvancedNumericSpecs)]{};
  HWND advancedSpinUpDowns[_countof(kAdvancedNumericSpecs)]{};
  HWND comboNormalization = nullptr;
  HWND comboInterpolationMultiplier = nullptr;
  HWND comboInterpolationAlgorithm = nullptr;

  HWND listCharacters = nullptr;
  HWND listSingleCharacters = nullptr;

  HWND editCharsKey = nullptr;
  HWND editCharsValue = nullptr;
  HWND btnCharsUpsert = nullptr;
  HWND btnCharsDelete = nullptr;

  HWND editSingleKey = nullptr;
  HWND editSingleValue = nullptr;
  HWND btnSingleUpsert = nullptr;
  HWND btnSingleDelete = nullptr;

  HFONT uiFont = nullptr;
  bool loadingUi = false;
  bool dirty = false;
  std::wstring iniPath;
};

AppState g_app;

std::wstring GetWindowTextString(HWND hwnd) {
  const int len = GetWindowTextLengthW(hwnd);
  if (len <= 0) return {};
  std::wstring out(static_cast<size_t>(len) + 1, L'\0');
  GetWindowTextW(hwnd, out.data(), len + 1);
  out.resize(wcslen(out.c_str()));
  return out;
}

void TrimInplace(std::wstring& s) {
  while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' || s.front() == L'\r' || s.front() == L'\n')) {
    s.erase(s.begin());
  }
  while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r' || s.back() == L'\n')) {
    s.pop_back();
  }
}

std::wstring TrimCopy(std::wstring s) {
  TrimInplace(s);
  return s;
}

bool ParseBool(const std::wstring& raw, bool fallback) {
  if (raw.empty()) return fallback;
  if (_wcsicmp(raw.c_str(), L"true") == 0 || _wcsicmp(raw.c_str(), L"yes") == 0) return true;
  if (_wcsicmp(raw.c_str(), L"false") == 0 || _wcsicmp(raw.c_str(), L"no") == 0) return false;

  wchar_t* end = nullptr;
  const long value = wcstol(raw.c_str(), &end, 10);
  if (end == raw.c_str()) return fallback;
  return value != 0;
}

bool ReadParameterString(const std::wstring& iniPath, const wchar_t* key, std::wstring& outValue) {
  wchar_t buffer[256]{};
  const DWORD n = GetPrivateProfileStringW(L"Parameters", key, L"", buffer, _countof(buffer), iniPath.c_str());
  if (n == 0) return false;
  outValue.assign(buffer, n);
  TrimInplace(outValue);
  return !outValue.empty();
}

int ReadParameterInt(const std::wstring& iniPath, const NumericSpec& spec) {
  std::wstring raw;
  if (!ReadParameterString(iniPath, spec.key, raw)) return spec.defaultValue;

  wchar_t* end = nullptr;
  const long parsed = wcstol(raw.c_str(), &end, 10);
  if (end == raw.c_str()) return spec.defaultValue;
  return std::clamp(static_cast<int>(parsed), spec.minValue, spec.maxValue);
}

bool ReadParameterBool(const std::wstring& iniPath, const BoolSpec& spec) {
  std::wstring raw;
  if (!ReadParameterString(iniPath, spec.key, raw)) return spec.defaultValue;
  return ParseBool(raw, spec.defaultValue);
}

int NormalizationIndexFromString(const std::wstring& raw) {
  if (_wcsicmp(raw.c_str(), L"NFKC") == 0) return 1;
  if (_wcsicmp(raw.c_str(), L"NFD") == 0) return 2;
  if (_wcsicmp(raw.c_str(), L"NFKD") == 0) return 3;
  return 0;
}

std::vector<DictEntry> ReadDictionarySection(const std::wstring& iniPath, const wchar_t* sectionName) {
  std::vector<wchar_t> buffer(4096);
  DWORD copied = 0;
  for (;;) {
    copied = GetPrivateProfileSectionW(sectionName, buffer.data(), static_cast<DWORD>(buffer.size()), iniPath.c_str());
    if (copied == 0) return {};
    if (copied < buffer.size() - 2) break;
    if (buffer.size() >= 1 << 20) break;
    buffer.resize(buffer.size() * 2);
  }

  std::vector<DictEntry> entries;
  const wchar_t* p = buffer.data();
  while (*p) {
    std::wstring line = p;
    p += line.size() + 1;

    const size_t eq = line.find(L'=');
    if (eq == std::wstring::npos) continue;

    DictEntry e;
    e.key = TrimCopy(line.substr(0, eq));
    e.value = TrimCopy(line.substr(eq + 1));
    if (!e.key.empty()) entries.push_back(std::move(e));
  }
  return entries;
}

HWND CreateControl(
    DWORD exStyle,
    const wchar_t* className,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int w,
    int h,
    HWND parent,
    int controlId) {
  HWND hwnd = CreateWindowExW(
      exStyle,
      className,
      text,
      style | WS_CHILD | WS_VISIBLE,
      x,
      y,
      w,
      h,
      parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
      GetModuleHandleW(nullptr),
      nullptr);
  if (hwnd && g_app.uiFont) {
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.uiFont), TRUE);
  }
  return hwnd;
}

void UpdateWindowTitle() {
  std::wstring title = kWindowTitleBase;
  if (g_app.dirty) title += L" *";
  SetWindowTextW(g_app.mainWindow, title.c_str());
}

void SetDirty(bool dirty) {
  g_app.dirty = dirty;
  UpdateWindowTitle();
}

int GetBoolIndexById(const int controlId) {
  const int idx = controlId - ID_BOOL_BASE;
  return (idx >= 0 && idx < static_cast<int>(_countof(kBoolSpecs))) ? idx : -1;
}

int GetNumericIndexBySliderId(const int controlId) {
  const int idx = controlId - ID_SLIDER_BASE;
  return (idx >= 0 && idx < static_cast<int>(_countof(kNumericSpecs))) ? idx : -1;
}

int GetNumericIndexBySpinEditId(const int controlId) {
  const int idx = controlId - ID_SPIN_EDIT_BASE;
  return (idx >= 0 && idx < static_cast<int>(_countof(kNumericSpecs))) ? idx : -1;
}

int GetAdvancedNumericIndexBySpinEditId(const int controlId) {
  const int idx = controlId - ID_ADV_SPIN_EDIT_BASE;
  return (idx >= 0 && idx < static_cast<int>(_countof(kAdvancedNumericSpecs))) ? idx : -1;
}

bool IsZeroToHundredRange(const NumericSpec& spec) {
  return spec.minValue == 0 && spec.maxValue == 100;
}

int NormalizeInterpolationMultiplier(int value) {
  if (value < 2) return 1;
  if (value > 2) return 4;
  return 2;
}

void SetSliderValueLabel(int numericIndex) {
  if (numericIndex < 0 || numericIndex >= static_cast<int>(_countof(kNumericSpecs))) return;
  const int value = static_cast<int>(SendMessageW(g_app.sliders[numericIndex], TBM_GETPOS, 0, 0));
  const std::wstring text = std::to_wstring(value);
  SetWindowTextW(g_app.valueLabels[numericIndex], text.c_str());
}

int ReadSpinEditValue(HWND edit, int minValue, int maxValue, int fallback) {
  if (!edit || !IsWindow(edit)) return std::clamp(fallback, minValue, maxValue);
  std::wstring raw = GetWindowTextString(edit);
  TrimInplace(raw);
  if (raw.empty()) return std::clamp(fallback, minValue, maxValue);
  wchar_t* end = nullptr;
  const long parsed = wcstol(raw.c_str(), &end, 10);
  if (end == raw.c_str()) return std::clamp(fallback, minValue, maxValue);
  return std::clamp(static_cast<int>(parsed), minValue, maxValue);
}

void WriteSpinEditValue(HWND edit, int value) {
  if (!edit || !IsWindow(edit)) return;
  const std::wstring text = std::to_wstring(value);
  SetWindowTextW(edit, text.c_str());
}

int ReadMainNumericUiValue(int index) {
  if (index < 0 || index >= static_cast<int>(_countof(kNumericSpecs))) return 0;
  const NumericSpec& spec = kNumericSpecs[index];
  if (IsZeroToHundredRange(spec)) {
    return static_cast<int>(SendMessageW(g_app.sliders[index], TBM_GETPOS, 0, 0));
  }
  return ReadSpinEditValue(g_app.spinEdits[index], spec.minValue, spec.maxValue, spec.defaultValue);
}

int ReadAdvancedNumericUiValue(int index) {
  if (index < 0 || index >= static_cast<int>(_countof(kAdvancedNumericSpecs))) return 0;
  if (index == ANI_INTERPOLATION_MULTIPLIER) {
    int sel = static_cast<int>(SendMessageW(g_app.comboInterpolationMultiplier, CB_GETCURSEL, 0, 0));
    if (sel < 0) sel = 0;
    if (sel == 0) return 1;
    if (sel == 1) return 2;
    return 4;
  }
  if (index == ANI_INTERPOLATION_ALGORITHM) {
    int sel = static_cast<int>(SendMessageW(g_app.comboInterpolationAlgorithm, CB_GETCURSEL, 0, 0));
    if (sel < 0) sel = 0;
    return (sel == 0) ? 0 : 1;
  }
  const NumericSpec& spec = kAdvancedNumericSpecs[index];
  return ReadSpinEditValue(g_app.advancedSpinEdits[index], spec.minValue, spec.maxValue, spec.defaultValue);
}

void SetMainNumericUiValue(int index, int value) {
  if (index < 0 || index >= static_cast<int>(_countof(kNumericSpecs))) return;
  const NumericSpec& spec = kNumericSpecs[index];
  value = std::clamp(value, spec.minValue, spec.maxValue);
  if (IsZeroToHundredRange(spec)) {
    SendMessageW(g_app.sliders[index], TBM_SETPOS, TRUE, value);
    SetSliderValueLabel(index);
  } else {
    WriteSpinEditValue(g_app.spinEdits[index], value);
  }
}

void SetAdvancedNumericUiValue(int index, int value) {
  if (index < 0 || index >= static_cast<int>(_countof(kAdvancedNumericSpecs))) return;
  if (index == ANI_INTERPOLATION_MULTIPLIER) {
    value = NormalizeInterpolationMultiplier(value);
    int sel = 0;
    if (value == 2) sel = 1;
    else if (value == 4) sel = 2;
    SendMessageW(g_app.comboInterpolationMultiplier, CB_SETCURSEL, sel, 0);
    return;
  }
  if (index == ANI_INTERPOLATION_ALGORITHM) {
    SendMessageW(g_app.comboInterpolationAlgorithm, CB_SETCURSEL, (value <= 0) ? 0 : 1, 0);
    return;
  }
  const NumericSpec& spec = kAdvancedNumericSpecs[index];
  value = std::clamp(value, spec.minValue, spec.maxValue);
  WriteSpinEditValue(g_app.advancedSpinEdits[index], value);
}

void UpdateNormalizationEnabledState() {
  // Keep combobox reachable via Tab regardless of checkbox state.
  EnableWindow(g_app.comboNormalization, TRUE);
}

std::wstring GetListViewText(HWND listView, int row, int subItem) {
  wchar_t buffer[1024]{};
  ListView_GetItemText(listView, row, subItem, buffer, _countof(buffer));
  return buffer;
}

int GetSelectedListIndex(HWND listView) {
  return ListView_GetNextItem(listView, -1, LVNI_SELECTED);
}

void ClearDictionaryEditors(HWND keyEdit, HWND valueEdit) {
  SetWindowTextW(keyEdit, L"");
  SetWindowTextW(valueEdit, L"");
}

void LoadDictionaryEditorsFromSelection(HWND listView, HWND keyEdit, HWND valueEdit) {
  const int selected = GetSelectedListIndex(listView);
  if (selected < 0) {
    ClearDictionaryEditors(keyEdit, valueEdit);
    return;
  }
  const std::wstring key = GetListViewText(listView, selected, 0);
  const std::wstring value = GetListViewText(listView, selected, 1);
  SetWindowTextW(keyEdit, key.c_str());
  SetWindowTextW(valueEdit, value.c_str());
}

void FillDictionaryList(HWND listView, const std::vector<DictEntry>& entries) {
  ListView_DeleteAllItems(listView);
  for (size_t i = 0; i < entries.size(); ++i) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = static_cast<int>(i);
    item.pszText = const_cast<wchar_t*>(entries[i].key.c_str());
    const int row = ListView_InsertItem(listView, &item);
    if (row >= 0) {
      ListView_SetItemText(listView, row, 1, const_cast<wchar_t*>(entries[i].value.c_str()));
    }
  }
}

int FindDictionaryRowByKey(HWND listView, const std::wstring& key, int ignoreRow) {
  const int count = ListView_GetItemCount(listView);
  for (int i = 0; i < count; ++i) {
    if (i == ignoreRow) continue;
    if (_wcsicmp(GetListViewText(listView, i, 0).c_str(), key.c_str()) == 0) {
      return i;
    }
  }
  return -1;
}

bool IsValidDictionaryKey(const std::wstring& key) {
  return !key.empty() && key.find_first_of(L"=\r\n") == std::wstring::npos;
}

void UpsertDictionaryEntry(HWND owner, HWND listView, HWND keyEdit, HWND valueEdit) {
  std::wstring key = TrimCopy(GetWindowTextString(keyEdit));
  std::wstring value = TrimCopy(GetWindowTextString(valueEdit));

  if (!IsValidDictionaryKey(key)) {
    MessageBoxW(
        owner,
        L"\u041D\u0435\u0434\u043E\u043F\u0443\u0441\u0442\u0438\u043C\u044B\u0439 \u043A\u043B\u044E\u0447. "
        L"\u041A\u043B\u044E\u0447 \u043D\u0435 \u0434\u043E\u043B\u0436\u0435\u043D \u0431\u044B\u0442\u044C \u043F\u0443\u0441\u0442\u044B\u043C "
        L"\u0438 \u043D\u0435 \u043C\u043E\u0436\u0435\u0442 \u0441\u043E\u0434\u0435\u0440\u0436\u0430\u0442\u044C '=', CR \u0438\u043B\u0438 LF.",
        kWindowTitleBase,
        MB_OK | MB_ICONWARNING);
    SetFocus(keyEdit);
    return;
  }
  if (value.find_first_of(L"\r\n") != std::wstring::npos) {
    MessageBoxW(owner, L"\u0422\u0435\u043A\u0441\u0442 \u0437\u0430\u043C\u0435\u043D\u044B \u043D\u0435 \u043C\u043E\u0436\u0435\u0442 \u0441\u043E\u0434\u0435\u0440\u0436\u0430\u0442\u044C CR \u0438\u043B\u0438 LF.", kWindowTitleBase, MB_OK | MB_ICONWARNING);
    SetFocus(valueEdit);
    return;
  }

  const int selected = GetSelectedListIndex(listView);
  const int duplicateRow = FindDictionaryRowByKey(listView, key, selected);
  if (duplicateRow >= 0) {
    MessageBoxW(owner, L"\u042D\u0442\u043E\u0442 \u043A\u043B\u044E\u0447 \u0443\u0436\u0435 \u0435\u0441\u0442\u044C \u0432 \u0442\u0435\u043A\u0443\u0449\u0435\u043C \u0441\u043F\u0438\u0441\u043A\u0435.", kWindowTitleBase, MB_OK | MB_ICONWARNING);
    ListView_SetItemState(listView, duplicateRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(listView, duplicateRow, FALSE);
    return;
  }

  if (selected >= 0) {
    ListView_SetItemText(listView, selected, 0, key.data());
    ListView_SetItemText(listView, selected, 1, value.data());
  } else {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(listView);
    item.pszText = key.data();
    const int row = ListView_InsertItem(listView, &item);
    if (row >= 0) {
      ListView_SetItemText(listView, row, 1, value.data());
      ListView_SetItemState(listView, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
      ListView_EnsureVisible(listView, row, FALSE);
    }
  }
  SetDirty(true);
}

void DeleteSelectedDictionaryEntry(HWND listView, HWND keyEdit, HWND valueEdit) {
  const int selected = GetSelectedListIndex(listView);
  if (selected < 0) return;
  ListView_DeleteItem(listView, selected);
  ClearDictionaryEditors(keyEdit, valueEdit);
  SetDirty(true);
}

std::vector<DictEntry> CollectDictionaryEntries(HWND listView) {
  std::vector<DictEntry> out;
  const int count = ListView_GetItemCount(listView);
  out.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    DictEntry e;
    e.key = TrimCopy(GetListViewText(listView, i, 0));
    e.value = TrimCopy(GetListViewText(listView, i, 1));
    if (!e.key.empty()) out.push_back(std::move(e));
  }
  return out;
}

bool WriteUtf16IniFile(const std::wstring& path, const std::wstring& content) {
  try {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  } catch (...) {
  }

  std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
  if (!file) return false;

  const unsigned char bom[2] = { 0xFF, 0xFE };
  file.write(reinterpret_cast<const char*>(bom), sizeof(bom));
  file.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size() * sizeof(wchar_t)));
  return file.good();
}

const wchar_t* BoolToIniString(bool value) {
  return value ? L"True" : L"False";
}

bool SaveIniFromUi(HWND owner) {
  std::vector<int> numericValues(_countof(kNumericSpecs));
  for (size_t i = 0; i < _countof(kNumericSpecs); ++i) {
    numericValues[i] = ReadMainNumericUiValue(static_cast<int>(i));
  }

  std::vector<int> advancedValues(_countof(kAdvancedNumericSpecs));
  for (size_t i = 0; i < _countof(kAdvancedNumericSpecs); ++i) {
    advancedValues[i] = ReadAdvancedNumericUiValue(static_cast<int>(i));
  }
  advancedValues[ANI_INTERPOLATION_MULTIPLIER] =
      NormalizeInterpolationMultiplier(advancedValues[ANI_INTERPOLATION_MULTIPLIER]);

  std::vector<bool> boolValues(_countof(kBoolSpecs));
  for (size_t i = 0; i < _countof(kBoolSpecs); ++i) {
    boolValues[i] = (SendMessageW(g_app.boolCheckboxes[i], BM_GETCHECK, 0, 0) == BST_CHECKED);
  }

  int normIndex = static_cast<int>(SendMessageW(g_app.comboNormalization, CB_GETCURSEL, 0, 0));
  if (normIndex < 0 || normIndex > 3) normIndex = 0;

  const std::vector<DictEntry> characters = CollectDictionaryEntries(g_app.listCharacters);
  const std::vector<DictEntry> singleCharacters = CollectDictionaryEntries(g_app.listSingleCharacters);

  std::wstringstream ini;
  ini << L"[Parameters]\r\n";
  ini << L"samples_per_sec = " << numericValues[NI_SAMPLES_PER_SEC] << L"\r\n";
  ini << L"interpolation_multiplier = " << advancedValues[ANI_INTERPOLATION_MULTIPLIER] << L"\r\n";
  ini << L"interpolation_algorithm = " << advancedValues[ANI_INTERPOLATION_ALGORITHM] << L"\r\n";
  ini << L"wave_buffer_size = " << advancedValues[ANI_WAVE_BUFFER_SIZE] << L"\r\n";
  ini << L"silence_at_begin = " << advancedValues[ANI_SILENCE_AT_BEGIN] << L"\r\n";
  ini << L"silence_at_end = " << advancedValues[ANI_SILENCE_AT_END] << L"\r\n";
  ini << L"general_gap_factor = " << advancedValues[ANI_GENERAL_GAP_FACTOR] << L"\r\n";
  ini << L"comma_gap_factor = " << numericValues[NI_COMMA_GAP_FACTOR] << L"\r\n";
  ini << L"dot_gap_factor = " << numericValues[NI_DOT_GAP_FACTOR] << L"\r\n";
  ini << L"semicolon_gap_factor = " << numericValues[NI_SEMICOLON_GAP_FACTOR] << L"\r\n";
  ini << L"colon_gap_factor = " << numericValues[NI_COLON_GAP_FACTOR] << L"\r\n";
  ini << L"question_gap_factor = " << numericValues[NI_QUESTION_GAP_FACTOR] << L"\r\n";
  ini << L"exclamation_gap_factor = " << numericValues[NI_EXCLAMATION_GAP_FACTOR] << L"\r\n";
  ini << L"intonational_gap_factor = " << numericValues[NI_INTONATIONAL_GAP_FACTOR] << L"\r\n";
  ini << L"dec_sep_point = " << BoolToIniString(boolValues[BI_DEC_SEP_POINT]) << L"\r\n";
  ini << L"dec_sep_comma = " << BoolToIniString(boolValues[BI_DEC_SEP_COMMA]) << L"\r\n";
  ini << L"use_unicode_normalization = " << BoolToIniString(boolValues[BI_USE_UNICODE_NORMALIZATION]) << L"\r\n";
  ini << L"unicode_normalization_form = " << kNormalizationForms[normIndex] << L"\r\n";
  ini << L"use_rulex = " << BoolToIniString(boolValues[BI_USE_RULEX]) << L"\r\n";
  ini << L"use_alternative_voice = " << BoolToIniString(boolValues[BI_USE_ALTERNATIVE_VOICE]) << L"\r\n";
  ini << L"use_rate_boost = " << BoolToIniString(boolValues[BI_USE_RATE_BOOST]) << L"\r\n";
  ini << L"use_legacy_rate_algo = " << BoolToIniString(boolValues[BI_USE_LEGACY_RATE_ALGO]) << L"\r\n";
  ini << L"intonation = " << numericValues[NI_INTONATION] << L"\r\n";
  ini << L"\r\n";
  ini << L"[Characters]\r\n";
  for (const auto& e : characters) ini << e.key << L" = " << e.value << L"\r\n";
  ini << L"\r\n";
  ini << L"[SingleCharacters]\r\n";
  for (const auto& e : singleCharacters) ini << e.key << L" = " << e.value << L"\r\n";

  if (!WriteUtf16IniFile(g_app.iniPath, ini.str())) {
    MessageBoxW(owner, L"\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u0437\u0430\u043F\u0438\u0441\u0430\u0442\u044C ini-\u0444\u0430\u0439\u043B.", kWindowTitleBase, MB_OK | MB_ICONERROR);
    return false;
  }

  SetDirty(false);
  return true;
}

void LoadIniToUi() {
  g_app.loadingUi = true;

  ParamReader::Load();

  for (size_t i = 0; i < _countof(kBoolSpecs); ++i) {
    const bool value = ReadParameterBool(g_app.iniPath, kBoolSpecs[i]);
    SendMessageW(g_app.boolCheckboxes[i], BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
  }

  for (size_t i = 0; i < _countof(kNumericSpecs); ++i) {
    const int value = ReadParameterInt(g_app.iniPath, kNumericSpecs[i]);
    SetMainNumericUiValue(static_cast<int>(i), value);
  }

  for (size_t i = 0; i < _countof(kAdvancedNumericSpecs); ++i) {
    int value = ReadParameterInt(g_app.iniPath, kAdvancedNumericSpecs[i]);
    if (i == ANI_INTERPOLATION_MULTIPLIER) {
      value = NormalizeInterpolationMultiplier(value);
    }
    SetAdvancedNumericUiValue(static_cast<int>(i), value);
  }

  std::wstring formRaw;
  if (!ReadParameterString(g_app.iniPath, L"unicode_normalization_form", formRaw)) {
    formRaw = L"NFC";
  }
  const int formIndex = NormalizationIndexFromString(formRaw);
  SendMessageW(g_app.comboNormalization, CB_SETCURSEL, formIndex, 0);
  UpdateNormalizationEnabledState();

  FillDictionaryList(g_app.listCharacters, ReadDictionarySection(g_app.iniPath, L"Characters"));
  FillDictionaryList(g_app.listSingleCharacters, ReadDictionarySection(g_app.iniPath, L"SingleCharacters"));
  ClearDictionaryEditors(g_app.editCharsKey, g_app.editCharsValue);
  ClearDictionaryEditors(g_app.editSingleKey, g_app.editSingleValue);

  g_app.loadingUi = false;
  SetDirty(false);
}

bool RestoreFactoryIni(HWND owner) {
  const DWORD attrs = GetFileAttributesW(g_app.iniPath.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    if (!DeleteFileW(g_app.iniPath.c_str())) {
      MessageBoxW(owner, L"\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u0432\u043E\u0441\u0441\u0442\u0430\u043D\u043E\u0432\u0438\u0442\u044C ini-\u0444\u0430\u0439\u043B \u043F\u043E \u0443\u043C\u043E\u043B\u0447\u0430\u043D\u0438\u044E.", kWindowTitleBase, MB_OK | MB_ICONERROR);
      return false;
    }
  }

  LoadIniToUi();
  return true;
}

bool ConfirmCloseIfDirty(HWND owner) {
  if (!g_app.dirty) return true;

  const int result = MessageBoxW(
      owner,
      L"\u041F\u0430\u0440\u0430\u043C\u0435\u0442\u0440\u044B \u0431\u044B\u043B\u0438 \u0438\u0437\u043C\u0435\u043D\u0435\u043D\u044B. "
      L"\u0421\u043E\u0445\u0440\u0430\u043D\u0438\u0442\u044C \u0438\u0437\u043C\u0435\u043D\u0435\u043D\u0438\u044F "
      L"\u043F\u0435\u0440\u0435\u0434 \u0432\u044B\u0445\u043E\u0434\u043E\u043C?",
      kWindowTitleBase,
      MB_ICONQUESTION | MB_YESNOCANCEL | MB_DEFBUTTON1);

  if (result == IDYES) return SaveIniFromUi(owner);
  if (result == IDNO) return true;
  return false;
}

void InitListViewColumns(HWND listView) {
  ListView_SetExtendedListViewStyle(listView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);

  LVCOLUMNW col{};
  col.mask = LVCF_TEXT | LVCF_WIDTH;

  col.cx = 120;
  col.pszText = const_cast<wchar_t*>(kColumnSymbolText);
  ListView_InsertColumn(listView, 0, &col);

  col.cx = 270;
  col.pszText = const_cast<wchar_t*>(kColumnReplacementText);
  ListView_InsertColumn(listView, 1, &col);
}

int CreateAdditionalControlsOnMain(HWND page, int startY);

void CreateMainPageControls(HWND page) {
  CreateControl(0, L"STATIC", kTabMainText, SS_LEFT, 16, 12, 280, 20, page, -1);

  constexpr int checkboxPositions[][2] = {
    { 16, 40 }, { 320, 40 }, { 16, 68 }, { 320, 68 }, { 16, 96 }, { 320, 96 },
    // 7-й чекбокс (старый алгоритм скорости) -- в третью колонку, чтобы не
    // двигать блок Unicode-нормализации и слайдеры ниже.
    { 624, 40 }
  };

  for (size_t i = 0; i < _countof(kBoolSpecs); ++i) {
    g_app.boolCheckboxes[i] = CreateControl(
        0,
        L"BUTTON",
        kBoolSpecs[i].label,
        BS_AUTOCHECKBOX | WS_TABSTOP,
        checkboxPositions[i][0],
        checkboxPositions[i][1],
        285,
        22,
        page,
        ID_BOOL_BASE + static_cast<int>(i));
  }

  CreateControl(0, L"STATIC", L"\u0424\u043E\u0440\u043C\u0430 Unicode-\u043D\u043E\u0440\u043C\u0430\u043B\u0438\u0437\u0430\u0446\u0438\u0438", SS_LEFT, 16, 126, 230, 20, page, -1);
  g_app.comboNormalization = CreateControl(
      0,
      L"COMBOBOX",
      nullptr,
      CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
      250,
      122,
      170,
      360,
      page,
      ID_COMBO_NORMALIZATION);

  for (const wchar_t* form : kNormalizationForms) {
    SendMessageW(g_app.comboNormalization, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(form));
  }

  int y = 150;
  for (size_t i = 0; i < _countof(kNumericSpecs); ++i) {
    CreateControl(0, L"STATIC", kNumericSpecs[i].label, SS_LEFT, 16, y + 5, 220, 20, page, -1);
    if (IsZeroToHundredRange(kNumericSpecs[i])) {
      g_app.sliders[i] = CreateControl(
          0,
          TRACKBAR_CLASSW,
          nullptr,
          TBS_AUTOTICKS | WS_TABSTOP,
          250,
          y,
          420,
          28,
          page,
          ID_SLIDER_BASE + static_cast<int>(i));
      g_app.valueLabels[i] = CreateControl(
          0,
          L"STATIC",
          L"0",
          SS_LEFT,
          690,
          y + 5,
          100,
          20,
          page,
          ID_VALUE_LABEL_BASE + static_cast<int>(i));

      SendMessageW(g_app.sliders[i], TBM_SETRANGEMIN, FALSE, kNumericSpecs[i].minValue);
      SendMessageW(g_app.sliders[i], TBM_SETRANGEMAX, FALSE, kNumericSpecs[i].maxValue);
      SendMessageW(g_app.sliders[i], TBM_SETLINESIZE, 0, 1);
      const int tick = std::max(1, (kNumericSpecs[i].maxValue - kNumericSpecs[i].minValue) / 10);
      SendMessageW(g_app.sliders[i], TBM_SETTICFREQ, tick, 0);
      y += 34;
    } else {
      g_app.spinEdits[i] = CreateControl(
          WS_EX_CLIENTEDGE,
          L"EDIT",
          L"0",
          ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
          250,
          y,
          170,
          24,
          page,
          ID_SPIN_EDIT_BASE + static_cast<int>(i));
      g_app.spinUpDowns[i] = CreateControl(
          0,
          UPDOWN_CLASSW,
          nullptr,
          UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_AUTOBUDDY,
          0,
          0,
          0,
          0,
          page,
          ID_SPIN_UPDOWN_BASE + static_cast<int>(i));
      SendMessageW(g_app.spinUpDowns[i], UDM_SETRANGE32, kNumericSpecs[i].minValue, kNumericSpecs[i].maxValue);
      SendMessageW(g_app.spinUpDowns[i], UDM_SETPOS32, 0, kNumericSpecs[i].defaultValue);
      y += 28;
    }
  }

  (void)CreateAdditionalControlsOnMain(page, y + 8);
}

int CreateAdditionalControlsOnMain(HWND page, int startY) {
  int y = startY;
  for (size_t i = 0; i < _countof(kAdvancedNumericSpecs); ++i) {
    CreateControl(0, L"STATIC", kAdvancedNumericSpecs[i].label, SS_LEFT, 16, y + 5, 220, 20, page, -1);

    if (i == ANI_INTERPOLATION_MULTIPLIER) {
      g_app.comboInterpolationMultiplier = CreateControl(
          0,
          L"COMBOBOX",
          nullptr,
          CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
          250,
          y,
          170,
          200,
          page,
          ID_COMBO_INTERPOLATION_MULTIPLIER);
      for (const wchar_t* item : kInterpolationMultiplierItems) {
        SendMessageW(g_app.comboInterpolationMultiplier, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
      }
      y += 28;
      continue;
    }

    if (i == ANI_INTERPOLATION_ALGORITHM) {
      g_app.comboInterpolationAlgorithm = CreateControl(
          0,
          L"COMBOBOX",
          nullptr,
          CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
          250,
          y,
          220,
          200,
          page,
          ID_COMBO_INTERPOLATION_ALGORITHM);
      for (const wchar_t* item : kInterpolationAlgorithmItems) {
        SendMessageW(g_app.comboInterpolationAlgorithm, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
      }
      y += 28;
      continue;
    }

    g_app.advancedSpinEdits[i] = CreateControl(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"0",
        ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
        250,
        y,
        170,
        24,
        page,
        ID_ADV_SPIN_EDIT_BASE + static_cast<int>(i));
    g_app.advancedSpinUpDowns[i] = CreateControl(
        0,
        UPDOWN_CLASSW,
        nullptr,
        UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_AUTOBUDDY,
        0,
        0,
        0,
        0,
        page,
        ID_ADV_SPIN_UPDOWN_BASE + static_cast<int>(i));
    SendMessageW(g_app.advancedSpinUpDowns[i], UDM_SETRANGE32, kAdvancedNumericSpecs[i].minValue, kAdvancedNumericSpecs[i].maxValue);
    SendMessageW(g_app.advancedSpinUpDowns[i], UDM_SETPOS32, 0, kAdvancedNumericSpecs[i].defaultValue);
    y += 28;
  }

  return y;
}

void CreateDictionaryPageControls(HWND page) {
  CreateControl(0, L"STATIC", kTabDictionaryText, SS_LEFT, 16, 12, 220, 20, page, -1);
  CreateControl(0, L"BUTTON", L"\u0417\u0430\u043C\u0435\u043D\u044B \u0441\u0438\u043C\u0432\u043E\u043B\u043E\u0432", BS_GROUPBOX, 16, 40, 430, 420, page, -1);
  CreateControl(0, L"BUTTON", L"\u041E\u0434\u0438\u043D\u043E\u0447\u043D\u044B\u0435 \u0437\u0430\u043C\u0435\u043D\u044B \u0441\u0438\u043C\u0432\u043E\u043B\u043E\u0432", BS_GROUPBOX, 472, 40, 430, 420, page, -1);

  g_app.listCharacters = CreateControl(
      WS_EX_CLIENTEDGE,
      WC_LISTVIEWW,
      nullptr,
      LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
      28,
      64,
      406,
      250,
      page,
      ID_LIST_CHARACTERS);
  g_app.listSingleCharacters = CreateControl(
      WS_EX_CLIENTEDGE,
      WC_LISTVIEWW,
      nullptr,
      LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
      484,
      64,
      406,
      250,
      page,
      ID_LIST_SINGLE_CHARACTERS);

  InitListViewColumns(g_app.listCharacters);
  InitListViewColumns(g_app.listSingleCharacters);

  CreateControl(0, L"STATIC", kLabelSymbolText, SS_LEFT, 28, 330, 60, 20, page, -1);
  g_app.editCharsKey = CreateControl(WS_EX_CLIENTEDGE, L"EDIT", nullptr, ES_AUTOHSCROLL | WS_TABSTOP, 96, 326, 120, 24, page, ID_EDIT_CHARS_KEY);
  CreateControl(0, L"STATIC", kLabelReplacementText, SS_LEFT, 224, 330, 60, 20, page, -1);
  g_app.editCharsValue = CreateControl(WS_EX_CLIENTEDGE, L"EDIT", nullptr, ES_AUTOHSCROLL | WS_TABSTOP, 292, 326, 142, 24, page, ID_EDIT_CHARS_VALUE);
  g_app.btnCharsUpsert = CreateControl(0, L"BUTTON", kBtnUpsertText, WS_TABSTOP, 96, 362, 210, 28, page, ID_BTN_CHARS_UPSERT);
  g_app.btnCharsDelete = CreateControl(0, L"BUTTON", kBtnDeleteText, WS_TABSTOP, 312, 362, 122, 28, page, ID_BTN_CHARS_DELETE);

  CreateControl(0, L"STATIC", kLabelSymbolText, SS_LEFT, 484, 330, 60, 20, page, -1);
  g_app.editSingleKey = CreateControl(WS_EX_CLIENTEDGE, L"EDIT", nullptr, ES_AUTOHSCROLL | WS_TABSTOP, 552, 326, 120, 24, page, ID_EDIT_SINGLE_KEY);
  CreateControl(0, L"STATIC", kLabelReplacementText, SS_LEFT, 680, 330, 60, 20, page, -1);
  g_app.editSingleValue = CreateControl(WS_EX_CLIENTEDGE, L"EDIT", nullptr, ES_AUTOHSCROLL | WS_TABSTOP, 748, 326, 142, 24, page, ID_EDIT_SINGLE_VALUE);
  g_app.btnSingleUpsert = CreateControl(0, L"BUTTON", kBtnUpsertText, WS_TABSTOP, 552, 362, 210, 28, page, ID_BTN_SINGLE_UPSERT);
  g_app.btnSingleDelete = CreateControl(0, L"BUTTON", kBtnDeleteText, WS_TABSTOP, 768, 362, 122, 28, page, ID_BTN_SINGLE_DELETE);
}

void SwitchTabPage(int pageIndex) {
  ShowWindow(g_app.pageMain, pageIndex == 0 ? SW_SHOW : SW_HIDE);
  ShowWindow(g_app.pageDictionary, pageIndex == 1 ? SW_SHOW : SW_HIDE);

  HWND focus = GetFocus();
  if (focus) {
    if (pageIndex == 0 && IsChild(g_app.pageDictionary, focus)) {
      SetFocus(g_app.boolCheckboxes[0]);
    } else if (pageIndex == 1 && IsChild(g_app.pageMain, focus)) {
      SetFocus(g_app.listCharacters);
    }
  }
}

std::vector<HWND> BuildTabOrder() {
  std::vector<HWND> order;
  order.reserve(48);

  auto add = [&order](HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) order.push_back(hwnd);
  };

  add(g_app.tab);

  const int tabIndex = (g_app.tab ? TabCtrl_GetCurSel(g_app.tab) : 0);
  if (tabIndex == 0) {
    for (HWND checkbox : g_app.boolCheckboxes) add(checkbox);
    add(g_app.comboNormalization);
    for (size_t i = 0; i < _countof(kNumericSpecs); ++i) {
      if (IsZeroToHundredRange(kNumericSpecs[i])) add(g_app.sliders[i]);
      else add(g_app.spinEdits[i]);
    }
    for (size_t i = 0; i < _countof(kAdvancedNumericSpecs); ++i) {
      if (i == ANI_INTERPOLATION_MULTIPLIER) add(g_app.comboInterpolationMultiplier);
      else if (i == ANI_INTERPOLATION_ALGORITHM) add(g_app.comboInterpolationAlgorithm);
      else add(g_app.advancedSpinEdits[i]);
    }
  } else {
    add(g_app.listCharacters);
    add(g_app.listSingleCharacters);

    add(g_app.editCharsKey);
    add(g_app.editCharsValue);
    add(g_app.btnCharsUpsert);
    add(g_app.btnCharsDelete);

    add(g_app.editSingleKey);
    add(g_app.editSingleValue);
    add(g_app.btnSingleUpsert);
    add(g_app.btnSingleDelete);
  }

  add(g_app.btnApply);
  add(g_app.btnRestore);
  add(g_app.btnExit);

  return order;
}

int FindFocusIndexInOrder(const std::vector<HWND>& order) {
  HWND current = GetFocus();
  while (current) {
    for (size_t i = 0; i < order.size(); ++i) {
      if (order[i] == current) return static_cast<int>(i);
    }
    if (current == g_app.mainWindow) break;
    current = GetParent(current);
  }
  return -1;
}

bool IsTabNavigable(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) return false;
  if (!IsWindowVisible(hwnd) || !IsWindowEnabled(hwnd)) return false;
  return (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_TABSTOP) != 0;
}

bool HandleTabNavigation(const MSG& msg) {
  if (msg.message != WM_KEYDOWN) return false;
  if (msg.wParam != VK_TAB) return false;
  if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) return false;

  const bool reverse = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  const std::vector<HWND> order = BuildTabOrder();
  if (order.empty()) return false;

  int currentIndex = FindFocusIndexInOrder(order);
  if (currentIndex < 0) {
    currentIndex = reverse ? 0 : static_cast<int>(order.size()) - 1;
  }

  const int count = static_cast<int>(order.size());
  const int step = reverse ? -1 : 1;
  for (int i = 0; i < count; ++i) {
    currentIndex = (currentIndex + step + count) % count;
    HWND candidate = order[static_cast<size_t>(currentIndex)];
    if (IsTabNavigable(candidate)) {
      SetFocus(candidate);
      return true;
    }
  }

  return false;
}

void CreateUi(HWND hwnd) {
  g_app.mainWindow = hwnd;
  g_app.uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  g_app.iniPath = ParamReader::IniPath();

  g_app.tab = CreateControl(
      WS_EX_CONTROLPARENT,
      WC_TABCONTROLW,
      nullptr,
      WS_CLIPSIBLINGS | WS_TABSTOP,
      10,
      10,
      948,
      648,
      hwnd,
      ID_TAB);

  TCITEMW tabItem{};
  tabItem.mask = TCIF_TEXT;
  tabItem.pszText = const_cast<wchar_t*>(kTabMainText);
  TabCtrl_InsertItem(g_app.tab, 0, &tabItem);
  tabItem.pszText = const_cast<wchar_t*>(kTabDictionaryText);
  TabCtrl_InsertItem(g_app.tab, 1, &tabItem);

  RECT rcTab{};
  GetClientRect(g_app.tab, &rcTab);
  TabCtrl_AdjustRect(g_app.tab, FALSE, &rcTab);
  // Область страницы приходит в координатах КЛИЕНТА вкладки. Страницы делаем
  // соседями вкладки (родитель -- главное окно), поэтому прямоугольник надо
  // перевести в координаты главного окна: MapWindowPoints по двум углам.
  MapWindowPoints(g_app.tab, hwnd, reinterpret_cast<POINT*>(&rcTab), 2);

  // Родитель страниц -- hwnd, а НЕ вкладка. Дочерние окна вкладки она затирает
  // при своей перерисовке (у SysTabControl32 нет WS_CLIPCHILDREN над ними):
  // контролы остаются в дереве -- скринридер их читает, -- но зрячий видит
  // пустую вкладку.
  //
  // Нужны ОБА флага отсечения, и это ключ ко всему багу:
  //   WS_CLIPSIBLINGS -- вкладка-сосед не закрашивает страницу;
  //   WS_CLIPCHILDREN -- сама страница (STATIC) при перерисовке своего фона не
  //   закрашивает свои же контролы. Без него контейнер стирает детей, а
  //   повторно их никто не инвалидирует -- ровно отсюда "пустая вкладка".
  const DWORD kPageStyle = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
  g_app.pageMain = CreateControl(
      WS_EX_CONTROLPARENT,
      L"STATIC",
      nullptr,
      kPageStyle,
      rcTab.left,
      rcTab.top,
      rcTab.right - rcTab.left,
      rcTab.bottom - rcTab.top,
      hwnd,
      -1);
  g_app.pageDictionary = CreateControl(
      WS_EX_CONTROLPARENT,
      L"STATIC",
      nullptr,
      kPageStyle,
      rcTab.left,
      rcTab.top,
      rcTab.right - rcTab.left,
      rcTab.bottom - rcTab.top,
      hwnd,
      -1);

  CreateMainPageControls(g_app.pageMain);
  CreateDictionaryPageControls(g_app.pageDictionary);
  SwitchTabPage(0);

  g_app.btnApply = CreateControl(0, L"BUTTON", kBtnApplyText, WS_TABSTOP, 470, 672, 140, 30, hwnd, ID_BTN_APPLY);
  g_app.btnRestore = CreateControl(0, L"BUTTON", kBtnRestoreText, WS_TABSTOP, 620, 672, 210, 30, hwnd, ID_BTN_RESTORE);
  g_app.btnExit = CreateControl(0, L"BUTTON", kBtnExitText, WS_TABSTOP, 840, 672, 118, 30, hwnd, ID_BTN_EXIT);

  LoadIniToUi();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE:
      CreateUi(hwnd);
      return 0;

    case WM_HSCROLL: {
      HWND slider = reinterpret_cast<HWND>(lParam);
      if (slider) {
        const int id = GetDlgCtrlID(slider);
        const int idx = GetNumericIndexBySliderId(id);
        if (idx >= 0) {
          SetSliderValueLabel(idx);
          if (!g_app.loadingUi) SetDirty(true);
        }
      }
      return 0;
    }

    case WM_NOTIFY: {
      const auto* nm = reinterpret_cast<NMHDR*>(lParam);
      if (!nm) break;

      if (nm->idFrom == ID_TAB && nm->code == TCN_SELCHANGE) {
        const int tabIndex = TabCtrl_GetCurSel(g_app.tab);
        SwitchTabPage(tabIndex);
        return 0;
      }

      if (nm->code == LVN_ITEMCHANGED) {
        if (nm->idFrom == ID_LIST_CHARACTERS) {
          LoadDictionaryEditorsFromSelection(g_app.listCharacters, g_app.editCharsKey, g_app.editCharsValue);
          return 0;
        }
        if (nm->idFrom == ID_LIST_SINGLE_CHARACTERS) {
          LoadDictionaryEditorsFromSelection(g_app.listSingleCharacters, g_app.editSingleKey, g_app.editSingleValue);
          return 0;
        }
      }
      break;
    }

    case WM_COMMAND: {
      const int id = LOWORD(wParam);
      const int code = HIWORD(wParam);

      if (id == ID_BTN_APPLY && code == BN_CLICKED) {
        SaveIniFromUi(hwnd);
        return 0;
      }
      if (id == ID_BTN_RESTORE && code == BN_CLICKED) {
        const int answer = MessageBoxW(
            hwnd,
            L"\u0412\u043E\u0441\u0441\u0442\u0430\u043D\u043E\u0432\u0438\u0442\u044C ini-\u0444\u0430\u0439\u043B \u043F\u043E \u0443\u043C\u043E\u043B\u0447\u0430\u043D\u0438\u044E?",
            kWindowTitleBase,
            MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
        if (answer == IDYES) RestoreFactoryIni(hwnd);
        return 0;
      }
      if (id == ID_BTN_EXIT && code == BN_CLICKED) {
        SendMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
      }

      const int boolIndex = GetBoolIndexById(id);
      if (boolIndex >= 0 && code == BN_CLICKED) {
        if (boolIndex == BI_USE_UNICODE_NORMALIZATION) {
          UpdateNormalizationEnabledState();
        }
        if (!g_app.loadingUi) SetDirty(true);
        return 0;
      }

      if (id == ID_COMBO_NORMALIZATION && code == CBN_SELCHANGE) {
        if (!g_app.loadingUi) SetDirty(true);
        return 0;
      }
      if ((id == ID_COMBO_INTERPOLATION_MULTIPLIER || id == ID_COMBO_INTERPOLATION_ALGORITHM) && code == CBN_SELCHANGE) {
        if (!g_app.loadingUi) SetDirty(true);
        return 0;
      }

      const int spinIdx = GetNumericIndexBySpinEditId(id);
      if (spinIdx >= 0) {
        if (code == EN_KILLFOCUS) {
          const NumericSpec& spec = kNumericSpecs[spinIdx];
          const int value = ReadSpinEditValue(g_app.spinEdits[spinIdx], spec.minValue, spec.maxValue, spec.defaultValue);
          WriteSpinEditValue(g_app.spinEdits[spinIdx], value);
        }
        if (code == EN_CHANGE && !g_app.loadingUi) SetDirty(true);
        return 0;
      }
      const int advSpinIdx = GetAdvancedNumericIndexBySpinEditId(id);
      if (advSpinIdx >= 0) {
        if (code == EN_KILLFOCUS) {
          const NumericSpec& spec = kAdvancedNumericSpecs[advSpinIdx];
          const int value = ReadSpinEditValue(g_app.advancedSpinEdits[advSpinIdx], spec.minValue, spec.maxValue, spec.defaultValue);
          WriteSpinEditValue(g_app.advancedSpinEdits[advSpinIdx], value);
        }
        if (code == EN_CHANGE && !g_app.loadingUi) SetDirty(true);
        return 0;
      }

      if (id == ID_BTN_CHARS_UPSERT && code == BN_CLICKED) {
        UpsertDictionaryEntry(hwnd, g_app.listCharacters, g_app.editCharsKey, g_app.editCharsValue);
        return 0;
      }
      if (id == ID_BTN_CHARS_DELETE && code == BN_CLICKED) {
        DeleteSelectedDictionaryEntry(g_app.listCharacters, g_app.editCharsKey, g_app.editCharsValue);
        return 0;
      }
      if (id == ID_BTN_SINGLE_UPSERT && code == BN_CLICKED) {
        UpsertDictionaryEntry(hwnd, g_app.listSingleCharacters, g_app.editSingleKey, g_app.editSingleValue);
        return 0;
      }
      if (id == ID_BTN_SINGLE_DELETE && code == BN_CLICKED) {
        DeleteSelectedDictionaryEntry(g_app.listSingleCharacters, g_app.editSingleKey, g_app.editSingleValue);
        return 0;
      }
      break;
    }

    case WM_CLOSE:
      if (ConfirmCloseIfDirty(hwnd)) {
        DestroyWindow(hwnd);
      }
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
  InitCommonControlsEx(&icc);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.hInstance = hInstance;
  wc.lpszClassName = kWindowClassName;
  wc.lpfnWndProc = WindowProc;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

  if (!RegisterClassExW(&wc)) return 1;

  HWND hwnd = CreateWindowExW(
      WS_EX_CONTROLPARENT,
      kWindowClassName,
      kWindowTitleBase,
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      980,
      760,
      nullptr,
      nullptr,
      hInstance,
      nullptr);
  if (!hwnd) return 1;

  ShowWindow(hwnd, nCmdShow);
  UpdateWindow(hwnd);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (HandleTabNavigation(msg)) continue;

    if (!IsDialogMessageW(hwnd, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  return static_cast<int>(msg.wParam);
}
