#pragma once

#include <cstdint>
#include <string>
#include <sys/types.h>
#include <SA.h>
#include <RenderWare/rw.h>

uint32_t GetTickCount();

// Returns true if the string contains non-ASCII bytes (>= 0x80), i.e. text
// that the game engine bitmap font cannot render and must go through ImGui.
bool StringHasNonAscii(const char* s);

// Converts GTA "~x~" style color codes to ImGui "{RRGGBB}" inline color codes.
// resetHex is used for ~s~ (reset to base color).
// If maxColorCodes >= 0, only the first maxColorCodes color tags (~r~/~g~/~h~/etc.) are applied; extra tags are stripped.
std::string GxtToImGuiColors(const std::string& in, const std::string& resetHex, int maxColorCodes = -1);

// Replaces "~k~~KEY~" sequences with readable key names.
std::string ReplaceKey(const std::string& input);

sa::CPed* GamePool_FindPlayerPed();
sa::CPed* GamePool_Ped_GetAt(int slot);
int GamePool_Ped_GetIndex(sa::CPed* pActor);
sa::CObject* GamePool_Object_GetAt(int iID);
uint32_t GamePool_Vehicle_GetIndex(sa::CVehicle* pGtaVehicle);
sa::CVehicle* GamePool_Vehicle_GetAt(int iID);

int GetVehicleSubtype(sa::CVehicle* pGtaVehicle);

sa::CBaseModelInfo* GetModelInfoByID(int iModelID);
uint16_t ModelInfoGetReferenceCount(int iModelID);
bool IsExistInfoForModel(int iModelID);
bool IsValidModel(int iModelID);
bool IsValidPedModel(uint modelID);
int ResolveEffectivePedSkin(int baseSkin, int customSkin);
uint EnsureValidPedModelPrototype(uint uiModel);
int GetModelRefCounts(int iModel);
RwObject* GetModelRWObject(uint modelID);

RwTexture* LoadTextureFromTxd(const char* txdname, const char* texturename);

float FloatOffset(float f1, float f2);
float DegToRad(float fDegress);
float fixAngle(float angle);
float subAngle(float a1, float a2);

void SetPlayerPedPtrRecord(uint8_t bytePlayer, sa::CPed* pPed);
uint8_t FindPlayerNumFromPedPtr(sa::CPed* dwPedPtr);

int GameGetWeaponModelIDFromWeaponID(int iWeaponID);

int GetFreeTextDrawTextureSlot();
void ResetTextDrawTextures();
void DestroyTextDrawTexture(int index);

void DrawTextureUV(RwTexture* texture, sa::CRect* rect, uint32_t dwColor, float* uv);

bool IsPointInRect(float x, float y, sa::CRect* rect);

uintptr_t ModelInfoCreateInstance(int iModel);

void RenderClumpOrAtomic(uintptr_t rwObject);

float GetModelColSphereRadius(int iModel);

void GetModelColSphereVecCenter(int iModel, sa::CVector* vec);

void DestroyAtomicOrClump(RwObject* rwObject);

void GamePrepareTrain(sa::CVehicle* pGtaVehicle);

void GameResetStats();

void ProjectMatrix(sa::CVector* vecOut, sa::CMatrix* mat, sa::CVector* vecPos);
void ProjectMatrix(RwV3d* vecOut, const RwMatrix* mat, const RwV3d* vecPos);
int RwMatrixRotate(RwMatrix* mat, int axis, float angle);
void RwMatrixScale(RwMatrix* mat, sa::CVector* vecScale);

void MatrixInvertOrthonormal(RwMatrix* dst, const RwMatrix* src);
RwMatrix* MatrixInvertGeneral(RwMatrix* dst, const RwMatrix* src);
RwMatrix* RwMatrixInvert(RwMatrix* dst, const RwMatrix* src);

void DefinedState();

void SetRenderWareCamera(RwCamera* camera);

void rpWorldAddCamera(RwCamera* camera);
void rpWorldAddLight(RpLight* rpLight);
void rpWorldRemoveLight(RpLight* rpLight);

int GetTaskTypeFromTask(sa::CTask** task);

bool IsEntityPlaceable(sa::CEntity* pEntity);

void CalcScreenCoors(const RwV3d* In, RwV3d* pResult, float pScaleX, float pScaleY, bool ClipFarPlane, bool ClipNearPlane);

#include "TouchWidgets.h"

void ProcessLineOfSight(sa::CVector* vecStart, sa::CVector* vecEnd, sa::CColPoint* colPoint, sa::CEntity** refEntityPtr,
		bool bCheckBuildings, bool bCheckVehicles, int bCheckPeds, bool bCheckObjects, bool bCheckDummies, bool bSeeThroughStuff, bool bIgnoreSomeObjectsForCamera, bool bShootThroughStuff);
