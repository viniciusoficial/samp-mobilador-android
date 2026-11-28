#pragma once

#include "Game/Engine/Symbols.h"
#include "../../Memory/include/Memory.h"

namespace sa {
class CTxdStore {
public:
	static void PushCurrentTxd()
	{
		eng::fn::CTxdStore_PushCurrentTxd();
	}

	static void PopCurrentTxd()
	{
		eng::fn::CTxdStore_PopCurrentTxd();
	}

	static int32_t FindTxdSlot(char const* pName)
	{
		return eng::fn::CTxdStore_FindTxdSlot( pName);
	}

	static void SetCurrentTxd(int index, const char* dbOverride = 0)
	{
		eng::fn::CTxdStore_SetCurrentTxd( index, dbOverride);
	}

	static int32_t AddTxdSlot(const char* pName, const char* dbName, bool keepCPU = false)
	{
		return eng::fn::CTxdStore_AddTxdSlot( pName, dbName, keepCPU);
	}

	static void AddRef(int index)
	{
		eng::fn::CTxdStore_AddRef( index);
	}

	static RwTexDictionary* GetTxdParent(RwTexDictionary* pTxd)
	{
		return eng::fn::CTxdStore_GetTxdParent<RwTexDictionary*>( pTxd);
	}
};
}
