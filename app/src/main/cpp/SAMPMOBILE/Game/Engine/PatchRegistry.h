#pragma once

#include "Symbols.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace patches {

enum class Kind : uint8_t {
	Ret,
	Ret0,
	Nop,
	Bytes,
	Data,
	Hook,
	HookBL,
};

enum class Scope : uint8_t {
	Boot,    // Always applied at startup (including offline mode); never removed
	Online,  // Applied at startup when online (not offline mode)
	Session, // Toggled dynamically by server settings or runtime state
};

struct SnapshotEntry {
	std::string name;
	std::string purpose;
	Kind kind = Kind::Nop;
	Scope scope = Scope::Boot;
	uintptr_t targetAddr = 0;
	bool reversible = false;
	bool active = false;
	bool failed = false;
};

struct Summary {
	size_t total = 0;
	size_t active = 0;
	size_t failed = 0;
	size_t hooks = 0;
	size_t bytePatches = 0;
	size_t dataPatches = 0;
	uint64_t nopCalls = 0;
	uint64_t restoreNopCalls = 0;
};

// Register code/data patches (deduped by target address; duplicate target logs an error).
bool registerRet(const char* name, const char* purpose, Scope scope, eng::Sym sym, bool reversible = true);
bool registerRet0(const char* name, const char* purpose, Scope scope, eng::Sym sym, bool reversible = true);
bool registerNop(const char* name, const char* purpose, Scope scope, uintptr_t fileOffset, unsigned int words, bool reversible = true);
bool registerBytes(const char* name, const char* purpose, Scope scope, uintptr_t targetAddr, const void* bytes, size_t len, bool reversible = true);
bool registerData(const char* name, const char* purpose, Scope scope, uintptr_t targetAddr);

// Apply all registered patches matching scope (logs any missing/failed entry).
bool apply(Scope scope);

// Enable or disable a registered reversible patch (or update a hook gate) by name.
bool set(const char* name, bool enabled);

// Update bytes of an existing Bytes patch in-place (backs up original bytes once).
// Creates the entry if not registered yet (defaults to Session/reversible for dynamic patches).
bool updateBytes(const char* name, const char* purpose, uintptr_t targetAddr, const void* bytes, size_t len,
                 Scope scope = Scope::Session, bool reversible = true);

// Install and register hooks (inline hooks are never uninstalled for AML chain safety).
void* installHook(const char* name, const char* purpose, Scope scope, eng::Sym sym,
                  void* hookFn, void** origFn, std::atomic<bool>* gate = nullptr);
void* installHookAddr(const char* name, const char* purpose, Scope scope, uintptr_t fileOffset,
                      void* hookFn, void** origFn, bool isShortFunc = false, std::atomic<bool>* gate = nullptr);
void* installHookBL(const char* name, const char* purpose, Scope scope, uintptr_t fileOffset,
                    void* hookFn, void** origFn, std::atomic<bool>* gate = nullptr);

Summary getSummary();
std::vector<SnapshotEntry> snapshot();
void dumpToLog();

const char* kindName(Kind k);
const char* scopeName(Scope s);

} // namespace patches

#define REG_HOOK(_scope, _symId, _name, _purpose) \
	patches::installHook(#_name, _purpose, patches::Scope::_scope, eng::Sym::_symId, \
		(void*)(&_name##_hook), (void**)(&_name))

#define REG_HOOK_GATED(_scope, _symId, _name, _gatePtr, _purpose) \
	patches::installHook(#_name, _purpose, patches::Scope::_scope, eng::Sym::_symId, \
		(void*)(&_name##_hook), (void**)(&_name), (_gatePtr))

#define REG_HOOK_ADDR(_scope, _fileOff, _name, _shortFunc, _purpose) \
	patches::installHookAddr(#_name, _purpose, patches::Scope::_scope, (_fileOff), \
		(void*)(&_name##_hook), (void**)(&_name), (_shortFunc))

#define REG_HOOK_BL(_scope, _fileOff, _name, _hookFn, _origFn, _purpose) \
	patches::installHookBL((_name), (_purpose), patches::Scope::_scope, (_fileOff), \
		(void*)(_hookFn), (void**)(_origFn))
