#include "RemotePlayer.h"
#include "NetGame.h"
#include "VehiclePool.h"
#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/Aimstuff.h"
#include "../Game/Animation.h"
#include "../Game/Common.h"
#include "../Game/Object.h"
#include "../Game/Pad.h"
#include "../Game/PlayerPed.h"
#include "../Game/RadarColors.h"
#include "../Game/Vehicle.h"
#include "../Game/Util.h"
#include "../UI/UI.h"
#include "../UI/SAMPWidgets/Chat.h"
#include <encoding.h>
#include <spdlog/spdlog.h>


CRemotePlayer::CRemotePlayer()
{
	m_VehicleID = 0;
	m_byteState = PLAYER_STATE_NONE;
	m_PlayerID = INVALID_PLAYER_ID;
	m_pPlayerPed = nullptr;
	m_byteUpdateFromNetwork = UPDATE_TYPE_NONE;
	m_byteTeam = NO_TEAM;
	m_bShowNameTag = true;
	m_bGlobalMarkerShown = false;
	m_dwMarker = 0;
	m_byteSpecialAction = SPECIAL_ACTION_NONE;
	m_bIsNPC = false;
	m_dwLastRecvTick = GetTickCount();
	m_dwLastHeadUpdate = 0;
	m_dwLastStoredSyncDataTime = 0;
	m_quat = sa::CQuaternion(0.0f, 0.0f, 0.0f, 1.0f);

	m_bWasAnimSettedFlag = false;
	m_dwPlayingAnim = 0;

	ResetAllSyncAttributes();
}

CRemotePlayer::~CRemotePlayer()
{
	if (m_dwMarker) {
		pGame->DisableMarker(m_dwMarker);
		m_dwMarker = 0;
	}

	// field_1E9 = 0;

	if (m_pPlayerPed) {
		pGame->RemovePlayer(m_pPlayerPed);
		m_pPlayerPed = nullptr;
	}
}

void CRemotePlayer::Process()
{
	sa::CVector vecMoveSpeed;
	RwMatrix matPlayer;
	RwMatrix matVehicle;

	if (IsActive() && m_pPlayerPed && m_pPlayerPed->m_ped) {
		// walk handling
		if (GetState() == PLAYER_STATE_ONFOOT && m_byteUpdateFromNetwork == UPDATE_TYPE_ONFOOT && !m_pPlayerPed->IsInVehicle()) {
			// If the user has sent X, Y or Z offsets we need to 'attach' him to the car using these offsets
			if (!IsSurfing() || !ProcessSurfing()) {
				// If the user hasn't sent these (or the surfed entity isn't streamed in), sync normal movement/speed
				UpdateOnFootPositionAndSpeed(&m_onFootSync.vecPos, &m_onFootSync.vecMoveSpeed);
				if (m_pPlayerPed->IsEnteringVehicle() == 0) {
					UpdateOnFootTargetPosition();
				}
			}
			else {
				UpdateOnFootPositionAndSpeed(&m_onFootSync.vecPos, &m_onFootSync.vecMoveSpeed);
			}

			// update weapons
			WeaponSyncCheck(m_onFootSync.byteCurrentWeapon & 0x3F);

			// sniper / rocket launcher scope state (aligned with PC CRemotePlayer::Process)
			if (m_onFootSync.wKeys & 128) {
				const uint8_t curWeapon = m_pPlayerPed->GetCurrentWeapon();
				if (curWeapon == sa::WEAPONTYPE_SNIPERRIFLE ||
						curWeapon == sa::WEAPONTYPE_RLAUNCHER ||
						curWeapon == sa::WEAPONTYPE_RLAUNCHER_HS ||
						curWeapon == sa::WEAPONTYPE_CAMERA) {
					m_pPlayerPed->m_ped->m_nPedState = sa::PED_SNIPER_MODE;
				}
			}
			else if (m_pPlayerPed->m_ped->m_nPedState == sa::PED_SNIPER_MODE) {
				m_pPlayerPed->m_ped->m_nPedState = sa::PED_IDLE;
			}
			m_pCurrentVehicle = nullptr;
			m_byteUpdateFromNetwork = UPDATE_TYPE_NONE;
		}
			// Driving Network Processing
		else if (GetState() == PLAYER_STATE_DRIVER && m_byteUpdateFromNetwork == UPDATE_TYPE_INCAR && m_pPlayerPed->IsInVehicle()) {
			if (!m_pCurrentVehicle || !m_pCurrentVehicle->m_pVehicle || !m_pCurrentVehicle->VerifyInstance()) {
				return;
			}

			m_inCarSync.quat.Normalize();
			m_inCarSync.quat.GetMatrix(&matVehicle);
			matVehicle.pos = m_inCarSync.vecPos;

			// train movement
			if (m_pCurrentVehicle->GetModelIndex() == 538 || m_pCurrentVehicle->GetModelIndex() == 537 || m_pCurrentVehicle->GetModelIndex() == 449) {
				UpdateTrainDriverMatrixAndSpeed(&matVehicle, &m_inCarSync.vecMoveSpeed, m_inCarSync.fTrainSpeed);
			}
			else {
				UpdateInCarMatrixAndSpeed(&matVehicle, &m_inCarSync.vecPos, &m_inCarSync.vecMoveSpeed);
				UpdateInCarTargetPosition();
			}

			// If the current vehicle is Hydra
			if (m_pCurrentVehicle->GetModelIndex() == HYDRA) {
				// Sync thruster status
				m_pCurrentVehicle->SetHydraThrusters(*(uint32_t*) m_inCarSync.sHydraThrustAngle);
			}

			if (m_pCurrentVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_BIKE || m_pCurrentVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_PUSHBIKE) {
				m_pCurrentVehicle->SetRemoteBikeLean(m_inCarSync.fTrainSpeed);
			}

			// set alarm
			m_pCurrentVehicle->SetSirenOn(m_inCarSync.byteSirenOn);

			m_pCurrentVehicle->SetRemoteLandingGearState(m_inCarSync.byteLandingGearState);

			m_pCurrentVehicle->SetHealth(m_inCarSync.fCarHealth);

			PLAYERID TrailerID = m_inCarSync.TrailerID;
			// invalid trailer
			if ((TrailerID == 0 || TrailerID >= MAX_VEHICLES) && m_pCurrentVehicle->GetTrailer()) {
				// split trailer
				m_pCurrentVehicle->DetachTrailer();
				m_pCurrentVehicle->SetTrailer(nullptr);
			}

			// Detect weapon sync on vehicle
			WeaponSyncCheck(m_inCarSync.byteCurrentWeapon & 0x3F);

			m_byteUpdateFromNetwork = UPDATE_TYPE_NONE;
		}
			// Passenger Network Processing
		else if (GetState() == PLAYER_STATE_PASSENGER && m_byteUpdateFromNetwork == UPDATE_TYPE_PASSENGER && m_pPlayerPed->IsInVehicle()) {
			if (!m_pCurrentVehicle || !m_pCurrentVehicle->m_pVehicle || !m_pCurrentVehicle->VerifyInstance()) return;

			// UPDATE CURRENT WEAPON
			WeaponSyncCheck(m_passengerSync.byteCurrentWeapon & 0x3F);

			if (m_passengerSync.byteSeatFlags & 0x40) {
				if (!m_pPlayerPed->IsInPassengerDriveByMode()) {
					if (m_pPlayerPed->IsAdded()) {
						if (m_pPlayerPed->StartPassengerDriveByMode()) {
							m_bPassengerDriveByMode = true;
						}
					}
				}
				else {
					m_bPassengerDriveByMode = true;
				}
			}
			else {
				if (m_pPlayerPed->IsInPassengerDriveByMode() || m_bPassengerDriveByMode) {
					m_pPlayerPed->StopPassengerDriveByMode();
					m_bPassengerDriveByMode = false;
				}
			}

			if (m_passengerSync.byteSeatFlags & 0x80) {
				if (!m_pPlayerPed->IsCuffed()) {
					m_pPlayerPed->ToggleCuffed(true);
				}
			}
			else if (m_pPlayerPed->IsCuffed()) {
				m_pPlayerPed->ToggleCuffed(false);
			}

			m_byteUpdateFromNetwork = UPDATE_TYPE_NONE;
		}
		// ------ PROCESSED FOR ALL FRAMES -----
		if (GetState() == PLAYER_STATE_ONFOOT && !m_pPlayerPed->IsInVehicle()) {
			m_bPassengerDriveByMode = false;

			bool bSurfAttached = false;
			if (IsSurfing()) {
				bSurfAttached = ProcessSurfing();
			}

			const bool bEnteringOrExitingVeh = (m_pPlayerPed->IsEnteringVehicle() != 0) || m_pPlayerPed->IsExitingVehicle();
			if (!bEnteringOrExitingVeh) {
				InterpolateAndRotate();
			}

			ProcessSpecialAction(m_onFootSync.byteSpecialAction);

			if (!bEnteringOrExitingVeh) {
				ProcessAnimation();
			}

			m_pPlayerPed->SetKeys(m_onFootSync.lrAnalog, m_onFootSync.udAnalog, m_onFootSync.wKeys);

			const bool bMoving = m_onFootSync.vecMoveSpeed.x != 0.0f
				|| m_onFootSync.vecMoveSpeed.y != 0.0f
				|| m_onFootSync.vecMoveSpeed.z != 0.0f;
			if (!bMoving && !bEnteringOrExitingVeh) {
				m_pPlayerPed->m_ped->SetVelocity(m_onFootSync.vecMoveSpeed);
			}
			// Match PC AFK thresholds: 1500ms moving, 3000ms stationary
			const uint32_t afkThreshold = bMoving ? 1500u : 3000u;
			if ((GetTickCount() - m_dwLastRecvTick) > afkThreshold) {
				m_bIsAFK = true;
			}
			//Lock player position during afk
			if (m_bIsAFK && ((GetTickCount() - m_dwLastRecvTick) > 3000)) {
				m_onFootSync.lrAnalog = 0;
				m_onFootSync.udAnalog = 0;

				vecMoveSpeed.x = 0.0f;
				vecMoveSpeed.y = 0.0f;
				vecMoveSpeed.z = 0.0f;
				m_pPlayerPed->m_ped->SetVelocity(vecMoveSpeed);
				matPlayer = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
				matPlayer.pos.x = m_onFootSync.vecPos.x;
				matPlayer.pos.y = m_onFootSync.vecPos.y;
				matPlayer.pos.z = m_onFootSync.vecPos.z;
				m_pPlayerPed->m_ped->SetMatrix(matPlayer);
			}
			if ((bSurfAttached && SurfingOnVehicle())
					|| m_onFootSync.byteSpecialAction == SPECIAL_ACTION_ENTER_VEHICLE) {
				m_pPlayerPed->SetGravityProcessing(false);
				m_pPlayerPed->SetCollisionChecking(false);
			}
			else {
				m_pPlayerPed->SetGravityProcessing(true);
				m_pPlayerPed->SetCollisionChecking(true);
			}
		}
		else if (GetState() == PLAYER_STATE_DRIVER && m_pPlayerPed->IsInVehicle()) {
			if (m_pCurrentVehicle && m_pCurrentVehicle->GetModelIndex() != 538 && m_pCurrentVehicle->GetModelIndex() != 537 && m_pCurrentVehicle->GetModelIndex() != 449) {
				UpdateVehicleRotation();
			}

			m_bPassengerDriveByMode = false;

			m_pPlayerPed->CheckVehicleParachute();

			if (m_pCurrentVehicle && m_pCurrentVehicle->IsRCVehicle()) {
				m_pPlayerPed->Remove();
			}

			if (m_bIsNPC) {
				if (m_inCarSync.vecMoveSpeed.x == 0.0f && m_inCarSync.vecMoveSpeed.y == 0.0f && m_inCarSync.vecMoveSpeed.z == 0.0f) {
					if (m_pCurrentVehicle && m_pCurrentVehicle->m_pVehicle) {
						m_pCurrentVehicle->m_pVehicle->SetVelocity(m_inCarSync.vecMoveSpeed);
					}
				}
			}

			m_pPlayerPed->SetKeys(m_inCarSync.lrAnalog, m_inCarSync.udAnalog, m_inCarSync.wKeys);
			m_pPlayerPed->ProcessVehicleHorn();

			if (!m_bIsNPC && m_pPlayerPed->IsAdded() && Settings::headMove()
					&& m_pPlayerPed->GetStuff() == STUFF_TYPE_NONE && (GetTickCount() - m_dwLastHeadUpdate) > 500) {
				CAMERA_AIM* Aim = GameGetRemotePlayerAim(m_pPlayerPed->m_bytePlayerNumber);
				if (Aim && m_pPlayerPed->m_ped) {
					RwMatrix matHead = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
					sa::CVector LookAt;
					LookAt.x = matHead.pos.x + (Aim->f1x * 20.0f);
					LookAt.y = matHead.pos.y + (Aim->f1y * 20.0f);
					LookAt.z = matHead.pos.z + (Aim->f1z * 20.0f);
					m_pPlayerPed->ApplyCommandTask("FollowPedSA", nullptr, 2000, -1, &LookAt, 0, 0.1f, 500, 3, 0);
				}
				m_dwLastHeadUpdate = GetTickCount();
			}

			if (m_inCarSync.vecMoveSpeed.x != 0.0f || m_inCarSync.vecMoveSpeed.y != 0.0f || m_inCarSync.vecMoveSpeed.z != 0.0f) {
				if (!m_bIsAFK) {
					if ((GetTickCount() - m_dwLastRecvTick) > 1500) {
						m_bIsAFK = true;
					}
				}
			}
			else if (!m_bIsAFK) {
				if ((GetTickCount() - m_dwLastRecvTick) > 3000) {
					m_bIsAFK = true;
				}
			}
		}
		else if (GetState() == PLAYER_STATE_PASSENGER && m_pPlayerPed->IsInVehicle()) {
			m_pPlayerPed->CheckVehicleParachute();
			m_pPlayerPed->SetKeys(m_passengerSync.lrAnalog, m_passengerSync.udAnalog, m_passengerSync.wKeys);

			if (m_pPlayerPed->IsCuffed()) {
				m_pPlayerPed->ProcessCuff();
			}

			if (!m_bIsNPC && !m_bPassengerDriveByMode && m_pPlayerPed->IsAdded() && Settings::headMove()
					&& m_pPlayerPed->GetStuff() == STUFF_TYPE_NONE) {
				if ((GetTickCount() - m_dwLastHeadUpdate) > 500) {
					sa::CVector LookAt;
					CAMERA_AIM* Aim = GameGetRemotePlayerAim(m_pPlayerPed->m_bytePlayerNumber);
					if (Aim && m_pPlayerPed->m_ped) {
						RwMatrix matHead = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
						LookAt.x = matHead.pos.x + (Aim->f1x * 20.0f);
						LookAt.y = matHead.pos.y + (Aim->f1y * 20.0f);
						LookAt.z = matHead.pos.z + (Aim->f1z * 20.0f);
						m_pPlayerPed->ApplyCommandTask("FollowPedSA", nullptr, 2000, -1, &LookAt, 0, 0.1f, 500, 3, 0);
					}
					m_dwLastHeadUpdate = GetTickCount();
				}
			}

			if ((GetTickCount() - m_dwLastRecvTick) > 3000) {
				m_bIsAFK = true;
			}
		}
		else {
			m_bPassengerDriveByMode = false;
			m_pPlayerPed->SetKeys(0, 0, 0);
			vecMoveSpeed.x = 0.0f;
			vecMoveSpeed.y = 0.0f;
			vecMoveSpeed.z = 0.0f;
			m_pPlayerPed->m_ped->SetVelocity(vecMoveSpeed);
		}

		if (GetState() != PLAYER_STATE_WASTED) {
			m_pPlayerPed->SetHealth(1000.0f);
		}

		if ((GetTickCount() - m_dwLastRecvTick) < 1500) {
			m_bIsAFK = false;
		}
	}
	else {
		if (m_pPlayerPed) {
			ResetAllSyncAttributes();
			pGame->RemovePlayer(m_pPlayerPed);
			m_pPlayerPed = nullptr;
		}
	}
}

void CRemotePlayer::ResetAllSyncAttributes()
{
	m_VehicleID = 0;
	m_fReportedHealth = 0.0f;
	m_fReportedArmour = 0.0f;
	m_pCurrentVehicle = nullptr;
	m_byteSeatID = 0;
	m_bPassengerDriveByMode = false;
	m_byteSpecialAction = SPECIAL_ACTION_NONE;
	m_byteUpdateFromNetwork = UPDATE_TYPE_NONE;
	m_dwPlayingAnim = 0;
	m_bWasAnimSettedFlag = false;
	m_bGlobalMarkerShown = false;
	m_quat = sa::CQuaternion(0.0f, 0.0f, 0.0f, 1.0f);
	m_bIsAFK = true;

	memset(&m_onFootSync, 0, sizeof(ONFOOT_SYNC_DATA));
	memset(&m_inCarSync, 0, sizeof(INCAR_SYNC_DATA));
	memset(&m_passengerSync, 0, sizeof(PASSENGER_SYNC_DATA));

	if (m_dwMarker) {
		pGame->DisableMarker(m_dwMarker);
		m_dwMarker = 0;
	}

	// RakNet ID_TIMESTAMP uses RakNet::GetTime() (process uptime ms), NOT wall-clock GetTickCount().
	// Initialize to 0 so the first timestamped packet always establishes the baseline.
	m_dwLastStoredSyncDataTime = 0;
	m_dwEnterVehicleTick = 0;
	m_bEnteredVehicleSpecialActionSeen = false;
}

void CRemotePlayer::SetPlayerColor(uint32_t dwColor)
{
	SetRadarColor(m_PlayerID, dwColor);
}

uint32_t CRemotePlayer::GetPlayerColor()
{
	return TranslateColorCodeToRGBA(m_PlayerID);
}

uint32_t CRemotePlayer::GetPlayerColorAsARGB()
{
	return (TranslateColorCodeToRGBA(m_PlayerID) >> 8) | 0xFF000000;
}

void CRemotePlayer::Say(char* szMessage)
{
	char* szPlayerName = pNetGame->GetPlayerPool()->GetPlayerName(m_PlayerID);
	if (pUI) {
		pUI->chat()->addChatMessage(Encoding::cp2utf(szMessage), Encoding::cp2utf(szPlayerName ? szPlayerName : ""), TranslateColorCodeToRGBA(m_PlayerID));
	}
}

bool CRemotePlayer::Spawn(uint8_t byteTeam, int iSkin, sa::CVector* vecPos, float fRotation, uint32_t dwColor, uint8_t byteFightingStyle, int iCustomSkin)
{
	if (m_pPlayerPed) {
		pGame->RemovePlayer(m_pPlayerPed);
		m_pPlayerPed = nullptr;
	}
	ResetAllSyncAttributes();

	const int effectiveSkin = ResolveEffectivePedSkin(iSkin, iCustomSkin);
	spdlog::info("CRemotePlayer::Spawn: {} {} {} (custom={} eff={}) {} {} {} {}",
		m_PlayerID, byteTeam, iSkin, iCustomSkin, effectiveSkin, vecPos->x, vecPos->y, vecPos->z, fRotation);
	CPlayerPed* pPlayerPed = pGame->NewPlayer(effectiveSkin, vecPos->x, vecPos->y, vecPos->z, fRotation, true, m_bIsNPC);
	if (pPlayerPed) {
		pPlayerPed->RememberServerSkin(iSkin, iCustomSkin);
		SetRadarColor(m_PlayerID, dwColor);

		if (m_dwMarker) {
			pGame->DisableMarker(m_dwMarker);
			m_dwMarker = 0;
		}
		if (pNetGame->m_netSet->showPlayerMarkers) {
			// Show near point markers (if the ped is loaded)
			pPlayerPed->ShowMarker(m_PlayerID);
		}

		m_byteTeam = byteTeam;
		m_pPlayerPed = pPlayerPed;
		m_fReportedHealth = pPlayerPed->GetHealth();
		pPlayerPed->SetKeys(0, 0, 0);
		if (byteFightingStyle != 4) {
			pPlayerPed->SetFightingStyle(byteFightingStyle);
		}

		SetState(PLAYER_STATE_SPAWNED);
		return true;
	}
	else {
		SetState(PLAYER_STATE_NONE);
		return false;
	}
}

void CRemotePlayer::Remove()
{
	if (m_pPlayerPed) {
		ResetAllSyncAttributes();
		pGame->RemovePlayer(m_pPlayerPed);
		m_pPlayerPed = nullptr;
	}

	SetState(PLAYER_STATE_NONE);
	m_bShowNameTag = true;
}

void CRemotePlayer::HandleDeath()
{
	if (m_pPlayerPed) {
		m_pPlayerPed->SetKeys(0, 0, 0);
		if (m_pPlayerPed->GetStuff() != STUFF_TYPE_NONE) m_pPlayerPed->DropStuff();
		if (m_pPlayerPed->IsDancing()) m_pPlayerPed->StopDancing();
		if (m_pPlayerPed->IsPissing()) m_pPlayerPed->TogglePissing(false);
		if (m_pPlayerPed->IsCarry()) m_pPlayerPed->ToggleCarry(false);
		if (m_pPlayerPed->IsCuffed()) m_pPlayerPed->ToggleCuffed(false);
		m_pPlayerPed->SetDead();
	}

	m_dwPlayingAnim = 0;
	m_bWasAnimSettedFlag = false;
	SetState(PLAYER_STATE_WASTED);
	ResetAllSyncAttributes();
}
//walk sync
void CRemotePlayer::RemoveFromVehicle()
{
	RwMatrix mat;

	if (m_pPlayerPed && m_pPlayerPed->m_ped) {
		if (m_pPlayerPed->IsInVehicle()) {
			mat = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
			m_pPlayerPed->RemoveFromVehicleAndPutAt(mat.pos.x, mat.pos.y, mat.pos.z);
			m_pCurrentVehicle = nullptr;
		}
	}
}

void CRemotePlayer::PutInCurrentVehicle()
{
	spdlog::info("PutInCurrentVehicle");

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();

	if (pVehiclePool && m_pPlayerPed) {
		CVehicle* pVehicle = pVehiclePool->GetAt(m_VehicleID);

		if (pVehicle) {
			uint8_t byteCurWeapon;
			if (m_byteSeatID != 0) {
				byteCurWeapon = m_passengerSync.byteCurrentWeapon;
			}
			else {
				byteCurWeapon = m_inCarSync.byteCurrentWeapon;
			}

			WeaponSyncCheck(byteCurWeapon & 0x3F);

			uint32_t dwGTAId = pVehiclePool->FindGtaIDFromID(m_VehicleID);
			m_pPlayerPed->PutDirectlyInVehicle(dwGTAId, m_byteSeatID);
		}
	}
}

float CRemotePlayer::GetDistanceFromLocalPlayer()
{
	if (!m_pPlayerPed) {
		return 10000.0f;
	}

	if (GetState() == PLAYER_STATE_DRIVER && m_pCurrentVehicle && m_pPlayerPed->IsInVehicle()) {
		return m_pCurrentVehicle->GetDistanceFromLocalPlayerPed();
	}
	else {
		return m_pPlayerPed->GetDistanceFromLocalPlayerPed();
	}
}
//Other players enter the vehicle
void CRemotePlayer::EnterVehicle(VEHICLEID VehicleID, bool bPassenger)
{
	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) return;
	CVehicle* pVehicle = pVehiclePool->GetAt(VehicleID);

	if (m_pPlayerPed && pVehicle && !m_pPlayerPed->IsInVehicle()) {
		uint32_t dwGTAId = pVehiclePool->FindGtaIDFromID(VehicleID);
		if (dwGTAId != INVALID_VEHICLE_ID) {
			m_dwEnterVehicleTick = GetTickCount();
			m_bEnteredVehicleSpecialActionSeen = false;
			m_pPlayerPed->SetKeys(0, 0, 0);
			m_pPlayerPed->EnterVehicle(dwGTAId, bPassenger);
		}
	}
}

void CRemotePlayer::ExitVehicle()
{
	m_dwEnterVehicleTick = 0;
	m_bEnteredVehicleSpecialActionSeen = false;
	if (m_pPlayerPed && (m_pPlayerPed->IsInVehicle() || m_pPlayerPed->IsEnteringVehicle() != 0)) {
		m_pPlayerPed->SetKeys(0, 0, 0);
		m_pPlayerPed->ExitCurrentVehicle();
	}
}
//Remote player global flags
void CRemotePlayer::ShowGlobalMarker(short sPosX, short sPosY, short sPosZ)
{
	m_bGlobalMarkerShown = true;

	if (m_dwMarker) {
		pGame->DisableMarker(m_dwMarker);
		m_dwMarker = 0;
	}
	// Remote markers must be disabled when the ped is loaded, using the markers in the PlayerPed to update in real time, there is a 1 second delay for this remote marker
	if (!m_pPlayerPed) {
		m_dwMarker = pGame->CreateRadarMarkerIcon(0, sPosX, sPosY, sPosZ, GetPlayerColor(), 0);
	}
}

void CRemotePlayer::HideGlobalMarker()
{
	if (m_dwMarker) {
		pGame->DisableMarker(m_dwMarker);
		m_dwMarker = 0;
	}

	m_bGlobalMarkerShown = false;
}

void CRemotePlayer::StateChange(uint8_t byteNewState, uint8_t byteOldState)
{
	if (byteNewState == PLAYER_STATE_DRIVER && byteOldState != PLAYER_STATE_DRIVER) {
		CPlayerPed* pPlayerPed = pGame->FindPlayerPed();

		if (pPlayerPed) {
			// if the player is the driver
			if (pPlayerPed->IsInVehicle() && !pPlayerPed->IsAPassenger()) {
				CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
				sa::CVehicle* pGtaVehicle = pPlayerPed->GetGtaVehicle();

				if (pVehiclePool && pVehiclePool->FindIDFromGtaPtr(pGtaVehicle) == m_VehicleID) {
					// Match PC: warp local ped out immediately when car-jacked
					RwMatrix mat = pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
					pPlayerPed->RemoveFromVehicleAndPutAt(mat.pos.x, mat.pos.y, mat.pos.z + 1.0f);
					if (pGame) {
						pGame->DisplayGameText("~r~Car Jacked", 2000, 5);
					}
				}
			}
		}
	}
}

void CRemotePlayer::ProcessSpecialAction(unsigned char byteSpecialAction)
{
	if (!m_pPlayerPed || !m_pPlayerPed->IsAdded()) {
		return;
	}

	if (GetState() != PLAYER_STATE_ONFOOT) {
		byteSpecialAction = SPECIAL_ACTION_NONE;
		m_onFootSync.byteSpecialAction = SPECIAL_ACTION_NONE;
	}

	m_pPlayerPed->SetSpecialAction(byteSpecialAction);

	if (!m_bIsNPC && GetState() == PLAYER_STATE_ONFOOT && m_pPlayerPed->IsAdded()
			&& Settings::headMove() && m_pPlayerPed->GetStuff() == STUFF_TYPE_NONE
			&& (m_onFootSync.wKeys & 0x84) == 0
			&& m_pPlayerPed->IsEnteringVehicle() == 0 && !m_pPlayerPed->IsExitingVehicle()) {
		//head turn sync
		if ((GetTickCount() - m_dwLastHeadUpdate) > 500) {
			CAMERA_AIM* Aim = GameGetRemotePlayerAim(m_pPlayerPed->m_bytePlayerNumber);
			if (Aim && m_pPlayerPed->m_ped) {
				RwMatrix matHead = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
				sa::CVector LookAt;
				LookAt.x = matHead.pos.x + (Aim->f1x * 20.0f);
				LookAt.y = matHead.pos.y + (Aim->f1y * 20.0f);
				LookAt.z = matHead.pos.z + (Aim->f1z * 20.0f);
				m_pPlayerPed->ApplyCommandTask("FollowPedSA", nullptr, 2000, -1, &LookAt, 0, 0.1f, 500, 3, 0);
			}
			m_dwLastHeadUpdate = GetTickCount();
		}
	}
}

void CRemotePlayer::ProcessAnimation()
{
	if (!m_pPlayerPed || !m_pPlayerPed->m_ped) {
		return;
	}

	const uint32_t dwAnim = m_onFootSync.dwAnimation;
	const bool bNonCustom = ((dwAnim >> 31) != 0) || ((dwAnim & 0xFFFF) == 0);

	if (!bNonCustom) {
		if (dwAnim != m_dwPlayingAnim) {
			m_dwPlayingAnim = dwAnim;
			m_pPlayerPed->ApplyPackedAnimation(dwAnim);
		}
		else {
			// Only re-apply if the animation is flagged loop (bit 0) or freeze (bit 3)
			const uint32_t flags = (dwAnim >> 24) & 0xFF;
			const uint16_t animIdx = static_cast<uint16_t>(dwAnim & 0xFFFF);
			if ((flags & 0x09u) != 0 && !m_pPlayerPed->IsPlayingAnim(animIdx)) {
				m_pPlayerPed->ApplyPackedAnimation(dwAnim);
			}
		}
		m_bWasAnimSettedFlag = true;
	}
	else if (m_bWasAnimSettedFlag) {
		m_dwPlayingAnim = 0;
		// reset animation
		m_pPlayerPed->PlayAnimByIdx(0, 4.0f);
		m_bWasAnimSettedFlag = false;
	}
}

bool CRemotePlayer::IsSurfing()
{
	if (SurfingOnObject() || SurfingOnVehicle()) {
		return true;
	}

	return false;
}

bool CRemotePlayer::SurfingOnVehicle()
{
	if (GetState() == PLAYER_STATE_ONFOOT) {
		if (m_onFootSync.wSurfInfo != 0 && m_onFootSync.wSurfInfo != INVALID_VEHICLE_ID && m_onFootSync.wSurfInfo < MAX_VEHICLES) {
			return true;
		}
	}
	return false;
}

bool CRemotePlayer::SurfingOnObject()
{
	if (GetState() == PLAYER_STATE_ONFOOT) {
		if (m_onFootSync.wSurfInfo != INVALID_VEHICLE_ID && m_onFootSync.wSurfInfo >= MAX_VEHICLES &&
				m_onFootSync.wSurfInfo < MAX_VEHICLES + MAX_OBJECTS) // its an object
		{
			return true;
		}
	}
	return false;
}

void CRemotePlayer::WeaponSyncCheck(uint8_t netCurrentWeapon)
{
	if (!m_pPlayerPed || !m_pPlayerPed->m_ped) {
		return;
	}

	//weapon sync
	if (m_pPlayerPed->GetCurrentWeapon() != netCurrentWeapon) {
		//Equipped with network-synced weapons
		m_pPlayerPed->SetArmedWeapon(netCurrentWeapon);
		//Second inspection
		if (m_pPlayerPed->GetCurrentWeapon() != netCurrentWeapon) {
			//If still doesn't exist, means there is no such weapon
			//give weapon, and then equip
			m_pPlayerPed->GiveWeapon(netCurrentWeapon, 9999);
			m_pPlayerPed->SetArmedWeapon(netCurrentWeapon);
		}
	}
}

bool CRemotePlayer::ProcessSurfing()
{
	if (!m_pPlayerPed || !m_pPlayerPed->m_ped || GetState() != PLAYER_STATE_ONFOOT
			|| m_onFootSync.wSurfInfo == 0 || m_onFootSync.wSurfInfo == INVALID_VEHICLE_ID) {
		return false;
	}

	const sa::CVector& off = m_onFootSync.vecSurfOffsets;
	if (!std::isfinite(off.x) || !std::isfinite(off.y) || !std::isfinite(off.z)
			|| std::fabs(off.x) > 100.0f || std::fabs(off.y) > 100.0f || std::fabs(off.z) > 100.0f) {
		return false;
	}

	CVehicle* pVehicleSurfing = nullptr;
	CObject* pObjectSurfing = nullptr;

	if (SurfingOnVehicle()) {
		CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
		if (pVehiclePool) {
			CVehicle* pVehicle = pVehiclePool->GetAt(m_onFootSync.wSurfInfo);
			if (pVehicle && pVehicle->VerifyInstance()) {
				pVehicleSurfing = pVehicle;
			}
		}
	}
	else if (SurfingOnObject()) {
		const uint16_t objId = static_cast<uint16_t>(m_onFootSync.wSurfInfo - MAX_VEHICLES);
		CObjectPool* pObjectPool = pNetGame->GetObjectPool();
		if (pObjectPool) {
			CObject* pObject = pObjectPool->GetAt(objId);
			if (pObject && pObject->m_entity && GamePool_Object_GetAt(pObject->m_gtaId) == pObject->m_entity) {
				pObjectSurfing = pObject;
			}
		}
	}

	if (pVehicleSurfing && pVehicleSurfing->m_pVehicle) {
		m_pPlayerPed->SetCollisionChecking(0);
		RwMatrix matEntity = pVehicleSurfing->m_pVehicle->GetMatrix().ToRwMatrix();
		RwMatrix matPlayer = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
		ProjectMatrix(&matPlayer.pos, &matEntity, &m_onFootSync.vecSurfOffsets);
		m_pPlayerPed->m_ped->SetMatrix(matPlayer);
		m_pPlayerPed->UpdateRwMatrixAndFrame();
		return true;
	}
	else if (pObjectSurfing && pObjectSurfing->m_entity) {
		if (!m_pPlayerPed->m_ped->m_bUsesCollision) {
			m_pPlayerPed->SetCollisionChecking(1);
		}
		RwMatrix matEntity = pObjectSurfing->m_entity->GetMatrix().ToRwMatrix();
		RwMatrix matPlayer = m_pPlayerPed->m_ped->GetMatrix().ToRwMatrix();
		ProjectMatrix(&matPlayer.pos, &matEntity, &m_onFootSync.vecSurfOffsets);
		m_pPlayerPed->m_ped->SetMatrix(matPlayer);
		m_pPlayerPed->UpdateRwMatrixAndFrame();
		return true;
	}
	return false;
}
