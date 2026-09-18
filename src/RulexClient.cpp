#include "RulexClient.h"

#include "WinUtil.h"

namespace {
FARPROC GetProcOrNull(HMODULE m, const char* name) {
    return m ? ::GetProcAddress(m, name) : nullptr;
}
} // namespace

RulexClient::~RulexClient() {
    Unload();
}

bool RulexClient::LoadApi(const std::wstring& archDir) {
    if (m_dll && m_handle) return true;

    const std::wstring path = JoinPath(archDir, L"rutts_rulex.dll");
    HMODULE dll = ::LoadLibraryW(path.c_str());
    if (!dll) return false;

#pragma warning(push)
#pragma warning(disable : 4191)
    create_fn createFn = reinterpret_cast<create_fn>(GetProcOrNull(dll, "rutts_rulex_create"));
    destroy_fn destroyFn = reinterpret_cast<destroy_fn>(GetProcOrNull(dll, "rutts_rulex_destroy"));
    ensure_loaded_fn ensureLoadedFn = reinterpret_cast<ensure_loaded_fn>(GetProcOrNull(dll, "rutts_rulex_ensure_loaded"));
    has_rulex_fn hasRulexFn = reinterpret_cast<has_rulex_fn>(GetProcOrNull(dll, "rutts_rulex_has_rulex"));
    apply_fn applyFn = reinterpret_cast<apply_fn>(GetProcOrNull(dll, "rutts_rulex_apply_to_russian_words"));
#pragma warning(pop)

    if (!createFn || !destroyFn || !ensureLoadedFn || !hasRulexFn || !applyFn) {
        ::FreeLibrary(dll);
        return false;
    }

    rutts_rulex_handle handle = createFn();
    if (!handle) {
        ::FreeLibrary(dll);
        return false;
    }

    m_dll = dll;
    m_handle = handle;
    m_create = createFn;
    m_destroy = destroyFn;
    m_ensureLoaded = ensureLoadedFn;
    m_hasRulex = hasRulexFn;
    m_apply = applyFn;
    return true;
}

void RulexClient::EnsureLoaded(const std::wstring& archDir) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (!LoadApi(archDir)) return;
    if (!m_ensureLoaded || !m_hasRulex || !m_handle) return;

    // Pass explicit arch directory hint to avoid intermittent path resolution issues.
    m_ensureLoaded(m_handle, archDir.empty() ? nullptr : archDir.c_str());

    // Retry with full API reload if the handle lost dictionary state.
    if (m_hasRulex(m_handle) != TRUE) {
        if (m_handle && m_destroy) {
            m_destroy(m_handle);
        }
        m_handle = nullptr;
        m_create = nullptr;
        m_destroy = nullptr;
        m_ensureLoaded = nullptr;
        m_hasRulex = nullptr;
        m_apply = nullptr;
        if (m_dll) {
            ::FreeLibrary(m_dll);
            m_dll = nullptr;
        }

        if (!LoadApi(archDir) || !m_ensureLoaded || !m_hasRulex || !m_handle) return;
        m_ensureLoaded(m_handle, archDir.empty() ? nullptr : archDir.c_str());
    }
}

void RulexClient::Unload() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_handle && m_destroy) {
        m_destroy(m_handle);
    }
    m_handle = nullptr;
    m_create = nullptr;
    m_destroy = nullptr;
    m_ensureLoaded = nullptr;
    m_hasRulex = nullptr;
    m_apply = nullptr;

    if (m_dll) {
        ::FreeLibrary(m_dll);
        m_dll = nullptr;
    }
}

bool RulexClient::HasRulex() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_handle || !m_hasRulex) return false;
    return m_hasRulex(m_handle) == TRUE;
}

std::wstring RulexClient::ApplyToRussianWords(const std::wstring& text) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (text.empty() || !m_handle || !m_hasRulex || m_hasRulex(m_handle) != TRUE || !m_apply) return text;

    const int required = m_apply(m_handle, text.c_str(), nullptr, 0);
    if (required <= 1) return text;

    std::wstring out(static_cast<size_t>(required), L'\0');
    const int written = m_apply(m_handle, text.c_str(), out.data(), required);
    if (written <= 0 || written > required) return text;

    out.resize(static_cast<size_t>(written - 1));
    return out;
}
