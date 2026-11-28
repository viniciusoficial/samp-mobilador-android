#include "../Addr.h"
#include "../Client.h"
#include "../InputArbiter.h"
#include "../Game/Engine/PatchRegistry.h"
#include "../Game/Engine/Symbols.h"
#include "MultiTouch.h"
#include "NV_Event.h"

#include <Memory.h>
#include <spdlog/spdlog.h>
#include <android/input.h>
#include <unistd.h>


/* MultiTouch */

using FnAndTouchEvent = void (*)(int type, int num, int posX, int posY);
static FnAndTouchEvent g_fnAndTouchEvent = nullptr;

extern void AND_TouchEvent_hook(int type, int num, int posX, int posY);

void touch_event(int type, int num, int x, int y)
{
	// While a modal overlay (Java Dialog/Keyboard, ImGui Keyboard, Scoreboard) is active,
	// dispatch directly to our hook so third-party AML touch overlays cannot steal modal taps.
	if (!InputArbiter::shouldBypassExternalTouch() && g_fnAndTouchEvent != nullptr) {
		g_fnAndTouchEvent(type, num, x, y);
		return;
	}

	AND_TouchEvent_hook(type, num, x, y);
}

static constexpr uint32_t kMultiTouchPresentMaskShift = 16;
static constexpr uint32_t kMultiTouchPresentMaskBits  = 0x0Fu << kMultiTouchPresentMaskShift;
static constexpr uint32_t kMultiTouchPrimaryBit       = 1u << 20;

static void dispatchMultiTouchEvent(const NVEvent& event)
{
	static bool s_pointerDown[4] = {false, false, false, false};
	static int s_lastX[4] = {0, 0, 0, 0};
	static int s_lastY[4] = {0, 0, 0, 0};

	const uint32_t rawAction = static_cast<uint32_t>(event.m_data.m_multi.m_action);
	const int type = static_cast<int>(rawAction & NV_MULTITOUCH_ACTION_MASK);
	const int num = static_cast<int>((rawAction & NV_MULTITOUCH_POINTER_MASK) >> NV_MULTITOUCH_POINTER_SHIFT);
	const uint32_t presentMask = (rawAction & kMultiTouchPresentMaskBits) >> kMultiTouchPresentMaskShift;
	const bool isPrimary = (rawAction & kMultiTouchPrimaryBit) != 0;

	const int xs[4] = {
		static_cast<int>(event.m_data.m_multi.m_x1),
		static_cast<int>(event.m_data.m_multi.m_x2),
		static_cast<int>(event.m_data.m_multi.m_x3),
		static_cast<int>(event.m_data.m_multi.m_x4)
	};
	const int ys[4] = {
		static_cast<int>(event.m_data.m_multi.m_y1),
		static_cast<int>(event.m_data.m_multi.m_y2),
		static_cast<int>(event.m_data.m_multi.m_y3),
		static_cast<int>(event.m_data.m_multi.m_y4)
	};

	// ACTION_CANCEL (e.g. MIUI 3-finger screenshot gesture, palm rejection,
	// notification shade, focus loss) cancels ALL active pointers at once.
	// Previously only pointer 0 got UP while pointers 1..3 got MOVE, leaving
	// phantom fingers permanently held down in CTouchInterface and forcing
	// single-finger camera drags into two-finger pinch zoom.
	if (type == NV_MULTITOUCH_CANCEL) {
		InputArbiter::onTouchCancel();
		for (int i = 0; i < 4; ++i) {
			if (s_pointerDown[i]) {
				const bool hasCoord = (presentMask & (1u << i)) != 0;
				touch_event(NV_MULTITOUCH_UP, i, hasCoord ? xs[i] : s_lastX[i], hasCoord ? ys[i] : s_lastY[i]);
				s_pointerDown[i] = false;
			}
		}
		return;
	}

	if (type == NV_MULTITOUCH_DOWN) {
		// Primary ACTION_DOWN starts a brand-new gesture; flush any stale fingers.
		if (isPrimary) {
			for (int i = 0; i < 4; ++i) {
				if (i != num && s_pointerDown[i]) {
					touch_event(NV_MULTITOUCH_UP, i, s_lastX[i], s_lastY[i]);
					s_pointerDown[i] = false;
				}
			}
		}
		// Self-heal any pointer absent from the active MotionEvent snapshot.
		for (int i = 0; i < 4; ++i) {
			if (i != num && s_pointerDown[i] && !(presentMask & (1u << i))) {
				touch_event(NV_MULTITOUCH_UP, i, s_lastX[i], s_lastY[i]);
				s_pointerDown[i] = false;
			}
		}
		if (num >= 0 && num < 4) {
			if (s_pointerDown[num]) {
				touch_event(NV_MULTITOUCH_UP, num, s_lastX[num], s_lastY[num]);
			}
			touch_event(NV_MULTITOUCH_DOWN, num, xs[num], ys[num]);
			s_pointerDown[num] = true;
			s_lastX[num] = xs[num];
			s_lastY[num] = ys[num];
		}
		for (int i = 0; i < 4; ++i) {
			if (i == num) continue;
			if (presentMask & (1u << i)) {
				if (s_pointerDown[i]) {
					touch_event(NV_MULTITOUCH_MOVE, i, xs[i], ys[i]);
				}
				else {
					touch_event(NV_MULTITOUCH_DOWN, i, xs[i], ys[i]);
					s_pointerDown[i] = true;
				}
				s_lastX[i] = xs[i];
				s_lastY[i] = ys[i];
			}
		}
		return;
	}

	if (type == NV_MULTITOUCH_UP) {
		if (num >= 0 && num < 4) {
			if (s_pointerDown[num] || (presentMask & (1u << num))) {
				touch_event(NV_MULTITOUCH_UP, num, xs[num], ys[num]);
			}
			s_pointerDown[num] = false;
		}
		if (isPrimary) {
			// Primary ACTION_UP ends the entire gesture: ensure zero stuck fingers.
			for (int i = 0; i < 4; ++i) {
				if (s_pointerDown[i]) {
					touch_event(NV_MULTITOUCH_UP, i, s_lastX[i], s_lastY[i]);
					s_pointerDown[i] = false;
				}
			}
		}
		else {
			for (int i = 0; i < 4; ++i) {
				if (i == num) continue;
				if (presentMask & (1u << i)) {
					if (s_pointerDown[i]) {
						touch_event(NV_MULTITOUCH_MOVE, i, xs[i], ys[i]);
						s_lastX[i] = xs[i];
						s_lastY[i] = ys[i];
					}
				}
				else if (s_pointerDown[i]) {
					touch_event(NV_MULTITOUCH_UP, i, s_lastX[i], s_lastY[i]);
					s_pointerDown[i] = false;
				}
			}
		}
		return;
	}

	// NV_MULTITOUCH_MOVE: update all present pointers and self-heal any missing ones.
	for (int i = 0; i < 4; ++i) {
		if (presentMask & (1u << i)) {
			if (!s_pointerDown[i]) {
				touch_event(NV_MULTITOUCH_DOWN, i, xs[i], ys[i]);
				s_pointerDown[i] = true;
			}
			else {
				touch_event(NV_MULTITOUCH_MOVE, i, xs[i], ys[i]);
			}
			s_lastX[i] = xs[i];
			s_lastY[i] = ys[i];
		}
		else if (s_pointerDown[i]) {
			touch_event(NV_MULTITOUCH_UP, i, s_lastX[i], s_lastY[i]);
			s_pointerDown[i] = false;
		}
	}
}

DECL_HOOK(int32_t, NVEventGetNextEvent, NVEvent* ev, int waitMSecs)
{
	if (!ev) {
		spdlog::warn("NVEvent: %d", waitMSecs);
	}
	static volatile pid_t nvEventTid = 0;
	if (!nvEventTid) nvEventTid = gettid();

	int32_t ret = NVEventGetNextEvent(ev, waitMSecs);

	if (ret && ev) {
		// Suppress both legacy single-touch and 2-finger multitouch NVEvents;
		// all touch input is routed exclusively through multiTouchEvent4Ex().
		if (ev->m_type == NV_EVENT_MULTITOUCH || ev->m_type == NV_EVENT_TOUCH) {
			ev->m_type = (NVEventType) 228;
		}
		else {
			// Delivered event visibility for boot-stall diagnosis
			// (1=KEY 2=CHAR 3=TOUCH 4=MULTITOUCH 5=ACCEL 6=WIN_SIZE 7=QUIT 8=PAUSE 9=RESUME)
			static int logged = 0;
			if (logged < 8) {
				++logged;
				spdlog::info("nv_event: delivered type={} waitMSecs={} tid={}", (int) ev->m_type, waitMSecs, (int) nvEventTid);
			}
		}
	}

	// Drain queued Java multi-touch events (including synthetic ACTION_CANCEL on
	// focus loss/pause that has no matching native NV_EVENT_MULTITOUCH).
	for (int drained = 0; drained < 32; ++drained) {
		NVEvent event;
		NV_Event::GetNextEvent(&event);
		if (event.m_type != NV_EVENT_MULTITOUCH) {
			break;
		}
		dispatchMultiTouchEvent(event);
	}

	return ret;
}

int g_points[1000];
int g_pointers[1000];

void MultiTouch::initialize()
{
	spdlog::info("Initializing multi touch..");

	// Points
	memset(g_points, 0, 999 * sizeof(int));
	Memory::protectAddr(g_saSym->Abs(addr::GOT_MultiTouch_points));
	*g_saSym->Abs<int**>(addr::GOT_MultiTouch_points) = &g_points[0];
	patches::registerData("MultiTouch.points", "Relocate NVEvent touch points array", patches::Scope::Boot, g_saSym->Abs<uintptr_t>(addr::GOT_MultiTouch_points));

	// pointers
	memset(g_pointers, 0, 999 * sizeof(int));
	Memory::protectAddr(g_saSym->Abs(addr::GOT_MultiTouch_pointers));
	*g_saSym->Abs<int**>(addr::GOT_MultiTouch_pointers) = &g_pointers[0];
	patches::registerData("MultiTouch.pointers", "Relocate NVEvent touch pointers array", patches::Scope::Boot, g_saSym->Abs<uintptr_t>(addr::GOT_MultiTouch_pointers));

#if VER_x32
	patches::updateBytes("MultiTouch.OS_PointerGetNumber", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::OS_PointerGetNumber_patch), "\x04\x20", 2, patches::Scope::Boot, false);
	patches::updateBytes("MultiTouch.OS_PointerGetType", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::OS_PointerGetType_patch), "\x04\x28", 2, patches::Scope::Boot, false);
	patches::updateBytes("MultiTouch.LIB_PointerGetCoordinates", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::LIB_PointerGetCoordinates_patch), "\x03\x28", 2, patches::Scope::Boot, false);
	patches::updateBytes("MultiTouch.LIB_PointerGetWheel", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::LIB_PointerGetWheel_patch), "\x03\x28", 2, patches::Scope::Boot, false);
	patches::updateBytes("MultiTouch.LIB_PointerDoubleClicked", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::LIB_PointerDoubleClicked_patch), "\x03\x28", 2, patches::Scope::Boot, false);
	patches::updateBytes("MultiTouch.LIB_PointerGetButton", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::LIB_PointerGetButton_patch), "\x03\x2A", 2, patches::Scope::Boot, false);
#else
	patches::updateBytes("MultiTouch.OS_PointerGetNumber", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::OS_PointerGetNumber_patch), "\x80\x00\x80\x52", 4, patches::Scope::Boot, false);
	patches::updateBytes("MultiTouch.OS_PointerGetType", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::OS_PointerGetType_patch), "\x1F\x10\x00\x71", 4, patches::Scope::Boot, false);
	patches::updateBytes("MultiTouch.LIB_PointerGetCoordinates", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::LIB_PointerGetCoordinates_patch), "\x1F\x0C\x00\x71", 4, patches::Scope::Boot, false);
	patches::updateBytes("MultiTouch.LIB_PointerGetWheel", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::LIB_PointerGetWheel_patch), "\x1F\x0C\x00\x71", 4, patches::Scope::Boot, false);
	patches::updateBytes("MultiTouch.LIB_PointerDoubleClicked", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::LIB_PointerDoubleClicked_patch), "\x1F\x0C\x00\x71", 4, patches::Scope::Boot, false);
	patches::updateBytes("MultiTouch.LIB_PointerGetButton", "Expand touch pointer cap to 4", g_saSym->Abs<uintptr_t>(addr::LIB_PointerGetButton_patch), "\x1F\x0D\x00\x71", 4, patches::Scope::Boot, false);
#endif

	// Resolve live _Z14AND_TouchEventiiii entry point so touches flow through
	// AML plugins hooked after Hooks::install() (e.g. libFPP64.so) and chain into AND_TouchEvent_hook.
	g_fnAndTouchEvent = eng::as<FnAndTouchEvent>(eng::Sym::AND_TouchEvent);

	// NVEventGetNextEvent
	REG_HOOK(Boot, NVEventGetNextEvent, NVEventGetNextEvent, "Multi-touch NVEvent queue intercept");
}

extern "C"
{
JNIEXPORT jboolean JNICALL Java_com_sampmobile_game_SAMP_multiTouchEvent4Ex(JNIEnv* env, jobject obj, jint action,
		jint pointer, jint x1, jint y1, jint x2, jint y2, jint x3, jint y3, jint x4, jint y4)
{
	(void) env;
	(void) obj;

	NVEvent ev;
	memset(&ev, 0, sizeof(ev));

	ev.m_type = NV_EVENT_MULTITOUCH;

	uint32_t actionType = NV_MULTITOUCH_MOVE;
	uint32_t extraFlags = 0;

	switch (action) {
		case AMOTION_EVENT_ACTION_UP:
			actionType = NV_MULTITOUCH_UP;
			extraFlags |= kMultiTouchPrimaryBit;
			break;
		case AMOTION_EVENT_ACTION_DOWN:
			actionType = NV_MULTITOUCH_DOWN;
			extraFlags |= kMultiTouchPrimaryBit;
			break;
		case AMOTION_EVENT_ACTION_POINTER_DOWN:
			actionType = NV_MULTITOUCH_DOWN;
			break;
		case AMOTION_EVENT_ACTION_POINTER_UP:
			actionType = NV_MULTITOUCH_UP;
			break;
		case AMOTION_EVENT_ACTION_CANCEL:
			actionType = NV_MULTITOUCH_CANCEL;
			break;
		default:
			actionType = NV_MULTITOUCH_MOVE;
			break;
	}

	const uint32_t actionPtr = static_cast<uint32_t>(pointer) & 0xFFu;
	const uint32_t presentMask = (static_cast<uint32_t>(pointer) >> 8) & 0x0Fu;

	ev.m_data.m_multi.m_action = static_cast<NVMultiTouchEventType>(
		actionType
		| (actionPtr << NV_MULTITOUCH_POINTER_SHIFT)
		| (presentMask << kMultiTouchPresentMaskShift)
		| extraFlags);

	ev.m_data.m_multi.m_x1 = x1;
	ev.m_data.m_multi.m_y1 = y1;

	ev.m_data.m_multi.m_x2 = x2;
	ev.m_data.m_multi.m_y2 = y2;

	ev.m_data.m_multi.m_x3 = x3;
	ev.m_data.m_multi.m_y3 = y3;

	ev.m_data.m_multi.m_x4 = x4;
	ev.m_data.m_multi.m_y4 = y4;

	NV_Event::InsertNewest(&ev);

	return JNI_TRUE;
}
}
