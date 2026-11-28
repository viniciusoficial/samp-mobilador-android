#include "PatchRegistry.h"

#include "../../Memory/include/Memory.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <mutex>
#include <unordered_map>


namespace patches {

namespace {

struct Entry {
	std::string name;
	std::string purpose;
	Kind kind = Kind::Nop;
	Scope scope = Scope::Boot;
	uintptr_t targetAddr = 0;
	bool reversible = false;
	bool active = false;
	bool failed = false;
	unsigned int nopWords = 0;
	std::vector<uint8_t> origBytes;
	std::vector<uint8_t> patchBytes;
	std::atomic<bool>* gate = nullptr;
};

std::mutex g_mutex;
std::vector<Entry> g_entries;
std::unordered_map<std::string, size_t> g_byName;
std::unordered_map<uintptr_t, size_t> g_byAddr;

uintptr_t normalizeTarget(Kind kind, uintptr_t addr)
{
	if (!addr) return 0;
#if !defined(__aarch64__)
	if (kind != Kind::Data) {
		return addr & ~uintptr_t(1);
	}
#endif
	return addr;
}

bool checkConflictLocked(const char* name, Kind kind, uintptr_t targetAddr)
{
	if (name && g_byName.count(name)) {
		spdlog::error("PatchRegistry: DUPLICATE entry name '{}'", name);
		return true;
	}
	const uintptr_t norm = normalizeTarget(kind, targetAddr);
	if (norm != 0) {
		auto it = g_byAddr.find(norm);
		if (it != g_byAddr.end()) {
			spdlog::error("PatchRegistry: DUPLICATE target 0x{:X} for '{}' (already owned by '{}')",
				norm, name ? name : "?", g_entries[it->second].name);
			return true;
		}
	}
	return false;
}

bool addEntryLocked(Entry&& e)
{
	if (checkConflictLocked(e.name.c_str(), e.kind, e.targetAddr)) {
		return false;
	}
	const uintptr_t norm = normalizeTarget(e.kind, e.targetAddr);
	e.targetAddr = norm;
	const size_t idx = g_entries.size();
	g_byName.emplace(e.name, idx);
	if (norm != 0) {
		g_byAddr.emplace(norm, idx);
	}
	g_entries.push_back(std::move(e));
	return true;
}

bool applyEntryLocked(Entry& e)
{
	if (e.kind == Kind::Hook || e.kind == Kind::HookBL || e.kind == Kind::Data) {
		return !e.failed;
	}
	if (!e.targetAddr) {
		e.failed = true;
		spdlog::error("PatchRegistry: cannot apply '{}' (null target address)", e.name);
		return false;
	}
	if (e.active) {
		return true;
	}

	switch (e.kind) {
		case Kind::Ret:
			Memory::ret(e.targetAddr);
			e.active = true;
			return true;
		case Kind::Ret0:
			Memory::ret0(e.targetAddr);
			e.active = true;
			return true;
		case Kind::Nop:
			Memory::nop(e.targetAddr, e.nopWords);
			e.active = true;
			return true;
		case Kind::Bytes: {
			if (e.patchBytes.empty()) {
				e.failed = true;
				spdlog::error("PatchRegistry: empty byte payload for '{}'", e.name);
				return false;
			}
			if (e.origBytes.empty()) {
				e.origBytes.resize(e.patchBytes.size());
				Memory::readMemory(e.targetAddr, reinterpret_cast<uintptr_t>(e.origBytes.data()), e.origBytes.size());
			}
			Memory::writeMemory(e.targetAddr, reinterpret_cast<uintptr_t>(e.patchBytes.data()), e.patchBytes.size());
			e.active = true;
			return true;
		}
		default:
			return false;
	}
}

bool revertEntryLocked(Entry& e)
{
	if (!e.active) {
		return true;
	}
	if (!e.reversible) {
		spdlog::warn("PatchRegistry: '{}' is not reversible", e.name);
		return false;
	}
	if (!e.targetAddr) {
		return false;
	}

	switch (e.kind) {
		case Kind::Ret:
		case Kind::Ret0:
			Memory::restoreRet(e.targetAddr);
			e.active = false;
			return true;
		case Kind::Nop:
			Memory::restoreNop(e.targetAddr);
			e.active = false;
			return true;
		case Kind::Bytes:
			if (e.origBytes.empty()) {
				spdlog::error("PatchRegistry: no original bytes saved for '{}'", e.name);
				return false;
			}
			Memory::writeMemory(e.targetAddr, reinterpret_cast<uintptr_t>(e.origBytes.data()), e.origBytes.size());
			e.active = false;
			return true;
		default:
			return false;
	}
}

} // namespace

const char* kindName(Kind k)
{
	switch (k) {
		case Kind::Ret:    return "Ret";
		case Kind::Ret0:   return "Ret0";
		case Kind::Nop:    return "Nop";
		case Kind::Bytes:  return "Bytes";
		case Kind::Data:   return "Data";
		case Kind::Hook:   return "Hook";
		case Kind::HookBL: return "HookBL";
	}
	return "?";
}

const char* scopeName(Scope s)
{
	switch (s) {
		case Scope::Boot:    return "Boot";
		case Scope::Online:  return "Online";
		case Scope::Session: return "Session";
	}
	return "?";
}

bool registerRet(const char* name, const char* purpose, Scope scope, eng::Sym sym, bool reversible)
{
	Entry e{};
	e.name = name ? name : "";
	e.purpose = purpose ? purpose : "";
	e.kind = Kind::Ret;
	e.scope = scope;
	e.targetAddr = eng::addr(sym);
	e.reversible = reversible;
	if (!e.targetAddr) {
		e.failed = true;
		spdlog::error("PatchRegistry: registerRet '{}' failed (null symbol {})", e.name, eng::name(sym));
	}
	std::lock_guard<std::mutex> lock(g_mutex);
	return addEntryLocked(std::move(e));
}

bool registerRet0(const char* name, const char* purpose, Scope scope, eng::Sym sym, bool reversible)
{
	Entry e{};
	e.name = name ? name : "";
	e.purpose = purpose ? purpose : "";
	e.kind = Kind::Ret0;
	e.scope = scope;
	e.targetAddr = eng::addr(sym);
	e.reversible = reversible;
	if (!e.targetAddr) {
		e.failed = true;
		spdlog::error("PatchRegistry: registerRet0 '{}' failed (null symbol {})", e.name, eng::name(sym));
	}
	std::lock_guard<std::mutex> lock(g_mutex);
	return addEntryLocked(std::move(e));
}

bool registerNop(const char* name, const char* purpose, Scope scope, uintptr_t fileOffset, unsigned int words, bool reversible)
{
	Entry e{};
	e.name = name ? name : "";
	e.purpose = purpose ? purpose : "";
	e.kind = Kind::Nop;
	e.scope = scope;
	e.targetAddr = g_saSym ? g_saSym->Abs<uintptr_t>(fileOffset) : 0;
	e.nopWords = words;
	e.reversible = reversible;
	if (!e.targetAddr) {
		e.failed = true;
		spdlog::error("PatchRegistry: registerNop '{}' failed (null addr)", e.name);
	}
	std::lock_guard<std::mutex> lock(g_mutex);
	return addEntryLocked(std::move(e));
}

bool registerBytes(const char* name, const char* purpose, Scope scope, uintptr_t targetAddr,
                   const void* bytes, size_t len, bool reversible)
{
	Entry e{};
	e.name = name ? name : "";
	e.purpose = purpose ? purpose : "";
	e.kind = Kind::Bytes;
	e.scope = scope;
	e.targetAddr = targetAddr;
	e.reversible = reversible;
	if (bytes && len > 0) {
		e.patchBytes.resize(len);
		memcpy(e.patchBytes.data(), bytes, len);
	}
	if (!e.targetAddr || e.patchBytes.empty()) {
		e.failed = true;
		spdlog::error("PatchRegistry: registerBytes '{}' failed (addr=0x{:X}, len={})", e.name, e.targetAddr, len);
	}
	std::lock_guard<std::mutex> lock(g_mutex);
	return addEntryLocked(std::move(e));
}

bool registerData(const char* name, const char* purpose, Scope scope, uintptr_t targetAddr)
{
	Entry e{};
	e.name = name ? name : "";
	e.purpose = purpose ? purpose : "";
	e.kind = Kind::Data;
	e.scope = scope;
	e.targetAddr = targetAddr;
	e.reversible = false;
	e.active = (targetAddr != 0);
	e.failed = (targetAddr == 0);
	if (e.failed) {
		spdlog::error("PatchRegistry: registerData '{}' failed (null target)", e.name);
	}
	std::lock_guard<std::mutex> lock(g_mutex);
	return addEntryLocked(std::move(e));
}

bool apply(Scope scope)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	bool allOk = true;
	size_t applied = 0;
	size_t failed = 0;
	for (auto& e : g_entries) {
		if (e.scope != scope) continue;
		if (e.kind == Kind::Hook || e.kind == Kind::HookBL || e.kind == Kind::Data) continue;
		if (applyEntryLocked(e)) {
			++applied;
		}
		else {
			++failed;
			allOk = false;
		}
	}
	spdlog::info("PatchRegistry: apply({}) -> applied={}, failed={}", scopeName(scope), applied, failed);
	return allOk;
}

bool set(const char* name, bool enabled)
{
	if (!name) return false;
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_byName.find(name);
	if (it == g_byName.end()) {
		spdlog::error("PatchRegistry: set('{}', {}) failed (not registered)", name, enabled);
		return false;
	}
	Entry& e = g_entries[it->second];
	if (e.kind == Kind::Hook || e.kind == Kind::HookBL) {
		if (e.failed) {
			spdlog::error("PatchRegistry: hook '{}' failed to install; cannot toggle", e.name);
			return false;
		}
		if (!e.gate) {
			spdlog::warn("PatchRegistry: hook '{}' has no runtime gate", e.name);
			return false;
		}
		e.gate->store(enabled, std::memory_order_release);
		e.active = enabled;
		return true;
	}
	return enabled ? applyEntryLocked(e) : revertEntryLocked(e);
}

bool updateBytes(const char* name, const char* purpose, uintptr_t targetAddr, const void* bytes, size_t len,
                 Scope scope, bool reversible)
{
	if (!name || !targetAddr || !bytes || len == 0) return false;
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_byName.find(name);
	if (it == g_byName.end()) {
		Entry e{};
		e.name = name;
		e.purpose = purpose ? purpose : "";
		e.kind = Kind::Bytes;
		e.scope = scope;
		e.targetAddr = targetAddr;
		e.reversible = reversible;
		e.patchBytes.resize(len);
		memcpy(e.patchBytes.data(), bytes, len);
		if (!addEntryLocked(std::move(e))) return false;
		it = g_byName.find(name);
	}
	Entry& e = g_entries[it->second];
	if (e.kind != Kind::Bytes || normalizeTarget(e.kind, e.targetAddr) != normalizeTarget(Kind::Bytes, targetAddr)
		|| (!e.origBytes.empty() && e.origBytes.size() != len)) {
		spdlog::error("PatchRegistry: updateBytes '{}' mismatch (kind/addr/len)", e.name);
		return false;
	}
	if (e.origBytes.empty()) {
		e.origBytes.resize(len);
		Memory::readMemory(e.targetAddr, reinterpret_cast<uintptr_t>(e.origBytes.data()), len);
	}
	e.patchBytes.resize(len);
	memcpy(e.patchBytes.data(), bytes, len);
	Memory::writeMemory(e.targetAddr, reinterpret_cast<uintptr_t>(e.patchBytes.data()), len);
	e.active = true;
	e.failed = false;
	return true;
}

void* installHook(const char* name, const char* purpose, Scope scope, eng::Sym sym,
                  void* hookFn, void** origFn, std::atomic<bool>* gate)
{
	const char* entryName = name ? name : eng::name(sym);
	void* target = eng::ptr(sym);
	const uintptr_t targetAddr = reinterpret_cast<uintptr_t>(target);

	std::lock_guard<std::mutex> lock(g_mutex);
	if (checkConflictLocked(entryName, Kind::Hook, targetAddr)) {
		return nullptr;
	}

	void* handle = Memory::hookSym(target, hookFn, origFn, entryName);

	Entry e{};
	e.name = entryName;
	e.purpose = purpose ? purpose : "";
	e.kind = Kind::Hook;
	e.scope = scope;
	e.targetAddr = targetAddr;
	e.reversible = (gate != nullptr);
	e.gate = gate;
	e.failed = (handle == nullptr);
	e.active = !e.failed && (!gate || gate->load(std::memory_order_acquire));

	addEntryLocked(std::move(e));
	return handle;
}

void* installHookAddr(const char* name, const char* purpose, Scope scope, uintptr_t fileOffset,
                      void* hookFn, void** origFn, bool isShortFunc, std::atomic<bool>* gate)
{
	const char* entryName = name ? name : "";
	void* target = g_saSym ? g_saSym->Abs<void*>(fileOffset) : nullptr;
	const uintptr_t targetAddr = reinterpret_cast<uintptr_t>(target);

	std::lock_guard<std::mutex> lock(g_mutex);
	if (checkConflictLocked(entryName, Kind::Hook, targetAddr)) {
		return nullptr;
	}

	void* handle = nullptr;
	if (target) {
		handle = Memory::hook(target, hookFn, origFn, isShortFunc);
		if (!handle) {
			spdlog::error("HOOK_ADDR FAILED: {} target=0x{:X}", entryName, targetAddr);
		}
	}
	else {
		spdlog::error("HOOK_ADDR FAILED (null target): {}", entryName);
	}

	Entry e{};
	e.name = entryName;
	e.purpose = purpose ? purpose : "";
	e.kind = Kind::Hook;
	e.scope = scope;
	e.targetAddr = targetAddr;
	e.reversible = (gate != nullptr);
	e.gate = gate;
	e.failed = (handle == nullptr);
	e.active = !e.failed && (!gate || gate->load(std::memory_order_acquire));

	addEntryLocked(std::move(e));
	return handle;
}

void* installHookBL(const char* name, const char* purpose, Scope scope, uintptr_t fileOffset,
                    void* hookFn, void** origFn, std::atomic<bool>* gate)
{
	const char* entryName = name ? name : "";
	void* target = g_saSym ? g_saSym->Abs<void*>(fileOffset) : nullptr;
	const uintptr_t targetAddr = reinterpret_cast<uintptr_t>(target);

	std::lock_guard<std::mutex> lock(g_mutex);
	if (checkConflictLocked(entryName, Kind::HookBL, targetAddr)) {
		return nullptr;
	}

	void* handle = nullptr;
	if (target) {
		handle = Memory::hookBL(target, hookFn, origFn);
		if (!handle) {
			spdlog::error("HOOK_BL FAILED: {} target=0x{:X}", entryName, targetAddr);
		}
	}
	else {
		spdlog::error("HOOK_BL FAILED (null target): {}", entryName);
	}

	Entry e{};
	e.name = entryName;
	e.purpose = purpose ? purpose : "";
	e.kind = Kind::HookBL;
	e.scope = scope;
	e.targetAddr = targetAddr;
	e.reversible = (gate != nullptr);
	e.gate = gate;
	e.failed = (handle == nullptr);
	e.active = !e.failed && (!gate || gate->load(std::memory_order_acquire));

	addEntryLocked(std::move(e));
	return handle;
}

Summary getSummary()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	Summary s{};
	s.total = g_entries.size();
	s.nopCalls = Memory::g_nopCalls.load(std::memory_order_relaxed);
	s.restoreNopCalls = Memory::g_restoreNopCalls.load(std::memory_order_relaxed);
	for (const auto& e : g_entries) {
		const bool isActive = e.gate ? e.gate->load(std::memory_order_acquire) : e.active;
		if (isActive && !e.failed) ++s.active;
		if (e.failed) ++s.failed;
		if (e.kind == Kind::Hook || e.kind == Kind::HookBL) ++s.hooks;
		else if (e.kind == Kind::Data) ++s.dataPatches;
		else ++s.bytePatches;
	}
	return s;
}

std::vector<SnapshotEntry> snapshot()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	std::vector<SnapshotEntry> out;
	out.reserve(g_entries.size());
	for (const auto& e : g_entries) {
		SnapshotEntry se{};
		se.name = e.name;
		se.purpose = e.purpose;
		se.kind = e.kind;
		se.scope = e.scope;
		se.targetAddr = e.targetAddr;
		se.reversible = e.reversible;
		se.active = e.gate ? e.gate->load(std::memory_order_acquire) : e.active;
		se.failed = e.failed;
		out.push_back(std::move(se));
	}
	return out;
}

void dumpToLog()
{
	const uintptr_t base = g_saSym ? g_saSym->GetLibAddr() : 0;
	const Summary sum = getSummary();
	const auto entries = snapshot();

	spdlog::info("[PATCHES] total={} active={} failed={} (hooks={}, bytePatches={}, dataPatches={}) nopCalls={} restoreNopCalls={}",
		sum.total, sum.active, sum.failed, sum.hooks, sum.bytePatches, sum.dataPatches,
		sum.nopCalls, sum.restoreNopCalls);

	for (const auto& e : entries) {
		const uintptr_t off = (base && e.targetAddr >= base) ? (e.targetAddr - base) : e.targetAddr;
		spdlog::info("[PATCHES] {:<7s} {:<6s} {:<7s} {:<4s} +0x{:06X}  {:<42s}  {}",
			scopeName(e.scope),
			kindName(e.kind),
			e.failed ? "FAIL" : (e.active ? "ON" : "OFF"),
			e.reversible ? "rev" : "fix",
			off,
			e.name,
			e.purpose);
	}
	if (auto logger = spdlog::default_logger()) {
		logger->flush();
	}
}

} // namespace patches
