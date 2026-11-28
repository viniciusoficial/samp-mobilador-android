#pragma once

#include "Gloss.h"
#include "spdlog/spdlog.h"
#include "SymUtils.h"
#include <atomic>
#include <mutex>

extern SymUtils* g_saSym;

#define DECL_HOOK(_ret, _name, ...) \
    _ret (*_name)(__VA_ARGS__);     \
    _ret _name##_hook(__VA_ARGS__)

#define HOOK(_sym, _name) \
    Memory::hook(_sym, (void*)(&_name##_hook), (void**)(&_name))

#define HOOK_ADDR(_addr, _name, _shortfunc) \
    Memory::hook(_addr, (void*)(&_name##_hook), (void**)(&_name), _shortfunc)

#define GET_LR(dwRetAddr) \
    do { \
        uintptr_t lr = reinterpret_cast<uintptr_t>(__builtin_return_address(0)); \
        dwRetAddr = lr - g_saSym->GetLibAddr(); \
    } while (0)

namespace Memory {
inline std::mutex patchMapMutex;
inline std::unordered_map<uintptr_t, std::vector<uint8_t>> nopOrigBytesMap;
inline std::unordered_map<uintptr_t, std::vector<uint8_t>> retOrigBytesMap;
inline std::atomic<uint64_t> g_nopCalls{0};
inline std::atomic<uint64_t> g_restoreNopCalls{0};

static std::string buffer2Str(const std::vector<uint8_t>& buffer)
{
	std::string bufferContents;
	for (unsigned char i : buffer) {
		char byteStr[4];
		snprintf(byteStr, sizeof(byteStr), "%02x ", i);
		bufferContents += byteStr;
	}
	return bufferContents;
}

inline static uintptr_t getLibAddr(const char* libName)
{
	return GlossGetLibBias(libName);
}

inline static GHandle getLibHandle(const char* libName)
{
	return GlossOpen(libName);
}

static void protectAddr(void* addr, size_t size = sizeof(uintptr_t))
{
	Unprotect((uintptr_t) addr, size);
}

// Note: wraps Gloss ReadMemory(srcAddr, dstBuf, len)
inline static void readMemory(uintptr_t srcAddr, uintptr_t dstBuf, size_t len)
{
	if (!srcAddr || !dstBuf || len == 0) return;
	ReadMemory((void*) srcAddr, (void*) dstBuf, len, true);
}

inline static void writeMemory(uintptr_t dest, uintptr_t src, size_t len)
{
	if (!dest || !src || len == 0) return;
	// Guard against libGlossHook SetMemoryPermission off-by-one when size == 1
	// on a page boundary (addr + 1 + pagesize - 2 < next page -> mprotect len=0).
	Unprotect(dest, len < 2 ? 2 : len);
	WriteMemory((void*) dest, (void*) src, len, false);
}

static void gotHook(uintptr_t addr, uintptr_t hook_func, uintptr_t* orig_func = nullptr)
{
	if (!addr) return;
	GlossGotHook((void*) addr, (void*) hook_func, (void**) orig_func);
}

inline static void* hook(const char* symbol, void* hook_func, void** orig_func)
{
	auto* sym = g_saSym->GetSymbol(symbol);
	if (!sym) {
		spdlog::error("HOOK FAILED (symbol missing): {}", symbol);
		return nullptr;
	}
	void* h = GlossHook(sym, hook_func, orig_func);
	if (!h) {
		spdlog::error("HOOK FAILED: {} sym={} handle=0x0", symbol, fmt::ptr(sym));
	}
	return h;
}

inline static void* hookSym(void* sym, void* hook_func, void** orig_func, const char* label)
{
	if (!sym) {
		spdlog::error("HOOK FAILED (symbol missing): {}", label ? label : "?");
		return nullptr;
	}
	void* h = GlossHook(sym, hook_func, orig_func);
	if (!h) {
		spdlog::error("HOOK FAILED: {} sym={} handle=0x0", label ? label : "?", fmt::ptr(sym));
	}
	return h;
}

inline static void* hook(void* addr, void* hook_func, void** orig_func, bool isShortFunc = false)
{
#ifdef __aarch64__
	return GlossHookAddr(addr, hook_func, orig_func, isShortFunc, $ARM64);
#else
	return GlossHookAddr((void*) (addr), (void*) (hook_func), (void**) (orig_func), isShortFunc, $THUMB);
#endif
}

// Hook a single call(branch) of a function
static void* hookBL(void* addr, void* hook_func, void** orig_func)
{
#ifdef __aarch64__
	return GlossHookBranchBL((void*) (addr), (void*) (hook_func), (void**) (orig_func), $ARM64);
#else
	const uintptr_t aligned = reinterpret_cast<uintptr_t>(addr) & ~uintptr_t(1);
	if (Gloss::Inst::GetBranch(aligned, $THUMB) == Gloss::Inst::branchs::BLX) {
		return GlossHookBranchBLX(reinterpret_cast<void*>(aligned), hook_func, orig_func, $THUMB);
	}
	return GlossHookBranchBL(reinterpret_cast<void*>(aligned), hook_func, orig_func, $THUMB);
#endif
}

static void saveBytesToMap(uintptr_t addr, uint32_t byte_count, std::unordered_map<uintptr_t, std::vector<uint8_t>>& map)
{
	std::lock_guard<std::mutex> lock(patchMapMutex);
	if (map.find(addr) == map.end()) {
		std::vector<uint8_t> buffer(byte_count);
		readMemory(addr, reinterpret_cast<uintptr_t>(buffer.data()), byte_count);
		spdlog::debug("saveBytesToMap addr: 0x{:x} bytes: {}", addr, buffer2Str(buffer));
		map[addr] = std::move(buffer);
	}
}

inline static void nop(uintptr_t addr, unsigned int word_count)
{
	if (!addr || word_count == 0) return;
#ifdef __aarch64__
	if ((word_count & 1u) != 0u) {
		spdlog::error("Memory::nop odd word_count={} on arm64 at 0x{:X}", word_count, addr);
		return;
	}
	const uintptr_t alignedAddr = addr;
#else
	const uintptr_t alignedAddr = addr & ~uintptr_t(1);
#endif
	g_nopCalls.fetch_add(1, std::memory_order_relaxed);
	unsigned int byte_count = word_count * 2;
	saveBytesToMap(alignedAddr, byte_count, nopOrigBytesMap);
	// Write contiguous NOP instructions directly via writeMemory (flushes I-cache)
	// instead of Gloss::Inst::MakeArm64NOP/MakeThumb32NOP which have a stride-16 bug.
#ifdef __aarch64__
	unsigned int insnCount = byte_count / 4;
	if (insnCount == 0) return;
	std::vector<uint32_t> nops(insnCount, 0xD503201Fu); // ARM64 NOP
	writeMemory(alignedAddr, reinterpret_cast<uintptr_t>(nops.data()), insnCount * sizeof(uint32_t));
#else
	std::vector<uint16_t> nops(word_count, 0xBF00u); // Thumb-16 NOP
	writeMemory(alignedAddr, reinterpret_cast<uintptr_t>(nops.data()), word_count * sizeof(uint16_t));
#endif
}

inline static void restoreNop(uintptr_t addr)
{
	if (!addr) return;
#ifdef __aarch64__
	const uintptr_t alignedAddr = addr;
#else
	const uintptr_t alignedAddr = addr & ~uintptr_t(1);
#endif
	g_restoreNopCalls.fetch_add(1, std::memory_order_relaxed);
	std::vector<uint8_t> bytes;
	{
		std::lock_guard<std::mutex> lock(patchMapMutex);
		auto it = nopOrigBytesMap.find(alignedAddr);
		if (it == nopOrigBytesMap.end()) return;
		bytes = it->second;
	}
	Memory::writeMemory(alignedAddr, reinterpret_cast<uintptr_t>(bytes.data()), bytes.size());
}

inline static void ret(uintptr_t addr)
{
	if (!addr) return;
#ifdef __aarch64__
	const uintptr_t alignedAddr = addr;
	const uint32_t insn = 0xD65F03C0u; // ARM64 RET
	saveBytesToMap(alignedAddr, sizeof(insn), retOrigBytesMap);
	writeMemory(alignedAddr, reinterpret_cast<uintptr_t>(&insn), sizeof(insn));
#else
	const uintptr_t alignedAddr = addr & ~uintptr_t(1);
	const uint16_t insn = 0x4770u; // Thumb BX LR
	saveBytesToMap(alignedAddr, sizeof(insn), retOrigBytesMap);
	writeMemory(alignedAddr, reinterpret_cast<uintptr_t>(&insn), sizeof(insn));
#endif
}

inline static void ret0(uintptr_t addr)
{
	if (!addr) return;
#ifdef __aarch64__
	const uintptr_t alignedAddr = addr;
	const uint32_t insns[2] = {0xD2800000u, 0xD65F03C0u}; // MOV X0, #0; RET
	saveBytesToMap(alignedAddr, sizeof(insns), retOrigBytesMap);
	writeMemory(alignedAddr, reinterpret_cast<uintptr_t>(insns), sizeof(insns));
#else
	const uintptr_t alignedAddr = addr & ~uintptr_t(1);
	const uint16_t insns[2] = {0x2000u, 0x4770u}; // MOVS R0, #0; BX LR
	saveBytesToMap(alignedAddr, sizeof(insns), retOrigBytesMap);
	writeMemory(alignedAddr, reinterpret_cast<uintptr_t>(insns), sizeof(insns));
#endif
}

inline static void restoreRet(uintptr_t addr)
{
	if (!addr) return;
#ifdef __aarch64__
	const uintptr_t alignedAddr = addr;
#else
	const uintptr_t alignedAddr = addr & ~uintptr_t(1);
#endif
	std::vector<uint8_t> bytes;
	{
		std::lock_guard<std::mutex> lock(patchMapMutex);
		auto it = retOrigBytesMap.find(alignedAddr);
		if (it == retOrigBytesMap.end()) return;
		bytes = it->second;
	}
	Memory::writeMemory(alignedAddr, reinterpret_cast<uintptr_t>(bytes.data()), bytes.size());
}

template<typename Ret = void, typename... Args>
inline Ret callFunction(const char* symbol, Args... args)
{
	return g_saSym->CallFunction<Ret>(symbol, args...);
}

template<typename Ret = void, typename... Args>
inline Ret callFunction(void* address, Args... args)
{
//	return reinterpret_cast<Ret(*)(Args...)>(address)(args...);
	return g_saSym->CallFunction<Ret>(reinterpret_cast<uintptr_t>(address), args...);
}

}
