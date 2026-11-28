#include <GLES2/gl2.h>
#include "SA.h"
#include "Entity/Placeable.h"
#include "Core/Matrix.h"
#include "Core/MatrixLinkList.h"

namespace sa {
RwGlobals* RwEngineInstance;
RsGlobalType* RsGlobal;
CScene* Scene;

void Initialize()
{
	spdlog::info("Initializing SA...");

	RwEngineInstance = *eng::as<RwGlobals**>(eng::Sym::RwEngineInstance);
	RsGlobal = eng::as<RsGlobalType*>(eng::Sym::RsGlobal);
	Scene = eng::as<CScene*>(eng::Sym::Scene);

	CMatrixLinkList::Initialize();
	CPlaceable::Initialize();
}
}
