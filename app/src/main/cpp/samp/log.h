#pragma once

#include <sstream>
#include <vector>
#include <string>
#include <atomic>
#include <android/log.h>

/*
 * Logging do samp
 * ---------------
 * LOGD/LOGI -> mensagens de fluxo (curtas, sem caminho de arquivo)
 * LOGW/LOGE -> problemas, sempre com  <arquivo>:<linha>  para achar o ponto exato
 *
 * As duas ultimas linhas de erro/aviso ficam em um ring buffer fixo (sem
 * alocacao) e sao despejadas no relatorio de crash, para saber o que falhou
 * imediatamente antes do SIGSEGV.
 */

void LogEx(char level, const char* file, int line, const char* fmt, ...);

void        LogRecentAdd(const char* line);
int         LogRecentCount();
const char* LogRecentAt(int index);
int         LogRecentCapacity();

#define LOGD(...) LogEx('D', __FILE__, __LINE__, __VA_ARGS__)
#define LOGI(...) LogEx('I', __FILE__, __LINE__, __VA_ARGS__)
#define LOGW(...) LogEx('W', __FILE__, __LINE__, __VA_ARGS__)
#define LOGE(...) LogEx('E', __FILE__, __LINE__, __VA_ARGS__)

struct TraceInfo
{
	std::string name;
	std::vector<std::string> params;
};

class Log
{
public:
	static void traceLastFunc(const std::string& func_name);

	template<typename T>
	static void addParameter(const std::string& func_param_name, T value)
	{
		// desativado: gerava ruido no log
	};

private:
};
