#include "Scripting.h"
#include "Game.h"
#include "Engine/Symbols.h"
#include "../Client.h"
#include <SA.h>
#include <spdlog/spdlog.h>
#include <atomic>
#include <memory>

namespace {
constexpr size_t kScriptBufSize = 512;
constexpr int kInlineScriptDepth = 4;

struct ScriptSlot {
	sa::CRunningScript script{};
	uint8_t buf[kScriptBufSize]{};
};

thread_local ScriptSlot s_slots[kInlineScriptDepth];
thread_local int s_depth = 0;
} // namespace

sa::CRunningScript* gst = nullptr;

void InitScripting()
{
	spdlog::info("Initiating scripting...");
	if (!gst) {
		gst = &s_slots[0].script;
	}
}

std::atomic<uint16_t> g_lastScriptOpcode{0};

int ScriptCommand(const SCRIPT_COMMAND* pScriptCommand, ...)
{
	if (!pScriptCommand) {
		return 0;
	}
	if (!gst) {
		InitScripting();
	}

	std::unique_ptr<ScriptSlot> heapSlot;
	const int slotIdx = s_depth++;
	struct DepthGuard {
		~DepthGuard() { --s_depth; }
	} depthGuard;

	ScriptSlot* slot = nullptr;
	if (slotIdx >= 0 && slotIdx < kInlineScriptDepth) {
		slot = &s_slots[slotIdx];
	}
	else {
		heapSlot = std::make_unique<ScriptSlot>();
		slot = heapSlot.get();
	}

	sa::CRunningScript& curScript = slot->script;
	uint8_t* scriptBuf = slot->buf;
	uint32_t* paramVars[MAX_SCRIPT_VARS]{};

	g_lastScriptOpcode.store(pScriptCommand->OpCode, std::memory_order_relaxed);

	va_list ap;
	const char* p = pScriptCommand->Params;
	va_start(ap, pScriptCommand);
	memcpy(scriptBuf, &pScriptCommand->OpCode, 2);
	size_t buf_pos = 2;
	uint16_t var_pos = 0;

	for (int i = 0; i < MAX_SCRIPT_VARS; i++) {
		curScript.m_nLocals[i] = 0;
	}

	while (*p) {
		switch (*p) {
			case 'i': {
				if (buf_pos + 5 > kScriptBufSize) { va_end(ap); return 0; }
				int i = va_arg(ap, int);
				scriptBuf[buf_pos++] = 0x01;
				memcpy(&scriptBuf[buf_pos], &i, 4);
				buf_pos += 4;
				break;
			}
			case 'f': {
				if (buf_pos + 5 > kScriptBufSize) { va_end(ap); return 0; }
				float f = (float) va_arg(ap, double);
				scriptBuf[buf_pos++] = 0x06;
				memcpy(&scriptBuf[buf_pos], &f, 4);
				buf_pos += 4;
				break;
			}
			case 'v': {
				if (buf_pos + 3 > kScriptBufSize || var_pos >= MAX_SCRIPT_VARS) { va_end(ap); return 0; }
				uint32_t* v = va_arg(ap, uint32_t*);
				if (!v) { va_end(ap); return 0; }
				scriptBuf[buf_pos++] = 0x03;
				paramVars[var_pos] = v;
				curScript.m_nLocals[var_pos] = *v;
				memcpy(&scriptBuf[buf_pos], &var_pos, 2);
				buf_pos += 2;
				var_pos++;
				break;
			}
			case 's': {
				char* sz = va_arg(ap, char*);
				if (!sz) { va_end(ap); return 0; }
				size_t len = strlen(sz);
				if (len > 255) len = 255;
				if (buf_pos + 2 + len > kScriptBufSize) { va_end(ap); return 0; }
				auto aLen = static_cast<uint8_t>(len);
				scriptBuf[buf_pos++] = 0x0E;
				scriptBuf[buf_pos++] = aLen;
				memcpy(&scriptBuf[buf_pos], sz, aLen);
				buf_pos += aLen;
				break;
			}
			case 'z': {
				if (buf_pos + 1 > kScriptBufSize) { va_end(ap); return 0; }
				scriptBuf[buf_pos++] = 0x00;
				break;
			}
			default: {
				va_end(ap);
				return 0;
			}
		}
		++p;
	}
	va_end(ap);

	curScript.m_bCmpFlag = false;
	curScript.m_pPCPointer = scriptBuf;
	curScript.ProcessOneCommand();
	const int result = curScript.m_bCmpFlag;

	for (uint16_t i = 0; i < var_pos; i++) {
		*paramVars[i] = curScript.m_nLocals[i];
	}

	return result;
}
