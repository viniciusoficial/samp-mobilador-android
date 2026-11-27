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

/* Trilha de execucao (breadcrumbs) impressa no relatorio de crash.
   Grava so um ponteiro para string literal + um int num ring buffer: nao
   aloca, nao formata e nao escreve em arquivo, entao pode ser chamado em
   caminho por-frame. Serve para saber exatamente onde o crash aconteceu
   quando o backtrace nao e' confiavel (PC/LR apontando para lixo). */
void        CrashTrace(const char* tag, int value = -1);
int         CrashTraceCount();
const char* CrashTraceTag(int index);
int         CrashTraceValue(int index);

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
