#include "ObjectPool.h"
#include "NetGame.h"
#include "Proto/RpcOutbound.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/Aimstuff.h"
#include "../Game/Camera.h"
#include "../Game/Object.h"

#include "../Game/OobCollision.h"
#include "../Game/Util.h"
#include "../Game/Artwork/ArtworkCore.h"
#include "../Game/Artwork/ArtworkManager.h"
#include "../Java/EditObject.h"
#include "../UI/UI.h"
#include "../UI/UISettings.h"
#include <RakNet/RakClientInterface.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstdio>


CObjectPool::CObjectPool()
{
	for (OBJECTID ObjectID = 0; ObjectID < MAX_OBJECTS; ObjectID++) {
		m_bObjectSlotState[ObjectID] = false;
		m_pObjects[ObjectID] = nullptr;
	}
	m_iObjectCount = 0;
}

CObjectPool::~CObjectPool()
{
	Reset();
}

void CObjectPool::Reset()
{
	SetSelectState(false);
	for (OBJECTID ObjectID = 0; ObjectID < MAX_OBJECTS; ObjectID++) {
		Delete(ObjectID);
	}
	m_mapGtaPtrToID.clear();
	m_iObjectCount = 0;
	m_dwLastTick = 0;
	std::lock_guard<std::mutex> lock(m_touchMutex);
	m_pendingTouches.clear();
	m_cancelRectValid = false;
}

/**
 * @param ObjectID
 * @param iModel
 * @param vecPos
 * @param vecRot
 * @param fDrawDistance
 * @return
 */
bool CObjectPool::New(OBJECTID ObjectID, int iModel, sa::CVector vecPos, sa::CVector vecRot, float fDrawDistance)
{
	spdlog::info("obj id: {} modelid: {}", ObjectID, iModel);

	if (ObjectID >= MAX_OBJECTS) return false;

	if (m_pObjects[ObjectID] != nullptr) {
		Delete(ObjectID);
	}

	m_pObjects[ObjectID] = pGame->NewObject(iModel, vecPos, vecRot, fDrawDistance);
	if (!m_pObjects[ObjectID]) return false;

	// a null m_entity means the engine never created the object (object
	// pool exhausted by IPL placement objects, or the opcode failed) — an
	// active slot with no entity is a zombie every later RPC silently
	// no-ops on, so fail the New instead of marking it live
	if (!m_pObjects[ObjectID]->m_entity) {
		spdlog::warn("CObjectPool::New: engine created no entity for object {} (model {})", ObjectID, iModel);
		delete m_pObjects[ObjectID];
		m_pObjects[ObjectID] = nullptr;
		return false;
	}

	m_mapGtaPtrToID[m_pObjects[ObjectID]->m_entity] = ObjectID;
	m_bObjectSlotState[ObjectID] = true;
	m_iObjectCount++;
	return true;
}

void CObjectPool::RefreshCustomModels()
{
	int refreshed = 0;
	for (OBJECTID id = 0; id < MAX_OBJECTS; ++id) {
		if (!m_bObjectSlotState[id] || !m_pObjects[id]) {
			continue;
		}
		CObject* pObj = m_pObjects[id];
		pObj->RefreshCustomMaterials();
		const int serverModel = pObj->GetServerModelIndex();
		if (!artwork::isCustomObjectId(serverModel)) {
			continue;
		}
		if (artwork::isInternalCustomSlot(pObj->GetModelIndex())) {
			continue; // already upgraded
		}
		const int32_t slot = artwork::ArtworkManager::instance().ensureCustomObjectLoaded(serverModel);
		if (slot < 0) {
			continue;
		}
		sa::CEntity* oldEntity = pObj->m_entity;
		if (oldEntity) {
			m_mapGtaPtrToID.erase(oldEntity);
		}
		if (pObj->SwapGtaModel(slot) && pObj->m_entity) {
			m_mapGtaPtrToID[pObj->m_entity] = id;
			++refreshed;
		} else if (pObj->m_entity) {
			m_mapGtaPtrToID[pObj->m_entity] = id;
		}
	}
	if (refreshed > 0) {
		spdlog::info("CObjectPool: refreshed {} custom objects after artwork download", refreshed);
	}
}

void CObjectPool::RevertCustomModelsBeforeUnload()
{
	int reverted = 0;
	for (OBJECTID id = 0; id < MAX_OBJECTS; ++id) {
		if (!m_bObjectSlotState[id] || !m_pObjects[id]) {
			continue;
		}
		CObject* pObj = m_pObjects[id];
		pObj->ReleaseCustomMaterialsBeforeUnload();
		if (!artwork::isInternalCustomSlot(pObj->GetModelIndex())) {
			continue;
		}
		sa::CEntity* oldEntity = pObj->m_entity;
		if (oldEntity) {
			m_mapGtaPtrToID.erase(oldEntity);
		}
		if (pObj->SwapGtaModel(artwork::kFallbackObjectModelId) && pObj->m_entity) {
			m_mapGtaPtrToID[pObj->m_entity] = id;
		}
		++reverted;
	}
	if (reverted > 0) {
		spdlog::info("CObjectPool::RevertCustomModelsBeforeUnload: reverted {} custom object(s) to fallback", reverted);
	}
}

bool CObjectPool::Delete(OBJECTID ObjectID)
{
	if (ObjectID >= MAX_OBJECTS || !m_bObjectSlotState[ObjectID]) {
		return false;
	}

	CObject* pObject = m_pObjects[ObjectID];
	if (pObject) {
		// Detach any child objects attached to this parent before destroying its GTA entity
		for (OBJECTID childId = 0; childId < MAX_OBJECTS; ++childId) {
			if (childId != ObjectID && m_bObjectSlotState[childId] && m_pObjects[childId]
					&& m_pObjects[childId]->m_AttachedObjectID == ObjectID) {
				m_pObjects[childId]->Detach();
			}
		}
		if (EditObject::m_visible && EditObject::m_type == EditObject::TYPE_OBJECT
				&& EditObject::m_id == (int) ObjectID) {
			EditObject::cancelEdit();
		}
		if (pGame && pGame->GetCamera() && pGame->GetCamera()->m_pAttachedTo == pObject) {
			pGame->GetCamera()->Attach(nullptr);
		}
		if (pObject->m_entity) {
			m_mapGtaPtrToID.erase(pObject->m_entity);
		}
		delete pObject;
		m_pObjects[ObjectID] = nullptr;
	}
	m_bObjectSlotState[ObjectID] = false;
	m_iObjectCount--;

	return true;
}

OBJECTID CObjectPool::FindIDFromGtaPtr(sa::CEntity* pGtaObject)
{
	auto it = m_mapGtaPtrToID.find(pGtaObject);
	if (it != m_mapGtaPtrToID.end()) {
		OBJECTID id = it->second;
		// engine pool slots get recycled — the same GTA object pool serves
		// IPL placement objects, so a stale wrapper's m_entity can alias a
		// recycled entity. Re-validate the mapping and drop it when the
		// slot's owner moved on.
		if (id < MAX_OBJECTS && m_pObjects[id] && m_pObjects[id]->m_entity == pGtaObject) {
			return id;
		}
		m_mapGtaPtrToID.erase(it);
	}

	return INVALID_OBJECT_ID;
}

/**
 * @param pGtaObject
 * @return
 */
CObject* CObjectPool::FindObjectFromGtaPtr(sa::CEntity* pGtaObject)
{
	OBJECTID ObjectID = FindIDFromGtaPtr(pGtaObject);
	return GetAt(ObjectID);
}

void CObjectPool::Process()
{
	uint32_t dwThisTick = GetTickCount();
	if (m_dwLastTick == 0) {
		m_dwLastTick = dwThisTick;
	}

	float fElapsedTime = (dwThisTick - m_dwLastTick) / 1000.0f;
	if (!std::isfinite(fElapsedTime) || fElapsedTime < 0.0f) {
		fElapsedTime = 0.0f;
	}
	else if (fElapsedTime > 0.25f) {
		fElapsedTime = 0.25f;
	}

	for (OBJECTID i = 0; i < MAX_OBJECTS; i++) {
		if (m_bObjectSlotState[i] && m_pObjects[i]) {
			m_pObjects[i]->Process(fElapsedTime);
		}
	}

	// OOB vehicles need their colModel slots pinned (position-driven
	// CColStore streaming never matches them) — sector registration itself
	// is fully handled by the hooked CPhysical::Add/RemoveAndAdd now
	ProcessOobVehicleColPins();

	m_dwLastTick = dwThisTick;
}

void CObjectPool::ProcessMaterialText()
{
	for (OBJECTID ObjectID = 0; ObjectID < MAX_OBJECTS; ObjectID++) {
		if (m_pObjects[ObjectID] && m_bObjectSlotState[ObjectID]) {
			m_pObjects[ObjectID]->ProcessMaterialText();
		}
	}
}

void CObjectPool::SetSelectState(bool bEnable)
{
	m_bSelectState.store(bEnable, std::memory_order_release);
	if (!bEnable) {
		std::lock_guard<std::mutex> lock(m_touchMutex);
		m_pendingTouches.clear();
		m_cancelRectValid = false;
	}
	spdlog::info("CObjectPool::SetSelectState({})", bEnable ? "true" : "false");
}

bool CObjectPool::enqueueTouchEvent(int type, int x, int y)
{
	if (!m_bSelectState.load(std::memory_order_acquire)) {
		m_touchTracking = false;
		m_touchStartedOnCancel = false;
		return true;
	}

	auto hitCancelBtn = [&](int px, int py) -> bool {
		std::lock_guard<std::mutex> lock(m_touchMutex);
		if (!m_cancelRectValid) return false;
		return px >= m_cancelMinX && px <= m_cancelMaxX && py >= m_cancelMinY && py <= m_cancelMaxY;
	};

	if (type == 2) { // push
		m_touchTracking = true;
		m_touchStartX = x;
		m_touchStartY = y;
		m_touchMaxDragSq = 0;
		m_touchStartedOnCancel = hitCancelBtn(x, y);
		return !m_touchStartedOnCancel; // consume only if pressed on Cancel banner
	}
	if (type == 3) { // move
		if (m_touchTracking) {
			const int dx = x - m_touchStartX;
			const int dy = y - m_touchStartY;
			const int d2 = dx * dx + dy * dy;
			if (d2 > m_touchMaxDragSq) m_touchMaxDragSq = d2;
		}
		return !m_touchStartedOnCancel;
	}
	if (type == 1) { // pop
		const bool wasTracking = m_touchTracking;
		const bool startedCancel = m_touchStartedOnCancel;
		const int maxDragSq = m_touchMaxDragSq;
		m_touchTracking = false;
		m_touchStartedOnCancel = false;

		if (startedCancel) {
			if (hitCancelBtn(x, y)) {
				std::lock_guard<std::mutex> lock(m_touchMutex);
				if (m_pendingTouches.size() >= 32) m_pendingTouches.pop_front();
				m_pendingTouches.push_back({type, x, y, true});
			}
			return false;
		}

		// Short tap (< 36px travel) picks object under finger; longer drags pass to GTA for camera orbit.
		constexpr int kMaxTapDragSq = 36 * 36;
		if (wasTracking && maxDragSq <= kMaxTapDragSq) {
			std::lock_guard<std::mutex> lock(m_touchMutex);
			if (m_pendingTouches.size() >= 32) m_pendingTouches.pop_front();
			m_pendingTouches.push_back({type, x, y, false});
		}
		return true;
	}

	return true;
}

void CObjectPool::processPendingTouches()
{
	std::deque<PendingTouch> queue;
	{
		std::lock_guard<std::mutex> lock(m_touchMutex);
		if (m_pendingTouches.empty()) return;
		queue.swap(m_pendingTouches);
	}

	if (!m_bSelectState.load(std::memory_order_acquire)) {
		return;
	}

	for (const auto& ev : queue) {
		if (!m_bSelectState.load(std::memory_order_acquire)) break;
		if (ev.isCancelBtn) {
			SetSelectState(false);
			break;
		}
		const OBJECTID picked = PickObjectAtScreenPos(static_cast<float>(ev.x), static_cast<float>(ev.y));
		if (picked != INVALID_OBJECT_ID) {
			SendSelectObject(picked);
		}
	}
}

OBJECTID CObjectPool::PickObjectAtScreenPos(float tapX, float tapY)
{
	if (!pGame || !pGame->GetCamera()) {
		return INVALID_OBJECT_ID;
	}

	constexpr float kMaxDistance = 150.0f;

	// Build exact 3D ray through (tapX, tapY) using camera aim + CalcScreenCoors calibration
	if (CAMERA_AIM* pCam = GameGetInternalAim()) {
		const float fLenSq = pCam->f1x * pCam->f1x + pCam->f1y * pCam->f1y + pCam->f1z * pCam->f1z;
		if (fLenSq > 1e-6f) {
			const float invF = 1.0f / std::sqrt(fLenSq);
			const RwV3d origin{pCam->pos1x, pCam->pos1y, pCam->pos1z};
			const RwV3d fwd{pCam->f1x * invF, pCam->f1y * invF, pCam->f1z * invF};

			// Build orthonormal right/up basis perpendicular to fwd
			RwV3d right{fwd.y, -fwd.x, 0.0f};
			const float rLenSq = right.x * right.x + right.y * right.y;
			if (rLenSq > 1e-5f) {
				const float invR = 1.0f / std::sqrt(rLenSq);
				right.x *= invR;
				right.y *= invR;
			}
			else {
				right = {1.0f, 0.0f, 0.0f};
			}
			const RwV3d up{
				right.y * fwd.z - right.z * fwd.y,
				right.z * fwd.x - right.x * fwd.z,
				right.x * fwd.y - right.y * fwd.x
			};

			constexpr float kCalibDepth = 10.0f;
			const RwV3d pCenter{origin.x + fwd.x * kCalibDepth, origin.y + fwd.y * kCalibDepth, origin.z + fwd.z * kCalibDepth};
			const RwV3d pRight{pCenter.x + right.x, pCenter.y + right.y, pCenter.z + right.z};
			const RwV3d pUp{pCenter.x + up.x, pCenter.y + up.y, pCenter.z + up.z};

			RwV3d sCenter{}, sRight{}, sUp{};
			CalcScreenCoors(&pCenter, &sCenter, 0.0f, 0.0f, false, false);
			CalcScreenCoors(&pRight, &sRight, 0.0f, 0.0f, false, false);
			CalcScreenCoors(&pUp, &sUp, 0.0f, 0.0f, false, false);

			const float a11 = sRight.x - sCenter.x;
			const float a12 = sUp.x - sCenter.x;
			const float a21 = sRight.y - sCenter.y;
			const float a22 = sUp.y - sCenter.y;
			const float det = a11 * a22 - a12 * a21;
			if (sCenter.z > 0.1f && std::fabs(det) > 1e-4f) {
				const float bx = tapX - sCenter.x;
				const float by = tapY - sCenter.y;
				const float uR = (a22 * bx - a12 * by) / det;
				const float uU = (-a21 * bx + a11 * by) / det;

				float dx = fwd.x * kCalibDepth + right.x * uR + up.x * uU;
				float dy = fwd.y * kCalibDepth + right.y * uR + up.y * uU;
				float dz = fwd.z * kCalibDepth + right.z * uR + up.z * uU;
				const float dLen = std::sqrt(dx * dx + dy * dy + dz * dz);
				if (dLen > 1e-5f) {
					dx /= dLen;
					dy /= dLen;
					dz /= dLen;

					sa::CVector rayStart{origin.x + dx * 0.5f, origin.y + dy * 0.5f, origin.z + dz * 0.5f};
					sa::CVector rayEnd{origin.x + dx * kMaxDistance, origin.y + dy * kMaxDistance, origin.z + dz * kMaxDistance};
					sa::CColPoint colPoint{};
					sa::CEntity* hitEntity = nullptr;
					ProcessLineOfSight(&rayStart, &rayEnd, &colPoint, &hitEntity,
						false, false, 0, true, false, false, false, false);
					if (hitEntity) {
						const OBJECTID hitId = FindIDFromGtaPtr(hitEntity);
						if (hitId != INVALID_OBJECT_ID) {
							return hitId;
						}
					}
				}
			}
		}
	}

	// Screen-space projection fallback (covers objects with small/hollow collision or slight tap offset)
	const ImVec2 disp = pUI ? pUI->size() : ImVec2(static_cast<float>(sa::RsGlobal->screenWidth), static_cast<float>(sa::RsGlobal->screenHeight));
	const float baseTol = std::max(48.0f, std::min(disp.x, disp.y) * 0.085f);

	OBJECTID bestId = INVALID_OBJECT_ID;
	float bestScore = 1e30f;

	for (OBJECTID id = 0; id < MAX_OBJECTS; ++id) {
		if (!m_bObjectSlotState[id] || !m_pObjects[id]) continue;
		CObject* pObj = m_pObjects[id];
		if (!pObj->m_gtaId) continue;
		sa::CEntity* liveEnt = GamePool_Object_GetAt(pObj->m_gtaId);
		if (!liveEnt || liveEnt != pObj->m_entity) continue;

		const RwMatrix mat = liveEnt->GetMatrix().ToRwMatrix();
		const RwV3d worldPos{mat.pos.x, mat.pos.y, mat.pos.z};
		RwV3d scrPos{};
		CalcScreenCoors(&worldPos, &scrPos, 0.0f, 0.0f, false, false);
		if (scrPos.z <= 0.5f || scrPos.z > kMaxDistance) continue;

		float colR = GetModelColSphereRadius(pObj->GetModelIndex());
		if (!std::isfinite(colR) || colR < 0.5f) colR = 0.75f;
		if (colR > 8.0f) colR = 8.0f;
		const float projR = (colR / scrPos.z) * disp.y * 0.65f;
		const float tol = std::max(baseTol, std::min(projR * 1.35f, disp.y * 0.25f));

		const float dx = scrPos.x - tapX;
		const float dy = scrPos.y - tapY;
		const float dist2d = std::sqrt(dx * dx + dy * dy);
		if (dist2d <= tol) {
			// Prefer tighter 2D hit; slightly favor closer depth on ties
			const float score = dist2d + scrPos.z * 0.15f;
			if (score < bestScore) {
				bestScore = score;
				bestId = id;
			}
		}
	}

	return bestId;
}

void CObjectPool::SendSelectObject(OBJECTID objectId)
{
	if (!pNetGame || !pNetGame->GetRakClient()) return;
	CObject* pObj = GetAt(objectId);
	if (!pObj || !pObj->m_gtaId) return;
	sa::CEntity* liveEnt = GamePool_Object_GetAt(pObj->m_gtaId);
	if (!liveEnt || liveEnt != pObj->m_entity) return;

	const RwMatrix mat = liveEnt->GetMatrix().ToRwMatrix();
	proto::SelectObjectReply reply{};
	reply.selectType = 0;
	reply.objectId = objectId;
	reply.modelId = static_cast<uint32_t>(pObj->GetServerModelIndex());
	reply.position = {mat.pos.x, mat.pos.y, mat.pos.z};

	RakNet::BitStream bs;
	proto::Writer writer(bs);
	reply.serialize(writer, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->RPC(&RPC_EnterEditObject, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, false, UNASSIGNED_NETWORK_ID, nullptr);

	spdlog::info("CObjectPool: selected object id={} model={} pos=({:.2f}, {:.2f}, {:.2f})",
		objectId, reply.modelId, reply.position.x, reply.position.y, reply.position.z);
}

void CObjectPool::DrawSelectOverlay(ImGuiRenderer* renderer)
{
	if (!renderer || !m_bSelectState.load(std::memory_order_acquire)) {
		return;
	}

	const ImVec2 disp = pUI ? pUI->size() : ImVec2(static_cast<float>(sa::RsGlobal->screenWidth), static_cast<float>(sa::RsGlobal->screenHeight));
	const float fontSize = std::max(16.0f, disp.y * 0.026f);

	// 1) Subtle markers on nearby selectable objects so touch users can identify targets
	int drawn = 0;
	for (OBJECTID id = 0; id < MAX_OBJECTS && drawn < 24; ++id) {
		if (!m_bObjectSlotState[id] || !m_pObjects[id]) continue;
		CObject* pObj = m_pObjects[id];
		if (!pObj->m_gtaId) continue;
		sa::CEntity* liveEnt = GamePool_Object_GetAt(pObj->m_gtaId);
		if (!liveEnt || liveEnt != pObj->m_entity) continue;

		const RwMatrix mat = liveEnt->GetMatrix().ToRwMatrix();
		const RwV3d worldPos{mat.pos.x, mat.pos.y, mat.pos.z};
		RwV3d scrPos{};
		CalcScreenCoors(&worldPos, &scrPos, 0.0f, 0.0f, false, false);
		if (scrPos.z <= 0.5f || scrPos.z > 80.0f) continue;
		if (scrPos.x < 0.0f || scrPos.x > disp.x || scrPos.y < 0.0f || scrPos.y > disp.y) continue;

		char tag[48];
		std::snprintf(tag, sizeof(tag), "#%u (%d)", static_cast<unsigned>(id), pObj->GetServerModelIndex());
		const ImVec2 tsz = renderer->calculateTextSize(tag, fontSize * 0.85f);
		const ImVec2 boxMin(scrPos.x - tsz.x * 0.5f - 6.0f, scrPos.y - tsz.y * 0.5f - 3.0f);
		const ImVec2 boxMax(scrPos.x + tsz.x * 0.5f + 6.0f, scrPos.y + tsz.y * 0.5f + 3.0f);
		renderer->drawRect(boxMin, boxMax, ImColor(18, 28, 42, 180), true);
		renderer->drawRect(boxMin, boxMax, ImColor(90, 190, 255, 220), false, 1.5f);
		renderer->drawText(ImVec2(boxMin.x + 6.0f, boxMin.y + 3.0f), ImColor(230, 245, 255, 255), tag, true, fontSize * 0.85f);
		++drawn;
	}

	// 2) Top status bar + Cancel button
	const std::string hint = "Select Object: tap an object";
	const std::string cancelLabel = "Cancel";
	const ImVec2 hintSz = renderer->calculateTextSize(hint, fontSize);
	const ImVec2 cancelSz = renderer->calculateTextSize(cancelLabel, fontSize);

	const float padX = fontSize * 0.7f;
	const float padY = fontSize * 0.4f;
	const float gap = fontSize * 0.8f;
	const float btnW = cancelSz.x + padX * 2.0f;
	const float btnH = cancelSz.y + padY * 1.4f;
	const float barW = padX + hintSz.x + gap + btnW + padX;
	const float barH = std::max(hintSz.y, btnH) + padY * 1.2f;
	const float barX = (disp.x - barW) * 0.5f;
	const float barY = disp.y * 0.03f;

	renderer->drawRect(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), ImColor(16, 20, 28, 215), true);
	renderer->drawRect(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), ImColor(90, 190, 255, 220), false, 1.5f);
	renderer->drawText(ImVec2(barX + padX, barY + (barH - hintSz.y) * 0.5f), ImColor(240, 245, 255, 255), hint, true, fontSize);

	const float btnX = barX + padX + hintSz.x + gap;
	const float btnY = barY + (barH - btnH) * 0.5f;
	renderer->drawRect(ImVec2(btnX, btnY), ImVec2(btnX + btnW, btnY + btnH), ImColor(185, 52, 52, 235), true);
	renderer->drawRect(ImVec2(btnX, btnY), ImVec2(btnX + btnW, btnY + btnH), ImColor(255, 160, 160, 220), false, 1.0f);
	renderer->drawText(ImVec2(btnX + padX, btnY + (btnH - cancelSz.y) * 0.5f), ImColor(255, 255, 255, 255), cancelLabel, true, fontSize);

	{
		std::lock_guard<std::mutex> lock(m_touchMutex);
		m_cancelMinX = btnX - 8.0f;
		m_cancelMinY = btnY - 8.0f;
		m_cancelMaxX = btnX + btnW + 8.0f;
		m_cancelMaxY = btnY + btnH + 8.0f;
		m_cancelRectValid = true;
	}
}
