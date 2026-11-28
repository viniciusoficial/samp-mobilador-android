#pragma once

#include <atomic>
#include "../Widgets/Button.h"

struct RwTexture;

class VoiceButton : public Button {
public:
	VoiceButton();

	void draw(ImGuiRenderer* renderer) override;
	void touchPopEvent() override;
	void focuseEvent(bool focused) override;
	void touchPushEvent() override;

	void setRecording(bool recording);
	[[nodiscard]] bool recording() const { return m_recording.load(std::memory_order_acquire); }

private:
	std::atomic<bool> m_recording{false};
	RwTexture* m_texture_micro_on = nullptr;
	RwTexture* m_texture_micro_off = nullptr;
};
