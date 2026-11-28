#include "InputArbiter.h"
#include "InputArbiterCore.h"

#include "Client.h"
#include "Game/CleoEngine.h"
#include "Game/Game.h"
#include "Java/EditObject.h"
#include "Net/MenuPool.h"
#include "Net/NetGame.h"
#include "Net/ObjectPool.h"
#include "Net/TextDrawPool.h"
#include "UI/UI.h"
#include "UI/SAMPWidgets/ButtonPanel.h"
#include "UI/SAMPWidgets/Keyboard.h"
#include "UI/SAMPWidgets/Scoreboard.h"

#include <atomic>
#include <chrono>

namespace {

std::atomic<uint32_t> g_overlayFlags{input_arbiter::kOverlayNone};
std::atomic<int64_t> g_javaKeyboardClosedMs{0};
std::atomic<bool> g_pendingBackDismiss{false};
std::atomic<InputArbiter::ExternalTouchFilterFn> g_externalTouchFilter{nullptr};
input_arbiter::GtaPointerTracker g_gtaPointers;

int64_t nowSteadyMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
}

void updateOverlayBit(uint32_t bit, bool active)
{
	if (active) {
		g_overlayFlags.fetch_or(bit, std::memory_order_acq_rel);
	}
	else {
		g_overlayFlags.fetch_and(~bit, std::memory_order_acq_rel);
	}
}

uint32_t computeGameMainOverlayBits()
{
	uint32_t bits = input_arbiter::kOverlayNone;

	if (pUI) {
		if (pUI->keyboard() && pUI->keyboard()->visible()) {
			bits |= input_arbiter::kOverlayImGuiKeyboard;
		}
		if (pUI->scoreboard() && pUI->scoreboard()->visible()) {
			bits |= input_arbiter::kOverlayScoreboard;
		}
	}
	if (CleoEngine::isMenuOpen()) {
		bits |= input_arbiter::kOverlayCleoMenu;
	}
	if (EditObject::m_visible.load(std::memory_order_acquire)) {
		bits |= input_arbiter::kOverlayEditObject;
	}
	if (pNetGame) {
		if (CMenuPool* pMenuPool = pNetGame->GetMenuPool(); pMenuPool && pMenuPool->IsVisible()) {
			bits |= input_arbiter::kOverlayServerMenu;
		}
		if (CObjectPool* pObjPool = pNetGame->GetObjectPool(); pObjPool && pObjPool->IsSelectState()) {
			bits |= input_arbiter::kOverlayObjectSelect;
		}
		if (CTextDrawPool* pTdPool = pNetGame->GetTextDrawPool(); pTdPool && pTdPool->GetState()) {
			bits |= input_arbiter::kOverlayTextDrawSelect;
		}
	}

	return bits;
}

} // namespace

extern "C" __attribute__((visibility("default")))
void SAMPMOBILE_RegisterTouchFilter(InputArbiter::ExternalTouchFilterFn fn)
{
	InputArbiter::registerExternalTouchFilter(fn);
}

namespace InputArbiter {

void setJavaDialogVisible(bool visible)
{
	updateOverlayBit(input_arbiter::kOverlayJavaDialog, visible);
}

void setJavaKeyboardVisible(bool visible)
{
	const uint32_t prev = visible
			? g_overlayFlags.fetch_or(input_arbiter::kOverlayJavaKeyboard, std::memory_order_acq_rel)
			: g_overlayFlags.fetch_and(~input_arbiter::kOverlayJavaKeyboard, std::memory_order_acq_rel);
	const bool wasVisible = (prev & input_arbiter::kOverlayJavaKeyboard) != 0u;
	if (wasVisible && !visible) {
		g_javaKeyboardClosedMs.store(nowSteadyMs(), std::memory_order_release);
	}
}

bool requestNativeBackDismiss()
{
	// Android UI thread: read only the atomically published overlay mask (zero pool derefs).
	const uint32_t flags = g_overlayFlags.load(std::memory_order_acquire);
	if (!input_arbiter::hasNativeBackTarget(flags)) {
		return false;
	}
	g_pendingBackDismiss.store(true, std::memory_order_release);
	return true;
}

void publishGameMainState()
{
	const uint32_t gameBits = computeGameMainOverlayBits();
	if (pGame) {
		pGame->SetHudHidden(Game::HUD_HIDE_SCOREBOARD, (gameBits & input_arbiter::kOverlayScoreboard) != 0u);
	}

	uint32_t cur = g_overlayFlags.load(std::memory_order_relaxed);
	while (true) {
		const uint32_t next = (cur & input_arbiter::kJavaOverlayMask) | (gameBits & input_arbiter::kGameMainOverlayMask);
		if (g_overlayFlags.compare_exchange_weak(cur, next, std::memory_order_acq_rel, std::memory_order_relaxed)) {
			break;
		}
	}
}

void processPendingBackDismiss()
{
	if (g_pendingBackDismiss.exchange(false, std::memory_order_acq_rel)) {
		dismissTopNativeModal();
	}
}

void dismissTopNativeModal()
{
	// Recompute live GameMain flags so back/ESC always acts on current-frame state.
	const uint32_t gameBits = computeGameMainOverlayBits();
	switch (input_arbiter::resolveTopBackTarget(gameBits)) {
		case input_arbiter::NativeBackTarget::ImGuiKeyboard:
			if (pUI && pUI->keyboard()) {
				pUI->keyboard()->hide();
			}
			break;

		case input_arbiter::NativeBackTarget::Scoreboard:
			if (pUI && pUI->scoreboard()) {
				pUI->scoreboard()->setVisible(false);
			}
			break;

		case input_arbiter::NativeBackTarget::CleoMenu:
			CleoEngine::dismissMenu();
			break;

		case input_arbiter::NativeBackTarget::ServerMenu:
			if (pNetGame && pNetGame->GetMenuPool()) {
				pNetGame->GetMenuPool()->ExitMenu();
			}
			break;

		case input_arbiter::NativeBackTarget::EditObject:
			EditObject::exitEdit();
			break;

		case input_arbiter::NativeBackTarget::ObjectSelect:
			if (pNetGame && pNetGame->GetObjectPool()) {
				pNetGame->GetObjectPool()->SetSelectState(false);
			}
			break;

		case input_arbiter::NativeBackTarget::TextDrawSelect:
			if (pNetGame && pNetGame->GetTextDrawPool()) {
				pNetGame->GetTextDrawPool()->SetSelectState(false, 0, true);
			}
			break;

		case input_arbiter::NativeBackTarget::None:
			break;
	}
}

bool isRadarSuppressed()
{
	const uint32_t flags = g_overlayFlags.load(std::memory_order_acquire);
	const int64_t closedAt = g_javaKeyboardClosedMs.load(std::memory_order_acquire);
	return input_arbiter::isRadarSuppressed(flags, nowSteadyMs(), closedAt);
}

bool shouldBypassExternalTouch()
{
	return input_arbiter::shouldBypassExternalTouch(g_overlayFlags.load(std::memory_order_acquire));
}

void onTouchCancel()
{
	if (pUI != nullptr) {
		pUI->enqueueTouchEvent(ImVec2(0.0f, 0.0f), TouchType::pop, true);
		if (pUI->buttonpanel() != nullptr) {
			pUI->buttonpanel()->clearHeldKeys();
		}
	}
}

void registerExternalTouchFilter(ExternalTouchFilterFn fn)
{
	g_externalTouchFilter.store(fn, std::memory_order_release);
}

void routeGtaTouch(int type, int num, int posX, int posY, ForwardGtaTouchFn forwardToGta)
{
	if (!forwardToGta) return;

	if (Game::IsGamePaused()) {
		g_gtaPointers.forward(type, num, posX, posY, forwardToGta);
		return;
	}

	const uint32_t flags = g_overlayFlags.load(std::memory_order_acquire);
	const bool javaDialogOpen = (flags & input_arbiter::kOverlayJavaDialog) != 0u;
	const bool modalBlocking = input_arbiter::isModalBlockingGtaTouch(flags);

	// 1. Java Dialog owns full-screen modal focus: cancel ImGui touch & release all GTA fingers.
	if (javaDialogOpen) {
		if (pUI != nullptr) {
			pUI->enqueueTouchEvent(ImVec2(0.0f, 0.0f), TouchType::pop, true);
		}
		g_gtaPointers.releaseAll(forwardToGta);
		return;
	}

	// 2. Non-modal external overlays (CLEO menu/gestures, MonetLoader touch filter).
	if (!modalBlocking) {
		const ImVec2 disp = pUI ? pUI->displaySizeSnapshot() : ImVec2(1920.0f, 1080.0f);
		if (CleoEngine::handleTouch(type, num, posX, posY, disp.x, disp.y)) {
			g_gtaPointers.releasePointer(num, forwardToGta);
			return;
		}
		if (auto touchFilter = g_externalTouchFilter.load(std::memory_order_acquire)) {
			if (touchFilter(type, num, posX, posY)) {
				g_gtaPointers.releasePointer(num, forwardToGta);
				return;
			}
		}
	}

	// 3. SA-MP Native UI (ImGui) & server interactive pools (TextDraw / Menu / ButtonPanel / SelectObject).
	if (pUI != nullptr) {
		const bool tdSelecting = !modalBlocking
				&& pNetGame && pNetGame->GetTextDrawPool() && pNetGame->GetTextDrawPool()->GetState();
		const bool hitBtnPanel = pUI->isButtonPanelHitSnapshot(ImVec2(static_cast<float>(posX), static_cast<float>(posY)));

		// Give selectable TextDraws priority over background ImGui widgets (chat/etc.)
		// while still letting the player tap ButtonPanel (e.g. ESC) to cancel selection.
		if (tdSelecting && !hitBtnPanel) {
			if (!pNetGame->GetTextDrawPool()->enqueueTouchEvent(type, posX, posY)) {
				g_gtaPointers.releasePointer(num, forwardToGta);
				return;
			}
		}

		switch (type) {
			case 2: // push
				pUI->enqueueTouchEvent(ImVec2(static_cast<float>(posX), static_cast<float>(posY)), TouchType::push, false, num);
				break;
			case 3: // move
				pUI->enqueueTouchEvent(ImVec2(static_cast<float>(posX), static_cast<float>(posY)), TouchType::move, false, num);
				break;
			case 1: // pop
				pUI->enqueueTouchEvent(ImVec2(static_cast<float>(posX), static_cast<float>(posY)), TouchType::pop, false, num);
				break;
		}

		if (modalBlocking) {
			g_gtaPointers.releaseAll(forwardToGta);
			return;
		}

		if (pNetGame && pNetGame->GetMenuPool()) {
			if (!pNetGame->GetMenuPool()->enqueueTouchEvent(type, posX, posY)) {
				g_gtaPointers.releasePointer(num, forwardToGta);
				return;
			}
		}

		if (hitBtnPanel) {
			g_gtaPointers.releasePointer(num, forwardToGta);
			return;
		}

		if (pNetGame && pNetGame->GetTextDrawPool()) {
			if (!pNetGame->GetTextDrawPool()->enqueueTouchEvent(type, posX, posY)) {
				g_gtaPointers.releasePointer(num, forwardToGta);
				return;
			}
		}

		if (pNetGame && pNetGame->GetObjectPool()) {
			if (!pNetGame->GetObjectPool()->enqueueTouchEvent(type, posX, posY)) {
				g_gtaPointers.releasePointer(num, forwardToGta);
				return;
			}
		}
	}

	// 4. GTA engine touch & camera input.
	if (pGame && pGame->IsGameInputEnabled()) {
		g_gtaPointers.forward(type, num, posX, posY, forwardToGta);
	}
	else {
		g_gtaPointers.releaseAll(forwardToGta);
	}
}

} // namespace InputArbiter
