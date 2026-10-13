#include <atomic>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <unordered_map>

extern std::atomic<long> g_objectCount;

#include "RuTtsSapiEngine.h"
#include "WinUtil.h"
#include "ParamReader.h"
#include "Koi8r.h"
#include "samplerate.h"

#include <windows.h>

static thread_local struct EngineContext* g_tlsCtx = nullptr;
static std::atomic<struct EngineContext*> g_activeCtx{ nullptr };

enum class CallbackInputFormat {
    Raw8BitBytes,
    Pcm16Samples,
    Pcm16Bytes
};

struct EngineContext {
    ISpTTSEngineSite* site = nullptr;

    int inRate = 10000;
    // Частота и разрядность потока, который хост попросил в Speak.
    int outRate = 10000;
    int outBits = 16;
    // Итоговое усиление -- громкость клиента (SetVolume) на громкость фрагмента
    // (<volume level>): SAPI велит их перемножать.
    int gainPercent = 100;
    int baseVolume = 100;
    int segmentVolume = 100;
    int interpolationAlgorithm = SRC_LINEAR;
    CallbackInputFormat inputFormat = CallbackInputFormat::Raw8BitBytes;

    SRC_STATE* converter = nullptr;
    std::vector<int16_t> wave16;
    std::vector<float> inFloat;
    std::vector<float> outFloat;
    std::vector<int16_t> silence16;
    std::vector<uint8_t> wave8;

    // Последние сэмплы звука, придержанные до того, как станет ясно, хвост ли это
    // (см. WritePcmSamples и FinishSound). tailLen -- длина затухания в сэмплах.
    std::vector<int16_t> tail;
    size_t tailLen = 0;

    // Клиент снял фразу (ABORT/SKIP/SP_AUDIO_STOPPED). Раз поднятый флаг гасит
    // всю дальнейшую запись в site до конца этого вызова Speak.
    bool sapiStop = false;

    // Сколько байт уже отдано site->Write за этот вызов Speak. Это и есть
    // ullAudioStreamOffset для событий-закладок: SAPI отсчитывает их от начала
    // потока, а поток начинается вместе с вызовом Speak.
    std::uint64_t audioBytes = 0;

    // Звук куска, придержанный до того, как станет известна его длина в байтах.
    // Нужно только когда в сегменте есть закладки: смещение события считается в
    // БАЙТАХ выходного потока, а сколько их даст ресемплер, заранее не знает
    // никто. Придержали -> измерили -> расставили события -> отдали звук.
    std::vector<uint8_t> capture;
    bool capturing = false;

    // Скорость клиента (SetRate). К ней прибавляется скорость фрагмента
    // (<rate>), а conf перестраивается на границе сегмента: внутри transfer его
    // трогать нельзя. Поэтому SPVES_RATE посреди фразы здесь просто запоминается.
    long baseRateAdj = 0;
};

// SP_AUDIO_STOPPED -- это КОД УСПЕХА (0x00045065), а не ошибка: FAILED() его не
// ловит. Без явной проверки движок продолжал синтезировать уже снятую клиентом
// фразу -- отсюда задержки, обрывы и наложение, особенно в JAWS, где речь
// снимается на каждом нажатии клавиши. На некоторых сборках символ не объявлен.
#ifndef SP_AUDIO_STOPPED
#define SP_AUDIO_STOPPED ((HRESULT)0x00045065L)
#endif

static int ClampInt(int v, int lo, int hi);

static void UpdateGain(EngineContext* ctx) {
    ctx->gainPercent = ClampInt(ctx->baseVolume * ctx->segmentVolume / 100, 0, 100);
}

// Снимает запросы клиента, приходящие ПОСРЕДИ фразы. Возвращает false, когда
// синтез надо прекратить (отмена/пропуск). Громкость применяется на лету --
// усиление берётся в колбэке для следующих кусков. Скорость ru_tts внутри
// одного transfer сменить нельзя: новое значение запоминается до следующего
// сегмента, а чтение GetRate снимает бит, чтобы SAPI не поднимала его снова.
static bool CheckSapiActions(EngineContext* ctx) {
    if (!ctx || !ctx->site || ctx->sapiStop) return false;

    const DWORD actions = ctx->site->GetActions();
    if (actions & SPVES_ABORT) {
        ctx->sapiStop = true;
        return false;
    }

    if (actions & SPVES_SKIP) {
        SPVSKIPTYPE skipType = SPVST_SENTENCE;
        long skipItems = 0;
        (void)ctx->site->GetSkipInfo(&skipType, &skipItems);
        (void)ctx->site->CompleteSkip(0);
        ctx->sapiStop = true;
        return false;
    }

    if (actions & SPVES_VOLUME) {
        USHORT volume = 100;
        if (SUCCEEDED(ctx->site->GetVolume(&volume))) {
            ctx->baseVolume = ClampInt(static_cast<int>(volume), 0, 100);
            UpdateGain(ctx);
        }
    }

    if (actions & SPVES_RATE) {
        long rateAdj = 0;
        if (SUCCEEDED(ctx->site->GetRate(&rateAdj))) {
            ctx->baseRateAdj = rateAdj;
        }
    }

    return true;
}

static wchar_t Lower1(wchar_t ch) {
    wchar_t tmp = ch;
    CharLowerBuffW(&tmp, 1);
    return tmp;
}

static void ToLowerInplace(std::wstring& s) {
    if (s.empty()) return;
    CharLowerBuffW(s.data(), (DWORD)s.size());
}

// Разметку разбирает сама SAPI, и до движка теги не доходят: '<' и '>' в тексте
// фрагмента -- это настоящие знаки ("a < b", код, &lt; из XML). Раньше они
// считались тегом, и всё от '<' до '>' или до конца фразы молча пропадало.
// Читать их ядро не умеет, поэтому они просто становятся пробелами.
static std::wstring BlankAngleBrackets(std::wstring s) {
    for (auto& ch : s) if (ch == L'<' || ch == L'>') ch = L' ';
    return s;
}

static bool IsIgnorableFormatChar(wchar_t ch) {
    if (ch == 0xFEFF) return true;
    if (ch == 0x00AD) return true;
    if (ch >= 0x200B && ch <= 0x200F) return true;
    if (ch >= 0x202A && ch <= 0x202E) return true;
    if (ch >= 0x2066 && ch <= 0x2069) return true;
    if (ch >= 0xFE00 && ch <= 0xFE0F) return true;
    return false;
}

static std::wstring RemoveControlChars(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    for (wchar_t ch : in) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t' || ch == L' ') { out.push_back(ch); continue; }


        if (ch == 0 || ch < 0x20) continue;

        if (IsIgnorableFormatChar(ch)) continue;

        out.push_back(ch);
    }
    return out;
}



static void TrimWsEnds(std::wstring& s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' || s.front() == L'\r' || s.front() == L'\n'))
        s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r' || s.back() == L'\n'))
        s.pop_back();
}

static void CollapseSpacesInplace(std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    bool prevSpace = false;
    for (wchar_t ch : s) {
        const bool isWs = (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n');
        if (isWs) {
            if (!prevSpace) out.push_back(L' ');
            prevSpace = true;
        } else {
            out.push_back(ch);
            prevSpace = false;
        }
    }
    s.swap(out);
    TrimWsEnds(s);
}

static std::wstring SanitizeText(std::wstring s) {
    s = BlankAngleBrackets(std::move(s));
    s = RemoveControlChars(s);


    for (auto& ch : s) if (ch == L'(' || ch == L')') ch = L' ';

    CollapseSpacesInplace(s);
    return s;
}

static NORM_FORM ToWindowsNormForm(UnicodeNormalizationForm form) {
    switch (form) {
    case UnicodeNormalizationForm::NFKC: return NormalizationKC;
    case UnicodeNormalizationForm::NFD:  return NormalizationD;
    case UnicodeNormalizationForm::NFKD: return NormalizationKD;
    case UnicodeNormalizationForm::NFC:
    default:
        return NormalizationC;
    }
}

static std::wstring ApplyUnicodeNormalizationIfNeeded(const std::wstring& text, const RuTtsParams& params) {
    if (!params.use_unicode_normalization || text.empty()) return text;

    const NORM_FORM form = ToWindowsNormForm(params.unicode_normalization_form);
    int needed = NormalizeString(form, text.c_str(), -1, nullptr, 0);
    if (needed <= 0) return text;

    std::wstring out(static_cast<size_t>(needed), L'\0');
    int written = NormalizeString(form, text.c_str(), -1, out.data(), needed);
    if (written <= 0) return text;

    out.resize(static_cast<size_t>(written - 1));
    return out;
}


static std::unordered_map<std::wstring, std::wstring> ReadIniMap(const std::wstring& iniPath, const wchar_t* section) {
    std::vector<wchar_t> buf(64 * 1024);
    DWORD n = GetPrivateProfileSectionW(section, buf.data(), (DWORD)buf.size(), iniPath.c_str());
    if (n == 0 || n >= buf.size() - 2) return {};

    auto trim = [](std::wstring& x) {
        while (!x.empty() && (x.front() == L' ' || x.front() == L'\t')) x.erase(x.begin());
        while (!x.empty() && (x.back()  == L' ' || x.back()  == L'\t')) x.pop_back();
    };

    std::unordered_map<std::wstring, std::wstring> out;
    const wchar_t* p = buf.data();

    while (*p) {
        std::wstring line = p;
        p += line.size() + 1;

        auto eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;

        std::wstring key = line.substr(0, eq);
        std::wstring val = line.substr(eq + 1);
        trim(key); trim(val);

        ToLowerInplace(key);

        if (!key.empty())
            out.emplace(std::move(key), std::move(val));
    }
    return out;
}

static bool CanEncodeKoi8r(wchar_t ch) {
    char out = 0;
    BOOL usedDefault = FALSE;
    const wchar_t w[2] = { ch, 0 };
    int n = WideCharToMultiByte(20866, WC_NO_BEST_FIT_CHARS, w, 1, &out, 1, "?", &usedDefault);
    if (n != 1) return false;
    if (ch == L'?') return true;        // '?' допустим
    return usedDefault == FALSE;        // если заменили defaultChar -> не кодируется
}

static void NormalizeForKoi8rInplace(std::wstring& s) {
    for (auto& ch : s) {
        const unsigned u = (unsigned)ch;

        // Все виды тире/минусов -> ASCII '-'
        if (u == 0x2014u || u == 0x2013u || u == 0x2212u || u == 0x2012u || u == 0x2011u) {
            ch = L'-';
            continue;
        }

        // "умные" кавычки -> обычные
        if (u == 0x201Cu || u == 0x201Du || u == 0x00ABu || u == 0x00BBu) {
            ch = L'"';
            continue;
        }
        if (u == 0x2018u || u == 0x2019u) {
            ch = L'\'';
            continue;
        }

        // всё, что KOI8-R не умеет -> пробел
        if (!CanEncodeKoi8r(ch)) {
            ch = L' ';
        }
    }

    CollapseSpacesInplace(s);
}

static std::wstring SpeakSpellOut(const std::wstring& s,
                                 const std::unordered_map<std::wstring, std::wstring>& singleMap)
{
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        std::wstring key(1, Lower1(s[i]));
        auto it = singleMap.find(key);
        if (it != singleMap.end()) out += it->second;
        else out.push_back(s[i]);
        if (i + 1 < s.size()) out.push_back(L' ');
    }
    return out;
}

// В [Characters] ключом может быть не только буква, но и сочетание ("sh",
// "ee"). Совпадения ищутся от самого длинного ключа, иначе "e = е" срабатывало
// бы раньше "ee = и" и сочетание бы не дожило.
static std::vector<std::pair<std::wstring, std::wstring>> SortCharMap(
    const std::unordered_map<std::wstring, std::wstring>& charMap)
{
    std::vector<std::pair<std::wstring, std::wstring>> sorted(charMap.begin(), charMap.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        if (a.first.size() != b.first.size()) return a.first.size() > b.first.size();
        return a.first < b.first;
    });
    return sorted;
}

static std::wstring ApplyCharMap(const std::wstring& s,
                                const std::vector<std::pair<std::wstring, std::wstring>>& charMap)
{
    if (charMap.empty()) return s;

    // Ключи в ini уже приведены к нижнему регистру, поэтому ищем по копии текста.
    std::wstring lower = s;
    ToLowerInplace(lower);

    std::wstring out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size();) {
        bool matched = false;
        for (const auto& pair : charMap) {
            if (pair.first.empty() || pair.first.size() > s.size() - i) continue;
            if (lower.compare(i, pair.first.size(), pair.first) != 0) continue;
            out += pair.second;
            i += pair.first.size();
            matched = true;
            break;
        }
        if (!matched) out.push_back(s[i++]);
    }
    return out;
}
static bool IsLatinLetter(wchar_t ch) {
    ch = Lower1(ch);
    return (ch >= L'a' && ch <= L'z');
}

static bool IsCyrLetter(wchar_t ch) {
    return (ch >= 0x0410 && ch <= 0x044F) || ch == 0x0401 || ch == 0x0451;
}

static bool IsAccent(wchar_t ch) { return ch == 0x0301; }
static bool IsWordChar(wchar_t ch) { return IsCyrLetter(ch) || IsAccent(ch); }

static bool IsRuOrLatLetter(wchar_t ch) {
    return IsLatinLetter(ch) || IsCyrLetter(ch);
}

static bool IsUpperRuOrLatLetter(wchar_t ch) {
    return (ch >= L'A' && ch <= L'Z') ||
        (ch >= 0x0410 && ch <= 0x042F) || ch == 0x0401;
}

static bool IsLowerRuOrLatLetter(wchar_t ch) {
    return IsRuOrLatLetter(ch) && !IsUpperRuOrLatLetter(ch);
}

static bool IsAbbrevChar(wchar_t ch) {
    ch = Lower1(ch);
    switch (ch) {
    case L'b': case L'c': case L'd': case L'f': case L'g': case L'h':
    case L'j': case L'k': case L'l': case L'm': case L'n': case L'p':
    case L'q': case L'r': case L's': case L't': case L'v': case L'w':
    case L'x': case L'z':
    case L'б': case L'в': case L'г': case L'д': case L'ж':
    case L'з': case L'к': case L'л': case L'м': case L'н':
    case L'п': case L'р': case L'с': case L'т': case L'ф':
    case L'х': case L'ц': case L'ч': case L'ш': case L'щ':
        return true;
    default:
        return false;
    }
}

// По буквам читаются только сплошные согласные: ТТС, ФСБ, HTML, PNG.
// Слова с гласными (POCO, OZON, ВНИМАНИЕ, НАТО, GIF) не трогаем.
static bool ShouldSpellRun(const std::wstring& run) {
    if (run.size() < 2) return false;
    for (wchar_t ch : run) {
        if (!IsAbbrevChar(ch)) return false;
    }
    return true;
}

// Разрезает camelCase, чтобы аббревиатура внутри слова стала отдельным
// фрагментом: chatGPT -> "chat GPT", RuTTS -> "Ru TTS", TTSEngine -> "TTS Engine".
static std::wstring SplitCamelCase(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);

    for (size_t i = 0; i < s.size(); ++i) {
        if (i > 0 && IsUpperRuOrLatLetter(s[i])) {
            const wchar_t prev = s[i - 1];
            const bool nextIsLower = (i + 1 < s.size()) && IsLowerRuOrLatLetter(s[i + 1]);
            // строчная -> Заглавная (chatGPT), либо ЗАГЛАВНАЯ -> Заглавная+строчная (TTSEngine)
            if (IsLowerRuOrLatLetter(prev) || (IsUpperRuOrLatLetter(prev) && nextIsLower)) {
                out.push_back(L' ');
            }
        }
        out.push_back(s[i]);
    }
    return out;
}
static std::wstring ReplaceSingleLatinManual(const std::wstring& s,
    const std::unordered_map<std::wstring, std::wstring>& singleMap)
{
    std::wstring out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t ch = s[i];
        if (IsLatinLetter(ch)) {
            wchar_t prev = (i > 0) ? s[i - 1] : 0;
            wchar_t next = (i + 1 < s.size()) ? s[i + 1] : 0;
            if (!IsRuOrLatLetter(prev) && !IsRuOrLatLetter(next)) {
                std::wstring key(1, Lower1(ch));
                auto it = singleMap.find(key);
                if (it != singleMap.end()) { out += it->second; continue; }
            }
        }
        out.push_back(ch);
    }
    return out;
}

static std::wstring ReplaceAbbreviationsManual(const std::wstring& s,
    const std::unordered_map<std::wstring, std::wstring>& singleMap)
{
    std::wstring out;
    out.reserve(s.size() * 2);

    size_t i = 0;
    while (i < s.size()) {
        if (IsRuOrLatLetter(s[i])) {
            size_t start = i;
            while (i < s.size() && IsRuOrLatLetter(s[i])) i++;
            size_t len = i - start;

            if (ShouldSpellRun(s.substr(start, len))) {
                for (size_t k = 0; k < len; ++k) {
                    std::wstring key(1, Lower1(s[start + k]));
                    auto it = singleMap.find(key);
                    if (it != singleMap.end()) out += it->second;
                    else out.push_back(s[start + k]);
                    if (k + 1 < len) out.push_back(L' ');
                }
                continue;
            } else {
                out.append(s, start, len);
                continue;
            }
        }

        out.push_back(s[i]);
        i++;
    }

    return out;
}
static std::wstring SplitLetterAfterNumber(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); ++i) {
        out.push_back(s[i]);
        if (i + 1 < s.size()) {
            if (iswdigit(s[i]) && (iswalpha(s[i + 1]) != 0)) out.push_back(L' ');
        }
    }
    return out;
}

static std::wstring ReplaceAccentInRussianWords(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());

    size_t i = 0;
    while (i < s.size()) {
        if (IsWordChar(s[i])) {
            size_t j = i;
            while (j < s.size() && IsWordChar(s[j])) j++;
            std::wstring word = s.substr(i, j - i);

            size_t acc = word.find((wchar_t)0x0301);
            if (acc != std::wstring::npos) {
                word.replace(acc, 1, L"+");

                for (auto& ch : word) if (ch == (wchar_t)0x0301) ch = L' ';
            }
            out += word;

            i = j;
            continue;
        }
        out.push_back(s[i]);
        i++;
    }

    std::wstring t = out;
    CollapseSpacesInplace(t);
    return t;
}

constexpr size_t kSiteWriteChunk = 4096;

static int WriteAllToSite(EngineContext* ctx, const void* data, size_t bytes) {
    if (!ctx || !ctx->site) return 1;
    if (bytes == 0) return 0;
    if (!data) return 1;

    // Сегмент придержан ради закладок -- звук пока копится в буфере, а не уходит
    // клиенту. Отдаст его EndCaptureAndWrite, когда события уже расставлены.
    if (ctx->capturing) {
        if (!CheckSapiActions(ctx)) return 1;
        const BYTE* first = static_cast<const BYTE*>(data);
        ctx->capture.insert(ctx->capture.end(), first, first + bytes);
        return 0;
    }

    const BYTE* ptr = static_cast<const BYTE*>(data);
    size_t left = bytes;
    int zeroWriteStreak = 0;

    while (left > 0) {
        if (!CheckSapiActions(ctx)) return 1;

        // Порциями: один огромный site->Write мог бы надолго заблокироваться, и
        // снятие речи между порциями не проверялось бы.
        const ULONG ask = static_cast<ULONG>((std::min)(left, kSiteWriteChunk));

        ULONG written = 0;
        HRESULT hr = ctx->site->Write(ptr, ask, &written);
        if (FAILED(hr) || written > ask) {
            ctx->sapiStop = true;
            return 1;
        }
        // Успешный, но означающий остановку устройства код: клиент снял речь.
        // FAILED() его пропускает -- проверяем явно (см. #define выше).
        if (hr == SP_AUDIO_STOPPED) {
            ctx->sapiStop = true;
            return 1;
        }
        if (written == 0) {
            // Some hosts can temporarily accept 0 bytes without hard failure.
            if (++zeroWriteStreak >= 64) {
                ctx->sapiStop = true;
                return 1;
            }
            Sleep(0);
            continue;
        }
        zeroWriteStreak = 0;
        ctx->audioBytes += written;

        ptr += written;
        left -= written;
    }

    return 0;
}

// Начать придержку звука текущего сегмента (есть закладки).
static void BeginCapture(EngineContext* ctx) {
    if (!ctx) return;
    ctx->capture.clear();
    ctx->capturing = true;
}

// Отдать придержанный звук клиенту -- теми же порциями, что и обычно: одним
// куском site->Write мог бы заблокироваться надолго, и проверка на снятие речи
// между порциями не сработала бы.
static int EndCaptureAndWrite(EngineContext* ctx) {
    if (!ctx) return 1;
    ctx->capturing = false;
    const size_t total = ctx->capture.size();
    constexpr size_t kChunk = kSiteWriteChunk;
    size_t offset = 0;
    while (offset < total) {
        const size_t take = (total - offset < kChunk) ? (total - offset) : kChunk;
        if (WriteAllToSite(ctx, ctx->capture.data() + offset, take) != 0) {
            ctx->capture.clear();
            return 1;
        }
        offset += take;
    }
    ctx->capture.clear();
    return 0;
}

// Событие закладки (<bookmark mark="..."/>). Ставится ДО записи звука, которому
// оно соответствует: SAPI стреляет событием, когда воспроизведение дойдёт до
// ullAudioStreamOffset, а событие, поставленное ПОСЛЕ своего звука, выстрелить
// вовремя уже не сможет (TTS Engine Vendor Porting Guide, "Queuing Events").
// Именно по закладкам JAWS ведёт непрерывное чтение: пока событие не пришло, он
// не знает, докуда дочитано.
static bool QueueBookmarkEvent(EngineContext* ctx, const std::wstring& name, std::uint64_t audioOffset) {
    if (!ctx || !ctx->site) return false;
    // "Смещение обязано приходиться на границу сэмпла": 2 байта у PCM16, 1 у PCM8.
    if (ctx->outBits == 16) audioOffset &= ~static_cast<std::uint64_t>(1);

    // lParam обязан быть НОЛЬ-ТЕРМИНИРОВАННОЙ копией имени: SAPI читает строку по
    // указателю прямо внутри AddEvents, а текст фрагмента в её списке не
    // терминирован (он кусок общего буфера).
    const std::wstring markName = name;
    long asNumber = 0;
    if (!markName.empty()) {
        wchar_t* end = nullptr;
        const long parsed = wcstol(markName.c_str(), &end, 10);
        // "Если имя закладки не число, wParam равен нулю."
        if (end != markName.c_str()) asNumber = parsed;
    }

    SPEVENT event{};
    event.eEventId = SPEI_TTS_BOOKMARK;
    event.elParamType = SPET_LPARAM_IS_STRING;
    event.ulStreamNum = 0;  // проставляет SAPI
    event.ullAudioStreamOffset = audioOffset;
    event.wParam = static_cast<WPARAM>(asNumber);
    event.lParam = reinterpret_cast<LPARAM>(markName.c_str());

    return SUCCEEDED(ctx->site->AddEvents(&event, 1));
}

// Ядро обрывает звук посреди волны: последний сэмпл фразы бывает далеко от
// нуля, а следом сразу тишина. Такая ступенька слышна щелчком на конце каждой
// реплики. В JAWS реплик особенно много -- отдельная фраза на каждую клавишу и
// каждый фокус, -- отсюда "артефакты на концах". Последние миллисекунды звука
// ядра поэтому плавно гасятся до нуля.
constexpr int kFadeOutMs = 5;

static void FreeAudioPipeline(EngineContext* ctx) {
    if (!ctx) return;
    if (ctx->converter) {
        src_delete(ctx->converter);
        ctx->converter = nullptr;
    }
    ctx->wave16.clear();
    ctx->inFloat.clear();
    ctx->outFloat.clear();
    ctx->silence16.clear();
    ctx->tail.clear();
    ctx->capture.clear();
    ctx->capturing = false;
}

static bool InitAudioPipeline(EngineContext* ctx) {
    if (!ctx) return false;

    if (ctx->inRate < 8000) ctx->inRate = 8000;
    if (ctx->inRate > 192000) ctx->inRate = 192000;
    if (ctx->outRate < 8000) ctx->outRate = 8000;
    if (ctx->outRate > 192000) ctx->outRate = 192000;
    if (ctx->gainPercent < 0) ctx->gainPercent = 0;
    if (ctx->gainPercent > 100) ctx->gainPercent = 100;
    ctx->tailLen = static_cast<size_t>(ctx->outRate) * kFadeOutMs / 1000;

    if (ctx->inRate == ctx->outRate) {
        return true;
    }

    int error = 0;
    ctx->converter = src_new(ctx->interpolationAlgorithm, 1, &error);
    return ctx->converter != nullptr && error == 0;
}

static int16_t ApplyGain(int16_t sample, int gainPercent) {
    if (gainPercent >= 100) return sample;
    if (gainPercent <= 0) return 0;

    int scaled = (static_cast<int>(sample) * gainPercent) / 100;
    if (scaled > 32767) scaled = 32767;
    if (scaled < -32768) scaled = -32768;
    return static_cast<int16_t>(scaled);
}

// Внутри звук идёт как PCM16; если хост попросил 8 бит, он понижается здесь.
static int EmitPcmSamples(EngineContext* ctx, const int16_t* samples, size_t count) {
    if (!ctx || !samples || count == 0) return 0;
    if (ctx->outBits != 8) return WriteAllToSite(ctx, samples, count * sizeof(int16_t));

    ctx->wave8.resize(count);
    for (size_t i = 0; i < count; ++i) {
        // 8-битный PCM в WAVE -- беззнаковый, с нулём в 128. Округление, а не
        // отбрасывание младшего байта: у пересчитанного звука тот половину
        // значений уводил бы вниз. Ровные 8-битные отсчёты ядра не страдают.
        const int rounded = (static_cast<int>(samples[i]) + 128) >> 8;
        ctx->wave8[i] = static_cast<uint8_t>(ClampInt(rounded, -128, 127) + 128);
    }
    return WriteAllToSite(ctx, ctx->wave8.data(), count);
}

// Отдаёт звук, но последние tailLen сэмплов придерживает: пока ядро не
// закончило, неизвестно, хвост это или середина фразы. Отдаст или погасит их
// следующий кусок либо FinishSound.
static int WritePcmSamples(EngineContext* ctx, const int16_t* samples, size_t count) {
    if (!ctx || !samples || count == 0) return 0;
    if (ctx->tailLen == 0) return EmitPcmSamples(ctx, samples, count);

    std::vector<int16_t>& tail = ctx->tail;
    if (count >= ctx->tailLen) {
        if (EmitPcmSamples(ctx, tail.data(), tail.size()) != 0) return 1;
        const size_t ready = count - ctx->tailLen;
        if (EmitPcmSamples(ctx, samples, ready) != 0) return 1;
        tail.assign(samples + ready, samples + count);
        return 0;
    }

    tail.insert(tail.end(), samples, samples + count);
    if (tail.size() <= ctx->tailLen) return 0;
    const size_t ready = tail.size() - ctx->tailLen;
    if (EmitPcmSamples(ctx, tail.data(), ready) != 0) return 1;
    tail.erase(tail.begin(), tail.begin() + static_cast<std::ptrdiff_t>(ready));
    return 0;
}

// Звук кончился (конец transfer ядра или паузы): придержанный хвост гасится
// половиной косинуса до нуля и отдаётся. Через закладки придержанного сегмента
// хвост тоже проходит до измерения его длины, поэтому смещения событий верны.
static int FinishSound(EngineContext* ctx) {
    if (!ctx) return 1;
    std::vector<int16_t>& tail = ctx->tail;
    const size_t n = tail.size();
    constexpr double kPi = 3.14159265358979323846;
    for (size_t i = 0; i < n; ++i) {
        const double gain = 0.5 * (1.0 + std::cos(kPi * static_cast<double>(i + 1) / static_cast<double>(n)));
        tail[i] = static_cast<int16_t>(std::lround(tail[i] * gain));
    }
    const int result = EmitPcmSamples(ctx, tail.data(), n);
    tail.clear();
    return result;
}

static int ProcessAndWriteSamples(EngineContext* ctx, const int16_t* inSamples, size_t inCount) {
    if (!ctx || !ctx->site || !inSamples || inCount == 0) return 0;

    if (!ctx->converter || ctx->inRate == ctx->outRate) {
        return WritePcmSamples(ctx, inSamples, inCount);
    }

    if (inCount > static_cast<size_t>(std::numeric_limits<int>::max())) return 1;
    if (inCount > static_cast<size_t>(std::numeric_limits<long>::max())) return 1;

    ctx->inFloat.resize(inCount);
    src_short_to_float_array(inSamples, ctx->inFloat.data(), static_cast<int>(inCount));

    const double ratio = static_cast<double>(ctx->outRate) / static_cast<double>(ctx->inRate);
    if (!src_is_valid_ratio(ratio)) return 1;

    size_t outCap = static_cast<size_t>(std::ceil(static_cast<double>(inCount) * ratio)) + 64;
    if (outCap < 64) outCap = 64;
    if (outCap > static_cast<size_t>(std::numeric_limits<long>::max())) return 1;

    ctx->outFloat.resize(outCap);

    SRC_DATA srcData{};
    srcData.data_in = ctx->inFloat.data();
    srcData.input_frames = static_cast<long>(inCount);
    srcData.data_out = ctx->outFloat.data();
    srcData.output_frames = static_cast<long>(outCap);
    srcData.src_ratio = ratio;
    srcData.end_of_input = 0;

    if (src_process(ctx->converter, &srcData) != 0) return 1;
    if (srcData.output_frames_gen <= 0) return 0;
    if (srcData.output_frames_gen > std::numeric_limits<int>::max()) return 1;

    const size_t outCount = static_cast<size_t>(srcData.output_frames_gen);
    ctx->wave16.resize(outCount);
    src_float_to_short_array(ctx->outFloat.data(), ctx->wave16.data(), static_cast<int>(outCount));
    return WritePcmSamples(ctx, ctx->wave16.data(), outCount);
}

static int FlushResampler(EngineContext* ctx) {
    if (!ctx || !ctx->site || !ctx->converter || ctx->inRate == ctx->outRate) return 0;

    const double ratio = static_cast<double>(ctx->outRate) / static_cast<double>(ctx->inRate);
    if (!src_is_valid_ratio(ratio)) return 1;

    ctx->outFloat.resize(512);
    // src_process отвергает data_in == nullptr (SRC_ERR_BAD_DATA_PTR) даже при
    // нуле входных кадров -- а ошибка здесь обрывает фразу и выбрасывает звук,
    // придержанный ради закладок. Поэтому нужен настоящий, хоть и пустой, буфер.
    if (ctx->inFloat.empty()) ctx->inFloat.resize(1);
    while (true) {
        SRC_DATA srcData{};
        srcData.data_in = ctx->inFloat.data();
        srcData.input_frames = 0;
        srcData.data_out = ctx->outFloat.data();
        srcData.output_frames = static_cast<long>(ctx->outFloat.size());
        srcData.src_ratio = ratio;
        srcData.end_of_input = 1;

        if (src_process(ctx->converter, &srcData) != 0) return 1;
        if (srcData.output_frames_gen <= 0) break;
        if (srcData.output_frames_gen > std::numeric_limits<int>::max()) return 1;

        const size_t outCount = static_cast<size_t>(srcData.output_frames_gen);
        ctx->wave16.resize(outCount);
        src_float_to_short_array(ctx->outFloat.data(), ctx->wave16.data(), static_cast<int>(outCount));
        if (WritePcmSamples(ctx, ctx->wave16.data(), outCount) != 0) return 1;

        if (srcData.output_frames_gen < srcData.output_frames) break;
    }

    src_reset(ctx->converter);
    return 0;
}

static int WriteSilenceMs(EngineContext* ctx, int milliseconds) {
    if (!ctx || !ctx->site || milliseconds <= 0) return 0;

    const long long totalSamplesLL =
        (static_cast<long long>(ctx->outRate) * static_cast<long long>(milliseconds)) / 1000;
    if (totalSamplesLL <= 0) return 0;
    if (static_cast<unsigned long long>(totalSamplesLL) >
        static_cast<unsigned long long>((std::numeric_limits<size_t>::max)())) {
        return 1;
    }

    size_t totalSamples = static_cast<size_t>(totalSamplesLL);
    size_t chunkSamples = static_cast<size_t>(ctx->outRate / 10);
    if (chunkSamples == 0) chunkSamples = 1;

    if (ctx->silence16.size() < chunkSamples) {
        ctx->silence16.assign(chunkSamples, 0);
    }

    while (totalSamples > 0) {
        const size_t n = (totalSamples < chunkSamples) ? totalSamples : chunkSamples;
        if (WritePcmSamples(ctx, ctx->silence16.data(), n) != 0) return 1;
        totalSamples -= n;
    }
    // Нули гасить незачем, но придерживать их дольше паузы тоже нельзя.
    return FinishSound(ctx);
}

static int __cdecl SapiConsumer16(void* buffer, size_t size, void* user_data) {
    EngineContext* ctx = user_data ? static_cast<EngineContext*>(user_data) : nullptr;
    if (!ctx) ctx = g_tlsCtx;
    if (!ctx) ctx = g_activeCtx.load(std::memory_order_acquire);
    if (!ctx || !ctx->site) return 1;
    // Empty callback chunk is not fatal; just continue synthesis.
    if (size == 0) return 0;
    if (!buffer) return 1;

    // Снимает стоп/пропуск/громкость на границе куска ядра. Ненулевой возврат
    // прекращает transfer -- ru_tts перестаёт синтезировать снятую фразу.
    if (!CheckSapiActions(ctx)) return 1;

    size_t inSamples = 0;
    switch (ctx->inputFormat) {
    case CallbackInputFormat::Pcm16Samples:
        inSamples = size;
        break;
    case CallbackInputFormat::Pcm16Bytes:
        inSamples = size / sizeof(int16_t);
        break;
    case CallbackInputFormat::Raw8BitBytes:
    default:
        inSamples = size;
        break;
    }
    if (inSamples == 0) return 0;

    ctx->wave16.resize(inSamples);
    if (ctx->inputFormat == CallbackInputFormat::Raw8BitBytes) {
        const int8_t* in8 = static_cast<const int8_t*>(buffer);
        for (size_t i = 0; i < inSamples; ++i) {
            const int16_t sample = static_cast<int16_t>(static_cast<int16_t>(in8[i]) << 8);
            ctx->wave16[i] = ApplyGain(sample, ctx->gainPercent);
        }
    } else {
        const int16_t* in16 = static_cast<const int16_t*>(buffer);
        for (size_t i = 0; i < inSamples; ++i) {
            ctx->wave16[i] = ApplyGain(in16[i], ctx->gainPercent);
        }
    }

    return ProcessAndWriteSamples(ctx, ctx->wave16.data(), ctx->wave16.size());
}

// --- Разбор фразы на упорядоченные сегменты --------------------------------
//
// Раньше Speak склеивал весь текст в одну строку и синтезировал одним transfer.
// Из-за этого терялись порядок и позиции: закладки (<bookmark>) и паузы
// (<silence>) выпадали целиком. Теперь фрагменты SAPI раскладываются в
// упорядоченный список сегментов -- текст (с привязанными закладками), пауза --
// и каждый обрабатывается на своём месте.

enum class SpeechSegmentKind {
    Text,
    Silence
};

// Закладка клиента. JAWS ставит её после КАЖДОГО слова и по событиям ведёт
// непрерывное чтение. Запоминаем не только имя, но и место в тексте сегмента --
// по нему потом считается смещение в звуке, на котором SAPI обязана событие
// выстрелить.
struct SpeechMark {
    std::wstring name;
    size_t textIndex = 0;
};

// Состояние голоса из XML фрагмента (SPVSTATE). JAWS задаёт им скорость,
// громкость и высоту КАЖДОЙ фразы (<rate AbsSpeed>, <volume level>,
// <pitch AbsMiddle>), а высоту ещё и отдельным словам -- заглавным. SAPI
// требует складывать скорость фрагмента со скоростью клиента (GetRate) и
// перемножать громкости; без этого скорость в JAWS не менялась вовсе.
struct VoiceState {
    long rateAdj = 0;
    int volume = 100;
    long pitchMiddle = 0;
    long pitchRange = 0;

    bool operator==(const VoiceState& o) const {
        return rateAdj == o.rateAdj && volume == o.volume &&
            pitchMiddle == o.pitchMiddle && pitchRange == o.pitchRange;
    }
};

static VoiceState VoiceStateOf(const SPVSTATE& state) {
    VoiceState v;
    v.rateAdj = state.RateAdj;
    v.volume = static_cast<int>((state.Volume > 100) ? 100 : state.Volume);
    v.pitchMiddle = state.PitchAdj.MiddleAdj;
    v.pitchRange = state.PitchAdj.RangeAdj;
    return v;
}

struct SpeechSegment {
    SpeechSegmentKind kind = SpeechSegmentKind::Text;
    std::wstring text;
    bool singleMode = false;
    VoiceState voice;
    std::uint64_t sourceEnd = 0;
    int silenceMs = 0;
    std::vector<SpeechMark> marks;
};

// Знаки, которые пишут вплотную к слову СЛЕВА: пробел перед ними не нужен.
static bool ClingsToWordBefore(wchar_t ch) {
    switch (ch) {
    case L',': case L'.': case L'!': case L'?': case L';': case L':':
    case L')': case L']': case L'}': case L'%':
    case 0x00BB: case 0x2019: case 0x201D: case 0x2026: // » ’ ” …
        return true;
    default:
        return false;
    }
}

// ...и те, что пишут вплотную СПРАВА: после них пробел тоже не ставят.
static bool ClingsToWordAfter(wchar_t ch) {
    switch (ch) {
    case L'(': case L'[': case L'{':
    case 0x00AB: case 0x2018: case 0x201C: // « ‘ “
        return true;
    default:
        return false;
    }
}

// Текстовый фрагмент присоединяется к предыдущему текстовому сегменту того же
// режима и состояния голоса, а не заводит новый: иначе каждое слово ушло бы в
// ядро отдельным transfer и интонация рассыпалась бы. Сменилось состояние --
// новый сегмент: скорость и высоту ядру задают на весь transfer. Если между
// фрагментами в исходном тексте был разрыв смещений -- значит там была граница
// слова, и её надо вернуть.
static void AppendTextSegment(
    std::vector<SpeechSegment>& segments,
    const std::wstring& text,
    bool singleMode,
    const VoiceState& voice,
    ULONG sourceOffset) {
    if (text.empty()) return;
    // Сегмент из одних закладок (текста ещё нет) состояния не имеет -- берёт
    // состояние первого текста.
    if (!segments.empty() && segments.back().kind == SpeechSegmentKind::Text &&
        segments.back().text.empty()) {
        segments.back().singleMode = singleMode;
        segments.back().voice = voice;
    }
    if (!segments.empty() && segments.back().kind == SpeechSegmentKind::Text &&
        segments.back().singleMode == singleMode && segments.back().voice == voice) {
        SpeechSegment& segment = segments.back();
        const std::uint64_t offset = sourceOffset;
        const bool sourceCharactersOmitted = offset > segment.sourceEnd;
        const bool sourceOffsetsReset = offset < segment.sourceEnd;
        if ((sourceCharactersOmitted || sourceOffsetsReset) && !segment.text.empty()) {
            const wchar_t left = segment.text.back();
            const wchar_t right = text.front();
            if (!std::iswspace(left) && !std::iswspace(right) &&
                !ClingsToWordBefore(right) && !ClingsToWordAfter(left)) {
                segment.text.push_back(L' ');
            }
        }
        segment.text += text;
        segment.sourceEnd = offset + text.size();
        return;
    }
    SpeechSegment segment;
    segment.kind = SpeechSegmentKind::Text;
    segment.text = text;
    segment.singleMode = singleMode;
    segment.voice = voice;
    segment.sourceEnd = static_cast<std::uint64_t>(sourceOffset) + text.size();
    segments.push_back(std::move(segment));
}

// Закладка цепляется к текущему текстовому сегменту, а НЕ рвёт его. JAWS ставит
// закладку после каждого слова; если резать по ним, каждое слово уедет отдельным
// transfer. Место закладки внутри текста запоминается для расчёта смещения.
static void AppendBookmark(std::vector<SpeechSegment>& segments, const std::wstring& name) {
    if (segments.empty() || segments.back().kind != SpeechSegmentKind::Text) {
        // Закладка пришла раньше любого текста -- ей нужен свой пустой сегмент,
        // иначе событие потеряется.
        SpeechSegment segment;
        segment.kind = SpeechSegmentKind::Text;
        segments.push_back(std::move(segment));
    }
    SpeechSegment& segment = segments.back();
    segment.marks.push_back({ name, segment.text.size() });
}

// <silence msec="N"/> -- фрагмент SPVA_Silence. Текста у него нет, поэтому
// раньше он отсеивался проверкой на пустую длину и пауза просто пропадала.
static void AppendSilence(std::vector<SpeechSegment>& segments, int milliseconds) {
    if (milliseconds <= 0) return;
    SpeechSegment segment;
    segment.kind = SpeechSegmentKind::Silence;
    segment.silenceMs = milliseconds;
    segments.push_back(std::move(segment));
}

// Единственный признак посимвольного чтения у SAPI -- SPVA_SpellOut, а JAWS его
// не шлёт: буква приходит обычным текстом. Отсюда правило: вся реплика ровно из
// одного символа -- это эхо клавиши, и читать её надо посимвольно (с именами из
// [SingleCharacters] и без чистки, которая съела бы одиночные '<' и '>'). Целой
// строкой текста один символ не приходит. NVDA такого не знает -- там режим
// приходит отдельной командой.
static void ApplySingleCharacterMode(std::vector<SpeechSegment>& segments) {
    SpeechSegment* onlyText = nullptr;
    for (SpeechSegment& segment : segments) {
        if (segment.kind != SpeechSegmentKind::Text) return;
        if (segment.text.empty()) continue;
        if (onlyText) return;
        onlyText = &segment;
    }
    if (!onlyText || onlyText->singleMode) return;

    std::wstring trimmed = onlyText->text;
    TrimWsEnds(trimmed);
    // Суррогатная пара -- тоже один символ, поэтому не просто size() == 1.
    const bool oneCharacter =
        trimmed.size() == 1 ||
        (trimmed.size() == 2 &&
         trimmed[0] >= 0xD800 && trimmed[0] <= 0xDBFF &&
         trimmed[1] >= 0xDC00 && trimmed[1] <= 0xDFFF);
    if (!oneCharacter) return;

    onlyText->text = trimmed;
    onlyText->singleMode = true;
}

// Смещение закладки в байтах внутри уже синтезированного сегмента. Байты
// известны точно (сегмент для этого и придержан), а куда внутри него пришлось
// слово -- считается долей от числа исходных символов. Это оценка, и честнее её
// движок дать не может: ядро не сообщает, докуда дочитало текст.
static std::uint64_t MarkAudioOffset(std::uint64_t segmentStart,
                                     size_t segmentBytes,
                                     size_t withinChars,
                                     size_t totalChars) {
    if (totalChars == 0) return segmentStart;
    if (withinChars > totalChars) withinChars = totalChars;
    return segmentStart +
        (static_cast<std::uint64_t>(segmentBytes) * withinChars) / totalChars;
}

// Полная подготовка текста одного сегмента к синтезу (без koi8 -- он в Speak).
// В посимвольном режиме имена букв из [SingleCharacters] подставляются ДО
// чистки: иначе SanitizeText превратил бы одиночные '<' и '>' в пробелы.
static std::wstring PrepareSegmentWide(
    std::wstring wtext,
    bool singleMode,
    const RuTtsParams& params,
    const std::unordered_map<std::wstring, std::wstring>& mapSingle,
    const std::vector<std::pair<std::wstring, std::wstring>>& mapChars,
    const RulexClient& rulex,
    bool rulexReady)
{
    if (singleMode) {
        wtext = SpeakSpellOut(wtext, mapSingle);
        wtext = SanitizeText(std::move(wtext));
        if (wtext.empty()) return {};
    } else {
        wtext = SanitizeText(std::move(wtext));
        if (wtext.empty()) return {};
        wtext = ApplyUnicodeNormalizationIfNeeded(wtext, params);
        if (wtext.empty()) return {};
        wtext = SplitCamelCase(wtext);
        wtext = ReplaceSingleLatinManual(wtext, mapSingle);
        wtext = ReplaceAbbreviationsManual(wtext, mapSingle);
        wtext = SplitLetterAfterNumber(wtext);
        wtext = ApplyCharMap(wtext, mapChars);
        wtext = SanitizeText(std::move(wtext));
        if (wtext.empty()) return {};
    }

    wtext = ReplaceAccentInRussianWords(wtext);
    if (rulexReady) {
        wtext = rulex.ApplyToRussianWords(wtext);
    }
    wtext = SanitizeText(std::move(wtext));
    return wtext;
}

// Журнал того, что присылает клиент: флаги, SetRate/SetVolume и состояние
// каждого фрагмента. По нему видно, как хост задаёт скорость -- через SetRate,
// через <rate> или обоими (JAWS шлёт <rate AbsSpeed> с каждой фразой).
// Включается созданием пустого sapi_trace.log рядом с rutts.ini: конфигуратор
// этот файл не трогает. Пишется только в существующий файл и не больше 4 МБ.
static void TraceSpeak(DWORD flags, const WAVEFORMATEX* format, long siteRate, USHORT siteVolume,
                       const SPVTEXTFRAG* frags) {
    const std::wstring path = ParamReader::TracePath();
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(file);
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t head[256];
    swprintf(head, 256, L"%02u:%02u:%02u.%03u pid %lu flags 0x%lX %luHz/%ubit GetRate %ld GetVolume %u\r\n",
             now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentProcessId(),
             static_cast<unsigned long>(flags), format ? format->nSamplesPerSec : 0ul,
             format ? format->wBitsPerSample : 0u, siteRate, siteVolume);
    std::wstring line = head;
    for (const SPVTEXTFRAG* f = frags; f; f = f->pNext) {
        wchar_t state[160];
        swprintf(state, 160, L"  action %d rate %ld volume %lu pitch %ld/%ld silence %lu: ",
                 static_cast<int>(f->State.eAction), f->State.RateAdj, f->State.Volume,
                 f->State.PitchAdj.MiddleAdj, f->State.PitchAdj.RangeAdj, f->State.SilenceMSecs);
        line += state;
        if (f->pTextStart) line.append(f->pTextStart, (std::min)(f->ulTextLen, 200ul));
        line += L"\r\n";
    }

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes > 0) {
        std::string utf8(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()),
                            utf8.data(), bytes, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(file);
}

RuTtsEngine::RuTtsEngine() { g_objectCount.fetch_add(1, std::memory_order_relaxed); }

RuTtsEngine::~RuTtsEngine() {
    if (m_token) { m_token->Release(); m_token = nullptr; }
    m_api.Unload();
    g_objectCount.fetch_sub(1, std::memory_order_relaxed);
}

HRESULT RuTtsEngine::QueryInterface(REFIID riid, void** ppv){
    if(!ppv) return E_POINTER;
    *ppv = nullptr;

    if(riid == IID_IUnknown || riid == __uuidof(ISpTTSEngine))
        *ppv = static_cast<ISpTTSEngine*>(this);
    else if(riid == __uuidof(ISpObjectWithToken))
        *ppv = static_cast<ISpObjectWithToken*>(this);
    else
        return E_NOINTERFACE;

    AddRef();
    return S_OK;
}

ULONG RuTtsEngine::AddRef(){ return (ULONG)InterlockedIncrement((LONG*)&m_ref); }

ULONG RuTtsEngine::Release(){
    ULONG r = (ULONG)InterlockedDecrement((LONG*)&m_ref);
    if(r==0) delete this;
    return r;
}

HRESULT RuTtsEngine::SetObjectToken(ISpObjectToken* pToken){
    if (!pToken) return E_POINTER;
    pToken->AddRef();
    if(m_token) m_token->Release();
    m_token = pToken;
    return S_OK;
}

HRESULT RuTtsEngine::GetObjectToken(ISpObjectToken** ppToken){
    if(!ppToken) return E_POINTER;
    *ppToken = m_token;
    if(m_token) m_token->AddRef();
    return S_OK;
}

HRESULT RuTtsEngine::GetOutputFormat(const GUID*, const WAVEFORMATEX*, GUID* pid, WAVEFORMATEX** ppw){
    if(!pid || !ppw) return E_POINTER;

    RuTtsParams params = ParamReader::Load();

    *pid = SPDFID_WaveFormatEx;
    auto* wfx = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
    if(!wfx) return E_OUTOFMEMORY;

    wfx->wFormatTag = WAVE_FORMAT_PCM;
    wfx->nChannels = 1;
    wfx->nSamplesPerSec = (DWORD)params.output_sample_rate;
    wfx->wBitsPerSample = 16;
    wfx->nBlockAlign = (wfx->nChannels * wfx->wBitsPerSample) / 8;
    wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
    wfx->cbSize = 0;

    *ppw = wfx;
    return S_OK;
}

static int ClampInt(int v, int lo, int hi){ return (v < lo) ? lo : (v > hi) ? hi : v; }

static int ToSrcConverterType(InterpolationAlgorithm alg) {
    return (alg == InterpolationAlgorithm::ZeroOrderHold) ? SRC_ZERO_ORDER_HOLD : SRC_LINEAR;
}

static long NormalizeRateAdj(long rawAdj) {
    long v = rawAdj;
    if (v > 100) v = 100;
    if (v < -100) v = -100;
    // Some hosts pass adjustment in [-100..100], while others use [-10..10].
    if (v > 10 || v < -10) {
        v = static_cast<long>(std::lround(static_cast<double>(v) / 10.0));
    }
    if (v > 10) v = 10;
    if (v < -10) v = -10;
    return v;
}

static int MapNormalizedAdjToRange(long normalizedAdj, int baseValue, int minValue, int maxValue) {
    const long adj = ClampInt(static_cast<int>(normalizedAdj), -10, 10);
    const int base = ClampInt(baseValue, minValue, maxValue);
    if (adj == 0) return base;
    if (adj > 0) {
        const double t = static_cast<double>(adj) / 10.0;
        const int span = maxValue - base;
        return ClampInt(base + static_cast<int>(std::lround(span * t)), minValue, maxValue);
    }
    const double t = static_cast<double>(-adj) / 10.0;
    const int span = base - minValue;
    return ClampInt(base - static_cast<int>(std::lround(span * t)), minValue, maxValue);
}

constexpr int kVoicePitchMin = 50;
constexpr int kVoicePitchMax = 300;
constexpr int kPitchAdjInputMin = -24;
constexpr int kPitchAdjInputMax = 24;

constexpr int kIntonationMin = 0;
constexpr int kIntonationMax = 140;

// Высота голоса -- от MiddleAdj (<pitch absmiddle>), размах интонации -- от
// RangeAdj (<pitch range>). Раньше оба шли в высоту (бралось большее по
// модулю), и <pitch range> менял высоту вместо интонации.
static int MapPitchAdjToVoicePitch(long rawPitchAdj) {
    const long raw = static_cast<long>(ClampInt(static_cast<int>(rawPitchAdj), kPitchAdjInputMin, kPitchAdjInputMax));
    const long rawOffset = raw - kPitchAdjInputMin;
    const long rawSpan = kPitchAdjInputMax - kPitchAdjInputMin;
    const int span = kVoicePitchMax - kVoicePitchMin;
    return ClampInt(
        kVoicePitchMin + static_cast<int>((rawOffset * static_cast<long>(span)) / rawSpan),
        kVoicePitchMin,
        kVoicePitchMax);
}

// Скорость клиента плюс скорость фрагмента, обрезанные до шкалы SAPI. Так же
// поступают и голоса Microsoft: <rate AbsSpeed> у них тоже прибавляется.
static long CombineRateAdj(long baseAdj, long fragmentAdj) {
    const long sum = NormalizeRateAdj(baseAdj) + NormalizeRateAdj(fragmentAdj);
    return (sum < -10) ? -10 : (sum > 10) ? 10 : sum;
}

static void ApplyRateAdjToConf(long normalizedRateAdj, bool useRateBoost, ru_tts_conf_t& conf) {
    constexpr int kSpeechRateMin = 20;
    constexpr int kSpeechRateMax = 250;
    constexpr int kSpeechRateHardMax = 500;

    int v = MapNormalizedAdjToRange(normalizedRateAdj, conf.speech_rate, kSpeechRateMin, kSpeechRateMax);
    if (useRateBoost) {
        // User-requested behavior: boost must stay active regardless of SAPI speed.
        const long long boosted = static_cast<long long>(v) * 2LL;
        v = (boosted > static_cast<long long>(kSpeechRateHardMax))
            ? kSpeechRateHardMax
            : static_cast<int>(boosted);
    }
    conf.speech_rate = (v < kSpeechRateMin) ? kSpeechRateMin : v;
}

HRESULT RuTtsEngine::Speak(DWORD dwSpeakFlags, REFGUID formatId, const WAVEFORMATEX* format,
                           const SPVTEXTFRAG* frags, ISpTTSEngineSite* site)
{
    if(!site) return E_POINTER;

    RuTtsParams params = ParamReader::Load();

    const std::wstring archDir = GetModuleDir();
    if(!m_api.LoadFromDir(archDir)) return E_FAIL;

    if (params.use_rulex) {
        m_rulex.EnsureLoaded(archDir);
        if (!HasRulex()) {
            m_rulex.EnsureLoaded(archDir);
        }
    }

    const std::wstring iniPath = ParamReader::IniPath();
    auto mapSingle = ReadIniMap(iniPath, L"SingleCharacters");
    const auto mapChars = SortCharMap(ReadIniMap(iniPath, L"Characters"));

    // Пишем ровно в том формате, который хост выбрал в Speak: обычно это наш же
    // GetOutputFormat, но SAPI вправе попросить и другой (частота, 8 бит).
    if (formatId != SPDFID_WaveFormatEx || !format || format->wFormatTag != WAVE_FORMAT_PCM ||
        format->nChannels != 1 || (format->wBitsPerSample != 8 && format->wBitsPerSample != 16) ||
        format->nSamplesPerSec < 8000 || format->nSamplesPerSec > 192000)
        return E_INVALIDARG;

    EngineContext ctx{};
    ctx.site = site;
    ctx.inRate = params.samples_per_sec;
    ctx.outRate = static_cast<int>(format->nSamplesPerSec);
    ctx.outBits = format->wBitsPerSample;
    ctx.interpolationAlgorithm = ToSrcConverterType(params.interpolation_algorithm);
    ctx.inputFormat = CallbackInputFormat::Raw8BitBytes;

    if (!InitAudioPipeline(&ctx)) {
        return E_FAIL;
    }

    g_tlsCtx = &ctx;
    g_activeCtx.store(&ctx, std::memory_order_release);

    auto clearCtx = [&ctx]() {
        if (g_tlsCtx == &ctx) {
            g_tlsCtx = nullptr;
        }
        EngineContext* expected = &ctx;
        (void)g_activeCtx.compare_exchange_strong(
            expected,
            nullptr,
            std::memory_order_release,
            std::memory_order_relaxed);
        FreeAudioPipeline(&ctx);
    };
    auto done = [&clearCtx](HRESULT hr) {
        clearCtx();
        return hr;
    };

    // --- Разбор фрагментов в упорядоченные сегменты --------------------------
    std::vector<SpeechSegment> segments;

    for (auto* f = frags; f; f = f->pNext) {
        // Пустая длина больше НЕ значит "пропустить фрагмент": у закладки и у
        // <silence> своего текста нет, а раньше проверка на неё стояла до разбора
        // действия -- и оба сорта фрагментов отсеивались, не доходя до switch.
        std::wstring part;
        if (f->pTextStart && f->ulTextLen != 0) {
            part.assign(f->pTextStart, f->pTextStart + f->ulTextLen);
        }

        switch (f->State.eAction) {
        case SPVA_Speak:
        case SPVA_Pronounce:
            if (part.empty()) break;
            AppendTextSegment(segments, part, false, VoiceStateOf(f->State), f->ulTextSrcOffset);
            break;

        case SPVA_SpellOut:
            if (part.empty()) break;
            AppendTextSegment(segments, part, true, VoiceStateOf(f->State), f->ulTextSrcOffset);
            break;

        case SPVA_Bookmark:
            // Текст такого фрагмента -- ИМЯ закладки, а не речь: говорить нельзя,
            // но и молча выбрасывать тоже -- клиент ждёт события.
            AppendBookmark(segments, part);
            break;

        case SPVA_Silence: {
            ULONG ms = f->State.SilenceMSecs;
            if (ms > 60000) ms = 60000;
            AppendSilence(segments, static_cast<int>(ms));
            break;
        }

        default:
            break;
        }
    }

    if (segments.empty()) return done(S_OK);
    ApplySingleCharacterMode(segments);

    // --- conf фразы; скорость, высота и громкость -- у каждого сегмента свои ---
    ru_tts_conf_t conf{};
    m_api.config_init(&conf);
    ParamReader::ApplyToConf(params, conf);
    // Скорость и интонация отсчитываются от настроек, а не от уже сдвинутых
    // значений: иначе каждый сегмент складывался бы с предыдущим.
    const int baseSpeechRate = conf.speech_rate;
    const int baseIntonation = conf.intonation;

    // Базовые значения клиента (SetVolume/SetRate). Не прочлись -- остаются
    // нейтральными: состояние фрагментов всё равно применится.
    USHORT vol = 100;
    if (SUCCEEDED(site->GetVolume(&vol))) ctx.baseVolume = ClampInt(static_cast<int>(vol), 0, 100);
    long rateAdj = 0;
    if (SUCCEEDED(site->GetRate(&rateAdj))) ctx.baseRateAdj = rateAdj;
    TraceSpeak(dwSpeakFlags, format, rateAdj, vol, frags);

    const bool rulexReady = params.use_rulex && HasRulex();
    const size_t waveBufferSize =
        static_cast<size_t>(ClampInt(params.wave_buffer_size, 256, 1024000));
    std::vector<uint8_t> waveBuf(waveBufferSize);

    // Тишина в начале фразы -- один раз, до сегментов.
    if (WriteSilenceMs(&ctx, params.silence_at_begin) != 0) return done(S_OK);

    // --- Проход по сегментам -------------------------------------------------
    for (const SpeechSegment& segment : segments) {
        if (!CheckSapiActions(&ctx)) return done(S_OK);

        if (segment.kind == SpeechSegmentKind::Silence) {
            if (WriteSilenceMs(&ctx, segment.silenceMs) != 0) return done(S_OK);
            continue;
        }

        size_t nextMark = 0;
        // Всё, что не разошлось по звуку, обязано выстрелить до конца потока:
        // именно последнюю закладку клиент ждёт, чтобы читать дальше. Отказ
        // AddEvents речь НЕ прерывает -- событие дело служебное.
        auto flushRemainingMarks = [&]() {
            while (nextMark < segment.marks.size()) {
                (void)QueueBookmarkEvent(&ctx, segment.marks[nextMark].name, ctx.audioBytes);
                nextMark++;
            }
        };

        std::wstring prepared = PrepareSegmentWide(
            segment.text, segment.singleMode, params, mapSingle, mapChars, m_rulex, rulexReady);
        if (prepared.empty()) {
            // Текста не осталось (пробелы, разметка, вычищенные знаки), но
            // закладки к нему всё равно привязаны -- иначе непрерывное чтение
            // встанет на пустой строке.
            flushRemainingMarks();
            continue;
        }

        // Состояние голоса сегмента. Скорость берётся заново от текущей
        // скорости клиента: SPVES_RATE посреди фразы задаёт её, а не прибавляет.
        conf.speech_rate = baseSpeechRate;
        ApplyRateAdjToConf(CombineRateAdj(ctx.baseRateAdj, segment.voice.rateAdj), params.use_rate_boost, conf);
        conf.voice_pitch = MapPitchAdjToVoicePitch(segment.voice.pitchMiddle);
        conf.intonation = MapNormalizedAdjToRange(
            ClampInt(static_cast<int>(segment.voice.pitchRange), -10, 10),
            baseIntonation, kIntonationMin, kIntonationMax);
        ctx.segmentVolume = segment.voice.volume;
        UpdateGain(&ctx);

        NormalizeForKoi8rInplace(prepared);
        std::string koi8 = WideToKoi8r(prepared);
        for (char& c : koi8) if (c == '\0') c = ' ';
        if (koi8.empty()) {
            flushRemainingMarks();
            continue;
        }

        const std::uint64_t segmentStart = ctx.audioBytes;
        const bool holdForMarks = !segment.marks.empty();

        // Закладки заставляют придержать звук сегмента: пока он не
        // досинтезирован, неизвестно, сколько в нём байт, а без этого событие
        // некуда ставить. Задержка тут -- время СИНТЕЗА (единицы миллисекунд), а
        // не звучания. Без закладок ничего не придерживается, и путь звука для
        // всех прочих клиентов остаётся прежним байт в байт.
        if (holdForMarks) BeginCapture(&ctx);

        m_api.transfer(
            &conf,
            koi8.c_str(),
            waveBuf.data(),
            waveBuf.size(),
            SapiConsumer16,
            &ctx);
        if (ctx.sapiStop) return done(S_OK);

        // Досдать хвост ресемплера и погасить конец звука, чтобы звук сегмента
        // был полон ДО измерения его длины и расстановки событий. src_reset
        // внутри готовит конвертер к следующему сегменту.
        if (FlushResampler(&ctx) != 0) return done(S_OK);
        if (FinishSound(&ctx) != 0) return done(S_OK);
        if (ctx.sapiStop) return done(S_OK);

        if (holdForMarks) {
            const size_t segmentBytes = ctx.capture.size();
            const size_t totalChars = segment.text.size();
            // События ставятся ДО записи своего звука -- иначе SAPI не сможет
            // выстрелить ими вовремя.
            for (const SpeechMark& mark : segment.marks) {
                const std::uint64_t offset =
                    MarkAudioOffset(segmentStart, segmentBytes, mark.textIndex, totalChars);
                (void)QueueBookmarkEvent(&ctx, mark.name, offset);
            }
            nextMark = segment.marks.size();
            if (EndCaptureAndWrite(&ctx) != 0) return done(S_OK);
        }
    }

    // Тишина в конце фразы -- один раз, после сегментов.
    if (ctx.sapiStop) return done(S_OK);
    if (WriteSilenceMs(&ctx, params.silence_at_end) != 0) return done(S_OK);
    return done(S_OK);
}
