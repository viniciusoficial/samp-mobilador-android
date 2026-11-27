#pragma once

class ButtonPanel : public Layout
{
public:
    ButtonPanel();
    void performLayout() override;
    void performLayerVisibility();
    void draw(ImGuiRenderer* renderer) override;
    void touchEvent(const ImVec2& pos, TouchType type) override;
    CButton* m_bH;
    OButton* m_bOpen;
    Button* m_bTab;
    Button* m_bAlt;
    Button* m_bSV;

private:
    Button* m_bY;
    Button* m_bN;
};
