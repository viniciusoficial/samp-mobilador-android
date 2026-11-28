#pragma once

#include "../Widget.h"
#include "../Widgets/ProgressBar.h"

class Image;

class SplashScreen : public Widget {
public:
	SplashScreen();

	virtual void performLayout() override;
	virtual void draw(ImGuiRenderer* renderer) override;

	void setProgressBarValue(float value) const { m_progressBar->setValue(value); }

private:
	Image* m_logo;
	ProgressBar* m_progressBar;
};