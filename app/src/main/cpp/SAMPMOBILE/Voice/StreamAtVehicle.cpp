#include "../Client.h"
#include "../Game/Game.h"
#include "../Game/Vehicle.h"
#include "../Net/NetGame.h"
#include "../Net/VehiclePool.h"
#include <SA.h>

#include "StreamAtVehicle.h"

#include "StreamInfo.h"


StreamAtVehicle::StreamAtVehicle(const uint32_t color, std::string name,
		const float distance, const VEHICLEID vehicleId) noexcept
		: LocalStream(StreamType::LocalStreamAtVehicle, color, std::move(name), distance), vehicleId(vehicleId) { }

void StreamAtVehicle::Tick() noexcept
{
	this->LocalStream::Tick();

	if (!pNetGame) return;

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) return;

	CVehicle* pVehicle = pVehiclePool->GetAt(this->vehicleId);
	if (!pVehicle || !pVehicle->m_pVehicle || !pVehicle->VerifyInstance()) return;

	RwMatrix pVehicleMatrix = pVehicle->m_pVehicle->GetMatrix().ToRwMatrix();

	for (const auto& channel : this->GetChannels()) {
		if (channel->HasSpeaker()) {
			BASS_ChannelSet3DPosition(channel->GetHandle(),
					reinterpret_cast<BASS_3DVECTOR*>(&pVehicleMatrix.pos),
					nullptr, nullptr);
		}
	}
}

void StreamAtVehicle::OnChannelCreate(const Channel& channel) noexcept
{
	static const BASS_3DVECTOR kZeroVector{0, 0, 0};

	this->LocalStream::OnChannelCreate(channel);

	if (!pNetGame) return;

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) return;

	CVehicle* pVehicle = pVehiclePool->GetAt(this->vehicleId);
	if (!pVehicle || !pVehicle->m_pVehicle || !pVehicle->VerifyInstance()) return;

	RwMatrix pVehicleMatrix = pVehicle->m_pVehicle->GetMatrix().ToRwMatrix();

	BASS_ChannelSet3DPosition(channel.GetHandle(),
			reinterpret_cast<BASS_3DVECTOR*>(&pVehicleMatrix.pos),
			&kZeroVector, &kZeroVector);
}
