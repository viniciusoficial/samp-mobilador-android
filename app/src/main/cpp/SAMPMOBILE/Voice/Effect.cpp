#include "Effect.h"
#include "Header.h"

Effect::Effect(const uint32_t type, const int priority,
		const void* const paramPtr, const uint32_t paramSize)
		: type(type), priority(priority), params((paramPtr && paramSize > 0) ? paramSize : 0)
{
	if (paramPtr && paramSize > 0) {
		std::memcpy(this->params.data(), paramPtr, paramSize);
	}
}

Effect::~Effect() noexcept
{
	for (const auto& fxHandle : this->fxHandles) {
		BASS_ChannelRemoveFX(fxHandle.first, fxHandle.second);
	}
}

static size_t expectedEffectParamSize(uint32_t type) noexcept
{
	switch (type) {
		case BASS_FX_DX8_CHORUS:      return sizeof(BASS_DX8_CHORUS);
		case BASS_FX_DX8_COMPRESSOR:  return sizeof(BASS_DX8_COMPRESSOR);
		case BASS_FX_DX8_DISTORTION:  return sizeof(BASS_DX8_DISTORTION);
		case BASS_FX_DX8_ECHO:        return sizeof(BASS_DX8_ECHO);
		case BASS_FX_DX8_FLANGER:     return sizeof(BASS_DX8_FLANGER);
		case BASS_FX_DX8_GARGLE:      return sizeof(BASS_DX8_GARGLE);
		case BASS_FX_DX8_I3DL2REVERB: return sizeof(BASS_DX8_I3DL2REVERB);
		case BASS_FX_DX8_PARAMEQ:     return sizeof(BASS_DX8_PARAMEQ);
		case BASS_FX_DX8_REVERB:      return sizeof(BASS_DX8_REVERB);
		default:                      return 0;
	}
}

void Effect::Apply(const Channel& channel)
{
	const size_t needSize = expectedEffectParamSize(this->type);
	if (needSize == 0 || this->params.size() < needSize) {
		LogVoice("[sv:err:effect:apply] : invalid effect type(%u) or paramSize(%zu < %zu)",
				this->type, this->params.size(), needSize);
		return;
	}
	if (const auto fxHandle = BASS_ChannelSetFX(channel.GetHandle(),
				this->type); fxHandle != NULL) {
		if (BASS_FXSetParameters(fxHandle, this->params.data()) == FALSE) {
			LogVoice("[sv:err:effect:apply] : failed "
					 "to set parameters (code:%d)", BASS_ErrorGetCode());
			BASS_ChannelRemoveFX(channel.GetHandle(), fxHandle);
		}
		else {
			this->fxHandles[channel.GetHandle()] = fxHandle;
		}
	}
	else {
		LogVoice("[sv:err:effect:apply] : failed to create "
				 "effect (code:%d)", BASS_ErrorGetCode());
	}
}
