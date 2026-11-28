// Compile-time dual-ABI layout and wire-protocol invariants.
// Compiled for both arm64-v8a and armeabi-v7a to catch struct drift or
// pointer-width regressions immediately at build time.

#include "Client.h"
#include "Game/Game.h"
#include "Net/NetGame.h"
#include "Net/LocalPlayer.h"
#include <RakNet/NetworkTypes.h>
#include <SA.h>

#include <cstddef>
#include <cstdint>

// 1. Pointer width must match VER_x32 build flag.
static_assert(sizeof(void*) == (VER_x32 ? 4 : 8), "Pointer width mismatches VER_x32");
static_assert(sizeof(uintptr_t) == sizeof(void*), "uintptr_t width mismatches void*");

// 2. RakNet wire types (prevents arm64 8-byte unsigned long timestamp shift).
static_assert(sizeof(RakNetTime) == 4, "RakNetTime must be 4 bytes on both ABIs");
static_assert(sizeof(MessageID) == 1, "MessageID must be 1 byte");
static_assert(sizeof(RPCIndex) == 1, "RPCIndex must be 1 byte");
static_assert(sizeof(PlayerIndex) == 2, "PlayerIndex must be 2 bytes");

// 3. SA-MP packed network sync structures (wire-protocol exact byte sizes).
static_assert(sizeof(PLAYER_SPAWN_INFO) == 46, "PLAYER_SPAWN_INFO wire size mismatch");
static_assert(sizeof(PLAYER_SPAWN_INFO_DL) == 50, "PLAYER_SPAWN_INFO_DL wire size mismatch");
static_assert(sizeof(ONFOOT_SYNC_DATA) == 68, "ONFOOT_SYNC_DATA wire size mismatch");
static_assert(sizeof(INCAR_SYNC_DATA) == 63, "INCAR_SYNC_DATA wire size mismatch");
static_assert(sizeof(PASSENGER_SYNC_DATA) == 24, "PASSENGER_SYNC_DATA wire size mismatch");
static_assert(sizeof(UNOCCUPIED_SYNC_DATA) == 67, "UNOCCUPIED_SYNC_DATA wire size mismatch");
static_assert(sizeof(TRAILER_SYNC_DATA) == 54, "TRAILER_SYNC_DATA wire size mismatch");
static_assert(sizeof(AIM_SYNC_DATA) == 31, "AIM_SYNC_DATA wire size mismatch");
static_assert(sizeof(BULLET_SYNC_DATA) == 40, "BULLET_SYNC_DATA wire size mismatch");
static_assert(sizeof(SPECTATOR_SYNC_DATA) == 18, "SPECTATOR_SYNC_DATA wire size mismatch");

// 4. Core GTA:SA Android v2.10 dual-ABI engine structs.
static_assert(sizeof(sa::CVector) == 12, "sa::CVector must be 12 bytes");
static_assert(sizeof(sa::CQuaternion) == 16, "sa::CQuaternion must be 16 bytes");
static_assert(sizeof(sa::CPlayerInfo) == (VER_x32 ? 0x194 : 0x1D8), "sa::CPlayerInfo ABI size mismatch");
static_assert(sizeof(sa::CObject) == (VER_x32 ? 0x184 : 0x1F8), "sa::CObject ABI size mismatch");
static_assert(offsetof(sa::CEntity, m_nFlags) == (VER_x32 ? 0x1C : 0x28), "sa::CEntity::m_nFlags ABI offset mismatch");
static_assert(offsetof(sa::CPhysical, m_nPhysicalFlags) == (VER_x32 ? 0x44 : 0x64), "sa::CPhysical::m_nPhysicalFlags ABI offset mismatch");
static_assert(offsetof(sa::CPed, m_fAimingRotation) == (VER_x32 ? 1376 : 1736), "sa::CPed::m_fAimingRotation ABI offset mismatch");
static_assert(offsetof(sa::CVehicle, pDriver) == (VER_x32 ? 0x464 : 0x5A0), "sa::CVehicle::pDriver ABI offset mismatch");
static_assert(offsetof(sa::CVehicle, m_apPassengers) == (VER_x32 ? 0x468 : 0x5A8), "sa::CVehicle::m_apPassengers ABI offset mismatch");
static_assert(offsetof(sa::CVehicle, m_nNumPassengers) == (VER_x32 ? 0x488 : 0x5E8), "sa::CVehicle::m_nNumPassengers ABI offset mismatch");
static_assert(offsetof(sa::CVehicle, m_nMaxPassengers) == (VER_x32 ? 0x48C : 0x5EC), "sa::CVehicle::m_nMaxPassengers ABI offset mismatch");
