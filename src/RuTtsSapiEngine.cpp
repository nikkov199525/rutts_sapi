#include <atomic>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <filesystem>

extern std::atomic<long> g_objectCount;

#include "RuTtsSapiEngine.h"
#include "WinUtil.h"
#include "ParamReader.h"
#include "Koi8r.h"

#include <windows.h>

static thread_local struct EngineContext* g_tlsCtx = nullptr;

struct EngineContext {
    ISpTTSEngineSite* site = nullptr;

    int inRate = 10000;
    int outRate = 22050;

    bool hasPrev = false;
    int16_t prev = 0;
    double t = 0.0;
};

static wchar_t Lower1(wchar_t ch) {
    wchar_t tmp = ch;
    CharLowerBuffW(&tmp, 1);
    return tmp;
}

static void ToLowerInplace(std::wstring& s) {
    if (s.empty()) return;
    CharLowerBuffW(s.data(), (DWORD)s.size());
}

static std::wstring StripAngleTags(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    bool inTag = false;
    for (wchar_t ch : in) {
        if (ch == L'<') { inTag = true; continue; }
        if (ch == L'>') { inTag = false; continue; }
        if (!inTag) out.push_back(ch);
    }
    return out;
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
    s = StripAngleTags(s);
    s = RemoveControlChars(s);


    for (auto& ch : s) if (ch == L'(' || ch == L')') ch = L' ';

    CollapseSpacesInplace(s);
    return s;
}


static bool IsAlphaNumW(wchar_t ch) { return (iswalnum(ch) != 0); }
static void AppendWithSmartSpace(std::wstring& dst, const std::wstring& part) {
    if (part.empty()) return;
    if (!dst.empty()) {
        wchar_t a = dst.back();
        wchar_t b = part.front();
        if (IsAlphaNumW(a) && IsAlphaNumW(b)) dst.push_back(L' ');
    }
    dst += part;
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

static std::wstring ApplyCharMap(const std::wstring& s,
                                const std::unordered_map<std::wstring, std::wstring>& charMap)
{
    if (charMap.empty()) return s;

    std::wstring out;
    out.reserve(s.size());

    for (wchar_t ch : s) {
        std::wstring key(1, Lower1(ch));
        auto it = charMap.find(key);
        if (it != charMap.end()) out += it->second;
        else out.push_back(ch);
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

static bool IsAbbrevChar(wchar_t ch) {
    ch = Lower1(ch);
    if (ch == L'b'||ch==L'c'||ch==L'd'||ch==L'f'||ch==L'g'||ch==L'h'||ch==L'j'||ch==L'k'||ch==L'l'||
        ch == L'm'||ch==L'n'||ch==L'p'||ch==L'q'||ch==L'r'||ch==L's'||ch==L't'||ch==L'v'||ch==L'w'||
        ch == L'x'||ch==L'z') return true;
    if (ch == L'б'||ch==L'в'||ch==L'г'||ch==L'д'||ch==L'ж'||ch==L'з'||ch==L'к'||ch==L'л'||ch==L'м'||
        ch == L'н'||ch==L'п'||ch==L'р'||ch==L'с'||ch==L'т'||ch==L'ф'||ch==L'х'||ch==L'ц'||ch==L'ч'||
        ch == L'ш'||ch==L'щ') return true;
    return false;
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
        if (IsAbbrevChar(s[i])) {
            size_t start = i;
            while (i < s.size() && IsAbbrevChar(s[i])) i++;
            size_t len = i - start;

            wchar_t prev = (start > 0) ? s[start - 1] : 0;
            wchar_t next = (i < s.size()) ? s[i] : 0;
            if (len >= 2 && !IsRuOrLatLetter(prev) && !IsRuOrLatLetter(next)) {
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
static std::string WideToAcpZ(const std::wstring& ws) {
    if (ws.empty()) return {};
    int needed = WideCharToMultiByte(CP_ACP, 0, ws.c_str(), -1, nullptr, 0, "?", nullptr);
    if (needed <= 0) return {};
    std::string out((size_t)needed, '\0');
    WideCharToMultiByte(CP_ACP, 0, ws.c_str(), -1, out.data(), needed, "?", nullptr);
    return out;
}

static std::wstring Koi8rToWide(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(20866, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out((size_t)needed, L'\0');
    MultiByteToWideChar(20866, 0, s.c_str(), (int)s.size(), out.data(), needed);
    return out;
}

static const int RULEXDB_SEARCH = 0;
static const int RULEXDB_SUCCESS = 0;
static const int RULEXDB_BUFSIZE = 256;
static const int RULEXDB_MAX_KEY_SIZE = 50;

void RuTtsEngine::EnsureRulexLoaded(const std::wstring& archDir) {
    if (m_rulexDll && m_rulexDb && m_rulexdb_search) return;

    std::filesystem::path pArch(archDir);
    std::filesystem::path pRulexDll = pArch / L"rulex.dll";

    std::filesystem::path pDb1 = pArch.parent_path() / L"rulex.db";
    std::filesystem::path pDb2 = pArch / L"rulex.db";
    std::filesystem::path pDb = std::filesystem::exists(pDb1) ? pDb1 : pDb2;

    if (!std::filesystem::exists(pRulexDll) || !std::filesystem::exists(pDb))
        return;

    HMODULE dll = LoadLibraryW(pRulexDll.c_str());
    if (!dll) return;

#pragma warning(push)
#pragma warning(disable : 4191)
    auto openFn   = reinterpret_cast<rulexdb_open_fn>(GetProcAddress(dll, "rulexdb_open"));
    auto searchFn = reinterpret_cast<rulexdb_search_fn>(GetProcAddress(dll, "rulexdb_search"));
    auto closeFn  = reinterpret_cast<rulexdb_close_fn>(GetProcAddress(dll, "rulexdb_close"));
#pragma warning(pop)

    if (!openFn || !searchFn || !closeFn) { FreeLibrary(dll); return; }

    std::string dbA = WideToAcpZ(pDb.wstring());
    if (dbA.empty()) { FreeLibrary(dll); return; }

    void* db = openFn(dbA.c_str(), RULEXDB_SEARCH);
    if (!db) { FreeLibrary(dll); return; }

    m_rulexDll = dll;
    m_rulexdb_open = openFn;
    m_rulexdb_search = searchFn;
    m_rulexdb_close = closeFn;
    m_rulexDb = db;
}

void RuTtsEngine::UnloadRulex() {
    if (m_rulexDb && m_rulexdb_close) { m_rulexdb_close(m_rulexDb); m_rulexDb = nullptr; }
    m_rulexdb_open = nullptr;
    m_rulexdb_search = nullptr;
    m_rulexdb_close = nullptr;
    if (m_rulexDll) { FreeLibrary(m_rulexDll); m_rulexDll = nullptr; }
}

std::wstring RuTtsEngine::RulexSearchWord(const std::wstring& word) {
    if (!HasRulex()) return word;

    std::wstring lower = word;
    ToLowerInplace(lower);

    std::string key = WideToKoi8r(lower);
    if (key.empty()) return word;
    if ((int)key.size() > RULEXDB_MAX_KEY_SIZE) return word;

    char buf[RULEXDB_BUFSIZE]{};
    int rc = m_rulexdb_search(m_rulexDb, key.c_str(), buf, 0);
    if (rc == RULEXDB_SUCCESS) {
        std::wstring wrec = Koi8rToWide(std::string(buf));
        if (!wrec.empty()) return wrec;
    }
    return word;
}

std::wstring RuTtsEngine::ApplyRulexToRussianWords(const std::wstring& s) {
    if (!HasRulex()) return s;

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
                out += word;
            } else {
                out += RulexSearchWord(word);
            }

            i = j;
            continue;
        }
        out.push_back(s[i]);
        i++;
    }
    return out;
}



static int __cdecl SapiConsumer16(void* buffer, size_t size, void* /*user_data*/) {
    EngineContext* ctx = g_tlsCtx;
    if (!ctx || !ctx->site || !buffer || size == 0) return 1;

    const DWORD act = ctx->site->GetActions();
    if (act & SPVES_ABORT) return 1;

    const int16_t* in = (const int16_t*)buffer;
    const size_t inN = size; 

    if (ctx->inRate == ctx->outRate) {
        const ULONG cb = (ULONG)(inN * sizeof(int16_t));
        ULONG written = 0;
        HRESULT hr = ctx->site->Write(in, cb, &written);
        return FAILED(hr) ? 1 : 0;
    }

    static thread_local std::vector<int16_t> stage;
    stage.clear();
    stage.reserve(inN + 1);
    if (ctx->hasPrev) stage.push_back(ctx->prev);
    stage.insert(stage.end(), in, in + inN);

    if (stage.size() < 2) {
        ctx->prev = stage.back();
        ctx->hasPrev = true;
        return 0;
    }

    const double step = (double)ctx->inRate / (double)ctx->outRate;

    static thread_local std::vector<int16_t> out;
    out.clear();

    while (ctx->t < (double)(stage.size() - 1)) {
        const double x = ctx->t;
        const size_t i0 = (size_t)x;
        const size_t i1 = i0 + 1;
        const double frac = x - (double)i0;

        const double s0 = (double)stage[i0];
        const double s1 = (double)stage[i1];
        const double y = s0 + (s1 - s0) * frac;

        int v = (int)std::lround(y);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out.push_back((int16_t)v);

        ctx->t += step;
    }

    ctx->t -= (double)(stage.size() - 1);
    ctx->prev = stage.back();
    ctx->hasPrev = true;

    if (!out.empty()) {
        const ULONG cb = (ULONG)(out.size() * sizeof(int16_t));
        ULONG written = 0;
        HRESULT hr = ctx->site->Write(out.data(), cb, &written);
        return FAILED(hr) ? 1 : 0;
    }

    return 0;
}

RuTtsEngine::RuTtsEngine() { g_objectCount.fetch_add(1, std::memory_order_relaxed); }

RuTtsEngine::~RuTtsEngine() {
    UnloadRulex();
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
    if(m_token){ m_token->Release(); m_token=nullptr; }
    if(pToken){ pToken->AddRef(); m_token = pToken; }
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
    wfx->nSamplesPerSec = (DWORD)params.sapi_samples_per_sec;
    wfx->wBitsPerSample = 16;
    wfx->nBlockAlign = (wfx->nChannels * wfx->wBitsPerSample) / 8;
    wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
    wfx->cbSize = 0;

    *ppw = wfx;
    return S_OK;
}

static int ClampInt(int v, int lo, int hi){ return (v < lo) ? lo : (v > hi) ? hi : v; }
static float ClampFloat(float v, float lo, float hi){ return (v < lo) ? lo : (v > hi) ? hi : v; }

static float RateAdjToSonicSpeed(long rateAdj){
    float sp = (float)std::pow(1.10, (double)rateAdj);
    return ClampFloat(sp, 0.25f, 4.0f);
}

static float VolumeUShortTo01(USHORT v){
    if(v > 100) v = 100;
    return (float)v / 100.0f;
}

static void ApplyPitchAdjToConf(long pitchAdj, ru_tts_conf_t& conf){
    double factor = std::pow(1.08, (double)pitchAdj);
    int v = (int)std::lround((double)conf.voice_pitch * factor);
    conf.voice_pitch = ClampInt(v, 50, 300);
}

static long GetPitchAdjMiddle(const SPVSTATE& st){
    return (long)st.PitchAdj.MiddleAdj;
}

struct ru_tts_conf_ex_t {
    ru_tts_conf_t base;
    int samples_per_sec;
};

HRESULT RuTtsEngine::Speak(DWORD /*dwSpeakFlags*/, REFGUID, const WAVEFORMATEX*,
                           const SPVTEXTFRAG* frags, ISpTTSEngineSite* site)
{
    if(!site) return E_POINTER;

    RuTtsParams params = ParamReader::Load();

    const std::wstring archDir = GetModuleDir();
    if(!m_api.LoadFromDir(archDir)) return E_FAIL;

    EnsureRulexLoaded(archDir);

    const std::wstring iniPath = ParamReader::IniPath();
    auto mapSingle = ReadIniMap(iniPath, L"SingleCharacters");
    auto mapChars  = ReadIniMap(iniPath, L"Characters");

    EngineContext ctx{};
    ctx.site = site;
    ctx.inRate  = params.samples_per_sec;
    ctx.outRate = params.sapi_samples_per_sec;
    ctx.hasPrev = false;
    ctx.prev = 0;
    ctx.t = 0.0;


    if (ctx.inRate  < 8000)  ctx.inRate  = 8000;
    if (ctx.inRate  > 16000) ctx.inRate  = 16000;
    if (ctx.outRate < 8000)  ctx.outRate = 8000;
    if (ctx.outRate > 22000) ctx.outRate = 22000;

    g_tlsCtx = &ctx;

    std::wstring wtext;
    for (auto* f = frags; f; f = f->pNext) {
        if (!f->pTextStart || f->ulTextLen == 0) continue;

        std::wstring part(f->pTextStart, f->pTextStart + f->ulTextLen);
        part = SanitizeText(std::move(part));
        if (part.empty()) continue;

        switch (f->State.eAction) {
        case SPVA_Speak:
        case SPVA_Pronounce:
            AppendWithSmartSpace(wtext, part);
            break;

        case SPVA_SpellOut: {
            std::wstring spelled = SpeakSpellOut(part, mapSingle);
            spelled = SanitizeText(std::move(spelled));
            if (!spelled.empty()) AppendWithSmartSpace(wtext, spelled);
            break;
        }
        default:
            break;
        }
    }

    wtext = SanitizeText(std::move(wtext));
    if (wtext.empty()) { g_tlsCtx = nullptr; return S_OK; }

    
    if (wtext.size() == 1) {
        wtext = SpeakSpellOut(wtext, mapSingle);
    } else {
        wtext = ReplaceSingleLatinManual(wtext, mapSingle);
        wtext = ReplaceAbbreviationsManual(wtext, mapSingle);
        wtext = SplitLetterAfterNumber(wtext);
        wtext = ApplyCharMap(wtext, mapChars);
    }

    wtext = SanitizeText(std::move(wtext));
    if (wtext.empty()) { g_tlsCtx = nullptr; return S_OK; }
    wtext = ReplaceAccentInRussianWords(wtext);

    
    wtext = ApplyRulexToRussianWords(wtext);

    wtext = SanitizeText(std::move(wtext));
    if (wtext.empty()) { g_tlsCtx = nullptr; return S_OK; }
NormalizeForKoi8rInplace(wtext);
    std::string koi8 = WideToKoi8r(wtext);
    if (koi8.empty()) { g_tlsCtx = nullptr; return S_OK; }
    for (char& c : koi8) if (c == '\0') c = ' ';

    ru_tts_conf_ex_t confEx{};
    m_api.config_init(&confEx.base);
    ParamReader::ApplyToConf(params, confEx.base);

    
    confEx.samples_per_sec = params.samples_per_sec;

    USHORT vol = 100;
    if (FAILED(site->GetVolume(&vol))) {
        ULONG v = frags ? frags->State.Volume : 100;
        if (v > 100) v = 100;
        vol = (USHORT)v;
    }

    long rateAdj = 0;
    if (FAILED(site->GetRate(&rateAdj))) {
        rateAdj = frags ? frags->State.RateAdj : 0;
    }

long pitchAdj = 0;

// применять pitch ТОЛЬКО если реально был текст
if (!wtext.empty() && frags) {
    pitchAdj = GetPitchAdjMiddle(frags->State);
    ApplyPitchAdjToConf(pitchAdj, confEx.base);
}


    TTS_t* tts = m_api.tts_create(SapiConsumer16);
    if (!tts) { g_tlsCtx = nullptr; return E_FAIL; }

    m_api.tts_setVolume(tts, VolumeUShortTo01(vol));
    m_api.tts_setSpeed(tts, RateAdjToSonicSpeed(rateAdj));

    m_api.tts_speak(tts, reinterpret_cast<const ru_tts_conf_t*>(&confEx), koi8.c_str());

    m_api.tts_destroy(tts);
    g_tlsCtx = nullptr;
    return S_OK;
}
