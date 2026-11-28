#pragma once

#include <cstddef>
#include <cstdint>

namespace eng {

enum class Sym : uint16_t {
#define SYM_DATA(id, mangled, defaultType, policy) id,
#define SYM_FN(id, mangled, defaultRet, policy) id,
#define SYM_FN_ABI(id, mangled64, mangled32, defaultRet, policy) id,
#include "Symbols.def"
#undef SYM_DATA
#undef SYM_FN
#undef SYM_FN_ABI
	kCount
};

enum class Policy : uint8_t {
	Required = 0,
	Optional = 1,
};

namespace detail {
extern void* g_symAddrs[static_cast<size_t>(Sym::kCount)];
void logNullCall(Sym id);
} // namespace detail

// Resolve every symbol in Symbols.def once at startup (after g_saSym->Open("libGTASA.so")).
// Logs every missing required symbol and returns false if any required symbol failed to resolve.
// Missing optional symbols log a warning and leave their slot null.
bool resolveAll();
bool isResolved();

const char* name(Sym id);
const char* mangled(Sym id);

inline void* ptr(Sym id)
{
	return detail::g_symAddrs[static_cast<size_t>(id)];
}

inline uintptr_t addr(Sym id)
{
	return reinterpret_cast<uintptr_t>(detail::g_symAddrs[static_cast<size_t>(id)]);
}

template <typename T>
inline T as(Sym id)
{
	return reinterpret_cast<T>(detail::g_symAddrs[static_cast<size_t>(id)]);
}

// Compute address of an Itanium vtable slot pointer (matches SymUtils::GetVmtFunction(sym, index, true)).
inline uintptr_t vmtSlot(Sym vtableSym, int index)
{
	if (index < 1) index = 1;
	const uintptr_t base = addr(vtableSym);
	if (!base) return 0;
	return base + static_cast<uintptr_t>(index + 1) * sizeof(void*);
}

// Runtime dynamic lookup — ONLY for CLEO opcode 0DD1 and diagnostic commands.
// Do not use for static libGTASA symbols; declare those in Symbols.def instead.
void* lookup(const char* symName);

// Live GOT-dereferencing accessors for globals that AML plugins (e.g. libSAUtils64.so)
// redirect after Client::initialize(). Must never be cached across calls.
namespace live {
uintptr_t* touchWidgets();
void* mobileSettings();
} // namespace live

namespace data {
#define SYM_DATA(id, mangled, defaultType, policy)                                   \
	template <typename T = defaultType>                                              \
	inline T* id()                                                                   \
	{                                                                                \
		return reinterpret_cast<T*>(detail::g_symAddrs[static_cast<size_t>(Sym::id)]); \
	}
#define SYM_FN(id, mangled, defaultRet, policy)
#define SYM_FN_ABI(id, mangled64, mangled32, defaultRet, policy)
#include "Symbols.def"
#undef SYM_DATA
#undef SYM_FN
#undef SYM_FN_ABI
} // namespace data

namespace fn {
#define SYM_DATA(id, mangled, defaultType, policy)
#define SYM_FN(id, mangled, defaultRet, policy)                                      \
	template <typename Ret = defaultRet, typename... Args>                           \
	inline Ret id(Args... args)                                                      \
	{                                                                                \
		void* a = detail::g_symAddrs[static_cast<size_t>(Sym::id)];                  \
		if (__builtin_expect(!a, 0)) {                                               \
			detail::logNullCall(Sym::id);                                            \
			return Ret();                                                            \
		}                                                                            \
		return reinterpret_cast<Ret (*)(Args...)>(a)(args...);                       \
	}
#define SYM_FN_ABI(id, mangled64, mangled32, defaultRet, policy)                     \
	SYM_FN(id, mangled64, defaultRet, policy)
#include "Symbols.def"
#undef SYM_DATA
#undef SYM_FN
#undef SYM_FN_ABI
} // namespace fn

} // namespace eng
