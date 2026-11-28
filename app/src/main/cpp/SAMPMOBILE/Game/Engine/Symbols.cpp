#include "Symbols.h"

#include "../../Addr.h"
#include "../../Memory/include/Memory.h"

#include <spdlog/spdlog.h>


namespace eng {

namespace detail {
void* g_symAddrs[static_cast<size_t>(Sym::kCount)] = {};

void logNullCall(Sym id)
{
	spdlog::error("eng::fn: null symbol call: {} ({})", name(id), mangled(id));
}
} // namespace detail

namespace {

struct SymMeta {
	const char* name;
	const char* mangled;
	Policy policy;
	bool isData;
};

#if VER_x32
#define SYM_ABI_PICK(m64, m32) (m32)
#else
#define SYM_ABI_PICK(m64, m32) (m64)
#endif

constexpr SymMeta kMeta[static_cast<size_t>(Sym::kCount)] = {
#define SYM_DATA(id, mangled, defaultType, policy) {#id, mangled, Policy::policy, true},
#define SYM_FN(id, mangled, defaultRet, policy) {#id, mangled, Policy::policy, false},
#define SYM_FN_ABI(id, mangled64, mangled32, defaultRet, policy) \
	{#id, SYM_ABI_PICK(mangled64, mangled32), Policy::policy, false},
#include "Symbols.def"
#undef SYM_DATA
#undef SYM_FN
#undef SYM_FN_ABI
};

#undef SYM_ABI_PICK

bool s_resolved = false;

} // namespace

bool resolveAll()
{
	if (!g_saSym || !g_saSym->handle) {
		spdlog::error("eng::resolveAll: g_saSym is not opened");
		return false;
	}

	size_t resolvedCount = 0;
	size_t missingRequired = 0;
	size_t missingOptional = 0;

	constexpr size_t kTotal = static_cast<size_t>(Sym::kCount);
	for (size_t i = 0; i < kTotal; ++i) {
		const SymMeta& meta = kMeta[i];
		void* resolved = g_saSym->GetSymbol<void*>(meta.mangled);
		detail::g_symAddrs[i] = resolved;
		if (resolved) {
			++resolvedCount;
		}
		else if (meta.policy == Policy::Required) {
			++missingRequired;
			spdlog::error("eng::resolveAll: MISSING required symbol: {} ({})", meta.name, meta.mangled);
		}
		else {
			++missingOptional;
			spdlog::warn("eng::resolveAll: missing optional symbol: {} ({})", meta.name, meta.mangled);
		}
	}

	if (missingRequired > 0) {
		spdlog::error("eng::resolveAll: FAILED ({}/{} resolved, {} required missing, {} optional missing)",
			resolvedCount, kTotal, missingRequired, missingOptional);
		s_resolved = false;
		return false;
	}

	s_resolved = true;
	spdlog::info("eng::resolveAll: OK ({}/{} resolved, {} optional missing)",
		resolvedCount, kTotal, missingOptional);
	return true;
}

bool isResolved()
{
	return s_resolved;
}

const char* name(Sym id)
{
	const auto idx = static_cast<size_t>(id);
	return idx < static_cast<size_t>(Sym::kCount) ? kMeta[idx].name : "?";
}

const char* mangled(Sym id)
{
	const auto idx = static_cast<size_t>(id);
	return idx < static_cast<size_t>(Sym::kCount) ? kMeta[idx].mangled : "?";
}

void* lookup(const char* symName)
{
	if (!g_saSym || !symName || !symName[0]) return nullptr;
	return g_saSym->GetSymbol<void*>(symName);
}

namespace live {

uintptr_t* touchWidgets()
{
	if (!g_saSym || g_saSym->GetLibAddr() == 0) return nullptr;
	// Read live GOT slot so AML plugins (e.g. libSAUtils64.so -> pNewWidgets)
	// that redirect CTouchInterface::m_pWidgets after Client::initialize() are respected.
	uintptr_t** pGotSlot = g_saSym->Abs<uintptr_t**>(addr::GOT_CTouchInterface_m_pWidgets);
	return pGotSlot ? *pGotSlot : nullptr;
}

void* mobileSettings()
{
	if (!g_saSym || g_saSym->GetLibAddr() == 0) return nullptr;
	// Read live GOT slot so AML plugins (e.g. libSAUtils64.so -> pNewSettings)
	// that redirect MobileSettings::settings after Client::initialize() are respected.
	void** pGotSlot = g_saSym->Abs<void**>(addr::GOT_MobileSettings_settings);
	if (pGotSlot && *pGotSlot) {
		return *pGotSlot;
	}
	return ptr(Sym::MobileSettings_settings);
}

} // namespace live

} // namespace eng
