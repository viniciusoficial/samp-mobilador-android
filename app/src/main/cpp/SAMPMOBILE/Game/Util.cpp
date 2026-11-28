#include "Util.h"
#include "Common.h"
#include "Game.h"
#include "Patches.h"
#include "PlayerPed.h"
#include "Artwork/ArtworkCore.h"
#include "Artwork/ArtworkManager.h"
#include "Engine/Symbols.h"
#include "../Addr.h"
#include "../Net/NetGame.h"
#include "../Client.h"
#include "../Settings.h"
#include "../Java/Java.h"
#include "../UI/UI.h"
#include "Enums/WidgetIDs.h"
#include <SA.h>
#include <spdlog/spdlog.h>
#include <unordered_map>


#define PI 3.14159265


sa::CPed* GamePool_FindPlayerPed()
{
	//spdlog::info("GamePool_FindPlayerPed");
	if (!WORLD_PLAYERS) return nullptr;
	return WORLD_PLAYERS->m_pPed;
}

sa::CPed* dwPlayerPedPtrs[PLAYER_PED_SLOTS];

void SetPlayerPedPtrRecord(uint8_t bytePlayer, sa::CPed* dwPedPtr)
{
	if (bytePlayer >= PLAYER_PED_SLOTS) return;
	dwPlayerPedPtrs[bytePlayer] = dwPedPtr;
}

uint8_t FindPlayerNumFromPedPtr(sa::CPed* dwPedPtr)
{
	if (!dwPedPtr) return 0;
	uint8_t x = 0;
	while (x != PLAYER_PED_SLOTS) {
		if (dwPlayerPedPtrs[x] == dwPedPtr) {
			return x;
		}
		x++;
	}

	return 0;
}

/**
 * @param txdName
 * @param textureName
 * @return
 */
RwTexture* LoadTextureFromTxd(const char* txdName, const char* textureName)
{
	spdlog::info("LoadTextureFromTxd: txdName: {}, textureName: {}", txdName ? txdName : "", textureName ? textureName : "");

	RwTexture* pTexture = nullptr;

	if (textureName && textureName[0]) {
		auto trySlot = [&](int iTxdSlot) -> RwTexture* {
			if (iTxdSlot == -1) return nullptr;
			sa::CTxdStore::PushCurrentTxd();
			sa::CTxdStore::SetCurrentTxd(iTxdSlot);

			sa::CSprite2d txdSprite;
			txdSprite.SetTexture(textureName);
			RwTexture* tex = txdSprite.m_pTexture;
			if (tex) {
				++tex->refCount;
			}

			sa::CTxdStore::PopCurrentTxd();
			return tex;
		};

		// Try the requested TXD dictionary first when a specific name is provided,
		// then fall back to the mobile TextureDatabaseRuntime wildcard ("*") slot.
		if (txdName && txdName[0] && strcmp(txdName, "*") != 0) {
			pTexture = trySlot(sa::CTxdStore::FindTxdSlot(txdName));
		}
		if (!pTexture) {
			pTexture = trySlot(sa::CTxdStore::FindTxdSlot("*"));
		}
	}

	if (!pTexture) {
		spdlog::warn("Texture \"{}\" (txd \"{}\") was not found!", textureName ? textureName : "", txdName ? txdName : "");
	}

	return pTexture;
}

uint32_t GetTickCount()
{
	struct timeval tv{};
	gettimeofday(&tv, nullptr);
	return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

bool StringHasNonAscii(const char* s)
{
	if (s == nullptr) return false;
	for (const unsigned char* p = (const unsigned char*) s; *p; ++p) {
		if (*p >= 0x80) return true;
	}
	return false;
}

std::string GxtToImGuiColors(const std::string& in, const std::string& resetHex, int maxColorCodes)
{
	static const struct { char code; uint8_t r, g, b; } colorTable[] = {
			{'r', 0xB4, 0x19, 0x1D}, // GTA HUD red
			{'g', 0x36, 0x68, 0x2C}, // GTA HUD green
			{'b', 0x32, 0x3C, 0x7F}, // GTA HUD blue
			{'y', 0xE2, 0xC0, 0x63}, // GTA HUD yellow
			{'p', 0xA8, 0x6E, 0xFC}, // GTA HUD purple
			{'q', 0xA8, 0x6E, 0xFC},
			{'o', 0xD9, 0x8B, 0x2B}, // orange
			{'w', 0xE1, 0xE1, 0xE1}, // GTA HUD white/light gray
			{'l', 0x10, 0x10, 0x10}, // black
			{'u', 0x9C, 0x9C, 0x9C},
			{'t', 0x9C, 0x9C, 0x9C},
	};

	auto parseHex = [](const std::string& hex, uint8_t& r, uint8_t& g, uint8_t& b) {
		r = 0xE1; g = 0xE1; b = 0xE1;
		const char* s = hex.c_str();
		if (hex.size() >= 8 && s[0] == '{' && s[7] == '}') {
			s += 1;
		}
		else if (hex.size() < 6) {
			return;
		}
		unsigned int v = 0;
		if (std::sscanf(s, "%6x", &v) == 1) {
			r = static_cast<uint8_t>((v >> 16) & 0xFF);
			g = static_cast<uint8_t>((v >> 8) & 0xFF);
			b = static_cast<uint8_t>(v & 0xFF);
		}
	};

	auto appendHex = [](std::string& dst, uint8_t r, uint8_t g, uint8_t b) {
		char buf[10];
		std::snprintf(buf, sizeof(buf), "{%02X%02X%02X}", r, g, b);
		dst += buf;
	};

	uint8_t curR = 0xE1, curG = 0xE1, curB = 0xE1;
	parseHex(resetHex, curR, curG, curB);
	int colorCodeCount = 0;

	std::string out;
	out.reserve(in.size() + 16);

	for (size_t i = 0; i < in.size(); i++) {
		char c = in[i];
		if (c == '~' && i + 2 < in.size() && in[i + 2] == '~') {
			char code = (char) tolower((unsigned char) in[i + 1]);
			i += 2;

			if (code == 'n') {
				out += '\n';
				continue;
			}
			if (code == '~') {
				out += '~';
				continue;
			}
			if (code == 's') {
				if (maxColorCodes < 0 || colorCodeCount < maxColorCodes) {
					++colorCodeCount;
					parseHex(resetHex, curR, curG, curB);
					out += resetHex;
				}
				continue;
			}
			if (code == 'h') {
				if (maxColorCodes < 0 || colorCodeCount < maxColorCodes) {
					++colorCodeCount;
					// GTA:SA ~h~ lightens the active color (stackable: ~r~~h~, ~b~~h~~h~, etc.)
					curR = static_cast<uint8_t>(std::min(255, static_cast<int>(curR) + (255 - static_cast<int>(curR)) / 2 + 24));
					curG = static_cast<uint8_t>(std::min(255, static_cast<int>(curG) + (255 - static_cast<int>(curG)) / 2 + 24));
					curB = static_cast<uint8_t>(std::min(255, static_cast<int>(curB) + (255 - static_cast<int>(curB)) / 2 + 24));
					appendHex(out, curR, curG, curB);
				}
				continue;
			}

			for (const auto& e : colorTable) {
				if (e.code == code) {
					if (maxColorCodes < 0 || colorCodeCount < maxColorCodes) {
						++colorCodeCount;
						curR = e.r;
						curG = e.g;
						curB = e.b;
						appendHex(out, curR, curG, curB);
					}
					break;
				}
			}
			// Unknown codes (~x~, ~+~, ~-~, ~k~ leftovers) are stripped.
			continue;
		}
		out += c;
	}

	return out;
}

std::string ReplaceKey(const std::string& input)
{
	static const std::unordered_map<std::string, std::string> keyMap = {
			{"~k~~PED_ANSWER_PHONE~", "TAB"},
			{"~k~~VEHICLE_FIREWEAPON_ALT~", "ALT GR / LCTRL / NUM0"},
			{"~k~~PED_DUCK~", "C"},
			{"~k~~VEHICLE_HORN~", "H / CAPSLOCK"},
			{"~k~~PED_FIREWEAPON~", "LCTRL / LMB"},
			{"~k~~VEHICLE_FIREWEAPON~", "LALT"},
			{"~k~~PED_SPRINT~", "SPC"},
			{"~k~~VEHICLE_ACCELERATE~", "W"},
			{"~k~~VEHICLE_ENTER_EXIT~", "ENTER"},
			{"~k~~PED_JUMPING~", "LSHIFT"},
			{"~k~~VEHICLE_BRAKE~", "S"},
			{"~k~~VEHICLE_LOOKRIGHT~", "E"},
			{"~k~~PED_LOCK_TARGET~", "RMB"},
			{"~k~~VEHICLE_HANDBRAKE~", "SPC"},
			{"~k~~VEHICLE_LOOKLEFT~", "Q"},
			{"~k~~PED_LOOKBEHIND~", "NUM1 / MMB"},
			{"~k~~VEHICLE_LOOKBEHIND~", "2"},
			{"~k~~TOGGLE_SUBMISSIONS~", "2"},
			{"~k~~SNEAK_ABOUT~", "LALT"},
			{"~k~~VEHICLE_TURRETUP~", "NUM8"},
			{"~k~~VEHICLE_TURRETDOWN~", "NUM2"},
			{"~k~~VEHICLE_TURRETLEFT~", "NUM4"},
			{"~k~~VEHICLE_TURRETRIGHT~", "NUM6"},
			{"~k~~CONVERSATION_YES~", "Y"},
			{"~k~~CONVERSATION_NO~", "N"},
			{"~k~~GROUP_CONTROL_BWD~", "H"},
			{"~k~~GROUP_CONTROL_FWD~", "G"},
			{"~k~~GO_FORWARD~", "UP"},
			{"~k~~VEHICLE_STEERUP~", "UP"},
			{"~k~~GO_BACK~", "DOWN"},
			{"~k~~VEHICLE_STEERDOWN~", "DOWN"},
			{"~k~~GO_LEFT~", "LEFT"},
			{"~k~~VEHICLE_STEERLEFT~", "LEFT"},
			{"~k~~GO_RIGHT~", "RIGHT"},
			{"~k~~VEHICLE_STEERRIGHT~", "RIGHT"}
	};

	std::string output = input;

	for (const auto& pair : keyMap) {
		std::string::size_type pos = 0;
		while ((pos = output.find(pair.first, pos)) != std::string::npos) {
			output.replace(pos, pair.first.length(), pair.second);
			pos += pair.second.length();
		}
	}

	return output;
}

uint32_t GamePool_Vehicle_GetIndex(sa::CVehicle* pVehicle)
{
	//spdlog::info("GamePool_Vehicle_GetIndex: 0x{:X}", reinterpret_cast<uintptr_t>(pVehicle));
	return eng::fn::GettPoolVehicleRef( pVehicle);
}

sa::CPed* GamePool_Ped_GetAt(int slot)
{
	//spdlog::info("GamePool_Ped_GetAt: {}", slot);
	return eng::fn::GetPoolPed<sa::CPed*>( slot);
}

sa::CObject* GamePool_Object_GetAt(int iID)
{
	//spdlog::info("GamePool_Object_GetAt: {}", iID);
	return eng::fn::GetPoolObj<sa::CObject*>( iID);
}

/**
 * @param task
 * @return
 */
int GetTaskTypeFromTask(sa::CTask** task)
{
	if (!task || !*task) {
		return 0;
	}

	const uintptr_t vtbl = *reinterpret_cast<uintptr_t*>(*task);
	const uintptr_t a = g_saSym->Abs<uintptr_t>(addr::start$CTaskSimpleUninterruptable);
	const uintptr_t b = g_saSym->Abs<uintptr_t>(addr::start$CTaskAllocatorKillThreatsDriveby);
	const uintptr_t lo = (a < b) ? a : b;
	const uintptr_t hi = (a < b) ? b : a;
	if (vtbl < lo || vtbl > hi) {
		return 0;
	}

	// CTask::GetTaskType() is vtable slot 5 (+20 arm32 / +40 arm64), verified via
	// CTaskManager::FindTaskByType; slot 3 is GetSubTask() which returns CTask*.
	return g_saSym->CallFunction<int>(g_saSym->GetVmtFunction<uintptr_t>(vtbl, 5), *task);
}

void GameResetStats()
{
	GameResetLocalPlayerSkills();
}

/**
 * @param f1
 * @param f2
 * @return
 */
float FloatOffset(float f1, float f2)
{
	if (f1 >= f2) {
		return f1 - f2;
	}
	else {
		return (f2 - f1);
	}
}

/**
 * @param fDegrees
 * @return
 */
float DegToRad(float fDegrees)
{
	if (fDegrees > 360.0f || fDegrees < 0.0f) {
		return 0.0f;
	}

	if (fDegrees > 180.0f) {
		return (float) (-(PI - (((fDegrees - 180.0f) * PI) / 180.0f)));
	}
	else {
		return (float) ((fDegrees * PI) / 180.0f);
	}
}

float fixAngle(float angle)
{
	if (angle > 180.0f) {
		angle -= 360.0f;
	}
	if (angle < -180.0f) {
		angle += 360.0f;
	}

	return angle;
}

float subAngle(float a1, float a2)
{
	return fixAngle(fixAngle(a2) - a1);
}

void DefinedState()
{
	eng::fn::DefinedState();
}

/**
 * @param pGtaVehicle
 * @return
 */
int GetVehicleSubtype(sa::CVehicle* pGtaVehicle)
{
	if (pGtaVehicle) {
		uintptr_t vtable = *(uintptr_t*) pGtaVehicle;

		if (vtable == g_saSym->Abs<uintptr_t>(addr::vptr$CAutomobile)) { // CAutomobile
			return VEHICLE_SUBTYPE_CAR;
		}
		else if (vtable == g_saSym->Abs<uintptr_t>(addr::vptr$CBike)) { // CBike
			return VEHICLE_SUBTYPE_BIKE;
		}
		else if (vtable == g_saSym->Abs<uintptr_t>(addr::vptr$CHeli)) { // CHeli
			return VEHICLE_SUBTYPE_HELI;
		}
		else if (vtable == g_saSym->Abs<uintptr_t>(addr::vptr$CBoat)) { // CBoat
			return VEHICLE_SUBTYPE_BOAT;
		}
		else if (vtable == g_saSym->Abs<uintptr_t>(addr::vptr$CPlane)) { // CPlane
			return VEHICLE_SUBTYPE_PLANE;
		}
		else if (vtable == g_saSym->Abs<uintptr_t>(addr::vptr$CBmx)) { // CBmx
			return VEHICLE_SUBTYPE_PUSHBIKE;
		}
		else if (vtable == g_saSym->Abs<uintptr_t>(addr::vptr$CTrain)) { // CTrain
			return VEHICLE_SUBTYPE_TRAIN;
		}
		else if (vtable == g_saSym->Abs<uintptr_t>(addr::vptr$CMonsterTruck)) { // CMonsterTruck
			return VEHICLE_SUBTYPE_MONSTERTRUCK;
		}
		else if (vtable == g_saSym->Abs<uintptr_t>(addr::vptr$CQuadBike)) { // CQuadBike
			return VEHICLE_SUBTYPE_QUADBIKE;
		}
		else if (vtable == g_saSym->Abs<uintptr_t>(addr::vptr$CTrailer)) { // CTrailer
			return VEHICLE_SUBTYPE_TRAILER;
		}
	}

	return 0;
}

void SetRenderWareCamera(RwCamera* camera)
{
	// CVisibilityPlugins::SetRenderWareCamera
	eng::fn::CVisibilityPlugins_SetRenderWareCamera( camera);
}

void rpWorldAddCamera(RwCamera* camera)
{
	RpWorld* rpWorld = sa::Scene->m_pRpWorld;
	if (rpWorld) {
		RpWorldAddCamera(rpWorld, camera);
	}
}

void rpWorldAddLight(RpLight* rpLight)
{
	RpWorld* rpWorld = sa::Scene->m_pRpWorld;
	if (rpWorld) {
		RpWorldAddLight(rpWorld, rpLight);
	}
}

void rpWorldRemoveLight(RpLight* rpLight)
{
	RpWorld* rpWorld = sa::Scene->m_pRpWorld;
	if (rpWorld) {
		RpWorldRemoveLight(rpWorld, rpLight);
	}
}

uint16_t ModelInfoGetReferenceCount(int iModelID)
{
	sa::CBaseModelInfo* pModel = GetModelInfoByID(iModelID);
	if (!pModel) {
		spdlog::error("ModelInfoGetReferenceCount: {} is not a valid model.", iModelID);
		return 0;
	}

	spdlog::info("ModelInfoGetReferenceCount: ({})", iModelID);
	return pModel->m_numRefs;
}

bool IsValidModel(int iModelID)
{
	sa::CBaseModelInfo* pModel = GetModelInfoByID(iModelID);
	if (pModel && *(uintptr_t*) pModel != 0xFFFFFFEC) {
		return true;
	}

	spdlog::error("IsValidModel: {} is not a valid model.", iModelID);
	return false;
}

bool IsValidPedModel(uint modelID)
{
	if (modelID < static_cast<uint>(artwork::kRelocatedModelTableSize)) {
		sa::CBaseModelInfo* modelInfo = GetModelInfoByID(static_cast<int>(modelID));
		if (!modelInfo) {
			spdlog::error("IsValidPedModel: {} is not a valid ped model.", modelID);
			return false;
		}

		if ((*(uintptr_t*) modelInfo) == g_saSym->Abs<uintptr_t>(addr::vptr$CPedModelInfo)) {
			return true;
		}
	}

	spdlog::error("IsValidPedModel: {} is not a valid ped model.", modelID);
	return false;
}

sa::CBaseModelInfo* GetModelInfoByID(int iModelID)
{
	if (iModelID < 0 || iModelID >= artwork::kRelocatedModelTableSize) {
		spdlog::error("Invalid model ID: {}", iModelID);
		return nullptr;
	}

	auto modelInfoPtrs = Patches::modelInfoPtrs();

	if (!modelInfoPtrs) {
		spdlog::error("Failed to retrieve model info pointers.");
		return nullptr;
	}

	if (!modelInfoPtrs[iModelID]) {
		spdlog::error("Model info for ID {} is null.", iModelID);
		return nullptr;
	}

	return modelInfoPtrs[iModelID];
}

int ResolveEffectivePedSkin(int baseSkin, int customSkin)
{
	if (artwork::isCustomSkinId(customSkin)) {
		const int32_t slot = artwork::ArtworkManager::instance().ensureCustomSkinLoaded(customSkin);
		if (slot >= 0 && IsValidPedModel(static_cast<uint>(slot))) {
			return slot;
		}
		return baseSkin;
	}
	if (artwork::isInternalCustomSlot(customSkin) && IsValidPedModel(static_cast<uint>(customSkin))) {
		return customSkin;
	}
	return baseSkin;
}

bool IsExistInfoForModel(int iModelID)
{
	spdlog::info("IsExistInfoForModel: {}", iModelID);
	return GetModelInfoByID(iModelID) != nullptr;
}

/**
 * @param uiModel
 * @return
 */
RwObject* GetModelRWObject(uint uiModel)
{
	sa::CBaseModelInfo* modelInfo = GetModelInfoByID(uiModel);
	if (!modelInfo) {
		spdlog::error("GetModelRWObject modelInfo is null.");
		return nullptr;
	}

	spdlog::info("GetModelRWObject: {}", uiModel);
	return modelInfo->m_pRwObject;
}

/**
 * @param iWeaponID
 * @return
 */
int GameGetWeaponModelIDFromWeaponID(int iWeaponID)
{
	switch (iWeaponID) {
		case WEAPON_BRASSKNUCKLE: return WEAPON_MODEL_BRASSKNUCKLE;
		case WEAPON_GOLFCLUB: return WEAPON_MODEL_GOLFCLUB;
		case WEAPON_NITESTICK: return WEAPON_MODEL_NITESTICK;
		case WEAPON_KNIFE: return WEAPON_MODEL_KNIFE;
		case WEAPON_BAT: return WEAPON_MODEL_BAT;
		case WEAPON_SHOVEL: return WEAPON_MODEL_SHOVEL;
		case WEAPON_POOLSTICK: return WEAPON_MODEL_POOLSTICK;
		case WEAPON_KATANA: return WEAPON_MODEL_KATANA;
		case WEAPON_CHAINSAW: return WEAPON_MODEL_CHAINSAW;
		case WEAPON_DILDO: return WEAPON_MODEL_DILDO;
		case WEAPON_DILDO2: return WEAPON_MODEL_DILDO2;
		case WEAPON_VIBRATOR: return WEAPON_MODEL_VIBRATOR;
		case WEAPON_VIBRATOR2: return WEAPON_MODEL_VIBRATOR2;
		case WEAPON_FLOWER: return WEAPON_MODEL_FLOWER;
		case WEAPON_CANE: return WEAPON_MODEL_CANE;
		case WEAPON_GRENADE: return WEAPON_MODEL_GRENADE;
		case WEAPON_TEARGAS: return WEAPON_MODEL_TEARGAS;
		case WEAPON_MOLOTOV: return WEAPON_MODEL_MOLOTOV;
		case WEAPON_COLT45: return WEAPON_MODEL_COLT45;
		case WEAPON_SILENCED: return WEAPON_MODEL_SILENCED;
		case WEAPON_DEAGLE: return WEAPON_MODEL_DEAGLE;
		case WEAPON_SHOTGUN: return WEAPON_MODEL_SHOTGUN;
		case WEAPON_SAWEDOFF: return WEAPON_MODEL_SAWEDOFF;
		case WEAPON_SHOTGSPA: return WEAPON_MODEL_SHOTGSPA;
		case WEAPON_UZI: return WEAPON_MODEL_UZI;
		case WEAPON_MP5: return WEAPON_MODEL_MP5;
		case WEAPON_AK47: return WEAPON_MODEL_AK47;
		case WEAPON_M4: return WEAPON_MODEL_M4;
		case WEAPON_TEC9: return WEAPON_MODEL_TEC9;
		case WEAPON_RIFLE: return WEAPON_MODEL_RIFLE;
		case WEAPON_SNIPER: return WEAPON_MODEL_SNIPER;
		case WEAPON_ROCKETLAUNCHER: return WEAPON_MODEL_ROCKETLAUNCHER;
		case WEAPON_HEATSEEKER: return WEAPON_MODEL_HEATSEEKER;
		case WEAPON_FLAMETHROWER: return WEAPON_MODEL_FLAMETHROWER;
		case WEAPON_MINIGUN: return WEAPON_MODEL_MINIGUN;
		case WEAPON_SATCHEL: return WEAPON_MODEL_SATCHEL;
		case WEAPON_BOMB: return WEAPON_MODEL_BOMB;
		case WEAPON_SPRAYCAN: return WEAPON_MODEL_SPRAYCAN;
		case WEAPON_FIREEXTINGUISHER: return WEAPON_MODEL_FIREEXTINGUISHER;
		case WEAPON_CAMERA: return WEAPON_MODEL_CAMERA;
		case WEAPON_NIGHTVISION: return WEAPON_MODEL_NIGHTVISION;
		case WEAPON_INFRARED: return WEAPON_MODEL_INFRARED;
		case WEAPON_PARACHUTE: return WEAPON_MODEL_PARACHUTE;
	}

	return -1;
}

int GetModelRefCounts(int iModel)
{
	sa::CBaseModelInfo* info = GetModelInfoByID(iModel);
	return info ? info->m_numRefs : 0;
}

/**
 * @param pActor
 * @return
 */
int GamePool_Ped_GetIndex(sa::CPed* pActor)
{
	//spdlog::info("GamePool_Ped_GetIndex: 0x{:X}", reinterpret_cast<uintptr_t>(pActor));
	return eng::fn::GettPoolPedRef( pActor);
}

/**
 * @param iID
 * @return
 */
sa::CVehicle* GamePool_Vehicle_GetAt(int iID)
{
	//spdlog::info("GamePool_Vehicle_GetAt: {}", iID);
	return eng::fn::GetPoolVehicle<sa::CVehicle*>( iID);
}

/**
 * @param iModel
 * @return
 */
float GetModelColSphereRadius(int iModel)
{
	sa::CBaseModelInfo* modelInfo = GetModelInfoByID(iModel);
	if (modelInfo) {
		sa::CColModel* colModel = modelInfo->m_pColModel;
		if (colModel != nullptr) {
			return colModel->m_boundSphere.m_fRadius;
		}
	}

	return 0.0f;
}

/**
 * @param iModel
 * @param vec
 */
void GetModelColSphereVecCenter(int iModel, sa::CVector* vec)
{
	sa::CBaseModelInfo* modelInfo = GetModelInfoByID(iModel);
	if (modelInfo) {
		sa::CColModel* colModel = modelInfo->m_pColModel;
		if (colModel != nullptr) {
			sa::CVector* v = &colModel->m_boundSphere.m_vecCenter;
			vec->x = v->x;
			vec->y = v->y;
			vec->z = v->z;
		}
	}
}

/**
 * @param rwObject
 */
void DestroyAtomicOrClump(RwObject* rwObject)
{
	if (rwObject) {
		int type = rwObject->type;

		if (type == 1) {
			void* parent = rwObject->parent;
			// RpAtomicDestroy
			eng::fn::RpAtomicDestroy( rwObject);

			if (parent) {
				// RwFrameDestroy
				eng::fn::RwFrameDestroy( parent);
			}
		}
		else if (type == 2) {
			// RpClumpDestroy
			eng::fn::RpClumpDestroy( rwObject);
		}
	}
}

void GamePrepareTrain(sa::CVehicle* pGtaVehicle)
{
	spdlog::info("GamePrepareTrain");

	sa::CPed* pDriver = pGtaVehicle->pDriver;

	if (pDriver) {
		if (pDriver->m_nPedType != 0 && pDriver->m_nPedType != 1) {
			pDriver->Destructor();
			pGtaVehicle->pDriver = nullptr;
		}
	}
}

void ProjectMatrix(sa::CVector* vecOut, sa::CMatrix* mat, sa::CVector* vecPos)
{
	vecOut->x = mat->m_up.x * vecPos->z + mat->m_forward.x * vecPos->y + mat->m_right.x * vecPos->x + mat->m_pos.x;
	vecOut->y = mat->m_up.y * vecPos->z + mat->m_forward.y * vecPos->y + mat->m_right.y * vecPos->x + mat->m_pos.y;
	vecOut->z = mat->m_up.z * vecPos->z + mat->m_forward.z * vecPos->y + mat->m_right.z * vecPos->x + mat->m_pos.z;
}

void ProjectMatrix(RwV3d* vecOut, const RwMatrix* mat, const RwV3d* vecPos)
{
	vecOut->x = mat->at.x * vecPos->z + mat->up.x * vecPos->y + mat->right.x * vecPos->x + mat->pos.x;
	vecOut->y = mat->at.y * vecPos->z + mat->up.y * vecPos->y + mat->right.y * vecPos->x + mat->pos.y;
	vecOut->z = mat->at.z * vecPos->z + mat->up.z * vecPos->y + mat->right.z * vecPos->x + mat->pos.z;
}

static sa::CVector g_axis[3] = {
		{1.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f},
		{0.0f, 0.0f, 1.0f}};

int RwMatrixRotate(RwMatrix* mat, int axis, float angle)
{
	return eng::fn::RwMatrixRotate( mat, &g_axis[axis], angle, rwCOMBINEPRECONCAT);
}

void RwMatrixScale(RwMatrix* mat, sa::CVector* vecScale)
{
	mat->right.x *= vecScale->x;
	mat->right.y *= vecScale->x;
	mat->right.z *= vecScale->x;

	mat->up.x *= vecScale->y;
	mat->up.y *= vecScale->y;
	mat->up.z *= vecScale->y;

	mat->at.x *= vecScale->z;
	mat->at.y *= vecScale->z;
	mat->at.z *= vecScale->z;

	mat->flags &= 0xFFFDFFFC;
}

enum Flags {
	IDENTITY = 0x20000
};

enum Type {
	TYPENORMAL = 1,
	TYPEORTHOGONAL = 2,
	TYPEORTHONORMAL = 3,
	TYPEMASK = 3
};

void MatrixInvertOrthonormal(RwMatrix* dst, const RwMatrix* src)
{
	dst->right.x = src->right.x;
	dst->right.y = src->up.x;
	dst->right.z = src->at.x;
	dst->up.x = src->right.y;
	dst->up.y = src->up.y;
	dst->up.z = src->at.y;
	dst->at.x = src->right.z;
	dst->at.y = src->up.z;
	dst->at.z = src->at.z;
	dst->pos.x = -(src->pos.x * src->right.x + src->pos.y * src->right.y + src->pos.z * src->right.z);
	dst->pos.y = -(src->pos.x * src->up.x + src->pos.y * src->up.y + src->pos.z * src->up.z);
	dst->pos.z = -(src->pos.x * src->at.x + src->pos.y * src->at.y + src->pos.z * src->at.z);
	dst->flags = TYPEORTHONORMAL;
}

RwMatrix* MatrixInvertGeneral(RwMatrix* dst, const RwMatrix* src)
{
	float det, invdet;
	// calculate a few cofactors
	dst->right.x = src->up.y * src->at.z - src->up.z * src->at.y;
	dst->right.y = src->at.y * src->right.z - src->at.z * src->right.y;
	dst->right.z = src->right.y * src->up.z - src->right.z * src->up.y;
	// get the determinant from that
	det = src->up.x * dst->right.y + src->at.x * dst->right.z + dst->right.x * src->right.x;
	invdet = 1.0;
	if (det != 0.0f) {
		invdet = 1.0f / det;
	}
	dst->right.x *= invdet;
	dst->right.y *= invdet;
	dst->right.z *= invdet;
	dst->up.x = invdet * (src->up.z * src->at.x - src->up.x * src->at.z);
	dst->up.y = invdet * (src->at.z * src->right.x - src->at.x * src->right.z);
	dst->up.z = invdet * (src->right.z * src->up.x - src->right.x * src->up.z);
	dst->at.x = invdet * (src->up.x * src->at.y - src->up.y * src->at.x);
	dst->at.y = invdet * (src->at.x * src->right.y - src->at.y * src->right.x);
	dst->at.z = invdet * (src->right.x * src->up.y - src->right.y * src->up.x);
	dst->pos.x = -(src->pos.x * dst->right.x + src->pos.y * dst->up.x + src->pos.z * dst->at.x);
	dst->pos.y = -(src->pos.x * dst->right.y + src->pos.y * dst->up.y + src->pos.z * dst->at.y);
	dst->pos.z = -(src->pos.x * dst->right.z + src->pos.y * dst->up.z + src->pos.z * dst->at.z);
	dst->flags &= ~IDENTITY;
	return dst;
}

RwMatrix* RwMatrixInvert(RwMatrix* dst, const RwMatrix* src)
{
	if (src->flags & IDENTITY) {
		*dst = *src;
	}
	else if ((src->flags & TYPEMASK) == TYPEORTHONORMAL) {
		MatrixInvertOrthonormal(dst, src);
	}
	else {
		return MatrixInvertGeneral(dst, src);
	}
	return dst;
}

bool IsEntityPlaceable(sa::CEntity* pEntity)
{
	// null means "not a real physical entity" — callers use
	// !IsEntityPlaceable to gate entity access, so answer accordingly
	if (!pEntity) return true;
	if (*(uintptr_t*) pEntity) {
		if (*(uintptr_t*) pEntity == g_saSym->Abs<uintptr_t>(addr::vptr$CPlaceable)) {
			return true;
		}
	}

	return false;
}

void CalcScreenCoors(const RwV3d* In, RwV3d* pResult, float pScaleX, float pScaleY, bool ClipFarPlane, bool ClipNearPlane)
{
	// CSprite::CalcScreenCoors(RwV3d const&,RwV3d*,float *,float *,bool,bool)
	eng::fn::CSprite_CalcScreenCoors( In, pResult, &pScaleX, &pScaleY, ClipFarPlane, ClipNearPlane);
}

/* =========== TexturedTextDraw ============= */

bool bTextDrawTextureSlotState[200];
RwTexture* TextDrawTexture[200];

void ResetTextDrawTextures()
{
	for (int i = 0; i < 200; i++) {
		if (TextDrawTexture[i]) {
			RwTextureDestroy(TextDrawTexture[i]);
			TextDrawTexture[i] = nullptr;
		}
		bTextDrawTextureSlotState[i] = false;
	}
}

int GetFreeTextDrawTextureSlot()
{
	for (int i = 0; i < 200; i++) {
		if (!bTextDrawTextureSlotState[i]) {
			bTextDrawTextureSlotState[i] = true;
			return i;
		}
	}

	return -1;
}

void DestroyTextDrawTexture(int index)
{
	if (index >= 0 && index < 200) {
		if (TextDrawTexture[index]) {
			RwTextureDestroy(TextDrawTexture[index]);
			TextDrawTexture[index] = nullptr;
		}
		bTextDrawTextureSlotState[index] = false;
	}
}

/**
 * @param texture
 * @param rect
 * @param dwColor
 * @param uv
 */
void DrawTextureUV(RwTexture* texture, sa::CRect* rect, uint32_t dwColor, float* uv)
{
	if (texture) {
		RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*) rwFILTERLINEAR);
		reinterpret_cast<sa::CSprite2d*>(&texture)->Draw(rect, dwColor, uv);
	}
}

/**
 * @param x
 * @param y
 * @param rect
 * @return
 */
bool IsPointInRect(float x, float y, sa::CRect* rect)
{
	if (x >= rect->left && x <= rect->right && y >= rect->bottom && y <= rect->top) {
		return true;
	}

	return false;
}

uintptr_t ModelInfoCreateInstance(int iModel)
{
	sa::CBaseModelInfo* modelInfo = GetModelInfoByID(iModel);
	if (modelInfo) {
		// CXXXModelInfo::CreateInstance(void)
		return Memory::callFunction<uintptr_t>(g_saSym->GetVmtFunction(modelInfo->vtable, 11), modelInfo);
	}

	return 0;
}

/**
 * @param rwObject
 */
void RenderClumpOrAtomic(uintptr_t rwObject)
{
	if (rwObject) {
		if (*(uint8_t*) rwObject == 1) {
			// Atomic
			spdlog::info("Render Atomic!");
			eng::fn::AtomicDefaultRenderCallBack( rwObject);
		}
		else if (*(uint8_t*) rwObject == 2) {
			spdlog::info("Render Clump!");
			// rpClumpRender
			eng::fn::RpClumpRender( rwObject);
		}
	}
}

void ProcessLineOfSight(sa::CVector* vecStart, sa::CVector* vecEnd, sa::CColPoint* colPoint, sa::CEntity** refEntityPtr,
		bool bCheckBuildings, bool bCheckVehicles, int bCheckPeds, bool bCheckObjects, bool bCheckDummies, bool bSeeThroughStuff, bool bIgnoreSomeObjectsForCamera, bool bShootThroughStuff)
{
	// CWorld::ProcessLineOfSight(CVector const&,CVector const&,CColPoint &,CEntity *&,bool,bool,bool,bool,bool,bool,bool,bool)
	eng::fn::CWorld_ProcessLineOfSight( vecStart, vecEnd, colPoint, refEntityPtr, bCheckBuildings, bCheckVehicles, bCheckPeds, bCheckObjects, bCheckDummies, bSeeThroughStuff, bIgnoreSomeObjectsForCamera, bShootThroughStuff);
}
