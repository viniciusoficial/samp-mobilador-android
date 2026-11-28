#pragma once

#include "Game/Engine/Symbols.h"
#include "Core/TDBArray.h"
#include "Enums/TextureDatabaseFormat.h"
#include "../../Memory/include/Memory.h"
#include "RenderWare/rwcore.h"

namespace sa {
class TextureDatabaseRuntime {
public:
	static void Register(TextureDatabaseRuntime* toRegister)
	{
		eng::fn::TextureDatabaseRuntime_Register( toRegister);
	}

	static void Unregister(TextureDatabaseRuntime* toUnregister)
	{
		eng::fn::TextureDatabaseRuntime_Unregister( toUnregister);
	}

	static RwTexture* GetTexture(const char* name)
	{
		return eng::fn::TextureDatabaseRuntime_GetTexture<RwTexture*>( name);
	}

	static TextureDatabaseRuntime* Load(const char* withName, bool fullyLoad, TextureDatabaseFormat forcedFormat)
	{
		return eng::fn::TextureDatabaseRuntime_Load<TextureDatabaseRuntime*>( withName, fullyLoad, forcedFormat);
	}

	static TextureDatabaseRuntime* GetDatabase(const char* withName)
	{
		return eng::fn::TextureDatabaseRuntime_GetDatabase<TextureDatabaseRuntime*>( withName);
	}

	static TDBArray<TextureDatabaseRuntime*>& registered() { return *g_saSym->GetSymbol<TDBArray<TextureDatabaseRuntime*>*>("_ZN22TextureDatabaseRuntime10registeredE"); }
};
}
