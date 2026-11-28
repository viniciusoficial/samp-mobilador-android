#include "HookDomains.h"
#include "../Hooks.h"
#include "../Game.h"
#include "../TouchCombat.h"
#include "../TouchWidgets.h"
#include "../Engine/PatchRegistry.h"
#include "../Engine/Symbols.h"
#include "../../InputArbiter.h"
#include "../../Net/NetGame.h"
#include "../../Client.h"
#include "../../GameSA/include/Enums/WidgetIDs.h"
#include <Memory.h>
#include <spdlog/spdlog.h>
#include <atomic>

DECL_HOOK(void, AND_TouchEvent, int type, int num, int posX, int posY)
{
	InputArbiter::routeGtaTouch(type, num, posX, posY, AND_TouchEvent);
}

DECL_HOOK(int, GetInputType)
{
	return 0;
}

// engine rewrites targeting widgets back onto the shared attack posIdx on
// every reposition — reapply our split layout immediately afterwards or the
// aim icon lands on the fire button and eats its taps
DECL_HOOK(void, TI_RepositionAdjustableWidgets)
{
	TI_RepositionAdjustableWidgets();
	FixTouchWidgetLayout();
}

// No HID device may claim to natively provide a control: DrawAll hides a
// touch widget whenever Replaces(mapping) is true, and on this client the
// keyboard-class device claims every button mapping (emulator / physical
// keyboard present) which suppressed all on-screen buttons and the stick.
DECL_HOOK(int, CHID_Replaces, uint32_t mapping)
{
	return 0;
}

// No HID device may implement a touch-widget mapping either: CWidget::IsTouched
// / CWidget::Update treat Implements(mapping) && IsPressed(mapping) as "widget
// held", and the emulator's virtual input device reports pressed for attack
// (mapping 1) and the move axes (30/31) whenever a finger is down, causing
// punch-while-walking. With Implements==0 every widget falls back to the real
// finger-claim path. Consistent with Replaces/GetInputType being forced to 0.
DECL_HOOK(int, CHID_Implements, uint32_t mapping)
{
	return 0;
}

DECL_HOOK(void, CWidget_Update, uintptr_t pWidget)
{
	if (pNetGame)
	{
		switch (ProcessFixedWidget(pWidget))
		{
		case STATE_NONE:
			break;
		case STATE_DISABLED:
			return;
		}
	}
	RunWithUnscaledJoystickSx(pWidget, CWidget_Update);
}

std::atomic<bool> g_suppressWidgetSetEnabled{false};

DECL_HOOK(void, CWidget_SetEnabled, uintptr_t pWidget, bool bEnabled)
{
	if (g_suppressWidgetSetEnabled.load(std::memory_order_acquire))
	{
		return;
	}
	if (pNetGame)
	{
		switch (ProcessFixedWidget(pWidget))
		{
		case STATE_NONE:
			break;
		case STATE_DISABLED:
			bEnabled = false;
			break;
		}
	}
	CWidget_SetEnabled(pWidget, bEnabled);
}

void Hooks::installWidgetFixHooks()
{
	spdlog::info("Installing widget fix hooks...");

	GetTouchWidgets();

	// HOOK("_ZN7CWidgetC2EPKcRK14WidgetPositionj10HIDMapping", CWidget);
	REG_HOOK(Boot, CWidget_Update, CWidget_Update, "Touch widget layout & visibility fix");
	REG_HOOK(Boot, CWidget_SetEnabled, CWidget_SetEnabled, "Suppress widget disable during remote ped control");
}

void InstallInputHooks()
{
	REG_HOOK(Boot, AND_TouchEvent, AND_TouchEvent, "Touch routing (UI/CLEO/Monet/TextDraw)");
	REG_HOOK(Boot, CHID_GetInputType, GetInputType, "Keyboard/controller input type override");
	REG_HOOK(Boot, CHID_Replaces, CHID_Replaces, "HID mapping replaces override");
	REG_HOOK(Boot, CHID_Implements, CHID_Implements, "HID mapping implements override");
	REG_HOOK(Boot, CTouchInterface_RepositionAdjustableWidgets, TI_RepositionAdjustableWidgets, "Preserve custom buttons on HUD adjust");
}
