#pragma once
#include "Game/Engine/Symbols.h"
#include "../../Memory/include/Memory.h"

namespace sa {
class CCheat {
public:
	static void WeaponSkillsCheat()
	{
		eng::fn::CCheat_WeaponSkillsCheat();
	}

	static void VehicleSkillsCheat()
	{
		eng::fn::CCheat_VehicleSkillsCheat();
	}

	static void StaminaCheat()
	{
		eng::fn::CCheat_StaminaCheat();
	}
};
}
