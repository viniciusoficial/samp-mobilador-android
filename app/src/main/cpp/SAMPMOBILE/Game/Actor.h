#pragma once

#include "Entity.h"

namespace sa {
class CPed;
}

class CActor : public CEntity {
public:
	CActor(int iSkin, float fX, float fY, float fZ, float fAngle, int iCustomSkin = 0);
	virtual ~CActor();

	void SetHealth(float fHealth);
	void SetInvulnerable(bool bInvulnerable);
	void ApplyAnimation(const char* szAnimName, const char* szAnimLib, float fDelta, int bLoop, int bLockX, int bLockY, int bFreeze, int iTime);
	void ClearAnimation();
	void SetFacingAngle(float fAngle);
	bool RefreshCustomSkin();
	void RevertCustomSkinBeforeUnload();

public:
	sa::CPed* m_actor;
	bool m_invulnerable;
	int m_iServerSkin = -1;
	int m_iFallbackSkin = 0;
};