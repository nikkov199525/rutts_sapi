#include "RulexApi.h"
#include "RulexDllApi.h"

#include <array>
#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>

namespace {
constexpr int RULEXDB_SEARCH = 0;
constexpr int RULEXDB_SUCCESS = 0;
constexpr int RULEXDB_BUFSIZE = 256;
constexpr int RULEXDB_MAX_KEY_SIZE = 50;
constexpr int RULEXDB_MAX_RECORD_SIZE = 200;

void ToLowerInplace(std::wstring& s) {
    if (!s.empty()) {
        CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    }
}

bool IsCyrLetter(wchar_t ch) {
    return (ch >= 0x0410 && ch <= 0x044F) || ch == 0x0401 || ch == 0x0451;
}

bool IsAccent(wchar_t ch) {
    return ch == 0x0301 || ch == L'+';
}

bool IsWordChar(wchar_t ch) {
    return IsCyrLetter(ch) || IsAccent(ch);
}

// rulexdb_open ждёт путь к базе в UTF-8: текущая rulex.dll пропускает свои
// внутренние access()/проверки файла через шим UTF-8 -> wide (см. в
// NVDA-драйвере access_utf8.c), и его питон-драйвер тоже передаёт
// db_path.encode("utf-8"). Прежняя конвертация в ACP молча возвращала пусто для
// любого не-ASCII пути (кириллица в имени пользователя, локализованная папка)
// или когда 8.3-имена отключены -- тогда кандидат пропускался и rulex выглядел
// «невидимым». UTF-8 байт-в-байт совпадает с ASCII, поэтому обычные пути не
// затрагиваются.
std::string DbPathToUtf8Z(const std::filesystem::path& dbPath) {
    const std::wstring ws = dbPath.wstring();
    if (ws.empty()) return {};

    int needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};

    std::string out(static_cast<size_t>(needed), '\0');
    int written = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, out.data(), needed, nullptr, nullptr);
    if (written <= 0) return {};

    out.resize(static_cast<size_t>(written - 1)); // отбросить хвостовой NUL
    return out;
}

std::wstring Koi8rToWide(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(20866, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};

    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(20866, 0, s.c_str(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

std::string WideToKoi8rIgnore(const std::wstring& ws) {
    if (ws.empty()) return {};

    std::string out;
    out.reserve(ws.size());
    for (wchar_t ch : ws) {
        char b = 0;
        BOOL usedDefault = FALSE;
        const int written = WideCharToMultiByte(
            20866, // KOI8-R
            WC_NO_BEST_FIT_CHARS,
            &ch,
            1,
            &b,
            1,
            nullptr,
            &usedDefault);
        if (written == 1 && usedDefault == FALSE) {
            out.push_back(b);
        }
    }
    return out;
}

size_t SafeStrLen(const char* s, size_t maxLen) {
    if (!s) return 0;
    size_t n = 0;
    while (n < maxLen && s[n] != '\0') {
        ++n;
    }
    return n;
}

bool IsRegularFile(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

struct RulexPaths {
    std::filesystem::path dllPath;
    std::filesystem::path dbPath;
};

bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

void AddCandidate(std::vector<RulexPaths>& out, const std::filesystem::path& dllPath, const std::filesystem::path& dbPath) {
    if (dllPath.empty() || dbPath.empty()) return;

    RulexPaths candidate{ dllPath.lexically_normal(), dbPath.lexically_normal() };
    for (const RulexPaths& existing : out) {
        if (SamePath(existing.dllPath, candidate.dllPath) && SamePath(existing.dbPath, candidate.dbPath)) {
            return;
        }
    }
    out.push_back(std::move(candidate));
}

void AddCandidatesFromArchDir(std::vector<RulexPaths>& candidates, const std::filesystem::path& archDir) {
    if (archDir.empty()) return;

    // Installer layout (source of truth): the setup drops rulex.dll next to the
    // SAPI/bridge DLLs in {app}\lib\<arch>\, and rulex.db in {app}\ -- i.e. two
    // levels up from the DLLs. See installer\rutts_sapi.iss.
    AddCandidate(candidates, archDir / L"rulex.dll", archDir.parent_path().parent_path() / L"rulex.db");

#if defined(_WIN64)
    constexpr wchar_t kArchName[] = L"x64";
#else
    constexpr wchar_t kArchName[] = L"x32";
#endif

    // Dev layout fallback (for builds from out\\<arch>\\Release).
    std::filesystem::path root = archDir;
    for (int i = 0; i < 6 && !root.empty(); ++i) {
        AddCandidate(
            candidates,
            root / L"ru_tts" / L"lib" / kArchName / L"rulex.dll",
            root / L"ru_tts" / L"rulex.db");

        const std::filesystem::path next = root.parent_path();
        if (next == root) break;
        root = next;
    }
}

extern "C" IMAGE_DOS_HEADER __ImageBase;

std::filesystem::path GetThisModuleDir() {
    std::array<wchar_t, 32768> path{};
    DWORD len = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path.data(), static_cast<DWORD>(path.size()));
    if (len == 0 || len >= path.size()) return {};
    return std::filesystem::path(std::wstring(path.data(), len)).parent_path();
}

std::vector<RulexPaths> BuildRulexPathCandidates(const std::filesystem::path& archDirHint) {
    std::vector<RulexPaths> candidates;

    // Main source of truth: rutts_rulex.dll location.
    AddCandidatesFromArchDir(candidates, GetThisModuleDir());
    // Optional hint from caller remains as fallback for backward compatibility.
    AddCandidatesFromArchDir(candidates, archDirHint);

    return candidates;
}
} // namespace

RulexApi::~RulexApi() {
    Unload();
}

void RulexApi::EnsureLoaded(const std::wstring& archDir) {
    if (m_rulexDll && m_rulexDb && m_rulexdb_search) return;

    const std::filesystem::path pArchHint(archDir);
    const std::vector<RulexPaths> candidates = BuildRulexPathCandidates(pArchHint);

    for (const RulexPaths& candidate : candidates) {
        if (!IsRegularFile(candidate.dllPath) || !IsRegularFile(candidate.dbPath)) {
            continue;
        }

        HMODULE dll = LoadLibraryW(candidate.dllPath.c_str());
        if (!dll) continue;

#pragma warning(push)
#pragma warning(disable : 4191)
        auto openFn = reinterpret_cast<rulexdb_open_fn>(GetProcAddress(dll, "rulexdb_open"));
        auto searchFn = reinterpret_cast<rulexdb_search_fn>(GetProcAddress(dll, "rulexdb_search"));
        auto closeFn = reinterpret_cast<rulexdb_close_fn>(GetProcAddress(dll, "rulexdb_close"));
#pragma warning(pop)

        if (!openFn || !searchFn || !closeFn) {
            FreeLibrary(dll);
            continue;
        }

        std::string dbA = DbPathToUtf8Z(candidate.dbPath);
        if (dbA.empty()) {
            FreeLibrary(dll);
            continue;
        }

        // rulexdb_open may keep the path pointer for later internal use.
        // Keep this buffer alive for the whole DB lifetime.
        m_dbPathA = std::move(dbA);
        void* db = openFn(m_dbPathA.c_str(), RULEXDB_SEARCH);
        if (!db) {
            m_dbPathA.clear();
            FreeLibrary(dll);
            continue;
        }

        m_rulexDll = dll;
        m_rulexdb_open = openFn;
        m_rulexdb_search = searchFn;
        m_rulexdb_close = closeFn;
        m_rulexDb = db;
        return;
    }
}

void RulexApi::Unload() {
    if (m_rulexDb && m_rulexdb_close) {
        m_rulexdb_close(m_rulexDb);
        m_rulexDb = nullptr;
    }

    m_rulexdb_open = nullptr;
    m_rulexdb_search = nullptr;
    m_rulexdb_close = nullptr;

    if (m_rulexDll) {
        FreeLibrary(m_rulexDll);
        m_rulexDll = nullptr;
    }
    m_dbPathA.clear();
    m_searchBuf.fill(0);
}

std::wstring RulexApi::SearchWord(const std::wstring& word) const {
    if (!HasRulex()) return word;

    std::wstring lower = word;
    ToLowerInplace(lower);

    std::string key = WideToKoi8rIgnore(lower);
    if (key.empty() || static_cast<int>(key.size()) > RULEXDB_MAX_KEY_SIZE) {
        return word;
    }

    m_searchBuf.fill(0);
    int rc = m_rulexdb_search(m_rulexDb, key.c_str(), m_searchBuf.data(), 0);
    if (rc == RULEXDB_SUCCESS) {
        const size_t len = SafeStrLen(m_searchBuf.data(), RULEXDB_BUFSIZE);
        if (len == 0 || len > static_cast<size_t>(RULEXDB_MAX_RECORD_SIZE)) {
            return word;
        }
        std::wstring record = Koi8rToWide(std::string(m_searchBuf.data(), len));
        if (!record.empty()) {
            return record;
        }
    }

    return word;
}

std::wstring RulexApi::ApplyToRussianWords(const std::wstring& text) const {
    if (!HasRulex()) return text;

    std::wstring out;
    out.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        if (IsWordChar(text[i])) {
            size_t j = i;
            while (j < text.size() && IsWordChar(text[j])) {
                ++j;
            }

            std::wstring word = text.substr(i, j - i);
            if (word.find(L'+') != std::wstring::npos || word.find(static_cast<wchar_t>(0x0301)) != std::wstring::npos) {
                for (auto& ch : word) {
                    if (ch == static_cast<wchar_t>(0x0301)) {
                        ch = L'+';
                    }
                }
                out += word;
            } else {
                out += SearchWord(word);
            }

            i = j;
            continue;
        }

        out.push_back(text[i]);
        ++i;
    }

    return out;
}

namespace {
RulexApi* ToApi(rutts_rulex_handle handle) {
    return static_cast<RulexApi*>(handle);
}
} // namespace

rutts_rulex_handle RUTTS_RULEX_CALL rutts_rulex_create() {
    try {
        return new RulexApi();
    } catch (...) {
        return nullptr;
    }
}

void RUTTS_RULEX_CALL rutts_rulex_destroy(rutts_rulex_handle handle) {
    delete ToApi(handle);
}

BOOL RUTTS_RULEX_CALL rutts_rulex_ensure_loaded(rutts_rulex_handle handle, const wchar_t* archDir) {
    RulexApi* api = ToApi(handle);
    if (!api) return FALSE;
    api->EnsureLoaded((archDir && *archDir) ? archDir : L"");
    return api->HasRulex() ? TRUE : FALSE;
}

BOOL RUTTS_RULEX_CALL rutts_rulex_has_rulex(rutts_rulex_handle handle) {
    RulexApi* api = ToApi(handle);
    return (api && api->HasRulex()) ? TRUE : FALSE;
}

int RUTTS_RULEX_CALL rutts_rulex_apply_to_russian_words(
    rutts_rulex_handle handle, const wchar_t* text, wchar_t* outBuf, int outChars) {
    RulexApi* api = ToApi(handle);
    if (!api || !text) return 0;

    const std::wstring transformed = api->ApplyToRussianWords(text);
    const int required = static_cast<int>(transformed.size() + 1);
    if (!outBuf || outChars < required) {
        return required;
    }

    for (size_t i = 0; i < transformed.size(); ++i) {
        outBuf[i] = transformed[i];
    }
    outBuf[transformed.size()] = L'\0';
    return required;
}
