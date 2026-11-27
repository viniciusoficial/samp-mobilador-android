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
