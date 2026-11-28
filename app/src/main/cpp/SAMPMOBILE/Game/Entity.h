#pragma once

#include <cstdint>
#include <sys/types.h>
#include <Core/Vector.h>
#include <RenderWare/rw.h>

namespace sa {
class CPhysical;
}

class CEntity {
public:
	CEntity() = default;
	virtual ~CEntity() = default;

	virtual void Add();
	virtual void Remove();
	virtual void TeleportTo(float x, float y, float z);

	bool IsAdded();

	uint GetModelIndex();
	void SetModelIndex(uint uiModel);

	float GetDistanceFromLocalPlayerPed();
	float GetDistanceFromCamera();
	float GetDistanceFromPoint(sa::CVector Vector);

	bool GetCollisionChecking();
	void SetCollisionChecking(bool bCheck);

	void SetGravityProcessing(bool state);

	virtual void UpdateMatrix(RwMatrix mat);
	void UpdateRwMatrixAndFrame();

	void Render();

	bool EnforceWorldBoundries(float fPX, float fZX, float fPY, float fNY);
	bool HasExceededWorldBoundries(float fPX, float fZX, float fPY, float fNY);

public:
	sa::CPhysical* m_entity;
	uint32_t m_gtaId;
};
