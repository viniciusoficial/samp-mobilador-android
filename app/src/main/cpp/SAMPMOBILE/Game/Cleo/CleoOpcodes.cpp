#include "CleoInternal.h"

namespace cleo_internal {

bool readTargetLabelOffset(CleoScript& sc, uint8_t*& outTarget)
{
	g_fnCollectParams(&sc.thread, 1);
	int32_t off = static_cast<int32_t>(g_scriptParams[0]);
	size_t rel = (off >= 0) ? static_cast<size_t>(off) : static_cast<size_t>(-static_cast<int64_t>(off));
	if (rel >= sc.code.size()) {
		cleoLog("Script '%s': jump offset %d out of bounds (size=%zu), terminating",
		        sc.fileName.c_str(), off, sc.code.size());
		sc.thread.m_bActive = 0;
		return false;
	}
	outTarget = sc.code.data() + rel;
	return true;
}

bool readStringParam(CleoScript& sc, std::string& out)
{
	out.clear();
	uint8_t* ip = sc.thread.m_pPCPointer;
	uint8_t* end = sc.code.data() + sc.code.size();
	if (ip >= end) return false;

	uint8_t type = *ip;
	switch (type) {
		case sa::SCRIPT_PARAM_STATIC_SHORT_STRING: { // 0x09 (8 bytes)
			if (ip + 9 > end) return false;
			char tmp[9]{};
			memcpy(tmp, ip + 1, 8);
			out = tmp;
			sc.thread.m_pPCPointer = ip + 9;
			return true;
		}
		case sa::SCRIPT_PARAM_STATIC_PASCAL_STRING: { // 0x0E (len + bytes)
			if (ip + 2 > end) return false;
			uint8_t len = ip[1];
			if (ip + 2 + len > end) return false;
			out.assign(reinterpret_cast<const char*>(ip + 2), len);
			sc.thread.m_pPCPointer = ip + 2 + len;
			return true;
		}
		case sa::SCRIPT_PARAM_STATIC_LONG_STRING: { // 0x0F (16 bytes)
			if (ip + 17 > end) return false;
			char tmp[17]{};
			memcpy(tmp, ip + 1, 16);
			out = tmp;
			sc.thread.m_pPCPointer = ip + 17;
			return true;
		}
		case sa::SCRIPT_PARAM_GLOBAL_SHORT_STRING_VARIABLE:
		case sa::SCRIPT_PARAM_LOCAL_SHORT_STRING_VARIABLE: {
			uint32_t* p = g_fnGetVarPtr(&sc.thread, 0);
			if (!p) return false;
			char tmp[9]{};
			memcpy(tmp, p, 8);
			out = tmp;
			return true;
		}
		case sa::SCRIPT_PARAM_GLOBAL_LONG_STRING_VARIABLE:
		case sa::SCRIPT_PARAM_LOCAL_LONG_STRING_VARIABLE: {
			uint32_t* p = g_fnGetVarPtr(&sc.thread, 0);
			if (!p) return false;
			char tmp[17]{};
			memcpy(tmp, p, 16);
			out = tmp;
			return true;
		}
		default: {
			// Integer / variable holding a 32-bit handle/pointer from 0DD0 / 0AC6 / 0AC8
			g_fnCollectParams(&sc.thread, 1);
			size_t bufSize = 0;
			void* ptr = resolveHandleOrAddr(g_scriptParams[0], false, &bufSize);
			if (ptr) {
				const char* cs = static_cast<const char*>(ptr);
				const size_t maxLen = (bufSize > 0) ? bufSize : 256;
				out.assign(cs, strnlen(cs, maxLen));
				return true;
			}
			return false;
		}
	}
}

uint16_t* makeTempGxt(const std::string& text)
{
	uint16_t* buf = g_msgGxtBuf[g_msgGxtSlot];
	g_msgGxtSlot = (g_msgGxtSlot + 1) & 3;
	if (g_fnAsciiToGxt) {
		std::string clipped = text.substr(0, 500);
		g_fnAsciiToGxt(clipped.c_str(), buf);
	} else {
		size_t n = std::min<size_t>(text.size(), 510);
		for (size_t i = 0; i < n; ++i) buf[i] = static_cast<unsigned char>(text[i]);
		buf[n] = 0;
	}
	return buf;
}

std::string resolveScriptPath(const std::string& rawPath)
{
	if (rawPath.empty()) return g_currentDir;
	std::string p = rawPath;
	for (char& c : p) {
		if (c == '\\') c = '/';
	}
	// Reject path traversal outside game/cleo sandbox
	std::vector<std::string> parts;
	size_t i = 0;
	while (i < p.size()) {
		while (i < p.size() && p[i] == '/') ++i;
		size_t start = i;
		while (i < p.size() && p[i] != '/') ++i;
		if (i > start) {
			std::string seg = p.substr(start, i - start);
			if (seg == ".") continue;
			if (seg == "..") {
				cleoLog("Blocked unsafe CLEO path traversal: '%s'", rawPath.c_str());
				return {};
			}
			parts.push_back(std::move(seg));
		}
	}
	if (parts.empty()) return g_currentDir;
	std::string joined;
	for (size_t k = 0; k < parts.size(); ++k) {
		if (k > 0) joined.push_back('/');
		joined += parts[k];
	}
	return g_currentDir + joined;
}

// ---------------------------------------------------------------------------
// Custom CLEO Opcode Dispatcher (returns true if handled)
// ---------------------------------------------------------------------------
bool handleCustomOpcode(CleoScript& sc, uint16_t rawOp)
{
	const bool isNot = (rawOp & 0x8000u) != 0;
	const uint16_t op = rawOp & 0x7FFFu;

	// Helper to set condition with NOT inversion handled by GTA UpdateCompareFlag
	auto setCond = [&](bool val) {
		sc.thread.m_bNotForLatestExpression = isNot ? 1 : 0;
		g_fnUpdateCmpFlag(&sc.thread, val ? 1 : 0);
	};

	switch (op) {
		// -------- Core Control Flow (relative to script.code) --------
		case 0x0002: { // JUMP
			uint8_t* target = nullptr;
			if (readTargetLabelOffset(sc, target)) {
				sc.thread.m_pPCPointer = target;
			}
			return true;
		}
		case 0x004C: { // JUMP_IF_TRUE (JT)
			uint8_t* target = nullptr;
			if (readTargetLabelOffset(sc, target) && sc.thread.m_bCmpFlag != 0) {
				sc.thread.m_pPCPointer = target;
			}
			return true;
		}
		case 0x004D: { // JUMP_IF_FALSE (JF)
			uint8_t* target = nullptr;
			if (readTargetLabelOffset(sc, target) && sc.thread.m_bCmpFlag == 0) {
				sc.thread.m_pPCPointer = target;
			}
			return true;
		}
		case 0x0050: { // GOSUB (CALL)
			uint8_t* target = nullptr;
			if (readTargetLabelOffset(sc, target)) {
				if (sc.thread.m_nStackDepth < 8) {
					sc.thread.m_pPCStack[sc.thread.m_nStackDepth++] = sc.thread.m_pPCPointer;
					sc.thread.m_pPCPointer = target;
				} else {
					cleoLog("Script '%s': GOSUB stack overflow, terminating", sc.fileName.c_str());
					sc.thread.m_bActive = 0;
				}
			}
			return true;
		}
		case 0x0051: { // RETURN
			if (sc.thread.m_nStackDepth > 0) {
				sc.thread.m_pPCPointer = sc.thread.m_pPCStack[--sc.thread.m_nStackDepth];
			} else {
				sc.thread.m_bActive = 0;
			}
			return true;
		}
		case 0x004E: // END_THREAD
		case 0x05DC: // END_CUSTOM_THREAD
		case 0x0A93: { // TERMINATE_THIS_CUSTOM_SCRIPT
			cleoLog("Script '%s' terminated (opcode 0x%04X)", sc.fileName.c_str(), op);
			sc.thread.m_bActive = 0;
			return true;
		}

		// -------- CLEO Android Opcodes (0DD0..0DF6) --------
		case 0x0DD0:   // GET_LABEL_ADDR
		case 0x0AC6: { // GET_LABEL_POINTER
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			uint8_t* target = nullptr;
			if (readTargetLabelOffset(sc, target) && outVar) {
				*outVar = registerHandle(target, sc.code.data() + sc.code.size() - target, false);
			}
			return true;
		}
		case 0x0DD1: { // GET_FUNC_ADDR_CSTR
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			std::string symName;
			readStringParam(sc, symName);
			void* sym = symName.empty() ? nullptr : eng::lookup(symName.c_str());
			if (!sym && !symName.empty()) {
				sym = dlsym(RTLD_DEFAULT, symName.c_str());
			}
			if (outVar) *outVar = registerHandle(sym, 0, false);
			return true;
		}
		case 0x0DD2: { // CONTEXT_CALL (32-bit only; safe no-op on 64-bit)
			g_fnCollectParams(&sc.thread, 1);
			cleoLog("Script '%s': 0DD2 CONTEXT_CALL skipped (unsafe in dual-ABI/SA-MP)", sc.fileName.c_str());
			return true;
		}
		case 0x0DD3: { // CONTEXT_SET_REG
			g_fnCollectParams(&sc.thread, 2);
			uint32_t reg = g_scriptParams[0];
			if (reg < 32) sc.context[reg] = g_scriptParams[1];
			return true;
		}
		case 0x0DD4: { // CONTEXT_GET_REG
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			g_fnCollectParams(&sc.thread, 1);
			uint32_t reg = g_scriptParams[0];
			if (outVar) *outVar = (reg < 32) ? sc.context[reg] : 0;
			return true;
		}
		case 0x0DD5: { // GET_PLATFORM (1 = Android)
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			if (outVar) *outVar = 1;
			return true;
		}
		case 0x0DD6: { // GET_GAME_VER
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			if (outVar) *outVar = kCleoVerGtaSa200Plus;
			return true;
		}
		case 0x0DD7: { // GET_IMAGE_BASE
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			if (outVar) *outVar = registerHandle(reinterpret_cast<void*>(g_gtasaBase), 0, false);
			return true;
		}
		case 0x0DD8: { // READ_MEM(outVar, addr, size, addBase)
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			g_fnCollectParams(&sc.thread, 3);
			uint32_t addr = g_scriptParams[0];
			uint32_t sz = std::min<uint32_t>(g_scriptParams[1], 4);
			bool addBase = g_scriptParams[2] != 0;
			if (outVar) {
				*outVar = 0;
				size_t bufSize = 0;
				if (void* ptr = resolveHandleOrAddr(addr, addBase, &bufSize)) {
					if (bufSize == 0 || sz <= bufSize) {
						memcpy(outVar, ptr, sz);
					}
				}
			}
			return true;
		}
		case 0x0DD9: { // WRITE_MEM(addr, val, size, addBase, vp)
			g_fnCollectParams(&sc.thread, 5);
			uint32_t addr = g_scriptParams[0];
			uint32_t val = g_scriptParams[1];
			uint32_t sz = std::min<uint32_t>(g_scriptParams[2], 4);
			bool addBase = g_scriptParams[3] != 0;
			// Only allow writing to CLEO-owned handles (protects libGTASA.so code on 64-bit)
			auto it = g_ptrHandles.find(addr);
			if (!addBase && it != g_ptrHandles.end() && it->second.ptr && sz <= it->second.size) {
				memcpy(it->second.ptr, &val, sz);
			}
			return true;
		}
		case 0x0DDA: { // SEARCH_MEM (safe stub returning 0)
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			g_fnCollectParams(&sc.thread, 2);
			if (outVar) *outVar = 0;
			return true;
		}
		case 0x0DDB: { // GET_GAME_VER_EX
			uint32_t* id = g_fnGetVarPtr(&sc.thread, 0);
			uint32_t* ver = g_fnGetVarPtr(&sc.thread, 0);
			uint32_t* code = g_fnGetVarPtr(&sc.thread, 0);
			if (id) *id = 0x53414D50u;
			if (ver) *ver = 210;
			if (code) *code = 210;
			return true;
		}
		case 0x0DDC: { // SET_MUTEX_VAR
			g_fnCollectParams(&sc.thread, 2);
			g_mutexVars[g_scriptParams[0]] = g_scriptParams[1];
			return true;
		}
		case 0x0DDD: { // GET_MUTEX_VAR
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			g_fnCollectParams(&sc.thread, 1);
			auto it = g_mutexVars.find(g_scriptParams[0]);
			if (outVar) *outVar = (it != g_mutexVars.end()) ? it->second : 0;
			return true;
		}
		case 0x0DDE: { // CALL_FUNCTION (consume varargs safely without crashing)
			g_fnCollectParams(&sc.thread, 2);
			uint8_t* end = sc.code.data() + sc.code.size();
			while (sc.thread.m_pPCPointer < end) {
				if (*sc.thread.m_pPCPointer == 0) {
					sc.thread.m_pPCPointer++;
					break;
				}
				std::string tag;
				if (!readStringParam(sc, tag)) break;
				tag = toLowerAscii(tag);
				if (tag == "i" || tag == "f") {
					g_fnCollectParams(&sc.thread, 1);
				} else if (tag == "ref" || tag == "resi" || tag == "resf") {
					uint32_t* v = g_fnGetVarPtr(&sc.thread, 0);
					if ((tag == "resi" || tag == "resf") && v) *v = 0;
				} else {
					break;
				}
			}
			return true;
		}
		case 0x0DE0: { // GET_TOUCH_POINT_STATE(outVar, zone, minTime)
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			g_fnCollectParams(&sc.thread, 2);
			if (outVar) *outVar = isZoneTouchedTimed(static_cast<int>(g_scriptParams[0]), g_scriptParams[1]) ? 1 : 0;
			return true;
		}
		case 0x0DE1: { // GET_TOUCH_SLIDE_STATE(outVar, from, to, minTime, maxTime)
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			g_fnCollectParams(&sc.thread, 4);
			if (outVar) {
				*outVar = checkSlideDone(static_cast<int>(g_scriptParams[0]),
				                         static_cast<int>(g_scriptParams[1]),
				                         g_scriptParams[2],
				                         g_scriptParams[3]) ? 1 : 0;
			}
			return true;
		}
		case 0x0DE2: { // GET_MENU_BUTTON_STATE
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			if (outVar) *outVar = g_menuBtnDown ? 1 : 0;
			return true;
		}
		case 0x0DE3: { // GET_MENU_BUTTON_PRESSED(outVar, minTime)
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			g_fnCollectParams(&sc.thread, 1);
			uint32_t t = nowMs();
			bool ok = g_menuBtnDown && (t >= g_menuBtnStart && (t - g_menuBtnStart) >= g_scriptParams[0]);
			if (outVar) *outVar = ok ? 1 : 0;
			return true;
		}
		case 0x0DE4: { // GET_PSP_CONTROL_STATE
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			g_fnCollectParams(&sc.thread, 1);
			if (outVar) *outVar = 0;
			return true;
		}
		case 0x0DE5: { // GET_PSP_CONTROL_PRESSED
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			g_fnCollectParams(&sc.thread, 2);
			if (outVar) *outVar = 0;
			return true;
		}
		case 0x0DEE: { // INVOKABLE_SCRIPT_STATS(count, pageCount)
			uint32_t invCount = 0;
			for (const auto& s : g_scripts) {
				if (s.invokable) ++invCount;
			}
			uint32_t* pCount = g_fnGetVarPtr(&sc.thread, 0);
			uint32_t* pPages = g_fnGetVarPtr(&sc.thread, 0);
			if (pCount) *pCount = invCount;
			if (pPages) *pPages = (invCount / 12) + 1;
			return true;
		}
		case 0x0DEF: { // START_INVOKABLE_SCRIPT(outStatus, id)
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			g_fnCollectParams(&sc.thread, 1);
			int32_t id = static_cast<int32_t>(g_scriptParams[0]);
			uint32_t status = static_cast<uint32_t>(-1);
			for (auto& s : g_scripts) {
				if (s.invokable && s.invokableId == id) {
					if (!s.startedOnce) {
						startOrRestartScript(s, false, 0);
						status = 0;
					} else if (!s.thread.m_bActive) {
						startOrRestartScript(s, true, 0);
						status = 1;
					} else {
						status = 2;
					}
					break;
				}
			}
			if (outVar) *outVar = status;
			return true;
		}
		case 0x0DF0: { // SHOW_MENU_OPEN_ARROW
			g_builtinMenu.showArrowHint = true;
			return true;
		}
		case 0x0DF1: { // HIDE_MENU_OPEN_ARROW
			g_builtinMenu.showArrowHint = false;
			return true;
		}
		case 0x0DF2: { // CREATE_MENU(addrHandle, itemCount)
			g_fnCollectParams(&sc.thread, 2);
			uint32_t addr = g_scriptParams[0];
			uint32_t itemCount = std::min<uint32_t>(g_scriptParams[1], 128);
			size_t bufSize = 0;
			const uint8_t* ptr = static_cast<const uint8_t*>(resolveHandleOrAddr(addr, false, &bufSize));
			if (ptr && (bufSize == 0 || bufSize >= 6)) {
				const size_t maxScan = (bufSize > 4) ? (bufSize - 4) : 4096;
				uint8_t flags = ptr[0];
				bool useGxt = (flags & 1) != 0;
				const char* p = reinterpret_cast<const char*>(ptr + 4);
				const char* end = p + maxScan;

				std::string title, closeLabel;
				if (!readBoundedCStr(p, end, title, 128)) {
					return true;
				}
				if (!readBoundedCStr(p, end, closeLabel, 64)) {
					return true;
				}

				std::string logTitle;
				size_t logCount = 0;
				{
					std::lock_guard<std::mutex> lock(g_touchMutex);
					g_scriptMenu = ScriptMenuState{};
					g_scriptMenu.active = true;
					g_scriptMenu.title = useGxt ? lookupGxtUtf8(title) : title;
					g_scriptMenu.closeLabel = closeLabel.empty() ? "CLOSE" : (useGxt ? lookupGxtUtf8(closeLabel) : closeLabel);
					g_scriptMenu.activeIndex = 0;
					g_scriptMenu.page = 0;
					g_scriptMenu.lastTouchedIndex = -1;
					g_scriptMenu.lastTouchedTime = 0;

					while (p < end && *p && itemCount > 0) {
						std::string raw;
						if (!readBoundedCStr(p, end, raw, 128)) break;
						g_scriptMenu.items.push_back(useGxt ? lookupGxtUtf8(raw) : raw);
						--itemCount;
					}
					g_builtinMenu.open = false;
					g_menuRowRects.clear();
					logTitle = g_scriptMenu.title;
					logCount = g_scriptMenu.items.size();
				}
				cleoLog("Script '%s' opened 0DF2 menu '%s' (%zu items)",
				        sc.fileName.c_str(), logTitle.c_str(), logCount);
			}
			return true;
		}
		case 0x0DF3: { // DELETE_MENU
			std::lock_guard<std::mutex> lock(g_touchMutex);
			g_scriptMenu.active = false;
			g_scriptMenu.items.clear();
			g_menuRowRects.clear();
			return true;
		}
		case 0x0DF4: { // GET_MENU_TOUCHED_ITEM_INDEX(outIdx, maxTimeMs)
			int32_t* outVar = reinterpret_cast<int32_t*>(g_fnGetVarPtr(&sc.thread, 0));
			g_fnCollectParams(&sc.thread, 1);
			uint32_t maxTime = g_scriptParams[0];
			int32_t res = -1;
			uint32_t t = nowMs();
			{
				std::lock_guard<std::mutex> lock(g_touchMutex);
				if (g_scriptMenu.lastTouchedIndex != -1 && t >= g_scriptMenu.lastTouchedTime &&
				    (maxTime == 0 || (t - g_scriptMenu.lastTouchedTime) <= maxTime)) {
					res = g_scriptMenu.lastTouchedIndex;
					g_scriptMenu.lastTouchedIndex = -1;
				}
			}
			if (outVar) *outVar = res;
			return true;
		}
		case 0x0DF5: { // SET_MENU_ACTIVE_ITEM_INDEX(idx)
			g_fnCollectParams(&sc.thread, 1);
			int32_t idx = static_cast<int32_t>(g_scriptParams[0]);
			std::lock_guard<std::mutex> lock(g_touchMutex);
			if (idx >= 0 && idx < static_cast<int32_t>(g_scriptMenu.items.size())) {
				g_scriptMenu.activeIndex = idx;
				g_scriptMenu.page = idx / kMenuItemsPerPage;
			}
			return true;
		}
		case 0x0DF6: { // GET_MENU_ACTIVE_ITEM_INDEX(outIdx)
			int32_t* outVar = reinterpret_cast<int32_t*>(g_fnGetVarPtr(&sc.thread, 0));
			std::lock_guard<std::mutex> lock(g_touchMutex);
			if (outVar) *outVar = g_scriptMenu.activeIndex;
			return true;
		}

		// -------- Popular PC CLEO4 Opcodes --------
		case 0x0A8E: // int add
		case 0x0A8F: // int sub
		case 0x0A90: // int mul
		case 0x0A91: { // int div
			g_fnCollectParams(&sc.thread, 2);
			int32_t a = static_cast<int32_t>(g_scriptParams[0]);
			int32_t b = static_cast<int32_t>(g_scriptParams[1]);
			int32_t r = 0;
			if (op == 0x0A8E) r = static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
			else if (op == 0x0A8F) r = static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
			else if (op == 0x0A90) r = static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
			else r = (b != 0 && !(a == INT32_MIN && b == -1)) ? (a / b) : 0;
			g_scriptParams[0] = static_cast<uint32_t>(r);
			g_fnStoreParams(&sc.thread, 1);
			return true;
		}
		case 0x0A99: { // SET_CURRENT_DIRECTORY(0=userdir/cleo, 1=root/files)
			g_fnCollectParams(&sc.thread, 1);
			g_currentDir = (g_scriptParams[0] == 1) ? g_gameDir : g_cleoDir;
			return true;
		}
		case 0x0A9A: { // OPENFILE(path, mode, outHandle)
			std::string path, mode;
			readStringParam(sc, path);
			readStringParam(sc, mode);
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			std::string resolved = resolveScriptPath(path);
			FILE* fp = (g_openFiles.size() < 64 && !resolved.empty() && !mode.empty()) ? fopen(resolved.c_str(), mode.c_str()) : nullptr;
			if (fp) {
				uint32_t fh = g_nextFileHandle++;
				g_openFiles[fh] = fp;
				if (outVar) *outVar = fh;
				setCond(true);
			} else {
				if (outVar) *outVar = 0;
				setCond(false);
			}
			return true;
		}
		case 0x0A9B: { // CLOSEFILE(handle)
			g_fnCollectParams(&sc.thread, 1);
			auto it = g_openFiles.find(g_scriptParams[0]);
			if (it != g_openFiles.end()) {
				if (it->second) fclose(it->second);
				g_openFiles.erase(it);
			}
			return true;
		}
		case 0x0A9C: { // GET_FILE_SIZE(handle, outSize)
			g_fnCollectParams(&sc.thread, 1);
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			uint32_t sz = 0;
			auto it = g_openFiles.find(g_scriptParams[0]);
			if (it != g_openFiles.end() && it->second) {
				long cur = ftell(it->second);
				fseek(it->second, 0, SEEK_END);
				long end = ftell(it->second);
				fseek(it->second, cur, SEEK_SET);
				if (end > 0) sz = static_cast<uint32_t>(end);
			}
			if (outVar) *outVar = sz;
			return true;
		}
		case 0x0A9D: { // READFILE(handle, size, outVar)
			g_fnCollectParams(&sc.thread, 2);
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			auto it = g_openFiles.find(g_scriptParams[0]);
			uint32_t sz = std::min<uint32_t>(g_scriptParams[1], 4);
			if (outVar) {
				*outVar = 0;
				if (it != g_openFiles.end() && it->second) {
					fread(outVar, 1, sz, it->second);
				}
			}
			return true;
		}
		case 0x0A9E: { // WRITEFILE(handle, size, val)
			g_fnCollectParams(&sc.thread, 3);
			auto it = g_openFiles.find(g_scriptParams[0]);
			uint32_t sz = std::min<uint32_t>(g_scriptParams[1], 4);
			uint32_t val = g_scriptParams[2];
			if (it != g_openFiles.end() && it->second) {
				fwrite(&val, 1, sz, it->second);
			}
			return true;
		}
		case 0x0AA0: { // GOSUB_IF_FALSE
			uint8_t* target = nullptr;
			if (readTargetLabelOffset(sc, target) && sc.thread.m_bCmpFlag == 0) {
				if (sc.thread.m_nStackDepth < 8) {
					sc.thread.m_pPCStack[sc.thread.m_nStackDepth++] = sc.thread.m_pPCPointer;
					sc.thread.m_pPCPointer = target;
				}
			}
			return true;
		}
		case 0x0AA1: { // RETURN_IF_FALSE
			if (sc.thread.m_bCmpFlag == 0 && sc.thread.m_nStackDepth > 0) {
				sc.thread.m_pPCPointer = sc.thread.m_pPCStack[--sc.thread.m_nStackDepth];
			}
			return true;
		}
		case 0x0AAB: { // FILE_EXISTS(path)
			std::string path;
			readStringParam(sc, path);
			struct stat st{};
			std::string resolved = resolveScriptPath(path);
			setCond(!resolved.empty() && stat(resolved.c_str(), &st) == 0 && !S_ISDIR(st.st_mode));
			return true;
		}
		case 0x0AB1: { // CALL_SCM_FUNC(label, argCount, args..., outVars...)
			uint8_t* target = nullptr;
			if (!readTargetLabelOffset(sc, target)) return true;
			g_fnCollectParams(&sc.thread, 1);
			int argCount = std::clamp<int>(static_cast<int>(g_scriptParams[0]), 0, 32);
			uint32_t args[32]{};
			if (argCount > 0) {
				g_fnCollectParams(&sc.thread, static_cast<int16_t>(argCount));
				memcpy(args, g_scriptParams, sizeof(uint32_t) * argCount);
			}
			if (static_cast<int>(sc.callFrames.size()) >= kMaxScmCallDepth) {
				cleoLog("Script '%s': 0AB1 call depth exceeded, terminating", sc.fileName.c_str());
				sc.thread.m_bActive = 0;
				return true;
			}
			ScmCallFrame frame{};
			frame.returnIp = sc.thread.m_pPCPointer; // points at return output vars (terminated by 0x00)
			memcpy(frame.savedLocals, sc.thread.m_nLocals, sizeof(frame.savedLocals));
			sc.callFrames.push_back(frame);

			memset(sc.thread.m_nLocals, 0, sizeof(uint32_t) * 32);
			for (int i = 0; i < argCount; ++i) {
				sc.thread.m_nLocals[i] = args[i];
			}
			sc.thread.m_pPCPointer = target;
			return true;
		}
		case 0x0AB2:   // RET(retCount, retVals...)
		case 0x0AF6:   // RET_IF_FALSE
		case 0x0AF7: { // RET_IF_TRUE
			g_fnCollectParams(&sc.thread, 1);
			int retCount = std::clamp<int>(static_cast<int>(g_scriptParams[0]), 0, 32);
			uint32_t rets[32]{};
			if (retCount > 0) {
				g_fnCollectParams(&sc.thread, static_cast<int16_t>(retCount));
				memcpy(rets, g_scriptParams, sizeof(uint32_t) * retCount);
			}
			if (sc.callFrames.empty()) {
				sc.thread.m_bActive = 0;
				return true;
			}
			ScmCallFrame frame = sc.callFrames.back();
			sc.callFrames.pop_back();
			memcpy(sc.thread.m_nLocals, frame.savedLocals, sizeof(frame.savedLocals));
			sc.thread.m_pPCPointer = frame.returnIp;
			uint8_t* codeEnd = sc.code.data() + sc.code.size();
			for (int i = 0; i < retCount; ++i) {
				if (sc.thread.m_pPCPointer >= codeEnd || *sc.thread.m_pPCPointer == 0) {
					break;
				}
				if (uint32_t* dst = g_fnGetVarPtr(&sc.thread, 0)) {
					*dst = rets[i];
				}
			}
			// Skip vararg terminator byte (0x00)
			if (sc.thread.m_pPCPointer < codeEnd && *sc.thread.m_pPCPointer == 0) {
				sc.thread.m_pPCPointer++;
			}
			if (op == 0x0AF6) setCond(false);
			else if (op == 0x0AF7) setCond(true);
			return true;
		}
		case 0x0AB3: { // SET_CLEO_SHARED_VAR(idx, val)
			g_fnCollectParams(&sc.thread, 2);
			uint32_t idx = g_scriptParams[0];
			if (idx < kMaxSharedVars) g_sharedVars[idx] = g_scriptParams[1];
			return true;
		}
		case 0x0AB4: { // GET_CLEO_SHARED_VAR(idx, outVal)
			g_fnCollectParams(&sc.thread, 1);
			uint32_t idx = g_scriptParams[0];
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			if (outVar) *outVar = (idx < kMaxSharedVars) ? g_sharedVars[idx] : 0;
			return true;
		}
		case 0x0ABA: { // TERMINATE_ALL_CUSTOM_SCRIPTS_WITH_THIS_NAME
			std::string name;
			readStringParam(sc, name);
			std::string target = toLowerAscii(name);
			for (auto& s : g_scripts) {
				if (toLowerAscii(s.thread.m_szScriptName) == target ||
				    toLowerAscii(s.displayName) == target ||
				    toLowerAscii(s.fileName) == target) {
					s.thread.m_bActive = 0;
				}
			}
			return true;
		}
		case 0x0AC8: { // ALLOCATE_MEMORY(size, outPtrHandle)
			g_fnCollectParams(&sc.thread, 1);
			uint32_t sz = std::clamp<uint32_t>(g_scriptParams[0], 1, 4 * 1024 * 1024);
			uint32_t* outVar = g_fnGetVarPtr(&sc.thread, 0);
			void* mem = calloc(1, sz);
			if (outVar) *outVar = mem ? registerHandle(mem, sz, true) : 0;
			setCond(mem != nullptr);
			return true;
		}
		case 0x0AC9: { // FREE_MEMORY(ptrHandle)
			g_fnCollectParams(&sc.thread, 1);
			auto it = g_ptrHandles.find(g_scriptParams[0]);
			if (it != g_ptrHandles.end() && it->second.owned) {
				free(it->second.ptr);
				g_ptrHandles.erase(it);
			}
			return true;
		}
		case 0x0ACA: { // SHOW_TEXT_BOX(text)
			std::string text;
			readStringParam(sc, text);
			if (g_fnSetHelpMsg) {
				g_fnSetHelpMsg(nullptr, makeTempGxt(text), true, false, false, 0);
			}
			return true;
		}
		case 0x0ACB: { // SHOW_STYLED_TEXT(text, timeMs, style)
			std::string text;
			readStringParam(sc, text);
			g_fnCollectParams(&sc.thread, 2);
			if (g_fnAddBigMessage) {
				g_fnAddBigMessage(makeTempGxt(text), g_scriptParams[0], static_cast<uint16_t>(g_scriptParams[1]));
			}
			return true;
		}
		case 0x0ACC: { // SHOW_TEXT_LOWPRIORITY(text, timeMs)
			std::string text;
			readStringParam(sc, text);
			g_fnCollectParams(&sc.thread, 1);
			if (g_fnAddMessage) {
				g_fnAddMessage(nullptr, makeTempGxt(text), g_scriptParams[0], 0, false);
			}
			return true;
		}
		case 0x0ACD: { // SHOW_TEXT_HIGHPRIORITY(text, timeMs)
			std::string text;
			readStringParam(sc, text);
			g_fnCollectParams(&sc.thread, 1);
			if (g_fnAddMessageJumpQ) {
				g_fnAddMessageJumpQ(nullptr, makeTempGxt(text), g_scriptParams[0], 0, false);
			}
			return true;
		}
		case 0x0AD3: { // STRING_FORMAT(outBuf, fmt, args...)
			// Destination can be a short/long string var or an allocated 0AC8 handle
			uint8_t* codeEnd = sc.code.data() + sc.code.size();
			char* dstBuf = nullptr;
			size_t dstCap = 0;
			if (sc.thread.m_pPCPointer < codeEnd) {
				uint8_t t = *sc.thread.m_pPCPointer;
				if (t == sa::SCRIPT_PARAM_GLOBAL_SHORT_STRING_VARIABLE || t == sa::SCRIPT_PARAM_LOCAL_SHORT_STRING_VARIABLE) {
					dstBuf = reinterpret_cast<char*>(g_fnGetVarPtr(&sc.thread, 0));
					dstCap = 8;
				} else if (t == sa::SCRIPT_PARAM_GLOBAL_LONG_STRING_VARIABLE || t == sa::SCRIPT_PARAM_LOCAL_LONG_STRING_VARIABLE) {
					dstBuf = reinterpret_cast<char*>(g_fnGetVarPtr(&sc.thread, 0));
					dstCap = 16;
				} else {
					g_fnCollectParams(&sc.thread, 1);
					auto it = g_ptrHandles.find(g_scriptParams[0]);
					if (it != g_ptrHandles.end() && it->second.owned && it->second.ptr && it->second.size > 0) {
						dstBuf = static_cast<char*>(it->second.ptr);
						dstCap = it->second.size;
					}
				}
			}
			std::string fmt;
			readStringParam(sc, fmt);

			// Collect varargs up to 0x00 terminator
			struct FmtArg {
				bool isStr = false;
				uint32_t val = 0;
				std::string str;
			};
			std::vector<FmtArg> args;
			int drained = 0;
			while (sc.thread.m_pPCPointer < codeEnd && drained < 64) {
				uint8_t pt = *sc.thread.m_pPCPointer;
				if (pt == 0) {
					sc.thread.m_pPCPointer++;
					break;
				}
				FmtArg a{};
				if (pt == sa::SCRIPT_PARAM_STATIC_SHORT_STRING ||
				    pt == sa::SCRIPT_PARAM_STATIC_PASCAL_STRING ||
				    pt == sa::SCRIPT_PARAM_STATIC_LONG_STRING ||
				    pt == sa::SCRIPT_PARAM_GLOBAL_SHORT_STRING_VARIABLE ||
				    pt == sa::SCRIPT_PARAM_LOCAL_SHORT_STRING_VARIABLE ||
				    pt == sa::SCRIPT_PARAM_GLOBAL_LONG_STRING_VARIABLE ||
				    pt == sa::SCRIPT_PARAM_LOCAL_LONG_STRING_VARIABLE) {
					a.isStr = true;
					readStringParam(sc, a.str);
				} else {
					g_fnCollectParams(&sc.thread, 1);
					a.val = g_scriptParams[0];
				}
				if (args.size() < 16) {
					args.push_back(std::move(a));
				}
				++drained;
			}

			// Format safely (no unchecked printf with user format string)
			std::string formatted;
			size_t argIdx = 0;
			for (size_t i = 0; i < fmt.size(); ++i) {
				if (fmt[i] != '%' || i + 1 >= fmt.size()) {
					formatted.push_back(fmt[i]);
					continue;
				}
				if (fmt[i + 1] == '%') {
					formatted.push_back('%');
					++i;
					continue;
				}
				size_t specStart = i;
				++i;
				int dotCount = 0;
				while (i < fmt.size() && (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0' ||
				       (fmt[i] >= '0' && fmt[i] <= '9') || fmt[i] == '.')) {
					if (fmt[i] == '.') ++dotCount;
					++i;
				}
				if (i >= fmt.size()) break;
				char spec = fmt[i];
				std::string specStr = fmt.substr(specStart, i - specStart + 1);
				if (specStr.size() > 16 || dotCount > 1) {
					formatted += specStr;
					continue;
				}
				char tmp[128]{};
				if (argIdx < args.size()) {
					const FmtArg& a = args[argIdx++];
					if (spec == 's') {
						if (a.isStr) {
							formatted += a.str;
						} else {
							size_t sBufSize = 0;
							if (void* p = resolveHandleOrAddr(a.val, false, &sBufSize)) {
								const char* cs = static_cast<const char*>(p);
								const size_t maxS = (sBufSize > 0 && sBufSize < 256) ? sBufSize : 256;
								std::string safeS;
								readBoundedCStr(cs, cs + maxS, safeS, maxS);
								formatted += safeS;
							}
						}
					} else if (spec == 'f' || spec == 'F' || spec == 'g' || spec == 'G' || spec == 'e' || spec == 'E') {
						float fv = 0.0f;
						memcpy(&fv, &a.val, 4);
						snprintf(tmp, sizeof(tmp), specStr.c_str(), static_cast<double>(fv));
						formatted += tmp;
					} else if (spec == 'd' || spec == 'i') {
						snprintf(tmp, sizeof(tmp), specStr.c_str(), static_cast<int32_t>(a.val));
						formatted += tmp;
					} else if (spec == 'u' || spec == 'x' || spec == 'X' || spec == 'o') {
						snprintf(tmp, sizeof(tmp), specStr.c_str(), a.val);
						formatted += tmp;
					} else if (spec == 'c') {
						formatted.push_back(static_cast<char>(a.val & 0xFF));
					} else {
						formatted += specStr;
					}
				}
			}

			if (dstBuf && dstCap > 0) {
				size_t copyLen = std::min(formatted.size(), dstCap - 1);
				memcpy(dstBuf, formatted.data(), copyLen);
				dstBuf[copyLen] = '\0';
			}
			return true;
		}
		case 0x0ADF: { // ADD_DYNAMIC_GXT_ENTRY(key, text)
			std::string key, text;
			readStringParam(sc, key);
			readStringParam(sc, text);
			addCustomGxtEntry(key, text);
			return true;
		}
		case 0x0AE0: { // REMOVE_DYNAMIC_GXT_ENTRY(key)
			std::string key;
			readStringParam(sc, key);
			removeCustomGxtEntry(key);
			return true;
		}
		case 0x0AEE: { // POW(base, exp, out)
			g_fnCollectParams(&sc.thread, 2);
			float a = 0.0f, b = 0.0f;
			memcpy(&a, &g_scriptParams[0], 4);
			memcpy(&b, &g_scriptParams[1], 4);
			float r = std::pow(a, b);
			memcpy(&g_scriptParams[0], &r, 4);
			g_fnStoreParams(&sc.thread, 1);
			return true;
		}
		case 0x0AEF: { // LOG(val, base, out)
			g_fnCollectParams(&sc.thread, 2);
			float a = 0.0f, b = 0.0f;
			memcpy(&a, &g_scriptParams[0], 4);
			memcpy(&b, &g_scriptParams[1], 4);
			float r = (a > 0.0f && b > 0.0f && b != 1.0f) ? (std::log(a) / std::log(b)) : 0.0f;
			memcpy(&g_scriptParams[0], &r, 4);
			g_fnStoreParams(&sc.thread, 1);
			return true;
		}
		case 0x0AFF: { // SET_COMPARE_FLAG(flag)
			g_fnCollectParams(&sc.thread, 1);
			setCond(g_scriptParams[0] != 0);
			return true;
		}
		case 0x0CB2: { // ATOF(str, outFloat)
			std::string s;
			readStringParam(sc, s);
			float f = static_cast<float>(atof(s.c_str()));
			memcpy(&g_scriptParams[0], &f, 4);
			g_fnStoreParams(&sc.thread, 1);
			return true;
		}
		case 0x0CB3: { // ATOI(str, outInt)
			std::string s;
			readStringParam(sc, s);
			int32_t v = atoi(s.c_str());
			g_scriptParams[0] = static_cast<uint32_t>(v);
			g_fnStoreParams(&sc.thread, 1);
			return true;
		}
		case 0x0CB4: { // GET_SCREEN_RES(outW, outH)
			float w = 1920.0f, h = 1080.0f;
			{
				std::lock_guard<std::mutex> lock(g_touchMutex);
				w = g_screenW;
				h = g_screenH;
			}
			g_scriptParams[0] = static_cast<uint32_t>(w);
			g_scriptParams[1] = static_cast<uint32_t>(h);
			g_fnStoreParams(&sc.thread, 2);
			return true;
		}
		case 0x0CB5: { // IS_ANY_FINGER_ONSCREEN
			setCond(isAnyFingerDown());
			return true;
		}
		default:
			return false;
	}
}

} // namespace cleo_internal
