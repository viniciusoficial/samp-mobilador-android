//
// Created by x1y2z on 03.02.2023.
//

#include "WidgetGta.h"
#include "main.h"
#include "game/game.h"
#include "net/netgame.h"
#include "vendor/armhook/patch.h"
#include "WidgetRegionLook.h"
#include "TouchInterface.h"
#include "WidgetButton.h"
#include "../pad.h"

#include "../../gui/gui.h"
extern UI* pUI;

extern CNetGame *pNetGame;
extern CGame *pGame;
extern bool PcInputIsExternalInputPresent();

CWidgetGta* m_pWidgets[WidgetIDs::NUM_WIDGETS];

enum eWidgetState {
    STATE_NONE,
    STATE_DISABLED,
};

WidgetIDs GetWidgetTypeFromWidget(CWidgetGta* pWidget)
{
    for (int i = 0; i < NUM_WIDGETS; i++) {
        if (CTouchInterface::m_pWidgets[i] && pWidget == CTouchInterface::m_pWidgets[i]) {
            return static_cast<WidgetIDs>(i);
        }
    }
    return static_cast<WidgetIDs>(-1);
}

void SetWidgetFromId(int idWidget, CWidgetGta* pWidget)
{
    m_pWidgets[idWidget] = pWidget;
}

eWidgetState ProcessFixedWidget(CWidgetGta* pWidget)
{
    WidgetIDs widgetType = GetWidgetTypeFromWidget(pWidget);
    CPlayerPed *pPlayerPed = pGame->FindPlayerPed();
    if(!pPlayerPed) return STATE_NONE;

    switch(widgetType)
    {
        case -1: return STATE_NONE;
            break;

        case WidgetIDs::WIDGET_ATTACK:
            if (pPlayerPed->IsInPassengerDriveByMode()) {
                return STATE_NONE;
            }
            break;

        case WidgetIDs::WIDGET_BUTTON_SPRINT:
        case WidgetIDs::WIDGET_SPRINT:
            if (pPlayerPed->IsInVehicle() || pPlayerPed->IsInJetpackMode()) {
                return STATE_DISABLED;
            }
            break;

        case WidgetIDs::WIDGET_HORN:
        case WidgetIDs::WIDGET_NITRO:
            if (!pPlayerPed->IsInVehicle()) {
                return STATE_DISABLED;
            }
            break;

        case WidgetIDs::WIDGET_VEHICLE_SHOOT_LEFT:
        case WidgetIDs::WIDGET_VEHICLE_SHOOT_RIGHT:
            if (!pPlayerPed->IsInVehicle() || pPlayerPed->IsAPassenger()) {
                return STATE_DISABLED;
            }
            break;

        case WidgetIDs::WIDGET_CAM_TOGGLE:
            return STATE_NONE;
            break;

        case WidgetIDs::WIDGET_ROCKET:
            if (pNetGame) {
                if (!pPlayerPed->IsInVehicle()) {
                    return STATE_DISABLED;
                }
                else {
                    if (!pPlayerPed->GetGtaVehicle()) return STATE_NONE;
                    int model = pPlayerPed->GetGtaVehicle()->m_nModelIndex;
                    if (pPlayerPed->IsAPassenger()) {
                        return STATE_DISABLED;
                    }
                    switch (model) {
                        case 407:
                        case 425:
                        case 432:
                        case 447:
                        case 464:
                        case 465:
                        case 476:
                        case 520: return STATE_NONE;
                        default: return STATE_DISABLED;
                    }
                }
            }
            break;

        case WidgetIDs::WIDGET_DROP_CRANE:
        case WidgetIDs::WIDGET_CRANE_UP:
        case WidgetIDs::WIDGET_CRANE_DOWN:
            if (!pPlayerPed->IsInVehicle()) {
                return STATE_DISABLED;
            }
            break;

        case WidgetIDs::WIDGET_ACCELERATE:
        case WidgetIDs::WIDGET_BRAKE:
            if (!pPlayerPed->IsInVehicle() && !pPlayerPed->IsInJetpackMode()) {
                return STATE_DISABLED;
            }
            break;

        case WidgetIDs::WIDGET_ENTER_CAR:
            if (pNetGame) {
                CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
                if (pVehiclePool) {
                    VEHICLEID vehicleId = pVehiclePool->FindNearestToLocalPlayerPed();
                    if (vehicleId == INVALID_VEHICLE_ID) {
                        return STATE_DISABLED;
                    }
                    else {
                        CVehicle* pVehicle = pVehiclePool->GetAt(vehicleId);
                        if (pVehicle) {
                            if (!pPlayerPed->IsInVehicle() && pPlayerPed->IsCrouching() && pVehicle->m_pVehicle->GetDistanceFromLocalPlayerPed() > 10.0f && !pPlayerPed->IsInJetpackMode()) {
                                return STATE_DISABLED;
                            }
                        }
                    }
                }
            }
            break;
    }
    return STATE_NONE;
}

void CWidgetGta::SetEnabled(bool bEnabled) {
    m_bEnabled = bEnabled;
}

bool (*CWidget__IsTouched)(uintptr_t *thiz, CVector2D *pVecOut);
bool CWidget__IsTouched_hook(uintptr_t *thiz, CVector2D *pVecOut) {
    return CWidget__IsTouched(thiz, pVecOut);
}

uintptr_t (*CWidget)(CWidgetButton* thiz, const char* name, uintptr_t* a3, int a4, uintptr_t* a5);
uintptr_t CWidget_hook(CWidgetButton* thiz, const char* name, uintptr_t*  a3, int a4, uintptr_t* a5)
{
    Log("New Widget: \"%s\" 0x%X", name, thiz-g_libGTASA);
    return CWidget(thiz, name, a3, a4, a5);
}

static bool IsTouchControlWidget(CWidgetGta* pWidget)
{
    if (!pWidget) return false;
    WidgetIDs id = GetWidgetTypeFromWidget(pWidget);
    if (id == WIDGET_PLAYER_INFO || id == WIDGET_RADAR ||
        id == WIDGET_HELP_TEXT || id == WIDGET_MAP) {
        return false;
    }
    return true;
}

static bool ShouldHideTouchWidget(CWidgetGta* pWidget)
{
    return PcInputIsExternalInputPresent() && IsTouchControlWidget(pWidget);
}

#define DEFINE_WIDGET_DRAW_HOOK(Name) \
    void (*Name##_orig)(CWidgetGta* thiz); \
    void Name##_hook(CWidgetGta* thiz) { \
        if (ShouldHideTouchWidget(thiz)) return; \
        Name##_orig(thiz); \
    }

DEFINE_WIDGET_DRAW_HOOK(CWidgetButton__Draw)
DEFINE_WIDGET_DRAW_HOOK(CWidgetButtonAnimated__Draw)
DEFINE_WIDGET_DRAW_HOOK(CWidgetButtonEnterCar__Draw)
DEFINE_WIDGET_DRAW_HOOK(CWidgetButtonHydraulics__Draw)
DEFINE_WIDGET_DRAW_HOOK(CWidgetRegion__Draw)
DEFINE_WIDGET_DRAW_HOOK(CWidgetRegionMove__Draw)
DEFINE_WIDGET_DRAW_HOOK(CWidgetRegionSteer__Draw)
DEFINE_WIDGET_DRAW_HOOK(CWidgetRegionSprint__Draw)
DEFINE_WIDGET_DRAW_HOOK(CWidgetRegionDriveHybrid__Draw)
DEFINE_WIDGET_DRAW_HOOK(CWidgetRegionHydraulics__Draw)
DEFINE_WIDGET_DRAW_HOOK(CWidgetRegionFlick__Draw)
DEFINE_WIDGET_DRAW_HOOK(CWidgetThumbCircle__Draw)

void (*CWidget__SetEnabled)(CWidgetGta* pWidget, bool bEnabled);
void CWidget__SetEnabled_hook(CWidgetGta* pWidget, bool bEnabled)
{
    WidgetIDs widgetType = GetWidgetTypeFromWidget(pWidget);

    if(widgetType == WidgetIDs::WIDGET_CAM_TOGGLE) {
        CWidget__SetEnabled(pWidget, true);
        pWidget->m_bEnabled = true;
        return;
    }

    if(pNetGame)
    {
        switch(ProcessFixedWidget(pWidget))
        {
            case STATE_NONE: break;
            case STATE_DISABLED:
                bEnabled = false;
                break;
        }
    }
    CWidget__SetEnabled(pWidget, bEnabled);
}

void (*CWidgetButton__Update)(CWidgetButton* thiz);
void CWidgetButton__Update_hook(CWidgetButton* thiz) {
    if (ShouldHideTouchWidget(thiz)) {
        return;
    }

    WidgetIDs widgetType = GetWidgetTypeFromWidget(thiz);

    if(widgetType == WidgetIDs::WIDGET_CAM_TOGGLE) {
        if (!thiz->m_bEnabled) {
            thiz->SetEnabled(true);
        }
        CWidgetButton__Update(thiz);
        return;
    }

    if(pNetGame)
    {
        switch(ProcessFixedWidget(thiz))
        {
            case STATE_NONE: break;
            case STATE_DISABLED: return;
        }
    }
    CWidgetButton__Update(thiz);
}

eWidgetState ProcessFixedWidgetFromId(int iWidgetId)
{
    CPlayerPed *pPlayerPed = pGame->FindPlayerPed();
    if(!pPlayerPed || !pPlayerPed->m_pPed) return STATE_NONE;

    switch(iWidgetId)
    {
        case WIDGET_CAM_TOGGLE:
            return STATE_NONE;
            break;

        case WIDGET_VEHICLE_STEER_LEFT:
            if(!pPlayerPed->IsInVehicle() || pPlayerPed->IsAPassenger() || pPlayerPed->IsInJetpackMode())
                return STATE_DISABLED;
            break;
        case WIDGET_VEHICLE_STEER_RIGHT:
            if(!pPlayerPed->IsInVehicle() || pPlayerPed->IsAPassenger() || pPlayerPed->IsInJetpackMode())
                return STATE_DISABLED;
            break;
        case WIDGET_PED_MOVE:
            if(pPlayerPed->IsInVehicle())
            {
                return STATE_DISABLED;
            }
            break;
        case WIDGET_VEHICLE_STEER_ANALOG:
            if(!pPlayerPed->IsInVehicle() &&
               !pPlayerPed->IsInJetpackMode())
            {
                return STATE_DISABLED;
            }
            break;
        default:
            return STATE_NONE;
            break;
    }
    return STATE_NONE;
}

int (*CTouchInterface__IsTouched)(int iWidgetId, CVector2D *pVecOut, int nFrameCount);
int CTouchInterface__IsTouched_hook(int iWidgetId, CVector2D *pVecOut, int nFrameCount)
{
    if (PcInputIsExternalInputPresent())
        return 0;

    if(iWidgetId == WIDGET_CAM_TOGGLE) {
        return CTouchInterface__IsTouched(iWidgetId, pVecOut, nFrameCount);
    }

    if(pNetGame)
    {
        switch(ProcessFixedWidgetFromId(iWidgetId))
        {
            case STATE_NONE: break;
            case STATE_DISABLED:
                nFrameCount = 0;
                break;
        }
    }
    return CTouchInterface__IsTouched(iWidgetId, pVecOut, nFrameCount);
}

void CWidgetGta::InjectHooks() {
    CHook::InlineHook("_ZN7CWidget6UpdateEv", &CWidgetButton__Update_hook, &CWidgetButton__Update);
    CHook::InlineHook("_ZN7CWidget10SetEnabledEb", &CWidget__SetEnabled_hook, &CWidget__SetEnabled);
    CHook::InlineHook("_ZN15CTouchInterface9IsTouchedENS_9WidgetIDsEP9CVector2Di", &CTouchInterface__IsTouched_hook,
                      &CTouchInterface__IsTouched);

    CHook::InlineHook("_ZN13CWidgetButton4DrawEv", &CWidgetButton__Draw_hook, &CWidgetButton__Draw_orig);
    CHook::InlineHook("_ZN21CWidgetButtonAnimated4DrawEv", &CWidgetButtonAnimated__Draw_hook, &CWidgetButtonAnimated__Draw_orig);
    CHook::InlineHook("_ZN21CWidgetButtonEnterCar4DrawEv", &CWidgetButtonEnterCar__Draw_hook, &CWidgetButtonEnterCar__Draw_orig);
    CHook::InlineHook("_ZN23CWidgetButtonHydraulics4DrawEv", &CWidgetButtonHydraulics__Draw_hook, &CWidgetButtonHydraulics__Draw_orig);
    CHook::InlineHook("_ZN13CWidgetRegion4DrawEv", &CWidgetRegion__Draw_hook, &CWidgetRegion__Draw_orig);
    CHook::InlineHook("_ZN17CWidgetRegionMove4DrawEv", &CWidgetRegionMove__Draw_hook, &CWidgetRegionMove__Draw_orig);
    CHook::InlineHook("_ZN18CWidgetRegionSteer4DrawEv", &CWidgetRegionSteer__Draw_hook, &CWidgetRegionSteer__Draw_orig);
    CHook::InlineHook("_ZN19CWidgetRegionSprint4DrawEv", &CWidgetRegionSprint__Draw_hook, &CWidgetRegionSprint__Draw_orig);
    CHook::InlineHook("_ZN24CWidgetRegionDriveHybrid4DrawEv", &CWidgetRegionDriveHybrid__Draw_hook, &CWidgetRegionDriveHybrid__Draw_orig);
    CHook::InlineHook("_ZN23CWidgetRegionHydraulics4DrawEv", &CWidgetRegionHydraulics__Draw_hook, &CWidgetRegionHydraulics__Draw_orig);
    CHook::InlineHook("_ZN18CWidgetRegionFlick4DrawEv", &CWidgetRegionFlick__Draw_hook, &CWidgetRegionFlick__Draw_orig);
    CHook::InlineHook("_ZN18CWidgetThumbCircle4DrawEv", &CWidgetThumbCircle__Draw_hook, &CWidgetThumbCircle__Draw_orig);
}

void CWidgetGta::SetTexture(const char *name) {
    m_Sprite.m_pTexture = CUtil::LoadTextureFromDB("mobile", name);
}

bool CWidgetGta::IsReleased(CVector2D *pVecOut) {
    return CHook::CallFunction<bool>(g_libGTASA + (VER_x32 ? 0x002B3484 + 1 : 0x372794), this, pVecOut);
}

bool CWidgetGta::IsTouched(CVector2D *pVecOut) {
    return CHook::CallFunction<bool>(g_libGTASA + (VER_x32 ? 0x002B3324 + 1 : 0x3725D0), this, pVecOut);
}
