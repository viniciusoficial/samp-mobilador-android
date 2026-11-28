#include "BackTrace.h"
#include "Crashlytics.h"

#include <spdlog/spdlog.h>
#include <android/log.h>
#include <sys/prctl.h>
#include <sys/system_properties.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <atomic>
#include <cstring>

extern std::atomic<uint16_t> g_lastScriptOpcode;
extern std::atomic<int> g_lastObjectModel;

void crash_log(const char* format, ...)
{
	va_list args;
	va_start(args, format);

	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), format, args);

	va_end(args);

	spdlog::error("{}", buffer);
	firebase::crashlytics::Log(buffer);
}

void logDeviceInfo()
{
	// Use properties to log device and ABI information
	char device[PROP_VALUE_MAX];
	char abi[PROP_VALUE_MAX];
	char version[PROP_VALUE_MAX];
	__system_property_get("ro.product.model", device);
	__system_property_get("ro.product.cpu.abi", abi);
	__system_property_get("ro.build.version.release", version);

	crash_log("Device: %s", device);
	crash_log("ABI: %s", abi);
	crash_log("Android Version: %s", version);
}

void logLastInfo()
{
	crash_log(" ");
	crash_log("Last Script Command: 0x%04X", (unsigned int) g_lastScriptOpcode.load(std::memory_order_relaxed));
	crash_log("Last Rendered Object Model: %d", g_lastObjectModel.load(std::memory_order_relaxed));
	crash_log(" ");
}

// Load base of our own .so, captured at startup via dl_iterate_phdr — lets
// crash_breadcrumb print pc as an offset into libSAMPMOBILE/libGTASA so every
// crash maps to a symbol without a tombstone
uintptr_t g_sampLibBase = 0;
uintptr_t g_gtasaLibBase = 0;

namespace {

// Async-signal-safe hex parser (zero heap allocation, safe in SIGSEGV handler)
bool parseHexField(const char*& p, uintptr_t& out)
{
	uintptr_t val = 0;
	bool any = false;
	while (*p) {
		char c = *p;
		uintptr_t d = 0;
		if (c >= '0' && c <= '9') d = static_cast<uintptr_t>(c - '0');
		else if (c >= 'a' && c <= 'f') d = static_cast<uintptr_t>(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') d = static_cast<uintptr_t>(c - 'A' + 10);
		else break;
		val = (val << 4) | d;
		any = true;
		++p;
	}
	out = val;
	return any;
}

// Async-signal-safe /proc/self/maps lookup: finds the mapped ELF/module containing addr
// and computes module-relative offset (so crashes in libmonetloader/libAML/plugins map instantly).
bool findModuleForAddrAsyncSafe(uintptr_t addr, char* outName, size_t outNameCap, uintptr_t& outOff)
{
	if (!addr || !outName || outNameCap < 2) return false;
	outName[0] = '\0';
	outOff = 0;

	int fd = open("/proc/self/maps", O_RDONLY);
	if (fd < 0) return false;

	char readBuf[1024];
	char line[512];
	size_t lineLen = 0;
	bool found = false;

	auto processLine = [&](const char* ln) {
		const char* p = ln;
		uintptr_t start = 0, end = 0, fileOff = 0;
		if (!parseHexField(p, start) || *p != '-') return false;
		++p;
		if (!parseHexField(p, end)) return false;
		if (addr < start || addr >= end) return false;

		// Skip perms (e.g. " r-xp ")
		while (*p == ' ') ++p;
		while (*p && *p != ' ') ++p;
		while (*p == ' ') ++p;
		parseHexField(p, fileOff);

		// Find pathname (6th column)
		const char* path = nullptr;
		const char* slash = strchr(p, '/');
		const char* bracket = strchr(p, '[');
		if (slash && (!bracket || slash < bracket)) {
			path = slash;
		} else if (bracket) {
			path = bracket;
		}

		const char* base = path ? strrchr(path, '/') : nullptr;
		const char* label = base ? (base + 1) : (path ? path : "anon");
		size_t i = 0;
		while (label[i] && label[i] != '\n' && label[i] != '\r' && i + 1 < outNameCap) {
			outName[i] = label[i];
			++i;
		}
		outName[i] = '\0';
		outOff = (addr - start) + fileOff;
		return true;
	};

	ssize_t n = 0;
	while (!found && (n = read(fd, readBuf, sizeof(readBuf))) > 0) {
		for (ssize_t i = 0; i < n; ++i) {
			char c = readBuf[i];
			if (c == '\n') {
				line[lineLen] = '\0';
				if (processLine(line)) {
					found = true;
					break;
				}
				lineLen = 0;
			} else if (lineLen + 1 < sizeof(line)) {
				line[lineLen++] = c;
			}
		}
	}
	if (!found && lineLen > 0) {
		line[lineLen] = '\0';
		found = processLine(line);
	}
	close(fd);
	return found;
}

void appendStrAsync(char* dst, size_t cap, size_t& pos, const char* s)
{
	if (!s || pos + 1 >= cap) return;
	while (*s && pos + 1 < cap) {
		dst[pos++] = *s++;
	}
	dst[pos] = '\0';
}

void appendHexAsync(char* dst, size_t cap, size_t& pos, uintptr_t v, int minDigits = 1)
{
	static const char kHex[] = "0123456789abcdef";
	char tmp[20];
	int n = 0;
	do {
		tmp[n++] = kHex[v & 0xFu];
		v >>= 4;
	} while (v != 0 && n < (int) sizeof(tmp));
	while (n < minDigits && n < (int) sizeof(tmp)) {
		tmp[n++] = '0';
	}
	while (n > 0 && pos + 1 < cap) {
		dst[pos++] = tmp[--n];
	}
	if (cap > 0) dst[pos] = '\0';
}

void appendDecAsync(char* dst, size_t cap, size_t& pos, long v)
{
	if (v < 0) {
		appendStrAsync(dst, cap, pos, "-");
		unsigned long uv = static_cast<unsigned long>(-(v + 1)) + 1UL;
		char tmp[24];
		int n = 0;
		do {
			tmp[n++] = static_cast<char>('0' + (uv % 10UL));
			uv /= 10UL;
		} while (uv != 0 && n < (int) sizeof(tmp));
		while (n > 0 && pos + 1 < cap) {
			dst[pos++] = tmp[--n];
		}
		if (cap > 0) dst[pos] = '\0';
		return;
	}
	unsigned long uv = static_cast<unsigned long>(v);
	char tmp[24];
	int n = 0;
	do {
		tmp[n++] = static_cast<char>('0' + (uv % 10UL));
		uv /= 10UL;
	} while (uv != 0 && n < (int) sizeof(tmp));
	while (n > 0 && pos + 1 < cap) {
		dst[pos++] = tmp[--n];
	}
	if (cap > 0) dst[pos] = '\0';
}

// Async-signal-safe single-line field extractor from a /proc file:
// opens the file with raw syscalls, scans for a line starting with `key`,
// and copies the remainder of that line (trimmed) into out.
bool readProcFieldAsyncSafe(const char* path, const char* key, char* out, size_t outCap)
{
	if (!path || !key || !out || outCap < 2) return false;
	out[0] = '\0';
	int fd = open(path, O_RDONLY);
	if (fd < 0) return false;

	char buf[2048];
	const size_t keyLen = strlen(key);
	size_t outPos = 0;
	bool inKeyLine = false;
	bool skippingLine = false;
	bool done = false;
	size_t keyPos = 0;

	while (!done) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if (n <= 0) break;
		for (ssize_t i = 0; i < n && !done; ++i) {
			char c = buf[i];
			if (c == '\n') {
				if (inKeyLine) {
					done = true;
				}
				inKeyLine = false;
				skippingLine = false;
				keyPos = 0;
				continue;
			}
			if (inKeyLine) {
				if (outPos + 1 < outCap && c != '\r') {
					out[outPos++] = c;
				}
				continue;
			}
			if (skippingLine) continue;
			if (keyPos < keyLen) {
				if (c == key[keyPos]) {
					++keyPos;
					if (keyPos == keyLen) {
						inKeyLine = true;
					}
				} else {
					skippingLine = true;
				}
			}
		}
	}
	close(fd);
	// trim leading spaces/tabs
	size_t lead = 0;
	while (lead < outPos && (out[lead] == ' ' || out[lead] == '\t')) ++lead;
	if (lead > 0) {
		for (size_t i = 0; i + lead < outPos; ++i) out[i] = out[i + lead];
		outPos -= lead;
	}
	out[outPos] = '\0';
	return outPos > 0;
}

} // namespace

// Preformatted "<gameDir>logcat/crash_" prefix captured at client init so the
// fatal handler can build the report filename without any allocation.
static char s_crashPathPrefix[320] = {};
static bool s_crashPathReady = false;

void crash_set_report_dir(const char* gameDir)
{
	if (!gameDir || !*gameDir) return;
	size_t pos = 0;
	s_crashPathPrefix[0] = '\0';
	appendStrAsync(s_crashPathPrefix, sizeof(s_crashPathPrefix), pos, gameDir);
	if (pos > 0 && s_crashPathPrefix[pos - 1] != '/') {
		appendStrAsync(s_crashPathPrefix, sizeof(s_crashPathPrefix), pos, "/");
	}
	appendStrAsync(s_crashPathPrefix, sizeof(s_crashPathPrefix), pos, "logcat/crash_");
	s_crashPathReady = pos + 8 < sizeof(s_crashPathPrefix);
}

void crash_breadcrumb(int signal, siginfo_t* info, void* ctx)
{
	uintptr_t pc = 0;
	uintptr_t lr = 0;
	if (ctx) {
#ifdef __aarch64__
		pc = reinterpret_cast<ucontext*>(ctx)->uc_mcontext.pc;
		lr = reinterpret_cast<ucontext*>(ctx)->uc_mcontext.regs[30];
#else
		pc = reinterpret_cast<ucontext*>(ctx)->uc_mcontext.arm_pc;
		lr = reinterpret_cast<ucontext*>(ctx)->uc_mcontext.arm_lr;
#endif
	}

	char pcMod[64] = "unknown";
	uintptr_t pcModOff = 0;
	findModuleForAddrAsyncSafe(pc, pcMod, sizeof(pcMod), pcModOff);

	char lrMod[64] = "unknown";
	uintptr_t lrModOff = 0;
	findModuleForAddrAsyncSafe(lr, lrMod, sizeof(lrMod), lrModOff);

	const uintptr_t faultAddr = info ? reinterpret_cast<uintptr_t>(info->si_addr) : 0;
	const uint16_t lastOpcode = g_lastScriptOpcode.load(std::memory_order_relaxed);
	const int lastObj = g_lastObjectModel.load(std::memory_order_relaxed);

	// ---- one-line summary to logcat (debuggerd/tombstone linkage) ----
	char buf[512];
	size_t pos = 0;
	buf[0] = '\0';
	appendStrAsync(buf, sizeof(buf), pos, "fatal signal ");
	appendDecAsync(buf, sizeof(buf), pos, signal);
	appendStrAsync(buf, sizeof(buf), pos, " (code ");
	appendDecAsync(buf, sizeof(buf), pos, info ? info->si_code : 0);
	appendStrAsync(buf, sizeof(buf), pos, "), fault 0x");
	appendHexAsync(buf, sizeof(buf), pos, faultAddr);
	appendStrAsync(buf, sizeof(buf), pos, ", pc 0x");
	appendHexAsync(buf, sizeof(buf), pos, pc);
	appendStrAsync(buf, sizeof(buf), pos, " (");
	appendStrAsync(buf, sizeof(buf), pos, pcMod);
	appendStrAsync(buf, sizeof(buf), pos, "+0x");
	appendHexAsync(buf, sizeof(buf), pos, pcModOff);
	appendStrAsync(buf, sizeof(buf), pos, "), lr 0x");
	appendHexAsync(buf, sizeof(buf), pos, lr);
	appendStrAsync(buf, sizeof(buf), pos, " (");
	appendStrAsync(buf, sizeof(buf), pos, lrMod);
	appendStrAsync(buf, sizeof(buf), pos, "+0x");
	appendHexAsync(buf, sizeof(buf), pos, lrModOff);
	appendStrAsync(buf, sizeof(buf), pos, "), lastObj ");
	appendDecAsync(buf, sizeof(buf), pos, lastObj);
	appendStrAsync(buf, sizeof(buf), pos, ", lastCmd 0x");
	appendHexAsync(buf, sizeof(buf), pos, lastOpcode, 4);

	__android_log_write(ANDROID_LOG_FATAL, "SAMPMOBILE", buf);

	// ---- full report to crash file (feedback bundle collects these) ----
	if (!s_crashPathReady) return;

	const long epoch = static_cast<long>(time(nullptr));
	const long pid = static_cast<long>(getpid());
	const long tid = static_cast<long>(syscall(SYS_gettid));

	static char report[8192];
	size_t rpos = 0;
	report[0] = '\0';

	appendStrAsync(report, sizeof(report), rpos, "========== SA-MP Mobile crash report ==========\n");
	appendStrAsync(report, sizeof(report), rpos, "epoch: ");
	appendDecAsync(report, sizeof(report), rpos, epoch);
	appendStrAsync(report, sizeof(report), rpos, "\npid: ");
	appendDecAsync(report, sizeof(report), rpos, pid);
	appendStrAsync(report, sizeof(report), rpos, " tid: ");
	appendDecAsync(report, sizeof(report), rpos, tid);

	{
		// Use prctl(PR_GET_NAME) so we capture the crashing thread's own name
		// (e.g. "NvEvent" vs "main"), not /proc/self/comm which is always the main thread.
		char cbuf[32] = {};
		if (prctl(PR_GET_NAME, cbuf, 0, 0, 0) == 0 && cbuf[0] != '\0') {
			cbuf[sizeof(cbuf) - 1] = '\0';
			appendStrAsync(report, sizeof(report), rpos, " comm: ");
			appendStrAsync(report, sizeof(report), rpos, cbuf);
		}
	}

	appendStrAsync(report, sizeof(report), rpos, "\nsignal: ");
	appendDecAsync(report, sizeof(report), rpos, signal);
	switch (signal) {
		case SIGSEGV: appendStrAsync(report, sizeof(report), rpos, " (SIGSEGV)"); break;
		case SIGABRT: appendStrAsync(report, sizeof(report), rpos, " (SIGABRT)"); break;
		case SIGBUS:  appendStrAsync(report, sizeof(report), rpos, " (SIGBUS)"); break;
		case SIGILL:  appendStrAsync(report, sizeof(report), rpos, " (SIGILL)"); break;
		case SIGFPE:  appendStrAsync(report, sizeof(report), rpos, " (SIGFPE)"); break;
		case SIGTRAP: appendStrAsync(report, sizeof(report), rpos, " (SIGTRAP)"); break;
		case SIGSYS:  appendStrAsync(report, sizeof(report), rpos, " (SIGSYS)"); break;
		default: break;
	}
	appendStrAsync(report, sizeof(report), rpos, " code: ");
	appendDecAsync(report, sizeof(report), rpos, info ? info->si_code : 0);
	if (signal == SIGSEGV && info) {
		if (info->si_code == SEGV_MAPERR) {
			appendStrAsync(report, sizeof(report), rpos, " (SEGV_MAPERR)");
		} else if (info->si_code == SEGV_ACCERR) {
			appendStrAsync(report, sizeof(report), rpos, " (SEGV_ACCERR)");
		} else if (info->si_code == 8) {
			appendStrAsync(report, sizeof(report), rpos, " (SEGV_MTEAERR)");
		} else if (info->si_code == 9) {
			appendStrAsync(report, sizeof(report), rpos, " (SEGV_MTESERR)");
		}
	} else if (signal == SIGSYS && info) {
		appendStrAsync(report, sizeof(report), rpos, " syscall: ");
		appendDecAsync(report, sizeof(report), rpos, info->si_syscall);
	}
	appendStrAsync(report, sizeof(report), rpos, "\nfault_addr: 0x");
	appendHexAsync(report, sizeof(report), rpos, faultAddr);

	appendStrAsync(report, sizeof(report), rpos, "\npc: 0x");
	appendHexAsync(report, sizeof(report), rpos, pc);
	appendStrAsync(report, sizeof(report), rpos, " (");
	appendStrAsync(report, sizeof(report), rpos, pcMod);
	appendStrAsync(report, sizeof(report), rpos, "+0x");
	appendHexAsync(report, sizeof(report), rpos, pcModOff);
	appendStrAsync(report, sizeof(report), rpos, ")");
	appendStrAsync(report, sizeof(report), rpos, "\nlr: 0x");
	appendHexAsync(report, sizeof(report), rpos, lr);
	appendStrAsync(report, sizeof(report), rpos, " (");
	appendStrAsync(report, sizeof(report), rpos, lrMod);
	appendStrAsync(report, sizeof(report), rpos, "+0x");
	appendHexAsync(report, sizeof(report), rpos, lrModOff);
	appendStrAsync(report, sizeof(report), rpos, ")");

	appendStrAsync(report, sizeof(report), rpos, "\nlast script opcode: 0x");
	appendHexAsync(report, sizeof(report), rpos, lastOpcode, 4);
	appendStrAsync(report, sizeof(report), rpos, "\nlast object model: ");
	appendDecAsync(report, sizeof(report), rpos, lastObj);

	appendStrAsync(report, sizeof(report), rpos, "\n\nregisters:\n");
	if (ctx) {
#ifdef __aarch64__
		auto* uc = reinterpret_cast<ucontext*>(ctx);
		for (int i = 0; i < 31; ++i) {
			appendStrAsync(report, sizeof(report), rpos, "x");
			appendDecAsync(report, sizeof(report), rpos, i);
			appendStrAsync(report, sizeof(report), rpos, ": 0x");
			appendHexAsync(report, sizeof(report), rpos, uc->uc_mcontext.regs[i]);
			appendStrAsync(report, sizeof(report), rpos, (i % 4 == 3) ? "\n" : "  ");
		}
		appendStrAsync(report, sizeof(report), rpos, "sp: 0x");
		appendHexAsync(report, sizeof(report), rpos, uc->uc_mcontext.sp);
		appendStrAsync(report, sizeof(report), rpos, "\n");
#else
		auto* uc = reinterpret_cast<ucontext*>(ctx);
		const unsigned int armRegs[13] = {
			uc->uc_mcontext.arm_r0, uc->uc_mcontext.arm_r1, uc->uc_mcontext.arm_r2,
			uc->uc_mcontext.arm_r3, uc->uc_mcontext.arm_r4, uc->uc_mcontext.arm_r5,
			uc->uc_mcontext.arm_r6, uc->uc_mcontext.arm_r7, uc->uc_mcontext.arm_r8,
			uc->uc_mcontext.arm_r9, uc->uc_mcontext.arm_r10, uc->uc_mcontext.arm_fp,
			uc->uc_mcontext.arm_ip};
		for (int i = 0; i < 13; ++i) {
			appendStrAsync(report, sizeof(report), rpos, "r");
			appendDecAsync(report, sizeof(report), rpos, i);
			appendStrAsync(report, sizeof(report), rpos, ": 0x");
			appendHexAsync(report, sizeof(report), rpos, armRegs[i]);
			appendStrAsync(report, sizeof(report), rpos, (i % 4 == 3) ? "\n" : "  ");
		}
		appendStrAsync(report, sizeof(report), rpos, "sp: 0x");
		appendHexAsync(report, sizeof(report), rpos, uc->uc_mcontext.arm_sp);
		appendStrAsync(report, sizeof(report), rpos, "\n");
#endif
	} else {
		appendStrAsync(report, sizeof(report), rpos, "(no ucontext)\n");
	}

	char vmrss[96];
	if (readProcFieldAsyncSafe("/proc/self/status", "VmRSS:", vmrss, sizeof(vmrss))) {
		appendStrAsync(report, sizeof(report), rpos, "\nVmRSS: ");
		appendStrAsync(report, sizeof(report), rpos, vmrss);
	}
	char memavail[96];
	if (readProcFieldAsyncSafe("/proc/meminfo", "MemAvailable:", memavail, sizeof(memavail))) {
		appendStrAsync(report, sizeof(report), rpos, "\nMemAvailable: ");
		appendStrAsync(report, sizeof(report), rpos, memavail);
	}
	char memtotal[96];
	if (readProcFieldAsyncSafe("/proc/meminfo", "MemTotal:", memtotal, sizeof(memtotal))) {
		appendStrAsync(report, sizeof(report), rpos, "\nMemTotal: ");
		appendStrAsync(report, sizeof(report), rpos, memtotal);
	}
	appendStrAsync(report, sizeof(report), rpos, "\n");

	// filename: <prefix><epoch>_<pid>.log
	char path[384];
	size_t ppos = 0;
	path[0] = '\0';
	appendStrAsync(path, sizeof(path), ppos, s_crashPathPrefix);
	appendDecAsync(path, sizeof(path), ppos, epoch);
	appendStrAsync(path, sizeof(path), ppos, "_");
	appendDecAsync(path, sizeof(path), ppos, pid);
	appendStrAsync(path, sizeof(path), ppos, ".log");

	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		size_t written = 0;
		while (written < rpos) {
			ssize_t w = write(fd, report + written, rpos - written);
			if (w <= 0) break;
			written += static_cast<size_t>(w);
		}
		close(fd);
	}
}

void dump_register(int signal, siginfo_t* info, void* ctx)
{
	// 1-indexed POSIX signal names (slot 0 unused)
	static const std::array<const char*, 13> signal_name = {
			"UNKNOWN", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP", "SIGABRT",
			"SIGBUS", "SIGFPE", "SIGKILL", "SIGUSR1", "SIGSEGV", "SIGUSR2"};

	crash_log(" ");
	crash_log("========== SA-MP Mobile Crashed ==========");

	logDeviceInfo();
	logLastInfo();

	std::string si_code_name = "UNKNOWN";
	if (info && info->si_code == SI_USER) {
		crash_log("Cause: Killed by user");
		si_code_name = "SI_USER";
	}

	if (signal == SIGSEGV && info) {
		if (info->si_code == SEGV_MAPERR) {
			crash_log("Cause: De-referencing a null pointer");
			si_code_name = "SEGV_MAPERR";
		}
		else if (info->si_code == SEGV_ACCERR) {
			crash_log("Cause: Read-only pointer");
			si_code_name = "SEGV_ACCERR";
		}
	}

	auto context = reinterpret_cast<ucontext*>(ctx);

	const char* sigLabel = (signal >= 0 && signal < static_cast<int>(signal_name.size()))
			? signal_name[signal] : "UNKNOWN";
	crash_log("Signal: %d (%s)", signal, sigLabel);
	crash_log("Code: %d (%s)", info ? info->si_code : 0, si_code_name.c_str());
	if (!context) return;
	crash_log("Fault Address: 0x%llX", (unsigned long long) context->uc_mcontext.fault_address);

	crash_log(" ");
	crash_log("Register States:");

#ifdef __aarch64__
	for (int i = 0; i < 31; ++i) {
		crash_log("\tx%d: 0x%llX", i, (unsigned long long) context->uc_mcontext.regs[i]);
	}
	crash_log("\tSP: 0x%llX, PC: 0x%llX",
			(unsigned long long) context->uc_mcontext.sp,
			(unsigned long long) context->uc_mcontext.pc);
#else
	crash_log("\tr0: 0x%X, r1: 0x%X, r2: 0x%X, r3: 0x%X",
			(unsigned int) context->uc_mcontext.arm_r0, (unsigned int) context->uc_mcontext.arm_r1,
			(unsigned int) context->uc_mcontext.arm_r2, (unsigned int) context->uc_mcontext.arm_r3);
	crash_log("\tr4: 0x%X, r5: 0x%X, r6: 0x%X, r7: 0x%X",
			(unsigned int) context->uc_mcontext.arm_r4, (unsigned int) context->uc_mcontext.arm_r5,
			(unsigned int) context->uc_mcontext.arm_r6, (unsigned int) context->uc_mcontext.arm_r7);
	crash_log("\tSP: 0x%X, LR: 0x%X, PC: 0x%X",
			(unsigned int) context->uc_mcontext.arm_sp,
			(unsigned int) context->uc_mcontext.arm_lr,
			(unsigned int) context->uc_mcontext.arm_pc);
#endif
}

struct BacktraceState {
	void** current;
	void** end;
};

_Unwind_Reason_Code unwind_callback(struct _Unwind_Context* context, void* arg)
{
	auto state = reinterpret_cast<BacktraceState*>(arg);
	uintptr_t pc = _Unwind_GetIP(context);  // Get the instruction pointer
	if (pc) {
		if (state->current == state->end) {
			return _URC_END_OF_STACK;  // End of stack trace
		}
		else {
			*state->current++ = reinterpret_cast<void*>(pc);
		}
	}
	return _URC_NO_REASON;
}

void dump_stack(int ignoreDepth, int maxDepth)
{
	crash_log(" ");
	crash_log("BackTrace:");

	const int max = 100;
	void* buffer[max];

	BacktraceState state{};
	state.current = buffer;
	state.end = buffer + max;

	_Unwind_Backtrace(unwind_callback, &state);

	int count = (int) (state.current - buffer);
	if (maxDepth >= 0 && count > maxDepth) {
		count = maxDepth;
	}
	if (count > max) {
		count = max;
	}
	if (ignoreDepth < 0) {
		ignoreDepth = 0;
	}

	for (int idx = ignoreDepth; idx < count; idx++) {
		const void* addr = buffer[idx];
		const char* symbol = "";

		Dl_info info{};
		int dlOk = dladdr(addr, &info);
		if (dlOk && info.dli_sname) {
			symbol = info.dli_sname;
		}

		int status{0};
		char* demangled = (symbol[0] != '\0') ? __cxxabiv1::__cxa_demangle(symbol, nullptr, nullptr, &status) : nullptr;

		crash_log("\t#%d [%s:0x%llX] + 0x%llX | [%s+0x%llX]",
				(idx - ignoreDepth),
				(dlOk && info.dli_fname) ? info.dli_fname : "unknown file",
				(unsigned long long) (uintptr_t) info.dli_fbase,
				(unsigned long long) ((uintptr_t) addr - (uintptr_t) info.dli_fbase),
				(dlOk && info.dli_sname) ? ((status == 0 && demangled) ? demangled : info.dli_sname) : "unknown symbol",
				(unsigned long long) ((uintptr_t) addr - (uintptr_t) info.dli_saddr));

		if (demangled) {
			free(demangled);
		}
	}
}
