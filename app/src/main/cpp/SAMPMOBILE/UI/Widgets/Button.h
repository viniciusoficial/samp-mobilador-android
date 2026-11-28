#pragma once

#include <functional>
#include "Label.h"

class Button : public Widget {
public:
	Button(const std::string& caption, float font_size = 0.0f);

	const std::string& caption() const { return m_label->text(); }
	void setCaption(const std::string& caption) { m_label->setText(caption); }

	std::function<void()> callback() { return m_callback; }
	void setCallback(const std::function<void()>& callback) { m_callback = callback; }

	/* held-key semantics: press fires on touch down inside, release fires
	 * whenever the touch sequence ends (pop inside or outside) */
	void setHoldCallback(const std::function<void()>& press, const std::function<void()>& release)
	{
		m_pressCallback = press;
		m_releaseCallback = release;
	}

	void setColor(const ImColor& color) { m_color = color; }
	void setColorFocused(const ImColor& color) { m_colorFocused = color; }
	void setCaptionColor(const ImColor& color) { m_label->setColor(color); }

	virtual void performLayout() override;

	virtual void draw(ImGuiRenderer* renderer) override;

	virtual void touchPushEvent() override;
	virtual void touchPopEvent() override;
	virtual void focuseEvent(bool focused) override;

private:
	Label* m_label;
	std::function<void()> m_callback;
	std::function<void()> m_pressCallback;
	std::function<void()> m_releaseCallback;

	ImColor m_color;
	ImColor m_colorFocused;
};
