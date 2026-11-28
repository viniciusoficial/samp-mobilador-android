//
// Created by ALYN on 25-Jan-25.
//

#include "EditObject.h"
#include "../Client.h"
#include "../Java/Java.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/Chat.h"
#include "../Game/Game.h"
#include "../Game/Object.h"
#include "../Game/PlayerPed.h"
#include "../Game/Util.h"
#include "../Net/NetGame.h"
#include "../Net/LocalPlayer.h"
#include "../Net/Proto/RpcOutbound.h"

#include <RakNet/RakClientInterface.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <spdlog/spdlog.h>


namespace {
bool g_hasOrigSnapshot = false;
sa::CVector g_origObjPos{};
sa::CVector g_origObjRot{};
NEW_ATTACHED_OBJECT g_origAttach{};
}

void EditObject::startEditAttachedObject(int slot)
{
	if (!pNetGame || !pNetGame->GetPlayerPool() || !pNetGame->GetPlayerPool()->GetLocalPlayer()) return;

	CPlayerPed* pPlayerPed = pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed();
	if (!pPlayerPed || !pPlayerPed->IsValidAttach(slot)) {
		if (pUI) pUI->chat()->addDebugMessage("Invalid attach slot %d", slot);
		return;
	}

	g_origAttach = pPlayerPed->m_attachedObjects[slot].info;
	g_hasOrigSnapshot = true;

	s_sessionGen.fetch_add(1, std::memory_order_acq_rel);
	EditObject::m_id = slot;
	EditObject::m_type = TYPE_ATTACHED_OBJECT;
	EditObject::showGui(true);
}

void EditObject::startEditObject(uint16_t objectId)
{
	if (!pNetGame || !pNetGame->GetObjectPool()) return;

	CObject* pObject = pNetGame->GetObjectPool()->GetAt(objectId);
	if (!pObject) {
		if (pUI) pUI->chat()->addDebugMessage("Invalid object id %d", objectId);
		return;
	}

	g_hasOrigSnapshot = false;
	if (pObject->m_entity) {
		g_origObjPos = pObject->m_entity->GetPosition();
		pObject->GetRotation(&g_origObjRot.x, &g_origObjRot.y, &g_origObjRot.z);
		g_hasOrigSnapshot = true;
	}

	s_sessionGen.fetch_add(1, std::memory_order_acq_rel);
	EditObject::m_id = objectId;
	EditObject::m_type = TYPE_OBJECT;
	EditObject::showGui(true);
}

void EditObject::showGui(bool show)
{
	if (g_java) {
		g_java->showEditObject(show);
	}
	m_visible = show;
}

void EditObject::cancelEdit()
{
	s_sessionGen.fetch_add(1, std::memory_order_acq_rel);
	g_hasOrigSnapshot = false;
	EditObject::m_id = -1;
	EditObject::m_type = TYPE_NONE;
	EditObject::showGui(false);
}

void EditObject::exitEdit()
{
	s_sessionGen.fetch_add(1, std::memory_order_acq_rel);
	applyExit();
	showGui(false);
}

/**
 * SPSC ring — the Java UI thread is the only producer. Drops the newest
 * op when full: a lost +/-0.005 click is harmless, a lost save is not —
 * but a flooded queue only happens if the game thread stalls, in which
 * case the user can't save anyway.
 */
void EditObject::enqueueOp(int kind, int button, bool state)
{
	uint32_t uPush = s_opPush.load(std::memory_order_relaxed);
	uint32_t uPop = s_opPop.load(std::memory_order_acquire);
	// Reserve 2 slots so OP_SAVE / OP_EXIT are never dropped during a +/- click burst.
	const uint32_t cap = (kind == OP_CLICK) ? (OP_CAPACITY - 2) : OP_CAPACITY;
	if (uPush - uPop >= cap) {
		return;
	}
	const uint32_t gen = s_sessionGen.load(std::memory_order_acquire);
	s_ops[uPush % OP_CAPACITY] = {(uint8_t) kind, (int32_t) button, (uint8_t) state, gen};
	s_opPush.store(uPush + 1, std::memory_order_release);
}

void EditObject::processPending()
{
	for (;;) {
		uint32_t uPop = s_opPop.load(std::memory_order_relaxed);
		if (uPop == s_opPush.load(std::memory_order_acquire)) {
			return;
		}
		PendingOp op = s_ops[uPop % OP_CAPACITY];
		s_opPop.store(uPop + 1, std::memory_order_release);

		// Discard stale ops queued from a prior edit session
		if (op.sessionGen != s_sessionGen.load(std::memory_order_acquire)) {
			continue;
		}

		switch (op.kind) {
			case OP_CLICK: applyClick(op.button, op.state != 0); break;
			case OP_EXIT:  applyExit(); break;
			case OP_SAVE:  applySave(); break;
		}
	}
}

void EditObject::applyClick(int button, bool state)
{
	if (!pNetGame || !pNetGame->GetPlayerPool() || !pNetGame->GetPlayerPool()->GetLocalPlayer()) {
		return;
	}

	CPlayerPed* pPlayer = pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed();
	if (!pPlayer) {
		return;
	}

	if (EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT) {
		if (!pPlayer->IsValidAttach(EditObject::m_id)) {
			EditObject::cancelEdit();
			return;
		}
	}

	CObject* pObject = nullptr;

	if (EditObject::m_type == EditObject::TYPE_OBJECT) {
		if (!pNetGame->GetObjectPool() || EditObject::m_id < 0) {
			EditObject::cancelEdit();
			return;
		}
		pObject = pNetGame->GetObjectPool()->GetAt(EditObject::m_id);
		if (!pObject || !pObject->m_entity) {
			if (pUI) pUI->chat()->addDebugMessage("Invalid object id %d", EditObject::m_id);
			EditObject::cancelEdit();
			return;
		}
	}

	NEW_ATTACHED_OBJECT* pAttach = nullptr;
	if (EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT) {
		if (EditObject::m_id < 0 || EditObject::m_id >= 10) {
			EditObject::cancelEdit();
			return;
		}
		pAttach = &pPlayer->m_attachedObjects[EditObject::m_id].info;
	}

	int BUTTON_LEFT_RIGHT = 0;
	int BUTTON_UP_DOWN = 1;
	int BUTTON_PUSH_PULL = 2;
	int BUTTON_SCALE = 3;
	int BUTTON_ROT_X = 4;
	int BUTTON_ROT_Y = 5;
	int BUTTON_ROT_Z = 6;

	float value = state ? 0.005f : -0.005f;
	float rotValue = state ? 1.0f : -1.0f;

	if (button == BUTTON_LEFT_RIGHT) {
		if (EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT) {
			pAttach->vecOffset.x += value;
		}
		if (EditObject::m_type == EditObject::TYPE_OBJECT) {
			RwMatrix matrix = pObject->m_entity->GetMatrix().ToRwMatrix();
			matrix.pos.x += value;
//			pObject->SetMatrix(matrix);
			pObject->TeleportTo(matrix.pos.x, matrix.pos.y, matrix.pos.z);
		}
	}
	if (button == BUTTON_UP_DOWN) {
		if (EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT) {
			pAttach->vecOffset.y += value;
		}
		if (EditObject::m_type == EditObject::TYPE_OBJECT) {
			RwMatrix matrix = pObject->m_entity->GetMatrix().ToRwMatrix();
			matrix.pos.y += value;
//			pObject->SetMatrix(matrix);
			pObject->TeleportTo(matrix.pos.x, matrix.pos.y, matrix.pos.z);
		}
	}
	if (button == BUTTON_PUSH_PULL) {
		if (EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT) {
			pAttach->vecOffset.z += value;
		}
		if (EditObject::m_type == EditObject::TYPE_OBJECT) {
			RwMatrix matrix = pObject->m_entity->GetMatrix().ToRwMatrix();
			matrix.pos.z += value;
//			pObject->SetMatrix(matrix);
			pObject->TeleportTo(matrix.pos.x, matrix.pos.y, matrix.pos.z);
		}
	}
	if (button == BUTTON_SCALE) {
		if (EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT) {
			pAttach->vecScale.x = std::max(0.05f, pAttach->vecScale.x + value);
			pAttach->vecScale.y = std::max(0.05f, pAttach->vecScale.y + value);
			pAttach->vecScale.z = std::max(0.05f, pAttach->vecScale.z + value);
		}
		if (EditObject::m_type == EditObject::TYPE_OBJECT) {
			sa::CVector vecScale = pObject->GetScale();
			vecScale.x = std::max(0.05f, vecScale.x + value);
			vecScale.y = std::max(0.05f, vecScale.y + value);
			vecScale.z = std::max(0.05f, vecScale.z + value);
			pObject->SetScale(vecScale);
		}
	}
	if (button == BUTTON_ROT_X) {
		if (EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT) {
			pAttach->vecRot.x += rotValue;
		}
		if (EditObject::m_type == EditObject::TYPE_OBJECT) {
			float x, y, z;
			pObject->GetRotation(&x, &y, &z);
			x += rotValue;
			sa::CVector vRot = {x, y, z};
			pObject->SetRotation(&vRot);
		}
	}
	if (button == BUTTON_ROT_Y) {
		if (EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT) {
			pAttach->vecRot.y += rotValue;
		}
		if (EditObject::m_type == EditObject::TYPE_OBJECT) {
			float x, y, z;
			pObject->GetRotation(&x, &y, &z);
			y += rotValue;
			sa::CVector vRot = {x, y, z};
			pObject->SetRotation(&vRot);
		}
	}
	if (button == BUTTON_ROT_Z) {
		if (EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT) {
			pAttach->vecRot.z += rotValue;
		}
		if (EditObject::m_type == EditObject::TYPE_OBJECT) {
			float x, y, z;
			pObject->GetRotation(&x, &y, &z);
			z += rotValue;
			sa::CVector vRot = {x, y, z};
			pObject->SetRotation(&vRot);
		}
	}

	// update object
	if (pNetGame && pNetGame->GetRakClient() && EditObject::m_type == EditObject::TYPE_OBJECT) {
		if (GetTickCount() - EditObject::m_lastUpdate > 500) {
			RwMatrix matrix = pObject->m_entity->GetMatrix().ToRwMatrix();
			sa::CVector rot;
			pObject->GetRotation(&rot.x, &rot.y, &rot.z);

			RakNet::BitStream bsSend;
			proto::Writer writer(bsSend);
			proto::EditObjectReply reply{};
			reply.playerObject = EditObject::m_playerObj;
			reply.objectId = static_cast<uint16_t>(EditObject::m_id);
			reply.response = static_cast<uint32_t>(EditObject::eResponse::EDIT_RESPONSE_UPDATE);
			reply.position = {matrix.pos.x, matrix.pos.y, matrix.pos.z};
			reply.rotation = {rot.x, rot.y, rot.z};
			reply.serialize(writer, proto::ProtoVersion::V037);
			pNetGame->GetRakClient()->RPC(&RPC_EditObject, &bsSend, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);

			EditObject::m_lastUpdate = GetTickCount();
		}
	}
}

void EditObject::applyExit()
{
	if (pNetGame && pNetGame->GetRakClient()) {
		if (EditObject::m_type == EditObject::TYPE_OBJECT && EditObject::m_id >= 0 &&
				pNetGame->GetObjectPool()) {
			CObject* pObject = pNetGame->GetObjectPool()->GetAt(EditObject::m_id);
			if (pObject && pObject->m_entity) {
				if (g_hasOrigSnapshot) {
					pObject->TeleportTo(g_origObjPos.x, g_origObjPos.y, g_origObjPos.z);
					pObject->SetRotation(&g_origObjRot);
				}
				RwMatrix matrix = pObject->m_entity->GetMatrix().ToRwMatrix();
				sa::CVector rot;
				pObject->GetRotation(&rot.x, &rot.y, &rot.z);

				RakNet::BitStream bsSend;
				proto::Writer writer(bsSend);
				proto::EditObjectReply reply{};
				reply.playerObject = EditObject::m_playerObj;
				reply.objectId = static_cast<uint16_t>(EditObject::m_id);
				reply.response = static_cast<uint32_t>(EditObject::eResponse::EDIT_RESPONSE_CANCEL);
				reply.position = {matrix.pos.x, matrix.pos.y, matrix.pos.z};
				reply.rotation = {rot.x, rot.y, rot.z};
				reply.serialize(writer, proto::ProtoVersion::V037);
				pNetGame->GetRakClient()->RPC(&RPC_EditObject, &bsSend, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
			}
		}
		else if (EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT &&
				EditObject::m_id >= 0 && EditObject::m_id < 10 &&
				pNetGame->GetPlayerPool() && pNetGame->GetPlayerPool()->GetLocalPlayer()) {
			CPlayerPed* pPlayer = pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed();
			if (pPlayer && pPlayer->IsValidAttach(EditObject::m_id)) {
				NEW_ATTACHED_OBJECT* pAttach = &pPlayer->m_attachedObjects[EditObject::m_id].info;
				if (g_hasOrigSnapshot) {
					*pAttach = g_origAttach;
				}

				RakNet::BitStream bsSend;
				proto::Writer writer(bsSend);
				proto::EditAttachedObjectReply reply{};
				reply.response = static_cast<uint32_t>(EditObject::eResponse::EDIT_RESPONSE_CANCEL);
				reply.index = EditObject::m_id;
				reply.modelId = pAttach->iModel;
				reply.boneId = pAttach->iBoneID;
				reply.offset = {pAttach->vecOffset.x, pAttach->vecOffset.y, pAttach->vecOffset.z};
				reply.rotation = {pAttach->vecRot.x, pAttach->vecRot.y, pAttach->vecRot.z};
				reply.scale = {pAttach->vecScale.x, pAttach->vecScale.y, pAttach->vecScale.z};
				reply.color1 = pAttach->dwMaterialColor1;
				reply.color2 = pAttach->dwMaterialColor2;
				reply.serialize(writer, proto::ProtoVersion::V037);
				pNetGame->GetRakClient()->RPC(&RPC_EditAttachedObject, &bsSend, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
			}
		}
	}
	g_hasOrigSnapshot = false;
	EditObject::m_id = -1;
	EditObject::m_type = TYPE_NONE;
	EditObject::m_visible = false;
}

void EditObject::applySave()
{
	if (pNetGame && pNetGame->GetRakClient()) {
		if (EditObject::m_type == EditObject::TYPE_ATTACHED_OBJECT) {
			if (pNetGame->GetPlayerPool() && pNetGame->GetPlayerPool()->GetLocalPlayer() &&
					EditObject::m_id >= 0 && EditObject::m_id < 10) {
				CPlayerPed* pPlayer = pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed();
				if (pPlayer && pPlayer->IsValidAttach(EditObject::m_id)) {
					auto& attach = pPlayer->m_attachedObjects[EditObject::m_id].info;

					RakNet::BitStream bsSend;
					proto::Writer writer(bsSend);
					proto::EditAttachedObjectReply reply{};
					reply.response = static_cast<uint32_t>(EditObject::eResponse::EDIT_RESPONSE_FINAL);
					reply.index = EditObject::m_id;
					reply.modelId = attach.iModel;
					reply.boneId = attach.iBoneID;
					reply.offset = {attach.vecOffset.x, attach.vecOffset.y, attach.vecOffset.z};
					reply.rotation = {attach.vecRot.x, attach.vecRot.y, attach.vecRot.z};
					reply.scale = {attach.vecScale.x, attach.vecScale.y, attach.vecScale.z};
					reply.color1 = attach.dwMaterialColor1;
					reply.color2 = attach.dwMaterialColor2;
					reply.serialize(writer, proto::ProtoVersion::V037);
					pNetGame->GetRakClient()->RPC(&RPC_EditAttachedObject, &bsSend, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
				}
			}
		}
		else if (EditObject::m_type == EditObject::TYPE_OBJECT) {
			if (pNetGame->GetObjectPool() && EditObject::m_id >= 0) {
				CObject* pObject = pNetGame->GetObjectPool()->GetAt(EditObject::m_id);
				if (!pObject || !pObject->m_entity) {
					if (pUI) pUI->chat()->addDebugMessage("Invalid object id %d", EditObject::m_id);
				}
				else {
					RwMatrix matrix = pObject->m_entity->GetMatrix().ToRwMatrix();
					sa::CVector rot;
					pObject->GetRotation(&rot.x, &rot.y, &rot.z);

					RakNet::BitStream bsSend;
					proto::Writer writer(bsSend);
					proto::EditObjectReply reply{};
					reply.playerObject = EditObject::m_playerObj;
					reply.objectId = static_cast<uint16_t>(EditObject::m_id);
					reply.response = static_cast<uint32_t>(EditObject::eResponse::EDIT_RESPONSE_FINAL);
					reply.position = {matrix.pos.x, matrix.pos.y, matrix.pos.z};
					reply.rotation = {rot.x, rot.y, rot.z};
					reply.serialize(writer, proto::ProtoVersion::V037);
					pNetGame->GetRakClient()->RPC(&RPC_EditObject, &bsSend, HIGH_PRIORITY, RELIABLE, 0, false, UNASSIGNED_NETWORK_ID, nullptr);
				}
			}
		}
	}
	g_hasOrigSnapshot = false;
	EditObject::m_id = -1;
	EditObject::m_type = TYPE_NONE;
	EditObject::m_visible = false;
}

/**
 * JNI entry points — Java UI thread. They only queue work + flip the GUI
 * flag; every GTA/entity/RPC access happens inside processPending() on
 * the game thread (the callbacks used to call TeleportTo -> ScriptCommand
 * straight off the UI thread, racing the shared ScriptBuf).
 */
extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_ui_widgets_EditObject_Click(JNIEnv* env, jobject thiz, jint button, jboolean state)
{
	EditObject::enqueueOp(EditObject::OP_CLICK, (int) button, state == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_ui_widgets_EditObject_Exit(JNIEnv* env, jobject thiz)
{
	EditObject::enqueueOp(EditObject::OP_EXIT, 0, false);
}

extern "C" JNIEXPORT void JNICALL Java_com_sampmobile_game_ui_widgets_EditObject_Save(JNIEnv* env, jobject thiz)
{
	EditObject::enqueueOp(EditObject::OP_SAVE, 0, false);
}
