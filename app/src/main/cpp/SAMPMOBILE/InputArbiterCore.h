#pragma once

#include <cstdint>

namespace input_arbiter {

// Unified overlay / modal state bitmask shared across Android UI thread, GameMain thread,
// and NVEvent touch thread. Java owns kJavaOverlayMask; GameMain owns kGameMainOverlayMask.
enum OverlayFlags : uint32_t {
	kOverlayNone           = 0u,

	// Java-owned layers (updated atomically from Android UI thread)
	kOverlayJavaDialog     = 1u << 0,
	kOverlayJavaKeyboard   = 1u << 1,

	// GameMain-owned layers (published atomically each frame)
	kOverlayImGuiKeyboard  = 1u << 2,
	kOverlayScoreboard     = 1u << 3,
	kOverlayCleoMenu       = 1u << 4,
	kOverlayServerMenu     = 1u << 5,
	kOverlayEditObject     = 1u << 6,
	kOverlayObjectSelect   = 1u << 7,
	kOverlayTextDrawSelect = 1u << 8,
};

inline constexpr uint32_t kJavaOverlayMask =
		kOverlayJavaDialog | kOverlayJavaKeyboard;

inline constexpr uint32_t kGameMainOverlayMask =
		kOverlayImGuiKeyboard
		| kOverlayScoreboard
		| kOverlayCleoMenu
		| kOverlayServerMenu
		| kOverlayEditObject
		| kOverlayObjectSelect
		| kOverlayTextDrawSelect;

// Layers that own full-screen modal touch focus and must block third-party AML overlays
// as well as GTA camera/touch input (releasing all held GTA pointer slots).
inline constexpr uint32_t kModalTouchBlockMask =
		kOverlayJavaDialog
		| kOverlayJavaKeyboard
		| kOverlayImGuiKeyboard
		| kOverlayScoreboard;

// Native layers that can be dismissed by Android Back / ESC key.
inline constexpr uint32_t kNativeBackTargetMask =
		kOverlayImGuiKeyboard
		| kOverlayScoreboard
		| kOverlayCleoMenu
		| kOverlayServerMenu
		| kOverlayEditObject
		| kOverlayObjectSelect
		| kOverlayTextDrawSelect;

inline constexpr int64_t kKeyboardRadarGraceMs = 250;

enum class NativeBackTarget : uint8_t {
	None = 0,
	ImGuiKeyboard,
	Scoreboard,
	CleoMenu,
	ServerMenu,
	EditObject,
	ObjectSelect,
	TextDrawSelect,
};

constexpr bool shouldBypassExternalTouch(uint32_t flags)
{
	return (flags & kModalTouchBlockMask) != 0u;
}

constexpr bool isModalBlockingGtaTouch(uint32_t flags)
{
	return (flags & kModalTouchBlockMask) != 0u;
}

constexpr bool hasNativeBackTarget(uint32_t flags)
{
	return (flags & kNativeBackTargetMask) != 0u;
}

constexpr bool isRadarSuppressed(uint32_t flags, int64_t nowMs, int64_t javaKeyboardClosedMs)
{
	if ((flags & (kOverlayJavaKeyboard | kOverlayImGuiKeyboard)) != 0u) {
		return true;
	}
	if (javaKeyboardClosedMs > 0) {
		const int64_t delta = nowMs - javaKeyboardClosedMs;
		if (delta >= 0 && delta < kKeyboardRadarGraceMs) {
			return true;
		}
	}
	return false;
}

// Single authoritative dismissal priority order for Back / ESC on the native side.
constexpr NativeBackTarget resolveTopBackTarget(uint32_t flags)
{
	if ((flags & kOverlayImGuiKeyboard) != 0u) return NativeBackTarget::ImGuiKeyboard;
	if ((flags & kOverlayScoreboard) != 0u)    return NativeBackTarget::Scoreboard;
	if ((flags & kOverlayCleoMenu) != 0u)      return NativeBackTarget::CleoMenu;
	if ((flags & kOverlayServerMenu) != 0u)    return NativeBackTarget::ServerMenu;
	if ((flags & kOverlayEditObject) != 0u)    return NativeBackTarget::EditObject;
	if ((flags & kOverlayObjectSelect) != 0u)  return NativeBackTarget::ObjectSelect;
	if ((flags & kOverlayTextDrawSelect) != 0u) return NativeBackTarget::TextDrawSelect;
	return NativeBackTarget::None;
}

// Tracks which pointer slots (0..3) are currently pressed inside libGTASA.so so intercepted
// touches release ONLY the intercepted finger (preserving finger 0's joystick when finger 1
// taps an overlay), while modal overlays release all active fingers to prevent stuck pinch-zoom.
class GtaPointerTracker {
public:
	static constexpr int kMaxPointers = 4;
	static constexpr int kTouchUp = 1;
	static constexpr int kTouchDown = 2;

	template <typename EmitFn>
	void forward(int type, int num, int posX, int posY, EmitFn&& emit)
	{
		if (num >= 0 && num < kMaxPointers) {
			m_lastX[num] = posX;
			m_lastY[num] = posY;
			if (type == kTouchDown) {
				m_down[num] = true;
			}
			else if (type == kTouchUp) {
				m_down[num] = false;
			}
		}
		emit(type, num, posX, posY);
	}

	template <typename EmitFn>
	void releasePointer(int num, EmitFn&& emit)
	{
		if (num >= 0 && num < kMaxPointers && m_down[num]) {
			m_down[num] = false;
			// Emit the finger's last known coordinates rather than (0, 0) so
			// CTouchInterface::m_vecCachedPos[num] never snaps into the top-left
			// joystick region (WIDGET_PED_MOVE) on synthetic release.
			emit(kTouchUp, num, m_lastX[num], m_lastY[num]);
		}
	}

	template <typename EmitFn>
	void releaseAll(EmitFn&& emit)
	{
		for (int i = 0; i < kMaxPointers; ++i) {
			if (m_down[i]) {
				m_down[i] = false;
				emit(kTouchUp, i, m_lastX[i], m_lastY[i]);
			}
		}
	}

	void reset()
	{
		for (int i = 0; i < kMaxPointers; ++i) {
			m_down[i] = false;
			m_lastX[i] = 0;
			m_lastY[i] = 0;
		}
	}

	[[nodiscard]] bool isDown(int num) const
	{
		return (num >= 0 && num < kMaxPointers) ? m_down[num] : false;
	}

	[[nodiscard]] int activeCount() const
	{
		int n = 0;
		for (bool d : m_down) {
			if (d) ++n;
		}
		return n;
	}

private:
	bool m_down[kMaxPointers] = {false, false, false, false};
	int m_lastX[kMaxPointers] = {0, 0, 0, 0};
	int m_lastY[kMaxPointers] = {0, 0, 0, 0};
};

} // namespace input_arbiter
