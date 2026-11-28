//
// Created by ALYN on 2024/8/15.
//

#include "Camera.h"
#include "Game.h"
#include "Object.h"
#include "Scripting.h"
#include "Util.h"
#include "Engine/Symbols.h"
#include "../Client.h"
#include <SA.h>

CCamera::CCamera()
{
	sa::CCamera& TheCamera = sa::getTheCamera();
	m_matPos = reinterpret_cast<RwMatrix*>(&TheCamera.m_mCameraMatrix);
	m_pAttachedTo = nullptr;
}

void CCamera::SetBehindPlayer()
{
	m_pAttachedTo = nullptr;
	ScriptCommand(&lock_camera_position, 0);
	ScriptCommand(&lock_camera_target_point, 0);
	ScriptCommand(&restore_camera_to_user);
	ScriptCommand(&set_camera_behind_player);
	ScriptCommand(&restore_camera_jumpcut);
}

void CCamera::SetPosition(float fX, float fY, float fZ, float fRotationX, float fRotationY, float fRotationZ)
{
	m_pAttachedTo = nullptr;
	ScriptCommand(&lock_camera_position, 0);
	ScriptCommand(&lock_camera_target_point, 0);
	ScriptCommand(&restore_camera_to_user);
	ScriptCommand(&set_camera_position, fX, fY, fZ, fRotationX, fRotationY, fRotationZ);
}

void CCamera::LookAtPoint(float fX, float fY, float fZ, int iType)
{
	m_pAttachedTo = nullptr;
	ScriptCommand(&lock_camera_position, 0);
	ScriptCommand(&lock_camera_target_point, 0);
	ScriptCommand(&restore_camera_to_user);
	ScriptCommand(&point_camera, fX, fY, fZ, iType);
}

/**
 * @param vecFrom
 * @param vecTo
 * @param iTime
 * @param byteMode
 */
void CCamera::InterpolateCameraPos(sa::CVector* vecFrom, sa::CVector* vecTo, int iTime, uint8_t byteMode)
{
	ScriptCommand(&restore_camera_to_user);
	ScriptCommand(&lock_camera_position, 1);
	ScriptCommand(&set_camera_pos_time_smooth, vecFrom->x, vecFrom->y, vecFrom->z, vecTo->x, vecTo->y, vecTo->z, iTime, byteMode);
}

/**
 * @param vecFrom
 * @param vecTo
 * @param iTime
 * @param byteMode
 */
void CCamera::InterpolateCameraLookAt(sa::CVector* vecFrom, sa::CVector* vecTo, int iTime, uint8_t byteMode)
{
	ScriptCommand(&lock_camera_target_point, 1);
	ScriptCommand(&point_camera_transverse, vecFrom->x, vecFrom->y, vecFrom->z, vecTo->x, vecTo->y, vecTo->z, iTime, byteMode);
}

void CCamera::Restore()
{
	m_pAttachedTo = nullptr;
	ScriptCommand(&lock_camera_position, 0);
	ScriptCommand(&lock_camera_target_point, 0);
	ScriptCommand(&restore_camera_to_user);
	ScriptCommand(&restore_camera_jumpcut);
}

void CCamera::Attach(CObject* pObject)
{
	m_pAttachedTo = pObject;
	SetToOwner();
}

void CCamera::SetToOwner()
{
	if (!m_pAttachedTo) {
		return;
	}

	// re-verify the entity owns its pool slot — the engine recycles object
	// slots and reading the matrix of a recycled/freed entity is a UAF
	sa::CPhysical* pEntity = m_pAttachedTo->m_gtaId
			? (sa::CPhysical*) GamePool_Object_GetAt(m_pAttachedTo->m_gtaId) : nullptr;
	if (!pEntity || pEntity != m_pAttachedTo->m_entity) {
		m_pAttachedTo = nullptr;
		return;
	}

	RwMatrix mat = pEntity->GetMatrix().ToRwMatrix();
	sa::CVector vecPos = {mat.pos.x, mat.pos.y, mat.pos.z};

	if (vecPos.x < 20000.0f && vecPos.x > -20000.0f &&
			vecPos.y < 20000.0f && vecPos.y > -20000.0f &&
			vecPos.z < 100000.0f && vecPos.z > -10000.0f) {
		InterpolateCameraPos(&vecPos, &vecPos, 100, 1);
	}
}

void CCamera::GetMatrix(RwMatrix* Matrix)
{
	*Matrix = *m_matPos;
}
