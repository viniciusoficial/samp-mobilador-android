//
// Created by ALYN on 11-Jan-25.
//

#include "Actor.h"
#include "Game.h"
#include "Scripting.h"
#include "Util.h"
#include "Artwork/ArtworkCore.h"
#include "Artwork/ArtworkManager.h"
#include "../Client.h"
#include <SA.h>
#include <spdlog/spdlog.h>


CActor::CActor(int iSkin, float fX, float fY, float fZ, float fAngle, int iCustomSkin)
{
	spdlog::info("CActor::CActor({}, {}, {}, {}, {}, custom={})", iSkin, fX, fY, fZ, fAngle, iCustomSkin);
	m_actor = nullptr;
	m_gtaId = 0;
	m_invulnerable = false;
	m_iFallbackSkin = iSkin;
	m_iServerSkin = artwork::isCustomSkinId(iCustomSkin) ? iCustomSkin : iSkin;
	iSkin = ResolveEffectivePedSkin(iSkin, iCustomSkin);

	if (!IsValidPedModel(iSkin)) {
		spdlog::error("CActor::CActor: Invalid skin model: {}", iSkin);
		iSkin = 29;
	}
	iSkin = static_cast<int>(EnsureValidPedModelPrototype(static_cast<uint>(iSkin)));

	if (!pGame || !pGame->IsModelLoaded(iSkin)) {
		if (!pGame) return;
		pGame->RequestModel(iSkin);
		pGame->LoadRequestedModels();
		if (!pGame->IsModelLoaded(iSkin)) {
			spdlog::error("CActor::CActor: model {} failed to load", iSkin);
			return;
		}
	}

	uint32_t dwRet = 0;
	ScriptCommand(&create_actor, 5, iSkin, fX, fY, fZ - 1.0f, &dwRet);
	if (!dwRet) {
		spdlog::error("CActor::CActor: create_actor failed for model {}", iSkin);
		return;
	}

	m_gtaId = dwRet;
	m_actor = GamePool_Ped_GetAt(m_gtaId);
	m_entity = m_actor;
	if (!m_actor) {
		m_gtaId = 0;
		return;
	}

	ScriptCommand(&set_actor_z_angle, m_gtaId, fAngle);
	ScriptCommand(&set_actor_can_be_decapitated, m_gtaId, 0);
	ScriptCommand(&set_actor_decision_marker, m_gtaId, 0x10006);

	// Match PC actor.cpp: place at exact server coordinates, clear waiting-for-collision
	// freeze flag (0x40000), and set bInfiniteMass (0x40) so CPhysical::ProcessShift skips
	// ProcessShiftSectorList capsule-penetration pushing across all clients.
	TeleportTo(fX, fY, fZ);
	SetFacingAngle(fAngle);
	m_actor->m_bIsStaticWaitingForCollision = false;
	m_actor->physicalFlags.bInfiniteMass = 1;
	m_actor->physicalFlags.bDisableTurnForce = 1;
	m_actor->ResetMoveSpeed();
	m_actor->ResetTurnSpeed();
}

CActor::~CActor()
{
	if (m_actor && GamePool_Ped_GetAt(m_gtaId) == m_actor && !IsEntityPlaceable(m_actor)) {
		m_actor->Destructor();
	}
	m_gtaId = 0;
	m_actor = nullptr;
	m_entity = nullptr;
}

void CActor::SetHealth(float fHealth)
{
	if (m_actor && GamePool_Ped_GetAt(m_gtaId) == m_actor) {
		m_actor->m_fHealth = fHealth;

		if (m_actor->m_fHealth <= 0.0f) {
			ScriptCommand(&kill_actor, m_gtaId);
		}
	}
}

void CActor::SetInvulnerable(bool bInvulnerable)
{
	if (!m_actor || GamePool_Ped_GetAt(m_gtaId) != m_actor) return;
	m_invulnerable = bInvulnerable;

	m_actor->physicalFlags.bInfiniteMass = 1;
	m_actor->physicalFlags.bDisableTurnForce = 1;
	m_actor->physicalFlags.bDisableMoveForce = bInvulnerable ? 1 : 0;

	if (bInvulnerable) {
		m_actor->ResetMoveSpeed();
		m_actor->ResetTurnSpeed();
		ScriptCommand(&set_actor_immunities, m_gtaId, 1, 1, 1, 1, 1);
	}
	else {
		ScriptCommand(&set_actor_immunities, m_gtaId, 0, 0, 0, 1, 0);
	}
}

void CActor::ApplyAnimation(const char* szAnimName, const char* szAnimLib, float fDelta, int bLoop, int bLockX, int bLockY, int bFreeze, int iTime)
{
	if (!m_actor || GamePool_Ped_GetAt(m_gtaId) != m_actor) {
		return;
	}

	if (!szAnimLib || !szAnimLib[0] || !szAnimName || !szAnimName[0]) {
		return;
	}

	if (!strcasecmp(szAnimLib, "SEX")) {
		return;
	}
	if (!eng::fn::CAnimManager_GetAnimationBlock<void*>(szAnimLib)) {
		return;
	}

	int iWaitAnimLoad = 0;

	if (!pGame->IsAnimationLoaded(szAnimLib)) {
		pGame->RequestAnimation(szAnimLib);
		pGame->LoadRequestedModels();
		while (!pGame->IsAnimationLoaded(szAnimLib)) {
			usleep(1000);
			iWaitAnimLoad++;
			if (iWaitAnimLoad > 15) {
				spdlog::error("Animation {} not loaded!", szAnimLib);
				return;
			}
		}
	}

	spdlog::info("Animation {} loaded!", szAnimLib);

	ScriptCommand(&apply_animation, m_gtaId, szAnimName, szAnimLib, fDelta, bLoop, bLockX, bLockY, bFreeze, iTime);
}

void CActor::ClearAnimation()
{
	if (m_actor && GamePool_Ped_GetAt(m_gtaId) == m_actor && m_actor->m_pIntelligence) {
		// CPedIntelligence::FlushImmediately
		eng::fn::CPedIntelligence_FlushImmediately( m_actor->m_pIntelligence, true);
	}
}

void CActor::SetFacingAngle(float fAngle)
{
	if (m_actor && GamePool_Ped_GetAt(m_gtaId) == m_actor) {
		const float rad = DegToRad(fAngle);
		m_actor->m_fCurrentRotation = rad;
		m_actor->m_fAimingRotation = rad;
		ScriptCommand(&set_actor_z_angle, m_gtaId, fAngle);
	}
}

bool CActor::RefreshCustomSkin()
{
	if (!m_actor || GamePool_Ped_GetAt(m_gtaId) != m_actor) {
		return false;
	}
	if (!artwork::isCustomSkinId(m_iServerSkin) || artwork::isInternalCustomSlot(m_actor->m_nModelIndex)) {
		return false;
	}
	const int32_t slot = artwork::ArtworkManager::instance().ensureCustomSkinLoaded(m_iServerSkin);
	if (slot >= 0 && IsValidPedModel(static_cast<uint>(slot))) {
		SetModelIndex(static_cast<uint>(slot));
		return true;
	}
	return false;
}

void CActor::RevertCustomSkinBeforeUnload()
{
	if (!m_actor || GamePool_Ped_GetAt(m_gtaId) != m_actor) {
		return;
	}
	if (!artwork::isInternalCustomSlot(m_actor->m_nModelIndex)) {
		return;
	}
	const uint fallback = (m_iFallbackSkin >= 0 && !artwork::isInternalCustomSlot(m_iFallbackSkin)
		&& IsValidPedModel(static_cast<uint>(m_iFallbackSkin)))
		? static_cast<uint>(m_iFallbackSkin)
		: 0u;
	SetModelIndex(fallback);
}