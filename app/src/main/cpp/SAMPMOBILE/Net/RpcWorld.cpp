#include "RpcHandlers.h"
#include "NetGame.h"
#include "LocalPlayer.h"
#include "RemotePlayer.h"
#include "VehiclePool.h"
#include "ActorPool.h"
#include "PickupPool.h"
#include "TextLabelPool.h"
#include "TextDrawPool.h"
#include "GangZonePool.h"
#include "MenuPool.h"
#include "../Client.h"
#include "../Settings.h"
#include "../Game/Game.h"
#include "../Game/AudioStream.h"
#include "../Game/Camera.h"
#include "../Game/Object.h"
#include "../Game/PlayerPed.h"
#include "../Game/RemoveBuilding.h"
#include "../Game/TextDraw.h"
#include "../Game/Vehicle.h"
#include "../Game/Util.h"
#include "../Game/Artwork/ArtworkCore.h"
#include "../Java/EditObject.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/Chat.h"
#include <encoding.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include "../L10n.h"

namespace {
inline bool isFinite3(float x, float y, float z) {
	return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}
} // namespace

void RpcHandlers::OnShowGangZone(const proto::ShowGangZone& p)
{
	spdlog::debug("ScriptRPC: ScrAddGangZone");

	if (!proto::isFinite(p.min) || !proto::isFinite(p.max)) {
		return;
	}

	CGangZonePool* pGangZonePool = pNetGame->GetGangZonePool();
	if (pGangZonePool) {
		pGangZonePool->New(p.zoneId, p.min.x, p.min.y, p.max.x, p.max.y, p.color);
	}
}

void RpcHandlers::OnHideGangZone(const proto::HideGangZone& p)
{
	spdlog::debug("ScriptRPC: ScrGangZoneDestroy");

	CGangZonePool* pGangZonePool = pNetGame->GetGangZonePool();
	if (pGangZonePool) {
		pGangZonePool->Delete(p.zoneId);
	}
}

void RpcHandlers::OnFlashGangZone(const proto::FlashGangZone& p)
{
	spdlog::debug("ScriptRPC: ScrGangZoneFlash");

	CGangZonePool* pGangZonePool = pNetGame->GetGangZonePool();
	if (pGangZonePool) {
		pGangZonePool->Flash(p.zoneId, p.color);
	}
}

void RpcHandlers::OnStopFlashGangZone(const proto::StopFlashGangZone& p)
{
	spdlog::debug("ScriptRPC: ScrGangZoneStopFlash");

	CGangZonePool* pGangZonePool = pNetGame->GetGangZonePool();
	if (pGangZonePool) {
		pGangZonePool->StopFlash(p.zoneId);
	}
}

static void ApplyObjectMaterial(CObject* pObject, const proto::ObjectMaterial& m, size_t maxTexNameLen)
{
	if (!pObject) return;

	if (m.materialType == MATERIAL_TYPE_TEXTURE) {
		if (m.txdName.size() >= maxTexNameLen || m.textureName.size() >= maxTexNameLen) {
			return;
		}

		int iMaterialModel = -1;
		if (m.modelId != 0xFFFF) {
			const int32_t decoded = artwork::decodeWireModelId16(m.modelId);
			if (artwork::isCustomModelId(decoded) || (decoded >= 0 && decoded <= 20000)) {
				iMaterialModel = decoded;
			}
		}

		pObject->SetMaterial(iMaterialModel, m.materialIndex,
			const_cast<char*>(m.txdName.c_str()),
			const_cast<char*>(m.textureName.c_str()),
			m.materialColor);
	}
	else if (m.materialType == MATERIAL_TYPE_TEXT) {
		if (m.fontName.size() >= 33) {
			return;
		}

		char utf8[2048 + 1];
		strncpy(utf8, Encoding::cp2utf(m.text.c_str()).c_str(), 2048);
		utf8[2048] = '\0';

		pObject->SetMaterialText(m.materialIndex, m.materialSize,
			const_cast<char*>(m.fontName.c_str()),
			m.fontSize, m.fontBold, m.fontColor, m.backgroundColor, m.alignment, utf8);
	}
}

void RpcHandlers::OnCreateObject(const proto::CreateObject& p)
{
	spdlog::debug("ScriptRPC: ScrCreateObject");

	// NaN coords/rotation would write straight into the engine transform
	if (!proto::isFinite(p.position) || !proto::isFinite(p.rotation)
			|| !proto::isFinite(p.attachOffset) || !proto::isFinite(p.attachRotation)) {
		spdlog::warn("ScrCreateObject: rejected non-finite transform for object {}", p.objectId);
		return;
	}

	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	if (!pObjectPool) return;

	sa::CVector vecPos(p.position.x, p.position.y, p.position.z);
	sa::CVector vecRot(p.rotation.x, p.rotation.y, p.rotation.z);
	if (!pObjectPool->New(p.objectId, p.modelId, vecPos, vecRot, p.drawDistance)) {
		return;
	}

	CObject* pObject = pObjectPool->GetAt(p.objectId);
	sa::CVector vecAttachOffset(p.attachOffset.x, p.attachOffset.y, p.attachOffset.z);
	sa::CVector vecAttachRot(p.attachRotation.x, p.attachRotation.y, p.attachRotation.z);
	if (p.attachedVehicleId != INVALID_VEHICLE_ID) {
		if (pObject) {
			pObject->SetAttachedVehicle(p.attachedVehicleId, &vecAttachOffset, &vecAttachRot);
		}
	}
	else if (p.attachedObjectId != INVALID_OBJECT_ID) {
		if (pObject) {
			pObject->SetAttachedObject(p.attachedObjectId, &vecAttachOffset, &vecAttachRot, p.syncRotation);
		}
	}

	for (const auto& mat : p.materials) {
		ApplyObjectMaterial(pObject, mat, 32);
	}

	if (pObject && p.noCameraCol) {
		pObject->m_bNoCameraCollision = true;
	}
}

void RpcHandlers::OnDestroyObject(const proto::DestroyObject& p)
{
	spdlog::debug("ScriptRPC: ScrDestroyObject");

	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	if (!pObjectPool) return;

	CObject* pObject = pObjectPool->GetAt(p.objectId);
	if (pObject) {
		CPlayerPool* pPlayerPool = pNetGame->GetPlayerPool();
		if (pPlayerPool) {
			CLocalPlayer* pLocalPlayer = pPlayerPool->GetLocalPlayer();
			if (pLocalPlayer && pLocalPlayer->m_surfSync.bIsActive &&
					!pLocalPlayer->m_surfSync.bIsVehicle &&
					pLocalPlayer->m_surfSync.pSurfInst == (uintptr_t) pObject) {
				pLocalPlayer->DisableSurfing();
			}
		}

		if (pObjectPool->Delete(p.objectId)) {
			spdlog::debug("DestroyObject; Total objects: {}", pObjectPool->m_iObjectCount);
		}
	}
}

void RpcHandlers::OnSetObjectMaterial(const proto::SetObjectMaterial& p)
{
	spdlog::debug("ScriptRPC: ScrSetObjectMaterial");

	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	if (!pObjectPool || p.objectId >= MAX_OBJECTS) {
		return;
	}

	CObject* pObject = pObjectPool->GetAt(p.objectId);
	if (pObject) {
		ApplyObjectMaterial(pObject, p.material, 33);
	}
}

void RpcHandlers::OnRemoveBuilding(const proto::RemoveBuilding& p)
{
	spdlog::debug("ScriptRPC: ScrRemoveBuilding");
	RemoveBuilding::add(p.modelId, {p.position.x, p.position.y, p.position.z}, p.radius);
}

void RpcHandlers::OnStopObjectCameraCollision(const proto::StopObjectCameraCollision& p)
{
	spdlog::debug("ScriptRPC: ScrStopObjectCameraCollision");

	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	if (!pObjectPool) return;

	CObject* pObject = pObjectPool->GetAt(p.objectId);
	if (pObject) {
		pObject->m_bNoCameraCollision = true;
	}
}

void RpcHandlers::OnShowTextDraw(const proto::ShowTextDraw& p)
{
	spdlog::debug("ScriptRPC: ScrShowTextDraw");

	CTextDrawPool* pTextDrawPool = pNetGame->GetTextDrawPool();
	if (pTextDrawPool == nullptr) {
		return;
	}

	if (p.style > 5 || !std::isfinite(p.position.x) || !std::isfinite(p.position.y)
			|| !std::isfinite(p.letterSize.x) || !std::isfinite(p.letterSize.y)
			|| !std::isfinite(p.lineSize.x) || !std::isfinite(p.lineSize.y)
			|| !isFinite3(p.rotation.x, p.rotation.y, p.rotation.z) || !std::isfinite(p.zoom)) {
		return;
	}

	if (p.text.size() < MAX_TEXT_DRAW_LINE) {
		TEXT_DRAW_TRANSMIT textDrawTransmit{};
		textDrawTransmit.byteFlags = p.flags;
		textDrawTransmit.fLetterWidth = p.letterSize.x;
		textDrawTransmit.fLetterHeight = p.letterSize.y;
		textDrawTransmit.dwLetterColor = p.letterColor;
		textDrawTransmit.fLineWidth = p.lineSize.x;
		textDrawTransmit.fLineHeight = p.lineSize.y;
		textDrawTransmit.dwBoxColor = p.boxColor;
		textDrawTransmit.byteShadow = p.shadow;
		textDrawTransmit.byteOutline = p.outline;
		textDrawTransmit.dwBackgroundColor = p.backgroundColor;
		textDrawTransmit.byteStyle = p.style;
		textDrawTransmit.byteSelectable = p.selectable;
		textDrawTransmit.fX = p.position.x;
		textDrawTransmit.fY = p.position.y;
		textDrawTransmit.wModelID = p.modelId;
		textDrawTransmit.vecRot = sa::CVector(p.rotation.x, p.rotation.y, p.rotation.z);
		textDrawTransmit.fZoom = p.zoom;
		textDrawTransmit.wColor1 = p.color1;
		textDrawTransmit.wColor2 = p.color2;

		pTextDrawPool->New(p.textDrawId, &textDrawTransmit, const_cast<char*>(p.text.c_str()));
	}
	else {
		if (pUI) pUI->chat()->addInfoMessage("Warning: ignoring large TetDraw size=%u", static_cast<unsigned>(p.text.size()));
	}
}

void RpcHandlers::OnHideTextDraw(const proto::HideTextDraw& p)
{
	spdlog::debug("ScriptRPC: ScrHideTextDraw");

	CTextDrawPool* pTextDrawPool = pNetGame->GetTextDrawPool();
	if (pTextDrawPool == nullptr) {
		return;
	}

	pTextDrawPool->Delete(p.textDrawId);
}

void RpcHandlers::OnTextDrawSetString(const proto::TextDrawSetString& p)
{
	spdlog::debug("ScriptRPC: ScrTextDrawSetString");

	CTextDrawPool* pTextDrawPool = pNetGame->GetTextDrawPool();
	if (pTextDrawPool == nullptr) {
		return;
	}

	if (p.text.size() < MAX_TEXT_DRAW_LINE) {
		CTextDraw* pTextDraw = pTextDrawPool->GetAt(p.textDrawId);
		if (pTextDraw) {
			pTextDraw->SetText(p.text.c_str());
		}
	}
}

void RpcHandlers::OnSelectTextDraw(const proto::SelectTextDraw& p)
{
	spdlog::debug("ScriptRPC: ScrSelectTextDraw");
	spdlog::debug("SELECTTEXTDRAW: bEnable: {}, dwColor: {}", p.enable, p.hoverColor);

	if (p.enable) {
		if (EditObject::m_visible) {
			EditObject::cancelEdit();
		}
		if (CObjectPool* pObjPool = pNetGame->GetObjectPool(); pObjPool && pObjPool->IsSelectState()) {
			pObjPool->SetSelectState(false);
		}
	}

	if (CTextDrawPool* pTdPool = pNetGame->GetTextDrawPool()) {
		pTdPool->SetSelectState(p.enable, p.hoverColor, !p.enable);
	}
}

void RpcHandlers::OnSetVehicleHealth(const proto::SetVehicleHealth& p)
{
	spdlog::debug("ScriptRPC: ScrSetVehicleHealth");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (pVehiclePool) {
		CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);
		if (pVehicle) {
			pVehicle->SetHealth(p.health);
		}
	}
}

void RpcHandlers::OnAttachTrailerToVehicle(const proto::AttachTrailerToVehicle& p)
{
	spdlog::debug("ScriptRPC: ScrAttachTrailerToVehicle");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CVehicle* pTrailer = pVehiclePool->GetAt(p.trailerId);
	CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);

	if (pTrailer && pVehicle) {
		pVehicle->SetTrailer(pTrailer);
		pVehicle->AttachTrailer();
	}
}

void RpcHandlers::OnDetachTrailerFromVehicle(const proto::DetachTrailerFromVehicle& p)
{
	spdlog::debug("ScriptRPC: ScrDetachTrailerFromVehicle");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);
	if (pVehicle) {
		pVehicle->DetachTrailer();
		pVehicle->SetTrailer(nullptr);
	}
}

void RpcHandlers::OnSetObjectPos(const proto::SetObjectPos& p)
{
	spdlog::debug("ScriptRPC: ScrSetObjectPos");

	if (!proto::isFinite(p.position)) {
		spdlog::warn("ScrSetObjectPos: rejected non-finite position for object {}", p.objectId);
		return;
	}

	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	if (!pObjectPool) {
		return;
	}

	CObject* pObject = pObjectPool->GetAt(p.objectId);
	if (pObject) {
		pObject->TeleportTo(p.position.x, p.position.y, p.position.z);
	}
}

void RpcHandlers::OnSetObjectRotation(const proto::SetObjectRotation& p)
{
	spdlog::debug("ScriptRPC: ScrSetObjectRotation");

	if (!proto::isFinite(p.rotation)) {
		spdlog::warn("ScrSetObjectRotation: rejected non-finite rotation for object {}", p.objectId);
		return;
	}

	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	if (!pObjectPool) {
		return;
	}

	CObject* pObject = pObjectPool->GetAt(p.objectId);
	if (!pObject) {
		return;
	}

	sa::CVector vecRot(p.rotation.x, p.rotation.y, p.rotation.z);
	pObject->SetRotation(&vecRot);
}

void RpcHandlers::OnSetVehicleNumberPlate(const proto::SetVehicleNumberPlate& p)
{
	spdlog::debug("ScriptRPC: ScrSetVehicleNumberPlate");
	if (p.plate.size() > 32) {
		return;
	}

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);
	if (!pVehicle) {
		return;
	}

	pVehicle->SetPlateText(p.plate.c_str());
}

void RpcHandlers::OnRemoveVehicleComponent(const proto::RemoveVehicleComponent& p)
{
	spdlog::debug("ScriptRPC: ScrRemoveVehicleComponent");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);
	if (!pVehicle) {
		return;
	}

	pVehicle->RemoveComponent(p.componentId);
}

void RpcHandlers::OnAttachObjectToPlayer(const proto::AttachObjectToPlayer& p)
{
	spdlog::debug("ScriptRPC: ScrAttachObjectToPlayer");

	if (!proto::isFinite(p.offset) || !proto::isFinite(p.rotation)) {
		spdlog::warn("ScrAttachObjectToPlayer: rejected non-finite offset/rot for object {}", p.objectId);
		return;
	}

	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	if (!pObjectPool) {
		return;
	}
	CObject* pObject = pObjectPool->GetAt(p.objectId);
	if (!pObject || !pObject->m_entity || !pObject->m_gtaId) {
		return;
	}

	sa::CVector offset(p.offset.x, p.offset.y, p.offset.z);
	sa::CVector rot(p.rotation.x, p.rotation.y, p.rotation.z);
	pObject->SetAttachedPlayer(p.playerId, &offset, &rot);
}

void RpcHandlers::OnSetVehicleVelocity(const proto::SetVehicleVelocity& p)
{
	spdlog::debug("ScriptRPC: ScrSetVehicleVelocity");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CPlayerPed* pPlayerPed = pGame->FindPlayerPed();
	if (!pPlayerPed) {
		return;
	}

	if (pPlayerPed->IsInVehicle()) {
		sa::CVehicle* pGtaVehicle = pPlayerPed->GetGtaVehicle();
		VEHICLEID VehicleID = pVehiclePool->FindIDFromGtaPtr(pGtaVehicle);
		CVehicle* pVehicle = pVehiclePool->GetAt(VehicleID);
		if (!pVehicle || !pVehicle->m_pVehicle || !pVehicle->VerifyInstance()) {
			return;
		}

		sa::CVector vecVelocity(p.velocity.x, p.velocity.y, p.velocity.z);
		if (p.type) {
			if (p.type == 1) {
				pVehicle->m_pVehicle->SetTurnSpeed(vecVelocity);
			}
		}
		else {
			pVehicle->m_pVehicle->SetVelocity(vecVelocity);
		}
	}
}

void RpcHandlers::OnSetVehicleTireDamageStatus(const proto::SetVehicleTireDamageStatus& p)
{
	spdlog::debug("ScriptRPC: ScrSetVehicleTireDamageStatus");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);
	if (!pVehicle) {
		return;
	}

	pVehicle->SetTireDamageStatus(p.tireDamageStatus);
}

void RpcHandlers::OnSetVehiclePos(const proto::SetVehiclePos& p)
{
	spdlog::debug("ScriptRPC: ScrSetVehiclePos");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);
	if (!pVehicle) {
		return;
	}

	pVehicle->TeleportTo(p.position.x, p.position.y, p.position.z);
}

void RpcHandlers::OnSetVehicleZAngle(const proto::SetVehicleZAngle& p)
{
	spdlog::debug("ScriptRPC: ScrSetVehicleZAngle");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	CVehicle* pVehicle = pVehiclePool->GetAt(p.vehicleId);
	if (!pVehicle) {
		return;
	}

	pVehicle->SetZAngle(p.angle);
}

void RpcHandlers::OnSetVehicleParamsForPlayer(const proto::SetVehicleParamsForPlayer& p)
{
	spdlog::debug("ScriptRPC: ScrSetVehicleParams");
	spdlog::debug("VehicleID: {}", p.vehicleId);
	spdlog::debug("objective: {}, doors: {}", p.objective, p.doorsLocked);

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (pVehiclePool) {
		pVehiclePool->AssignSpecialParamsToVehicle(p.vehicleId, p.objective, p.doorsLocked);
	}
}

void RpcHandlers::OnLinkVehicleToInterior(const proto::LinkVehicleToInterior& p)
{
	spdlog::debug("ScriptRPC: ScrLinkVehicleToInterior");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) {
		return;
	}

	pVehiclePool->LinkToInterior(p.vehicleId, p.interiorId);
}

void RpcHandlers::OnApplyActorAnimation(const proto::ApplyActorAnimation& p)
{
	spdlog::debug("ScriptRPC: ScrApplyActorAnimation");

	CActorPool* pActorPool = pNetGame->GetActorPool();
	if (!pActorPool) {
		return;
	}

	CActor* pActor = pActorPool->GetAt(p.actorId);
	if (pActor) {
		pActor->ApplyAnimation(
			const_cast<char*>(p.animName.c_str()),
			const_cast<char*>(p.animLib.c_str()),
			p.delta, p.loop, p.lockX, p.lockY, p.freeze, p.time);
	}
}

void RpcHandlers::OnClearActorAnimation(const proto::ClearActorAnimation& p)
{
	spdlog::debug("ScriptRPC: ScrClearActorAnimation");

	CActorPool* pActorPool = pNetGame->GetActorPool();
	if (!pActorPool) {
		return;
	}

	CActor* pActor = pActorPool->GetAt(p.actorId);
	if (pActor) {
		pActor->ClearAnimation();
	}
}

void RpcHandlers::OnSetActorFacingAngle(const proto::SetActorFacingAngle& p)
{
	spdlog::debug("ScriptRPC: ScrSetActorFacingAngle");

	CActorPool* pActorPool = pNetGame->GetActorPool();
	if (!pActorPool) {
		return;
	}

	CActor* pActor = pActorPool->GetAt(p.actorId);
	if (pActor) {
		pActor->SetFacingAngle(p.angle);
	}
}

void RpcHandlers::OnSetActorPos(const proto::SetActorPos& p)
{
	spdlog::debug("ScriptRPC: ScrSetActorPos");

	CActorPool* pActorPool = pNetGame->GetActorPool();
	if (!pActorPool) {
		return;
	}

	CActor* pActor = pActorPool->GetAt(p.actorId);
	if (pActor) {
		pActor->TeleportTo(p.position.x, p.position.y, p.position.z);
	}
}

void RpcHandlers::OnSetActorHealth(const proto::SetActorHealth& p)
{
	spdlog::debug("ScriptRPC: ScrSetActorHealth");

	CActorPool* pActorPool = pNetGame->GetActorPool();
	if (!pActorPool) {
		return;
	}

	CActor* pActor = pActorPool->GetAt(p.actorId);
	if (pActor) {
		pActor->SetHealth(p.health);
	}
}

void RpcHandlers::OnPlayAudioStream(const proto::PlayAudioStream& p)
{
	spdlog::debug("ScriptRPC: ScrPlayAudioStream");

	if (Settings::audioMsg() && pUI && pUI->chat()) {
		pUI->chat()->addInfoMessage(L10N("[Audio Stream]: %s", "[音频流]：%s"), p.url.c_str());
	}

	if (Settings::logUrls() && Client::gameDir()) {
		char szPath[512];
		snprintf(szPath, sizeof(szPath), "%sSAMP/urls.txt", Client::gameDir());
		if (FILE* f = fopen(szPath, "a")) {
			fprintf(f, "%s\n", p.url.c_str());
			fclose(f);
		}
	}

	if (pAudioStream) {
		pAudioStream->Play(p.url.c_str(), p.position.x, p.position.y, p.position.z, p.radius, p.usePos != 0);
	}
}

void RpcHandlers::OnStopAudioStream(const proto::StopAudioStream&)
{
	spdlog::debug("ScriptRPC: ScrStopAudioStream");

	if (pAudioStream) {
		pAudioStream->Stop(false);
	}
	// pAudioStream->Stop(true);
}

void RpcHandlers::OnMoveObject(const proto::MoveObject& p)
{
	spdlog::debug("ScriptRPC: ScrMoveObject");

	if (!proto::isFinite(p.targetPos) || !proto::isFinite(p.targetRot) || !std::isfinite(p.speed)) {
		spdlog::warn("ScrMoveObject: rejected non-finite params for object {}", p.objectId);
		return;
	}

	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	if (!pObjectPool) return;

	if (EditObject::m_visible && EditObject::m_type == EditObject::TYPE_OBJECT &&
			EditObject::m_id == (int) p.objectId) {
		return;
	}

	CObject* pObject = pObjectPool->GetAt(p.objectId);
	if (pObject) {
		pObject->MoveTo(p.targetPos.x, p.targetPos.y, p.targetPos.z, p.speed,
			p.targetRot.x, p.targetRot.y, p.targetRot.z);
	}
}

void RpcHandlers::OnStopObject(const proto::StopObject& p)
{
	spdlog::debug("ScriptRPC: ScrStopObject");

	CObjectPool* pObjectPool = pNetGame->GetObjectPool();
	if (!pObjectPool) return;

	CObject* pObject = pObjectPool->GetAt(p.objectId);
	if (pObject) {
		pObject->StopMoving();
	}
}

void RpcHandlers::OnDisableVehicleCollisions(const proto::DisableVehicleCollisions& p)
{
	spdlog::debug("ScriptRPC: ScrDisableVehicleCollisions");
	pNetGame->m_netSet->disableVehicleCollisions = p.disable;
}

void RpcHandlers::OnInitMenu(const proto::InitMenu& p)
{
	spdlog::debug("ScriptRPC: ScrInitMenu id={} cols={}", p.menuId, p.hasTwoColumns ? 2 : 1);
	if (CMenuPool* pMenuPool = pNetGame->GetMenuPool()) {
		pMenuPool->InitMenu(p);
	}
}

void RpcHandlers::OnShowMenu(const proto::ShowMenu& p)
{
	spdlog::debug("ScriptRPC: ScrShowMenu id={}", p.menuId);
	if (CMenuPool* pMenuPool = pNetGame->GetMenuPool()) {
		pMenuPool->ShowMenu(p.menuId);
	}
}

void RpcHandlers::OnHideMenu(const proto::HideMenu& p)
{
	spdlog::debug("ScriptRPC: ScrHideMenu id={}", p.menuId);
	if (CMenuPool* pMenuPool = pNetGame->GetMenuPool()) {
		pMenuPool->HideMenu(p.menuId);
	}
}
