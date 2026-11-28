#pragma once

#include "../Widget.h"

class DebugLabels : public Widget {
public:
	DebugLabels() = default;
	virtual void draw(ImGuiRenderer* renderer) override;

	bool vehicleLabelsVisible() const { return m_showVehicleLabels; }
	void setVehicleLabelsVisible(bool show)
	{
		m_showVehicleLabels = show;
		setVisible(m_showVehicleLabels || m_showCameraTargets);
	}

	bool cameraTargetsVisible() const { return m_showCameraTargets; }
	void setCameraTargetsVisible(bool show)
	{
		m_showCameraTargets = show;
		setVisible(m_showVehicleLabels || m_showCameraTargets);
	}

private:
	bool m_showVehicleLabels = false;
	bool m_showCameraTargets = false;
};
