#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>

#include "log.h"

// Log() (arquivo + logcat + crashlytics) vive em main.cpp
void Log(const char* fmt, ...);

namespace
{
	constexpr int RECENT_MAX = 24;
	constexpr int RECENT_LEN = 220;

	char g_recent[RECENT_MAX][RECENT_LEN];
	std::atomic<int> g_recentTotal{ 0 };

	constexpr int TRACE_MAX = 28;
	const char* g_traceTag[TRACE_MAX];
	int g_traceValue[TRACE_MAX];
	std::atomic<unsigned> g_traceTotal{ 0 };

	const char* BaseName(const char* path)
	{
		if (!path) return "?";
		const char* slash = strrchr(path, '/');
		return slash ? slash + 1 : path;
	}
}

void LogEx(char level, const char* file, int line, const char* fmt, ...)
{
	char msg[900];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);

	char out[1024];

	if (level == 'W' || level == 'E')
	{
		snprintf(out, sizeof(out), "[%c] %s:%d | %s", level, BaseName(file), line, msg);
		LogRecentAdd(out);
	}
	else
	{
		snprintf(out, sizeof(out), "[%c] %s", level, msg);
	}

	Log("%s", out);
}

void CrashTrace(const char* tag, int value)
{
	if (!tag) return;

	const unsigned slot = g_traceTotal.fetch_add(1, std::memory_order_relaxed) % TRACE_MAX;
	// value primeiro, tag depois: quem le no handler nunca ve tag novo com valor velho.
	g_traceValue[slot] = value;
	__atomic_store_n(&g_traceTag[slot], tag, __ATOMIC_RELEASE);
}

int CrashTraceCount()
{
	const unsigned total = g_traceTotal.load(std::memory_order_acquire);
	return total < TRACE_MAX ? (int)total : TRACE_MAX;
}

const char* CrashTraceTag(int index)
{
	if (index < 0 || index >= CrashTraceCount()) return nullptr;

	const unsigned total = g_traceTotal.load(std::memory_order_acquire);
	const int count = total < TRACE_MAX ? (int)total : TRACE_MAX;
	const unsigned slot = (total - (unsigned)count + (unsigned)index) % TRACE_MAX;
	return __atomic_load_n(&g_traceTag[slot], __ATOMIC_ACQUIRE);
}

int CrashTraceValue(int index)
{
	if (index < 0 || index >= CrashTraceCount()) return -1;

	const unsigned total = g_traceTotal.load(std::memory_order_acquire);
	const int count = total < TRACE_MAX ? (int)total : TRACE_MAX;
	const unsigned slot = (total - (unsigned)count + (unsigned)index) % TRACE_MAX;
	return g_traceValue[slot];
}

void LogRecentAdd(const char* line)
{
	if (!line) return;

	const int slot = g_recentTotal.fetch_add(1) % RECENT_MAX;
	strncpy(g_recent[slot], line, RECENT_LEN - 1);
	g_recent[slot][RECENT_LEN - 1] = '\0';
}

int LogRecentCount()
{
	const int total = g_recentTotal.load();
	return total < RECENT_MAX ? total : RECENT_MAX;
}

const char* LogRecentAt(int index)
{
	if (index < 0) return nullptr;

	const int total = g_recentTotal.load();
	const int count = total < RECENT_MAX ? total : RECENT_MAX;
	if (index >= count) return nullptr;

	// indice logico 0 = mais antigo ainda guardado
	const int slot = (total - count + index) % RECENT_MAX;
	return g_recent[slot];
}

int LogRecentCapacity()
{
	return RECENT_MAX;
}

void Log::traceLastFunc(const std::string& func_name)
{
	(void)func_name;
}
